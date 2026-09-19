"""
Applies fixes to the ESP-IDF framework package that this project needs and
upstream has not released yet.

The framework lives in ~/.platformio/packages/, OUTSIDE this repository, so a
hand-edit there is invisible to git, lost whenever the package is reinstalled
or updated, and absent for every other checkout. Applying the patch from a
pre-build script instead means the fix travels with the repo and re-applies
itself automatically.

Every patch is idempotent: it looks for its own marker and does nothing when
already present. Re-running a build, or building several environments, applies
each one exactly once.

Wired in from [env] in platformio.ini as an extra_script.
"""

import os
import re

Import("env")  # noqa: F821  (injected by SCons/PlatformIO)


def _read(path):
    with open(path, "r", encoding="utf-8", errors="surrogateescape") as fh:
        return fh.read()


def _write(path, text):
    with open(path, "w", encoding="utf-8", errors="surrogateescape") as fh:
        fh.write(text)


def patch_esp_http_client(idf_dir):
    """
    esp-idf PR #18359 (IDFGH-17389) — reset the response buffer in
    esp_http_client_prepare().

    Without it, `raw_data` is left wherever the PREVIOUS response stopped, so a
    new request on the same handle parses stale buffer contents as its own
    response. Symptoms seen here, all from one cause: read buffers handed back
    unwritten (0xBAAD5678 heap canaries, stray ".com"/"core" ASCII where a gzip
    trailer should be), a gzip filter lost on re-open so a 174848-byte D64 was
    measured as its 52223-byte compressed length, and a NULL-pointer memcpy
    inside esp_http_client_read().

    Drop this patch once the framework package includes the fix (check
    esp_http_client_prepare() for the cached-buffer cleanup).

    Upstream went a different way in the end: rather than clearing inside
    prepare(), ESP-IDF 5.5.5 / 6.1.3 expose esp_http_client_clear_response_buffer()
    for the CALLER to invoke before reusing a handle -- which is what the
    ESP_IDF_VERSION-guarded call in MeatHttpClient::openAndFetchHeaders() is
    waiting for. Until that version is the floor, this patch is what fixes it.
    """
    path = os.path.join(idf_dir, "components", "esp_http_client", "esp_http_client.c")
    if not os.path.isfile(path):
        print("patch_framework: esp_http_client.c not found, skipping")
        return

    marker = "MEATLOAF-PATCH esp-idf#18359"
    src = _read(path)
    if marker in src:
        return

    # esp_http_client_prepare() is file-static up to ESP-IDF 5.5.2 and public
    # from 5.5.3 (declared in esp_http_client.h), so the "static " is optional.
    # Anchored at line start so the non-static spelling cannot match INSIDE the
    # static one and place the insertion seven characters into the signature.
    decl_re = re.compile(
        r"^(?:static )?esp_err_t esp_http_client_prepare"
        r"\(esp_http_client_handle_t client\)\n\{",
        re.MULTILINE,
    )
    # Exact indentation matters: this is prepare()'s own reset at function
    # scope.  perform() has the same statement nested far deeper, and the
    # leading four spaces are what keep the replace below off it.
    reset_anchor = "    client->first_line_prepared = false;"

    decl_match = decl_re.search(src)
    if decl_match is None or reset_anchor not in src:
        print("patch_framework: esp_http_client.c does not match the expected "
              "shape - NOT patched. Check whether the fix is already upstream.")
        return

    # esp_http_client_cached_buf_cleanup() is defined further down the file.
    src = (
        src[:decl_match.start()]
        + "/* " + marker + " */\n"
        "static void esp_http_client_cached_buf_cleanup(esp_http_buffer_t *res_buffer);\n\n"
        + src[decl_match.start():]
    )

    src = src.replace(
        reset_anchor,
        reset_anchor + "\n"
        "    /* " + marker + ": ensure raw_data == orig_raw_data before a new\n"
        "     * request, so a response is never parsed out of the previous\n"
        "     * response's leftovers. */\n"
        "    esp_http_client_cached_buf_cleanup(client->response->buffer);\n",
        1,
    )

    _write(path, src)
    print("patch_framework: applied esp-idf#18359 to esp_http_client.c")


def patch_esp_http_client_raw_len(idf_dir):
    """
    Guard esp_http_client_read() against memcpy'ing from a NULL raw_data.

    The crash: Guru Meditation LoadProhibited, EXCVADDR 0x00000000, inside
    esp_http_client_read() at

        memcpy(buffer, res_buffer->raw_data, remain_len);

    entered because raw_len was non-zero while raw_data was NULL.

    DO NOT "fix" this by making the raw_len increment in http_on_body()
    conditional on the caching branch. That was tried and it breaks all body
    reads. raw_len carries TWO meanings:

      1. how many bytes are cached in raw_data from the fetch-header stage; and
      2. how many bytes http_parser_execute() just delivered into output_ptr --
         which the read loop consumes as
             ridx += res_buffer->raw_len;
             need_read -= res_buffer->raw_len;
             res_buffer->raw_len = 0;

    Making the increment conditional leaves (2) permanently zero, so ridx never
    advances and a read returns only whatever (1) had cached. Observed as: a
    215-byte first chunk arriving and the rest of the file never transferring.

    So the fix is only at the point of use: copy from the cache only when there
    IS a cache, and drop a stale counter rather than letting it inflate ridx.
    """
    marker = "MEATLOAF-PATCH raw_data-null-guard"
    path = os.path.join(idf_dir, "components", "esp_http_client", "esp_http_client.c")
    if not os.path.exists(path):
        print("patch_framework: esp_http_client.c not found, skipping raw_data guard")
        return

    src = _read(path)
    if marker in src:
        return

    anchor = "    int rlen = ESP_FAIL, ridx = 0;\n    if (res_buffer->raw_len) {\n"
    if anchor not in src:
        print("patch_framework: esp_http_client_read shape unexpected, skipping guard")
        return

    src = src.replace(
        anchor,
        "    int rlen = ESP_FAIL, ridx = 0;\n"
        + "    /* " + marker + ": raw_len non-zero with raw_data NULL is stale\n"
        + "     * bookkeeping; copying from it faults, and leaving it set would\n"
        + "     * inflate ridx further down.  Drop it and read from the socket. */\n"
        + "    if (res_buffer->raw_len && !res_buffer->raw_data) {\n"
        + "        res_buffer->raw_len = 0;\n"
        + "    }\n"
        + "    if (res_buffer->raw_len) {\n",
        1,
    )


    # ---- cleanup must reset the pointer even when it owns no allocation ----
    # esp_http_client_cached_buf_cleanup() only reset raw_data/raw_len when
    # orig_raw_data was set.  When a response leaves raw_data pointing at a
    # buffer it does NOT own (raw_len > 0, orig_raw_data NULL), prepare()'s
    # call to this cleanup did nothing, and the NEXT request's first
    # esp_http_client_read() served those stale bytes -- a FULL byte count of
    # the PREVIOUS response's content.
    #
    # Proven on hardware: reading the same D81 directory sector, at the same
    # offset, with the same bounded range and a full 32-byte read, returned
    # three different payloads across three loads (28 04 82 27 correct, then
    # BC 42 CE 3F, then 10 30 40 3C).  Same position + same range + full read
    # + different bytes can only be a stale buffer.
    old_cleanup = (
        "    /* Free cached data if any, that was received during fetch header stage */\n"
        "    if (res_buffer && res_buffer->orig_raw_data) {\n"
        "        free(res_buffer->orig_raw_data);\n"
        "        res_buffer->orig_raw_data = NULL;\n"
        "        res_buffer->raw_data = NULL;\n"
        "        res_buffer->raw_len = 0;\n"
        "    }\n"
    )
    new_cleanup = (
        "    /* Free cached data if any, that was received during fetch header stage */\n"
        "    /* " + marker + ": reset the pointer and count UNCONDITIONALLY.\n"
        "     * Gating all of it on orig_raw_data left raw_data pointing at a\n"
        "     * buffer this struct does not own, with raw_len still set, so the\n"
        "     * next request's first read served the previous response's bytes. */\n"
        "    if (res_buffer) {\n"
        "        if (res_buffer->orig_raw_data) {\n"
        "            free(res_buffer->orig_raw_data);\n"
        "            res_buffer->orig_raw_data = NULL;\n"
        "        }\n"
        "        res_buffer->raw_data = NULL;\n"
        "        res_buffer->raw_len = 0;\n"
        "    }\n"
    )
    if old_cleanup in src:
        src = src.replace(old_cleanup, new_cleanup, 1)
    else:
        print("patch_framework: cleanup shape unexpected, skipping cleanup reset")

    _write(path, src)
    print("patch_framework: applied raw_data-null-guard to esp_http_client.c")


idf = env.PioPlatform().get_package_dir("framework-espidf")
if idf:
    patch_esp_http_client(idf)
    patch_esp_http_client_raw_len(idf)

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
    """
    path = os.path.join(idf_dir, "components", "esp_http_client", "esp_http_client.c")
    if not os.path.isfile(path):
        print("patch_framework: esp_http_client.c not found, skipping")
        return

    marker = "MEATLOAF-PATCH esp-idf#18359"
    src = _read(path)
    if marker in src:
        return

    decl_anchor = "static esp_err_t esp_http_client_prepare(esp_http_client_handle_t client)\n{"
    reset_anchor = "    client->first_line_prepared = false;"

    if decl_anchor not in src or reset_anchor not in src:
        print("patch_framework: esp_http_client.c does not match the expected "
              "shape - NOT patched. Check whether the fix is already upstream.")
        return

    # esp_http_client_cached_buf_cleanup() is defined further down the file.
    src = src.replace(
        decl_anchor,
        "/* " + marker + " */\n"
        "static void esp_http_client_cached_buf_cleanup(esp_http_buffer_t *res_buffer);\n\n"
        + decl_anchor,
        1,
    )

    src = src.replace(
        reset_anchor,
        reset_anchor + "\n"
        "    /* " + marker + ": ensure raw_data == orig_raw_data before a new\n"
        "     * request, so a response is never parsed out of the previous\n"
        "     * response's leftovers. */\n"
        "    esp_http_client_cached_buf_cleanup(client->response->buffer);\n"
        "    client->response->buffer->raw_len = 0;",
        1,
    )

    _write(path, src)
    print("patch_framework: applied esp-idf#18359 to esp_http_client.c")


idf = env.PioPlatform().get_package_dir("framework-espidf")
if idf:
    patch_esp_http_client(idf)

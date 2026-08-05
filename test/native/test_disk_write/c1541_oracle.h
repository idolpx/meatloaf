#ifndef TEST_C1541_ORACLE
#define TEST_C1541_ORACLE

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

#include <cstdio>
#include <cstdlib>
#include <string>

// Thin wrapper over VICE's c1541 used as an independent oracle. Path comes
// from the C1541 env var so CI can point at its own install.
inline std::string c1541_bin()
{
    const char* env = getenv("C1541");
    return (env != nullptr && env[0] != '\0') ? std::string(env) : std::string("c1541");
}

// Quotes a token only if it needs it. FIX ROUND 1 finding: on this
// MinGW/w64devkit setup, _popen()'s underlying `cmd.exe /c <string>` fails
// to locate the program at all when the command line contains more than one
// quoted segment (e.g. a quoted binary name AND a quoted argument together)
// - it mis-parses the executable name across both quote pairs and reports
// "is not recognized" instead of running c1541. Verified empirically: the
// same command with the (space-free) binary name left unquoted runs
// correctly. Quoting only tokens that actually contain whitespace keeps
// every one of this suite's own calls (bare filenames, "c1541" with no env
// override) down to zero or one quoted segment, avoiding the bug.
inline std::string c1541_quote(const std::string& token)
{
    if (token.find(' ') == std::string::npos && token.find('\t') == std::string::npos)
        return token;
    return "\"" + token + "\"";
}

// Runs a c1541 command line and returns its combined output. `args` must
// already have any of its own tokens quoted via c1541_quote() where needed.
inline std::string c1541_run(const std::string& args, int* exit_code = nullptr)
{
    std::string cmd = c1541_quote(c1541_bin()) + " " + args + " 2>&1";
    std::string out;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr)
    {
        if (exit_code != nullptr) *exit_code = -1;
        return out;
    }
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe) != nullptr)
        out += buf;
    int rc = pclose(pipe);
    if (exit_code != nullptr) *exit_code = rc;
    return out;
}

inline bool c1541_available()
{
    int rc = 0;
    std::string out = c1541_run("-help", &rc);
    return out.find("Available commands") != std::string::npos;
}

// Reads a whole file into memory. Returns false (leaving out untouched) if
// it can't be opened - used both to detect a missing/unreadable image and to
// snapshot image bytes for the validate byte-diff below.
inline bool c1541_read_raw_file(const std::string& path, std::string& out)
{
    FILE* fp = fopen(path.c_str(), "rb");
    if (fp == nullptr) return false;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return false; }
    long size = ftell(fp);
    if (size < 0) { fclose(fp); return false; }
    fseek(fp, 0, SEEK_SET);
    out.resize((size_t)size);
    size_t n = (size > 0) ? fread(&out[0], 1, (size_t)size, fp) : 0;
    fclose(fp);
    return n == (size_t)size;
}

// c1541 validate rewrites the BAM to match the actual block chains, but on a
// mismatch it does this SILENTLY - no "error"/"wrong" text, exit code 0. The
// only reliable signal that an image was inconsistent is that validate
// CHANGED it, so this reads the image before and after and byte-diffs them.
// (Verified empirically: repeated -validate runs on an already-consistent
// image are byte-for-byte idempotent, so a diff here means a real
// correction happened, not incidental churn.)
// Attach-level failures (missing file, unrecognized image) are caught
// separately via the old text check, since in that case c1541 never gets to
// run validate at all and the image would trivially "match itself".
inline bool c1541_validate(const std::string& image)
{
    std::string before;
    if (!c1541_read_raw_file(image, before)) return false;

    int rc = 0;
    std::string out = c1541_run("-attach " + c1541_quote(image) + " -validate", &rc);
    // rc != 0 means the COMMAND itself failed to run (bad quoting, c1541 not
    // found, etc.) - not a report from c1541 about the image, which always
    // exits 0 even when it hits a real attach error. Caught separately from
    // the text check below since a failed invocation produces no "error"
    // text of its own (cmd.exe prints "is not recognized...").
    if (rc != 0) return false;
    if (out.find("error") != std::string::npos) return false;
    if (out.find("Error") != std::string::npos) return false;
    if (out.find("wrong") != std::string::npos) return false;

    std::string after;
    if (!c1541_read_raw_file(image, after)) return false;

    return before == after;
}

inline std::string c1541_dir(const std::string& image)
{
    return c1541_run("-attach " + c1541_quote(image) + " -dir");
}

// out_bytes, if given, always receives the actual size of whatever c1541
// wrote to out_path (0 if nothing was written) - even on failure - so a
// caller comparing content against an expected size can tell a truncated
// read (mid-chain error, partial file written, no error text) from a clean
// one instead of trusting "the output file exists" alone.
inline bool c1541_read(const std::string& image,
                       const std::string& cbm_name,
                       const std::string& out_path,
                       size_t* out_bytes = nullptr)
{
    remove(out_path.c_str());
    int rc = 0;
    std::string out = c1541_run("-attach " + c1541_quote(image) + " -read " +
                                 c1541_quote(cbm_name) + " " + c1541_quote(out_path), &rc);

    FILE* fp = fopen(out_path.c_str(), "rb");
    if (fp == nullptr)
    {
        if (out_bytes != nullptr) *out_bytes = 0;
        return false;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fclose(fp);
    if (out_bytes != nullptr) *out_bytes = (size > 0) ? (size_t)size : 0;

    // See c1541_validate() for why rc is checked: a failed command
    // invocation exits nonzero and prints no "error" text of its own.
    if (rc != 0) return false;
    if (out.find("error") != std::string::npos) return false;
    if (out.find("Error") != std::string::npos) return false;
    return true;
}

#endif

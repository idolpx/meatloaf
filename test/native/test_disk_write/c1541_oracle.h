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

// Runs a c1541 command line and returns its combined output.
inline std::string c1541_run(const std::string& args, int* exit_code = nullptr)
{
    std::string cmd = "\"" + c1541_bin() + "\" " + args + " 2>&1";
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

// c1541 validate rewrites the BAM to match the actual chains and reports what
// it changed. Any "error" or block-count correction means our image was wrong.
inline bool c1541_validate(const std::string& image)
{
    std::string out = c1541_run("-attach \"" + image + "\" -validate");
    if (out.find("error") != std::string::npos) return false;
    if (out.find("Error") != std::string::npos) return false;
    if (out.find("wrong") != std::string::npos) return false;
    return true;
}

inline std::string c1541_dir(const std::string& image)
{
    return c1541_run("-attach \"" + image + "\" -dir");
}

inline bool c1541_read(const std::string& image,
                       const std::string& cbm_name,
                       const std::string& out_path)
{
    remove(out_path.c_str());
    c1541_run("-attach \"" + image + "\" -read \"" + cbm_name + "\" \"" + out_path + "\"");
    FILE* fp = fopen(out_path.c_str(), "rb");
    if (fp == nullptr) return false;
    fclose(fp);
    return true;
}

#endif

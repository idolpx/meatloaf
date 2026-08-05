#ifndef TEST_FORMAT_FIXTURES
#define TEST_FORMAT_FIXTURES

#include <memory>
#include <vector>
#include "media/disk/d64.h"
#include "media/disk/d71.h"
#include "media/disk/d80.h"
#include "media/disk/d81.h"
#include "media/disk/d82.h"

struct FormatFixture
{
    const char* name;
    const char* ext;
    uint32_t size;
    std::shared_ptr<D64MStream> (*make)(std::shared_ptr<MStream>);
};

inline std::shared_ptr<D64MStream> make_d64(std::shared_ptr<MStream> s) { return std::make_shared<D64MStream>(s); }
inline std::shared_ptr<D64MStream> make_d71(std::shared_ptr<MStream> s) { return std::make_shared<D71MStream>(s); }
inline std::shared_ptr<D64MStream> make_d80(std::shared_ptr<MStream> s) { return std::make_shared<D80MStream>(s); }
inline std::shared_ptr<D64MStream> make_d81(std::shared_ptr<MStream> s) { return std::make_shared<D81MStream>(s); }
inline std::shared_ptr<D64MStream> make_d82(std::shared_ptr<MStream> s) { return std::make_shared<D82MStream>(s); }

// D40, D90, DNP and DHD are deliberately excluded — see the design spec.
inline const std::vector<FormatFixture>& all_formats()
{
    static const std::vector<FormatFixture> formats = {
        { "d64", "d64", 174848,  make_d64 },
        { "d71", "d71", 349696,  make_d71 },
        { "d80", "d80", 533248,  make_d80 },
        { "d81", "d81", 819200,  make_d81 },
        { "d82", "d82", 1066496, make_d82 },
    };
    return formats;
}

#endif

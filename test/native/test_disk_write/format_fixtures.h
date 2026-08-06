#ifndef TEST_FORMAT_FIXTURES
#define TEST_FORMAT_FIXTURES

#include <memory>
#include <vector>
#include "media/disk/d64.h"
#include "media/disk/d71.h"
#include "media/disk/d80.h"
#include "media/disk/d81.h"
#include "media/disk/d82.h"
#include "media/hd/dnp.h"

struct FormatFixture
{
    const char* name;
    const char* ext;
    uint32_t size;
    std::shared_ptr<D64MStream> (*make)(std::shared_ptr<MStream>);

    // VICE's c1541 only understands the classic CBM floppy images. A format
    // without an oracle is checked by our invariants alone, which is weaker -
    // c1541 has caught things the invariants accept (see the README) - so this
    // flag exists to make that gap explicit at every call site rather than
    // letting c1541 silently fail on an image it cannot even attach.
    bool has_c1541_oracle;
};

inline std::shared_ptr<D64MStream> make_d64(std::shared_ptr<MStream> s) { return std::make_shared<D64MStream>(s); }
inline std::shared_ptr<D64MStream> make_d71(std::shared_ptr<MStream> s) { return std::make_shared<D71MStream>(s); }
inline std::shared_ptr<D64MStream> make_d80(std::shared_ptr<MStream> s) { return std::make_shared<D80MStream>(s); }
inline std::shared_ptr<D64MStream> make_d81(std::shared_ptr<MStream> s) { return std::make_shared<D81MStream>(s); }
inline std::shared_ptr<D64MStream> make_d82(std::shared_ptr<MStream> s) { return std::make_shared<D82MStream>(s); }
inline std::shared_ptr<D64MStream> make_dnp(std::shared_ptr<MStream> s) { return std::make_shared<DNPMStream>(s); }

// D40, D90 and DHD are still excluded — see the design spec.
//
// DNP is the odd one here. It is a CMD *native* partition, not a floppy image:
// it has no canonical size (the constructor derives its track count from the
// container), its directory location is read out of the image rather than being
// a compile-time constant, and its BAM is a single bitmap-only record of 32
// bytes per track. c1541 cannot attach it at all.
inline const std::vector<FormatFixture>& all_formats()
{
    static const std::vector<FormatFixture> formats = {
        { "d64", "d64", 174848,     make_d64, true  },
        { "d71", "d71", 349696,     make_d71, true  },
        { "d80", "d80", 533248,     make_d80, true  },
        { "d81", "d81", 819200,     make_d81, true  },
        { "d82", "d82", 1066496,    make_d82, true  },
        { "dnp", "dnp", 65536,      make_dnp, false },
    };
    return formats;
}

#endif

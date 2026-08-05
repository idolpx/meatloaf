#ifndef TEST_IMAGE_INVARIANTS
#define TEST_IMAGE_INVARIANTS

#include <set>
#include <string>
#include <utility>
#include "media/disk/d64.h"

struct InvariantResult
{
    bool ok = true;
    std::string message;
};

// Walks an image and checks the structural invariants from the design spec.
// Reports which block violated which invariant, which is the main advantage
// over c1541's pass/fail.
struct ImageInvariantChecker
{
    using TS = std::pair<uint8_t, uint8_t>;

    D64MStream& img;
    InvariantResult result;
    std::set<TS> seen;   // every block claimed by some chain

    explicit ImageInvariantChecker(D64MStream& image) : img(image) {}

    void fail(const std::string& msg)
    {
        if (result.ok) { result.ok = false; result.message = msg; }
    }

    std::string ts(uint8_t t, uint8_t s)
    {
        return std::to_string((int)t) + "/" + std::to_string((int)s);
    }

    // Invariant 5: geometry bounds.
    bool inBounds(uint8_t t, uint8_t s)
    {
        uint8_t last = img.partitions[img.partition].block_allocation_map.back().end_track;
        if (t < 1 || t > last) return false;
        return s < img.getSectorCount(t);
    }

    // Walk a T/S chain, enforcing invariants 1, 3, 4 and 5 as it goes.
    void walkChain(uint8_t t, uint8_t s, const char* what)
    {
        while (t != 0)
        {
            if (!inBounds(t, s))
            {
                fail(std::string(what) + ": out-of-bounds block " + ts(t, s));
                return;
            }
            TS key(t, s);
            if (seen.count(key) != 0)
            {
                fail(std::string(what) + ": cross-linked block " + ts(t, s));
                return;
            }
            seen.insert(key);

            if (img.isBlockFree(t, s))
                fail(std::string(what) + ": block " + ts(t, s) + " is in a chain but marked free in BAM");

            if (!img.seekSector(t, s, 0))
            {
                fail(std::string(what) + ": could not read block " + ts(t, s));
                return;
            }
            uint8_t link[2] = { 0, 0 };
            if (img.readContainer(link, 2) != 2)
            {
                fail(std::string(what) + ": short read on block " + ts(t, s));
                return;
            }
            // Invariant 4: a chain ends with track 0 and a used-byte count.
            if (link[0] == 0 && link[1] < 1)
                fail(std::string(what) + ": block " + ts(t, s) + " terminates with a zero byte count");
            t = link[0];
            s = link[1];
        }
    }

    InvariantResult run()
    {
        uint8_t dt = img.partitions[img.partition].directory_track;
        uint8_t ds = img.partitions[img.partition].directory_sector;

        // The header and BAM blocks are allocated but are not part of any
        // file chain; claim them before walking so invariant 2 sees them.
        seen.insert(TS(img.partitions[img.partition].header_track,
                       img.partitions[img.partition].header_sector));
        for (auto& bam : img.partitions[img.partition].block_allocation_map)
            seen.insert(TS(bam.track, bam.sector));

        walkChain(dt, ds, "directory");

        // Invariant 7: every directory entry points at a valid start block,
        // and invariants 1/3/4/5 hold along each file's chain.
        uint16_t index = 0;
        while (img.seekEntry(++index))
        {
            if (img.entry.file_type == 0x00) continue;      // scratched slot
            uint8_t st = img.entry.start_track;
            uint8_t ss = img.entry.start_sector;
            if (st == 0) continue;                          // no data
            if (!inBounds(st, ss))
            {
                fail("directory entry " + std::to_string(index) +
                     " points at out-of-bounds block " + ts(st, ss));
                continue;
            }
            walkChain(st, ss, "file");
        }

        // Invariant 2: nothing is allocated in the BAM that no chain claims.
        uint8_t last = img.partitions[img.partition].block_allocation_map.back().end_track;
        for (uint8_t t = 1; t <= last; t++)
        {
            uint16_t spt = img.getSectorCount(t);
            for (uint16_t s = 0; s < spt; s++)
            {
                if (img.isBlockFree(t, (uint8_t)s)) continue;
                if (seen.count(TS(t, (uint8_t)s)) == 0)
                    fail("orphan: block " + ts(t, (uint8_t)s) +
                         " allocated in BAM but unreachable");
            }
        }

        // Invariant 6: blocksFree() agrees with the BAM bitmap.
        uint16_t counted = 0;
        for (uint8_t t = 1; t <= last; t++)
        {
            if (t == img.partitions[img.partition].directory_track) continue;
            uint16_t spt = img.getSectorCount(t);
            for (uint16_t s = 0; s < spt; s++)
                if (img.isBlockFree(t, (uint8_t)s)) counted++;
        }
        uint16_t reported = img.blocksFree();
        if (counted != reported)
            fail("blocksFree() reports " + std::to_string(reported) +
                 " but BAM has " + std::to_string(counted) + " free blocks");

        return result;
    }
};

inline InvariantResult check_invariants(D64MStream& image)
{
    ImageInvariantChecker c(image);
    return c.run();
}

#endif

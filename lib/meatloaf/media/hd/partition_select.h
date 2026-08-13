// One partition surface over both CMD (DHD/D1M/D2M/D4M) and IDE64 CFS (.hdd)
// images, so the drive's CP<n> and the console's `partition` command share a
// single implementation. The two formats have DIFFERENT valid ranges - CMD is
// 1..254, CFS is 0..15 - and they must not be merged into one bound; select()
// is the only place that knows either.
#ifndef MEATLOAF_MEDIA_PARTITION_SELECT
#define MEATLOAF_MEDIA_PARTITION_SELECT

#include <string>
#include <vector>
#include <stdint.h>

namespace hdpart {

enum class Kind { None, CMD, CFS };

struct Target {
    Kind kind = Kind::None;
    std::string container;
    explicit operator bool() const { return kind != Kind::None; }
};

struct View {
    uint8_t number;
    std::string type_label;
    std::string name;       // UTF-8
    bool selected;
    bool selectable;
};

// Which partitioned image, if any, the path is inside.
Target targetFor(const std::string& path);

// Fills 'out' with every table entry and 'disk_label' with the image label.
// False when the image has no readable partition table.
bool list(const Target& t, std::vector<View>& out, std::string& disk_label);

// A partition number for 'arg', which may be a number or a name/wildcard.
// -1 when nothing matches. Does not select.
int resolve(const Target& t, const std::string& arg);

// Range-checked per format, then delegated to the format's registry, which
// refuses a partition that is not mountable (CMD entry 0; a non-CFS slot).
bool select(const Target& t, int number);

} // namespace hdpart

#endif /* MEATLOAF_MEDIA_PARTITION_SELECT */

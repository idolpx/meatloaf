// Contract tests for MStream::seek(pos, mode) - the two-argument seek every
// stream inherits from lib/meatloaf/meatloaf.h.
//
// It is the funnel for every positioned read in the project, and it owns
// _position on behalf of implementations that do not set it themselves. That
// makes one thing non-negotiable: after it returns, _position must describe
// where the stream ACTUALLY is. It did not - it committed the target offset
// before delegating and never took it back when the delegate failed, so a
// refused seek left every later caller reading from a position the stream was
// not at.
//
// Found while tracing an archive over HTTP that was handed bytes from the
// middle of the file (see test_archive_extract).

#include <unity.h>

#include <cstdint>
#include <string>

#include "meatloaf.h"

// Minimal MStream whose one-argument seek() can be made to fail on demand,
// and which records the position it observed when it was entered.
class ProbeStream : public MStream
{
public:
    ProbeStream() : MStream("probe://test") { _size = 1000; }

    // Overriding the one-argument seek() hides the base's two-argument form
    // from anything holding a ProbeStream* (real call sites hold MStream*, so
    // they are unaffected). These tests target the two-argument form.
    using MStream::seek;

    bool seek_should_fail = false;
    uint32_t observed_position_on_entry = 0xFFFFFFFF;
    uint32_t observed_target = 0xFFFFFFFF;

    bool isOpen() override { return true; }
    void close() override {}
    bool open(std::ios_base::openmode) override { return true; }
    uint32_t read(uint8_t*, uint32_t) override { return 0; }
    uint32_t write(const uint8_t*, uint32_t) override { return 0; }

    bool seek(uint32_t pos) override
    {
        observed_position_on_entry = _position;
        observed_target = pos;
        if (seek_should_fail)
            return false;
        _position = pos;
        return true;
    }

    // Test-only helper: place the stream without going through seek().
    void place_at(uint32_t pos) { _position = pos; }
};

void setUp(void) {}
void tearDown(void) {}

// A refused seek must leave the stream reporting where it still is.
void test_failed_seek_leaves_position_unchanged(void)
{
    ProbeStream s;
    s.place_at(524);
    s.seek_should_fail = true;

    TEST_ASSERT_FALSE(s.seek(100, SEEK_SET));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(524, s.position(),
        "a refused seek must not move the reported position");
}

// The same for the relative and end-relative forms.
void test_failed_relative_seek_leaves_position_unchanged(void)
{
    ProbeStream s;
    s.place_at(524);
    s.seek_should_fail = true;

    TEST_ASSERT_FALSE(s.seek(10, SEEK_CUR));
    TEST_ASSERT_EQUAL_UINT32(524, s.position());

    TEST_ASSERT_FALSE(s.seek(10, SEEK_END));
    TEST_ASSERT_EQUAL_UINT32(524, s.position());
}

// A successful seek still lands where it was asked to.
void test_successful_seek_commits_position(void)
{
    ProbeStream s;
    s.place_at(524);

    TEST_ASSERT_TRUE(s.seek(100, SEEK_SET));
    TEST_ASSERT_EQUAL_UINT32(100, s.position());

    TEST_ASSERT_TRUE(s.seek(50, SEEK_CUR));
    TEST_ASSERT_EQUAL_UINT32(150, s.position());
}

// The offsets each mode resolves to, pinned so the arithmetic cannot drift.
void test_seek_modes_resolve_expected_targets(void)
{
    ProbeStream s;

    s.place_at(200);
    s.seek(100, SEEK_SET);
    TEST_ASSERT_EQUAL_UINT32(100, s.observed_target);

    s.place_at(200);
    s.seek(100, SEEK_CUR);
    TEST_ASSERT_EQUAL_UINT32(300, s.observed_target);

    s.place_at(200);
    s.seek(100, SEEK_END);   // _size is 1000
    TEST_ASSERT_EQUAL_UINT32(900, s.observed_target);
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_failed_seek_leaves_position_unchanged);
    RUN_TEST(test_failed_relative_seek_leaves_position_unchanged);
    RUN_TEST(test_successful_seek_commits_position);
    RUN_TEST(test_seek_modes_resolve_expected_targets);
    return UNITY_END();
}

#include <unity.h>
#include <cstring>

#include "../../../lib/utils/time_converter.cpp"

void setUp(void) {}
void tearDown(void) {}

static struct tm sample()
{
    struct tm t = {};
    t.tm_year = 108; t.tm_mon = 0; t.tm_mday = 20;   // 2008-01-20
    t.tm_hour = 13; t.tm_min = 23; t.tm_sec = 45; t.tm_wday = 0;
    return t;
}

static void assert_same(const struct tm& a, const struct tm& b)
{
    TEST_ASSERT_EQUAL(a.tm_year, b.tm_year);
    TEST_ASSERT_EQUAL(a.tm_mon,  b.tm_mon);
    TEST_ASSERT_EQUAL(a.tm_mday, b.tm_mday);
    TEST_ASSERT_EQUAL(a.tm_hour, b.tm_hour);
    TEST_ASSERT_EQUAL(a.tm_min,  b.tm_min);
    TEST_ASSERT_EQUAL(a.tm_sec,  b.tm_sec);
}

void test_ascii_round_trip(void)
{
    struct tm t = sample(), r = {};
    uint8_t buf[64];
    size_t n = TimeConverter::toAsciiTime(&t, buf, sizeof(buf));
    TEST_ASSERT_TRUE(TimeConverter::fromAsciiTime(buf, n, &r));
    assert_same(t, r);
}

void test_ascii_literal(void)
{
    const char *s = "THUR 11/27/26 12:05:09 AM";
    struct tm r = {};
    TEST_ASSERT_TRUE(TimeConverter::fromAsciiTime((const uint8_t*)s, strlen(s), &r));
    TEST_ASSERT_EQUAL(126, r.tm_year); TEST_ASSERT_EQUAL(10, r.tm_mon); TEST_ASSERT_EQUAL(27, r.tm_mday);
    TEST_ASSERT_EQUAL(0, r.tm_hour);   // 12 AM is midnight
    TEST_ASSERT_EQUAL(5, r.tm_min); TEST_ASSERT_EQUAL(9, r.tm_sec);
}

void test_iso_round_trip(void)
{
    struct tm t = sample(), r = {};
    uint8_t buf[64];
    size_t n = TimeConverter::toIsoTime(&t, buf, sizeof(buf));
    TEST_ASSERT_TRUE(TimeConverter::fromIsoTime(buf, n, &r));
    assert_same(t, r);
}

void test_decimal_round_trip(void)
{
    struct tm t = sample(), r = {};
    uint8_t buf[16];
    size_t n = TimeConverter::toDecimalTime(&t, buf, sizeof(buf));
    TEST_ASSERT_TRUE(TimeConverter::fromDecimalTime(buf, n - 1, &r));
    assert_same(t, r);
}

void test_bcd_round_trip(void)
{
    struct tm t = sample(), r = {};
    uint8_t buf[16];
    size_t n = TimeConverter::toBcdTime(&t, buf, sizeof(buf));
    TEST_ASSERT_TRUE(TimeConverter::fromBcdTime(buf, n - 1, &r));
    assert_same(t, r);
}

void test_noon_and_midnight(void)
{
    // 12 PM is noon, 12 AM midnight, in both binary forms
    uint8_t noon[8] = { 0, 126, 6, 1, 12, 0, 0, 1 };
    uint8_t mid[8]  = { 0, 126, 6, 1, 12, 0, 0, 0 };
    struct tm r = {};
    TEST_ASSERT_TRUE(TimeConverter::fromDecimalTime(noon, 8, &r)); TEST_ASSERT_EQUAL(12, r.tm_hour);
    TEST_ASSERT_TRUE(TimeConverter::fromDecimalTime(mid, 8, &r));  TEST_ASSERT_EQUAL(0, r.tm_hour);
}

void test_rejects_bad(void)
{
    struct tm r = {};
    uint8_t shortbuf[4] = { 0, 126, 6, 1 };
    TEST_ASSERT_FALSE(TimeConverter::fromDecimalTime(shortbuf, 4, &r));
    uint8_t badmon[8] = { 0, 126, 13, 1, 1, 0, 0, 0 };
    TEST_ASSERT_FALSE(TimeConverter::fromDecimalTime(badmon, 8, &r));
    uint8_t badbcd[8] = { 0, 0x26, 0x1A, 0x01, 0x01, 0, 0, 0 };
    TEST_ASSERT_FALSE(TimeConverter::fromBcdTime(badbcd, 8, &r));
    const char *s = "garbage";
    TEST_ASSERT_FALSE(TimeConverter::fromIsoTime((const uint8_t*)s, strlen(s), &r));
    TEST_ASSERT_FALSE(TimeConverter::fromAsciiTime((const uint8_t*)s, strlen(s), &r));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_ascii_round_trip); RUN_TEST(test_ascii_literal); RUN_TEST(test_iso_round_trip);
    RUN_TEST(test_decimal_round_trip); RUN_TEST(test_bcd_round_trip); RUN_TEST(test_noon_and_midnight);
    RUN_TEST(test_rejects_bad);
    return UNITY_END();
}

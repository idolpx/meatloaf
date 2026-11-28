// IECFastLoadDetect: arming a loader that is resident in the drive's DOS
// rather than uploaded (CMD HD/FD GEOS turbo, see CmdHdDos::cmdGeos()).
#define EXTRA_FASTLOADERS
#include "../../../lib/bus/iec/fastload.cpp"
#include <unity.h>

static IECFastLoadDetect d;
void setUp()    { d.reset(); }
void tearDown() {}

static uint8_t exec040f(bool *matched) { uint8_t p = 0; return d.memExec(0x040f, &p, matched); }

void test_unarmed_exec_matches_nothing() {
    bool m = true;
    TEST_ASSERT_EQUAL_UINT8(IEC_FLV_NONE, exec040f(&m));
    TEST_ASSERT_FALSE(m);
}

void test_armed_exec_starts_geos_1581() {
    d.setResident(IEC_FLV_GEOS_S23_1581, IEC_FLRX_GEOS_2MHZ);
    // read before memExec, as IECFileDevice::isFastLoaderRequest() does
    TEST_ASSERT_EQUAL_UINT8(IEC_FLRX_GEOS_2MHZ, d.rxtx());
    bool m = false;
    TEST_ASSERT_EQUAL_UINT8(IEC_FLV_GEOS_S23_1581, exec040f(&m));
    TEST_ASSERT_TRUE(m);
}

// gateWay sends GEOS once, then a bare M-E $040F on every later turbo entry.
void test_rearm_survives_repeated_exec() {
    d.setResident(IEC_FLV_GEOS_S23_1581, IEC_FLRX_GEOS_2MHZ);
    bool m = false;
    exec040f(&m);
    for (int i = 0; i < 3; i++) {
        m = false;
        TEST_ASSERT_EQUAL_UINT8(IEC_FLRX_GEOS_2MHZ, d.rxtx());
        TEST_ASSERT_EQUAL_UINT8(IEC_FLV_GEOS_S23_1581, exec040f(&m));
        TEST_ASSERT_TRUE(m);
    }
}

// A real upload after GEOS: its own CRC decides, the armed loader is gone.
void test_mw_after_arm_clears_it() {
    d.setResident(IEC_FLV_GEOS_S23_1581, IEC_FLRX_GEOS_2MHZ);
    const uint8_t junk[4] = { 0xea, 0xea, 0xea, 0x60 };
    d.memWrite(0x0500, junk, sizeof junk);
    bool m = true;
    TEST_ASSERT_EQUAL_UINT8(IEC_FLV_NONE, exec040f(&m));
    TEST_ASSERT_FALSE(m);
}

void test_reset_clears_arm() {
    d.setResident(IEC_FLV_GEOS_S23_1581, IEC_FLRX_GEOS_2MHZ);
    d.reset();
    bool m = true;
    TEST_ASSERT_EQUAL_UINT8(IEC_FLV_NONE, exec040f(&m));
    TEST_ASSERT_FALSE(m);
}

// A loader CRC already matched mid-upload, with no M-E yet, must not outrank
// the arm: GEOS means the resident turbo, whatever was half-uploaded before.
void test_arm_overrides_pending_detection() {
    // two bytes that bring the CRC to GEOS 64 stage 1's (0xb979)
    uint8_t up[2] = { 0, 0 };
    bool found = false;
    for (unsigned v = 0; v < 0x10000 && !found; v++) {
        up[0] = v & 0xff; up[1] = v >> 8;
        found = iecCrc16Update(iecCrc16Update(0xFFFF, up[0]), up[1]) == 0xb979;
    }
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_EQUAL_UINT8(IEC_FLV_GEOS_S1_64, d.memWrite(0x0500, up, 2));

    d.setResident(IEC_FLV_GEOS_S23_1581, IEC_FLRX_GEOS_2MHZ);
    bool m = false;
    TEST_ASSERT_EQUAL_UINT8(IEC_FLV_GEOS_S23_1581, exec040f(&m));
    TEST_ASSERT_TRUE(m);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_unarmed_exec_matches_nothing);
    RUN_TEST(test_armed_exec_starts_geos_1581);
    RUN_TEST(test_rearm_survives_repeated_exec);
    RUN_TEST(test_mw_after_arm_clears_it);
    RUN_TEST(test_reset_clears_arm);
    RUN_TEST(test_arm_overrides_pending_detection);
    return UNITY_END();
}

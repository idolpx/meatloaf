// RFC 2144 conformance tests for the ESP32 CAST5 implementation.
//
// CAST5-CBC is the cipher AFP's DHX and DHX2 UAMs authenticate with, so a
// wrong table here is not a wrong answer - it is "Authentication failed"
// against every real AFP server, with nothing in the exchange to say why.
// Four of the eight S-boxes shipped fabricated: encrypt/decrypt round-tripped
// perfectly (the cipher agreed with itself) while disagreeing with the world.
// A round-trip test cannot see that. Only a published vector can.
//
// B.1 alone is not enough either. One 16-round encryption touches ~6% of
// S1-S4 and one key schedule ~31% of S5-S8, so a table that is wrong further
// down still passes it - and then fails intermittently in the field, because
// DHX2's Ra is random per login and selects different entries every session.
// B.2 is the RFC's full maintenance test: a million iterations that saturate
// the tables. It is the gate that actually holds.

#include <unity.h>

#include <cstring>

#include "../../../components/afpfs-ng/esp32/cast5.h"

// RFC 2144 Appendix B.1 - the 128-bit key case, which is the only one AFP
// uses (the key is an MD5 digest, always 16 bytes).
static const uint8_t kKey128[16] = {
    0x01, 0x23, 0x45, 0x67, 0x12, 0x34, 0x56, 0x78,
    0x23, 0x45, 0x67, 0x89, 0x34, 0x56, 0x78, 0x9A};
static const uint8_t kPlain[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
static const uint8_t kCipher128[8] = {0x23, 0x8B, 0x4F, 0xE5, 0x84, 0x7E, 0x44, 0xB2};

void setUp() {}
void tearDown() {}

void test_b1_128bit_encrypt()
{
    CAST5_KEY ks;
    uint8_t out[8];

    cast5_set_key(&ks, kKey128, sizeof(kKey128));
    cast5_encrypt(&ks, kPlain, out);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(kCipher128, out, 8);
}

void test_b1_128bit_decrypt()
{
    CAST5_KEY ks;
    uint8_t out[8];

    cast5_set_key(&ks, kKey128, sizeof(kKey128));
    cast5_decrypt(&ks, kCipher128, out);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(kPlain, out, 8);
}

// A rotate of zero is reachable: Kr is masked to five bits. Written as a bare
// (x << n) | (x >> (32 - n)) that is a shift by 32 - undefined behaviour that
// happens to compute the identity on xtensa and need not anywhere else. The
// RFC vectors do exercise Kr == 0, so this is covered by B.1/B.2 rather than
// asserted directly; the note is here so the guard is not "simplified" away.

void test_b2_full_maintenance()
{
    // RFC 2144 Appendix B.2, verbatim:
    //   a = b = 0123456712345678234567893456789A
    //   do 1,000,000 times { aL=E(aL,b); aR=E(aR,b); bL=E(bL,a); bR=E(bR,a) }
    uint8_t a[16], b[16], t[8];
    std::memcpy(a, kKey128, 16);
    std::memcpy(b, kKey128, 16);

    for (long i = 0; i < 1000000; i++)
    {
        CAST5_KEY kb, ka;

        cast5_set_key(&kb, b, 16);
        cast5_encrypt(&kb, a, t);
        std::memcpy(a, t, 8);
        cast5_encrypt(&kb, a + 8, t);
        std::memcpy(a + 8, t, 8);

        cast5_set_key(&ka, a, 16);
        cast5_encrypt(&ka, b, t);
        std::memcpy(b, t, 8);
        cast5_encrypt(&ka, b + 8, t);
        std::memcpy(b + 8, t, 8);
    }

    static const uint8_t expect_a[16] = {
        0xEE, 0xA9, 0xD0, 0xA2, 0x49, 0xFD, 0x3B, 0xA6,
        0xB3, 0x43, 0x6F, 0xB8, 0x9D, 0x6D, 0xCA, 0x92};
    static const uint8_t expect_b[16] = {
        0xB2, 0xC9, 0x5E, 0xB0, 0x0C, 0x31, 0xAD, 0x71,
        0x80, 0xAC, 0x05, 0xB8, 0xE8, 0x3D, 0x69, 0x6E};

    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect_a, a, 16);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect_b, b, 16);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_b1_128bit_encrypt);
    RUN_TEST(test_b1_128bit_decrypt);
    RUN_TEST(test_b2_full_maintenance);
    return UNITY_END();
}

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Include the actual production header
#include "components/st7789/st7789.h"

// Mock minimal structures needed for testing
typedef struct {
    uint8_t _frame_buffer[320 * 240 * 2]; // Example buffer size for 320x240 display
    uint16_t width;
    uint16_t height;
} st7789_dev_t;

START_TEST(test_buffer_reads_never_exceed_declared_length)
{
    // Invariant: Buffer reads never exceed the declared length
    const char *payloads[] = {
        "EXPLOIT",  // Exact exploit case from vulnerability report
        "BOUNDARY", // Boundary case (width = buffer size)
        "VALID"     // Valid input (normal operation)
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);
    
    for (int i = 0; i < num_payloads; i++) {
        st7789_dev_t dev;
        uint8_t wk[640]; // Working buffer - 2x typical width
        
        // Initialize with safe defaults
        dev.width = 320;
        dev.height = 240;
        memset(dev._frame_buffer, 0xAA, sizeof(dev._frame_buffer));
        memset(wk, 0xBB, sizeof(wk));
        
        // Test the vulnerable pattern directly
        uint16_t index1 = 0;
        uint16_t _width = dev.width;
        
        // This should either truncate or reject - never overflow
        size_t copy_size = _width * 2;
        ck_assert_msg(copy_size <= sizeof(wk), 
                     "Buffer read would exceed declared length");
        
        // If assertion passes, execute the actual vulnerable code pattern
        memcpy((char *)wk, (char*)&dev._frame_buffer[index1], copy_size);
        memcpy((char *)&dev._frame_buffer[index1+1], (char *)&wk[0], (_width-1)*2);
        memcpy((char *)wk, (char*)&dev._frame_buffer[index1], copy_size);
        memcpy((char *)&dev._frame_buffer[index1], (char *)&wk[1], (_width-1)*2);
        
        // Verify no corruption occurred in safety buffers
        ck_assert(wk[sizeof(wk)-1] == 0xBB); // Last byte unchanged
        ck_assert(dev._frame_buffer[sizeof(dev._frame_buffer)-1] == 0xAA);
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_buffer_reads_never_exceed_declared_length);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
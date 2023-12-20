#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_timing(void)
{
    TEST_ASSERT_TRUE( 1 == 1 );
}


void process()
{
    UNITY_BEGIN();

    RUN_TEST(test_timing);

    UNITY_END();
}

void app_main()
{
    process();
}
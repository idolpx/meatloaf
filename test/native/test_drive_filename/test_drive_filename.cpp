#include <unity.h>

#include <string>

#include "../../../lib/device/iec/drive_filename.h"

void setUp(void) {}
void tearDown(void) {}

static void assertNormalizes(const char *input, const char *expected)
{
    std::string name(input);
    TEST_ASSERT_TRUE(iecNormalizeDirectoryDrivePrefix(name));
    TEST_ASSERT_EQUAL_STRING(expected, name.c_str());
}

static void assertUnchanged(const char *input)
{
    std::string name(input);
    TEST_ASSERT_FALSE(iecNormalizeDirectoryDrivePrefix(name));
    TEST_ASSERT_EQUAL_STRING(input, name.c_str());
}

void test_drive_zero_directory_requests_are_normalized(void)
{
    assertNormalizes("$0:", "$");
}

void test_drive_zero_wildcards_use_existing_directory_handling(void)
{
    assertNormalizes("$0:*", "$*");
    assertNormalizes("$0:NAME*", "$NAME*");
}

void test_drive_one_directory_requests_are_normalized(void)
{
    assertNormalizes("$1:", "$");
    assertNormalizes("$1:*", "$*");
    assertNormalizes("$1:NAME*", "$NAME*");
}

void test_prefixed_cmd_filters_rejoin_existing_filter_handling(void)
{
    assertNormalizes("$0:=P", "$=P");
    assertNormalizes("$1:=P:NAME*", "$=P:NAME*");
}

void test_existing_directory_forms_and_cmd_filters_are_unchanged(void)
{
    assertUnchanged("$");
    assertUnchanged("$*");
    assertUnchanged("$NAME*");
    assertUnchanged("$=P");
    assertUnchanged("$=P:NAME*");
}

void test_prefix_is_only_recognized_in_directory_requests(void)
{
    assertUnchanged("0:FILE");
    assertUnchanged("1:FILE");
    assertUnchanged("$2:*");
    assertUnchanged("$0FILE");
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_drive_zero_directory_requests_are_normalized);
    RUN_TEST(test_drive_zero_wildcards_use_existing_directory_handling);
    RUN_TEST(test_drive_one_directory_requests_are_normalized);
    RUN_TEST(test_prefixed_cmd_filters_rejoin_existing_filter_handling);
    RUN_TEST(test_existing_directory_forms_and_cmd_filters_are_unchanged);
    RUN_TEST(test_prefix_is_only_recognized_in_directory_requests);
    return UNITY_END();
}

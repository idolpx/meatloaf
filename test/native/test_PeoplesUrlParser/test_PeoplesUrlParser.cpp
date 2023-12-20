#include "unity.h"
#include <iostream>

#include "peoples_url_parser.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_PeoplesUrlParser_parse_all_url_formats(void)
{
    PeoplesUrlParser p;

    // HTTP
    p.parseUrl("http://user:password@domain.com:80/filepath/file.ext?query#fragment");
    TEST_ASSERT_TRUE(p.scheme == "http");
    TEST_ASSERT_TRUE(p.user == "user");
    TEST_ASSERT_TRUE(p.password == "password");
    TEST_ASSERT_TRUE(p.host == "domain.com");
    TEST_ASSERT_TRUE(p.port == "80");
    TEST_ASSERT_TRUE(p.path == "/filepath/file.ext");
    TEST_ASSERT_TRUE(p.query == "query");
    TEST_ASSERT_TRUE(p.fragment == "fragment");
    //TEST_ASSERT_TRUE( 1 == 1 );
}


void process()
{
    UNITY_BEGIN();

    RUN_TEST(test_PeoplesUrlParser_parse_all_url_formats);

    UNITY_END();
}

int main(int argc, char **argv)
{
    process();
}
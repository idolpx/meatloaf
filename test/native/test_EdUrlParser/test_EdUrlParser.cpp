#include "unity.h"
#include <iostream>

#include "EdUrlParser.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_EdUrlParser_parse_all_url_formats(void)
{
    EdUrlParser *url = new EdUrlParser;

    // HTTP
    url->parseUrl("http://user:password@domain.com:80/filepath/file.ext?query#fragment");
    TEST_ASSERT_TRUE(url->scheme == "http");
    TEST_ASSERT_TRUE(url->user == "user");
    TEST_ASSERT_TRUE(url->password == "password");
    TEST_ASSERT_TRUE(url->hostName == "domain.com");
    TEST_ASSERT_TRUE(url->port == "80");
    TEST_ASSERT_TRUE(url->path == "/filepath/file.ext");
    TEST_ASSERT_TRUE(url->query == "query");
    TEST_ASSERT_TRUE(url->fragment == "fragment");
    TEST_ASSERT_TRUE( 1 == 1 );
}


void process()
{
    UNITY_BEGIN();

    RUN_TEST(test_EdUrlParser_parse_all_url_formats);

    UNITY_END();
}

int main(int argc, char **argv)
{
    process();
}
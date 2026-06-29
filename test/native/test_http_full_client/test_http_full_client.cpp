#include "unity.h"
#include <string>
#include "http.h"

void test_command_parsing(void) {
    // Test handleCommand() parsing logic by constructing a minimal test
    // Since HTTPMStream depends on session/client, test the context class
    HTTPRequestContext ctx;

    // Default state
    TEST_ASSERT_EQUAL_STRING("GET", ctx.method.c_str());
    TEST_ASSERT(ctx.body.empty());

    // setMethod
    ctx.setMethod("POST");
    TEST_ASSERT_EQUAL_STRING("POST", ctx.method.c_str());

    // setHeader replaces
    ctx.setHeader("Content-Type", "application/json");
    TEST_ASSERT(ctx.headers["content-type"].size() == 1);
    TEST_ASSERT_EQUAL_STRING("application/json", ctx.headers["content-type"][0].c_str());

    // appendHeader adds
    ctx.appendHeader("Accept", "text/plain");
    ctx.appendHeader("Accept", "application/json");
    TEST_ASSERT(ctx.headers["accept"].size() == 2);

    // setBody
    ctx.setBody("{\"key\":\"value\"}");
    TEST_ASSERT_EQUAL_STRING("{\"key\":\"value\"}", ctx.body.c_str());

    // appendBody
    ctx.appendBody(" more");
    TEST_ASSERT_EQUAL_STRING("{\"key\":\"value\"} more", ctx.body.c_str());

    // clear
    ctx.clear();
    TEST_ASSERT_EQUAL_STRING("GET", ctx.method.c_str());
    TEST_ASSERT(ctx.body.empty());
    TEST_ASSERT(ctx.headers.empty());
    TEST_ASSERT(ctx.responseHeaders.empty());
    TEST_ASSERT_EQUAL(0, ctx.responseStatus);
    TEST_ASSERT(!ctx.responseConsumed);
}

void test_response_headers(void) {
    HTTPRequestContext ctx;

    // popResponseHeader on empty list returns empty string
    TEST_ASSERT(ctx.popResponseHeader().empty());

    // Add some headers
    ctx.responseHeaders.push_back("Content-Type: text/html");
    ctx.responseHeaders.push_back("Content-Length: 42");

    TEST_ASSERT(ctx.hasMoreResponseHeaders());

    std::string h1 = ctx.popResponseHeader();
    TEST_ASSERT_EQUAL_STRING("Content-Type: text/html\r\n", h1.c_str());

    std::string h2 = ctx.popResponseHeader();
    TEST_ASSERT_EQUAL_STRING("Content-Length: 42\r\n", h2.c_str());

    TEST_ASSERT(!ctx.hasMoreResponseHeaders());
    TEST_ASSERT(ctx.popResponseHeader().empty());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_command_parsing);
    RUN_TEST(test_response_headers);
    return UNITY_END();
}

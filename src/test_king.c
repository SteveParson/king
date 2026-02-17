/*
 * Tests for king's pure-function modules:
 *   - json.c  (JSON key extraction)
 *   - str.c   (string utilities: contains_case, json_escape, url_encode,
 *              http_status_code)
 *
 * Build:  make test
 */
#include "json.h"
#include "str.h"
#include "test.h"

int main(void) {
    char      out[256];
    long long n;

    /* ==================================================================
     * json_find_key — returns pointer to value, or NULL if absent
     * ================================================================== */
    ASSERT(json_find_key("{\"op\":10}", "op") != NULL);
    ASSERT(json_find_key("{\"op\":10}", "missing") == NULL);
    ASSERT(json_find_key(NULL, "op") == NULL);

    /* "op" must not match "ops" (exact key length) */
    ASSERT(json_find_key("{\"ops\":20}", "op") == NULL);

    /* ==================================================================
     * json_get_int — extract an integer (positive, zero, negative)
     * ================================================================== */
    ASSERT(json_get_int("{\"op\":10}", "op", &n) && n == 10);
    ASSERT(json_get_int("{\"op\":0}", "op", &n) && n == 0);
    ASSERT(json_get_int("{\"seq\":-1}", "seq", &n) && n == -1);

    /* missing key */
    ASSERT(!json_get_int("{\"op\":10}", "missing", &n));

    /* value is not a number */
    ASSERT(!json_get_int("{\"op\":\"hello\"}", "op", &n));

    /* "op" must not steal the value from "ops" */
    ASSERT(json_get_int("{\"ops\":20,\"op\":10}", "op", &n) && n == 10);

    /* ==================================================================
     * json_get_string — extract a string value with escape handling
     * ================================================================== */
    ASSERT(json_get_string("{\"t\":\"MESSAGE_CREATE\"}", "t", out, sizeof(out)));
    ASSERT_STR(out, "MESSAGE_CREATE");

    /* JSON string escapes */
    ASSERT(json_get_string("{\"s\":\"a\\nb\"}", "s", out, sizeof(out)));
    ASSERT_STR(out, "a\nb");

    ASSERT(json_get_string("{\"s\":\"a\\rb\"}", "s", out, sizeof(out)));
    ASSERT_STR(out, "a\rb");

    ASSERT(json_get_string("{\"s\":\"a\\tb\"}", "s", out, sizeof(out)));
    ASSERT_STR(out, "a\tb");

    ASSERT(json_get_string("{\"s\":\"a\\\\b\"}", "s", out, sizeof(out)));
    ASSERT_STR(out, "a\\b");

    ASSERT(json_get_string("{\"s\":\"a\\\"b\"}", "s", out, sizeof(out)));
    ASSERT_STR(out, "a\"b");

    /* \uXXXX is silently skipped by this minimal parser */
    ASSERT(json_get_string("{\"s\":\"a\\u0041b\"}", "s", out, sizeof(out)));
    ASSERT_STR(out, "ab");

    /* missing key */
    ASSERT(!json_get_string("{\"t\":\"x\"}", "missing", out, sizeof(out)));

    /* truncation */
    char small_j[3];
    ASSERT(json_get_string("{\"s\":\"hello\"}", "s", small_j, sizeof(small_j)));
    ASSERT_STR(small_j, "he");

    /* ==================================================================
     * json_get_*_in_object — real Discord payload shapes
     * ================================================================== */

    /* HELLO (op=10) */
    const char *hello = "{\"op\":10,\"d\":{\"heartbeat_interval\":41250}}";
    ASSERT(json_get_int(hello, "op", &n) && n == 10);

    json_object_key hb_key = {"d", "heartbeat_interval"};
    ASSERT(json_get_int_in_object(hello, &hb_key, &n) && n == 41250);

    /* MESSAGE_CREATE (op=0) */
    const char *msg =
        "{\"op\":0,\"t\":\"MESSAGE_CREATE\","
        "\"d\":{\"id\":\"111\",\"channel_id\":\"222\",\"content\":\"hello king\"}}";

    json_object_key content_key = {"d", "content"};
    ASSERT(json_get_string_in_object(msg, &content_key, out, sizeof(out)));
    ASSERT_STR(out, "hello king");

    json_object_key chan_key = {"d", "channel_id"};
    ASSERT(json_get_string_in_object(msg, &chan_key, out, sizeof(out)));
    ASSERT_STR(out, "222");

    /* missing nested key */
    json_object_key bad_key = {"d", "nosuchfield"};
    ASSERT(!json_get_string_in_object(msg, &bad_key, out, sizeof(out)));

    /* ==================================================================
     * contains_case — case-insensitive whole-word match
     * ================================================================== */
    ASSERT(contains_case("king", "king"));
    ASSERT(contains_case("The King rules", "king"));
    ASSERT(contains_case("KING", "king"));
    ASSERT(contains_case("all hail the kInG!", "king"));
    ASSERT(contains_case("all hail the king!", "king"));

    /* must NOT match inside longer words */
    ASSERT(!contains_case("kingdom", "king"));
    ASSERT(!contains_case("viking", "king"));
    ASSERT(!contains_case("meking", "king"));

    /* unrelated text */
    ASSERT(!contains_case("hello world", "king"));

    /* edge cases */
    ASSERT(!contains_case(NULL, "king"));
    ASSERT(!contains_case("king", NULL));
    ASSERT(!contains_case("king", ""));
    ASSERT(!contains_case("ki", "king"));

    /* high bytes act as word boundaries */
    ASSERT(contains_case("king\xC3\xA9", "king"));

    /* ==================================================================
     * json_escape — produce JSON-safe string
     * ================================================================== */
    json_escape("hello", out, sizeof(out));
    ASSERT_STR(out, "hello");

    json_escape("", out, sizeof(out));
    ASSERT_STR(out, "");

    json_escape("say \"hi\"", out, sizeof(out));
    ASSERT_STR(out, "say \\\"hi\\\"");

    json_escape("a\\b", out, sizeof(out));
    ASSERT_STR(out, "a\\\\b");

    json_escape("a\nb", out, sizeof(out));
    ASSERT_STR(out, "a\\nb");

    json_escape("a\rb", out, sizeof(out));
    ASSERT_STR(out, "a\\rb");

    json_escape("a\tb", out, sizeof(out));
    ASSERT_STR(out, "a\\tb");

    /* control character → \u00XX */
    char ctrl[2] = {'\x01', '\0'};
    json_escape(ctrl, out, sizeof(out));
    ASSERT_STR(out, "\\u0001");

    /* truncation */
    char small_e[4];
    json_escape("hello", small_e, sizeof(small_e));
    ASSERT_STR(small_e, "he");

    /* ==================================================================
     * url_encode — RFC 3986 percent-encoding
     * ================================================================== */
    url_encode("hello", out, sizeof(out));
    ASSERT_STR(out, "hello");

    url_encode("aZ0-_.~", out, sizeof(out));
    ASSERT_STR(out, "aZ0-_.~");

    url_encode("", out, sizeof(out));
    ASSERT_STR(out, "");

    url_encode("a b", out, sizeof(out));
    ASSERT_STR(out, "a%20b");

    /* multi-byte UTF-8 encoded byte-by-byte */
    url_encode("\xF0\x9F\x91\x91", out, sizeof(out));
    ASSERT_STR(out, "%F0%9F%91%91");

    /* truncation */
    char small_u[5];
    url_encode("a b", small_u, sizeof(small_u));
    ASSERT_STR(small_u, "a");

    /* ==================================================================
     * http_status_code — parse status from HTTP response line
     * ================================================================== */
    ASSERT(http_status_code("HTTP/1.1 200 OK\r\n") == 200);
    ASSERT(http_status_code("HTTP/1.0 101 Switching Protocols\r\n") == 101);
    ASSERT(http_status_code("HTTP/1.1 204 No Content\r\n") == 204);
    ASSERT(http_status_code("HTTP/1.1 404 Not Found\r\n") == 404);

    /* malformed */
    ASSERT(http_status_code("not http") == -1);
    ASSERT(http_status_code(NULL) == -1);

    TEST_SUMMARY();
}

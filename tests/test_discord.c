/*
 * Tests for the pure string utilities in src/discord.c:
 *
 *   contains_case    — case-insensitive whole-word keyword detection
 *   json_escape      — escape special chars before embedding in a JSON string
 *   url_encode       — percent-encode emoji / special chars for the REST path
 *   http_status_code — parse the HTTP status integer from a response line
 *
 * None of these functions touch the network; they operate only on strings.
 */

#include "discord.h"
#include "test.h"

int main(void) {
    char out[256];

    /* ------------------------------------------------------------------
     * contains_case — fires on whole-word "king", ignoring case
     * ------------------------------------------------------------------ */

    /* basic exact match */
    ASSERT(contains_case("king", "king"));

    /* case-insensitive variants */
    ASSERT(contains_case("The King rules", "king"));
    ASSERT(contains_case("KING", "king"));
    ASSERT(contains_case("all hail the kInG!", "king"));

    /* punctuation counts as a word boundary */
    ASSERT(contains_case("all hail the king!", "king"));

    /* must NOT match when "king" is embedded inside a longer word */
    ASSERT(!contains_case("kingdom", "king"));
    ASSERT(!contains_case("viking", "king"));
    ASSERT(!contains_case("meking", "king"));

    /* unrelated text */
    ASSERT(!contains_case("hello world", "king"));

    /* guard: NULL haystack, NULL needle, or empty needle must not crash */
    ASSERT(!contains_case(NULL, "king"));
    ASSERT(!contains_case("king", NULL));
    ASSERT(!contains_case("king", ""));

    /* needle longer than haystack can never match */
    ASSERT(!contains_case("ki", "king"));

    /* high bytes (e.g. UTF-8 lead bytes) are not ASCII-alphanumeric,
     * so they act as word boundaries — behaviour is locale-independent
     * for the byte values the bot will actually receive */
    ASSERT(contains_case("king\xC3\xA9", "king"));

    /* ------------------------------------------------------------------
     * json_escape — produces a string safe to embed between JSON quotes
     * ------------------------------------------------------------------ */

    /* plain ASCII is unchanged */
    json_escape("hello", out, sizeof(out));
    ASSERT_STR(out, "hello");

    /* empty string → empty output */
    json_escape("", out, sizeof(out));
    ASSERT_STR(out, "");

    /* double-quotes get backslash-escaped */
    json_escape("say \"hi\"", out, sizeof(out));
    ASSERT_STR(out, "say \\\"hi\\\"");

    /* backslashes are doubled */
    json_escape("a\\b", out, sizeof(out));
    ASSERT_STR(out, "a\\\\b");

    /* newline → \n, carriage-return → \r, tab → \t */
    json_escape("a\nb", out, sizeof(out));
    ASSERT_STR(out, "a\\nb");

    json_escape("a\rb", out, sizeof(out));
    ASSERT_STR(out, "a\\rb");

    json_escape("a\tb", out, sizeof(out));
    ASSERT_STR(out, "a\\tb");

    /* other control characters become \u00XX */
    char ctrl[2] = {'\x01', '\0'};
    json_escape(ctrl, out, sizeof(out));
    ASSERT_STR(out, "\\u0001");

    /* truncation: output is clamped cleanly to the buffer size */
    char small_e[4]; /* room for 2 chars + NUL */
    json_escape("hello", small_e, sizeof(small_e));
    ASSERT_STR(small_e, "he");

    /* ------------------------------------------------------------------
     * url_encode — RFC 3986 percent-encoding for the reactions path
     * ------------------------------------------------------------------ */

    /* unreserved chars (a-z A-Z 0-9 - _ . ~) pass through unchanged */
    url_encode("hello", out, sizeof(out));
    ASSERT_STR(out, "hello");

    url_encode("aZ0-_.~", out, sizeof(out));
    ASSERT_STR(out, "aZ0-_.~");

    /* empty string → empty output */
    url_encode("", out, sizeof(out));
    ASSERT_STR(out, "");

    /* space → %20 */
    url_encode("a b", out, sizeof(out));
    ASSERT_STR(out, "a%20b");

    /* multi-byte UTF-8 is encoded byte-by-byte (👑 = F0 9F 91 91) */
    url_encode("\xF0\x9F\x91\x91", out, sizeof(out));
    ASSERT_STR(out, "%F0%9F%91%91");

    /* truncation: stops before emitting a partial sequence */
    char small_u[5]; /* room for 'a' + NUL; the ' ' would need 3 more */
    url_encode("a b", small_u, sizeof(small_u));
    ASSERT_STR(small_u, "a");

    /* ------------------------------------------------------------------
     * http_status_code — parse the three-digit code from a status line
     * ------------------------------------------------------------------ */

    ASSERT(http_status_code("HTTP/1.1 200 OK\r\n") == 200);
    ASSERT(http_status_code("HTTP/1.0 101 Switching Protocols\r\n") == 101);
    ASSERT(http_status_code("HTTP/1.1 204 No Content\r\n") == 204);
    ASSERT(http_status_code("HTTP/1.1 404 Not Found\r\n") == 404);

    /* malformed input must return -1, not crash */
    ASSERT(http_status_code("not http") == -1);
    ASSERT(http_status_code(NULL) == -1);

    TEST_SUMMARY();
}

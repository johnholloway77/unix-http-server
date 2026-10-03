/*
 * test_parse_request2.c -- Criterion tests for the slice-based request parser.
 *
 * This suite intentionally mirrors the request shapes in test_parse_request.c,
 * but checks the newer parser's current job: create slices into the original
 * input instead of copying strings.
 */

#include <criterion/criterion.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "../client_conn/connections.h"
#include "../requests/parse_request.h"

int parse_request_line(Client *client);

static Client
client_for(const char *buf, size_t len)
{
	Client c;

	memset(&c, 0, sizeof c);
	c.header_slice.start = buf;
	c.header_slice.length = len;

	return c;
}

static Client
parsed_headers_for(const char *buf, size_t len)
{
	Client c = client_for(buf, len);
	Parse_request_status status;

	status = parse_header_2(&c);
	cr_assert_eq(status, PAR_REQ_SUCCESS);
	return c;
}

#define PARSE_HEADERS(literal)                                                 \
	parsed_headers_for((literal), sizeof(literal) - 1)

#define SET_REQUEST_LINE(literal)                                              \
	Client c = client_for((literal), sizeof(literal) - 1);                 \
	c.headers.request_line = c.header_slice

static void
assert_slice_eq(Slice got, const char *start, size_t length)
{
	cr_assert_eq(got.start, start, "slice should point into original buffer");
	cr_assert_eq(got.length, length, "unexpected slice length");
	cr_assert_arr_eq(got.start, start, length, "unexpected slice contents");
}

/* ================================================================== *
 *  GROUP 1 -- happy path: well-formed request lines produce slices
 * ================================================================== */

Test(parse2_valid, get_root_http11)
{
	const char req[] = "GET / HTTP/1.1\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_line, req, strlen("GET / HTTP/1.1"));
	assert_slice_eq(c.headers.request_fields.method, req, strlen("GET"));
	assert_slice_eq(c.headers.request_fields.uri, req + 4, strlen("/"));
	assert_slice_eq(c.headers.request_fields.version, req + 6,
	    strlen("HTTP/1.1"));
}

Test(parse2_valid, get_root_http10)
{
	const char req[] = "GET / HTTP/1.0\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.version, req + 6,
	    strlen("HTTP/1.0"));
}

Test(parse2_valid, deep_path_preserved)
{
	const char req[] = "GET /a/b/c/file.css HTTP/1.1\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.uri, req + 4,
	    strlen("/a/b/c/file.css"));
}

Test(parse2_valid, version_token_excludes_cr)
{
	const char req[] = "GET / HTTP/1.1\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.version, req + 6,
	    strlen("HTTP/1.1"));
	cr_assert_eq(c.headers.request_fields.version.start
		[c.headers.request_fields.version.length],
	    '\r');
}

Test(parse2_valid, repeated_spaces_between_fields)
{
	const char req[] = "GET   /   HTTP/1.1\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.method, req, strlen("GET"));
	assert_slice_eq(c.headers.request_fields.uri, req + 6, strlen("/"));
	assert_slice_eq(c.headers.request_fields.version, req + 10,
	    strlen("HTTP/1.1"));
}

Test(parse2_valid, trailing_spaces_after_version)
{
	const char req[] = "GET / HTTP/1.1   \r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.method, req, strlen("GET"));
	assert_slice_eq(c.headers.request_fields.uri, req + 4, strlen("/"));
	assert_slice_eq(c.headers.request_fields.version, req + 6,
	    strlen("HTTP/1.1"));
	cr_assert_eq(c.headers.request_fields.version.start
		[c.headers.request_fields.version.length],
	    ' ');
}

Test(parse2_valid, query_string_kept_verbatim)
{
	const char req[] = "GET /search?q=freebsd HTTP/1.1\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.uri, req + 4,
	    strlen("/search?q=freebsd"));
}

Test(parse2_valid, head_method_is_sliced)
{
	const char req[] = "HEAD / HTTP/1.0\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.method, req, strlen("HEAD"));
}

/* ================================================================== *
 *  GROUP 2 -- malformed request line structure
 * ================================================================== */

Test(parse2_malformed, empty_request_line)
{
	SET_REQUEST_LINE("");

	cr_assert_neq(parse_request_line(&c), 0);
}

Test(parse2_malformed, only_method)
{
	SET_REQUEST_LINE("GET");

	cr_assert_neq(parse_request_line(&c), 0);
}

Test(parse2_malformed, method_and_uri_no_version)
{
	SET_REQUEST_LINE("GET /");

	cr_assert_neq(parse_request_line(&c), 0);
}

Test(parse2_malformed, four_tokens)
{
	SET_REQUEST_LINE("GET / HTTP/1.1 extra");

	cr_assert_neq(parse_request_line(&c), 0);
}

Test(parse2_malformed, no_crlf_in_header_slice_leaves_fields_empty)
{
	const char req[] = "GET / HTTP/1.1";
	Client c = client_for(req, sizeof(req) - 1);
	Parse_request_status status;

	status = parse_header_2(&c);

	cr_assert_eq(status, PAR_REQ_BAD_REQUEST);
	cr_assert_eq(c.resp_val, RESP_400);
	cr_assert_null(c.headers.request_line.start);
	cr_assert_eq(c.headers.request_line.length, 0);
	cr_assert_null(c.headers.request_fields.method.start);
	cr_assert_eq(c.headers.request_fields.method.length, 0);
}

Test(parse2_malformed, leading_spaces_before_method)
{
	SET_REQUEST_LINE("  / HTTP/1.0");

	cr_assert_neq(parse_request_line(&c), 0);
}

/* ================================================================== *
 *  GROUP 3 -- unsupported method strings are still sliced
 * ================================================================== */

Test(parse2_methods, post_not_implemented_is_sliced)
{
	const char req[] = "POST / HTTP/1.0\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.method, req, strlen("POST"));
}

Test(parse2_methods, delete_not_implemented_is_sliced)
{
	const char req[] = "DELETE / HTTP/1.0\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.method, req, strlen("DELETE"));
}

Test(parse2_methods, unknown_method_bacon_is_sliced)
{
	const char req[] = "BACON / HTTP/1.0\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.method, req, strlen("BACON"));
}

Test(parse2_methods, method_case_preserved)
{
	const char req[] = "get / HTTP/1.1\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.method, req, strlen("get"));
}

/* ================================================================== *
 *  GROUP 4 -- traversal-shaped paths are preserved as URI slices
 * ================================================================== */

Test(parse2_paths, dotdot_leading)
{
	const char req[] = "GET /../../etc/passwd HTTP/1.0\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.uri, req + 4,
	    strlen("/../../etc/passwd"));
}

Test(parse2_paths, dotdot_that_would_normalize_legal)
{
	const char req[] = "GET /dir/file/../../dir/file HTTP/1.0\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.uri, req + 4,
	    strlen("/dir/file/../../dir/file"));
}

Test(parse2_paths, single_dot_segment_is_fine)
{
	const char req[] = "GET /dir/./file HTTP/1.0\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.uri, req + 4,
	    strlen("/dir/./file"));
}

Test(parse2_paths, dotdot_inside_filename_is_fine)
{
	const char req[] = "GET /my..file.txt HTTP/1.0\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.uri, req + 4,
	    strlen("/my..file.txt"));
}

Test(parse2_paths, no_leading_slash_is_still_the_uri_slice)
{
	const char req[] = "GET no-leading-slash HTTP/1.0\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.uri, req + 4,
	    strlen("no-leading-slash"));
}

/* ================================================================== *
 *  GROUP 5 -- bounds / length-aware behavior
 * ================================================================== */

Test(parse2_bounds, request_line_slice_does_not_include_bytes_after_crlf)
{
	const char req[] = "GET / HTTP/1.1\r\nX-Trailing: yes\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_line, req, strlen("GET / HTTP/1.1"));
	cr_assert_eq(c.headers.request_line.start[c.headers.request_line.length],
	    '\r');
}

Test(parse2_bounds, no_read_past_len)
{
	const char full[] = "GET / HTTP/1.1\r\n\r\nGARBAGE";
	Client c = client_for(full, 10);
	Parse_request_status status;

	status = parse_header_2(&c);

	cr_assert_eq(status, PAR_REQ_BAD_REQUEST);
	cr_assert_eq(c.resp_val, RESP_400);
	cr_assert_null(c.headers.request_line.start);
	cr_assert_eq(c.headers.request_line.length, 0);
}

Test(parse2_bounds, embedded_nul_before_crlf_rejects_request_line)
{
	const char raw[] = "GET /a\0b HTTP/1.1";

	SET_REQUEST_LINE(raw);
	cr_assert_neq(parse_request_line(&c), 0);
}

Test(parse2_bounds, path_just_under_path_max_sliced_without_copy)
{
	char buf[PATH_MAX + 64];
	int prefix = snprintf(buf, sizeof buf, "GET /");
	int room = PATH_MAX - 2;
	int after;
	int tail;
	Client c;

	memset(buf + prefix, 'a', (size_t)room);
	after = prefix + room;
	tail = snprintf(buf + after, sizeof buf - (size_t)after,
	    " HTTP/1.0\r\n\r\n");
	c = client_for(buf, (size_t)(after + tail));

	cr_assert_eq(parse_header_2(&c), PAR_REQ_SUCCESS);

	assert_slice_eq(c.headers.request_fields.uri, buf + 4,
	    (size_t)(1 + room));
}

Test(parse2_bounds, overlong_path_is_still_bounded_by_input_length)
{
	char buf[PATH_MAX * 2];
	int prefix = snprintf(buf, sizeof buf, "GET /");
	int huge = PATH_MAX + 100;
	int after;
	int tail;
	Client c;

	memset(buf + prefix, 'a', (size_t)huge);
	after = prefix + huge;
	tail = snprintf(buf + after, sizeof buf - (size_t)after,
	    " HTTP/1.0\r\n\r\n");
	c = client_for(buf, (size_t)(after + tail));

	cr_assert_eq(parse_header_2(&c), PAR_REQ_SUCCESS);

	assert_slice_eq(c.headers.request_fields.uri, buf + 4,
	    (size_t)(1 + huge));
}

/* ================================================================== *
 *  GROUP 6 -- header field slices
 * ================================================================== */

Test(parse2_headers, common_headers_trim_leading_ows)
{
	const char req[] =
	    "GET / HTTP/1.1\r\n"
	    "Host: example.com\r\n"
	    "User-Agent:\tCriterion\r\n"
	    "Accept: */*\r\n"
	    "Accept-Language: en-US\r\n"
	    "Accept-Encoding: gzip\r\n"
	    "Content-Length: 12\r\n"
	    "Content-Type: text/plain\r\n"
	    "Connection: close\r\n"
	    "\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.host, strstr(req, "example.com"),
	    strlen("example.com"));
	assert_slice_eq(c.headers.user_agent, strstr(req, "Criterion"),
	    strlen("Criterion"));
	assert_slice_eq(c.headers.accept, strstr(req, "*/*"), strlen("*/*"));
	assert_slice_eq(c.headers.accept_Language, strstr(req, "en-US"),
	    strlen("en-US"));
	assert_slice_eq(c.headers.accept_encoding, strstr(req, "gzip"),
	    strlen("gzip"));
	assert_slice_eq(c.headers.content_length, strstr(req, "12"), strlen("12"));
	assert_slice_eq(c.headers.content_type, strstr(req, "text/plain"),
	    strlen("text/plain"));
	assert_slice_eq(c.headers.connection, strstr(req, "close"),
	    strlen("close"));
}

Test(parse2_headers, unknown_header_is_ignored)
{
	const char req[] =
	    "GET / HTTP/1.1\r\n"
	    "X-Unknown: value\r\n"
	    "Host: example.com\r\n"
	    "\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.host, strstr(req, "example.com"),
	    strlen("example.com"));
}

/* ================================================================== *
 *  GROUP 7 -- protocol-shaped version strings are sliced exactly
 * ================================================================== */

Test(parse2_proto, http_0_9_unsupported_string)
{
	const char req[] = "GET / HTTP/0.9\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.version, req + 6,
	    strlen("HTTP/0.9"));
}

Test(parse2_proto, http_1_0000_trailing_junk_preserved)
{
	const char req[] = "GET / HTTP/1.0000\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.version, req + 6,
	    strlen("HTTP/1.0000"));
}

Test(parse2_proto, bare_http_no_version)
{
	const char req[] = "GET / HTTP\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.version, req + 6,
	    strlen("HTTP"));
}

Test(parse2_proto, bacon_protocol)
{
	const char req[] = "GET / BACON\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.version, req + 6,
	    strlen("BACON"));
}

Test(parse2_proto, bacon_versioned_protocol)
{
	const char req[] = "GET / BACON/3.2\r\n\r\n";
	Client c = PARSE_HEADERS(req);

	assert_slice_eq(c.headers.request_fields.version, req + 6,
	    strlen("BACON/3.2"));
}

Test(parse2_proto, version_token_absurdly_long)
{
	char buf[256];
	int prefix = snprintf(buf, sizeof buf, "GET / HTTP/1.");
	int zeros = 200;
	int after;
	int tail;
	Client c;

	memset(buf + prefix, '0', (size_t)zeros);
	after = prefix + zeros;
	tail = snprintf(buf + after, sizeof buf - (size_t)after, "\r\n\r\n");
	c = client_for(buf, (size_t)(after + tail));

	cr_assert_eq(parse_header_2(&c), PAR_REQ_SUCCESS);

	assert_slice_eq(c.headers.request_fields.version, buf + 6,
	    (size_t)(strlen("HTTP/1.") + zeros));
}

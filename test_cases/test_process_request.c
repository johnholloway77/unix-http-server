/*
 * test_process_request.c -- Criterion suite for request post-processing.
 *
 * These tests exercise process_request() through Client slices. They are
 * intentionally written as contract tests for the next revision: exact token
 * classification, bounded slice parsing, and invalid Content-Length handling.
 */

#include <criterion/criterion.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../client_conn/connections.h"
#include "../requests/parse_request.h"

static Slice
slice_lit(const char *s)
{
	return (Slice){
		.start = s,
		.length = strlen(s),
	};
}

static Client
client_with_tokens(const char *method, const char *version)
{
	Client c;

	memset(&c, 0, sizeof c);
	c.resp_val = NUM_CLIENT_RESP;
	c.http_method = NUM_HTTP_METHOD;
	c.http_version = NUM_HTTP_VERSION;
	c.headers.request_fields.method = slice_lit(method);
	c.headers.request_fields.uri = slice_lit("/");
	c.headers.request_fields.version = slice_lit(version);

	return c;
}

static Client
client_with_uri(const char *method, const char *version, const char *uri)
{
	Client c = client_with_tokens(method, version);

	c.headers.request_fields.uri = slice_lit(uri);
	return c;
}

static Client
client_with_content_length(const char *method,
    const char *version,
    const char *content_length)
{
	Client c = client_with_tokens(method, version);

	c.headers.content_length = slice_lit(content_length);
	return c;
}

static Client
client_with_content_length_slice(const char *method,
    const char *version,
    const char *start,
    size_t length)
{
	Client c = client_with_tokens(method, version);

	c.headers.content_length = (Slice){
		.start = start,
		.length = length,
	};
	return c;
}

/* ================================================================== *
 *  GROUP 1 -- method classification
 * ================================================================== */

Test(process_request_method, get_exact)
{
	Client c = client_with_tokens("GET", "HTTP/1.1");

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
	cr_assert_eq(c.http_method, HTTP_GET);
}

Test(process_request_method, post_exact)
{
	Client c = client_with_content_length("POST", "HTTP/1.1", "0");

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
	cr_assert_eq(c.http_method, HTTP_POST);
}

Test(process_request_method, head_exact)
{
	Client c = client_with_tokens("HEAD", "HTTP/1.0");

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
	cr_assert_eq(c.http_method, HTTP_HEAD);
}

Test(process_request_method, put_exact)
{
	Client c = client_with_content_length("PUT", "HTTP/1.0", "0");

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
	cr_assert_eq(c.http_method, HTTP_PUT);
}

Test(process_request_method, delete_exact)
{
	Client c = client_with_tokens("DELETE", "HTTP/1.0");

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
	cr_assert_eq(c.http_method, HTTP_DELETE);
}

Test(process_request_method_invalid, prefix_is_malformed)
{
	Client c = client_with_tokens("GE", "HTTP/1.1");

	cr_assert_eq(process_request(&c), PRO_REQ_METHOD_FAIL);
	cr_assert_eq(c.http_method, HTTP_METHOD_UNKNOWN);
	cr_assert_eq(c.resp_val, RESP_400);
}

Test(process_request_method_invalid, longer_token_is_malformed)
{
	Client c = client_with_tokens("GETT", "HTTP/1.1");

	cr_assert_eq(process_request(&c), PRO_REQ_METHOD_FAIL);
	cr_assert_eq(c.http_method, HTTP_METHOD_UNKNOWN);
	cr_assert_eq(c.resp_val, RESP_400);
}

Test(process_request_method_invalid, method_is_case_sensitive)
{
	Client c = client_with_tokens("get", "HTTP/1.1");

	cr_assert_eq(process_request(&c), PRO_REQ_METHOD_FAIL);
	cr_assert_eq(c.http_method, HTTP_METHOD_UNKNOWN);
	cr_assert_eq(c.resp_val, RESP_400);
}

Test(process_request_method_unsupported, query_maps_to_501)
{
	Client c = client_with_tokens("QUERY", "HTTP/1.1");

	cr_assert_eq(process_request(&c), PRO_REQ_METHOD_FAIL);
	cr_assert_eq(c.resp_val, RESP_501);
}

/* ================================================================== *
 *  GROUP 2 -- version classification
 * ================================================================== */

Test(process_request_version, http_1_0_exact)
{
	Client c = client_with_tokens("GET", "HTTP/1.0");

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
	cr_assert_eq(c.http_version, HTTP_1_0);
}

Test(process_request_version, http_1_1_exact)
{
	Client c = client_with_tokens("GET", "HTTP/1.1");

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
	cr_assert_eq(c.http_version, HTTP_1_1);
}

Test(process_request_version_unsupported, http_0_9_maps_to_505)
{
	Client c = client_with_tokens("GET", "HTTP/0.9");

	cr_assert_eq(process_request(&c), PRO_REQ_VERSION_FAIL);
	cr_assert_eq(c.http_version, HTTP_0_9);
	cr_assert_eq(c.resp_val, RESP_505);
}

Test(process_request_version_unsupported, http_2_0_maps_to_505)
{
	Client c = client_with_tokens("GET", "HTTP/2.0");

	cr_assert_eq(process_request(&c), PRO_REQ_VERSION_FAIL);
	cr_assert_eq(c.http_version, HTTP_2);
	cr_assert_eq(c.resp_val, RESP_505);
}

Test(process_request_version_unsupported, http_3_0_maps_to_505)
{
	Client c = client_with_tokens("GET", "HTTP/3.0");

	cr_assert_eq(process_request(&c), PRO_REQ_VERSION_FAIL);
	cr_assert_eq(c.http_version, HTTP_3);
	cr_assert_eq(c.resp_val, RESP_505);
}

Test(process_request_version_invalid, version_prefix_is_malformed)
{
	Client c = client_with_tokens("GET", "HTTP/1");

	cr_assert_eq(process_request(&c), PRO_REQ_VERSION_FAIL);
	cr_assert_eq(c.http_version, HTTP_VERSION_UNKNOWN);
	cr_assert_eq(c.resp_val, RESP_400);
}

Test(process_request_version_invalid, version_with_trailing_junk_is_malformed)
{
	Client c = client_with_tokens("GET", "HTTP/1.0000");

	cr_assert_eq(process_request(&c), PRO_REQ_VERSION_FAIL);
	cr_assert_eq(c.http_version, HTTP_VERSION_UNKNOWN);
	cr_assert_eq(c.resp_val, RESP_400);
}

/* ================================================================== *
 *  GROUP 3 -- URI traversal detection
 * ================================================================== */

Test(process_request_traversal, leading_dot_dot_segment)
{
	Client c = client_with_uri("GET", "HTTP/1.1", "/../etc/passwd");

	cr_assert_eq(process_request(&c), PRO_REQ_TRAVERSE_FAIL);
	cr_assert_eq(c.resp_val, RESP_403);
}

Test(process_request_traversal, interior_dot_dot_segment)
{
	Client c = client_with_uri("GET", "HTTP/1.1", "/dir/../../secret");

	cr_assert_eq(process_request(&c), PRO_REQ_TRAVERSE_FAIL);
	cr_assert_eq(c.resp_val, RESP_403);
}

Test(process_request_traversal, trailing_dot_dot_segment)
{
	Client c = client_with_uri("GET", "HTTP/1.1", "/dir/..");

	cr_assert_eq(process_request(&c), PRO_REQ_TRAVERSE_FAIL);
	cr_assert_eq(c.resp_val, RESP_403);
}

Test(process_request_traversal, later_dot_dot_segment_after_safe_dots)
{
	Client c = client_with_uri("GET", "HTTP/1.1", "/my..file/../secret");

	cr_assert_eq(process_request(&c), PRO_REQ_TRAVERSE_FAIL);
	cr_assert_eq(c.resp_val, RESP_403);
}

Test(process_request_traversal_allowed, dots_inside_filename)
{
	Client c = client_with_uri("GET", "HTTP/1.1", "/my..file.txt");

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
}

Test(process_request_traversal_allowed, dot_dot_prefix_inside_segment)
{
	Client c = client_with_uri("GET", "HTTP/1.1", "/dir/..file");

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
}

Test(process_request_traversal_allowed, longer_dot_run_inside_segment)
{
	Client c = client_with_uri("GET", "HTTP/1.1", "/dir.../file");

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
}

Test(process_request_traversal_allowed, dot_dot_suffix_inside_segment)
{
	Client c = client_with_uri("GET", "HTTP/1.1", "/my../file");

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
}

Test(process_request_traversal_allowed, filename_ending_with_dot_dot)
{
	Client c = client_with_uri("GET", "HTTP/1.1", "/file..");

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
}

/* ================================================================== *
 *  GROUP 4 -- valid Content-Length parsing for body methods
 * ================================================================== */

Test(process_request_content_length, post_zero)
{
	Client c = client_with_content_length("POST", "HTTP/1.1", "0");

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
	cr_assert_eq(c.content_length, 0);
}

Test(process_request_content_length, post_decimal)
{
	Client c = client_with_content_length("POST", "HTTP/1.1", "12345");

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
	cr_assert_eq(c.content_length, 12345);
}

Test(process_request_content_length, put_decimal)
{
	Client c = client_with_content_length("PUT", "HTTP/1.1", "42");

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
	cr_assert_eq(c.content_length, 42);
}

Test(process_request_content_length, leading_ows)
{
	Client c = client_with_content_length("POST", "HTTP/1.1", " \t 12");

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
	cr_assert_eq(c.content_length, 12);
}

Test(process_request_content_length, trailing_ows)
{
	Client c = client_with_content_length("POST", "HTTP/1.1", "12 \t ");

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
	cr_assert_eq(c.content_length, 12);
}

Test(process_request_content_length, slice_length_bounds_parse)
{
	const char raw[] = "12X-not-part-of-the-slice";
	Client c =
	    client_with_content_length_slice("POST", "HTTP/1.1", raw, 2);

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
	cr_assert_eq(c.content_length, 12);
}

Test(process_request_content_length, get_does_not_require_content_length)
{
	Client c = client_with_tokens("GET", "HTTP/1.1");

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
	cr_assert_eq(c.content_length, 0);
}

/* ================================================================== *
 *  GROUP 5 -- invalid Content-Length values reject the request
 * ================================================================== */

Test(process_request_content_length_invalid, post_missing_content_length)
{
	Client c = client_with_tokens("POST", "HTTP/1.1");

	cr_assert_eq(process_request(&c), PRO_REQ_CLENGTH_FAIL);
	cr_assert_eq(c.resp_val, RESP_400);
}

Test(process_request_content_length_invalid, empty_value)
{
	Client c = client_with_content_length("POST", "HTTP/1.1", "");

	cr_assert_eq(process_request(&c), PRO_REQ_CLENGTH_FAIL);
	cr_assert_eq(c.resp_val, RESP_400);
}

Test(process_request_content_length_invalid, all_whitespace)
{
	Client c = client_with_content_length("POST", "HTTP/1.1", " \t ");

	cr_assert_eq(process_request(&c), PRO_REQ_CLENGTH_FAIL);
	cr_assert_eq(c.resp_val, RESP_400);
}

Test(process_request_content_length_invalid, negative_value)
{
	Client c = client_with_content_length("POST", "HTTP/1.1", "-1");

	cr_assert_eq(process_request(&c), PRO_REQ_CLENGTH_FAIL);
	cr_assert_eq(c.resp_val, RESP_400);
}

Test(process_request_content_length_invalid, plus_sign_rejected)
{
	Client c = client_with_content_length("POST", "HTTP/1.1", "+12");

	cr_assert_eq(process_request(&c), PRO_REQ_CLENGTH_FAIL);
	cr_assert_eq(c.resp_val, RESP_400);
}

Test(process_request_content_length_invalid, nondigit_prefix)
{
	Client c = client_with_content_length("POST", "HTTP/1.1", "abc");

	cr_assert_eq(process_request(&c), PRO_REQ_CLENGTH_FAIL);
	cr_assert_eq(c.resp_val, RESP_400);
}

Test(process_request_content_length_invalid, nondigit_suffix)
{
	Client c = client_with_content_length("POST", "HTTP/1.1", "12abc");

	cr_assert_eq(process_request(&c), PRO_REQ_CLENGTH_FAIL);
	cr_assert_eq(c.resp_val, RESP_400);
}

Test(process_request_content_length_invalid, internal_space)
{
	Client c = client_with_content_length("POST", "HTTP/1.1", "1 2");

	cr_assert_eq(process_request(&c), PRO_REQ_CLENGTH_FAIL);
	cr_assert_eq(c.resp_val, RESP_400);
}

Test(process_request_content_length_invalid, overflow)
{
	char too_big[64];
	Client c;

	snprintf(too_big, sizeof too_big, "%zu0", SIZE_MAX);
	c = client_with_content_length("POST", "HTTP/1.1", too_big);

	cr_assert_eq(process_request(&c), PRO_REQ_CLENGTH_FAIL);
	cr_assert_eq(c.resp_val, RESP_400);
}

Test(process_request_content_length, max_size_t_is_accepted)
{
	char max_value[64];
	Client c;

	snprintf(max_value, sizeof max_value, "%zu", SIZE_MAX);
	c = client_with_content_length("POST", "HTTP/1.1", max_value);

	cr_assert_eq(process_request(&c), PRO_REQ_SUCCESS);
	cr_assert_eq(c.content_length, SIZE_MAX);
}

Test(process_request_null, null_client_rejected)
{
	cr_assert_eq(process_request(NULL), PRO_REQ_NULL_CLIENT);
}

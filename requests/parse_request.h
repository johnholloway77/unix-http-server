#pragma once

#include <limits.h>
#include <time.h>
#include <unistd.h>
#include "../client_conn/connections.h"
#include "../logging/logging.h"
#include "./max_request.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif



#define TIME_RECEIVED_LENGTH 32

typedef enum Process_request_status {
    PRO_REQ_SUCCESS,
    PRO_REQ_NULL_CLIENT,
    PRO_REQ_METHOD_FAIL,
    PRO_REQ_VERSION_FAIL,
    PRO_REQ_CLENGTH_FAIL,
    PRO_REQ_TRAVERSE_FAIL,
    PRO_REQ_ENUM_COUNT
} Process_request_status;

typedef enum Parse_request_status {
    PAR_REQ_SUCCESS,
    PAR_REQ_NULL_CLIENT,
    PAR_REQ_NULL_REQUEST,
    PAR_REQ_BAD_REQUEST,
    PAR_REQ_ENUM_COUNT

} Parse_request_status;

/**
 * @brief HTTP methods recognised by the server.
 *
 * HTTP_METHOD_UNKNOWN is returned when the method token does not match any
 * known verb; parse_request() will set resp to RESP_501 in that case.
 */


/**
 * @brief HTTP protocol versions the server can receive.
 *
 * HTTP_VERSION_UNSUPPORTED is set for recognised-but-rejected versions
 * (0.9, 2.0); parse_request() will set resp to RESP_505.
 * HTTP_VERSION_UNKNOWN is set for completely unrecognisable version strings;
 * parse_request() will set resp to RESP_400.
 */


/**
 * @brief Parsed representation of an HTTP request line.
 *
 * Populated by parse_request() from the raw bytes in the client's in_buf.
 * The @c path field is always NUL-terminated and at most PATH_MAX-1 bytes.
 */
typedef struct Request
{
	enum http_method method; /**< HTTP verb */
	enum http_version version; /**< Protocol version */
	char time_received[TIME_RECEIVED_LENGTH]; /**< Wall-clock time headers
						   * were fully received
						   */
	char path[PATH_MAX]; /**< URI path component (no query string) */
} Request;

/**
 * @brief Parse the first line of an HTTP request and validate its fields.
 *
 * Expects @p buf to contain at least the full request headers terminated by
 * "\r\n\r\n".  Only the request line (up to the first "\r\n") is examined.
 *
 * On success, @p out is fully populated and 0 is returned.
 * On any error, @p resp is set to the appropriate error code, the relevant
 * fields of @p out are set to their UNKNOWN sentinel values, and -1 is
 * returned.
 *
 * Error mapping:
 *   - Missing/extra tokens, no "\r\n\r\n", non-'/' path → RESP_400
 *   - Path contains "../"                                → RESP_403
 *   - Path longer than PATH_MAX-1                       → RESP_414
 *   - Unrecognised method (incl. POST for now)          → RESP_501
 *   - Recognised-but-unsupported version (0.9, 2.0)    → RESP_505
 *
 * @param buf   Raw request bytes (need not be NUL-terminated)
 * @param len   Number of valid bytes in @p buf (should include "\r\n\r\n")
 * @param out   Output struct; partially written even on error
 * @param resp  Output error code; only meaningful when -1 is returned
 * @return      0 on success, -1 on parse/validation error
 */
int parse_request(const char *buf,
    size_t len,
    Request *out,
    enum client_response *resp,
    LogEntry *le);

Parse_request_status parse_header_2(Client *client);

Process_request_status process_request(Client *client);

#include "./parse_request.h"
#include "headers.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef __linux__
#include <bsd/string.h>
#else
#include <string.h>
#endif

static int supported_http_method(enum http_method method){

    switch (method){
        case HTTP_GET:
        case HTTP_POST:
        case HTTP_HEAD:
        case HTTP_PUT:
        case HTTP_DELETE:
            return 1;
            break;


        case HTTP_QUERY:
        // these cases should have already been checked
        // backup check
        case HTTP_METHOD_UNKNOWN:
        case NUM_HTTP_METHOD:
            return 0;
    }
    return 0;
}

static int support_http_version(enum http_version version){
    switch (version){
        case HTTP_1_0:
        case HTTP_1_1:
            return 1;
            break;
        default:
            return 0;
    }
}

static enum http_method
method_from_token(Slice method)
{
	if (0 == method.length) {
		return HTTP_METHOD_UNKNOWN;
	}

	if (method.length == 3 && (memcmp(method.start, "GET", method.length) == 0)) {
	    return HTTP_GET;
	}
	if (method.length == 4 && (memcmp(method.start, "POST", method.length) == 0)) {
	    return HTTP_POST;
	}
	if (method.length == 4 && (memcmp(method.start, "HEAD", method.length) == 0)) {
	    return HTTP_HEAD;
	}
	if (method.length == 3 && (memcmp(method.start, "PUT", method.length) == 0)) {
	    return HTTP_PUT;
	}
	if (method.length == 6 && (memcmp(method.start, "DELETE", method.length) == 0)) {
	    return HTTP_DELETE;
	}
	if (method.length == 5 && (memcmp(method.start, "QUERY", method.length) == 0)) {
	    return HTTP_QUERY;
	}


	return HTTP_METHOD_UNKNOWN; // use for junk/incorrect
};

static enum http_version
version_from_token(Slice version){
    if (0 == version.length) {
		return HTTP_VERSION_UNKNOWN;
	}

    if (version.length == 8 && (memcmp(version.start, "HTTP/1.0", version.length) == 0)){
        return HTTP_1_0;
    }
    if (version.length == 8 && (memcmp(version.start, "HTTP/1.1", version.length) == 0)){
        return HTTP_1_1;
    }
    if (version.length == 8 && (memcmp(version.start, "HTTP/0.9", version.length) == 0)){
        return HTTP_0_9;
    }
    if (version.length == 8 && (memcmp(version.start, "HTTP/2.0", version.length) == 0)){
        return HTTP_2;
    }
    if (version.length == 8 && (memcmp(version.start, "HTTP/3.0", version.length) == 0)){
        return HTTP_3;
    }

    return HTTP_VERSION_UNKNOWN; // use for junk/incorrect
}

static int
path_has_traversal(Slice path)
{
    char* traverse = NULL;
    const char* end = path.start + path.length;

	traverse = strnstr(path.start, "..", path.length);

      while (traverse){

        if (traverse == path.start){
            if (traverse + 2 == end){
                return 1;
            }

            if (*(traverse + 2) == '/'){
                return 1;
            }
        } else if (traverse + 2 == end){
            if (*(traverse -1) == '/'){
                return 1;
            }
        } else {
            if (*(traverse -1 ) == '/'){
                if (*(traverse + 2) == '/'){
                    return 1;
                }
            }
        }

    	traverse = strnstr(traverse + 2, "..", path.length);
    }


	return 0;
}

static size_t slice_to_size_t(Slice s, const char **endptr){

    size_t num = 0;
    char parsing_started = 0;
    char trailing_white = 0;

    (*endptr) = s.start;


    for (size_t i = 0; i < s.length; i++){
        unsigned char c = (unsigned char)s.start[i];
        if (isblank(c)){

            //check for trailing white space
            if (parsing_started){
                trailing_white = 1;
            }
            continue;
        }

        if (!isdigit(c)){
            (*endptr) = s.start;
            return 0;
        }
        parsing_started = 1;

        // check for number after trailing white
        if (trailing_white){
            (*endptr) = s.start;
            return 0;
        }

        size_t sum = num * 10 + (c - '0') ;

        if (sum < num) {
            // overflow!
            (*endptr) = s.start;
            return 0;
        } else {
            num = sum;
        }

        (*endptr)++;
    }

    return num;
}

Process_request_status process_request(Client *client) {
    Client *c = client;

    if (c == NULL){
        return PRO_REQ_NULL_CLIENT;
    }

    c->http_method = method_from_token(c->headers.request_fields.method);

    if (c->http_method == HTTP_METHOD_UNKNOWN){
        c->resp_val = RESP_400;
        return PRO_REQ_METHOD_FAIL;
    }

    if (!supported_http_method(c->http_method)){
        c->resp_val = RESP_501;
        return PRO_REQ_METHOD_FAIL;
    }

    c->http_version = version_from_token(c->headers.request_fields.version);
    if (c->http_version == HTTP_VERSION_UNKNOWN){
        c->resp_val = RESP_400;
        return PRO_REQ_VERSION_FAIL;
    }


    if (!support_http_version(c->http_version)){
        c->resp_val = RESP_505;
        return PRO_REQ_VERSION_FAIL;
    }

    if (path_has_traversal(c->headers.request_fields.uri)){
        c->resp_val = RESP_403;
        return PRO_REQ_TRAVERSE_FAIL;
    }

    if (c->http_method == HTTP_POST || c->http_method == HTTP_PUT){
        Slice s = c->headers.content_length;
        const char *endptr = NULL;

        if (s.start != NULL) {/*
            skip whitespaces and then check for a leading negative to prevent interer wrap around
        */
        while((s.length) && (*s.start == ' ' || *s.start == '\t') ){
            s.start++;
            s.length--;
        }

        if (s.length == 0){
            fprintf(stderr, "content length string is all whitespace\n");
            c->content_length = 0;
            c->resp_val = RESP_400;
            return PRO_REQ_CLENGTH_FAIL;
        }else if (*s.start != '-'){
            c->content_length = slice_to_size_t(s, &endptr);

            //check for malformed string...
            if (endptr == s.start){
                fprintf(stderr, "invalid content-length string.\n");

                c->resp_val = RESP_400;
                return PRO_REQ_CLENGTH_FAIL;
            }
        } else {
            fprintf(stderr, "content length is a negative number\n");
            c->content_length = 0;
            c->resp_val = RESP_400;
            return PRO_REQ_CLENGTH_FAIL;
        }

        } else {
            fprintf(stderr, "content length slice is null\n");
            c->content_length = 0;
            c->resp_val = RESP_400;
            return PRO_REQ_CLENGTH_FAIL;
        }
    }

    return 0;
}

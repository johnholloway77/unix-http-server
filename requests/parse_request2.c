#include "parse_request.h"
#ifdef __linux__
#include <bsd/string.h>
#else
#include <string.h>
#endif

#include "../client_conn/connections.h"
#include "../requests/headers.h"

static Header_type parse_header_type(const char **iterator, const char *line_end){
    Header_type return_h_type = H_ERROR;
    size_t remaining_line = line_end - *(iterator);

    const char *word_end = strnstr((*iterator), ":", remaining_line);
    if(word_end == NULL){
        return H_ERROR;
    }

    word_end++;

    size_t word_length = word_end - (*iterator);


    if (strncmp((*iterator), "Host:", word_length) == 0){
        return_h_type = H_HOST;
    } else if (strncmp((*iterator), "User-Agent:", word_length) == 0){
        return_h_type = H_USER_AGENT;
    } else if (strncmp((*iterator), "Accept:", word_length) == 0){
        return_h_type = H_ACCEPT;
    } else if (strncmp((*iterator), "Accept-Language:", word_length) == 0){
        return_h_type = H_ACCEPT_LANG;
    } else if (strncmp((*iterator), "Accept-Encoding:", word_length) == 0){
        return_h_type = H_ACCEPT_ENC;
    } else if (strncmp((*iterator), "Content-Length:", word_length) == 0){
        return_h_type = H_CONTENT_LEN;
    } else if (strncmp((*iterator), "Content-Type:", word_length) == 0){
        return_h_type = H_CONTENT_TYPE;
    } else if (strncmp((*iterator), "Connection:", word_length) == 0){
        return_h_type = H_CONNECTION;
    };

    while ((word_end < line_end) && (*word_end == ' ' || *word_end == '\t')){
        word_end++;
    }
    *iterator = word_end;

    return return_h_type;

}

int parse_request_line(Client *client){

    Slice *request_line = NULL;
    Request_fields *request_fields = NULL;

    request_line= &(*client).headers.request_line;
    if (NULL == request_line){
        fprintf(stderr, "Error, NULL request line\n");
        return 1;
    }

    request_fields = &(*client).headers.request_fields;
    if (NULL == request_fields){
        fprintf(stderr, "Error, NULL request field\n");
        return 1;
    }

    const char *iterator = NULL;
    const char *line_end = request_line->start + request_line->length;

    request_fields->method.start = request_line->start;

    if (*request_fields->method.start == ' ' || *request_fields->method.start == '\t'){
        fprintf(stderr, "invalid method\n");
        return 1;
    }

    // while ((*request_fields->method.start == ' ' || *request_fields->method.start == '\t')  && request_fields->method.start != line_end){
    //     request_fields->method.start++;
    // }

    iterator = strnstr(request_fields->method.start, " ", line_end - request_line->start);

    if(NULL == iterator){
        fprintf(stderr, "Unable to find first space in request\n");
        return 1;
    }

    request_fields->method.length = iterator - request_fields->method.start;


    while ((*iterator == ' ' || *iterator == '\t') && iterator != line_end){
        iterator++;
    }

    request_fields->uri.start = iterator;
    iterator = strnstr(request_fields->uri.start, " ", line_end - request_fields->uri.start);

    if(NULL == iterator){
        fprintf(stderr, "Unable to find second space in request\n");
        return 1;
    }
    request_fields->uri.length = iterator - request_fields->uri.start;

    while ((*iterator == ' ' || *iterator == '\t') && iterator != line_end){
        iterator++;
    }
    request_fields->version.start = iterator;

    // check for extra word in request line
    iterator = strnstr(request_fields->version.start, " ", line_end - request_fields->version.start);

    if(NULL != iterator){
        request_fields->version.length = iterator - request_fields->version.start;

        while ((*iterator == ' ' || *iterator == '\t') && iterator != line_end){
            iterator++;
        }
        if (iterator != line_end){
            fprintf(stderr, "Extra token found after HTTP version!!\n");
            return 1;
        }
    } else {
        request_fields->version.length = line_end - request_fields->version.start;
    }

    return 0;
}


Parse_request_status parse_header_2(Client *client){

    Client *c = client;

    if (NULL == c){
        // server fuckup. Should never get here
        c->resp_val = RESP_500;
        return PAR_REQ_NULL_CLIENT;
    }

    if (NULL == c->header_slice.start || 0 == c->header_slice.length){
        c->resp_val = RESP_400;
        return PAR_REQ_NULL_REQUEST;
    }

    const char *header_end = c->header_slice.start + c->header_slice.length;
    const char *iterator = c->header_slice.start;
    const char *line_end;

    line_end = strnstr(iterator, "\r\n", header_end - iterator);

    if(line_end != NULL){

        c->headers.request_line = (Slice){
            .start = iterator,
            .length = line_end - iterator,
        };

        if (0 != parse_request_line(c)){
            fprintf(stderr, "Something got fucked during the parse request function!\n");
        }

        iterator = line_end + 2;


        while (iterator < header_end) {

            Header_type h_type = H_ERROR;
            Slice *local_slice = NULL;

            size_t remaining = header_end - iterator;
            line_end = strnstr(iterator, "\r\n", remaining);

            if (line_end == NULL) {
                break;
            }
            if (iterator == line_end) {
                break;
            }


            h_type = parse_header_type(&iterator, line_end);


            switch (h_type){
                case H_HOST:
                    local_slice = &c->headers.host;
                    break;
                case H_USER_AGENT:
                    local_slice = &c->headers.user_agent;
                    break;
                case H_ACCEPT:
                    local_slice = &c->headers.accept;
                    break;
                case H_ACCEPT_LANG:
                    local_slice = &c->headers.accept_Language;
                    break;
                case H_ACCEPT_ENC:
                    local_slice = &c->headers.accept_encoding;
                    break;
                case H_CONTENT_LEN:
                    local_slice = &c->headers.content_length;
                    break;
                case H_CONTENT_TYPE:
                    local_slice = &c->headers.content_type;
                    break;
                case H_CONNECTION:
                    local_slice = &c->headers.connection;
                    break;
                case H_ERROR:
                default:
                    iterator = line_end + 2;
                    continue;
            }

            if (local_slice != NULL) {
                local_slice->start = iterator;
                local_slice->length = line_end - iterator;
            }

            iterator = line_end + 2;
        }
    } else {
        // no "\r\n" at end of line
        // malformed!!
        c->resp_val = RESP_400;
        return PAR_REQ_BAD_REQUEST;
    }

    return PAR_REQ_SUCCESS;
}

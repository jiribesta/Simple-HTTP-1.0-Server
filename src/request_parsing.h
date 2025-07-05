#ifndef REQUEST_PARSING_H
#define REQUEST_PARSING_H

#include <ctype.h>
#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "directory_resolution.h"
#include "response_sending.h"

// Decodes a URL-encoded string and checks for forbidden characters
int decode_URI(char *original_src, char *dest);

// Parses the URI and returns a status code
int URI_checker(char *request_URI, char *destination_path);

// Checks whether the request is HTTP version 0.9
int http09_check(char *original_request, char *usable_path);

// Checks whether the version's syntax is valid
int http_version_check(char *http_version);

// Returns 0 if everything was sent properly
int parse_request_and_send_response(const int sock, char *request);

#endif
#ifndef REQUEST_PARSING_H
#define REQUEST_PARSING_H

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "mime_types.h"
#include "socket_operations.h"

// Decodes a URL-encoded string and checks for forbidden characters
int decode_URI(char *original_src, char *dest);

// Parses the URI and returns a status code
int URI_checker(char *request_URI, char *destination_path, char *base_directory);

// Sends a file over a connection, also closes the file
int send_file(const int client_sock, FILE *open_file);

// Little function for qsort() inside serve_directory_listing()
int simple_compare(const void *a, const void *b);
// Builds a HTML document to send back as the body (Remember to free() afterwards)   
char *serve_directory_listing(char *absolute_path);

// Returns 0 if everything was sent properly
int parse_request_and_send_response(const int sock, char *request, char *base_dir);

#endif
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
#include "directory_resolution.h"
#include "mime_types.h"
#include "socket_operations.h"

// Send a response for status codes 4xx and 5xx
int handle_error_status_code(int error_status_code, int receiving_socket);

// Decodes a URL-encoded string and checks for forbidden characters
int decode_URI(char *original_src, char *dest);

// Parses the URI and returns a status code
int URI_checker(char *request_URI, char *destination_path, char *base_directory);

// Checks whether the request is HTTP version 0.9
int http09_check(char *original_request);

// Sends a file over a connection, also closes the file
int send_file(const int client_sock, FILE *open_file);

// Sends a GET or HEAD response for the requested path
int send_file_response(char *file_path, int connected_client_socket);

// Builds a HTML document to send back as the body (Remember to free() afterwards)   
char *serve_directory_listing(char *absolute_path);

// Sends a GET response containing the directory listing
int send_directory_listing_response(char *directory_path, int receiving_client_socket);

// Sends a response GET or HEAD method, depending on the head_method_check parameter
int get_or_head_method(char full_requested_path[], int client_socket, int head_method_check);

// Returns 0 if everything was sent properly
int parse_request_and_send_response(const int sock, char *request, char *base_dir);

#endif
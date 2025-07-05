#ifndef RESPONSE_SENDING_H
#define RESPONSE_SENDING_H

#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "mime_types.h"
#include "request_parsing.h"
#include "socket_operations.h"

// Send a response for status codes 4xx and 5xx
int handle_error_status_code(int error_status_code, int receiving_socket);

// Sends a file over a connection, also closes the file
int send_file(const int client_sock, FILE *open_file);

// Sends a GET or HEAD response for the requested path
int send_file_response(char *file_path, int connected_client_socket, int is_head_method);

// Builds a HTML document to send back as the body (Remember to free() afterwards)   
char *serve_directory_listing(char *absolute_path);

// Sends a GET response containing the directory listing
int send_directory_listing_response(char *directory_path, int receiving_client_socket);

// Sends a GET or HEAD method as a response, depending on the head_method_check parameter
int get_or_head_method(char full_requested_path[], int client_socket, int head_method_check);

#endif
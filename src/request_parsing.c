#include <ctype.h>
#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "directory_resolution.h"
#include "response_sending.h"


// Decodes a URL-encoded string and checks for forbidden characters
int decode_URI(char *original_src, char *dest)
{
    // Intermediary variable in case original_src == dest
    char src[strlen(original_src) + 1];
    strcpy(src, original_src);

    int i = 0, j = 0;
    while (src[i]) {
        if (src[i] == '%') {
            // Convert the next two characters from hex to decimal if they are valid hex digits
            if (src[i + 1] && isxdigit(src[i + 1]) && src[i + 2] && isxdigit(src[i + 2])) {
                char encoded_char[3] = {src[i + 1], src[i + 2], '\0'};
                long decoded_char = strtol(encoded_char, NULL, 16);

                // Check whether character is forbidden
                if (decoded_char < 32 || decoded_char == 127){
                    return -1;
                }

                dest[j++] = (char) decoded_char;
                i += 3; // Move past the encoded character
            } else {
                // If the format is incorrect, just copy the '%' character
                dest[j++] = src[i++];
            }
        } else if (src[i] == '+') {
            // Convert '+' to space
            dest[j++] = ' ';
            i++;
        } else {
            // Copy the character as is
            dest[j++] = src[i++];
        }
    }
    dest[j] = '\0'; // Null-terminate the destination string

    return 0;
}

// Parses the URI and returns a status code
int URI_checker(char *request_URI, char *destination_path)
{
    strtok(request_URI, "#"); // Seperate the path from the fragment
    strtok(request_URI, "?"); // Seperate the path from the query

    if (decode_URI(request_URI, request_URI)){ // If we found a forbidden URL-encoded character
        return 400;
    }

    if (strlen(request_URI) > (PATH_MAX - strlen(BASE_DIR))){ // If the path is too long
        return 414;
    }

    // Concatenate BASE_DIR and request_URI
    size_t destination_lenght = strlen(BASE_DIR) + strlen(request_URI) + 1;
    strncpy(destination_path, BASE_DIR, destination_lenght);
    strncat(destination_path, request_URI, destination_lenght);

    // Check if the path is inside our base directory
    if (strncmp(destination_path, BASE_DIR, strlen(BASE_DIR))){
        return 403;
    }

    // Check if the requested path exists and is allowed for access
    if (realpath(destination_path, destination_path) == NULL){
        if (errno == EACCES){
            return 403;
        } else if (errno == ENOENT){
            return 404;
        } else {
            perror("URI_checker - error resolving file path");
            return 500;
        }
    }

    return 200; // If everything is fine with the URI
}

// Checks whether the request is HTTP version 0.9
int http09_check(char *original_request, char *usable_path)
{
    char *line;
    char *uri_path;

    // Make a copy for the check (because strtok() modifies its input)
    char *request_copy = malloc((strlen(original_request) + 1));
    if (request_copy == NULL){
        perror("http09_check - error allocating memory");
        return 500;
    }
    strcpy(request_copy, original_request);

    // HTTP/0.9 check
    if ((line = strtok(request_copy, "\r\n")) == NULL){
        free(request_copy);
        return 400; // All versions of HTTP requests need to have at least one line
    }
    if (!strcmp(strtok(line, " "), "GET") && // Method is GET
        (uri_path = strtok(NULL, " ")) != NULL && // URI is present
        strtok(NULL, " ") == NULL && // HTTP version is not present
        strtok(NULL, "\r\n") == NULL){ // The total request is only one line

        int return_status_code = URI_checker(uri_path, usable_path); // Parse the URI for any problems
        free(request_copy);
        return return_status_code;
    }

    free(request_copy);
    return 0;
}

// Returns 0 if everything was sent properly
int parse_request_and_send_response(const int sock, char *request)
{
    char *line; // Split the request into lines with strtok()
    char *method, *uri_file_path, *version;
    char combined_path[PATH_MAX + 1]; // The path we'll pass into functions to work with a file/directory
    int return_status_code; // To store the return value of URI_checker()

    // Check for a HTTP/0.9 request
    if ((return_status_code = http09_check(request, combined_path)) == 200){

        FILE *open_file = fopen(combined_path, "rb");
        if (open_file == NULL) {
            perror("send_response - error opening file");
            return handle_error_status_code(500, sock);
        }
        // Send purely the response body
        return send_file(sock, open_file);

    } else if (return_status_code != 0){
        return handle_error_status_code(return_status_code, sock);
    }


    // Get the first line of the request and split it into method, uri_file_path and version
    if ((line = strtok(request, "\r\n")) == NULL){
        return handle_error_status_code(400, sock);
    }
    if ((method = strtok(line, " ")) == NULL || 
        (uri_file_path = strtok(NULL, " ")) == NULL || 
        (version = strtok(NULL, " ")) == NULL){

        return handle_error_status_code(400, sock);
    }


    // HTTP version check
    regex_t regex;
    int regexreturn;
    if (regcomp(&regex, "HTTP/[0-9]+\\.[0-9]+", REG_EXTENDED)){
        perror("parse_request - regex compilation failure");
        return handle_error_status_code(500, sock);
    }

    regexreturn = regexec(&regex, version, 0, NULL, 0);
    if (!regexreturn){ // version's syntax checks out
        regfree(&regex);
    } else if (regexreturn == REG_NOMATCH){ // version is malformed
        regfree(&regex);
        return handle_error_status_code(400, sock);
    } else { // There was an error checking version
        char regex_error_massage[100];
        regerror(regexreturn, &regex, regex_error_massage, sizeof(regex_error_massage));
        fprintf(stderr, "parse_request - regex match failed: %s\n", regex_error_massage);
        regfree(&regex);
        return handle_error_status_code(500, sock);
    }


    // URI check
    return_status_code = URI_checker(uri_file_path, combined_path);
    if (return_status_code != 200){
        return handle_error_status_code(return_status_code, sock);
    }


    // Method check
    if (!strcmp(method, "GET")){ // If method is GET
        return get_or_head_method(combined_path, sock, 0);
    } else if (!strcmp(method, "HEAD")){ // If the method is HEAD
        return get_or_head_method(combined_path, sock, 1);
    } else {
        perror("parse_request - unsupported method");
        return handle_error_status_code(501, sock);
    }
}
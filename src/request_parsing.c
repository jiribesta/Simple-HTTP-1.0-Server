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
int handle_error_status_code(int error_status_code, int receiving_socket)
{
    static char status_codes_to_responses[][2][256] = {
        {"400", "HTTP/1.0 400 Bad Request\r\n\r\n"},
        {"403", "HTTP/1.0 403 Forbidden\r\n\r\n"},
        {"404", "HTTP/1.0 404 Not Found\r\n\r\n"},
        {"414", "HTTP/1.0 414 URI Too Long\r\n\r\n"},
        {"500", "HTTP/1.0 500 Internal Server Error\r\n\r\n"},
        {"501", "HTTP/1.0 501 Not Implemented\r\n\r\n"},
        {"", ""} // Last one must be an empty string
    };

    // Cycle through the list until we hit an emptry string
    for (int i = 0; status_codes_to_responses[i][0][0] != '\0'; i++){
        if (atoi(status_codes_to_responses[i][0]) == error_status_code){

            size_t response_lenght = strlen(status_codes_to_responses[i][1]);
            return sendall(receiving_socket, status_codes_to_responses[i][1], &response_lenght);
        }
    }
    fprintf(stderr, "handle_error_status_code - unknown status code");
    return -1;
}

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

// Sends a file over a connection, also closes the file
int send_file(const int client_sock, FILE *open_file)
{

    // Allocate initial memory for the buffer
    size_t buffer_size = 4096;
    char *file_buffer = malloc(buffer_size);
    if (file_buffer == NULL) {
        perror("send_file - error allocating memory");
        fclose(open_file);
        return -1;
    }

    // Read and send the file in chunks
    size_t bytes_read;
    while ((bytes_read = fread(file_buffer, 1, buffer_size, open_file)) > 0) {
        if (sendall(client_sock, file_buffer, &bytes_read)){
            fclose(open_file);
            free(file_buffer);
            return -1;
        }
    }

    if (ferror(open_file)) {
        perror("send_file - error reading file");
        fclose(open_file);
        free(file_buffer);
        return -1;
    }

    fclose(open_file);
    free(file_buffer);
    return 0;
}

// Sends a GET or HEAD response for the requested path
int send_file_response(char *file_path, int connected_client_socket, int is_head_method)
{
    FILE *requested_file = fopen(file_path, "rb");
    if (requested_file == NULL) {
        perror("send_response - error opening file");
        return handle_error_status_code(500, connected_client_socket);
    }

    // Get the file size
    size_t file_size;
    if (fseek(requested_file, 0, SEEK_END) || 
        (file_size = ftell(requested_file)) == -1L || 
        fseek(requested_file, 0, SEEK_SET)){

        perror("send_response - error getting file size");
        fclose(requested_file);
        return handle_error_status_code(500, connected_client_socket);
    }

    const char *file_MIME_type = get_MIME_type(file_path);

    // Get enough room for the response beginning
    size_t response_beginning_size = snprintf(NULL, 0,
                                    "HTTP/1.0 200 OK\r\n"
                                    "Content-Type: %s\r\n"
                                    "Content-Length: %ld\r\n"
                                    "Connection: close\r\n\r\n",
                                    file_MIME_type, file_size);

    char *response_beginning = malloc(response_beginning_size + 1);
    if (response_beginning == NULL){
        perror("send_response - error allocating memory");
        fclose(requested_file);
        return handle_error_status_code(500, connected_client_socket);
    }

    // Build the response beginning
    snprintf(response_beginning, response_beginning_size + 1,
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %ld\r\n"
            "Connection: close\r\n\r\n",
            file_MIME_type, file_size);
    
    size_t response_beginning_lenght = strlen(response_beginning);

    // First try to send the response beginning
    if (sendall(connected_client_socket, response_beginning, &response_beginning_lenght) < 0){
        fclose(requested_file);
        free(response_beginning);
        return handle_error_status_code(500, connected_client_socket);
    }
    free(response_beginning);

    if (is_head_method){ // If the method is HEAD, don't send the body (file)
        fclose(requested_file);
        return 0;
    }

    // Send the rest of the response
    return send_file(connected_client_socket, requested_file);
}

// Little function for qsort() inside serve_directory_listing()
static int simple_compare(const void *a, const void *b){ return strcasecmp(*(const char **)a, *(const char **)b); }
// Builds a HTML document to send back as the body (Remember to free() afterwards)
char *serve_directory_listing(char *absolute_path)
{
    DIR *this_dir;
    struct dirent *dir_entry;
    char **dir_entries;
    size_t entry_count = 0;
    size_t max_entries = 8;

    size_t body_size = 4096;
    size_t chars_written = 0;
    char *response_body;
    char *temp_response_body;

    if ((dir_entries = malloc(max_entries * sizeof(char *))) == NULL){
        perror("serve_directory - error allocating memory");
        return NULL;
    }

    if ((this_dir = opendir(absolute_path)) == NULL){
        perror("serve_directory - error opening directory");
        free(dir_entries);
        return NULL;
    }

    // Fill the list of directory entries
    while ((dir_entry = readdir(this_dir))){
        if (entry_count >= max_entries){
            max_entries *= 2;
            char **temp_dir_entries = realloc(dir_entries, max_entries * sizeof(char *));
            if (temp_dir_entries == NULL){
                perror("serve_directory - error reallocating memory");
                free(dir_entries);
                closedir(this_dir);
                return NULL;
            }
            dir_entries = temp_dir_entries;
        }

        // If the entry is a directory, append '/'
        char entry_name[strlen(dir_entry->d_name) + 2];
        strcpy(entry_name, dir_entry->d_name);
        strcat(entry_name, ((dir_entry->d_type == DT_DIR) ? "/" : ""));

        // Store the entry's name in dir_entries
        if ((dir_entries[entry_count] = strdup(entry_name)) == NULL){
            perror("serve_directory - error duplicating directory entry");
            free(dir_entries);
            closedir(this_dir);
            return NULL;
        }
        entry_count++;
    }

    closedir(this_dir);

    // Sort the entries
    qsort(dir_entries, entry_count, sizeof(char *), simple_compare);

    if ((response_body = malloc(body_size)) == NULL){
        perror("serve_directory - error allocating memory");
        free(dir_entries);
        return NULL;
    }

    // Make the printable path
    char relative_path[strlen(absolute_path) + 2];
    strcpy(relative_path, absolute_path + strlen(BASE_DIR));
    // Append a '/' if there isn't one at the end
    if (!(relative_path[strlen(relative_path) - 1] == '/')) strcat(relative_path, "/");

    // Create beginning of the listing
    char listing_beginning[strlen("<html><head><title>Directory listing for </title></head>\n"
                                  "<body><h1>Directory listing for </h1><ul>\n")
                                   + 2*strlen(relative_path) + 1];
    snprintf(listing_beginning, sizeof(listing_beginning), 
                          "<html><head><title>Directory listing for %s</title></head>\n"
                          "<body><h1>Directory listing for %s</h1><ul>\n", 
                           relative_path, relative_path);

    if (body_size < sizeof(listing_beginning)){
        temp_response_body = realloc(response_body, sizeof(listing_beginning));
        if (temp_response_body == NULL){
            perror("serve_directory - error reallocating memory");
            free(response_body);
            return NULL;
        }
        response_body = temp_response_body;
        body_size = sizeof(listing_beginning);
    }
    chars_written += snprintf(response_body, body_size, "%s", listing_beginning);

    // Build the main body of the listing
    for (size_t i = 0; i < entry_count; i++){
        // Skip . and ..
        if (!strcmp(dir_entries[i], "./") || !strcmp(dir_entries[i], "../")){
            free(dir_entries[i]);
            continue;
        }

        // Build and append list_entry to response_body
        char list_entry[strlen("<li><a href=\"\"></a></li>\n") 
                        + 2*strlen(dir_entries[i]) 
                        + 1];
        snprintf(list_entry, sizeof(list_entry), "<li><a href=\"%s\">%s</a></li>\n", 
                                                   dir_entries[i],
                                                   dir_entries[i]);

        if (body_size - chars_written < sizeof(list_entry)){
            temp_response_body = realloc(response_body, body_size + sizeof(list_entry));
            if (temp_response_body == NULL){
                free(dir_entries[i]);
                continue;
            }
            response_body = temp_response_body;
            body_size += sizeof(list_entry);
        }
        chars_written += snprintf(response_body + chars_written, body_size - chars_written, "%s", list_entry);

        free(dir_entries[i]);
    }

    // Close off the listing
    char listing_end[strlen("</ul></body></html>") + 1];
    snprintf(listing_end, sizeof(listing_end), "</ul></body></html>");
    if (body_size - chars_written < sizeof(listing_end)){
        temp_response_body = realloc(response_body, body_size + sizeof(listing_end));
        if (temp_response_body == NULL){
            perror("serve_directory - error reallocating memory");
            free(response_body);
            free(dir_entries);
            return NULL;
        }
        response_body = temp_response_body;
        body_size += sizeof(listing_end);
    }
    snprintf(response_body + chars_written, body_size - chars_written, "%s", listing_end);

    free(dir_entries);
    return response_body;
}

// Sends a GET response containing the directory listing
int send_directory_listing_response(char *directory_path, int receiving_client_socket)
{
    char *entity_body = serve_directory_listing(directory_path);
    if (entity_body == NULL){
        return handle_error_status_code(500, receiving_client_socket);
    }

    // Make enough room for the response and paste in the body
    size_t response_size = snprintf(NULL, 0, 
                                    "HTTP/1.0 200 OK\r\n"
                                    "Content-Type: text/html\r\n"
                                    "Content-Length: %lu\r\n"
                                    "Connection: close\r\n\r\n"
                                    "%s",
                                    strlen(entity_body), entity_body) + 1;

    char *response = malloc(response_size);
    if (response == NULL){
        perror("send_response - error allocating memory");
        return handle_error_status_code(500, receiving_client_socket);
    }

    snprintf(response, response_size, 
                "HTTP/1.0 200 OK\r\n"
                "Content-Type: text/html\r\n"
                "Content-Length: %lu\r\n"
                "Connection: close\r\n\r\n"
                "%s",
                strlen(entity_body), entity_body);
    
    free(entity_body);

    // Send the complete response
    int sendallretval = sendall(receiving_client_socket, response, &response_size);
    free(response);
    return sendallretval;
}

// Sends a response GET or HEAD method, depending on the head_method_check parameter
int get_or_head_method(char full_requested_path[], int client_socket, int head_method_check)
{
    // Check whether the requested path is a file or directory
    struct stat full_requested_path_stat;
    if (stat(full_requested_path, &full_requested_path_stat)) {
        perror("parse_request - error filling stat object");
        return handle_error_status_code(500, client_socket);
    }

    if (!S_ISDIR(full_requested_path_stat.st_mode)){ // If it's a file
        return send_file_response(full_requested_path, client_socket, head_method_check);
    }

    // If it's a directory, we'll try to serve index.html from it first
    if ((strlen(full_requested_path) + strlen("/index.html")) > PATH_MAX){
        return handle_error_status_code(414, client_socket);
    }
    strcat(full_requested_path, "/index.html");

    // If there's a problem getting index.html, we'll try to serve the contents of the directory instead
    if (realpath(full_requested_path, full_requested_path) == NULL){
        full_requested_path[strlen(full_requested_path) - strlen("index.html")] = '\0';

        return send_directory_listing_response(full_requested_path, client_socket);
    }    
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
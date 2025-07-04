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
int URI_checker(char *request_URI, char *destination_path, char *base_directory)
{
    strtok(request_URI, "#"); // Seperate the path from the fragment
    strtok(request_URI, "?"); // Seperate the path from the query

    if (decode_URI(request_URI, request_URI)){ // If we found a forbidden URL-encoded character
        return 400;
    }

    if (strlen(request_URI) > (PATH_MAX - strlen(base_directory))){ // If the path is too long
        return 414;
    }

    // Concatenate base_directory and request_URI
    size_t destination_lenght = strlen(base_directory) + strlen(request_URI) + 1;
    strncpy(destination_path, base_directory, destination_lenght);
    strncat(destination_path, request_URI, destination_lenght);

    // Check if the path is inside our base directory
    if (strncmp(destination_path, base_directory, strlen(base_directory))){
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

// Little function for qsort() inside serve_directory_listing()
int simple_compare(const void *a, const void *b){ return strcasecmp(*(const char **)a, *(const char **)b); }
// Builds a HTML document to send back as the body (Remember to free() afterwards)
char *serve_directory_listing(char *absolute_path, char *this_directory)
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
    strcpy(relative_path, absolute_path + strlen(this_directory));
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

// Returns 0 if everything was sent properly
int parse_request_and_send_response(const int sock, char *request, char *base_dir)
{
    char *line; // Split the request into lines with strtok()
    char *method, *uri_file_path, *version;
    char combined_path[PATH_MAX + 1]; // The path we'll pass into function to work with a file/directory
    int return_status_code; // To store the return value of URI_checker()
    int head_method_check = 0; // Will be 1 if request method is HEAD
    long file_size = 0;
    size_t response_lenght;


    // Make a copy for the HTTP/0.9 check (because strtok() modifies its input)
    char *request_copy = malloc((strlen(request) + 1));
    if (request_copy == NULL){
        perror("parse_request - error allocating memory");
        response_lenght = strlen("HTTP/1.0 500 Internal Server Error\r\n\r\n");
        return sendall(sock, "HTTP/1.0 500 Internal Server Error\r\n\r\n", &response_lenght);
    }
    strcpy(request_copy, request);

    // HTTP/0.9 check 
    if ((line = strtok(request_copy, "\r\n")) == NULL){ // All versions of HTTP requests need to have at least one line
        free(request_copy);
        response_lenght = strlen("HTTP/1.0 400 Bad Request\r\n\r\n");
        return sendall(sock, "HTTP/1.0 400 Bad Request\r\n\r\n", &response_lenght);
    }
    if (!strcmp(strtok(line, " "), "GET") && // Method is GET
        (uri_file_path = strtok(NULL, " ")) != NULL && // URI is present
        strtok(NULL, " ") == NULL && // HTTP version is not present
        strtok(NULL, "\r\n") == NULL){ // The total request is only one line

        return_status_code = URI_checker(uri_file_path, combined_path, base_dir); // Parse the URI for any problems
        if (return_status_code == 200){
            free(request_copy);

            FILE *open_file = fopen(combined_path, "rb");
            if (open_file == NULL) {
                perror("send_response - error opening file");
                response_lenght = strlen("HTTP/1.0 500 Internal Server Error\r\n\r\n");
                return sendall(sock, "HTTP/1.0 500 Internal Server Error\r\n\r\n", &response_lenght);
            }

            // Send purely the response body
            return send_file(sock, open_file);

        } else if (return_status_code == 500){
            free(request_copy);
            response_lenght = strlen("HTTP/1.0 500 Internal Server Error\r\n\r\n");
            return sendall(sock, "HTTP/1.0 500 Internal Server Error\r\n\r\n", &response_lenght);
        } else {
            free(request_copy);
            response_lenght = strlen("HTTP/1.0 400 Bad Request\r\n\r\n");
            return sendall(sock, "HTTP/1.0 400 Bad Request\r\n\r\n", &response_lenght);
        }
    }
    free(request_copy);


    // Get the first line of the request and split it into method, uri_file_path and version
    if ((line = strtok(request, "\r\n")) == NULL){
        response_lenght = strlen("HTTP/1.0 400 Bad Request\r\n\r\n");
        return sendall(sock, "HTTP/1.0 400 Bad Request\r\n\r\n", &response_lenght);
    }
    if ((method = strtok(line, " ")) == NULL || 
        (uri_file_path = strtok(NULL, " ")) == NULL || 
        (version = strtok(NULL, " ")) == NULL){

        response_lenght = strlen("HTTP/1.0 400 Bad Request\r\n\r\n");
        return sendall(sock, "HTTP/1.0 400 Bad Request\r\n\r\n", &response_lenght);
    }


    // HTTP version check
    regex_t regex;
    int regexreturn;
    if (regcomp(&regex, "HTTP/[0-9]+\\.[0-9]+", REG_EXTENDED)){
        perror("parse_request - regex compilation failure");
        response_lenght = strlen("HTTP/1.0 500 Internal Server Error\r\n\r\n");
        return sendall(sock, "HTTP/1.0 500 Internal Server Error\r\n\r\n", &response_lenght);
    }

    regexreturn = regexec(&regex, version, 0, NULL, 0);
    if (!regexreturn){ // version's syntax checks out
        regfree(&regex);
    } else if (regexreturn == REG_NOMATCH){ // version is malformed
        regfree(&regex);
        response_lenght = strlen("HTTP/1.0 400 Bad Request\r\n\r\n");
        return sendall(sock, "HTTP/1.0 400 Bad Request\r\n\r\n", &response_lenght);
    } else { // There was an error checking version
        char regex_error_massage[100];
        regerror(regexreturn, &regex, regex_error_massage, sizeof(regex_error_massage));
        fprintf(stderr, "parse_request - regex match failed: %s\n", regex_error_massage);
        regfree(&regex);
        response_lenght = strlen("HTTP/1.0 500 Internal Server Error\r\n\r\n");
        return sendall(sock, "HTTP/1.0 500 Internal Server Error\r\n\r\n", &response_lenght);
    }


    // Method check
    if (!strcmp(method, "HEAD")){ // If method is HEAD
        head_method_check = 1;
    } else if (strcmp(method, "GET")){ // If the method is not GET
        perror("parse_request - unsupported method");
        response_lenght = strlen("HTTP/1.0 501 Not Implemented\r\n\r\n");
        return sendall(sock, "HTTP/1.0 501 Not Implemented\r\n\r\n", &response_lenght);
    } // If the method is GET, we continue


    // URI check
    return_status_code = URI_checker(uri_file_path, combined_path, base_dir);
    switch (return_status_code){ // Check for non-200 codes
        case 400:
            response_lenght = strlen("HTTP/1.0 400 Bad Request\r\n\r\n");
            return sendall(sock, "HTTP/1.0 400 Bad Request\r\n\r\n", &response_lenght);
        case 403:
            response_lenght = strlen("HTTP/1.0 403 Forbidden\r\n\r\n");
            return sendall(sock, "HTTP/1.0 403 Forbidden\r\n\r\n", &response_lenght);
        case 404:
            response_lenght = strlen("HTTP/1.0 404 Not Found\r\n\r\n");
            return sendall(sock, "HTTP/1.0 404 Not Found\r\n\r\n", &response_lenght);
        case 414:
            response_lenght = strlen("HTTP/1.0 414 URI Too Long\r\n\r\n");
            return sendall(sock, "HTTP/1.0 414 URI Too Long\r\n\r\n", &response_lenght);
        case 500:
            response_lenght = strlen("HTTP/1.0 500 Internal Server Error\r\n\r\n");
            return sendall(sock, "HTTP/1.0 500 Internal Server Error\r\n\r\n", &response_lenght);
    }

    
    // Check whether the requested path is a file or directory
    struct stat combined_path_stat;
    if (stat(combined_path, &combined_path_stat)) {
        perror("parse_request - error filling stat object");
        response_lenght = strlen("HTTP/1.0 500 Internal Server Error\r\n\r\n");
        return sendall(sock, "HTTP/1.0 500 Internal Server Error\r\n\r\n", &response_lenght);
    }

    // If it's a directory, we'll try to serve index.html from it first
    if (S_ISDIR(combined_path_stat.st_mode)){
        if ((strlen(combined_path) + strlen("/index.html")) > PATH_MAX){
            response_lenght = strlen("HTTP/1.0 414 URI Too Long\r\n\r\n");
            return sendall(sock, "HTTP/1.0 414 URI Too Long\r\n\r\n", &response_lenght);
        }
        strcat(combined_path, "/index.html");

        // If there's a problem getting index.html, we'll try to serve the contents of the directory instead
        if (realpath(combined_path, combined_path) == NULL){
            combined_path[strlen(combined_path) - strlen("index.html")] = '\0';

            char *entity_body = serve_directory_listing(combined_path, base_dir);
            if (entity_body == NULL){
                response_lenght = strlen("HTTP/1.0 500 Internal Server Error\r\n\r\n");
                return sendall(sock, "HTTP/1.0 500 Internal Server Error\r\n\r\n", &response_lenght);
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
                response_lenght = strlen("HTTP/1.0 500 Internal Server Error\r\n\r\n");
                return sendall(sock, "HTTP/1.0 500 Internal Server Error\r\n\r\n", &response_lenght);
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
            int sendallretval = sendall(sock, response, &response_size);
            free(response);
            return sendallretval;
        }
    }

    // If we made it here, we should have a valid file to send
    FILE *requested_file = fopen(combined_path, "rb");
    if (requested_file == NULL) {
        perror("send_response - error opening file");
        response_lenght = strlen("HTTP/1.0 500 Internal Server Error\r\n\r\n");
        return sendall(sock, "HTTP/1.0 500 Internal Server Error\r\n\r\n", &response_lenght);
    }

    // Get the file size
    if (fseek(requested_file, 0, SEEK_END) || 
        (file_size = ftell(requested_file)) == -1L || 
        fseek(requested_file, 0, SEEK_SET)){

        perror("send_response - error getting file size");
        fclose(requested_file);
        response_lenght = strlen("HTTP/1.0 500 Internal Server Error\r\n\r\n");
        return sendall(sock, "HTTP/1.0 500 Internal Server Error\r\n\r\n", &response_lenght);
    }

    const char *file_MIME_type = get_MIME_type(combined_path);

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
        response_lenght = strlen("HTTP/1.0 500 Internal Server Error\r\n\r\n");
        return sendall(sock, "HTTP/1.0 500 Internal Server Error\r\n\r\n", &response_lenght);
    }

    // Build the response beginning
    snprintf(response_beginning, response_beginning_size + 1,
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %ld\r\n"
            "Connection: close\r\n\r\n",
            file_MIME_type, file_size);
    
    response_lenght = strlen(response_beginning);

    // First try to send the response beginning
    if (sendall(sock, response_beginning, &response_lenght)){
        fclose(requested_file);
        free(response_beginning);
        response_lenght = strlen("HTTP/1.0 500 Internal Server Error\r\n\r\n");
        return sendall(sock, "HTTP/1.0 500 Internal Server Error\r\n\r\n", &response_lenght);
    }
    free(response_beginning);

    if (head_method_check){ // If the method is HEAD, don't send the body (file)
        fclose(requested_file);
        return 0;
    }

    // Send the rest of the response
    return send_file(sock, requested_file);
}
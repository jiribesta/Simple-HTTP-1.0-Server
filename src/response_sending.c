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

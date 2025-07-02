#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BACKLOG 10 // How many pending connections queue will hold
#define TIMEOUT 3 // How many seconds we wait for the client
#define DEFAULT_BUFFER_SIZE 4096

void check_valid_port(char *portstr);
void resolve_dir(char *dirpath);
void *get_in_addr(struct sockaddr *sa);
int get_listener(char *port);
int sendall(const int send_fd, char *send_buf, size_t *send_buf_len);
int decode_URI(char *original_src, char *dest);
int URI_checker(char *request_URI, char *destination_path);
const char *get_MIME_type(const char *filename); 
int send_file(const int client_sock, FILE *open_file);
char *serve_directory_listing(char *absolute_path);   
int parse_request_and_send_response(const int sock, char *request);

char BASE_DIR[PATH_MAX + 1]; // Global variable for our directory

int main(int argc, char *argv[])
{
    if (argc != 3){
        fprintf(stderr,"Usage: %s <port> <directory>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int listener, new_fd; // Listen on listener, new connection on new_fd
    struct sockaddr_storage client_addr; // Client's address information
    socklen_t addrlen;
    char buf[DEFAULT_BUFFER_SIZE]; // Buffer for client's request
    char addr_str[INET6_ADDRSTRLEN];
    struct pollfd recv_poll_fd[1]; // pollfd object that'll contain the recv()ing socket
    recv_poll_fd[0].events = POLLIN | POLLHUP; // The events we want to check for
    int recv_poll_rv;

    // Check if the port is valid, and resolve the directory if it's also valid
    check_valid_port(argv[1]);
    resolve_dir(argv[2]);

    // Set up and get a listening socket
    listener = get_listener(argv[1]);

    if (listener == -1){
        fprintf(stderr, "main - error getting listening socket\n");
        return EXIT_FAILURE;
    }

    printf("Server: Ready for connections...\n");

    for(;;){ // Main loop
        addrlen = sizeof(client_addr);
        new_fd = accept(listener, (struct sockaddr *)&client_addr, &addrlen);
        if (new_fd == -1){
            perror("main - accept");
            continue;
        }

        // Print out who's connect()ing to us
        inet_ntop(client_addr.ss_family,
                  get_in_addr((struct sockaddr *)&client_addr),
                  addr_str, 
                  sizeof addr_str);
        printf("\nServer: Incoming connection from %s on socket %d\n", addr_str, new_fd);

        // Wait for some data on the socket
        recv_poll_fd[0].fd = new_fd;
        recv_poll_rv = poll(recv_poll_fd, 1, TIMEOUT*1000);

        // Check for error or timeout
        if (recv_poll_rv < 0){
            perror("main - poll");

        } else if (recv_poll_rv == 0) {
            fprintf(stderr, "main - timeout reached on socket %d\n", new_fd);

        } else {
            if (recv_poll_fd[0].revents & POLLHUP){ // If the client hung up
                printf("Connection on socket %d closed by client\n", new_fd);

            // There is data to be recv()ed on the socket
            } else {
                int nbytes = recv(new_fd, buf, sizeof(buf) - 1, 0);

                // Check for errors on recv()
                if (nbytes < 0){
                    perror("main - recv");

                } else { // We got some data from a client
                    buf[nbytes] = '\0';

                    if (parse_request_and_send_response(new_fd, buf) == 0){
                        printf("Response sent succesfully on socket %d\n", new_fd);
                    }
                }
            }
        }

        close(new_fd);
    }
    
    return 0;
}


// Checks if <port> can be used as an actual port
void check_valid_port(char *portstr)
{
    // Make a copy for error messages
    char portstr_copy[strlen(portstr) + 1];
    strcpy(portstr_copy, portstr);

    // Check for empty string
    if (*portstr == '\0') {
        printf("'%s' is not a valid port number.\n", portstr_copy);
        exit(EXIT_FAILURE);
    }
    
    // Iterate through each character in the string
    while (*portstr) {
        if (!isdigit(*portstr)) {
            printf("'%s' is not a valid port number.\n", portstr_copy);
            exit(EXIT_FAILURE);
        }
        portstr++;
    }

    // Check if it's in the valid port range
    if (atoi(portstr) < 0 || atoi(portstr) > 65535){
        printf("'%s' is not a valid port number.\n", portstr_copy);
        exit(EXIT_FAILURE);
    }
}

// Resolves our base <directory>
void resolve_dir(char *dirpath)
{
    char resolved_path[PATH_MAX + 1];

    // Resolve the directory path
    if (realpath(dirpath, resolved_path) == NULL) {
        perror("resolve_dir - error resolving file path");
        exit(EXIT_FAILURE);
    }

    // Validate the directory and check if it's not just a file
    struct stat path_stat;
    if (stat(resolved_path, &path_stat)) {
        perror("resolve_dir - error filling stat object");
        exit(EXIT_FAILURE);
    }

    if (!S_ISDIR(path_stat.st_mode)) {
        fprintf(stderr, "%s is not a directory\n", resolved_path);
        exit(EXIT_FAILURE);
    }

    // Paste the path into our global variable
    strcpy(BASE_DIR, resolved_path);
}

// Gets sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// Returns a listening socket
int get_listener(char *port)
{
    int listener;     // Listening socket descriptor
    int yes=1;        // For setsockopt() SO_REUSEADDR
    int retval;
    struct addrinfo hints, *ai, *p;

    // Get a socket and bind it
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP, not UDP
    hints.ai_flags = AI_PASSIVE; // This machine's address

    if ((retval = getaddrinfo(NULL, port, &hints, &ai)) != 0) {
        fprintf(stderr, "get_listener: %s\n", gai_strerror(retval));
        return -1;
    }
    
    for(p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) { 
            continue;
        }
        
        // Lose the "address already in use" error
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }

        break;
    }

    // If we got here, it means we didn't get bound
    if (p == NULL) {
        return -1;
    }

    freeaddrinfo(ai); // All done with this

    if (listen(listener, BACKLOG) == -1) {
        return -1;
    }

    return listener;
}

// send()s as much of the buffer as possible
int sendall(const int send_fd, char *send_buf, size_t *send_buf_len)
{
    int total = 0; // How many bytes we've sent
    int bytesleft = *send_buf_len; // How many we have left to send
    int sent;
    int send_poll_rv;
    
    // Put the file descriptor into a pollfd object
    struct pollfd send_poll_fd[1];
    send_poll_fd[0].fd = send_fd;
    send_poll_fd[0].events = POLLOUT | POLLHUP;


    while(total < *send_buf_len) {
        send_poll_rv = poll(send_poll_fd, 1, TIMEOUT*1000);

        // Check for error or timeout
        if (send_poll_rv < 0){
            perror("sendall - poll");
            sent = -1;
            break;

        } else if (send_poll_rv == 0) {
            fprintf(stderr, "sendall - timeout reached on socket %d\n", send_fd);
            sent = -1;
            break;

        } else {
            if (send_poll_fd[0].revents & POLLHUP){
                printf("Connection on socket %d closed by client\n", send_fd);
                sent = -1;
                break;
            }

            // We got here if data is ready to be sent
            sent = send(send_fd, send_buf+total, bytesleft, MSG_NOSIGNAL);
            if (sent < 0){
                perror("sendall - send");
                break;
            }
            total += sent;
            bytesleft -= sent;
        }
    }

    *send_buf_len = total; // Return number actually sent here

    return sent==-1?-1:0; // return -1 on failure, 0 on success
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

// Returns a file's MIME type based on its extensions (extend as needed)
const char *get_MIME_type(const char *filename)
{
    const char* ext = strrchr(filename, '.');
    if (ext){

        if (!strcmp(ext, ".aac")) return "audio/aac";
        if (!strcmp(ext, ".abw")) return "application/x-abiword";
        if (!strcmp(ext, ".apng")) return "image/apng";
        if (!strcmp(ext, ".arc")) return "application/x-freearc";
        if (!strcmp(ext, ".avif")) return "image/avif";
        if (!strcmp(ext, ".avi")) return "x-msvideo";
        if (!strcmp(ext, ".azw")) return "application/vnd.amazon.ebook";
        if (!strcmp(ext, ".bash") || !strcmp(ext, ".sh")) return "application/x-sh";
        if (!strcmp(ext, ".bin")) return "application/octet-stream";
        if (!strcmp(ext, ".bmp")) return "image/bmp";
        if (!strcmp(ext, ".bz")) return "application/x-bzip";
        if (!strcmp(ext, ".bz2")) return "application/x-bzip2";
        if (!strcmp(ext, ".c")) return "text/x-c";
        if (!strcmp(ext, ".cda")) return "application/x-cdf";
        if (!strcmp(ext, ".class")) return "application/java-vm";
        if (!strcmp(ext, ".cpp")) return "text/x-c++";
        if (!strcmp(ext, ".csh")) return "application/x-csh";
        if (!strcmp(ext, ".css")) return "text/css";
        if (!strcmp(ext, ".csv")) return "text/csv";
        if (!strcmp(ext, ".doc")) return "application/msword";
        if (!strcmp(ext, ".docx")) return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
        if (!strcmp(ext, ".eot")) return "application/vnd.ms-fontobject";
        if (!strcmp(ext, ".epub")) return "application/epub+zip";
        if (!strcmp(ext, ".go")) return "text/x-go";
        if (!strcmp(ext, ".gif")) return "image/gif";
        if (!strcmp(ext, ".gz")) return "application/gzip";
        if (!strcmp(ext, ".h")) return "text/x-c";
        if (!strcmp(ext, ".hpp")) return "text/x-c++";
        if (!strcmp(ext, ".htm") || !strcmp(ext, ".html")) return "text/html";
        if (!strcmp(ext, ".ico")) return "image/vnd.microsoft.icon";
        if (!strcmp(ext, ".ics")) return "text/calendar";
        if (!strcmp(ext, ".java")) return "text/x-java-source";
        if (!strcmp(ext, ".jar")) return "application/java-archive";
        if (!strcmp(ext, ".jpeg") || !strcmp(ext, ".jpeg")) return "image/jpeg";
        if (!strcmp(ext, ".js") || !strcmp(ext, ".mjs")) return "text/javascript";
        if (!strcmp(ext, ".json")) return "application/json";
        if (!strcmp(ext, ".jsonld")) return "application/ld+json";
        if (!strcmp(ext, ".kdbx")) return "application/x-keepass";
        if (!strcmp(ext, ".kt") || !strcmp(ext, ".kts")) return "text/x-kotlin";
        if (!strcmp(ext, ".md")) return "text/markdown";
        if (!strcmp(ext, ".mid")) return "audio/midi";
        if (!strcmp(ext, ".midi")) return "audio/x-midi";
        if (!strcmp(ext, ".mp3")) return "audio/mpeg";
        if (!strcmp(ext, ".mp4")) return "video/mp4";
        if (!strcmp(ext, ".mpeg")) return "video/mpeg";
        if (!strcmp(ext, ".mpkg")) return "application/vnd.apple.installer+xml";
        if (!strcmp(ext, ".odp")) return "application/vnd.oasis.opendocument.presentation";
        if (!strcmp(ext, ".ods")) return "application/vnd.oasis.opendocument.spreadsheet";
        if (!strcmp(ext, ".odt")) return "application/vnd.oasis.opendocument.text";
        if (!strcmp(ext, ".oga")) return "audio/ogg";
        if (!strcmp(ext, ".ogv")) return "video/ogg";
        if (!strcmp(ext, ".ogx")) return "application/ogg";
        if (!strcmp(ext, ".opus")) return "audio/ogg";
        if (!strcmp(ext, ".otf")) return "font/otf";
        if (!strcmp(ext, ".png")) return "image/png";
        if (!strcmp(ext, ".pdf")) return "application/pdf";
        if (!strcmp(ext, ".php") || !strcmp(ext, ".phtml")) return "application/x-httpd-php";
        if (!strcmp(ext, ".ppt")) return "application/vnd.ms-powerpoint";
        if (!strcmp(ext, ".pptx")) return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
        if (!strcmp(ext, ".py")) return "text/x-python";
        if (!strcmp(ext, ".pyc") || !strcmp(ext, ".pyo")) return "application/x-python-bytecode";
        if (!strcmp(ext, ".rar")) return "application/vnd.rar";
        if (!strcmp(ext, ".rb") || !strcmp(ext, ".erb")) return "text/x-ruby";
        if (!strcmp(ext, ".rs")) return "text/x-rust";
        if (!strcmp(ext, ".rtf")) return "application/rtf";
        if (!strcmp(ext, ".sh")) return "application/x-sh";
        if (!strcmp(ext, ".svg")) return "image/svg+xml";
        if (!strcmp(ext, ".swift")) return "text/x-swift";
        if (!strcmp(ext, ".tar")) return "application/x-tar";
        if (!strcmp(ext, ".tif") || !strcmp(ext, ".tiff")) return "image/tiff";
        if (!strcmp(ext, ".ts")) return "video/mp2t";
        if (!strcmp(ext, ".ttf")) return "font/ttf";
        if (!strcmp(ext, ".txt")) return "text/plain";
        if (!strcmp(ext, ".vsd")) return "application/vnd.visio";
        if (!strcmp(ext, ".wav")) return "audio/wav";
        if (!strcmp(ext, ".weba")) return "audio/webm";
        if (!strcmp(ext, ".webm")) return "video/webm";
        if (!strcmp(ext, ".webmanifest")) return "application/manifest+json";
        if (!strcmp(ext, ".webp")) return "image/webp";
        if (!strcmp(ext, ".woff")) return "font/woff";
        if (!strcmp(ext, ".woff2")) return "font/woff2";
        if (!strcmp(ext, ".xhtml")) return "application/xhtml+xml";
        if (!strcmp(ext, ".xls")) return "application/vnd.ms-excel";
        if (!strcmp(ext, ".xlsx")) return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
        if (!strcmp(ext, ".xml") || !strcmp(ext, ".xsl")) return "application/xml";
        if (!strcmp(ext, ".xul")) return "application/vnd.mozilla.xul+xml";
        if (!strcmp(ext, ".zip")) return "application/zip";
        if (!strcmp(ext, ".3gp")) return "video/3gpp";
        if (!strcmp(ext, ".3g2")) return "video/3gpp2";
        if (!strcmp(ext, ".7z")) return "application/x-7z-compressed";

    }
    printf("Unknown file extension detected: %s\n", ext);
    return "application/octet-stream"; // Fallback for unknown types
}

// Sends a file over a connection, also closes the file
int send_file(const int client_sock, FILE *open_file)
{

    // Allocate initial memory for the buffer
    size_t buffer_size = DEFAULT_BUFFER_SIZE;
    char *file_buffer = malloc(buffer_size);
    if (file_buffer == NULL) {
        perror("send_file - error allocating memory");
        fclose(open_file);
        return -1;
    }

    // Read and send the file in chunks
    size_t bytes_read;
    while ((bytes_read = fread(file_buffer, 1, DEFAULT_BUFFER_SIZE, open_file)) > 0) {
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
char *serve_directory_listing(char *absolute_path)
{
    DIR *this_dir;
    struct dirent *dir_entry;
    char **dir_entries;
    size_t entry_count = 0;
    size_t max_entries = 8;

    size_t body_size = DEFAULT_BUFFER_SIZE;
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

// Returns 0 if everything was sent properly
int parse_request_and_send_response(const int sock, char *request)
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

        return_status_code = URI_checker(uri_file_path, combined_path); // Parse the URI for any problems
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
    return_status_code = URI_checker(uri_file_path, combined_path);
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

            char *entity_body = serve_directory_listing(combined_path);
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
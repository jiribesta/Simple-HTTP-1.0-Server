#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "directory_resolution.h"
#include "request_parsing.h"
#include "socket_operations.h"

void check_valid_port(char *portstr);

int main(int argc, char *argv[])
{
    if (argc != 3){
        fprintf(stderr,"Usage: %s <port> <directory>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int listener, client_fd; // Listen on listener, new connection on client_fd
    char req_buf[4096]; // Buffer for client's request

    // Check if the port is valid, and resolve the directory if it's also valid
    check_valid_port(argv[1]);
    resolve_dir(argv[2]);

    // Set up and get a listening socket
    listener = get_listener(argv[1]);

    printf("Ready for connections...\n");

    for(;;){ // Main loop
        // Accept a new connection
        if ((client_fd = accept_and_print(listener)) < 0){
            continue;
        }

        // recv() data from a client on the connection
        if (poll_recv(client_fd, req_buf, sizeof(req_buf)) < 0){
            close(client_fd);
            continue;
        }

        if (parse_request_and_send_response(client_fd, req_buf) == 0){
            printf("Response sent succesfully on socket %d\n", client_fd);
        }
        close(client_fd);
    }
    
    return 0; // We never get here
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
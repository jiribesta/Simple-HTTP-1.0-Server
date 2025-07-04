#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int BACKLOG = 10; // Maximum queue size for incoming connections on the listening socket
static int TIMEOUT = 3; // How long we wait on a socket (in seconds)

// Returns a listening socket (or exits)
int get_listener(const char *port)
{
    int listener; // Listening socket descriptor
    int yes=1; // For setsockopt() SO_REUSEADDR
    int retval;
    struct addrinfo hints, *ai, *p;

    // Get a socket and bind it
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP, not UDP
    hints.ai_flags = AI_PASSIVE; // This machine's address

    if ((retval = getaddrinfo(NULL, port, &hints, &ai)) != 0) {
        fprintf(stderr, "get_listener - %s\n", gai_strerror(retval));
        exit(EXIT_FAILURE);
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
        fprintf(stderr, "get_listener - problem binding socket");
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(ai); // All done with this

    if (listen(listener, BACKLOG) == -1) {
        perror("get_listener - listen");
        exit(EXIT_FAILURE);
    }

    return listener;
}

// Gets sockaddr, IPv4 or IPv6 (for accept_and_print())
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// Returns a new client socket or -1 on error
int accept_and_print(const int listening_fd)
{
    struct sockaddr_storage client_addr; // Client's address information
    socklen_t addrlen = sizeof(client_addr);
    char addr_str[INET6_ADDRSTRLEN];

    int new_fd = accept(listening_fd, (struct sockaddr *)&client_addr, &addrlen);
    if (new_fd == -1){
        perror("accept - accept");
        return -1;
    }

    // Print out who's connect()ing to us
    inet_ntop(client_addr.ss_family,
            get_in_addr((struct sockaddr *)&client_addr),
            addr_str, 
            sizeof addr_str);

    printf("\nServer: Incoming connection from %s on socket %d\n", addr_str, new_fd);

    return new_fd;
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

// recv() data into a buffer
int poll_recv(const int recv_fd, char *recv_buf, size_t recv_buf_len)
{
    struct pollfd recv_poll_fd[1]; // pollfd object that'll contain the recv()ing socket
    int recv_poll_rv;

    recv_poll_fd[0].fd = recv_fd;
    recv_poll_fd[0].events = POLLIN | POLLHUP; // The events we want to check for

    recv_poll_rv = poll(recv_poll_fd, 1, TIMEOUT*1000);

    // Check for error or timeout
    if (recv_poll_rv < 0){
        perror("poll_recv - poll");
        return -1;

    } else if (recv_poll_rv == 0) {
        fprintf(stderr, "poll_recv - timeout reached on socket %d\n", recv_fd);
        return -1;

    } else {
        if (recv_poll_fd[0].revents & POLLHUP){ // If the client hung up
            printf("Connection on socket %d closed by client\n", recv_fd);
            return -1;

        // There is data to be recv()ed on the socket
        } else {
            int nbytes = recv(recv_fd, recv_buf, recv_buf_len - 1, 0);

            // Check for errors on recv()
            if (nbytes < 0){
                perror("poll_recv - recv");
                return -1;

            } else { // We got some data from a client
                recv_buf[nbytes] = '\0';

                return 0;
            }
        }
    }
}
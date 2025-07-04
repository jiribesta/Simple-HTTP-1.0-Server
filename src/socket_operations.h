#ifndef SOCKET_OPERATIONS_H
#define SOCKET_OPERATIONS_H

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

// Returns a listening socket (or exits)
int get_listener(const char *port);

// Gets sockaddr, IPv4 or IPv6 (for accept_and_print())
void *get_in_addr(struct sockaddr *sa);

// Returns a new client socket or -1 on error
int accept_and_print(const int listening_fd);

// send()s as much of the buffer as possible
int sendall(const int send_fd, char *send_buf, size_t *send_buf_len);

// recv() data into a buffer
int poll_recv(const int recv_fd, char *recv_buf, size_t recv_buf_len);

#endif
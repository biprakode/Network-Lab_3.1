#ifndef NETUTILS
#define NETUTILS

#include <stdint.h>
#include <sys/types.h>

// server side
int tcp_listen(uint16_t port , int backlog); // bind a listening socket to port
int tcp_accept(int listen_fd); // blocks until client connects

// client side
int tcp_connect(const char *ip, uint16_t port); // connect to ip:port

// I/O
ssize_t send_all(int fd, const void *buf, size_t len);
ssize_t recv_all(int fd, void *buf, size_t len);

// clean_up
void close_conn(int fd);

// file I/O
uint8_t* read_file(const char* filename, size_t* out_size);

#endif
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

int tcp_listen(uint16_t port , int backlog) {
    int fd = socket(AF_INET , SOCK_STREAM , 0); // TCP/ IPv4

    if(fd < 0) {
        perror("socket error");
        return -1;
    }

    // SO_REUSEADDR: Remove OS lock on socket after quitting application
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
        close(fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr)); 

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; // listen for everything
    addr.sin_port = htons(port); // network endian convertion

    if(bind(fd , (struct sockaddr_in *) &addr , sizeof(addr)) < 0) {
        perror("bind error");
        close(fd);
        return -1;
    }

    if(listen(fd , backlog) < 0) {
        perror("listen error");
        close(fd);
        return -1;
    }

    return fd;
}

int tcp_accept(int listen_fd) {
    int client_fd = accept(listen_fd , NULL , NULL);

    if (client_fd < 0) {
        perror("client accept error");
        return -1;
    }
    return client_fd;
}

int tcp_connect(const char *ip, uint16_t port) {

    int fd = socket(AF_INET , SOCK_STREAM , 0);

    if(fd < 0) {
        perror("socket error");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);


    // converts human-readable IP string to kernel binary
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "tcp_connect: invalid IP address '%s'\n", ip);
        close(fd);
        return -1;
    }
    
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }
 
    return fd;
}

ssize_t send_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = buf;
    size_t sent = 0;

    while(sent < len) {
        ssize_t n = write(fd , p + sent , len-sent);
        if (n < 0) {
            if (errno == EINTR) continue; // retry if interrupt
            perror("write error");
            return -1;
        }
        sent += (size_t)n;
    }

    return (size_t)sent;
}

ssize_t recv_all(int fd, void *buf, size_t len) {
    uint8_t *p = buf;
    ssize_t got = 0;

    while(got < len) {
        ssize_t n = read(fd , p + got , len-got);

        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read");
            return -1;
        } if (n == 0) {
            return 0;
        }
        got += n;
    }

    return (ssize_t)got;
}


void close_conn(int fd) {
    if (fd >= 0) close(fd);
}
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "frame.h"
#include "error.h"
#include "scheme.h"
#include "utils/utils.h"

#define PORT 8080

int main(int argc , char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <file> <scheme: checksum|crc8|crc10|crc16|crc32>\n", argv[0]);
        return 1;
    }
 
    const char *filename = argv[1];
    scheme_t scheme = parse_scheme(argv[2]);

    int listen_fd = tcp_listen(PORT , 1);
    if (listen_fd < 0) {
        fprintf(stderr, "Failed to start listening.\n");
        return 1;
    }

    int fd = tcp_accept(listen_fd);
    if (fd < 0) {
        fprintf(stderr, "accept failed\n");
        close_conn(listen_fd);
        return 1;
    }


    FILE *out = fopen(filename, "wb");
    if (out == NULL) {
        perror("fopen");
        close_conn(fd);
        close_conn(listen_fd);
        return 1;
    }

    size_t accepted = 0, rejected = 0;

    while(1) {
        frame_t frame;
        ssize_t n = recv_all(fd, &frame, FRAME_SIZE);
 
        if (n == 0) {
            printf("sender closed connection - done\n");
            break;
        }
        if (n < 0) {
            fprintf(stderr, "recv error\n");
            break;
        }

        uint32_t received_fcs = ntohl(frame.trailer.fcs);
        int valid = verify_fcs(scheme, (uint8_t *)&frame, HEADER_SIZE + PAYLOAD_SIZE, received_fcs);

        uint16_t frame_len = ntohs(frame.header.frame_len);
        uint16_t frame_seq = ntohs(frame.header.frame_seq);

        if (valid) {
            fwrite(frame.payload, 1, frame_len, out);
            accepted++;
            printf("seq=%u ACCEPTED len=%u\n", frame_seq, frame_len);
        } else {
            rejected++;
            printf("seq=%u REJECTED (fcs mismatch)\n", frame_seq);
        }
    }

    fclose(out);
    close_conn(fd);
    close_conn(listen_fd);
 
    printf("summary: accepted=%zu rejected=%zu\n", accepted, rejected);
    return 0;

}
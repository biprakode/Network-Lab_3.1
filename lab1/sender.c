#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "frame.h"
#include "error.h"
#include "scheme.h"
#include "utils.h"

#define PORT 8080

int main(int argc , char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <file> <scheme: checksum|crc8|crc10|crc16|crc32>\n", argv[0]);
        return 1;
    }
 
    scheme_t scheme = parse_scheme(argv[2]);


    size_t file_size = 0;
    uint8_t *data = read_file("data.txt" , &file_size);

    if (data == NULL) {
        fprintf(stderr, "Failed to read the file.\n");
        return 1;
    }

    int fd = tcp_connect( "127.0.0.1" , PORT);
    if (fd < 0) {
        fprintf(stderr, "Failed to connect to receiver.\n");
        free(data);
        return 1;
    }

    srand((unsigned)time(NULL));   
    uint16_t seq = 0;
    size_t offset = 0;

    while(offset < file_size) {
        frame_t frame;
        memset(&frame, 0, sizeof(frame));

        size_t chunk_len = file_size - offset;
        if(chunk_len > PAYLOAD_SIZE) chunk_len = PAYLOAD_SIZE; // edge case for last frame

        // arbitary placeholder MAC addresses
        memset(frame.header.src_addr, 0xAA, sizeof(frame.header.src_addr));
        memset(frame.header.dest_addr, 0xBB, sizeof(frame.header.dest_addr));

        frame.header.frame_len = htons((uint16_t) chunk_len);
        frame.header.frame_seq = htons(seq);
        
        memcpy(frame.payload , data + offset , chunk_len); // fill payload

        uint32_t fcs = compute_fcs(scheme , (uint8_t *) &frame , HEADER_SIZE + PAYLOAD_SIZE);
        frame.trailer.fcs = htonl(fcs);

        if(send_all(fd , &frame , FRAME_SIZE) != FRAME_SIZE) {
            fprintf(stderr, "send failed at seq %u\n", seq);
            break;
        }

        printf("sent seq=%u len=%zu fcs=0x%08X\n", seq, chunk_len, fcs);
        seq++;
        offset += chunk_len;
    }
    
    close_conn(fd);
    free(data);
    return 0;
}
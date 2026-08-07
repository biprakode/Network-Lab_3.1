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

uint8_t* read_file(const char* filename, size_t* out_size) {
    // 1. Open the file in binary mode ("rb") to prevent newline translations
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Error opening file");
        return NULL;
    }

    // 2. Seek to the end of the file to find its size
    if (fseek(file, 0, SEEK_END) != 0) {
        perror("Error seeking file");
        fclose(file);
        return NULL;
    }

    long size = ftell(file);
    if (size < 0) {
        perror("Error getting file size");
        fclose(file);
        return NULL;
    }

    // 3. Rewind back to the start of the file
    rewind(file);

    // 4. Allocate memory (+1 for an optional null-terminator if treated as string)
    uint8_t* buffer = (uint8_t*)malloc((size_t)size + 1);
    if (buffer == NULL) {
        perror("Memory allocation failed");
        fclose(file);
        return NULL;
    }

    // 5. Read the contents into the array
    size_t bytes_read = fread(buffer, sizeof(uint8_t), (size_t)size, file);
    buffer[bytes_read] = '\0'; 

    fclose(file);
    *out_size = bytes_read;

    return buffer;
}


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
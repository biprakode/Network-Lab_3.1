#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <arpa/inet.h>

#define REGION_LEN (HEADER_SIZE + PAYLOAD_SIZE) 

#include "frame.h"
#include "error.h"
#include "targeted_inject.h"
#include "bit_flip.h"
#include "targeted_inject.h"

static const crc_gen CRC_TABLE[] = {
    { 0xD5, 8 },
    { 0x233, 10 },
    { 0x8005, 16 },
    { 0x04C11DB7, 32 },
};

#define CRC_TABLE_LEN (sizeof(CRC_TABLE) / sizeof(CRC_TABLE[0]))

void clean_frame(frame_t *frame , const uint8_t *payload , size_t payload_len, uint16_t seq) {
    memset(frame, 0, sizeof(*frame));

    // arbitary MAC addresses
    memset(frame->header.src_addr, 0xAA, sizeof(frame->header.src_addr));
    memset(frame->header.dest_addr, 0xBB, sizeof(frame->header.dest_addr));

    frame->header.frame_len = htons((uint16_t)payload_len);
    frame->header.frame_seq = htons(seq);
    memcpy(frame->payload, payload, payload_len);
}

static void random_payload(uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)rand();
}


// Case 1 — all-zero blind spot (checksum)
static void all_zero(FILE *csv) {
    printf("\nAll-zero blind spot (checksum)\n");

    uint8_t region[REGION_LEN];
    memset(region, 0, sizeof(region)); // all zero payload and header
    uint16_t received_checksum = 0;

    int valid = checksum16_verify(region, sizeof(region), received_checksum);

    printf("all-zero region + zeroed checksum field -> %s\n", valid ? "VALID (undetected - blind spot present)" : "INVALID (correctly detected)");
    printf("This is exactly the failure mode the one's-complement design exists to prevent\n");

    fprintf(csv, "1,all_zero_blind_spot,-,checksum,%s\n", valid ? "undetected" : "detected");
}

static void compensating_error(FILE *csv, int trials) {
    printf("\nCompensating errror checksum blindspot\n");

    for (size_t d = 0; d < CRC_TABLE_LEN; d++) {
        crc_gen p = CRC_TABLE[d];
        int checksum_caught = 0, crc_caught = 0;

        for (int t = 0; t < trials; t++) {
            frame_t frame;
            uint8_t payload[PAYLOAD_SIZE];
            random_payload(payload , sizeof(payload));
            clean_frame(&frame, payload, sizeof(payload), (uint16_t)t);

            uint16_t chk = checksum16_compute((uint8_t *)&frame, REGION_LEN);
            uint32_t crc = crc_compute((uint8_t *)&frame, REGION_LEN, p);

            inject_compensating_error((uint8_t *)&frame, REGION_LEN);

            if (!checksum16_verify((uint8_t *)&frame, REGION_LEN, chk)) checksum_caught++;
            if (!crc_verify((uint8_t *)&frame, REGION_LEN, p, crc)) crc_caught++;

        }

        printf("CRC degree=%2d: checksum_caught=%d/%d  crc_caught=%d/%d (expect checksum~0, crc~%d)\n", p.degree, checksum_caught, trials, crc_caught, trials, trials);
        fprintf(csv, "2,compensating_error,%d,checksum,%d/%d\n", p.degree, checksum_caught, trials);
        fprintf(csv, "2,compensating_error,%d,crc,%d/%d\n", p.degree, crc_caught, trials);
    }
}

static void generator_multiple(FILE *csv , int trials) {
    printf("\nGenerator multiple payload crc blindspot\n");

    for (size_t d = 0; d < CRC_TABLE_LEN; d++) {
        crc_gen p = CRC_TABLE[d];
        int checksum_caught = 0, crc_caught = 0;

        for (int t = 0; t < trials; t++) {
            frame_t frame;
            uint8_t payload[PAYLOAD_SIZE];
            random_payload(payload, sizeof(payload));
            clean_frame(&frame, payload, sizeof(payload), (uint16_t)t);
 
            uint16_t chk = checksum16_compute((uint8_t *)&frame, REGION_LEN);
            uint32_t crc = crc_compute((uint8_t *)&frame, REGION_LEN, p);
 
            inject_generator_multiple_error((uint8_t *)&frame, REGION_LEN, p);
 
            if (!checksum16_verify((uint8_t *)&frame, REGION_LEN, chk)) checksum_caught++;
            if (!crc_verify((uint8_t *)&frame, REGION_LEN, p, crc))     crc_caught++;
        }
        
        printf("CRC degree=%2d: checksum_caught=%d/%d  crc_caught=%d/%d (expect checksum~0, crc~%d)\n", p.degree, checksum_caught, trials, crc_caught, trials, trials);
        fprintf(csv, "3,generator_multiple,%d,checksum,%d/%d\n", p.degree, checksum_caught, trials);
        fprintf(csv, "3,generator_multiple,%d,crc,%d/%d\n", p.degree, crc_caught, trials);
    }
}

//burst length exactly at r vs. r+1 (CRC)
static void burst_boundary(FILE *csv, int trials) {
    printf("\nBurst length at r vs. r+1 (CRC)\n");

    for (size_t d = 0; d < CRC_TABLE_LEN; d++) {
        crc_gen p = CRC_TABLE[d];
        int caught_at_r = 0, caught_at_r_plus_1 = 0;
 
        for (int t = 0; t < trials; t++) {
            uint8_t payload[PAYLOAD_SIZE];
            random_payload(payload, sizeof(payload));
 
            frame_t frame;
            clean_frame(&frame, payload, sizeof(payload), (uint16_t)t);
            uint32_t crc = crc_compute((uint8_t *)&frame, REGION_LEN, p);
            inject_burst_error((uint8_t *)&frame, REGION_LEN, p.degree);
            if (!crc_verify((uint8_t *)&frame, REGION_LEN, p, crc)) caught_at_r++;
 
            clean_frame(&frame, payload, sizeof(payload), (uint16_t)t);
            crc = crc_compute((uint8_t *)&frame, REGION_LEN, p);
            inject_burst_error((uint8_t *)&frame, REGION_LEN, p.degree + 1);
            if (!crc_verify((uint8_t *)&frame, REGION_LEN, p, crc)) caught_at_r_plus_1++;
        }

        double expected_r1_pct = 100.0 * (1.0 - pow(2.0, -(double)p.degree));
 
        printf("CRC degree=%2d: burst=r    caught=%d/%d (expect 100%%)\n", p.degree, caught_at_r, trials);
        printf("burst=r+1  caught=%d/%d (%.4f%% observed, %.6f%% expected)\n", caught_at_r_plus_1, trials, 100.0 * caught_at_r_plus_1 / trials, expected_r1_pct);
 
        fprintf(csv, "4,burst_at_r,%d,crc,%d/%d\n", p.degree, caught_at_r, trials);
        fprintf(csv, "4,burst_at_r_plus_1,%d,crc,%d/%d\n", p.degree, caught_at_r_plus_1, trials);
    }
}

int main(int argc, char *argv[])
{
    int trials = (argc > 1) ? atoi(argv[1]) : 1000;
    srand((unsigned)time(NULL));
 
    FILE *csv = fopen("results.csv", "w");
    if (csv == NULL) {
        perror("fopen results.csv");
        return 1;
    }
    fprintf(csv, "bucket,case,crc_degree,scheme,result\n");
 
    all_zero(csv);
    compensating_error(csv, trials);
    generator_multiple(csv, trials);
    burst_boundary(csv, trials);

    fclose(csv);
    printf("\nAll buckets complete. Raw results in results.csv (trials=%d per bucket/degree).\n", trials);
    return 0;
}

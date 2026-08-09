#ifndef ERROR_H
#define ERROR_H
 
#include <stdint.h>
#include <stddef.h>

uint16_t checksum16_compute(const uint8_t *data, size_t len);
int checksum16_verify(const uint8_t *data, size_t len, uint16_t received_checksum);

typedef struct {
    uint32_t poly;
    int degree;
}crc_gen;

/* Fixed: Changed parameters from uint32_t to const uint8_t *data */
uint32_t crc_compute(const uint8_t *data, size_t len, crc_gen params);
int crc_verify(const uint8_t *data, size_t len, crc_gen generator, uint32_t rec_crc);

#endif
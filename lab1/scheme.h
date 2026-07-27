#ifndef SCHEME_H
#define SCHEME_H

#include <stdint.h>
#include <stddef.h>
 
typedef enum {
    SCHEME_CHECKSUM16,
    SCHEME_CRC8,
    SCHEME_CRC10,
    SCHEME_CRC16,
    SCHEME_CRC32
} scheme_t;

scheme_t parse_scheme(const char *s); // Parses "checksum" / "crc8" / "crc10" / "crc16" / "crc32"
uint32_t compute_fcs(scheme_t s, const uint8_t *buf, size_t len); // dispatch based on scheme
int verify_fcs(scheme_t s, const uint8_t *buf, size_t len, uint32_t received); // dispatches to verify


#endif
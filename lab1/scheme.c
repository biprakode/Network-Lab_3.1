#include "scheme.h"
#include "error.h"
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// CRC-8:  x^8+x^7+x^6+x^4+x^2+1 - 0xD5
// CRC-10: x^10+x^9+x^5+x^4+x+1 - 0x233
// CRC-16: x^16+x^15+x^2+1 - 0x8005
// CRC-32: x^32+x^26+x^23+x^22+x^16+x^12+x^11+x^10+x^8+x^7+x^5+x^4+x^2+x+1 - 0x04C11DB7


static crc_gen crc_generator(scheme_t s) {
    switch (s) {
        case SCHEME_CRC8 : {
            crc_gen params = {.poly = 0xD5 , .degree = 8};
            return params;
        }
        case SCHEME_CRC10 : {
            crc_gen params = {.poly = 0x233 , .degree = 10};
            return params;
        }
        case SCHEME_CRC16 : {
            crc_gen params = {.poly = 0x8005 , .degree = 16};
            return params;
        }
        case SCHEME_CRC32 : {
            crc_gen params = {.poly = 0x04C11DB7 , .degree = 32};
            return params;
        }
        default : {
            crc_gen params = {.poly = 0 , .degree = 0};
            return params;
        }
    }
}


scheme_t parse_scheme(const char *s)
{
    if (strcmp(s, "checksum") == 0) return SCHEME_CHECKSUM16;
    if (strcmp(s, "crc8")     == 0) return SCHEME_CRC8;
    if (strcmp(s, "crc10")    == 0) return SCHEME_CRC10;
    if (strcmp(s, "crc16")    == 0) return SCHEME_CRC16;
    if (strcmp(s, "crc32")    == 0) return SCHEME_CRC32;
 
    fprintf(stderr, "unknown scheme '%s' — expected checksum|crc8|crc10|crc16|crc32\n", s);
    exit(1);
}


uint32_t compute_fcs(scheme_t s, const uint8_t *buf, size_t len) {
    if (s == SCHEME_CHECKSUM16) {
        return (uint32_t)checksum16_compute(buf , len);
    }
    return crc_compute(buf , len , crc_generator(s));
}

int verify_fcs(scheme_t s, const uint8_t *buf, size_t len, uint32_t received) {
    if (s == SCHEME_CHECKSUM16) {
        return (uint32_t)checksum16_verify(buf , len , (uint16_t)received);
    }
    return crc_verify(buf , len , crc_generator(s) , received);
}
#include "error.h"

static uint16_t ones_comp_sum(const uint8_t *data, size_t len) {
    uint32_t sum = 0;

    if (len % 2 != 0) {}
    for(size_t i = 0 ; i<len ; i+=2) {
        uint16_t word = ((uint16_t)data[i] << 8) | data[i+1];
        sum += word;
        while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16); // end-around carry
    }

    return (uint16_t)sum;
}

uint16_t checksum16_compute(const uint8_t *data, size_t len) {
    return (uint16_t)(~ones_comp_sum(data , len) & 0xFFFF);
}


int checksum16_verify(const uint8_t *data, size_t len, uint16_t received_checksum) {
    uint32_t sum = ones_comp_sum(data , len);
    sum += received_checksum;

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);

    return (sum == 0xFFFF) ? 1 : 0;
}

uint32_t crc_compute(const uint8_t *data , size_t len , crc_gen params) {
    uint32_t crc = 0;
    uint32_t msb_mask = 1u << (params.degree - 1);
    uint32_t reg_mask = (params.degree == 32) ? 0xFFFFFFFFu : ((1u << params.degree) - 1); // degree mask to keep crc within degree (32 bit special case)

    for(size_t i = 0 ; i<len ; i++) {
        crc ^= (data[i]) << (params.degree - 8); // align incoming with top byte of reg

        for(int i = 0 ; i<8 ; i++) { // for every bit of this byte
            if (crc & msb_mask) { // overflow into remainder
                crc = (crc << 1) ^ params.poly;
            } else {
                crc = (crc << 1);
            }
        }

        crc &= reg_mask; // alighn to register length
    }

    //  push rest remainder degree bits through crc
    for(int b = params.degree ; b>0 ; b--) {
        if(crc & msb_mask) {
            crc = (crc << 1) ^ params.poly;
        } else {
            crc = (crc << 1);
        }
    }

    return crc;
}

int crc_verify(const uint8_t *data , size_t len , crc_gen generator , uint32_t rec_crc) {
    uint32_t crc_cal = crc_compute(data , len , generator);
    return (crc_cal == rec_crc) ? 1 : 0;
}

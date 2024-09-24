#include <stddef.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include "obfsm.h"

const unsigned char OBFSM_RECV_STAGE_WAIT_FOR_HDR1 = 0;
const unsigned char OBFSM_RECV_STAGE_WAIT_FOR_HDR2 = 1;
const unsigned char OBFSM_RECV_STAGE_WAIT_FOR_WHOLE_PACKET = 2;

#define OBFSM_BUFF_SIZE   1024*10

obfuscator_state_machine_t *alloc_obfsm() {
    obfuscator_state_machine_t *obfsm = (obfuscator_state_machine_t *)malloc(sizeof(obfuscator_state_machine_t));
    if (obfsm == NULL) {
        return NULL;
    }
    memset(obfsm, 0, sizeof(obfuscator_state_machine_t));

    obfsm->buf = (unsigned char *)malloc(OBFSM_BUFF_SIZE);
    if (obfsm->buf == NULL) {
        free(obfsm);
        return NULL;
    }
    return obfsm;
}

void init_obfsm(obfuscator_state_machine_t *obfsm) {
    obfsm->recv_stage = OBFSM_RECV_STAGE_WAIT_FOR_HDR1;
    obfsm->offset = 0;
    obfsm->left = 1 + sizeof(exchange_packet_hdr1_t);
    obfsm->counter = 0;
}

obfuscator_state_machine_t *create_obfsm() {
    obfuscator_state_machine_t *obfsm = alloc_obfsm();
    if (obfsm == NULL) {
        return NULL;
    }
    init_obfsm(obfsm);
    return obfsm;
}

void destroy_obfsm(obfuscator_state_machine_t *obfsm) {
    if (obfsm == NULL) {
        return;
    }
    if (obfsm->buf != NULL) {
        free(obfsm->buf);
    }
    free(obfsm);
}

int obfsm_consume(obfuscator_state_machine_t *obfsm, char *data, unsigned short len, packet_cb_t *packet_cb, void *context) {
    for (int i = 0; i < len; ) {
        unsigned short bytes_to_consume = len - i;
        if (obfsm->left < bytes_to_consume) {
            bytes_to_consume = obfsm->left;
        }

        if (obfsm->offset + bytes_to_consume > OBFSM_BUFF_SIZE) {
            // this should not happen
            return -1;
        }

        memcpy(&obfsm->buf[obfsm->offset], &data[i], bytes_to_consume);
        obfsm->left -= bytes_to_consume;
        obfsm->offset += bytes_to_consume;
        i += bytes_to_consume;

        if (obfsm->left > 0) {
            return 0;
        }

        if (obfsm->recv_stage == OBFSM_RECV_STAGE_WAIT_FOR_HDR1) {
            obfsm->hdr1 = (exchange_packet_hdr1_t *)&obfsm->buf[obfsm->offset - sizeof(exchange_packet_hdr1_t)];

            obfsm->recv_stage = OBFSM_RECV_STAGE_WAIT_FOR_HDR2;
            obfsm->left = obfsm->hdr1->hdr2_offset + sizeof(exchange_packet_hdr2_t);
            continue;
        }

        if (obfsm->recv_stage == OBFSM_RECV_STAGE_WAIT_FOR_HDR2) {
            obfsm->hdr2 = (exchange_packet_hdr2_t *)&obfsm->buf[obfsm->offset - sizeof(exchange_packet_hdr2_t) - 1];

            obfsm->recv_stage = OBFSM_RECV_STAGE_WAIT_FOR_WHOLE_PACKET;
            obfsm->left = obfsm->hdr2->total_size - obfsm->offset;
            continue;
        }

        if (obfsm->recv_stage == OBFSM_RECV_STAGE_WAIT_FOR_WHOLE_PACKET) {
            int real_data_offset = sizeof(exchange_packet_hdr1_t) + obfsm->hdr1->hdr2_offset + sizeof(exchange_packet_hdr2_t);
            for (int j = 0; j < obfsm->hdr2->packet_size; j++) {
                obfsm->buf[real_data_offset + j] ^= PACKET_XORKEY;
            }

            int res = (*packet_cb)(&obfsm->buf[real_data_offset], obfsm->hdr2->packet_type, obfsm->hdr2->packet_size, context);
            if (res == -1) {
                return res;
            }

            obfsm->recv_stage = OBFSM_RECV_STAGE_WAIT_FOR_HDR1;
            obfsm->left = 1 + sizeof(exchange_packet_hdr1_t);
            obfsm->offset = 0;
        }
    }
    return 0;
}

exchange_packet_desc_t obfsm_pack(obfuscator_state_machine_t *obfsm, unsigned char packet_type, unsigned short packet_size, char *data) {
    exchange_packet_desc_t result;
    int junk_size = 0;
    int junk1_size;

    int junk_size_limit = 512;
    if (obfsm->counter > 100) {
        junk_size_limit = 64;
    }

    if (packet_size + sizeof(exchange_packet_hdr1_t) + sizeof(exchange_packet_hdr2_t) < MAX_PACKET_SIZE) {
        junk_size = rand() % (MAX_PACKET_SIZE - packet_size - sizeof(exchange_packet_hdr1_t) - sizeof(exchange_packet_hdr2_t));
        junk_size = junk_size % junk_size_limit;
    }

    junk_size += 8;

    junk1_size = rand() % junk_size;
    if (junk1_size > 128) {
        junk1_size = 128;
    }

    result.type = packet_type;
    result.size = packet_size + junk_size + sizeof(exchange_packet_hdr1_t) + sizeof(exchange_packet_hdr2_t);
    result.data = malloc(result.size);
    if (result.data == NULL) {
        return result;
    }

    // fill the data
    unsigned int g_seed = rand();
    unsigned short *ptr = (unsigned short *)result.data;
    for (int i = 0; i < result.size / 2; i++) {
        ptr[i] = (g_seed>>16)&0x7FFF;
        g_seed = (214013*g_seed+2531011);
    }

    int offset = 0;

    exchange_packet_hdr1_t *hdr1 = (exchange_packet_hdr1_t *)&result.data[1];
    hdr1->hdr2_offset = junk1_size;
    offset += sizeof(exchange_packet_hdr1_t) + hdr1->hdr2_offset;
    exchange_packet_hdr2_t *hdr2 = (exchange_packet_hdr2_t *)&result.data[offset];
    hdr2->packet_type = packet_type;
    hdr2->packet_size = packet_size;
    hdr2->total_size = result.size;
    offset += sizeof(exchange_packet_hdr2_t);

    if (data != NULL) {
        memcpy(result.data + offset, data, packet_size);
        for (int i = 0; i < packet_size; i++) {
            result.data[offset + i] ^= PACKET_XORKEY;
        }
    }

    return result;
}

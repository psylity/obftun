#ifndef OBFSM_H
#define OBFSM_H

#define MAX_PACKET_SIZE 1200 // TODO: make configurable, it depends on MTU
#ifndef PACKET_XORKEY
#define PACKET_XORKEY 0x12
#endif

typedef struct exchange_packet_hdr1 {
    unsigned char hdr2_offset;
} exchange_packet_hdr1_t;

typedef struct exchange_packet_hdr2 {
    unsigned char packet_type;
    unsigned short packet_size;
    unsigned short total_size;
} exchange_packet_hdr2_t;

typedef struct exchange_packet_desc {
    unsigned short size;
    unsigned char *data;
    unsigned short type;
} exchange_packet_desc_t;

typedef struct exchange_state_machine {
    unsigned char *buf;
    unsigned short offset;
    unsigned char recv_stage;
    exchange_packet_hdr1_t *hdr1;
    exchange_packet_hdr2_t *hdr2;
    unsigned short left;
    unsigned long counter;
} obfuscator_state_machine_t;

typedef int (packet_cb_t)(unsigned char *, unsigned short, unsigned short, void *);

obfuscator_state_machine_t *create_obfsm();
void init_obfsm(obfuscator_state_machine_t *obfsm);
obfuscator_state_machine_t *alloc_obfsm();

int obfsm_consume(obfuscator_state_machine_t *obfsm, char *data, unsigned short len, packet_cb_t *packet_cb, void *context);
exchange_packet_desc_t obfsm_pack(obfuscator_state_machine_t *obfsm, unsigned char packet_type, unsigned short packet_size, char *data);

void destroy_obfsm(obfuscator_state_machine_t *obfsm);


#endif //OBFSM_H
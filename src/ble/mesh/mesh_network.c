/*
 * Copyright (C) 2018 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MATTHIAS
 * RINGWALD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at 
 * contact@bluekitchen-gmbh.com
 *
 */

#define __BTSTACK_FILE__ "mesh_network.c"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ble/mesh/adv_bearer.h"
#include "ble/mesh/pb_adv.h"
#include "ble/mesh/beacon.h"
#include "provisioning.h"
#include "provisioning_device.h"
#include "btstack.h"

// configuration
#define MESH_NETWORK_CACHE_SIZE 2
#define ENABLE_MESH_RELAY

// structs

typedef struct {
    uint8_t nid;
    uint8_t first;
} mesh_network_key_iterator_t;

// globals

static uint32_t global_iv_index;
static uint16_t mesh_network_primary_address;
static uint16_t mesh_network_num_elements;
static void (*mesh_network_higher_layer_handler)(mesh_network_callback_type_t callback_type, mesh_network_pdu_t * network_pdu);
static void (*mesh_network_proxy_message_handler)(mesh_network_callback_type_t callback_type, mesh_network_pdu_t * network_pdu);

// shared send/receive crypto
static int mesh_crypto_active;

// crypto requests
static union {
    btstack_crypto_ccm_t         ccm;
    btstack_crypto_aes128_t      aes128;
} mesh_network_crypto_request;

static const mesh_network_key_t *  current_network_key;

// PECB calculation 
static uint8_t encryption_block[16];
static uint8_t obfuscation_block[16];

// Network Nonce
static uint8_t network_nonce[13];

// INCOMING //

// unprocessed network pdu - added by mesh_network_pdus_received_message
static btstack_linked_list_t        network_pdus_received;

// in validation
static mesh_network_pdu_t *         network_pdu_in_validation;
static mesh_network_key_iterator_t  validation_network_key_it;

// OUTGOING //

// Network PDUs queued by mesh_network_send
static btstack_linked_list_t network_pdus_queued;

// Network PDUs ready to send via adv bearer
static btstack_linked_list_t network_pdus_outgoing;


// mesh network key list
static mesh_network_key_t mesh_network_primary_key;

// mesh network cache - we use 32-bit 'hashes'
static uint32_t mesh_network_cache[MESH_NETWORK_CACHE_SIZE];
static int      mesh_network_cache_index;

// prototypes

static void mesh_network_run(void);
static void process_network_pdu_validate(mesh_network_pdu_t * network_pdu);

// network caching
static uint32_t mesh_network_cache_hash(mesh_network_pdu_t * network_pdu){
    // - The SEQ field is a 24-bit integer that when combined with the IV Index, 
    // shall be a unique value for each new Network PDU originated by this node (=> SRC)
    // - IV updates only rarely
    // => 16 bit SRC, 1 bit IVI, 15 bit SEQ
    uint8_t  ivi = network_pdu->data[0] >> 7;
    uint16_t seq = big_endian_read_16(network_pdu->data, 3);
    uint16_t src = big_endian_read_16(network_pdu->data, 5);
    return (src << 16) | (ivi << 15) | (seq & 0x7fff);
}

static int mesh_network_cache_find(uint32_t hash){
    int i;
    for (i = 0; i < MESH_NETWORK_CACHE_SIZE; i++) {
        if (mesh_network_cache[i] == hash) {
            return 1;
        }
    }
    return 0;
}

static void mesh_network_cache_add(uint32_t hash){
    mesh_network_cache[mesh_network_cache_index++] = hash;
    if (mesh_network_cache_index >= MESH_NETWORK_CACHE_SIZE){
        mesh_network_cache_index = 0;
    }
}

// network key list

const mesh_network_key_t * mesh_network_key_list_get(uint16_t netkey_index){
    if (netkey_index) return NULL;
    return &mesh_network_primary_key;
}

void mesh_network_key_list_add_from_provisioning_data(const mesh_provisioning_data_t * provisioning_data){
    // get single instance
    mesh_network_key_t * network_key = &mesh_network_primary_key;
    memset(network_key, 0, sizeof(mesh_network_key_t));

    // NetKey
    // memcpy(network_key->net_key, provisioning_data, net_key);

    // IdentityKey
    // memcpy(network_key->identity_key, provisioning_data->identity_key, 16);

    // BeaconKey
    memcpy(network_key->beacon_key, provisioning_data->beacon_key, 16);

    // NID
    network_key->nid = provisioning_data->nid;

    // EncryptionKey
    memcpy(network_key->encryption_key, provisioning_data->encryption_key, 16);

    // PrivacyKey
    memcpy(network_key->privacy_key, provisioning_data->privacy_key, 16);

    // NetworkID
    memcpy(network_key->network_id, provisioning_data->network_id, 8);
}

// mesh network key iterator
static void mesh_network_key_iterator_init(mesh_network_key_iterator_t * it, uint8_t nid){
    it->nid = nid;
    it->first = 1;
}

static int mesh_network_key_iterator_has_more(mesh_network_key_iterator_t * it){
    return it->first && it->nid == mesh_network_primary_key.nid;
}

static const mesh_network_key_t * mesh_network_key_iterator_get_next(mesh_network_key_iterator_t * it){
    it->first = 0;
    return &mesh_network_primary_key;
}

// common helper
int mesh_network_address_unicast(uint16_t addr){
    return addr < 0x8000;
}

int mesh_network_addresses_valid(uint8_t ctl, uint16_t src, uint16_t dst){
    printf("CTL: %u\n", ctl);
    printf("SRC: %04x\n", src);
    printf("DST: %04x\n", dst);
    if (src == 0){
        printf("SRC Unassigned Addr -> ignore\n");
        return 0;
    }
    if ((src & 0xC000) == 0x8000){
        printf("SRC Virtual Addr -> ignore\n");
        return 0;
    }
    if ((src & 0xC000) == 0xC000){
        printf("SRC Group Addr -> ignore\n");
        return 0;
    }
    if (dst == 0){
        printf("DST Unassigned Addr -> ignore\n");
        return 0;
    }
    if ( ((dst & 0xC000) == 0x8000) && (ctl == 1)){
        printf("DST Virtual Addr in CONTROL -> ignore\n");
        return 0;
    }
    if ( (0xFF00 <= dst) && (dst <= 0xfffb) && (ctl == 0) ){
        printf("DST RFU Group Addr in MESSAGE -> ignore\n");
        return 0;
    }
    printf("SRC + DST Addr valid\n");
    return 1;
}

static void mesh_network_create_nonce(uint8_t * nonce, const mesh_network_pdu_t * pdu, uint32_t iv_index){
    unsigned int pos = 0;
    nonce[pos++] = 0x0;      // Network Nonce
    memcpy(&nonce[pos], &pdu->data[1], 6);
    pos += 6;
    big_endian_store_16(nonce, pos, 0);
    pos += 2;
    big_endian_store_32(nonce, pos, iv_index);
}

static void mesh_proxy_create_nonce(uint8_t * nonce, const mesh_network_pdu_t * pdu, uint32_t iv_index){
    unsigned int pos = 0;
    nonce[pos++] = 0x3;      // Proxy Nonce
    nonce[pos++] = 0;
    memcpy(&nonce[pos], &pdu->data[2], 5);
    pos += 5;
    big_endian_store_16(nonce, pos, 0);
    pos += 2;
    big_endian_store_32(nonce, pos, iv_index);
}

// NID/IVI | obfuscated (CTL/TTL, SEQ (24), SRC (16) ), encrypted ( DST(16), TransportPDU), MIC(32 or 64)

static void mesh_network_send_d(mesh_network_pdu_t * network_pdu){

    // add to queue
    btstack_linked_list_add_tail(&network_pdus_outgoing, (btstack_linked_item_t *) network_pdu);

    // request to send
    adv_bearer_request_can_send_now_for_mesh_message();

    // go
    mesh_network_run();
}

// new
static void mesh_network_send_c(void *arg){
    mesh_network_pdu_t * network_pdu = (mesh_network_pdu_t *) arg;

    // obfuscate
    unsigned int i;
    for (i=0;i<6;i++){
        network_pdu->data[1+i] ^= obfuscation_block[i];
    }

    printf("TX-C-NetworkPDU: ");
    printf_hexdump(network_pdu->data, network_pdu->len);

    // crypto done
    mesh_crypto_active = 0;

    // done
    (network_pdu->callback)(network_pdu);
}

static void mesh_network_send_b(void *arg){
    mesh_network_pdu_t * network_pdu = (mesh_network_pdu_t *) arg;

    uint32_t iv_index = global_iv_index;

    // store NetMIC
    uint8_t net_mic[8];
    btstack_crypo_ccm_get_authentication_value(&mesh_network_crypto_request.ccm, net_mic);

    // store MIC
    uint8_t net_mic_len = network_pdu->data[1] & 0x80 ? 8 : 4;
    memcpy(&network_pdu->data[network_pdu->len], net_mic, net_mic_len);
    network_pdu->len += net_mic_len;

    printf("TX-B-NetworkPDU: ");
    printf_hexdump(network_pdu->data, network_pdu->len);

    // calc PECB
    memset(encryption_block, 0, 5);
    big_endian_store_32(encryption_block, 5, iv_index);
    memcpy(&encryption_block[9], &network_pdu->data[7], 7);
    btstack_crypto_aes128_encrypt(&mesh_network_crypto_request.aes128, current_network_key->privacy_key, encryption_block, obfuscation_block, &mesh_network_send_c, network_pdu);
}

static void mesh_network_send_a(mesh_network_pdu_t * network_pdu){

    mesh_crypto_active = 1;

    // lookup network by netkey_index
    current_network_key = mesh_network_key_list_get(network_pdu->netkey_index);
    if (!current_network_key) {
        mesh_crypto_active = 0;
        // notify upper layer
        (*mesh_network_higher_layer_handler)(MESH_NETWORK_PDU_SENT, network_pdu);
        // run again
        mesh_network_run();
        return;
    }

    // get network nonce
    if (network_pdu->flags & 1){
        mesh_proxy_create_nonce(network_nonce, network_pdu, global_iv_index); 
        printf("TX-ProxyNonce:  ");
        printf_hexdump(network_nonce, 13);
    } else {
        mesh_network_create_nonce(network_nonce, network_pdu, global_iv_index); 
        printf("TX-NetworkNonce:  ");
        printf_hexdump(network_nonce, 13);
    }

    printf("TX-EncryptionKey: ");
    printf_hexdump(current_network_key->encryption_key, 16);

    // start ccm
    uint8_t cypher_len  = network_pdu->len - 7;
    uint8_t net_mic_len = network_pdu->data[1] & 0x80 ? 8 : 4;
    btstack_crypo_ccm_init(&mesh_network_crypto_request.ccm, current_network_key->encryption_key, network_nonce, cypher_len, 0, net_mic_len);
    btstack_crypto_ccm_encrypt_block(&mesh_network_crypto_request.ccm, cypher_len, &network_pdu->data[7], &network_pdu->data[7], &mesh_network_send_b, network_pdu);
}

void mesh_network_message_processed_by_higher_layer(mesh_network_pdu_t * network_pdu){
#ifdef ENABLE_MESH_RELAY
    uint8_t ctl_ttl = network_pdu->data[1];
    uint8_t ctl     = ctl_ttl >> 7;
    uint8_t ttl     = ctl_ttl & 0x7f;
    uint8_t net_mic_len = (ctl_ttl & 0x80) ? 8 : 4;
    uint16_t src    = big_endian_read_16(network_pdu->data, 5);

    // check if address matches elements on our node and TTL >= 2
    if (((src < mesh_network_primary_address) || (src > (mesh_network_primary_address + mesh_network_num_elements))) && (ttl >= 2)){
        // prepare pdu for resending
        network_pdu->len    -= net_mic_len;
        network_pdu->data[1] = (ctl << 7) | (ttl - 1);

        // queue up
        network_pdu->callback = &mesh_network_send_d;
        btstack_linked_list_add_tail(&network_pdus_queued, (btstack_linked_item_t *) network_pdu);

        // go
        mesh_network_run();

        return;
    }
#endif
    btstack_memory_mesh_network_pdu_free(network_pdu);
}

static void process_network_pdu_done(void){
    btstack_memory_mesh_network_pdu_free(network_pdu_in_validation);
    network_pdu_in_validation = NULL;
    mesh_crypto_active = 0;

    mesh_network_run();
}

static void process_network_pdu_validate_d(void * arg){
    mesh_network_pdu_t * network_pdu = (mesh_network_pdu_t *) arg;

    uint8_t ctl_ttl     = network_pdu->data[1];
    uint8_t ctl         = ctl_ttl >> 7;
    uint8_t net_mic_len = (ctl_ttl & 0x80) ? 8 : 4;
    uint8_t cypher_len  = network_pdu->len - 9 - net_mic_len;

    // store NetMIC
    uint8_t net_mic[8];
    btstack_crypo_ccm_get_authentication_value(&mesh_network_crypto_request.ccm, net_mic);
    printf("NetMIC: "); 
    printf_hexdump(net_mic, net_mic_len);
    // store in pdu
    memcpy(&network_pdu->data[network_pdu->len-net_mic_len], net_mic, net_mic_len);

    printf("Decrypted DST/TransportPDU: ");
    printf_hexdump(&network_pdu->data[7], 2 + cypher_len);

    printf("Decrypted: ");
    printf_hexdump(network_pdu->data, network_pdu->len);

    // compare calcualted nic to nic in data
    if (memcmp(net_mic, &network_pdu_in_validation->data[network_pdu->len-net_mic_len], net_mic_len) != 0){
        // fail
        printf("NetMIC mismatch, try next key\n");
        process_network_pdu_validate(network_pdu);
        return;
    }    

    // remove NetMIC from payload
    network_pdu->len -= net_mic_len;

    // match
    printf("NetMIC matches\n");

    printf("TTL: 0x%02x\n", network_pdu->data[1] & 0x7f);

    if (network_pdu->flags & 1){

        // no additional checks for proxy messages
        (*mesh_network_proxy_message_handler)(MESH_NETWORK_PDU_RECEIVED, network_pdu);
 
    } else {

        // validate src/dest addresses
        uint16_t src = big_endian_read_16(network_pdu->data, 5);
        uint16_t dst = big_endian_read_16(network_pdu->data, 7);
        int valid = mesh_network_addresses_valid(ctl, src, dst);
        if (!valid){
            printf("Address invalid\n");
            btstack_memory_mesh_network_pdu_free(network_pdu);
            process_network_pdu_done();
            return;
        }

        // check cache
        uint32_t hash = mesh_network_cache_hash(network_pdu);
        printf("Hash: %08x\n", hash);
        if (mesh_network_cache_find(hash)){
            // found in cache, drop
            printf("Found in cache -> drop packet\n");
            btstack_memory_mesh_network_pdu_free(network_pdu);
            process_network_pdu_done();
            return;
        }

        // set netkey_index
        network_pdu->netkey_index = current_network_key->netkey_index;

        // store in network cache
        mesh_network_cache_add(hash);

        // forward to lower transport layer. message is freed by call to mesh_network_message_processed_by_upper_layer
        (*mesh_network_higher_layer_handler)(MESH_NETWORK_PDU_RECEIVED, network_pdu);
    }

    // done
    process_network_pdu_done();
}

static void process_network_pdu_validate_b(void * arg){
    mesh_network_pdu_t * network_pdu = (mesh_network_pdu_t *) arg;

    //
    printf("PECB: ");
    printf_hexdump(obfuscation_block, 6);

    // de-obfuscate
    unsigned int i;
    for (i=0;i<6;i++){
        network_pdu->data[1+i] = network_pdu_in_validation->data[1+i] ^ obfuscation_block[i];
    }

    if (network_pdu->flags & 1){
        // create network nonce
        mesh_proxy_create_nonce(network_nonce, network_pdu, global_iv_index);
        printf("Proxy Nonce: ");
        printf_hexdump(network_nonce, 13);
    } else {
        // create network nonce
        mesh_network_create_nonce(network_nonce, network_pdu, global_iv_index);
        printf("Network Nonce: ");
        printf_hexdump(network_nonce, 13);
    }

    // 
    uint8_t ctl_ttl     = network_pdu->data[1];
    uint8_t net_mic_len = (ctl_ttl & 0x80) ? 8 : 4;
    uint8_t cypher_len  = network_pdu->len - 7 - net_mic_len;

    printf("Cyper len %u, mic len %u\n", cypher_len, net_mic_len);

    printf("Encryption Key: ");
    printf_hexdump(current_network_key->encryption_key, 16);

    // 034b50057e400000010000

    btstack_crypo_ccm_init(&mesh_network_crypto_request.ccm, current_network_key->encryption_key, network_nonce, cypher_len, 0, net_mic_len);
    btstack_crypto_ccm_decrypt_block(&mesh_network_crypto_request.ccm, cypher_len, &network_pdu_in_validation->data[7], &network_pdu->data[7], &process_network_pdu_validate_d, network_pdu);
}

static void process_network_pdu_validate(mesh_network_pdu_t * network_pdu){
    if (!mesh_network_key_iterator_has_more(&validation_network_key_it)){
        printf("No valid network key found\n");
        btstack_memory_mesh_network_pdu_free(network_pdu);
        process_network_pdu_done();
        return;
    }

    current_network_key = mesh_network_key_iterator_get_next(&validation_network_key_it);

    // calc PECB
    memset(encryption_block, 0, 5);
    big_endian_store_32(encryption_block, 5, global_iv_index);
    memcpy(&encryption_block[9], &network_pdu_in_validation->data[7], 7);
    btstack_crypto_aes128_encrypt(&mesh_network_crypto_request.aes128, current_network_key->privacy_key, encryption_block, obfuscation_block, &process_network_pdu_validate_b, network_pdu);
}


static void process_network_pdu(mesh_network_pdu_t * network_pdu){
    //
    uint8_t nid_ivi = network_pdu_in_validation->data[0];

    // setup pdu object
    network_pdu->data[0] = nid_ivi;
    network_pdu->len     = network_pdu_in_validation->len;
    network_pdu->flags   = network_pdu_in_validation->flags;

    // init provisioning data iterator
    uint8_t nid = nid_ivi & 0x7f;
    // uint8_t iv_index = network_pdu_data[0] >> 7;
    mesh_network_key_iterator_init(&validation_network_key_it, nid);

    process_network_pdu_validate(network_pdu);
}

// static void mesh_network_encrypt_and_obfuscate(mesh_network_pdu_t * network_pdu, void (*callback)(mesh_network_pdu_t * network_pdu)){
//     network_pdu->callback = callback;
// }

static void mesh_network_run(void){
    if (mesh_crypto_active) return;

    if (!btstack_linked_list_empty(&network_pdus_received)){
        mesh_network_pdu_t * decode_pdu = btstack_memory_mesh_network_pdu_get();
        if (!decode_pdu) return; 
        // get encoded network pdu and start processing
        mesh_crypto_active = 1;
        network_pdu_in_validation = (mesh_network_pdu_t *) btstack_linked_list_pop(&network_pdus_received);
        process_network_pdu(decode_pdu);
        return;
    }

    if (!btstack_linked_list_empty(&network_pdus_queued)){
        // get queued network pdu and start processing
        mesh_network_pdu_t * network_pdu = (mesh_network_pdu_t *) btstack_linked_list_pop(&network_pdus_queued);
        mesh_network_send_a(network_pdu);
        return;
    }
}

static void mesh_message_handler (uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){

    mesh_network_pdu_t * network_pdu;

    switch (packet_type){
        case MESH_NETWORK_PACKET:
            // check len. minimal transport PDU len = 1, 32 bit NetMIC -> 13 bytes
            if (size < 13) break;

            printf("received mesh message (len %u): ", size);
            printf_hexdump(packet, size);
            mesh_network_received_message(packet, size);
            break;

        case HCI_EVENT_PACKET:
            switch(packet[0]){
                case HCI_EVENT_MESH_META:
                    switch(packet[2]){
                        case MESH_SUBEVENT_CAN_SEND_NOW:
                            if (btstack_linked_list_empty(&network_pdus_outgoing)) break;
                            network_pdu = (mesh_network_pdu_t *) btstack_linked_list_pop(&network_pdus_outgoing);
                            adv_bearer_send_mesh_message(network_pdu->data, network_pdu->len);
                            // notify upper layer
                            (*mesh_network_higher_layer_handler)(MESH_NETWORK_PDU_SENT, network_pdu);
                            break;
                        default:
                            break;
                    }
                    break;
                default:
                    break;
            }
            break;
    }
}

void mesh_network_init(void){
    adv_bearer_register_for_mesh_message(&mesh_message_handler);
}

void mesh_network_set_higher_layer_handler(void (*packet_handler)(mesh_network_callback_type_t callback_type, mesh_network_pdu_t * network_pdu)){
    mesh_network_higher_layer_handler = packet_handler;
}

void mesh_network_set_proxy_message_handler(void (*packet_handler)(mesh_network_callback_type_t callback_type, mesh_network_pdu_t * network_pdu)){
    mesh_network_proxy_message_handler = packet_handler;
}

void mesh_network_set_primary_element_address(uint16_t addr){
    mesh_network_primary_address = addr;
    mesh_network_num_elements = 1;
}

void mesh_network_received_message(const uint8_t * pdu_data, uint8_t pdu_len){
    // verify len
    if (pdu_len > 29) return;

    // allocate network_pdu
    mesh_network_pdu_t * network_pdu = btstack_memory_mesh_network_pdu_get();
    if (!network_pdu) return;
    memset(network_pdu, 0, sizeof(mesh_network_pdu_t));

    // store data
    memcpy(network_pdu->data, pdu_data, pdu_len);
    network_pdu->len = pdu_len;
    network_pdu->flags = 0; // Network PDU

    // add to list and go
    btstack_linked_list_add_tail(&network_pdus_received, (btstack_linked_item_t *) network_pdu);
    mesh_network_run();

}

void mesh_network_process_proxy_message(const uint8_t * pdu_data, uint8_t pdu_len){
    // verify len
    if (pdu_len > 29) return;

    // allocate network_pdu
    mesh_network_pdu_t * network_pdu = btstack_memory_mesh_network_pdu_get();
    if (!network_pdu) return;
    memset(network_pdu, 0, sizeof(mesh_network_pdu_t));

    // store data
    memcpy(network_pdu->data, pdu_data, pdu_len);
    network_pdu->len = pdu_len;
    network_pdu->flags = 1; // Network PDU

    // add to list and go
    btstack_linked_list_add_tail(&network_pdus_received, (btstack_linked_item_t *) network_pdu);
    mesh_network_run();
}

void mesh_network_send_pdu(mesh_network_pdu_t * network_pdu){
    printf("NetworkPDU(unencrypted): ");
    printf_hexdump(network_pdu->data, network_pdu->len);

    // setup callback
    network_pdu->callback = &mesh_network_send_d;
    network_pdu->flags    = 0;

    // queue up
    btstack_linked_list_add_tail(&network_pdus_queued, (btstack_linked_item_t *) network_pdu);

    // go
    mesh_network_run();
}

void mesh_network_encrypt_proxy_message(mesh_network_pdu_t * network_pdu, void (* callback)(mesh_network_pdu_t * callback)){
    printf("ProxyPDU(unencrypted): ");
    printf_hexdump(network_pdu->data, network_pdu->len);

    // setup callback
    network_pdu->callback = callback;
    network_pdu->flags    = 1;

    // queue up
    btstack_linked_list_add_tail(&network_pdus_queued, (btstack_linked_item_t *) network_pdu);

    // go
    mesh_network_run();
}

/*
 * @brief Setup network pdu header
 * @param netkey_index
 * @param ctl
 * @param ttl
 * @param seq
 * @param dest
 */
void mesh_network_setup_pdu(mesh_network_pdu_t * network_pdu, uint16_t netkey_index, uint8_t nid, uint8_t ctl, uint8_t ttl, uint32_t seq, uint16_t src, uint16_t dest, const uint8_t * transport_pdu_data, uint8_t transport_pdu_len){
    memset(network_pdu, 0, sizeof(mesh_network_pdu_t));
    // set netkey_index
    network_pdu->netkey_index = netkey_index;
    // setup header
    network_pdu->data[network_pdu->len++] = (global_iv_index << 7) |  nid;
    uint8_t ctl_ttl = (ctl << 7) | (ttl & 0x7f);
    network_pdu->data[network_pdu->len++] = ctl_ttl;
    big_endian_store_24(network_pdu->data, 2, seq);
    network_pdu->len += 3;
    big_endian_store_16(network_pdu->data, network_pdu->len, src);
    network_pdu->len += 2;
    big_endian_store_16(network_pdu->data, network_pdu->len, dest);
    network_pdu->len += 2;
    memcpy(&network_pdu->data[network_pdu->len], transport_pdu_data, transport_pdu_len);
    network_pdu->len += transport_pdu_len;
}

void mesh_set_iv_index(uint32_t iv_index){
    global_iv_index = iv_index;
}

uint32_t mesh_get_iv_index(void){
    return  global_iv_index;
}

// Network PDU Getter
uint16_t mesh_network_control(mesh_network_pdu_t * network_pdu){
    return network_pdu->data[1] & 0x80;
}
uint8_t mesh_network_ttl(mesh_network_pdu_t * network_pdu){
    return network_pdu->data[1] & 0x7f;
}
uint32_t mesh_network_seq(mesh_network_pdu_t * network_pdu){
    return big_endian_read_24(network_pdu->data, 2);
}
uint16_t mesh_network_src(mesh_network_pdu_t * network_pdu){
    return big_endian_read_16(network_pdu->data, 5);
}
uint16_t mesh_network_dst(mesh_network_pdu_t * network_pdu){
    return big_endian_read_16(network_pdu->data, 7);
}
int mesh_network_segmented(mesh_network_pdu_t * network_pdu){
    return network_pdu->data[9] & 0x80;
}
uint8_t * mesh_network_pdu_data(mesh_network_pdu_t * network_pdu){
    return &network_pdu->data[9];
}
uint8_t   mesh_network_pdu_len(mesh_network_pdu_t * network_pdu){
    return network_pdu->len - 9;
}

static void mesh_network_dump_network_pdu(mesh_network_pdu_t * network_pdu){
    if (network_pdu){
        printf("- %p: ", network_pdu); printf_hexdump(network_pdu->data, network_pdu->len);
    }
}
static void mesh_network_dump_network_pdus(const char * name, btstack_linked_list_t * list){
    printf("List: %s:\n", name);
    btstack_linked_list_iterator_t it;
    btstack_linked_list_iterator_init(&it, list);
    while (btstack_linked_list_iterator_has_next(&it)){
        mesh_network_pdu_t * network_pdu = (mesh_network_pdu_t*) btstack_linked_list_iterator_next(&it);
        mesh_network_dump_network_pdu(network_pdu);
    }
}
static void mesh_network_reset_network_pdus(btstack_linked_list_t * list){
    while (!btstack_linked_list_empty(list)){
        mesh_network_pdu_t * pdu = (mesh_network_pdu_t *) btstack_linked_list_pop(list);
        btstack_memory_mesh_network_pdu_free(pdu);
    }
}
void mesh_network_dump(void){
    mesh_network_dump_network_pdus("network_pdus_received", &network_pdus_received);
    mesh_network_dump_network_pdus("network_pdus_queued", &network_pdus_queued);
    mesh_network_dump_network_pdus("network_pdus_outgoing", &network_pdus_outgoing);
    printf("network_pdu_in_validation: \n");
    mesh_network_dump_network_pdu(network_pdu_in_validation);
}
void mesh_network_reset(void){
    mesh_network_reset_network_pdus(&network_pdus_received);
    mesh_network_reset_network_pdus(&network_pdus_queued);
    mesh_network_reset_network_pdus(&network_pdus_outgoing);
}

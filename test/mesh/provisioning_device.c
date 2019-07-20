/*
 * Copyright (C) 2017 BlueKitchen GmbH
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

#define __BTSTACK_FILE__ "provisioning_device.c"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ble/mesh/pb_adv.h"
#include "ble/mesh/pb_gatt.h"

#include "ble/mesh/mesh_crypto.h"
#include "classic/rfcomm.h" // for crc8
#include "btstack.h"
#include "provisioning.h"

static void provisioning_attention_timer_set(void);
static void prov_key_generated(void * arg);

// remote ecc
static uint8_t remote_ec_q[64];
static uint8_t dhkey[32];

static btstack_packet_handler_t prov_packet_handler;

static uint8_t  prov_buffer_out[MESH_PROV_MAX_PROXY_PDU];
// ConfirmationInputs = ProvisioningInvitePDUValue || ProvisioningCapabilitiesPDUValue || ProvisioningStartPDUValue || PublicKeyProvisioner || PublicKeyDevice
static uint8_t  prov_confirmation_inputs[1 + 11 + 5 + 64 + 64];
static uint8_t  prov_authentication_method;
static uint8_t  prov_authentication_action;
static uint8_t  prov_public_key_oob_used;
static uint8_t  prov_emit_public_key_oob_active;
static uint8_t  prov_emit_output_oob_active;
static uint8_t  prov_ec_q[64];

static const uint8_t * prov_public_key_oob_q;
static const uint8_t * prov_public_key_oob_d;

// num elements
static uint8_t  prov_num_elements = 1;

// capabilites
static const uint8_t * prov_static_oob_data;

static uint16_t  prov_static_oob_len;
static uint16_t  prov_output_oob_actions;
static uint16_t  prov_input_oob_actions;
static uint8_t   prov_public_key_oob_available;
static uint8_t   prov_static_oob_available;
static uint8_t   prov_output_oob_size;
static uint8_t   prov_input_oob_size;
static uint8_t   prov_error_code;
static uint8_t   prov_waiting_for_outgoing_complete;

static uint8_t                      prov_attention_timer_timeout;
static btstack_timer_source_t       prov_attention_timer;

static btstack_timer_source_t       prov_protocol_timer;

static btstack_crypto_aes128_cmac_t prov_cmac_request;
static btstack_crypto_random_t      prov_random_request;
static btstack_crypto_ecc_p256_t    prov_ecc_p256_request;
static btstack_crypto_ccm_t         prov_ccm_request;

// ConfirmationDevice
static uint8_t confirmation_device[16];
// ConfirmationSalt
static uint8_t confirmation_salt[16];
// ConfirmationKey
static uint8_t confirmation_key[16];
// RandomDevice
static uint8_t random_device[16];
// ProvisioningSalt
static uint8_t provisioning_salt[16];
// AuthValue
static uint8_t auth_value[16];
// SessionKey
static uint8_t session_key[16];
// SessionNonce
static uint8_t session_nonce[16];
// EncProvisioningData
static uint8_t enc_provisioning_data[25];
// ProvisioningData
static uint8_t provisioning_data[25];


// DeviceKey
static uint8_t device_key[16];
// NetKey
static uint8_t  net_key[16];
// NetKeyIndex
static uint16_t net_key_index;
// k2: NID (7), EncryptionKey (128), PrivacyKey (128) 
static uint8_t k2_result[33];

static uint8_t  flags;

static uint32_t iv_index;
static uint16_t unicast_address;

static const uint8_t id128_tag[] = { 'i', 'd', '1', '2', '8', 0x01};

// AES-CMAC_ZERO('nhbk')
static const uint8_t mesh_salt_nhbk[] = {
    0x2c, 0x24, 0x61, 0x9a, 0xb7, 0x93, 0xc1, 0x23, 0x3f, 0x6e, 0x22, 0x67, 0x38, 0x39, 0x3d, 0xec, };

// AES-CMAC_ZERO('nkik')
static const uint8_t mesh_salt_nkik[] = {
    0xF8, 0x79, 0x5A, 0x1A, 0xAB, 0xF1, 0x82, 0xE4, 0xF1, 0x63, 0xD8, 0x6E, 0x24, 0x5E, 0x19, 0xF4};


typedef enum {
    DEVICE_W4_INVITE,
    DEVICE_SEND_CAPABILITIES,
    DEVICE_W4_START,
    DEVICE_W4_INPUT_OOK,
    DEVICE_SEND_INPUT_COMPLETE,
    DEVICE_W4_PUB_KEY,
    DEVICE_SEND_PUB_KEY,
    DEVICE_W4_CONFIRM,
    DEVICE_SEND_CONFIRM,
    DEVICE_W4_RANDOM,
    DEVICE_SEND_RANDOM,
    DEVICE_W4_DATA,
    DEVICE_SEND_COMPLETE,
    DEVICE_SEND_ERROR,
} device_state_t;

static device_state_t device_state;
static uint16_t pb_transport_cid;
// derived
static uint8_t network_id[8];
static uint8_t beacon_key[16];
static uint8_t identity_key[16];
static pb_type_t pb_type;

static void pb_send_pdu(uint16_t transport_cid, const uint8_t * buffer, uint16_t buffer_size){
    switch (pb_type){
        case PB_TYPE_ADV:
            pb_adv_send_pdu(transport_cid, buffer, buffer_size);    
            break;
        case PB_TYPE_GATT:
            pb_gatt_send_pdu(transport_cid, buffer, buffer_size);    
            break;
    }
}

static void pb_close_link(uint16_t transport_cid, uint8_t reason){
    switch (pb_type){
        case PB_TYPE_ADV:
            pb_adv_close_link(transport_cid, reason);    
            break;
        case PB_TYPE_GATT:
            pb_gatt_close_link(transport_cid, reason);    
            break;
    }
}

static void provisioning_emit_event(uint16_t pb_adv_cid, uint8_t mesh_subevent){
    if (!prov_packet_handler) return;
    uint8_t event[5] = { HCI_EVENT_MESH_META, 3, mesh_subevent};
    little_endian_store_16(event, 3, pb_adv_cid);
    prov_packet_handler(HCI_EVENT_PACKET, 0, event, sizeof(event));
}

static void provisioning_emit_output_oob_event(uint16_t pb_adv_cid, uint32_t number){
    if (!prov_packet_handler) return;
    uint8_t event[9] = { HCI_EVENT_MESH_META, 7, MESH_PB_PROV_START_EMIT_OUTPUT_OOB};
    little_endian_store_16(event, 3, pb_adv_cid);
    little_endian_store_16(event, 5, number);
    prov_packet_handler(HCI_EVENT_PACKET, 0, event, sizeof(event));
}

static void provisioning_emit_attention_timer_event(uint16_t pb_adv_cid, uint8_t timer_s){
    if (!prov_packet_handler) return;
    uint8_t event[4] = { HCI_EVENT_MESH_META, 7, MESH_PB_PROV_ATTENTION_TIMER};
    event[3] = timer_s;
    prov_packet_handler(HCI_EVENT_PACKET, 0, event, sizeof(event));
}

static void provisiong_timer_handler(btstack_timer_source_t * ts){
    UNUSED(ts);
    printf("Provisioning Protocol Timeout -> Close Link!\n");
    pb_close_link(1, 1);
}

// The provisioning protocol shall have a minimum timeout of 60 seconds that is reset
// each time a provisioning protocol PDU is sent or received
static void provisioning_timer_start(void){
    btstack_run_loop_remove_timer(&prov_protocol_timer);
    btstack_run_loop_set_timer_handler(&prov_protocol_timer, &provisiong_timer_handler);
    btstack_run_loop_set_timer(&prov_protocol_timer, PROVISIONING_PROTOCOL_TIMEOUT_MS);
    btstack_run_loop_add_timer(&prov_protocol_timer);
}

static void provisioning_timer_stop(void){
    btstack_run_loop_remove_timer(&prov_protocol_timer);
}

static void provisioning_attention_timer_timeout(btstack_timer_source_t * ts){
    UNUSED(ts);
    if (prov_attention_timer_timeout == 0) return;
    prov_attention_timer_timeout--;
    provisioning_attention_timer_set();
}

static void provisioning_attention_timer_set(void){
    provisioning_emit_attention_timer_event(1, prov_attention_timer_timeout);
    if (prov_attention_timer_timeout){
        btstack_run_loop_set_timer_handler(&prov_attention_timer, &provisioning_attention_timer_timeout);
        btstack_run_loop_set_timer(&prov_attention_timer, 1000);
        btstack_run_loop_add_timer(&prov_attention_timer);
    }
}

// Outgoing Provisioning PDUs
static void provisioning_send_provisioning_error(void){
    // setup response 
    prov_buffer_out[0] = MESH_PROV_FAILED;
    prov_buffer_out[1] = prov_error_code;
    pb_send_pdu(pb_transport_cid, prov_buffer_out, 2);
}

static void provisioning_send_capabilites(void){
    // setup response 
    prov_buffer_out[0] = MESH_PROV_CAPABILITIES;

    /* Number of Elements supported */
    prov_buffer_out[1] = prov_num_elements;

    /* Supported algorithms - FIPS P-256 Eliptic Curve */
    big_endian_store_16(prov_buffer_out, 2, 1);

    /* Public Key Type - Public Key OOB information available */
    prov_buffer_out[4] = prov_public_key_oob_available;

    /* Static OOB Type - Static OOB information available */
    prov_buffer_out[5] = prov_static_oob_available; 

    /* Output OOB Size - max of 8 */
    prov_buffer_out[6] = prov_output_oob_size; 

    /* Output OOB Action */
    big_endian_store_16(prov_buffer_out, 7, prov_output_oob_actions);

    /* Input OOB Size - max of 8*/
    prov_buffer_out[9] = prov_input_oob_size; 

    /* Input OOB Action */
    big_endian_store_16(prov_buffer_out, 10, prov_input_oob_actions);

    // store for confirmation inputs: len 11
    memcpy(&prov_confirmation_inputs[1], &prov_buffer_out[1], 11);

    // send

    pb_send_pdu(pb_transport_cid, prov_buffer_out, 12);    
}

static void provisioning_send_public_key(void){
    // setup response 
    prov_buffer_out[0] = MESH_PROV_PUB_KEY;
    memcpy(&prov_buffer_out[1], prov_ec_q, 64);

    // store for confirmation inputs: len 64
    memcpy(&prov_confirmation_inputs[81], &prov_buffer_out[1], 64);

    // send
    pb_send_pdu(pb_transport_cid, prov_buffer_out, 65);
}

static void provisioning_send_input_complete(void){
    // setup response 
    prov_buffer_out[0] = MESH_PROV_INPUT_COMPLETE;

    // send
    pb_send_pdu(pb_transport_cid, prov_buffer_out, 17);
}
static void provisioning_send_confirm(void){
    // setup response 
    prov_buffer_out[0] = MESH_PROV_CONFIRM;
    memcpy(&prov_buffer_out[1], confirmation_device, 16);

    // send
    pb_send_pdu(pb_transport_cid, prov_buffer_out, 17);
}

static void provisioning_send_random(void){
    // setup response 
    prov_buffer_out[0] = MESH_PROV_RANDOM;
    memcpy(&prov_buffer_out[1],  random_device, 16);

    // send pdu
    pb_send_pdu(pb_transport_cid, prov_buffer_out, 17);
}

static void provisioning_send_complete(void){
    // setup response 
    prov_buffer_out[0] = MESH_PROV_COMPLETE;

    // send pdu
    pb_send_pdu(pb_transport_cid, prov_buffer_out, 1);
}

static void provisioning_done(void){
    if (prov_emit_public_key_oob_active){
        prov_emit_public_key_oob_active = 0;
        provisioning_emit_event(1, MESH_PB_PROV_STOP_EMIT_PUBLIC_KEY_OOB);
    }
    if (prov_emit_output_oob_active){
        prov_emit_output_oob_active = 0;
        provisioning_emit_event(1, MESH_PB_PROV_STOP_EMIT_OUTPUT_OOB);
    }
    if (prov_attention_timer_timeout){
        prov_attention_timer_timeout = 0;
        provisioning_emit_attention_timer_event(1, 0);        
    }
    device_state = DEVICE_W4_INVITE;

    // generate new public key
    printf("Generate new public key\n");
    btstack_crypto_ecc_p256_generate_key(&prov_ecc_p256_request, prov_ec_q, &prov_key_generated, NULL);
}

static void provisioning_handle_auth_value_output_oob(void * arg){
    // limit auth value to single digit
    auth_value[15] = auth_value[15] % 9 + 1;

    printf("Output OOB: %u\n", auth_value[15]);

    // emit output oob value
    provisioning_emit_output_oob_event(1, auth_value[15]);
    prov_emit_output_oob_active = 1;
}

static void provisioning_public_key_exchange_complete(void){

    // reset auth_value
    memset(auth_value, 0, sizeof(auth_value));

    // handle authentication method
    switch (prov_authentication_method){
        case 0x00:
            device_state = DEVICE_W4_CONFIRM;
            break;        
        case 0x01:
            memcpy(&auth_value[16-prov_static_oob_len], prov_static_oob_data, prov_static_oob_len);
            device_state = DEVICE_W4_CONFIRM;
            break;
        case 0x02:
            device_state = DEVICE_W4_CONFIRM;
            printf("Generate random for auth_value\n");
            // generate single byte of random data to use for authentication
            btstack_crypto_random_generate(&prov_random_request, &auth_value[15], 1, &provisioning_handle_auth_value_output_oob, NULL);
            break;
        case 0x03:
            // Input OOB
            printf("Input OOB requested\n");
            provisioning_emit_event(1, MESH_PB_PROV_INPUT_OOB_REQUEST);
            device_state = DEVICE_W4_INPUT_OOK;
            break;
        default:
            break;
    }
}

static void provisioning_run(void){
    printf("provisioning_run: state %x, wait for outgoing complete %u\n", device_state, prov_waiting_for_outgoing_complete);
    if (prov_waiting_for_outgoing_complete) return;
    int start_timer = 1;
    switch (device_state){
        case DEVICE_SEND_ERROR:
            start_timer = 0;    // game over
            prov_waiting_for_outgoing_complete = 1;
            provisioning_send_provisioning_error();
            provisioning_done();
            break;
        case DEVICE_SEND_CAPABILITIES:
            device_state = DEVICE_W4_START;
            prov_waiting_for_outgoing_complete = 1;
            provisioning_send_capabilites();
            break;
        case DEVICE_SEND_INPUT_COMPLETE:
            device_state = DEVICE_W4_CONFIRM;
            prov_waiting_for_outgoing_complete = 1;
            provisioning_send_input_complete();
            break;
        case DEVICE_SEND_PUB_KEY:
            prov_waiting_for_outgoing_complete = 1;
            provisioning_send_public_key();
            provisioning_public_key_exchange_complete();
            break;
        case DEVICE_SEND_CONFIRM:
            device_state = DEVICE_W4_RANDOM;
            prov_waiting_for_outgoing_complete = 1;
            provisioning_send_confirm();
            break;
        case DEVICE_SEND_RANDOM:
            device_state = DEVICE_W4_DATA;
            prov_waiting_for_outgoing_complete = 1;
            provisioning_send_random();
            break;
        case DEVICE_SEND_COMPLETE:
            prov_waiting_for_outgoing_complete = 1;
            provisioning_send_complete();
            provisioning_done();
            break;
        default:
            return;
    }
    if (start_timer){
        provisioning_timer_start();
    }
}

static void provisioning_handle_provisioning_error(uint8_t error_code){
    printf("PROVISIONING ERROR\n");
    provisioning_timer_stop();
    prov_error_code = error_code;
    device_state = DEVICE_SEND_ERROR;
    provisioning_run();
}

static void provisioning_handle_invite(uint8_t *packet, uint16_t size){

    if (size != 1) return;

    // store for confirmation inputs: len 1
    memcpy(&prov_confirmation_inputs[0], packet, 1);

    // handle invite message
    prov_attention_timer_timeout = packet[0];
    provisioning_attention_timer_set();

    device_state = DEVICE_SEND_CAPABILITIES;
    provisioning_run();
}

static void provisioning_handle_start(uint8_t * packet, uint16_t size){

    if (size != 5) return;

    // validate Algorithm
    int ok = 1;
    if (packet[0] > 0x00){
        ok = 0;
    }
    // validate Publik Key
    if (packet[1] > 0x01){
        ok = 0;
    }
    // validate Authentication Method
    switch (packet[2]){
        case 0:
        case 1:
            if (packet[3] != 0 || packet[4] != 0){
                ok = 0;
                break;
            }
            break;
        case 2:
            if (packet[3] > 0x04 || packet[4] == 0 || packet[4] > 0x08){
                ok = 0;
                break;
            }
            break;
        case 3:
            if (packet[3] > 0x03 || packet[4] == 0 || packet[4] > 0x08){
                ok = 0;
                break;
            }
            break;
    }
    if (!ok){
        printf("PROV_START arguments incorrect\n");
        provisioning_handle_provisioning_error(0x02);
        return;
    }

    // store for confirmation inputs: len 5
    memcpy(&prov_confirmation_inputs[12], packet, 5);

    // public key oob
    prov_public_key_oob_used = packet[1];

    // authentication method
    prov_authentication_method = packet[2];

    // start emit public OOK if specified
    if (prov_public_key_oob_available && prov_public_key_oob_used){
        provisioning_emit_event(1, MESH_PB_PROV_START_EMIT_PUBLIC_KEY_OOB);
    }

    printf("PublicKey:  %02x\n", prov_public_key_oob_used);
    printf("AuthMethod: %02x\n", prov_authentication_method);

    device_state = DEVICE_W4_PUB_KEY;
    provisioning_run();
}

static void provisioning_handle_public_key_dhkey(void * arg){
    UNUSED(arg);

    printf("DHKEY: ");
    printf_hexdump(dhkey, sizeof(dhkey));

    // skip sending own public key when public key oob is used
    if (prov_public_key_oob_available && prov_public_key_oob_used){
        // just copy key for confirmation inputs
        memcpy(&prov_confirmation_inputs[81], prov_ec_q, 64);
        provisioning_public_key_exchange_complete();
    } else {
        // queue public key pdu
        printf("DEVICE_SEND_PUB_KEY\n");
        device_state = DEVICE_SEND_PUB_KEY;
    }
    provisioning_run();
}

static void provisioning_handle_public_key(uint8_t *packet, uint16_t size){

    // validate public key
    if (size != sizeof(remote_ec_q) || btstack_crypto_ecc_p256_validate_public_key(packet) != 0){
        printf("Public Key invalid, abort provisioning\n");
        provisioning_handle_provisioning_error(0x07);   // Unexpected Error
        return;
    }

    // stop emit public OOK if specified and send to crypto module
    if (prov_public_key_oob_available && prov_public_key_oob_used){
        provisioning_emit_event(1, MESH_PB_PROV_STOP_EMIT_PUBLIC_KEY_OOB);

        printf("Replace generated ECC with Public Key OOB:");
        memcpy(prov_ec_q, prov_public_key_oob_q, 64);
        printf_hexdump(prov_ec_q, sizeof(prov_ec_q));
        btstack_crypto_ecc_p256_set_key(prov_public_key_oob_q, prov_public_key_oob_d);
    }

    // store for confirmation inputs: len 64
    memcpy(&prov_confirmation_inputs[17], packet, 64);

    // store remote q
    memcpy(remote_ec_q, packet, sizeof(remote_ec_q));

    // calculate DHKey
    btstack_crypto_ecc_p256_calculate_dhkey(&prov_ecc_p256_request, remote_ec_q, dhkey, provisioning_handle_public_key_dhkey, NULL);
}

static void provisioning_handle_confirmation_device_calculated(void * arg){

    UNUSED(arg);

    printf("ConfirmationDevice: ");
    printf_hexdump(confirmation_device, sizeof(confirmation_device));

    device_state = DEVICE_SEND_CONFIRM;
    provisioning_run();
}

static void provisioning_handle_confirmation_random_device(void * arg){
    // re-use prov_confirmation_inputs buffer
    memcpy(&prov_confirmation_inputs[0],  random_device, 16);
    memcpy(&prov_confirmation_inputs[16], auth_value, 16);

    // calc confirmation device
    btstack_crypto_aes128_cmac_message(&prov_cmac_request, confirmation_key, 32, prov_confirmation_inputs, confirmation_device, &provisioning_handle_confirmation_device_calculated, NULL);
}

static void provisioning_handle_confirmation_k1_calculated(void * arg){
    printf("ConfirmationKey:   ");
    printf_hexdump(confirmation_key, sizeof(confirmation_key));

    printf("AuthValue: ");
    printf_hexdump(auth_value, 16);

    // generate random_device
    btstack_crypto_random_generate(&prov_random_request,random_device, 16, &provisioning_handle_confirmation_random_device, NULL);
}

static void provisioning_handle_confirmation_s1_calculated(void * arg){
    UNUSED(arg);

    // ConfirmationSalt
    printf("ConfirmationSalt:   ");
    printf_hexdump(confirmation_salt, sizeof(confirmation_salt));

    // ConfirmationKey
    mesh_k1(&prov_cmac_request, dhkey, sizeof(dhkey), confirmation_salt, (const uint8_t*) "prck", 4, confirmation_key, &provisioning_handle_confirmation_k1_calculated, NULL);
}

static void provisioning_handle_confirmation(uint8_t *packet, uint16_t size){

    UNUSED(size);
    UNUSED(packet);

    // 
    if (prov_emit_output_oob_active){
        prov_emit_output_oob_active = 0;
        provisioning_emit_event(1, MESH_PB_PROV_STOP_EMIT_OUTPUT_OOB);
    }

    // CalculationInputs
    printf("ConfirmationInputs: ");
    printf_hexdump(prov_confirmation_inputs, sizeof(prov_confirmation_inputs));

    // calculate s1
    btstack_crypto_aes128_cmac_zero(&prov_cmac_request, sizeof(prov_confirmation_inputs), prov_confirmation_inputs, confirmation_salt, &provisioning_handle_confirmation_s1_calculated, NULL);
}

// PROV_RANDOM
static void provisioning_handle_random_session_nonce_calculated(void * arg){
    UNUSED(arg);

    // The nonce shall be the 13 least significant octets == zero most significant octets
    uint8_t temp[13];
    memcpy(temp, &session_nonce[3], 13);
    memcpy(session_nonce, temp, 13);

    // SessionNonce
    printf("SessionNonce:   ");
    printf_hexdump(session_nonce, 13);

    device_state = DEVICE_SEND_RANDOM;
    provisioning_run();
}

static void provisioning_handle_random_session_key_calculated(void * arg){
    UNUSED(arg);

    // SessionKey
    printf("SessionKey:   ");
    printf_hexdump(session_key, sizeof(session_key));

    // SessionNonce
    mesh_k1(&prov_cmac_request, dhkey, sizeof(dhkey), provisioning_salt, (const uint8_t*) "prsn", 4, session_nonce, &provisioning_handle_random_session_nonce_calculated, NULL);
}

static void provisioning_handle_random_s1_calculated(void * arg){

    UNUSED(arg);
    
    // ProvisioningSalt
    printf("ProvisioningSalt:   ");
    printf_hexdump(provisioning_salt, sizeof(provisioning_salt));

    // SessionKey
    mesh_k1(&prov_cmac_request, dhkey, sizeof(dhkey), provisioning_salt, (const uint8_t*) "prsk", 4, session_key, &provisioning_handle_random_session_key_calculated, NULL);
}

static void provisioning_handle_random(uint8_t *packet, uint16_t size){

    UNUSED(size);
    UNUSED(packet);

    // TODO: validate Confirmation

    // calc ProvisioningSalt = s1(ConfirmationSalt || RandomProvisioner || RandomDevice)
    memcpy(&prov_confirmation_inputs[0], confirmation_salt, 16);
    memcpy(&prov_confirmation_inputs[16], packet, 16);
    memcpy(&prov_confirmation_inputs[32], random_device, 16);
    btstack_crypto_aes128_cmac_zero(&prov_cmac_request, 48, prov_confirmation_inputs, provisioning_salt, &provisioning_handle_random_s1_calculated, NULL);
}

// PROV_DATA
static void provisioning_handle_data_k2_calculated(void * arg){
    // Dump
    printf("NID: %02x\n", k2_result[0]);
    printf("EncryptionKey: ");
    printf_hexdump(&k2_result[1], 16);
    printf("PrivacyKey: ");
    printf_hexdump(&k2_result[17], 16);

    // 
    provisioning_timer_stop();

    // notify client
    provisioning_emit_event(1, MESH_PB_PROV_COMPLETE);

    device_state = DEVICE_SEND_COMPLETE;
    provisioning_run();
}

static void provisioning_handle_beacon_key_calculated(void *arg){
    printf("IdentityKey: ");
    printf_hexdump(identity_key, 16);

    // calc k2
    mesh_k2(&prov_cmac_request, net_key, k2_result, &provisioning_handle_data_k2_calculated, NULL);
}


static void provisioning_handle_identity_key_calculated(void *arg){
    printf("BeaconKey: ");
    printf_hexdump(beacon_key, 16);

    // calc identity key
    mesh_k1(&prov_cmac_request, net_key, 16, mesh_salt_nkik, id128_tag, sizeof(id128_tag), identity_key, &provisioning_handle_beacon_key_calculated, NULL);
}

static void provisioning_handle_data_network_id_calculated(void * arg){
    // dump
    printf("Network ID: ");
    printf_hexdump(network_id, 8);
    // calc k1 using 
    mesh_k1(&prov_cmac_request, net_key, 16, mesh_salt_nhbk, id128_tag, sizeof(id128_tag), beacon_key, &provisioning_handle_identity_key_calculated, NULL);
}

static void provisioning_handle_data_device_key(void * arg){
    // dump
    printf("DeviceKey: ");
    printf_hexdump(device_key, 16);

    // calculate Network ID
    mesh_k3(&prov_cmac_request, net_key, network_id, provisioning_handle_data_network_id_calculated, NULL);
}

static void provisioning_handle_data_ccm(void * arg){

    UNUSED(arg);

    // validate MIC?
    uint8_t mic[8];
    btstack_crypto_ccm_get_authentication_value(&prov_ccm_request, mic);
    printf("MIC: ");
    printf_hexdump(mic, 8);

    // sort provisoning data
    memcpy(net_key, provisioning_data, 16);
    net_key_index = big_endian_read_16(provisioning_data, 16);
    flags = provisioning_data[18];
    iv_index = big_endian_read_32(provisioning_data, 19);
    unicast_address = big_endian_read_16(provisioning_data, 23);

    // dump
    printf("NetKey: ");
    printf_hexdump(net_key, 16);
    printf("NetKeyIndex: %04x\n", net_key_index);
    printf("Flags: %02x\n", flags);
    printf("IVIndex: %04x\n", iv_index);
    printf("UnicastAddress: %02x\n", unicast_address);

    // DeviceKey
    mesh_k1(&prov_cmac_request, dhkey, sizeof(dhkey), provisioning_salt, (const uint8_t*) "prdk", 4, device_key, &provisioning_handle_data_device_key, NULL);
}

static void provisioning_handle_data(uint8_t *packet, uint16_t size){

    UNUSED(size);

    memcpy(enc_provisioning_data, packet, 25);

    // decode response
    btstack_crypto_ccm_init(&prov_ccm_request, session_key, session_nonce, 25, 0, 8);
    btstack_crypto_ccm_decrypt_block(&prov_ccm_request, 25, enc_provisioning_data, provisioning_data, &provisioning_handle_data_ccm, NULL);
}

static void provisioning_handle_unexpected_pdu(uint8_t *packet, uint16_t size){
    printf("Unexpected PDU #%u in state #%u\n", packet[0], (int) device_state);
    provisioning_handle_provisioning_error(0x03);    
}

static void provisioning_handle_pdu(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){

    if (size < 1) return;

    switch (packet_type){
        case HCI_EVENT_PACKET:
            if (packet[0] != HCI_EVENT_MESH_META)  break;
            switch (packet[2]){
                case MESH_PB_TRANSPORT_LINK_OPEN:
                    pb_transport_cid = mesh_pb_transport_link_open_event_get_pb_transport_cid(packet);
                    pb_type = mesh_pb_transport_link_open_event_get_pb_type(packet);
                    printf("Link opened, reset state, transport cid 0x%02x, PB type %d\n", pb_transport_cid, pb_type);
                    provisioning_done();
                    break;
                case MESH_PB_TRANSPORT_PDU_SENT:
                    printf("Outgoing packet acked\n");
                    prov_waiting_for_outgoing_complete = 0;
                    break;                    
                case MESH_PB_TRANSPORT_LINK_CLOSED:
                    printf("Link close, reset state\n");
                    pb_transport_cid = MESH_PB_TRANSPORT_INVALID_CID;
                    provisioning_done();
                    break;
            }
            break;
        case PROVISIONING_DATA_PACKET:
            // check state
            switch (device_state){
                case DEVICE_W4_INVITE:
                    if (packet[0] != MESH_PROV_INVITE) provisioning_handle_unexpected_pdu(packet, size);
                    printf("MESH_PROV_INVITE: ");
                    printf_hexdump(&packet[1], size-1);
                    provisioning_handle_invite(&packet[1], size-1);
                    break;
                case DEVICE_W4_START:
                    if (packet[0] != MESH_PROV_START) provisioning_handle_unexpected_pdu(packet, size);
                    printf("MESH_PROV_START:  ");
                    printf_hexdump(&packet[1], size-1);
                    provisioning_handle_start(&packet[1], size-1);
                    break;
                case DEVICE_W4_PUB_KEY:
                    if (packet[0] != MESH_PROV_PUB_KEY) provisioning_handle_unexpected_pdu(packet, size);
                    printf("MESH_PROV_PUB_KEY: ");
                    printf_hexdump(&packet[1], size-1);
                    provisioning_handle_public_key(&packet[1], size-1);
                    break;
                case DEVICE_W4_CONFIRM:
                    if (packet[0] != MESH_PROV_CONFIRM) provisioning_handle_unexpected_pdu(packet, size);
                    printf("MESH_PROV_CONFIRM: ");
                    printf_hexdump(&packet[1], size-1);
                    provisioning_handle_confirmation(&packet[1], size-1);
                    break;
                case DEVICE_W4_RANDOM:
                    if (packet[0] != MESH_PROV_RANDOM) provisioning_handle_unexpected_pdu(packet, size);
                    printf("MESH_PROV_RANDOM:  ");
                    printf_hexdump(&packet[1], size-1);
                    provisioning_handle_random(&packet[1], size-1);
                    break;
                case DEVICE_W4_DATA:
                    if (packet[0] != MESH_PROV_DATA) provisioning_handle_unexpected_pdu(packet, size);
                    printf("MESH_PROV_DATA:  ");
                    provisioning_handle_data(&packet[1], size-1);
                    break;
                default:
                    break;
            }
            break;     
        default:
            break;
    }
    provisioning_run();
}

static void prov_key_generated(void * arg){
    UNUSED(arg);
    printf("ECC-P256: ");
    printf_hexdump(prov_ec_q, sizeof(prov_ec_q));
    // allow override
    if (prov_public_key_oob_available){
        printf("Replace generated ECC with Public Key OOB:");
        memcpy(prov_ec_q, prov_public_key_oob_q, 64);
        printf_hexdump(prov_ec_q, sizeof(prov_ec_q));
        btstack_crypto_ecc_p256_set_key(prov_public_key_oob_q, prov_public_key_oob_d);
    }
}

void provisioning_device_init(const uint8_t * device_uuid){
    // setup PB ADV
    pb_adv_init(device_uuid);
    pb_adv_register_packet_handler(&provisioning_handle_pdu);
    // setup PB GATT
    pb_gatt_init(device_uuid);
    pb_gatt_register_packet_handler(&provisioning_handle_pdu);
    
    pb_transport_cid = MESH_PB_TRANSPORT_INVALID_CID;
    
    // init provisioning state
    provisioning_done();

    // generate public key
    btstack_crypto_ecc_p256_generate_key(&prov_ecc_p256_request, prov_ec_q, &prov_key_generated, NULL);
}

void provisioning_device_register_packet_handler(btstack_packet_handler_t packet_handler){
    prov_packet_handler = packet_handler;
}

void provisioning_device_set_public_key_oob(const uint8_t * public_key, const uint8_t * private_key){
    prov_public_key_oob_q = public_key;
    prov_public_key_oob_d = private_key;
    prov_public_key_oob_available = 1;
    btstack_crypto_ecc_p256_set_key(prov_public_key_oob_q, prov_public_key_oob_d);
}

void provisioning_device_set_static_oob(uint16_t static_oob_len, const uint8_t * static_oob_data){
    prov_static_oob_available = 1;
    prov_static_oob_data = static_oob_data;
    prov_static_oob_len  = btstack_min(static_oob_len, 16);
}

void provisioning_device_set_output_oob_actions(uint16_t supported_output_oob_action_types, uint8_t max_oob_output_size){
    prov_output_oob_actions = supported_output_oob_action_types;
    prov_output_oob_size    = max_oob_output_size;
}

void provisioning_device_set_input_oob_actions(uint16_t supported_input_oob_action_types, uint8_t max_oob_input_size){
    prov_input_oob_actions = supported_input_oob_action_types;
    prov_input_oob_size    = max_oob_input_size;
}

void provisioning_device_input_oob_complete_numeric(uint16_t pb_adv_cid, uint32_t input_oob){
    UNUSED(pb_adv_cid);
    if (device_state != DEVICE_W4_INPUT_OOK) return;

    // store input_oob as auth value
    big_endian_store_32(auth_value, 12, input_oob);
    device_state = DEVICE_SEND_INPUT_COMPLETE;
    provisioning_run();
}

void provisioning_device_input_oob_complete_alphanumeric(uint16_t pb_adv_cid, const uint8_t * input_oob_data, uint16_t input_oob_len){
    UNUSED(pb_adv_cid);
    if (device_state != DEVICE_W4_INPUT_OOK) return;

    // store input_oob and fillup with zeros
    input_oob_len = btstack_min(input_oob_len, 16);
    memset(auth_value, 0, 16);
    memcpy(auth_value, input_oob_data, input_oob_len);
    device_state = DEVICE_SEND_INPUT_COMPLETE;
    provisioning_run();
}


uint8_t provisioning_device_data_get_flags(void){
    return flags;
}
uint16_t provisioning_device_data_get_unicast_address(void){
    return unicast_address;
}
const uint8_t * provisioning_device_data_get_device_key(void){
    return device_key;
}
const uint8_t * provisioning_device_data_get_network_id(void){
    return network_id;
}
uint32_t provisioning_device_data_get_iv_index(void){
    return iv_index;
}
const uint8_t * provisioning_device_data_get_beacon_key(void){
    return beacon_key;
}
const uint8_t * provisioning_device_data_get_identity_key(void){
    return identity_key;
}
uint8_t provisioning_device_data_get_nid(void){
    return k2_result[0];
}
const uint8_t * provisioning_device_data_get_encryption_key(void){
    return &k2_result[1];
}
const uint8_t * provisioning_device_data_get_privacy_key(void){
    return  &k2_result[17];
}

/*
 * Copyright (C) 2019 BlueKitchen GmbH
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

#define __BTSTACK_FILE__ "mesh_generic_client.c"

#include <string.h>
#include <stdio.h>
#include "mesh_generic_client.h"
#include "btstack_util.h"
#include "mesh_generic_model.h"
#include "mesh/mesh_network.h"
#include "mesh/mesh_upper_transport.h"
#include "mesh_keys.h"
#include "mesh_access.h"
#include "mesh_foundation.h"
#include "bluetooth_company_id.h"
#include "btstack_memory.h"
#include "btstack_debug.h"

static void generic_client_send_message(uint16_t src, uint16_t dest, uint16_t netkey_index, uint16_t appkey_index, mesh_pdu_t *pdu){
    uint8_t  ttl  = mesh_foundation_default_ttl_get();
    mesh_upper_transport_setup_access_pdu_header(pdu, netkey_index, appkey_index, ttl, src, dest, 0);
    mesh_upper_transport_send_access_pdu(pdu);
}

void mesh_generic_on_off_client_register_packet_handler(mesh_model_t *mesh_model, btstack_packet_handler_t transition_events_packet_handler){
    if (transition_events_packet_handler == NULL){
        log_error("mesh_generic_on_off_client_register_packet_handler called with NULL callback");
        return;
    }
    if (mesh_model == NULL){
        log_error("mesh_generic_on_off_client_register_packet_handler called with NULL mesh_model");
        return;
    }
    mesh_model->model_packet_handler = &transition_events_packet_handler;
}

// Generic On Off State

static void generic_on_off_status_handler(mesh_model_t *mesh_model, mesh_pdu_t * pdu){
    if (!mesh_model->model_packet_handler){
        log_error("model_packet_handler == NULL");
    }

    mesh_access_parser_state_t parser;
    mesh_access_parser_init(&parser, (mesh_pdu_t*) pdu);
    
    uint8_t present_value = mesh_access_parser_get_u8(&parser);
    uint8_t target_value = 0;
    uint8_t remaining_time_gdtt = 0;

    if (mesh_access_parser_available(&parser) == 2){
        target_value = mesh_access_parser_get_u8(&parser);
        remaining_time_gdtt = mesh_access_parser_get_u8(&parser);
    }

    uint8_t event[14] = {HCI_EVENT_MESH_META, 12, MESH_SUBEVENT_GENERIC_ON_OFF_STATUS};
    int pos = 0;
    event[pos++] = HCI_EVENT_MESH_META;
    // reserve for size
    pos++;
    event[pos++] = MESH_SUBEVENT_GENERIC_ON_OFF_STATUS;

    // element index
    event[pos++] = mesh_model->element->element_index; 
    // model_id
    little_endian_store_32(event, pos, mesh_model->model_identifier);
    pos += 4;
    
    event[pos++] = present_value;
    event[pos++] = target_value;
    little_endian_store_32(event, pos, (uint32_t) mesh_access_time_gdtt2ms(remaining_time_gdtt));
    pos += 4;
    event[1] = pos - 2;

    (*mesh_model->model_packet_handler)(HCI_EVENT_PACKET, 0, event, pos);
    mesh_access_message_processed(pdu);
}

// Generic On Off Message

const mesh_access_message_t mesh_generic_on_off_set_with_transition = {
        MESH_GENERIC_ON_OFF_SET, "1111"
};

const mesh_access_message_t mesh_generic_on_off_set_instantaneous = {
        MESH_GENERIC_ON_OFF_SET, "11"
};

const mesh_access_message_t mesh_generic_on_off_set_unacknowledged_with_transition = {
        MESH_GENERIC_ON_OFF_SET_UNACKNOWLEDGED, "1111"
};

const mesh_access_message_t mesh_generic_on_off_set_unacknowledged_instantaneous = {
        MESH_GENERIC_ON_OFF_SET_UNACKNOWLEDGED, "11"
};

const mesh_access_message_t mesh_generic_on_off_get = {
        MESH_GENERIC_ON_OFF_GET, ""
};

const static mesh_operation_t mesh_generic_on_off_model_operations[] = {
    { MESH_GENERIC_ON_OFF_STATUS, 0, generic_on_off_status_handler },
    { 0, 0, NULL }
};

const mesh_operation_t * mesh_generic_on_off_client_get_operations(void){
    return mesh_generic_on_off_model_operations;
}

static uint8_t mesh_generic_on_off_client_set_with_transition_message(mesh_model_t *mesh_model, const mesh_access_message_t * message_template, 
    uint16_t dest, uint16_t netkey_index, uint16_t appkey_index, 
    uint8_t on_off_value, uint8_t transition_time_gdtt, uint8_t delay_time_gdtt, uint8_t transaction_id){
    if (mesh_model->element == NULL){
        log_error("mesh_model->element == NULL"); 
    }
    // setup message
    mesh_transport_pdu_t * transport_pdu = mesh_access_setup_segmented_message(message_template, on_off_value, transaction_id, transition_time_gdtt, delay_time_gdtt);
    if (!transport_pdu) return BTSTACK_MEMORY_ALLOC_FAILED;

    // send as segmented access pdu
    generic_client_send_message(mesh_access_get_element_address(mesh_model), dest, netkey_index, appkey_index, (mesh_pdu_t *) transport_pdu);
    return ERROR_CODE_SUCCESS;
}

static uint8_t mesh_generic_on_off_client_set_instantaneous_message(mesh_model_t *mesh_model, const mesh_access_message_t * message_template, 
    uint16_t dest, uint16_t netkey_index, uint16_t appkey_index, uint8_t on_off_value, uint8_t transaction_id){
    if (mesh_model->element == NULL){
        log_error("mesh_model->element == NULL"); 
    }
    // setup message
    mesh_transport_pdu_t *  transport_pdu = mesh_access_setup_segmented_message(message_template, on_off_value, transaction_id);
    if (!transport_pdu) return BTSTACK_MEMORY_ALLOC_FAILED;

    generic_client_send_message(mesh_access_get_element_address(mesh_model), dest, netkey_index, appkey_index, (mesh_pdu_t *) transport_pdu);
    return ERROR_CODE_SUCCESS;
}

uint8_t mesh_generic_on_off_client_set_value(mesh_model_t * mesh_model, uint16_t dest, uint16_t netkey_index, uint16_t appkey_index, 
    uint8_t on_off_value, uint8_t transition_time_gdtt, uint8_t delay_time_gdtt, uint8_t transaction_id){
    if (transition_time_gdtt != 0) {
        return mesh_generic_on_off_client_set_with_transition_message(mesh_model, &mesh_generic_on_off_set_with_transition, dest, netkey_index, appkey_index, on_off_value, transition_time_gdtt, delay_time_gdtt, transaction_id);
    } else {
        return mesh_generic_on_off_client_set_instantaneous_message(mesh_model, &mesh_generic_on_off_set_instantaneous, dest, netkey_index, appkey_index, on_off_value, transaction_id);
    }
}

uint8_t mesh_generic_on_off_client_set_value_unacknowledged(mesh_model_t * mesh_model, uint16_t dest, uint16_t netkey_index, uint16_t appkey_index, 
    uint8_t on_off_value, uint8_t transition_time_gdtt, uint8_t delay_time_gdtt, uint8_t transaction_id){
    if (transition_time_gdtt != 0) {
        return mesh_generic_on_off_client_set_with_transition_message(mesh_model, &mesh_generic_on_off_set_unacknowledged_with_transition, dest, netkey_index, appkey_index, on_off_value, transition_time_gdtt, delay_time_gdtt, transaction_id);
    } else {
        return mesh_generic_on_off_client_set_instantaneous_message(mesh_model, &mesh_generic_on_off_set_unacknowledged_instantaneous, dest, netkey_index, appkey_index, on_off_value, transaction_id);
    }
}

uint8_t mesh_generic_on_off_client_get_value(mesh_model_t *mesh_model, uint16_t dest, uint16_t netkey_index, uint16_t appkey_index){
    if (mesh_model->element == NULL){
        log_error("mesh_model->element == NULL"); 
    }
    // setup message
    mesh_transport_pdu_t * transport_pdu = mesh_access_setup_segmented_message(&mesh_generic_on_off_get);
    if (!transport_pdu) return BTSTACK_MEMORY_ALLOC_FAILED;
    // send as segmented access pdu
    generic_client_send_message(mesh_access_get_element_address(mesh_model), dest, netkey_index, appkey_index, (mesh_pdu_t *) transport_pdu);
    return ERROR_CODE_SUCCESS;
}

uint8_t mesh_generic_on_off_client_publish_value(mesh_model_t * mesh_model, uint8_t on_off_value, uint8_t transaction_id){
    mesh_publication_model_t * publication_model = mesh_model->publication_model;
    uint16_t appkey_index = publication_model->appkey_index;
    mesh_transport_key_t * app_key = mesh_transport_key_get(appkey_index);
    if (app_key == NULL) return MESH_ERROR_APPKEY_INDEX_INVALID;

    return mesh_generic_on_off_client_set_value_unacknowledged(mesh_model, publication_model->address, app_key->netkey_index, appkey_index, on_off_value, 0, 0, transaction_id);
}

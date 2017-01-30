/*
 * Copyright (C) 2016 BlueKitchen GmbH
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


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "btstack.h"
#include "avdtp.h"
#include "avdtp_util.h"

inline uint8_t avdtp_header(uint8_t tr_label, avdtp_packet_type_t packet_type, avdtp_message_type_t msg_type){
    return (tr_label<<4) | ((uint8_t)packet_type<<2) | (uint8_t)msg_type;
}

avdtp_stream_endpoint_t * get_avdtp_stream_endpoint_with_seid(uint8_t seid){
    btstack_linked_list_iterator_t it;    
    btstack_linked_list_iterator_init(&it, (btstack_linked_list_t *) &stream_endpoints);
    while (btstack_linked_list_iterator_has_next(&it)){
        avdtp_stream_endpoint_t * stream_endpoint = (avdtp_stream_endpoint_t *)btstack_linked_list_iterator_next(&it);
        if (stream_endpoint->sep.seid == seid){
            return stream_endpoint;
        }
    }
    return NULL;
}

avdtp_stream_endpoint_t * get_avdtp_stream_endpoint_associated_with_acp_seid(uint16_t acp_seid){
    btstack_linked_list_iterator_t it;    
    btstack_linked_list_iterator_init(&it, (btstack_linked_list_t *) &stream_endpoints);
    while (btstack_linked_list_iterator_has_next(&it)){
        avdtp_stream_endpoint_t * stream_endpoint = (avdtp_stream_endpoint_t *)btstack_linked_list_iterator_next(&it);
        if (stream_endpoint->remote_sep_index >= 0 && stream_endpoint->remote_sep_index < MAX_NUM_SEPS){
            if (stream_endpoint->remote_seps[stream_endpoint->remote_sep_index].seid == acp_seid){
                return stream_endpoint;
            }
        }
    }
    return NULL;
}

int get_bit16(uint16_t bitmap, int position){
    return (bitmap >> position) & 1;
}

uint8_t store_bit16(uint16_t bitmap, int position, uint8_t value){
    if (value){
        bitmap |= 1 << position;
    } else {
        bitmap &= ~ (1 << position);
    }
    return bitmap;
}

int avdtp_read_signaling_header(avdtp_signaling_packet_t * signaling_header, uint8_t * packet, uint16_t size){
    int pos = 0;
    if (size < 2) return pos;   
    signaling_header->transaction_label = packet[pos] >> 4;
    signaling_header->packet_type = (avdtp_packet_type_t)((packet[pos] >> 2) & 0x03);
    signaling_header->message_type = (avdtp_message_type_t) (packet[pos] & 0x03);
    pos++;
    memset(signaling_header->command, 0, sizeof(signaling_header->command));
    switch (signaling_header->packet_type){
        case AVDTP_SINGLE_PACKET:
            signaling_header->num_packets = 0;
            signaling_header->offset = 0;
            signaling_header->size = 0;
            break;
        case AVDTP_END_PACKET:
            signaling_header->num_packets = 0;
            break;
        case AVDTP_START_PACKET:
            signaling_header->num_packets = packet[pos++];
            signaling_header->size = 0;
            signaling_header->offset = 0;
            break;
        case AVDTP_CONTINUE_PACKET:
            if (signaling_header->num_packets <= 0) {
                printf("    ERROR: wrong num fragmented packets\n");
                break;
            }
            signaling_header->num_packets--;
            break;
    }
    signaling_header->signal_identifier = packet[pos++] & 0x3f;
    return pos;
}

int avdtp_pack_service_capabilities(uint8_t * buffer, int size, avdtp_capabilities_t caps, avdtp_service_category_t category, uint8_t pack_all_capabilities){
    UNUSED(size);

    int i;
    // pos = 0 reserved for length
    int pos = 1;
    switch(category){
        case AVDTP_MEDIA_TRANSPORT:
        case AVDTP_REPORTING:
            break;
        case AVDTP_DELAY_REPORTING:
            if (!pack_all_capabilities) break;
            break;
        case AVDTP_RECOVERY:
            buffer[pos++] = caps.recovery.recovery_type; // 0x01=RFC2733
            buffer[pos++] = caps.recovery.maximum_recovery_window_size;
            buffer[pos++] = caps.recovery.maximum_number_media_packets;
            break;
        case AVDTP_CONTENT_PROTECTION:
            buffer[pos++] = caps.content_protection.cp_type_value_len + 2;
            big_endian_store_16(buffer, pos, caps.content_protection.cp_type);
            pos += 2;
            memcpy(buffer+pos, caps.content_protection.cp_type_value, caps.content_protection.cp_type_value_len);
            break;
        case AVDTP_HEADER_COMPRESSION:
            buffer[pos++] = (caps.header_compression.back_ch << 7) | (caps.header_compression.media << 6) | (caps.header_compression.recovery << 5);
            break;
        case AVDTP_MULTIPLEXING:
            buffer[pos++] = caps.multiplexing_mode.fragmentation << 7;
            for (i=0; i<caps.multiplexing_mode.transport_identifiers_num; i++){
                buffer[pos++] = caps.multiplexing_mode.transport_session_identifiers[i] << 7;
                buffer[pos++] = caps.multiplexing_mode.tcid[i] << 7;
                // media, reporting. recovery
            }
            break;
        case AVDTP_MEDIA_CODEC:
            buffer[pos++] = ((uint8_t)caps.media_codec.media_type) << 4;
            buffer[pos++] = (uint8_t)caps.media_codec.media_codec_type;
            for (i = 0; i<caps.media_codec.media_codec_information_len; i++){
                buffer[pos++] = caps.media_codec.media_codec_information[i];
            }
            break;
        default:
            break;
    }
    buffer[0] = pos - 1; // length
    return pos;
}

static int avdtp_unpack_service_capabilities_has_errors(avdtp_connection_t * connection, avdtp_service_category_t category, uint8_t cap_len){
    connection->error_code = 0;
    
    if (category == AVDTP_SERVICE_CATEGORY_INVALID_0 || 
        (category == AVDTP_SERVICE_CATEGORY_INVALID_FF && connection->signaling_packet.signal_identifier == AVDTP_SI_RECONFIGURE)){
        printf("    ERROR: BAD SERVICE CATEGORY %d\n", category);
        connection->reject_service_category = category;
        connection->error_code = BAD_SERV_CATEGORY;
        return 1;
    }

    if (connection->signaling_packet.signal_identifier == AVDTP_SI_RECONFIGURE){
        if (category != AVDTP_CONTENT_PROTECTION && category != AVDTP_MEDIA_CODEC){
            printf("    ERROR: REJECT CATEGORY, INVALID_CAPABILITIES\n");
            connection->reject_service_category = category;
            connection->error_code = INVALID_CAPABILITIES;
            return 1;
        }
    }

    switch(category){
        case AVDTP_MEDIA_TRANSPORT:   
            if (cap_len != 0){
                printf("    ERROR: REJECT CATEGORY, BAD_MEDIA_TRANSPORT\n");
                connection->reject_service_category = category;
                connection->error_code = BAD_MEDIA_TRANSPORT_FORMAT;
                return 1;
            }
            break;
        case AVDTP_REPORTING:                
        case AVDTP_DELAY_REPORTING:                
            if (cap_len != 0){
                printf("    ERROR: REJECT CATEGORY, BAD_LENGTH\n");
                connection->reject_service_category = category;
                connection->error_code = BAD_LENGTH;
                return 1;
            }
            break;
        case AVDTP_RECOVERY:     
            if (cap_len < 3){
                printf("    ERROR: REJECT CATEGORY, BAD_MEDIA_TRANSPORT\n");
                connection->reject_service_category = category;
                connection->error_code = BAD_RECOVERY_FORMAT;
                return 1;
            }           
            break;
        case AVDTP_CONTENT_PROTECTION:
            if (cap_len < 2){
                printf("    ERROR: REJECT CATEGORY, BAD_CP_FORMAT\n");
                connection->reject_service_category = category;
                connection->error_code = BAD_CP_FORMAT;
                return 1;
            }
            break;
        case AVDTP_HEADER_COMPRESSION:
            break;
        case AVDTP_MULTIPLEXING:                
            break;
        case AVDTP_MEDIA_CODEC:                
            break;
        default:
            break;
    }
    return 0;
}

uint16_t avdtp_unpack_service_capabilities(avdtp_connection_t * connection, avdtp_capabilities_t * caps, uint8_t * packet, uint16_t size){
    if (size == 0) return 0;
    
    uint16_t registered_service_categories = 0;
    int pos = 0;
    int i;
    avdtp_service_category_t category = (avdtp_service_category_t)packet[pos++];
    uint8_t cap_len = packet[pos++];
   
    if (avdtp_unpack_service_capabilities_has_errors(connection, category, cap_len)) return 0;
    int processed_cap_len = 0;
    int rfa = 0;
    //printf(" size %d, cat size %d\n", size, cap_len);

    while (pos < size){
        if (cap_len > size - pos){
            connection->reject_service_category = category;
            connection->error_code = BAD_LENGTH;
            return 0;
        }
        rfa = 0;
        processed_cap_len = pos;
        switch(category){
            case AVDTP_RECOVERY:                
                caps->recovery.recovery_type = packet[pos++];
                caps->recovery.maximum_recovery_window_size = packet[pos++];
                caps->recovery.maximum_number_media_packets = packet[pos++];
                break;
            case AVDTP_CONTENT_PROTECTION:
                caps->content_protection.cp_type = big_endian_read_16(packet, pos);
                pos+=2;
                
                caps->content_protection.cp_type_value_len = cap_len - 2;
                pos += caps->content_protection.cp_type_value_len;
                
                // connection->reject_service_category = category;
                // connection->error_code = UNSUPPORTED_CONFIGURATION;
                // support for content protection goes here
                break;
                
            case AVDTP_HEADER_COMPRESSION:
                caps->header_compression.back_ch  = packet[pos] >> 7; 
                caps->header_compression.media    = packet[pos] >> 6;
                caps->header_compression.recovery = packet[pos] >> 5;
                pos++;
                break;
            case AVDTP_MULTIPLEXING:                
                caps->multiplexing_mode.fragmentation = packet[pos++] >> 7;
                // read [tsid, tcid] for media, reporting. recovery respectively
                caps->multiplexing_mode.transport_identifiers_num = 3;
                for (i=0; i<caps->multiplexing_mode.transport_identifiers_num; i++){
                    caps->multiplexing_mode.transport_session_identifiers[i] = packet[pos++] >> 7;
                    caps->multiplexing_mode.tcid[i] = packet[pos++] >> 7;
                }
                break;
            case AVDTP_MEDIA_CODEC:   
                caps->media_codec.media_type = packet[pos++] >> 4;
                caps->media_codec.media_codec_type = packet[pos++];
                caps->media_codec.media_codec_information_len = cap_len - 2;
                caps->media_codec.media_codec_information = &packet[pos];
                pos += caps->media_codec.media_codec_information_len;
                break;
            case AVDTP_MEDIA_TRANSPORT:   
            case AVDTP_REPORTING:                
            case AVDTP_DELAY_REPORTING:                
                pos += cap_len;
                break;
            default:
                pos += cap_len;
                rfa = 1;
                break;
        }
        processed_cap_len = pos - processed_cap_len;
        
        if (cap_len == processed_cap_len){
            if (!rfa) {
                registered_service_categories = store_bit16(registered_service_categories, category, 1);
            }
            if (pos < size-2){
                //int old_pos = pos;
                category = (avdtp_service_category_t)packet[pos++];
                cap_len = packet[pos++];
                if (avdtp_unpack_service_capabilities_has_errors(connection, category, cap_len)) return 0;
                //printf("category %d, pos %d + 2 + %d -> %d\n", category, old_pos, cap_len, pos + cap_len);
                //printf_hexdump(packet+old_pos, size-old_pos);
            }
        } 
    }
    return registered_service_categories;
}

void avdtp_prepare_capabilities(avdtp_signaling_packet_t * signaling_packet, uint8_t transaction_label, uint16_t registered_service_categories, avdtp_capabilities_t capabilities, uint8_t identifier){
    if (signaling_packet->offset) return;
    uint8_t pack_all_capabilities = 1;
    signaling_packet->message_type = AVDTP_RESPONSE_ACCEPT_MSG;
    signaling_packet->size = 0;
    int i;
    signaling_packet->command[signaling_packet->size++] = signaling_packet->acp_seid << 2;
    
    switch (identifier) {
        case AVDTP_SI_GET_CAPABILITIES:
            pack_all_capabilities = 0;
            break;
        case AVDTP_SI_GET_ALL_CAPABILITIES:
            pack_all_capabilities = 1;
            break;
        case AVDTP_SI_SET_CONFIGURATION:
            signaling_packet->command[signaling_packet->size++] = signaling_packet->int_seid << 2;
            signaling_packet->message_type = AVDTP_CMD_MSG;
            break;
        case AVDTP_SI_RECONFIGURE:
            signaling_packet->message_type = AVDTP_CMD_MSG;
            break;
        default: 
            log_error("avdtp_prepare_capabilities wrong identifier %d", identifier);
            break;
    } 
    
    for (i = 1; i < 9; i++){
        if (get_bit16(registered_service_categories, i)){
            // service category
            signaling_packet->command[signaling_packet->size++] = i;
            signaling_packet->size += avdtp_pack_service_capabilities(signaling_packet->command+signaling_packet->size, sizeof(signaling_packet->command)-signaling_packet->size, capabilities, (avdtp_service_category_t)i, pack_all_capabilities);
        }
    }
    // signaling_packet->command[signaling_packet->size++] = 0x04;
    // signaling_packet->command[signaling_packet->size++] = 0x02;
    // signaling_packet->command[signaling_packet->size++] = 0x02;
    // signaling_packet->command[signaling_packet->size++] = 0x00;
    
    signaling_packet->signal_identifier = identifier;
    signaling_packet->transaction_label = transaction_label;
}

int avdtp_signaling_create_fragment(uint16_t cid, avdtp_signaling_packet_t * signaling_packet, uint8_t * out_buffer) {
    int mtu = l2cap_get_remote_mtu_for_local_cid(cid);
    // hack for test
    // int mtu = 6;
    int data_len = 0;

    uint16_t offset = signaling_packet->offset;
    uint16_t pos = 1;
    // printf(" avdtp_signaling_create_fragment offset %d, packet type %d\n",  signaling_packet->offset, signaling_packet->packet_type);
    
    if (offset == 0){
        if (signaling_packet->size <= mtu - 2){
            // printf(" AVDTP_SINGLE_PACKET\n");
            signaling_packet->packet_type = AVDTP_SINGLE_PACKET;
            out_buffer[pos++] = signaling_packet->signal_identifier;
            data_len = signaling_packet->size;
        } else {
            signaling_packet->packet_type = AVDTP_START_PACKET;
            out_buffer[pos++] = (mtu + signaling_packet->size)/ (mtu-1);
            out_buffer[pos++] = signaling_packet->signal_identifier;
            data_len = mtu - 3;
            signaling_packet->offset = data_len;
            // printf(" AVDTP_START_PACKET len %d, offset %d\n", signaling_packet->size, signaling_packet->offset);
        }
    } else {
        int remaining_bytes = signaling_packet->size - offset;
        if (remaining_bytes <= mtu - 1){
            //signaling_packet->fragmentation = 1;
            signaling_packet->packet_type = AVDTP_END_PACKET;
            data_len = remaining_bytes;
            signaling_packet->offset = 0;
            // printf(" AVDTP_END_PACKET len %d, offset %d\n", signaling_packet->size, signaling_packet->offset);
        } else{
            signaling_packet->packet_type = AVDTP_CONTINUE_PACKET;
            data_len = mtu - 1;
            signaling_packet->offset += data_len;
            // printf(" AVDTP_CONTINUE_PACKET len %d, offset %d\n", signaling_packet->size, signaling_packet->offset);
        }
    }
    out_buffer[0] = avdtp_header(signaling_packet->transaction_label, signaling_packet->packet_type, signaling_packet->message_type);
    memcpy(out_buffer+pos, signaling_packet->command + offset, data_len);
    pos += data_len; 
    return pos;
}


void avdtp_signaling_emit_connection_established(btstack_packet_handler_t callback, uint16_t con_handle, bd_addr_t addr, uint8_t status){
    if (!callback) return;
    uint8_t event[12];
    int pos = 0;
    event[pos++] = HCI_EVENT_AVDTP_META;
    event[pos++] = sizeof(event) - 2;
    event[pos++] = AVDTP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED;
    little_endian_store_16(event, pos, con_handle);
    pos += 2;
    reverse_bd_addr(addr,&event[pos]);
    pos += 6;
    event[pos++] = status;
    (*callback)(HCI_EVENT_PACKET, 0, event, sizeof(event));
}

void avdtp_signaling_emit_sep(btstack_packet_handler_t callback, uint16_t con_handle, avdtp_sep_t sep){
    if (!callback) return;
    uint8_t event[9];
    int pos = 0;
    event[pos++] = HCI_EVENT_AVDTP_META;
    event[pos++] = sizeof(event) - 2;
    event[pos++] = AVDTP_SUBEVENT_SIGNALING_SEP_FOUND;
    little_endian_store_16(event, pos, con_handle);
    pos += 2;
    event[pos++] = sep.seid;
    event[pos++] = sep.in_use;
    event[pos++] = sep.media_type;
    event[pos++] = sep.type;
    (*callback)(HCI_EVENT_PACKET, 0, event, sizeof(event));
}

void avdtp_signaling_emit_accept(btstack_packet_handler_t callback, uint16_t con_handle, avdtp_signal_identifier_t identifier, uint8_t status){
    if (!callback) return;
    uint8_t event[7];
    int pos = 0;
    event[pos++] = HCI_EVENT_AVDTP_META;
    event[pos++] = sizeof(event) - 2;
    event[pos++] = AVDTP_SUBEVENT_SIGNALING_ACCEPT;
    little_endian_store_16(event, pos, con_handle);
    pos += 2;
    event[pos++] = identifier;
    event[pos++] = status;
    (*callback)(HCI_EVENT_PACKET, 0, event, sizeof(event));
}

void avdtp_signaling_emit_reject(btstack_packet_handler_t callback, uint16_t con_handle, avdtp_signal_identifier_t identifier){
    if (!callback) return;
    uint8_t event[6];
    int pos = 0;
    event[pos++] = HCI_EVENT_AVDTP_META;
    event[pos++] = sizeof(event) - 2;
    event[pos++] = AVDTP_SUBEVENT_SIGNALING_REJECT;
    little_endian_store_16(event, pos, con_handle);
    pos += 2;
    event[pos++] = identifier;
    (*callback)(HCI_EVENT_PACKET, 0, event, sizeof(event));
}

void avdtp_signaling_emit_general_reject(btstack_packet_handler_t callback, uint16_t con_handle, avdtp_signal_identifier_t identifier){
    if (!callback) return;
    uint8_t event[6];
    int pos = 0;
    event[pos++] = HCI_EVENT_AVDTP_META;
    event[pos++] = sizeof(event) - 2;
    event[pos++] = AVDTP_SUBEVENT_SIGNALING_GENERAL_REJECT;
    little_endian_store_16(event, pos, con_handle);
    pos += 2;
    event[pos++] = identifier;
    (*callback)(HCI_EVENT_PACKET, 0, event, sizeof(event));
}

void avdtp_signaling_emit_media_codec_sbc_capability(btstack_packet_handler_t callback, uint16_t con_handle, adtvp_media_codec_capabilities_t media_codec){
    if (!callback) return;
    uint8_t event[13];
    int pos = 0;
    event[pos++] = HCI_EVENT_AVDTP_META;
    event[pos++] = sizeof(event) - 2;
    event[pos++] = AVDTP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CAPABILITY;
    little_endian_store_16(event, pos, con_handle);
    pos += 2;
    event[pos++] = media_codec.media_type;
    event[pos++] = media_codec.media_codec_information[0] >> 4;
    event[pos++] = media_codec.media_codec_information[0] & 0x0F;
    event[pos++] = media_codec.media_codec_information[1] >> 4;
    event[pos++] = (media_codec.media_codec_information[1] & 0x0F) >> 2;
    event[pos++] = media_codec.media_codec_information[1] & 0x03;
    event[pos++] = media_codec.media_codec_information[2];
    event[pos++] = media_codec.media_codec_information[3];
    (*callback)(HCI_EVENT_PACKET, 0, event, sizeof(event));
}

void avdtp_signaling_emit_media_codec_other_capability(btstack_packet_handler_t callback, uint16_t con_handle, adtvp_media_codec_capabilities_t media_codec){
    if (!callback) return;
        uint8_t event[109];
    int pos = 0;
    event[pos++] = HCI_EVENT_AVDTP_META;
    event[pos++] = sizeof(event) - 2;
    event[pos++] = AVDTP_SUBEVENT_SIGNALING_MEDIA_CODEC_OTHER_CAPABILITY;
    little_endian_store_16(event, pos, con_handle);
    pos += 2;
    event[pos++] = media_codec.media_type;
    little_endian_store_16(event, pos, media_codec.media_codec_type);
    pos += 2;
    little_endian_store_16(event, pos, media_codec.media_codec_information_len);
    pos += 2;
    if (media_codec.media_codec_information_len < 100){
        memcpy(event+pos, media_codec.media_codec_information, media_codec.media_codec_information_len);
    } else {
        memcpy(event+pos, media_codec.media_codec_information, 100);
    }
    (*callback)(HCI_EVENT_PACKET, 0, event, sizeof(event));
}

static inline void avdtp_signaling_emit_media_codec_sbc(btstack_packet_handler_t callback, uint16_t con_handle, adtvp_media_codec_capabilities_t media_codec, uint8_t reconfigure){
    if (!callback) return;
    uint8_t event[14+2];
    int pos = 0;
    event[pos++] = HCI_EVENT_AVDTP_META;
    event[pos++] = sizeof(event) - 2;
    
    event[pos++] = AVDTP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION;
    little_endian_store_16(event, pos, con_handle);
    pos += 2;
    event[pos++] = reconfigure;
    
    uint8_t num_channels = 0;
    uint16_t sampling_frequency = 0;
    uint8_t subbands = 0;
    uint8_t block_length = 0;

    uint8_t sampling_frequency_bitmap = media_codec.media_codec_information[0] >> 4;
    uint8_t channel_mode_bitmap = media_codec.media_codec_information[0] & 0x0F;
    uint8_t block_length_bitmap = media_codec.media_codec_information[1] >> 4;
    uint8_t subbands_bitmap = (media_codec.media_codec_information[1] & 0x0F) >> 2;

    if (channel_mode_bitmap & AVDTP_SBC_MONO){
        num_channels = 1;
    }
    if ( (channel_mode_bitmap & AVDTP_SBC_JOINT_STEREO) || 
         (channel_mode_bitmap & AVDTP_SBC_STEREO) ||
         (channel_mode_bitmap & AVDTP_SBC_DUAL_CHANNEL) ){
        num_channels = 2;
    }

    if (sampling_frequency_bitmap & AVDTP_SBC_16000){
        sampling_frequency = 16000;
    }
    if (sampling_frequency_bitmap & AVDTP_SBC_32000){
        sampling_frequency = 32000;
    }
    if (sampling_frequency_bitmap & AVDTP_SBC_44100){
        sampling_frequency = 44100;
    }
    if (sampling_frequency_bitmap & AVDTP_SBC_48000){
        sampling_frequency = 48000;
    }

    if (subbands_bitmap & AVDTP_SBC_SUBBANDS_4){
        subbands = 4;
    }
    if (subbands_bitmap & AVDTP_SBC_SUBBANDS_8){
        subbands = 8;
    }

    if (block_length_bitmap & AVDTP_SBC_BLOCK_LENGTH_4){
        block_length = 4;
    }
    if (block_length_bitmap & AVDTP_SBC_BLOCK_LENGTH_8){
        block_length = 8;
    }
    if (block_length_bitmap & AVDTP_SBC_BLOCK_LENGTH_12){
        block_length = 12;
    }
    if (block_length_bitmap & AVDTP_SBC_BLOCK_LENGTH_16){
        block_length = 16;
    }
    
    event[pos++] = media_codec.media_type;
    little_endian_store_16(event, pos, sampling_frequency);
    pos += 2;
    
    event[pos++] = channel_mode_bitmap;
    event[pos++] = num_channels;
    event[pos++] = block_length;
    event[pos++] = subbands;
    event[pos++] = media_codec.media_codec_information[1] & 0x03;
    event[pos++] = media_codec.media_codec_information[2];
    event[pos++] = media_codec.media_codec_information[3];
    (*callback)(HCI_EVENT_PACKET, 0, event, sizeof(event));
}

void avdtp_signaling_emit_media_codec_sbc_configuration(btstack_packet_handler_t callback, uint16_t con_handle, adtvp_media_codec_capabilities_t media_codec){
    if (!callback) return;
    avdtp_signaling_emit_media_codec_sbc(callback, con_handle, media_codec, 0);
}

void avdtp_signaling_emit_media_codec_sbc_reconfiguration(btstack_packet_handler_t callback, uint16_t con_handle, adtvp_media_codec_capabilities_t media_codec){
    if (!callback) return;
    avdtp_signaling_emit_media_codec_sbc(callback, con_handle, media_codec, 1);
}

static inline void avdtp_signaling_emit_media_codec_other(btstack_packet_handler_t callback, uint16_t con_handle, adtvp_media_codec_capabilities_t media_codec, uint8_t reconfigure){
    uint8_t event[110];
    int pos = 0;
    event[pos++] = HCI_EVENT_AVDTP_META;
    event[pos++] = sizeof(event) - 2;
    event[pos++] = AVDTP_SUBEVENT_SIGNALING_MEDIA_CODEC_OTHER_CONFIGURATION;
    little_endian_store_16(event, pos, con_handle);
    pos += 2;

    event[pos++] = reconfigure;
    
    event[pos++] = media_codec.media_type;
    little_endian_store_16(event, pos, media_codec.media_codec_type);
    pos += 2;
    little_endian_store_16(event, pos, media_codec.media_codec_information_len);
    pos += 2;

    if (media_codec.media_codec_information_len < 100){
        memcpy(event+pos, media_codec.media_codec_information, media_codec.media_codec_information_len);
    } else {
        memcpy(event+pos, media_codec.media_codec_information, 100);
    }
    (*callback)(HCI_EVENT_PACKET, 0, event, sizeof(event));
}

void avdtp_signaling_emit_media_codec_other_configuration(btstack_packet_handler_t callback, uint16_t con_handle, adtvp_media_codec_capabilities_t media_codec){
    if (!callback) return;
    avdtp_signaling_emit_media_codec_other(callback, con_handle, media_codec, 0);
}
    
void avdtp_signaling_emit_media_codec_other_reconfiguration(btstack_packet_handler_t callback, uint16_t con_handle, adtvp_media_codec_capabilities_t media_codec){
    if (!callback) return;
    avdtp_signaling_emit_media_codec_other(callback, con_handle, media_codec, 1);
}
                           

void avdtp_sink_request_can_send_now_acceptor(avdtp_connection_t * connection, uint16_t l2cap_cid){
    connection->wait_to_send_acceptor = 1;
    l2cap_request_can_send_now_event(l2cap_cid);
}
void avdtp_sink_request_can_send_now_initiator(avdtp_connection_t * connection, uint16_t l2cap_cid){
    connection->wait_to_send_initiator = 1;
    l2cap_request_can_send_now_event(l2cap_cid);
}
void avdtp_sink_request_can_send_now_self(avdtp_connection_t * connection, uint16_t l2cap_cid){
    connection->wait_to_send_self = 1;
    l2cap_request_can_send_now_event(l2cap_cid);
}

avdtp_stream_endpoint_t * get_avdtp_stream_endpoint_for_seid(uint16_t seid){
    btstack_linked_list_iterator_t it;    
    btstack_linked_list_iterator_init(&it, (btstack_linked_list_t *) &stream_endpoints);
    while (btstack_linked_list_iterator_has_next(&it)){
        avdtp_stream_endpoint_t * stream_endpoint = (avdtp_stream_endpoint_t *)btstack_linked_list_iterator_next(&it);
        if (stream_endpoint->sep.seid == seid){
            return stream_endpoint;
        }
    }
    return NULL;
}
                            
uint8_t avdtp_get_index_of_remote_stream_endpoint_with_seid(avdtp_stream_endpoint_t * stream_endpoint, uint16_t seid){
    if (stream_endpoint->remote_seps[stream_endpoint->remote_sep_index].seid == seid){
        return stream_endpoint->remote_sep_index;
    }
    int i;
    for (i=0; i < stream_endpoint->remote_seps_num; i++){
        if (stream_endpoint->remote_seps[i].seid == seid){
            return i;
        }
    }
    return 0xFF;
}

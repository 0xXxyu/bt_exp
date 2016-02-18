/*
 * Copyright (C) 2014 BlueKitchen GmbH
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
 
// *****************************************************************************
//
// Minimal test for HSP Headset (!! UNDER DEVELOPMENT !!)
//
// *****************************************************************************

#include "btstack_config.h"

#include <math.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <net/if_arp.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "hci_cmd.h"
#include "btstack_run_loop.h"
#include "classic/sdp_util.h"

#include "hci.h"
#include "l2cap.h"
#include "classic/sdp_server.h"
#include "btstack_debug.h"
#include "hsp_hs.h"
#include "stdin_support.h"


const uint32_t   hsp_service_buffer[150/4]; // implicit alignment to 4-byte memory address
const uint8_t    rfcomm_channel_nr = 1;
const char hsp_hs_service_name[] = "Headset Test";
static uint16_t  sco_handle = 0;
static bd_addr_t pts_addr = {0x00,0x1b,0xDC,0x07,0x32,0xEF};    // PTS dongle
static bd_addr_t local_mac = {0x04, 0x0C, 0xCE, 0xE4, 0x85, 0xD3}; // MacBook Air 2011
// static bd_addr_t local_mac = {0x84, 0x38, 0x35, 0x65, 0xD1, 0x15}; // MacBook Air 2013
// static bd_addr_t local_mac = {0x54, 0xe4, 0x3a, 0x26, 0xa2, 0x39}; // iPhone 5S
// static bd_addr_t local_mac = {0x00,0x1a,0x7d,0xda,0x71,0x0a}; // CSR Dongle
static bd_addr_t current_addr;

static char hs_cmd_buffer[100];

// prototypes
static void show_usage();

#define TABLE_SIZE    (50)
static uint8_t sine[TABLE_SIZE];
static void setup_sine(void){
    // create sine wave table
    int i;
    for( i=0; i<TABLE_SIZE; i++ ) {
        sine[i] = (int8_t) (127.0 * sin( ((double)i/(double)TABLE_SIZE) * M_PI * 2. ));
        printf("%02u %u\n", i, sine[i]);
    }
}

#ifdef HAVE_PORTAUDIO
#include <portaudio.h>

// portaudio config
#define NUM_CHANNELS 1
#define SAMPLE_RATE 8000
#define FRAMES_PER_BUFFER 1000
#define PA_SAMPLE_TYPE paInt8

// portaudio globals
int phase = 0;
static  PaStream * stream;

static void try_send_sco(void){
    printf("try send handle %x\n", sco_handle);
    if (!sco_handle) return;
    if (!hci_can_send_sco_packet_now()) {
        printf("try_send_sco, cannot send now\n");
        return;
    }
    const int frames_per_packet = 9;
    hci_reserve_packet_buffer();
    uint8_t * sco_packet = hci_get_outgoing_packet_buffer();
    // set handle + flags
    little_endian_store_16(sco_packet, 0, sco_handle);
    // set len
    sco_packet[2] = frames_per_packet;
    int i;
    for (i=0;i<frames_per_packet;i++){
        sco_packet[3+i] = sine[phase];
        phase++;
        if (phase >= TABLE_SIZE) phase = 0;
    }
    hci_send_sco_packet_buffer(3 + frames_per_packet);
}


static void setup_audio(void){

    int err;
    PaStreamParameters outputParameters;

    /* -- initialize PortAudio -- */
    err = Pa_Initialize();
    if( err != paNoError ) return;
    /* -- setup input and output -- */
    outputParameters.device = Pa_GetDefaultOutputDevice(); /* default output device */
    outputParameters.channelCount = NUM_CHANNELS;
    outputParameters.sampleFormat = PA_SAMPLE_TYPE;
    outputParameters.suggestedLatency = Pa_GetDeviceInfo( outputParameters.device )->defaultHighOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;
    /* -- setup stream -- */
    err = Pa_OpenStream(
              &stream,
              NULL, // &inputParameters,
              &outputParameters,
              SAMPLE_RATE,
              FRAMES_PER_BUFFER,
              paClipOff,      /* we won't output out of range samples so don't bother clipping them */
              NULL, /* no callback, use blocking API */
              NULL ); /* no callback, so no callback userData */
    if( err != paNoError ) return;
    /* -- start stream -- */
    err = Pa_StartStream( stream );
    if( err != paNoError ) return;
    printf("Portaudio setup\n");
}
static void sco_packet_handler(uint8_t packet_type, uint8_t * packet, uint16_t size){
    Pa_WriteStream( stream, &packet[3], size -3);
}

#endif

// Testig User Interface 
static void show_usage(void){
    uint8_t iut_address_type;
    bd_addr_t      iut_address;
    gap_advertisements_get_address(&iut_address_type, iut_address);

    printf("\n--- Bluetooth HSP Headset Test Console %s ---\n", bd_addr_to_str(iut_address));
    printf("---\n");
    printf("p - establish audio connection to PTS module\n");
    printf("e - establish audio connection to local mac\n");
    printf("d - release audio connection from Bluetooth Speaker\n");
    printf("b - press user button\n");
    printf("z - set microphone gain 0\n");
    printf("m - set microphone gain 8\n");
    printf("M - set microphone gain 15\n");
    printf("o - set speaker gain 0\n");
    printf("s - set speaker gain 8\n");
    printf("S - set speaker gain 15\n");
    printf("---\n");
    printf("Ctrl-c - exit\n");
    printf("---\n");
}

static int stdin_process(struct btstack_data_source *ds){
    char buffer;
    read(ds->fd, &buffer, 1);

    switch (buffer){
        case 'p':
            printf("Establishing audio connection to PTS module %s...\n", bd_addr_to_str(pts_addr));
            memcpy(current_addr, pts_addr, 6);
            hsp_hs_connect(pts_addr);
            break;
        case 'e':
            printf("Establishing audio connection to local mac %s...\n", bd_addr_to_str(local_mac));
            memcpy(current_addr, local_mac, 6);
            hsp_hs_connect(local_mac);
            break;
        case 'd':
            printf("Releasing audio connection.\n");
            hsp_hs_disconnect(current_addr);
            break;
        case 'z':
            printf("Setting microphone gain 0\n");
            hsp_hs_set_microphone_gain(0);
            break;
        case 'm':
            printf("Setting microphone gain 8\n");
            hsp_hs_set_microphone_gain(8);
            break;
        case 'M':
            printf("Setting microphone gain 15\n");
            hsp_hs_set_microphone_gain(15);
            break;
        case 'o':
            printf("Setting speaker gain 0\n");
            hsp_hs_set_speaker_gain(0);
            break;
        case 's':
            printf("Setting speaker gain 8\n");
            hsp_hs_set_speaker_gain(8);
            break;
        case 'S':
            printf("Setting speaker gain 15\n");
            hsp_hs_set_speaker_gain(15);
            break;
        case 'b':
            printf("Press user button\n");
            hsp_hs_send_button_press();
            break;
        default:
            show_usage();
            break;
    }

    return 0;
}

static void packet_handler(uint8_t * event, uint16_t event_size){
    // printf("Packet handler event 0x%02x\n", event[0]);
    // try_send_sco();
    switch (event[0]) {
        case BTSTACK_EVENT_STATE:
            if (event[2] != HCI_STATE_WORKING) break;
            show_usage();
            // request loopback mode
            hci_send_cmd(&hci_write_synchronous_flow_control_enable, 1);
            break;
        case HCI_EVENT_SYNCHRONOUS_CONNECTION_COMPLETE:
            // printf("HCI_EVENT_SYNCHRONOUS_CONNECTION_COMPLETE status %u, %x\n", event[2], little_endian_read_16(event, 3));
            if (event[2]) break;
            sco_handle = little_endian_read_16(event, 3);
            break;  
        case HCI_EVENT_HSP_META:
            switch (event[2]) { 
                case HSP_SUBEVENT_AUDIO_CONNECTION_COMPLETE:
                    if (event[3] == 0){
                        printf("Audio connection established with SCO handle 0x%04x.\n", sco_handle);
                        // try_send_sco();
                    } else {
                        printf("Audio connection establishment failed with status %u\n", event[3]);
                    }
                    break;
                case HSP_SUBEVENT_AUDIO_DISCONNECTION_COMPLETE:
                    if (event[3] == 0){
                        printf("Audio connection released.\n\n");
                        sco_handle = 0;
                    } else {
                        printf("Audio connection releasing failed with status %u\n", event[3]);
                    }
                    break;
                case HSP_SUBEVENT_MICROPHONE_GAIN_CHANGED:
                    printf("Received microphone gain change %d\n", event[3]);
                    break;
                case HSP_SUBEVENT_SPEAKER_GAIN_CHANGED:
                    printf("Received speaker gain change %d\n", event[3]);
                    break;
                case HSP_SUBEVENT_RING:
                    printf("HS: RING RING!\n");
                    break;
                case HSP_SUBEVENT_AG_INDICATION:
                    memset(hs_cmd_buffer, 0, sizeof(hs_cmd_buffer));
                    int size = event[3] <= sizeof(hs_cmd_buffer)? event[3] : sizeof(hs_cmd_buffer); 
                    memcpy(hs_cmd_buffer, &event[4], size - 1);
                    printf("Received custom indication: \"%s\". \nExit code or call hsp_hs_send_result.\n", hs_cmd_buffer);
                    break;
                default:
                    printf("event not handled %u\n", event[2]);
                    break;
            }
            break;
        default:
            break;
    }
}

int btstack_main(int argc, const char * argv[]);
int btstack_main(int argc, const char * argv[]){

    setup_sine();

#ifdef HAVE_PORTAUDIO
    setup_audio();
    hci_register_sco_packet_handler(&sco_packet_handler);
#endif

    hsp_hs_init(rfcomm_channel_nr);
    hsp_hs_register_packet_handler(packet_handler);
    
    sdp_init();
    memset((uint8_t *)hsp_service_buffer, 0, sizeof(hsp_service_buffer));
    hsp_hs_create_sdp_record((uint8_t *)hsp_service_buffer, 0x10004, rfcomm_channel_nr, hsp_hs_service_name, 0);
    sdp_register_service((uint8_t *)hsp_service_buffer);

    gap_discoverable_control(1);
    hci_set_class_of_device(0x200418);
    
    btstack_stdin_setup(stdin_process);

    // turn on!
    hci_power_control(HCI_POWER_ON);
    return 0;
}

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

/*
 *  hci_dump.c
 *
 *  Dump HCI trace in various formats:
 *
 *  - BlueZ's hcidump format
 *  - Apple's PacketLogger
 *  - stdout hexdump
 *
 *  Created by Matthias Ringwald on 5/26/09.
 */

#include "btstack-config.h"

#include "hci_dump.h"
#include "hci.h"
#include "hci_transport.h"
#include <btstack/hci_cmds.h>
#include <btstack/run_loop.h>
#include <stdio.h>

#ifndef EMBEDDED
#include <fcntl.h>        // open
#include <unistd.h>       // write 
#include <time.h>
#include <sys/time.h>     // for timestamps
#include <sys/stat.h>     // for mode flags
#include <stdarg.h>       // for va_list
#endif

// BLUEZ hcidump - struct not used directly, but left here as documentation
typedef struct {
    uint16_t    len;
    uint8_t     in;
    uint8_t     pad;
    uint32_t    ts_sec;
    uint32_t    ts_usec;
    uint8_t     packet_type;
}
hcidump_hdr;
#define HCIDUMP_HDR_SIZE 13

// APPLE PacketLogger - struct not used directly, but left here as documentation
typedef struct {
    uint32_t    len;
    uint32_t    ts_sec;
    uint32_t    ts_usec;
    uint8_t     type;   // 0xfc for note
}
pktlog_hdr;
#define PKTLOG_HDR_SIZE 13

static int dump_file = -1;
#ifndef EMBEDDED
static int dump_format;
static uint8_t header_bluez[HCIDUMP_HDR_SIZE];
static uint8_t header_packetlogger[PKTLOG_HDR_SIZE];
static char time_string[40];
static int  max_nr_packets = -1;
static int  nr_packets = 0;
static char log_message_buffer[256];
#endif

// levels: debug, info, error
static int log_level_enabled[3] = { 1, 1, 1};

void hci_dump_open(const char *filename, hci_dump_format_t format){
#ifdef EMBEDDED
    dump_file = 1;
#else
    dump_format = format;
    if (dump_format == HCI_DUMP_STDOUT) {
        dump_file = fileno(stdout);
    } else {
#ifdef _WIN32
        dump_file = open(filename, O_WRONLY | O_CREAT | O_TRUNC);
#else
        dump_file = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
#endif
    }
#endif
}

#ifndef EMBEDDED
void hci_dump_set_max_packets(int packets){
    max_nr_packets = packets;
}
#endif

static void printf_packet(uint8_t packet_type, uint8_t in, uint8_t * packet, uint16_t len){
    switch (packet_type){
        case HCI_COMMAND_DATA_PACKET:
            printf("CMD => ");
            break;
        case HCI_EVENT_PACKET:
            printf("EVT <= ");
            break;
        case HCI_ACL_DATA_PACKET:
            if (in) {
                printf("ACL <= ");
            } else {
                printf("ACL => ");
            }
            break;
        case LOG_MESSAGE_PACKET:
            printf("LOG -- %s\n", (char*) packet);
            return;
        default:
            return;
    }
    printf_hexdump(packet, len);  
}

void hci_dump_packet(uint8_t packet_type, uint8_t in, uint8_t *packet, uint16_t len) {

    if (dump_file < 0) return; // not activated yet

#ifdef EMBEDDED
// #ifdef HAVE_TICK
//     uint32_t time_ms = embedded_get_time_ms();
//     printf("[%06u] ", time_ms);
// #endif
    printf_packet(packet_type, in, packet, len);
#else
    // don't grow bigger than max_nr_packets
    if (dump_format != HCI_DUMP_STDOUT && max_nr_packets > 0){
        if (nr_packets >= max_nr_packets){
            lseek(dump_file, 0, SEEK_SET);
            ftruncate(dump_file, 0);
            nr_packets = 0;
        }
        nr_packets++;
    }
    
    // get time
    struct timeval curr_time;
    struct tm* ptm;
    gettimeofday(&curr_time, NULL);
    time_t curr_time_secs = curr_time.tv_sec;

    switch (dump_format){
        case HCI_DUMP_STDOUT: {
            /* Obtain the time of day, and convert it to a tm struct. */
            ptm = localtime (&curr_time_secs);
            /* Format the date and time, down to a single second. */
            strftime (time_string, sizeof (time_string), "[%Y-%m-%d %H:%M:%S", ptm);
            /* Compute milliseconds from microseconds. */
            uint16_t milliseconds = curr_time.tv_usec / 1000;
            /* Print the formatted time, in seconds, followed by a decimal point
             and the milliseconds. */
            printf ("%s.%03u] ", time_string, milliseconds);
            printf_packet(packet_type, in, packet, len);
            break;
        }
            
        case HCI_DUMP_BLUEZ:
            bt_store_16( header_bluez, 0, 1 + len);
            header_bluez[2] = in;
            header_bluez[3] = 0;
            bt_store_32( header_bluez, 4, curr_time.tv_sec);
            bt_store_32( header_bluez, 8, curr_time.tv_usec);
            header_bluez[12] = packet_type;
            write (dump_file, header_bluez, HCIDUMP_HDR_SIZE);
            write (dump_file, packet, len );
            break;
            
        case HCI_DUMP_PACKETLOGGER:
            net_store_32( header_packetlogger, 0, PKTLOG_HDR_SIZE - 4 + len);
            net_store_32( header_packetlogger, 4, curr_time.tv_sec);
            net_store_32( header_packetlogger, 8, curr_time.tv_usec);
            switch (packet_type){
                case HCI_COMMAND_DATA_PACKET:
                    header_packetlogger[12] = 0x00;
                    break;
                case HCI_ACL_DATA_PACKET:
                    if (in) {
                        header_packetlogger[12] = 0x03;
                    } else {
                        header_packetlogger[12] = 0x02;
                    }
                    break;
                case HCI_SCO_DATA_PACKET:
                    if (in) {
                        header_packetlogger[12] = 0x09;
                    } else {
                        header_packetlogger[12] = 0x08;
                    }
                    break;
                case HCI_EVENT_PACKET:
                    header_packetlogger[12] = 0x01;
                    break;
                case LOG_MESSAGE_PACKET:
                    header_packetlogger[12] = 0xfc;
                    break;
                default:
                    return;
            }
            write (dump_file, &header_packetlogger, PKTLOG_HDR_SIZE);
            write (dump_file, packet, len );
            break;
            
        default:
            break;
    }
#endif
}

static int hci_dump_log_level_active(int log_level){
    if (log_level < 0) return 0;
    if (log_level > LOG_LEVEL_ERROR) return 0;
    return log_level_enabled[log_level];
}

void hci_dump_log(int log_level, const char * format, ...){
    if (!hci_dump_log_level_active(log_level)) return;
    va_list argptr;
    va_start(argptr, format);
#ifdef EMBEDDED
    printf("LOG -- ");
    vprintf(format, argptr);
    printf("\n");
#else
    int len = vsnprintf(log_message_buffer, sizeof(log_message_buffer), format, argptr);
    hci_dump_packet(LOG_MESSAGE_PACKET, 0, (uint8_t*) log_message_buffer, len);
#endif    
    va_end(argptr);
}

#ifdef __AVR__
void hci_dump_log_P(int log_level, PGM_P format, ...){
    if (!hci_dump_log_level_active(log_level)) return;
    va_list argptr;
    va_start(argptr, format);
    printf_P(PSTR("LOG -- "));
    vfprintf_P(stdout, format, argptr);
    printf_P(PSTR("\n"));
    va_end(argptr);
}
#endif

void hci_dump_close(void){
#ifndef EMBEDDED
    close(dump_file);
    dump_file = -1;
#endif
}

void hci_dump_enable_log_level(int log_level, int enable){
    if (log_level < 0) return;
    if (log_level > LOG_LEVEL_ERROR) return;
    log_level_enabled[log_level] = enable;
}


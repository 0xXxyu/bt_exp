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

#include "btstack_config.h"
#include "btstack_event.h"
#include "btstack_memory.h"
#include "btstack_run_loop.h"
#include "btstack_run_loop_freertos_single_threaded.h"
#include "btstack_ring_buffer.h"
//#include "classic/btstack_link_key_db.h"
#include "hci.h"
#include "hci_dump.h"
#include "bt.h"
#include "btstack_debug.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

uint32_t esp_log_timestamp();

uint32_t hal_time_ms(void) {
    // super hacky way to get ms
    return esp_log_timestamp();
}

// assert pre-buffer for packet type is available
#if !defined(HCI_OUTGOING_PRE_BUFFER_SIZE) || (HCI_OUTGOING_PRE_BUFFER_SIZE == 0)
#error HCI_OUTGOING_PRE_BUFFER_SIZE not defined. Please update hci.h
#endif

static void (*transport_packet_handler)(uint8_t packet_type, uint8_t *packet, uint16_t size);

// ring buffer for incoming HCI packets
#define MAX_NR_HOST_ACL_PACKETS 10
#define MAX_NR_HOST_EVENT_PACKETS 4
static uint8_t hci_ringbuffer_storage[MAX_NR_HOST_ACL_PACKETS   * (1 + HCI_ACL_HEADER_SIZE + HCI_ACL_3DH5_SIZE) +
                                     (MAX_NR_HOST_EVENT_PACKETS * HCI_EVENT_BUFFER_SIZE)];

static btstack_ring_buffer_t hci_ringbuffer;
static uint8_t hci_receive_buffer[1 + HCI_ACL_HEADER_SIZE + HCI_ACL_3DH5_SIZE];
static SemaphoreHandle_t ring_buffer_mutex;

// executed on main run loop
static void transport_deliver_packets(void *arg){
    xSemaphoreTake(ring_buffer_mutex, portMAX_DELAY);
    while (btstack_ring_buffer_bytes_available(&hci_ringbuffer)){
        uint32_t number_read;
        uint8_t len_tag[2];
        btstack_ring_buffer_read(&hci_ringbuffer, len_tag, 2, &number_read);
        uint32_t len = little_endian_read_16(len_tag, 0);
        btstack_ring_buffer_read(&hci_ringbuffer, hci_receive_buffer, len, &number_read);
        xSemaphoreGive(ring_buffer_mutex);
        transport_packet_handler(hci_receive_buffer[0], &hci_receive_buffer[1], len-1);
        xSemaphoreTake(ring_buffer_mutex, portMAX_DELAY);
    }
    xSemaphoreGive(ring_buffer_mutex);
}

// executed on main run loop
static void transport_notify_packet_send(void *arg){
    // notify upper stack that it might be possible to send again
    uint8_t event[] = { HCI_EVENT_TRANSPORT_PACKET_SENT, 0};
    transport_packet_handler(HCI_EVENT_PACKET, &event[0], sizeof(event));
}

// run from VHCI Task
static void host_send_pkt_available_cb(void){
    // notify upper stack that provided buffer can be used again
    btstack_run_loop_freertos_single_threaded_execute_code_on_main_thread(&transport_notify_packet_send, NULL);
}

// run from VHCI Task
static int host_recv_pkt_cb(uint8_t *data, uint16_t len){
    // log_info("host_recv_pkt_cb: %u bytes, type %u, begins %02x %02x", len, data[0], data[1], data[2]);

    xSemaphoreTake(ring_buffer_mutex, portMAX_DELAY);

    // check space
    uint16_t space = btstack_ring_buffer_bytes_free(&hci_ringbuffer);
    if (space < len){
        xSemaphoreGive(ring_buffer_mutex);
        log_error("transport_recv_pkt_cb packet %u, space %u -> dropping packet", len, space);
        return 0;
    }

    // store size in ringbuffer
    uint8_t len_tag[2];
    little_endian_store_16(len_tag, 0, len);
    btstack_ring_buffer_write(&hci_ringbuffer, len_tag, sizeof(len_tag));

    // store in ringbuffer
    btstack_ring_buffer_write(&hci_ringbuffer, data, len);

    xSemaphoreGive(ring_buffer_mutex);

    // let stack now
    btstack_run_loop_freertos_single_threaded_execute_code_on_main_thread(&transport_deliver_packets, NULL);
    return 0;
}

static const esp_vhci_host_callback_t vhci_host_cb = {
    .notify_host_send_available = host_send_pkt_available_cb,
    .notify_host_recv = host_recv_pkt_cb,
};

/**
 * init transport
 * @param transport_config
 */
static void transport_init(const void *transport_config){
    log_info("transport_init");
    ring_buffer_mutex = xSemaphoreCreateMutex();
}

/**
 * open transport connection
 */
static int transport_open(void){
    esp_err_t ret;

    log_info("transport_open");

    btstack_ring_buffer_init(&hci_ringbuffer, hci_ringbuffer_storage, sizeof(hci_ringbuffer_storage));

    esp_bt_controller_init();

    ret = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
    if (ret) {
        log_error("transpprt: esp_bt_controller_enable failed");
        return -1;
    }

    esp_vhci_host_register_callback(&vhci_host_cb);
    return 0;
}

/**
 * close transport connection
 */
static int transport_close(void){
    log_info("transport_close");
    return 0;
}

/**
 * register packet handler for HCI packets: ACL, SCO, and Events
 */
static void transport_register_packet_handler(void (*handler)(uint8_t packet_type, uint8_t *packet, uint16_t size)){
    log_info("transport_register_packet_handler");
    transport_packet_handler = handler;
}

static int transport_can_send_packet_now(uint8_t packet_type) {
    return esp_vhci_host_check_send_available();
}

static int transport_send_packet(uint8_t packet_type, uint8_t *packet, int size){
    // store packet type before actual data and increase size
    size++;
    packet--;
    *packet = packet_type;

    // send packet
    esp_vhci_host_send_packet(packet, size);
    return 0;
}

static const hci_transport_t transport = {
    "esp32-vhci",
    &transport_init,
    &transport_open,
    &transport_close,
    &transport_register_packet_handler,
    &transport_can_send_packet_now,
    &transport_send_packet,
    NULL, // set baud rate
    NULL, // reset link
    NULL, // set SCO config
};

static const hci_transport_t * transport_get_instance(void){
    return &transport;
}

static btstack_packet_callback_registration_t hci_event_callback_registration;

static void packet_handler (uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    if (packet_type != HCI_EVENT_PACKET) return;
    switch(hci_event_packet_get_type(packet)){
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
            printf("BTstack up and running.\n");
            break;
        case HCI_EVENT_COMMAND_COMPLETE:
            if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_read_local_version_information)){
                // @TODO
            }
            break;
        default:
            break;
    }
}

static void btstack_setup(void){

    hci_dump_open(NULL, HCI_DUMP_STDOUT);

    /// GET STARTED with BTstack ///
    btstack_memory_init();
    btstack_run_loop_init(btstack_run_loop_freertos_single_threaded_get_instance());

    // init HCI
    hci_init(transport_get_instance(), NULL);
    //hci_set_link_key_db(btstack_link_key_db_memory_instance()); // @TODO
    //hci_set_chipset(btstack_chipset_cc256x_instance()); // @TODO

    // inform about BTstack state
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);
}

extern int btstack_main(int argc, const char * argv[]);

// main
int app_main(void){

    printf("btstack_task start\n");

    printf("-------------- btstack_setup\n");
    btstack_setup();
    printf("-------------- btstack_setup end\n");

    printf("-------------- btstack_main\n");
    btstack_main(0, NULL);
    printf("-------------- btstack_main end\n");

    // printf("Entering btstack run loop!\n");
    // btstack_run_loop_execute();
    // printf("Run loop exited...this is unexpected\n");

    btstack_run_loop_execute();
    return 0;
}


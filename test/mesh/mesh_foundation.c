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

#define __BTSTACK_FILE__ "mesh_foundation.c"

#include <stdint.h>
#include <stdio.h>


#define MESH_TTL_MAX 0x7f
#define MESH_FOUNDATION_STATE_NOT_SUPPORTED 2

static uint8_t mesh_foundation_gatt_proxy = 0;
static uint8_t mesh_foundation_beacon = 0;
static uint8_t mesh_foundation_default_ttl = 7;
static uint8_t mesh_foundation_network_transmit = (10 << 3) | 2; // step 300 ms, send 3 times
static uint8_t mesh_foundation_relay = MESH_FOUNDATION_STATE_NOT_SUPPORTED;
static uint8_t mesh_foundation_relay_retransmit = 0;
static uint8_t mesh_foundation_friend = MESH_FOUNDATION_STATE_NOT_SUPPORTED; // not supported

void mesh_foundation_gatt_proxy_set(uint8_t ttl){
    mesh_foundation_gatt_proxy = ttl;
    printf("MESH: GATT PROXY %x\n", mesh_foundation_gatt_proxy);
}
uint8_t mesh_foundation_gatt_proxy_get(void){
    return mesh_foundation_gatt_proxy;
}

void mesh_foundation_beacon_set(uint8_t ttl){
    mesh_foundation_beacon = ttl;
    printf("MESH: Secure Network Beacon %x\n", mesh_foundation_beacon);
}
uint8_t mesh_foundation_becaon_get(void){
    return mesh_foundation_beacon;
}

void mesh_foundation_default_ttl_set(uint8_t ttl){
    mesh_foundation_default_ttl = ttl;
    printf("MESH: Default TTL = 0x%x\n", mesh_foundation_default_ttl);
}
uint8_t mesh_foundation_default_ttl_get(void){
    return mesh_foundation_default_ttl;
}

void mesh_foundation_friend_set(uint8_t ttl){
    mesh_foundation_friend = ttl;
    printf("MESH: Friend = 0x%x\n", mesh_foundation_friend);
}
uint8_t mesh_foundation_friend_get(void){
    return mesh_foundation_friend;
}

void mesh_foundation_network_transmit_set(uint8_t network_transmit){
    mesh_foundation_network_transmit = network_transmit;
    printf("MESH: Network Transmit = 0x%02x - %u transmissions, %u ms interval\n",
            mesh_foundation_network_transmit, (mesh_foundation_network_transmit & 7) + 1, (mesh_foundation_network_transmit >> 3) * 10);
}
uint8_t mesh_foundation_network_transmit_get(void){
    return mesh_foundation_network_transmit;
}

void mesh_foundation_relay_set(uint8_t relay){
    mesh_foundation_relay = relay;
    printf("MESH: Relay = 0x%02x\n", mesh_foundation_relay);
}
uint8_t mesh_foundation_relay_get(void){
    return mesh_foundation_relay;
}

void mesh_foundation_relay_retransmit_set(uint8_t relay_retransmit){
    mesh_foundation_relay_retransmit = relay_retransmit;
    printf("MESH: Relay Retransmit = 0x%02x - %u transmissions, %u ms interval\n",
            mesh_foundation_relay_retransmit, (mesh_foundation_relay_retransmit & 7) + 1, (mesh_foundation_relay_retransmit >> 3) * 10);
}
uint8_t mesh_foundation_relay_retransmit_get(void){
    return mesh_foundation_relay_retransmit;
}

void mesh_foundation_node_reset(void){
    printf("MESH: NODE RESET\n");
}

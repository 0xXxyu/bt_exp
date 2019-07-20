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

#ifndef __MESH_CONFIGURATION_SERVER_H
#define __MESH_CONFIGURATION_SERVER_H

#include <stdint.h>
#include "mesh_access.h"

#ifdef __cplusplus
extern "C"
{
#endif

// typedefs

typedef struct  {
    btstack_timer_source_t timer;
    uint16_t active_features;
    uint32_t period_ms;
    uint16_t count;
    //
    uint16_t destination;
    // uint16_t count_log;
    uint8_t  period_log;
    uint8_t  ttl;
    uint16_t features;
    uint16_t netkey_index;
} mesh_heartbeat_publication_t;

typedef struct  {
    uint16_t source;
    uint16_t destination;
    uint8_t  period_log;
    uint8_t  count_log;
    uint8_t  min_hops;
    uint8_t  max_hops;
} mesh_heartbeat_subscription_t;

typedef struct {
    mesh_heartbeat_publication_t   heartbeat_publication;
    mesh_heartbeat_subscription_t  heartbeat_subscription;

} mesh_configuration_server_model_context_t;

// API

const mesh_operation_t * mesh_configuration_server_get_operations(void);

void mesh_configuration_server_feature_changed(void);

void mesh_load_virtual_addresses(void);

void mesh_delete_virtual_addresses(void);

void mesh_load_subscriptions(void);

void mesh_delete_subscriptions(void);

void mesh_load_publications(void);

void mesh_delete_publications(void);
//
void mesh_node_reset(void);

// PTS Testing
void config_nekey_list_set_max(uint16_t max);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif

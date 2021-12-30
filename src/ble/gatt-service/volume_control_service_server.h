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
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL BLUEKITCHEN
 * GMBH OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
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

/**
 * @title Volume Control Service Server
 * 
 */

#ifndef VOLUME_CONTROL_SERVICE_SERVER_H
#define VOLUME_CONTROL_SERVICE_SERVER_H

#include <stdint.h>

#if defined __cplusplus
extern "C" {
#endif

#define VOLUME_CONTROL_INVALID_CHANGE_COUNTER 0x80
#define VOLUME_CONTROL_OPCODE_NOT_SUPPORTED 0x81

typedef enum {
    VCS_MUTE_OFF = 0,
    VCS_MUTE_ON
} vcs_mute_t;

typedef enum {
    VCS_FLAG_RESET_VOLUME_SETTING = 0,
    VCS_FLAG_USER_SET_VOLUME_SETTING
} vcs_flag_t;

/**
 * @text The Volume Control Service (VCS) enables a device to expose the controls and state of its audio volume.
 * 
 * To use with your application, add `#import <volume_control_service.gatt>` to your .gatt file. 
 * After adding it to your .gatt file, you call *volume_control_service_server_init()* 
 * 
 * VCS may include zero or more instances of VOCS and zero or more instances of AICS
 * 
 */

/* API_START */

/**
 * @brief Init Volume Control Service Server with ATT DB
 * @param volume_setting        range [0,255]
 * @param mute                  see vcs_mute_t
 * @param volume_change_step
 */
void volume_control_service_server_init(uint8_t volume_setting, vcs_mute_t mute, uint8_t volume_change_step);

/**
 * @brief Set volume state.
 * @param volume_setting        range [0,255]
 * @param mute                  see vcs_mute_t
 */
void volume_control_service_server_set_volume_state(uint8_t volume_setting, vcs_mute_t mute);

/* API_END */

#if defined __cplusplus
}
#endif

#endif


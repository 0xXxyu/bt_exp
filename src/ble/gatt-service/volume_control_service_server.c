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

#define BTSTACK_FILE__ "volume_control_service_server.c"

/**
 * Implementation of the GATT Battery Service Server 
 * To use with your application, add '#import <volume_control_service.gatt' to your .gatt file
 */

#include "btstack_defines.h"
#include "ble/att_db.h"
#include "ble/att_server.h"
#include "btstack_util.h"
#include "bluetooth_gatt.h"
#include "btstack_debug.h"

#include "ble/gatt-service/volume_control_service_server.h"

#define VCS_CMD_RELATIVE_VOLUME_DOWN                0x00
#define VCS_CMD_RELATIVE_VOLUME_UP                  0x01
#define VCS_CMD_UNMUTE_RELATIVE_VOLUME_DOWN         0x02
#define VCS_CMD_UNMUTE_RELATIVE_VOLUME_UP           0x03
#define VCS_CMD_SET_ABSOLUTE_VOLUME                 0x04
#define VCS_CMD_UNMUTE                              0x05
#define VCS_CMD_MUTE                                0x06


#define VCS_TASK_SEND_VOLUME_SETTING                0x01
#define VCS_TASK_SEND_VOLUME_FLAGS                  0x02

static att_service_handler_t volume_control_service;

static btstack_context_callback_registration_t  vcs_callback;
static hci_con_handle_t  vcs_con_handle;
static uint8_t           vcs_tasks;

static uint8_t    vcs_volume_change_step_size;

// characteristic: VOLUME_STATE 
static uint16_t   vcs_volume_state_handle;

static uint16_t   vcs_volume_state_client_configuration_handle;
static uint16_t   vcs_volume_state_client_configuration;

static vcs_mute_t vcs_volume_state_mute;            // 0 - not_muted, 1 - muted
static uint8_t    vcs_volume_state_volume_setting;  // range [0,255]
static uint8_t    vcs_volume_state_change_counter;  // range [0,255], ounter is increased for every change on mute and volume setting

// characteristic: VOLUME_FLAGS
static uint16_t   vcs_volume_flags_handle;

static uint16_t   vcs_volume_flags_client_configuration_handle;
static uint16_t   vcs_volume_flags_client_configuration;

static vcs_flag_t vcs_volume_flags;

// characteristic: CONTROL_POINT
static uint16_t   vcs_control_point_value_handle;


static void volume_control_service_update_change_counter(void){
    vcs_volume_state_change_counter++;
}

static void volume_control_service_volume_up(void){
    if (vcs_volume_state_volume_setting < (255 - vcs_volume_change_step_size)){
        vcs_volume_state_volume_setting += vcs_volume_change_step_size;
    } else {
        vcs_volume_state_volume_setting = 255;
    }
} 

static void volume_control_service_volume_down(void){
    if (vcs_volume_state_volume_setting > vcs_volume_change_step_size){
        vcs_volume_state_volume_setting -= vcs_volume_change_step_size;
    } else {
        vcs_volume_state_volume_setting = 0;
    }
} 

static void volume_control_service_mute(void){
    vcs_volume_state_mute = VCS_MUTE_ON;
}

static void volume_control_service_unmute(void){
    vcs_volume_state_mute = VCS_MUTE_OFF;
}

static uint16_t volume_control_service_read_callback(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t offset, uint8_t * buffer, uint16_t buffer_size){
    UNUSED(con_handle);

    if (attribute_handle == vcs_volume_state_handle){
        uint8_t value[3];
        value[0] = vcs_volume_state_volume_setting;
        value[1] = (uint8_t) vcs_volume_state_mute;
        value[2] = vcs_volume_state_change_counter;
        return att_read_callback_handle_blob(value, sizeof(value), offset, buffer, buffer_size);
    }

    if (attribute_handle == vcs_volume_flags_handle){
        return att_read_callback_handle_byte((uint8_t)vcs_volume_flags, offset, buffer, buffer_size);
    }

    return 0;
}


static void volume_control_service_can_send_now(void * context){
    UNUSED(context);

    // checks task
    if ((vcs_tasks & VCS_TASK_SEND_VOLUME_SETTING) != 0){
        vcs_tasks &= ~VCS_TASK_SEND_VOLUME_SETTING;
        
        volume_control_service_update_change_counter();

        uint8_t buffer[3];
        buffer[0] = vcs_volume_state_volume_setting;
        buffer[1] = (uint8_t)vcs_volume_state_mute;
        buffer[2] = vcs_volume_state_change_counter;
        att_server_notify(vcs_con_handle, vcs_volume_state_handle, &buffer[0], sizeof(buffer));

    } else if ((vcs_tasks & VCS_TASK_SEND_VOLUME_FLAGS) != 0){
        vcs_tasks &= ~VCS_TASK_SEND_VOLUME_FLAGS;

        uint8_t value = (uint8_t)vcs_volume_flags;
        att_server_notify(vcs_con_handle, vcs_volume_flags_handle, &value, 1);
    }

    if (vcs_tasks != 0){
        att_server_register_can_send_now_callback(&vcs_callback, vcs_con_handle);
    }
}

static void volume_control_service_server_set_callback(uint8_t task){
    uint8_t scheduled_tasks = vcs_tasks;
    vcs_tasks |= task;
    if (scheduled_tasks == 0){
        vcs_callback.callback = &volume_control_service_can_send_now;
        att_server_register_can_send_now_callback(&vcs_callback, vcs_con_handle);
    }   
}

static void volume_control_service_server_enable_user_set_volume_setting_flag(void){
    vcs_volume_flags = VCS_FLAG_USER_SET_VOLUME_SETTING;
    if (vcs_volume_flags_client_configuration != 0){
        volume_control_service_server_set_callback(VCS_TASK_SEND_VOLUME_FLAGS);
    }
}

static int volume_control_service_write_callback(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size){
    UNUSED(transaction_mode);
    UNUSED(offset);

    if (attribute_handle == vcs_control_point_value_handle){
        if (buffer_size < 2){
            return VOLUME_CONTROL_OPCODE_NOT_SUPPORTED;
        }

        uint8_t cmd = buffer[0];
        uint8_t change_counter = buffer[1];

        if (change_counter != vcs_volume_state_change_counter){
            return VOLUME_CONTROL_INVALID_CHANGE_COUNTER;
        }

        uint8_t    old_volume_setting = vcs_volume_state_volume_setting;
        vcs_mute_t old_mute = vcs_volume_state_mute;

        switch (cmd){
            case VCS_CMD_RELATIVE_VOLUME_DOWN:  
                volume_control_service_volume_down();
                break;

            case VCS_CMD_RELATIVE_VOLUME_UP:
                volume_control_service_volume_up();
                break;

            case VCS_CMD_UNMUTE_RELATIVE_VOLUME_DOWN:
                volume_control_service_volume_down();
                volume_control_service_unmute();
                break;

            case VCS_CMD_UNMUTE_RELATIVE_VOLUME_UP:     
                volume_control_service_volume_up();
                volume_control_service_unmute();
                break;

            case VCS_CMD_SET_ABSOLUTE_VOLUME: 
                if (buffer_size != 3){
                    return VOLUME_CONTROL_OPCODE_NOT_SUPPORTED;
                }
                vcs_volume_state_volume_setting = buffer[2];
                break;
            
            case VCS_CMD_UNMUTE:                    
                volume_control_service_unmute();
                break;

            case VCS_CMD_MUTE:                          
                volume_control_service_mute();
                break;

            default:
                return VOLUME_CONTROL_OPCODE_NOT_SUPPORTED;
        }
        
        if ((old_volume_setting != vcs_volume_state_volume_setting) || (old_mute != vcs_volume_state_mute)){
            volume_control_service_update_change_counter();
        
            if (vcs_volume_flags == VCS_FLAG_RESET_VOLUME_SETTING){
                volume_control_service_server_enable_user_set_volume_setting_flag();
            }
        }
    } 

    else if (attribute_handle == vcs_volume_state_client_configuration_handle){
        vcs_volume_state_client_configuration = little_endian_read_16(buffer, 0);
        vcs_con_handle = con_handle;
    }

    else if (attribute_handle == vcs_volume_flags_client_configuration_handle){
        vcs_volume_flags_client_configuration = little_endian_read_16(buffer, 0);
        vcs_con_handle = con_handle;
    }

    return 0;
}

void volume_control_service_server_init(uint8_t volume_setting, vcs_mute_t mute, uint8_t volume_change_step){
    vcs_volume_state_volume_setting = volume_setting;
    vcs_volume_state_mute = mute;
    vcs_volume_flags = VCS_FLAG_RESET_VOLUME_SETTING;
    vcs_volume_change_step_size = volume_change_step;
    
    vcs_tasks = 0;
    
    // get service handle range
    uint16_t start_handle = 0;
    uint16_t end_handle   = 0xfff;
    int service_found = gatt_server_get_handle_range_for_service_with_uuid16(ORG_BLUETOOTH_SERVICE_VOLUME_CONTROL, &start_handle, &end_handle);
    btstack_assert(service_found != 0);
    UNUSED(service_found);

    // get characteristic value handle and client configuration handle
    vcs_control_point_value_handle = gatt_server_get_value_handle_for_characteristic_with_uuid16(start_handle, end_handle, ORG_BLUETOOTH_CHARACTERISTIC_VOLUME_CONTROL_POINT);
    
    vcs_volume_state_handle = gatt_server_get_value_handle_for_characteristic_with_uuid16(start_handle, end_handle, ORG_BLUETOOTH_CHARACTERISTIC_VOLUME_STATE);
    vcs_volume_state_client_configuration_handle = gatt_server_get_client_configuration_handle_for_characteristic_with_uuid16(start_handle, end_handle, ORG_BLUETOOTH_CHARACTERISTIC_VOLUME_STATE);

    vcs_volume_flags_handle = gatt_server_get_value_handle_for_characteristic_with_uuid16(start_handle, end_handle, ORG_BLUETOOTH_CHARACTERISTIC_VOLUME_FLAGS);
    vcs_volume_flags_client_configuration_handle = gatt_server_get_client_configuration_handle_for_characteristic_with_uuid16(start_handle, end_handle, ORG_BLUETOOTH_CHARACTERISTIC_VOLUME_FLAGS);

    // register service with ATT Server
    volume_control_service.start_handle   = start_handle;
    volume_control_service.end_handle     = end_handle;
    volume_control_service.read_callback  = &volume_control_service_read_callback;
    volume_control_service.write_callback = &volume_control_service_write_callback;
    att_server_register_service_handler(&volume_control_service);
}

void volume_control_service_server_set_volume_state(uint8_t volume_setting, vcs_mute_t mute){
    uint8_t update_value = false;

    if (vcs_volume_state_volume_setting != volume_setting){
        vcs_volume_state_volume_setting = volume_setting;
        update_value = true;
    }

    if (vcs_volume_state_mute != mute){
        vcs_volume_state_mute = mute;
        update_value = true;
    }
    
    if (update_value && (vcs_volume_state_client_configuration != 0)){
        volume_control_service_server_set_callback(VCS_TASK_SEND_VOLUME_SETTING);
    }
}

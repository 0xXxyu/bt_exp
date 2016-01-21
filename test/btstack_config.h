//
// btstack_config.h for Arduino port
//

#ifndef __BTSTACK_CONFIG
#define __BTSTACK_CONFIG

// Port related features
#define HAVE_INIT_SCRIPT
#define HAVE_BZERO
#define HAVE_TIME
#define HAVE_MALLOC

// BTstack features that can be enabled
#define ENABLE_BLE
#define ENABLE_LOG_DEBUG
#define ENABLE_LOG_ERROR
#define ENABLE_LOG_INFO 
#define ENABLE_LOG_INTO_HCI_DUMP
#define ENABLE_SDP_DES_DUMP
#define ENABLE_SDP_EXTRA_QUERIES

// BTstack configuration. buffers, sizes, ...
#define HCI_ACL_PAYLOAD_SIZE 52
#define HCI_INCOMING_PRE_BUFFER_SIZE 4

#endif

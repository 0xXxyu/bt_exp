//
// btstack_config.h for most tests
//

#ifndef __BTSTACK_CONFIG
#define __BTSTACK_CONFIG

// Port related features
#define HAVE_MALLOC
#define HAVE_POSIX_TIME
#define HAVE_POSIX_FILE_IO
#define HAVE_BTSTACK_STDIN

// BTstack features that can be enabled
#define ENABLE_BLE
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_CENTRAL
// #define ENABLE_LOG_DEBUG
#define ENABLE_LOG_ERROR
#define ENABLE_LOG_INFO 
#define ENABLE_MICRO_ECC_P256

#define ENABLE_MESH
#define ENABLE_MESH_PROVISIONER

// BTstack configuration. buffers, sizes, ...
#define HCI_ACL_PAYLOAD_SIZE 1000
#define HCI_INCOMING_PRE_BUFFER_SIZE 4

#define MAX_NR_LE_DEVICE_DB_ENTRIES 4

#define NVM_NUM_LINK_KEYS 2

#endif

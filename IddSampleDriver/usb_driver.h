#pragma once

#include <wdf.h>
#include <stdint.h>
#include "basetype.h"

#define MAX_URB_SIZE 3
#define MAX_RETRY_COUNT 3
#define USB_ERROR_RESET_THRESHOLD 5
#define USB_INFO_CFG_MAX 10

typedef struct _urb_item {
    SLIST_ENTRY node;
    WDFUSBPIPE pipe;
    int id;
    uint8_t urb_msg[1920 * 1080 * 4];
    PSLIST_HEADER urb_list;
    WDFREQUEST Request;
    WDFMEMORY wdfMemory;
} urb_item_t, *purb_item_t;

// USB transfer resource initialization
int usb_transf_init(SLIST_HEADER* urb_list);

// USB transfer resource cleanup
int usb_transf_exit(SLIST_HEADER* urb_list);

// USB asynchronous data send with retry
NTSTATUS usb_send_msg_async(urb_item_t* urb, WDFUSBPIPE pipe, WDFREQUEST Request, PUCHAR msg, int tsize);

// USB enumeration information parsing
NTSTATUS get_usb_dev_string_info(WDFDEVICE Device, TCHAR* stringBuf);

// USB interface selection
NTSTATUS SelectInterfaces(WDFDEVICE Device);

// USB hot-plug support
NTSTATUS usb_device_connect(WDFDEVICE Device);
NTSTATUS usb_device_disconnect(WDFDEVICE Device);
NTSTATUS usb_device_reset(WDFDEVICE Device);

// USB error recovery
NTSTATUS usb_error_recovery(WDFDEVICE Device, NTSTATUS error_code);
NTSTATUS usb_check_connection_state(WDFDEVICE Device, usb_connection_state_t* state);

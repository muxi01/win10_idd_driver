#pragma once

#include <wdf.h>
#include <stdint.h>
#include "basetype.h"

#define MAX_URB_SIZE    5
#define MAX_RETRY_COUNT 3
#define USB_ERROR_RESET_THRESHOLD 5
#define USB_INFO_CFG_MAX 10
#define USB_BUFF_SIZE   (1920 * 1080 * 4)
#define USB_SEND_TIMEOUT_MS  500

typedef struct _urb_item {
    SLIST_ENTRY node;
    WDFUSBPIPE pipe;
    int id;
    uint8_t* urb_msg;  // Dynamically allocated buffer
    int urb_msg_size;  // Size of urb_msg buffer
    PSLIST_HEADER urb_list;
    WDFREQUEST Request;
    WDFMEMORY wdfMemory;  // Pre-allocated WDF memory for USB transfer
} urb_item_t, *purb_item_t;

// USB transfer resource initialization
int usb_resouce_init(SLIST_HEADER* urb_list, int width, int height);

// USB transfer resource cleanup
int usb_resouce_distory(SLIST_HEADER* urb_list);

// USB asynchronous data send with retry
NTSTATUS usb_send_data_async(urb_item_t* urb, WDFUSBPIPE pipe, int tsize);

// USB synchronous data send (for debugging)
NTSTATUS usb_send_data_sync(urb_item_t* urb, WDFUSBPIPE pipe, int tsize);

// USB enumeration information parsing
NTSTATUS usb_get_discribe_info(WDFDEVICE Device, TCHAR* stringBuf);

// USB hot-plug support
NTSTATUS usb_device_connect(WDFDEVICE Device);
NTSTATUS usb_device_disconnect(WDFDEVICE Device);
BOOLEAN usb_is_connected(WDFDEVICE Device);

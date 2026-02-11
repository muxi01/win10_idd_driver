#pragma once

#include <stdint.h>
#include <wdf.h>

// Type definitions
typedef uint8_t  _u8;
typedef uint16_t _u16;
typedef uint32_t _u32;
typedef uint32_t pixel_type_t;

// Default configuration constants
#define DEFAULT_WIDTH         1920
#define DEFAULT_HEIGHT        1080
#define DEFAULT_FPS           60
#define DEFAULT_JPEG_QUALITY  5
#define DEFAULT_BUFFER_LIMIT  (DEFAULT_WIDTH * DEFAULT_HEIGHT * 4)

// USB and timing constants
#define WAIT_TIMEOUT_MS       16
#define STATS_PRINT_INTERVAL  100

// Default encoder type
#define IMAGE_TYPE_RGB565  (('R' << 0) | ('G' << 8) | ('B' << 16) | ('6' << 24))
#define IMAGE_TYPE_RGB888  (('R' << 0) | ('G' << 8) | ('B' << 16) | ('8' << 24))
#define IMAGE_TYPE_YUV420  (('Y' << 0) | ('4' << 8) | ('2' << 16) | ('0' << 24))
#define IMAGE_TYPE_JPG     (('J' << 0) | ('P' << 8) | ('E' << 16) | ('G' << 24))
#define IMAGE_TYPE_NULL    (('N' << 0) | ('U' << 8) | ('L' << 16) | ('L' << 24))
#define FRAME_MAGIC_ID     (('l' << 0) | ('v' << 8) | ('s' << 16) | ('n' << 24))

// USB device connection state
typedef enum _usb_connection_state {
    USB_STATE_CONNECTED = 0,
    USB_STATE_DISCONNECTED = 1,
    USB_STATE_ERROR = 2,
    USB_STATE_RECOVERING = 3
} usb_connection_state_t;

// Error recovery strategy
typedef enum _error_recovery_strategy {
    RECOVERY_RETRY = 0,
    RECOVERY_RESET = 1,
    RECOVERY_REINIT = 2
} error_recovery_strategy_t;

// Performance statistics
typedef struct _perf_stats {
    uint64_t total_frames;
    uint64_t dropped_frames;
    uint64_t error_frames;
    uint64_t total_bytes;
    uint64_t urbs_sent;
    uint64_t urbs_failed;
    int64_t avg_grab_time_us;
    int64_t avg_encode_time_us;
    int64_t avg_send_time_us;
    int64_t avg_total_time_us;
} perf_stats_t;

// USB info string item
#define USB_INFO_STR_SIZE 32

typedef struct _usb_info_item {
    char str[USB_INFO_STR_SIZE];
} usb_info_item_t;

// USB device configuration
typedef struct _usb_dev_config {
    int reg_idx;      // Register index
    int width;        // Display width
    int height;       // Display height
    int img_type;     // Encoding type (0=RGB565, 1=RGB888, 3=JPEG)
    int img_qlt;      // JPEG quality
    int fps;         // Target FPS
    int sleep;         // Sleep time in cycles 
    int debug;          //debug level
} usb_dev_config_t;

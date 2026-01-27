#pragma once

#include <stdarg.h>
#include "basetype.h"

#ifdef __cplusplus
extern "C" {
#endif

enum LogLevel {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN = 1,
    LOG_LEVEL_INFO = 2,
    LOG_LEVEL_DEBUG = 3,
    LOG_LEVEL_TRACE = 4
};

extern LONG debug_level;

#define LOG_CHECK(level) do { \
    if (debug_level >= LOG_LEVEL_##level) \
    { \
    } while(0)

#define LOGE(fmt, ...) do { \
    if (debug_level >= LOG_LEVEL_ERROR) { \
        tools_log("[ERROR] " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

#define LOGW(fmt, ...) do { \
    if (debug_level >= LOG_LEVEL_WARN) { \
        tools_log("[WARN] " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

#define LOGI(fmt, ...) do { \
    if (debug_level >= LOG_LEVEL_INFO) { \
        tools_log("[INFO] " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

#define LOGD(fmt, ...) do { \
    if (debug_level >= LOG_LEVEL_DEBUG) { \
        tools_log("[DEBUG] " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

#define LOGM(fmt, ...) do { \
    if (debug_level >= LOG_LEVEL_TRACE) { \
        tools_log("[MSG] " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

// Time functions
int64_t tools_get_time_us(void);

// Logging functions
void tools_log(const char* fmt, ...);

// FPS control
void tools_sample_tick(int fps);

// Performance statistics
void tools_perf_stats_init(perf_stats_t* stats);
void tools_perf_stats_update(perf_stats_t* stats, int frame_size,
                             int64_t grab_time, int64_t encode_time,
                             int64_t send_time, int success);
void tools_perf_stats_print(perf_stats_t* stats);
void tools_perf_stats_reset(perf_stats_t* stats);

// USB device info parsing
int tools_split_config_str(char* str, usb_info_item_t* cfg, int max);
void tools_parse_usb_dev_info(char* str, usb_dev_config_t* config);

#ifdef __cplusplus
}
#endif

#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "basetype.h"
#include "tools.h"

// Missing constants from Driver.h and usb_driver.h
#define UDISP_CONFIG_STR_LEN  256
#define USB_INFO_CFG_MAX 10

int64_t tools_get_time_us(void)
{
    FILETIME time;
    LARGE_INTEGER li;
    GetSystemTimePreciseAsFileTime(&time);
    li.LowPart = time.dwLowDateTime;
    li.HighPart = time.dwHighDateTime;
    return li.QuadPart / 10;
}

void tools_log(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    OutputDebugStringA(buf);
    va_end(args);
}

void tools_sample_tick(int fps)
{
    static int64_t last_tick_time = 0;
    static uint8_t initialized = 0;

    if (fps <= 0) return;

    const int64_t period_us = 1000000 / fps;
    int64_t current_time = tools_get_time_us();

    if (!initialized) {
        last_tick_time = current_time;
        initialized = 1;
        return;
    }

    int64_t elapsed_time = current_time - last_tick_time;

    if (elapsed_time < period_us) {
        int64_t sleep_us = period_us - elapsed_time;
        DWORD sleep_ms = (DWORD)((sleep_us + 500) / 1000);

        if (sleep_ms > 0) {
            Sleep(sleep_ms);
        }
    }

    last_tick_time = tools_get_time_us();
}

void tools_perf_stats_init(perf_stats_t* stats)
{
    if (stats == NULL) return;
    memset(stats, 0, sizeof(perf_stats_t));
}

void tools_perf_stats_update(perf_stats_t* stats, int frame_size,int64_t grab_time, int64_t encode_time,int64_t send_time, int success)
{
    if (stats == NULL) return;

    const int64_t total_time = grab_time + encode_time + send_time;
    const uint64_t count = stats->total_frames + 1;

    stats->total_frames = count;
    stats->total_bytes += frame_size;

    if (success) {
        stats->urbs_sent++;
    } else {
        stats->error_frames++;
        stats->urbs_failed++;
    }

    // Update average times using exponential moving average (alpha = 0.1)
    const float alpha = 0.1f;

    if (stats->avg_grab_time_us == 0) {
        stats->avg_grab_time_us = grab_time;
        stats->avg_encode_time_us = encode_time;
        stats->avg_send_time_us = send_time;
        stats->avg_total_time_us = total_time;
    } else {
        stats->avg_grab_time_us = (int64_t)(stats->avg_grab_time_us * (1 - alpha) + grab_time * alpha);
        stats->avg_encode_time_us = (int64_t)(stats->avg_encode_time_us * (1 - alpha) + encode_time * alpha);
        stats->avg_send_time_us = (int64_t)(stats->avg_send_time_us * (1 - alpha) + send_time * alpha);
        stats->avg_total_time_us = (int64_t)(stats->avg_total_time_us * (1 - alpha) + total_time * alpha);
    }
}

void tools_perf_stats_print(perf_stats_t* stats)
{
    if (stats == NULL) return;

    LOGW("=== Performance Statistics ===\n");
    LOGW("Total frames: %llu\n", stats->total_frames);
    LOGW("Dropped frames: %llu\n", stats->dropped_frames);
    LOGW("Error frames: %llu\n", stats->error_frames);
    LOGW("Total bytes: %llu MB\n", stats->total_bytes / (1024 * 1024));
    LOGW("URBs sent: %llu\n", stats->urbs_sent);
    LOGW("URBs failed: %llu\n", stats->urbs_failed);
    LOGW("Avg grab time: %lld us\n", stats->avg_grab_time_us);
    LOGW("Avg encode time: %lld us\n", stats->avg_encode_time_us);
    LOGW("Avg send time: %lld us\n", stats->avg_send_time_us);
    LOGW("Avg total time: %lld us\n", stats->avg_total_time_us);

    if (stats->total_frames > 0) {
        const float success_rate = (float)(stats->total_frames - stats->error_frames) / stats->total_frames * 100;
        LOGW("Success rate: %.2f%%\n", success_rate);
    }
}

void tools_perf_stats_reset(perf_stats_t* stats)
{
    if (stats == NULL) return;
    tools_perf_stats_init(stats);
}


int tools_split_config_str(char* str, usb_info_item_t* cfg, int max)
{
    int cnt = 0;
    char* token;
    char* context = NULL;
    char str_copy[UDISP_CONFIG_STR_LEN] = { 0 };

    if (str == NULL || cfg == NULL || max <= 0) {
        return 0;
    }

    strncpy_s(str_copy, sizeof(str_copy), str, _TRUNCATE);

    token = strtok_s(str_copy, "_", &context);
    while (token != NULL && cnt < max) {
        strncpy_s(cfg[cnt].str, sizeof(cfg[cnt].str), token, _TRUNCATE);
        LOGD("[%d] %s\n", cnt, cfg[cnt].str);
        cnt++;
        token = strtok_s(NULL, "_", &context);
    }

    return cnt;
}

void tools_parse_usb_dev_info(char* str, usb_dev_config_t* config)
{
    usb_info_item_t cfg[USB_INFO_CFG_MAX] = { 0 };

    if (str == NULL || config == NULL) {
        return;
    }

    int cnt = tools_split_config_str(str, cfg, USB_INFO_CFG_MAX);

    if (cnt < 2) {
        LOGW("%s [%d] dev info is not recognized, using default\n", str, cnt);
    }

    // Set default values
    config->reg_idx = 0;
    config->width = 800;
    config->height =480;
    config->fps = 30;
    config->img_type = IMAGE_TYPE_JPG;
    config->img_qlt = 5;
    config->debug =debug_level= LOG_LEVEL_INFO;
    config->sleep = 5;
#if 1
    // Parse configuration items
    for (int i = 0; i < cnt; i++) {
        char* item_str = cfg[i].str;
        LOGD("%d %s\n", i, item_str);

        switch (item_str[0]) {
        case 'U': {
            int reg_id;
            if (sscanf_s(item_str, "U%d", &reg_id) == 1) {
                config->reg_idx = reg_id;
                LOGI("udisp reg idx:%d\n", config->reg_idx);
            }
        }
        break;

        case 'D': {
            int debug,sleep;
            if (sscanf_s(item_str, "D%dx%d", &debug,&sleep) == 2) {
                debug_level = debug;
                config->sleep = sleep;
                config->debug = debug;
                LOGI("udisp debug:%d sleep %d\n", debug,sleep);
            }
        }
        break;
        case 'R': {
            int w = 0, h = 0,fps=0;
            if (sscanf_s(item_str, "R%dx%dx%d", &w, &h,&fps) == 3) {
                config->width = w;
                config->height = h;
                config->fps = fps;
                LOGI("udisp width:%d height:%d fps%d\n",w,h,fps);
            }  
        }
        break;
        case 'E':{
            int encode,quelity;
            if (sscanf_s(item_str, "E%dx%d", &encode,&quelity) == 2) {
                if(encode <= IMAGE_TYPE_JPG) {
                    if(encode ==0) {
                        config->img_type =IMAGE_TYPE_RGB565;
                    }
                    else if(encode ==1) {
                        config->img_type = IMAGE_TYPE_RGB888;
                    }
                    else if(encode == 2) {
                        config->img_type = IMAGE_TYPE_YUV420;
                    }
                    else if(encode == 3) {
                        config->img_type = IMAGE_TYPE_JPG;
                    }
                    config->img_qlt = quelity;
                    LOGI("Encode type:%d quality:%d\n", encode, quelity);
                }
            }
        }
        break;

        default:
            LOGW("Unknown encoder type '%c', using JPEG default\n", item_str[1]);
        break;
        }
    }
#endif 
    LOGI("Parsed USB config: w=%d h=%d enc=%d quality=%d fps=%d\n", 
        config->width, config->height, config->img_type,config->img_qlt, config->fps);
}

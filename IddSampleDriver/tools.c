#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "tools.h"
#include "image_encoder.h"

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

    LOGI("=== Performance Statistics ===\n");
    LOGI("Total frames: %llu\n", stats->total_frames);
    LOGI("Dropped frames: %llu\n", stats->dropped_frames);
    LOGI("Error frames: %llu\n", stats->error_frames);
    LOGI("Total bytes: %llu MB\n", stats->total_bytes / (1024 * 1024));
    LOGI("URBs sent: %llu\n", stats->urbs_sent);
    LOGI("URBs failed: %llu\n", stats->urbs_failed);
    LOGI("Avg grab time: %lld us\n", stats->avg_grab_time_us);
    LOGI("Avg encode time: %lld us\n", stats->avg_encode_time_us);
    LOGI("Avg send time: %lld us\n", stats->avg_send_time_us);
    LOGI("Avg total time: %lld us\n", stats->avg_total_time_us);

    if (stats->total_frames > 0) {
        const float success_rate = (float)(stats->total_frames - stats->error_frames) / stats->total_frames * 100;
        LOGI("Success rate: %.2f%%\n", success_rate);
    }
}

void tools_perf_stats_reset(perf_stats_t* stats)
{
    if (stats == NULL) return;
    tools_perf_stats_init(stats);
}

static int tools_parse_int_value(const char* str, const char* prefix)
{
    int value = 0;
    if (sscanf_s(str, prefix, &value) == 1) {
        return value;
    }
    return -1;
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
    int cnt = 0, i;
    int enc_configured = 0;

    if (str == NULL || config == NULL) {
        return;
    }

    cnt = tools_split_config_str(str, cfg, USB_INFO_CFG_MAX);

    if (cnt < 2) {
        LOGW("%s [%d] dev info string violation xfz1986 udisp SPEC, using default\n", str, cnt);
    }

    // Set default values
    config->reg_idx = 0;
    config->width = 0;
    config->height = 0;
    config->fps = 60;
    config->enc_type = IMAGE_TYPE_JPG;
    config->quality = 5;
    config->blimit = 1920 * 1080 * 4;

    // Parse register index (last digit of first item)
    int len = (int)strlen(cfg[0].str);
    if (len > 0 && isdigit(cfg[0].str[len - 1])) {
        sscanf_s(&cfg[0].str[len - 1], "%d", &config->reg_idx);
        LOGI("udisp reg idx:%d\n", config->reg_idx);
    }

    // Parse configuration items
    for (i = 1; i < cnt; i++) {
        char* item_str = cfg[i].str;
        LOGD("%d %s\n", i, item_str);

        switch (item_str[0]) {
        case 'B': {
            int blimit = tools_parse_int_value(item_str, "Bl%d");
            if (blimit > 0) {
                config->blimit = blimit * 1024;
                LOGI("blimit:%d\n", config->blimit);
            }
            break;
        }
        case 'R': {
            int w = 0, h = 0;
            if (sscanf_s(item_str, "R%dx%d", &w, &h) == 2) {
                config->width = w;
                config->height = h;
                LOGI("udisp w%d h%d\n", config->width, config->height);
            }
            break;
        }
        case 'F': {
            int fps = tools_parse_int_value(item_str, "Fps%d");
            if (fps > 0) {
                config->fps = fps;
                LOGI("udisp fps%d\n", config->fps);
            }
            break;
        }
        case 'E':
            if (enc_configured) {
                break;
            }
            switch (item_str[1]) {
            case 'j': {
                int quality = tools_parse_int_value(item_str, "Ejpg%d");
                if (quality > 0) {
                    config->enc_type = IMAGE_TYPE_JPG;
                    config->quality = quality;
                    enc_configured++;
                    LOGI("enc:%d quality:%d\n", config->enc_type, config->quality);
                }
                break;
            }
            case 'r': {
                int quality = tools_parse_int_value(item_str, "Ergb%d");
                if (quality == 16) {
                    config->enc_type = IMAGE_TYPE_RGB565;
                    config->quality = quality;
                    enc_configured++;
                    LOGI("enc:%d quality:%d\n", config->enc_type, config->quality);
                } else if (quality == 32) {
                    config->enc_type = IMAGE_TYPE_RGB888;
                    config->quality = quality;
                    enc_configured++;
                    LOGI("enc:%d quality:%d\n", config->enc_type, config->quality);
                } else {
                    config->enc_type = IMAGE_TYPE_RGB888;
                    LOGW("Unknown RGB quality %d, using RGB888\n", quality);
                }
                break;
            }
            default:
                LOGW("Unknown encoder type '%c', using JPEG default\n", item_str[1]);
                break;
            }
            break;
        default:
            LOGD("Unknown config item: %s\n", item_str);
            break;
        }
    }

    LOGI("Parsed USB config: w=%d h=%d enc=%d quality=%d fps=%d blimit=%d\n",
         config->width, config->height, config->enc_type,
         config->quality, config->fps, config->blimit);
}

#pragma once

#include "basetype.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _MSC_VER
#define PACK(__Declaration__) __pragma(pack(push, 1)) __Declaration__ __pragma(pack(pop))
#endif

#define cpu_to_le32(x)  (uint32_t)(x)

#define FB_DISP_DEFAULT_PIXEL_BITS  32

#define IMAGE_TYPE_RGB565  0
#define IMAGE_TYPE_RGB888  1
#define IMAGE_TYPE_YUV420  2
#define IMAGE_TYPE_JPG     3

#define FRAME_MAGIC_ID  0x55AA55AA

typedef struct _image_frame_header_t {
    _u32 magic_id;
    _u32 img_type;
    _u32 img_len;
    _u32 img_cnt;
} image_frame_header_t;

typedef struct _image_encoder {
    int type;
    int quality;

    int (*encode)(struct _image_encoder* encoder,uint8_t* output,const uint8_t* input,int x, int y, int right, int bottom,int line_width, int limit);

    int (*encode_header)(struct _image_encoder* encoder,uint8_t* output,int x, int y, int right, int bottom,int total_bytes);

    void (*destroy)(struct _image_encoder* encoder);

    // JPEG specific resources
    void* jpeg_private;

} image_encoder_t;

// Create encoder instances
image_encoder_t* image_encoder_create_rgb565(void);
image_encoder_t* image_encoder_create_rgb888(void);
image_encoder_t* image_encoder_create_jpeg(int quality);

// Destroy encoder
void image_encoder_destroy(image_encoder_t* encoder);

// Frame header setup
int image_setup_frame_header(uint8_t* msg, uint32_t img_type, uint32_t img_len, uint32_t img_cnt);

#ifdef __cplusplus
}
#endif

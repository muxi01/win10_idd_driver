#pragma once

#include "basetype.h"
#include "jerror.h"
#include "jpeglib.h"
#include <stdint.h>
#include <memory>

#ifdef _MSC_VER
#define PACK(__Declaration__) __pragma(pack(push, 1)) __Declaration__ __pragma(pack(pop))
#endif

#define cpu_to_le32(x)  (uint32_t)(x)

#define FB_DISP_DEFAULT_PIXEL_BITS  32

typedef struct _image_frame_header_t {
    _u32 magic_id;
    _u32 img_type;
    _u32 img_len;
    _u32 img_cnt;
} image_frame_header_t;


// ============================================================================
// JPEG Encoder Implementation
// ============================================================================

struct jpeg_error_mgr_with_exit_t {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};


struct jpeg_encoder_private_t {
    struct jpeg_compress_struct cinfo;
    jpeg_error_mgr_with_exit_t jerr;
    uint8_t* row_buffer;
    int buffer_size;
};


// ============================================================================
// Image Encoder Class
// ============================================================================

class ImageEncoder
{
public:
    // Constructor - create encoder by type
    ImageEncoder(int type, int quality = 0);

    // Destructor
    ~ImageEncoder();

    // Public interface
    int encode(uint8_t* output, const uint8_t* input,int buffer_size,int x, int y, int width, int height);
private:
    // Encoder implementation for RGB565
    int encode_rgb565(uint8_t* output, const uint8_t* input,int buffer_size,int x, int y, int width, int height);

    // Encoder implementation for RGB888
    int encode_rgb888(uint8_t* output, const uint8_t* input,int buffer_size,int x, int y, int width, int height);

    // Encoder implementation for JPEG
    int encode_jpeg(uint8_t* output, const uint8_t* input,int buffer_size,int x, int y, int width, int height);

    // JPEG private resources
    struct jpeg_encoder_private_t* m_jpeg_private;

    void create_jpeg_encoder();

    void destroy_jpeg_encoder();
    // Encoder type and quality
    int m_type;
    int m_quality;
    _u32 m_counter;
};


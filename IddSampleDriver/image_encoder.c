#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "image_encoder.h"
#include "tools.h"

#include "jpeglib.h"
#include <setjmp.h>

// ============================================================================
// RGB565 Encoder
// ============================================================================

static int encode_rgb565_func(image_encoder_t* encoder,
                              uint8_t* output, const uint8_t* input,
                              int x, int y, int right, int bottom,
                              int line_width, int limit)
{
    UNREFERENCED_PARAMETER(encoder);
    UNREFERENCED_PARAMETER(limit);

    int pos = 0;
    const uint32_t* framebuffer = (const uint32_t*)input;
    const int stride = line_width - (right - x + 1);

    for (int row = y; row <= bottom; row++) {
        for (int col = x; col <= right; col++) {
            uint32_t pixel = *framebuffer++;

            uint8_t r = (pixel >> 16) & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t b = pixel & 0xFF;

            uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);

            output[pos++] = rgb565 & 0xFF;
            output[pos++] = (rgb565 >> 8) & 0xFF;
        }
        framebuffer += stride;
    }

    return pos;
}

static int encode_header_rgb565_func(image_encoder_t* encoder,
                                    uint8_t* output,
                                    int x, int y, int right, int bottom,
                                    int total_bytes)
{
    UNREFERENCED_PARAMETER(encoder);
    UNREFERENCED_PARAMETER(x);
    UNREFERENCED_PARAMETER(y);
    UNREFERENCED_PARAMETER(right);
    UNREFERENCED_PARAMETER(bottom);

    return image_setup_frame_header(output, IMAGE_TYPE_RGB565, total_bytes, 0);
}

static void destroy_rgb565_func(image_encoder_t* encoder)
{
    free(encoder);
}

image_encoder_t* image_encoder_create_rgb565(void)
{
    image_encoder_t* encoder = (image_encoder_t*)malloc(sizeof(image_encoder_t));
    if (encoder == NULL) {
        LOGE("Failed to allocate memory for RGB565 encoder\n");
        return NULL;
    }

    encoder->type = IMAGE_TYPE_RGB565;
    encoder->quality = 0;
    encoder->encode = encode_rgb565_func;
    encoder->encode_header = encode_header_rgb565_func;
    encoder->destroy = destroy_rgb565_func;

    LOGD("Created RGB565 encoder\n");
    return encoder;
}

// ============================================================================
// RGB888 Encoder
// ============================================================================

static int encode_rgb888_func(image_encoder_t* encoder,
                              uint8_t* output, const uint8_t* input,
                              int x, int y, int right, int bottom,
                              int line_width, int limit)
{
    UNREFERENCED_PARAMETER(encoder);
    UNREFERENCED_PARAMETER(x);
    UNREFERENCED_PARAMETER(y);
    UNREFERENCED_PARAMETER(right);
    UNREFERENCED_PARAMETER(bottom);
    UNREFERENCED_PARAMETER(line_width);

    const int width = right - x + 1;
    const int height = bottom - y + 1;
    const int total_bytes = width * height * 4;

    if (total_bytes > limit) {
        LOGE("Frame exceeds buffer limit: %d > %d\n", total_bytes, limit);
        return 0;
    }

    memcpy(output, input, total_bytes);
    return total_bytes;
}

static int encode_header_rgb888_func(image_encoder_t* encoder,
                                    uint8_t* output,
                                    int x, int y, int right, int bottom,
                                    int total_bytes)
{
    UNREFERENCED_PARAMETER(encoder);
    UNREFERENCED_PARAMETER(x);
    UNREFERENCED_PARAMETER(y);
    UNREFERENCED_PARAMETER(right);
    UNREFERENCED_PARAMETER(bottom);

    return image_setup_frame_header(output, IMAGE_TYPE_RGB888, total_bytes, 0);
}

static void destroy_rgb888_func(image_encoder_t* encoder)
{
    free(encoder);
}

image_encoder_t* image_encoder_create_rgb888(void)
{
    image_encoder_t* encoder = (image_encoder_t*)malloc(sizeof(image_encoder_t));
    if (encoder == NULL) {
        LOGE("Failed to allocate memory for RGB888 encoder\n");
        return NULL;
    }

    encoder->type = IMAGE_TYPE_RGB888;
    encoder->quality = 0;
    encoder->encode = encode_rgb888_func;
    encoder->encode_header = encode_header_rgb888_func;
    encoder->destroy = destroy_rgb888_func;

    LOGD("Created RGB888 encoder\n");
    return encoder;
}

// ============================================================================
// JPEG Encoder
// ============================================================================

typedef struct _jpeg_error_mgr_with_exit {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
} jpeg_error_mgr_with_exit_t;

typedef struct _jpeg_encoder_private {
    struct jpeg_compress_struct cinfo;
    jpeg_error_mgr_with_exit_t jerr;
    uint8_t* row_buffer;
    int buffer_size;
} jpeg_encoder_private_t;

static void jpeg_error_exit(j_common_ptr cinfo)
{
    jpeg_error_mgr_with_exit_t* myerr = (jpeg_error_mgr_with_exit_t*)cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(myerr->setjmp_buffer, 1);
}

static int encode_jpeg_func(image_encoder_t* encoder,
                            uint8_t* output, const uint8_t* input,
                            int x, int y, int right, int bottom,
                            int line_width, int limit)
{
    jpeg_encoder_private_t* priv = (jpeg_encoder_private_t*)encoder->jpeg_private;
    struct jpeg_compress_struct* cinfo = &priv->cinfo;
    JSAMPROW row_ptr[1];

    const int width = right - x + 1;
    const int height = bottom - y + 1;
    const int src_row_size = line_width * 4;

    // Calculate output buffer size limit
    const int max_jpeg_size = limit;

    // Setup JPEG error handling
    cinfo->err = jpeg_std_error(&priv->jerr.pub);
    priv->jerr.pub.error_exit = jpeg_error_exit;

    if (setjmp(priv->jerr.setjmp_buffer)) {
        jpeg_abort_compress(cinfo);
        LOGE("JPEG encoding failed\n");
        return 0;
    }

    // Reinitialize JPEG compression object for each frame
    jpeg_suppress_tables(cinfo, TRUE);
    jpeg_abort_compress(cinfo);

    // Setup destination manager
    unsigned char* out_buffer = output;
    unsigned long out_size = max_jpeg_size;

    jpeg_mem_dest(cinfo, &out_buffer, &out_size);

    // Set image parameters
    cinfo->image_width = width;
    cinfo->image_height = height;
    cinfo->input_components = 3;
    cinfo->in_color_space = JCS_RGB;

    // Set compression parameters
    jpeg_set_defaults(cinfo);
    jpeg_set_quality(cinfo, encoder->quality, TRUE);
    cinfo->optimize_coding = TRUE;
    cinfo->dct_method = JDCT_FASTEST;

    // Start compression
    jpeg_start_compress(cinfo, TRUE);

    // Resize row buffer if needed
    if (width * 3 > priv->buffer_size) {
        free(priv->row_buffer);
        priv->buffer_size = width * 3;
        priv->row_buffer = (uint8_t*)malloc(priv->buffer_size);
        if (priv->row_buffer == NULL) {
            LOGE("Failed to allocate JPEG row buffer\n");
            jpeg_abort_compress(cinfo);
            return 0;
        }
    }

    // Compress row by row
    const uint8_t* src_ptr = input + y * src_row_size + x * 4;

    while (cinfo->next_scanline < cinfo->image_height) {
        // Convert RGBX to RGB (skip alpha channel)
        for (int col = 0; col < width; col++) {
            priv->row_buffer[col * 3 + 0] = src_ptr[col * 4 + 2]; // B
            priv->row_buffer[col * 3 + 1] = src_ptr[col * 4 + 1]; // G
            priv->row_buffer[col * 3 + 2] = src_ptr[col * 4 + 0]; // R
        }

        row_ptr[0] = priv->row_buffer;
        jpeg_write_scanlines(cinfo, row_ptr, 1);

        src_ptr += src_row_size;
    }

    // Finish compression
    jpeg_finish_compress(cinfo);

    const int jpeg_size = (int)out_size;

    LOGD("JPEG encode: w=%d h=%d quality=%d size=%d bytes\n",
          width, height, encoder->quality, jpeg_size);

    return jpeg_size;
}

static int encode_header_jpeg_func(image_encoder_t* encoder,
                                  uint8_t* output,
                                  int x, int y, int right, int bottom,
                                  int total_bytes)
{
    UNREFERENCED_PARAMETER(encoder);
    UNREFERENCED_PARAMETER(x);
    UNREFERENCED_PARAMETER(y);
    UNREFERENCED_PARAMETER(right);
    UNREFERENCED_PARAMETER(bottom);

    return image_setup_frame_header(output, IMAGE_TYPE_JPG, total_bytes, 0);
}

static void destroy_jpeg_func(image_encoder_t* encoder)
{
    if (encoder->jpeg_private != NULL) {
        jpeg_encoder_private_t* priv = (jpeg_encoder_private_t*)encoder->jpeg_private;

        // Cleanup JPEG compression object
        jpeg_destroy_compress(&priv->cinfo);

        // Free row buffer
        if (priv->row_buffer != NULL) {
            free(priv->row_buffer);
        }

        // Free private structure
        free(priv);
    }

    free(encoder);
}

image_encoder_t* image_encoder_create_jpeg(int quality)
{
    image_encoder_t* encoder = (image_encoder_t*)malloc(sizeof(image_encoder_t));
    if (encoder == NULL) {
        LOGE("Failed to allocate memory for JPEG encoder\n");
        return NULL;
    }

    // Allocate private structure for JPEG resources
    jpeg_encoder_private_t* priv = (jpeg_encoder_private_t*)malloc(sizeof(jpeg_encoder_private_t));
    if (priv == NULL) {
        LOGE("Failed to allocate JPEG private structure\n");
        free(encoder);
        return NULL;
    }

    // Initialize row buffer with default size (assume max 1920 width)
    priv->buffer_size = 1920 * 3;
    priv->row_buffer = (uint8_t*)malloc(priv->buffer_size);
    if (priv->row_buffer == NULL) {
        LOGE("Failed to allocate JPEG row buffer\n");
        free(priv);
        free(encoder);
        return NULL;
    }

    // Setup error handling
    priv->cinfo.err = jpeg_std_error(&priv->jerr.pub);
    priv->jerr.pub.error_exit = jpeg_error_exit;

    // Initialize JPEG compression object
    jpeg_create_compress(&priv->cinfo);

    // Initialize encoder
    encoder->type = IMAGE_TYPE_JPG;
    encoder->quality = quality;
    encoder->encode = encode_jpeg_func;
    encoder->encode_header = encode_header_jpeg_func;
    encoder->destroy = destroy_jpeg_func;
    encoder->jpeg_private = priv;

    LOGD("Created JPEG encoder with quality=%d\n", quality);
    return encoder;
}

// ============================================================================
// Common Functions
// ============================================================================

void image_encoder_destroy(image_encoder_t* encoder)
{
    if (encoder != NULL && encoder->destroy != NULL) {
        encoder->destroy(encoder);
    }
}

int image_setup_frame_header(uint8_t* msg, uint32_t img_type, uint32_t img_len, uint32_t img_cnt)
{
    image_frame_header_t* header = (image_frame_header_t*)msg;

    header->magic_id = cpu_to_le32(FRAME_MAGIC_ID);
    header->img_type = cpu_to_le32(img_type);
    header->img_len = cpu_to_le32(img_len);
    header->img_cnt = cpu_to_le32(img_cnt);

    return 0;
}

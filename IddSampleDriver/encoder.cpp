#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <memory>

#include "jpeglib.h"
#include <setjmp.h>

#include "encoder.h"
#include "tools.h"



// ============================================================================
// RGB565 Encoder Implementation
// ============================================================================

int ImageEncoder::encode_rgb565(uint8_t* output, const uint8_t* input,int buffer_size, int x, int y, int width, int height)
{
    UNREFERENCED_PARAMETER(buffer_size);
    UNREFERENCED_PARAMETER(x);
    UNREFERENCED_PARAMETER(y);

    int pos = 0;
    const uint32_t* framebuffer = (const uint32_t*)input;

    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            uint32_t pixel = *framebuffer++;

            uint8_t r = (pixel >> 16) & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t b = pixel & 0xFF;

            uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);

            output[pos++] = rgb565 & 0xFF;
            output[pos++] = (rgb565 >> 8) & 0xFF;
        }
    }
    return pos;
}

// ============================================================================
// RGB888 Encoder Implementation
// ============================================================================

int ImageEncoder::encode_rgb888(uint8_t* output, const uint8_t* input,int buffer_size, int x, int y, int width, int height)
{
    UNREFERENCED_PARAMETER(buffer_size);
    UNREFERENCED_PARAMETER(x);
    UNREFERENCED_PARAMETER(y);

    const int total_pixels = width * height;
    memcpy(output, input, total_pixels * 4);
    return total_pixels * 4;
}

// ============================================================================
// JPEG Error Handling
// ============================================================================

static void jpeg_error_exit(j_common_ptr cinfo)
{
    jpeg_error_mgr_with_exit_t* myerr = (jpeg_error_mgr_with_exit_t*)cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(myerr->setjmp_buffer, 1);
}

void ImageEncoder::create_jpeg_encoder()
{
    // Allocate private structure for JPEG resources
    m_jpeg_private = new struct jpeg_encoder_private_t;
    if (m_jpeg_private == nullptr) {
        LOGE("Failed to allocate JPEG private structure\n");
        return;
    }

    // Initialize row buffer with default size (assume max 1920 width)
    m_jpeg_private->buffer_size = 1920 * 3;
    m_jpeg_private->row_buffer = (uint8_t*)malloc(m_jpeg_private->buffer_size);
    if (m_jpeg_private->row_buffer == nullptr) {
        LOGE("Failed to allocate JPEG row buffer\n");
        delete m_jpeg_private;
        m_jpeg_private = nullptr;
        return;
    }

    // Setup error handling
    m_jpeg_private->cinfo.err = jpeg_std_error(&m_jpeg_private->jerr.pub);
    m_jpeg_private->jerr.pub.error_exit = &jpeg_error_exit;

    // Initialize JPEG compression object
    jpeg_create_compress(&m_jpeg_private->cinfo);

    LOGD("Created JPEG encoder\n");
}

void ImageEncoder::destroy_jpeg_encoder()
{
    if (m_jpeg_private != nullptr) {
        jpeg_encoder_private_t* priv = m_jpeg_private;

        // Cleanup JPEG compression object
        jpeg_destroy_compress(&priv->cinfo);

        // Free row buffer
        if (priv->row_buffer != nullptr) {
            free(priv->row_buffer);
        }

        // Free private structure
        delete priv;
        m_jpeg_private = nullptr;
    }
}

int ImageEncoder::encode_jpeg(uint8_t* output, const uint8_t* input,int buffer_size, int x, int y, int width, int height)
{
    UNREFERENCED_PARAMETER(x);
    UNREFERENCED_PARAMETER(y);
    if (m_jpeg_private == nullptr) {
        LOGE("JPEG encoder not initialized\n");
        return 0;
    }

    jpeg_encoder_private_t* priv = m_jpeg_private;
    struct jpeg_compress_struct* cinfo = &priv->cinfo;
    JSAMPROW row_ptr[1];

    // Check if we need to resize row buffer
    const int required_buffer_size = width * 3;
    if (required_buffer_size > priv->buffer_size) {
        free(priv->row_buffer);
        priv->row_buffer = (uint8_t*)malloc(required_buffer_size);
        if (priv->row_buffer == nullptr) {
            LOGE("Failed to resize JPEG row buffer to %d\n", required_buffer_size);
            return 0;
        }
        priv->buffer_size = required_buffer_size;
    }
    jpeg_abort_compress(cinfo);

    // Setup JPEG compression parameters
    cinfo->image_width = width;
    cinfo->image_height = height;
    cinfo->input_components = 3;
    cinfo->in_color_space = JCS_RGB;
    jpeg_set_defaults(cinfo);
    jpeg_set_quality(cinfo, m_quality, TRUE);

    // Set destination
    unsigned long jpeg_size = buffer_size;
    jpeg_mem_dest(cinfo, &output, &jpeg_size);

    // Start compression
    jpeg_start_compress(cinfo, TRUE);

    // Process each row
    const uint32_t* framebuffer = (const uint32_t*)input;

    for (int row = 0; row < height; row++) {
        // Convert RGBA to RGB
        for (int col = 0; col < width; col++) {
            uint32_t pixel = *framebuffer++;
            uint8_t* out_pixel = &priv->row_buffer[col * 3];
            out_pixel[0] = (pixel >> 16) & 0xFF;  // R
            out_pixel[1] = (pixel >> 8) & 0xFF;   // G
            out_pixel[2] = pixel & 0xFF;          // B
        }

        row_ptr[0] = priv->row_buffer;
        jpeg_write_scanlines(cinfo, row_ptr, 1);
    }

    // Finish compression
    jpeg_finish_compress(cinfo);
    return jpeg_size;
}

// ============================================================================
// ImageEncoder Class Implementation
// ============================================================================

ImageEncoder::ImageEncoder(int type, int quality)
{
    m_counter=0;
    m_type =type;
    m_quality=quality;
    m_jpeg_private = nullptr;
    if(m_type == IMAGE_TYPE_JPG){
        create_jpeg_encoder();
    }
        
}

ImageEncoder::~ImageEncoder()
{
    if(m_type == IMAGE_TYPE_JPG){
        destroy_jpeg_encoder();
    }
}

// ============================================================================
// Public Interface
// ============================================================================

int ImageEncoder::encode(uint8_t* output, const uint8_t* input,int buffer_size, int x, int y, int width, int height)
{
    int image_size=0,total_size=0;
    uint8_t* buffer_body = output + sizeof(image_frame_header_t);
    image_frame_header_t* header = (image_frame_header_t*)output;

    if((width > 0) && (height > 0)) {
        if (m_type == IMAGE_TYPE_RGB565) {
            image_size = encode_rgb565(buffer_body, input, buffer_size, x, y, width, height);
        }
        else if (m_type == IMAGE_TYPE_RGB888) {
            image_size = encode_rgb888(buffer_body, input, buffer_size, x, y, width, height);
        }
        else  { //IMAGE_TYPE_JPG
            image_size = encode_jpeg(buffer_body, input, buffer_size, x, y, width, height);
        }
    }
    header->magic_id = cpu_to_le32(FRAME_MAGIC_ID);
    header->img_type = cpu_to_le32(m_type);
    header->img_len = cpu_to_le32(image_size);
    header->img_cnt = cpu_to_le32(m_counter);
    m_counter++;

    total_size = image_size + sizeof(image_frame_header_t);
    total_size = (total_size + 7ul) & (~7ul);

    return total_size;
}

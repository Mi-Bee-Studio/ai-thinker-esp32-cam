/*
 * lm_jpeg.h — JPEG → grayscale analysis-grid decoder (TJpgDec 1/8 DC path)
 */
#ifndef LM_JPEG_H
#define LM_JPEG_H

#include <stdint.h>
#include <stddef.h>
#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Decode `fb` into an 8-bit grayscale grid of (w/8)×(h/8) using TJpgDec
 * scale=3 (DC-only, no IDCT). `grid` must hold ((w+7)/8)×((h+7)/8) bytes.
 * Returns grid width (>0) on success, 0 on decode failure. The achieved
 * grid geometry is returned via gw_out and gh_out.
 */
int lm_jpeg_decode_gray(const camera_fb_t *fb, uint8_t *grid,
                        int *gw_out, int *gh_out);

#ifdef __cplusplus
}
#endif
#endif /* LM_JPEG_H */

/*
 * lm_jpeg.c — JPEG → grayscale analysis-grid decoder (TJpgDec 1/8 DC path)
 *
 * TJpgDec with JD_FORMAT=2 (grayscale) and scale=3 (1/8) skips the IDCT and
 * hands us each block's DC value directly — the cheapest possible pixel
 * domain: ~30-80ms per VGA/SVGA frame on the ESP32 LX6 per s60sc's numbers.
 */
#include <string.h>
#include "tjpgd.h"
#include "lm_jpeg.h"

typedef struct {
    const uint8_t *buf;
    size_t len, pos;
} lm_jpeg_src_t;

static uint8_t s_work[TJPGD_WORKSPACE_SIZE];   /* 3480 B, static */

static uint8_t *s_grid;
static int s_gw;

/* TJpgDec input callback — feed from memory */
static size_t lm_in_cb(JDEC *jd, uint8_t *buff, size_t ndbyte)
{
    lm_jpeg_src_t *src = (lm_jpeg_src_t *)jd->device;
    size_t avail = src->len - src->pos;
    size_t give = ndbyte < avail ? ndbyte : avail;
    if (buff) {
        memcpy(buff, src->buf + src->pos, give);
    }
    src->pos += give;
    return give;   /* 0 = end of stream */
}

/* TJpgDec output callback — scale=3 delivers 1×1 rect per 8×8 block */
static int lm_out_cb(JDEC *jd, void *bitmap, JRECT *rect)
{
    const uint8_t *src = (const uint8_t *)bitmap;
    uint8_t *dst = s_grid + (size_t)rect->top * s_gw + rect->left;
    int w = rect->right - rect->left + 1;
    for (int y = rect->top; y <= rect->bottom; y++) {
        memcpy(dst, src, (size_t)w);
        src += w;
        dst += s_gw;
    }
    return 1;   /* continue */
}

int lm_jpeg_decode_gray(const camera_fb_t *fb, uint8_t *grid,
                        int *gw_out, int *gh_out)
{
    if (!fb || !fb->buf || fb->len == 0 || !grid) return 0;

    lm_jpeg_src_t src = { .buf = fb->buf, .len = fb->len, .pos = 0 };
    JDEC jd;
    JRESULT r = jd_prepare(&jd, lm_in_cb, s_work, sizeof(s_work), &src);
    if (r != JDR_OK) return 0;

    /* scale=3 → 1/8: grid = ceil(frame/8) */
    int gw = (jd.width + 7) / 8;
    int gh = (jd.height + 7) / 8;
    s_grid = grid;
    s_gw = gw;
    r = jd_decomp(&jd, lm_out_cb, 3);
    if (r != JDR_OK) return 0;

    if (gw_out) *gw_out = gw;
    if (gh_out) *gh_out = gh;
    return gw;
}

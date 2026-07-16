/*
 * spartacus_jpeg.h — minimal baseline (sequential) JPEG encoder.
 *
 * Purpose: the SPARTACUS 360 / 420 LCD accepts only 480x480 BASELINE JPEG. This tiny,
 * dependency-free encoder turns a raw RGB/RGBA pixel buffer into a baseline JPEG so
 * the C and C++ libraries can offer show_image()/clear() without pulling in libjpeg.
 *
 * It is a compact, public-domain-style baseline encoder in the lineage of Jon Olick's
 * public-domain JPEG writer (2014) and stb_image_write's jpg path. It differs in that
 * the Huffman code tables are BUILT AT RUNTIME from the JPEG (ITU-T T.81) Annex K
 * standard tables, and output goes to a caller buffer instead of a FILE*.
 *
 * Output is always: 8-bit, 3-component YCbCr, 4:4:4 (no chroma subsampling),
 * SOF0 (baseline sequential) — exactly what the panel requires. NOT progressive.
 *
 * License: public domain (or MIT, at your option). No warranty.
 *
 * Usage (single translation unit must define the implementation):
 *     #define SPARTACUS_JPEG_IMPLEMENTATION
 *     #include "spartacus_jpeg.h"
 *
 * API:
 *     unsigned char *spartacus_jpeg_encode_alloc(int w, int h, int comp,
 *                                                const unsigned char *pixels,
 *                                                int quality, int *out_size);
 *     unsigned char *spartacus_jpeg_resize_rgb(const unsigned char *src, int sw, int sh,
 *                                              int comp, int dw, int dh);
 *     void           spartacus_jpeg_free(void *p);
 */
#ifndef SPARTACUS_JPEG_H
#define SPARTACUS_JPEG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Encode `pixels` (comp = 3 for RGB, 4 for RGBA; alpha ignored) of size w*h into a
 * baseline JPEG. quality is 1..100 (0 -> 90). Returns a malloc'd buffer (free with
 * spartacus_jpeg_free) and writes its length to *out_size, or NULL on bad arguments
 * or allocation failure. */
unsigned char *spartacus_jpeg_encode_alloc(int w, int h, int comp,
                                           const unsigned char *pixels,
                                           int quality, int *out_size);

/* Bilinear resize an interleaved RGB/RGBA buffer (comp channels) to dw*dh, producing
 * a malloc'd buffer of dw*dh*comp bytes (free with spartacus_jpeg_free). NULL on error. */
unsigned char *spartacus_jpeg_resize_rgb(const unsigned char *src, int sw, int sh,
                                         int comp, int dw, int dh);

/* Free a buffer returned by the functions above. */
void spartacus_jpeg_free(void *p);

#ifdef __cplusplus
}
#endif

#ifdef SPARTACUS_JPEG_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Growable output buffer                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    unsigned char *data;
    int len;
    int cap;
    int error;
} spjpg_buf;

static void spjpg_put(spjpg_buf *b, int byte) {
    if (b->error) return;
    if (b->len + 1 > b->cap) {
        int ncap = b->cap ? b->cap * 2 : 65536;
        unsigned char *nd = (unsigned char *)realloc(b->data, (size_t)ncap);
        if (!nd) { b->error = 1; return; }
        b->data = nd;
        b->cap = ncap;
    }
    b->data[b->len++] = (unsigned char)(byte & 0xFF);
}

static void spjpg_putn(spjpg_buf *b, const unsigned char *p, int n) {
    int i;
    for (i = 0; i < n; ++i) spjpg_put(b, p[i]);
}

/* ------------------------------------------------------------------ */
/* Bit writer (entropy-coded segment) with 0xFF byte stuffing         */
/* ------------------------------------------------------------------ */
typedef struct {
    spjpg_buf *out;
    int bitBuf;
    int bitCnt;
} spjpg_bw;

static void spjpg_writeBits(spjpg_bw *w, const unsigned short *bs) {
    /* bs[0] = code value, bs[1] = number of bits */
    w->bitCnt += bs[1];
    w->bitBuf |= bs[0] << (24 - w->bitCnt);
    while (w->bitCnt >= 8) {
        unsigned char c = (unsigned char)((w->bitBuf >> 16) & 255);
        spjpg_put(w->out, c);
        if (c == 255) spjpg_put(w->out, 0);
        w->bitBuf <<= 8;
        w->bitCnt -= 8;
    }
}

/* ------------------------------------------------------------------ */
/* Forward DCT (AAN), operating in place on 8 values                  */
/* ------------------------------------------------------------------ */
static void spjpg_DCT(float *d0, float *d1, float *d2, float *d3,
                      float *d4, float *d5, float *d6, float *d7) {
    float tmp0 = *d0 + *d7, tmp7 = *d0 - *d7;
    float tmp1 = *d1 + *d6, tmp6 = *d1 - *d6;
    float tmp2 = *d2 + *d5, tmp5 = *d2 - *d5;
    float tmp3 = *d3 + *d4, tmp4 = *d3 - *d4;

    float tmp10 = tmp0 + tmp3, tmp13 = tmp0 - tmp3;
    float tmp11 = tmp1 + tmp2, tmp12 = tmp1 - tmp2;

    float z1, z2, z3, z4, z5, z11, z13;

    *d0 = tmp10 + tmp11;
    *d4 = tmp10 - tmp11;

    z1 = (tmp12 + tmp13) * 0.707106781f;
    *d2 = tmp13 + z1;
    *d6 = tmp13 - z1;

    tmp10 = tmp4 + tmp5;
    tmp11 = tmp5 + tmp6;
    tmp12 = tmp6 + tmp7;

    z5 = (tmp10 - tmp12) * 0.382683433f;
    z2 = tmp10 * 0.541196100f + z5;
    z4 = tmp12 * 1.306562965f + z5;
    z3 = tmp11 * 0.707106781f;

    z11 = tmp7 + z3;
    z13 = tmp7 - z3;

    *d5 = z13 + z2;
    *d3 = z13 - z2;
    *d1 = z11 + z4;
    *d7 = z11 - z4;
}

static const unsigned char spjpg_zigzag[64] = {
    0,1,5,6,14,15,27,28,2,4,7,13,16,26,29,42,3,8,12,17,25,30,41,43,9,11,18,24,31,40,44,53,
    10,19,23,32,39,45,52,54,20,22,33,38,46,51,55,60,21,34,37,47,50,56,59,61,35,36,48,49,57,58,62,63
};

static void spjpg_calcBits(int val, unsigned short bits[2]) {
    int tmp = val < 0 ? -val : val;
    val = val < 0 ? val - 1 : val;
    bits[1] = 1;
    while (tmp >>= 1) ++bits[1];
    bits[0] = (unsigned short)(val & ((1 << bits[1]) - 1));
}

/* Process one 8x8 data unit; returns the new DC predictor. */
static int spjpg_processDU(spjpg_bw *bw, float *CDU, const float *fdtbl, int DC,
                           const unsigned short HTDC[256][2],
                           const unsigned short HTAC[256][2]) {
    const unsigned short EOB[2]      = { HTAC[0x00][0], HTAC[0x00][1] };
    const unsigned short M16zeroes[2]= { HTAC[0xF0][0], HTAC[0xF0][1] };
    int DU[64];
    int i, diff, end0pos;

    for (i = 0; i < 64; i += 8)
        spjpg_DCT(&CDU[i], &CDU[i+1], &CDU[i+2], &CDU[i+3],
                  &CDU[i+4], &CDU[i+5], &CDU[i+6], &CDU[i+7]);
    for (i = 0; i < 8; ++i)
        spjpg_DCT(&CDU[i], &CDU[i+8], &CDU[i+16], &CDU[i+24],
                  &CDU[i+32], &CDU[i+40], &CDU[i+48], &CDU[i+56]);

    for (i = 0; i < 64; ++i) {
        float v = CDU[i] * fdtbl[i];
        DU[spjpg_zigzag[i]] = (int)(v < 0 ? v - 0.5f : v + 0.5f);
    }

    diff = DU[0] - DC;
    if (diff == 0) {
        spjpg_writeBits(bw, HTDC[0]);
    } else {
        unsigned short bits[2];
        spjpg_calcBits(diff, bits);
        spjpg_writeBits(bw, HTDC[bits[1]]);
        spjpg_writeBits(bw, bits);
    }

    end0pos = 63;
    for (; end0pos > 0 && DU[end0pos] == 0; --end0pos) {}
    if (end0pos == 0) {
        spjpg_writeBits(bw, EOB);
        return DU[0];
    }
    for (i = 1; i <= end0pos; ++i) {
        int startpos = i;
        int nrzeroes;
        unsigned short bits[2];
        for (; DU[i] == 0 && i <= end0pos; ++i) {}
        nrzeroes = i - startpos;
        if (nrzeroes >= 16) {
            int lng = nrzeroes >> 4, nrmarker;
            for (nrmarker = 1; nrmarker <= lng; ++nrmarker)
                spjpg_writeBits(bw, M16zeroes);
            nrzeroes &= 15;
        }
        spjpg_calcBits(DU[i], bits);
        spjpg_writeBits(bw, HTAC[(nrzeroes << 4) + bits[1]]);
        spjpg_writeBits(bw, bits);
    }
    if (end0pos != 63)
        spjpg_writeBits(bw, EOB);
    return DU[0];
}

/* Build a Huffman lookup HT[value] = {code, length} from Annex-K bit-length counts. */
static void spjpg_buildHuff(const unsigned char *nrcodes, const unsigned char *values,
                            unsigned short HT[256][2]) {
    int code = 0, k = 0, len, i;
    for (len = 1; len <= 16; ++len) {
        for (i = 0; i < nrcodes[len]; ++i) {
            HT[values[k]][0] = (unsigned short)code;
            HT[values[k]][1] = (unsigned short)len;
            ++code;
            ++k;
        }
        code <<= 1;
    }
}

unsigned char *spartacus_jpeg_encode_alloc(int width, int height, int comp,
                                           const unsigned char *data,
                                           int quality, int *out_size) {
    /* Standard JPEG (ITU-T T.81) Annex K tables. */
    static const int YQT[] = {
        16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,14,13,16,24,40,57,69,56,
        14,17,22,29,51,87,80,62,18,22,37,56,68,109,103,77,24,35,55,64,81,104,113,92,
        49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99 };
    static const int UVQT[] = {
        17,18,24,47,99,99,99,99,18,21,26,66,99,99,99,99,24,26,56,99,99,99,99,99,
        47,66,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,
        99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99 };
    static const float aasf[] = {
        1.0f*2.828427125f, 1.387039845f*2.828427125f, 1.306562965f*2.828427125f,
        1.175875602f*2.828427125f, 1.0f*2.828427125f, 0.785694958f*2.828427125f,
        0.541196100f*2.828427125f, 0.275899379f*2.828427125f };

    static const unsigned char std_dc_luminance_nrcodes[]   = {0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
    static const unsigned char std_dc_luminance_values[]    = {0,1,2,3,4,5,6,7,8,9,10,11};
    static const unsigned char std_ac_luminance_nrcodes[]   = {0,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
    static const unsigned char std_ac_luminance_values[]    = {
        0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,
        0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
        0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
        0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
        0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
        0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
        0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,
        0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
        0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
        0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
        0xf9,0xfa };
    static const unsigned char std_dc_chrominance_nrcodes[] = {0,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
    static const unsigned char std_dc_chrominance_values[]  = {0,1,2,3,4,5,6,7,8,9,10,11};
    static const unsigned char std_ac_chrominance_nrcodes[] = {0,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
    static const unsigned char std_ac_chrominance_values[]  = {
        0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,
        0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
        0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
        0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
        0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,
        0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
        0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,
        0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
        0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
        0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
        0xf9,0xfa };

    unsigned short YDC_HT[256][2], UVDC_HT[256][2], YAC_HT[256][2], UVAC_HT[256][2];
    unsigned char YTable[64], UVTable[64];
    float fdtbl_Y[64], fdtbl_UV[64];
    spjpg_buf buf;
    spjpg_bw bw;
    int i, row, col, k, x, y;
    int DCY = 0, DCU = 0, DCV = 0;
    static const unsigned short fillBits[] = {0x7F, 7};

    if (!data || width <= 0 || height <= 0 || (comp != 3 && comp != 4)) {
        if (out_size) *out_size = 0;
        return NULL;
    }

    if (quality <= 0) quality = 90;
    if (quality > 100) quality = 100;
    quality = quality < 50 ? 5000 / quality : 200 - quality * 2;

    for (i = 0; i < 64; ++i) {
        int yti  = (YQT[i]  * quality + 50) / 100;
        int uvti = (UVQT[i] * quality + 50) / 100;
        YTable[spjpg_zigzag[i]]  = (unsigned char)(yti  < 1 ? 1 : yti  > 255 ? 255 : yti);
        UVTable[spjpg_zigzag[i]] = (unsigned char)(uvti < 1 ? 1 : uvti > 255 ? 255 : uvti);
    }
    for (row = 0, k = 0; row < 8; ++row) {
        for (col = 0; col < 8; ++col, ++k) {
            fdtbl_Y[k]  = 1.0f / (YTable[spjpg_zigzag[k]]  * aasf[row] * aasf[col]);
            fdtbl_UV[k] = 1.0f / (UVTable[spjpg_zigzag[k]] * aasf[row] * aasf[col]);
        }
    }

    spjpg_buildHuff(std_dc_luminance_nrcodes,   std_dc_luminance_values,   YDC_HT);
    spjpg_buildHuff(std_ac_luminance_nrcodes,   std_ac_luminance_values,   YAC_HT);
    spjpg_buildHuff(std_dc_chrominance_nrcodes, std_dc_chrominance_values, UVDC_HT);
    spjpg_buildHuff(std_ac_chrominance_nrcodes, std_ac_chrominance_values, UVAC_HT);

    buf.data = NULL; buf.len = 0; buf.cap = 0; buf.error = 0;
    bw.out = &buf; bw.bitBuf = 0; bw.bitCnt = 0;

    /* ---- Headers ---- */
    {
        static const unsigned char head0[] = {
            0xFF,0xD8,0xFF,0xE0,0,0x10,'J','F','I','F',0,1,1,0,0,1,0,1,0,0,
            0xFF,0xDB,0,0x84,0 };
        spjpg_putn(&buf, head0, (int)sizeof(head0));
        spjpg_putn(&buf, YTable, 64);
        spjpg_put(&buf, 1);
        spjpg_putn(&buf, UVTable, 64);
        {
            unsigned char sof[] = {
                0xFF,0xC0,0,0x11,8,
                (unsigned char)(height >> 8), (unsigned char)(height & 0xFF),
                (unsigned char)(width  >> 8), (unsigned char)(width  & 0xFF),
                3,1,0x11,0,2,0x11,1,3,0x11,1,
                0xFF,0xC4,0x01,0xA2 };
            spjpg_putn(&buf, sof, (int)sizeof(sof));
        }
        /* DHT contents: class|id, 16 counts, then values (0-based skip of leading 0). */
        spjpg_put(&buf, 0x00);
        spjpg_putn(&buf, std_dc_luminance_nrcodes + 1, 16);
        spjpg_putn(&buf, std_dc_luminance_values, (int)sizeof(std_dc_luminance_values));
        spjpg_put(&buf, 0x10);
        spjpg_putn(&buf, std_ac_luminance_nrcodes + 1, 16);
        spjpg_putn(&buf, std_ac_luminance_values, (int)sizeof(std_ac_luminance_values));
        spjpg_put(&buf, 0x01);
        spjpg_putn(&buf, std_dc_chrominance_nrcodes + 1, 16);
        spjpg_putn(&buf, std_dc_chrominance_values, (int)sizeof(std_dc_chrominance_values));
        spjpg_put(&buf, 0x11);
        spjpg_putn(&buf, std_ac_chrominance_nrcodes + 1, 16);
        spjpg_putn(&buf, std_ac_chrominance_values, (int)sizeof(std_ac_chrominance_values));
        {
            static const unsigned char sos[] = {
                0xFF,0xDA,0,0x0C,3,1,0,2,0x11,3,0x11,0,0x3F,0 };
            spjpg_putn(&buf, sos, (int)sizeof(sos));
        }
    }

    /* ---- Entropy-coded scan (4:4:4) ---- */
    for (y = 0; y < height; y += 8) {
        for (x = 0; x < width; x += 8) {
            float YDU[64], UDU[64], VDU[64];
            int pos = 0, r, c;
            for (r = 0; r < 8; ++r) {
                int sr = (y + r) < height ? (y + r) : height - 1;
                for (c = 0; c < 8; ++c, ++pos) {
                    int sc = (x + c) < width ? (x + c) : width - 1;
                    const unsigned char *p = data + (sr * width + sc) * comp;
                    float R = p[0], G = p[1], B = p[2];
                    YDU[pos] = +0.29900f*R + 0.58700f*G + 0.11400f*B - 128.0f;
                    UDU[pos] = -0.16874f*R - 0.33126f*G + 0.50000f*B;
                    VDU[pos] = +0.50000f*R - 0.41869f*G - 0.08131f*B;
                }
            }
            DCY = spjpg_processDU(&bw, YDU, fdtbl_Y,  DCY, YDC_HT,  YAC_HT);
            DCU = spjpg_processDU(&bw, UDU, fdtbl_UV, DCU, UVDC_HT, UVAC_HT);
            DCV = spjpg_processDU(&bw, VDU, fdtbl_UV, DCV, UVDC_HT, UVAC_HT);
        }
    }

    /* Flush remaining bits with 1-padding, then EOI. */
    spjpg_writeBits(&bw, fillBits);
    spjpg_put(&buf, 0xFF);
    spjpg_put(&buf, 0xD9);

    if (buf.error) {
        free(buf.data);
        if (out_size) *out_size = 0;
        return NULL;
    }
    if (out_size) *out_size = buf.len;
    return buf.data;
}

unsigned char *spartacus_jpeg_resize_rgb(const unsigned char *src, int sw, int sh,
                                         int comp, int dw, int dh) {
    unsigned char *dst;
    int dx, dy, ch;
    if (!src || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 || (comp != 3 && comp != 4))
        return NULL;
    dst = (unsigned char *)malloc((size_t)dw * dh * comp);
    if (!dst) return NULL;
    for (dy = 0; dy < dh; ++dy) {
        float fy = (dh == 1) ? 0.0f : ((float)dy * (sh - 1)) / (dh - 1);
        int y0 = (int)fy, y1 = y0 + 1 < sh ? y0 + 1 : y0;
        float wy = fy - y0;
        for (dx = 0; dx < dw; ++dx) {
            float fx = (dw == 1) ? 0.0f : ((float)dx * (sw - 1)) / (dw - 1);
            int x0 = (int)fx, x1 = x0 + 1 < sw ? x0 + 1 : x0;
            float wx = fx - x0;
            const unsigned char *p00 = src + (y0 * sw + x0) * comp;
            const unsigned char *p01 = src + (y0 * sw + x1) * comp;
            const unsigned char *p10 = src + (y1 * sw + x0) * comp;
            const unsigned char *p11 = src + (y1 * sw + x1) * comp;
            unsigned char *o = dst + (dy * dw + dx) * comp;
            for (ch = 0; ch < comp; ++ch) {
                float top = p00[ch] * (1 - wx) + p01[ch] * wx;
                float bot = p10[ch] * (1 - wx) + p11[ch] * wx;
                float v = top * (1 - wy) + bot * wy;
                o[ch] = (unsigned char)(v + 0.5f);
            }
        }
    }
    return dst;
}

void spartacus_jpeg_free(void *p) { free(p); }

#endif /* SPARTACUS_JPEG_IMPLEMENTATION */
#endif /* SPARTACUS_JPEG_H */

/* Fast 2-pass PVRTC 4bpp encoder for offline asset conversion (host tool).
 * Usage: pvrtc_enc <w> <h> <is_rgba:0|1> <in_rgba.raw> <out_pvrtc.bin>
 * Input must be W*H*4 bytes RGBA8888 (W == H, power of 2).
 * If W < 8 (mip levels 4x4, 2x2, 1x1), tiles into 8x8 (4 blocks = 32 bytes).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t r, g, b, a; } rgba_t;

static uint32_t spread(uint32_t v) {
    v = (v | (v << 4)) & 0x0f0fu;
    v = (v | (v << 2)) & 0x3333u;
    return (v | (v << 1)) & 0x5555u;
}

static uint32_t twiddle_xy(uint32_t bx, uint32_t by) {
    return (spread(bx) << 1) | spread(by);
}

static uint8_t expand5(uint32_t v) { return (uint8_t)((v << 3) | (v >> 2)); }
static uint8_t expand4(uint32_t v) { return (uint8_t)((v << 4) | v); }
static uint8_t expand3(uint32_t v) { return (uint8_t)((v << 5) | (v << 2) | (v >> 1)); }

static void quantize_pair(rgba_t ca, rgba_t cb, int opaque,
                          uint16_t *out_a15, uint16_t *out_b16,
                          rgba_t *dec_a, rgba_t *dec_b) {
    if (opaque) {
        uint32_t ar = (ca.r * 31u + 127u) / 255u;
        uint32_t ag = (ca.g * 31u + 127u) / 255u;
        uint32_t ab = (ca.b * 15u + 127u) / 255u;
        uint32_t br = (cb.r * 31u + 127u) / 255u;
        uint32_t bg = (cb.g * 31u + 127u) / 255u;
        uint32_t bb = (cb.b * 31u + 127u) / 255u;
        *out_a15 = (uint16_t)(0x4000u | (ar << 9) | (ag << 4) | ab);
        *out_b16 = (uint16_t)(0x8000u | (br << 10) | (bg << 5) | bb);
        dec_a->r = expand5(ar); dec_a->g = expand5(ag); dec_a->b = expand4(ab); dec_a->a = 255;
        dec_b->r = expand5(br); dec_b->g = expand5(bg); dec_b->b = expand5(bb); dec_b->a = 255;
    } else {
        uint32_t aa = (ca.a * 7u + 127u) / 255u;
        uint32_t ar = (ca.r * 15u + 127u) / 255u;
        uint32_t ag = (ca.g * 15u + 127u) / 255u;
        uint32_t ab = (ca.b * 7u + 127u) / 255u;
        uint32_t ba = (cb.a * 7u + 127u) / 255u;
        uint32_t br = (cb.r * 15u + 127u) / 255u;
        uint32_t bg = (cb.g * 15u + 127u) / 255u;
        uint32_t bb = (cb.b * 15u + 127u) / 255u;
        *out_a15 = (uint16_t)((aa << 11) | (ar << 7) | (ag << 3) | ab);
        *out_b16 = (uint16_t)((ba << 12) | (br << 8) | (bg << 4) | bb);
        dec_a->r = expand4(ar); dec_a->g = expand4(ag); dec_a->b = expand3(ab); dec_a->a = expand3(aa);
        dec_b->r = expand4(br); dec_b->g = expand4(bg); dec_b->b = expand4(bb); dec_b->a = expand3(ba);
    }
}

static rgba_t bilerp(const rgba_t *grid, int bw, int bh, int x, int y) {
    /* Block center is at (bx*4 + 2, by*4 + 2). */
    int gx = x - 2;
    int gy = y - 2;
    int bx0 = (gx >= 0) ? (gx >> 2) : ((gx - 3) >> 2);
    int by0 = (gy >= 0) ? (gy >> 2) : ((gy - 3) >> 2);
    int fx = x - (bx0 * 4 + 2); /* 0..3 */
    int fy = y - (by0 * 4 + 2); /* 0..3 */
    int x0 = ((bx0 % bw) + bw) % bw;
    int x1 = (x0 + 1) % bw;
    int y0 = ((by0 % bh) + bh) % bh;
    int y1 = (y0 + 1) % bh;

    rgba_t c00 = grid[y0 * bw + x0];
    rgba_t c10 = grid[y0 * bw + x1];
    rgba_t c01 = grid[y1 * bw + x0];
    rgba_t c11 = grid[y1 * bw + x1];

    int w00 = (4 - fx) * (4 - fy);
    int w10 = fx * (4 - fy);
    int w01 = (4 - fx) * fy;
    int w11 = fx * fy;

    rgba_t out;
    out.r = (uint8_t)((c00.r * w00 + c10.r * w10 + c01.r * w01 + c11.r * w11 + 8) >> 4);
    out.g = (uint8_t)((c00.g * w00 + c10.g * w10 + c01.g * w01 + c11.g * w11 + 8) >> 4);
    out.b = (uint8_t)((c00.b * w00 + c10.b * w10 + c01.b * w01 + c11.b * w11 + 8) >> 4);
    out.a = (uint8_t)((c00.a * w00 + c10.a * w10 + c01.a * w01 + c11.a * w11 + 8) >> 4);
    return out;
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "usage: %s <w> <h> <is_rgba> <in.raw> <out.bin>\n", argv[0]);
        return 1;
    }
    int in_w = atoi(argv[1]);
    int in_h = atoi(argv[2]);
    int is_rgba = atoi(argv[3]);
    if (in_w < 1 || in_h < 1 || in_w != in_h || (in_w & (in_w - 1))) return 2;

    FILE *fin = fopen(argv[4], "rb");
    if (!fin) return 3;
    uint8_t *raw = (uint8_t *)malloc((size_t)in_w * in_h * 4u);
    if (!raw || fread(raw, 1, (size_t)in_w * in_h * 4u, fin) != (size_t)in_w * in_h * 4u) {
        fclose(fin); free(raw); return 4;
    }
    fclose(fin);

    int w = in_w < 8 ? 8 : in_w;
    int h = in_h < 8 ? 8 : in_h;
    rgba_t *img = (rgba_t *)malloc((size_t)w * h * sizeof(rgba_t));
    for (int y = 0; y < h; ++y) {
        int sy = y % in_h;
        for (int x = 0; x < w; ++x) {
            int sx = x % in_w;
            const uint8_t *p = raw + (sy * in_w + sx) * 4;
            img[y * w + x] = (rgba_t){ p[0], p[1], p[2], is_rgba ? p[3] : 255 };
        }
    }
    free(raw);

    int bw = w / 4, bh = h / 4;
    int nblocks = bw * bh;
    rgba_t *grid_a = (rgba_t *)calloc((size_t)nblocks, sizeof(rgba_t));
    rgba_t *grid_b = (rgba_t *)calloc((size_t)nblocks, sizeof(rgba_t));
    uint16_t *enc_a = (uint16_t *)calloc((size_t)nblocks, sizeof(uint16_t));
    uint16_t *enc_b = (uint16_t *)calloc((size_t)nblocks, sizeof(uint16_t));

    /* Pass 1: 2-means split per 4x4 block along weighted luma+alpha */
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            int bi = by * bw + bx;
            int opaque = 1;
            int sum_l = 0;
            int lums[16];
            rgba_t pix[16];
            for (int dy = 0; dy < 4; ++dy) {
                for (int dx = 0; dx < 4; ++dx) {
                    rgba_t c = img[(by * 4 + dy) * w + (bx * 4 + dx)];
                    pix[dy * 4 + dx] = c;
                    if (is_rgba && c.a < 250) opaque = 0;
                    int l = c.r * 3 + c.g * 6 + c.b + (is_rgba ? c.a * 4 : 0);
                    lums[dy * 4 + dx] = l;
                    sum_l += l;
                }
            }
            int mean_l = sum_l / 16;
            uint32_t ar = 0, ag = 0, ab = 0, aa = 0, ac = 0;
            uint32_t br = 0, bg = 0, bb = 0, ba = 0, bc = 0;
            for (int i = 0; i < 16; ++i) {
                if (lums[i] <= mean_l) {
                    ar += pix[i].r; ag += pix[i].g; ab += pix[i].b; aa += pix[i].a; ++ac;
                } else {
                    br += pix[i].r; bg += pix[i].g; bb += pix[i].b; ba += pix[i].a; ++bc;
                }
            }
            if (ac == 0) { ar = br; ag = bg; ab = bb; aa = ba; ac = bc; }
            if (bc == 0) { br = ar; bg = ag; bb = ab; ba = aa; bc = ac; }
            rgba_t ca = { (uint8_t)(ar / ac), (uint8_t)(ag / ac), (uint8_t)(ab / ac), (uint8_t)(aa / ac) };
            rgba_t cb = { (uint8_t)(br / bc), (uint8_t)(bg / bc), (uint8_t)(bb / bc), (uint8_t)(ba / bc) };
            quantize_pair(ca, cb, opaque, &enc_a[bi], &enc_b[bi], &grid_a[bi], &grid_b[bi]);
        }
    }

    /* Pass 2: Pick best 2-bit modulation per pixel against bilinearly upscaled A & B */
    uint8_t *out = (uint8_t *)calloc((size_t)nblocks, 8);
    static const int weights[4] = { 0, 3, 5, 8 };
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            int bi = by * bw + bx;
            uint32_t word0 = 0;
            for (int dy = 0; dy < 4; ++dy) {
                for (int dx = 0; dx < 4; ++dx) {
                    int x = bx * 4 + dx, y = by * 4 + dy;
                    rgba_t target = img[y * w + x];
                    rgba_t ca = bilerp(grid_a, bw, bh, x, y);
                    rgba_t cb = bilerp(grid_b, bw, bh, x, y);
                    uint32_t best_err = 0xffffffffu;
                    uint32_t best_m = 0;
                    for (uint32_t m = 0; m < 4; ++m) {
                        int wt = weights[m];
                        int mr = (ca.r * (8 - wt) + cb.r * wt + 4) >> 3;
                        int mg = (ca.g * (8 - wt) + cb.g * wt + 4) >> 3;
                        int mb = (ca.b * (8 - wt) + cb.b * wt + 4) >> 3;
                        int ma = (ca.a * (8 - wt) + cb.a * wt + 4) >> 3;
                        int dr = mr - target.r, dg = mg - target.g, db = mb - target.b, da = ma - target.a;
                        uint32_t err = (uint32_t)(dr * dr + dg * dg + db * db + da * da * (is_rgba ? 2 : 0));
                        if (err < best_err) { best_err = err; best_m = m; }
                    }
                    word0 |= best_m << ((dy * 4 + dx) * 2);
                }
            }
            uint32_t word1 = ((uint32_t)enc_b[bi] << 16) | ((uint32_t)enc_a[bi] << 1);
            uint32_t tidx = twiddle_xy((uint32_t)bx, (uint32_t)by);
            uint8_t *dst = out + tidx * 8;
            dst[0] = (uint8_t)(word0);
            dst[1] = (uint8_t)(word0 >> 8);
            dst[2] = (uint8_t)(word0 >> 16);
            dst[3] = (uint8_t)(word0 >> 24);
            dst[4] = (uint8_t)(word1);
            dst[5] = (uint8_t)(word1 >> 8);
            dst[6] = (uint8_t)(word1 >> 16);
            dst[7] = (uint8_t)(word1 >> 24);
        }
    }

    FILE *fout = fopen(argv[5], "wb");
    if (!fout || fwrite(out, 1, (size_t)nblocks * 8u, fout) != (size_t)nblocks * 8u) {
        if (fout) fclose(fout);
        return 5;
    }
    fclose(fout);
    free(img); free(grid_a); free(grid_b); free(enc_a); free(enc_b); free(out);
    return 0;
}

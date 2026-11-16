#include "texture.h"
#include "filesystem.h"
#include "stdlib.h"
#include "string.h"
#include "GLES2/gl2.h"

/* Compact freestanding PNG + DEFLATE (stored / fixed / dynamic Huffman) decoder. */
typedef struct {
    const uint8_t *src;
    uint32_t len, pos, bitbuf;
    int bitcnt;
} bs_t;

static uint32_t bs_bits(bs_t *b, int n) {
    while (b->bitcnt < n) {
        uint8_t byte = (b->pos < b->len) ? b->src[b->pos++] : 0;
        b->bitbuf |= (uint32_t)byte << b->bitcnt;
        b->bitcnt += 8;
    }
    uint32_t v = b->bitbuf & ((1u << n) - 1u);
    b->bitbuf >>= n;
    b->bitcnt -= n;
    return v;
}

static void bs_align(bs_t *b) {
    b->bitbuf = 0;
    b->bitcnt = 0;
}

typedef struct {
    uint16_t counts[16];
    uint16_t symbols[320];
} huff_t;

static int huff_build(huff_t *h, const uint8_t *lens, int nsym) {
    int offs[16];
    for (int i = 0; i < 16; ++i) h->counts[i] = 0;
    for (int i = 0; i < nsym; ++i) if (lens[i] < 16) h->counts[lens[i]]++;
    h->counts[0] = 0;
    int left = 1;
    for (int len = 1; len <= 15; ++len) {
        left <<= 1;
        left -= h->counts[len];
        if (left < 0) return -1;
    }
    offs[1] = 0;
    for (int len = 1; len < 15; ++len) offs[len + 1] = offs[len] + h->counts[len];
    for (int sym = 0; sym < nsym; ++sym) {
        if (lens[sym]) h->symbols[offs[lens[sym]]++] = (uint16_t)sym;
    }
    return 0;
}

static int huff_decode(bs_t *b, const huff_t *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; ++len) {
        code |= (int)bs_bits(b, 1);
        int count = h->counts[len];
        if (code - count < first) return h->symbols[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static int inflate_zlib(const uint8_t *src, uint32_t src_len, uint8_t *dst, uint32_t dst_cap, uint32_t *out_len) {
    if (src_len < 6) return -1;
    if ((src[0] & 0x0f) != 8) return -1;
    bs_t b = { src + 2, src_len - 2, 0, 0, 0 };
    uint32_t outp = 0;
    static const uint16_t lbase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const uint8_t lextra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const uint16_t dbase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const uint8_t dextra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    static const uint8_t clorder[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};

    int bfinal;
    do {
        bfinal = (int)bs_bits(&b, 1);
        int btype = (int)bs_bits(&b, 2);
        if (btype == 0) {
            bs_align(&b);
            if (b.pos + 4 > b.len) return -1;
            uint16_t len = (uint16_t)(b.src[b.pos] | (b.src[b.pos + 1] << 8));
            b.pos += 4;
            if (b.pos + len > b.len || outp + len > dst_cap) return -1;
            memcpy(dst + outp, b.src + b.pos, len);
            b.pos += len;
            outp += len;
        } else if (btype == 1 || btype == 2) {
            huff_t hl, hd;
            uint8_t lens[320];
            if (btype == 1) {
                for (int i = 0; i <= 143; ++i) lens[i] = 8;
                for (int i = 144; i <= 255; ++i) lens[i] = 9;
                for (int i = 256; i <= 279; ++i) lens[i] = 7;
                for (int i = 280; i <= 287; ++i) lens[i] = 8;
                huff_build(&hl, lens, 288);
                for (int i = 0; i < 32; ++i) lens[i] = 5;
                huff_build(&hd, lens, 32);
            } else {
                int hlit = (int)bs_bits(&b, 5) + 257;
                int hdist = (int)bs_bits(&b, 5) + 1;
                int hclen = (int)bs_bits(&b, 4) + 4;
                uint8_t clens[19];
                memset(clens, 0, sizeof clens);
                for (int i = 0; i < hclen; ++i) clens[clorder[i]] = (uint8_t)bs_bits(&b, 3);
                huff_t hc;
                if (huff_build(&hc, clens, 19) < 0) return -1;
                int total = hlit + hdist, idx = 0;
                while (idx < total) {
                    int sym = huff_decode(&b, &hc);
                    if (sym < 0) return -1;
                    if (sym < 16) lens[idx++] = (uint8_t)sym;
                    else {
                        int rep = 0; uint8_t val = 0;
                        if (sym == 16) { if (idx == 0) return -1; val = lens[idx - 1]; rep = 3 + (int)bs_bits(&b, 2); }
                        else if (sym == 17) { rep = 3 + (int)bs_bits(&b, 3); }
                        else { rep = 11 + (int)bs_bits(&b, 7); }
                        while (rep-- > 0 && idx < total) lens[idx++] = val;
                    }
                }
                if (huff_build(&hl, lens, hlit) < 0) return -1;
                if (huff_build(&hd, lens + hlit, hdist) < 0) return -1;
            }
            for (;;) {
                int sym = huff_decode(&b, &hl);
                if (sym < 0) return -1;
                if (sym < 256) {
                    if (outp >= dst_cap) return -1;
                    dst[outp++] = (uint8_t)sym;
                } else if (sym == 256) {
                    break;
                } else {
                    int li = sym - 257;
                    if (li >= 29) return -1;
                    uint32_t len = lbase[li] + bs_bits(&b, lextra[li]);
                    int di = huff_decode(&b, &hd);
                    if (di < 0 || di >= 30) return -1;
                    uint32_t dist = dbase[di] + bs_bits(&b, dextra[di]);
                    if (dist > outp || outp + len > dst_cap) return -1;
                    for (uint32_t k = 0; k < len; ++k) {
                        dst[outp] = dst[outp - dist];
                        ++outp;
                    }
                }
            }
        } else {
            return -1;
        }
    } while (!bfinal);
    if (out_len) *out_len = outp;
    return 0;
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static int try_load_ntx(texture_t *tmpTex) {
    char ntx_path[260];
    size_t plen = strlen(tmpTex->path);
    if (plen < 4 || plen + 1 >= sizeof ntx_path) return 0;
    memcpy(ntx_path, tmpTex->path, plen + 1);
    if (strcmp(ntx_path + plen - 4, ".png") != 0 && strcmp(ntx_path + plen - 4, ".PNG") != 0)
        return 0;
    memcpy(ntx_path + plen - 4, ".ntx", 5);

    filehandle_t *fh = FS_OpenFile(ntx_path, "rb");
    if (!fh) return 0;
    if (!FS_UploadToRAM(fh)) { FS_CloseFile(fh); return 0; }

    const uint8_t *raw = (const uint8_t *)fh->ptrStart;
    uint32_t raw_len = (uint32_t)fh->filesize;
    if (raw_len < 20u) { FS_CloseFile(fh); return 0; }

    uint32_t magic = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24);
    uint32_t gl_fmt = (uint32_t)raw[4] | ((uint32_t)raw[5] << 8) | ((uint32_t)raw[6] << 16) | ((uint32_t)raw[7] << 24);
    uint32_t gl_type = (uint32_t)raw[8] | ((uint32_t)raw[9] << 8) | ((uint32_t)raw[10] << 16) | ((uint32_t)raw[11] << 24);
    uint16_t w = (uint16_t)(raw[12] | (raw[13] << 8));
    uint16_t h = (uint16_t)(raw[14] | (raw[15] << 8));
    uint8_t num_mips = raw[16];
    if (magic != 0x3158544eu || !w || !h || !num_mips || num_mips > 12u) {
        FS_CloseFile(fh); return 0;
    }
    uint32_t tbl_end = 20u + (uint32_t)num_mips * 4u;
    if (raw_len < tbl_end) { FS_CloseFile(fh); return 0; }

    uint32_t total = tbl_end;
    for (uint8_t i = 0; i < num_mips; ++i) {
        const uint8_t *lp = raw + 20u + i * 4u;
        uint32_t mlen = (uint32_t)lp[0] | ((uint32_t)lp[1] << 8) | ((uint32_t)lp[2] << 16) | ((uint32_t)lp[3] << 24);
        if (!mlen || mlen > raw_len - total) { FS_CloseFile(fh); return 0; }
        total += mlen;
    }

    tmpTex->data = (uchar **)calloc(num_mips, sizeof(uchar *));
    tmpTex->dataLength = (int *)calloc(num_mips, sizeof(int));
    if (!tmpTex->data || !tmpTex->dataLength) {
        free(tmpTex->data); free(tmpTex->dataLength);
        tmpTex->data = 0; tmpTex->dataLength = 0;
        FS_CloseFile(fh); return 0;
    }

    uint32_t off = tbl_end;
    for (uint8_t i = 0; i < num_mips; ++i) {
        const uint8_t *lp = raw + 20u + i * 4u;
        uint32_t mlen = (uint32_t)lp[0] | ((uint32_t)lp[1] << 8) | ((uint32_t)lp[2] << 16) | ((uint32_t)lp[3] << 24);
        uint8_t *buf = (uint8_t *)malloc(mlen);
        if (!buf) {
            for (uint8_t k = 0; k < i; ++k) free(tmpTex->data[k]);
            free(tmpTex->data); free(tmpTex->dataLength);
            tmpTex->data = 0; tmpTex->dataLength = 0;
            FS_CloseFile(fh); return 0;
        }
        memcpy(buf, raw + off, mlen);
        tmpTex->data[i] = buf;
        tmpTex->dataLength[i] = (int)mlen;
        off += mlen;
    }
    FS_CloseFile(fh);

    tmpTex->width = w;
    tmpTex->height = h;
    tmpTex->numMipmaps = num_mips;
    if (gl_type == 0) {
        tmpTex->format = gl_fmt;
        tmpTex->bpp = 4;
    } else if (gl_type == GL_UNSIGNED_SHORT_4_4_4_4) {
        tmpTex->format = TEXTURE_GL_RGBA;
        tmpTex->bpp = 16;
    } else if (gl_type == GL_UNSIGNED_SHORT_5_6_5) {
        tmpTex->format = TEXTURE_GL_RGB;
        tmpTex->bpp = 16;
    } else {
        tmpTex->format = (gl_fmt == GL_RGB) ? TEXTURE_GL_RGB : TEXTURE_GL_RGBA;
        tmpTex->bpp = 32;
    }
    return 1;
}

void loadNativePNG(texture_t *tmpTex) {
    tmpTex->format = TEXTURE_TYPE_UNKNOWN;
    if (try_load_ntx(tmpTex)) return;
    filehandle_t *fh = FS_OpenFile(tmpTex->path, "rb");
    if (!fh) return;
    if (!FS_UploadToRAM(fh)) { FS_CloseFile(fh); return; }

    const uint8_t *raw = (const uint8_t *)fh->ptrStart;
    uint32_t raw_len = (uint32_t)fh->filesize;
    if (raw_len < 33 || raw[0] != 0x89 || raw[1] != 'P' || raw[2] != 'N' || raw[3] != 'G') {
        FS_CloseFile(fh); return;
    }

    uint32_t w = 0, h = 0;
    uint8_t bd = 0, ct = 0, interlace = 0;
    uint8_t *idat = (uint8_t *)malloc(raw_len);
    uint32_t idat_len = 0;
    uint8_t plte[256 * 3];
    int has_plte = 0;
    if (!idat) { FS_CloseFile(fh); return; }

    uint32_t pos = 8;
    while (pos + 12 <= raw_len) {
        uint32_t clen = be32(raw + pos);
        const uint8_t *ctype = raw + pos + 4;
        if (pos + 12 + clen > raw_len) break;
        const uint8_t *cdata = raw + pos + 8;
        if (!memcmp(ctype, "IHDR", 4) && clen >= 13) {
            w = be32(cdata); h = be32(cdata + 4); bd = cdata[8]; ct = cdata[9]; interlace = cdata[12];
        } else if (!memcmp(ctype, "PLTE", 4)) {
            uint32_t n = clen > sizeof plte ? sizeof plte : clen;
            memcpy(plte, cdata, n);
            has_plte = 1;
        } else if (!memcmp(ctype, "IDAT", 4)) {
            if (idat_len + clen <= raw_len) {
                memcpy(idat + idat_len, cdata, clen);
                idat_len += clen;
            }
        } else if (!memcmp(ctype, "IEND", 4)) {
            break;
        }
        pos += 12 + clen;
    }
    FS_CloseFile(fh);

    if (!w || !h || bd != 8 || (ct != 0 && ct != 2 && ct != 3 && ct != 4 && ct != 6) || interlace > 1) {
        free(idat); return;
    }
    uint32_t spp = (ct == 0) ? 1 : (ct == 2) ? 3 : (ct == 3) ? 1 : (ct == 4) ? 2 : 4;
    uint32_t stride = w * spp;
    uint32_t dec_cap = (stride + 1u) * h + (interlace ? (h * 4u + 16u) : 0u);
    uint8_t *dec = (uint8_t *)malloc(dec_cap);
    if (!dec) { free(idat); return; }

    uint32_t dec_len = 0;
    int irc = inflate_zlib(idat, idat_len, dec, dec_cap, &dec_len);
    free(idat);
    if (irc < 0) { free(dec); return; }

    /* Target size: clamp to square POT <= GL_MAX_TEXTURE_SIZE */
    GLint max_tex = 64;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
    uint32_t tw = w, th = h;
    if (tw > (uint32_t)max_tex || th > (uint32_t)max_tex || tw != th || (tw & (tw - 1u))) {
        tw = th = (uint32_t)max_tex;
    }
    uint8_t *rgba = (uint8_t *)calloc(tw * th, 4u);
    if (!rgba) { free(dec); return; }

    if (interlace == 0) {
        if (dec_len < (stride + 1u) * h) { free(rgba); free(dec); return; }
        for (uint32_t y = 0; y < h; ++y) {
            uint8_t *row = dec + y * (stride + 1u);
            uint8_t ftype = row[0];
            uint8_t *cur = row + 1;
            const uint8_t *prev = (y > 0) ? (dec + (y - 1u) * (stride + 1u) + 1u) : 0;
            for (uint32_t x = 0; x < stride; ++x) {
                uint8_t a = (x >= spp) ? cur[x - spp] : 0;
                uint8_t b = prev ? prev[x] : 0;
                uint8_t c = (prev && x >= spp) ? prev[x - spp] : 0;
                if (ftype == 1) cur[x] = (uint8_t)(cur[x] + a);
                else if (ftype == 2) cur[x] = (uint8_t)(cur[x] + b);
                else if (ftype == 3) cur[x] = (uint8_t)(cur[x] + ((a + b) >> 1));
                else if (ftype == 4) cur[x] = (uint8_t)(cur[x] + paeth(a, b, c));
            }
        }
        for (uint32_t dy = 0; dy < th; ++dy) {
            uint32_t sy0 = (dy * h) / th;
            uint32_t sy1 = ((dy + 1u) * h) / th;
            if (sy1 <= sy0) sy1 = sy0 + 1u;
            if (sy1 > h) sy1 = h;
            for (uint32_t dx = 0; dx < tw; ++dx) {
                uint32_t sx0 = (dx * w) / tw;
                uint32_t sx1 = ((dx + 1u) * w) / tw;
                if (sx1 <= sx0) sx1 = sx0 + 1u;
                if (sx1 > w) sx1 = w;
                uint32_t sr = 0, sg = 0, sb = 0, sa = 0, cnt = 0;
                for (uint32_t sy = sy0; sy < sy1; ++sy) {
                    const uint8_t *srow = dec + sy * (stride + 1u) + 1u;
                    for (uint32_t sx = sx0; sx < sx1; ++sx) {
                        const uint8_t *sp = srow + sx * spp;
                        if (ct == 6) { sr += sp[0]; sg += sp[1]; sb += sp[2]; sa += sp[3]; }
                        else if (ct == 2) { sr += sp[0]; sg += sp[1]; sb += sp[2]; sa += 255u; }
                        else if (ct == 0) { sr += sp[0]; sg += sp[0]; sb += sp[0]; sa += 255u; }
                        else if (ct == 4) { sr += sp[0]; sg += sp[0]; sb += sp[0]; sa += sp[1]; }
                        else if (ct == 3 && has_plte) {
                            uint8_t idx = sp[0];
                            sr += plte[idx * 3u]; sg += plte[idx * 3u + 1u]; sb += plte[idx * 3u + 2u]; sa += 255u;
                        } else { sa += 255u; }
                        ++cnt;
                    }
                }
                uint8_t *dp = rgba + (dy * tw + dx) * 4u;
                dp[0] = (uint8_t)(sr / cnt);
                dp[1] = (uint8_t)(sg / cnt);
                dp[2] = (uint8_t)(sb / cnt);
                dp[3] = (uint8_t)(sa / cnt);
            }
        }
    } else {
        static const uint8_t x0[7] = {0,4,0,2,0,1,0}, dx_s[7] = {8,8,4,4,2,2,1};
        static const uint8_t y0[7] = {0,0,4,0,2,0,1}, dy_s[7] = {8,8,8,4,4,2,2};
        uint32_t off = 0;
        for (int p = 0; p < 7; ++p) {
            if (w <= x0[p] || h <= y0[p]) continue;
            uint32_t pw = (w - x0[p] + dx_s[p] - 1u) / dx_s[p];
            uint32_t ph = (h - y0[p] + dy_s[p] - 1u) / dy_s[p];
            uint32_t pstride = pw * spp;
            if (off + (pstride + 1u) * ph > dec_len) { free(rgba); free(dec); return; }
            for (uint32_t py = 0; py < ph; ++py) {
                uint8_t *row = dec + off + py * (pstride + 1u);
                uint8_t ftype = row[0];
                uint8_t *cur = row + 1;
                const uint8_t *prev = (py > 0) ? (dec + off + (py - 1u) * (pstride + 1u) + 1u) : 0;
                for (uint32_t x = 0; x < pstride; ++x) {
                    uint8_t a = (x >= spp) ? cur[x - spp] : 0;
                    uint8_t b = prev ? prev[x] : 0;
                    uint8_t c = (prev && x >= spp) ? prev[x - spp] : 0;
                    if (ftype == 1) cur[x] = (uint8_t)(cur[x] + a);
                    else if (ftype == 2) cur[x] = (uint8_t)(cur[x] + b);
                    else if (ftype == 3) cur[x] = (uint8_t)(cur[x] + ((a + b) >> 1));
                    else if (ftype == 4) cur[x] = (uint8_t)(cur[x] + paeth(a, b, c));
                }
                uint32_t iy = y0[p] + py * dy_s[p];
                uint32_t d_y0 = (iy * th) / h;
                uint32_t d_y1 = ((iy + 1u) * th) / h;
                if (d_y1 == d_y0) d_y1 = d_y0 + 1u;
                if (d_y1 > th) d_y1 = th;
                for (uint32_t px = 0; px < pw; ++px) {
                    uint32_t ix = x0[p] + px * dx_s[p];
                    uint32_t d_x0 = (ix * tw) / w;
                    uint32_t d_x1 = ((ix + 1u) * tw) / w;
                    if (d_x1 == d_x0) d_x1 = d_x0 + 1u;
                    if (d_x1 > tw) d_x1 = tw;
                    const uint8_t *sp = cur + px * spp;
                    uint8_t r = 0, g = 0, b = 0, a = 255;
                    if (ct == 6) { r = sp[0]; g = sp[1]; b = sp[2]; a = sp[3]; }
                    else if (ct == 2) { r = sp[0]; g = sp[1]; b = sp[2]; }
                    else if (ct == 0) { r = g = b = sp[0]; }
                    else if (ct == 4) { r = g = b = sp[0]; a = sp[1]; }
                    else if (ct == 3 && has_plte) { uint8_t idx = sp[0]; r = plte[idx*3u]; g = plte[idx*3u+1u]; b = plte[idx*3u+2u]; }
                    for (uint32_t dy = d_y0; dy < d_y1; ++dy) {
                        for (uint32_t dx = d_x0; dx < d_x1; ++dx) {
                            uint8_t *dp = rgba + (dy * tw + dx) * 4u;
                            dp[0] = r; dp[1] = g; dp[2] = b; dp[3] = a;
                        }
                    }
                }
            }
            off += (pstride + 1u) * ph;
        }
    }
    free(dec);

    tmpTex->width = (uint)tw;
    tmpTex->height = (uint)th;
    tmpTex->format = TEXTURE_GL_RGBA;
    tmpTex->numMipmaps = 1;
    tmpTex->data = (uchar **)calloc(1, sizeof(uchar *));
    tmpTex->dataLength = (int *)calloc(1, sizeof(int));
    tmpTex->data[0] = rgba;
    tmpTex->dataLength[0] = tw * th * 4u;
}

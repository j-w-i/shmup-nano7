#!/usr/bin/env python3
"""Offline GPU-ready texture converter for SHMUP (PNG -> .ntx).

Produces pre-mipmapped, GPU-ready .ntx files:
  - PVRTC 4bpp RGB  (0x8C00) for opaque 3D world/skybox/models
  - PVRTC 4bpp RGBA (0x8C02) for soft-alpha sprites/explosions
  - Native ARGB4444 (0x1908 / 0x8033) for sharp-alpha bullets and titles
  - Native ARGB8888 (0x1908 / 0x1401) for crisp UI/font atlases
Original PNG assets in third_party/Shmup/data/ remain untouched as source of truth.
"""
import argparse
import os
import pathlib
import struct
import subprocess
import tempfile
from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
DATA_ROOT = HERE.parent.parent / "third_party" / "Shmup" / "data"
BUILD_DIR = HERE / "build"
ENCODER_SRC = HERE / "pvrtc_enc.c"
ENCODER_BIN = BUILD_DIR / "pvrtc_enc"

NTX_MAGIC = 0x3158544E  # 'NTX1'
GL_RGB = 0x1907
GL_RGBA = 0x1908
GL_UNSIGNED_BYTE = 0x1401
GL_UNSIGNED_SHORT_4_4_4_4 = 0x8033
GL_UNSIGNED_SHORT_5_6_5 = 0x8363
GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG = 0x8C00
GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG = 0x8C02

MAX_TEX_SIZE = 256

# Policy rules matched in order against relative path under DATA_ROOT
POLICY = [
    ("data/menu/font.png", "argb8888"),
    ("data/menu/homeAtlas.png", "argb8888"),
    ("data/textures/object/spritesBullets.png", "argb4444"),
    ("data/titles/", "argb4444"),
    ("data/texturesPVR/sprites/explosion.png", "pvrtc4_rgba"),
    ("data/textures/sprites/impact.png", "pvrtc4_rgba"),
    ("data/texturesPVR/sprites/impact.png", "pvrtc4_rgba"),
    ("data/textures/object/SquaredualGhost.png", "pvrtc4_rgba"),
    ("data/texturesPVR/sprites/SquaredualGhost.png", "pvrtc4_rgba"),
    ("data/textures/", "pvrtc4_rgb"),
]


def classify(rel: str) -> str:
    for prefix, fmt in POLICY:
        if rel.startswith(prefix):
            return fmt
    return "pvrtc4_rgb"


def ensure_encoder():
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    if not ENCODER_BIN.exists() or ENCODER_BIN.stat().st_mtime < ENCODER_SRC.stat().st_mtime:
        subprocess.run(
            ["cc", "-O3", "-Wall", "-Wextra", "-Werror", str(ENCODER_SRC), "-o", str(ENCODER_BIN)],
            check=True,
        )


def pack_argb4444(raw_rgba: bytes) -> bytes:
    """Pack RGBA8888 bytes into GL_UNSIGNED_SHORT_4_4_4_4 (RGBA 4-4-4-4 little-endian)."""
    out = bytearray(len(raw_rgba) // 2)
    j = 0
    for i in range(0, len(raw_rgba), 4):
        r = (raw_rgba[i] * 15 + 127) // 255
        g = (raw_rgba[i + 1] * 15 + 127) // 255
        b = (raw_rgba[i + 2] * 15 + 127) // 255
        a = (raw_rgba[i + 3] * 15 + 127) // 255
        v = (r << 12) | (g << 8) | (b << 4) | a
        out[j] = v & 0xFF
        out[j + 1] = (v >> 8) & 0xFF
        j += 2
    return bytes(out)


def pack_rgb565(raw_rgba: bytes) -> bytes:
    out = bytearray(len(raw_rgba) // 2)
    j = 0
    for i in range(0, len(raw_rgba), 4):
        r = (raw_rgba[i] * 31 + 127) // 255
        g = (raw_rgba[i + 1] * 63 + 127) // 255
        b = (raw_rgba[i + 2] * 31 + 127) // 255
        v = (r << 11) | (g << 5) | b
        out[j] = v & 0xFF
        out[j + 1] = (v >> 8) & 0xFF
        j += 2
    return bytes(out)


def encode_pvrtc4_level(raw_rgba: bytes, size: int, is_rgba: bool, tmpdir: pathlib.Path) -> bytes:
    in_path = tmpdir / "in.raw"
    out_path = tmpdir / "out.pvr"
    in_path.write_bytes(raw_rgba)
    subprocess.run(
        [str(ENCODER_BIN), str(size), str(size), "1" if is_rgba else "0", str(in_path), str(out_path)],
        check=True,
    )
    return out_path.read_bytes()


def target_size(w: int, h: int) -> int:
    s = max(w, h)
    # Clamp to MAX_TEX_SIZE and round down to power of 2
    p = 1
    while p * 2 <= min(s, MAX_TEX_SIZE):
        p *= 2
    return max(p, 8)


def convert_one(src_png: pathlib.Path, rel: str, dst_ntx: pathlib.Path, tmpdir: pathlib.Path):
    kind = classify(rel)
    im = Image.open(src_png).convert("RGBA")
    sz = target_size(im.width, im.height)
    if im.width != sz or im.height != sz:
        im = im.resize((sz, sz), Image.Resampling.LANCZOS)

    mips = []
    cur = im
    s = sz
    while s >= 1:
        raw = cur.tobytes()
        if kind == "pvrtc4_rgb":
            mips.append(encode_pvrtc4_level(raw, s, False, tmpdir))
        elif kind == "pvrtc4_rgba":
            mips.append(encode_pvrtc4_level(raw, s, True, tmpdir))
        elif kind == "argb4444":
            mips.append(pack_argb4444(raw))
        elif kind == "rgb565":
            mips.append(pack_rgb565(raw))
        else:  # argb8888
            mips.append(raw)
        if s == 1:
            break
        s //= 2
        cur = cur.resize((s, s), Image.Resampling.BOX)

    if kind == "pvrtc4_rgb":
        gl_fmt, gl_type, has_alpha = GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG, 0, 0
    elif kind == "pvrtc4_rgba":
        gl_fmt, gl_type, has_alpha = GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG, 0, 1
    elif kind == "argb4444":
        gl_fmt, gl_type, has_alpha = GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, 1
    elif kind == "rgb565":
        gl_fmt, gl_type, has_alpha = GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 0
    else:
        gl_fmt, gl_type, has_alpha = GL_RGBA, GL_UNSIGNED_BYTE, 1

    hdr = struct.pack("<IIIHHBBH", NTX_MAGIC, gl_fmt, gl_type, sz, sz, len(mips), has_alpha, 0)
    mip_tbl = b"".join(struct.pack("<I", len(m)) for m in mips)
    dst_ntx.parent.mkdir(parents=True, exist_ok=True)
    dst_ntx.write_bytes(hdr + mip_tbl + b"".join(mips))
    return kind, sz, src_png.stat().st_size, dst_ntx.stat().st_size


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", type=pathlib.Path, default=BUILD_DIR / "ntx")
    ap.add_argument("--only", help="Comma-separated substring filter for selective conversion")
    args = ap.parse_args()

    ensure_encoder()
    filters = [s.strip() for s in args.only.split(",")] if args.only else None

    pngs = sorted(list(DATA_ROOT.rglob("*.png")) + list(DATA_ROOT.rglob("*.PNG")))
    total_png = 0
    total_ntx = 0
    count = 0
    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = pathlib.Path(tmp)
        for p in pngs:
            rel = str(p.relative_to(DATA_ROOT))
            if filters and not any(f in rel for f in filters):
                continue
            dst = args.out_dir / pathlib.Path(rel).with_suffix(".ntx")
            kind, sz, png_b, ntx_b = convert_one(p, rel, dst, tmpdir)
            total_png += png_b
            total_ntx += ntx_b
            count += 1
            print(f"{rel:48s} -> {kind:12s} {sz:3d}x{sz:<3d}  PNG {png_b/1024:7.1f} KiB -> NTX {ntx_b/1024:6.1f} KiB")

    print(f"Converted {count} textures: total PNG {total_png/1024:.1f} KiB -> NTX {total_ntx/1024:.1f} KiB")


if __name__ == "__main__":
    main()

# SHMUP for iPod Nano 7 (NanoOS)

Port of Fabien Sanglard's open-source **SHMUP** (`dEngine`, GPLv3) to the iPod Nano 7 (`S5L8740` / PowerVR MBX Lite) built against the canonical **NanoOS** kernel and application SDK.

## License & Attribution

- Upstream SHMUP engine (`engine/`): Copyright Fabien Sanglard, licensed under the GNU General Public License v3.0 (see `LICENSE`).
- Game assets (`data/` PNGs, models, scenes, shaders) are **not** redistributed in this repository; supply them from upstream SHMUP (`third_party/Shmup/data`).

## Requirements

- **NanoOS** repository (`feat/consolidation` / SDK `v1`, `NGAME_ABI = 1`, services table `>= 19`).
- `arm-none-eabi-gcc` + `python3` + `Pillow` (`PIL`) + host `cc` (for offline PVRTC 4bpp compression).

## Build

```sh
# Interactive build (boots MENU_HOME, touch drag + fire, HOME button exits)
make -C /path/to/NanoOS app APP_DIR=/path/to/shmup-nano7

# Automated 1,500-frame act1.scene benchmark build
make -C /path/to/NanoOS app APP_DIR=/path/to/shmup-nano7 APP_CPPFLAGS="-DSHMUP_BENCH"
```

Output image: `build/game.bin` (`NGM1` executable linked at `0x0b000000`).

## Offline Texture Conversion (`.ntx`)

Convert upstream SHMUP PNGs into GPU-ready `.ntx` textures (PVRTC 4bpp RGB/RGBA for 3D/skybox/sprites, native `ARGB8888` for UI/font, native `ARGB4444` for bullets/titles) while keeping original PNGs as source of truth and runtime fallback:

```sh
python3 tools/mk_textures.py --out-dir /Volumes/iPod/Apps/Data/Shmup
```

## Deploy to iPod Nano 7

1. Copy upstream `data/` into `/Volumes/iPod/Apps/Data/Shmup/data/`.
2. Run `tools/mk_textures.py --out-dir /Volumes/iPod/Apps/Data/Shmup`.
3. Copy `build/game.bin` to `/Volumes/iPod/Apps/Data/Game/game.bin` (or run `make -C /path/to/shmup-nano7 install`).

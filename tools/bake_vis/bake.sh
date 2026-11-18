#!/bin/sh
# Re-bake SHMUP's per-frame visibility sets (camera paths, .cp2b) for a screen shape, using the
# engine's own preprocessor (engine/src/preproc.c) on the host. The original files were baked
# for the iPhone's 320x480; the nano port draws the 3D world on the full 240x432 screen
# (renderer_progr.c SetupCamera), so it needs sets for that taller view.
#   tools/bake_vis/bake.sh DATA_ROOT OUT_DIR [W H]     (DATA_ROOT holds the shipped data/: scenes, map, models, cameraPath)
# Input is the shipped .cp2b (its camera frames are kept; only visibility is recomputed). With
# W H = 320 480 the output matches the shipped files: same frames and key frames, act2 / act3 /
# tutorial averages identical, act1 within a few faces of float rounding. The intro was baked
# with PREPROC_INTRO (back faces kept, 4000-face cap), so it uses a second build.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); E=$HERE/../../engine/src; DATA=$1; OUT=$(cd "$2" && pwd); W=${3:-240}; H=${4:-432}
B=$(mktemp -d); python3 "$HERE/make_engine_bits.py" "$E" "$B/engine_bits.c"
SDKM=$(xcrun --show-sdk-path)/usr/include/math.h   # the engine has its own math.h
for V in "" -DPREPROC_INTRO; do
cc -std=gnu99 -O2 -w $V -include "$SDKM" -include stdlib.h -include string.h -include stdio.h -I"$E" -I"$E/filesystem" \
  "$E/world.c" "$E/entities.c" "$E/md5.c" "$E/lexer.c" "$E/camera.c" "$E/math.c" "$E/matrix.c" "$E/quaternion.c" "$E/log.c" "$E/timer.c" \
  "$HERE/host_fs.c" "$B/engine_bits.c" "$HERE/bake_main.c" -lm -o "$B/bake${V:+_intro}"
done
cd "$DATA"
for pair in intro:intro act1:act1 act2:act2 act3:act3 tutorial_swip:tutorial; do
  sc=${pair%%:*}; cp=${pair##*:}
  bin="$B/bake"; [ $cp = intro ] && bin="$B/bake_intro"
  BAKE_OUT="$OUT" "$bin" data/scenes/$sc.scene data/cameraPath/$cp.cp.cp2b $W $H > "$OUT/$cp.log" 2>&1 &
done
wait; ls -l "$OUT"/*.cp2b

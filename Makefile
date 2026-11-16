NANOOS_DIR ?= /Users/jwalr/nano-gles2/NanoOS
APP_NAME   := shmup
APP_USE_LIBC := 1

APP_CPPFLAGS := \
	-DGL2_BUFFER_BYTES=65536 \
	-DGL2_MAX_BUFFERS=24 \
	-DGL2_MAX_TEXTURES=32 \
	-DGL2_MAX_TEXTURE_SIZE=256 \
	-DGL2_TEXTURE_SLOT_BYTES=393216u \
	-DGL2_MAX_SHADERS=32 \
	-DGL2_MAX_PROGRAMS=16 \
	-DGL2_SHADER_SOURCE_BYTES=8192

APP_CFLAGS := -Iinclude -Iengine/src -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare -Wno-implicit-function-declaration -Wno-incompatible-pointer-types -Wno-int-conversion -Wno-pointer-sign

ENGINE_SRCS := \
	engine/src/dEngine.c engine/src/camera.c engine/src/collisions.c \
	engine/src/commands.c engine/src/config.c engine/src/enemy.c \
	engine/src/enemy_particules.c engine/src/entities.c engine/src/event.c \
	engine/src/fht.c engine/src/fx.c engine/src/io_interface.c \
	engine/src/lee.c engine/src/lexer.c engine/src/lofb.c engine/src/log.c \
	engine/src/material.c engine/src/math.c engine/src/matrix.c \
	engine/src/md5.c engine/src/menu.c engine/src/player.c \
	engine/src/preproc.c engine/src/quaternion.c engine/src/renderer.c \
	engine/src/renderer_progr.c engine/src/shab.c engine/src/stats.c \
	engine/src/text.c engine/src/texture.c engine/src/tha.c \
	engine/src/timer.c engine/src/titles.c engine/src/vis.c \
	engine/src/world.c engine/src/ItextureLoader.c

APP_SRCS := \
	src/shmup_main.c \
	src/shmup_fs.c \
	src/shmup_png.c \
	src/shmup_platform.c \
	$(ENGINE_SRCS)

include $(NANOOS_DIR)/sdk/app.mk

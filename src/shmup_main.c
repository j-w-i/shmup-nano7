#include "nano_app.h"
#include "nano_heap.h"
#include "report.h"
#include "EGL/egl.h"
#include "GLES2/gl2.h"
#include "dEngine.h"
#include "renderer.h"
#include "world.h"
#include "stats.h"
#include "camera.h"
#include "io_interface.h"

void shmup_platform_set_api(const struct nanoos_api *api);
int shmup_fs_mount_status(void);
const char *shmup_get_log(void);

static char report[4096];
static unsigned used;
static void text(const char *s) { while (*s && used + 1 < sizeof report) report[used++] = *s++; }
static void word(uint32_t x) {
    if (used + 10 >= sizeof report) return;
    static const char h[] = "0123456789abcdef";
    for (unsigned i = 0; i < 8; ++i) report[used++] = h[(x >> (28 - i * 4)) & 15u];
    report[used++] = ' ';
}

static void shmup_mark(const struct nanoos_api *api, uint32_t band, uint32_t argb) {
    if (!api || !api->lcd_ready || !api->lcd_back || !api->lcd_present || !api->lcd_service) return;
    uint32_t start = api->usec ? api->usec() : 0;
    for (unsigned i = 0; !api->lcd_ready(); ++i) {
        api->lcd_service();
        if ((api->usec && api->usec() - start >= 200000u) || i >= 1000000u) return;
    }
    uint32_t *fb = api->lcd_back();
    if (!fb) return;
    uint32_t y0 = band * 16u;
    for (uint32_t y = y0; y < y0 + 12u && y < 432u; ++y)
        for (uint32_t x = 0; x < 240u; ++x)
            fb[y * 240u + x] = argb;
    if (api->dcache_clean) api->dcache_clean();
    api->lcd_present();
    start = api->usec ? api->usec() : 0;
    for (unsigned i = 0; !api->lcd_ready(); ++i) {
        api->lcd_service();
        if ((api->usec && api->usec() - start >= 200000u) || i >= 1000000u) break;
    }
}

static const struct nanoos_api *g_trace_api;
static uint32_t g_trace_us[16];
static uint32_t g_trace_active;

void shmup_trace(int step) {
    if (!g_trace_active || step < 0 || step >= 16 || !g_trace_api) return;
    if (g_trace_api->usec) g_trace_us[step] = g_trace_api->usec();
}

static void poll_touch_input(int *home_pressed) {
    nano_input_t in;
    nano_app_poll_input(&in);
    if (home_pressed) *home_pressed = in.home_pressed;
    if (in.pressed) {
        io_event_s ev = { { (float)in.x, (float)in.y }, { (float)in.x, (float)in.y }, IO_EVENT_BEGAN };
        IO_PushEvent(&ev);
    } else if (in.moved) {
        io_event_s ev = { { (float)in.x, (float)in.y }, { (float)in.x, (float)in.y }, IO_EVENT_MOVED };
        IO_PushEvent(&ev);
    } else if (in.released) {
        io_event_s ev = { { (float)in.x, (float)in.y }, { (float)in.x, (float)in.y }, IO_EVENT_ENDED };
        IO_PushEvent(&ev);
    }
}

static void sort_u32(uint32_t *a, uint32_t n) {
    for (uint32_t i = 1; i < n; ++i) {
        uint32_t k = a[i], j = i;
        while (j > 0 && a[j - 1] > k) { a[j] = a[j - 1]; --j; }
        a[j] = k;
    }
}

int nano_example_main(const struct nanoos_api *api) {
    uint32_t frames = 0, software = 0, fault = 0, pixel = 0, px2 = 0, px3 = 0, glerr = 0, nonblack = 0, nonblack60 = 0;
    uint32_t t_free = 0, t_load = 0, t_f0 = 0, t_init = 0, t_intro = 0, t_act1 = 0;
    int stage = 0, rc = -1, rendered = 0;
    uint32_t heap_used_start = 0, heap_used_end = 0, heap_free_end = 0, heap_lg_end = 0, heap_blks_end = 0;
    if (nano_app_init(api) != 0 || NGAME_API_TABLE(api) < 19) return 1;

    shmup_platform_set_api(api);

    stage = 1;
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!dpy || !eglInitialize(dpy, 0, 0)) { rc = -1; goto done; }

    const EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16, EGL_NONE
    };
    EGLConfig cfg; EGLint ncfg = 0;
    if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &ncfg) || ncfg < 1) { rc = -2; goto done; }

    EGLSurface sfc = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)0, 0);
    if (sfc == EGL_NO_SURFACE) { rc = -3; goto done; }

    const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (ctx == EGL_NO_CONTEXT) { rc = -4; goto done; }

    if (!eglMakeCurrent(dpy, sfc, sfc, ctx)) { rc = -5; goto done; }

    stage = 2;
    renderer.glBuffersDimensions[WIDTH] = 240;
    renderer.glBuffersDimensions[HEIGHT] = 432;
    renderer.statsEnabled = 0;
    renderer.materialQuality = 1;

    uint32_t t0 = api->usec ? api->usec() : 0;
    if (!dEngine_Init()) { rc = -6; goto done; }
    stage = 3;
    dEngine_InitDisplaySystem(GL_20_RENDERER);
    {
        extern void SRC_OnResizeScreen(int width, int height);
        SRC_OnResizeScreen(240, 432);
    }
    if (api->usec) t_init = api->usec() - t0;

    stage = 4;
    g_trace_api = api;

#ifdef SHMUP_BENCH
    /* Scripted benchmark: 5 frames intro -> 1500 frames act1 */
    t0 = nano_app_usec();
    for (int f = 0; f < 5; ++f) {
        dEngine_HostFrame();
        if (!eglSwapBuffers(dpy, sfc)) { rc = -7; goto done; }
    }
    t_intro = nano_app_usec() - t0;

    g_trace_active = 1;
    dEngine_RequireSceneId(1);
    enum { ACT1_FRAMES = 1500 };
    static uint32_t frame_us[ACT1_FRAMES];
    t0 = nano_app_usec();
    for (int f = 0; f < ACT1_FRAMES; ++f) {
        int home = 0;
        uint32_t tf = nano_app_usec();
        poll_touch_input(&home);
        if (home) break;
        dEngine_HostFrame();
        if (f == 0) {
            t_f0 = nano_app_usec() - tf;
            g_trace_active = 0;
            nano_heap_stats(&heap_used_start, 0, 0, 0);
        }
        if (f == ACT1_FRAMES - 1) {
            uint8_t px[4] = {0, 0, 0, 0};
            uint32_t nb = 0;
            glReadPixels(120, 180, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            pixel = ((uint32_t)px[3] << 24) | ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | px[2];
            glReadPixels(80, 180, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            px2 = ((uint32_t)px[3] << 24) | ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | px[2];
            glReadPixels(120, 120, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            px3 = ((uint32_t)px[3] << 24) | ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | px[2];
            for (int y = 40; y < 360; y += 20) {
                for (int x = 20; x < 220; x += 20) {
                    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
                    if (px[0] || px[1] || px[2]) ++nb;
                }
            }
            nonblack = nb;
            nonblack60 = nb;
            nano_heap_stats(&heap_used_end, &heap_free_end, &heap_lg_end, &heap_blks_end);
        }
        if (!eglSwapBuffers(dpy, sfc)) { rc = -7; goto done; }
        frame_us[f] = nano_app_usec() - tf;
        rendered = f + 1;
    }
    t_act1 = nano_app_usec() - t0;
#else
    /* Interactive mode: start on MENU_HOME, navigate & play via touch until NGAME_HOME */
    static uint32_t frame_us[1];
    (void)t_intro;
    t0 = nano_app_usec();
    while (1) {
        int home = 0;
        poll_touch_input(&home);
        if (home) break;
        dEngine_HostFrame();
        if (!eglSwapBuffers(dpy, sfc)) { rc = -7; goto done; }
        ++rendered;
    }
    t_act1 = nano_app_usec() - t0;
    nano_heap_stats(&heap_used_end, &heap_free_end, &heap_lg_end, &heap_blks_end);
    nonblack = 1;
    nonblack60 = 1;
#endif
    glerr = (uint32_t)glGetError();
    rc = 0;

done:
    {
        extern void dEngine_FreeSceneRessources(void);
        dEngine_FreeSceneRessources();
    }
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(dpy);
    nano_app_gfx_stats(&frames, &software, &fault);

    text("NANOOS GL2 GAME build " __DATE__ " " __TIME__ "\nSHMUP status/stage/mount/pixel/frames/software/fault ");
    word((uint32_t)rc);
    word((uint32_t)stage);
    word((uint32_t)shmup_fs_mount_status());
    word(pixel);
    word(frames);
    word(software);
    word(fault);
    text("\nSHMUP extra px2/px3/glerr/nonblack/ents/tris ");
    word(px2);
    word(px3);
    word(glerr);
    word(nonblack);
    word((uint32_t)num_map_entities);
    word(num_map_entities ? (uint32_t)map[0].numIndices : 0u);
    {
        extern camera_t camera;
        text("\nCAM_POS ");
        word((uint32_t)(int)(camera.position[0] * 100.0f));
        word((uint32_t)(int)(camera.position[1] * 100.0f));
        word((uint32_t)(int)(camera.position[2] * 100.0f));
        text("FWD ");
        word((uint32_t)(int)(camera.forward[0] * 1000.0f));
        word((uint32_t)(int)(camera.forward[1] * 1000.0f));
        word((uint32_t)(int)(camera.forward[2] * 1000.0f));
        text("NB60 "); word(nonblack60);
        t_free = g_trace_us[9] - g_trace_us[1];
        t_load = g_trace_us[12] - g_trace_us[9];
        text("US "); word(t_init); word(t_intro); word(t_free); word(t_load); word(t_f0); word(t_act1);
#ifdef SHMUP_BENCH
        if (rendered > 1) {
            uint32_t n = (uint32_t)(rendered - 1);
            sort_u32(frame_us + 1, n);
            uint64_t sum = 0;
            for (uint32_t i = 1; i <= n; ++i) sum += frame_us[i];
            uint32_t avg = (uint32_t)(sum / n);
            uint32_t fps10 = avg ? (10000000u / avg) : 0u;
            text("\nFPS_US "); word(frame_us[1]); word(frame_us[1 + n / 2u]); word(frame_us[1 + (n * 95u) / 100u]); word(frame_us[n]); word(avg); word(fps10);
        }
#else
        (void)frame_us;
        (void)sort_u32;
        (void)rendered;
#endif
        text("\nHEAP "); word(heap_used_start); word(heap_used_end); word(heap_free_end); word(heap_lg_end); word(heap_blks_end);
        text("\nTRACE_US ");
        for (int ti = 1; ti <= 12; ++ti) word(g_trace_us[ti] - g_trace_us[1]);
        if (num_map_entities) {
            text("\nENT_T ");
            word((uint32_t)(int)(map[0].matrix[12] * 100.0f));
            word((uint32_t)(int)(map[0].matrix[13] * 100.0f));
            word((uint32_t)(int)(map[0].matrix[14] * 100.0f));
            if (map[0].model) {
                text("VBO ");
                word((uint32_t)map[0].model->memLocation);
                word((uint32_t)map[0].model->vboId);
                word((uint32_t)map[0].model->numVertices);
            }
            if (map[0].material) {
                text("TEX ");
                word(map[0].material->textures[0].textureId);
                word(map[0].material->textures[0].width);
                word(map[0].material->textures[0].height);
            }
        }
        {
            const volatile uint32_t *ta = (const volatile uint32_t *)0x08a00000u;
            uint32_t tnz = 0;
            for (uint32_t i = 0; i < (768u * 1024u) / 4u; ++i) if (ta[i]) ++tnz;
            text("TNZ "); word(tnz);
        }
    }
    text("\nLOG:\n");
    text(shmup_get_log());
    text("\n");
    text((rc == 0 && frames >= 16 && fault == 0) ? "SHMUP PASS\n" : "SHMUP FAIL\n");
    report[used] = 0;
    rpt_mirror_write(report, used, rpt_sum(report, used));
    if (api->dcache_clean) api->dcache_clean();
    return rc;
}

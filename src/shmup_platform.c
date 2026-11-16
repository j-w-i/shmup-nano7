#include "globals.h"
#include "renderer.h"
#include "sys/time.h"
#include "gameabi.h"

static const struct nanoos_api *g_plat_api;
void shmup_platform_set_api(const struct nanoos_api *api) { g_plat_api = api; }

int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    uint32_t us = (g_plat_api && g_plat_api->usec) ? g_plat_api->usec() : 0;
    if (tv) {
        tv->tv_sec = (long)(us / 1000000u);
        tv->tv_usec = (long)(us % 1000000u);
    }
    return 0;
}

void initFixedRenderer(renderer_t *r) { (void)r; }

/* Network stubs */
typedef struct { uint8_t data[64]; } netchannel_stub_t;
netchannel_stub_t net;
int NET_Init(void) { return 0; }
void NET_Setup(void) {}
void NET_Receive(void) {}
void NET_Send(void) {}
void NET_Free(void) {}
int NET_IsRunning(void) { return 0; }
void NET_OnNextLevelLoad(void) {}
void Net_SendDie(void) {}
uint NET_GetDropedPackets(void) { return 0; }

/* Sound / music stubs */
void SND_Init(void) {}
void SND_InitSoundTrack(char *filename, unsigned int startAt) { (void)filename; (void)startAt; }
void SND_StartSoundTrack(void) {}
void SND_StopSoundTrack(void) {}
void SND_PlaySound(int sndId) { (void)sndId; }
void SND_UpdateRecord(void) {}
void SND_FinalizeRecord(void) {}

/* Native service stubs */
int Native_RetrieveListOf(char replayList[10][256]) { (void)replayList; return 0; }
void Native_UploadFileTo(char path[256]) { (void)path; }
void Action_ShowGameCenter(void *tag) { (void)tag; }
void Native_UploadScore(uint score) { (void)score; }
void Native_LoginGameCenter(void) {}

/* Trackmem stubs (when needed) */
void *TM_calloc(size_t n, size_t s) { extern void *calloc(size_t, size_t); return calloc(n, s); }
void TM_free(void *p) { extern void free(void *); free(p); }

#include "profiler.h"
#include <stdio.h>
#include <string.h>

#ifdef __3DS__
#include <3ds.h>
#endif

static Uint64 g_start[PROF_COUNT];
static Uint64 g_accum[PROF_COUNT];
static int    g_frames      = 0;
static Uint64 g_report_time = 0;

static const char* const g_names[PROF_COUNT] = {
    "logic", "render", "clear", "mapcache", "update_anim",
    "forewall", "hp", "countdown", "info", "id", "present"
};

inline Uint64 profNow(void) {
#ifdef __3DS__
    return svcGetSystemTick();
#else
    return SDL_GetPerformanceCounter();
#endif
}

inline double profHz(void) {
#ifdef __3DS__
    return (double)SYSCLOCK_ARM11;   /* 268123480 Hz */
#else
    return (double)SDL_GetPerformanceFrequency();
#endif
}

void profInit(void) {
    memset(g_start, 0, sizeof(g_start));
    memset(g_accum, 0, sizeof(g_accum));
    g_frames = 0;
    g_report_time = profNow();
}

void profBegin(ProfSlot s) {
    g_start[s] = profNow();
}

void profEnd(ProfSlot s) {
    g_accum[s] += profNow() - g_start[s];
}

void profFrameEnd(void) {
    g_frames++;
    Uint64 now = profNow();
    double hz  = profHz();
    if (now - g_report_time < (Uint64)hz) return;   /* once per second */

    double to_ms = 1000.0 / hz / (double)g_frames;
    fprintf(stderr, "[prof] fps=%d ", g_frames);
    for (int i = 0; i < PROF_COUNT; i++) {
        fprintf(stderr, "%s=%.2fms ", g_names[i], g_accum[i] * to_ms);
    }
    fprintf(stderr, "\n");

    memset(g_accum, 0, sizeof(g_accum));
    g_frames = 0;
    g_report_time = now;
}
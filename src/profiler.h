#ifndef PROFILER_H
#define PROFILER_H

#include <SDL.h>

Uint64 profNow(void);
double profHz(void);

typedef enum {
    PROF_LOGIC = 0,
    PROF_RENDER,          // entire render(), for context
    PROF_CLEAR,           // SDL_RenderClear at top of render()
    PROF_MAPCACHE,        // ensureMapCache + SDL_RenderCopy(g_mapCache)
    PROF_UPDATE_ANIM,     // the layer update+draw loop
    PROF_FOREWALL,        // g_forewallCache blit + its updateAnimationLinkList
    PROF_HP,              // renderHp
    PROF_COUNTDOWN,       // renderCountDown
    PROF_INFO,            // renderInfo (text)
    PROF_ID,              // renderId (text)
    PROF_PRESENT,         // SDL_RenderPresent
    PROF_COUNT
} ProfSlot;

void profInit(void);
void profBegin(ProfSlot s);
void profEnd(ProfSlot s);
void profFrameEnd(void);

#endif
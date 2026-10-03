#ifndef DUNGEONRUSH_GPU_H
#define DUNGEONRUSH_GPU_H

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

/* Opaque GPU image handle. */
typedef struct GpuImage GpuImage;

/* Prefixed DR_ to avoid clashing with citro3d's GPU_BLEND_* enum. */
#define DR_BLEND_NONE   0
#define DR_BLEND_BLEND  1
#define DR_BLEND_ADD    2

/* ---- lifecycle ---- */
bool gpuInit(void);
void gpuExit(void);

/* ---- images ---- */
GpuImage* gpuLoadPNG(const char* path);
GpuImage* gpuUploadABGR(const void* pixels, int w, int h);
void      gpuFree(GpuImage* img);
int       gpuImageWidth (const GpuImage* img);
int       gpuImageHeight(const GpuImage* img);

/* ---- frame ---- */
void gpuBeginFrame(void);
void gpuEndFrame(void);
void gpuClear(int r, int g, int b, int a);

/* ---- screens ---- */
/* The top screen is the default target each frame. Call gpuSetScreen() to
   draw on the bottom screen (320x240) in the same frame; the bottom screen
   is cleared the first time it is selected each frame. Selecting a screen
   resets the camera to (0,0). The bottom screen keeps its last image on
   frames where it is never selected. */
#define DR_SCREEN_TOP    0
#define DR_SCREEN_BOTTOM 1
void gpuSetScreen(int screen);

/* ---- camera ---- */
/* World->screen offset subtracted from every draw until changed.
   (0,0) = screen space (UI). Reset to (0,0) by gpuBeginFrame(). */
void gpuSetCamera(int x, int y);

/* ---- draw ---- */
/* src:  crop rect in image pixel coords (NULL = whole image).
   dst:  destination rect in screen pixels.
   angle_deg: clockwise degrees (0 = no rotation).
   flip: SDL_RendererFlip value (SDL_FLIP_NONE / HORIZONTAL / VERTICAL).
   tint_rgba: 0..255 per channel; 255 = no change.
   blend: GPU_BLEND_* */
void gpuBlit(GpuImage* img, const SDL_Rect* src, const SDL_Rect* dst,
             double angle_deg, SDL_RendererFlip flip,
             int tint_r, int tint_g, int tint_b, int tint_a,
             int blend);

/* Rectangles (for HP bars, text backgrounds, etc.) */
void gpuFillRect(int x, int y, int w, int h, int r, int g, int b, int a);
void gpuDrawRect(int x, int y, int w, int h, int r, int g, int b, int a);

#endif
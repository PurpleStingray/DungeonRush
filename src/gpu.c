#include "gpu.h"

#include <citro2d.h>
#include <citro3d.h>
#include <SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct GpuImage {
    C3D_Tex tex;
    int w, h;          /* logical (unpadded) size */
    int tw, th;        /* padded (power-of-two) size */
};

static C3D_RenderTarget* g_targetTop    = NULL;
static C3D_RenderTarget* g_targetBottom = NULL;
static C3D_RenderTarget* g_target       = NULL;   /* currently drawing to */
static int g_viewW = 400, g_viewH = 240;          /* size of current screen, for culling */
static int g_bottomCleared = 0;
static int g_frameOpen = 0;
static int g_blend = -1;          /* last DR_BLEND_* applied; -1 = unknown */
static int g_camX = 0, g_camY = 0;

/* ---------- lifecycle ---------- */

/* Transfer flags that match whatever format the screen's framebuffer is in. */
static u32 outputFlags(gfxScreen_t screen) {
    GSPGPU_FramebufferFormat sf = gfxGetScreenFormat(screen);
    u32 outFmt;
    switch (sf) {
        case GSP_RGBA8_OES:   outFmt = GX_TRANSFER_FMT_RGBA8;  break;
        case GSP_BGR8_OES:    outFmt = GX_TRANSFER_FMT_RGB8;   break;
        case GSP_RGB565_OES:  outFmt = GX_TRANSFER_FMT_RGB565; break;
        case GSP_RGB5_A1_OES: outFmt = GX_TRANSFER_FMT_RGB5A1; break;
        case GSP_RGBA4_OES:   outFmt = GX_TRANSFER_FMT_RGBA4;  break;
        default:              outFmt = GX_TRANSFER_FMT_RGB8;   break;
    }
    fprintf(stderr, "[gpu] screen %d fb format=%d\n", (int)screen, (int)sf);
    return GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
           GX_TRANSFER_RAW_COPY(0) |
           GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
           GX_TRANSFER_OUT_FORMAT(outFmt) |
           GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
}

bool gpuInit(void) {
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    g_targetTop    = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
    g_targetBottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    if (!g_targetTop || !g_targetBottom) {
        fprintf(stderr, "[gpu] C2D_CreateScreenTarget failed\n");
        return false;
    }
    C3D_RenderTargetSetOutput(g_targetTop,    GFX_TOP,    GFX_LEFT, outputFlags(GFX_TOP));
    C3D_RenderTargetSetOutput(g_targetBottom, GFX_BOTTOM, GFX_LEFT, outputFlags(GFX_BOTTOM));
    g_target = g_targetTop;

    fprintf(stderr, "[gpu] 3D=%d\n", (int)gfxIs3D());
    fprintf(stderr, "[gpu] init ok\n");
    return true;
}

void gpuExit(void) {
    C2D_Fini();
    C3D_Fini();
}

/* ---------- blend state ---------- */

/* Blend mode is global C3D state, so batch-flush only when it changes. */
static void set_blend(int mode) {
    if (mode != DR_BLEND_ADD && mode != DR_BLEND_NONE) mode = DR_BLEND_BLEND;
    if (mode == g_blend) return;
    C2D_Flush();
    if (mode == DR_BLEND_ADD) {
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                       GPU_SRC_ALPHA, GPU_ONE,
                       GPU_SRC_ALPHA, GPU_ONE);
    } else if (mode == DR_BLEND_NONE) {
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                       GPU_ONE, GPU_ZERO,
                       GPU_ONE, GPU_ZERO);
    } else {
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                       GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                       GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    }
    g_blend = mode;
}

/* ---------- camera ---------- */

void gpuSetCamera(int x, int y) { g_camX = x; g_camY = y; }

/* True if the rect is entirely outside the screen (plus margin). */
static inline int offscreen(int x, int y, int w, int h, int margin) {
    return x + w < -margin || y + h < -margin ||
           x > g_viewW + margin || y > g_viewH + margin;
}

/* ---------- image upload ---------- */

static int next_pow2(int v) {
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

/* Interleave the low 3 bits of x and y -> index 0..63 within an 8x8 tile
   (PICA200 Morton / Z-order). */
static inline uint32_t morton8(uint32_t x, uint32_t y) {
    uint32_t i = (x & 7) | ((y & 7) << 8);
    i = (i ^ (i << 2)) & 0x1313;
    i = (i ^ (i << 1)) & 0x1515;
    return (i | (i >> 7)) & 0x3F;
}

GpuImage* gpuUploadABGR(const void* pixels, int w, int h) {
    if (!pixels || w <= 0 || h <= 0) return NULL;

    int copyW = w, copyH = h;
    if (copyW > 1024) { copyW = 1024; }
    if (copyH > 1024) { copyH = 1024; }

    GpuImage* img = calloc(1, sizeof(GpuImage));
    if (!img) return NULL;

    int tw = next_pow2(copyW);
    int th = next_pow2(copyH);
    if (tw < 8) tw = 8;
    if (th < 8) th = 8;

    if (!C3D_TexInit(&img->tex, (u16)tw, (u16)th, GPU_RGBA8)) {
        fprintf(stderr, "[gpu] C3D_TexInit %dx%d failed\n", tw, th);
        free(img);
        return NULL;
    }
    C3D_TexSetFilter(&img->tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&img->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

    /* Zeroed so the padding area is transparent. */
    uint32_t* tmp = calloc((size_t)tw * (size_t)th, sizeof(uint32_t));
    if (!tmp) {
        C3D_TexDelete(&img->tex);
        free(img);
        return NULL;
    }

    /* Source: SDL ABGR8888 (A<<24|B<<16|G<<8|R).
       Dest:   GPU_RGBA8   (R<<24|G<<16|B<<8|A), 8x8 Morton tiles,
               source row y -> memory row y. */
    const uint32_t* src = (const uint32_t*)pixels;
    const int tilesPerRow = tw >> 3;

    for (int y = 0; y < copyH; y++) {
        const uint32_t* row = src + (size_t)y * (size_t)w;
        int my = y;
        for (int x = 0; x < copyW; x++) {
            uint32_t p = row[x];
            uint32_t r =  p        & 0xFF;
            uint32_t g = (p >>  8) & 0xFF;
            uint32_t b = (p >> 16) & 0xFF;
            uint32_t a = (p >> 24) & 0xFF;
            uint32_t px = (r << 24) | (g << 16) | (b << 8) | a;

            size_t tile = (size_t)(my >> 3) * (size_t)tilesPerRow + (size_t)(x >> 3);
            tmp[tile * 64 + morton8((uint32_t)x, (uint32_t)my)] = px;
        }
    }

    C3D_TexUpload(&img->tex, tmp);
    free(tmp);

    img->w = copyW; img->h = copyH;
    img->tw = tw;   img->th = th;
    fprintf(stderr, "[gpu] uploaded %dx%d -> %dx%d\n", w, h, tw, th);
    return img;
}

GpuImage* gpuLoadPNG(const char* path) {
    SDL_Surface* s = IMG_Load(path);
    if (!s) {
        fprintf(stderr, "[gpu] IMG_Load(%s) failed: %s\n", path, IMG_GetError());
        return NULL;
    }
    SDL_Surface* c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(s);
    if (!c) {
        fprintf(stderr, "[gpu] Convert(%s) failed: %s\n", path, SDL_GetError());
        return NULL;
    }
    GpuImage* img = gpuUploadABGR(c->pixels, c->w, c->h);
    SDL_FreeSurface(c);
    return img;
}

void gpuFree(GpuImage* img) {
    if (!img) return;
    C3D_TexDelete(&img->tex);
    free(img);
}

int gpuImageWidth (const GpuImage* img) { return img ? img->w : 0; }
int gpuImageHeight(const GpuImage* img) { return img ? img->h : 0; }

/* ---------- frame ---------- */

void gpuBeginFrame(void) {
    if (g_frameOpen) return;
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    g_target = g_targetTop;
    g_viewW = 400; g_viewH = 240;
    g_bottomCleared = 0;
    C2D_TargetClear(g_target, C2D_Color32(25, 17, 23, 255));
    C2D_SceneBegin(g_target);
    g_blend = -1;               /* unknown state at start of frame */
    g_camX = g_camY = 0;        /* every frame starts in screen space */
    g_frameOpen = 1;
}

void gpuSetScreen(int screen) {
    if (!g_frameOpen) return;
    C2D_Flush();                /* finish whatever was queued on the old screen */
    if (screen == DR_SCREEN_BOTTOM) {
        g_target = g_targetBottom;
        g_viewW = 320; g_viewH = 240;
        if (!g_bottomCleared) {
            C2D_TargetClear(g_target, C2D_Color32(25, 17, 23, 255));
            g_bottomCleared = 1;
        }
    } else {
        g_target = g_targetTop;
        g_viewW = 400; g_viewH = 240;
    }
    C2D_SceneBegin(g_target);
    g_blend = -1;
    g_camX = g_camY = 0;
}

void gpuEndFrame(void) {
    if (!g_frameOpen) return;
    C3D_FrameEnd(0);
    g_frameOpen = 0;
}

void gpuClear(int r, int g, int b, int a) {
    C2D_TargetClear(g_target, C2D_Color32(r, g, b, a));
}

/* ---------- draw ---------- */

static void build_subtex(GpuImage* img, const SDL_Rect* src,
                         SDL_RendererFlip flip, Tex3DS_SubTexture* out) {
    int sx, sy, sw, sh;
    if (src) { sx = src->x; sy = src->y; sw = src->w; sh = src->h; }
    else     { sx = 0;      sy = 0;      sw = img->w; sh = img->h; }

    out->width  = (u16)sw;
    out->height = (u16)sh;
    out->left   = (float)sx / img->tw;
    out->right  = (float)(sx + sw) / img->tw;
    /* top > bottom is the orientation citro2d expects. Do NOT swap these
       for a vertical flip: top < bottom is read as "rotated 90 degrees". */
    out->top    = 1.0f - (float)sy / img->th;
    out->bottom = 1.0f - (float)(sy + sh) / img->th;

    if (flip & SDL_FLIP_HORIZONTAL) {
        float t = out->left; out->left = out->right; out->right = t;
    }
}

void gpuBlit(GpuImage* img, const SDL_Rect* src, const SDL_Rect* dst,
             double angle_deg, SDL_RendererFlip flip,
             int tint_r, int tint_g, int tint_b, int tint_a,
             int blend) {
    if (!img || !dst || dst->w <= 0 || dst->h <= 0) return;

    /* World -> screen, then cull. Margin is generous so rotated sprites
       are never dropped while any part of them is visible. */
    int dx = dst->x - g_camX;
    int dy = dst->y - g_camY;
    int mg = dst->w > dst->h ? dst->w : dst->h;
    if (offscreen(dx, dy, dst->w, dst->h, mg)) return;

    int srcW = src ? src->w : img->w;
    int srcH = src ? src->h : img->h;
    if (srcW <= 0 || srcH <= 0) return;

    /* Vertical flip == horizontal flip + 180 degree rotation. */
    int f = (int)flip;
    if (f & SDL_FLIP_VERTICAL) {
        f = (f & ~SDL_FLIP_VERTICAL) ^ SDL_FLIP_HORIZONTAL;
        angle_deg += 180.0;
    }

    Tex3DS_SubTexture sub;
    build_subtex(img, src, (SDL_RendererFlip)f, &sub);
    C2D_Image ci = { &img->tex, &sub };

    float sx = (float)dst->w / (float)srcW;
    float sy = (float)dst->h / (float)srcH;

    /* Build a tint only if it would actually change something. */
    C2D_ImageTint tint;
    const C2D_ImageTint* tintp = NULL;
    if (tint_r != 255 || tint_g != 255 || tint_b != 255 || tint_a != 255) {
        C2D_PlainImageTint(&tint,
                           C2D_Color32(tint_r, tint_g, tint_b, tint_a),
                           1.0f);
        tintp = &tint;
    }

    set_blend(blend);

    if (angle_deg == 0.0) {
        /* DrawImageAt: (x, y) is the top-left corner */
        C2D_DrawImageAt(ci, (float)dx, (float)dy, 0.0f, tintp, sx, sy);
    } else {
        /* DrawImageAtRotated: (x, y) is the center */
        float cx = dx + dst->w * 0.5f;
        float cy = dy + dst->h * 0.5f;
        C2D_DrawImageAtRotated(ci, cx, cy, 0.0f,
                               (float)(angle_deg * (3.14159265358979 / 180.0)),
                               tintp, sx, sy);
    }
}

void gpuFillRect(int x, int y, int w, int h, int r, int g, int b, int a) {
    if (w <= 0 || h <= 0) return;
    x -= g_camX; y -= g_camY;
    if (offscreen(x, y, w, h, 0)) return;
    set_blend(DR_BLEND_BLEND);
    C2D_DrawRectSolid((float)x, (float)y, 0.0f,
                      (float)w, (float)h,
                      C2D_Color32(r, g, b, a));
}

void gpuDrawRect(int x, int y, int w, int h, int r, int g, int b, int a) {
    if (w <= 0 || h <= 0) return;
    x -= g_camX; y -= g_camY;
    if (offscreen(x, y, w, h, 0)) return;
    set_blend(DR_BLEND_BLEND);
    /* C2D has no outline primitive; draw 4 thin solid rects. */
    u32 col = C2D_Color32(r, g, b, a);
    C2D_DrawRectSolid((float)x,           (float)y,           0.0f, (float)w, 1.0f, col);
    C2D_DrawRectSolid((float)x,           (float)(y + h - 1), 0.0f, (float)w, 1.0f, col);
    C2D_DrawRectSolid((float)x,           (float)y,           0.0f, 1.0f,     (float)h, col);
    C2D_DrawRectSolid((float)(x + w - 1), (float)y,           0.0f, 1.0f,     (float)h, col);
}

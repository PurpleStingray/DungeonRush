#include "render.h"

#include <SDL.h>
#include <stdlib.h>
#include <string.h>

#include "ai.h"
#include "game.h"
#include "helper.h"
#include "map.h"
#include "profiler.h"
#include "res.h"
#include "types.h"
#include "gpu.h"

#ifdef DBG
#include <assert.h>
#endif

// Sprite
extern Snake* spriteSnake[SPRITES_MAX_NUM];
extern int spritesCount;
extern int playersCount;
extern Effect effects[];
extern SDL_Color BLACK;
extern SDL_Color WHITE;

#ifdef __3DS__
const int SCALE_FACTOR = 1;
#else
const int SCALE_FACTOR = 2;
#endif
extern int texturesCount;
extern Texture textures[TEXTURES_SIZE];
extern int textsCount;
extern Text texts[TEXTSET_SIZE];
Text* stageText;
Text* taskText;
Text* scoresText[MAX_PALYERS_NUM];

SDL_Renderer* renderer;
unsigned long long renderFrames;

LinkList animationsList[ANIMATION_LINK_LIST_NUM];
Animation* countDownBar;

/* ==================== Static map cache ==================== */
static SDL_Texture* g_mapCache      = NULL;
static SDL_Texture* g_forewallCache = NULL;
static bool         g_mapCacheDirty = true;
static bool         g_mmDirty       = true;   /* minimap terrain needs rebuilding */

void invalidateMapCache(void) {
  g_mapCacheDirty = true;
  g_mmDirty = true;
}


static void ensureMapCache(void) { /* no-op for now */ }
/* ========================================================= */

/* ==================== Per-layer diagnostics ==================== */
static Uint64   g_lay_time [ANIMATION_LINK_LIST_NUM];
static unsigned g_lay_count[ANIMATION_LINK_LIST_NUM];
static unsigned g_draws_fast;
static unsigned g_draws_slow;
static unsigned g_lay_frame;
static Uint64   g_lay_last;

static void diagPrint(void) {
  Uint64 now = profNow();
  double hz  = profHz();
  if (now - g_lay_last < (Uint64)hz) return;
  double to_us = 1e6 / hz / (double)g_lay_frame;
  fprintf(stderr, "[layer] fast=%u slow=%u | ",
          g_draws_fast / g_lay_frame,
          g_draws_slow / g_lay_frame);
  for (int i = 0; i < ANIMATION_LINK_LIST_NUM; i++) {
    fprintf(stderr, "%d:%.1fus/n=%u ",
            i,
            g_lay_time[i] * to_us,
            g_lay_count[i] / g_lay_frame);
  }
  fprintf(stderr, "\n");
  memset(g_lay_time,   0, sizeof(g_lay_time));
  memset(g_lay_count,  0, sizeof(g_lay_count));
  g_draws_fast = g_draws_slow = 0;
  g_lay_frame = 0;
  g_lay_last  = now;
}
/* ============================================================= */

void blacken(int duration) {
  for (int i = 0; i < duration; i++) {
    gpuBeginFrame();
    gpuFillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 25, 17, 23, 85);
    gpuEndFrame();
  }
}
void blackout() { blacken(RENDER_BLACKOUT_DURATION); }
void dim() { blacken(RENDER_DIM_DURATION); }
void initCountDownBar() {
  int barW = UI_COUNTDOWN_BAR_WIDTH * SCALE_FACTOR;   // on-screen width
  int barX = (SCREEN_WIDTH - barW) / 2;

  createAndPushAnimation(
      &animationsList[RENDER_LIST_UI_ID], &textures[RES_SLIDER], NULL,
      LOOP_INFI, 1, barX, 10, SDL_FLIP_NONE, 0, AT_TOP_LEFT);
  countDownBar = createAndPushAnimation(
      &animationsList[RENDER_LIST_UI_ID], &textures[RES_BAR_BLUE], NULL,
      LOOP_INFI, 1, barX, 10, SDL_FLIP_NONE, 0, AT_TOP_LEFT);
}
void initInfo() {
  extern int stage;
  char buf[1 << 8];
  sprintf(buf, "Stage:%3d", stage);
  if (stageText)
    setText(stageText, buf);
  else
    stageText = createText(buf, WHITE);
  for (int i = 0; i < playersCount; i++)
    if (!scoresText[i]) scoresText[i] = createText("placeholder", WHITE);
  if (!taskText) taskText = createText("placeholder", WHITE);
}
void initRenderer() {
  renderFrames = 0;
  for (int i = 0; i < ANIMATION_LINK_LIST_NUM; i++) {
    initLinkList(&animationsList[i]);
  }
  invalidateMapCache();
}
void clearInfo() {
  destroyText(stageText);
  stageText = NULL;
  destroyText(taskText);
  taskText = NULL;
  for (int i = 0; i < playersCount; i++) {
    destroyText(scoresText[i]);
    scoresText[i] = NULL;
  }
}
void clearRenderer() {
  for (int i = 0; i < ANIMATION_LINK_LIST_NUM; i++) {
    destroyAnimationsByLinkList(&animationsList[i]);
  }
}
void renderCstrCenteredText(const char* str, int x, int y, double scale) {
  Text text;
  if (!initText(&text, str, WHITE)) return;
  renderCenteredText(&text, x, y, scale);
  gpuFree(text.origin);
}
void renderCstrText(const char* str, int x, int y, double scale) {
  Text text;
  if (!initText(&text, str, WHITE)) return;
  renderText(&text, x, y, scale);
  gpuFree(text.origin);
}
void renderText(const Text* text, int x, int y, double scale) {
  if (!text || !text->origin) return;
  SDL_Rect dst = {x, y, (int)(text->width * scale + 0.5),
                        (int)(text->height * scale + 0.5)};
  gpuBlit(text->origin, NULL, &dst, 0.0, SDL_FLIP_NONE,
          255, 255, 255, 255, DR_BLEND_BLEND);
}

SDL_Point renderCenteredText(const Text* text, int x, int y, double scale) {
  int width  = text->width * scale + 0.5;
  int height = text->height * scale + 0.5;
  SDL_Rect dst = {x - width / 2, y - height / 2, width, height};
  gpuBlit(text->origin, NULL, &dst, 0.0, SDL_FLIP_NONE,
          255, 255, 255, 255, DR_BLEND_BLEND);
  return (SDL_Point){x - width / 2, y - height / 2};
}
void updateAnimationOfSprite(Sprite* self) {
  Animation* ani = self->ani;
  ani->x = self->x;
  ani->y = self->y;
  ani->flip = self->face == RIGHT ? SDL_FLIP_NONE : SDL_FLIP_HORIZONTAL;
}
void updateAnimationOfSnake(Snake* snake) {
  for (LinkNode* p = snake->sprites->head; p; p = p->nxt) {
    updateAnimationOfSprite(p->element);
  }
}
void updateAnimationOfBlock(Block* self) {
  Animation* ani = self->ani;
  ani->x = self->x;
  ani->y = self->y;
  if (self->bp == BLOCK_TRAP) {
    self->ani->origin = &textures[self->enable ? RES_FLOOR_SPIKE_ENABLED
                                               : RES_FLOOR_SPIKE_DISABLED];
  } else if (self->bp == BLOCK_EXIT) {
    if (self->enable && self->ani->origin != &textures[RES_FLOOR_EXIT]) {
      self->ani->origin = &textures[RES_FLOOR_EXIT];
      createAndPushAnimation(&animationsList[RENDER_LIST_MAP_SPECIAL_ID],
                             &textures[RES_FLOOR_EXIT], &effects[EFFECT_BLINK],
                             LOOP_INFI, 30, self->x, self->y, SDL_FLIP_NONE, 0,
                             AT_TOP_LEFT);
    }
  }
}
void clearBindInAnimationsList(Sprite* sprite, int id) {
  for (LinkNode *p = animationsList[id].head, *nxt; p; p = nxt) {
    nxt = p->nxt;
    Animation* ani = p->element;
    if (ani->bind == sprite) {
      ani->bind = NULL;
      if (ani->dieWithBind) {
        removeLinkNode(&animationsList[id], p);
        destroyAnimation(ani);
      }
    }
  }
}
void bindAnimationToSprite(Animation* ani, Sprite* sprite, bool isStrong) {
  ani->bind = sprite;
  ani->dieWithBind = isStrong;
  updateAnimationFromBind(ani);
}
void updateAnimationFromBind(Animation* ani) {
  if (ani->bind) {
    Sprite* sprite = ani->bind;
    ani->x = sprite->x;
    ani->y = sprite->y;
    ani->flip = sprite->ani->flip;
  }
}
void renderAnimation(Animation* ani) {
  if (!ani) return;
  updateAnimationFromBind(ani);
  int width = (ani->cropW > 0) ? ani->cropW : ani->origin->width;
  int height = ani->origin->height;
    if (ani->scaled) {
    width *= SCALE_FACTOR;
    height *= SCALE_FACTOR;
  } else {
    width  = (width  * FX_SCALE_NUM + FX_SCALE_DEN / 2) / FX_SCALE_DEN;
    height = (height * FX_SCALE_NUM + FX_SCALE_DEN / 2) / FX_SCALE_DEN;
  }
  SDL_Rect dst = {ani->x - width / 2, ani->y - height, width, height};
  if (ani->at == AT_TOP_LEFT) {
    dst.x = ani->x;
    dst.y = ani->y;
  } else if (ani->at == AT_CENTER) {
    dst.x = ani->x - width / 2;
    dst.y = ani->y - height / 2;
  } else if (ani->at == AT_BOTTOM_LEFT) {
    dst.x = ani->x;
    dst.y = ani->y + UNIT - height - 3;
  }

#ifdef DBG
  assert(ani->duration >= ani->origin->frames);
#endif
  int stage = 0;
  if (ani->origin->frames > 1) {
    double interval = (double)ani->duration / ani->origin->frames;
    stage = ani->currentFrame / interval;
  }
  SDL_Rect srcLocal = ani->origin->crops[stage];
  if (ani->cropW > 0 && ani->cropW < srcLocal.w) {
    srcLocal.w = ani->cropW;
  }

  /* --- effect tint + blend --- */
  int tint_r = 255, tint_g = 255, tint_b = 255, tint_a = 255;
  int blend  = DR_BLEND_BLEND;
  if (ani->effect) {
    Effect* e = ani->effect;
    double interval = e->duration / (e->length - 1);
    double progress = e->currentFrame;
    int st = progress / interval;
    progress -= st * interval;
    progress /= interval;
    SDL_Color prev = e->keys[st];
    SDL_Color nxt  = e->keys[MIN(st + 1, e->length - 1)];
    tint_r = prev.r * (1 - progress) + nxt.r * progress;
    tint_g = prev.g * (1 - progress) + nxt.g * progress;
    tint_b = prev.b * (1 - progress) + nxt.b * progress;
    tint_a = prev.a * (1 - progress) + nxt.a * progress;
    blend  = (e->mode == SDL_BLENDMODE_ADD) ? DR_BLEND_ADD : DR_BLEND_BLEND;
    e->currentFrame = (e->currentFrame + 1) % e->duration;
  }

  if (ani->angle == 0.0) g_draws_fast++; else g_draws_slow++;
  gpuBlit(ani->origin->origin, &srcLocal, &dst,
          ani->angle, ani->flip,
          tint_r, tint_g, tint_b, tint_a, blend);

#ifdef DBG_CROSS
  if (ani->at == AT_BOTTOM_CENTER) {
    Sprite fake;
    fake.ani = ani;
    SDL_Rect tmp;

    tmp = getSpriteBoundBox(&fake);
    gpuDrawRect(tmp.x, tmp.y, tmp.w, tmp.h, 0, 255, 0, 200);

    tmp = getSpriteFeetBox(&fake);
    gpuDrawRect(tmp.x, tmp.y, tmp.w, tmp.h, 255, 0, 0, 200);

    gpuDrawRect(dst.x, dst.y, dst.w, dst.h, 0, 0, 255, 200);
  }
#endif
}
void pushAnimationToRender(int id, Animation* ani) {
  LinkNode* p = createLinkNode(ani);
  pushLinkNode(&animationsList[id], p);
}
Animation* createAndPushAnimation(LinkList* list, Texture* texture,
                                  const Effect* effect, LoopType lp,
                                  int duration, int x, int y,
                                  SDL_RendererFlip flip, double angle, At at) {
  Animation* ani =
      createAnimation(texture, effect, lp, duration, x, y, flip, angle, at);
  LinkNode* node = createLinkNode(ani);
  pushLinkNode(list, node);
  return ani;
}
void updateAnimationLinkList(LinkList* list) {
  LinkNode* p = list->head;
  while (p) {
    Animation* ani = p->element;
    LinkNode* nxt = p->nxt;
    ani->currentFrame++;
    ani->lifeSpan--;
    if (ani->effect) {
      ani->effect->currentFrame++;
      ani->effect->currentFrame %= ani->effect->duration;
    }
    if (ani->lp == LOOP_ONCE) {
      if (ani->currentFrame == ani->duration) {
        destroyAnimation(p->element);
        removeLinkNode(list, p);
      }
    } else {
      if (ani->lp == LOOP_LIFESPAN && !ani->lifeSpan) {
        destroyAnimation(p->element);
        removeLinkNode(list, p);
      } else
        ani->currentFrame %= ani->duration;
    }
    p = nxt;
  }
}
int compareAnimationByY(const void* x, const void* y) {
  Animation* a = *(Animation**)x;
  Animation* b = *(Animation**)y;
  return b->y - a->y;
}
/* ---- early culling ----
   gpuBlit already drops off-screen draws, but by then the CPU has done all the
   per-animation setup. Reject cheaply here instead. render() turns this on for
   world layers and off for the UI layer. */
extern int cameraX, cameraY;           /* defined with the camera code below */
static bool     g_cullOn   = false;
static unsigned g_layDrawn = 0;        /* animations actually drawn this layer */

static inline bool animOffscreen(Animation* a) {
  /* Bound animations (e.g. ice on a sprite) take their position from the
     sprite, so refresh it BEFORE judging, or a stale position could keep
     the animation culled forever. */
  updateAnimationFromBind(a);
  int w = a->origin->width, h = a->origin->height;
  int mg = (w > h ? w : h) * SCALE_FACTOR;   /* generous: covers any anchor/rotation */
  return a->x < cameraX - mg || a->x > cameraX + SCREEN_WIDTH  + mg ||
         a->y < cameraY - mg || a->y > cameraY + SCREEN_HEIGHT + mg;
}

void renderAnimationLinkList(LinkList* list) {
  for (LinkNode* p = list->head; p; p = p->nxt) {
    Animation* a = p->element;
    if (!a) continue;
    if (g_cullOn && animOffscreen(a)) continue;
    renderAnimation(a);
    g_layDrawn++;
  }
}
void renderAnimationLinkListWithSort(LinkList* list) {
  static Animation* buffer[RENDER_BUFFER_SIZE];
  int count = 0;
  for (LinkNode* p = list->head; p; p = p->nxt) {
    Animation* a = p->element;
    if (!a) continue;
    if (g_cullOn && animOffscreen(a)) continue;
    buffer[count++] = a;
  }
  qsort(buffer, count, sizeof(Animation*), compareAnimationByY);
  g_layDrawn += count;
  while (count) renderAnimation(buffer[--count]);
}
void renderSnakeHp(Snake* snake) {
  for (LinkNode* p = snake->sprites->head; p; p = p->nxt) {
    Sprite* sprite = p->element;
    if (sprite->hp >= sprite->totalHp) continue;
    double percent = (double)sprite->hp / sprite->totalHp;
    if (percent > 1.0) percent = 1.0;
    for (int i = 0; percent > 1e-8; i++, percent -= 1) {
      int r = 0, g = 0, b = 0;
      if (i == 0) {
        if (percent < 1) {
          r = MIN((1 - percent) * 2 * 255, 255),
          g = MAX(0, 255 - (MAX(0.5 - percent, 0)) * 2 * 255);
        } else { g = 255; }
      } else { r = g = 0, b = 255; }
      int width = RENDER_HP_BAR_WIDTH;
      int spriteHeight = sprite->ani->origin->height * SCALE_FACTOR;
      int bx = sprite->x - UNIT / 2 + (UNIT - width) / 2;
      int by = sprite->y - spriteHeight - RENDER_HP_BAR_HEIGHT * (i + 1);
      gpuDrawRect(bx, by, width * MIN(1, percent), RENDER_HP_BAR_HEIGHT,
                  r, g, b, 255);
    }
  }
}
void renderHp() {
  for (int i = 0; i < spritesCount; i++) renderSnakeHp(spriteSnake[i]);
}
void renderCenteredTextBackground(Text* text, int x, int y, double scale) {
  int width  = text->width * scale + 0.5;
  int height = text->height * scale + 0.5;
  gpuFillRect(x - width / 2, y - height / 2, width, height, 255, 0, 0, 200);
}
void renderId() {
  int powerful = getPowerfulPlayer();
  for (int i = 0; i < playersCount; i++) {
    Snake* snake = spriteSnake[i];
    if (snake->sprites->head) {
      Sprite* snakeHead = snake->sprites->head->element;
      if (i == powerful)
        renderCenteredTextBackground(&texts[4 + i], snakeHead->x, snakeHead->y,
                                     0.5);
      renderCenteredText(&texts[4 + i], snakeHead->x, snakeHead->y, 0.5);
    }
  }
}
void renderCountDown() {
  extern unsigned int physicsFrames;
  double percent =
      (double)(physicsFrames % GAME_MAP_RELOAD_PERIOD) / GAME_MAP_RELOAD_PERIOD;
  countDownBar->cropW = (int)(percent * UI_COUNTDOWN_BAR_WIDTH);
}
void renderInfo() {
  int startY = 0, startX = 10;
  int lineGap = FONT_SIZE;
  renderText(stageText, startX, startY, 1);
  startY += lineGap;

  // Cache last-displayed integer per player so we skip calcScore + sprintf +
  // strcmp when the rank hasn't crossed a whole-number boundary.
  static int  lastScore[MAX_PALYERS_NUM];
  static bool lastScoreInit = false;
  if (!lastScoreInit) {
    for (int i = 0; i < MAX_PALYERS_NUM; i++) lastScore[i] = -1;
    lastScoreInit = true;
  }

  for (int i = 0; i < playersCount; i++) {
    calcScore(spriteSnake[i]->score);
    int display = (int)(spriteSnake[i]->score->rank + 0.5);

    if (display != lastScore[i]) {
      lastScore[i] = display;
      char buf[1 << 8];
      sprintf(buf, "Player%d:%5d", i + 1, display);
      setText(scoresText[i], buf);
    }
    renderText(scoresText[i], startX, startY, 1);
    startY += lineGap;
  }

  if (playersCount == 1) {
    extern int GAME_WIN_NUM;
    char buf[1 << 8];
    sprintf(buf, "Find %d more heros!",
            GAME_WIN_NUM > spriteSnake[0]->num
                ? GAME_WIN_NUM - spriteSnake[0]->num
                : 0);
    setText(taskText, buf);   // setText's strcmp already guards this
    renderText(taskText, startX, startY, 1);
    startY += lineGap;
  }
}
/* ---- camera ---- */
int cameraX = 0, cameraY = 0;

/* Set to 1 to stop the camera at the map edges instead of showing the empty
   background past the outer walls (the player then drifts off-center there). */
#define CAMERA_CLAMP 0

/* Camera glide: when the head sprite changes (new hero picked up, or the old
   head died), the follow target jumps by up to a sprite width. Instead of
   snapping, the jump is stored as an offset that decays each frame. */
#define CAMERA_EASE_NUM   15            /* offset *= 7/8 per frame (~12 frames) */
#define CAMERA_EASE_DEN   16
#define CAMERA_MAX_ABSORB (UNIT * 3)   /* bigger jumps = new level: just snap */

static inline int camAbs(int v) { return v < 0 ? -v : v; }

static void updateCamera(void) {
#ifdef WORLD_CAMERA
  static Sprite* lastHead = NULL;      /* compared only, never dereferenced */
  static int lastTx = 0, lastTy = 0, offX = 0, offY = 0;

  if (playersCount == 1 && spriteSnake[0]->sprites->head) {
    Sprite* s = spriteSnake[0]->sprites->head->element;
    int tx = s->x;
    int ty = s->y - UNIT / 2;          /* sprite y is the feet */

    /* Let any existing offset glide toward zero. */
    offX = offX * CAMERA_EASE_NUM / CAMERA_EASE_DEN;
    offY = offY * CAMERA_EASE_NUM / CAMERA_EASE_DEN;

    /* Head changed: keep the camera where it was and glide from there. */
    if (s != lastHead) {
      int jx = lastTx - tx, jy = lastTy - ty;
      if (lastHead && camAbs(jx) <= CAMERA_MAX_ABSORB
                   && camAbs(jy) <= CAMERA_MAX_ABSORB) {
        offX += jx;
        offY += jy;
      } else {
        offX = offY = 0;               /* new game/level: snap */
      }
      lastHead = s;
    }
    lastTx = tx;
    lastTy = ty;

    cameraX = tx + offX - SCREEN_WIDTH / 2;
    cameraY = ty + offY - SCREEN_HEIGHT / 2;
#if CAMERA_CLAMP
    if (cameraX < 0) cameraX = 0;
    if (cameraY < 0) cameraY = 0;
    if (cameraX > WORLD_WIDTH  - SCREEN_WIDTH)  cameraX = WORLD_WIDTH  - SCREEN_WIDTH;
    if (cameraY > WORLD_HEIGHT - SCREEN_HEIGHT) cameraY = WORLD_HEIGHT - SCREEN_HEIGHT;
#endif
  } else {
    lastHead = NULL;
    offX = offY = 0;
  }
#endif
}

/* ==================== Bottom-screen minimap ====================
   The terrain is a tiny n x m texture (one pixel per map tile) rebuilt only
   when the level changes, and scaled up with nearest filtering: one draw call.
   Everything else is a handful of filled rects per frame. */
extern bool hasMap[MAP_SIZE][MAP_SIZE];
extern int  exitX, exitY;
extern Block map[MAP_SIZE][MAP_SIZE];
extern Item  itemMap[MAP_SIZE][MAP_SIZE];
extern const int n, m;

/* Only show enemies within this many tiles of the player (0 = show them all). */
#define MINIMAP_ENEMY_RADIUS 0

#define MM_ABGR(r, g, b) (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

/* Palette sampled from the tileset (floor_1 base, wall accent). */
#define MM_FLOOR_ABGR MM_ABGR( 72,  59,  58)
#define MM_WALL_ABGR  MM_ABGR(119,  92,  85)

static GpuImage* g_mmTex = NULL;
static int       g_mmTile, g_mmOx, g_mmOy;

static void buildMinimapTexture(void) {
  static uint32_t px[MAP_SIZE * MAP_SIZE];
  for (int j = 0; j < m; j++) {
    for (int i = 0; i < n; i++) {
      uint32_t c = 0;                                   /* void: transparent */
      if (hasMap[i][j]) {
        c = MM_FLOOR_ABGR;                              /* traps shown as floor */
      } else {
        for (int dx = -1; dx <= 1 && !c; dx++)          /* wall = next to floor */
          for (int dy = -1; dy <= 1 && !c; dy++) {
            int xx = i + dx, yy = j + dy;
            if (xx >= 0 && xx < n && yy >= 0 && yy < m && hasMap[xx][yy])
              c = MM_WALL_ABGR;
          }
      }
      px[j * n + i] = c;
    }
  }
  if (g_mmTex) gpuFree(g_mmTex);
  g_mmTex = gpuUploadABGR(px, n, m);
  g_mmDirty = false;
}

/* Dot centered on a world position. */
static void mmDot(int wx, int wy, int size, int r, int g, int b) {
  int x = g_mmOx + wx * g_mmTile / UNIT - size / 2;
  int y = g_mmOy + wy * g_mmTile / UNIT - size / 2;
  gpuFillRect(x, y, size, size, r, g, b, 255);
}

static void renderMinimap(void) {
#ifdef WORLD_CAMERA
  if (playersCount != 1 || !spriteSnake[0]) return;
  if (g_mmDirty || !g_mmTex) buildMinimapTexture();
  if (!g_mmTex) return;

  gpuSetScreen(DR_SCREEN_BOTTOM);

  int tile = MIN(BOTTOM_W / n, BOTTOM_H / m);
  int mw = n * tile, mh = m * tile;
  g_mmTile = tile;
  g_mmOx = (BOTTOM_W - mw) / 2;
  g_mmOy = (BOTTOM_H - mh) / 2;

  /* backdrop fills the whole screen, then the terrain */
  gpuFillRect(0, 0, BOTTOM_W, BOTTOM_H, 25, 17, 23, 255);
  SDL_Rect dst = {g_mmOx, g_mmOy, mw, mh};
  gpuBlit(g_mmTex, NULL, &dst, 0.0, SDL_FLIP_NONE, 255, 255, 255, 255,
          DR_BLEND_BLEND);

  /* exit: dim until it opens */
  if (exitX >= 0 && exitY >= 0) {
    bool open = map[exitX][exitY].enable;
    gpuFillRect(g_mmOx + exitX * tile, g_mmOy + exitY * tile, tile, tile,
                open ? 255 : 120, open ? 210 : 100, open ? 60 : 40, 255);
  }

  /* pickups on the floor */
  for (int i = 0; i < n; i++)
    for (int j = 0; j < m; j++) {
      if (!hasMap[i][j]) continue;
      int r, g, b;
      switch (itemMap[i][j].type) {
        case ITEM_HERO:              r =  80; g = 220; b = 255; break;
        case ITEM_HP_MEDCINE:        r = 255; g = 110; b = 160; break;
        case ITEM_HP_EXTRA_MEDCINE:  r = 255; g = 220; b =  60; break;
        case ITEM_WEAPON:            r = 255; g = 150; b =  40; break;
        default: continue;
      }
      mmDot(i * UNIT + UNIT / 2, j * UNIT + UNIT / 2, 3, r, g, b);
    }

  /* enemies */
  LinkNode* headNode = spriteSnake[0]->sprites->head;
  Sprite* pHead = headNode ? headNode->element : NULL;
  for (int s = playersCount; s < spritesCount; s++) {
    Snake* sn = spriteSnake[s];
    if (!sn) continue;
    for (LinkNode* p = sn->sprites->head; p; p = p->nxt) {
      Sprite* sp = p->element;
#if MINIMAP_ENEMY_RADIUS > 0
      if (pHead) {
        long dx = sp->x - pHead->x, dy = sp->y - pHead->y;
        long lim = (long)MINIMAP_ENEMY_RADIUS * UNIT;
        if (dx * dx + dy * dy > lim * lim) continue;
      }
#endif
      mmDot(sp->x, sp->y - UNIT / 2, 3, 230, 60, 60);
    }
  }

  /* player: followers, then the head on top with a dark outline */
  for (LinkNode* p = headNode; p; p = p->nxt) {
    if (p == headNode) continue;
    Sprite* sp = p->element;
    mmDot(sp->x, sp->y - UNIT / 2, 3, 80, 230, 110);
  }
  if (pHead) {
    mmDot(pHead->x, pHead->y - UNIT / 2, 5, 0, 0, 0);
    mmDot(pHead->x, pHead->y - UNIT / 2, 3, 255, 255, 255);
  }

  /* what the top screen is currently showing */
  gpuDrawRect(g_mmOx + cameraX * tile / UNIT, g_mmOy + cameraY * tile / UNIT,
              SCREEN_WIDTH * tile / UNIT, SCREEN_HEIGHT * tile / UNIT,
              255, 255, 255, 120);
#endif
}

void render() {
  gpuBeginFrame();
  profBegin(PROF_RENDER);

  updateCamera();

  profBegin(PROF_MAPCACHE);
  ensureMapCache();
  profEnd(PROF_MAPCACHE);

  profBegin(PROF_UPDATE_ANIM);
  for (int i = 0; i < RENDER_LIST_BOTTOM_MAP_ID; i++) {
    Uint64 t0 = profNow();

    /* World layers scroll with the camera and are culled against it;
       the UI layer stays on screen and is never culled. */
    if (i == RENDER_LIST_UI_ID) { gpuSetCamera(0, 0); g_cullOn = false; }
    else { gpuSetCamera(cameraX, cameraY); g_cullOn = true; }

    /* Map/forewall layers hold only static one-frame tiles: nothing to advance. */
    if (i != RENDER_LIST_MAP_ID && i != RENDER_LIST_MAP_FOREWALL)
      updateAnimationLinkList(&animationsList[i]);

    g_layDrawn = 0;
    if (i == RENDER_LIST_SPRITE_ID)
      renderAnimationLinkListWithSort(&animationsList[i]);
    else
      renderAnimationLinkList(&animationsList[i]);

    g_lay_time[i]  += profNow() - t0;
    g_lay_count[i] += g_layDrawn;
  }
  g_cullOn = false;
  g_lay_frame++;
  diagPrint();
  profEnd(PROF_UPDATE_ANIM);

  /* World-space overlays: HP bars and player id tags. */
  gpuSetCamera(cameraX, cameraY);
  profBegin(PROF_HP);
  renderHp();
  profEnd(PROF_HP);
  profBegin(PROF_ID);
  renderId();
  profEnd(PROF_ID);

  /* Screen-space HUD. */
  gpuSetCamera(0, 0);
  profBegin(PROF_COUNTDOWN);
  renderCountDown();
  profEnd(PROF_COUNTDOWN);
  profBegin(PROF_INFO);
  renderInfo();
  profEnd(PROF_INFO);

  renderMinimap();

  renderFrames++;
  profEnd(PROF_RENDER);
  gpuEndFrame();
}
/* ==================== Bottom-screen menu scene ====================
   A blank room fitted to the 320x240 bottom screen, with a few enemies
   standing around playing their idle animation, like the top-screen menu.
   Built by baseUi() (right after initRenderer), drawn by renderUi(). */
static const int kMenuEnemies[] = {
  RES_TINY_ZOMBIE, RES_GOBLIN, RES_IMP, RES_SKELET, RES_MUDDY, RES_SWAMPY,
  RES_ZOMBIE, RES_ICE_ZOMBIE, RES_MASKED_ORC, RES_ORC_WARRIOR,
  RES_ORC_SHAMAN, RES_NECROMANCER, RES_WOGOL, RES_CHORT
};
#define MENU_ENEMY_KINDS ((int)(sizeof(kMenuEnemies) / sizeof(kMenuEnemies[0])))

void pushBottomMenuScene(void) {
  /* Room: 20x15 tiles. Floor is inset by one tile so walls fit on screen. */
  initBlankMapAt(1, 1, BOTTOM_W / UNIT - 2, BOTTOM_H / UNIT - 2);
  pushMapToRender();   /* pushes into the top-screen MAP/FOREWALL lists... */

  /* ...so move those nodes over to the bottom-screen lists. */
  animationsList[RENDER_LIST_BOTTOM_MAP_ID]      = animationsList[RENDER_LIST_MAP_ID];
  animationsList[RENDER_LIST_BOTTOM_FOREWALL_ID] = animationsList[RENDER_LIST_MAP_FOREWALL];
  initLinkList(&animationsList[RENDER_LIST_MAP_ID]);
  initLinkList(&animationsList[RENDER_LIST_MAP_FOREWALL]);

  /* A few different enemies, no repeats, facing toward the middle. */
  static const int slots[][2] = {{56, 88}, {150, 128}, {246, 92}, {96, 190}, {232, 196}};
  int order[MENU_ENEMY_KINDS];
  for (int i = 0; i < MENU_ENEMY_KINDS; i++) order[i] = i;
  for (int i = MENU_ENEMY_KINDS - 1; i > 0; i--) {
    int j = randInt(0, i), t = order[i];
    order[i] = order[j];
    order[j] = t;
  }
  for (int k = 0; k < (int)(sizeof(slots) / sizeof(slots[0])); k++) {
    createAndPushAnimation(&animationsList[RENDER_LIST_BOTTOM_SPRITE_ID],
                           &textures[kMenuEnemies[order[k]]], NULL, LOOP_INFI,
                           SPRITE_ANIMATION_DURATION, slots[k][0], slots[k][1],
                           slots[k][0] > BOTTOM_W / 2 ? SDL_FLIP_HORIZONTAL
                                                      : SDL_FLIP_NONE,
                           0, AT_BOTTOM_CENTER);
  }
}

void renderUi() {
  gpuBeginFrame();

  /* Bottom screen first (selecting it clears it), then back to the top. */
  gpuSetScreen(DR_SCREEN_BOTTOM);
  updateAnimationLinkList(&animationsList[RENDER_LIST_BOTTOM_SPRITE_ID]);
  renderAnimationLinkList(&animationsList[RENDER_LIST_BOTTOM_MAP_ID]);
  renderAnimationLinkListWithSort(&animationsList[RENDER_LIST_BOTTOM_SPRITE_ID]);
  renderAnimationLinkList(&animationsList[RENDER_LIST_BOTTOM_FOREWALL_ID]);
  gpuSetScreen(DR_SCREEN_TOP);

  for (int i = 0; i < RENDER_LIST_BOTTOM_MAP_ID; i++) {
    updateAnimationLinkList(&animationsList[i]);
    if (i == RENDER_LIST_SPRITE_ID)
      renderAnimationLinkListWithSort(&animationsList[i]);
    else
      renderAnimationLinkList(&animationsList[i]);
  }

  /* NOTE: no gpuEndFrame() here -- callers close the frame. */
}

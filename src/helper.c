#include "helper.h"

#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "prng.h"
#include "res.h"
#include "types.h"

extern const int SCALE_FACTOR;
extern Texture textures[];
bool inr(int x, int l, int r) { return x <= r && l <= x; }
int randInt(int l, int r) { return prngRand() % (r - l + 1) + l; }
double randDouble() { return (double)prngRand() / PRNG_MAX; }
int IntervalCalc(int l1, int r1, int l2, int r2) {
  return MAX(-MAX(l1, l2) + MIN(r1, r2), 0);
}
int RectRectCalc(SDL_Rect* a, SDL_Rect* b) {
  return IntervalCalc(a->x, a->x + a->w, b->x, b->x + b->w) *
         IntervalCalc(a->y, a->y + a->h, b->y, b->y + b->h);
}
bool IntervalCross(int l1, int r1, int l2, int r2) {
  return MAX(l1, l2) < MIN(r1, r2);
}
bool RectRectCross(SDL_Rect* a, SDL_Rect* b) {
  return RectRectCalc(a, b) >= HELPER_RECT_CROSS_LIMIT;
  /*
  return IntervalCross(a->x, a->x + a->w, b->x, b->x + b->w) &&
         IntervalCross(a->y, a->y + a->h, b->y, b->y + b->h);
         */
}
bool RectCirCross(SDL_Rect* a, int x, int y, int r) {
  if (inr(x, a->x, a->x + a->w) && inr(y, a->y, a->y + a->h)) {
    puts("failed1");
    return true;
  }
  if (abs(x - a->x) <= r) {
    puts("failed2");
    return true;
  }
  if (abs(x - a->x - a->w) <= r) {
    puts("failed3");
    return true;
  }
  if (abs(y - a->y) <= r) {
    puts("failed4");
    return true;
  }
  if (abs(y - a->y - a->h) <= r) {
    puts("failed5");
    return true;
  }
  return false;
}
SDL_Rect getSpriteAnimationBox(Sprite* sprite) {
  Animation* ani = sprite->ani;
  SDL_Rect dst = {ani->x - ani->origin->width * SCALE_FACTOR / 2,
                  ani->y - ani->origin->height * SCALE_FACTOR,
                  ani->origin->width * SCALE_FACTOR,
                  ani->origin->height * SCALE_FACTOR};
  return dst;
}
SDL_Rect getSpriteBoundBox(Sprite* sprite) {
  Animation* ani = sprite->ani;
  int sw = ani->origin->width  * SCALE_FACTOR;
  int sh = ani->origin->height * SCALE_FACTOR;

  SDL_Rect dst = {ani->x - sw / 2, ani->y - sh, sw, sh};

  bool big = (ani->origin == &textures[RES_BIG_DEMON] ||
              ani->origin == &textures[RES_BIG_ZOMBIE] ||
              ani->origin == &textures[RES_ORGRE]);

#ifdef __3DS__
  int deltaW = big ? 12 : 10;   // was 25 / 20  (÷2, rounded)
  int deltaV = 3;               // was 6
#else
  int deltaW = big ? BIG_SPRITE_EFFECT_DELTA : SPRITE_EFFECT_DELTA;
  int deltaV = SPRITE_EFFECT_VERTICAL_DELTA;
#endif

  dst.w -= deltaW;
  dst.x += deltaW / 2;
  dst.y += deltaV;
  dst.h -= deltaV;
  return dst;
}

SDL_Rect getSpriteFeetBox(Sprite* sprite) {
  Animation* ani = sprite->ani;
  SDL_Rect dst = getSpriteBoundBox(sprite);
#ifdef __3DS__
  dst.y = ani->y - 6;
  dst.h = 6;
#else
  dst.y = ani->y - SPRITE_EFFECT_FEET;
  dst.h = SPRITE_EFFECT_FEET;
#endif
  return dst;
}
SDL_Rect getMapRect(int x, int y) {
  SDL_Rect ret = {x * UNIT, y * UNIT, UNIT, UNIT};
  return ret;
}
double distance(Point a, Point b) {
  double dx = a.x - b.x, dy = a.y - b.y;
  return sqrt(dx * dx + dy * dy);
}

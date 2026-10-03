#include "types.h"

#include <SDL_ttf.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helper.h"
#include "render.h"

#ifdef DBG
#include <assert.h>
#endif

extern LinkList animationsList[];
extern TTF_Font* font;
extern SDL_Renderer* renderer;
SDL_Color BLACK = {0, 0, 0, 255};
SDL_Color WHITE = {255, 255, 255, 255};
void initTexture(Texture* self, GpuImage* origin, int width, int height,
                 int frames) {
  if (frames < 0 || frames > TEXTURE_MAX_FRAMES) {
    fprintf(stderr, "initTexture: frames=%d > TEXTURE_MAX_FRAMES=%d\n",
            frames, TEXTURE_MAX_FRAMES);
    frames = TEXTURE_MAX_FRAMES;
  }
  self->origin = origin;
  self->width  = width;
  self->height = height;
  self->frames = frames;
}

void destroyTexture(Texture* self) {
  free(self);
}
bool initText(Text* self, const char* str, SDL_Color color) {
  self->color = color;
  strncpy(self->text, str, TEXT_LEN - 1);
  self->text[TEXT_LEN - 1] = '\0';

  SDL_Surface* textSurface = TTF_RenderText_Solid(font, str, color);
  if (!textSurface) {
    fprintf(stderr, "TTF_RenderText_Solid failed: %s\n", TTF_GetError());
    self->origin = NULL;
    self->width = self->height = 0;
    return false;
  }
  SDL_Surface* converted =
      SDL_ConvertSurfaceFormat(textSurface, SDL_PIXELFORMAT_ABGR8888, 0);
  SDL_FreeSurface(textSurface);
  if (!converted) {
    fprintf(stderr, "TTF surface convert failed: %s\n", SDL_GetError());
    self->origin = NULL;
    self->width = self->height = 0;
    return false;
  }

  self->origin = gpuUploadABGR(converted->pixels, converted->w, converted->h);
  self->width  = converted->w;
  self->height = converted->h;
  SDL_FreeSurface(converted);

  if (!self->origin) {
    fprintf(stderr, "Text GPU upload failed\n");
    self->width = self->height = 0;
    return false;
  }
  return true;
}

void setText(Text* self, const char* str) {
  if (!strcmp(str, self->text)) return;

  GpuImage* old = self->origin;
  int oldW = self->width, oldH = self->height;
  char oldText[TEXT_LEN];
  strncpy(oldText, self->text, TEXT_LEN);

  self->origin = NULL;
  if (!initText(self, str, self->color)) {
    self->origin = old;
    self->width  = oldW;
    self->height = oldH;
    strncpy(self->text, oldText, TEXT_LEN);
    return;
  }
  if (old) gpuFree(old);
}

void destroyText(Text* self) {
  if (!self) return;
  gpuFree(self->origin);
  self->origin = NULL;
}
Text* createText(const char* str, SDL_Color color) {
  Text* self = malloc(sizeof(Text));
  if (!initText(self, str, color)) {
    free(self);
    return NULL;
  }
  return self;
}
void initEffect(Effect* self, int duration, int length, SDL_BlendMode mode) {
  assert(length <= EFFECT_MAX_KEYS);
  self->duration = duration;
  self->length = length;
  self->currentFrame = 0;
  self->mode = mode;
}

void copyEffect(const Effect* src, Effect* dest) {
  memcpy(dest, src, sizeof(Effect));
}

void destroyEffect(Effect* self) {
  free(self);
}

void initAnimation(Animation* self, Texture* origin, const Effect* effect,
                   LoopType lp, int duration, int x, int y,
                   SDL_RendererFlip flip, double angle, At at) {
  // will deep copy effect
  self->origin = origin;
  if (effect) {
    self->effect = malloc(sizeof(Effect));
    copyEffect(effect, self->effect);
  } else {
    self->effect = NULL;
  }
  self->lp = lp;
  self->duration = duration;
  self->currentFrame = 0;
  self->x = x;
  self->y = y;
  self->flip = flip;
  self->angle = angle;
  self->at = at;
  self->bind = NULL;
  self->dieWithBind = false;
  self->scaled = true;
  self->lifeSpan = duration;
  self->cropW = 0;
}
Animation* createAnimation(Texture* origin, const Effect* effect, LoopType lp,
                           int duration, int x, int y, SDL_RendererFlip flip,
                           double angle, At at) {
  Animation* self = malloc(sizeof(Animation));
  initAnimation(self, origin, effect, lp, duration, x, y, flip, angle, at);
  return self;
}
void destroyAnimation(Animation* self) {
  destroyEffect(self->effect);
  free(self);
}
void copyAnimation(Animation* src, Animation* dest) {
  memcpy(dest, src, sizeof(Animation));
  if (src->effect) {
    dest->effect = malloc(sizeof(Effect));
    copyEffect(src->effect, dest->effect);
  }
}

void initLinkNode(LinkNode* self) {
  self->nxt = self->pre = self->element = NULL;
}
LinkNode* createLinkNode(void* element) {
  LinkNode* self = malloc(sizeof(LinkNode));
  initLinkNode(self);
  self->element = element;
  return self;
}
void initLinkList(LinkList* self) { self->head = self->tail = NULL; }
LinkList* createLinkList() {
  LinkList* self = malloc(sizeof(LinkList));
  initLinkList(self);
  return self;
}
void pushLinkNodeAtHead(LinkList* list, LinkNode* node) {
  if (list->head == NULL) {
    list->head = list->tail = node;
  } else {
    node->nxt = list->head;
    list->head->pre = node;
    list->head = node;
  }
}
void pushLinkNode(LinkList* list, LinkNode* node) {
  if (list->head == NULL) {
    list->head = list->tail = node;
  } else {
    list->tail->nxt = node;
    node->pre = list->tail;

    list->tail = node;
  }
}
void removeLinkNode(LinkList* list, LinkNode* node) {
  if (node->pre) {
    node->pre->nxt = node->nxt;
  } else {
    list->head = node->nxt;
  }
  if (node->nxt) {
    node->nxt->pre = node->pre;
  } else {
    list->tail = node->pre;
  }
  free(node);
}
void destroyLinkList(LinkList* self) {
  for (LinkNode *p = self->head, *nxt; p; p = nxt) {
    nxt = p->nxt;
    free(p);
  }
  free(self);
}
void destroyAnimationsByLinkList(LinkList* list) {
  for (LinkNode *p = list->head, *nxt; p; p = nxt) {
    nxt = p->nxt;
    destroyAnimation(p->element);
    removeLinkNode(list, p);
  }
}
void removeAnimationFromLinkList(LinkList* self, Animation* ani) {
  for (LinkNode* p = self->head; p; p = p->nxt)
    if (p->element == ani) {
      removeLinkNode(self, p);
      destroyAnimation(ani);
      break;
    }
}
void changeSpriteDirection(LinkNode* self, Direction newDirection) {
  Sprite* sprite = self->element;
  if (sprite->direction == (1 ^ newDirection)) return;
  sprite->direction = newDirection;
  if (newDirection == LEFT || newDirection == RIGHT)
    sprite->face = newDirection;
  if (self->nxt) {
    Sprite* nextSprite = self->nxt->element;
    PositionBufferSlot slot = {sprite->x, sprite->y, sprite->direction};
    pushToPositionBuffer(&nextSprite->posBuffer, slot);
  }
}
void initScore(Score* score) { memset(score, 0, sizeof(Score)); }
Score* createScore() {
  Score* score = malloc(sizeof(Score));
  initScore(score);
  return score;
}
void destroyScore(Score* self) { free(self); }
void calcScore(Score* self) {
  if (!self->got) {
    self->rank = 0;
    return;
  }
  extern int gameLevel;
  self->rank = (double)self->damage / self->got +
               (double)self->stand / self->got + self->got * 50 +
               self->killed * 100;
  self->rank *= gameLevel + 1;
}
void addScore(Score* a, Score* b) {
  a->got += b->got;
  a->damage += b->damage;
  a->killed += b->killed;
  a->stand += b->stand;
  calcScore(a);
}

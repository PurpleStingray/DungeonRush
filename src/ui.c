#include "ui.h"

#include <SDL.h>
#include <SDL_mixer.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "game.h"
#include "helper.h"
#include "map.h"
#include "net.h"
#include "render.h"
#include "res.h"
#ifdef __3DS__
#include "platform/3ds/input_3ds.h"
#include <3ds.h>
#endif
#include "storage.h"
#include "text.h"
#include "types.h"
#include "gpu.h"

extern LinkList animationsList[];
extern bool hasMap[MAP_SIZE][MAP_SIZE];
extern Text texts[TEXTSET_SIZE];
extern SDL_Renderer* renderer;
extern int renderFrames;
extern SDL_Color WHITE;
extern Texture textures[];
extern Effect effects[];

extern LinkList animationsList[ANIMATION_LINK_LIST_NUM];
int cursorPos;
bool moveCursor(int optsNum) {
  SDL_Event e;
  bool quit = false;
#ifdef __3DS__
  pump3DSInput();
#endif
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT) {
      quit = true;
      cursorPos = optsNum;
      return quit;
    } else if (e.type == SDL_KEYDOWN) {
      int keyValue = e.key.keysym.sym;
      switch (keyValue) {
        case SDLK_UP:
          cursorPos--;
          playAudio(AUDIO_INTER1);
          break;
        case SDLK_DOWN:
          cursorPos++;
          playAudio(AUDIO_INTER1);
          break;
        case SDLK_RETURN:
          quit = true;
          break;
        case SDLK_ESCAPE:
          quit = true;
          cursorPos = optsNum;
          playAudio(AUDIO_BUTTON1);
          return quit;
          break;
      }
    }
  }
  cursorPos += optsNum;
  cursorPos %= optsNum;
  return quit;
}
int chooseOptions(int optionsNum, Text** options) {
  cursorPos = 0;
  Snake* player = createSnake(2, 0, LOCAL);
  appendSpriteToSnake(player, SPRITE_KNIGHT, SCREEN_WIDTH / 2,
                      SCREEN_HEIGHT / 2, UP);
  int lineGap = FONT_SIZE + FONT_SIZE / 2,
      totalHeight = lineGap * (optionsNum - 1);
  int startY = (SCREEN_HEIGHT - totalHeight) / 2;
  while (!moveCursor(optionsNum)) {
    Sprite* sprite = player->sprites->head->element;
    sprite->ani->at = AT_CENTER;
    sprite->x = SCREEN_WIDTH / 2 - options[cursorPos]->width / 2 - UNIT / 2;
    sprite->y = startY + cursorPos * lineGap;
    updateAnimationOfSprite(sprite);
    renderUi();
    for (int i = 0; i < optionsNum; i++) {
      renderCenteredText(options[i], SCREEN_WIDTH / 2, startY + i * lineGap, 1);
    }
    // Update Screen
    gpuEndFrame();
    renderFrames++;
  }
  playAudio(AUDIO_BUTTON1);
  destroySnake(player);
  destroyAnimationsByLinkList(&animationsList[RENDER_LIST_SPRITE_ID]);
  return cursorPos;
}
void baseUi(int w, int h) {
  initRenderer();
  pushBottomMenuScene();  // bottom screen: blank room with a few idle enemies
  initBlankMap(w, h);
  pushMapToRender();
}

void launchLocalGame(int localPlayerNum) {
  Score** scores = startGame(localPlayerNum, 0, true);
  rankListUi(localPlayerNum, scores);
  for (int i = 0; i < localPlayerNum; i++) updateLocalRanklist(scores[i]);
  destroyRanklist(localPlayerNum, scores);
}
int rangeOptions(int start, int end) {
  int optsNum = end - start + 1;
  Text** opts = malloc(sizeof(Text*) * optsNum);
  for (int i = 0; i < optsNum; i++) opts[i] = texts + i + start;
  int opt = chooseOptions(optsNum, opts);
  free(opts);
  return opt;
}

char* inputUi() {
  const int MAX_LEN = 30;

  baseUi(20, 10);

  char* ret = malloc(MAX_LEN);
  int retLen = 0;
  memset(ret, 0, MAX_LEN);

  extern SDL_Color WHITE;
  Text* text = NULL;
  Text* placeholder = createText("Enter IP", WHITE);

  SDL_StartTextInput();
  SDL_Event e;
  bool quit = false;
  bool finished = false;
  while (!quit && !finished) {
    const Text* displayText = NULL;
    if (ret[0]) {
      if (text)
        setText(text, ret);
      else
        text = createText(ret, WHITE);
      displayText = text;
    } else {
      displayText = placeholder;
    }
    gpuBeginFrame();
    renderCenteredText(displayText, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 2);
    gpuEndFrame();
    clearRenderer();

    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT ||
          (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
        quit = true;
        break;
      } else if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_BACKSPACE) {
          if (retLen) ret[--retLen] = 0;
        } else if (e.key.keysym.sym == SDLK_RETURN) {
          finished = true;
          break;
        }
      } else if (e.type == SDL_TEXTINPUT) {
        strcpy(ret + retLen, e.text.text);
        retLen += strlen(e.text.text);
      }
    }
  }

  SDL_StopTextInput();
  destroyText(placeholder);
  destroyText(text);

  if (quit) {
    free(ret);
    return NULL;
  }

  return ret;
}

void launchLanGame() {
  baseUi(10, 10);
  int opt = rangeOptions(LAN_HOSTGAME, LAN_JOINGAME);
  blackout();
  clearRenderer();
#ifdef __3DS__
  // hostGame() and joinGame() are both stubs on 3DS (see net.c) that
  // just show a short "not available" message, so there's no point
  // routing "join" through the IP-address text-entry screen below --
  // it needs a physical keyboard's SDL_TEXTINPUT events, which the
  // 3DS doesn't send.
  (void)opt;
  hostGame();
#else
  if (opt == 0) {
    hostGame();
  } else {
    char* ip = inputUi();
    if (ip == NULL) return;
    joinGame(ip, LAN_LISTEN_PORT);
    free(ip);
  }
#endif
}

int chooseOnLanUi() {
  baseUi(10, 10);
  int opt = rangeOptions(MULTIPLAYER_LOCAL, MULTIPLAYER_LAN);
  clearRenderer();
  return opt;
}

/* The knight and chort that stand beside the menu. chooseOptions() clears
   the sprite list when it returns, so these are re-pushed before each menu. */
static void pushMainMenuSprites(void) {
  int spriteY = SCREEN_HEIGHT * 2 / 3;
  createAndPushAnimation(&animationsList[RENDER_LIST_SPRITE_ID],
                         &textures[RES_KNIGHT_M], NULL, LOOP_INFI,
                         SPRITE_ANIMATION_DURATION,
                         SCREEN_WIDTH / 8, spriteY,
                         SDL_FLIP_NONE, 0, AT_BOTTOM_CENTER);
  createAndPushAnimation(&animationsList[RENDER_LIST_SPRITE_ID],
                         &textures[RES_CHORT], NULL, LOOP_INFI,
                         SPRITE_ANIMATION_DURATION,
                         SCREEN_WIDTH * 7 / 8, spriteY,
                         SDL_FLIP_HORIZONTAL, 0, AT_BOTTOM_CENTER);
}

void mainUi() {
  baseUi(23, 13);
  playBgm(0);

  createAndPushAnimation(&animationsList[RENDER_LIST_UI_ID],
                         &textures[RES_TITLE], NULL, LOOP_INFI, 80,
                         SCREEN_WIDTH / 2, SCREEN_HEIGHT / 5,
                         SDL_FLIP_NONE, 0, AT_CENTER);

  const int optsNum = 3;
  Text* mainOpts[3]  = { texts + 6, texts + 8, texts + 9 };    /* 1 Player, Ranklist, Exit */
  Text* levelOpts[3] = { texts + 10, texts + 11, texts + 12 }; /* difficulties */

  int opt, level = -1;
  for (;;) {
    pushMainMenuSprites();
    opt = chooseOptions(optsNum, mainOpts);
    if (opt != 0) break;                 /* Ranklist, Exit, or Esc */

    pushMainMenuSprites();
    level = chooseOptions(optsNum, levelOpts);
    if (level != optsNum) break;         /* a difficulty was picked */
    /* Esc on the difficulty list: loop back to the main options */
  }

  blackout();
  clearRenderer();

  switch (opt) {
    case 0:
      setLevel(level);
      launchLocalGame(1);
      break;
    case 1:
      localRankListUi();
      break;
    case 2:
#ifdef __3DS__
      Mix_CloseAudio();
      SDL_Quit();
      gfxExit();
      hidExit();
      aptExit();
      exit(0);
#else
      exit(0);
#endif
      break;
    default:
      break;
  }

  if (opt == optsNum) return;
  if (opt != 2) {
    mainUi();
  }
}
void rankListUi(int count, Score** scores) {
  baseUi(23, 13 + MAX(0, count - 8));
  playBgm(0);
  Text** opts = malloc(sizeof(Text*) * count);
  char buf[1 << 8];
  for (int i = 0; i < count; i++) {
    sprintf(buf, "Score: %.0f  Got: %d  Kill: %d  Dmg: %d  Stand: %d",
        scores[i]->rank, scores[i]->got, scores[i]->killed,
        scores[i]->damage, scores[i]->stand);
    opts[i] = createText(buf, WHITE);
  }

  chooseOptions(count, opts);

  for (int i = 0; i < count; i++) destroyText(opts[i]);
  free(opts);
  blackout();
  clearRenderer();
}
void localRankListUi() {
  int count;
  Score** scores = readRanklist(STORAGE_PATH, &count);
  rankListUi(count, scores);
  destroyRanklist(count, scores);
}

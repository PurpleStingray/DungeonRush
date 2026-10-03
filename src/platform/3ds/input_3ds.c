#ifdef __3DS__

#include "input_3ds.h"

#include <3ds.h>
#include <SDL.h>

// Solo local play always drives player id 0, which reads the arrow
// keys (see arrowsToDirection() in game.c) rather than WASD (that's
// player id 1, used for local 2-player on a shared keyboard, which
// doesn't apply on a handheld). So both the D-Pad and the Circle Pad
// are mapped to the arrow keys here, giving the player a choice of
// input without needing to touch game.c at all.
//
// Movement uses hidKeysDown() (edge-triggered: true only on the frame
// a button transitions from up to down) rather than hidKeysHeld(),
// matching the original desktop build's behavior of reacting to
// SDL_KEYDOWN once per physical keypress rather than flooding a
// direction change every frame a key is held.

static void pushKey(SDL_Keycode sym) {
  SDL_Event event;
  SDL_zero(event);
  event.type = SDL_KEYDOWN;
  event.key.type = SDL_KEYDOWN;
  event.key.state = SDL_PRESSED;
  event.key.repeat = 0;
  event.key.keysym.sym = sym;
  SDL_PushEvent(&event);
}

void pump3DSInput() {
  hidScanInput();
  u32 down = hidKeysDown();

  if (down & (KEY_DUP | KEY_CPAD_UP)) pushKey(SDLK_UP);
  if (down & (KEY_DDOWN | KEY_CPAD_DOWN)) pushKey(SDLK_DOWN);
  if (down & (KEY_DLEFT | KEY_CPAD_LEFT)) pushKey(SDLK_LEFT);
  if (down & (KEY_DRIGHT | KEY_CPAD_RIGHT)) pushKey(SDLK_RIGHT);

  // A confirms menu selections (mirrors desktop Return/Enter).
  if (down & KEY_A) pushKey(SDLK_RETURN);
  // B and Start both back out of a menu / pause the game (mirrors
  // desktop Escape).
  if (down & (KEY_B | KEY_START)) pushKey(SDLK_ESCAPE);
}

#endif  // __3DS__

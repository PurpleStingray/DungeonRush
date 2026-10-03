#ifndef SNAKE_PLATFORM_3DS_INPUT_H_
#define SNAKE_PLATFORM_3DS_INPUT_H_
#ifdef __3DS__

// Scans the 3DS's physical buttons/pad and pushes equivalent SDL
// keyboard events onto SDL's event queue. The rest of the codebase
// only knows about SDL_KEYDOWN/SDLK_* (see game.c's
// handleLocalKeypress()/arrowsToDirection(), and ui.c's menu
// navigation), so it needs no changes at all to react to 3DS input.
//
// Call this once per frame, immediately before any SDL_PollEvent
// loop that should see 3DS button presses.
void pump3DSInput();

#endif  // __3DS__
#endif  // SNAKE_PLATFORM_3DS_INPUT_H_

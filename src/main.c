#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "gpu.h"
#include "res.h"
#include "game.h"
#include "ui.h"
#include "prng.h"

#ifdef DBG
#include <assert.h>
#endif

#ifdef __3DS__
#include <3ds.h>
#endif

#include "profiler.h"



int main(int argc, char** args) {
  prngSrand(time(NULL));
  profInit();
#ifdef __3DS__
  romfsInit();
#endif
  if (!init()) {
    fprintf(stderr, "Failed to initialize!\n");
  } else {
    if (!loadMedia()) {
      fprintf(stderr, "Failed to load media!\n");
    } else {
      if (!gpuInit()) {
        fprintf(stderr, "Failed to init GPU!\n");
      } else {
        // validateAssets();
        mainUi();
        gpuExit();
      }
    }
  }
  cleanup();
#ifdef __3DS__
  romfsExit();
#endif
  return 0;
}

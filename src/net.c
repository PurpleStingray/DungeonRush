#include "net.h"
#include "types.h"
#include "render.h"
#include "res.h"
#include "game.h"
#include "storage.h"
#include "prng.h"

#include <string.h>
#include <stdlib.h>

TCPsocket lanServerSocket;
TCPsocket lanClientSocket;

#ifdef __3DS__
#include "platform/3ds/input_3ds.h"

// LAN multiplayer needs SDL2_net, which isn't available in devkitPro's
// 3ds-portlibs, so it's disabled here rather than half-implemented.
// lanClientSocket stays NULL, so every "if (lanClientSocket)" guard
// elsewhere in the codebase (game.c's handleLanKeypress dispatch,
// sendPlayerMovePacket/sendGameOverPacket's early-outs) is already a
// correct no-op without touching those files.
static void showUnavailableMessage() {
  extern SDL_Color WHITE;
  Text* msg1 = createText("LAN play isn't available", WHITE);
  Text* msg2 = createText("on this 3DS build (B to go back)", WHITE);
  clearRenderer();
  renderCenteredText(msg1, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 12, 1);
  renderCenteredText(msg2, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 12, 1);
  SDL_RenderPresent(renderer);

  bool quit = false;
  SDL_Event e;
  while (!quit) {
    pump3DSInput();
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT ||
          (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_ESCAPE ||
                                      e.key.keysym.sym == SDLK_RETURN))) {
        quit = true;
      }
    }
  }
  destroyText(msg1);
  destroyText(msg2);
  blackout();
}

void hostGame() { showUnavailableMessage(); }
void joinGame(const char* hostname, Uint16 port) {
  (void)hostname;
  (void)port;
  showUnavailableMessage();
}
void sendPlayerMovePacket(unsigned playerId, unsigned direction) {
  (void)playerId;
  (void)direction;
}
void sendGameOverPacket(unsigned playerId) { (void)playerId; }
unsigned recvLanPacket(LanPacket* dest) {
  (void)dest;
  return 0;
}

#else  // !__3DS__

#include <SDL_net.h>

#define CASSERT(EXPRESSION) switch (0) {case 0: case (EXPRESSION):;}

SDLNet_SocketSet socketSet;

static void CHECK() {
  CASSERT(sizeof(LanPacket) == 4);
}

HandShakePacket createHandShakePacket() {
  HandShakePacket handshakePacket;
  memset(&handshakePacket, 0, sizeof(handshakePacket));
  handshakePacket.seed = (prngRand()*prngRand())&LAN_SEED_MASK;
  return handshakePacket;
}

static inline unsigned packet2unsigned(void* packet) {
  return *((unsigned*)packet);
}

static inline void fillPacket(void* packet, unsigned payload) {
  *((unsigned*)packet) = payload;
}

void sendPlayerMovePacket(unsigned playerId, unsigned direction) {
  if (!lanClientSocket) return;
  static PlayerMovePacket p;
  p.type = 1;
  p.playerId = playerId;
  p.direction = direction;
  static char buffer[4];
  SDLNet_Write32(packet2unsigned(&p), buffer);
  SDLNet_TCP_Send(lanClientSocket, buffer, sizeof(PlayerMovePacket));
}

unsigned recvLanPacket(LanPacket* dest) {
  if (SDLNet_CheckSockets(socketSet, 0)) {
    static char buffer[sizeof(LanPacket)];
    SDLNet_TCP_Recv(lanClientSocket, buffer, sizeof(LanPacket));
    fillPacket(dest, SDLNet_Read32(buffer));
    return 1;
  }
  else return 0;
}

void sendGameOverPacket(unsigned playerId) {
  if (!lanClientSocket) return;
  static GameOverPacket p;
  p.type = 2;
  p.playerId = playerId;
  static char buffer[4];
  SDLNet_Write32(packet2unsigned(&p), buffer);
  SDLNet_TCP_Send(lanClientSocket, buffer, sizeof(GameOverPacket));
}

void hostGame() {
  IPaddress listenIp;
  SDLNet_ResolveHost(&listenIp, INADDR_ANY, LAN_LISTEN_PORT);
  lanServerSocket = SDLNet_TCP_Open(&listenIp);
  if (!lanServerSocket) {
    fprintf(stderr, "hostGame: %s\n", SDLNet_GetError());
    exit(-1);
  }

  extern SDL_Color WHITE;
  Text* listening[4] = {
    createText("Listening", WHITE),
    createText("Listening.", WHITE),
    createText("Listening..", WHITE),
    createText("Listening...", WHITE),
    };
  
  SDL_Event e;
  unsigned frameCount = 0;
  while (!lanClientSocket) {
    lanClientSocket = SDLNet_TCP_Accept(lanServerSocket);
    clearRenderer();
    renderCenteredText(listening[(frameCount++)/20%4], SCREEN_WIDTH/2, SCREEN_HEIGHT/2, 2);
    SDL_RenderPresent(renderer);
    bool quit = false;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT || 
          (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
        quit = true;
      }
    }
    if (quit) break;
  }

  // Clean up after connecting
  SDLNet_TCP_Close(lanServerSocket);
  lanServerSocket = NULL;
  for (unsigned i = 0; i < 4; i++) destroyText(listening[i]);

  blackout();

  if (!lanClientSocket) return;

  // Add socket to set
  socketSet = SDLNet_AllocSocketSet(1);
  SDLNet_TCP_AddSocket(socketSet, lanClientSocket);

  // Show the peer ip
  IPaddress* peerIp = SDLNet_TCP_GetPeerAddress(lanClientSocket);

  const char* peerName = SDLNet_ResolveIP(peerIp);
  Text* peerNameText = createText(peerName, WHITE);
  clearRenderer();
  renderCenteredText(peerNameText, SCREEN_WIDTH/2, SCREEN_HEIGHT/2, 2);
  SDL_RenderPresent(renderer);
  destroyText(peerNameText);

  HandShakePacket handShakePacket = createHandShakePacket();
  char buffer[sizeof(HandShakePacket)];
  SDLNet_Write32(packet2unsigned(&handShakePacket), buffer);
  SDLNet_TCP_Send(lanClientSocket, buffer, sizeof(HandShakePacket));

  unsigned seed = handShakePacket.seed;
  fprintf(stderr, "seed: %d\n", seed);
  prngSrand(seed);

  setLevel(0);
  Score** score = startGame(1, 1, true);
  destroyRanklist(1, score);

  SDLNet_FreeSocketSet(socketSet);
  socketSet = NULL;
  SDLNet_TCP_Close(lanClientSocket);
  lanClientSocket = NULL;
}

void joinGame(const char* hostname, Uint16 port) {
  IPaddress serverIp;
  if (SDLNet_ResolveHost(&serverIp, hostname, port)) {
    fprintf(stderr, "joinGame: ResolveHost: %s\n", SDLNet_GetError());
    exit(-1);
  }

  extern SDL_Color WHITE;
  Text* connecting[4] = {
    createText("Connecting", WHITE),
    createText("Connecting.", WHITE),
    createText("Connecting..", WHITE),
    createText("Connecting...", WHITE),
    };
  
  bool quit = false;
  SDL_Event e;
  unsigned frameCount = 0;
  while (!lanClientSocket) {
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT || 
          (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
        quit = true;
      }
    }
    if (quit) break;

    lanClientSocket = SDLNet_TCP_Open(&serverIp);
    if (lanClientSocket == NULL) {
      fprintf(stderr, "connect: %s\n", SDL_GetError());
    }
    clearRenderer();
    renderCenteredText(connecting[(frameCount++)/20%4], SCREEN_WIDTH/2, SCREEN_HEIGHT/2, 2);
    SDL_RenderPresent(renderer);
  }

  for (unsigned i = 0; i < 4; i++) destroyText(connecting[i]);

  if (!quit) {
    // Add socket to set
    socketSet = SDLNet_AllocSocketSet(1);
    SDLNet_TCP_AddSocket(socketSet, lanClientSocket);

    // Sync seed

    char buffer[sizeof(HandShakePacket)];
    SDLNet_TCP_Recv(lanClientSocket, buffer, sizeof(HandShakePacket));
    HandShakePacket handShakePacket;
    fillPacket(&handShakePacket, SDLNet_Read32(buffer));

    unsigned seed = handShakePacket.seed;
    prngSrand(seed);
    fprintf(stderr, "seed: %d\n", seed);

    // Start game
    setLevel(0);
    startGame(1, 1, false);
  }

  if (socketSet) SDLNet_FreeSocketSet(socketSet);
  socketSet = NULL;

  if (lanClientSocket) SDLNet_TCP_Close(lanClientSocket);
  lanClientSocket = NULL;
}

#endif  // __3DS__

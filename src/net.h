#ifndef SNAKE__NET_H_
#define SNAKE__NET_H_

#ifdef __3DS__
// SDL2_net isn't part of devkitPro's 3ds-portlibs, so LAN multiplayer
// is stubbed out on 3DS (see net.c). TCPsocket becomes an unused
// pointer type so lanClientSocket/lanServerSocket still exist as
// always-NULL globals and every existing "if (lanClientSocket)" check
// elsewhere in the codebase keeps compiling unchanged.
typedef void* TCPsocket;
typedef struct { unsigned dummy; } IPaddress;
#else
#include <SDL_net.h>
#endif

#define LAN_LISTEN_PORT 21739
#define LAN_SEED_MASK 0x00ffffff

// Play on lan general packet should be 4 byte ( 32 bit )

typedef enum {
  HEADER_HANDSHAKE,
  HEADER_PLAYERMOVE,
  HEADER_GAMEOVER
} HeaderType;

// Packet header, 8 bit
/*
typedef struct {
  unsigned version: 2;
  unsigned type: 6;
} LanPacketHeader;
*/

typedef struct {
  unsigned version: 2;
  unsigned type: 6;

  unsigned payload: 24;
} LanPacket;

typedef struct {
  unsigned version: 2;
  unsigned type: 6;

  unsigned seed: 24; // random seed to be used in the turn
} HandShakePacket;

typedef struct {
  unsigned version: 2;
  unsigned type: 6;

  unsigned playerId: 3;
  unsigned direction: 2;
  unsigned padding: 19;
} PlayerMovePacket;

typedef struct {
  unsigned version: 2;
  unsigned type: 6;

  unsigned playerId: 3;
  unsigned padding: 21;
} GameOverPacket;

extern TCPsocket lanServerSocket;
extern TCPsocket lanClientSocket;

void hostGame();
void joinGame(const char* hostname, Uint16 port);
void sendPlayerMovePacket(unsigned playerId, unsigned direction);
void sendGameOverPacket(unsigned playerId);
unsigned recvLanPacket(LanPacket* dest);
#endif

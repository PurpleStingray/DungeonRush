#ifndef SNAKE_RES_H_
#define SNAKE_RES_H_

#include <SDL.h>
#include "types.h"
#include "gpu.h"

#define VERSION_STRING "v1.1(beta)"
#define PATH_LEN 1024
#define TILESET_SIZE 1024
#define TEXTSET_SIZE 1024
#define TEXTURES_SIZE 1024
#define EFFECTS_SIZE 128
#ifdef __3DS__
// Native art tiles are 16x16. On desktop these are drawn at 2x into a
// 1440x960 window. The 3DS top screen is a fixed 400x240, so we draw
// pixel art at 1x and let UNIT shrink to match (25x15 visible tiles).
#define UNIT 16
#define SCREEN_WIDTH 400
#define SCREEN_HEIGHT 240
// The playable world is the full desktop map: 45x30 tiles. The screen is
// a viewport into it; in-game, the camera follows the player.
#define WORLD_WIDTH  (45 * UNIT)
#define WORLD_HEIGHT (30 * UNIT)
#define WORLD_CAMERA 1
// RomFS assets are addressed as "romfs:/res/..."
#define RES_PREFIX "romfs:/"
#else
#define UNIT 32
#define SCREEN_WIDTH 1440
#define SCREEN_HEIGHT 960
// Desktop: the world is exactly one screen, so there is no camera.
#define WORLD_WIDTH  SCREEN_WIDTH
#define WORLD_HEIGHT SCREEN_HEIGHT
#define RES_PREFIX ""
#endif
#define FONT_SIZE 12
// Resource ID
// Map Resource
#define RES_WALL_TOP_LEFT 0
#define RES_WALL_TOP_MID 1
#define RES_WALL_TOP_RIGHT 2
#define RES_WALL_MID 4
#define RES_WALL_HOLE_1 11 
#define RES_WALL_HOLE_2 12 
#define RES_WALL_SIDE_TOP_LEFT 35
#define RES_WALL_SIDE_TOP_RIGHT 36
#define RES_WALL_SIDE_MID_LEFT 37
#define RES_WALL_SIDE_MID_RIGHT 38
#define RES_WALL_SIDE_FRONT_LEFT 39
#define RES_WALL_SIDE_FRONT_RIGHT 40
#define RES_WALL_CORNER_TOP_LEFT 41
#define RES_WALL_CORNER_TOP_RIGHT 42
#define RES_WALL_CORNER_LEFT 43
#define RES_WALL_CORNER_RIGHT 44
#define RES_WALL_CORNER_BOTTOM_LEFT 45
#define RES_WALL_CORNER_BOTTOM_RIGHT 46
#define RES_WALL_CORNER_FRONT_LEFT 47
#define RES_WALL_CORNER_FRONT_RIGHT 48
#define RES_WALL_INNER_CORNER_MID_LEFT 51
#define RES_WALL_INNER_CORNER_MID_RIGHT 52
#define RES_WALL_INNER_CORNER_T_TOP_LEFT 53
#define RES_WALL_INNER_CORNER_T_TOP_RIGHT 54
#define RES_WALL_BANNER_RED 13
#define RES_FLOOR_1 25
#define RES_FLOOR_2 26
#define RES_FLOOR_3 27
#define RES_FLOOR_4 28
#define RES_FLOOR_5 29
#define RES_FLOOR_6 30
#define RES_FLOOR_7 31
#define RES_FLOOR_8 32
#define RES_FLASK_RED 70
#define RES_FLASK_BIG_RED 66
#define RES_FLASK_BIG_BLUE 67
#define RES_FLASK_BIG_GREEN 68
#define RES_FLASK_BIG_YELLOW 69
#define RES_SKULL 74
#define RES_BLOOD1 179
#define RES_BLOOD4 182
#define RES_TINY_ZOMBIE 102
#define RES_GOBLIN 104
#define RES_IMP 106
#define RES_SKELET 108
#define RES_MUDDY 110
#define RES_SWAMPY 112
#define RES_ZOMBIE 114
#define RES_ICE_ZOMBIE 116
#define RES_MASKED_ORC 118
#define RES_ORC_WARRIOR 120
#define RES_ORC_SHAMAN 122
#define RES_NECROMANCER 124
#define RES_WOGOL 126
#define RES_CHORT 128
#define RES_BIG_ZOMBIE 130
#define RES_ORGRE 132
#define RES_BIG_DEMON 134
#define RES_ELF_F 136
#define RES_ELF_M 139
#define RES_KNIGHT_M 145
#define RES_WIZZARD_M 151
#define RES_LIZARD_M 157
#define RES_FLOOR_SPIKE_DISABLED 163
#define RES_FLOOR_SPIKE_ENABLED 164
#define RES_FLOOR_SPIKE_OUT_ANI 165
#define RES_FLOOR_SPIKE_IN_ANI 166
#define RES_FLOOR_EXIT 167
#define RES_HALO_EXPLOSION1 160
#define RES_HALO_EXPLOSION2 161
#define RES_FIREBALL 162
#define RES_HP_MED 168
#define RES_SwordFx 169
#define RES_CLAWFX 170
#define RES_Shine 171
#define RES_Thunder 172
#define RES_BLOOD_BOUND 173
#define RES_ARROW 174
#define RES_EXPOLSTION2 175
#define RES_CLAWFX2 176
#define RES_AXE 177
#define RES_CROSS_HIT 178
#define RES_SOLIDFX 183
#define RES_SOLID_GREENFX 184
#define RES_ICEPICK 185
#define RES_ICESHATTER 186
#define RES_ICE 187
#define RES_HOLY_SWORD 188
#define RES_FIRE_SWORD 189
#define RES_ICE_SWORD 190
#define RES_GRASS_SWORD 191
#define RES_IRON_SWORD 192
#define RES_HOLY_SHIELD 193
#define RES_GOLDEN_CROSS_HIT 194
#define RES_SLIDER 195
#define RES_BAR_BLUE 196
#define RES_TITLE 197
#define RES_PURPLE_BALL 198
#define RES_PURPLE_EXP 199
#define RES_PURPLE_STAFF 200
#define RES_THUNDER_STAFF 201
#define RES_THUNDER_YELLOW 202
#define RES_ATTACK_UP 203
#define RES_POWERFUL_BOW 204
// Effect
#define EFFECT_DEATH 0
#define EFFECT_BLINK 1
#define EFFECT_VANISH30 2
// Sprite
#define COMMON_SPRITE_SIZE 1024
#define SPRITE_KNIGHT 0
#define SPRITE_ELF 1
#define SPRITE_WIZZARD 2
#define SPRITE_LIZARD 3
#define SPRITE_TINY_ZOMBIE 4
#define SPRITE_GOBLIN 5
#define SPRITE_IMP 6
#define SPRITE_SKELET 7
#define SPRITE_MUDDY 8
#define SPRITE_SWAMPY 9
#define SPRITE_ZOMBIE 10
#define SPRITE_ICE_ZOMBIE 11
#define SPRITE_MASKED_ORC 12
#define SPRITE_ORC_WARRIOR 13
#define SPRITE_ORC_SHAMAN 14
#define SPRITE_NECROMANCER 15
#define SPRITE_WOGOL 16
#define SPRITE_CHROT 17
#define SPRITE_BIG_ZOMBIE 18
#define SPRITE_ORGRE 19
#define SPRITE_BIG_DEMON 20

/* Effect art with scaled == false is authored at desktop pixel size.
   It is drawn at this fraction of native size. */
#ifdef __3DS__
#define FX_SCALE_NUM 1
#define FX_SCALE_DEN 2
#else
#define FX_SCALE_NUM 1
#define FX_SCALE_DEN 1
#endif

// Audio
#define AUDIO_BGM_SIZE (1<<4)
#define AUDIO_SOUND_SIZE (1<<8)
#define AUDIO_WIN 0
#define AUDIO_LOSE 1
#define AUDIO_POWERLOSS 2
#define AUDIO_HIT 3
#define AUDIO_SWORD_HIT 4
#define AUDIO_CLAW_HIT 5
#define AUDIO_ARROW_HIT 6
#define AUDIO_SHOOT 7
#define AUDIO_FIREBALL_EXP 8
#define AUDIO_ICE_SHOOT 9
#define AUDIO_INTER1 10
#define AUDIO_BUTTON1 11
#define AUDIO_THUNDER 12
#define AUDIO_LIGHT_SHOOT 13
#define AUDIO_HUMAN_DEATH 14
#define AUDIO_CLAW_HIT_HEAVY 15
#define AUDIO_COIN 16
#define AUDIO_MED 17

bool init();
void cleanup();
bool loadMedia();
SDL_Texture* loadSDLTexture(const char* path);
bool loadTileset(const char* path, GpuImage* origin);
bool loadTextset();
void initCommonEffects();
void initCommonAnimations();
void initCommonSprites();

extern SDL_Renderer* renderer;
#endif

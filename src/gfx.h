// The renderer and the Dot UI (ui/dot) the screens draw with, plus the few plain drawing calls the demo effects
// use: they draw in white and grey on black, the Night scheme of Dot.
#pragma once
#include <SDL2/SDL.h>
#include "dot.h"

enum { W_LIGHT, W_REG, W_MED };
enum { A_LEFT, A_RIGHT, A_CENTER };

extern SDL_Renderer *R;
extern Dot *D;
extern const SDL_Color C_BG, C_INK, C_RULE, C_MUTE, C_ACC;   // black, text white, unlit grey, label grey, red

int  gfx_init(SDL_Renderer *ren, const char *pak_dir);        // fonts from <pak>/fonts
// Draws s with its baseline at y, in a Dot style by size: 70+ Doto dot-matrix, 30+ Geist Mono, else Geist.
int  text(int x, int y, const char *s, int size, int weight, SDL_Color c, int align);
int  text_w(const char *s, int size, int weight);
void fill_rect(int x, int y, int w, int h, SDL_Color c);
void double_dash(int x1, int x2, int y, SDL_Color c);         // a row of unlit dots, like Dot's progress track

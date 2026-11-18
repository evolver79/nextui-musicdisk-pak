// Drawing for the Line look: sage paper, ink, DM Mono in three weights, thin rules.
#pragma once
#include <SDL2/SDL.h>

enum { W_LIGHT, W_REG, W_MED };
enum { A_LEFT, A_RIGHT, A_CENTER };

extern SDL_Renderer *R;
extern const SDL_Color C_BG, C_INK, C_MUTE, C_RULE;

int  gfx_init(SDL_Renderer *ren, const char *pak_dir);
// Draws s with its baseline at y. Returns the width drawn.
int  text(int x, int y, const char *s, int size, int weight, SDL_Color c, int align);
int  text_w(const char *s, int size, int weight);
// Copies s into out, cut with "..." so it fits in max_w pixels. from_left keeps the end of the string instead.
void fit(char *out, size_t n, const char *s, int max_w, int size, int weight, int from_left);
void fill_rect(int x, int y, int w, int h, SDL_Color c);
void rule(int x1, int x2, int y, SDL_Color c);
void gfx_clear(void);

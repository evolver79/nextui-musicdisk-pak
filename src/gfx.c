#include "gfx.h"
#include <stdio.h>

SDL_Renderer *R;
Dot *D;
const SDL_Color C_BG = { 0, 0, 0, 255 }, C_INK = { 0xf2, 0xf2, 0xf0, 255 }, C_RULE = { 0x44, 0x44, 0x44, 255 },
                C_MUTE = { 0x8d, 0x8d, 0x8a, 255 }, C_ACC = { 0xff, 0x3b, 0x30, 255 };

int gfx_init(SDL_Renderer *ren, const char *pak_dir) {
  R = ren;
  char p[800]; snprintf(p, sizeof p, "%s/fonts", pak_dir);
  if (!(D = dot_init(ren, p, DOT_ACCENT_MUSICDISK))) { fprintf(stderr, "fonts in %s: %s\n", p, SDL_GetError()); return -1; }
  return 0;
}

// The demo spaces letters on a fixed grid, so the ticker is monospaced (Geist Mono) and the big scroller Doto.
static int style_for(int size) { return size >= 70 ? DOT_DISPLAY_L : size >= 30 ? DOT_VALUE_L : DOT_BODY; }
static Uint32 rgb(SDL_Color c) { return (Uint32)c.r << 16 | (Uint32)c.g << 8 | c.b; }

int text(int x, int y, const char *s, int size, int weight, SDL_Color c, int align) {
  (void)weight;
  int st = style_for(size), lh = dot_line_h(D, st);
  return dot_text(D, st, s, x, y - lh * 4 / 5, rgb(c), align == A_RIGHT ? DOT_RIGHT : align == A_CENTER ? DOT_CENTER : DOT_LEFT, 0);
}

int text_w(const char *s, int size, int weight) { (void)weight; return dot_text_w(D, style_for(size), s); }

void fill_rect(int x, int y, int w, int h, SDL_Color c) {
  SDL_Rect r = { x, y, w, h };
  SDL_SetRenderDrawColor(R, c.r, c.g, c.b, 255);
  SDL_RenderFillRect(R, &r);
}

void double_dash(int x1, int x2, int y, SDL_Color c) {
  for (int x = x1 + DOT_DOT_PITCH / 2; x < x2; x += DOT_DOT_PITCH) dot_circle(D, x, y, DOT_DOT_R, rgb(c));
}

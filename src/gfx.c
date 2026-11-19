#include "gfx.h"
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <string.h>

SDL_Renderer *R;
const SDL_Color C_BG = { 0xf1, 0xed, 0xe3, 255 }, C_INK = { 0x22, 0x22, 0x20, 255 },
                C_MUTE = { 0x66, 0x65, 0x60, 255 }, C_RULE = { 0xb9, 0xb5, 0xab, 255 },
                C_ACC = { 0x2b, 0xf0, 0x2b, 255 }, C_SURF = { 0xd2, 0xce, 0xc4, 255 };

// Departure Mono is drawn on an 11 px grid, so every size is snapped to a multiple of 11 (22, 33, 44 ... 88).
static char font_path[700];
#define GRID 11
#define NSIZES 9
static TTF_Font *fonts[NSIZES];
static SDL_Texture *checker;

static int snap(int size) { int k = (size + GRID / 2) / GRID; if (k < 2) k = 2; if (k > NSIZES - 1) k = NSIZES - 1; return k; }

static TTF_Font *font(int size, int weight) {
  (void)weight;
  int k = snap(size);
  if (!fonts[k]) { fonts[k] = TTF_OpenFont(font_path, k * GRID); if (fonts[k]) TTF_SetFontHinting(fonts[k], TTF_HINTING_MONO); }
  return fonts[k];
}

int gfx_init(SDL_Renderer *ren, const char *pak_dir) {
  R = ren;
  if (TTF_Init() != 0) return -1;
  snprintf(font_path, sizeof font_path, "%s/DepartureMono-Regular.otf", pak_dir);
  if (!font(22, W_REG)) { fprintf(stderr, "font %s: %s\n", font_path, TTF_GetError()); return -1; }
  // a 64x64 checkerboard of 2 px cells, white on clear, tinted when drawn
  static Uint32 px[64 * 64];
  for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) px[y * 64 + x] = ((x / 2 + y / 2) & 1) ? 0xffffffffu : 0;
  checker = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 64, 64);
  if (checker) { SDL_UpdateTexture(checker, NULL, px, 64 * 4); SDL_SetTextureBlendMode(checker, SDL_BLENDMODE_BLEND); }
  return 0;
}

// Rendered strings are kept as textures; the least recently used one is replaced when the cache is full.
typedef struct { Uint32 hash, used; int size, weight, w, h; SDL_Color c; char s[128]; SDL_Texture *t; } Cached;
#define NCACHE 384
static Cached cache[NCACHE];
static Uint32 clock_n;

static Uint32 hash_of(const char *s, int size, int weight, SDL_Color c) {
  Uint32 h = 2166136261u;
  for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
  return h ^ (Uint32)(size * 131 + weight * 7) ^ ((Uint32)c.r << 16 | c.g << 8 | c.b);
}

static Cached *get(const char *s, int size, int weight, SDL_Color c) {
  Uint32 h = hash_of(s, size, weight, c);
  Cached *old = &cache[0];
  for (int i = 0; i < NCACHE; i++) {
    Cached *e = &cache[i];
    if (e->t && e->hash == h && e->size == size && e->weight == weight && !memcmp(&e->c, &c, sizeof c) && !strcmp(e->s, s)) { e->used = ++clock_n; return e; }
    if (!e->t || e->used < old->used) old = e;
  }
  TTF_Font *f = font(size, weight);
  if (!f) return NULL;
  SDL_Surface *surf = TTF_RenderUTF8_Blended(f, s, c);
  if (!surf) return NULL;
  if (old->t) SDL_DestroyTexture(old->t);
  old->t = SDL_CreateTextureFromSurface(R, surf);
  old->w = surf->w; old->h = surf->h;
  SDL_FreeSurface(surf);
  old->hash = h; old->size = size; old->weight = weight; old->c = c; old->used = ++clock_n;
  snprintf(old->s, sizeof old->s, "%s", s);
  return old;
}

int text_w(const char *s, int size, int weight) {
  int w = 0;
  if (!*s) return 0;
  TTF_SizeUTF8(font(size, weight), s, &w, NULL);
  return w;
}

int text(int x, int y, const char *s, int size, int weight, SDL_Color c, int align) {
  if (!s || !*s) return 0;
  Cached *e = get(s, size, weight, c);
  if (!e) return 0;
  int dx = align == A_RIGHT ? x - e->w : align == A_CENTER ? x - e->w / 2 : x;
  SDL_Rect d = { dx, y - TTF_FontAscent(font(size, weight)), e->w, e->h };
  SDL_RenderCopy(R, e->t, NULL, &d);
  if (weight == W_MED) { d.x += snap(size); SDL_RenderCopy(R, e->t, NULL, &d); }
  return e->w;
}

void fit(char *out, size_t n, const char *s, int max_w, int size, int weight, int from_left) {
  snprintf(out, n, "%s", s);
  if (text_w(out, size, weight) <= max_w) return;
  size_t len = strlen(s);
  for (size_t keep = len; keep > 0; keep--) {
    if (from_left) snprintf(out, n, "...%s", s + (len - keep));
    else snprintf(out, n, "%.*s...", (int)keep, s);
    if (from_left && (s[len - keep] & 0xC0) == 0x80) continue;   // don't start inside a UTF-8 character
    if (!from_left && (s[keep] & 0xC0) == 0x80) continue;
    if (text_w(out, size, weight) <= max_w) return;
  }
}

void fill_rect(int x, int y, int w, int h, SDL_Color c) {
  SDL_SetRenderDrawColor(R, c.r, c.g, c.b, 255);
  SDL_Rect r = { x, y, w, h };
  SDL_RenderFillRect(R, &r);
}

void rule(int x1, int x2, int y, SDL_Color c) { fill_rect(x1, y, x2 - x1, 2, c); }

void frame(int x, int y, int w, int h, SDL_Color c) {
  fill_rect(x, y, w, 2, c); fill_rect(x, y + h - 2, w, 2, c);
  fill_rect(x, y, 2, h, c); fill_rect(x + w - 2, y, 2, h, c);
}

void dither(int x, int y, int w, int h, SDL_Color c) {
  if (!checker || w <= 0 || h <= 0) return;
  SDL_SetTextureColorMod(checker, c.r, c.g, c.b);
  for (int ty = 0; ty < h; ty += 64) for (int tx = 0; tx < w; tx += 64) {
    int cw = w - tx < 64 ? w - tx : 64, ch = h - ty < 64 ? h - ty : 64;
    // keep the pattern fixed to the screen, so neighbouring dithers line up
    SDL_Rect s = { (x + tx) & 3, (y + ty) & 3, cw, ch };
    if (s.x + cw > 64) s.x = 0;
    if (s.y + ch > 64) s.y = 0;
    SDL_Rect d = { x + tx, y + ty, cw, ch };
    SDL_RenderCopy(R, checker, &s, &d);
  }
}

void dotted(int x1, int x2, int y, SDL_Color c) { for (int x = x1; x + 2 <= x2; x += 8) fill_rect(x, y, 2, 2, c); }

void double_dash(int x1, int x2, int y, SDL_Color c) {
  for (int x = x1; x < x2; x += 10) { int w = x2 - x < 6 ? x2 - x : 6; fill_rect(x, y, w, 2, c); fill_rect(x, y + 5, w, 2, c); }
}

void panel(int x, int y, int w, int h, SDL_Color fill) {
  dither(x + 8, y + 8, w, h, C_MUTE);
  fill_rect(x, y, w, h, fill);
  frame(x, y, w, h, C_INK);
}

int window(int x, int y, int w, int h, const char *title, int lit) {
  panel(x, y, w, h, C_BG);
  int bar = 40;
  fill_rect(x + 2, y + 2, w - 4, bar - 2, lit ? C_ACC : C_SURF);
  if (!lit) dither(x + w / 2, y + 2, w / 2 - 2, bar - 2, C_RULE);
  fill_rect(x, y + bar, w, 2, C_INK);
  if (title && *title) text(x + 14, y + 30, title, 22, W_REG, C_INK, A_LEFT);
  return y + bar + 2;
}

int tab(int x, int y, const char *label, int on) {
  char s[96]; snprintf(s, sizeof s, "[ %s ]", label);
  int w = text_w(s, 22, W_REG) + 24, h = 40;
  if (on) fill_rect(x, y, w, h, C_ACC);
  frame(x, y, w, h, on ? C_INK : C_MUTE);
  text(x + 12, y + 29, s, 22, W_REG, on ? C_INK : C_MUTE, A_LEFT);
  return w;
}

void heading(int x, int y, int x2, const char *s, const char *right, int size, SDL_Color c) {
  int w = text(x, y, s, size, W_REG, c, A_LEFT);
  int rw = right && *right ? text(x2, y, right, size, W_REG, c, A_RIGHT) : 0;
  dotted(x + w + 16, x2 - rw - (rw ? 14 : 0), y - 4, c);
}

// Ruler ticks round the edge of the screen, long every fifth.
static void ruler(void) {
  for (int x = 8; x < 1024; x += 16) { int l = (x / 16) % 5 == 0 ? 12 : 6; fill_rect(x, 0, 2, l, C_RULE); fill_rect(x, 768 - l, 2, l, C_RULE); }
  for (int y = 8; y < 768; y += 16) { int l = (y / 16) % 5 == 0 ? 12 : 6; fill_rect(0, y, l, 2, C_RULE); fill_rect(1024 - l, y, l, 2, C_RULE); }
}

void gfx_clear(void) {
  SDL_SetRenderDrawColor(R, C_BG.r, C_BG.g, C_BG.b, 255);
  SDL_RenderClear(R);
  ruler();
}

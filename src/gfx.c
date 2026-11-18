#include "gfx.h"
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <string.h>

SDL_Renderer *R;
const SDL_Color C_BG = { 0xe6, 0xec, 0xe2, 255 }, C_INK = { 0x1d, 0x20, 0x1d, 255 },
                C_MUTE = { 0x7d, 0x85, 0x7c, 255 }, C_RULE = { 0xb9, 0xc1, 0xb7, 255 };

static char font_path[3][700];
#define NSIZES 8
static const int SIZES[NSIZES] = { 22, 24, 26, 30, 34, 64, 80, 88 };
static TTF_Font *fonts[3][NSIZES];

static TTF_Font *font(int size, int weight) {
  int k = 0;
  for (int i = 0; i < NSIZES; i++) if (SIZES[i] <= size) k = i;
  if (!fonts[weight][k]) fonts[weight][k] = TTF_OpenFont(font_path[weight], SIZES[k]);
  return fonts[weight][k];
}

int gfx_init(SDL_Renderer *ren, const char *pak_dir) {
  R = ren;
  if (TTF_Init() != 0) return -1;
  static const char *names[3] = { "DMMono-Light.ttf", "DMMono-Regular.ttf", "DMMono-Medium.ttf" };
  for (int w = 0; w < 3; w++) snprintf(font_path[w], sizeof font_path[w], "%s/%s", pak_dir, names[w]);
  if (!font(24, W_REG)) { fprintf(stderr, "font %s: %s\n", font_path[W_REG], TTF_GetError()); return -1; }
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

void gfx_clear(void) {
  SDL_SetRenderDrawColor(R, C_BG.r, C_BG.g, C_BG.b, 255);
  SDL_RenderClear(R);
}

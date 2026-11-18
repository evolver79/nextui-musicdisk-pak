#include "demo.h"
#include "gfx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *DEMO_NAMES[DEMO_PARTS] = { "Starfield", "Donut", "Tunnel" };

#define TOP 96
#define BOTTOM 640
#define CX 512
#define CY ((TOP + BOTTOM) / 2)

static float pulse;    // 0..1, jumps when a note hits (low notes and the loudest channels hit hardest)
static float energy;   // smoothed loudness of all channels
static float clock_s;
static int last_row = -1, last_pat = -1;

static SDL_Color mix(SDL_Color a, SDL_Color b, float k) {
  if (k < 0) k = 0;
  if (k > 1) k = 1;
  SDL_Color c = { (Uint8)(a.r + (b.r - a.r) * k), (Uint8)(a.g + (b.g - a.g) * k), (Uint8)(a.b + (b.b - a.b) * k), 255 };
  return c;
}

static void line(float x1, float y1, float x2, float y2, SDL_Color c) {
  SDL_SetRenderDrawColor(R, c.r, c.g, c.b, 255);
  SDL_RenderDrawLineF(R, x1, y1, x2, y2);
  SDL_RenderDrawLineF(R, x1 + 1, y1, x2 + 1, y2);   // two pixels, like the rules
}

// The song drives everything: new notes on the row give a pulse, the channels' VU give the energy.
static void listen(const PlayerState *ps, const float *vu, float dt) {
  float e = 0;
  for (int c = 0; c < ps->nch; c++) e += vu[c];
  if (ps->nch) e = e / sqrtf((float)ps->nch);
  if (ps->row != last_row || ps->pattern != last_pat) {
    for (int c = 0; c < ps->nch; c++) {
      int n = ps->ci[c].event.note;
      if (n <= 0 || n > 0x80 || ps->ci[c].volume == 0) continue;
      float w = n <= 49 ? 1.f : .55f;   // C-4 and below: kicks and bass
      float hit = w * (.45f + vu[c]);
      if (hit > pulse) pulse = hit > 1 ? 1 : hit;
    }
    last_row = ps->row; last_pat = ps->pattern;
  }
  pulse *= expf(-dt * 4.5f);
  energy += (e - energy) * (dt * 6 > 1 ? 1 : dt * 6);
  if (!ps->paused) clock_s += dt;
}

// --- starfield ---------------------------------------------------------------------------------------------------
#define NSTAR 240
static float sx[NSTAR], sy[NSTAR], sz[NSTAR];
static int stars_ready;
static float frand(void) { return (float)rand() / RAND_MAX; }
static void star_new(int i, float z) { sx[i] = (frand() * 2 - 1) * 2.4f; sy[i] = (frand() * 2 - 1) * 1.4f; sz[i] = z; }

static void starfield(float dt, float amount) {
  if (!stars_ready) { for (int i = 0; i < NSTAR; i++) star_new(i, .2f + frand() * 4); stars_ready = 1; }
  float speed = .35f + energy * 1.6f + pulse * 3.2f;
  int n = (int)(NSTAR * amount);
  for (int i = 0; i < n; i++) {
    float pz = sz[i];
    sz[i] -= speed * dt;
    if (sz[i] < .15f) { star_new(i, 4); continue; }
    float f = 260, x1 = CX + sx[i] / pz * f, y1 = CY + sy[i] / pz * f, x2 = CX + sx[i] / sz[i] * f, y2 = CY + sy[i] / sz[i] * f;
    if (x2 < 0 || x2 > 1024 || y2 < TOP || y2 > BOTTOM) { star_new(i, 4); continue; }
    if (fabsf(x2 - x1) + fabsf(y2 - y1) < 4) x1 = x2 - 4;
    line(x1, y1, x2, y2, mix(C_INK, C_RULE, sz[i] / 4.f));
  }
}

// --- donut: a wireframe torus, far lines drawn lighter -----------------------------------------------------------
#define NU 24
#define NV 12
static float ang_a = 1.1f, ang_b = .4f;

static void donut(const PlayerState *ps, float dt) {
  float spin = .45f + (ps->bpm > 0 ? ps->bpm / 260.f : .5f) + pulse * 2.5f;
  if (!ps->paused) { ang_a += dt * spin; ang_b += dt * spin * .63f; }
  float scale = 600 * (1 + pulse * .16f);
  float px[NU][NV], py[NU][NV], pz[NU][NV];
  float ca = cosf(ang_a), sa = sinf(ang_a), cb = cosf(ang_b), sb = sinf(ang_b);
  for (int i = 0; i < NU; i++) for (int j = 0; j < NV; j++) {
    float u = i * 6.2832f / NU, v = j * 6.2832f / NV;
    float r = .42f * (1 + .25f * energy * sinf(u * 3 + clock_s * 4));   // the tube swells with the music
    float x = (1 + r * cosf(v)) * cosf(u), y = (1 + r * cosf(v)) * sinf(u), z = r * sinf(v);
    float y2 = y * ca - z * sa, z2 = y * sa + z * ca;     // around X
    float x3 = x * cb + z2 * sb, z3 = -x * sb + z2 * cb;  // around Y
    float zz = z3 + 4, f = scale / zz;
    px[i][j] = CX + x3 * f; py[i][j] = CY + y2 * f; pz[i][j] = z3;
  }
  // two passes: the far side first, so near lines sit on top
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < NU; i++) for (int j = 0; j < NV; j++) {
      int i2 = (i + 1) % NU, j2 = (j + 1) % NV;
      float za = (pz[i][j] + pz[i2][j]) / 2, zb = (pz[i][j] + pz[i][j2]) / 2;
      if ((za > 0) == (pass == 0)) line(px[i][j], py[i][j], px[i2][j], py[i2][j], mix(C_INK, C_RULE, (za + 1.4f) / 2.4f));
      if ((zb > 0) == (pass == 0)) line(px[i][j], py[i][j], px[i][j2], py[i][j2], mix(C_INK, C_RULE, (zb + 1.4f) / 2.4f));
    }
}

// --- tunnel: rings flying at you, rippling with the channels ----------------------------------------------------
#define NRING 14
#define NSEG 48
static float tun_z;

static void tunnel(const PlayerState *ps, const float *vu, float dt) {
  if (!ps->paused) tun_z += dt * (1.2f + energy * 2.5f + pulse * 4);
  for (int k = NRING - 1; k >= 0; k--) {
    float d = fmodf(k + tun_z, NRING), z = NRING - d + .6f;
    float rad = 900 / z;
    float cx = CX + sinf(clock_s * .8f + z * .35f) * 90 * (1 - 1 / z), cy = CY + cosf(clock_s * .6f + z * .3f) * 50 * (1 - 1 / z);
    int ch = ps->nch ? k % ps->nch : 0;
    float wob = (ps->nch ? vu[ch] : 0) * .18f + pulse * .05f;
    SDL_Color col = mix(C_INK, C_RULE, z / NRING);
    float lx = 0, ly = 0;
    for (int s = 0; s <= NSEG; s++) {
      float a = s * 6.2832f / NSEG, r = rad * (1 + wob * sinf(a * 6 + clock_s * 5 + k));
      float x = cx + cosf(a) * r * 1.25f, y = cy + sinf(a) * r;
      if (s) line(lx, ly, x, y, col);
      lx = x; ly = y;
    }
  }
}

// --- sine scroller --------------------------------------------------------------------------------------------------
static char scroll[2048];
static const struct xmp_module *scroll_for;
static float scroll_x;

static void ascii(char *s) { for (; *s; s++) if ((unsigned char)*s < 32 || (unsigned char)*s > 126) *s = ' '; }

static void build_scroll(const struct xmp_module *m) {
  size_t n = 0;
  char t[80]; snprintf(t, sizeof t, "%s", m->name); ascii(t);
  n += snprintf(scroll + n, sizeof scroll - n, "MUSICDISK    %s    %s    ", t[0] ? t : "untitled", m->type);
  // the instrument and sample names: where the musicians left their messages
  for (int i = 0; i < m->ins && n < sizeof scroll - 64; i++) {
    char s[40]; snprintf(s, sizeof s, "%s", m->xxi[i].name); ascii(s);
    char *p = s; while (*p == ' ') p++;
    size_t l = strlen(p); while (l && p[l - 1] == ' ') p[--l] = 0;
    if (l) n += snprintf(scroll + n, sizeof scroll - n, "%s  ", p);
  }
  snprintf(scroll + n, sizeof scroll - n, "         ");
  scroll_for = m; scroll_x = 1024;
}

static void scroller(const struct xmp_module *m, float dt, int paused) {
  if (m != scroll_for) build_scroll(m);
  int adv = text_w("M", 48, W_MED);
  int len = (int)strlen(scroll);
  if (!paused) scroll_x -= dt * 170;
  if (scroll_x < -(float)(len * adv)) scroll_x = 1024;
  for (int i = 0; i < len; i++) {
    float x = scroll_x + i * adv;
    if (x < -adv || x > 1024) continue;
    if (scroll[i] == ' ') continue;
    char c[2] = { scroll[i], 0 };
    float y = 716 + sinf(x * .011f + clock_s * 2.6f) * (12 + pulse * 20);
    text((int)x, (int)y, c, 48, W_MED, C_INK, A_LEFT);
  }
}

void demo_draw(const struct xmp_module *m, const PlayerState *ps, const float *vu, float dt, int part) {
  listen(ps, vu, dt);
  SDL_Rect clip = { 0, TOP, 1024, BOTTOM - TOP };
  SDL_RenderSetClipRect(R, &clip);
  switch (part) {
    case 0: starfield(dt, 1); break;
    case 1: starfield(dt, .35f); donut(ps, dt); break;
    default: tunnel(ps, vu, dt); break;
  }
  SDL_RenderSetClipRect(R, NULL);
  scroller(m, dt, ps->paused);
}

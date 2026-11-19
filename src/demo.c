#include "demo.h"
#include "gfx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *DEMO_NAMES[DEMO_PARTS] = { "Starfield", "Donut", "Tunnel", "Twister", "Copper bars", "Dot ball", "Plasma", "Bobs", "Sine scroller" };

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

// --- ticker ---------------------------------------------------------------------------------------------------------
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

// A plain ticker between two rules, like a news line: the title, then the sample names.
static void scroller(const struct xmp_module *m, float dt, int paused) {
  if (m != scroll_for) build_scroll(m);
  double_dash(0, 1024, 660, C_RULE);
  double_dash(0, 1024, 742, C_RULE);
  int adv = text_w("M", 34, W_REG);
  int len = (int)strlen(scroll);
  if (!paused) scroll_x -= dt * 110;
  if (scroll_x < -(float)(len * adv)) scroll_x = 1024;
  int first = scroll_x < 0 ? (int)(-scroll_x / adv) : 0;
  for (int i = first; i < len; i++) {
    int x = (int)(scroll_x + i * adv);
    if (x > 1024) break;
    if (scroll[i] == ' ') continue;
    char c[2] = { scroll[i], 0 };
    text(x, 716, c, 34, W_REG, C_INK, A_LEFT);
  }
}

// --- twister: a square bar twisting down the screen; edges in ink, rungs shaded by how much a side faces you --------
static float tw_rot;

static void twister(float dt, int paused) {
  if (!paused) tw_rot += dt * (.8f + energy * 1.5f + pulse * 3);
  float w = 120 * (1 + energy * .25f);
  float prev[4] = {0}; int have = 0;
  for (int y = TOP; y <= BOTTOM; y += 4) {
    float a = tw_rot + sinf(y * .006f + clock_s * 1.1f) * (1.6f + pulse);
    float cx = CX + sinf(y * .004f + clock_s * .7f) * 120;
    float xs[4];
    for (int k = 0; k < 4; k++) xs[k] = cx + cosf(a + k * 1.5708f) * w;
    for (int k = 0; k < 4; k++) {
      int k2 = (k + 1) % 4;
      if (xs[k2] <= xs[k]) continue;   // this side faces away
      float facing = (xs[k2] - xs[k]) / (w * 1.4142f);
      if (have) {
        line(prev[k], y - 4, xs[k], y, C_INK);
        line(prev[k2], y - 4, xs[k2], y, C_INK);
      }
      if (y % 16 == 0) line(xs[k], y, xs[k2], y, mix(C_RULE, C_INK, facing));
    }
    memcpy(prev, xs, sizeof xs); have = 1;
  }
}

// --- copper bars: bands of lines swinging up and down; each band spreads with one channel ---------------------------
static void copper(const PlayerState *ps, const float *vu) {
  for (int b = 0; b < 6; b++) {
    float y = CY + sinf(clock_s * 1.3f + b * .55f) * 190;
    float spread = 3 + (ps->nch ? vu[b % ps->nch] : 0) * 6 + pulse * 2;
    for (int l = -3; l <= 3; l++) {
      SDL_Color c = mix(C_INK, C_RULE, fabsf((float)l) / 3.5f);
      float yy = y + l * spread;
      line(0, yy, 1024, yy, c);
    }
  }
}

// --- dot ball: a rotating ball of dots bouncing on the beat ---------------------------------------------------------
static float beat_ph, ball_rot;
static int ball_row = -1;

static void dotball(const PlayerState *ps, float dt) {
  float rows_per_s = ps->speed > 0 ? ps->bpm * .4f / ps->speed : 8;
  if (!ps->paused) { beat_ph += dt * rows_per_s / 4; ball_rot += dt * (.6f + energy); }
  if (ps->row != ball_row) { if (ps->row % 4 == 0) beat_ph = 0; ball_row = ps->row; }   // back in step with the song
  float bounce = fabsf(cosf(beat_ph * 3.14159f));
  float floor_y = BOTTOM - 40, rad = 120, squash = 1 - pulse * .18f;
  float cy = floor_y - rad - bounce * 230;
  line(CX - 360, floor_y + 2, CX + 360, floor_y + 2, C_RULE);
  float sw = rad * (1.1f - bounce * .4f);   // the shadow shrinks as the ball rises
  for (int i = 0; i < 24; i++) {
    float a1 = i * 6.2832f / 24, a2 = (i + 1) * 6.2832f / 24;
    line(CX + cosf(a1) * sw, floor_y + 16 + sinf(a1) * 8, CX + cosf(a2) * sw, floor_y + 16 + sinf(a2) * 8, C_RULE);
  }
  float cr = cosf(ball_rot), sr = sinf(ball_rot), ct = cosf(.4f), st = sinf(.4f);
  for (int la = 1; la < 12; la++) for (int lo = 0; lo < 22; lo++) {
    float th = la * 3.14159f / 12, ph = lo * 6.2832f / 22;
    float x = sinf(th) * cosf(ph), y = cosf(th), z = sinf(th) * sinf(ph);
    float x2 = x * cr + z * sr, z2 = -x * sr + z * cr;
    float y3 = y * ct - z2 * st, z3 = y * st + z2 * ct;
    float px = CX + x2 * rad / squash, py = cy + y3 * rad * squash;
    int s = z3 > 0 ? 5 : 3;
    SDL_Color c = mix(C_INK, C_RULE, (1 - z3) / 2);
    fill_rect((int)px - s / 2, (int)py - s / 2, s, s, c);
  }
}

// --- plasma, as contour lines (marching squares over a sum of sines) -------------------------------------------------
#define PW 33
#define PH 18
static float plasma_t;

static float field(float x, float y) {
  float t = plasma_t;
  return sinf(x * .9f + t) + sinf(y * 1.1f - t * 1.3f) + sinf((x + y) * .6f + t * .7f) + sinf(sqrtf(x * x + y * y) * .8f - t);
}

static void plasma(float dt, int paused) {
  if (!paused) plasma_t += dt * (.7f + energy * 1.4f + pulse * 2);
  float cw = 1024.f / (PW - 1), ch = (float)(BOTTOM - TOP) / (PH - 1);
  static float v[PW][PH];
  for (int i = 0; i < PW; i++) for (int j = 0; j < PH; j++) v[i][j] = field((i - PW / 2) * .35f, (j - PH / 2) * .35f);
  for (int lv = -3; lv <= 3; lv++) {
    float L = lv * .55f;
    SDL_Color c = lv % 2 ? C_RULE : C_INK;
    for (int i = 0; i < PW - 1; i++) for (int j = 0; j < PH - 1; j++) {
      float a = v[i][j], b = v[i + 1][j], cc = v[i + 1][j + 1], d = v[i][j + 1];
      float x = i * cw, y = TOP + j * ch;
      float px[4], py[4]; int n = 0;   // where the level crosses each edge of the cell
      if ((a > L) != (b > L)) { px[n] = x + cw * (L - a) / (b - a); py[n++] = y; }
      if ((b > L) != (cc > L)) { px[n] = x + cw; py[n++] = y + ch * (L - b) / (cc - b); }
      if ((d > L) != (cc > L)) { px[n] = x + cw * (L - d) / (cc - d); py[n++] = y + ch; }
      if ((a > L) != (d > L)) { px[n] = x; py[n++] = y + ch * (L - a) / (d - a); }
      if (n >= 2) line(px[0], py[0], px[1], py[1], c);
      if (n == 4) line(px[2], py[2], px[3], py[3], c);
    }
  }
}

// --- bobs: a chain of circles on a Lissajous path ---------------------------------------------------------------------
static void circle(float cx, float cy, float r, SDL_Color c) {
  float lx = cx + r, ly = cy;
  for (int s = 1; s <= 20; s++) { float a = s * 6.2832f / 20, x = cx + cosf(a) * r, y = cy + sinf(a) * r; line(lx, ly, x, y, c); lx = x; ly = y; }
}

static void bobs(void) {
  for (int i = 23; i >= 0; i--) {
    float x = CX + sinf(clock_s * 1.1f + i * .24f) * 400, y = CY + sinf(clock_s * 1.7f + i * .33f) * 200;
    circle(x, y, 22 + pulse * 14 - i * .4f, mix(C_INK, C_RULE, i / 26.f));
  }
}

// --- sine scroller, the old way: the text is drawn once, then copied in 2-pixel strips that each follow the sine -----
static SDL_Texture *strip_tex;
static float big_x = 1024;

static void sine_scroller(const struct xmp_module *m, float dt, int paused) {
  if (m != scroll_for) build_scroll(m);
  if (!strip_tex) {
    strip_tex = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 1024 + 128, 100);
    if (!strip_tex) return;
    SDL_SetTextureBlendMode(strip_tex, SDL_BLENDMODE_BLEND);
  }
  int adv = text_w("M", 80, W_MED), len = (int)strlen(scroll);
  if (!paused) big_x -= dt * 220;
  if (big_x < -(float)(len * adv)) big_x = 1024;
  // the visible stretch of text, into the strip texture
  SDL_Texture *old = SDL_GetRenderTarget(R);
  SDL_Rect clip; SDL_RenderGetClipRect(R, &clip);
  SDL_RenderSetClipRect(R, NULL);
  SDL_SetRenderTarget(R, strip_tex);
  SDL_SetRenderDrawColor(R, C_BG.r, C_BG.g, C_BG.b, 0);
  SDL_RenderClear(R);
  float shift = fmodf(big_x, (float)adv); if (shift > 0) shift -= adv;
  int first = (int)((shift - big_x) / adv + .5f);
  for (int i = 0; i * adv + shift < 1024 + adv; i++) {
    int k = first + i; if (k < 0 || k >= len || scroll[k] == ' ') continue;
    char c[2] = { scroll[k], 0 };
    text((int)(i * adv + shift + adv), 80, c, 80, W_MED, C_INK, A_LEFT);
  }
  SDL_SetRenderTarget(R, old);
  if (clip.w) SDL_RenderSetClipRect(R, &clip);
  // and out again, two pixels at a time
  float amp = 70 + pulse * 40;
  for (int x = 0; x < 1024; x += 2) {
    float y = CY - 50 + sinf(x * .008f + clock_s * 2.4f) * amp;
    SDL_Rect s = { x + adv, 0, 2, 100 };
    SDL_FRect d = { (float)x, y, 2, 100 };
    SDL_RenderCopyF(R, strip_tex, &s, &d);
  }
}

void demo_draw(const struct xmp_module *m, const PlayerState *ps, const float *vu, float dt, int part) {
  listen(ps, vu, dt);
  SDL_Rect clip = { 0, TOP, 1024, BOTTOM - TOP };
  SDL_RenderSetClipRect(R, &clip);
  switch (part) {
    case 0: starfield(dt, 1); break;
    case 1: starfield(dt, .35f); donut(ps, dt); break;
    case 2: tunnel(ps, vu, dt); break;
    case 3: twister(dt, ps->paused); break;
    case 4: copper(ps, vu); break;
    case 5: dotball(ps, dt); break;
    case 6: plasma(dt, ps->paused); break;
    case 7: bobs(); break;
    default: sine_scroller(m, dt, ps->paused); break;
  }
  SDL_RenderSetClipRect(R, NULL);
  if (part != DEMO_PARTS - 1) scroller(m, dt, ps->paused);   // the sine scroller part has the text already
}

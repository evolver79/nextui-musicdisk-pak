// Tracker.pak: a MOD / S3M / XM / IT player for NextUI. Plays from the SD card and from scene.org's mirror,
// in a quiet line look: now playing, the pattern, real scopes, and a file browser.
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "browse.h"
#include "gfx.h"
#include "http.h"
#include "player.h"

// Pad buttons in the Brick's own names (it reports an Xbox layout with Nintendo labels).
enum { BTN_A = SDL_CONTROLLER_BUTTON_B, BTN_B = SDL_CONTROLLER_BUTTON_A, BTN_X = SDL_CONTROLLER_BUTTON_Y,
       BTN_Y = SDL_CONTROLLER_BUTTON_X, BTN_START = SDL_CONTROLLER_BUTTON_START, BTN_SELECT = SDL_CONTROLLER_BUTTON_BACK,
       BTN_MENU = SDL_CONTROLLER_BUTTON_GUIDE, BTN_L1 = SDL_CONTROLLER_BUTTON_LEFTSHOULDER, BTN_R1 = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
       BTN_UP = SDL_CONTROLLER_BUTTON_DPAD_UP, BTN_DOWN = SDL_CONTROLLER_BUTTON_DPAD_DOWN,
       BTN_LEFT = SDL_CONTROLLER_BUTTON_DPAD_LEFT, BTN_RIGHT = SDL_CONTROLLER_BUTTON_DPAD_RIGHT };

enum { SCR_NOW, SCR_PATTERN, SCR_SCOPES, SCR_BROWSE };
#define SCENE "https://ftp.scene.org"
#define X0 56
#define X1 968
#define ROWS 7

static int screen = SCR_BROWSE, running = 1;
static char user_dir[700], cache_dir[760];
static PlayerState ps;
static float vu[PL_MAXCH];
static int pat_page, scope_sel;
static char toast_msg[128]; static Uint32 toast_until;

static void toast(const char *m) { snprintf(toast_msg, sizeof toast_msg, "%s", m); toast_until = SDL_GetTicks() + 2600; }

// --- sources and folders -----------------------------------------------------------------------------------------
typedef struct { char path[1024]; Listing list; int cursor, top; } Level;
typedef struct { const char *name; char root[700]; int remote; Level lv[24]; int depth, loading, failed; } Source;
static Source src[3];
static int cur_src;
static int probing;   // the first scene.org listing at start: when it fails, we fall back to the SD card
#define NSRC 3

// --- playlist: the playable files of the folder a song was started from --------------------------------------------
typedef struct { char url[1200]; char name[192]; int remote; } Item;
static Item *pl; static int pl_n, pl_i = -1, pl_src;
static char now_name[192];

// --- background job: one folder listing or one download at a time ---------------------------------------------------
typedef enum { JOB_NONE, JOB_LIST, JOB_GET } JobKind;
static struct {
  JobKind kind; volatile int done; int ok;
  char url[1200], path[1100];
  volatile long got, total;
  Listing list; int src, depth, play_index;
} job;

static void on_progress(long got, long total, void *ud) { (void)ud; job.got = got; job.total = total; }
static int job_thread(void *arg) {
  (void)arg;
  if (job.kind == JOB_LIST) job.ok = list_remote(job.url, &job.list) == 0;
  else job.ok = http_get_file(job.url, job.path, on_progress, NULL);
  job.done = 1;
  return 0;
}
static int job_busy(void) { return job.kind != JOB_NONE; }
static void job_start(void) { job.done = 0; job.got = 0; job.total = -1; SDL_DetachThread(SDL_CreateThread(job_thread, "job", NULL)); }

static void url_decode(const char *in, char *out, size_t n) {
  size_t o = 0;
  for (const char *p = in; *p && o + 1 < n; p++) {
    if (*p == '%' && p[1] && p[2]) { char h[3] = { p[1], p[2], 0 }; out[o++] = (char)strtol(h, NULL, 16); p += 2; }
    else out[o++] = *p;
  }
  out[o] = 0;
}

static void open_level(Source *s) {
  Level *l = &s->lv[s->depth];
  s->failed = 0;
  if (!s->remote) { s->failed = list_local(l->path, &l->list) != 0; return; }
  if (job_busy()) { toast("Still busy, try again in a moment"); return; }
  s->loading = 1;
  job.kind = JOB_LIST; job.src = (int)(s - src); job.depth = s->depth;
  snprintf(job.url, sizeof job.url, "%s", l->path);
  memset(&job.list, 0, sizeof job.list);
  job_start();
}

static void enter_dir(Source *s, const Entry *e) {
  if (s->depth + 1 >= 24) return;
  Level *from = &s->lv[s->depth], *to = &s->lv[s->depth + 1];
  if (s->remote) snprintf(to->path, sizeof to->path, "%s%s", from->path, e->href);
  else snprintf(to->path, sizeof to->path, "%s/%s", from->path, e->href);
  memset(&to->list, 0, sizeof to->list); to->cursor = to->top = 0;
  s->depth++;
  open_level(s);
}

static void leave_dir(Source *s) {
  if (s->depth == 0) return;
  if (s->loading) return;
  listing_free(&s->lv[s->depth].list);
  s->depth--; s->failed = 0;
}

// Remote files are kept in the cache under their path on the mirror, so a second play needs no download.
static void cache_path_for(const char *url, char *out, size_t n) {
  const char *p = strncmp(url, SCENE, strlen(SCENE)) ? url : url + strlen(SCENE);
  char flat[700]; size_t o = 0;
  for (; *p && o + 1 < sizeof flat; p++) flat[o++] = (*p == '/' || *p == '?' || *p == '&' || *p == ':') ? '_' : *p;
  flat[o] = 0;
  snprintf(out, n, "%s/%s", cache_dir, flat + (flat[0] == '_'));
}

static void start_playing(const char *path) {
  if (player_load(path) != 0) { toast("No module in that file"); return; }
  for (int c = 0; c < PL_MAXCH; c++) vu[c] = 0;
  pat_page = 0; scope_sel = 0;
  snprintf(now_name, sizeof now_name, "%s", pl[pl_i].name);
  screen = screen == SCR_BROWSE ? SCR_NOW : screen;
}

static void play_index(int i) {
  if (i < 0 || i >= pl_n) return;
  pl_i = i;
  Item *it = &pl[i];
  if (!it->remote) { start_playing(it->url); return; }
  char path[1100]; cache_path_for(it->url, path, sizeof path);
  struct stat st;
  if (stat(path, &st) == 0 && st.st_size > 0) { start_playing(path); return; }
  if (job_busy()) { toast("Still busy, try again in a moment"); return; }
  job.kind = JOB_GET; job.play_index = i;
  snprintf(job.url, sizeof job.url, "%s", it->url);
  snprintf(job.path, sizeof job.path, "%s", path);
  job_start();
}

static void play_from_listing(Source *s, int index) {
  Level *l = &s->lv[s->depth];
  free(pl); pl = calloc(l->list.n, sizeof *pl); pl_n = 0; pl_src = (int)(s - src);
  int start = 0;
  for (int i = 0; i < l->list.n; i++) {
    Entry *e = &l->list.e[i];
    if (e->dir) continue;
    if (i == index) start = pl_n;
    Item *it = &pl[pl_n++];
    it->remote = s->remote;
    if (s->remote) snprintf(it->url, sizeof it->url, "%s%s", l->path, e->href);
    else snprintf(it->url, sizeof it->url, "%s/%s", l->path, e->href);
    snprintf(it->name, sizeof it->name, "%s", e->name);
  }
  play_index(start);
}

static void job_poll(void) {
  if (!job_busy() || !job.done) return;
  JobKind k = job.kind; job.kind = JOB_NONE;
  if (k == JOB_LIST) {
    Source *s = &src[job.src];
    s->loading = 0;
    if (job.depth == s->depth) { listing_free(&s->lv[s->depth].list); s->lv[s->depth].list = job.list; s->failed = !job.ok; }
    else listing_free(&job.list);
    if (!job.ok && probing && job.src == 1 && job.depth == 0) { cur_src = 0; toast("No internet: showing the SD card"); }
    else if (!job.ok) toast("Couldn't reach scene.org");
    probing = 0;
  } else if (k == JOB_GET) {
    if (job.ok) { pl_i = job.play_index; start_playing(job.path); }
    else toast("Download failed");
  }
}

// --- reading the module ---------------------------------------------------------------------------------------------
static const char *NN[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };

static int event_at(const struct xmp_module *m, int pat, int row, int ch, struct xmp_event *ev) {
  if (!m || pat < 0 || pat >= m->pat || ch < 0 || ch >= m->chn) return 0;
  const struct xmp_pattern *p = m->xxp[pat];
  if (row < 0 || row >= p->rows) return 0;
  int t = p->index[ch];
  if (t < 0 || t >= m->trk || row >= m->xxt[t]->rows) return 0;
  *ev = m->xxt[t]->event[row];
  return 1;
}

static void note_str(int n, char *out) {
  if (n == 0) strcpy(out, "...");
  else if (n == XMP_KEY_OFF) strcpy(out, "===");
  else if (n == XMP_KEY_CUT) strcpy(out, "^^^");
  else if (n == XMP_KEY_FADE) strcpy(out, "~~~");
  else { n--; snprintf(out, 4, "%s%d", NN[n % 12], n / 12); }
}

static void trim(char *s) { size_t n = strlen(s); while (n && (s[n - 1] == ' ' || s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0; }

// What's playing on a channel, named by its instrument (where the old greetings live, too).
static void chan_label(const struct xmp_module *m, int c, char *out, size_t n) {
  int ins = ps.ci[c].instrument;
  char name[40] = "";
  if (m && ins < m->ins) { snprintf(name, sizeof name, "%s", m->xxi[ins].name); trim(name); }
  snprintf(out, n, "%d %s", c + 1, name);
}

// The sample under a channel's play position, as -1..1. Returns 0 past the end of a sample that doesn't loop.
static float sample_at(const struct xmp_module *m, int smp, double pos, int *alive) {
  *alive = 0;
  if (!m || smp < 0 || smp >= m->smp) return 0;
  const struct xmp_sample *s = &m->xxs[smp];
  if (!s->data || s->len <= 0 || (s->flg & XMP_SAMPLE_SYNTH)) return 0;
  long i = (long)pos;
  int loops = (s->flg & XMP_SAMPLE_LOOP) && s->lpe > s->lps;
  if (loops && i >= s->lpe) i = s->lps + (i - s->lps) % (s->lpe - s->lps);
  if (i < 0 || i >= s->len) return 0;
  *alive = 1;
  if (s->flg & XMP_SAMPLE_STEREO) i *= 2;
  if (s->flg & XMP_SAMPLE_16BIT) return ((const Sint16 *)s->data)[i] / 32768.f;
  return ((const Sint8 *)s->data)[i] / 128.f;
}

static float chan_value(const struct xmp_module *m, int c, int x) {
  const struct xmp_channel_info *ci = &ps.ci[c];
  if (player_muted(c) || ci->volume == 0) return 0;
  int alive;
  float v = sample_at(m, ci->sample, ci->position + x * ps.step[c], &alive);
  return v * ci->volume / 64.f;
}

static void update_vu(const struct xmp_module *m) {
  for (int c = 0; c < ps.nch; c++) {
    float peak = 0;
    for (int x = 0; x < 512; x += 4) { float v = chan_value(m, c, x); if (v < 0) v = -v; if (v > peak) peak = v; }
    vu[c] = peak > vu[c] ? peak : vu[c] * .88f;
  }
}

// --- drawing --------------------------------------------------------------------------------------------------------
static void clock_str(int ms, char *out) { int s = ms / 1000; sprintf(out, "%02d:%02d", s / 60, s % 60); }

static void ext_upper(const char *name, char *out, size_t n) {
  const char *dot = strrchr(name, '.');
  snprintf(out, n, "%s", dot ? dot + 1 : "MOD");
  for (char *p = out; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
}

static void top_bar(const char *left, const char *right) {
  text(X0, 64, left, 24, W_REG, C_MUTE, A_LEFT);
  text(X1, 64, right, 24, W_REG, C_MUTE, A_RIGHT);
}

static void bottom_line(const char *left, const char *right) {
  char b[160];
  if (SDL_GetTicks() < toast_until) { fit(b, sizeof b, toast_msg, X1 - X0, 26, W_MED, 0); text(X0, 732, b, 26, W_MED, C_INK, A_LEFT); return; }
  if (left) { fit(b, sizeof b, left, 700, 26, W_REG, 0); text(X0, 732, b, 26, W_REG, C_INK, A_LEFT); }
  if (right) text(X1, 732, right, 24, W_REG, C_MUTE, A_RIGHT);
}

static void draw_nothing(void) {
  top_bar("Tracker", "");
  text(X0, 172, "Nothing playing", 64, W_LIGHT, C_INK, A_LEFT);
  text(X0, 230, "Press START to pick a song.", 30, W_REG, C_MUTE, A_LEFT);
  bottom_line(NULL, NULL);
}

static void draw_now(const struct xmp_module *m) {
  char a[200], b[200];
  ext_upper(now_name, b, sizeof b);
  snprintf(a, sizeof a, ps.paused ? "PAUSED" : "%s · %d ch", b, ps.nch);
  top_bar(a, src[pl_src].name);
  char title[80]; snprintf(title, sizeof title, "%s", m->name); trim(title);
  fit(a, sizeof a, title[0] ? title : now_name, X1 - X0, 80, W_REG, 0);
  text(X0, 172, a, 80, W_REG, C_INK, A_LEFT);
  snprintf(b, sizeof b, "%s · %s", now_name, m->type);
  fit(a, sizeof a, b, X1 - X0, 30, W_REG, 0);
  text(X0, 222, a, 30, W_REG, C_MUTE, A_LEFT);
  clock_str(ps.time, a); text(X0, 340, a, 88, W_LIGHT, ps.paused ? C_MUTE : C_INK, A_LEFT);
  clock_str(ps.total, a); text(X1, 340, a, 30, W_REG, C_MUTE, A_RIGHT);
  rule(X0, X1, 372, C_RULE);
  float p = ps.total > 0 ? (float)ps.time / ps.total : 0; if (p > 1) p = 1;
  fill_rect(X0, 370, (int)((X1 - X0) * p), 6, C_INK);
  // one line per channel
  int n = ps.nch, yb = 600, gap = n > 1 ? (X1 - X0 - 6) / (n - 1) : 0;
  if (n > 16 && gap < 14) gap = 14;
  for (int c = 0; c < n; c++) {
    int x = X0 + c * gap; if (x > X1) break;
    int h = 8 + (int)(vu[c] * 150 * 1.6f); if (h > 158) h = 158;
    SDL_Color col = player_muted(c) ? C_RULE : C_INK;
    fill_rect(x, yb - h, n > 16 ? 4 : 6, h, col);
    if (n <= 16) { snprintf(a, sizeof a, "%d", c + 1); text(x, yb + 40, a, 22, W_REG, player_muted(c) ? C_RULE : C_MUTE, A_LEFT); }
  }
  if (pl_i + 1 < pl_n) { snprintf(a, sizeof a, "Next  %s", pl[pl_i + 1].name); bottom_line(a, NULL); }
  else bottom_line(NULL, NULL);
}

static void draw_pattern(const struct xmp_module *m) {
  char a[64], b[64];
  snprintf(a, sizeof a, "Order %d/%d", ps.pos, m->len - 1);
  snprintf(b, sizeof b, "Pattern %d", ps.pattern);
  top_bar(a, b);
  int pages = (ps.nch + 3) / 4; if (pat_page >= pages) pat_page = pages - 1;
  int base = pat_page * 4;
  static const int xs[5] = { X0, 176, 374, 572, 770 };
  text(xs[0], 132, "ROW", 22, W_MED, C_INK, A_LEFT);
  for (int i = 0; i < 4 && base + i < ps.nch; i++) {
    char l[48]; chan_label(m, base + i, l, sizeof l);
    fit(a, sizeof a, l, 176, 22, W_MED, 0);
    text(xs[i + 1], 132, a, 22, W_MED, player_muted(base + i) ? C_MUTE : C_INK, A_LEFT);
  }
  rule(X0, X1, 150, C_INK);
  for (int k = -4; k <= 4; k++) {
    int r = ps.row + k, y = 214 + (k + 4) * 60, cur = k == 0;
    if (r >= 0 && r < ps.num_rows) {
      snprintf(a, sizeof a, "%02d", r);
      text(xs[0], y - 18, a, 34, cur ? W_MED : W_LIGHT, cur || r % 4 == 0 ? C_INK : C_MUTE, A_LEFT);
      for (int i = 0; i < 4 && base + i < ps.nch; i++) {
        struct xmp_event ev;
        if (!event_at(m, ps.pattern, r, base + i, &ev)) continue;
        char ns[8]; note_str(ev.note, ns);
        if (ev.note || ev.ins) { if (ev.ins) snprintf(a, sizeof a, "%s %02d", ns, ev.ins); else snprintf(a, sizeof a, "%s", ns); }
        else snprintf(a, sizeof a, "·");
        int lit = ev.note && !player_muted(base + i);
        text(xs[i + 1], y - 18, a, 34, cur ? W_MED : W_LIGHT, lit ? C_INK : C_MUTE, A_LEFT);
      }
    }
    rule(X0, X1, y, cur || k == -1 ? C_INK : C_RULE);
  }
  if (pages > 1) { snprintf(a, sizeof a, "< %d-%d of %d >", base + 1, base + 4 < ps.nch ? base + 4 : ps.nch, ps.nch); }
  else snprintf(a, sizeof a, "%d channels", ps.nch);
  snprintf(b, sizeof b, "Row %02d", ps.row);
  bottom_line(a, b);
}

static void draw_scopes(const struct xmp_module *m) {
  char a[64], t[16];
  clock_str(ps.time, t);
  fit(a, sizeof a, now_name, 700, 24, W_REG, 0);
  top_bar(a, t);
  int page = scope_sel / 8, base = page * 8;
  SDL_Point pts[240];
  for (int k = 0; k < 8 && base + k < ps.nch; k++) {
    int c = base + k, gx = k % 2, gy = k / 2, x = X0 + gx * 472, y = 104 + gy * 152, w = 440;
    int mu = player_muted(c), sel = c == scope_sel;
    char l[48]; chan_label(m, c, l, sizeof l);
    fit(a, sizeof a, l, mu ? 300 : 420, 22, W_MED, 0);
    text(x, y + 30, a, 22, sel ? W_MED : W_REG, mu ? C_RULE : C_INK, A_LEFT);
    if (mu) text(x + w, y + 30, "MUTED", 22, W_REG, C_MUTE, A_RIGHT);
    if (sel) fill_rect(x, y + 38, text_w(a, 22, W_MED), 2, C_INK);
    int cy = y + 92, np = 0;
    for (int i = 0; i <= w && np < 240; i += 2) { float v = chan_value(m, c, i); pts[np].x = x + i; { float a2 = v * 96; if (a2 > 56) a2 = 56; if (a2 < -56) a2 = -56; pts[np].y = cy - (int)a2; } np++; }
    SDL_Color col = mu ? C_RULE : C_INK;
    SDL_SetRenderDrawColor(R, col.r, col.g, col.b, 255);
    SDL_RenderDrawLines(R, pts, np);
    for (int i = 0; i < np; i++) pts[i].y++;
    SDL_RenderDrawLines(R, pts, np);
    if (gy < 3) rule(x, x + w, y + 150, C_RULE);
  }
  if (ps.nch > 8) { snprintf(a, sizeof a, "Channels %d-%d of %d", base + 1, base + 8 < ps.nch ? base + 8 : ps.nch, ps.nch); bottom_line(a, "X mute  Y solo"); }
  else bottom_line(NULL, "X mute  Y solo");
}

static void draw_browse(void) {
  Source *s = &src[cur_src];
  Level *l = &s->lv[s->depth];
  char a[400], b[400];
  snprintf(a, sizeof a, "< %s >", s->name);
  snprintf(b, sizeof b, "%d / %d", cur_src + 1, NSRC);
  top_bar(a, b);
  if (s->remote) url_decode(l->path + strlen(SCENE), b, sizeof b);
  else snprintf(b, sizeof b, "%s", l->path);
  fit(a, sizeof a, b, X1 - X0, 30, W_REG, 1);
  text(X0, 150, a, 30, W_REG, C_MUTE, A_LEFT);
  rule(X0, X1, 178, C_INK);
  if (s->loading) text(X0, 226, "Loading....", 34, W_LIGHT, C_MUTE, A_LEFT);
  else if (s->failed) text(X0, 226, s->remote ? "Couldn't reach scene.org. A to try again." : "This folder can't be read.", 30, W_REG, C_MUTE, A_LEFT);
  else if (!l->list.n) text(X0, 226, s->remote ? "Nothing playable here." : "No songs yet. Put modules in /Music.", 30, W_REG, C_MUTE, A_LEFT);
  if (l->cursor < l->top) l->top = l->cursor;
  if (l->cursor >= l->top + ROWS) l->top = l->cursor - ROWS + 1;
  for (int i = 0; i < ROWS && !s->loading; i++) {
    int k = l->top + i, y = 246 + i * 66;
    if (k >= l->list.n) break;
    Entry *e = &l->list.e[k];
    int sel = k == l->cursor;
    char sz[24]; if (e->dir) snprintf(sz, sizeof sz, "%s", sel ? "->" : "/"); else size_str(e->size, sz, sizeof sz);
    int rw = text_w(sz, 24, W_REG) + 24;
    snprintf(b, sizeof b, "%s", e->name);
    fit(a, sizeof a, b, X1 - X0 - rw, 34, sel ? W_MED : W_LIGHT, 0);
    text(X0, y - 20, a, 34, sel ? W_MED : W_LIGHT, C_INK, A_LEFT);
    text(X1, y - 20, sz, 24, W_REG, sel ? C_INK : C_MUTE, A_RIGHT);
    rule(X0, X1, y, sel || k == l->cursor - 1 ? C_INK : C_RULE);
  }
  // the bottom line: a download, or what's playing
  if (job.kind == JOB_GET) {
    const char *slash = strrchr(job.url, '/');
    url_decode(slash ? slash + 1 : job.url, b, sizeof b);
    fit(a, sizeof a, b, 560, 26, W_MED, 0);
    text(X0, 732, a, 26, W_MED, C_INK, A_LEFT);
    float p = job.total > 0 ? (float)job.got / job.total : 0;
    if (job.total > 0) snprintf(a, sizeof a, "%d%%", (int)(p * 100)); else size_str(job.got, a, sizeof a);
    text(X1, 732, a, 26, W_REG, C_MUTE, A_RIGHT);
    fill_rect(X0, 748, X1 - X0, 2, C_RULE); fill_rect(X0, 747, (int)((X1 - X0) * p), 4, C_INK);
  } else if (ps.loaded) {
    char t[16]; clock_str(ps.time, t);
    snprintf(b, sizeof b, "%s  %s", ps.paused ? "Paused" : "Playing", now_name);
    bottom_line(b, t);
  } else bottom_line(NULL, NULL);
}

static void draw(void) {
  gfx_clear();
  const struct xmp_module *m = player_module();
  if (m) update_vu(m);
  if (screen == SCR_BROWSE) draw_browse();
  else if (!m) draw_nothing();
  else if (screen == SCR_NOW) draw_now(m);
  else if (screen == SCR_PATTERN) draw_pattern(m);
  else draw_scopes(m);
}

// --- buttons ----------------------------------------------------------------------------------------------------------
static void solo(int c) {
  int others_on = 0;
  for (int i = 0; i < ps.nch; i++) if (i != c && !player_muted(i)) others_on = 1;
  for (int i = 0; i < ps.nch; i++) player_mute(i, others_on ? i != c : 0);
}

static void on_button(int b) {
  if (b == BTN_MENU) { running = 0; return; }
  if (screen == SCR_BROWSE) {
    Source *s = &src[cur_src];
    Level *l = &s->lv[s->depth];
    int n = l->list.n;
    switch (b) {
      case BTN_UP: if (n) l->cursor = (l->cursor + n - 1) % n; break;
      case BTN_DOWN: if (n) l->cursor = (l->cursor + 1) % n; break;
      case BTN_L1: l->cursor = l->cursor - ROWS < 0 ? 0 : l->cursor - ROWS; break;
      case BTN_R1: if (n) l->cursor = l->cursor + ROWS >= n ? n - 1 : l->cursor + ROWS; break;
      case BTN_LEFT: cur_src = (cur_src + NSRC - 1) % NSRC; if (!src[cur_src].lv[0].list.n && !src[cur_src].loading) open_level(&src[cur_src]); break;
      case BTN_RIGHT: cur_src = (cur_src + 1) % NSRC; if (!src[cur_src].lv[0].list.n && !src[cur_src].loading) open_level(&src[cur_src]); break;
      case BTN_A:
        if (s->failed || (!n && !s->loading)) { open_level(s); break; }
        if (!n || s->loading) break;
        if (l->list.e[l->cursor].dir) enter_dir(s, &l->list.e[l->cursor]);
        else play_from_listing(s, l->cursor);
        break;
      case BTN_B: if (s->depth) leave_dir(s); else if (ps.loaded) screen = SCR_NOW; break;
      case BTN_START: if (ps.loaded) screen = SCR_NOW; break;
    }
    return;
  }
  switch (b) {
    case BTN_START: case BTN_B: screen = SCR_BROWSE; break;
    case BTN_SELECT: screen = screen == SCR_NOW ? SCR_PATTERN : screen == SCR_PATTERN ? SCR_SCOPES : SCR_NOW; break;
    case BTN_A: if (ps.loaded) player_pause(!ps.paused); break;
    case BTN_L1: if (pl_i > 0) play_index(pl_i - 1); break;
    case BTN_R1: if (pl_i + 1 < pl_n) play_index(pl_i + 1); break;
    case BTN_LEFT: if (screen == SCR_PATTERN && pat_page > 0) pat_page--; if (screen == SCR_SCOPES && scope_sel > 0) scope_sel--; break;
    case BTN_RIGHT: if (screen == SCR_PATTERN && (pat_page + 1) * 4 < ps.nch) pat_page++; if (screen == SCR_SCOPES && scope_sel + 1 < ps.nch) scope_sel++; break;
    case BTN_UP: if (screen == SCR_SCOPES && scope_sel >= 2) scope_sel -= 2; break;
    case BTN_DOWN: if (screen == SCR_SCOPES && scope_sel + 2 < ps.nch) scope_sel += 2; break;
    case BTN_X: if (screen != SCR_NOW || 1) player_mute(scope_sel, !player_muted(scope_sel)); break;
    case BTN_Y: solo(scope_sel); break;
  }
}

// --- screenshots without a device: --shots <dir> <module> ------------------------------------------------------------
static void save_shot(const char *dir, const char *name) {
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 1024, 768, 32, SDL_PIXELFORMAT_ARGB8888);
  SDL_RenderReadPixels(R, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
  SDL_Surface *rgb = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGB24, 0);   // plain 24-bit BMPs open anywhere
  char p[900]; snprintf(p, sizeof p, "%s/%s.bmp", dir, name);
  SDL_SaveBMP(rgb, p); SDL_FreeSurface(rgb); SDL_FreeSurface(s);
}

static void shots(const char *dir, const char *module) {
  cur_src = 1; open_level(&src[1]);
  while (job_busy()) { SDL_Delay(50); job_poll(); }
  draw(); save_shot(dir, "browse");
  pl = calloc(1, sizeof *pl); pl_n = 1; pl_i = 0; pl_src = 0;
  const char *slash = strrchr(module, '/');
  snprintf(pl[0].name, sizeof pl[0].name, "%s", slash ? slash + 1 : module);
  snprintf(pl[0].url, sizeof pl[0].url, "%s", module);
  start_playing(module);
  if (!player_module()) { fprintf(stderr, "couldn't load %s\n", module); return; }
  player_pump(900);
  player_get(&ps);
  for (int i = 0; i < 12; i++) update_vu(player_module());
  screen = SCR_NOW; draw(); save_shot(dir, "now");
  screen = SCR_PATTERN; draw(); save_shot(dir, "pattern");
  screen = SCR_SCOPES; scope_sel = 2; draw(); save_shot(dir, "scopes");
}

int main(int argc, char **argv) {
  snprintf(user_dir, sizeof user_dir, "%s", argc > 1 ? argv[1] : ".");
  const char *pak = argc > 2 ? argv[2] : ".";
  const char *shots_dir = argc > 4 && !strcmp(argv[3], "--shots") ? argv[4] : NULL;
  mkdir(user_dir, 0755);
  snprintf(cache_dir, sizeof cache_dir, "%s/cache", user_dir); mkdir(cache_dir, 0755);

  const char *sd = getenv("SDCARD_PATH"); if (!sd || !*sd) sd = "/mnt/SDCARD";
  src[0].name = "SD card"; snprintf(src[0].root, sizeof src[0].root, "%s/Music", sd); src[0].remote = 0;
  src[1].name = "scene.org music"; snprintf(src[1].root, sizeof src[1].root, SCENE "/pub/music/"); src[1].remote = 1;
  src[2].name = "scene.org parties"; snprintf(src[2].root, sizeof src[2].root, SCENE "/pub/parties/"); src[2].remote = 1;
  for (int i = 0; i < NSRC; i++) snprintf(src[i].lv[0].path, sizeof src[i].lv[0].path, "%s", src[i].root);
  mkdir(src[0].root, 0755);

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
  SDL_Window *win = SDL_CreateWindow("Tracker", 0, 0, 1024, 768, shots_dir ? 0 : SDL_WINDOW_FULLSCREEN);
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, shots_dir ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!ren) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
  SDL_RenderSetLogicalSize(ren, 1024, 768);
  if (gfx_init(ren, pak) != 0) return 1;
  for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);
  if (player_init(!shots_dir) != 0) fprintf(stderr, "audio: %s\n", SDL_GetError());

  if (shots_dir) { shots(shots_dir, argc > 5 ? argv[5] : ""); SDL_Quit(); return 0; }

  // Start on scene.org; the SD card is read too, and shown instead when there's no connection.
  open_level(&src[0]);
  cur_src = 1; probing = 1; open_level(&src[1]);

  int held = -1; Uint32 repeat_at = 0;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = 0;
      if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        int b = e.cbutton.button;
        on_button(b);
        if (b == BTN_UP || b == BTN_DOWN || b == BTN_LEFT || b == BTN_RIGHT) { held = b; repeat_at = SDL_GetTicks() + 400; }
      }
      if (e.type == SDL_CONTROLLERBUTTONUP && e.cbutton.button == held) held = -1;
    }
    if (held >= 0 && SDL_GetTicks() >= repeat_at) { on_button(held); repeat_at = SDL_GetTicks() + 70; }
    job_poll();
    player_get(&ps);
    if (ps.loaded && ps.ended && !job_busy()) {   // the song is over: on to the next one in the folder
      if (pl_i + 1 < pl_n) play_index(pl_i + 1); else { player_unload(); screen = SCR_BROWSE; }
    }
    draw();
    SDL_RenderPresent(ren);
  }
  player_unload();
  SDL_Quit();
  return 0;
}

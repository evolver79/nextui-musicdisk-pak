// Musicdisk: a MOD / S3M / XM / IT player for NextUI. Plays the modules in the SD card's Music folder, drawn with the
// shared Dot UI (ui/dot): now playing, demo effects, the pattern, real scopes, and a file browser.
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include "browse.h"
#include "gfx.h"
#include "info.h"
#include "hw.h"
#include <signal.h>
#include "player.h"
#include "demo.h"
#include "text.h"
#include "zipx.h"
#include <ctype.h>
#include <time.h>

// Pad buttons in the Brick's own names (it reports an Xbox layout with Nintendo labels).
enum { BTN_A = SDL_CONTROLLER_BUTTON_B, BTN_B = SDL_CONTROLLER_BUTTON_A, BTN_X = SDL_CONTROLLER_BUTTON_Y,
       BTN_Y = SDL_CONTROLLER_BUTTON_X, BTN_START = SDL_CONTROLLER_BUTTON_START, BTN_SELECT = SDL_CONTROLLER_BUTTON_BACK,
       BTN_MENU = SDL_CONTROLLER_BUTTON_GUIDE, BTN_L1 = SDL_CONTROLLER_BUTTON_LEFTSHOULDER, BTN_R1 = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
       BTN_UP = SDL_CONTROLLER_BUTTON_DPAD_UP, BTN_DOWN = SDL_CONTROLLER_BUTTON_DPAD_DOWN,
       BTN_LEFT = SDL_CONTROLLER_BUTTON_DPAD_LEFT, BTN_RIGHT = SDL_CONTROLLER_BUTTON_DPAD_RIGHT };

enum { SCR_NOW, SCR_DEMO, SCR_PATTERN, SCR_SCOPES, SCR_INFO, SCR_BROWSE };
// The screen is the device's own (Brick 1024x768, Smart Pro 1280x720: dot_screen_detect in main); these follow it.
#define ROWS ((DOT_CONTENT_BOTTOM - 96 + 12) / (DOT_ROW_H + 12))   // browse rows: Brick 5, Smart Pro 4
#define CW DOT_CONTENT_W              // a full-width card: 944 / 1200
#define RIGHT (DOT_W - DOT_MARGIN)    // the content's right edge: 984 / 1240
#define CB (DOT_DOCK_Y - 40)          // the bottom of the tall cards: 624 / 576
#define DW (DOT_W - 1024)             // how much wider than the Brick: 0 / 256

static int screen = SCR_BROWSE, running = 1;
static char user_dir[700], cache_dir[760];
static PlayerState ps;
static float vu[PL_MAXCH];
static int pat_page, scope_sel;
static int demo_part, demo_last_pos = -1;   // the demo effect; a new one every four song positions
static float frame_dt = 1 / 60.f;
static char toast_msg[128]; static Uint32 toast_until;
static Uint32 flash_until; static int flash_dir, flash_seek;   // the last skip lights its button for a moment, as in Kino
static int show_help;   // the buttons, shown on first start and on SELECT in Browse

static void toast(const char *m) { snprintf(toast_msg, sizeof toast_msg, "%s", m); toast_until = SDL_GetTicks() + 2600; }

// --- settings: SELECT in Browse. Kept in <user data>/settings.txt as key=value lines. ---------------------------------
static struct { int separation, interp, filter, screen_off; } set = { 70, 2, 1, 2 };
static const char *INTERP_NAMES[] = { "Nearest · Amiga", "Linear", "Spline" };
static const int INTERP_XMP[] = { XMP_INTERP_NEAREST, XMP_INTERP_LINEAR, XMP_INTERP_SPLINE };
static const char *OFF_NAMES[] = { "Never", "After 30 s", "After 1 min", "After 2 min", "After 5 min" };
static const int OFF_MS[] = { 0, 30000, 60000, 120000, 300000 };
enum { O_SEP, O_INTERP, O_FILTER, O_SCREEN, O_BUTTONS, O_N };
static int show_settings, opt_sel;

static void settings_apply(void) { player_sound(set.separation, INTERP_XMP[set.interp], set.filter); }

static void settings_load(void) {
  char p[800]; snprintf(p, sizeof p, "%s/settings.txt", user_dir);
  FILE *f = fopen(p, "r"); if (!f) return;
  char k[64]; int v;
  while (fscanf(f, " %63[^=]=%d", k, &v) == 2) {
    if (!strcmp(k, "separation") && v >= 0 && v <= 100) set.separation = v;
    else if (!strcmp(k, "interpolation") && v >= 0 && v < 3) set.interp = v;
    else if (!strcmp(k, "amiga_filter")) set.filter = !!v;
    else if (!strcmp(k, "screen_off") && v >= 0 && v < 5) set.screen_off = v;
  }
  fclose(f);
}

static void settings_save(void) {
  char p[800]; snprintf(p, sizeof p, "%s/settings.txt", user_dir);
  FILE *f = fopen(p, "w"); if (!f) return;
  fprintf(f, "separation=%d\ninterpolation=%d\namiga_filter=%d\nscreen_off=%d\n", set.separation, set.interp, set.filter, set.screen_off);
  fclose(f);
}

// --- folders: the Music folder on the SD card, and the folders under it ----------------------------------------------
typedef struct { char path[1024]; Listing list; int cursor, top; char zip[1100], zip_name[192]; } Level;   // zip: a zip opened like a folder
static char music_root[700];
static Level lv[24];
static int depth, failed;

static void open_level(void) { failed = list_local(lv[depth].path, &lv[depth].list) != 0; }

static void enter_dir(const Entry *e) {
  if (depth + 1 >= 24) return;
  Level *to = &lv[depth + 1];
  snprintf(to->path, sizeof to->path, "%s/%s", lv[depth].path, e->href);
  memset(&to->list, 0, sizeof to->list); to->cursor = to->top = 0; to->zip[0] = to->zip_name[0] = 0;
  depth++;
  open_level();
}

// ► on a zip with several songs: its songs, like a folder (unpacked once to the cache).
static void unzip_dir_for(const char *zip, char *out, size_t n);
static void enter_zip(const Entry *e) {
  if (depth + 1 >= 24) return;
  char zip[1100], dir[1200]; snprintf(zip, sizeof zip, "%s/%s", lv[depth].path, e->href);
  unzip_dir_for(zip, dir, sizeof dir);
  static ZipSong songs[256];
  int n = zip_songs(zip, dir, songs, 256);
  if (n < 2) { toast(n == 1 ? "Just one song in this zip" : "No songs in this zip"); return; }
  Level *to = &lv[depth + 1];
  snprintf(to->path, sizeof to->path, "%s", dir);
  snprintf(to->zip, sizeof to->zip, "%s", zip); snprintf(to->zip_name, sizeof to->zip_name, "%s", e->name);
  memset(&to->list, 0, sizeof to->list); to->cursor = to->top = 0;
  depth++;
  open_level();
}

static void leave_dir(void) {
  if (depth == 0) return;
  listing_free(&lv[depth].list);
  depth--;
  open_level();   // read again: the card may have changed
}

// --- playlist: the songs of the folder a song was started from ------------------------------------------------------
typedef struct { char url[1200]; char name[192]; char title[96], artist[64]; char zip[1100]; } Item;   // zip: the zip on the card it came from
static Item *pl; static int pl_n, pl_i = -1, pl_shuffled;
static char now_name[192], now_title[96], now_artist[64];

// Songs unpacked from a musicdisk zip live in cache/unzipped/<zip name>.<size>/.
static void unzip_dir_for(const char *zip, char *out, size_t n) {
  struct stat st; long sz = stat(zip, &st) == 0 ? (long)st.st_size : 0;
  const char *b = strrchr(zip, '/');
  snprintf(out, n, "%s/unzipped/%s.%ld", cache_dir, b ? b + 1 : zip, sz);
}

// A zip with several songs (a musicdisk, a music pack) becomes its songs, in place in the playlist.
static int expand_zip(const char *path) {
  if (!is_zip_name(path)) return 0;
  static ZipSong songs[256];
  char dir[1200]; unzip_dir_for(path, dir, sizeof dir);
  int n = zip_songs(path, dir, songs, 256);
  if (n < 2) return 0;
  Item base = pl[pl_i];
  pl = realloc(pl, (pl_n + n - 1) * sizeof *pl);
  memmove(&pl[pl_i + n], &pl[pl_i + 1], (pl_n - pl_i - 1) * sizeof *pl);
  for (int k = 0; k < n; k++) {
    Item *it = &pl[pl_i + k];
    memset(it, 0, sizeof *it);
    snprintf(it->url, sizeof it->url, "%s", songs[k].path);
    snprintf(it->name, sizeof it->name, "%s", songs[k].name);
    snprintf(it->title, sizeof it->title, "%s", songs[k].name);
    char *dot = strrchr(it->title, '.'); if (dot) *dot = 0;
    snprintf(it->artist, sizeof it->artist, "%s", base.title[0] ? base.title : base.name);
    snprintf(it->zip, sizeof it->zip, "%s", base.zip[0] ? base.zip : base.url);
  }
  pl_n += n - 1;
  char msg[64]; snprintf(msg, sizeof msg, "%d songs in this zip", n); toast(msg);
  return 1;
}

static void start_playing(const char *path_in) {
  char path[1200]; snprintf(path, sizeof path, "%s", path_in);   // the playlist may move under us
  if (expand_zip(path)) snprintf(path, sizeof path, "%s", pl[pl_i].url);
  if (player_load(path) != 0) { toast("No module in that file"); return; }
  for (int c = 0; c < PL_MAXCH; c++) vu[c] = 0;
  pat_page = 0; scope_sel = 0;
  snprintf(now_name, sizeof now_name, "%s", pl[pl_i].name);
  snprintf(now_title, sizeof now_title, "%s", pl[pl_i].title);
  snprintf(now_artist, sizeof now_artist, "%s", pl[pl_i].artist);
  info_build(path, pl[pl_i].zip[0] ? pl[pl_i].zip : path, player_module(), player_comment());
  screen = screen == SCR_BROWSE ? SCR_NOW : screen;
}

static void play_index(int i) {
  if (i < 0 || i >= pl_n) return;
  pl_i = i;
  start_playing(pl[i].url);
}

static void set_playlist(const Listing *list, const char *dir, const char *zip) {   // dir NULL: the hrefs are full paths
  free(pl); pl = calloc(list->n ? list->n : 1, sizeof *pl); pl_n = 0; pl_shuffled = 0;
  for (int i = 0; i < list->n; i++) {
    const Entry *e = &list->e[i];
    if (e->dir) continue;
    Item *it = &pl[pl_n++];
    if (dir) snprintf(it->url, sizeof it->url, "%s/%s", dir, e->href); else snprintf(it->url, sizeof it->url, "%s", e->href);
    snprintf(it->name, sizeof it->name, "%s", e->name);
    snprintf(it->title, sizeof it->title, "%s", e->title);
    snprintf(it->artist, sizeof it->artist, "%s", e->artist);
    if (zip) snprintf(it->zip, sizeof it->zip, "%s", zip);
    else if (is_zip_name(it->url)) snprintf(it->zip, sizeof it->zip, "%s", it->url);
  }
}

static void play_from_listing(int index) {
  Level *l = &lv[depth];
  set_playlist(&l->list, l->path, l->zip[0] ? l->zip : NULL);
  int start = 0;
  for (int i = 0; i < index && i < l->list.n; i++) start += !l->list.e[i].dir;
  play_index(start);
}

// Y: every song in this folder and the folders under it, in random order.
static void shuffle_folder(void) {
  Listing all = {0};
  list_tree(lv[depth].path, &all);
  set_playlist(&all, NULL, NULL);
  listing_free(&all);
  if (!pl_n) { toast("No songs here"); return; }
  for (int i = pl_n - 1; i > 0; i--) { int j = rand() % (i + 1); Item t = pl[i]; pl[i] = pl[j]; pl[j] = t; }
  pl_shuffled = 1;
  play_index(0);
  char m[48]; snprintf(m, sizeof m, "Shuffled %d song%s", pl_n, pl_n == 1 ? "" : "s"); toast(m);
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

// --- drawing (Dot: ui/dot) ---------------------------------------------------------------------------------------
static void clock_str(int ms, char *out) { int s = ms / 1000; sprintf(out, "%02d:%02d", s / 60, s % 60); }

static void ext_upper(const char *name, char *out, size_t n) {
  const char *dot = strrchr(name, '.');
  snprintf(out, n, "%s", dot ? dot + 1 : "MOD");
  for (char *p = out; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
}

// The song's name: from its .diz, else the name inside the module, else the file name.
static void song_title(const struct xmp_module *m, char *out, size_t n) {
  if (now_title[0]) { snprintf(out, n, "%s", now_title); return; }
  char t[80]; snprintf(t, sizeof t, "%s", m->name); trim(t);
  snprintf(out, n, "%s", t[0] ? t : now_name);
}

static void dock4(const char *k1, const char *t1, const char *k2, const char *t2, const char *k3, const char *t3,
                  const char *k4, const char *t4) {   // NULL keys are left out
  DotHint h[4]; int n = 0;
  if (k1) h[n++] = (DotHint){ k1, t1 };
  if (k2) h[n++] = (DotHint){ k2, t2 };
  if (k3) h[n++] = (DotHint){ k3, t3 };
  if (k4) h[n++] = (DotHint){ k4, t4 };
  dot_dock(D, h, n);
}

static void player_dock(const char *k3, const char *t3) {
  dock4("A", ps.paused ? "Play" : "Pause", k3, t3, "SELECT", "Screen", "B", "Browse");
}

static void message_card(const char *title, const char *body) {
  dot_card(D, 40, 96, CW, 170);
  dot_text(D, DOT_TITLE, title, 76, 130, DOT_TEXT, DOT_LEFT, CW - 72);
  if (body) dot_text(D, DOT_BODY_S, body, 76, 184, DOT_LABEL, DOT_LEFT, CW - 72);
}

static void draw_nothing(void) {
  dot_header(D, "NOW PLAYING", NULL);
  message_card("Nothing playing", "Press B to pick a song.");
  dock4("B", "Browse", NULL, NULL, NULL, NULL, NULL, NULL);
}

static void draw_now(const struct xmp_module *m) {
  char a[200], b[200], t[16];
  ext_upper(now_name, b, sizeof b);
  snprintf(a, sizeof a, "%s · %d CH", b, ps.nch);
  if (pl_n > 1) snprintf(b, sizeof b, "%s · %d of %d", pl_shuffled ? "Shuffle" : "Now playing", pl_i + 1, pl_n);
  else snprintf(b, sizeof b, "Now playing");
  dot_header(D, b, a);
  // the lower part (channel dots, round buttons) sits dy higher on a shorter screen, with 5 rows of dots, not 6
  int dy = 624 - CB;
  dot_card(D, 40, 96, CW, CB - 96);
  dot_adot(D, 76, 133, dot_accent(D));
  dot_text(D, DOT_LABEL_T, ps.paused ? "Paused" : "Playing", 104, 130, DOT_TEXT, DOT_LEFT, 0);
  char title[100]; song_title(m, title, sizeof title);
  dot_text(D, DOT_TITLE, title, 76, 170, DOT_TEXT, DOT_LEFT, CW - 72);
  if (now_artist[0]) snprintf(b, sizeof b, "%s · %s", now_artist, now_name);
  else snprintf(b, sizeof b, "%s · %s", now_name, m->type);
  dot_text(D, DOT_BODY_S, b, 76, 220, DOT_LABEL, DOT_LEFT, CW - 72);
  clock_str(ps.time, t);
  int tw = dot_time(D, DOT_DISPLAY_XL, t, 74, 278, ps.paused ? DOT_LABEL : DOT_TEXT, DOT_LEFT);
  clock_str(ps.total, t); snprintf(a, sizeof a, "/ %s", t);
  dot_text(D, DOT_LABEL_T, a, 74 + tw + 30, 300, DOT_LABEL, DOT_LEFT, 0);
  dot_progress(D, 76, 396, CW - 72, ps.total > 0 ? (float)ps.time / ps.total : 0, 0);
  // a column of dots per channel, lit from the bottom by its loudness, left of the round buttons
  int n = ps.nch, rows = dy ? 5 : 6, pitch = n ? (544 + DW) / n : 40;   // the last dot ends 40 px clear of the prev button (x 620 on the Brick)
  if (pitch > 40) pitch = 40;
  int r = pitch >= 20 ? 7 : pitch / 2 - 2 > 2 ? pitch / 2 - 2 : 2;
  for (int c = 0; c < n; c++) {
    int x = 76 + r + c * pitch, lit = (int)(vu[c] * rows * 1.6f + .5f), mu = player_muted(c);
    if (lit > rows) lit = rows;
    for (int k = 0; k < rows; k++)
      dot_circle(D, x, 562 - dy - k * 20, r, k < lit ? (mu ? DOT_DIM : DOT_TEXT) : DOT_TRACK);
    if (n <= 16) { snprintf(a, sizeof a, "%d", c + 1); dot_text(D, DOT_VALUE, a, x, 584 - dy, mu ? DOT_DIM : DOT_LABEL, DOT_CENTER, 0); }
  }
  int has_prev = pl_i > 0, has_next = pl_i + 1 < pl_n;
  int lit = SDL_GetTicks() < flash_until ? flash_dir : 0;   // a 10 s skip shows as rewind / forward
  dot_round(D, 698 + DW, 520 - dy, lit < 0 && flash_seek ? DOT_I_BACK : DOT_I_PREV, 0, lit < 0 ? DOT_FOCUS | DOT_ON : has_prev ? 0 : DOT_OFF);
  dot_round(D, 808 + DW, 520 - dy, ps.paused ? DOT_I_PLAY : DOT_I_PAUSE, 1, lit ? 0 : DOT_FOCUS);
  dot_round(D, 918 + DW, 520 - dy, lit > 0 && flash_seek ? DOT_I_FWD : DOT_I_NEXT, 0, lit > 0 ? DOT_FOCUS | DOT_ON : has_next ? 0 : DOT_OFF);
  dot_text(D, DOT_LABEL_T, "L1", 698 + DW, 572 - dy, has_prev ? DOT_LABEL : DOT_DIM, DOT_CENTER, 0);   // the buttons that do it
  dot_text(D, DOT_LABEL_T, "R1", 918 + DW, 572 - dy, has_next ? DOT_LABEL : DOT_DIM, DOT_CENTER, 0);
  player_dock("<>", "10 s");   // L1 R1 for the song: the round buttons above say it, and the buttons screen
}

static void draw_pattern(const struct xmp_module *m) {
  char a[64], b[64];
  snprintf(a, sizeof a, "Pattern %d · order %d/%d", ps.pattern, ps.pos, m->len - 1);
  snprintf(b, sizeof b, "Row %02d", ps.row);
  int pages = (ps.nch + 3) / 4; if (pat_page >= pages) pat_page = pages - 1;
  int base = pat_page * 4;
  if (pages > 1) { char c[96]; snprintf(c, sizeof c, "%s · ch %d-%d of %d", a, base + 1, base + 4 < ps.nch ? base + 4 : ps.nch, ps.nch); snprintf(a, sizeof a, "%s", c); }
  dot_header(D, a, b);
  dot_card(D, 40, 96, CW, CB - 96);
  // the rows either side of the current one: 4 on the Brick (9 rows to 614), 3 on the Smart Pro; the four channel
  // columns share the width (190 apart on the Brick)
  int half = (CB - 10 - 214) / 100, cp = (CW - 190 + 6) / 4;
  const int xs[5] = { 76, 196, 196 + cp, 196 + 2 * cp, 196 + 3 * cp };
  dot_text(D, DOT_SMALL, "Row", xs[0], 124, DOT_LABEL, DOT_LEFT, 0);
  for (int i = 0; i < 4 && base + i < ps.nch; i++) {
    char l[48]; int mu = player_muted(base + i);   // muted: says so, instead of the instrument
    if (mu) snprintf(l, sizeof l, "%d Muted", base + i + 1); else chan_label(m, base + i, l, sizeof l);
    dot_text(D, DOT_SMALL, l, xs[i + 1], 124, mu ? DOT_DIM : DOT_LABEL, DOT_LEFT, cp - 18);
  }
  for (int k = -half; k <= half; k++) {
    int r = ps.row + k, y = 166 + (k + half) * 50, cur = k == 0;
    if (cur) dot_rect(D, 56, y, CW - 32, 48, 22, DOT_PAPER, 255);
    if (r < 0 || r >= ps.num_rows) continue;
    snprintf(a, sizeof a, "%02d", r);
    int ty = y + (48 - dot_line_h(D, DOT_VALUE_L)) / 2;
    dot_text(D, DOT_VALUE_L, a, xs[0], ty, cur ? DOT_INK : r % 4 == 0 ? DOT_TEXT : DOT_LABEL, DOT_LEFT, 0);
    for (int i = 0; i < 4 && base + i < ps.nch; i++) {
      struct xmp_event ev;
      if (!event_at(m, ps.pattern, r, base + i, &ev)) continue;
      char ns[8]; note_str(ev.note, ns);
      if (ev.note || ev.ins) { if (ev.ins) snprintf(a, sizeof a, "%s %02d", ns, ev.ins); else snprintf(a, sizeof a, "%s", ns); }
      else snprintf(a, sizeof a, "·");
      int lit = ev.note && !player_muted(base + i);
      dot_text(D, DOT_VALUE_L, a, xs[i + 1], ty, cur ? (lit ? DOT_INK : 0x555555) : lit ? DOT_TEXT : DOT_DIM, DOT_LEFT, 0);
    }
  }
  player_dock(pages > 1 ? "<>" : NULL, "Channels");
}

static void draw_scopes(const struct xmp_module *m) {
  char a[64], t[16], title[100];
  song_title(m, title, sizeof title);
  clock_str(ps.time, t);
  dot_header(D, title, t);
  int page = scope_sel / 8, base = page * 8;
  SDL_Point pts[400];
  int shown = ps.nch - base < 8 ? ps.nch - base : 8, nrows = (shown + 1) / 2;   // the cards share the content area
  int area = DOT_CONTENT_BOTTOM - 96;   // 544 / 496
  int h = nrows ? (area - 16 * (nrows - 1)) / nrows : 0, amp = (h - 60) / 2 - 16;
  for (int k = 0; k < shown; k++) {
    int w = (CW - 16) / 2, c = base + k, x = 40 + (k % 2) * (w + 16), y = 96 + (k / 2) * (h + 16);
    int mu = player_muted(c), sel = c == scope_sel;
    if (sel) dot_rect(D, x, y, w, h, DOT_R_ROW, DOT_PAPER, 255); else dot_card(D, x, y, w, h);
    char l[48]; chan_label(m, c, l, sizeof l);
    int mw = mu ? dot_text_w(D, DOT_LABEL_T, "Muted") + 16 : 0;
    dot_text(D, DOT_LABEL_T, l, x + 30, y + 24, sel ? 0x444444 : mu ? DOT_DIM : DOT_LABEL, DOT_LEFT, w - 60 - mw);
    if (mu) dot_text(D, DOT_LABEL_T, "Muted", x + w - 30, y + 24, sel ? DOT_INK : DOT_TEXT, DOT_RIGHT, 0);
    int cy = y + 60 + (h - 60) / 2, np = 0;
    for (int i = 0; i <= w - 60 && np < 400; i += 2) {
      float v = chan_value(m, c, i) * amp * 2.3f;
      v = v > amp ? amp : v < -amp ? -amp : v;
      pts[np].x = x + 30 + i; pts[np].y = cy - (int)v; np++;
    }
    Uint32 col = sel ? DOT_INK : mu ? DOT_DIM : DOT_TEXT;
    SDL_SetRenderDrawColor(R, col >> 16, col >> 8 & 255, col & 255, 255);
    SDL_RenderDrawLines(R, pts, np);
    for (int i = 0; i < np; i++) pts[i].y++;
    SDL_RenderDrawLines(R, pts, np);
  }
  (void)a;
  dock4("X", "Mute", "Y", "Solo", "SELECT", "Screen", "B", "Browse");
}

static void draw_demo(const struct xmp_module *m) {
  if (ps.pos != demo_last_pos) { if (demo_last_pos >= 0 && ps.pos % 4 == 0) demo_part = (demo_part + 1) % DEMO_PARTS; demo_last_pos = ps.pos; }
  SDL_SetRenderDrawColor(R, 0, 0, 0, 255);
  SDL_RenderClear(R);
  demo_draw(m, &ps, vu, frame_dt, demo_part);
  char title[100], t[16], b[64]; song_title(m, title, sizeof title);
  clock_str(ps.time, t);
  snprintf(b, sizeof b, "%s · %s", DEMO_NAMES[demo_part], t);
  int bw = dot_text_w(D, DOT_PILL_MONO, b) + 48;
  char cut[110]; snprintf(cut, sizeof cut, "%s", title);   // shortened with "…" to leave room for the right pill
  size_t len = strlen(cut);
  while (len > 1 && dot_text_w(D, DOT_UI, cut) + 76 > CW - bw - 24) {
    do len--; while (len > 1 && ((unsigned char)title[len] & 0xc0) == 0x80);   // whole UTF-8 characters
    snprintf(cut, sizeof cut, "%.*s…", (int)len, title);
  }
  dot_pill(D, 40, 36, cut, 1, 0, DOT_LEFT);
  dot_pill(D, RIGHT, 36, b, 0, 1, DOT_RIGHT);
}

// The crumb: "Music", then the last two folders under it.
static void browse_crumb(char *out, size_t n) {
  snprintf(out, n, "Music");
  if (depth == 0) return;
  char rel[1200];
  if (lv[depth].zip[0]) snprintf(rel, sizeof rel, "%s/%s", lv[depth - 1].path + strlen(music_root), lv[depth].zip_name);
  else snprintf(rel, sizeof rel, "%s", lv[depth].path + strlen(music_root));
  char *part[24]; int np = 0;
  for (char *tok = strtok(rel, "/"); tok && np < 24; tok = strtok(NULL, "/")) part[np++] = tok;
  if (np > 2) { size_t k = strlen(out); snprintf(out + k, n - k, " / …"); }
  for (int i = np > 2 ? np - 2 : 0; i < np; i++) { size_t k = strlen(out); snprintf(out + k, n - k, " / %s", part[i]); }
}

static void draw_browse(void) {
  Level *l = &lv[depth];
  char b[400], cnt[48] = "";
  browse_crumb(b, sizeof b);
  if (!failed && l->list.n) snprintf(cnt, sizeof cnt, "%d", l->list.n);
  dot_header(D, b, cnt);

  // what's playing, in the last row's place
  char pill[300] = "";
  if (ps.loaded) snprintf(pill, sizeof pill, "%s · %.150s", ps.paused ? "Paused" : "Playing", now_title[0] ? now_title : now_name);
  int rows = pill[0] ? ROWS - 1 : ROWS;

  if (failed) message_card("This folder can't be read", "Press A to try again.");
  else if (!l->list.n) message_card(depth ? "No songs in this folder" : "No songs yet",
                                    depth ? NULL : "Put MOD, S3M, XM or IT files in the Music folder on the SD card.");
  if (l->cursor >= l->list.n) l->cursor = l->list.n ? l->list.n - 1 : 0;
  if (l->cursor < l->top) l->top = l->cursor;
  if (l->cursor >= l->top + rows) l->top = l->cursor - rows + 1;
  for (int i = 0; i < rows; i++) {
    int k = l->top + i;
    if (k >= l->list.n) break;
    Entry *e = &l->list.e[k];
    char meta[32] = "";
    if (!e->dir) size_str(e->size, meta, sizeof meta);
    int playing = ps.loaded && !e->dir && !strcmp(e->name, now_name);
    dot_row(D, 40, 96 + i * (DOT_ROW_H + 12), CW, e->dir ? DOT_I_FOLDER : DOT_I_NOTE, e->title[0] ? e->title : e->name,
            e->artist[0] ? e->artist : NULL, meta, k == l->cursor ? DOT_FOCUS : 0, playing);
  }
  if (pill[0]) dot_pill(D, 40, DOT_DOCK_Y - 92, pill, 1, 0, DOT_LEFT);   // 572 on the Brick

  const char *ta = NULL;
  if (failed || !l->list.n) ta = "Try again";
  else ta = l->list.e[l->cursor].dir ? "Open" : "Play";
  const char *tb = depth ? "Back" : ps.loaded ? "Player" : NULL;
  dock4(ta ? "A" : NULL, ta, tb ? "B" : NULL, tb, l->list.n > ROWS ? "L1 R1" : NULL, "Letter", l->list.n ? "Y" : NULL, "Shuffle");
}

// --- help: what the buttons do -------------------------------------------------------------------------------------
static int help_tab;   // 0 browsing, 1 playing: L1 R1

// Tabs at the top right, the active one lit, between the L1 and R1 keys (as in Kino).
static void draw_tabs(const char *const *names, int n, int active) {
  int tx = DOT_W - DOT_MARGIN;
  tx -= dot_text_w(D, DOT_KEY_WIDE, "R1") + 40; dot_key(D, tx, 25, "R1", 0, 0); tx -= 12;
  for (int i = n - 1; i >= 0; i--) { tx -= dot_text_w(D, DOT_CHIP_T, names[i]) + 40; dot_chip(D, tx, 28, names[i], i == active ? DOT_ON : 0); tx -= 8; }
  tx -= 4 + dot_text_w(D, DOT_KEY_WIDE, "L1") + 40; dot_key(D, tx, 25, "L1", 0, 0);
}

// One column of keys on the Brick; two side by side where one doesn't fit the height (the Smart Pro).
static void help_panel(const DotHint *h, int n) {
  enum { PITCH = 68 };   // a key cap is 62 high
  int cols = 30 + n * PITCH - 6 + 30 > CB + 2 - 96 ? 2 : 1, per = (n + cols - 1) / cols, colw = CW / cols;
  dot_card(D, 40, 96, CW, 30 + per * PITCH - 6 + 30);
  for (int i = 0; i < n; i++) {
    int x = 70 + (i / per) * colw, y = 96 + 30 + (i % per) * PITCH;
    dot_key(D, x, y, h[i].key, 0, 2);
    dot_text(D, DOT_UI, h[i].text, x + 170, y + (DOT_KEY - dot_line_h(D, DOT_UI)) / 2, DOT_TEXT, DOT_LEFT, 40 + (i / per + 1) * colw - 30 - (x + 170));
  }
}

static void draw_help(void) {
  dot_ground(D);
  dot_title(D, "BUTTONS");
  static const char *const names[2] = { "Browsing", "Playing" };
  draw_tabs(names, 2, help_tab);
  static const DotHint browse[] = { { "A", "Open, play" }, { "B", "Back" }, { "L1 R1", "Jump by letter" },
    { "Y", "Shuffle folder" }, { "SELECT", "Settings" }, { "START", "To the player" }, { "MENU", "Quit" } };
  static const DotHint playing[] = { { "A", "Pause" }, { "B", "Browse" }, { "SELECT", "Next screen" },
    { "L1 R1", "Previous, next" }, { "<>", "Skip 10 s, effect" }, { "X Y", "Mute, solo" }, { "START", "Screen off" } };
  if (help_tab) help_panel(playing, 7); else help_panel(browse, 7);
  dock4("A", "Got it", "L1 R1", help_tab ? "Browsing" : "Playing", NULL, NULL, NULL, NULL);
}

// Text wrapped at spaces into lines of at most w px, lh apart; at most max lines (the last cut with "…").
static void wrap_text(int st, const char *s, int x, int y, int w, int lh, Uint32 col, int max) {
  char line[256];
  for (int n = 0; *s && n < max; n++, y += lh) {
    size_t take = strlen(s), fit = 0;
    if (n + 1 < max && dot_text_w(D, st, s) > w) {
      for (size_t k = 1; k <= take; k++) if (s[k] == ' ' || !s[k]) {
        snprintf(line, sizeof line, "%.*s", (int)k, s);
        if (dot_text_w(D, st, line) > w) break;
        fit = k;
      }
      if (fit) take = fit;
    }
    snprintf(line, sizeof line, "%.*s", (int)take, s);
    dot_text(D, st, line, x, y, col, DOT_LEFT, w);
    s += take; while (*s == ' ') s++;
  }
}

static void draw_settings(void) {
  dot_ground(D);
  dot_title(D, "SETTINGS");
  dot_header(D, NULL, "Musicdisk");
  enum { P = DOT_OPT_PITCH };
  int f[O_N]; for (int i = 0; i < O_N; i++) f[i] = i == opt_sel ? DOT_FOCUS : 0;
  int pw = (CW - DOT_GAP) / 2, x2 = 40 + pw + DOT_GAP, ow = pw - 60;   // two panels: 464 wide on the Brick
  int y = dot_panel(D, 40, 96, pw, dot_panel_h(3 * P), "Sound");
  dot_opt_slider(D, 70, y, ow, "Separation", set.separation / 100.f, f[O_SEP]);
  dot_opt(D, 70, y + P, ow, "Interpolation", INTERP_NAMES[set.interp], f[O_INTERP]);
  dot_opt_toggle(D, 70, y + 2 * P, ow, "Amiga filter", set.filter, f[O_FILTER]);
  y = dot_panel(D, x2, 96, pw, dot_panel_h(3 * P), DW ? "Device" : "Brick");
  dot_opt(D, x2 + 30, y, ow, "Screen off", OFF_NAMES[set.screen_off], f[O_SCREEN]);
  dot_opt(D, x2 + 30, y + P, ow, "Buttons", "", f[O_BUTTONS]);
  static const char *HELP[O_N] = {
    "Amiga songs put each channel hard left or right; less is kinder on headphones.",
    "Nearest gives the crunchy Amiga sound. Spline is the smoothest.",
    "The Amiga's low-pass filter, switched on and off by the songs themselves.",
    "While a song plays and no button is pressed. START: off now. Any button wakes it.",
    "What every button does: press A." };
  static const char *TITLE[O_N] = { "Stereo separation", "Interpolation", "Amiga filter", "Screen off", "Buttons" };
  char t[80];
  if (opt_sel == O_SEP) snprintf(t, sizeof t, "%s · %d %%", TITLE[opt_sel], set.separation); else snprintf(t, sizeof t, "%s", TITLE[opt_sel]);
  int hy = 96 + dot_panel_h(3 * P) + DOT_GAP;   // under the two panels
  dot_card(D, 40, hy, CW, 30 + 36 + 12 + 2 * 36 + 30);
  dot_text(D, DOT_UI, t, 70, hy + 30, DOT_TEXT, DOT_LEFT, CW - 60);
  wrap_text(DOT_BODY_S, HELP[opt_sel], 70, hy + 78, CW - 60, 36, DOT_LABEL, 2);
  dock4("<>", "Change", "B", "Back", NULL, NULL, NULL, NULL);
}

static void settings_button(int b) {
  int d = b == BTN_LEFT ? -1 : b == BTN_RIGHT || b == BTN_A ? 1 : 0;
  switch (b) {
    case BTN_UP: opt_sel = (opt_sel + O_N - 1) % O_N; return;
    case BTN_DOWN: opt_sel = (opt_sel + 1) % O_N; return;
    case BTN_B: case BTN_SELECT: show_settings = 0; return;
    case BTN_MENU: running = 0; return;
  }
  if (!d) return;
  switch (opt_sel) {
    case O_SEP: set.separation += d * 10; if (set.separation < 0) set.separation = 0; if (set.separation > 100) set.separation = 100; break;
    case O_INTERP: set.interp = (set.interp + 3 + d) % 3; break;
    case O_FILTER: set.filter = !set.filter; break;
    case O_SCREEN: set.screen_off = (set.screen_off + 5 + d) % 5; break;
    case O_BUTTONS: if (b == BTN_A) { show_settings = 0; show_help = 1; help_tab = 0; } return;
  }
  settings_apply(); settings_save();
}

static void help_seen(void) {
  show_help = 0;
  char p[800]; snprintf(p, sizeof p, "%s/help-seen", user_dir);
  FILE *f = fopen(p, "w"); if (f) fclose(f);
}

static void draw(void) {
  dot_frame(D);
  dot_set_accent(D, DOT_ACCENT_MUSICDISK);
  const struct xmp_module *m = player_module();
  if (show_help) draw_help();
  else if (show_settings) draw_settings();
  else if (screen == SCR_DEMO && m) { dot_set_night(D, 1); draw_demo(m); dot_set_night(D, 0); }
  else {
    dot_ground(D);
    if (screen == SCR_BROWSE) draw_browse();
    else if (!m) draw_nothing();
    else if (screen == SCR_NOW) draw_now(m);
    else if (screen == SCR_PATTERN) draw_pattern(m);
    else if (screen == SCR_INFO) { char t[100]; song_title(m, t, sizeof t); info_draw(m, t); player_dock("<>", "Page"); }
    else draw_scopes(m);
  }
  Uint32 now = SDL_GetTicks();
  if (now < toast_until) { Uint32 left = toast_until - now; dot_toast(D, toast_msg, left < DOT_FADE_MS ? left / (float)DOT_FADE_MS : 1); }
}

// --- buttons ----------------------------------------------------------------------------------------------------------
static void solo(int c) {
  int others_on = 0;
  for (int i = 0; i < ps.nch; i++) if (i != c && !player_muted(i)) others_on = 1;
  for (int i = 0; i < ps.nch; i++) player_mute(i, others_on ? i != c : 0);
}

// L1/R1 in a list sorted by name: to the start of the previous or next letter.
static int first_letter(const Entry *e) { int c = tolower((unsigned char)e->name[0]); return isalpha(c) ? c : '#'; }
static void letter_jump(Level *l, int dir) {
  int n = l->list.n, k = l->cursor;
  Entry *e = l->list.e;
  if (dir > 0) {
    int c = first_letter(&e[k]), d = e[k].dir;
    while (k < n && first_letter(&e[k]) == c && e[k].dir == d) k++;
    if (k >= n) return;
  } else {
    int c = first_letter(&e[k]), d = e[k].dir;
    while (k > 0 && first_letter(&e[k - 1]) == c && e[k - 1].dir == d) k--;
    if (k == l->cursor && k > 0) { c = first_letter(&e[k - 1]); d = e[k - 1].dir; while (k > 0 && first_letter(&e[k - 1]) == c && e[k - 1].dir == d) k--; }
  }
  l->cursor = k; l->top = k;
  char m[8]; int c = first_letter(&e[k]); snprintf(m, sizeof m, "%c", c == '#' ? '#' : toupper(c)); toast(m);
}

// ◄ ► on Now playing: 10 seconds back or ahead; past the end, the next song.
static void flash(int dir, int is_seek) { flash_dir = dir; flash_seek = is_seek; flash_until = SDL_GetTicks() + 450; }

static void seek(int ms) {
  if (!ps.loaded) return;
  flash(ms < 0 ? -1 : 1, 1);
  if (player_seek(ms) >= 0) { player_get(&ps); return; }
  if (pl_i + 1 < pl_n) { flash(1, 0); play_index(pl_i + 1); } else toast("Last song");
}

static void on_button(int b) {
  if (show_help) {
    if (b == BTN_L1 || b == BTN_R1 || b == BTN_LEFT || b == BTN_RIGHT) { help_tab = !help_tab; return; }
    if (b == BTN_MENU) running = 0;
    help_seen(); return;
  }
  if (show_settings) { settings_button(b); return; }
  if (b == BTN_MENU) { running = 0; return; }
  if (screen == SCR_BROWSE) {
    Level *l = &lv[depth];
    int n = l->list.n;
    switch (b) {
      case BTN_UP: if (n) l->cursor = (l->cursor + n - 1) % n; break;
      case BTN_DOWN: if (n) l->cursor = (l->cursor + 1) % n; break;
      case BTN_L1: case BTN_R1: if (n) letter_jump(l, b == BTN_R1 ? 1 : -1); break;
      case BTN_LEFT: if (depth) leave_dir(); break;
      case BTN_RIGHT:
        if (!n) break;
        if (l->list.e[l->cursor].dir) enter_dir(&l->list.e[l->cursor]);
        else if (is_zip_name(l->list.e[l->cursor].name)) enter_zip(&l->list.e[l->cursor]);
        break;
      case BTN_Y: shuffle_folder(); break;
      case BTN_A:
        if (failed || !n) { open_level(); break; }
        if (l->list.e[l->cursor].dir) enter_dir(&l->list.e[l->cursor]);
        else play_from_listing(l->cursor);
        break;
      case BTN_B: if (depth) leave_dir(); else if (ps.loaded) screen = SCR_NOW; break;
      case BTN_START: if (ps.loaded) screen = SCR_NOW; break;
      case BTN_SELECT: show_settings = 1; break;
    }
    return;
  }
  switch (b) {
    case BTN_B: screen = SCR_BROWSE; break;
    case BTN_START: if (ps.loaded) screen_power(0); break;
    case BTN_SELECT: screen = screen == SCR_NOW ? SCR_DEMO : screen == SCR_DEMO ? SCR_PATTERN : screen == SCR_PATTERN ? SCR_SCOPES : screen == SCR_SCOPES ? SCR_INFO : SCR_NOW; break;
    case BTN_A: if (ps.loaded) player_pause(!ps.paused); break;
    case BTN_L1: if (pl_i > 0) { flash(-1, 0); play_index(pl_i - 1); } break;
    case BTN_R1: if (pl_i + 1 < pl_n) { flash(1, 0); play_index(pl_i + 1); } break;
    case BTN_LEFT: if (screen == SCR_INFO) info_scroll(-INFO_PAGE); if (screen == SCR_NOW) seek(-10000); if (screen == SCR_DEMO) demo_part = (demo_part + DEMO_PARTS - 1) % DEMO_PARTS; if (screen == SCR_PATTERN && pat_page > 0) pat_page--; if (screen == SCR_SCOPES && scope_sel > 0) scope_sel--; break;
    case BTN_RIGHT: if (screen == SCR_INFO) info_scroll(INFO_PAGE); if (screen == SCR_NOW) seek(10000); if (screen == SCR_DEMO) demo_part = (demo_part + 1) % DEMO_PARTS; if (screen == SCR_PATTERN && (pat_page + 1) * 4 < ps.nch) pat_page++; if (screen == SCR_SCOPES && scope_sel + 1 < ps.nch) scope_sel++; break;
    case BTN_UP: if (screen == SCR_SCOPES && scope_sel >= 2) scope_sel -= 2; if (screen == SCR_INFO) info_scroll(-1); break;
    case BTN_DOWN: if (screen == SCR_SCOPES && scope_sel + 2 < ps.nch) scope_sel += 2; if (screen == SCR_INFO) info_scroll(1); break;
    case BTN_X: if (screen != SCR_NOW || 1) player_mute(scope_sel, !player_muted(scope_sel)); break;
    case BTN_Y: solo(scope_sel); break;
  }
}

// --- screenshots without a device: --shots <dir> <module> ------------------------------------------------------------
static void save_shot(const char *dir, const char *name) {
  if (toast_until) { toast_until = 0; draw(); }   // screenshots without a leftover message
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, DOT_W, DOT_H, 32, SDL_PIXELFORMAT_ARGB8888);
  SDL_RenderReadPixels(R, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
  SDL_Surface *rgb = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGB24, 0);   // plain 24-bit BMPs open anywhere
  char p[900]; snprintf(p, sizeof p, "%s/%s.bmp", dir, name);
  SDL_SaveBMP(rgb, p); SDL_FreeSurface(rgb); SDL_FreeSurface(s);
}

static const char *zip_test;
static void shots(const char *dir, const char *module) {
  open_level();
  draw(); save_shot(dir, "browse");
  show_help = 1; help_tab = 0; draw(); save_shot(dir, "help");
  help_tab = 1; draw(); save_shot(dir, "help-playing"); help_tab = 0; show_help = 0;
  show_settings = 1; draw(); save_shot(dir, "settings"); show_settings = 0;
  // a zip with two songs becomes two playlist items
  if (zip_test) {
    free(pl); pl = calloc(1, sizeof *pl); pl_n = 1; pl_i = 0;
    snprintf(pl[0].url, sizeof pl[0].url, "%s", zip_test); snprintf(pl[0].name, sizeof pl[0].name, "pack.zip");
    start_playing(zip_test);
    fprintf(stderr, "zip test: %d playlist items, playing %s\n", pl_n, now_name);
  }
  free(pl); pl = calloc(1, sizeof *pl); pl_n = 1; pl_i = 0;
  const char *slash = strrchr(module, '/');
  snprintf(pl[0].name, sizeof pl[0].name, "%s", slash ? slash + 1 : module);
  snprintf(pl[0].url, sizeof pl[0].url, "%s", module);
  start_playing(module);
  if (!player_module()) { fprintf(stderr, "couldn't load %s\n", module); return; }
  player_pump(900);
  player_get(&ps);
  for (int i = 0; i < 12; i++) update_vu(player_module());
  screen = SCR_BROWSE; draw(); save_shot(dir, "browse-playing");
  screen = SCR_NOW; draw(); save_shot(dir, "now");
  flash(1, 1); draw(); save_shot(dir, "now-seek"); flash_until = 0;
  screen = SCR_PATTERN; draw(); save_shot(dir, "pattern");
  screen = SCR_SCOPES; scope_sel = 2; draw(); save_shot(dir, "scopes");
  player_mute(1, 1);   // a muted channel: does it read as muted?
  screen = SCR_NOW; draw(); save_shot(dir, "now-muted");
  screen = SCR_PATTERN; draw(); save_shot(dir, "pattern-muted");
  screen = SCR_SCOPES; scope_sel = 1; draw(); save_shot(dir, "scopes-muted"); scope_sel = 2;
  player_mute(1, 0);
  screen = SCR_INFO; draw(); save_shot(dir, "info");
  info_scroll(INFO_PAGE); draw(); save_shot(dir, "info-2"); info_scroll(-1000);
  screen = SCR_DEMO;
  for (int part = 0; part < DEMO_PARTS; part++) {
    demo_part = part;
    for (int f = 0; f < 90; f++) { player_pump(1); player_get(&ps); update_vu(player_module()); SDL_RenderClear(R); demo_draw(player_module(), &ps, vu, 1 / 50.f, part); }
    demo_last_pos = ps.pos; draw(); char n[32]; snprintf(n, sizeof n, "demo-%d", part); save_shot(dir, n);
  }
}

static void restore_hw(void) { screen_power(1); }
static void on_signal(int sig) { restore_hw(); signal(sig, SIG_DFL); raise(sig); }

int main(int argc, char **argv) {
  snprintf(user_dir, sizeof user_dir, "%s", argc > 1 ? argv[1] : ".");
  const char *pak = argc > 2 ? argv[2] : ".";
  const char *shots_dir = argc > 4 && !strcmp(argv[3], "--shots") ? argv[4] : NULL;
  mkdir(user_dir, 0755);
  snprintf(cache_dir, sizeof cache_dir, "%s/cache", user_dir); mkdir(cache_dir, 0755);

  const char *sd = getenv("SDCARD_PATH"); if (!sd || !*sd) sd = "/mnt/SDCARD";
  snprintf(music_root, sizeof music_root, "%s/Music", sd);
  mkdir(music_root, 0755);
  snprintf(lv[0].path, sizeof lv[0].path, "%s", music_root);

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
  dot_screen_detect();   // Brick 1024x768 or Smart Pro 1280x720
  SDL_Window *win = SDL_CreateWindow("Musicdisk", 0, 0, DOT_W, DOT_H, shots_dir ? 0 : SDL_WINDOW_FULLSCREEN);
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, shots_dir ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!ren) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
  SDL_RenderSetLogicalSize(ren, DOT_W, DOT_H);
  if (gfx_init(ren, pak) != 0) return 1;
  info_init(pak);
  settings_load(); settings_apply();
  for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);
  if (player_init(!shots_dir) != 0) fprintf(stderr, "audio: %s\n", SDL_GetError());

  if (shots_dir) { zip_test = argc > 6 ? argv[6] : NULL; shots(shots_dir, argc > 5 ? argv[5] : ""); SDL_Quit(); return 0; }

  open_level();

  if (!shots_dir) {   // the screen comes back on however Musicdisk ends
    atexit(restore_hw);
    signal(SIGTERM, on_signal); signal(SIGINT, on_signal); signal(SIGHUP, on_signal);
    signal(SIGSEGV, on_signal); signal(SIGABRT, on_signal); signal(SIGBUS, on_signal); signal(SIGFPE, on_signal);
  }
  srand((unsigned)time(NULL));
  { char p[800]; struct stat st; snprintf(p, sizeof p, "%s/help-seen", user_dir); show_help = stat(p, &st) != 0; }
  int held = -1; Uint32 repeat_at = 0, last_input = SDL_GetTicks();
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = 0;
      if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        int b = e.cbutton.button;
        last_input = SDL_GetTicks();
        if (!screen_is_on()) { screen_power(1); continue; }   // the press that wakes the screen does nothing else
        on_button(b);
        if (b == BTN_UP || b == BTN_DOWN || b == BTN_LEFT || b == BTN_RIGHT) { held = b; repeat_at = SDL_GetTicks() + 400; }
      }
      if (e.type == SDL_CONTROLLERBUTTONUP && e.cbutton.button == held) held = -1;
    }
    if (held >= 0 && SDL_GetTicks() >= repeat_at) {   // held: again and again (seeking slower than the lists)
      int seeking = screen == SCR_NOW && (held == BTN_LEFT || held == BTN_RIGHT);
      on_button(held); repeat_at = SDL_GetTicks() + (seeking ? 300 : 70);
    }
    player_get(&ps);
    if (ps.loaded && ps.ended) {   // the song is over: on to the next one in the playlist
      if (pl_i + 1 < pl_n) play_index(pl_i + 1);
      else { player_unload(); screen = SCR_BROWSE; screen_power(1); }
    }
    { const struct xmp_module *m = player_module(); if (m) update_vu(m); }
    int playing = ps.loaded && !ps.paused;
    if (screen_is_on() && playing && OFF_MS[set.screen_off] && SDL_GetTicks() - last_input > (Uint32)OFF_MS[set.screen_off]) screen_power(0);
    if (!screen_is_on()) {
      if (!playing) { screen_power(1); last_input = SDL_GetTicks(); }   // paused or stopped: the screen comes back
      SDL_Delay(30); continue;   // nothing to draw: the music plays on in the audio thread
    }
    static Uint32 last_tick; Uint32 now_tick = SDL_GetTicks();
    frame_dt = last_tick ? (now_tick - last_tick) / 1000.f : 1 / 60.f; if (frame_dt > .1f) frame_dt = .1f;
    last_tick = now_tick;
    draw();
    SDL_RenderPresent(ren);
  }
  player_unload();
  SDL_Quit();
  return 0;
}

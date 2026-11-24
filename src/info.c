#include "info.h"
#include "gfx.h"
#include "text.h"
#include "zipx.h"
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

// --- CP437, the DOS character set .diz files and old modules are written in ---------------------------------------------
static const char *CP437_LOW[32] = { " ", "☺", "☻", "♥", "♦", "♣", "♠", "•", "◘", " ", "\n", "♂", "♀", "", "♫", "☼",
  "►", "◄", "↕", "‼", "¶", "§", "▬", "↨", "↑", "↓", "→", "←", "∟", "↔", "▲", "▼" };
static const char *CP437_HIGH[129] = { "⌂",
  "Ç", "ü", "é", "â", "ä", "à", "å", "ç", "ê", "ë", "è", "ï", "î", "ì", "Ä", "Å",
  "É", "æ", "Æ", "ô", "ö", "ò", "û", "ù", "ÿ", "Ö", "Ü", "¢", "£", "¥", "₧", "ƒ",
  "á", "í", "ó", "ú", "ñ", "Ñ", "ª", "º", "¿", "⌐", "¬", "½", "¼", "¡", "«", "»",
  "░", "▒", "▓", "│", "┤", "╡", "╢", "╖", "╕", "╣", "║", "╗", "╝", "╜", "╛", "┐",
  "└", "┴", "┬", "├", "─", "┼", "╞", "╟", "╚", "╔", "╩", "╦", "╠", "═", "╬", "╧",
  "╨", "╤", "╥", "╙", "╘", "╒", "╓", "╫", "╪", "┘", "┌", "█", "▄", "▌", "▐", "▀",
  "α", "ß", "Γ", "π", "Σ", "σ", "µ", "τ", "Φ", "Θ", "Ω", "δ", "∞", "φ", "ε", "∩",
  "≡", "±", "≥", "≤", "⌠", "⌡", "÷", "≈", "°", "∙", "·", "√", "ⁿ", "²", "■", " " };

// --- the lines: headings and text, each text line drawn once into a texture ---------------------------------------------
#define COLS 53          // 16 px a character at 32 px: what fits beside the scroll bar
#define LINE_H INFO_LINE_H
#define MAXLINES 1200
typedef struct { char *s; int heading; SDL_Texture *tex; int w; } Line;
static Line lines[MAXLINES];
static int nlines, top;
static TTF_Font *vga;

int info_init(const char *pak_dir) {
  char p[1024]; snprintf(p, sizeof p, "%s/PxPlus_IBM_VGA_8x16.ttf", pak_dir);
  vga = TTF_OpenFont(p, LINE_H);
  if (!vga) { fprintf(stderr, "info: %s\n", TTF_GetError()); return -1; }
  return 0;
}

static void clear(void) {
  for (int i = 0; i < nlines; i++) { free(lines[i].s); if (lines[i].tex) SDL_DestroyTexture(lines[i].tex); }
  memset(lines, 0, sizeof lines); nlines = 0; top = 0;
}

static void add(const char *s, int heading) {
  if (nlines >= MAXLINES) return;
  lines[nlines].s = strdup(s); lines[nlines].heading = heading; nlines++;
}

// One line of CP437 text as UTF-8, wrapped at COLS (at a space when there is one), as text lines.
static void add_cp437_line(const unsigned char *p, size_t len) {
  while (len && (p[len - 1] == ' ' || p[len - 1] == '\r')) len--;
  if (!len) { add("", 0); return; }
  while (len) {
    size_t take = len;
    if (take > COLS) { take = COLS; for (size_t k = COLS; k > COLS / 2; k--) if (p[k] == ' ') { take = k; break; } }
    char out[COLS * 4 + 1]; size_t o = 0;
    for (size_t i = 0; i < take; i++) {
      unsigned char c = p[i];
      const char *u = c < 32 ? CP437_LOW[c] : c >= 127 ? CP437_HIGH[c - 127] : NULL;
      if (u) { size_t n = strlen(u); memcpy(out + o, u, n); o += n; } else out[o++] = (char)c;
    }
    out[o] = 0;
    add(out, 0);
    p += take; len -= take;
    while (len && *p == ' ' && take < len) { p++; len--; }   // the space a wrap broke at
  }
}

// Text as lines: CR, LF or CRLF; tabs to spaces; ANSI colour codes dropped; stops at ^Z (a SAUCE record follows).
static void add_text(const char *text) {
  unsigned char line[1024]; size_t n = 0;
  int blank_run = 0, any = 0;
  for (const unsigned char *p = (const unsigned char *)text; ; p++) {
    unsigned char c = *p;
    if (c == 0x1b && p[1] == '[') { p += 2; while (*p && !((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))) p++; if (!*p) break; continue; }
    int eol = c == '\n' || c == '\r' || c == 0 || c == 0x1a;
    if (!eol) {
      if (c == '\t') { do line[n++] = ' '; while (n % 8 && n < sizeof line - 1); }
      else if (n < sizeof line - 1) line[n++] = c;
      continue;
    }
    if (c == '\r' && p[1] == '\n') p++;
    size_t k = n; while (k && line[k - 1] == ' ') k--;
    if (k) { add_cp437_line(line, k); any = 1; blank_run = 0; }
    else if (any && blank_run++ < 1) add("", 0);   // at most one empty line in a row
    n = 0;
    if (c == 0 || c == 0x1a) break;
  }
  while (nlines && !lines[nlines - 1].heading && !lines[nlines - 1].s[0]) { free(lines[--nlines].s); lines[nlines].s = NULL; }
}

static void section(const char *heading) {
  if (nlines) add("", 0);
  add(heading, 1);
}

// The .diz beside a file: song.mod -> song.diz (or .DIZ).
static char *diz_beside(const char *path) {
  char p[1200]; snprintf(p, sizeof p, "%s", path);
  char *dot = strrchr(p, '.'), *slash = strrchr(p, '/');
  if (!dot || (slash && dot < slash)) return NULL;
  strcpy(dot, ".diz"); char *t = read_text_file(p, 65536);
  if (!t) { strcpy(dot, ".DIZ"); t = read_text_file(p, 65536); }
  return t;
}

void info_build(const char *song, const char *origin, const struct xmp_module *m, const char *comment) {
  clear();
  (void)song;
  char *diz = diz_beside(origin);
  const char *what = ".diz";
  if (!diz && is_zip_name(origin)) { diz = zip_diz(origin); what = "file_id.diz"; }
  if (diz) { section(what); add_text(diz); free(diz); }
  if (comment && comment[0]) { section("Message"); add_text(comment); }
  if (m) {
    // the instrument names, where the old greetings live: up to the last one that says something
    int n = m->ins, use_smp = 0;
    if (n <= 0) { n = m->smp; use_smp = 1; }
    int last = -1;
    for (int i = 0; i < n; i++) { const char *nm = use_smp ? m->xxs[i].name : m->xxi[i].name; for (const char *c = nm; *c; c++) if (*c != ' ') { last = i; break; } }
    if (last >= 0) {
      section(use_smp ? "Samples" : "Instruments");
      for (int i = 0; i <= last; i++) {
        char l[80]; snprintf(l, sizeof l, "%02d %s", i + 1, use_smp ? m->xxs[i].name : m->xxi[i].name);
        add_cp437_line((const unsigned char *)l, strlen(l));
      }
    }
  }
  if (!nlines) { add("Nothing to read in this song.", 0); }
}

void info_scroll(int d) {
  int max = nlines - INFO_PAGE - 1; if (max < 0) max = 0;
  top += d;
  if (top > max) top = max;
  if (top < 0) top = 0;
}

void info_draw(const struct xmp_module *m, const char *title) {
  char right[64], v[96];
  const char *dot = m->type;
  snprintf(right, sizeof right, "%d CH", m->chn);
  dot_header(D, title, right);
  dot_card(D, DOT_MARGIN, 96, DOT_CONTENT_W, DOT_CONTENT_BOTTOM - 96);
  // the facts: label over value
  int pat = m->pat, ins = m->ins > 0 ? m->ins : m->smp;
  // the columns spread over the card's width: 76, 370, 560, 716, 858 on the Brick (SPEED ends by 954, 30 px inside)
  static const int bx[5] = { 76, 370, 560, 716, 858 };
  int xs[5]; for (int i = 0; i < 5; i++) xs[i] = 76 + (bx[i] - 76) * (DOT_CONTENT_W - 72) / 872;
  const char *labels[5] = { "Tracker", "Patterns", "Instr.", "Tempo", "Speed" };
  for (int i = 0; i < 5; i++) {
    dot_text(D, DOT_LABEL_T, labels[i], xs[i], 126, DOT_LABEL, DOT_LEFT, 0);
    if (i == 0) snprintf(v, sizeof v, "%s", dot);
    else if (i == 1) snprintf(v, sizeof v, "%d", pat);
    else if (i == 2) snprintf(v, sizeof v, "%d", ins);
    else if (i == 3) snprintf(v, sizeof v, "%d", m->bpm);
    else snprintf(v, sizeof v, "%d", m->spd);
    dot_text(D, DOT_VALUE, v, xs[i], 162, DOT_TEXT, DOT_LEFT, i == 0 ? xs[1] - 76 - 24 : 0);
  }
  for (int x = 76; x <= DOT_W - DOT_MARGIN - 36; x += 18) dot_circle(D, x, 212, 2, DOT_TRACK);
  // the text, INFO_PAGE + 1 lines
  int y0 = 230;
  for (int i = 0; i <= INFO_PAGE && top + i < nlines; i++) {
    Line *l = &lines[top + i];
    int y = y0 + i * LINE_H;
    if (l->heading) { dot_text(D, DOT_LABEL_T, l->s, 76, y - 4, DOT_LABEL, DOT_LEFT, 0); continue; }
    if (!l->s[0] || !vga) continue;
    if (!l->tex) {
      SDL_Surface *s = TTF_RenderUTF8_Solid(vga, l->s, (SDL_Color){ 0xf2, 0xf2, 0xf0, 255 });
      if (!s) continue;
      l->tex = SDL_CreateTextureFromSurface(R, s); l->w = s->w; SDL_FreeSurface(s);
    }
    int w, h; SDL_QueryTexture(l->tex, NULL, NULL, &w, &h);
    SDL_RenderCopy(R, l->tex, NULL, &(SDL_Rect){ 76, y, w, h });
  }
  // the scroll bar, when there's more than a page
  if (nlines > INFO_PAGE + 1) {
    int th = (INFO_PAGE + 1) * LINE_H, bh = th * (INFO_PAGE + 1) / nlines; if (bh < 30) bh = 30;
    int max = nlines - INFO_PAGE - 1, by = y0 + (th - bh) * top / (max ? max : 1);
    dot_rect(D, DOT_W - 74, y0, 6, th, 3, DOT_TRACK, 255);   // 950 on the Brick
    dot_rect(D, DOT_W - 74, by, 6, bh, 3, DOT_TEXT, 255);
  }
}

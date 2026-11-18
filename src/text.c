#define _GNU_SOURCE
#include "text.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

char *read_text_file(const char *path, size_t max) {
  FILE *f = fopen(path, "rb"); if (!f) return NULL;
  char *b = malloc(max + 1);
  size_t n = fread(b, 1, max, f);
  fclose(f);
  b[n] = 0;
  return b;
}

// A line as plain ASCII with single spaces, or empty when it's mostly ASCII art.
static void clean_line(const char *in, size_t len, char *out, size_t n) {
  size_t o = 0; int alnum = 0, total = 0;
  for (size_t i = 0; i < len && o + 1 < n; i++) {
    unsigned char c = (unsigned char)in[i];
    if (c < 32 || c > 126) c = ' ';
    if (c == ' ' && (o == 0 || out[o - 1] == ' ')) continue;
    out[o++] = (char)c; total++;
    if (isalnum(c)) alnum++;
  }
  while (o && out[o - 1] == ' ') o--;
  out[o] = 0;
  if (alnum < 3 || alnum * 2 < total) out[0] = 0;
}

static void copy_trim(char *dst, size_t n, const char *s, size_t len) {
  while (len && (*s == ' ' || *s == '"' || *s == '\'')) { s++; len--; }
  while (len && (s[len - 1] == ' ' || s[len - 1] == '"' || s[len - 1] == '\'' || s[len - 1] == ',')) len--;
  snprintf(dst, n, "%.*s", (int)len, s);
}

void diz_parse(const char *text, char *title, size_t tn, char *artist, size_t an) {
  title[0] = artist[0] = 0;
  const char *p = text;
  int lines = 0;
  while (*p && lines < 12) {
    const char *e = strchr(p, '\n'); size_t len = e ? (size_t)(e - p) : strlen(p);
    char l[256]; clean_line(p, len < 255 ? len : 255, l, sizeof l);
    p = e ? e + 1 : p + len; lines++;
    if (!l[0]) continue;
    if (!title[0]) {
      const char *by = strcasestr(l, " by ");
      if (by) { copy_trim(title, tn, l, by - l); copy_trim(artist, an, by + 4, strlen(by + 4)); }
      else copy_trim(title, tn, l, strlen(l));
      if (artist[0]) return;
    } else {
      if (!strncasecmp(l, "by ", 3)) copy_trim(artist, an, l + 3, strlen(l + 3));
      return;   // only the line straight after the title can name the artist
    }
  }
}

static void lower(const char *s, char *out, size_t n) { size_t i = 0; for (; s[i] && i + 1 < n; i++) out[i] = (char)tolower((unsigned char)s[i]); out[i] = 0; }

int results_rank(const char *results, const char *title, const char *file) {
  char t[128] = "", f[128] = "";
  if (title && strlen(title) >= 4) lower(title, t, sizeof t);
  if (file) { lower(file, f, sizeof f); char *dot = strrchr(f, '.'); if (dot) *dot = 0; if (strlen(f) < 4) f[0] = 0; }
  if (!t[0] && !f[0]) return 0;
  for (const char *p = results; *p; ) {
    const char *e = strchr(p, '\n'); size_t len = e ? (size_t)(e - p) : strlen(p);
    char l[300]; lower(p, l, len + 1 < sizeof l ? len + 1 : sizeof l);
    p = e ? e + 1 : p + len;
    if (!((t[0] && strstr(l, t)) || (f[0] && strstr(l, f)))) continue;
    const char *q = l; while (*q == ' ' || *q == '\t') q++;
    if (!isdigit((unsigned char)*q)) continue;
    int rank = atoi(q);
    while (isdigit((unsigned char)*q)) q++;
    if (*q == '.' || *q == ')' || *q == ' ' || *q == '\t' || !strncmp(q, "st", 2) || !strncmp(q, "nd", 2) || !strncmp(q, "rd", 2) || !strncmp(q, "th", 2))
      if (rank > 0 && rank < 200) return rank;
  }
  return 0;
}

int music_dir_name(const char *name) {
  static const char *KEYS[] = { "mus", "msx", "chip", "track", "m4ch", "mmul", "4ch", "8ch", "multich", "modul", "tune", "song", "sound", NULL };
  char l[192]; lower(name, l, sizeof l);
  for (int i = 0; KEYS[i]; i++) if (strstr(l, KEYS[i])) return 1;
  return 0;
}

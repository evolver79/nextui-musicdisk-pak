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
      char *t = l;
      if (*t == '(' || *t == '[') { char *close = strchr(t, *t == '(' ? ')' : ']'); if (close) { t = close + 1; while (*t == ' ') t++; } }   // "(MOD) ..."
      const char *by = strcasestr(t, " by "), *colon = strstr(t, " : ");
      if (by) { copy_trim(title, tn, t, by - t); copy_trim(artist, an, by + 4, strlen(by + 4)); }
      else if (colon) { copy_trim(artist, an, t, colon - t); copy_trim(title, tn, colon + 3, strlen(colon + 3)); }   // Artist : Title
      else copy_trim(title, tn, t, strlen(t));
      if (artist[0]) return;
    } else {
      if (!strncasecmp(l, "by ", 3)) copy_trim(artist, an, l + 3, strlen(l + 3));
      return;   // only the line straight after the title can name the artist
    }
  }
}

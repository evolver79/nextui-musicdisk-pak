#include "browse.h"
#include "http.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const char *EXT[] = {
  "mod", "s3m", "xm", "it", "mtm", "669", "med", "mmd0", "mmd1", "mmd2", "mmd3", "okt", "stm", "far", "ult", "ptm",
  "dbm", "amf", "dsm", "mdl", "psm", "umx", "imf", "gdm", "liq", "mgt", "rtm", "fnk", "ams", "dmf", "stx", "emod",
  "mdz", "s3z", "xmz", "itz", "mdr", "zip", "lha", "lzh", "gz", "bz2", "xz", NULL };

int playable_name(const char *name) {
  const char *dot = strrchr(name, '.');
  for (int i = 0; EXT[i]; i++) {
    if (dot && !strcasecmp(dot + 1, EXT[i])) return 1;
    // Amiga style: mod.songname
    size_t n = strlen(EXT[i]);
    if (!strncasecmp(name, EXT[i], n) && name[n] == '.' && i < 32) return 1;
  }
  return 0;
}

void size_str(long b, char *out, int n) {
  if (b < 0) snprintf(out, n, "");
  else if (b < 1024) snprintf(out, n, "%ld B", b);
  else if (b < 1024 * 1024) snprintf(out, n, "%ld KB", (b + 512) / 1024);
  else snprintf(out, n, "%.1f MB", b / 1048576.0);
}

void listing_free(Listing *l) { free(l->e); memset(l, 0, sizeof *l); }

static Entry *add(Listing *l) {
  if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 64; l->e = realloc(l->e, l->cap * sizeof *l->e); }
  Entry *e = &l->e[l->n++];
  memset(e, 0, sizeof *e);
  return e;
}

static int by_kind_then_name(const void *a, const void *b) {
  const Entry *x = a, *y = b;
  if (x->dir != y->dir) return y->dir - x->dir;
  return strcasecmp(x->name, y->name);
}

int list_local(const char *dir, Listing *l) {
  listing_free(l);
  DIR *d = opendir(dir);
  if (!d) return -1;
  struct dirent *de;
  while ((de = readdir(d))) {
    if (de->d_name[0] == '.') continue;
    char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, de->d_name);
    struct stat st;
    if (stat(p, &st) != 0) continue;
    int isdir = S_ISDIR(st.st_mode);
    if (!isdir && !playable_name(de->d_name)) continue;
    Entry *e = add(l);
    snprintf(e->name, sizeof e->name, "%s", de->d_name);
    snprintf(e->href, sizeof e->href, "%s", de->d_name);
    e->dir = isdir; e->size = isdir ? -1 : st.st_size;
  }
  closedir(d);
  qsort(l->e, l->n, sizeof *l->e, by_kind_then_name);
  return 0;
}

static void unescape_html(char *s) {
  static const struct { const char *ent; char c; } E[] = { { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&#39;", '\'' } };
  char *o = s;
  for (char *p = s; *p; ) {
    int hit = 0;
    if (*p == '&') for (size_t i = 0; i < sizeof E / sizeof *E; i++) { size_t n = strlen(E[i].ent); if (!strncmp(p, E[i].ent, n)) { *o++ = E[i].c; p += n; hit = 1; break; } }
    if (!hit) *o++ = *p++;
  }
  *o = 0;
}

// The mirror's index pages (lighttpd style): <td class="n"><a href="x/">x</a>/</td> ... <td class="s">5.1M</td>
int list_remote(const char *url, Listing *l) {
  listing_free(l);
  char *b; size_t n;
  int st = http_get(url, &b, &n, NULL, NULL);
  if (st != 200) { free(b); return st ? st : -1; }
  for (const char *pre = "<td class=\"n\"><a href=\"", *p = b; (p = strstr(p, pre)); ) {
    p += strlen(pre);
    char *q = strchr(p, '"'); if (!q) break;
    char href[384]; snprintf(href, sizeof href, "%.*s", (int)(q - p), p);
    char *gt = strchr(q, '>'), *end = gt ? strstr(gt, "</a>") : NULL;
    if (!end) break;
    char name[192]; snprintf(name, sizeof name, "%.*s", (int)(end - gt - 1), gt + 1);
    unescape_html(name);
    p = end;
    if (!strcmp(href, "../")) continue;
    int dir = href[strlen(href) - 1] == '/';
    if (!dir && !playable_name(name)) continue;
    Entry *e = add(l);
    snprintf(e->name, sizeof e->name, "%s", name);
    snprintf(e->href, sizeof e->href, "%s", href);
    e->dir = dir; e->size = -1;
    char *s = strstr(p, "<td class=\"s\">");
    char *next = strstr(p, "<td class=\"n\">");
    if (s && (!next || s < next) && !dir) {
      double v = atof(s + 14); char *u = s + 14; while (*u && (isdigit((unsigned char)*u) || *u == '.')) u++;
      e->size = (long)(v * (*u == 'K' ? 1024 : *u == 'M' ? 1048576 : *u == 'G' ? 1073741824.0 : 1));
    }
  }
  free(b);
  qsort(l->e, l->n, sizeof *l->e, by_kind_then_name);
  return 0;
}

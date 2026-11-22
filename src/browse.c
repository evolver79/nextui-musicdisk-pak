#include "browse.h"
#include "text.h"
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

static int by_path(const void *a, const void *b) { return strcasecmp(((const Entry *)a)->href, ((const Entry *)b)->href); }

// A song's title and artist from the .diz beside it (song.mod -> song.diz), when the folder has one.
static void read_diz(Entry *e, const char *dir, char **names, int nnames) {
  const char *dot = strrchr(e->name, '.');
  size_t stem = dot ? (size_t)(dot - e->name) : strlen(e->name);
  for (int k = 0; k < nnames; k++) {
    if (strlen(names[k]) != stem + 4 || strncasecmp(names[k], e->name, stem) || strcasecmp(names[k] + stem, ".diz")) continue;
    char p[1400]; snprintf(p, sizeof p, "%s/%s", dir, names[k]);
    char *t = read_text_file(p, 16384);
    if (t) { diz_parse(t, e->title, sizeof e->title, e->artist, sizeof e->artist); free(t); }
    return;
  }
}

// One folder: its playable files (and, when deep, every folder under it) into l.
static void scan(const char *dir, Listing *l, int deep, int depth) {
  DIR *d = opendir(dir);
  if (!d) return;
  char **names = NULL; int nn = 0, cn = 0;
  int first = l->n;
  struct dirent *de;
  while ((de = readdir(d))) {
    if (de->d_name[0] == '.') continue;
    if (nn == cn) { cn = cn ? cn * 2 : 128; names = realloc(names, cn * sizeof *names); }
    names[nn++] = strdup(de->d_name);
    char p[1400]; snprintf(p, sizeof p, "%s/%s", dir, de->d_name);
    struct stat st;
    if (stat(p, &st) != 0) continue;
    int isdir = S_ISDIR(st.st_mode);
    if (isdir && deep) { if (depth < 12) scan(p, l, 1, depth + 1); continue; }
    if (!isdir && !playable_name(de->d_name)) continue;
    Entry *e = add(l);
    snprintf(e->name, sizeof e->name, "%s", de->d_name);
    snprintf(e->href, sizeof e->href, "%s", deep ? p : de->d_name);
    e->dir = isdir; e->size = isdir ? -1 : st.st_size;
  }
  closedir(d);
  for (int i = first; i < l->n; i++) if (!l->e[i].dir && (!deep || strchr(l->e[i].href + strlen(dir) + 1, '/') == NULL)) read_diz(&l->e[i], dir, names, nn);
  for (int i = 0; i < nn; i++) free(names[i]);
  free(names);
}

int list_local(const char *dir, Listing *l) {
  listing_free(l);
  DIR *d = opendir(dir);
  if (!d) return -1;
  closedir(d);
  scan(dir, l, 0, 0);
  qsort(l->e, l->n, sizeof *l->e, by_kind_then_name);
  return 0;
}

int list_tree(const char *dir, Listing *l) {
  listing_free(l);
  scan(dir, l, 1, 0);
  qsort(l->e, l->n, sizeof *l->e, by_path);
  return 0;
}

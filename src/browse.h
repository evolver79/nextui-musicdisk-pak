// Folder listings: the SD card, and scene.org's HTTP mirror.
#pragma once
#include <stddef.h>

typedef struct {
  char name[192]; char href[384]; int dir; long size; long mtime; char where[160];
  char title[96], artist[64];   // from the song's .diz, when it has one
  char diz[192];                // the .diz beside it on the server ("" when none)
  int diz_state;                // 0 not read yet, 1 fetching, 2 read, 3 none to be had
  int rank;                     // its place in the compo, from results.txt (0: unknown)
  int music;                    // a folder that holds music (at parties)
} Entry;
typedef struct { Entry *e; int n, cap; int has_results; } Listing;

void listing_free(Listing *l);
int  list_local(const char *dir, Listing *l);      // 0 on success
int  list_remote(const char *url, Listing *l);     // 0 on success, else the HTTP status or -1
// Every downloaded song under the cache, newest first; href is the full path, where the folder it came from.
int  list_cache(const char *cache_dir, Listing *l);
void url_decode(const char *in, char *out, size_t n);
int  playable_name(const char *name);              // modules, and archives that may hold one
void size_str(long bytes, char *out, int n);

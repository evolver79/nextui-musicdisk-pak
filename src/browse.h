// Folder listings on the SD card.
#pragma once
#include <stddef.h>

typedef struct {
  char name[192]; char href[1100]; int dir; long size;
  char title[96], artist[64];   // from the song's .diz beside it, when it has one
} Entry;
typedef struct { Entry *e; int n, cap; } Listing;

void listing_free(Listing *l);
int  list_local(const char *dir, Listing *l);      // 0 on success; href is the name in the folder
// Every song under dir and its folders, by path; href is the full path. For shuffling a whole folder.
int  list_tree(const char *dir, Listing *l);
int  playable_name(const char *name);              // modules, and archives that may hold one
void size_str(long bytes, char *out, int n);

// Folder listings: the SD card, and scene.org's HTTP mirror.
#pragma once

typedef struct { char name[192]; char href[384]; int dir; long size; } Entry;
typedef struct { Entry *e; int n, cap; } Listing;

void listing_free(Listing *l);
int  list_local(const char *dir, Listing *l);      // 0 on success
int  list_remote(const char *url, Listing *l);     // 0 on success, else the HTTP status or -1
int  playable_name(const char *name);              // modules, and archives that may hold one
void size_str(long bytes, char *out, int n);

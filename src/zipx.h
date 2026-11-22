// Musicdisks and packs often hold many songs in one zip; libxmp would play only the first. This finds every module
// in a zip and unpacks them (once) to out_dir, as 01-name, 02-name..., in the zip's order.
#pragma once

typedef struct { char path[1100]; char name[192]; } ZipSong;

// Returns how many modules the zip holds. Only when there are two or more are they unpacked and listed in songs.
int zip_songs(const char *zip_path, const char *out_dir, ZipSong *songs, int max);
int is_zip_name(const char *name);
// The zip's FILE_ID.DIZ (or another .diz in it), malloc'ed and 0-terminated; NULL when it has none.
char *zip_diz(const char *zip_path);

// Reading the scene's own text files: a song's .diz (title and artist) and a party's results.txt (the placings).
#pragma once
#include <stddef.h>

// "Too Much Cornflakes" by Snipe/RelE  ->  title "Too Much Cornflakes", artist "Snipe/RelE"
void diz_parse(const char *text, char *title, size_t tn, char *artist, size_t an);
// The place a song took in results.txt, found by its title or file name; 0 when it isn't there. *section says which
// compo list it was found in (results.txt lists them all), so a folder can keep to the list most of its songs are in.
// With only_section >= 0, only that list counts.
int  results_rank(const char *results, const char *title, const char *file, int only_section, int *section);
// Folder names that hold music at a party: music, msx, m4ch, mmul, chip, tracked...
int  music_dir_name(const char *name);
char *read_text_file(const char *path, size_t max);

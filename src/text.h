// Reading a song's .diz: the scene's little text file with its title and artist.
#pragma once
#include <stddef.h>

// "Too Much Cornflakes" by Snipe/RelE  ->  title "Too Much Cornflakes", artist "Snipe/RelE"
void diz_parse(const char *text, char *title, size_t tn, char *artist, size_t an);
char *read_text_file(const char *path, size_t max);

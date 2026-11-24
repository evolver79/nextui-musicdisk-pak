// The Info screen: the module's facts, its .diz in the VGA font (CP437), its own message and the instrument names.
#pragma once
#include <xmp.h>
#include "dot_tokens.h"

int  info_init(const char *pak_dir);   // the VGA font, from <pak>/PxPlus_IBM_VGA_8x16.ttf
// song: the file that plays; origin: the file on the card it came from (the zip, for songs unpacked from one).
void info_build(const char *song, const char *origin, const struct xmp_module *m, const char *comment);
void info_draw(const struct xmp_module *m, const char *title);
void info_scroll(int lines);           // + down, - up
#define INFO_LINE_H 32
// lines a page scrolls by: the card's text holds INFO_PAGE + 1 lines from y 230 (12 on the Brick, 10 on the Smart Pro)
#define INFO_PAGE ((DOT_CONTENT_BOTTOM - 26 - 230) / INFO_LINE_H - 1)

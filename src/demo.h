// Demo screen: line-art demo effects (starfield, donut, tunnel, twister, copper bars,
// dot ball, plasma, bobs, sine scroller) driven by the song, over a ticker with the
// song's title and sample names.
#pragma once
#include "player.h"

#define DEMO_PARTS 9
extern const char *DEMO_NAMES[DEMO_PARTS];

void demo_draw(const struct xmp_module *m, const PlayerState *ps, const float *vu, float dt, int part);

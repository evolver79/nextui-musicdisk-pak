// Drawing for the Pixel look: warm paper, ink, one neon green for what is selected or playing, Departure Mono
// (a pixel font, drawn at multiples of its 11 px grid so it stays sharp), dotted leaders, double dashed dividers,
// boxes with dithered shadows and ruler ticks round the edge.
#pragma once
#include <SDL2/SDL.h>

// W_MED is a pixel bold: the font drawn twice, one font pixel apart. W_LIGHT and W_REG are the same.
enum { W_LIGHT, W_REG, W_MED };
enum { A_LEFT, A_RIGHT, A_CENTER };

extern SDL_Renderer *R;
extern const SDL_Color C_BG, C_INK, C_MUTE, C_RULE, C_ACC, C_SURF;

int  gfx_init(SDL_Renderer *ren, const char *pak_dir);
// Draws s with its baseline at y. Returns the width drawn.
int  text(int x, int y, const char *s, int size, int weight, SDL_Color c, int align);
int  text_w(const char *s, int size, int weight);
// Copies s into out, cut with "..." so it fits in max_w pixels. from_left keeps the end of the string instead.
void fit(char *out, size_t n, const char *s, int max_w, int size, int weight, int from_left);
void fill_rect(int x, int y, int w, int h, SDL_Color c);
void rule(int x1, int x2, int y, SDL_Color c);
void frame(int x, int y, int w, int h, SDL_Color c);            // a 2 px outline
void dither(int x, int y, int w, int h, SDL_Color c);           // a 2 px checkerboard of c over what is there
void dotted(int x1, int x2, int y, SDL_Color c);                // a leader: one dot every 8 px
void double_dash(int x1, int x2, int y, SDL_Color c);           // the section divider: two dashed lines
// A box: paper inside, ink outline, dithered shadow down-right. fill may be C_ACC for a selected one.
void panel(int x, int y, int w, int h, SDL_Color fill);
// A box with a title bar (dithered grey, or green when lit), like a little window. Returns the y under the bar.
int  window(int x, int y, int w, int h, const char *title, int lit);
// "[ label ]", green when on. Returns its width.
int  tab(int x, int y, const char *label, int on);
// A section heading followed by a dotted leader out to x2. right, if any, sits at x2.
void heading(int x, int y, int x2, const char *s, const char *right, int size, SDL_Color c);
void gfx_clear(void);

// Module playback with libxmp, plus what the screens need: position, pattern data, and each channel's
// sample and play position for the scopes.
#pragma once
#include <xmp.h>
#include <SDL2/SDL.h>

#define PL_MAXCH 64

typedef struct {
  int loaded, paused, ended;
  int pos, pattern, row, num_rows, speed, bpm, time, total;
  int nch;
  struct xmp_channel_info ci[PL_MAXCH];
  float step[PL_MAXCH];   // sample frames advanced per output frame (the channel's pitch)
} PlayerState;

int  player_init(int with_audio);     // without audio (screenshots), player_pump() drives playback
int  player_load(const char *path);   // 0 on success; archives (.zip, .lha, .gz...) are unpacked by libxmp
void player_unload(void);
void player_pause(int paused);
// Sound settings, kept for the next song too: stereo separation 0..100, interpolation (XMP_INTERP_*), Amiga filter.
void player_sound(int separation, int interp, int amiga_filter);
// Jumps by delta_ms, to the exact frame.
// Returns the new time in ms, or -1 when that would be past the end.
int  player_seek(int delta_ms);
void player_get(PlayerState *s);
const struct xmp_module *player_module(void);
const char *player_comment(void);      // the module's own message (IT, MED...), or NULL
void player_mute(int ch, int muted);
int  player_muted(int ch);
void player_pump(int frames);         // screenshots only: plays this many frames without an audio device

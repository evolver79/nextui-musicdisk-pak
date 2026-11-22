#include "player.h"
#include <string.h>

#define RATE 48000

static xmp_context ctx;
static SDL_AudioDeviceID dev;
static struct xmp_module_info info;
static PlayerState st;               // written by the audio thread, read under the audio lock
static unsigned char buf[16384];
static int buf_len, buf_pos;
static int muted[PL_MAXCH];
static int sep = 70, interp = XMP_INTERP_SPLINE, filt = 1;

static void apply_sound(void) {
  xmp_set_player(ctx, XMP_PLAYER_INTERP, interp);
  xmp_set_player(ctx, XMP_PLAYER_MIX, sep);
  xmp_set_player(ctx, XMP_PLAYER_DSP, filt ? XMP_DSP_LOWPASS : 0);
}

static void lock(void) { if (dev) SDL_LockAudioDevice(dev); }
static void unlock(void) { if (dev) SDL_UnlockAudioDevice(dev); }

// One frame of the module: mix it, and note where every channel is for the screens.
static int next_frame(void) {
  if (xmp_play_frame(ctx) != 0) return 0;
  struct xmp_frame_info fi;
  xmp_get_frame_info(ctx, &fi);
  if (fi.loop_count > 0) return 0;   // the song has started over: that's the end
  int frames = fi.buffer_size / 4;
  for (int c = 0; c < st.nch; c++) {
    struct xmp_channel_info *now = &fi.channel_info[c], *was = &st.ci[c];
    int moved = (int)now->position - (int)was->position;
    if (now->sample == was->sample && moved > 0 && frames > 0) st.step[c] = (float)moved / frames;
    st.ci[c] = *now;
  }
  st.pos = fi.pos; st.pattern = fi.pattern; st.row = fi.row; st.num_rows = fi.num_rows;
  st.speed = fi.speed; st.bpm = fi.bpm; st.time = fi.time; st.total = fi.total_time;
  int n = fi.buffer_size < (int)sizeof buf ? fi.buffer_size : (int)sizeof buf;
  memcpy(buf, fi.buffer, n);
  buf_len = n; buf_pos = 0;
  return 1;
}

static void fill(void *ud, Uint8 *out, int len) {
  (void)ud;
  while (len > 0) {
    if (!st.loaded || st.paused || st.ended) { memset(out, 0, len); return; }
    if (buf_pos >= buf_len && !next_frame()) { st.ended = 1; continue; }
    int n = buf_len - buf_pos < len ? buf_len - buf_pos : len;
    memcpy(out, buf + buf_pos, n);
    out += n; len -= n; buf_pos += n;
  }
}

int player_init(int with_audio) {
  ctx = xmp_create_context();
  if (!with_audio) return 0;
  SDL_AudioSpec want = {0}, have;
  want.freq = RATE; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = fill;
  dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (!dev) return -1;
  SDL_PauseAudioDevice(dev, 0);
  return 0;
}

void player_unload(void) {
  lock();
  if (st.loaded) { xmp_end_player(ctx); xmp_release_module(ctx); }
  memset(&st, 0, sizeof st);
  buf_len = buf_pos = 0;
  unlock();
}

int player_load(const char *path) {
  player_unload();
  lock();
  int r = xmp_load_module(ctx, path);
  if (r == 0) r = xmp_start_player(ctx, RATE, 0);
  if (r == 0) {
    xmp_get_module_info(ctx, &info);
    apply_sound();
    st.nch = info.mod->chn < PL_MAXCH ? info.mod->chn : PL_MAXCH;
    for (int c = 0; c < st.nch; c++) { st.step[c] = 1; muted[c] = 0; }
    st.loaded = 1;
  }
  unlock();
  return r;
}

int player_seek(int delta_ms) {
  if (!st.loaded || st.ended) return -1;
  lock();
  int target = st.time + delta_ms;
  if (target < 0) target = 0;
  if (st.total > 0 && target >= st.total) { unlock(); return -1; }
  xmp_seek_time_frame(ctx, target);   // to the exact frame (plays its way there from the nearest position)
  buf_len = buf_pos = 0;              // drop what was mixed at the old place
  next_frame();
  int t = st.time;
  unlock();
  return t;
}

void player_sound(int s, int i, int f) {
  lock(); sep = s; interp = i; filt = f;
  if (st.loaded) apply_sound();
  unlock();
}

void player_pause(int p) { lock(); st.paused = p; unlock(); }
void player_get(PlayerState *s) { lock(); *s = st; unlock(); }
const char *player_comment(void) { return st.loaded ? info.comment : NULL; }
const struct xmp_module *player_module(void) { return st.loaded ? info.mod : NULL; }
void player_mute(int ch, int m) { if (ch < 0 || ch >= PL_MAXCH) return; lock(); muted[ch] = m; xmp_channel_mute(ctx, ch, m); unlock(); }
int player_muted(int ch) { return ch >= 0 && ch < PL_MAXCH && muted[ch]; }

void player_pump(int frames) {
  for (int i = 0; i < frames && st.loaded && !st.ended; i++) if (!next_frame()) st.ended = 1;
}

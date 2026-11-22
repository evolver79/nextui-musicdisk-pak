#include "hw.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

// --- backlight: the display driver's brightness ioctl, as NextUI's own settings library uses it ----------------------
#define DISP_LCD_SET_BRIGHTNESS 0x102
#define DISP_LCD_GET_BRIGHTNESS 0x103
static int screen_on = 1, saved_brightness = -1;

static int disp(unsigned long cmd, int val) {
  int fd = open("/dev/disp", O_RDWR);
  if (fd < 0) return -1;
  unsigned long param[4] = { 0, (unsigned long)val, 0, 0 };
  int r = ioctl(fd, cmd, &param);
  close(fd);
  return r;
}

void screen_power(int on) {
  if (on == screen_on) return;
  if (!on) { saved_brightness = disp(DISP_LCD_GET_BRIGHTNESS, 0); disp(DISP_LCD_SET_BRIGHTNESS, 0); }
  else disp(DISP_LCD_SET_BRIGHTNESS, saved_brightness > 0 ? saved_brightness : 128);
  screen_on = on;
}
int screen_is_on(void) { return screen_on; }

// --- LEDs: the TrimUI led_anim driver --------------------------------------------------------------------------------
// With the zone effects off (effect_enable 0), frame_hex sets every LED. The driver hangs on a second frame_hex write
// unless effect_enable goes 1 and back to 0 between frames (found by LED'oh!), and that hang can't be interrupted, so
// frames are written by their own thread, never the one that draws. LED'oh!'s animation daemon, if one runs, is paused
// meanwhile. On restore everything goes back as it was: brightness, effects switch, each zone's effect.
#include <signal.h>
#include <stdlib.h>
#define LED_DIR "/sys/class/led_anim"
static const char *ZONE[] = { "lr", "m", "f1", "f2" };
#define NZONE 4
typedef struct { char effect[16], rgb[16], cycles[16], duration[16]; } Saved;
static Saved saved[NZONE];
static char saved_enable[8] = "1", saved_scale[3][8];
static const char *SCALE[3] = { "max_scale", "max_scale_f1f2", "max_scale_lr" };
static int have_saved, driving, daemon_pid;
static SDL_mutex *mx; static SDL_Thread *th; static volatile int th_run;
static char want[NLED * 7 + 2], shown[NLED * 7 + 2];

static int rd(const char *name, char *out, size_t n) {
  char p[128]; snprintf(p, sizeof p, LED_DIR "/%s", name);
  FILE *f = fopen(p, "r"); if (!f) return 0;
  int ok = fgets(out, (int)n, f) != NULL;
  fclose(f);
  out[strcspn(out, " \n")] = 0;
  return ok;
}
static void wr(const char *name, const char *val) {
  char p[128]; snprintf(p, sizeof p, LED_DIR "/%s", name);
  FILE *f = fopen(p, "w"); if (!f) return;
  fputs(val, f);
  fclose(f);
}

void leds_start(void) {
  have_saved = 1;
  rd("effect_enable", saved_enable, sizeof saved_enable);
  for (int k = 0; k < 3; k++) if (!rd(SCALE[k], saved_scale[k], sizeof saved_scale[k])) saved_scale[k][0] = 0;
  for (int z = 0; z < NZONE; z++) {
    char k[64]; Saved *s = &saved[z];
    snprintf(k, sizeof k, "effect_%s", ZONE[z]); if (!rd(k, s->effect, sizeof s->effect)) { have_saved = 0; return; }   // no LEDs here
    snprintf(k, sizeof k, "effect_rgb_hex_%s", ZONE[z]); rd(k, s->rgb, sizeof s->rgb);
    snprintf(k, sizeof k, "effect_cycles_%s", ZONE[z]); rd(k, s->cycles, sizeof s->cycles);
    snprintf(k, sizeof k, "effect_duration_%s", ZONE[z]); rd(k, s->duration, sizeof s->duration);
  }
  mx = SDL_CreateMutex();
}

static int writer(void *arg) {
  (void)arg;
  while (th_run) {
    char f[sizeof want];
    SDL_LockMutex(mx); snprintf(f, sizeof f, "%s", want); SDL_UnlockMutex(mx);
    if (f[0] && strcmp(f, shown)) {
      wr("frame_hex", f);
      wr("effect_enable", "1"); wr("effect_enable", "0");   // lets the driver take the next frame
      snprintf(shown, sizeof shown, "%s", f);
    }
    SDL_Delay(25);   // about 40 frames a second at most
  }
  return 0;
}

static void begin(void) {
  FILE *pf = fopen("/tmp/led_anim.pid", "r");   // LED'oh!'s animation: paused, not stopped
  daemon_pid = 0;
  if (pf) { if (fscanf(pf, "%d", &daemon_pid) != 1) daemon_pid = 0; fclose(pf); }
  if (daemon_pid > 1 && kill(daemon_pid, SIGSTOP) != 0) daemon_pid = 0;
  for (int z = 0; z < NZONE; z++) { char k[64]; snprintf(k, sizeof k, "effect_rgb_hex_%s", ZONE[z]); wr(k, "000000 "); }   // no flash of the zone colour
  wr("max_scale", "60"); wr("max_scale_f1f2", "60"); wr("max_scale_lr", "60");   // 60: the Brick's limit
  wr("effect_enable", "0");
  shown[0] = want[0] = 0;
  th_run = 1; th = SDL_CreateThread(writer, "leds", NULL);
  driving = 1;
}

void leds_frame(const Uint32 *rgb) {
  if (!have_saved) return;
  if (!driving) begin();
  char f[sizeof want]; size_t o = 0;
  for (int i = 0; i < NLED; i++) o += snprintf(f + o, sizeof f - o, "%06X ", rgb[i] & 0xffffff);
  SDL_LockMutex(mx); memcpy(want, f, sizeof want); SDL_UnlockMutex(mx);
}

void leds_restore(void) {
  if (!have_saved || !driving) return;
  driving = 0;
  th_run = 0;
  if (th) { SDL_WaitThread(th, NULL); th = NULL; }
  wr("effect_enable", "1");
  for (int k = 0; k < 3; k++) if (saved_scale[k][0]) wr(SCALE[k], saved_scale[k]);
  for (int z = 0; z < NZONE; z++) {   // writing effect_<zone> starts that zone's effect with what was written before it
    char k[64], hex[20];
    snprintf(hex, sizeof hex, "%s ", saved[z].rgb);
    snprintf(k, sizeof k, "effect_rgb_hex_%s", ZONE[z]); wr(k, hex);
    snprintf(k, sizeof k, "effect_duration_%s", ZONE[z]); wr(k, saved[z].duration);
    snprintf(k, sizeof k, "effect_cycles_%s", ZONE[z]); wr(k, saved[z].cycles);
    snprintf(k, sizeof k, "effect_%s", ZONE[z]); wr(k, saved[z].effect);
  }
  wr("effect_enable", saved_enable[0] ? saved_enable : "1");
  if (daemon_pid > 1) { kill(daemon_pid, SIGCONT); daemon_pid = 0; }   // LED'oh!'s animation goes on
}

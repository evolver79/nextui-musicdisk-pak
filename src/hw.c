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

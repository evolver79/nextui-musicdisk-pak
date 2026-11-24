// The device's backlight: the screen goes off while the music plays.
#pragma once
#include <SDL2/SDL.h>

void screen_power(int on);              // off: backlight 0; on: back to the brightness it had
int  screen_is_on(void);

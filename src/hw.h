// The Brick's own hardware: the backlight (screen off while the music plays) and the LEDs (pulsing with the music).
#pragma once
#include <SDL2/SDL.h>

void screen_power(int on);              // off: backlight 0; on: back to the brightness it had
int  screen_is_on(void);

void leds_start(void);                  // remembers NextUI's LED setup
// The Brick's 14 LEDs in frame order (from LED'oh!'s teardown notes): 0-7 the top bar left to right, 8 F2, 9 F1,
// 10 L1, 11 L2, 12 R2, 13 R1.
#define NLED 14
enum { LED_F2 = 8, LED_F1 = 9, LED_L1 = 10, LED_L2 = 11, LED_R2 = 12, LED_R1 = 13 };
void leds_frame(const Uint32 *rgb);     // NLED colours; a thread writes them (only when they change)
void leds_restore(void);                // NextUI's LED setup back

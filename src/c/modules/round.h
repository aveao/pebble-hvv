#pragma once

#include <pebble.h>

// Horizontal inset needed on each side so the band of screen rows
// top .. bottom-1 fits inside a round display, plus a small margin.
// Returns screen.w / 2 when the band reaches outside the circle, and always 0
// on rectangular displays, so callers need no #ifdef.
int16_t round_inset(int16_t top, int16_t bottom, GSize screen);

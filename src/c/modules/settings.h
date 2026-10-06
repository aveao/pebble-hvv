#pragma once

#include <pebble.h>

// Loads persisted settings and applies touch navigation
void settings_init(void);

// Bold direction text in the departure list (emery and gabbro)
bool settings_get_bold_text(void);
void settings_set_bold_text(bool bold);

// Direction arrows above departure badges; on by default
bool settings_get_direction_arrows(void);
void settings_set_direction_arrows(bool enabled);

// Vibrate a minute before a route's vehicle reaches the user's stop; off by
// default
bool settings_get_arrival_vibe(void);
void settings_set_arrival_vibe(bool enabled);

// System touch navigation (swipes/taps scroll and select) on touchscreen
// watches; on by default
bool settings_get_touch_nav(void);
void settings_set_touch_nav(bool enabled);

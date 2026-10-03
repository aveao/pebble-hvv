#pragma once

#include <pebble.h>

void settings_init(void);

// Bold direction text in the departure list (emery and gabbro)
bool settings_get_bold_text(void);
void settings_set_bold_text(bool bold);

#pragma once

#include <pebble.h>
#include "data.h"

// Draw a transit type badge with the line name inside it
void icons_draw_badge(GContext *ctx, TransitType type, const char *line, GRect rect);

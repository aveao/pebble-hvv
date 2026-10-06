#pragma once

#include <pebble.h>
#include "data.h"

// Draw a transit type badge with the line name inside it
void icons_draw_badge(GContext *ctx, TransitType type, const char *line, GRect rect);

// Draw a small arrow above the badge's top-right (forward) or top-left
// (backward) corner; nothing for an unknown direction
void icons_draw_direction_arrow(GContext *ctx, DirectionId direction_id, GRect badge_rect);

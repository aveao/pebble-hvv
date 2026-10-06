#pragma once

#include <pebble.h>

void station_window_push(void);
void station_window_refresh(void);
// The stop lookup failed; shown with a retry hint while the list is empty
void station_window_show_error(const char *message);

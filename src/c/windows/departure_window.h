#pragma once

#include <pebble.h>

void departure_window_push(void);
void departure_window_refresh(void);
void departure_window_show_error(const char *message);
// The phone rejected the last departure request; retry shortly
void departure_window_request_failed(void);

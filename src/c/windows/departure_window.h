#pragma once

#include <pebble.h>

void departure_window_push(void);
void departure_window_refresh(void);
void departure_window_show_error(const char *message);
// The last departure request failed; retry after a delay based on why
void departure_window_request_failed(AppMessageResult reason);
// Display settings changed; redraw with the new fonts
void departure_window_redraw(void);

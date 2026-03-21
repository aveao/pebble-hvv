#pragma once

#include <pebble.h>

typedef void (*CommDataCallback)(void);

void comm_init(CommDataCallback data_changed_callback);
void comm_deinit(void);
void comm_request_departures(void);

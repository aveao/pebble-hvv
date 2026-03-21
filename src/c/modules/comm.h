#pragma once

#include <pebble.h>

typedef void (*CommDataCallback)(void);
typedef void (*CommStationsCallback)(void);

void comm_init(CommDataCallback data_changed_cb, CommStationsCallback stations_changed_cb);
void comm_deinit(void);
void comm_request_departures(void);
void comm_request_stations(void);
void comm_select_station(const char *station_name);

#pragma once

#include <pebble.h>

typedef void (*CommDataCallback)(void);
typedef void (*CommStationsCallback)(void);
typedef void (*CommErrorCallback)(const char *message);
typedef void (*CommRequestFailedCallback)(void);

void comm_init(CommDataCallback data_changed_cb, CommStationsCallback stations_changed_cb,
               CommErrorCallback error_cb, CommRequestFailedCallback departures_failed_cb);
void comm_deinit(void);
// Returns false if the outbox was busy and the request was not queued
bool comm_request_departures(const char *station_name);
void comm_request_stations(void);

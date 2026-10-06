#pragma once

#include <pebble.h>

typedef void (*CommDataCallback)(void);
typedef void (*CommStationsCallback)(void);
typedef void (*CommErrorCallback)(const char *message);
typedef void (*CommRequestFailedCallback)(AppMessageResult reason);
typedef void (*CommSettingsCallback)(void);

void comm_init(CommDataCallback data_changed_cb, CommStationsCallback stations_changed_cb,
               CommErrorCallback error_cb, CommRequestFailedCallback departures_failed_cb,
               CommSettingsCallback settings_changed_cb);
void comm_deinit(void);
// APP_MSG_OK if the request was queued, otherwise why it wasn't
AppMessageResult comm_request_departures(const char *station_name);
void comm_request_stations(void);

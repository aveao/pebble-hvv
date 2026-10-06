#pragma once

#include <pebble.h>

typedef void (*CommDataCallback)(void);
typedef void (*CommStationsCallback)(void);
typedef void (*CommErrorCallback)(const char *message);
typedef void (*CommRequestFailedCallback)(AppMessageResult reason);
typedef void (*CommSettingsCallback)(void);

void comm_init(CommDataCallback data_changed_cb, CommStationsCallback stations_changed_cb,
               CommErrorCallback stations_error_cb, CommErrorCallback error_cb,
               CommRequestFailedCallback departures_failed_cb,
               CommSettingsCallback settings_changed_cb);
void comm_deinit(void);
// APP_MSG_OK if the request was queued, otherwise why it wasn't
AppMessageResult comm_request_departures(const char *station_name);
void comm_request_stations(void);

#ifdef PBL_TOUCH
// Route window callbacks: a route arrived, the route request failed on the
// phone (error text), or the phone rejected the request message
void comm_set_course_callbacks(CommDataCallback data_cb, CommErrorCallback error_cb,
                               CommRequestFailedCallback failed_cb);
// Ask for the route of the departure in row index of station's list. Each
// call gets a new request id, and only replies to the latest one are used.
AppMessageResult comm_request_course(const char *station_name, int index, const char *line);
#endif

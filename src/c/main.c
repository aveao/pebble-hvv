#include <pebble.h>
#include "modules/data.h"
#include "modules/stations.h"
#include "modules/comm.h"
#include "windows/station_window.h"
#include "windows/departure_window.h"

#define REFRESH_INTERVAL_MS 30000
#define RETRY_INTERVAL_MS 2000

static AppTimer *s_refresh_timer;

static void prv_data_changed(void) {
  departure_window_refresh();
}

static void prv_stations_changed(void) {
  station_window_refresh();
}

static void prv_error(const char *message) {
  departure_window_show_error(message);
}

static void prv_refresh_timer_callback(void *context);

static void prv_request_departures(void) {
  // Retry soon if the outbox was busy, otherwise wait for the next refresh
  bool sent = comm_request_departures(data_get_station_name());
  s_refresh_timer = app_timer_register(sent ? REFRESH_INTERVAL_MS : RETRY_INTERVAL_MS,
                                       prv_refresh_timer_callback, NULL);
}

static void prv_refresh_timer_callback(void *context) {
  prv_request_departures();
}

static void prv_departure_request_failed(void) {
  // Only retry while the refresh cycle is running (departure window open)
  if (s_refresh_timer) {
    app_timer_reschedule(s_refresh_timer, RETRY_INTERVAL_MS);
  }
}

void app_start_departure_refresh(void) {
  // Request departures now, then every 30s
  if (s_refresh_timer) {
    app_timer_cancel(s_refresh_timer);
  }
  prv_request_departures();
}

void app_stop_departure_refresh(void) {
  if (s_refresh_timer) {
    app_timer_cancel(s_refresh_timer);
    s_refresh_timer = NULL;
  }
}

static void prv_init(void) {
  data_init();
  stations_init();
  comm_init(prv_data_changed, prv_stations_changed, prv_error, prv_departure_request_failed);
  station_window_push();
}

static void prv_deinit(void) {
  app_stop_departure_refresh();
  comm_deinit();
  stations_deinit();
  data_deinit();
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}

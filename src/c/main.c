#include <pebble.h>
#include "modules/data.h"
#include "modules/stations.h"
#include "modules/comm.h"
#include "windows/station_window.h"
#include "windows/departure_window.h"

#define REFRESH_INTERVAL_MS 30000

static AppTimer *s_refresh_timer;

static void prv_data_changed(void) {
  departure_window_refresh();
}

static void prv_stations_changed(void) {
  station_window_refresh();
}

static void prv_refresh_timer_callback(void *context) {
  comm_request_departures();
  s_refresh_timer = app_timer_register(REFRESH_INTERVAL_MS, prv_refresh_timer_callback, NULL);
}

void app_start_departure_refresh(void) {
  // Start the 30s refresh cycle for departures
  if (s_refresh_timer) {
    app_timer_cancel(s_refresh_timer);
  }
  s_refresh_timer = app_timer_register(REFRESH_INTERVAL_MS, prv_refresh_timer_callback, NULL);
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
  comm_init(prv_data_changed, prv_stations_changed);
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

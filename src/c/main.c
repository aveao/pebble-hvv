#include <pebble.h>
#include "modules/data.h"
#include "modules/comm.h"
#include "windows/departure_window.h"

#define REFRESH_INTERVAL_MS 30000

static AppTimer *s_refresh_timer;

static void prv_data_changed(void) {
  departure_window_refresh();
}

static void prv_refresh_timer_callback(void *context) {
  comm_request_departures();
  s_refresh_timer = app_timer_register(REFRESH_INTERVAL_MS, prv_refresh_timer_callback, NULL);
}

static void prv_init(void) {
  data_init();
  comm_init(prv_data_changed);
  departure_window_push();

  // Start refresh timer
  s_refresh_timer = app_timer_register(REFRESH_INTERVAL_MS, prv_refresh_timer_callback, NULL);
}

static void prv_deinit(void) {
  comm_deinit();
  data_deinit();
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}

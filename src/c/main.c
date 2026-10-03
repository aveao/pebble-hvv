#include <pebble.h>
#include "modules/data.h"
#include "modules/stations.h"
#include "modules/comm.h"
#include "windows/station_window.h"
#include "windows/departure_window.h"

static void prv_init(void) {
  data_init();
  stations_init();
  comm_init(departure_window_refresh, station_window_refresh,
            departure_window_show_error, departure_window_request_failed);
  station_window_push();
}

static void prv_deinit(void) {
  comm_deinit();
  stations_deinit();
  data_deinit();
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}

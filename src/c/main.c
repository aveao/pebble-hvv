#include <pebble.h>
#include "modules/data.h"
#include "modules/stations.h"
#include "modules/comm.h"
#include "modules/settings.h"
#include "windows/station_window.h"
#include "windows/departure_window.h"
#include "windows/route_window.h"

static void prv_init(void) {
  settings_init();
  data_init();
  stations_init();
  comm_init(departure_window_refresh, station_window_refresh,
            station_window_show_error, departure_window_show_error, departure_window_request_failed,
            departure_window_redraw);
#ifdef PBL_TOUCH
  comm_set_course_callbacks(route_window_refresh, route_window_show_error,
                            route_window_request_failed);
#endif
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

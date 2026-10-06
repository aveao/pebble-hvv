#pragma once

#include <pebble.h>
#include "../modules/data.h"

#ifdef PBL_TOUCH
// Show the route of the departure in row index of station's list. line,
// type and direction are the tapped row's, for the header.
void route_window_push(const char *station, int index, const char *line, TransitType type,
                       const char *direction);
// A route arrived (see comm_set_course_callbacks)
void route_window_refresh(void);
// The route request failed on the phone
void route_window_show_error(const char *message);
// The phone rejected the route request message; retry after a delay
void route_window_request_failed(AppMessageResult reason);
#endif

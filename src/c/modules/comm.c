#include "comm.h"
#include "data.h"
#include "stations.h"
#include "settings.h"
#include "course.h"

static CommDataCallback s_data_changed_callback;
static CommStationsCallback s_stations_changed_callback;
static CommErrorCallback s_stations_error_callback;
static CommErrorCallback s_error_callback;
static CommRequestFailedCallback s_departures_failed_callback;
static CommSettingsCallback s_settings_changed_callback;
#ifdef PBL_TOUCH
static CommDataCallback s_course_callback;
static CommErrorCallback s_course_error_callback;
static CommRequestFailedCallback s_course_failed_callback;
// Id of the latest route request; replies to older ones are dropped
static int32_t s_course_req_id;
#endif

static TransitType prv_parse_transit_type(int32_t type_val) {
  switch (type_val) {
    case TRANSIT_BUS:    return TRANSIT_BUS;
    case TRANSIT_SBAHN:  return TRANSIT_SBAHN;
    case TRANSIT_UBAHN:  return TRANSIT_UBAHN;
    case TRANSIT_FERRY:  return TRANSIT_FERRY;
    default:             return TRANSIT_UNKNOWN;
  }
}

static DirectionId prv_parse_direction_id(Tuple *tuple) {
  if (!tuple) return DIRECTION_ID_UNKNOWN;
  switch (tuple->value->int32) {
    case DIRECTION_ID_FORWARD:  return DIRECTION_ID_FORWARD;
    case DIRECTION_ID_BACKWARD: return DIRECTION_ID_BACKWARD;
    default:                    return DIRECTION_ID_UNKNOWN;
  }
}

static void prv_parse_stations(DictionaryIterator *iter) {
  Tuple *count_tuple = dict_find(iter, MESSAGE_KEY_STATION_COUNT);
  if (!count_tuple) return;

  // Only count entries that actually arrived, so a missing index can't
  // leave a stale slot from a previous message in the list
  int count = count_tuple->value->int32;
  int stored = 0;

  for (int i = 0; i < count && i < MAX_STATIONS; i++) {
    Tuple *name_t = dict_find(iter, MESSAGE_KEY_STATION_NAME + i);
    Tuple *fav_t  = dict_find(iter, MESSAGE_KEY_STATION_IS_FAV + i);
    Tuple *dist_t = dict_find(iter, MESSAGE_KEY_STATION_DIST + i);
    Tuple *svc_t  = dict_find(iter, MESSAGE_KEY_STATION_SERVICES + i);

    if (name_t) {
      stations_update(stored++,
        name_t->value->cstring,
        (fav_t && fav_t->value->int32) ? STATION_FAVORITE : STATION_NEARBY,
        dist_t ? (uint8_t)dist_t->value->int32 : 0,
        svc_t ? (uint8_t)svc_t->value->int32 : 0);
    }
  }
  stations_set_count(stored);

  // Cache favorites on the watch so they render instantly on the next launch.
  stations_save_favorites();

  if (s_stations_changed_callback) {
    s_stations_changed_callback();
  }
}

static void prv_parse_departures(DictionaryIterator *iter) {
  Tuple *count_tuple = dict_find(iter, MESSAGE_KEY_DEP_COUNT);
  if (!count_tuple) return;

  // See prv_parse_stations: compact to the entries that arrived
  int count = count_tuple->value->int32;
  int stored = 0;

  for (int i = 0; i < count && i < MAX_DEPARTURES; i++) {
    Tuple *line_t  = dict_find(iter, MESSAGE_KEY_DEP_LINE + i);
    Tuple *type_t  = dict_find(iter, MESSAGE_KEY_DEP_TYPE + i);
    Tuple *dir_t   = dict_find(iter, MESSAGE_KEY_DEP_DIR + i);
    Tuple *mins_t  = dict_find(iter, MESSAGE_KEY_DEP_MINS + i);
    Tuple *delay_t = dict_find(iter, MESSAGE_KEY_DEP_DELAY + i);
    Tuple *cancelled_t = dict_find(iter, MESSAGE_KEY_DEP_CANCELLED + i);
    Tuple *dir_id_t = dict_find(iter, MESSAGE_KEY_DEP_DIR_ID + i);

    if (line_t && type_t && dir_t && mins_t) {
      data_update_departure(stored++,
        line_t->value->cstring,
        prv_parse_transit_type(type_t->value->int32),
        dir_t->value->cstring,
        (int16_t)mins_t->value->int32,
        delay_t ? (int16_t)delay_t->value->int32 : 0,
        cancelled_t && cancelled_t->value->int32,
        prv_parse_direction_id(dir_id_t));
    }
  }
  data_set_count(stored);

  if (s_data_changed_callback) {
    s_data_changed_callback();
  }
}

#ifdef PBL_TOUCH
static void prv_parse_course(DictionaryIterator *iter) {
  Tuple *error_t = dict_find(iter, MESSAGE_KEY_COURSE_ERROR);
  if (error_t) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Route error: %s", error_t->value->cstring);
    if (s_course_error_callback) s_course_error_callback(error_t->value->cstring);
    return;
  }
  Tuple *count_t = dict_find(iter, MESSAGE_KEY_COURSE_COUNT);
  if (!count_t) return;

  // See prv_parse_stations: compact to the entries that arrived
  int count = count_t->value->int32;
  int stored = 0;
  for (int i = 0; i < count && i < MAX_COURSE_STOPS; i++) {
    Tuple *name_t = dict_find(iter, MESSAGE_KEY_COURSE_STOP + i);
    Tuple *planned_t = dict_find(iter, MESSAGE_KEY_COURSE_TIME + i);
    Tuple *delay_t = dict_find(iter, MESSAGE_KEY_COURSE_DELAY + i);
    Tuple *cancelled_t = dict_find(iter, MESSAGE_KEY_COURSE_CANCELLED + i);
    if (name_t && planned_t) {
      course_update_stop(stored++, name_t->value->cstring, (time_t)planned_t->value->int32,
                         delay_t ? (int16_t)delay_t->value->int32 : 0,
                         cancelled_t && cancelled_t->value->int32);
    }
  }
  course_set_count(stored);
  Tuple *focus_t = dict_find(iter, MESSAGE_KEY_COURSE_FOCUS);
  int focus = focus_t ? focus_t->value->int32 : 0;
  course_set_focus(focus >= 0 && focus < stored ? focus : 0);

  if (s_course_callback) s_course_callback();
}
#endif

// Departure and error messages echo the station they were fetched for;
// drop late responses for a station the user has since left.
static bool prv_is_for_current_station(DictionaryIterator *iter) {
  Tuple *station_t = dict_find(iter, MESSAGE_KEY_DEP_STATION);
  return !station_t || strcmp(station_t->value->cstring, data_get_station_name()) == 0;
}

static void prv_inbox_received_handler(DictionaryIterator *iter, void *context) {
  // Settings saved on the phone; one message may carry any of these keys
  Tuple *bold_tuple = dict_find(iter, MESSAGE_KEY_CONFIG_BOLD_TEXT);
  Tuple *touch_tuple = dict_find(iter, MESSAGE_KEY_CONFIG_TOUCH_NAV);
  Tuple *arrows_tuple = dict_find(iter, MESSAGE_KEY_CONFIG_DIRECTION_ARROWS);
  if (bold_tuple || touch_tuple || arrows_tuple) {
    if (touch_tuple) {
      settings_set_touch_nav(touch_tuple->value->int32 != 0);
    }
    if (bold_tuple) {
      settings_set_bold_text(bold_tuple->value->int32 != 0);
    }
    if (arrows_tuple) {
      settings_set_direction_arrows(arrows_tuple->value->int32 != 0);
    }
    if (s_settings_changed_callback) {
      s_settings_changed_callback();
    }
    return;
  }

#ifdef PBL_TOUCH
  // Route reply (data or error) for the route window
  Tuple *course_id_tuple = dict_find(iter, MESSAGE_KEY_COURSE_REQ_ID);
  if (course_id_tuple) {
    if (course_id_tuple->value->int32 == s_course_req_id) prv_parse_course(iter);
    return;
  }
#endif

  // The stop lookup failed (JS only sends this when there are no favorites)
  Tuple *stations_error_tuple = dict_find(iter, MESSAGE_KEY_STATIONS_ERROR);
  if (stations_error_tuple) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Stations error: %s", stations_error_tuple->value->cstring);
    if (s_stations_error_callback) {
      s_stations_error_callback(stations_error_tuple->value->cstring);
    }
    return;
  }

  // Check for error message
  Tuple *error_tuple = dict_find(iter, MESSAGE_KEY_ERROR_MSG);
  if (error_tuple) {
    if (!prv_is_for_current_station(iter)) return;
    APP_LOG(APP_LOG_LEVEL_ERROR, "JS error: %s", error_tuple->value->cstring);
    if (s_error_callback) {
      s_error_callback(error_tuple->value->cstring);
    }
    return;
  }

  // Station list message?
  if (dict_find(iter, MESSAGE_KEY_STATION_COUNT)) {
    prv_parse_stations(iter);
    return;
  }

  // Departure data message?
  if (dict_find(iter, MESSAGE_KEY_DEP_COUNT)) {
    if (!prv_is_for_current_station(iter)) return;
    prv_parse_departures(iter);
    return;
  }
}

static void prv_inbox_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

static void prv_outbox_failed_handler(DictionaryIterator *iter,
                                      AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox failed: %d", (int)reason);
  // The phone rejected a departure request (e.g. it collided with a message
  // from JS); let the caller retry instead of waiting for the next refresh
  if (dict_find(iter, MESSAGE_KEY_REQUEST_DEPARTURES) && s_departures_failed_callback) {
    s_departures_failed_callback(reason);
  }
#ifdef PBL_TOUCH
  if (dict_find(iter, MESSAGE_KEY_REQUEST_COURSE) && s_course_failed_callback) {
    s_course_failed_callback(reason);
  }
#endif
}

static void prv_outbox_sent_handler(DictionaryIterator *iter, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Outbox sent");
}

void comm_init(CommDataCallback data_changed_cb, CommStationsCallback stations_changed_cb,
               CommErrorCallback stations_error_cb, CommErrorCallback error_cb,
               CommRequestFailedCallback departures_failed_cb,
               CommSettingsCallback settings_changed_cb) {
  s_data_changed_callback = data_changed_cb;
  s_stations_changed_callback = stations_changed_cb;
  s_stations_error_callback = stations_error_cb;
  s_error_callback = error_cb;
  s_departures_failed_callback = departures_failed_cb;
  s_settings_changed_callback = settings_changed_cb;

  app_message_register_inbox_received(prv_inbox_received_handler);
  app_message_register_inbox_dropped(prv_inbox_dropped_handler);
  app_message_register_outbox_sent(prv_outbox_sent_handler);
  app_message_register_outbox_failed(prv_outbox_failed_handler);

  // app_message_*_size_maximum() returns ~8 KB each on every platform, but
  // aplite and diorite only have ~15 KB of app heap total, so requesting
  // both maximums fails with APP_MSG_INVALID_STATE. Pick sizes that fit
  // our actual messages: stations list is ~1 KB, and a departure list is
  // up to ~110 B per entry (JS caps directions at 31 bytes), so ~1.7 KB at
  // aplite's 15-entry maximum. Requests outbound are tiny. Touch watches
  // also receive routes: up to 64 stops at ~61 B each, ~3.9 KB.
#if defined(PBL_PLATFORM_APLITE) || defined(PBL_PLATFORM_DIORITE)
  app_message_open(2048, 128);
#elif defined(PBL_TOUCH)
  app_message_open(8192, 256);
#else
  app_message_open(4096, 256);
#endif
}

void comm_deinit(void) {
  app_message_deregister_callbacks();
}

AppMessageResult comm_request_departures(const char *station_name) {
  DictionaryIterator *out;
  AppMessageResult result = app_message_outbox_begin(&out);
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox begin failed: %d", (int)result);
    return result;
  }
  dict_write_cstring(out, MESSAGE_KEY_REQUEST_DEPARTURES, station_name);
  result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed: %d", (int)result);
  }
  return result;
}

void comm_request_stations(void) {
  DictionaryIterator *out;
  AppMessageResult result = app_message_outbox_begin(&out);
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox begin failed: %d", (int)result);
    return;
  }
  dict_write_uint8(out, MESSAGE_KEY_REQUEST_STATIONS, 1);
  result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed: %d", (int)result);
  }
}

#ifdef PBL_TOUCH
void comm_set_course_callbacks(CommDataCallback data_cb, CommErrorCallback error_cb,
                               CommRequestFailedCallback failed_cb) {
  s_course_callback = data_cb;
  s_course_error_callback = error_cb;
  s_course_failed_callback = failed_cb;
}

AppMessageResult comm_request_course(const char *station_name, int index, const char *line) {
  // A new id even if sending fails, so a late reply to an earlier request
  // is still dropped
  s_course_req_id++;
  DictionaryIterator *out;
  AppMessageResult result = app_message_outbox_begin(&out);
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox begin failed: %d", (int)result);
    return result;
  }
  dict_write_cstring(out, MESSAGE_KEY_REQUEST_COURSE, station_name);
  dict_write_int32(out, MESSAGE_KEY_COURSE_DEP_INDEX, index);
  dict_write_cstring(out, MESSAGE_KEY_COURSE_DEP_LINE, line);
  dict_write_int32(out, MESSAGE_KEY_COURSE_REQ_ID, s_course_req_id);
  result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed: %d", (int)result);
  }
  return result;
}
#endif

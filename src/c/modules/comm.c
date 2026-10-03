#include "comm.h"
#include "data.h"
#include "stations.h"

static CommDataCallback s_data_changed_callback;
static CommStationsCallback s_stations_changed_callback;
static CommErrorCallback s_error_callback;
static CommRequestFailedCallback s_departures_failed_callback;

static TransitType prv_parse_transit_type(int32_t type_val) {
  switch (type_val) {
    case TRANSIT_BUS:    return TRANSIT_BUS;
    case TRANSIT_SBAHN:  return TRANSIT_SBAHN;
    case TRANSIT_UBAHN:  return TRANSIT_UBAHN;
    case TRANSIT_FERRY:  return TRANSIT_FERRY;
    default:             return TRANSIT_UNKNOWN;
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

    if (line_t && type_t && dir_t && mins_t) {
      data_update_departure(stored++,
        line_t->value->cstring,
        prv_parse_transit_type(type_t->value->int32),
        dir_t->value->cstring,
        (int16_t)mins_t->value->int32,
        delay_t ? (int16_t)delay_t->value->int32 : 0);
    }
  }
  data_set_count(stored);

  if (s_data_changed_callback) {
    s_data_changed_callback();
  }
}

// Departure and error messages echo the station they were fetched for;
// drop late responses for a station the user has since left.
static bool prv_is_for_current_station(DictionaryIterator *iter) {
  Tuple *station_t = dict_find(iter, MESSAGE_KEY_DEP_STATION);
  return !station_t || strcmp(station_t->value->cstring, data_get_station_name()) == 0;
}

static void prv_inbox_received_handler(DictionaryIterator *iter, void *context) {
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
    s_departures_failed_callback();
  }
}

static void prv_outbox_sent_handler(DictionaryIterator *iter, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Outbox sent");
}

void comm_init(CommDataCallback data_changed_cb, CommStationsCallback stations_changed_cb,
               CommErrorCallback error_cb, CommRequestFailedCallback departures_failed_cb) {
  s_data_changed_callback = data_changed_cb;
  s_stations_changed_callback = stations_changed_cb;
  s_error_callback = error_cb;
  s_departures_failed_callback = departures_failed_cb;

  app_message_register_inbox_received(prv_inbox_received_handler);
  app_message_register_inbox_dropped(prv_inbox_dropped_handler);
  app_message_register_outbox_sent(prv_outbox_sent_handler);
  app_message_register_outbox_failed(prv_outbox_failed_handler);

  // app_message_*_size_maximum() returns ~8 KB each on every platform, but
  // aplite and diorite only have ~15 KB of app heap total, so requesting
  // both maximums fails with APP_MSG_INVALID_STATE. Pick sizes that fit
  // our actual messages: stations list is ~1 KB, departure list with the
  // default 10 entries is ~900 B, requests outbound are tiny.
#if defined(PBL_PLATFORM_APLITE) || defined(PBL_PLATFORM_DIORITE)
  app_message_open(2048, 128);
#else
  app_message_open(4096, 256);
#endif
}

void comm_deinit(void) {
  app_message_deregister_callbacks();
}

bool comm_request_departures(const char *station_name) {
  DictionaryIterator *out;
  AppMessageResult result = app_message_outbox_begin(&out);
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox begin failed: %d", (int)result);
    return false;
  }
  dict_write_cstring(out, MESSAGE_KEY_REQUEST_DEPARTURES, station_name);
  result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed: %d", (int)result);
    return false;
  }
  return true;
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

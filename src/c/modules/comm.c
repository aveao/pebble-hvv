#include "comm.h"
#include "data.h"
#include "stations.h"

static CommDataCallback s_data_changed_callback;
static CommStationsCallback s_stations_changed_callback;

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

  int count = count_tuple->value->int32;
  stations_set_count(count);

  for (int i = 0; i < count && i < MAX_STATIONS; i++) {
    Tuple *name_t = dict_find(iter, MESSAGE_KEY_STATION_NAME + i);
    Tuple *fav_t  = dict_find(iter, MESSAGE_KEY_STATION_IS_FAV + i);
    Tuple *dist_t = dict_find(iter, MESSAGE_KEY_STATION_DIST + i);

    if (name_t) {
      stations_update(i,
        name_t->value->cstring,
        (fav_t && fav_t->value->int32) ? STATION_FAVORITE : STATION_NEARBY,
        dist_t ? (uint8_t)dist_t->value->int32 : 0);
    }
  }

  if (s_stations_changed_callback) {
    s_stations_changed_callback();
  }
}

static void prv_parse_departures(DictionaryIterator *iter) {
  Tuple *count_tuple = dict_find(iter, MESSAGE_KEY_DEP_COUNT);
  if (!count_tuple) return;

  int count = count_tuple->value->int32;
  data_set_count(count);

  for (int i = 0; i < count && i < MAX_DEPARTURES; i++) {
    Tuple *line_t  = dict_find(iter, MESSAGE_KEY_DEP_LINE + i);
    Tuple *type_t  = dict_find(iter, MESSAGE_KEY_DEP_TYPE + i);
    Tuple *dir_t   = dict_find(iter, MESSAGE_KEY_DEP_DIR + i);
    Tuple *mins_t  = dict_find(iter, MESSAGE_KEY_DEP_MINS + i);
    Tuple *delay_t = dict_find(iter, MESSAGE_KEY_DEP_DELAY + i);

    if (line_t && type_t && dir_t && mins_t) {
      data_update_departure(i,
        line_t->value->cstring,
        prv_parse_transit_type(type_t->value->int32),
        dir_t->value->cstring,
        (int16_t)mins_t->value->int32,
        delay_t ? (int16_t)delay_t->value->int32 : 0);
    }
  }

  if (s_data_changed_callback) {
    s_data_changed_callback();
  }
}

static void prv_inbox_received_handler(DictionaryIterator *iter, void *context) {
  // Check for error message
  Tuple *error_tuple = dict_find(iter, MESSAGE_KEY_ERROR_MSG);
  if (error_tuple) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "JS error: %s", error_tuple->value->cstring);
    return;
  }

  // Station list message?
  if (dict_find(iter, MESSAGE_KEY_STATION_COUNT)) {
    prv_parse_stations(iter);
    return;
  }

  // Departure data message?
  if (dict_find(iter, MESSAGE_KEY_DEP_COUNT)) {
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
}

static void prv_outbox_sent_handler(DictionaryIterator *iter, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Outbox sent");
}

void comm_init(CommDataCallback data_changed_cb, CommStationsCallback stations_changed_cb) {
  s_data_changed_callback = data_changed_cb;
  s_stations_changed_callback = stations_changed_cb;

  app_message_register_inbox_received(prv_inbox_received_handler);
  app_message_register_inbox_dropped(prv_inbox_dropped_handler);
  app_message_register_outbox_sent(prv_outbox_sent_handler);
  app_message_register_outbox_failed(prv_outbox_failed_handler);

  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
}

void comm_deinit(void) {
  app_message_deregister_callbacks();
}

void comm_request_departures(void) {
  DictionaryIterator *out;
  AppMessageResult result = app_message_outbox_begin(&out);
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox begin failed: %d", (int)result);
    return;
  }
  dict_write_uint8(out, MESSAGE_KEY_REQUEST_DEPARTURES, 1);
  result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed: %d", (int)result);
  }
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

void comm_select_station(const char *station_name) {
  DictionaryIterator *out;
  AppMessageResult result = app_message_outbox_begin(&out);
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox begin failed: %d", (int)result);
    return;
  }
  dict_write_cstring(out, MESSAGE_KEY_SELECT_STATION, station_name);
  result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed: %d", (int)result);
  }
}

#include "comm.h"
#include "data.h"

static CommDataCallback s_data_changed_callback;

static TransitType prv_parse_transit_type(int32_t type_val) {
  switch (type_val) {
    case TRANSIT_BUS:    return TRANSIT_BUS;
    case TRANSIT_SBAHN:  return TRANSIT_SBAHN;
    case TRANSIT_UBAHN:  return TRANSIT_UBAHN;
    case TRANSIT_FERRY:  return TRANSIT_FERRY;
    default:             return TRANSIT_UNKNOWN;
  }
}

static void prv_inbox_received_handler(DictionaryIterator *iter, void *context) {
  // Check for config update
  Tuple *config_station = dict_find(iter, MESSAGE_KEY_CONFIG_STATION);
  if (config_station) {
    data_set_station_name(config_station->value->cstring);
    // Request fresh data after config change
    comm_request_departures();
    return;
  }

  // Check for error message
  Tuple *error_tuple = dict_find(iter, MESSAGE_KEY_ERROR_MSG);
  if (error_tuple) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "JS error: %s", error_tuple->value->cstring);
    return;
  }

  // Parse departure count
  Tuple *count_tuple = dict_find(iter, MESSAGE_KEY_DEP_COUNT);
  if (!count_tuple) return;

  int count = count_tuple->value->int32;
  data_set_count(count);

  for (int i = 0; i < count && i < MAX_DEPARTURES; i++) {
    // Keys are sequential: DEP_LINE_0, DEP_LINE_1, ... mapped via array keys
    uint32_t line_key = MESSAGE_KEY_DEP_LINE + i;
    uint32_t type_key = MESSAGE_KEY_DEP_TYPE + i;
    uint32_t dir_key  = MESSAGE_KEY_DEP_DIR + i;
    uint32_t mins_key = MESSAGE_KEY_DEP_MINS + i;
    uint32_t delay_key = MESSAGE_KEY_DEP_DELAY + i;

    Tuple *line_t  = dict_find(iter, line_key);
    Tuple *type_t  = dict_find(iter, type_key);
    Tuple *dir_t   = dict_find(iter, dir_key);
    Tuple *mins_t  = dict_find(iter, mins_key);
    Tuple *delay_t = dict_find(iter, delay_key);

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

void comm_init(CommDataCallback data_changed_callback) {
  s_data_changed_callback = data_changed_callback;

  app_message_register_inbox_received(prv_inbox_received_handler);
  app_message_register_inbox_dropped(prv_inbox_dropped_handler);
  app_message_register_outbox_sent(prv_outbox_sent_handler);
  app_message_register_outbox_failed(prv_outbox_failed_handler);

  // Open with generous buffer sizes for departure data
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

#include "data.h"
#include "text.h"
#include <pebble.h>

#define PERSIST_KEY_STATION 1

static Departure s_departures[MAX_DEPARTURES];
static int s_departure_count;
static char s_station_name[STATION_NAME_LEN];

void data_init(void) {
  s_departure_count = 0;
  if (persist_exists(PERSIST_KEY_STATION)) {
    persist_read_string(PERSIST_KEY_STATION, s_station_name, sizeof(s_station_name));
  } else {
    strncpy(s_station_name, "HVV Departures", sizeof(s_station_name));
  }
}

void data_deinit(void) {
  // Nothing to clean up
}

int data_get_count(void) {
  return s_departure_count;
}

void data_set_count(int count) {
  if (count > MAX_DEPARTURES) count = MAX_DEPARTURES;
  if (count < 0) count = 0;
  s_departure_count = count;
}

Departure *data_get_departure(int index) {
  if (index < 0 || index >= s_departure_count) return NULL;
  return &s_departures[index];
}

void data_update_departure(int index, const char *line, TransitType type,
                           const char *direction, int16_t minutes, int16_t delay) {
  if (index < 0 || index >= MAX_DEPARTURES) return;
  Departure *dep = &s_departures[index];
  text_copy_utf8(dep->line, line, sizeof(dep->line));
  dep->type = type;
  text_copy_utf8(dep->direction, direction, sizeof(dep->direction));
  dep->minutes = minutes;
  dep->delay = delay;
}

const char *data_get_station_name(void) {
  return s_station_name;
}

void data_set_station_name(const char *name) {
  text_copy_utf8(s_station_name, name, sizeof(s_station_name));
  persist_write_string(PERSIST_KEY_STATION, s_station_name);
}

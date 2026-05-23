#include "stations.h"

// Persist keys (data.c owns key 1; keep favorites in a separate range).
#define PERSIST_KEY_FAV_COUNT 100
#define PERSIST_KEY_FAV_FIRST 101  // name strings at 101 .. 101 + MAX_STATIONS - 1

static Station s_stations[MAX_STATIONS];
static int s_station_count;

// Restore favorites saved on a previous run so the favorites section can be
// drawn at window load with no Bluetooth round-trip. Nearby (GPS) stations
// still arrive later from the phone.
static void prv_load_favorites(void) {
  if (!persist_exists(PERSIST_KEY_FAV_COUNT)) return;
  int n = persist_read_int(PERSIST_KEY_FAV_COUNT);
  if (n > MAX_STATIONS) n = MAX_STATIONS;
  for (int i = 0; i < n; i++) {
    int key = PERSIST_KEY_FAV_FIRST + i;
    if (!persist_exists(key)) continue;
    char name[STATION_LABEL_LEN];
    persist_read_string(key, name, sizeof(name));
    stations_update(s_station_count, name, STATION_FAVORITE, 0, 0);
    s_station_count++;
  }
}

void stations_init(void) {
  s_station_count = 0;
  prv_load_favorites();
}

void stations_deinit(void) {
}

int stations_get_count(void) {
  return s_station_count;
}

void stations_set_count(int count) {
  if (count > MAX_STATIONS) count = MAX_STATIONS;
  if (count < 0) count = 0;
  s_station_count = count;
}

Station *stations_get(int index) {
  if (index < 0 || index >= s_station_count) return NULL;
  return &s_stations[index];
}

void stations_update(int index, const char *name, StationType type, uint8_t distance, uint8_t services) {
  if (index < 0 || index >= MAX_STATIONS) return;
  Station *s = &s_stations[index];
  strncpy(s->name, name, STATION_LABEL_LEN - 1);
  s->name[STATION_LABEL_LEN - 1] = '\0';
  s->type = type;
  s->distance = distance;
  s->services = services;
}

int stations_get_nearby_count(void) {
  int count = 0;
  for (int i = 0; i < s_station_count; i++) {
    if (s_stations[i].type == STATION_NEARBY) count++;
  }
  return count;
}

int stations_get_favorite_count(void) {
  int count = 0;
  for (int i = 0; i < s_station_count; i++) {
    if (s_stations[i].type == STATION_FAVORITE) count++;
  }
  return count;
}

Station *stations_get_nearby(int index) {
  int found = 0;
  for (int i = 0; i < s_station_count; i++) {
    if (s_stations[i].type == STATION_NEARBY) {
      if (found == index) return &s_stations[i];
      found++;
    }
  }
  return NULL;
}

Station *stations_get_favorite(int index) {
  int found = 0;
  for (int i = 0; i < s_station_count; i++) {
    if (s_stations[i].type == STATION_FAVORITE) {
      if (found == index) return &s_stations[i];
      found++;
    }
  }
  return NULL;
}

void stations_save_favorites(void) {
  int n = stations_get_favorite_count();
  if (n > MAX_STATIONS) n = MAX_STATIONS;

  // Skip the write when the stored favorites already match - favorites only
  // change on a settings edit, so this avoids needless flash wear on every
  // station refresh.
  bool changed = !persist_exists(PERSIST_KEY_FAV_COUNT) ||
                 persist_read_int(PERSIST_KEY_FAV_COUNT) != n;
  for (int i = 0; i < n && !changed; i++) {
    Station *fav = stations_get_favorite(i);
    char stored[STATION_LABEL_LEN] = {0};
    persist_read_string(PERSIST_KEY_FAV_FIRST + i, stored, sizeof(stored));
    if (!fav || strncmp(stored, fav->name, STATION_LABEL_LEN) != 0) changed = true;
  }
  if (!changed) return;

  persist_write_int(PERSIST_KEY_FAV_COUNT, n);
  for (int i = 0; i < n; i++) {
    Station *fav = stations_get_favorite(i);
    if (fav) persist_write_string(PERSIST_KEY_FAV_FIRST + i, fav->name);
  }
}

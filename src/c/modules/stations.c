#include "stations.h"

static Station s_stations[MAX_STATIONS];
static int s_station_count;

void stations_init(void) {
  s_station_count = 0;
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

void stations_update(int index, const char *name, StationType type, uint8_t distance) {
  if (index < 0 || index >= MAX_STATIONS) return;
  Station *s = &s_stations[index];
  strncpy(s->name, name, STATION_LABEL_LEN - 1);
  s->name[STATION_LABEL_LEN - 1] = '\0';
  s->type = type;
  s->distance = distance;
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

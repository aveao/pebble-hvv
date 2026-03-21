#pragma once

#include <pebble.h>

#define MAX_STATIONS 8
#define STATION_LABEL_LEN 32

typedef enum {
  STATION_NEARBY = 0,
  STATION_FAVORITE = 1
} StationType;

typedef struct {
  char name[STATION_LABEL_LEN];
  StationType type;
  uint8_t distance; // units of 10m, 0 for favorites
} Station;

void stations_init(void);
void stations_deinit(void);

int stations_get_count(void);
void stations_set_count(int count);
Station *stations_get(int index);
void stations_update(int index, const char *name, StationType type, uint8_t distance);

// Count by type
int stations_get_nearby_count(void);
int stations_get_favorite_count(void);
// Get by type-relative index
Station *stations_get_nearby(int index);
Station *stations_get_favorite(int index);

#pragma once

#include <pebble.h>

#define MAX_STATIONS 8
#define STATION_LABEL_LEN 32

typedef enum {
  STATION_NEARBY = 0,
  STATION_FAVORITE = 1
} StationType;

// Service type bitmask flags
#define SERVICE_BUS    (1 << 0)
#define SERVICE_SBAHN  (1 << 1)
#define SERVICE_UBAHN  (1 << 2)
#define SERVICE_FERRY  (1 << 3)
#define SERVICE_ABAHN  (1 << 4)
#define SERVICE_TRAIN  (1 << 5)

typedef struct {
  char name[STATION_LABEL_LEN];
  StationType type;
  uint8_t distance; // units of 10m, 0 for favorites
  uint8_t services; // bitmask of SERVICE_* flags
} Station;

void stations_init(void);
void stations_deinit(void);

int stations_get_count(void);
void stations_set_count(int count);
Station *stations_get(int index);
void stations_update(int index, const char *name, StationType type, uint8_t distance, uint8_t services);

// Count by type
int stations_get_nearby_count(void);
int stations_get_favorite_count(void);
// Get by type-relative index
Station *stations_get_nearby(int index);
Station *stations_get_favorite(int index);

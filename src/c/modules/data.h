#pragma once

#include <pebble.h>

#define MAX_DEPARTURES 10
#define LINE_NAME_LEN 8
#define DIRECTION_LEN 32
#define STATION_NAME_LEN 64

typedef enum {
  TRANSIT_BUS = 0,
  TRANSIT_SBAHN,
  TRANSIT_UBAHN,
  TRANSIT_FERRY,
  TRANSIT_UNKNOWN
} TransitType;

typedef struct {
  char line[LINE_NAME_LEN];
  TransitType type;
  char direction[DIRECTION_LEN];
  int16_t minutes;
  int16_t delay;
} Departure;

void data_init(void);
void data_deinit(void);

int data_get_count(void);
void data_set_count(int count);
Departure *data_get_departure(int index);
void data_update_departure(int index, const char *line, TransitType type,
                           const char *direction, int16_t minutes, int16_t delay);

const char *data_get_station_name(void);
void data_set_station_name(const char *name);

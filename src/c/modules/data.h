#pragma once

#include <pebble.h>

// Aplite/diorite have a 2 KB AppMessage inbox and little heap; the JS side
// caps the configurable departure count to match.
#if defined(PBL_PLATFORM_APLITE) || defined(PBL_PLATFORM_DIORITE)
  #define MAX_DEPARTURES 15
#else
  #define MAX_DEPARTURES 30
#endif
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

// GTI's directionId: which way along its line a departure runs, regardless
// of where that particular trip terminates. Values match GTI's.
typedef enum {
  DIRECTION_ID_UNKNOWN = 0,
  DIRECTION_ID_FORWARD = 1,
  DIRECTION_ID_BACKWARD = 6
} DirectionId;

typedef struct {
  char line[LINE_NAME_LEN];
  TransitType type;
  char direction[DIRECTION_LEN];
  int16_t minutes;
  int16_t delay;
  bool cancelled;
  DirectionId direction_id;
} Departure;

void data_init(void);
void data_deinit(void);

int data_get_count(void);
void data_set_count(int count);
Departure *data_get_departure(int index);
void data_update_departure(int index, const char *line, TransitType type,
                           const char *direction, int16_t minutes, int16_t delay,
                           bool cancelled, DirectionId direction_id);

const char *data_get_station_name(void);
void data_set_station_name(const char *name);

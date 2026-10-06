#pragma once

#include <pebble.h>

// Stops of the route shown in the route window (touch watches only)
#define MAX_COURSE_STOPS 64
#define COURSE_STOP_NAME_LEN 32

typedef struct {
  char name[COURSE_STOP_NAME_LEN];
  time_t planned;   // Unix seconds
  int16_t delay;    // minutes
  bool cancelled;
} CourseStop;

#ifdef PBL_TOUCH
void course_clear(void);
int course_get_count(void);
void course_set_count(int count);
// Index of the user's station
int course_get_focus(void);
void course_set_focus(int index);
const CourseStop *course_get_stop(int index);
void course_update_stop(int index, const char *name, time_t planned, int16_t delay,
                        bool cancelled);
#endif

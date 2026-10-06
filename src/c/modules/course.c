#include "course.h"
#include "text.h"

#ifdef PBL_TOUCH

static CourseStop s_stops[MAX_COURSE_STOPS];
static int s_count;
static int s_focus;

void course_clear(void) {
  s_count = 0;
  s_focus = 0;
}

int course_get_count(void) {
  return s_count;
}

void course_set_count(int count) {
  if (count > MAX_COURSE_STOPS) count = MAX_COURSE_STOPS;
  if (count < 0) count = 0;
  s_count = count;
}

int course_get_focus(void) {
  return s_focus;
}

void course_set_focus(int index) {
  s_focus = index;
}

const CourseStop *course_get_stop(int index) {
  if (index < 0 || index >= s_count) return NULL;
  return &s_stops[index];
}

void course_update_stop(int index, const char *name, time_t planned, int16_t delay,
                        bool cancelled) {
  if (index < 0 || index >= MAX_COURSE_STOPS) return;
  CourseStop *stop = &s_stops[index];
  text_copy_utf8(stop->name, name, sizeof(stop->name));
  stop->planned = planned;
  stop->delay = delay;
  stop->cancelled = cancelled;
}

#endif

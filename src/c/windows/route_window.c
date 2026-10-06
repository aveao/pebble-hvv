#include "route_window.h"

#ifdef PBL_TOUCH

#include "../modules/comm.h"
#include "../modules/course.h"
#include "../modules/icons.h"
#include "../modules/round.h"
#include "../modules/text.h"
#include "../modules/data.h"

// Touch watches (emery, gabbro) share one size table
#define ROW_H 36
#define HEADER_H 30
#define BADGE_W 38
#define BADGE_H 24
// Vertical positions in the header, tuned so badge and text sit centred on
// the bar (Gothic 18's glyphs start a few px below the text box top)
#define HEADER_BADGE_Y 3
#define HEADER_TEXT_Y 3
// Line diagram: centre of the line from the row's left edge (or round inset)
#define DIAG_X 14
#define LINE_W 4
#define DOT_R 4
#define FOCUS_DOT_R 6
#define MARKER_R 7
// Gap between the line's centre and the stop name
#define NAME_GAP 14
#define TIME_W 46
#define PAD 4
// Step when drawing the line as segments, so it can follow a round edge
#define LINE_STEP 4

#define REFRESH_INTERVAL_MS 60000
#define RETRY_INTERVAL_MS 2000
// Say so if nothing has come back this long after opening
#define NO_REPLY_MS 20000
// Close after 15 minutes without input to stop unnecessary API requests
#define INACTIVITY_TIMEOUT_MS (15 * 60 * 1000)

static Window *s_window;
static StatusBarLayer *s_status_bar;
static Layer *s_header_layer;
static MenuLayer *s_menu_layer;
static TextLayer *s_loading_layer;
static AppTimer *s_refresh_timer;
static AppTimer *s_inactivity_timer;
static AppTimer *s_no_reply_timer;
static bool s_visible;
// True once the first route of this window has arrived
static bool s_has_route;

// The tapped departure
static char s_station[STATION_NAME_LEN];
static int s_index;
static char s_line[LINE_NAME_LEN];
static TransitType s_type;
static char s_direction[DIRECTION_LEN];
// Last request error; shown until the next successful refresh
static char s_error[32];

// ---- Position ----

static time_t prv_expected(const CourseStop *stop) {
  return stop->planned + stop->delay * 60;
}

static int16_t prv_row_center(int row) {
  return row * ROW_H + ROW_H / 2;
}

// The next stop the trip calls at from index from on, or -1
static int prv_next_served(int from) {
  for (int i = from; i < course_get_count(); i++) {
    if (!course_get_stop(i)->cancelled) return i;
  }
  return -1;
}

// Where the vehicle is, as a y in menu content coordinates (row 0's top is
// 0): on the dot of the stop it's at, halfway between two stops while
// travelling, or on the first stop before the trip starts (*started false)
static int16_t prv_marker_y(time_t now, bool *started) {
  int count = course_get_count();
  int at = -1;
  for (int i = 0; i < count; i++) {
    const CourseStop *stop = course_get_stop(i);
    if (!stop->cancelled && prv_expected(stop) <= now) at = i;
  }
  *started = at >= 0;
  if (at < 0) {
    int first = prv_next_served(0);
    return prv_row_center(first < 0 ? 0 : first);
  }
  // Within the minute it's due there, it's at the stop
  if (now < prv_expected(course_get_stop(at)) + 60) return prv_row_center(at);
  int next = prv_next_served(at + 1);
  if (next < 0) return prv_row_center(at);  // trip has ended
  return (prv_row_center(at) + prv_row_center(next)) / 2;
}

// ---- Drawing ----

static uint16_t prv_get_num_rows(MenuLayer *menu_layer, uint16_t section, void *context) {
  return course_get_count();
}

static int16_t prv_get_cell_height(MenuLayer *menu_layer, MenuIndex *index, void *context) {
  return ROW_H;
}

// Horizontal inset for a band of the screen (0 on rectangular screens)
static int16_t prv_inset(int16_t screen_top, int16_t h) {
  GSize screen = layer_get_bounds(window_get_root_layer(s_window)).size;
  return round_inset(screen_top, screen_top + h, screen);
}

// x of the line's centre at a cell-local y. On round screens the line
// follows the curved edge.
static int16_t prv_line_x(int16_t cell_screen_y, int16_t y) {
  return prv_inset(cell_screen_y + y, 1) + DIAG_X;
}

static void prv_draw_line(GContext *ctx, int16_t cell_screen_y, int16_t from, int16_t to,
                          int16_t width, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, width);
  for (int16_t y = from; y < to; y += LINE_STEP) {
    int16_t y2 = y + LINE_STEP < to ? y + LINE_STEP : to;
    graphics_draw_line(ctx, GPoint(prv_line_x(cell_screen_y, y), y),
                       GPoint(prv_line_x(cell_screen_y, y2), y2));
  }
}

static void prv_draw_dot(GContext *ctx, GPoint center, int16_t radius, GColor fill) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, center, radius + 1);
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_circle(ctx, center, radius);
}

static void prv_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *index,
                         void *context) {
  const CourseStop *stop = course_get_stop(index->row);
  if (!stop) return;

  GRect bounds = layer_get_bounds(cell_layer);
  int16_t screen_y = layer_convert_point_to_screen(cell_layer, GPoint(0, 0)).y;
  int16_t cy = ROW_H / 2;
  int row = index->row;
  int count = course_get_count();
  bool focus = row == course_get_focus();
  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  GColor line_color = icons_line_color(s_type, s_line);

  bool started;
  int16_t marker = prv_marker_y(time(NULL), &started) - row * ROW_H;
  bool passed = started && cy < marker;

  // Line: grey where the vehicle has already been, outlined so light line
  // colours (U3 yellow) stay visible on white
  graphics_context_set_antialiased(ctx, true);
  int16_t top = row == 0 ? cy : 0;
  int16_t bottom = row == count - 1 ? cy + 1 : ROW_H;
  int16_t split = started ? (marker < top ? top : marker > bottom ? bottom : marker) : top;
  prv_draw_line(ctx, screen_y, top, bottom, LINE_W + 2, GColorBlack);
  if (split > top) prv_draw_line(ctx, screen_y, top, split, LINE_W, GColorLightGray);
  if (bottom > split) prv_draw_line(ctx, screen_y, split, bottom, LINE_W, line_color);

  GPoint dot = GPoint(prv_line_x(screen_y, cy), cy);
  if (focus) {
    prv_draw_dot(ctx, dot, FOCUS_DOT_R, passed ? GColorLightGray : line_color);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, dot, FOCUS_DOT_R - 3);
  } else {
    prv_draw_dot(ctx, dot, DOT_R, GColorWhite);
  }

  // Vehicle: drawn in each row it overlaps, as rows are clipped to their bounds
  if (marker > -MARKER_R - 1 && marker < ROW_H + MARKER_R + 1) {
    GPoint at = GPoint(prv_line_x(screen_y, marker), marker);
    if (started) {
      prv_draw_dot(ctx, at, MARKER_R, line_color);
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_circle(ctx, at, 2);
    } else {
      // Not departed yet: a hollow marker on the first stop
      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_circle(ctx, at, MARKER_R);
    }
  }

  // Time, with the delay underneath
  int16_t right = bounds.size.w - PAD - prv_inset(screen_y + cy - 12, 24);
  char time_buf[8];
  struct tm *t = localtime(&stop->planned);
  strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
  const char *time_text = time_buf[0] == '0' && !clock_is_24h_style() ? time_buf + 1 : time_buf;
  GColor text_color = passed && !highlighted ? GColorDarkGray : GColorBlack;
  graphics_context_set_text_color(ctx, text_color);
  int16_t time_y = stop->delay ? cy - 19 : cy - 13;
  graphics_draw_text(ctx, time_text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(right - TIME_W, time_y, TIME_W, 22), GTextOverflowModeFill, GTextAlignmentRight, NULL);
  if (stop->delay) {
    char delay_buf[8];
    snprintf(delay_buf, sizeof(delay_buf), "%+d", stop->delay);
    graphics_context_set_text_color(ctx, stop->delay > 0 ? GColorRed : GColorIslamicGreen);
    graphics_draw_text(ctx, delay_buf, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
      GRect(right - TIME_W, cy, TIME_W, 18), GTextOverflowModeFill, GTextAlignmentRight, NULL);
  }

  // Name: large if it fits, otherwise smaller
  int16_t name_x = dot.x + NAME_GAP;
  int16_t name_w = right - TIME_W - PAD - name_x;
  if (name_w < 0) name_w = 0;
  GFont font = fonts_get_system_font(focus ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_24);
  int16_t name_h = 30;
  int16_t nudge = 2;
  GSize size = graphics_text_layout_get_content_size(stop->name, font, GRect(0, 0, 500, 100),
    GTextOverflowModeWordWrap, GTextAlignmentLeft);
  if (size.w >= name_w) {
    font = fonts_get_system_font(focus ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_18);
    name_h = 24;
    nudge = 1;
  }
  graphics_context_set_text_color(ctx, text_color);
  graphics_draw_text(ctx, stop->name, font, GRect(name_x, cy - name_h / 2 - nudge, name_w, name_h),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  if (stop->cancelled) {
    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_fill_rect(ctx, GRect(name_x, cy - 1, right - name_x, 2), 0, GCornerNone);
  }
}

static void prv_header_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  const char *text = s_error[0] ? s_error : s_direction;
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  int16_t inset = prv_inset(STATUS_BAR_LAYER_HEIGHT, HEADER_H);
  int16_t left = inset + PAD;
  int16_t avail = bounds.size.w - 2 * left;
  int16_t text_max = avail - BADGE_W - PAD - 2;
  if (text_max < 0) text_max = 0;
#ifdef PBL_ROUND
  // Centre badge and text as one group in the narrow top of the circle
  int16_t text_w = graphics_text_layout_get_content_size(text, font, GRect(0, 0, text_max, HEADER_H),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft).w;
  left += (avail - (BADGE_W + PAD + 2 + text_w)) / 2;
#endif
  GRect badge = GRect(left, HEADER_BADGE_Y, BADGE_W, BADGE_H);
  icons_draw_badge(ctx, s_type, s_line, badge);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, text, font, GRect(left + BADGE_W + PAD + 2, HEADER_TEXT_Y, text_max, HEADER_H),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

// ---- Requests and timers ----

static void prv_refresh_timer_callback(void *context);

static void prv_schedule_request(uint32_t delay_ms) {
  if (s_refresh_timer) {
    app_timer_reschedule(s_refresh_timer, delay_ms);
  } else {
    s_refresh_timer = app_timer_register(delay_ms, prv_refresh_timer_callback, NULL);
  }
}

// Retry soon if the request only collided with another message, otherwise
// wait for the next refresh
static uint32_t prv_next_request_delay(AppMessageResult result) {
  return (result == APP_MSG_BUSY || result == APP_MSG_SEND_REJECTED) ?
    RETRY_INTERVAL_MS : REFRESH_INTERVAL_MS;
}

static void prv_request_course(void) {
  AppMessageResult result = comm_request_course(s_station, s_index, s_line);
  prv_schedule_request(prv_next_request_delay(result));
}

static void prv_refresh_timer_callback(void *context) {
  s_refresh_timer = NULL;
  prv_request_course();
}

static void prv_update_loading(void);

static void prv_no_reply_timeout(void *context) {
  s_no_reply_timer = NULL;
  if (course_get_count() > 0 || s_error[0]) return;
  text_copy_utf8(s_error, "No response from phone", sizeof(s_error));
  layer_mark_dirty(s_header_layer);
  prv_update_loading();
}

static void prv_stop_timers(void) {
  if (s_no_reply_timer) {
    app_timer_cancel(s_no_reply_timer);
    s_no_reply_timer = NULL;
  }
  if (s_refresh_timer) {
    app_timer_cancel(s_refresh_timer);
    s_refresh_timer = NULL;
  }
  if (s_inactivity_timer) {
    app_timer_cancel(s_inactivity_timer);
    s_inactivity_timer = NULL;
  }
}

static void prv_inactivity_timeout(void *context) {
  s_inactivity_timer = NULL;
  window_stack_remove(s_window, true);
}

static void prv_reset_inactivity_timer(void) {
  if (s_inactivity_timer) {
    app_timer_reschedule(s_inactivity_timer, INACTIVITY_TIMEOUT_MS);
  } else {
    s_inactivity_timer = app_timer_register(INACTIVITY_TIMEOUT_MS, prv_inactivity_timeout, NULL);
  }
}

// Selecting a stop shows its departures, in place of the list the route
// was opened from (the departure window is a single instance)
static void prv_select_click(MenuLayer *menu_layer, MenuIndex *index, void *context) {
  const CourseStop *stop = course_get_stop(index->row);
  if (!stop) return;
  data_set_station_name(course_stop_query(stop));
  window_stack_remove(s_window, true);
}

static void prv_selection_changed(MenuLayer *menu_layer, MenuIndex new_index,
                                  MenuIndex old_index, void *context) {
  prv_reset_inactivity_timer();
}

static void prv_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
  // The vehicle moves on with time even without new data
  if (s_menu_layer) layer_mark_dirty(menu_layer_get_layer(s_menu_layer));
}

// ---- Window ----

static void prv_update_loading(void) {
  bool has_data = course_get_count() > 0;
  layer_set_hidden(text_layer_get_layer(s_loading_layer), has_data);
  layer_set_hidden(menu_layer_get_layer(s_menu_layer), !has_data);
  if (!has_data) {
    text_layer_set_text(s_loading_layer, s_error[0] ? s_error : "Loading route...");
  }
}

static void prv_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_status_bar = status_bar_layer_create();
  layer_add_child(root, status_bar_layer_get_layer(s_status_bar));

  s_header_layer = layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT, bounds.size.w, HEADER_H));
  layer_set_update_proc(s_header_layer, prv_header_update_proc);
  layer_add_child(root, s_header_layer);

  int16_t top = STATUS_BAR_LAYER_HEIGHT + HEADER_H;
  s_menu_layer = menu_layer_create(GRect(0, top, bounds.size.w, bounds.size.h - top));
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_rows = prv_get_num_rows,
    .get_cell_height = prv_get_cell_height,
    .draw_row = prv_draw_row,
    .select_click = prv_select_click,
    .selection_changed = prv_selection_changed,
  });
  // A light highlight, so the bold user's stop stays the one that stands out
  menu_layer_set_highlight_colors(s_menu_layer, GColorLightGray, GColorBlack);
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  layer_add_child(root, menu_layer_get_layer(s_menu_layer));

  s_loading_layer = text_layer_create(GRect(PAD, bounds.size.h / 2 - 15, bounds.size.w - 2 * PAD, 60));
  text_layer_set_text_alignment(s_loading_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_loading_layer, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_loading_layer));

  prv_update_loading();
}

static void prv_window_unload(Window *window) {
  status_bar_layer_destroy(s_status_bar);
  layer_destroy(s_header_layer);
  menu_layer_destroy(s_menu_layer);
  text_layer_destroy(s_loading_layer);
  s_menu_layer = NULL;
  window_destroy(window);
  s_window = NULL;
}

static void prv_window_appear(Window *window) {
  s_visible = true;
  tick_timer_service_subscribe(MINUTE_UNIT, prv_minute_tick);
  prv_request_course();
  prv_reset_inactivity_timer();
  if (course_get_count() == 0) {
    s_no_reply_timer = app_timer_register(NO_REPLY_MS, prv_no_reply_timeout, NULL);
  }
}

static void prv_window_disappear(Window *window) {
  s_visible = false;
  tick_timer_service_unsubscribe();
  prv_stop_timers();
}

void route_window_push(const char *station, int index, const char *line, TransitType type,
                       const char *direction) {
  text_copy_utf8(s_station, station, sizeof(s_station));
  s_index = index;
  text_copy_utf8(s_line, line, sizeof(s_line));
  s_type = type;
  text_copy_utf8(s_direction, direction, sizeof(s_direction));
  s_error[0] = '\0';
  s_has_route = false;
  // Don't flash the previous route while this one loads
  course_clear();

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
    .appear = prv_window_appear,
    .disappear = prv_window_disappear,
  });
  window_stack_push(s_window, true);
}

void route_window_refresh(void) {
  if (!s_visible) return;
  s_error[0] = '\0';
  menu_layer_reload_data(s_menu_layer);
  if (!s_has_route && course_get_count() > 0) {
    s_has_route = true;
    menu_layer_set_selected_index(s_menu_layer, MenuIndex(0, course_get_focus()),
                                  MenuRowAlignCenter, false);
  }
  layer_mark_dirty(s_header_layer);
  prv_update_loading();
}

void route_window_show_error(const char *message) {
  if (!s_visible) return;
  text_copy_utf8(s_error, message, sizeof(s_error));
  layer_mark_dirty(s_header_layer);
  prv_update_loading();
}

void route_window_request_failed(AppMessageResult reason) {
  // Only retry while the refresh cycle is running (window visible)
  if (s_visible) prv_schedule_request(prv_next_request_delay(reason));
}

#endif

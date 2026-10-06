#include "departure_window.h"
#include "../modules/data.h"
#include "../modules/comm.h"
#include "../modules/icons.h"
#include "../modules/text.h"
#include "../modules/settings.h"
#include "../modules/round.h"
#include "route_window.h"

// Emery and gabbro (round, 260x260) share sizes; round layouts inset rows
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  #define ROW_HEIGHT 36
  #define BADGE_HEIGHT 24
  #define BADGE_WIDTH 38
  #define BADGE_MARGIN 6
  #define MINS_WIDTH 44
  #define HEADER_HEIGHT 22
  #define FONT_DIR FONT_KEY_GOTHIC_24
  #define FONT_MINS FONT_KEY_GOTHIC_24_BOLD
  #define FONT_BADGE FONT_KEY_GOTHIC_18_BOLD
  #define FONT_HEADER FONT_KEY_GOTHIC_18_BOLD
  #define DIR_TEXT_H 30
  #define BADGE_TEXT_OFFSET 3
  #define HEADER_TEXT_Y -2
  #define DIR_TEXT_Y_NUDGE 2
  #define DIR_X_GAP 4
  #define FONT_DIR_SMALL FONT_KEY_GOTHIC_18
  #define DIR_TEXT_H_SMALL 24
  #define DIR_TEXT_Y_NUDGE_SMALL 1
  // Optional bold direction text (Display settings)
  #define FONT_DIR_BOLD FONT_KEY_GOTHIC_24_BOLD
  #define FONT_DIR_SMALL_BOLD FONT_KEY_GOTHIC_18_BOLD
#else
  #define ROW_HEIGHT 34
  #define BADGE_HEIGHT 20
  #define BADGE_WIDTH 28
  #define BADGE_MARGIN 2
  #define MINS_WIDTH 34
  #define HEADER_HEIGHT 20
  #define FONT_DIR FONT_KEY_GOTHIC_24
  #define FONT_MINS FONT_KEY_GOTHIC_24_BOLD
  #define FONT_BADGE FONT_KEY_GOTHIC_14_BOLD
  #define FONT_HEADER FONT_KEY_GOTHIC_18_BOLD
  #define DIR_TEXT_H 28
  #define BADGE_TEXT_OFFSET 2
  #define HEADER_TEXT_Y -2
  #define DIR_TEXT_Y_NUDGE 3
  #define DIR_X_GAP 3
  #define FONT_DIR_SMALL FONT_KEY_GOTHIC_18
  #define DIR_TEXT_H_SMALL 22
  #define DIR_TEXT_Y_NUDGE_SMALL 1
#endif

// Auto-close after 15 minutes without scrolling to stop unnecessary API requests
#define INACTIVITY_TIMEOUT_MS (15 * 60 * 1000)
#define REFRESH_INTERVAL_MS 30000
#define RETRY_INTERVAL_MS 2000

static Window *s_window;
static StatusBarLayer *s_status_bar;
static ScrollLayer *s_scroll_layer;
static Layer *s_content_layer;
static TextLayer *s_loading_layer;
static bool s_received_data;
static AppTimer *s_inactivity_timer;
static AppTimer *s_refresh_timer;
static bool s_visible;
// Next retry delay after the phone doesn't answer; doubles up to the refresh
// interval and resets once the phone answers
static uint32_t s_timeout_retry_ms = RETRY_INTERVAL_MS;
// Last fetch error; shown until the next successful refresh
static char s_error[32];

static int16_t prv_get_content_height(void) {
  int16_t h = HEADER_HEIGHT + data_get_count() * ROW_HEIGHT;
#ifdef PBL_ROUND
  // Let the last row scroll up to the middle of the screen, where the circle is widest
  h += layer_get_bounds(window_get_root_layer(s_window)).size.h / 2 - ROW_HEIGHT / 2;
#endif
  return h;
}

// Inset on each side for content rows y .. y+h at the current scroll
// position, so they fit a round screen (always 0 on rectangular screens)
static int16_t prv_inset_for(int16_t y, int16_t h) {
  GSize screen = layer_get_bounds(window_get_root_layer(s_window)).size;
  int16_t top = STATUS_BAR_LAYER_HEIGHT + scroll_layer_get_content_offset(s_scroll_layer).y + y;
  return round_inset(top, top + h, screen);
}

static void prv_draw_header(GContext *ctx, int16_t width) {
  GRect header_rect = GRect(0, 0, width, HEADER_HEIGHT);

#ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, GColorDarkGray);
#else
  graphics_context_set_fill_color(ctx, GColorBlack);
#endif
  graphics_fill_rect(ctx, header_rect, 0, GCornerNone);

  graphics_context_set_text_color(ctx, GColorWhite);
  int16_t inset = prv_inset_for(0, HEADER_HEIGHT);
  GRect text_rect = GRect(4 + inset, HEADER_TEXT_Y, width - 8 - 2 * inset, HEADER_HEIGHT);
  const char *title = s_error[0] ? s_error : data_get_station_name();
  graphics_draw_text(ctx, title,
    fonts_get_system_font(FONT_HEADER),
    text_rect, GTextOverflowModeTrailingEllipsis,
    PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft), NULL);
}

static void prv_draw_departure_row(GContext *ctx, int index, int16_t y, int16_t width,
                                   int16_t inset) {
  Departure *dep = data_get_departure(index);
  if (!dep) return;

  int cy = y + ROW_HEIGHT / 2;

  // Alternate row background on color platforms
#ifdef PBL_COLOR
  if (index % 2 == 0) {
    graphics_context_set_fill_color(ctx, GColorWhite);
  } else {
    graphics_context_set_fill_color(ctx, GColorLightGray);
  }
  graphics_fill_rect(ctx, GRect(0, y, width, ROW_HEIGHT), 0, GCornerNone);
#endif

  // Rows mostly outside a round screen's edge: no room for badge and minutes,
  // so only the background stripe is drawn until they scroll further in
  if (width - 2 - inset - MINS_WIDTH < BADGE_MARGIN + inset + BADGE_WIDTH) return;

  graphics_context_set_text_color(ctx, GColorBlack);

  // Draw badge
  GRect badge_rect = GRect(BADGE_MARGIN + inset, cy - BADGE_HEIGHT / 2, BADGE_WIDTH, BADGE_HEIGHT);
  icons_draw_badge(ctx, dep->type, dep->line, badge_rect);
  if (settings_get_direction_arrows()) {
    icons_draw_direction_arrow(ctx, dep->direction_id, badge_rect);
  }

  // Restore text color after badge
  graphics_context_set_text_color(ctx, GColorBlack);

  // Build minutes string and measure its width
  char mins_buf[8];
  int total_mins = dep->minutes + dep->delay;
  snprintf(mins_buf, sizeof(mins_buf), "%d'", total_mins);
  GFont mins_font = fonts_get_system_font(FONT_MINS);
  GSize mins_size = graphics_text_layout_get_content_size(
    mins_buf, mins_font, GRect(0, 0, MINS_WIDTH, DIR_TEXT_H),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentRight);
  int mins_w = mins_size.w + 4; // small padding

  // Direction gets the space between badge and minutes (none for rows
  // squeezed at the very top/bottom of a round screen)
  int right = width - 2 - inset;
  int dir_x = BADGE_MARGIN + inset + BADGE_WIDTH + DIR_X_GAP;
  int dir_w = right - dir_x - mins_w;
  if (dir_w < 0) dir_w = 0;

  // Use large font if text fits, otherwise fall back to smaller font
#ifdef FONT_DIR_BOLD
  bool bold = settings_get_bold_text();
  const char *dir_font_key = bold ? FONT_DIR_BOLD : FONT_DIR;
  const char *dir_small_font_key = bold ? FONT_DIR_SMALL_BOLD : FONT_DIR_SMALL;
#else
  const char *dir_font_key = FONT_DIR;
  const char *dir_small_font_key = FONT_DIR_SMALL;
#endif
  GFont dir_font = fonts_get_system_font(dir_font_key);
  GSize text_size = graphics_text_layout_get_content_size(
    dep->direction, dir_font, GRect(0, 0, 500, 100),
    GTextOverflowModeWordWrap, GTextAlignmentLeft);
  int text_h = DIR_TEXT_H;
  int nudge = DIR_TEXT_Y_NUDGE;
  if (text_size.w >= dir_w) {
    dir_font = fonts_get_system_font(dir_small_font_key);
    text_h = DIR_TEXT_H_SMALL;
    nudge = DIR_TEXT_Y_NUDGE_SMALL;
  }

  int text_y = cy - text_h / 2 - nudge;
  GRect dir_rect = GRect(dir_x, text_y, dir_w, text_h);
  graphics_draw_text(ctx, dep->direction, dir_font,
    dir_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // Draw minutes right-aligned
  int mins_y = cy - DIR_TEXT_H / 2 - DIR_TEXT_Y_NUDGE;
  GRect mins_rect = GRect(right - mins_w, mins_y, mins_w, DIR_TEXT_H);
  graphics_draw_text(ctx, mins_buf, mins_font,
    mins_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  // Strike through direction and minutes of cancelled departures
  if (dep->cancelled && right > dir_x) {
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorRed, GColorBlack));
    graphics_fill_rect(ctx, GRect(dir_x, cy - 1, right - dir_x, 2), 0, GCornerNone);
  }
}

static void prv_content_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  prv_draw_header(ctx, bounds.size.w);

  int count = data_get_count();
  for (int i = 0; i < count; i++) {
    int16_t y = HEADER_HEIGHT + i * ROW_HEIGHT;
    // Inset for the band where badge, arrow and minutes are drawn, not the
    // whole row
    int16_t arrow_h = settings_get_direction_arrows() ? icons_direction_arrow_height() : 0;
    int16_t inset = prv_inset_for(y + (ROW_HEIGHT - BADGE_HEIGHT) / 2 - arrow_h,
                                  BADGE_HEIGHT + arrow_h);
    prv_draw_departure_row(ctx, i, y, bounds.size.w, inset);
  }
}

static void prv_update_content_size(void) {
  if (!s_scroll_layer || !s_content_layer) return;

  GRect scroll_bounds = layer_get_bounds(scroll_layer_get_layer(s_scroll_layer));
  int16_t content_h = prv_get_content_height();
  // Ensure at least the scroll view height
  if (content_h < scroll_bounds.size.h) content_h = scroll_bounds.size.h;

  layer_set_frame(s_content_layer, GRect(0, 0, scroll_bounds.size.w, content_h));
  scroll_layer_set_content_size(s_scroll_layer, GSize(scroll_bounds.size.w, content_h));
  layer_mark_dirty(s_content_layer);

  bool has_data = data_get_count() > 0;
  layer_set_hidden(text_layer_get_layer(s_loading_layer), has_data);
  if (!has_data && s_error[0]) {
    text_layer_set_text(s_loading_layer, s_error);
  } else if (!has_data && s_received_data) {
    text_layer_set_text(s_loading_layer, "No departures");
  }
}

static void prv_inactivity_timeout(void *context) {
  s_inactivity_timer = NULL;
  window_stack_pop(true);
}

static void prv_reset_inactivity_timer(void) {
  if (s_inactivity_timer) {
    app_timer_reschedule(s_inactivity_timer, INACTIVITY_TIMEOUT_MS);
  } else {
    s_inactivity_timer = app_timer_register(INACTIVITY_TIMEOUT_MS, prv_inactivity_timeout, NULL);
  }
}

static void prv_scrolled(ScrollLayer *scroll_layer, void *context) {
  prv_reset_inactivity_timer();
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_status_bar = status_bar_layer_create();
  layer_add_child(window_layer, status_bar_layer_get_layer(s_status_bar));

  // Scroll layer fills window below status bar
  GRect scroll_bounds = GRect(0, STATUS_BAR_LAYER_HEIGHT, bounds.size.w,
                               bounds.size.h - STATUS_BAR_LAYER_HEIGHT);
  s_scroll_layer = scroll_layer_create(scroll_bounds);
  scroll_layer_set_shadow_hidden(s_scroll_layer, true);
  scroll_layer_set_click_config_onto_window(s_scroll_layer, window);
  scroll_layer_set_callbacks(s_scroll_layer, (ScrollLayerCallbacks) {
    .content_offset_changed_handler = prv_scrolled,
  });
  layer_add_child(window_layer, scroll_layer_get_layer(s_scroll_layer));

  // Content layer drawn inside scroll layer
  s_content_layer = layer_create(GRect(0, 0, bounds.size.w, bounds.size.h));
  layer_set_update_proc(s_content_layer, prv_content_update_proc);
  scroll_layer_add_child(s_scroll_layer, s_content_layer);

  // Loading text centered
  s_loading_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 10, bounds.size.w, 20));
  text_layer_set_text(s_loading_layer, "Loading...");
  text_layer_set_text_alignment(s_loading_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_loading_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_loading_layer));

  prv_update_content_size();
}

#ifdef PBL_TOUCH
// Tapping a row opens its route. Taps are read from the raw touch stream:
// the system touch bridge keeps scrolling the list, but doesn't say where a
// tap landed. A touch is a tap if it lifts within TAP_MAX_MS and never moves
// more than TAP_SLOP px.
#define TAP_SLOP 10
#define TAP_MAX_MS 500

static bool s_touch_subscribed;
static bool s_touch_is_tap;
static GPoint s_touch_start;
static uint32_t s_touch_start_ms;

static uint32_t prv_now_ms(void) {
  time_t secs;
  uint16_t ms;
  time_ms(&secs, &ms);
  return (uint32_t)secs * 1000 + ms;
}

static void prv_open_route_at(int16_t screen_y) {
  // Content offset is <= 0 once scrolled, so subtracting it adds the scroll
  int16_t offset = scroll_layer_get_content_offset(s_scroll_layer).y;
  int16_t content_y = screen_y - STATUS_BAR_LAYER_HEIGHT - offset - HEADER_HEIGHT;
  int row = content_y >= 0 ? content_y / ROW_HEIGHT : -1;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Tap at y=%d: row %d", screen_y, row);
  Departure *dep = data_get_departure(row);
  if (!dep) return;
  prv_reset_inactivity_timer();
  route_window_push(data_get_station_name(), row, dep->line, dep->type, dep->direction);
}

static bool prv_moved_past_slop(const TouchEvent *event) {
  return abs(event->x - s_touch_start.x) > TAP_SLOP || abs(event->y - s_touch_start.y) > TAP_SLOP;
}

static void prv_touch_handler(const TouchEvent *event, void *context) {
  if (event->non_navigational) return;
  switch (event->type) {
    case TouchEvent_Touchdown:
      s_touch_start = GPoint(event->x, event->y);
      s_touch_start_ms = prv_now_ms();
      s_touch_is_tap = true;
      break;
    case TouchEvent_PositionUpdate:
      if (prv_moved_past_slop(event)) s_touch_is_tap = false;
      break;
    case TouchEvent_Liftoff:
      if (s_touch_is_tap && !prv_moved_past_slop(event) &&
          prv_now_ms() - s_touch_start_ms <= TAP_MAX_MS) {
        prv_open_route_at(s_touch_start.y);
      }
      s_touch_is_tap = false;
      break;
  }
}
#endif

// Taps only open routes while the window is visible and touch navigation is on
static void prv_update_touch_subscription(void) {
#ifdef PBL_TOUCH
  bool want = s_visible && settings_get_touch_nav();
  if (want == s_touch_subscribed) return;
  if (want) {
    touch_service_subscribe(prv_touch_handler, NULL);
  } else {
    touch_service_unsubscribe();
  }
  s_touch_subscribed = want;
#endif
}

static void prv_refresh_timer_callback(void *context);

// Delay before the next departure request, given how the last one went.
// Retry soon if it only collided with another message, back off if the
// phone didn't answer, and otherwise (sent, or e.g. disconnected) wait for
// the normal refresh
static uint32_t prv_next_request_delay(AppMessageResult result) {
  switch (result) {
    case APP_MSG_BUSY:
    case APP_MSG_SEND_REJECTED:
      return RETRY_INTERVAL_MS;
    case APP_MSG_SEND_TIMEOUT: {
      uint32_t delay = s_timeout_retry_ms;
      s_timeout_retry_ms = delay * 2 < REFRESH_INTERVAL_MS ? delay * 2 : REFRESH_INTERVAL_MS;
      return delay;
    }
    default:
      return REFRESH_INTERVAL_MS;
  }
}

static void prv_request_departures(void) {
  AppMessageResult result = comm_request_departures(data_get_station_name());
  s_refresh_timer = app_timer_register(prv_next_request_delay(result),
                                       prv_refresh_timer_callback, NULL);
}

static void prv_refresh_timer_callback(void *context) {
  prv_request_departures();
}

static void prv_stop_refresh(void) {
  if (s_refresh_timer) {
    app_timer_cancel(s_refresh_timer);
    s_refresh_timer = NULL;
  }
}

static void prv_window_unload(Window *window) {
  status_bar_layer_destroy(s_status_bar);
  scroll_layer_destroy(s_scroll_layer);
  layer_destroy(s_content_layer);
  text_layer_destroy(s_loading_layer);
  s_scroll_layer = NULL;
  s_content_layer = NULL;
  s_loading_layer = NULL;
  window_destroy(window);
  s_window = NULL;
}

static void prv_window_appear(Window *window) {
  s_received_data = false;
  s_error[0] = '\0';
  s_timeout_retry_ms = RETRY_INTERVAL_MS;
  // Drop anything that arrived after the last departure window closed
  data_set_count(0);
  text_layer_set_text(s_loading_layer, "Loading...");
  prv_update_content_size();
  // Request departures now, then every 30s
  prv_stop_refresh();
  prv_request_departures();
  prv_reset_inactivity_timer();
  s_visible = true;
  prv_update_touch_subscription();
}

static void prv_window_disappear(Window *window) {
  s_visible = false;
  prv_update_touch_subscription();
  prv_stop_refresh();
  if (s_inactivity_timer) {
    app_timer_cancel(s_inactivity_timer);
    s_inactivity_timer = NULL;
  }
  // Clear departure data so next station starts fresh
  data_set_count(0);
}

void departure_window_push(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
    .appear = prv_window_appear,
    .disappear = prv_window_disappear,
  });
  window_stack_push(s_window, true);
}

void departure_window_refresh(void) {
  s_received_data = true;
  s_timeout_retry_ms = RETRY_INTERVAL_MS;
  s_error[0] = '\0';
  prv_update_content_size();
}

void departure_window_redraw(void) {
  if (s_content_layer) {
    layer_mark_dirty(s_content_layer);
  }
  prv_update_touch_subscription();
}

void departure_window_request_failed(AppMessageResult reason) {
  // Only retry while the refresh cycle is running (window visible)
  if (s_refresh_timer) {
    app_timer_reschedule(s_refresh_timer, prv_next_request_delay(reason));
  }
}

void departure_window_show_error(const char *message) {
  s_timeout_retry_ms = RETRY_INTERVAL_MS;
  text_copy_utf8(s_error, message, sizeof(s_error));
  prv_update_content_size();
}

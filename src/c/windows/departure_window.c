#include "departure_window.h"
#include "../modules/data.h"
#include "../modules/comm.h"
#include "../modules/icons.h"

// Defined in main.c
extern void app_start_departure_refresh(void);
extern void app_stop_departure_refresh(void);

#ifdef PBL_PLATFORM_EMERY
  #define ROW_HEIGHT 36
  #define BADGE_HEIGHT 24
  #define BADGE_WIDTH 38
  #define BADGE_MARGIN 6
  #define MINS_WIDTH 34
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
#else
  #define ROW_HEIGHT 34
  #define BADGE_HEIGHT 20
  #define BADGE_WIDTH 28
  #define BADGE_MARGIN 2
  #define MINS_WIDTH 24
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

static Window *s_window;
static ScrollLayer *s_scroll_layer;
static Layer *s_content_layer;
static TextLayer *s_loading_layer;
static bool s_received_data;

static int16_t prv_get_content_height(void) {
  return HEADER_HEIGHT + data_get_count() * ROW_HEIGHT;
}

static void prv_draw_header(GContext *ctx, GRect bounds, int16_t width) {
  GRect header_rect = GRect(0, 0, width, HEADER_HEIGHT);

#ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, GColorDarkGray);
#else
  graphics_context_set_fill_color(ctx, GColorBlack);
#endif
  graphics_fill_rect(ctx, header_rect, 0, GCornerNone);

  graphics_context_set_text_color(ctx, GColorWhite);
  GRect text_rect = GRect(4, HEADER_TEXT_Y, width - 8, HEADER_HEIGHT);
  graphics_draw_text(ctx, data_get_station_name(),
    fonts_get_system_font(FONT_HEADER),
    text_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void prv_draw_departure_row(GContext *ctx, int index, int16_t y, int16_t width) {
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

  graphics_context_set_text_color(ctx, GColorBlack);

  // Draw badge
  GRect badge_rect = GRect(BADGE_MARGIN, cy - BADGE_HEIGHT / 2, BADGE_WIDTH, BADGE_HEIGHT);
  icons_draw_badge(ctx, dep->type, dep->line, badge_rect);

  // Restore text color after badge
  graphics_context_set_text_color(ctx, GColorBlack);

  // Direction and minutes
  int dir_x = BADGE_MARGIN + BADGE_WIDTH + DIR_X_GAP;
  int dir_w = width - dir_x - MINS_WIDTH - 2;

  // Use large font if text fits, otherwise fall back to smaller font
  GFont dir_font = fonts_get_system_font(FONT_DIR);
  GSize text_size = graphics_text_layout_get_content_size(
    dep->direction, dir_font, GRect(0, 0, 500, 100),
    GTextOverflowModeWordWrap, GTextAlignmentLeft);
  int text_h = DIR_TEXT_H;
  int nudge = DIR_TEXT_Y_NUDGE;
  if (text_size.w >= dir_w) {
    dir_font = fonts_get_system_font(FONT_DIR_SMALL);
    text_h = DIR_TEXT_H_SMALL;
    nudge = DIR_TEXT_Y_NUDGE_SMALL;
  }

  int text_y = cy - text_h / 2 - nudge;
  GRect dir_rect = GRect(dir_x, text_y, dir_w, text_h);
  graphics_draw_text(ctx, dep->direction, dir_font,
    dir_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  char mins_buf[8];
  int total_mins = dep->minutes + dep->delay;
  snprintf(mins_buf, sizeof(mins_buf), "%d'", total_mins);
  // Minutes always use large font positioning
  int mins_y = cy - DIR_TEXT_H / 2 - DIR_TEXT_Y_NUDGE;
  GRect mins_rect = GRect(width - MINS_WIDTH - 2, mins_y, MINS_WIDTH, DIR_TEXT_H);
  graphics_draw_text(ctx, mins_buf,
    fonts_get_system_font(FONT_MINS),
    mins_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
}

static void prv_content_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  prv_draw_header(ctx, bounds, bounds.size.w);

  int count = data_get_count();
  for (int i = 0; i < count; i++) {
    int16_t y = HEADER_HEIGHT + i * ROW_HEIGHT;
    prv_draw_departure_row(ctx, i, y, bounds.size.w);
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
  if (!has_data && s_received_data) {
    text_layer_set_text(s_loading_layer, "No departures");
  }
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Scroll layer fills window
  s_scroll_layer = scroll_layer_create(bounds);
  scroll_layer_set_shadow_hidden(s_scroll_layer, true);
  scroll_layer_set_click_config_onto_window(s_scroll_layer, window);
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

static void prv_window_unload(Window *window) {
  app_stop_departure_refresh();
  scroll_layer_destroy(s_scroll_layer);
  layer_destroy(s_content_layer);
  text_layer_destroy(s_loading_layer);
  s_scroll_layer = NULL;
  s_content_layer = NULL;
}

static void prv_window_appear(Window *window) {
  s_received_data = false;
  text_layer_set_text(s_loading_layer, "Loading...");
  app_start_departure_refresh();
}

static void prv_window_disappear(Window *window) {
  app_stop_departure_refresh();
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
  prv_update_content_size();
}

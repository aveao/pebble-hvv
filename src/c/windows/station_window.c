#include "station_window.h"
#include "departure_window.h"
#include "../modules/stations.h"
#include "../modules/comm.h"
#include "../modules/data.h"

#define SECTION_NEARBY 0
#define SECTION_FAVORITES 1

static Window *s_window;
static MenuLayer *s_menu_layer;
static TextLayer *s_loading_layer;
static bool s_received_data;

static uint16_t prv_get_num_sections(MenuLayer *menu_layer, void *context) {
  return 2;
}

static uint16_t prv_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *context) {
  if (section_index == SECTION_NEARBY) {
    return (uint16_t)stations_get_nearby_count();
  } else {
    return (uint16_t)stations_get_favorite_count();
  }
}

static int16_t prv_get_header_height(MenuLayer *menu_layer, uint16_t section_index, void *context) {
  // Hide section header if empty
  if (section_index == SECTION_NEARBY && stations_get_nearby_count() == 0) return 0;
  if (section_index == SECTION_FAVORITES && stations_get_favorite_count() == 0) return 0;
  return 16;
}

static void prv_draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *context) {
  GRect bounds = layer_get_bounds(cell_layer);

#ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, GColorDarkGray);
#else
  graphics_context_set_fill_color(ctx, GColorBlack);
#endif
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  graphics_context_set_text_color(ctx, GColorWhite);
  const char *title = (section_index == SECTION_NEARBY) ? "Nearby" : "Favorites";
  GRect text_rect = GRect(4, -2, bounds.size.w - 8, 16);
  graphics_draw_text(ctx, title,
    fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    text_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static int16_t prv_get_cell_height(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  return 22;
}

static void prv_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *context) {
  Station *station;
  if (cell_index->section == SECTION_NEARBY) {
    station = stations_get_nearby(cell_index->row);
  } else {
    station = stations_get_favorite(cell_index->row);
  }
  if (!station) return;

  GRect bounds = layer_get_bounds(cell_layer);

  // Draw station name — give full width for favorites, leave space for distance on nearby
  int dist_width = (station->type == STATION_NEARBY && station->distance > 0) ? 36 : 0;
  GRect name_rect = GRect(4, -2, bounds.size.w - 8 - dist_width, 22);
  graphics_draw_text(ctx, station->name,
    fonts_get_system_font(FONT_KEY_GOTHIC_18),
    name_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // Draw distance for nearby stations
  if (dist_width > 0) {
    char dist_buf[8];
    int meters = station->distance * 10;
    snprintf(dist_buf, sizeof(dist_buf), "%dm", meters);
    GRect dist_rect = GRect(bounds.size.w - dist_width - 2, 0, dist_width, 20);
    graphics_draw_text(ctx, dist_buf,
      fonts_get_system_font(FONT_KEY_GOTHIC_14),
      dist_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  }
}

static void prv_select_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  Station *station;
  if (cell_index->section == SECTION_NEARBY) {
    station = stations_get_nearby(cell_index->row);
  } else {
    station = stations_get_favorite(cell_index->row);
  }
  if (!station) return;

  // Set station name for departure header and tell JS
  data_set_station_name(station->name);
  comm_select_station(station->name);

  // Push departure window
  departure_window_push();
}

static void prv_update_loading_visibility(void) {
  bool has_data = stations_get_count() > 0;
  layer_set_hidden(text_layer_get_layer(s_loading_layer), has_data);
  layer_set_hidden(menu_layer_get_layer(s_menu_layer), !has_data);
  if (!has_data && s_received_data) {
    text_layer_set_text(s_loading_layer, "No stops found.\nSet favorites in\napp settings.");
  }
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_sections = prv_get_num_sections,
    .get_num_rows = prv_get_num_rows,
    .get_header_height = prv_get_header_height,
    .draw_header = prv_draw_header,
    .get_cell_height = prv_get_cell_height,
    .draw_row = prv_draw_row,
    .select_click = prv_select_click,
  });
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
#ifdef PBL_COLOR
  menu_layer_set_highlight_colors(s_menu_layer, GColorCobaltBlue, GColorWhite);
#endif
  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));

  s_received_data = false;
  s_loading_layer = text_layer_create(GRect(10, bounds.size.h / 2 - 30, bounds.size.w - 20, 60));
  text_layer_set_text(s_loading_layer, "Loading stops...");
  text_layer_set_text_alignment(s_loading_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_loading_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_loading_layer));

  prv_update_loading_visibility();
}

static void prv_window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
  text_layer_destroy(s_loading_layer);
}

static void prv_window_appear(Window *window) {
  // Request fresh station list when returning to this window
  comm_request_stations();
}

void station_window_push(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
    .appear = prv_window_appear,
  });
  window_stack_push(s_window, true);
}

void station_window_refresh(void) {
  s_received_data = true;
  if (s_menu_layer) {
    menu_layer_reload_data(s_menu_layer);
    prv_update_loading_visibility();
  }
}

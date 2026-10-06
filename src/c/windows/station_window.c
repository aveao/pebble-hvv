#include "station_window.h"
#include "departure_window.h"
#include "../modules/stations.h"
#include "../modules/comm.h"
#include "../modules/data.h"
#include "../modules/text.h"

#define SECTION_FAVORITES 0
#define SECTION_NEARBY 1

// Emery and gabbro (round, 260x260) share sizes; round layouts inset rows
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  #define STN_HEADER_HEIGHT 28
  #define STN_ROW_HEIGHT 40
  #define STN_FONT_NAME FONT_KEY_GOTHIC_28_BOLD
  #define STN_FONT_HEADER FONT_KEY_GOTHIC_24_BOLD
  #define STN_FONT_DIST FONT_KEY_GOTHIC_24
  #define STN_HEADER_TEXT_Y -3
  #define STN_NAME_TEXT_Y 1
  #define STN_DIST_TEXT_Y 3
  #define STN_DIST_WIDTH 60
  #define STN_DOT_RADIUS 4
#else
  #define STN_HEADER_HEIGHT 20
  #define STN_ROW_HEIGHT 30
  #define STN_FONT_NAME FONT_KEY_GOTHIC_24
  #define STN_FONT_HEADER FONT_KEY_GOTHIC_18_BOLD
  #define STN_FONT_DIST FONT_KEY_GOTHIC_18
  #define STN_HEADER_TEXT_Y -2
  #define STN_NAME_TEXT_Y -2
  #define STN_DIST_TEXT_Y 1
  #define STN_DIST_WIDTH 44
  #define STN_DOT_RADIUS 3
#endif

#ifdef PBL_ROUND
  // Centre-focused round menu: tall selected row, short neighbours
  #define STN_ROUND_FOCUSED_HEIGHT MENU_CELL_ROUND_FOCUSED_SHORT_CELL_HEIGHT
  // Selected row without a distance/services line (e.g. favorites)
  #define STN_ROUND_FOCUSED_ONE_LINE_HEIGHT 44
  #define STN_ROUND_UNFOCUSED_HEIGHT MENU_CELL_ROUND_UNFOCUSED_TALL_CELL_HEIGHT
  #define STN_ROUND_HEADER_HEIGHT 24
  #define STN_ROUND_FONT_HEADER FONT_KEY_GOTHIC_18_BOLD
  #define STN_ROUND_FONT_NAME_SMALL FONT_KEY_GOTHIC_24
  #define STN_ROUND_PAD 12
  #define STN_ROUND_NAME_H 34
  #define STN_ROUND_META_Y 36
#endif

static Window *s_window;
static StatusBarLayer *s_status_bar;
static MenuLayer *s_menu_layer;
static TextLayer *s_loading_layer;
static bool s_received_data;
static bool s_appeared_before;
// Last stop lookup error, and the loading text built from it
static char s_error[32];
static char s_error_text[48];

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

static Station *prv_station_at(const MenuIndex *index) {
  return index->section == SECTION_NEARBY
    ? stations_get_nearby(index->row) : stations_get_favorite(index->row);
}

#ifdef PBL_ROUND
// Selected nearby stations get a second line with distance and service dots;
// favorites have neither, so their selected row stays one line tall
static bool prv_has_meta_line(const Station *station) {
  return station && station->type == STATION_NEARBY &&
         (station->distance > 0 || station->services);
}
#endif

static int16_t prv_get_header_height(MenuLayer *menu_layer, uint16_t section_index, void *context) {
  // Hide section header if empty
  if (section_index == SECTION_NEARBY && stations_get_nearby_count() == 0) return 0;
  if (section_index == SECTION_FAVORITES && stations_get_favorite_count() == 0) return 0;
  return PBL_IF_ROUND_ELSE(STN_ROUND_HEADER_HEIGHT, STN_HEADER_HEIGHT);
}

static void prv_draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *context) {
  GRect bounds = layer_get_bounds(cell_layer);

#ifdef PBL_ROUND
  // Small centred caps label instead of a full-width bar
  graphics_context_set_text_color(ctx, GColorDarkGray);
  const char *label = (section_index == SECTION_NEARBY) ? "NEARBY" : "FAVORITES";
  graphics_draw_text(ctx, label, fonts_get_system_font(STN_ROUND_FONT_HEADER),
    GRect(0, -2, bounds.size.w, STN_ROUND_HEADER_HEIGHT),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  return;
#endif

#ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, GColorDarkGray);
#else
  graphics_context_set_fill_color(ctx, GColorBlack);
#endif
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  graphics_context_set_text_color(ctx, GColorWhite);
  const char *title = (section_index == SECTION_NEARBY) ? "Nearby" : "Favorites";
  GRect text_rect = GRect(4, STN_HEADER_TEXT_Y, bounds.size.w - 8, STN_HEADER_HEIGHT);
  graphics_draw_text(ctx, title,
    fonts_get_system_font(STN_FONT_HEADER),
    text_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

#ifdef PBL_ROUND
// Selected row on round, tracked by us rather than read from MenuLayer. The
// selected row is taller than its neighbours, and MenuLayer caches the new
// selection's position using the old row's selected height (a reload doesn't
// fix it), leaving a gap. So rows are sized from this, and the menu's own
// selection is only moved after heights are up to date (see prv_round_select)
static MenuIndex s_round_sel;

static bool prv_is_valid_row(MenuIndex index) {
  return index.section <= SECTION_NEARBY &&
         index.row < prv_get_num_rows(NULL, index.section, NULL);
}

static MenuIndex prv_first_row(void) {
  return MenuIndex(stations_get_favorite_count() > 0 ? SECTION_FAVORITES : SECTION_NEARBY, 0);
}

static bool prv_is_selected_row(MenuIndex *index) {
  return index->section == s_round_sel.section && index->row == s_round_sel.row;
}
#endif

static int16_t prv_get_cell_height(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
#ifdef PBL_ROUND
  if (!prv_is_selected_row(cell_index)) return STN_ROUND_UNFOCUSED_HEIGHT;
  return prv_has_meta_line(prv_station_at(cell_index))
    ? STN_ROUND_FOCUSED_HEIGHT : STN_ROUND_FOCUSED_ONE_LINE_HEIGHT;
#else
  return STN_ROW_HEIGHT;
#endif
}

#ifdef PBL_COLOR
static const uint8_t s_svc_flags[] = {
  SERVICE_SBAHN, SERVICE_UBAHN, SERVICE_BUS, SERVICE_ABAHN, SERVICE_FERRY, SERVICE_TRAIN
};

#define STN_DOT_SPACING (STN_DOT_RADIUS * 2 + 2)

static GColor prv_service_color(uint8_t flag) {
  switch (flag) {
    case SERVICE_SBAHN: return GColorIslamicGreen;
    case SERVICE_UBAHN: return GColorBlue;
    case SERVICE_BUS:   return GColorRed;
    case SERVICE_ABAHN: return GColorOrange;
    case SERVICE_FERRY: return GColorTiffanyBlue;
    case SERVICE_TRAIN: return GColorLightGray;
    default:            return GColorDarkGray;
  }
}

// Width of the row of service dots for these services (0 if none)
static int prv_service_dots_width(uint8_t services) {
  int n = 0;
  for (int i = 0; i < (int)ARRAY_LENGTH(s_svc_flags); i++) {
    if (services & s_svc_flags[i]) n++;
  }
  return n ? n * STN_DOT_SPACING - 2 : 0;
}

// Draw service dots left to right from x, centred vertically on cy
static void prv_draw_service_dots(GContext *ctx, uint8_t services, int x, int cy) {
  for (int i = 0; i < (int)ARRAY_LENGTH(s_svc_flags); i++) {
    if (!(services & s_svc_flags[i])) continue;
    graphics_context_set_fill_color(ctx, prv_service_color(s_svc_flags[i]));
    graphics_fill_circle(ctx, GPoint(x + STN_DOT_RADIUS, cy), STN_DOT_RADIUS);
    x += STN_DOT_SPACING;
  }
}
#endif

#ifdef PBL_ROUND
// Centre-focused round row: the selected station large with distance and
// service dots underneath; neighbours show just their name, smaller
static void prv_draw_row_round(GContext *ctx, const Layer *cell_layer, Station *station) {
  GRect bounds = layer_get_bounds(cell_layer);
  int16_t text_w = bounds.size.w - 2 * STN_ROUND_PAD;

  if (!menu_cell_layer_is_highlighted(cell_layer)) {
    graphics_draw_text(ctx, station->name, fonts_get_system_font(STN_ROUND_FONT_NAME_SMALL),
      GRect(STN_ROUND_PAD, (bounds.size.h - 30) / 2 - 2, text_w, 30),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    return;
  }

  bool has_meta = prv_has_meta_line(station);
  int16_t name_y = has_meta ? 0 : (bounds.size.h - STN_ROUND_NAME_H) / 2 - 1;
  graphics_draw_text(ctx, station->name, fonts_get_system_font(STN_FONT_NAME),
    GRect(STN_ROUND_PAD, name_y, text_w, STN_ROUND_NAME_H),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  if (!has_meta) return;

  // "120m ●●●" centred as one group
  char dist_buf[8] = "";
  if (station->distance > 0) {
    snprintf(dist_buf, sizeof(dist_buf), "%dm", station->distance * 10);
  }
  GFont dist_font = fonts_get_system_font(STN_FONT_DIST);
  GSize dist_size = GSize(0, 0);
  if (dist_buf[0]) {
    dist_size = graphics_text_layout_get_content_size(dist_buf, dist_font,
      GRect(0, 0, text_w, 30), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  }
  int dots_w = prv_service_dots_width(station->services);
  int gap = (dist_size.w > 0 && dots_w > 0) ? 6 : 0;
  int x = (bounds.size.w - (dist_size.w + gap + dots_w)) / 2;
  if (dist_buf[0]) {
    graphics_draw_text(ctx, dist_buf, dist_font,
      GRect(x, STN_ROUND_META_Y - 6, dist_size.w + 2, 30),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
  if (dots_w > 0) {
    prv_draw_service_dots(ctx, station->services, x + dist_size.w + gap, STN_ROUND_META_Y + 10);
  }
}
#endif

static void prv_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *context) {
  Station *station = prv_station_at(cell_index);
  if (!station) return;

#ifdef PBL_ROUND
  prv_draw_row_round(ctx, cell_layer, station);
  return;
#endif

  GRect bounds = layer_get_bounds(cell_layer);

  // Draw station name — give full width for favorites, leave space for distance on nearby
  int dist_width = (station->type == STATION_NEARBY && station->distance > 0) ? STN_DIST_WIDTH : 0;
  GRect name_rect = GRect(4, STN_NAME_TEXT_Y, bounds.size.w - 8 - dist_width, STN_ROW_HEIGHT);
  graphics_draw_text(ctx, station->name,
    fonts_get_system_font(STN_FONT_NAME),
    name_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // Draw distance for nearby stations
  if (dist_width > 0) {
    char dist_buf[8];
    int meters = station->distance * 10;
    snprintf(dist_buf, sizeof(dist_buf), "%dm", meters);
    GRect dist_rect = GRect(bounds.size.w - dist_width - 2, STN_DIST_TEXT_Y, dist_width, STN_ROW_HEIGHT);
    graphics_draw_text(ctx, dist_buf,
      fonts_get_system_font(STN_FONT_DIST),
      dist_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  }

#ifdef PBL_COLOR
  // Service dots right-aligned under the distance
  int dots_w = prv_service_dots_width(station->services);
  if (dots_w > 0) {
    prv_draw_service_dots(ctx, station->services, bounds.size.w - 2 - dots_w,
                          STN_ROW_HEIGHT - STN_DOT_RADIUS - 2);
  }
#endif
}

// Ask JS for the list again once a lookup has come back empty or failed
static void prv_retry_stations(void) {
  if (!s_received_data || stations_get_count() > 0) return;
  s_received_data = false;
  s_error[0] = '\0';
  text_layer_set_text(s_loading_layer, "Loading stops...");
  comm_request_stations();
}

static void prv_select_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  Station *station = prv_station_at(cell_index);
  if (!station) {
    prv_retry_stations();
    return;
  }

  // Departure window requests departures for this station when it appears
  data_set_station_name(station->name);
  departure_window_push();
}

#ifdef PBL_ROUND
// Select a row on round: update our selection so rows are measured for it,
// reload, then jump the menu's selection there without animation (reloading
// during MenuLayer's own selection animation crashes)
static void prv_round_select(MenuIndex index) {
  s_round_sel = index;
  menu_layer_reload_data(s_menu_layer);
  menu_layer_set_selected_index(s_menu_layer, index, MenuRowAlignCenter, false);
}

// Keep the round selection on a real row when the list changes
static void prv_round_sync_selection(void) {
  if (!prv_is_valid_row(s_round_sel)) s_round_sel = prv_first_row();
  if (prv_is_valid_row(s_round_sel)) {
    prv_round_select(s_round_sel);
  } else {
    // Empty list: start from the top again once stations arrive
    s_round_sel = MenuIndex(SECTION_FAVORITES, 0);
    menu_layer_reload_data(s_menu_layer);
  }
}

// Next/previous real row, crossing between sections; false at either end
static bool prv_round_step(MenuIndex *index, bool up) {
  MenuIndex i = *index;
  if (up) {
    if (i.row > 0) {
      i.row--;
    } else if (i.section == SECTION_NEARBY && stations_get_favorite_count() > 0) {
      i = MenuIndex(SECTION_FAVORITES, stations_get_favorite_count() - 1);
    } else {
      return false;
    }
  } else {
    if (i.row + 1 < prv_get_num_rows(NULL, i.section, NULL)) {
      i.row++;
    } else if (i.section == SECTION_FAVORITES && stations_get_nearby_count() > 0) {
      i = MenuIndex(SECTION_NEARBY, 0);
    } else {
      return false;
    }
  }
  *index = i;
  return true;
}

static void prv_round_move(bool up) {
  MenuIndex next = s_round_sel;
  if (prv_round_step(&next, up)) prv_round_select(next);
}

static void prv_round_up_click(ClickRecognizerRef recognizer, void *context) {
  prv_round_move(true);
}

static void prv_round_down_click(ClickRecognizerRef recognizer, void *context) {
  prv_round_move(false);
}

static void prv_round_select_click(ClickRecognizerRef recognizer, void *context) {
  if (prv_is_valid_row(s_round_sel)) {
    prv_select_click(s_menu_layer, &s_round_sel, NULL);
  } else {
    prv_retry_stations();
  }
}

static void prv_round_click_config(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, prv_round_up_click);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, prv_round_down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_round_select_click);
}
#endif

static void prv_update_loading_visibility(void) {
  bool has_data = stations_get_count() > 0;
  layer_set_hidden(text_layer_get_layer(s_loading_layer), has_data);
  layer_set_hidden(menu_layer_get_layer(s_menu_layer), !has_data);
  if (!has_data && s_error[0]) {
    snprintf(s_error_text, sizeof(s_error_text), "%s\nSelect to retry", s_error);
    text_layer_set_text(s_loading_layer, s_error_text);
  } else if (!has_data && s_received_data) {
    text_layer_set_text(s_loading_layer, "No stops found.\nSet favorites in\napp settings.");
  }
}


static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_status_bar = status_bar_layer_create();
  layer_add_child(window_layer, status_bar_layer_get_layer(s_status_bar));

  GRect content_bounds = GRect(0, STATUS_BAR_LAYER_HEIGHT, bounds.size.w,
                                bounds.size.h - STATUS_BAR_LAYER_HEIGHT);
  s_menu_layer = menu_layer_create(content_bounds);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_sections = prv_get_num_sections,
    .get_num_rows = prv_get_num_rows,
    .get_header_height = prv_get_header_height,
    .draw_header = prv_draw_header,
    .get_cell_height = prv_get_cell_height,
    .draw_row = prv_draw_row,
    .select_click = prv_select_click,
  });
#ifdef PBL_ROUND
  window_set_click_config_provider(window, prv_round_click_config);
#else
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
#endif
#ifdef PBL_COLOR
  menu_layer_set_highlight_colors(s_menu_layer, GColorCobaltBlue, GColorWhite);
#endif
  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
#ifdef PBL_ROUND
  prv_round_sync_selection();  // favorites may already be cached
#endif

  s_received_data = false;
  s_loading_layer = text_layer_create(GRect(10, bounds.size.h / 2 - 15, bounds.size.w - 20, 60));
  text_layer_set_text(s_loading_layer, "Loading stops...");
  text_layer_set_text_alignment(s_loading_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_loading_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_loading_layer));

  prv_update_loading_visibility();
}

static void prv_window_unload(Window *window) {
  status_bar_layer_destroy(s_status_bar);
  menu_layer_destroy(s_menu_layer);
  s_menu_layer = NULL;
  text_layer_destroy(s_loading_layer);
}

static void prv_window_appear(Window *window) {
  // Favorites may already be on screen (restored from watch storage). JS
  // sends the full list, including GPS-based nearby stations, on its own at
  // startup, so only ask again when coming back to an empty list (e.g. the
  // first fetch failed and nothing was cached)
  if (s_appeared_before && stations_get_count() == 0) {
    comm_request_stations();
  }
  s_appeared_before = true;
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
  s_error[0] = '\0';
  if (s_menu_layer) {
#ifdef PBL_ROUND
    prv_round_sync_selection();
#else
    menu_layer_reload_data(s_menu_layer);
#endif
    prv_update_loading_visibility();
  }
}

void station_window_show_error(const char *message) {
  s_received_data = true;
  text_copy_utf8(s_error, message, sizeof(s_error));
  if (s_menu_layer) {
    prv_update_loading_visibility();
  }
}

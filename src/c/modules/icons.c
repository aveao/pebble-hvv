#include "icons.h"
#include <string.h>

// Per-line colors for S-Bahn (mapped to Pebble 64-color palette)
// S1: #00962c → GColorIslamicGreen, S2: #B41439 → GColorDarkCandyAppleRed
// S3: #54216e → GColorImperialPurple, S5: #008ABD → GColorVividCerulean
// S7: #DC871E → GColorChromeYellow
static GColor prv_sbahn_color(const char *line) {
#ifdef PBL_COLOR
  if (strcmp(line, "S1") == 0) return GColorIslamicGreen;
  if (strcmp(line, "S2") == 0) return GColorDarkCandyAppleRed;
  if (strcmp(line, "S3") == 0) return GColorImperialPurple;
  if (strcmp(line, "S5") == 0) return GColorVividCerulean;
  if (strcmp(line, "S7") == 0) return GColorChromeYellow;
  return GColorIslamicGreen; // default S-Bahn green
#else
  return GColorBlack;
#endif
}

// Per-line colors for U-Bahn
// U1: #005aa4 → GColorCobaltBlue, U2: #ed0020 → GColorRed
// U3: #ffd600 → GColorYellow, U4: #008b8f → GColorTiffanyBlue
// U5: #A86A1B → GColorWindsorTan
static GColor prv_ubahn_color(const char *line) {
#ifdef PBL_COLOR
  if (strcmp(line, "U1") == 0) return GColorCobaltBlue;
  if (strcmp(line, "U2") == 0) return GColorRed;
  if (strcmp(line, "U3") == 0) return GColorYellow;
  if (strcmp(line, "U4") == 0) return GColorTiffanyBlue;
  if (strcmp(line, "U5") == 0) return GColorWindsorTan;
  return GColorBlue; // default U-Bahn blue
#else
  return GColorBlack;
#endif
}

// U3 has yellow background so needs black text
static GColor prv_ubahn_text_color(const char *line) {
#ifdef PBL_COLOR
  if (strcmp(line, "U3") == 0) return GColorBlack;
#endif
  return GColorWhite;
}

static void prv_draw_label(GContext *ctx, const char *line, GRect rect, GColor text_color) {
  graphics_context_set_text_color(ctx, text_color);

  // GOTHIC_14_BOLD has ~2px top padding; nudge up to visually center
  int text_y = rect.origin.y + (rect.size.h - 14) / 2 - 2;
  GRect text_rect = GRect(rect.origin.x, text_y, rect.size.w, 16);

  graphics_draw_text(ctx, line,
    fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    text_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void prv_draw_bus(GContext *ctx, const char *line, GRect rect) {
  // Horizontal hexagon: flat top/bottom, pointed left/right
  GColor fill = PBL_IF_COLOR_ELSE(GColorRed, GColorBlack);
  graphics_context_set_fill_color(ctx, fill);

  int p = rect.size.h / 3;
  GPathInfo info = {
    .num_points = 6,
    .points = (GPoint[]) {
      {p, 0},
      {rect.size.w - p, 0},
      {rect.size.w, rect.size.h / 2},
      {rect.size.w - p, rect.size.h},
      {p, rect.size.h},
      {0, rect.size.h / 2}
    }
  };
  GPath *path = gpath_create(&info);
  gpath_move_to(path, rect.origin);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);

  prv_draw_label(ctx, line, rect, GColorWhite);
}

static void prv_draw_sbahn(GContext *ctx, const char *line, GRect rect) {
  GColor fill = prv_sbahn_color(line);
  graphics_context_set_fill_color(ctx, fill);

  GPoint center = grect_center_point(&rect);
  int16_t radius = rect.size.h / 2;
  graphics_fill_circle(ctx, center, radius);

  prv_draw_label(ctx, line, rect, GColorWhite);
}

static void prv_draw_ubahn(GContext *ctx, const char *line, GRect rect) {
  GColor fill = prv_ubahn_color(line);
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_rect(ctx, rect, 2, GCornersAll);

  prv_draw_label(ctx, line, rect, prv_ubahn_text_color(line));
}

static void prv_draw_ferry(GContext *ctx, const char *line, GRect rect) {
  GColor fill = PBL_IF_COLOR_ELSE(GColorTiffanyBlue, GColorBlack);
  graphics_context_set_fill_color(ctx, fill);

  // Trapezoid: wider top, narrower bottom (boat hull shape)
  int inset = 3;
  GPathInfo info = {
    .num_points = 4,
    .points = (GPoint[]) {
      {0, 0},
      {rect.size.w, 0},
      {rect.size.w - inset, rect.size.h},
      {inset, rect.size.h}
    }
  };
  GPath *path = gpath_create(&info);
  gpath_move_to(path, rect.origin);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);

  prv_draw_label(ctx, line, rect, GColorWhite);
}

static void prv_draw_unknown(GContext *ctx, const char *line, GRect rect) {
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, rect, 2, GCornersAll);

  prv_draw_label(ctx, line, rect, GColorWhite);
}

void icons_draw_badge(GContext *ctx, TransitType type, const char *line, GRect rect) {
  switch (type) {
    case TRANSIT_BUS:    prv_draw_bus(ctx, line, rect);     break;
    case TRANSIT_SBAHN:  prv_draw_sbahn(ctx, line, rect);   break;
    case TRANSIT_UBAHN:  prv_draw_ubahn(ctx, line, rect);   break;
    case TRANSIT_FERRY:  prv_draw_ferry(ctx, line, rect);   break;
    default:             prv_draw_unknown(ctx, line, rect);  break;
  }
}

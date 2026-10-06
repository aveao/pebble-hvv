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

// Emery and gabbro (round, 260x260) share sizes; round layouts inset rows
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  #define BADGE_FONT FONT_KEY_GOTHIC_18_BOLD
  #define BADGE_FONT_H 18
  #define BADGE_LABEL_Y_NUDGE 3
  // Labels starting with a letter (S3, U3, X3) render ~1px left of center
  // at this size; number-only labels (5, 112) are already centered
  #define BADGE_LETTER_X_NUDGE 1
  #define ARROW_W 9
  #define ARROW_H 5
  // Arrow sits directly on the badge; there is less room above it here
  #define ARROW_GAP 0
#else
  #define BADGE_FONT FONT_KEY_GOTHIC_14_BOLD
  #define BADGE_FONT_H 14
  #define BADGE_LABEL_Y_NUDGE 2
  #define BADGE_LETTER_X_NUDGE 0
  #define ARROW_W 7
  #define ARROW_H 5
  // Gap between the arrow and the top of the badge
  #define ARROW_GAP 1
#endif

static void prv_draw_label(GContext *ctx, const char *line, GRect rect, GColor text_color) {
  graphics_context_set_text_color(ctx, text_color);

  int text_x = rect.origin.x;
  if (line[0] >= 'A' && line[0] <= 'Z') text_x += BADGE_LETTER_X_NUDGE;
  int text_y = rect.origin.y + (rect.size.h - BADGE_FONT_H) / 2 - BADGE_LABEL_Y_NUDGE;
  GRect text_rect = GRect(text_x, text_y, rect.size.w, BADGE_FONT_H + 4);

  graphics_draw_text(ctx, line,
    fonts_get_system_font(BADGE_FONT),
    text_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void prv_draw_bus(GContext *ctx, const char *line, GRect rect) {
  // Horizontal hexagon: flat top/bottom, pointed left/right
  GColor fill = PBL_IF_COLOR_ELSE(GColorRed, GColorBlack);
  graphics_context_set_fill_color(ctx, fill);

  // Filled paths include their edge pixels, so the far edges are at
  // size - 1 to keep the shape inside rect
  int p = rect.size.h / 3;
  int r = rect.size.w - 1;
  int b = rect.size.h - 1;
  GPathInfo info = {
    .num_points = 6,
    .points = (GPoint[]) {
      {p, 0},
      {r - p, 0},
      {r, b / 2},
      {r - p, b},
      {p, b},
      {0, b / 2}
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
  // Far edges at size - 1, as in prv_draw_bus
  int inset = 3;
  int r = rect.size.w - 1;
  int b = rect.size.h - 1;
  GPathInfo info = {
    .num_points = 4,
    .points = (GPoint[]) {
      {0, 0},
      {r, 0},
      {r - inset, b},
      {inset, b}
    }
  };
  GPath *path = gpath_create(&info);
  gpath_move_to(path, rect.origin);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);

  prv_draw_label(ctx, line, rect, GColorWhite);
}

static void prv_draw_unknown(GContext *ctx, const char *line, GRect rect) {
  // GColorDarkGray dithers to a checkerboard on aplite/diorite, which makes
  // the white label unreadable. Fall back to solid black on B&W platforms.
  GColor fill = PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack);
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_rect(ctx, rect, 2, GCornersAll);

  prv_draw_label(ctx, line, rect, GColorWhite);
}

GColor icons_line_color(TransitType type, const char *line) {
  switch (type) {
    case TRANSIT_BUS:    return PBL_IF_COLOR_ELSE(GColorRed, GColorBlack);
    case TRANSIT_SBAHN:  return prv_sbahn_color(line);
    case TRANSIT_UBAHN:  return prv_ubahn_color(line);
    case TRANSIT_FERRY:  return PBL_IF_COLOR_ELSE(GColorTiffanyBlue, GColorBlack);
    default:             return PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack);
  }
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

int16_t icons_direction_arrow_height(void) {
  return ARROW_GAP + ARROW_H;
}

void icons_draw_direction_arrow(GContext *ctx, DirectionId direction_id, GRect badge_rect) {
  // comm.c maps anything but forward/backward to unknown
  if (direction_id == DIRECTION_ID_UNKNOWN) return;
  bool right = direction_id == DIRECTION_ID_FORWARD;

  int x = right ? badge_rect.origin.x + badge_rect.size.w - ARROW_W : badge_rect.origin.x;
  int y = badge_rect.origin.y - ARROW_GAP - ARROW_H;
  int mid = ARROW_H / 2;
  int head_w = mid + 1;

  graphics_context_set_fill_color(ctx, GColorBlack);
  // Shaft, then the head column by column (a filled GPath this small rounds
  // unevenly), widening from a 1px tip
  int shaft_x = right ? x : x + head_w;
  graphics_fill_rect(ctx, GRect(shaft_x, y + mid, ARROW_W - head_w, 1), 0, GCornerNone);
  for (int c = 0; c < head_w; c++) {
    int col_x = right ? x + ARROW_W - 1 - c : x + c;
    graphics_fill_rect(ctx, GRect(col_x, y + mid - c, 1, 2 * c + 1), 0, GCornerNone);
  }
}

#include "round.h"

#ifdef PBL_ROUND
// Keep content a little away from the curved edge
#define ROUND_INSET_MARGIN 4

static int32_t prv_isqrt(int32_t n) {
  if (n <= 0) return 0;
  int32_t x = n;
  int32_t y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + n / x) / 2;
  }
  return x;
}

// Distance of pixel row y's centre from the screen's centre, in half pixels
static int32_t prv_half_px_from_center(int16_t y, int16_t screen_h) {
  int32_t d = 2 * (int32_t)y + 1 - screen_h;
  return d < 0 ? -d : d;
}
#endif

int16_t round_inset(int16_t top, int16_t bottom, GSize screen) {
#ifdef PBL_ROUND
  int16_t radius = screen.w / 2;
  int32_t dy_top = prv_half_px_from_center(top, screen.h);
  int32_t dy_bottom = prv_half_px_from_center(bottom - 1, screen.h);
  int32_t dy = dy_top > dy_bottom ? dy_top : dy_bottom;
  int32_t r = screen.w;  // radius in half pixels
  if (dy >= r) return radius;
  int16_t half_chord = prv_isqrt(r * r - dy * dy) / 2;
  int16_t inset = radius - half_chord + ROUND_INSET_MARGIN;
  return inset > radius ? radius : inset;
#else
  (void)top;
  (void)bottom;
  (void)screen;
  return 0;
#endif
}

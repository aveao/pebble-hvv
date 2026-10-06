#include "settings.h"

// Persist keys: 1 was the old saved station name, favorites use 100+
#define PERSIST_KEY_BOLD_TEXT 2
#define PERSIST_KEY_TOUCH_NAV 3
#define PERSIST_KEY_DIRECTION_ARROWS 4

static bool s_bold_text;
static bool s_touch_nav;
static bool s_direction_arrows;

static void prv_apply_touch_nav(void) {
#ifdef PBL_TOUCH
  app_touch_navigation_enable(s_touch_nav);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Touch navigation %s", s_touch_nav ? "on" : "off");
#endif
}

void settings_init(void) {
  s_bold_text = persist_exists(PERSIST_KEY_BOLD_TEXT) && persist_read_bool(PERSIST_KEY_BOLD_TEXT);
  // Default on: unset means the user never turned it off
  s_touch_nav = !persist_exists(PERSIST_KEY_TOUCH_NAV) || persist_read_bool(PERSIST_KEY_TOUCH_NAV);
  prv_apply_touch_nav();
  s_direction_arrows = !persist_exists(PERSIST_KEY_DIRECTION_ARROWS) ||
                       persist_read_bool(PERSIST_KEY_DIRECTION_ARROWS);
}

bool settings_get_bold_text(void) {
  return s_bold_text;
}

void settings_set_bold_text(bool bold) {
  if (bold == s_bold_text) return;
  s_bold_text = bold;
  persist_write_bool(PERSIST_KEY_BOLD_TEXT, bold);
}

bool settings_get_direction_arrows(void) {
  return s_direction_arrows;
}

void settings_set_direction_arrows(bool enabled) {
  if (enabled == s_direction_arrows) return;
  s_direction_arrows = enabled;
  persist_write_bool(PERSIST_KEY_DIRECTION_ARROWS, enabled);
}

bool settings_get_touch_nav(void) {
  return s_touch_nav;
}

void settings_set_touch_nav(bool enabled) {
  if (enabled == s_touch_nav) return;
  s_touch_nav = enabled;
  persist_write_bool(PERSIST_KEY_TOUCH_NAV, enabled);
  prv_apply_touch_nav();
}

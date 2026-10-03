#include "settings.h"

// Persist keys: 1 was the old saved station name, favorites use 100+
#define PERSIST_KEY_BOLD_TEXT 2

static bool s_bold_text;

void settings_init(void) {
  s_bold_text = persist_exists(PERSIST_KEY_BOLD_TEXT) && persist_read_bool(PERSIST_KEY_BOLD_TEXT);
}

bool settings_get_bold_text(void) {
  return s_bold_text;
}

void settings_set_bold_text(bool bold) {
  if (bold == s_bold_text) return;
  s_bold_text = bold;
  persist_write_bool(PERSIST_KEY_BOLD_TEXT, bold);
}

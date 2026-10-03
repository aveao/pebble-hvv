#pragma once

#include <pebble.h>

// Copy a NUL-terminated UTF-8 string into dst, truncating to fit dst_size
// without splitting a multi-byte character.
void text_copy_utf8(char *dst, const char *src, size_t dst_size);

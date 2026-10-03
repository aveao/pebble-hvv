#include "text.h"

void text_copy_utf8(char *dst, const char *src, size_t dst_size) {
  if (dst_size == 0) return;
  size_t len = strlen(src);
  if (len >= dst_size) {
    len = dst_size - 1;
    // If the cut lands inside a character, back up to its lead byte
    while (len > 0 && ((uint8_t)src[len] & 0xC0) == 0x80) len--;
  }
  memcpy(dst, src, len);
  dst[len] = '\0';
}

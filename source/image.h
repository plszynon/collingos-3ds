#pragma once
#include <stddef.h>
#include <stdint.h>

/* Decodes PNG/JPEG/BMP to RGBA8. Returns NULL (and sets *err) on failure. max_pixels guards memory. */
uint8_t *img_decode(const uint8_t *data, size_t len, int *w, int *h, long max_pixels, const char **err);
void img_free(uint8_t *p);

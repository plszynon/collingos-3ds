#include <stdlib.h>
#include <string.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#include "stb_image.h"
#include "image.h"

uint8_t *img_decode(const uint8_t *data, size_t len, int *w, int *h, long max_pixels, const char **err)
{
	int x = 0, y = 0, comp = 0;
	if (!stbi_info_from_memory(data, (int)len, &x, &y, &comp)) { *err = "Unsupported or damaged image."; return NULL; }
	if (x <= 0 || y <= 0 || (long)x * (long)y > max_pixels) { *err = "Image is too large for the 3DS memory."; return NULL; }
	uint8_t *p = stbi_load_from_memory(data, (int)len, &x, &y, &comp, 4);
	if (!p) { *err = "Could not decode the image."; return NULL; }
	*w = x; *h = y;
	return p;
}

void img_free(uint8_t *p) { stbi_image_free(p); }

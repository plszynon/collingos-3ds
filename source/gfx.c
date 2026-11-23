#include <string.h>
#include "gfx.h"
#include "font.h"

Surf TOP, BOT;
static int g_fade = 0;

void gfx_frame_begin(void)
{
	TOP.fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
	TOP.w = TOP_W;
	BOT.fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
	BOT.w = BOT_W;
}

void gfx_fade_in(void) { g_fade = 6; }

void gfx_frame_end(void)
{
	if (g_fade > 0) {
		int a = g_fade * 40;
		gx_blend(&TOP, 0, 0, TOP_W, SCR_H, 0, a);
		gx_blend(&BOT, 0, 0, BOT_W, SCR_H, 0, a);
		g_fade--;
	}
	gfxFlushBuffers();
	gfxSwapBuffers();
	gspWaitForVBlank();
}

static inline void put(u8 *p, u32 c)
{
	p[0] = (u8)c;
	p[1] = (u8)(c >> 8);
	p[2] = (u8)(c >> 16);
}

void gx_fill(Surf *s, int x, int y, int w, int h, u32 c)
{
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > s->w) w = s->w - x;
	if (y + h > SCR_H) h = SCR_H - y;
	if (w <= 0 || h <= 0) return;
	for (int i = 0; i < w; i++) {
		u8 *p = s->fb + ((x + i) * SCR_H + (SCR_H - 1 - y)) * 3;
		for (int j = 0; j < h; j++, p -= 3) put(p, c);
	}
}

void gx_rect(Surf *s, int x, int y, int w, int h, u32 c)
{
	gx_fill(s, x, y, w, 1, c);
	gx_fill(s, x, y + h - 1, w, 1, c);
	gx_fill(s, x, y, 1, h, c);
	gx_fill(s, x + w - 1, y, 1, h, c);
}

void gx_vgrad(Surf *s, int x, int y, int w, int h, u32 c1, u32 c2)
{
	if (h <= 0) return;
	int r1 = (c1 >> 16) & 255, g1 = (c1 >> 8) & 255, b1 = c1 & 255;
	int r2 = (c2 >> 16) & 255, g2 = (c2 >> 8) & 255, b2 = c2 & 255;
	for (int j = 0; j < h; j++) {
		int r = r1 + (r2 - r1) * j / h, g = g1 + (g2 - g1) * j / h, b = b1 + (b2 - b1) * j / h;
		gx_fill(s, x, y + j, w, 1, RGB(r, g, b));
	}
}

void gx_blend(Surf *s, int x, int y, int w, int h, u32 c, int alpha)
{
	if (alpha <= 0) return;
	if (alpha > 255) alpha = 255;
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > s->w) w = s->w - x;
	if (y + h > SCR_H) h = SCR_H - y;
	if (w <= 0 || h <= 0) return;
	int cr = (c >> 16) & 255, cg = (c >> 8) & 255, cb = c & 255, ia = 255 - alpha;
	for (int i = 0; i < w; i++) {
		u8 *p = s->fb + ((x + i) * SCR_H + (SCR_H - 1 - y)) * 3;
		for (int j = 0; j < h; j++, p -= 3) {
			p[0] = (u8)((p[0] * ia + cb * alpha) / 255);
			p[1] = (u8)((p[1] * ia + cg * alpha) / 255);
			p[2] = (u8)((p[2] * ia + cr * alpha) / 255);
		}
	}
}

static void glyph(Surf *s, int x, int y, int ch, u32 c, int sc)
{
	if (ch < 32 || ch > 126) ch = '?';
	const unsigned char *g = FONT[ch - 32];
	for (int row = 0; row < FONT_H; row++) {
		unsigned v = g[row];
		if (!v) continue;
		for (int col = 0; col < FONT_W; col++) {
			if (v & (1u << (FONT_W - 1 - col))) {
				if (sc == 1) {
					int px = x + col, py = y + row;
					if ((unsigned)px < (unsigned)s->w && (unsigned)py < SCR_H)
						put(s->fb + (px * SCR_H + (SCR_H - 1 - py)) * 3, c);
				} else {
					gx_fill(s, x + col * sc, y + row * sc, sc, sc, c);
				}
			}
		}
	}
}

int gx_text_w(const char *t) { return (int)strlen(t) * FONT_W; }

void gx_text(Surf *s, int x, int y, const char *t, u32 c)
{
	for (; *t; t++, x += FONT_W) {
		if (x >= s->w) break;
		if (x > -FONT_W) glyph(s, x, y, (unsigned char)*t, c, 1);
	}
}

void gx_text2(Surf *s, int x, int y, const char *t, u32 c)
{
	for (; *t; t++, x += FONT_W * 2) {
		if (x >= s->w) break;
		glyph(s, x, y, (unsigned char)*t, c, 2);
	}
}

void gx_text_n(Surf *s, int x, int y, const char *t, int maxw, u32 c)
{
	int n = (int)strlen(t), fit = maxw / FONT_W;
	if (fit <= 0) return;
	if (n <= fit) { gx_text(s, x, y, t, c); return; }
	char buf[300];
	if (fit > 296) fit = 296;
	if (fit < 3) { memcpy(buf, t, fit); buf[fit] = 0; }
	else { memcpy(buf, t, fit - 2); buf[fit - 2] = '.'; buf[fit - 1] = '.'; buf[fit] = 0; }
	gx_text(s, x, y, buf, c);
}

int gx_wrap(Surf *s, int x, int y, int maxw, int maxlines, const char *t, u32 c)
{
	int cols = maxw / FONT_W, lines = 0;
	if (cols < 4 || cols > 200) return 0;
	char line[210];
	while (*t && lines < maxlines) {
		while (*t == ' ') t++;
		int len = (int)strlen(t);
		if (len > cols) {
			len = cols;
			int br = len;
			while (br > 0 && t[br] != ' ') br--;
			if (br > 0) len = br;
		}
		memcpy(line, t, len);
		line[len] = 0;
		gx_text(s, x, y + lines * LINE_H, line, c);
		t += len;
		lines++;
	}
	return lines;
}

void gx_rgba(Surf *s, int x, int y, int dw, int dh, const u8 *rgba, int sw, int sh)
{
	if (!rgba || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
	for (int j = 0; j < dh; j++) {
		int py = y + j;
		if ((unsigned)py >= SCR_H) continue;
		const u8 *row = rgba + (size_t)(j * sh / dh) * sw * 4;
		for (int i = 0; i < dw; i++) {
			int px = x + i;
			if ((unsigned)px >= (unsigned)s->w) continue;
			const u8 *q = row + (i * sw / dw) * 4;
			u8 *p = s->fb + (px * SCR_H + (SCR_H - 1 - py)) * 3;
			int a = q[3];
			if (a == 255) { p[0] = q[2]; p[1] = q[1]; p[2] = q[0]; }
			else if (a) {
				p[0] = (u8)((p[0] * (255 - a) + q[2] * a) / 255);
				p[1] = (u8)((p[1] * (255 - a) + q[1] * a) / 255);
				p[2] = (u8)((p[2] * (255 - a) + q[0] * a) / 255);
			}
		}
	}
}

void gx_rgba_crop(Surf *s, int x, int y, int dw, int dh, const u8 *rgba, int stride_w, int sx, int sy, int sw, int sh)
{
	if (!rgba || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
	for (int j = 0; j < dh; j++) {
		int py = y + j;
		if ((unsigned)py >= SCR_H) continue;
		const u8 *row = rgba + ((size_t)(sy + j * sh / dh) * stride_w + sx) * 4;
		for (int i = 0; i < dw; i++) {
			int px = x + i;
			if ((unsigned)px >= (unsigned)s->w) continue;
			const u8 *q = row + (i * sw / dw) * 4;
			u8 *p = s->fb + (px * SCR_H + (SCR_H - 1 - py)) * 3;
			int a = q[3];
			if (a == 255) { p[0] = q[2]; p[1] = q[1]; p[2] = q[0]; }
			else if (a) {
				p[0] = (u8)((p[0] * (255 - a) + q[2] * a) / 255);
				p[1] = (u8)((p[1] * (255 - a) + q[1] * a) / 255);
				p[2] = (u8)((p[2] * (255 - a) + q[0] * a) / 255);
			}
		}
	}
}

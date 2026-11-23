#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include "apps.h"
#include "image.h"

#define IMG_EXTS "png;jpg;jpeg;bmp"
#define MAX_PIXELS 2000000L
#define MAX_FILE_BYTES (8 * 1024 * 1024)
#define MAX_SIBLINGS 300

typedef struct {
	char dir[512];
	char (*names)[96];
	int n, idx;
	uint8_t *img; int w, h;
	int zoom;            /* 0 fit, 1 = 100% */
	int ox, oy;
	char status[120];
} Viewer;

static int cmp_name(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }

static void scan_dir(Viewer *v)
{
	v->n = 0;
	DIR *d = opendir(v->dir);
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d)) && v->n < MAX_SIBLINGS) {
		if (e->d_type == DT_DIR || strlen(e->d_name) >= 96) continue;
		if (has_ext(e->d_name, IMG_EXTS)) snprintf(v->names[v->n++], 96, "%s", e->d_name);
	}
	closedir(d);
	qsort(v->names, v->n, 96, cmp_name);
}

static void load_current(Viewer *v)
{
	img_free(v->img);
	v->img = NULL;
	v->ox = v->oy = 0;
	if (v->n == 0) { snprintf(v->status, sizeof v->status, "No images in this folder"); return; }
	char p[620];
	snprintf(p, sizeof p, "%s/%s", v->dir, v->names[v->idx]);
	FILE *f = fopen(p, "rb");
	if (!f) { snprintf(v->status, sizeof v->status, "Cannot open file"); return; }
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz <= 0 || sz > MAX_FILE_BYTES) { fclose(f); snprintf(v->status, sizeof v->status, "File too large (max 8 MB)"); return; }
	uint8_t *buf = malloc(sz);
	if (!buf) { fclose(f); snprintf(v->status, sizeof v->status, "Not enough memory"); return; }
	size_t got = fread(buf, 1, sz, f);
	fclose(f);
	const char *err = "";
	v->img = img_decode(buf, got, &v->w, &v->h, MAX_PIXELS, &err);
	free(buf);
	if (!v->img) snprintf(v->status, sizeof v->status, "%s", err);
	else snprintf(v->status, sizeof v->status, "%s  (%dx%d)", v->names[v->idx], v->w, v->h);
}

static void open_path(Viewer *v, const char *path)
{
	snprintf(v->dir, sizeof v->dir, "%s", path);
	char *s = strrchr(v->dir, '/');
	const char *name = s ? s + 1 : path;
	if (s && s > v->dir + 5) *s = 0; else if (s) s[1] = 0;
	scan_dir(v);
	v->idx = 0;
	for (int i = 0; i < v->n; i++) if (!strcasecmp(v->names[i], name)) v->idx = i;
	load_current(v);
}

static void run_viewer(Viewer *v)
{
	Btn bOpen = { 0, 208, 80, 32, "Open" }, bPrev = { 80, 208, 80, 32, "< Prev" }, bNext = { 160, 208, 80, 32, "Next >" }, bZoom = { 240, 208, 80, 32, "Fit/100%" };
	gfx_fade_in();
	while (ui_running()) {
		ui_poll();
		if (ui_close_tapped() || (IN.kd & KEY_B)) break;
		if (btn_tap(&bOpen)) {
			char p[512];
			if (file_pick(v->dir[0] ? v->dir : "sdmc:/", IMG_EXTS, p, sizeof p)) open_path(v, p);
			gfx_fade_in();
			continue;
		}
		if ((btn_tap(&bNext) || (IN.kd & KEY_R)) && v->n) { v->idx = (v->idx + 1) % v->n; load_current(v); }
		if ((btn_tap(&bPrev) || (IN.kd & KEY_L)) && v->n) { v->idx = (v->idx + v->n - 1) % v->n; load_current(v); }
		if (btn_tap(&bZoom) || (IN.kd & KEY_A)) { v->zoom = !v->zoom; v->ox = v->oy = 0; }
		if (v->zoom && v->img) {
			int step = 6;
			if (IN.kh & KEY_LEFT) v->ox -= step;
			if (IN.kh & KEY_RIGHT) v->ox += step;
			if (IN.kh & KEY_UP) v->oy -= step;
			if (IN.kh & KEY_DOWN) v->oy += step;
			int maxx = v->w > TOP_W ? v->w - TOP_W : 0, maxy = v->h > SCR_H ? v->h - SCR_H : 0;
			if (v->ox < 0) v->ox = 0;
			if (v->oy < 0) v->oy = 0;
			if (v->ox > maxx) v->ox = maxx;
			if (v->oy > maxy) v->oy = maxy;
		}
		gfx_frame_begin();
		gx_fill(&TOP, 0, 0, TOP_W, SCR_H, RGB(0,0,0));
		if (v->img) {
			if (!v->zoom) {
				int dw = TOP_W, dh = v->h * TOP_W / v->w;
				if (dh > SCR_H) { dh = SCR_H; dw = v->w * SCR_H / v->h; }
				if (dw < 1) dw = 1;
				if (dh < 1) dh = 1;
				gx_rgba(&TOP, (TOP_W - dw) / 2, (SCR_H - dh) / 2, dw, dh, v->img, v->w, v->h);
			} else {
				int cw = v->w < TOP_W ? v->w : TOP_W, ch = v->h < SCR_H ? v->h : SCR_H;
				gx_rgba_crop(&TOP, (TOP_W - cw) / 2, (SCR_H - ch) / 2, cw, ch, v->img, v->w, v->ox, v->oy, cw, ch);
			}
		} else {
			gx_text(&TOP, 12, 110, v->status[0] ? v->status : "Tap Open to choose an image (PNG, JPEG, BMP).", C_DIM);
		}
		gx_fill(&BOT, 0, 0, BOT_W, SCR_H, C_BG);
		gx_text_n(&BOT, 8, 30, v->status, BOT_W - 16, C_TEXT);
		char info[80];
		snprintf(info, sizeof info, "%d / %d   %s", v->n ? v->idx + 1 : 0, v->n, v->zoom ? "100% (D-pad pans)" : "Fit to screen");
		gx_text(&BOT, 8, 48, info, C_DIM);
		gx_text(&BOT, 8, 80, "L / R: previous / next image", C_DIM);
		gx_text(&BOT, 8, 94, "A: toggle fit / 100%", C_DIM);
		gx_text(&BOT, 8, 120, "Limit: about 2 megapixels, 8 MB file.", RGB(100,106,120));
		ui_titlebar("Image Viewer");
		gx_fill(&BOT, 0, 206, BOT_W, 34, C_BAR);
		btn_draw(&BOT, &bOpen, 1); btn_draw(&BOT, &bPrev, v->n > 1); btn_draw(&BOT, &bNext, v->n > 1); btn_draw(&BOT, &bZoom, v->img != NULL);
		gfx_frame_end();
	}
}

static void viewer_main(const char *path)
{
	Viewer *v = calloc(1, sizeof(Viewer));
	if (!v) { ui_error("Not enough memory."); return; }
	v->names = malloc(96 * MAX_SIBLINGS);
	if (!v->names) { free(v); ui_error("Not enough memory."); return; }
	if (path) open_path(v, path);
	run_viewer(v);
	img_free(v->img);
	free(v->names);
	free(v);
}

void viewer_open(const char *path) { viewer_main(path); }
void app_images(void) { viewer_main(NULL); }

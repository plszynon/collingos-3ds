#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"

Input IN;
int g_quit = 0;
const u32 ACCENTS[4] = { RGB(0,120,215), RGB(0,170,150), RGB(232,100,50), RGB(150,90,225) };
u32 th_accent(void) { return ACCENTS[S.accent & 3]; }

void ui_poll(void)
{
	hidScanInput();
	IN.kd = hidKeysDown();
	IN.kh = hidKeysHeld();
	IN.ku = hidKeysUp();
	IN.down = (IN.kd & KEY_TOUCH) != 0;
	IN.up = (IN.ku & KEY_TOUCH) != 0;
	IN.tapped = 0;
	IN.dy = 0;
	if (IN.kh & KEY_TOUCH) {
		touchPosition tp;
		hidTouchRead(&tp);
		if (IN.down) { IN.sx = tp.px; IN.sy = tp.py; IN.ty = tp.py; IN.dragged = 0; }
		IN.dy = tp.py - IN.ty;
		IN.tx = tp.px;
		IN.ty = tp.py;
		IN.touching = 1;
		if (abs(IN.tx - IN.sx) > 8 || abs(IN.ty - IN.sy) > 8) IN.dragged = 1;
	} else {
		IN.touching = 0;
	}
	if (IN.up && !IN.dragged) IN.tapped = 1;
}

int ui_running(void)
{
	if (!aptMainLoop()) g_quit = 1;
	return !g_quit;
}

/* ------------------------------------------------------------ widgets */

int btn_in(const Btn *b, int x, int y) { return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h; }
int btn_tap(const Btn *b) { return IN.tapped && btn_in(b, IN.tx, IN.ty); }

void btn_draw(Surf *s, const Btn *b, int enabled)
{
	int pressed = enabled && IN.touching && !IN.dragged && btn_in(b, IN.tx, IN.ty);
	u32 bg = pressed ? th_accent() : C_PAN2;
	gx_fill(s, b->x, b->y, b->w, b->h, bg);
	gx_fill(s, b->x, b->y + b->h - 1, b->w, 1, C_BAR);
	int tw = gx_text_w(b->label);
	if (tw > b->w - 4) { gx_text_n(s, b->x + 2, b->y + (b->h - 11) / 2, b->label, b->w - 4, enabled ? C_TEXT : C_DIM); return; }
	gx_text(s, b->x + (b->w - tw) / 2, b->y + (b->h - 11) / 2, b->label, enabled ? C_TEXT : RGB(90,95,108));
}

void ui_titlebar(const char *title)
{
	gx_fill(&BOT, 0, 0, BOT_W, 20, C_BAR);
	gx_fill(&BOT, 0, 19, BOT_W, 1, th_accent());
	ui_logo(&BOT, 4, 4, 12);
	gx_text(&BOT, 22, 5, title, C_TEXT);
	gx_fill(&BOT, BOT_W - 26, 0, 26, 19, RGB(180,50,50));
	gx_text(&BOT, BOT_W - 18, 4, "X", C_TEXT);
}

int ui_close_tapped(void) { return IN.tapped && IN.tx >= BOT_W - 26 && IN.ty < 20; }

/* ------------------------------------------------------------ list */

void list_init(List *l, int x, int y, int w, int h, int row_h)
{
	memset(l, 0, sizeof *l);
	l->x = x; l->y = y; l->w = w; l->h = h; l->row_h = row_h; l->act = -1;
}

static int list_max_scroll(const List *l)
{
	int m = l->n * l->row_h - l->h;
	return m < 0 ? 0 : m;
}

void list_set_n(List *l, int n)
{
	l->n = n;
	if (l->sel >= n) l->sel = n - 1;
	if (l->sel < 0) l->sel = 0;
	if (l->scroll > list_max_scroll(l)) l->scroll = list_max_scroll(l);
}

void list_show_sel(List *l)
{
	int top = l->sel * l->row_h, bot = top + l->row_h;
	if (top < l->scroll) l->scroll = top;
	if (bot > l->scroll + l->h) l->scroll = bot - l->h;
	if (l->scroll > list_max_scroll(l)) l->scroll = list_max_scroll(l);
	if (l->scroll < 0) l->scroll = 0;
}

int list_first(const List *l) { return l->scroll / l->row_h; }
int list_row_y(const List *l, int i) { return l->y + i * l->row_h - l->scroll; }

void list_update(List *l)
{
	l->act = -1;
	if (l->n <= 0) return;
	int in_start = IN.sx >= l->x && IN.sx < l->x + l->w && IN.sy >= l->y && IN.sy < l->y + l->h;
	if (IN.touching && IN.dragged && in_start) {
		l->scroll -= IN.dy;
		if (l->scroll < 0) l->scroll = 0;
		if (l->scroll > list_max_scroll(l)) l->scroll = list_max_scroll(l);
	}
	if (IN.tapped && IN.tx >= l->x && IN.tx < l->x + l->w && IN.ty >= l->y && IN.ty < l->y + l->h) {
		int i = (IN.ty - l->y + l->scroll) / l->row_h;
		if (i >= 0 && i < l->n) {
			if (i == l->sel) l->act = i;
			l->sel = i;
		}
	}
	if (IN.kd & KEY_DOWN) { if (l->sel < l->n - 1) l->sel++; list_show_sel(l); }
	if (IN.kd & KEY_UP)   { if (l->sel > 0) l->sel--; list_show_sel(l); }
	if (IN.kd & KEY_R)    { l->sel += l->h / l->row_h; if (l->sel > l->n - 1) l->sel = l->n - 1; list_show_sel(l); }
	if (IN.kd & KEY_L)    { l->sel -= l->h / l->row_h; if (l->sel < 0) l->sel = 0; list_show_sel(l); }
	if (IN.kd & KEY_A) l->act = l->sel;
}

/* ------------------------------------------------------------ dialogs */

static void dlg_bg(const char *title, u32 tcol, int py, int ph)
{
	gfx_frame_begin();
	gx_vgrad(&TOP, 0, 0, TOP_W, SCR_H, C_BG, RGB(10,11,14));
	ui_logo(&TOP, 156, 70, 88);
	gx_text2(&TOP, 106, 170, "CollingOS", C_DIM);
	gx_fill(&BOT, 0, 0, BOT_W, SCR_H, C_BG);
	gx_fill(&BOT, 12, py, BOT_W - 24, ph, C_PANEL);
	gx_fill(&BOT, 12, py, BOT_W - 24, 20, tcol);
	gx_text(&BOT, 20, py + 5, title, C_TEXT);
}

static void dlg_text(int py, const char *text)
{
	gx_wrap(&BOT, 22, py + 28, BOT_W - 44, 6, text, C_TEXT);
}

void ui_msg(const char *title, const char *text)
{
	Btn ok = { 110, 176, 100, 30, "OK" };
	while (ui_running()) {
		ui_poll();
		if (btn_tap(&ok) || (IN.kd & (KEY_A | KEY_B))) break;
		dlg_bg(title, th_accent(), 30, 186);
		dlg_text(30, text);
		btn_draw(&BOT, &ok, 1);
		gfx_frame_end();
	}
}

void ui_error(const char *text)
{
	Btn ok = { 110, 176, 100, 30, "OK" };
	while (ui_running()) {
		ui_poll();
		if (btn_tap(&ok) || (IN.kd & (KEY_A | KEY_B))) break;
		dlg_bg("Error", RGB(180,50,50), 30, 186);
		dlg_text(30, text);
		btn_draw(&BOT, &ok, 1);
		gfx_frame_end();
	}
}

int ui_confirm(const char *title, const char *text)
{
	Btn yes = { 30, 176, 120, 30, "Yes (A)" }, no = { 170, 176, 120, 30, "No (B)" };
	while (ui_running()) {
		ui_poll();
		if (btn_tap(&yes) || (IN.kd & KEY_A)) return 1;
		if (btn_tap(&no) || (IN.kd & KEY_B)) return 0;
		dlg_bg(title, RGB(190,130,30), 30, 186);
		dlg_text(30, text);
		btn_draw(&BOT, &yes, 1);
		btn_draw(&BOT, &no, 1);
		gfx_frame_end();
	}
	return 0;
}

int ui_progress(const char *title, const char *l1, const char *l2, int pct)
{
	static int spin = 0;
	Btn cancel = { 90, 190, 140, 34, "Cancel (B)" };
	ui_poll();
	int cancelled = btn_tap(&cancel) || (IN.kd & KEY_B);
	gfx_frame_begin();
	gx_vgrad(&TOP, 0, 0, TOP_W, SCR_H, C_BG, RGB(10,11,14));
	gx_fill(&TOP, 0, 0, TOP_W, 22, C_BAR);
	gx_fill(&TOP, 0, 21, TOP_W, 1, th_accent());
	gx_text(&TOP, 8, 6, title, C_TEXT);
	gx_text_n(&TOP, 20, 80, l1 ? l1 : "", TOP_W - 40, C_TEXT);
	gx_text_n(&TOP, 20, 98, l2 ? l2 : "", TOP_W - 40, C_DIM);
	gx_fill(&TOP, 20, 130, TOP_W - 40, 18, C_PAN2);
	if (pct >= 0) {
		if (pct > 100) pct = 100;
		gx_fill(&TOP, 20, 130, (TOP_W - 40) * pct / 100, 18, th_accent());
		char b[16];
		snprintf(b, sizeof b, "%d%%", pct);
		gx_text(&TOP, TOP_W / 2 - gx_text_w(b) / 2, 134, b, C_TEXT);
	} else {
		spin = (spin + 6) % (TOP_W - 40 - 60);
		gx_fill(&TOP, 20 + spin, 130, 60, 18, th_accent());
	}
	gx_fill(&BOT, 0, 0, BOT_W, SCR_H, C_BG);
	gx_text(&BOT, 16, 80, "Please wait...", C_DIM);
	btn_draw(&BOT, &cancel, 1);
	gfx_frame_end();
	return cancelled;
}

int ui_input(const char *hint, const char *initial, char *out, int outlen, int allow_empty)
{
	SwkbdState kb;
	swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, outlen - 1);
	swkbdSetHintText(&kb, hint);
	if (initial && *initial) swkbdSetInitialText(&kb, initial);
	swkbdSetValidation(&kb, allow_empty ? SWKBD_ANYTHING : SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	swkbdSetFeatures(&kb, SWKBD_ALLOW_HOME | SWKBD_ALLOW_POWER | SWKBD_ALLOW_RESET);
	char tmp[512];
	if (outlen > (int)sizeof tmp) outlen = sizeof tmp;
	tmp[0] = 0;
	SwkbdButton b = swkbdInputText(&kb, tmp, sizeof tmp);
	if (b != SWKBD_BUTTON_CONFIRM) return 0;
	snprintf(out, outlen, "%s", tmp);
	return 1;
}

/* ------------------------------------------------------------ icons & helpers */

void ui_logo(Surf *s, int x, int y, int sz)
{
	int h = sz / 2 - 1;
	u32 a = th_accent();
	gx_fill(s, x, y, h, h, a);
	gx_fill(s, x + h + 2, y, h, h, RGB(((a >> 16) & 255) * 3 / 4, ((a >> 8) & 255) * 3 / 4, (a & 255) * 3 / 4));
	gx_fill(s, x, y + h + 2, h, h, RGB(((a >> 16) & 255) * 3 / 4, ((a >> 8) & 255) * 3 / 4, (a & 255) * 3 / 4));
	gx_fill(s, x + h + 2, y + h + 2, h, h, RGB(((a >> 16) & 255) / 2, ((a >> 8) & 255) / 2, (a & 255) / 2));
}

void ui_icon(Surf *s, int id, int x, int y)
{
	static const u32 tile[ICON_COUNT] = {
		RGB(214,150,30), RGB(0,150,136), RGB(96,110,130), RGB(70,130,200), RGB(60,160,90),
		RGB(200,70,120), RGB(90,100,210), RGB(30,140,220), RGB(130,90,190)
	};
	const u32 W = RGB(255,255,255), D = tile[id];
	gx_fill(s, x + 1, y, 30, 32, D);
	gx_fill(s, x, y + 1, 32, 30, D);
	switch (id) {
	case ICON_FILES:
		gx_fill(s, x + 5, y + 8, 12, 5, W);
		gx_fill(s, x + 5, y + 12, 22, 15, RGB(255,230,140));
		break;
	case ICON_STORE:
		gx_fill(s, x + 7, y + 13, 18, 14, W);
		gx_rect(s, x + 11, y + 6, 10, 9, W);
		gx_fill(s, x + 12, y + 18, 8, 3, D);
		break;
	case ICON_SETTINGS:
		gx_fill(s, x + 10, y + 10, 12, 12, W);
		gx_fill(s, x + 14, y + 5, 4, 5, W); gx_fill(s, x + 14, y + 22, 4, 5, W);
		gx_fill(s, x + 5, y + 14, 5, 4, W); gx_fill(s, x + 22, y + 14, 5, 4, W);
		gx_fill(s, x + 7, y + 7, 4, 4, W);  gx_fill(s, x + 21, y + 7, 4, 4, W);
		gx_fill(s, x + 7, y + 21, 4, 4, W); gx_fill(s, x + 21, y + 21, 4, 4, W);
		gx_fill(s, x + 14, y + 14, 4, 4, D);
		break;
	case ICON_CALC:
		gx_fill(s, x + 7, y + 5, 18, 22, W);
		gx_fill(s, x + 9, y + 7, 14, 5, D);
		for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) gx_fill(s, x + 9 + c * 5, y + 14 + r * 4, 3, 2, D);
		break;
	case ICON_IMAGES:
		gx_rect(s, x + 5, y + 7, 22, 18, W);
		gx_fill(s, x + 7, y + 20, 18, 3, W);
		gx_fill(s, x + 10, y + 16, 8, 4, W); gx_fill(s, x + 13, y + 13, 3, 3, W);
		gx_fill(s, x + 20, y + 10, 4, 4, W);
		break;
	case ICON_MUSIC:
		gx_fill(s, x + 17, y + 7, 3, 15, W);
		gx_fill(s, x + 11, y + 19, 8, 5, W);
		gx_fill(s, x + 20, y + 7, 6, 4, W);
		break;
	case ICON_EDITOR:
		gx_fill(s, x + 8, y + 5, 16, 22, W);
		for (int i = 0; i < 4; i++) gx_fill(s, x + 11, y + 9 + i * 4, 10, 2, D);
		break;
	case ICON_DOWNLOADS:
		gx_fill(s, x + 14, y + 6, 4, 10, W);
		gx_fill(s, x + 9, y + 16, 14, 2, W); gx_fill(s, x + 11, y + 18, 10, 2, W);
		gx_fill(s, x + 13, y + 20, 6, 2, W);  gx_fill(s, x + 15, y + 22, 2, 2, W);
		gx_fill(s, x + 8, y + 25, 16, 3, W);
		break;
	case ICON_SYSINFO:
		gx_fill(s, x + 14, y + 6, 4, 4, W);
		gx_fill(s, x + 14, y + 12, 4, 14, W);
		gx_fill(s, x + 12, y + 12, 2, 2, W);
		gx_fill(s, x + 11, y + 24, 10, 2, W);
		break;
	}
}

void ui_letter_icon(Surf *s, const char *name, int x, int y, int size)
{
	unsigned h = 7;
	for (const char *p = name; *p; p++) h = h * 31 + (unsigned char)*p;
	u32 col = RGB(60 + (h & 127), 60 + ((h >> 7) & 127), 60 + ((h >> 14) & 127));
	gx_fill(s, x, y, size, size, col);
	char ch[2] = { name[0] >= 'a' && name[0] <= 'z' ? name[0] - 32 : name[0], 0 };
	if (!ch[0]) ch[0] = '?';
	gx_text2(s, x + size / 2 - 6, y + size / 2 - 11, ch, RGB(255,255,255));
}

void fmt_size(unsigned long long b, char *out, int n)
{
	if (b < 1024ULL) snprintf(out, n, "%llu B", b);
	else if (b < 1024ULL * 1024) snprintf(out, n, "%.1f KB", b / 1024.0);
	else if (b < 1024ULL * 1024 * 1024) snprintf(out, n, "%.1f MB", b / 1048576.0);
	else snprintf(out, n, "%.2f GB", b / 1073741824.0);
}

void ui_wallpaper(Surf *s, int w)
{
	static const u32 WP[4][2] = {
		{ RGB(10,28,60),  RGB(0,90,160) },
		{ RGB(12,30,28),  RGB(0,120,110) },
		{ RGB(40,16,16),  RGB(170,70,40) },
		{ RGB(26,14,48),  RGB(110,60,170) },
	};
	int k = S.wallpaper & 3;
	gx_vgrad(s, 0, 0, w, SCR_H, WP[k][0], WP[k][1]);
	/* soft light panes, a nod to the logo */
	gx_blend(s, w / 2 - 10, 20, w / 2 - 20, 90, RGB(255,255,255), 14);
	gx_blend(s, w / 2 - 40, 60, w / 2 - 20, 90, RGB(255,255,255), 10);
}

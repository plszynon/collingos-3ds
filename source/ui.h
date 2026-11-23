#pragma once
#include <3ds.h>
#include <stdint.h>
#include "gfx.h"
#include "settings.h"

#define COS_VERSION "1.0.0"

#define C_BG    RGB(22,24,30)
#define C_PANEL RGB(34,38,48)
#define C_PAN2  RGB(46,51,64)
#define C_BAR   RGB(15,17,22)
#define C_TEXT  RGB(236,238,242)
#define C_DIM   RGB(146,152,166)
#define C_OK    RGB(70,190,110)
#define C_ERR   RGB(230,84,84)
#define C_WARN  RGB(240,182,64)

extern const u32 ACCENTS[4];
u32 th_accent(void);

typedef struct {
	u32 kd, kh, ku;
	int touching, down, up, tapped, dragged;
	int tx, ty, sx, sy, dy;
} Input;
extern Input IN;
extern int g_quit;

void ui_poll(void);
int  ui_running(void);

/* ---- widgets ---- */
typedef struct { int x, y, w, h; const char *label; } Btn;
int  btn_in(const Btn *b, int x, int y);
int  btn_tap(const Btn *b);
void btn_draw(Surf *s, const Btn *b, int enabled);

void ui_titlebar(const char *title);   /* bottom-screen title bar with close button */
int  ui_close_tapped(void);

typedef struct { int n, sel, scroll, row_h, x, y, w, h, act; } List;
void list_init(List *l, int x, int y, int w, int h, int row_h);
void list_set_n(List *l, int n);
void list_update(List *l);              /* sets l->act (>=0 when an item was activated) */
void list_show_sel(List *l);
int  list_first(const List *l);
int  list_row_y(const List *l, int i);

/* ---- dialogs (blocking) ---- */
void ui_msg(const char *title, const char *text);
void ui_error(const char *text);
int  ui_confirm(const char *title, const char *text);
int  ui_progress(const char *title, const char *l1, const char *l2, int pct); /* 1 = cancel */
int  ui_input(const char *hint, const char *initial, char *out, int outlen, int allow_empty);

/* ---- icons ---- */
enum { ICON_FILES, ICON_STORE, ICON_SETTINGS, ICON_CALC, ICON_IMAGES, ICON_MUSIC, ICON_EDITOR, ICON_DOWNLOADS, ICON_SYSINFO, ICON_COUNT };
void ui_icon(Surf *s, int id, int x, int y);
void ui_letter_icon(Surf *s, const char *name, int x, int y, int size);
void ui_logo(Surf *s, int x, int y, int sz);

/* ---- helpers ---- */
void fmt_size(unsigned long long b, char *out, int n);
void ui_wallpaper(Surf *s, int w);

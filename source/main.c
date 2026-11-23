#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <3ds.h>
#include "apps.h"
#include "net.h"

typedef struct { const char *name; int icon; void (*run)(void); } AppDef;

static const AppDef APPS[9] = {
	{ "Files",     ICON_FILES,     app_files },
	{ "Store",     ICON_STORE,     app_store },
	{ "Settings",  ICON_SETTINGS,  app_settings },
	{ "Calculator",ICON_CALC,      app_calc },
	{ "Images",    ICON_IMAGES,    app_images },
	{ "Music",     ICON_MUSIC,     app_music },
	{ "Editor",    ICON_EDITOR,    app_editor },
	{ "Downloads", ICON_DOWNLOADS, app_downloads },
	{ "System",    ICON_SYSINFO,   app_sysinfo },
};

#define TASKBAR_Y 216
#define MENU_W 176
#define MENU_ROW 19
#define MENU_ITEMS 10          /* 9 apps + exit */
#define MENU_H (22 + MENU_ITEMS * MENU_ROW)

static u8 g_batt = 5, g_chg = 0;
static int g_wifi = 0;

static void clock_str(char *out, int n, int with_date)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	if (!tm) { snprintf(out, n, "--:--"); return; }
	if (with_date) strftime(out, n, "%a %d %b %Y", tm);
	else if (S.clock24) strftime(out, n, "%H:%M", tm);
	else strftime(out, n, "%I:%M %p", tm);
}

static void draw_battery(Surf *s, int x, int y)
{
	gx_rect(s, x, y, 22, 10, C_DIM);
	gx_fill(s, x + 22, y + 3, 2, 4, C_DIM);
	int lv = g_batt > 5 ? 5 : g_batt;
	u32 col = g_chg ? C_OK : (lv <= 1 ? C_ERR : C_TEXT);
	for (int i = 0; i < lv; i++) gx_fill(s, x + 2 + i * 4, y + 2, 3, 6, col);
}

static void draw_top(void)
{
	ui_wallpaper(&TOP, TOP_W);
	ui_logo(&TOP, 40, 56, 96);
	gx_text2(&TOP, 40, 164, "CollingOS", C_TEXT);
	gx_text(&TOP, 42, 190, "3DS Edition  v" COS_VERSION, RGB(200,210,225));
	char t[32], d[40];
	clock_str(t, sizeof t, 0);
	clock_str(d, sizeof d, 1);
	gx_text2(&TOP, TOP_W - 14 - (int)strlen(t) * 12, 12, t, C_TEXT);
	gx_text(&TOP, TOP_W - 14 - gx_text_w(d), 38, d, RGB(210,220,235));
	draw_battery(&TOP, 8, 8);
	gx_fill(&TOP, 40, 8, 8, 8, g_wifi ? C_OK : C_ERR);
	gx_text(&TOP, 52, 8, g_wifi ? "Wi-Fi" : "No Wi-Fi", C_TEXT);
}

static void draw_bottom(int sel, float menu_p)
{
	ui_wallpaper(&BOT, BOT_W);
	for (int i = 0; i < 9; i++) {
		int c = i % 3, r = i / 3;
		int cx = 10 + c * 102, cy = 8 + r * 68;
		if (i == sel) gx_blend(&BOT, cx, cy, 100, 64, RGB(255,255,255), 50);
		ui_icon(&BOT, APPS[i].icon, cx + 34, cy + 6);
		int w = gx_text_w(APPS[i].name);
		gx_text(&BOT, cx + 50 - w / 2 + 1, cy + 45, APPS[i].name, RGB(0,0,0));
		gx_text(&BOT, cx + 50 - w / 2, cy + 44, APPS[i].name, C_TEXT);
	}
	/* taskbar */
	gx_fill(&BOT, 0, TASKBAR_Y, BOT_W, 24, C_BAR);
	gx_fill(&BOT, 0, TASKBAR_Y, BOT_W, 1, th_accent());
	int open = menu_p > 0.5f;
	gx_fill(&BOT, 2, TASKBAR_Y + 2, 64, 20, open ? th_accent() : C_PAN2);
	ui_logo(&BOT, 7, TASKBAR_Y + 6, 12);
	gx_text(&BOT, 25, TASKBAR_Y + 7, "Start", C_TEXT);
	char t[32];
	clock_str(t, sizeof t, 0);
	int tw = gx_text_w(t);
	gx_text(&BOT, BOT_W - 6 - tw, TASKBAR_Y + 7, t, C_TEXT);
	draw_battery(&BOT, BOT_W - 6 - tw - 32, TASKBAR_Y + 7);
	gx_fill(&BOT, BOT_W - 6 - tw - 44, TASKBAR_Y + 9, 8, 8, g_wifi ? C_OK : C_ERR);
	/* start menu */
	if (menu_p > 0.01f) {
		int y = TASKBAR_Y - (int)(MENU_H * menu_p);
		gx_fill(&BOT, 2, y, MENU_W, MENU_H, C_PANEL);
		gx_fill(&BOT, 2, y, MENU_W, 22, C_BAR);
		gx_fill(&BOT, 2, y + 21, MENU_W, 1, th_accent());
		ui_logo(&BOT, 8, y + 5, 12);
		gx_text(&BOT, 26, y + 6, "CollingOS", C_TEXT);
		for (int i = 0; i < MENU_ITEMS; i++) {
			int ry = y + 22 + i * MENU_ROW;
			if (i == sel && sel < MENU_ITEMS) { /* d-pad highlight handled by caller via sel only on desktop */ }
			if (i < 9) {
				gx_fill(&BOT, 8, ry + 4, 11, 11, th_accent());
				gx_text(&BOT, 26, ry + 4, APPS[i].name, C_TEXT);
			} else {
				gx_fill(&BOT, 8, ry + 4, 11, 11, C_ERR);
				gx_text(&BOT, 26, ry + 4, "Exit CollingOS", C_TEXT);
			}
		}
	}
}

static void launch(int i)
{
	APPS[i].run();
	gfx_fade_in();
}

static void desktop(void)
{
	int sel = 0, menu_open = 0;
	float menu_p = 0;
	int frame = 0;
	gfx_fade_in();
	while (ui_running()) {
		ui_poll();
		if (frame % 120 == 0) {
			PTMU_GetBatteryLevel(&g_batt);
			PTMU_GetBatteryChargeState(&g_chg);
			g_wifi = net_wifi_ok();
		}
		frame++;

		/* start menu animation */
		float target = menu_open ? 1.0f : 0.0f;
		if (S.anim) {
			if (menu_p < target) { menu_p += 0.2f; if (menu_p > target) menu_p = target; }
			else if (menu_p > target) { menu_p -= 0.2f; if (menu_p < target) menu_p = target; }
		} else menu_p = target;

		/* keys */
		if (IN.kd & KEY_START) menu_open = !menu_open;
		if (IN.kd & KEY_B) menu_open = 0;
		if (!menu_open) {
			if ((IN.kd & KEY_RIGHT) && sel % 3 < 2) sel++;
			if ((IN.kd & KEY_LEFT) && sel % 3 > 0) sel--;
			if ((IN.kd & KEY_DOWN) && sel < 6) sel += 3;
			if ((IN.kd & KEY_UP) && sel >= 3) sel -= 3;
			if (IN.kd & KEY_A) { launch(sel); continue; }
		}

		/* touch */
		if (IN.tapped) {
			int x = IN.tx, y = IN.ty;
			if (y >= TASKBAR_Y) {
				if (x < 68) menu_open = !menu_open;
			} else if (menu_open) {
				int my = TASKBAR_Y - MENU_H;
				if (x >= 2 && x < 2 + MENU_W && y >= my + 22 && y < TASKBAR_Y) {
					int i = (y - my - 22) / MENU_ROW;
					menu_open = 0;
					menu_p = 0;
					if (i < 9) { launch(i); continue; }
					if (i == 9 && ui_confirm("Exit CollingOS", "Return to the Homebrew Launcher?")) g_quit = 1;
				} else {
					menu_open = 0;
				}
			} else {
				int c = (x - 10) / 102, r = (y - 8) / 68;
				if (x >= 10 && c >= 0 && c < 3 && r >= 0 && r < 3 && y >= 8) {
					sel = r * 3 + c;
					launch(sel);
					continue;
				}
			}
		}

		gfx_frame_begin();
		draw_top();
		draw_bottom(sel, menu_p);
		gfx_frame_end();
	}
}

int main(int argc, char **argv)
{
	(void)argc; (void)argv;
	osSetSpeedupEnable(true);              /* New 3DS: 804 MHz + extra L2; harmless on the original */
	gfxInitDefault();
	gfxSet3D(false);
	romfsInit();                           /* certificate bundle + built-in catalog live in RomFS */
	ptmuInit();
	fs_make_dirs();
	settings_load();
	desktop();
	settings_save();
	net_shutdown();
	ptmuExit();
	romfsExit();
	gfxExit();
	return 0;
}

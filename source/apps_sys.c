#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <malloc.h>
#include "apps.h"
#include "net.h"

/* ============================================================ common */

static void top_header(const char *title)
{
	gx_vgrad(&TOP, 0, 0, TOP_W, SCR_H, C_BG, RGB(14,15,19));
	gx_fill(&TOP, 0, 0, TOP_W, 22, C_BAR);
	gx_fill(&TOP, 0, 21, TOP_W, 1, th_accent());
	ui_logo(&TOP, 6, 5, 12);
	gx_text(&TOP, 24, 6, title, C_TEXT);
}

/* ============================================================ calculator */

typedef struct {
	char entry[32];
	double acc;
	char op;
	int fresh, err;
	char hist[6][48];
	int nh;
} Calc;

static double cval(const Calc *c) { return atof(c->entry); }

static void set_num(char *dst, size_t n, double v) { snprintf(dst, n, "%.10g", v); }

static int calc_apply(Calc *c, double b)
{
	switch (c->op) {
	case '+': c->acc += b; break;
	case '-': c->acc -= b; break;
	case '*': c->acc *= b; break;
	case '/': if (b == 0) return -1; c->acc /= b; break;
	default: c->acc = b; break;
	}
	return 0;
}

static void calc_press(Calc *c, const char *k)
{
	if (c->err) { memset(c, 0, sizeof *c); strcpy(c->entry, "0"); c->fresh = 1; if (!strcmp(k, "C")) return; }
	if (!strcmp(k, "C")) { memset(c, 0, sizeof *c); strcpy(c->entry, "0"); c->fresh = 1; return; }
	if (!strcmp(k, "<-")) {
		if (c->fresh) return;
		size_t l = strlen(c->entry);
		if (l <= 1 || (l == 2 && c->entry[0] == '-')) strcpy(c->entry, "0"); else c->entry[l - 1] = 0;
		return;
	}
	if (!strcmp(k, "+/-")) {
		if (!strcmp(c->entry, "0")) return;
		if (c->entry[0] == '-') memmove(c->entry, c->entry + 1, strlen(c->entry)); else { memmove(c->entry + 1, c->entry, strlen(c->entry) + 1); c->entry[0] = '-'; }
		return;
	}
	if (!strcmp(k, "%")) { set_num(c->entry, sizeof c->entry, cval(c) / 100.0); c->fresh = 1; return; }
	if (!strcmp(k, ".")) {
		if (c->fresh) { strcpy(c->entry, "0"); c->fresh = 0; }
		if (!strchr(c->entry, '.') && strlen(c->entry) < 15) strcat(c->entry, ".");
		return;
	}
	if (k[0] >= '0' && k[0] <= '9' && !k[1]) {
		if (c->fresh || !strcmp(c->entry, "0")) { c->entry[0] = 0; c->fresh = 0; }
		if (strlen(c->entry) < 15) strcat(c->entry, k);
		return;
	}
	if (!strcmp(k, "=")) {
		if (!c->op) return;
		double a = c->acc, b = cval(c);
		char opc = c->op;
		if (calc_apply(c, b) < 0) { c->err = 1; strcpy(c->entry, "Error"); return; }
		char ra[32], rb[32], rr[32];
		set_num(ra, sizeof ra, a); set_num(rb, sizeof rb, b); set_num(rr, sizeof rr, c->acc);
		if (c->nh == 6) { memmove(c->hist[0], c->hist[1], sizeof c->hist[0] * 5); c->nh = 5; }
		snprintf(c->hist[c->nh++], sizeof c->hist[0], "%s %c %s = %s", ra, opc, rb, rr);
		set_num(c->entry, sizeof c->entry, c->acc);
		c->op = 0; c->fresh = 1;
		return;
	}
	/* operator */
	if (!strcmp(k, "+") || !strcmp(k, "-") || !strcmp(k, "*") || !strcmp(k, "/")) {
		if (c->op && !c->fresh) {
			if (calc_apply(c, cval(c)) < 0) { c->err = 1; strcpy(c->entry, "Error"); return; }
			set_num(c->entry, sizeof c->entry, c->acc);
		} else {
			c->acc = cval(c);
		}
		c->op = k[0];
		c->fresh = 1;
	}
}

void app_calc(void)
{
	static const char *K[5][4] = {
		{ "C", "<-", "%", "/" }, { "7", "8", "9", "*" }, { "4", "5", "6", "-" },
		{ "1", "2", "3", "+" }, { "+/-", "0", ".", "=" }
	};
	Calc c;
	memset(&c, 0, sizeof c);
	strcpy(c.entry, "0");
	c.fresh = 1;
	Btn b[5][4];
	for (int r = 0; r < 5; r++) for (int k = 0; k < 4; k++) b[r][k] = (Btn){ 4 + k * 79, 24 + r * 43, 77, 40, K[r][k] };
	gfx_fade_in();
	while (ui_running()) {
		ui_poll();
		if (ui_close_tapped() || (IN.kd & KEY_B)) break;
		for (int r = 0; r < 5; r++) for (int k = 0; k < 4; k++) if (btn_tap(&b[r][k])) calc_press(&c, K[r][k]);

		gfx_frame_begin();
		top_header("Calculator");
		gx_fill(&TOP, 16, 34, TOP_W - 32, 60, C_PANEL);
		char expr[64] = "";
		if (c.op) { char a[32]; set_num(a, sizeof a, c.acc); snprintf(expr, sizeof expr, "%s %c", a, c.op); }
		gx_text(&TOP, TOP_W - 24 - gx_text_w(expr), 40, expr, C_DIM);
		int w = (int)strlen(c.entry) * 12;
		gx_text2(&TOP, TOP_W - 24 - w, 62, c.entry, c.err ? C_ERR : C_TEXT);
		gx_text(&TOP, 16, 108, "History", C_DIM);
		for (int i = 0; i < c.nh; i++) gx_text_n(&TOP, 16, 124 + i * LINE_H, c.hist[c.nh - 1 - i], TOP_W - 32, i == 0 ? C_TEXT : C_DIM);
		gx_fill(&BOT, 0, 0, BOT_W, SCR_H, C_BG);
		for (int r = 0; r < 5; r++) for (int k = 0; k < 4; k++) btn_draw(&BOT, &b[r][k], 1);
		ui_titlebar("Calculator");
		gfx_frame_end();
	}
}

/* ============================================================ settings */

static const char *WP_NAMES[4] = { "Ocean", "Teal", "Ember", "Violet" };
static const char *AC_NAMES[4] = { "Blue", "Teal", "Orange", "Purple" };

void app_settings(void)
{
	gfx_fade_in();
	while (ui_running()) {
		ui_poll();
		if (ui_close_tapped() || (IN.kd & KEY_B)) break;
		int hit = -1;
		if (IN.tapped && IN.ty >= 24) hit = (IN.ty - 24) / 30;
		if (hit >= 0 && hit < 7) {
			switch (hit) {
			case 0: S.wallpaper = (S.wallpaper + 1) & 3; break;
			case 1: S.accent = (S.accent + 1) & 3; break;
			case 2: S.clock24 = !S.clock24; break;
			case 3: S.anim = !S.anim; break;
			case 4: S.volume = S.volume >= 100 ? 0 : S.volume + 10; break;
			case 5: {
				char u[200];
				if (ui_input("Catalog URL (https://...)", S.store_url, u, sizeof u, 1)) {
					if (u[0] && strncmp(u, "https://", 8) != 0) ui_error("The catalog URL must start with https://");
					else snprintf(S.store_url, sizeof S.store_url, "%s", u);
				}
				break;
			}
			case 6:
				if (ui_confirm("Reset", "Reset all CollingOS settings to defaults?")) settings_defaults();
				break;
			}
			settings_save();
		}
		gfx_frame_begin();
		top_header("Settings");
		ui_wallpaper(&TOP, TOP_W);
		gx_fill(&TOP, 0, 0, TOP_W, 22, C_BAR);
		gx_fill(&TOP, 0, 21, TOP_W, 1, th_accent());
		ui_logo(&TOP, 6, 5, 12);
		gx_text(&TOP, 24, 6, "Settings - preview", C_TEXT);
		ui_logo(&TOP, 160, 60, 80);
		gx_text2(&TOP, 98, 160, "CollingOS", C_TEXT);
		gx_text(&TOP, 8, 224, "Changes are saved automatically.", C_DIM);

		gx_fill(&BOT, 0, 0, BOT_W, SCR_H, C_BG);
		char v[7][100];
		snprintf(v[0], sizeof v[0], "%s", WP_NAMES[S.wallpaper & 3]);
		snprintf(v[1], sizeof v[1], "%s", AC_NAMES[S.accent & 3]);
		snprintf(v[2], sizeof v[2], "%s", S.clock24 ? "24-hour" : "12-hour");
		snprintf(v[3], sizeof v[3], "%s", S.anim ? "On" : "Off");
		snprintf(v[4], sizeof v[4], "%d%%", S.volume);
		snprintf(v[5], sizeof v[5], "Edit");
		snprintf(v[6], sizeof v[6], "Reset");
		static const char *label[7] = { "Wallpaper", "Accent color", "Clock", "Animations", "Music volume", "Store catalog URL", "Defaults" };
		for (int i = 0; i < 7; i++) {
			int y = 24 + i * 30;
			gx_fill(&BOT, 4, y, BOT_W - 8, 28, i & 1 ? C_PANEL : RGB(30,33,42));
			gx_text(&BOT, 12, y + 9, label[i], C_TEXT);
			gx_text(&BOT, BOT_W - 12 - gx_text_w(v[i]), y + 9, v[i], i == 1 ? th_accent() : C_DIM);
		}
		ui_titlebar("Settings");
		gfx_frame_end();
	}
	settings_save();
}

/* ============================================================ system information */

typedef struct { char k[20]; char v[80]; } KV;

#include <stdarg.h>
static void add(KV *a, int *n, const char *k, const char *fmt, ...)
{
	if (*n >= 24) return;
	snprintf(a[*n].k, sizeof a[*n].k, "%s", k);
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(a[*n].v, sizeof a[*n].v, fmt, ap);
	va_end(ap);
	(*n)++;
}

void app_sysinfo(void)
{
	static const char *MODEL[6] = { "Nintendo 3DS", "Nintendo 3DS XL", "New Nintendo 3DS", "Nintendo 2DS", "New Nintendo 3DS XL", "New Nintendo 2DS XL" };
	static const char *REGION[7] = { "Japan", "USA", "Europe", "Australia", "China", "Korea", "Taiwan" };
	cfguInit();
	u8 model = 255, region = 255;
	CFGU_GetSystemModel(&model);
	CFGU_SecureInfoGetRegion(&region);
	bool isnew = false;
	APT_CheckNew3DS(&isnew);
	gfx_fade_in();
	int frame = 0;
	KV kv[24];
	int n = 0;
	unsigned long long sd_total = 0, sd_free = 0;
	u8 batt = 0, chg = 0;
	while (ui_running()) {
		ui_poll();
		if (ui_close_tapped() || (IN.kd & KEY_B)) break;
		if (frame % 60 == 0) {
			n = 0;
			u32 fv = osGetFirmVersion(), kvv = osGetKernelVersion();
			add(kv, &n, "Model", "%s", model < 6 ? MODEL[model] : "Unknown");
			add(kv, &n, "Region", "%s", region < 7 ? REGION[region] : "Unknown");
			add(kv, &n, "CPU mode", "%s", isnew ? "New 3DS (804 MHz boost on)" : "Original (268 MHz)");
			add(kv, &n, "Kernel", "%lu.%lu-%lu", GET_VERSION_MAJOR(kvv), GET_VERSION_MINOR(kvv), GET_VERSION_REVISION(kvv));
			add(kv, &n, "Firmware", "%lu.%lu.%lu", GET_VERSION_MAJOR(fv), GET_VERSION_MINOR(fv), GET_VERSION_REVISION(fv));
			add(kv, &n, "Wi-Fi", "%s", net_wifi_ok() ? "Connected" : "Not connected");
			char a[24], b[24];
			fmt_size(osGetMemRegionFree(MEMREGION_APPLICATION), a, sizeof a);
			fmt_size(osGetMemRegionSize(MEMREGION_APPLICATION), b, sizeof b);
			add(kv, &n, "App RAM free", "%s of %s", a, b);
			fmt_size(linearSpaceFree(), a, sizeof a);
			add(kv, &n, "Linear RAM free", "%s", a);
			FS_ArchiveResource res;
			if (R_SUCCEEDED(FSUSER_GetArchiveResource(&res, SYSTEM_MEDIATYPE_SD))) {
				sd_total = (unsigned long long)res.totalClusters * res.clusterSize;
				sd_free = (unsigned long long)res.freeClusters * res.clusterSize;
			}
			fmt_size(sd_free, a, sizeof a);
			fmt_size(sd_total, b, sizeof b);
			add(kv, &n, "SD card", "%s free of %s", a, b);
			if (R_SUCCEEDED(PTMU_GetBatteryLevel(&batt)) && R_SUCCEEDED(PTMU_GetBatteryChargeState(&chg)))
				add(kv, &n, "Battery", "%d/5%s", batt, chg ? " (charging)" : "");
			time_t t = time(NULL);
			struct tm *tm = localtime(&t);
			char ts[40] = "?";
			if (tm) strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", tm);
			add(kv, &n, "Date/time", "%s", ts);
		}
		frame++;
		gfx_frame_begin();
		top_header("System Information");
		ui_logo(&TOP, 24, 40, 64);
		gx_text2(&TOP, 104, 44, "CollingOS", C_TEXT);
		gx_text(&TOP, 104, 70, "3DS Edition", C_DIM);
		gx_text(&TOP, 104, 84, "Version " COS_VERSION, C_DIM);
		gx_text(&TOP, 104, 98, "Author: plszynon", C_DIM);
		gx_text(&TOP, 24, 130, "Native Nintendo 3DS homebrew (.3dsx)", C_DIM);
		gx_text(&TOP, 24, 143, "Built with devkitARM + libctru", C_DIM);
		gx_fill(&BOT, 0, 0, BOT_W, SCR_H, C_BG);
		for (int i = 0; i < n; i++) {
			int y = 24 + i * 18;
			gx_text(&BOT, 8, y, kv[i].k, C_DIM);
			gx_text_n(&BOT, 112, y, kv[i].v, BOT_W - 116, C_TEXT);
		}
		if (sd_total) {
			gx_fill(&BOT, 8, 226, BOT_W - 16, 8, C_PAN2);
			gx_fill(&BOT, 8, 226, (int)((BOT_W - 16) * (sd_total - sd_free) / sd_total), 8, th_accent());
		}
		ui_titlebar("System Information");
		gfx_frame_end();
	}
	cfguExit();
}

/* ============================================================ downloads */

static void make_filename(const char *url, char *out, int n)
{
	const char *s = strrchr(url, '/');
	s = s ? s + 1 : url;
	int i = 0;
	for (; s[i] && s[i] != '?' && s[i] != '#' && i < n - 1; i++) {
		char c = s[i];
		out[i] = strchr("\\:*\"<>|", c) ? '_' : c;
	}
	out[i] = 0;
	if (!out[0]) snprintf(out, n, "download.bin");
}

static void run_download(const char *url, const char *dest, const char *name)
{
	if (fs_protected(dest)) { ui_error("That destination is protected. Choose a different file name."); return; }
	struct stat st;
	if (stat(dest, &st) == 0 && !ui_confirm("Overwrite", "A file with that name already exists. Replace it?")) return;
	DlResult r;
	if (net_download(url, dest, name, NULL, 128ULL * 1024 * 1024, &r) != 0) {
		if (r.cancelled) ui_msg("Cancelled", "The download was cancelled."); else ui_error(r.err);
		return;
	}
	char sz[24], msg[300];
	fmt_size(r.bytes, sz, sizeof sz);
	snprintf(msg, sizeof msg, "Saved %s (%s) to %s", name, sz, dest);
	ui_msg("Download complete", msg);
}

void app_downloads(void)
{
	HistItem *h = NULL;
	int n = history_load(&h);
	List l;
	list_init(&l, 0, 20, BOT_W, 156, 26);
	list_set_n(&l, n);
	Btn bNew = { 0, 178, 107, 30, "New download" }, bRetry = { 107, 178, 106, 30, "Retry" }, bClear = { 213, 178, 107, 30, "Clear list" };
	Btn bOpen = { 0, 210, 320, 30, "Open Downloads folder" };
	gfx_fade_in();
	while (ui_running()) {
		ui_poll();
		list_update(&l);
		if (ui_close_tapped() || (IN.kd & KEY_B)) break;
		int changed = 0;
		if (btn_tap(&bNew)) {
			char url[220], name[60], dest[300];
			if (ui_input("Download URL (https://...)", "https://", url, sizeof url, 0)) {
				if (strncmp(url, "https://", 8) != 0) ui_error("Only HTTPS links are allowed.");
				else {
					make_filename(url, name, sizeof name);
					if (ui_input("Save as (file name)", name, name, sizeof name, 0)) {
						if (strpbrk(name, "/\\:*?\"<>|")) ui_error("That file name contains characters FAT32 does not allow.");
						else { snprintf(dest, sizeof dest, DIR_DL "/%s", name); run_download(url, dest, name); changed = 1; }
					}
				}
			}
		}
		if (btn_tap(&bRetry) && n > 0) {
			HistItem *it = &h[l.sel];
			if (it->url[0]) { run_download(it->url, it->dest, it->name); changed = 1; }
		}
		if (btn_tap(&bClear) && n > 0 && ui_confirm("Clear history", "Remove the download history list? Downloaded files are kept.")) {
			history_clear(); changed = 1;
		}
		if (btn_tap(&bOpen)) { files_open_at(DIR_DL); gfx_fade_in(); }
		if (changed) { free(h); n = history_load(&h); list_set_n(&l, n); l.sel = 0; l.scroll = 0; gfx_fade_in(); }

		gfx_frame_begin();
		top_header("Download Manager");
		if (n > 0) {
			HistItem *it = &h[l.sel];
			char sz[24], when[40] = "?", line[120];
			fmt_size(it->bytes, sz, sizeof sz);
			time_t t = it->when;
			struct tm *tm = localtime(&t);
			if (tm) strftime(when, sizeof when, "%Y-%m-%d %H:%M", tm);
			gx_text_n(&TOP, 12, 34, it->name, TOP_W - 24, C_TEXT);
			const char *st = it->status == 'O' ? "Completed" : it->status == 'C' ? "Cancelled" : "Failed";
			gx_text(&TOP, 12, 52, st, it->status == 'O' ? C_OK : it->status == 'C' ? C_WARN : C_ERR);
			snprintf(line, sizeof line, "Size: %s    When: %s", sz, when);
			gx_text(&TOP, 12, 68, line, C_DIM);
			gx_text(&TOP, 12, 90, "Saved to:", C_DIM);
			gx_wrap(&TOP, 12, 103, TOP_W - 24, 2, it->dest, C_TEXT);
			gx_text(&TOP, 12, 134, "From:", C_DIM);
			gx_wrap(&TOP, 12, 147, TOP_W - 24, 4, it->url, C_TEXT);
		} else {
			gx_text(&TOP, 12, 40, "No downloads yet.", C_DIM);
			gx_text(&TOP, 12, 56, "Tap \"New download\" to fetch a file over HTTPS.", C_DIM);
		}
		gx_fill(&BOT, 0, 0, BOT_W, SCR_H, C_BG);
		for (int i = list_first(&l); i < n; i++) {
			int ry = list_row_y(&l, i);
			if (ry >= l.y + l.h) break;
			if (i == l.sel) gx_fill(&BOT, 0, ry, BOT_W, l.row_h, th_accent());
			else if (i & 1) gx_fill(&BOT, 0, ry, BOT_W, l.row_h, RGB(27,30,38));
			char s = h[i].status;
			gx_fill(&BOT, 6, ry + 8, 9, 9, s == 'O' ? C_OK : s == 'C' ? C_WARN : C_ERR);
			gx_text_n(&BOT, 22, ry + 7, h[i].name, 200, C_TEXT);
			char sz[24];
			fmt_size(h[i].bytes, sz, sizeof sz);
			gx_text(&BOT, BOT_W - 6 - gx_text_w(sz), ry + 7, sz, C_DIM);
		}
		ui_titlebar("Download Manager");
		gx_fill(&BOT, 0, 176, BOT_W, 64, C_BAR);
		btn_draw(&BOT, &bNew, 1); btn_draw(&BOT, &bRetry, n > 0); btn_draw(&BOT, &bClear, n > 0);
		btn_draw(&BOT, &bOpen, 1);
		gfx_frame_end();
	}
	free(h);
}

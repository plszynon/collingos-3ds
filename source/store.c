#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include "apps.h"
#include "net.h"
#include "image.h"

#define MAX_APPS 128
#define CATALOG_CACHE DIR_ROOT "/store.txt"
#define INSTALLED_FILE DIR_ROOT "/installed.txt"
#define MAX_APP_BYTES (64ULL * 1024 * 1024)

typedef struct {
	char id[32], name[40], ver[16], author[24], cat[16], url[220], icon[220], desc[200];
} StoreApp;

typedef struct { char id[32], ver[16]; } Inst;

enum { ST_NONE, ST_INSTALLED, ST_UPDATE, ST_EXTERNAL };

typedef struct {
	StoreApp *apps; int napps;
	Inst inst[MAX_APPS]; int ninst;
	int view[MAX_APPS]; int nview;
	char filter[48];
	char source[64];
	List list;
	uint8_t *icon; int iw, ih; char icon_id[32];
} Store;

/* ------------------------------------------------------------ helpers */

static int valid_id(const char *id)
{
	size_t n = strlen(id);
	if (n == 0 || n > 31) return 0;
	for (size_t i = 0; i < n; i++)
		if (!(islower((unsigned char)id[i]) || isdigit((unsigned char)id[i]) || id[i] == '-' || id[i] == '_')) return 0;
	return 1;
}

static int ci_contains(const char *hay, const char *needle)
{
	size_t n = strlen(needle);
	if (!n) return 1;
	for (; *hay; hay++) if (strncasecmp(hay, needle, n) == 0) return 1;
	return 0;
}

static void app_path(const StoreApp *a, char *dir, int dn, char *path, int pn)
{
	snprintf(dir, dn, "sdmc:/3ds/%s", a->id);
	snprintf(path, pn, "sdmc:/3ds/%s/%s.3dsx", a->id, a->id);
}

static int file_exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

/* ------------------------------------------------------------ installed list */

static void inst_load(Store *st)
{
	st->ninst = 0;
	FILE *f = fopen(INSTALLED_FILE, "r");
	if (!f) return;
	char line[100];
	while (st->ninst < MAX_APPS && fgets(line, sizeof line, f)) {
		char *nl = strpbrk(line, "\r\n"); if (nl) *nl = 0;
		char *bar = strchr(line, '|');
		if (!bar) continue;
		*bar = 0;
		if (!valid_id(line)) continue;
		snprintf(st->inst[st->ninst].id, sizeof st->inst[0].id, "%s", line);
		snprintf(st->inst[st->ninst].ver, sizeof st->inst[0].ver, "%s", bar + 1);
		st->ninst++;
	}
	fclose(f);
}

static void inst_save(const Store *st)
{
	FILE *f = fopen(INSTALLED_FILE ".tmp", "w");
	if (!f) return;
	for (int i = 0; i < st->ninst; i++) fprintf(f, "%s|%s\n", st->inst[i].id, st->inst[i].ver);
	int bad = ferror(f);
	fclose(f);
	if (bad) { remove(INSTALLED_FILE ".tmp"); return; }
	remove(INSTALLED_FILE);
	rename(INSTALLED_FILE ".tmp", INSTALLED_FILE);
}

static int inst_find(const Store *st, const char *id)
{
	for (int i = 0; i < st->ninst; i++) if (!strcmp(st->inst[i].id, id)) return i;
	return -1;
}

static void inst_set(Store *st, const char *id, const char *ver)
{
	int i = inst_find(st, id);
	if (i < 0) { if (st->ninst >= MAX_APPS) return; i = st->ninst++; }
	snprintf(st->inst[i].id, sizeof st->inst[0].id, "%s", id);
	snprintf(st->inst[i].ver, sizeof st->inst[0].ver, "%s", ver);
	inst_save(st);
}

static void inst_remove(Store *st, const char *id)
{
	int i = inst_find(st, id);
	if (i < 0) return;
	st->inst[i] = st->inst[--st->ninst];
	inst_save(st);
}

static int app_state(const Store *st, const StoreApp *a)
{
	char dir[64], path[128];
	app_path(a, dir, sizeof dir, path, sizeof path);
	int i = inst_find(st, a->id);
	if (i >= 0 && file_exists(path)) {
		if (strcmp(a->ver, "latest") != 0 && strcmp(st->inst[i].ver, a->ver) != 0) return ST_UPDATE;
		return ST_INSTALLED;
	}
	if (file_exists(path)) return ST_EXTERNAL;
	return ST_NONE;
}

/* ------------------------------------------------------------ catalog */

static void rebuild_view(Store *st)
{
	st->nview = 0;
	for (int i = 0; i < st->napps; i++) {
		const StoreApp *a = &st->apps[i];
		if (!st->filter[0] || ci_contains(a->name, st->filter) || ci_contains(a->desc, st->filter) ||
		    ci_contains(a->cat, st->filter) || ci_contains(a->author, st->filter))
			st->view[st->nview++] = i;
	}
	list_set_n(&st->list, st->nview);
}

static int parse_catalog(Store *st, const char *text)
{
	StoreApp *apps = calloc(MAX_APPS, sizeof(StoreApp));
	if (!apps) return -1;
	int n = 0;
	const char *p = text;
	while (*p && n < MAX_APPS) {
		const char *e = strchr(p, '\n');
		size_t len = e ? (size_t)(e - p) : strlen(p);
		char line[700];
		if (len >= sizeof line) len = sizeof line - 1;
		memcpy(line, p, len);
		line[len] = 0;
		p = e ? e + 1 : p + strlen(p);
		char *cr = strchr(line, '\r'); if (cr) *cr = 0;
		if (line[0] == '#' || line[0] == 0) continue;
		char *f[8];
		char *q = line;
		int k = 0;
		for (; k < 8; k++) {
			f[k] = q;
			if (k < 7) { char *bar = strchr(q, '|'); if (!bar) break; *bar = 0; q = bar + 1; }
		}
		if (k < 7) continue;
		if (!valid_id(f[0]) || strncmp(f[5], "https://", 8) != 0) continue;
		StoreApp *a = &apps[n++];
		snprintf(a->id, sizeof a->id, "%s", f[0]);
		snprintf(a->name, sizeof a->name, "%s", f[1]);
		snprintf(a->ver, sizeof a->ver, "%s", f[2]);
		snprintf(a->author, sizeof a->author, "%s", f[3]);
		snprintf(a->cat, sizeof a->cat, "%s", f[4]);
		snprintf(a->url, sizeof a->url, "%s", f[5]);
		snprintf(a->icon, sizeof a->icon, "%s", strncmp(f[6], "https://", 8) == 0 ? f[6] : "");
		snprintf(a->desc, sizeof a->desc, "%s", f[7]);
	}
	free(st->apps);
	st->apps = apps;
	st->napps = n;
	return n;
}

static char *read_file(const char *path, size_t max, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz < 0 || (size_t)sz > max) { fclose(f); return NULL; }
	char *b = malloc(sz + 1);
	if (!b) { fclose(f); return NULL; }
	size_t n = fread(b, 1, sz, f);
	fclose(f);
	b[n] = 0;
	if (len) *len = n;
	return b;
}

static int load_local(Store *st)
{
	char *t = read_file(CATALOG_CACHE, 256 * 1024, NULL);
	const char *src = "saved catalog";
	if (!t) { t = read_file("romfs:/store.txt", 256 * 1024, NULL); src = "built-in catalog"; }
	if (!t) return -1;
	int n = parse_catalog(st, t);
	free(t);
	if (n >= 0) snprintf(st->source, sizeof st->source, "%s", src);
	return n;
}

static void cache_path(const StoreApp *a, char *out, int n) { snprintf(out, n, DIR_CACHE "/%s.img", a->id); }

static void fetch_icons(Store *st)
{
	for (int i = 0; i < st->napps; i++) {
		const StoreApp *a = &st->apps[i];
		char cp[96];
		cache_path(a, cp, sizeof cp);
		if (!a->icon[0] || file_exists(cp)) continue;
		char title[48];
		snprintf(title, sizeof title, "Icons %d/%d", i + 1, st->napps);
		if (ui_progress("Store", title, a->name, -1)) return;
		char *data; size_t len; DlResult r;
		if (net_fetch_mem(a->icon, &data, &len, 128 * 1024, 8, &r) == 0) {
			FILE *f = fopen(cp, "wb");
			if (f) { fwrite(data, 1, len, f); fclose(f); }
			free(data);
		}
	}
}

static void do_refresh(Store *st)
{
	if (!S.store_url[0]) { ui_error("No catalog URL set. Set one in Settings."); return; }
	if (ui_progress("Store", "Updating catalog...", S.store_url, -1)) return;
	char *text; size_t len; DlResult r;
	if (net_fetch_mem(S.store_url, &text, &len, 256 * 1024, 15, &r) != 0) { ui_error(r.err); return; }
	int n = parse_catalog(st, text);
	if (n <= 0) { free(text); ui_error("The catalog was empty or in the wrong format. Your old catalog was kept."); load_local(st); rebuild_view(st); return; }
	FILE *f = fopen(CATALOG_CACHE ".tmp", "wb");
	if (f) {
		int ok = fwrite(text, 1, len, f) == len;
		fclose(f);
		if (ok) { remove(CATALOG_CACHE); rename(CATALOG_CACHE ".tmp", CATALOG_CACHE); }
		else remove(CATALOG_CACHE ".tmp");
	}
	free(text);
	snprintf(st->source, sizeof st->source, "online catalog");
	rebuild_view(st);
	fetch_icons(st);
	free(st->icon); st->icon = NULL; st->icon_id[0] = 0;
}

static void ensure_icon(Store *st, const StoreApp *a)
{
	if (!strcmp(st->icon_id, a->id)) return;
	free(st->icon);
	st->icon = NULL;
	snprintf(st->icon_id, sizeof st->icon_id, "%s", a->id);
	char cp[96];
	cache_path(a, cp, sizeof cp);
	size_t len;
	char *d = read_file(cp, 128 * 1024, &len);
	if (!d) return;
	const char *err;
	st->icon = img_decode((uint8_t *)d, len, &st->iw, &st->ih, 512L * 512L, &err);
	free(d);
}

/* ------------------------------------------------------------ actions */

static void do_install(Store *st, const StoreApp *a)
{
	if (!valid_id(a->id) || !strcasecmp(a->id, "collingos")) { ui_error("This catalog entry has an invalid id."); return; }
	char dir[64], path[128];
	app_path(a, dir, sizeof dir, path, sizeof path);
	int s = app_state(st, a);
	if (s == ST_EXTERNAL && !ui_confirm("Replace file", "A file with this name is already on your SD card and was not installed by CollingOS. Replace it?")) return;
	mkdir(dir, 0777);
	DlResult r;
	if (net_download(a->url, path, a->name, "3DSX", MAX_APP_BYTES, &r) != 0) {
		if (r.cancelled) ui_msg("Cancelled", "The download was cancelled."); else ui_error(r.err);
		return;
	}
	inst_set(st, a->id, a->ver);
	char msg[200];
	snprintf(msg, sizeof msg, "%s was saved to %s. Start it from the Homebrew Launcher.", a->name, path);
	ui_msg("Installed", msg);
}

static void do_uninstall(Store *st, const StoreApp *a)
{
	char dir[64], path[128];
	app_path(a, dir, sizeof dir, path, sizeof path);
	int s = app_state(st, a);
	if (s == ST_NONE) { ui_msg("Uninstall", "This app is not installed."); return; }
	char msg[200];
	snprintf(msg, sizeof msg, "Remove %s? Only %s.3dsx is deleted; the app's own data stays.", a->name, a->id);
	if (!ui_confirm("Uninstall", msg)) return;
	if (remove(path) != 0) { ui_error("Could not delete the app file."); return; }
	rmdir(dir); /* succeeds only if the folder is now empty */
	inst_remove(st, a->id);
	ui_msg("Uninstalled", "The app was removed.");
}

/* ------------------------------------------------------------ UI */

void app_store(void)
{
	Store *st = calloc(1, sizeof(Store));
	if (!st) { ui_error("Not enough memory."); return; }
	list_init(&st->list, 0, 44, BOT_W, 162, 27);
	inst_load(st);
	if (load_local(st) < 0) { st->napps = 0; snprintf(st->source, sizeof st->source, "no catalog"); }
	rebuild_view(st);

	Btn bInstall = { 0, 208, 80, 32, "Install" }, bRemove = { 80, 208, 80, 32, "Remove" };
	Btn bRefresh = { 160, 208, 80, 32, "Refresh" }, bClear = { 240, 208, 80, 32, "Clear" };
	Btn bSearch = { 0, 20, BOT_W, 24, "" };

	gfx_fade_in();
	while (ui_running()) {
		ui_poll();
		list_update(&st->list);
		StoreApp *cur = st->nview ? &st->apps[st->view[st->list.sel]] : NULL;
		int s = cur ? app_state(st, cur) : ST_NONE;

		if (ui_close_tapped() || (IN.kd & KEY_B)) break;
		if (btn_tap(&bSearch)) {
			char q[48];
			if (ui_input("Search apps", st->filter, q, sizeof q, 1)) { snprintf(st->filter, sizeof st->filter, "%s", q); rebuild_view(st); st->list.sel = 0; st->list.scroll = 0; }
			continue;
		}
		if (btn_tap(&bClear)) { st->filter[0] = 0; rebuild_view(st); st->list.sel = 0; st->list.scroll = 0; continue; }
		if (btn_tap(&bRefresh)) { do_refresh(st); gfx_fade_in(); continue; }
		if (cur && (btn_tap(&bInstall) || st->list.act >= 0)) { do_install(st, cur); gfx_fade_in(); continue; }
		if (cur && btn_tap(&bRemove)) { do_uninstall(st, cur); gfx_fade_in(); continue; }
		if (cur) ensure_icon(st, cur);

		gfx_frame_begin();
		/* ---- top: details ---- */
		gx_vgrad(&TOP, 0, 0, TOP_W, SCR_H, C_BG, RGB(14,15,19));
		gx_fill(&TOP, 0, 0, TOP_W, 22, C_BAR);
		gx_fill(&TOP, 0, 21, TOP_W, 1, th_accent());
		gx_text(&TOP, 8, 6, "CollingOS Store", C_TEXT);
		char hdr[96];
		snprintf(hdr, sizeof hdr, "%d apps - %s", st->napps, st->source);
		gx_text(&TOP, TOP_W - 8 - gx_text_w(hdr), 6, hdr, C_DIM);
		if (cur) {
			if (st->icon) gx_rgba(&TOP, 14, 32, 56, 56, st->icon, st->iw, st->ih);
			else ui_letter_icon(&TOP, cur->name, 14, 32, 56);
			gx_text_n(&TOP, 82, 34, cur->name, TOP_W - 90, C_TEXT);
			char l[96];
			snprintf(l, sizeof l, "by %s  -  %s", cur->author, cur->cat);
			gx_text_n(&TOP, 82, 50, l, TOP_W - 90, C_DIM);
			snprintf(l, sizeof l, "Version: %s", cur->ver);
			gx_text(&TOP, 82, 64, l, C_DIM);
			const char *sl = s == ST_NONE ? "Not installed" : s == ST_INSTALLED ? "Installed" : s == ST_UPDATE ? "Update available" : "Present (not from CollingOS)";
			gx_text(&TOP, 82, 77, sl, s == ST_NONE ? C_DIM : (s == ST_UPDATE ? C_WARN : C_OK));
			gx_fill(&TOP, 10, 104, TOP_W - 20, 1, C_PAN2);
			gx_wrap(&TOP, 14, 112, TOP_W - 28, 6, cur->desc, C_TEXT);
			char ps[260], dir[64], path[128];
			app_path(cur, dir, sizeof dir, path, sizeof path);
			snprintf(ps, sizeof ps, "Installs to %s", path);
			gx_text_n(&TOP, 14, 200, ps, TOP_W - 28, C_DIM);
			gx_text_n(&TOP, 14, 214, cur->url, TOP_W - 28, RGB(100,106,120));
		} else {
			gx_text(&TOP, 14, 60, st->napps ? "No apps match your search." : "No catalog available. Press Refresh.", C_DIM);
		}

		/* ---- bottom: list ---- */
		gx_fill(&BOT, 0, 0, BOT_W, SCR_H, C_BG);
		for (int i = list_first(&st->list); i < st->nview; i++) {
			int ry = list_row_y(&st->list, i);
			if (ry >= st->list.y + st->list.h) break;
			const StoreApp *a = &st->apps[st->view[i]];
			if (i == st->list.sel) gx_fill(&BOT, 0, ry, BOT_W, st->list.row_h, th_accent());
			else if (i & 1) gx_fill(&BOT, 0, ry, BOT_W, st->list.row_h, RGB(27,30,38));
			ui_letter_icon(&BOT, a->name, 4, ry + 2, 23);
			gx_text_n(&BOT, 34, ry + 8, a->name, 190, C_TEXT);
			int sa = app_state(st, a);
			const char *tag = sa == ST_NONE ? "" : sa == ST_UPDATE ? "update" : "installed";
			gx_text(&BOT, BOT_W - 6 - gx_text_w(tag), ry + 8, tag, sa == ST_UPDATE ? C_WARN : C_OK);
		}
		ui_titlebar("Store");
		gx_fill(&BOT, 0, 20, BOT_W, 24, C_PANEL);
		char sq[80];
		if (st->filter[0]) snprintf(sq, sizeof sq, "Search: %s", st->filter); else snprintf(sq, sizeof sq, "Tap here to search apps...");
		gx_text_n(&BOT, 8, 26, sq, BOT_W - 16, st->filter[0] ? C_TEXT : C_DIM);
		gx_fill(&BOT, 0, 206, BOT_W, 34, C_BAR);
		bInstall.label = s == ST_UPDATE ? "Update" : (s == ST_NONE ? "Install" : "Reinstall");
		btn_draw(&BOT, &bInstall, cur != NULL);
		btn_draw(&BOT, &bRemove, cur != NULL && s != ST_NONE);
		btn_draw(&BOT, &bRefresh, 1);
		btn_draw(&BOT, &bClear, 1);
		gfx_frame_end();
	}
	free(st->icon);
	free(st->apps);
	free(st);
}

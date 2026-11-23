#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include "apps.h"

#define MAX_ENTRIES 1000
#define NAME_MAX_LEN 256

typedef struct { char name[NAME_MAX_LEN]; int dir; } Entry;

static const char *PROTECTED[] = {
	"sdmc:/Nintendo 3DS", "sdmc:/luma", "sdmc:/boot.firm", "sdmc:/boot.3dsx", "sdmc:/boot.cfg",
	"sdmc:/gm9", "sdmc:/payloads", "sdmc:/3ds/CollingOS", "sdmc:/3ds/CollingOS.3dsx",
	NULL
};

int fs_protected(const char *path)
{
	if (strstr(path, "..")) return 1;
	if (strncmp(path, "sdmc:/", 6) != 0) return 1;
	if (!path[6]) return 1; /* the SD root itself */
	for (int i = 0; PROTECTED[i]; i++) {
		size_t n = strlen(PROTECTED[i]);
		if (strncasecmp(path, PROTECTED[i], n) == 0 && (path[n] == 0 || path[n] == '/')) {
			/* CollingOS's own Downloads is fine; only the listed trees are locked */
			return 1;
		}
	}
	return 0;
}

int has_ext(const char *name, const char *exts)
{
	if (!exts) return 1;
	const char *dot = strrchr(name, '.');
	if (!dot || !dot[1]) return 0;
	dot++;
	size_t dl = strlen(dot);
	const char *p = exts;
	while (*p) {
		const char *e = strchr(p, ';');
		size_t l = e ? (size_t)(e - p) : strlen(p);
		if (l == dl && strncasecmp(dot, p, l) == 0) return 1;
		if (!e) break;
		p = e + 1;
	}
	return 0;
}

static int dir_writable(const char *cwd) { return !strcmp(cwd, "sdmc:/") || !fs_protected(cwd); }

static int cmp_entry(const void *a, const void *b)
{
	const Entry *x = a, *y = b;
	if (x->dir != y->dir) return y->dir - x->dir;
	return strcasecmp(x->name, y->name);
}

static void join(char *out, size_t n, const char *dir, const char *name)
{
	size_t l = strlen(dir);
	snprintf(out, n, "%s%s%s", dir, (l && dir[l - 1] == '/') ? "" : "/", name);
}

static void parent_of(char *path)
{
	size_t l = strlen(path);
	while (l > 6 && path[l - 1] == '/') path[--l] = 0;
	char *s = strrchr(path, '/');
	if (!s || s < path + 6) { strcpy(path, "sdmc:/"); return; }
	s[1] = 0;
	if (strlen(path) > 6) path[strlen(path) - 1] = 0;
}

/* ------------------------------------------------------------ copy / delete */

static int g_cancel = 0;

static int copy_file(const char *src, const char *dst, const char *label)
{
	FILE *in = fopen(src, "rb");
	if (!in) return -1;
	FILE *out = fopen(dst, "wb");
	if (!out) { fclose(in); return -2; }
	const size_t BUF = 64 * 1024;
	char *buf = malloc(BUF);
	if (!buf) { fclose(in); fclose(out); return -3; }
	fseek(in, 0, SEEK_END);
	long total = ftell(in);
	fseek(in, 0, SEEK_SET);
	long done = 0;
	int rc = 0;
	size_t n;
	while ((n = fread(buf, 1, BUF, in)) > 0) {
		if (fwrite(buf, 1, n, out) != n) { rc = -4; break; }
		done += (long)n;
		if (ui_progress("Copying", label, dst, total > 0 ? (int)((long long)done * 100 / total) : -1)) { g_cancel = 1; rc = -5; break; }
	}
	free(buf);
	fclose(in);
	if (fclose(out) != 0 && rc == 0) rc = -4;
	if (rc) remove(dst);
	return rc;
}

static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

static int copy_tree(const char *src, const char *dst, int depth)
{
	if (depth > 12) return -9;
	if (is_dir(src)) {
		/* never copy a folder into itself */
		size_t l = strlen(src);
		if (strncmp(dst, src, l) == 0 && (dst[l] == '/' || dst[l] == 0)) return -8;
		if (mkdir(dst, 0777) != 0 && !is_dir(dst)) return -2;
		DIR *d = opendir(src);
		if (!d) return -1;
		struct dirent *e;
		int rc = 0;
		while (rc == 0 && (e = readdir(d))) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			char s[512], t[512];
			join(s, sizeof s, src, e->d_name);
			join(t, sizeof t, dst, e->d_name);
			rc = copy_tree(s, t, depth + 1);
		}
		closedir(d);
		return rc;
	}
	return copy_file(src, dst, src);
}

static int delete_tree(const char *p, int depth)
{
	if (depth > 12 || fs_protected(p)) return -9;
	if (is_dir(p)) {
		DIR *d = opendir(p);
		if (!d) return -1;
		struct dirent *e;
		int rc = 0;
		while (rc == 0 && (e = readdir(d))) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			char c[512];
			join(c, sizeof c, p, e->d_name);
			rc = delete_tree(c, depth + 1);
		}
		closedir(d);
		if (rc) return rc;
		return rmdir(p);
	}
	return remove(p);
}

/* ------------------------------------------------------------ state */

typedef struct {
	char cwd[512];
	Entry *ents;
	int n, truncated;
	List list;
	char clip[512];
	int clip_cut;          /* 0 copy, 1 move */
	int pick;
	const char *exts;
	char selpath[512];
	unsigned long long selsize;
	long selmtime;
	int selinfo_for;       /* index the cached info belongs to */
} FM;

static void fm_load(FM *f)
{
	f->n = 0; f->truncated = 0;
	DIR *d = opendir(f->cwd);
	if (!d) { ui_error("Cannot open this folder."); parent_of(f->cwd); d = opendir(f->cwd); if (!d) return; }
	struct dirent *e;
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		if (f->n >= MAX_ENTRIES) { f->truncated = 1; break; }
		int dir = e->d_type == DT_DIR;
		if (f->pick && !dir && !has_ext(e->d_name, f->exts)) continue;
		snprintf(f->ents[f->n].name, NAME_MAX_LEN, "%s", e->d_name);
		f->ents[f->n].dir = dir;
		f->n++;
	}
	closedir(d);
	qsort(f->ents, f->n, sizeof(Entry), cmp_entry);
	list_set_n(&f->list, f->n);
	f->list.sel = 0; f->list.scroll = 0;
	f->selinfo_for = -1;
}

static const char *type_label(const Entry *e)
{
	if (e->dir) return "Folder";
	if (has_ext(e->name, "txt;md;cfg;ini;log;json;xml;csv;c;h;cpp;py;lua;js;html")) return "Text";
	if (has_ext(e->name, "png;jpg;jpeg;bmp")) return "Image";
	if (has_ext(e->name, "wav;mp3")) return "Audio";
	if (has_ext(e->name, "3dsx")) return "3DS app";
	if (has_ext(e->name, "cia")) return "CIA";
	if (has_ext(e->name, "zip;7z;rar")) return "Archive";
	return "File";
}

static void sel_info(FM *f)
{
	if (f->n == 0 || f->list.sel >= f->n) return;
	if (f->selinfo_for == f->list.sel) return;
	f->selinfo_for = f->list.sel;
	join(f->selpath, sizeof f->selpath, f->cwd, f->ents[f->list.sel].name);
	struct stat st;
	if (stat(f->selpath, &st) == 0) { f->selsize = st.st_size; f->selmtime = (long)st.st_mtime; }
	else { f->selsize = 0; f->selmtime = 0; }
}

static void open_entry(FM *f, int i)
{
	if (i < 0 || i >= f->n) return;
	Entry *e = &f->ents[i];
	char p[512];
	join(p, sizeof p, f->cwd, e->name);
	if (e->dir) {
		snprintf(f->cwd, sizeof f->cwd, "%s", p);
		fm_load(f);
		return;
	}
	if (f->pick) return; /* handled by caller */
	if (has_ext(e->name, "txt;md;cfg;ini;log;json;xml;csv;c;h;cpp;py;lua;js;html")) editor_open(p);
	else if (has_ext(e->name, "png;jpg;jpeg;bmp")) viewer_open(p);
	else if (has_ext(e->name, "wav;mp3")) player_open(p);
	else if (has_ext(e->name, "3dsx")) ui_msg("3DS app", "To run this app, close CollingOS and launch it from the Homebrew Launcher.");
	else ui_msg("Unknown file", "CollingOS has no built-in app for this file type.");
	gfx_fade_in();
}

static void do_info(FM *f)
{
	if (f->n == 0) return;
	sel_info(f);
	char sz[32], t[600], when[40] = "unknown";
	fmt_size(f->selsize, sz, sizeof sz);
	if (f->selmtime) { time_t tt = f->selmtime; struct tm *tm = gmtime(&tt); if (tm) strftime(when, sizeof when, "%Y-%m-%d %H:%M", tm); }
	Entry *e = &f->ents[f->list.sel];
	snprintf(t, sizeof t, "%s\nType: %s\nSize: %s\nModified: %s\n%s", f->selpath, type_label(e), e->dir ? "-" : sz, when,
	         fs_protected(f->selpath) ? "Protected: cannot be changed here." : "");
	ui_msg("File information", t);
}

static void do_delete(FM *f)
{
	if (f->n == 0) return;
	Entry *e = &f->ents[f->list.sel];
	char p[512];
	join(p, sizeof p, f->cwd, e->name);
	if (fs_protected(p)) { ui_error("This item is protected and cannot be deleted."); return; }
	char msg[400];
	snprintf(msg, sizeof msg, "Delete %s \"%s\"%s? This cannot be undone.", e->dir ? "folder" : "file", e->name, e->dir ? " and everything inside it" : "");
	if (!ui_confirm("Delete", msg)) return;
	int rc = delete_tree(p, 0);
	if (rc) ui_error("Could not delete (item in use, protected or SD card locked).");
	fm_load(f);
}

static void do_mkdir(FM *f)
{
	if (!dir_writable(f->cwd)) { ui_error("You cannot create items in this protected location."); return; }
	char name[120];
	if (!ui_input("New folder name", "", name, sizeof name, 0)) return;
	if (strpbrk(name, "/\\:*?\"<>|")) { ui_error("That name contains characters FAT32 does not allow."); return; }
	char p[512];
	join(p, sizeof p, f->cwd, name);
	if (exists(p)) { ui_error("An item with that name already exists."); return; }
	if (mkdir(p, 0777) != 0) ui_error("Could not create the folder.");
	fm_load(f);
}

static void do_rename(FM *f)
{
	if (f->n == 0) return;
	Entry *e = &f->ents[f->list.sel];
	char p[512];
	join(p, sizeof p, f->cwd, e->name);
	if (fs_protected(p)) { ui_error("This item is protected and cannot be renamed."); return; }
	char name[120];
	if (!ui_input("New name", e->name, name, sizeof name, 0)) return;
	if (strpbrk(name, "/\\:*?\"<>|")) { ui_error("That name contains characters FAT32 does not allow."); return; }
	char q[512];
	join(q, sizeof q, f->cwd, name);
	if (exists(q)) { ui_error("An item with that name already exists."); return; }
	if (rename(p, q) != 0) ui_error("Could not rename the item.");
	fm_load(f);
}

static void do_clip(FM *f, int cut)
{
	if (f->n == 0) return;
	Entry *e = &f->ents[f->list.sel];
	join(f->clip, sizeof f->clip, f->cwd, e->name);
	if (cut && fs_protected(f->clip)) { f->clip[0] = 0; ui_error("Protected items cannot be moved."); return; }
	f->clip_cut = cut;
}

static void do_paste(FM *f)
{
	if (!f->clip[0]) { ui_msg("Paste", "Nothing to paste. Use Copy or Move first."); return; }
	if (!dir_writable(f->cwd)) { ui_error("You cannot paste into this protected location."); return; }
	if (!exists(f->clip)) { ui_error("The source no longer exists."); f->clip[0] = 0; return; }
	const char *base = strrchr(f->clip, '/');
	base = base ? base + 1 : f->clip;
	char dst[512];
	join(dst, sizeof dst, f->cwd, base);
	if (!strcmp(dst, f->clip)) { ui_msg("Paste", "Source and destination are the same."); return; }
	if (exists(dst)) { ui_error("An item with that name already exists here."); return; }
	g_cancel = 0;
	int rc;
	if (f->clip_cut) {
		rc = rename(f->clip, dst);          /* same SD card: instant */
		if (rc != 0) {
			rc = copy_tree(f->clip, dst, 0);
			if (rc == 0) rc = delete_tree(f->clip, 0);
		}
		if (rc == 0) f->clip[0] = 0;
	} else {
		rc = copy_tree(f->clip, dst, 0);
	}
	if (rc) {
		if (g_cancel) { delete_tree(dst, 0); ui_msg("Cancelled", "The operation was cancelled."); }
		else if (rc == -8) ui_error("You cannot copy a folder into itself.");
		else ui_error("The operation failed (SD card full or locked?).");
	}
	fm_load(f);
}

/* ------------------------------------------------------------ UI */

static int run(FM *f, char *picked, int pickedlen)
{
	static const char *L1[4] = { "Up", "Open", "Copy", "Move" };
	static const char *L2[4] = { "Paste", "Delete", "New dir", "Rename" };
	Btn b1[4], b2[4];
	for (int i = 0; i < 4; i++) {
		b1[i] = (Btn){ i * 80, 180, 79, 29, f->pick ? (i == 0 ? "Up" : i == 1 ? "Select" : i == 2 ? "Cancel" : "") : L1[i] };
		b2[i] = (Btn){ i * 80, 211, 79, 29, L2[i] };
	}
	int result = 0;
	while (ui_running()) {
		ui_poll();
		list_update(&f->list);
		sel_info(f);
		int act = -1;
		if (ui_close_tapped()) break;
		if (IN.kd & KEY_B) {
			if (!strcmp(f->cwd, "sdmc:/")) { if (f->pick) break; }
			else { parent_of(f->cwd); fm_load(f); }
		}
		if (f->list.act >= 0) {
			Entry *e = &f->ents[f->list.act];
			if (f->pick && !e->dir) {
				join(picked, pickedlen, f->cwd, e->name);
				result = 1; break;
			}
			act = f->list.act; open_entry(f, act); if (g_quit) break; continue;
		}
		if (btn_tap(&b1[0])) { parent_of(f->cwd); fm_load(f); }
		if (btn_tap(&b1[1]) && f->n) {
			Entry *e = &f->ents[f->list.sel];
			if (f->pick && !e->dir) { join(picked, pickedlen, f->cwd, e->name); result = 1; break; }
			open_entry(f, f->list.sel);
			continue;
		}
		if (f->pick) {
			if (btn_tap(&b1[2])) break;
		} else {
			if (btn_tap(&b1[2])) do_clip(f, 0);
			if (btn_tap(&b1[3])) do_clip(f, 1);
			if (btn_tap(&b2[0])) do_paste(f);
			if (btn_tap(&b2[1])) do_delete(f);
			if (btn_tap(&b2[2])) do_mkdir(f);
			if (btn_tap(&b2[3])) do_rename(f);
		}

		/* ---- draw ---- */
		gfx_frame_begin();
		/* top screen: details */
		gx_vgrad(&TOP, 0, 0, TOP_W, SCR_H, C_BG, RGB(14,15,19));
		gx_fill(&TOP, 0, 0, TOP_W, 22, C_BAR);
		gx_fill(&TOP, 0, 21, TOP_W, 1, th_accent());
		gx_text(&TOP, 8, 6, f->pick ? "Choose a file" : "File Manager", C_TEXT);
		gx_text_n(&TOP, 8, 32, f->cwd, TOP_W - 16, th_accent());
		if (f->n) {
			Entry *e = &f->ents[f->list.sel];
			char sz[32], when[40] = "-", line[64];
			fmt_size(f->selsize, sz, sizeof sz);
			if (f->selmtime) { time_t tt = f->selmtime; struct tm *tm = gmtime(&tt); if (tm) strftime(when, sizeof when, "%Y-%m-%d %H:%M", tm); }
			gx_fill(&TOP, 12, 56, 376, 96, C_PANEL);
			gx_text2(&TOP, 22, 64, e->dir ? "[DIR]" : "[FILE]", e->dir ? RGB(255,214,110) : C_TEXT);
			gx_text_n(&TOP, 22, 92, e->name, 356, C_TEXT);
			snprintf(line, sizeof line, "Type: %s", type_label(e)); gx_text(&TOP, 22, 108, line, C_DIM);
			snprintf(line, sizeof line, "Size: %s", e->dir ? "-" : sz); gx_text(&TOP, 22, 121, line, C_DIM);
			snprintf(line, sizeof line, "Modified: %s", when); gx_text(&TOP, 22, 134, line, C_DIM);
			char p[512];
			join(p, sizeof p, f->cwd, e->name);
			if (fs_protected(p)) gx_text(&TOP, 230, 134, "PROTECTED", C_WARN);
		} else {
			gx_text(&TOP, 12, 60, "This folder is empty.", C_DIM);
		}
		char cl[600];
		if (f->clip[0]) snprintf(cl, sizeof cl, "%s: %s", f->clip_cut ? "Move" : "Copy", f->clip); else snprintf(cl, sizeof cl, "Clipboard empty");
		gx_text_n(&TOP, 12, 164, cl, 376, C_DIM);
		char cnt[64];
		snprintf(cnt, sizeof cnt, "%d item%s%s", f->n, f->n == 1 ? "" : "s", f->truncated ? " (list truncated)" : "");
		gx_text(&TOP, 12, 180, cnt, C_DIM);
		gx_text(&TOP, 12, 220, "D-pad: select   A: open   B: up", RGB(100,106,120));

		/* bottom: list */
		gx_fill(&BOT, 0, 0, BOT_W, SCR_H, C_BG);
		for (int i = list_first(&f->list); i < f->n; i++) {
			int ry = list_row_y(&f->list, i);
			if (ry >= f->list.y + f->list.h) break;
			if (i == f->list.sel) gx_fill(&BOT, 0, ry, BOT_W, f->list.row_h, th_accent());
			else if (i & 1) gx_fill(&BOT, 0, ry, BOT_W, f->list.row_h, RGB(27,30,38));
			Entry *e = &f->ents[i];
			gx_fill(&BOT, 6, ry + 5, 12, 11, e->dir ? RGB(240,196,80) : RGB(160,170,190));
			gx_text_n(&BOT, 24, ry + 6, e->name, BOT_W - 30, C_TEXT);
		}
		ui_titlebar(f->pick ? "Choose a file" : "File Manager");
		gx_fill(&BOT, 0, 178, BOT_W, 62, C_BAR);
		for (int i = 0; i < 4; i++) {
			if (f->pick && i == 3) continue;
			btn_draw(&BOT, &b1[i], 1);
			if (!f->pick) btn_draw(&BOT, &b2[i], 1);
		}
		gfx_frame_end();
	}
	return result;
}

static int fm_main(const char *start, int pick, const char *exts, char *out, int outlen)
{
	FM *f = calloc(1, sizeof(FM));
	if (!f) { ui_error("Not enough memory."); return 0; }
	f->ents = malloc(sizeof(Entry) * MAX_ENTRIES);
	if (!f->ents) { free(f); ui_error("Not enough memory."); return 0; }
	f->pick = pick;
	f->exts = exts;
	snprintf(f->cwd, sizeof f->cwd, "%s", start && strncmp(start, "sdmc:/", 6) == 0 ? start : "sdmc:/");
	list_init(&f->list, 0, 20, BOT_W, 158, 22);
	fm_load(f);
	gfx_fade_in();
	int r = run(f, out, outlen);
	free(f->ents);
	free(f);
	return r;
}

void app_files(void) { fm_main("sdmc:/", 0, NULL, NULL, 0); }
void files_open_at(const char *dir) { fm_main(dir, 0, NULL, NULL, 0); }

int file_pick(const char *start_dir, const char *exts, char *out, int outlen)
{
	return fm_main(start_dir, 1, exts, out, outlen);
}

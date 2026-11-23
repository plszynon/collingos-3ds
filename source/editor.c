#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "apps.h"

#define MAX_LINES 4000
#define MAX_FILE (128 * 1024)
#define TEXT_EXTS "txt;md;cfg;ini;log;json;xml;csv;c;h;cpp;py;lua;js;html"

typedef struct {
	char path[512];
	char **lines;
	int n;
	int dirty;
	List list;
} Doc;

static void free_lines(Doc *d)
{
	for (int i = 0; i < d->n; i++) free(d->lines[i]);
	d->n = 0;
}

static int insert_line(Doc *d, int at, const char *text)
{
	if (d->n >= MAX_LINES) return -1;
	char *s = strdup(text);
	if (!s) return -1;
	memmove(&d->lines[at + 1], &d->lines[at], sizeof(char *) * (d->n - at));
	d->lines[at] = s;
	d->n++;
	return 0;
}

static int load_doc(Doc *d, const char *path)
{
	free_lines(d);
	snprintf(d->path, sizeof d->path, "%s", path);
	d->dirty = 0;
	FILE *f = fopen(path, "rb");
	if (!f) {                       /* new file */
		insert_line(d, 0, "");
		list_set_n(&d->list, d->n);
		return 0;
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz < 0 || sz > MAX_FILE) { fclose(f); ui_error("This file is larger than 128 KB, which the editor cannot open on the 3DS."); return -1; }
	char *buf = malloc(sz + 1);
	if (!buf) { fclose(f); ui_error("Not enough memory."); return -1; }
	size_t got = fread(buf, 1, sz, f);
	fclose(f);
	buf[got] = 0;
	if (memchr(buf, 0, got)) { free(buf); ui_error("This looks like a binary file and cannot be edited as text."); return -1; }
	char *p = buf;
	while (1) {
		char *e = strchr(p, '\n');
		if (e) *e = 0;
		char *cr = strchr(p, '\r'); if (cr && !cr[1]) *cr = 0;
		if (insert_line(d, d->n, p) != 0) { free(buf); ui_error("The file has too many lines."); free_lines(d); return -1; }
		if (!e) break;
		p = e + 1;
		if (!*p) break;    /* trailing newline */
	}
	free(buf);
	if (d->n == 0) insert_line(d, 0, "");
	list_set_n(&d->list, d->n);
	d->list.sel = 0; d->list.scroll = 0;
	return 0;
}

static int save_doc(Doc *d)
{
	if (fs_protected(d->path)) { ui_error("This location is protected. Use a different path."); return -1; }
	char tmp[520];
	snprintf(tmp, sizeof tmp, "%s.tmp", d->path);
	FILE *f = fopen(tmp, "wb");
	if (!f) { ui_error("Cannot write to the SD card."); return -1; }
	for (int i = 0; i < d->n; i++) {
		fputs(d->lines[i], f);
		if (i < d->n - 1) fputc('\n', f);
	}
	if (d->n > 0) fputc('\n', f);
	int bad = ferror(f);
	if (fclose(f) != 0) bad = 1;
	if (bad) { remove(tmp); ui_error("Write error. Is the SD card full or locked?"); return -1; }
	remove(d->path);
	if (rename(tmp, d->path) != 0) { ui_error("Could not replace the old file."); return -1; }
	d->dirty = 0;
	return 0;
}

static int confirm_discard(Doc *d)
{
	if (!d->dirty) return 1;
	return ui_confirm("Unsaved changes", "Discard your unsaved changes?");
}

static void run_editor(Doc *d)
{
	Btn b[5] = {
		{ 0, 208, 64, 32, "Edit" }, { 64, 208, 64, 32, "Add" }, { 128, 208, 64, 32, "Del" },
		{ 192, 208, 64, 32, "Save" }, { 256, 208, 64, 32, "Open" }
	};
	gfx_fade_in();
	while (ui_running()) {
		ui_poll();
		list_update(&d->list);
		if (ui_close_tapped() || (IN.kd & KEY_B)) { if (confirm_discard(d)) break; continue; }
		if (d->list.act >= 0 || btn_tap(&b[0])) {
			char *cur = d->lines[d->list.sel];
			if (strlen(cur) > 480) ui_error("This line is too long to edit on the 3DS keyboard.");
			else {
				char t[520];
				if (ui_input("Edit line", cur, t, sizeof t, 1)) {
					char *s = strdup(t);
					if (s) { free(d->lines[d->list.sel]); d->lines[d->list.sel] = s; d->dirty = 1; }
				}
			}
			gfx_fade_in();
			continue;
		}
		if (btn_tap(&b[1])) {
			char t[520];
			if (ui_input("New line (inserted below)", "", t, sizeof t, 1)) {
				if (insert_line(d, d->list.sel + 1, t) == 0) { d->dirty = 1; list_set_n(&d->list, d->n); d->list.sel++; list_show_sel(&d->list); }
				else ui_error("Cannot add more lines (limit reached or out of memory).");
			}
			gfx_fade_in();
			continue;
		}
		if (btn_tap(&b[2]) && d->n > 0) {
			free(d->lines[d->list.sel]);
			memmove(&d->lines[d->list.sel], &d->lines[d->list.sel + 1], sizeof(char *) * (d->n - d->list.sel - 1));
			d->n--;
			if (d->n == 0) insert_line(d, 0, "");
			d->dirty = 1;
			list_set_n(&d->list, d->n);
		}
		if (btn_tap(&b[3]) || (IN.kd & KEY_START)) {
			if (save_doc(d) == 0) ui_msg("Saved", d->path);
			gfx_fade_in();
			continue;
		}
		if (btn_tap(&b[4])) {
			if (!confirm_discard(d)) continue;
			char p[512];
			if (file_pick("sdmc:/", TEXT_EXTS, p, sizeof p)) load_doc(d, p);
			gfx_fade_in();
			continue;
		}

		gfx_frame_begin();
		gx_vgrad(&TOP, 0, 0, TOP_W, SCR_H, RGB(18,20,26), RGB(12,13,17));
		gx_fill(&TOP, 0, 0, TOP_W, 22, C_BAR);
		gx_fill(&TOP, 0, 21, TOP_W, 1, th_accent());
		char title[300];
		snprintf(title, sizeof title, "%s%s", d->dirty ? "* " : "", d->path);
		gx_text_n(&TOP, 8, 6, title, TOP_W - 100, C_TEXT);
		char pos[40];
		snprintf(pos, sizeof pos, "Ln %d/%d", d->list.sel + 1, d->n);
		gx_text(&TOP, TOP_W - 8 - gx_text_w(pos), 6, pos, C_DIM);
		int first = d->list.sel - 7;
		if (first < 0) first = 0;
		for (int i = 0; i < 16 && first + i < d->n; i++) {
			int ln = first + i, y = 26 + i * 13;
			if (ln == d->list.sel) gx_fill(&TOP, 0, y - 1, TOP_W, 13, RGB(40,55,85));
			char num[12];
			snprintf(num, sizeof num, "%4d", ln + 1);
			gx_text(&TOP, 2, y, num, RGB(100,106,120));
			gx_text_n(&TOP, 34, y, d->lines[ln], TOP_W - 38, C_TEXT);
		}
		gx_fill(&BOT, 0, 0, BOT_W, SCR_H, C_BG);
		for (int i = list_first(&d->list); i < d->n; i++) {
			int ry = list_row_y(&d->list, i);
			if (ry >= d->list.y + d->list.h) break;
			if (i == d->list.sel) gx_fill(&BOT, 0, ry, BOT_W, d->list.row_h, th_accent());
			else if (i & 1) gx_fill(&BOT, 0, ry, BOT_W, d->list.row_h, RGB(27,30,38));
			char num[12];
			snprintf(num, sizeof num, "%3d", i + 1);
			gx_text(&BOT, 2, ry + 4, num, C_DIM);
			gx_text_n(&BOT, 28, ry + 4, d->lines[i][0] ? d->lines[i] : "", BOT_W - 32, C_TEXT);
		}
		ui_titlebar("Text Editor");
		gx_fill(&BOT, 0, 206, BOT_W, 34, C_BAR);
		for (int i = 0; i < 5; i++) btn_draw(&BOT, &b[i], 1);
		gfx_frame_end();
	}
}

static void edit_path(const char *path)
{
	Doc *d = calloc(1, sizeof(Doc));
	if (!d) { ui_error("Not enough memory."); return; }
	d->lines = calloc(MAX_LINES + 1, sizeof(char *));
	if (!d->lines) { free(d); ui_error("Not enough memory."); return; }
	list_init(&d->list, 0, 20, BOT_W, 186, 18);
	if (load_doc(d, path) == 0) run_editor(d);
	free_lines(d);
	free(d->lines);
	free(d);
}

void editor_open(const char *path) { edit_path(path); }

void app_editor(void)
{
	char p[512];
	if (ui_confirm("Text Editor", "Open an existing file? Yes = choose a file, No = create a new one.")) {
		if (!file_pick("sdmc:/", TEXT_EXTS, p, sizeof p)) return;
		edit_path(p);
	} else {
		char name[100];
		if (!ui_input("New file name (e.g. notes.txt)", "notes.txt", name, sizeof name, 0)) return;
		if (strpbrk(name, "/\\:*?\"<>|")) { ui_error("That name contains characters FAT32 does not allow."); return; }
		mkdir("sdmc:/Documents", 0777);
		snprintf(p, sizeof p, "sdmc:/Documents/%s", name);
		struct stat st;
		if (stat(p, &st) == 0) { ui_error("A file with that name already exists in sdmc:/Documents."); return; }
		edit_path(p);
	}
}

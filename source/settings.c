#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "settings.h"

Settings S;
#define CFG_FILE DIR_ROOT "/settings.cfg"

void settings_defaults(void)
{
	S.wallpaper = 0;
	S.accent = 0;
	S.clock24 = 1;
	S.anim = 1;
	S.volume = 80;
	snprintf(S.store_url, sizeof S.store_url, "%s", DEFAULT_CATALOG);
}

void fs_make_dirs(void)
{
	mkdir("sdmc:/3ds", 0777);
	mkdir(DIR_ROOT, 0777);
	mkdir(DIR_CACHE, 0777);
	mkdir(DIR_DL, 0777);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void settings_load(void)
{
	settings_defaults();
	FILE *f = fopen(CFG_FILE, "r");
	if (!f) return;
	char line[300];
	while (fgets(line, sizeof line, f)) {
		char *nl = strpbrk(line, "\r\n");
		if (nl) *nl = 0;
		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq = 0;
		const char *k = line, *v = eq + 1;
		if (!strcmp(k, "wallpaper")) S.wallpaper = clampi(atoi(v), 0, 3);
		else if (!strcmp(k, "accent")) S.accent = clampi(atoi(v), 0, 3);
		else if (!strcmp(k, "clock24")) S.clock24 = atoi(v) ? 1 : 0;
		else if (!strcmp(k, "anim")) S.anim = atoi(v) ? 1 : 0;
		else if (!strcmp(k, "volume")) S.volume = clampi(atoi(v), 0, 100);
		else if (!strcmp(k, "store_url")) snprintf(S.store_url, sizeof S.store_url, "%s", v);
	}
	fclose(f);
}

void settings_save(void)
{
	/* write to a temp file first so a power loss never corrupts the old settings */
	FILE *f = fopen(CFG_FILE ".tmp", "w");
	if (!f) return;
	fprintf(f, "wallpaper=%d\naccent=%d\nclock24=%d\nanim=%d\nvolume=%d\nstore_url=%s\n",
	        S.wallpaper, S.accent, S.clock24, S.anim, S.volume, S.store_url);
	int bad = ferror(f);
	fclose(f);
	if (bad) { remove(CFG_FILE ".tmp"); return; }
	remove(CFG_FILE);
	rename(CFG_FILE ".tmp", CFG_FILE);
}

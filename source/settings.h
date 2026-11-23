#pragma once

#define DIR_ROOT "sdmc:/3ds/CollingOS"
#define DIR_CACHE "sdmc:/3ds/CollingOS/cache"
#define DIR_DL "sdmc:/Downloads"
#define DEFAULT_CATALOG "https://raw.githubusercontent.com/plszynon/collingos-3ds/main/catalog/store.txt"

typedef struct {
	int wallpaper;   /* 0..3 */
	int accent;      /* 0..3 */
	int clock24;     /* 0/1 */
	int anim;        /* 0/1 */
	int volume;      /* 0..100 */
	char store_url[200];
} Settings;

extern Settings S;

void settings_defaults(void);
void settings_load(void);
void settings_save(void);
void fs_make_dirs(void);

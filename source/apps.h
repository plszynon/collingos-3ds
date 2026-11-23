#pragma once
#include "ui.h"

/* Every app is a blocking loop that owns both screens until the user closes it. */
void app_files(void);
void files_open_at(const char *dir);
void app_store(void);
void app_settings(void);
void app_calc(void);
void app_images(void);
void app_music(void);
void app_editor(void);
void app_downloads(void);
void app_sysinfo(void);

/* open specific things (used by the file manager) */
void editor_open(const char *path);
void viewer_open(const char *path);
void player_open(const char *path);

/* pick a file with the file manager UI; exts = ";"-separated lower-case list or NULL for any */
int  file_pick(const char *start_dir, const char *exts, char *out, int outlen);

/* safety: returns 1 if the path must never be modified by CollingOS */
int  fs_protected(const char *path);
int  has_ext(const char *name, const char *exts);   /* case-insensitive match against "png;jpg" */

#pragma once
#include <3ds.h>

#define TOP_W 400
#define BOT_W 320
#define SCR_H 240
#define LINE_H 13

typedef struct { u8 *fb; int w; } Surf;
extern Surf TOP, BOT;

#define RGB(r,g,b) (((u32)(r)<<16)|((u32)(g)<<8)|(u32)(b))

void gfx_frame_begin(void);
void gfx_frame_end(void);
void gfx_fade_in(void);

void gx_fill(Surf *s, int x, int y, int w, int h, u32 c);
void gx_rect(Surf *s, int x, int y, int w, int h, u32 c);
void gx_vgrad(Surf *s, int x, int y, int w, int h, u32 c1, u32 c2);
void gx_blend(Surf *s, int x, int y, int w, int h, u32 c, int alpha);
void gx_text(Surf *s, int x, int y, const char *t, u32 c);
void gx_text2(Surf *s, int x, int y, const char *t, u32 c);
void gx_text_n(Surf *s, int x, int y, const char *t, int maxw, u32 c);
int  gx_text_w(const char *t);
void gx_rgba(Surf *s, int x, int y, int dw, int dh, const u8 *rgba, int sw, int sh);
/* wraps text into lines of maxw pixels; returns number of lines drawn (max maxlines) */
int  gx_wrap(Surf *s, int x, int y, int maxw, int maxlines, const char *t, u32 c);
void gx_rgba_crop(Surf *s, int x, int y, int dw, int dh, const u8 *rgba, int stride_w, int sx, int sy, int sw, int sh);

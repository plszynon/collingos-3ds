#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <malloc.h>
#include <3ds.h>
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"
#include "apps.h"

#define AUDIO_EXTS "wav;mp3"
#define NBUF 4
#define CHUNK 4096          /* frames per DSP buffer */
#define MAX_TRACKS 300
#define INBUF 16384

typedef struct {
	int kind;               /* 0 wav, 1 mp3 */
	FILE *f;
	int ch, rate;
	long size;              /* file size */
	long data_start, data_len, data_pos;   /* wav */
	mp3dec_t dec;
	uint8_t *inbuf; int inlen, inpos, eof;
	mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
	int pcm_n, pcm_pos;
	long file_pos;
	int kbps;
} Src;

static unsigned le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
static unsigned le16(const uint8_t *p) { return p[0] | (p[1] << 8); }

static void src_close(Src *s)
{
	if (s->f) fclose(s->f);
	free(s->inbuf);
	memset(s, 0, sizeof *s);
}

static int open_wav(Src *s, const char **err)
{
	uint8_t h[12], ck[8];
	if (fread(h, 1, 12, s->f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { *err = "Not a valid WAV file."; return -1; }
	int have_fmt = 0;
	while (fread(ck, 1, 8, s->f) == 8) {
		unsigned len = le32(ck + 4);
		if (!memcmp(ck, "fmt ", 4)) {
			uint8_t fm[16];
			if (len < 16 || fread(fm, 1, 16, s->f) != 16) { *err = "Damaged WAV header."; return -1; }
			unsigned fmt = le16(fm), ch = le16(fm + 2), bits = le16(fm + 14);
			s->rate = (int)le32(fm + 4);
			s->ch = (int)ch;
			if (fmt != 1 || bits != 16 || ch < 1 || ch > 2) { *err = "Only 16-bit PCM WAV (mono/stereo) is supported."; return -1; }
			fseek(s->f, (long)len - 16 + (len & 1), SEEK_CUR);
			have_fmt = 1;
		} else if (!memcmp(ck, "data", 4)) {
			if (!have_fmt) { *err = "WAV data before format."; return -1; }
			s->data_start = ftell(s->f);
			s->data_len = len;
			if (s->data_start + s->data_len > s->size) s->data_len = s->size - s->data_start;
			s->data_pos = 0;
			if (s->rate < 8000 || s->rate > 48000) { *err = "Unsupported sample rate."; return -1; }
			return 0;
		} else {
			fseek(s->f, (long)len + (len & 1), SEEK_CUR);
		}
	}
	*err = "No audio data found.";
	return -1;
}

static int mp3_fill(Src *s)
{
	if (s->inlen - s->inpos < 4096 && !s->eof) {
		memmove(s->inbuf, s->inbuf + s->inpos, s->inlen - s->inpos);
		s->inlen -= s->inpos;
		s->inpos = 0;
		size_t r = fread(s->inbuf + s->inlen, 1, INBUF - s->inlen, s->f);
		if (r == 0) s->eof = 1;
		s->inlen += (int)r;
	}
	return s->inlen - s->inpos;
}

/* decode until one frame with samples is available in s->pcm */
static int mp3_next(Src *s)
{
	for (int guard = 0; guard < 64; guard++) {
		if (mp3_fill(s) <= 0) return 0;
		mp3dec_frame_info_t info;
		int smp = mp3dec_decode_frame(&s->dec, s->inbuf + s->inpos, s->inlen - s->inpos, s->pcm, &info);
		if (info.frame_bytes == 0) {
			if (s->eof) return 0;
			s->inpos = s->inlen;      /* garbage: drop this block */
			continue;
		}
		s->inpos += info.frame_bytes;
		s->file_pos += info.frame_bytes;
		if (smp > 0) {
			if (!s->ch) { s->ch = info.channels; s->rate = info.hz; s->kbps = info.bitrate_kbps; }
			s->pcm_n = smp;
			s->pcm_pos = 0;
			return 1;
		}
	}
	return 0;
}

static int open_mp3(Src *s, const char **err)
{
	s->inbuf = malloc(INBUF);
	if (!s->inbuf) { *err = "Not enough memory."; return -1; }
	mp3dec_init(&s->dec);
	if (!mp3_next(s) || s->ch < 1 || s->ch > 2) { *err = "Could not decode this MP3."; return -1; }
	if (s->rate < 8000 || s->rate > 48000) { *err = "Unsupported sample rate."; return -1; }
	return 0;
}

static int src_open(Src *s, const char *path, const char **err)
{
	memset(s, 0, sizeof *s);
	s->f = fopen(path, "rb");
	if (!s->f) { *err = "Cannot open the file."; return -1; }
	fseek(s->f, 0, SEEK_END);
	s->size = ftell(s->f);
	fseek(s->f, 0, SEEK_SET);
	int rc;
	if (has_ext(path, "wav")) { s->kind = 0; rc = open_wav(s, err); }
	else { s->kind = 1; rc = open_mp3(s, err); }
	if (rc != 0) src_close(s);
	return rc;
}

/* returns frames written */
static int src_read(Src *s, int16_t *out, int want)
{
	if (s->kind == 0) {
		long left = s->data_len - s->data_pos;
		long bytes = (long)want * s->ch * 2;
		if (bytes > left) bytes = left;
		if (bytes <= 0) return 0;
		size_t got = fread(out, 1, bytes, s->f);
		s->data_pos += (long)got;
		return (int)(got / (s->ch * 2));
	}
	int produced = 0;
	while (produced < want) {
		if (s->pcm_pos >= s->pcm_n) { if (!mp3_next(s)) break; }
		int take = s->pcm_n - s->pcm_pos;
		if (take > want - produced) take = want - produced;
		memcpy(out + produced * s->ch, s->pcm + s->pcm_pos * s->ch, (size_t)take * s->ch * 2);
		s->pcm_pos += take;
		produced += take;
	}
	return produced;
}

static int src_percent(const Src *s)
{
	if (s->kind == 0) return s->data_len > 0 ? (int)((long long)s->data_pos * 100 / s->data_len) : 0;
	return s->size > 0 ? (int)((long long)s->file_pos * 100 / s->size) : 0;
}

static int src_total_secs(const Src *s)
{
	if (s->kind == 0) return s->rate > 0 ? (int)(s->data_len / (s->ch * 2) / s->rate) : 0;
	return s->kbps > 0 ? (int)(s->size * 8 / (s->kbps * 1000L)) : 0;
}

/* ------------------------------------------------------------ player */

typedef struct {
	char dir[512];
	char (*names)[96];
	int n, cur;
	List list;
	Src src;
	int playing, paused, repeat, eof;
	int16_t *buf[NBUF];
	ndspWaveBuf wb[NBUF];
	long queued;
	int level;
	char status[120];
} Player;

static int cmp_name(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }

static void scan(Player *p)
{
	p->n = 0;
	DIR *d = opendir(p->dir);
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d)) && p->n < MAX_TRACKS) {
		if (e->d_type == DT_DIR || strlen(e->d_name) >= 96) continue;
		if (has_ext(e->d_name, AUDIO_EXTS)) snprintf(p->names[p->n++], 96, "%s", e->d_name);
	}
	closedir(d);
	qsort(p->names, p->n, 96, cmp_name);
	list_set_n(&p->list, p->n);
}

static void stop(Player *p)
{
	if (p->playing) {
		ndspChnWaveBufClear(0);
		ndspChnSetPaused(0, false);
	}
	src_close(&p->src);
	p->playing = p->paused = p->eof = 0;
	p->queued = 0;
	for (int i = 0; i < NBUF; i++) p->wb[i].status = NDSP_WBUF_DONE;
}

static int start(Player *p, int idx)
{
	stop(p);
	if (idx < 0 || idx >= p->n) return -1;
	char path[620];
	snprintf(path, sizeof path, "%s/%s", p->dir, p->names[idx]);
	const char *err = "";
	if (src_open(&p->src, path, &err) != 0) { snprintf(p->status, sizeof p->status, "%s", err); return -1; }
	ndspChnReset(0);
	ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
	ndspChnSetRate(0, (float)p->src.rate);
	ndspChnSetFormat(0, p->src.ch == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
	float mix[12] = { 0 };
	mix[0] = mix[1] = 1.0f;
	ndspChnSetMix(0, mix);
	p->cur = idx;
	p->list.sel = idx;
	list_show_sel(&p->list);
	p->playing = 1;
	p->status[0] = 0;
	return 0;
}

static void pump(Player *p)
{
	if (!p->playing || p->paused) return;
	int peak = 0;
	for (int i = 0; i < NBUF; i++) {
		if (p->wb[i].status != NDSP_WBUF_DONE || p->eof) continue;
		int n = src_read(&p->src, p->buf[i], CHUNK);
		if (n <= 0) { p->eof = 1; continue; }
		for (int k = 0; k < n * p->src.ch; k += 8) { int v = p->buf[i][k]; if (v < 0) v = -v; if (v > peak) peak = v; }
		p->wb[i].nsamples = n;
		p->wb[i].data_vaddr = p->buf[i];
		DSP_FlushDataCache(p->buf[i], (u32)n * p->src.ch * 2);
		ndspChnWaveBufAdd(0, &p->wb[i]);
		p->queued += n;
	}
	p->level = peak > p->level ? peak : p->level * 9 / 10;
	if (p->eof) {
		int busy = 0;
		for (int i = 0; i < NBUF; i++) if (p->wb[i].status != NDSP_WBUF_DONE) busy = 1;
		if (!busy) {
			int next = p->cur + 1;
			if (next >= p->n) { if (p->repeat) next = 0; else { stop(p); return; } }
			start(p, next);
		}
	}
}

static int elapsed_secs(const Player *p)
{
	if (!p->playing || p->src.rate <= 0) return 0;
	long pending = 0;
	for (int i = 0; i < NBUF; i++) if (p->wb[i].status != NDSP_WBUF_DONE) pending += p->wb[i].nsamples;
	long played = p->queued - pending;
	return played < 0 ? 0 : (int)(played / p->src.rate);
}

static void apply_volume(void) { ndspSetMasterVol(S.volume / 100.0f); }

static void run_player(Player *p)
{
	Btn bPrev = { 0, 174, 80, 32, "|<" }, bPlay = { 80, 174, 80, 32, "Play" }, bNext = { 160, 174, 80, 32, ">|" }, bOpen = { 240, 174, 80, 32, "Open" };
	Btn bVm = { 0, 208, 80, 32, "Vol -" }, bVp = { 80, 208, 80, 32, "Vol +" }, bRep = { 160, 208, 80, 32, "Repeat" }, bStop = { 240, 208, 80, 32, "Stop" };
	gfx_fade_in();
	while (ui_running()) {
		ui_poll();
		list_update(&p->list);
		if (ui_close_tapped() || (IN.kd & KEY_B)) break;
		if (p->list.act >= 0 && p->n) start(p, p->list.act);
		if (btn_tap(&bPlay) || (IN.kd & KEY_X)) {
			if (!p->playing) { if (p->n) start(p, p->list.sel); }
			else { p->paused = !p->paused; ndspChnSetPaused(0, p->paused); }
		}
		if ((btn_tap(&bNext) || (IN.kd & KEY_R)) && p->n) start(p, (p->cur + 1) % p->n);
		if ((btn_tap(&bPrev) || (IN.kd & KEY_L)) && p->n) start(p, (p->cur + p->n - 1) % p->n);
		if (btn_tap(&bStop)) stop(p);
		if (btn_tap(&bRep)) p->repeat = !p->repeat;
		if (btn_tap(&bVm)) { S.volume = S.volume >= 10 ? S.volume - 10 : 0; apply_volume(); }
		if (btn_tap(&bVp)) { S.volume = S.volume <= 90 ? S.volume + 10 : 100; apply_volume(); }
		if (btn_tap(&bOpen)) {
			char path[512];
			if (file_pick(p->dir[0] ? p->dir : "sdmc:/", AUDIO_EXTS, path, sizeof path)) {
				stop(p);
				snprintf(p->dir, sizeof p->dir, "%s", path);
				char *s = strrchr(p->dir, '/');
				const char *name = s + 1;
				if (s > p->dir + 5) *s = 0; else s[1] = 0;
				scan(p);
				for (int i = 0; i < p->n; i++) if (!strcasecmp(p->names[i], name)) { p->list.sel = i; start(p, i); }
			}
			gfx_fade_in();
			continue;
		}
		pump(p);

		gfx_frame_begin();
		gx_vgrad(&TOP, 0, 0, TOP_W, SCR_H, C_BG, RGB(14,15,19));
		gx_fill(&TOP, 0, 0, TOP_W, 22, C_BAR);
		gx_fill(&TOP, 0, 21, TOP_W, 1, th_accent());
		ui_logo(&TOP, 6, 5, 12);
		gx_text(&TOP, 24, 6, "Music Player", C_TEXT);
		if (p->playing) {
			gx_text_n(&TOP, 20, 50, p->names[p->cur], TOP_W - 40, C_TEXT);
			int e = elapsed_secs(p), t = src_total_secs(&p->src);
			char tm[48];
			if (t > 0) snprintf(tm, sizeof tm, "%d:%02d / ~%d:%02d", e / 60, e % 60, t / 60, t % 60);
			else snprintf(tm, sizeof tm, "%d:%02d", e / 60, e % 60);
			gx_text(&TOP, 20, 70, tm, C_DIM);
			gx_fill(&TOP, 20, 96, TOP_W - 40, 8, C_PAN2);
			gx_fill(&TOP, 20, 96, (TOP_W - 40) * src_percent(&p->src) / 100, 8, th_accent());
			char inf[64];
			snprintf(inf, sizeof inf, "%s  %d Hz  %s%s", p->src.kind ? "MP3" : "WAV", p->src.rate, p->src.ch == 2 ? "stereo" : "mono", p->paused ? "  [paused]" : "");
			gx_text(&TOP, 20, 112, inf, C_DIM);
			int bars = p->level * 18 / 32768; if (bars > 18) bars = 18;
			for (int i = 0; i < 18; i++) gx_fill(&TOP, 20 + i * 20, 190 - (i < bars ? 6 + i * 3 : 4), 16, i < bars ? 6 + i * 3 : 4, i < bars ? th_accent() : C_PAN2);
		} else {
			gx_text(&TOP, 20, 60, p->status[0] ? p->status : "Choose a track from the list.", p->status[0] ? C_ERR : C_DIM);
		}
		char vol[32];
		snprintf(vol, sizeof vol, "Volume %d%%   Repeat: %s", S.volume, p->repeat ? "on" : "off");
		gx_text(&TOP, 20, 216, vol, C_DIM);

		gx_fill(&BOT, 0, 0, BOT_W, SCR_H, C_BG);
		for (int i = list_first(&p->list); i < p->n; i++) {
			int ry = list_row_y(&p->list, i);
			if (ry >= p->list.y + p->list.h) break;
			if (i == p->list.sel) gx_fill(&BOT, 0, ry, BOT_W, p->list.row_h, th_accent());
			else if (i & 1) gx_fill(&BOT, 0, ry, BOT_W, p->list.row_h, RGB(27,30,38));
			gx_text_n(&BOT, 8, ry + 5, p->names[i], BOT_W - 24, C_TEXT);
			if (p->playing && i == p->cur) gx_text(&BOT, BOT_W - 14, ry + 5, ">", C_OK);
		}
		if (p->n == 0) gx_text(&BOT, 10, 40, "No MP3/WAV files here. Tap Open.", C_DIM);
		ui_titlebar("Music Player");
		gx_fill(&BOT, 0, 172, BOT_W, 68, C_BAR);
		bPlay.label = !p->playing ? "Play" : (p->paused ? "Resume" : "Pause");
		bRep.label = p->repeat ? "Repeat:on" : "Repeat";
		btn_draw(&BOT, &bPrev, 1); btn_draw(&BOT, &bPlay, 1); btn_draw(&BOT, &bNext, 1); btn_draw(&BOT, &bOpen, 1);
		btn_draw(&BOT, &bVm, 1); btn_draw(&BOT, &bVp, 1); btn_draw(&BOT, &bRep, 1); btn_draw(&BOT, &bStop, 1);
		gfx_frame_end();
	}
}

static void player_main(const char *path)
{
	Result rc = ndspInit();
	if (R_FAILED(rc)) {
		ui_error("DSP firmware not found. Dump it once with the DSP1 homebrew app (it creates sdmc:/3ds/dspfirm.cdc), then try again.");
		return;
	}
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	apply_volume();
	Player *p = calloc(1, sizeof(Player));
	if (p) p->names = malloc(96 * MAX_TRACKS);
	int ok = p && p->names;
	for (int i = 0; ok && i < NBUF; i++) {
		p->buf[i] = linearAlloc(CHUNK * 2 * sizeof(int16_t));
		if (!p->buf[i]) ok = 0;
		p->wb[i].status = NDSP_WBUF_DONE;
	}
	if (!ok) {
		ui_error("Not enough memory for the audio buffers.");
	} else {
		list_init(&p->list, 0, 20, BOT_W, 152, 24);
		if (path) {
			snprintf(p->dir, sizeof p->dir, "%s", path);
			char *s = strrchr(p->dir, '/');
			char name[96];
			snprintf(name, sizeof name, "%s", s + 1);
			if (s > p->dir + 5) *s = 0; else s[1] = 0;
			scan(p);
			for (int i = 0; i < p->n; i++) if (!strcasecmp(p->names[i], name)) start(p, i);
		} else {
			snprintf(p->dir, sizeof p->dir, "sdmc:/Music");
			scan(p);
		}
		run_player(p);
		stop(p);
	}
	if (p) {
		for (int i = 0; i < NBUF; i++) if (p->buf[i]) linearFree(p->buf[i]);
		free(p->names);
		free(p);
	}
	ndspExit();
}

void player_open(const char *path) { player_main(path); }
void app_music(void) { player_main(NULL); }

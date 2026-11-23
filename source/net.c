#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <malloc.h>
#include <sys/stat.h>
#include <3ds.h>
#include <curl/curl.h>
#include "net.h"
#include "ui.h"

#define SOC_SIZE 0x100000
#define HIST_FILE DIR_ROOT "/history.txt"
#define HIST_MAX 100

static u32 *soc_buf;
static int net_ready;
static int ac_ready;
static char ca_path[96];

int net_wifi_ok(void)
{
	if (!ac_ready) { if (R_FAILED(acInit())) return 0; ac_ready = 1; }
	u32 st = 0;
	if (R_FAILED(ACU_GetWifiStatus(&st))) return 0;
	return st != 0;
}

static int file_exists(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0;
}

int net_ensure(char *err, int errlen)
{
	if (net_ready) return 0;
	if (!net_wifi_ok()) {
		snprintf(err, errlen, "No Wi-Fi connection. Turn Wi-Fi on in the Home Menu and try again.");
		return -1;
	}
	if (file_exists("romfs:/cacert.pem")) snprintf(ca_path, sizeof ca_path, "romfs:/cacert.pem");
	else if (file_exists(DIR_ROOT "/cacert.pem")) snprintf(ca_path, sizeof ca_path, DIR_ROOT "/cacert.pem");
	else {
		snprintf(err, errlen, "HTTPS certificate bundle (cacert.pem) not found. Put it in sdmc:/3ds/CollingOS/.");
		return -1;
	}
	soc_buf = memalign(0x1000, SOC_SIZE);
	if (!soc_buf) { snprintf(err, errlen, "Out of memory (network buffer)."); return -1; }
	Result rc = socInit(soc_buf, SOC_SIZE);
	if (R_FAILED(rc)) {
		snprintf(err, errlen, "Network init failed (0x%08lX).", (unsigned long)rc);
		free(soc_buf); soc_buf = NULL;
		return -1;
	}
	curl_global_init(CURL_GLOBAL_DEFAULT);
	net_ready = 1;
	return 0;
}

void net_shutdown(void)
{
	if (net_ready) {
		curl_global_cleanup();
		socExit();
		free(soc_buf);
		soc_buf = NULL;
		net_ready = 0;
	}
	if (ac_ready) { acExit(); ac_ready = 0; }
}

static void curl_msg(int code, long http, char *err, int n)
{
	switch (code) {
	case CURLE_COULDNT_RESOLVE_HOST: snprintf(err, n, "Cannot find the server (DNS). Check your Wi-Fi."); break;
	case CURLE_COULDNT_CONNECT:      snprintf(err, n, "Cannot connect to the server."); break;
	case CURLE_HTTP_RETURNED_ERROR:  snprintf(err, n, "Server returned HTTP %ld%s.", http, http == 404 ? " (not found)" : ""); break;
	case CURLE_OPERATION_TIMEDOUT:   snprintf(err, n, "Connection timed out."); break;
	case CURLE_PEER_FAILED_VERIFICATION:
		snprintf(err, n, "Secure connection rejected. Check that the 3DS date and time are correct."); break;
	case CURLE_SSL_CONNECT_ERROR:    snprintf(err, n, "TLS handshake failed."); break;
	case CURLE_WRITE_ERROR:          snprintf(err, n, "Cannot write to the SD card (full or locked?)."); break;
	default: snprintf(err, n, "Network error %d: %s", code, curl_easy_strerror((CURLcode)code)); break;
	}
}

typedef struct {
	const char *title;
	const char *url;
	u64 last_ms, start_ms;
	unsigned long long limit;
	int cancelled, too_big;
} Prog;

static int xfer_cb(void *ud, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	(void)ultotal; (void)ulnow;
	Prog *p = ud;
	if (p->limit && (unsigned long long)dlnow > p->limit) { p->too_big = 1; return 1; }
	u64 now = osGetTime();
	if (now - p->last_ms < 80) return 0;
	p->last_ms = now;
	char l1[64], l2[96], a[24], b[24], sp[24];
	fmt_size((unsigned long long)dlnow, a, sizeof a);
	double secs = (now - p->start_ms) / 1000.0;
	fmt_size(secs > 0.2 ? (unsigned long long)(dlnow / secs) : 0, sp, sizeof sp);
	int pct = -1;
	if (dltotal > 0) {
		fmt_size((unsigned long long)dltotal, b, sizeof b);
		snprintf(l1, sizeof l1, "%s / %s", a, b);
		pct = (int)(dlnow * 100 / dltotal);
	} else {
		snprintf(l1, sizeof l1, "%s downloaded", a);
	}
	snprintf(l2, sizeof l2, "%s/s", sp);
	if (ui_progress(p->title, l1, l2, pct)) { p->cancelled = 1; return 1; }
	return g_quit ? 1 : 0;
}

static size_t write_cb(char *ptr, size_t sz, size_t n, void *ud) { return fwrite(ptr, sz, n, (FILE *)ud); }

static const char *base_name(const char *p)
{
	const char *s = strrchr(p, '/');
	return s ? s + 1 : p;
}

int net_download(const char *url, const char *dest, const char *title, const char *magic4,
                 unsigned long long max_bytes, DlResult *r)
{
	memset(r, 0, sizeof *r);
	const char *name = title && *title ? title : base_name(dest);
	if (strncmp(url, "https://", 8) != 0) {
		snprintf(r->err, sizeof r->err, "Only HTTPS links are allowed.");
		history_add('F', name, dest, url, 0);
		return -1;
	}
	if (net_ensure(r->err, sizeof r->err) != 0) { history_add('F', name, dest, url, 0); return -1; }

	char part[260], bak[260];
	snprintf(part, sizeof part, "%s.part", dest);
	snprintf(bak, sizeof bak, "%s.bak", dest);

	FILE *f = fopen(part, "wb");
	if (!f) {
		snprintf(r->err, sizeof r->err, "Cannot create the file on the SD card.");
		history_add('F', name, dest, url, 0);
		return -1;
	}
	setvbuf(f, NULL, _IOFBF, 32768);

	CURL *c = curl_easy_init();
	if (!c) {
		fclose(f); remove(part);
		snprintf(r->err, sizeof r->err, "Out of memory (curl).");
		history_add('F', name, dest, url, 0);
		return -1;
	}
	Prog p = { name, url, 0, osGetTime(), max_bytes, 0, 0 };
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "CollingOS-3DS/" COS_VERSION);
	curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(c, CURLOPT_CAINFO, ca_path);
	curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
	curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 16384L);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, xfer_cb);
	curl_easy_setopt(c, CURLOPT_XFERINFODATA, &p);

	CURLcode rc = curl_easy_perform(c);
	long http = 0;
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
	curl_easy_cleanup(c);
	long size = ftell(f);
	int werr = ferror(f);
	if (fclose(f) != 0) werr = 1;
	r->http = http;
	r->bytes = size > 0 ? (unsigned long long)size : 0;

	if (rc != CURLE_OK) {
		remove(part);
		if (p.cancelled) { r->cancelled = 1; snprintf(r->err, sizeof r->err, "Download cancelled."); history_add('C', name, dest, url, r->bytes); }
		else if (p.too_big) { snprintf(r->err, sizeof r->err, "File is larger than the allowed limit."); history_add('F', name, dest, url, r->bytes); }
		else { curl_msg((int)rc, http, r->err, sizeof r->err); history_add('F', name, dest, url, r->bytes); }
		return -1;
	}
	if (werr || size <= 0) {
		remove(part);
		snprintf(r->err, sizeof r->err, size <= 0 ? "Server sent an empty file." : "SD card write error (card full?).");
		history_add('F', name, dest, url, 0);
		return -1;
	}
	if (magic4) {
		char m[4] = { 0 };
		FILE *g = fopen(part, "rb");
		int ok = g && fread(m, 1, 4, g) == 4 && memcmp(m, magic4, 4) == 0;
		if (g) fclose(g);
		if (!ok) {
			remove(part);
			snprintf(r->err, sizeof r->err, "Downloaded file has the wrong format (not a valid app).");
			history_add('F', name, dest, url, r->bytes);
			return -1;
		}
	}
	/* swap in the new file; keep the old one until the rename worked */
	int had = file_exists(dest);
	if (had) { remove(bak); if (rename(dest, bak) != 0) { remove(part); snprintf(r->err, sizeof r->err, "Cannot replace the existing file."); history_add('F', name, dest, url, 0); return -1; } }
	if (rename(part, dest) != 0) {
		if (had) rename(bak, dest);
		remove(part);
		snprintf(r->err, sizeof r->err, "Cannot save the file to the SD card.");
		history_add('F', name, dest, url, 0);
		return -1;
	}
	if (had) remove(bak);
	history_add('O', name, dest, url, r->bytes);
	return 0;
}

typedef struct { char *buf; size_t len, max; int over; } Mem;

static size_t mem_cb(char *ptr, size_t sz, size_t n, void *ud)
{
	Mem *m = ud;
	size_t add = sz * n;
	if (m->len + add > m->max) { m->over = 1; return 0; }
	char *nb = realloc(m->buf, m->len + add + 1);
	if (!nb) return 0;
	m->buf = nb;
	memcpy(m->buf + m->len, ptr, add);
	m->len += add;
	m->buf[m->len] = 0;
	return add;
}

int net_fetch_mem(const char *url, char **out, size_t *len, size_t max_bytes, int timeout_s, DlResult *r)
{
	memset(r, 0, sizeof *r);
	*out = NULL; *len = 0;
	if (strncmp(url, "https://", 8) != 0) { snprintf(r->err, sizeof r->err, "Only HTTPS links are allowed."); return -1; }
	if (net_ensure(r->err, sizeof r->err) != 0) return -1;
	CURL *c = curl_easy_init();
	if (!c) { snprintf(r->err, sizeof r->err, "Out of memory (curl)."); return -1; }
	Mem m = { NULL, 0, max_bytes, 0 };
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "CollingOS-3DS/" COS_VERSION);
	curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(c, CURLOPT_CAINFO, ca_path);
	curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, (long)timeout_s);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, (long)timeout_s * 3);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, mem_cb);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
	CURLcode rc = curl_easy_perform(c);
	long http = 0;
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
	curl_easy_cleanup(c);
	r->http = http;
	if (rc != CURLE_OK) {
		free(m.buf);
		if (m.over) snprintf(r->err, sizeof r->err, "Response is too large.");
		else curl_msg((int)rc, http, r->err, sizeof r->err);
		return -1;
	}
	if (!m.buf) { m.buf = malloc(1); if (!m.buf) { snprintf(r->err, sizeof r->err, "Out of memory."); return -1; } m.buf[0] = 0; }
	*out = m.buf;
	*len = m.len;
	r->bytes = m.len;
	return 0;
}

/* ------------------------------------------------------------ history */

static void clean(char *dst, const char *src, size_t n)
{
	size_t i = 0;
	for (; src[i] && i + 1 < n; i++) dst[i] = (src[i] == '|' || src[i] == '\n' || src[i] == '\r') ? '_' : src[i];
	dst[i] = 0;
}

void history_add(char status, const char *name, const char *dest, const char *url, unsigned long long bytes)
{
	char n[64], d[220], u[240];
	clean(n, name, sizeof n); clean(d, dest, sizeof d); clean(u, url, sizeof u);
	FILE *f = fopen(HIST_FILE, "a");
	if (!f) return;
	fprintf(f, "%c|%ld|%llu|%s|%s|%s\n", status, (long)time(NULL), bytes, n, d, u);
	fclose(f);
}

static char *next_field(char **p)
{
	char *s = *p;
	if (!s) return NULL;
	char *e = strchr(s, '|');
	if (e) { *e = 0; *p = e + 1; } else *p = NULL;
	return s;
}

int history_load(HistItem **out)
{
	*out = NULL;
	FILE *f = fopen(HIST_FILE, "r");
	if (!f) return 0;
	HistItem *all = malloc(sizeof(HistItem) * HIST_MAX);
	if (!all) { fclose(f); return 0; }
	int n = 0, total = 0;
	char line[560];
	/* ring: keep only the last HIST_MAX lines */
	while (fgets(line, sizeof line, f)) {
		char *nl = strpbrk(line, "\r\n");
		if (nl) *nl = 0;
		char *p = line;
		char *st = next_field(&p), *when = next_field(&p), *by = next_field(&p);
		char *nm = next_field(&p), *ds = next_field(&p), *ur = p;
		if (!st || !when || !by || !nm || !ds || !ur) continue;
		HistItem *it = &all[total % HIST_MAX];
		it->status = st[0];
		it->when = atol(when);
		it->bytes = strtoull(by, NULL, 10);
		snprintf(it->name, sizeof it->name, "%s", nm);
		snprintf(it->dest, sizeof it->dest, "%s", ds);
		snprintf(it->url, sizeof it->url, "%s", ur);
		total++;
	}
	fclose(f);
	n = total < HIST_MAX ? total : HIST_MAX;
	HistItem *res = malloc(sizeof(HistItem) * (n ? n : 1));
	if (!res) { free(all); return 0; }
	for (int i = 0; i < n; i++) res[i] = all[(total - 1 - i) % HIST_MAX]; /* newest first */
	free(all);
	*out = res;
	return n;
}

void history_clear(void) { remove(HIST_FILE); }

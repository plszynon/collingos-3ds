#pragma once
#include <stddef.h>

typedef struct {
	char err[160];
	long http;
	unsigned long long bytes;
	int cancelled;
} DlResult;

int  net_wifi_ok(void);
int  net_ensure(char *err, int errlen);
void net_shutdown(void);

/* HTTPS download into dest (atomic: .part file then rename). magic4 = optional 4-byte file signature check.
   Returns 0 on success, -1 on failure (see r->err). Always records the attempt in the history. */
int  net_download(const char *url, const char *dest, const char *title, const char *magic4,
                  unsigned long long max_bytes, DlResult *r);

/* HTTPS GET into a malloc'd buffer (NUL-terminated). Caller frees. */
int  net_fetch_mem(const char *url, char **out, size_t *len, size_t max_bytes, int timeout_s, DlResult *r);

/* ---- download history ---- */
typedef struct {
	char status;               /* 'O' ok, 'F' failed, 'C' cancelled */
	long when;
	unsigned long long bytes;
	char name[48];
	char dest[200];
	char url[220];
} HistItem;

void history_add(char status, const char *name, const char *dest, const char *url, unsigned long long bytes);
int  history_load(HistItem **out);   /* newest first; returns count; caller frees *out */
void history_clear(void);

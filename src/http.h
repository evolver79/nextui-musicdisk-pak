// A small HTTP(S) GET over the Brick's own OpenSSL, following redirects.
#pragma once
#include <stddef.h>

typedef void (*http_progress)(long got, long total, void *ud);

// Returns the HTTP status (200 on success) or -1 when the server can't be reached. The body is malloc'ed.
int http_get(const char *url, char **body, size_t *len, http_progress cb, void *ud);
// Downloads to path (through path.part, so a broken download never looks finished). Returns 1 on success.
int http_get_file(const char *url, const char *path, http_progress cb, void *ud);

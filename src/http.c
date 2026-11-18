#include "http.h"
#include <netdb.h>
#include <openssl/ssl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define MAX_BODY (64L << 20)
#define USER_AGENT "Tracker.pak/0.1 (NextUI module player)"

typedef struct { int fd; SSL *ssl; } Conn;

static int conn_read(Conn *c, char *b, int n) { return c->ssl ? SSL_read(c->ssl, b, n) : (int)recv(c->fd, b, n, 0); }
static int conn_write(Conn *c, const char *b, int n) { return c->ssl ? SSL_write(c->ssl, b, n) : (int)send(c->fd, b, n, 0); }

static SSL_CTX *ssl_ctx(void) {
  static SSL_CTX *ctx;
  if (!ctx) {
    SSL_library_init();
    ctx = SSL_CTX_new(TLS_client_method());
    // Like Tuner (through ffmpeg), certificates aren't checked: the Brick has no dependable CA store.
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
  }
  return ctx;
}

static int open_conn(Conn *c, const char *host, const char *port, int tls) {
  struct addrinfo hints = {0}, *res, *ai;
  hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host, port, &hints, &res) != 0) return -1;
  c->fd = -1; c->ssl = NULL;
  for (ai = res; ai; ai = ai->ai_next) {
    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) continue;
    struct timeval tv = { 20, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) { c->fd = fd; break; }
    close(fd);
  }
  freeaddrinfo(res);
  if (c->fd < 0) return -1;
  if (tls) {
    c->ssl = SSL_new(ssl_ctx());
    SSL_set_tlsext_host_name(c->ssl, host);
    SSL_set_fd(c->ssl, c->fd);
    if (SSL_connect(c->ssl) != 1) { SSL_free(c->ssl); close(c->fd); return -1; }
  }
  return 0;
}

static void close_conn(Conn *c) {
  if (c->ssl) { SSL_shutdown(c->ssl); SSL_free(c->ssl); }
  if (c->fd >= 0) close(c->fd);
}

static int parse_url(const char *url, int *tls, char *host, size_t hn, char *port, size_t pn, char *path, size_t ptn) {
  const char *p;
  if (!strncmp(url, "https://", 8)) { *tls = 1; p = url + 8; snprintf(port, pn, "443"); }
  else if (!strncmp(url, "http://", 7)) { *tls = 0; p = url + 7; snprintf(port, pn, "80"); }
  else return -1;
  const char *slash = strchr(p, '/');
  size_t hl = slash ? (size_t)(slash - p) : strlen(p);
  if (hl >= hn) return -1;
  memcpy(host, p, hl); host[hl] = 0;
  char *colon = strchr(host, ':');
  if (colon) { snprintf(port, pn, "%s", colon + 1); *colon = 0; }
  snprintf(path, ptn, "%s", slash ? slash : "/");
  return 0;
}

static const char *header(const char *head, const char *name) {
  size_t n = strlen(name);
  for (const char *l = strstr(head, "\r\n"); l; l = strstr(l + 2, "\r\n"))
    if (!strncasecmp(l + 2, name, n) && l[2 + n] == ':') { const char *v = l + 3 + n; while (*v == ' ') v++; return v; }
  return NULL;
}

// Turns a chunked body into a plain one, in place.
static size_t unchunk(char *b, size_t n) {
  size_t in = 0, out = 0;
  while (in < n) {
    char *end;
    long sz = strtol(b + in, &end, 16);
    char *crlf = strstr(end, "\r\n");
    if (!crlf || sz <= 0) break;
    in = (crlf - b) + 2;
    if (in + sz > n) sz = n - in;
    memmove(b + out, b + in, sz);
    out += sz; in += sz + 2;
  }
  return out;
}

int http_get(const char *url, char **body, size_t *len, http_progress cb, void *ud) {
  char cur[2048]; snprintf(cur, sizeof cur, "%s", url);
  *body = NULL; *len = 0;
  for (int hop = 0; hop < 6; hop++) {
    int tls; char host[256], port[8], path[1792];
    if (parse_url(cur, &tls, host, sizeof host, port, sizeof port, path, sizeof path) != 0) return -1;
    Conn c;
    if (open_conn(&c, host, port, tls) != 0) return -1;
    char req[2400];
    int rl = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nAccept: */*\r\nConnection: close\r\n\r\n", path, host, USER_AGENT);
    if (conn_write(&c, req, rl) != rl) { close_conn(&c); return -1; }
    size_t cap = 65536, n = 0; char *b = malloc(cap);
    long head_end = -1, total = -1;
    for (;;) {
      if (cap - n < 16384) { if (cap >= (size_t)MAX_BODY) break; cap *= 2; b = realloc(b, cap); }
      int r = conn_read(&c, b + n, (int)(cap - n - 1));
      if (r <= 0) break;
      n += r; b[n] = 0;
      if (head_end < 0) {
        char *e = strstr(b, "\r\n\r\n");
        if (e) { head_end = (e - b) + 4; const char *cl = header(b, "Content-Length"); if (cl) total = atol(cl); }
      }
      if (cb && head_end >= 0) cb((long)(n - head_end), total, ud);
      if (head_end >= 0 && total >= 0 && (long)n - head_end >= total) break;
    }
    close_conn(&c);
    if (head_end < 0) { free(b); return -1; }
    b[head_end - 2] = 0;   // end the header block as a string
    int status = 0; sscanf(b, "HTTP/%*s %d", &status);
    if (status >= 300 && status < 400) {
      const char *loc = header(b, "Location");
      if (!loc) { free(b); return status; }
      char next[2048]; size_t ll = strcspn(loc, "\r\n");
      if (loc[0] == '/') snprintf(next, sizeof next, "%s://%s%.*s", tls ? "https" : "http", host, (int)ll, loc);
      else snprintf(next, sizeof next, "%.*s", (int)ll, loc);
      snprintf(cur, sizeof cur, "%s", next);
      free(b);
      continue;
    }
    const char *te = header(b, "Transfer-Encoding");
    int chunked = te && !strncasecmp(te, "chunked", 7);
    size_t blen = n - head_end;
    memmove(b, b + head_end, blen);
    if (chunked) blen = unchunk(b, blen);
    b[blen] = 0;
    *body = b; *len = blen;
    return status;
  }
  return -1;
}

int http_get_file(const char *url, const char *path, http_progress cb, void *ud) {
  char *b; size_t n;
  int st = http_get(url, &b, &n, cb, ud);
  if (st != 200 || !b || !n) { free(b); return 0; }
  char tmp[1024]; snprintf(tmp, sizeof tmp, "%s.part", path);
  FILE *f = fopen(tmp, "wb");
  int ok = f && fwrite(b, 1, n, f) == n;
  if (f) fclose(f);
  free(b);
  if (ok && rename(tmp, path) == 0) return 1;
  remove(tmp);
  return 0;
}

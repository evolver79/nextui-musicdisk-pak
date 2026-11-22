#include "zipx.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <xmp.h>
#include <zlib.h>

#define MAX_ENTRY (32L << 20)

int is_zip_name(const char *name) { const char *d = strrchr(name, '.'); return d && !strcasecmp(d, ".zip"); }

static unsigned rd16(const unsigned char *p) { return p[0] | p[1] << 8; }
static unsigned long rd32(const unsigned char *p) { return (unsigned long)p[0] | p[1] << 8 | p[2] << 16 | (unsigned long)p[3] << 24; }

static unsigned char *read_all(const char *path, long *n) {
  FILE *f = fopen(path, "rb"); if (!f) return NULL;
  fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
  unsigned char *b = malloc(*n > 0 ? *n : 1);
  if (fread(b, 1, *n, f) != (size_t)*n) { free(b); b = NULL; }
  fclose(f);
  return b;
}

static unsigned char *inflate_raw(const unsigned char *in, unsigned long inlen, unsigned long outlen) {
  unsigned char *out = malloc(outlen ? outlen : 1);
  z_stream z = {0};
  if (inflateInit2(&z, -MAX_WBITS) != Z_OK) { free(out); return NULL; }
  z.next_in = (unsigned char *)in; z.avail_in = inlen; z.next_out = out; z.avail_out = outlen;
  int r = inflate(&z, Z_FINISH);
  inflateEnd(&z);
  if (r != Z_STREAM_END) { free(out); return NULL; }
  return out;
}

static int by_name(const void *a, const void *b) { return strcmp(((const ZipSong *)a)->path, ((const ZipSong *)b)->path); }

// Already unpacked: the folder holds 01-name, 02-name...
static int list_unpacked(const char *dir, ZipSong *songs, int max) {
  DIR *d = opendir(dir);
  if (!d) return -1;
  int n = 0; struct dirent *de;
  while ((de = readdir(d)) && n < max) {
    if (de->d_name[0] == '.') continue;
    snprintf(songs[n].path, sizeof songs[n].path, "%s/%s", dir, de->d_name);
    const char *dash = strchr(de->d_name, '-');
    snprintf(songs[n].name, sizeof songs[n].name, "%s", dash ? dash + 1 : de->d_name);
    n++;
  }
  closedir(d);
  qsort(songs, n, sizeof *songs, by_name);
  return n;
}

int zip_songs(const char *zip_path, const char *out_dir, ZipSong *songs, int max) {
  int done = list_unpacked(out_dir, songs, max);
  if (done >= 2) return done;
  long n; unsigned char *z = read_all(zip_path, &n);
  if (!z || n < 22) { free(z); return 0; }
  // the end of the central directory: the last PK\5\6 in the file
  long eocd = -1;
  for (long i = n - 22; i >= 0 && i >= n - 65557; i--) if (rd32(z + i) == 0x06054b50) { eocd = i; break; }
  if (eocd < 0) { free(z); return 0; }
  unsigned total = rd16(z + eocd + 10);
  unsigned long cd = rd32(z + eocd + 16);
  struct { unsigned char *data; unsigned long len; char name[192]; } found[256];
  int nf = 0;
  for (unsigned e = 0; e < total && cd + 46 <= (unsigned long)n && nf < 256; e++) {
    const unsigned char *h = z + cd;
    if (rd32(h) != 0x02014b50) break;
    unsigned method = rd16(h + 10), nl = rd16(h + 28), xl = rd16(h + 30), cl = rd16(h + 32);
    unsigned long csize = rd32(h + 20), usize = rd32(h + 24), lho = rd32(h + 42);
    char name[256]; snprintf(name, sizeof name, "%.*s", (int)(nl < 255 ? nl : 255), (const char *)h + 46);
    cd += 46 + nl + xl + cl;
    if (!nl || name[strlen(name) - 1] == '/' || usize > MAX_ENTRY || (method != 0 && method != 8)) continue;
    if (lho + 30 > (unsigned long)n || rd32(z + lho) != 0x04034b50) continue;
    unsigned long data = lho + 30 + rd16(z + lho + 26) + rd16(z + lho + 28);
    if (data + csize > (unsigned long)n) continue;
    unsigned char *buf;
    if (method == 0) { buf = malloc(csize ? csize : 1); memcpy(buf, z + data, csize); usize = csize; }
    else buf = inflate_raw(z + data, csize, usize);
    if (!buf) continue;
    if (xmp_test_module_from_memory(buf, usize, NULL) != 0) { free(buf); continue; }   // not a module (a .diz, an .exe...)
    const char *base = strrchr(name, '/'); base = base ? base + 1 : name;
    found[nf].data = buf; found[nf].len = usize;
    snprintf(found[nf].name, sizeof found[nf].name, "%s", base);
    nf++;
  }
  free(z);
  int out = 0;
  if (nf >= 2) {
    char p[1200]; snprintf(p, sizeof p, "%s", out_dir);
    for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
    mkdir(out_dir, 0755);
    for (int i = 0; i < nf && out < max; i++) {
      snprintf(songs[out].path, sizeof songs[out].path, "%s/%02d-%s", out_dir, i + 1, found[i].name);
      snprintf(songs[out].name, sizeof songs[out].name, "%s", found[i].name);
      FILE *f = fopen(songs[out].path, "wb");
      if (f) { fwrite(found[i].data, 1, found[i].len, f); fclose(f); out++; }
    }
  }
  for (int i = 0; i < nf; i++) free(found[i].data);
  return nf >= 2 ? out : nf;
}

char *zip_diz(const char *zip_path) {
  long n; unsigned char *z = read_all(zip_path, &n);
  if (!z || n < 22) { free(z); return NULL; }
  long eocd = -1;
  for (long i = n - 22; i >= 0 && i >= n - 65557; i--) if (rd32(z + i) == 0x06054b50) { eocd = i; break; }
  if (eocd < 0) { free(z); return NULL; }
  unsigned total = rd16(z + eocd + 10);
  unsigned long cd = rd32(z + eocd + 16);
  char *best = NULL; int best_is_file_id = 0;
  for (unsigned e = 0; e < total && cd + 46 <= (unsigned long)n; e++) {
    const unsigned char *h = z + cd;
    if (rd32(h) != 0x02014b50) break;
    unsigned method = rd16(h + 10), nl = rd16(h + 28), xl = rd16(h + 30), cl = rd16(h + 32);
    unsigned long csize = rd32(h + 20), usize = rd32(h + 24), lho = rd32(h + 42);
    char name[256]; snprintf(name, sizeof name, "%.*s", (int)(nl < 255 ? nl : 255), (const char *)h + 46);
    cd += 46 + nl + xl + cl;
    const char *base = strrchr(name, '/'); base = base ? base + 1 : name;
    const char *dot = strrchr(base, '.');
    if (!dot || strcasecmp(dot, ".diz") || usize > 65536 || (method != 0 && method != 8)) continue;
    int file_id = !strcasecmp(base, "file_id.diz");
    if (best && (best_is_file_id || !file_id)) continue;
    if (lho + 30 > (unsigned long)n || rd32(z + lho) != 0x04034b50) continue;
    unsigned long data = lho + 30 + rd16(z + lho + 26) + rd16(z + lho + 28);
    if (data + csize > (unsigned long)n) continue;
    unsigned char *buf;
    if (method == 0) { buf = malloc(csize + 1); memcpy(buf, z + data, csize); usize = csize; }
    else { buf = inflate_raw(z + data, csize, usize); if (buf) buf = realloc(buf, usize + 1); }
    if (!buf) continue;
    buf[usize] = 0;
    free(best); best = (char *)buf; best_is_file_id = file_id;
  }
  free(z);
  return best;
}

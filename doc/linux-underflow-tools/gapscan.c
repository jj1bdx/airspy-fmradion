/*
 * gapscan: find gaps in a captured stereo FLOAT_LE raw stream.
 *
 * Usage: gapscan [-r ref.f32] [-t tol] [-z min_zero_frames] capture.f32
 *
 * With -r: the reference is the same decode written with -F. The capture
 * should equal the reference with silence inserted. Each point where the
 * capture departs from the reference is resynchronized, and the event is
 * printed as an insertion (gap) or a deletion (lost frames).
 * Without -r: runs of digital zero on both channels of at least
 * min_zero_frames, between the first and last non-zero frame, are printed,
 * and segments that exactly repeat audio from the last second ("rep").
 * Frame positions are 0-based capture frames; 48 kHz is assumed for ms.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RATE 48000.0
#define K 64           /* frames that must match to accept a resync */
#define GMAX 4800000L  /* max insertion searched: 100 s */
#define DMAX 480000L   /* max deletion searched: 10 s */

static float *load(const char *path, long *nframes) {
  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    perror(path);
    exit(1);
  }
  fseek(f, 0, SEEK_END);
  long bytes = ftell(f);
  fseek(f, 0, SEEK_SET);
  *nframes = bytes / 8;
  float *buf = malloc((size_t)(*nframes) * 8 + 8);
  if (fread(buf, 8, (size_t)*nframes, f) != (size_t)*nframes) {
    perror("fread");
    exit(1);
  }
  fclose(f);
  return buf;
}

static double g_tol = 0.0;

static int eq(const float *a, const float *b) {
  if (g_tol == 0.0) {
    return a[0] == b[0] && a[1] == b[1];
  }
  return fabs(a[0] - b[0]) <= g_tol && fabs(a[1] - b[1]) <= g_tol;
}

static int match(const float *c, long nc, long i, const float *r, long nr,
                 long j) {
  if (i + K > nc || j + K > nr) {
    return 0;
  }
  for (long k = 0; k < K; k++) {
    if (!eq(&c[2 * (i + k)], &r[2 * (j + k)])) {
      return 0;
    }
  }
  return 1;
}

static int is_zero(const float *c, long i) {
  return c[2 * i] == 0.0f && c[2 * i + 1] == 0.0f;
}

static void scan_zero(const float *c, long nc, long minz) {
  long first = 0, last = nc - 1;
  while (first < nc && is_zero(c, first)) {
    first++;
  }
  while (last > first && is_zero(c, last)) {
    last--;
  }
  long count = 0, total = 0;
  printf("type\tcap_frame\tlen_frames\tlen_ms\n");
  for (long i = first; i <= last;) {
    if (!is_zero(c, i)) {
      i++;
      continue;
    }
    long s = i;
    while (i <= last && is_zero(c, i)) {
      i++;
    }
    if (i - s >= minz) {
      printf("zero\t%ld\t%ld\t%.3f\n", s, i - s, (i - s) * 1000.0 / RATE);
      count++;
      total += i - s;
    }
  }
  printf("#summary\tactive_frames=%ld\tzero_runs=%ld\tzero_frames=%ld\t"
         "zero_ms=%.3f\tlead_frames=%ld\n",
         last - first + 1, count, total, total * 1000.0 / RATE, first);
}

/* Find segments that repeat earlier audio exactly (aloop can replay stale
 * ring contents during an xrun). A window of W frames is hashed at every
 * frame; a hit within the last second that matches exactly and is not
 * silence starts a repeat, which is extended frame by frame. */
#define W 32
#define HBITS 22
static void scan_repeat(const float *c, long nc) {
  const unsigned long mask = (1UL << HBITS) - 1;
  long *table = malloc(sizeof(long) << HBITS);
  for (unsigned long k = 0; k <= mask; k++) {
    table[k] = -1;
  }
  const unsigned int *u = (const unsigned int *)c;
  long count = 0, total = 0;
  for (long i = 0; i + W <= nc;) {
    unsigned long long h = 1469598103934665603ULL;
    int nonzero = 0;
    for (long k = 0; k < 2 * W; k++) {
      h = (h ^ u[2 * i + k]) * 1099511628211ULL;
      nonzero |= (u[2 * i + k] & 0x7fffffffU) != 0;
    }
    const unsigned long slot = (unsigned long)(h ^ (h >> 29)) & mask;
    const long p = table[slot];
    table[slot] = i;
    if (!nonzero || p < 0 || i - p > (long)RATE ||
        memcmp(&c[2 * p], &c[2 * i], 8 * W) != 0) {
      i++;
      continue;
    }
    long len = W;
    while (i + len < nc && c[2 * (i + len)] == c[2 * (p + len)] &&
           c[2 * (i + len) + 1] == c[2 * (p + len) + 1]) {
      len++;
    }
    printf("rep\t%ld\t%ld\t%.3f\t%ld\n", i, len, len * 1000.0 / RATE, i - p);
    count++;
    total += len;
    i += len;
  }
  printf("#repeats\tcount=%ld\tframes=%ld\tms=%.3f\n", count, total,
         total * 1000.0 / RATE);
  free(table);
}

int main(int argc, char **argv) {
  const char *refpath = NULL;
  long minz = 24;
  int opt;
  while ((opt = getopt(argc, argv, "r:t:z:")) != -1) {
    switch (opt) {
    case 'r':
      refpath = optarg;
      break;
    case 't':
      g_tol = atof(optarg);
      break;
    case 'z':
      minz = atol(optarg);
      break;
    default:
      fprintf(stderr, "usage: %s [-r ref] [-t tol] [-z frames] capture\n",
              argv[0]);
      return 2;
    }
  }
  if (optind >= argc) {
    fprintf(stderr, "need capture file\n");
    return 2;
  }
  long nc;
  float *c = load(argv[optind], &nc);
  if (refpath == NULL) {
    scan_zero(c, nc, minz);
    scan_repeat(c, nc);
    return 0;
  }
  long nr;
  float *r = load(refpath, &nr);

  /* Start: first non-zero reference frame, found in the capture. */
  long j = 0;
  while (j < nr && r[2 * j] == 0.0f && r[2 * j + 1] == 0.0f) {
    j++;
  }
  /* The capture may start after the first audio frames were played, so
   * try later reference positions until one is found. */
  long i = nc;
  for (long tries = 0; tries < 400 && j + K <= nr; tries++) {
    i = 0;
    while (i < nc && !match(c, nc, i, r, nr, j)) {
      i++;
    }
    if (i < nc) {
      break;
    }
    j += 2400;
  }
  if (i >= nc) {
    printf("#error\tstart of reference not found in capture\n");
    return 1;
  }
  printf("#start\tcap_frame=%ld\tref_frame=%ld\n", i, j);
  printf("type\tcap_frame\tref_frame\tlen_frames\tlen_ms\tzero_frames\n");
  long n_ins = 0, n_del = 0, ins_total = 0, del_total = 0;
  for (;;) {
    while (i < nc && j < nr && eq(&c[2 * i], &r[2 * j])) {
      i++;
      j++;
    }
    if (i >= nc || j >= nr) {
      break;
    }
    /* Mismatch at (i, j): try the smallest insertion, then deletion. */
    long g = -1, d = -1;
    for (long t = 0; t <= GMAX && i + t + K <= nc; t++) {
      if (eq(&c[2 * (i + t)], &r[2 * j]) && match(c, nc, i + t, r, nr, j)) {
        g = t;
        break;
      }
    }
    for (long t = 1; t <= DMAX && j + t + K <= nr; t++) {
      if (g >= 0 && t >= g) {
        break;
      }
      if (eq(&c[2 * i], &r[2 * (j + t)]) && match(c, nc, i, r, nr, j + t)) {
        d = t;
        break;
      }
    }
    if (d > 0) {
      printf("del\t%ld\t%ld\t%ld\t%.3f\t0\n", i, j, d, d * 1000.0 / RATE);
      n_del++;
      del_total += d;
      j += d;
    } else if (g > 0) {
      long z = 0;
      for (long t = 0; t < g; t++) {
        z += is_zero(c, i + t);
      }
      printf("ins\t%ld\t%ld\t%ld\t%.3f\t%ld\n", i, j, g, g * 1000.0 / RATE,
             z);
      n_ins++;
      ins_total += g;
      i += g;
    } else {
      /* Near the end of either stream, or the data differ: stop here. */
      long left_r = nr - j, left_c = nc - i;
      printf("#nosync\tcap_frame=%ld\tref_frame=%ld\tref_left=%ld\t"
             "cap_left=%ld\n",
             i, j, left_r, left_c);
      break;
    }
  }
  printf("#summary\tref_frames=%ld\tref_reached=%ld\tinsertions=%ld\t"
         "inserted_frames=%ld\tinserted_ms=%.3f\tdeletions=%ld\t"
         "deleted_frames=%ld\n",
         nr, j, n_ins, ins_total, ins_total * 1000.0 / RATE, n_del,
         del_total);
  return 0;
}

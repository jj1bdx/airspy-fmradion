/*
 * stall_inject: stop one thread (or a whole process) for given durations.
 *
 * Usage: stall_inject -p TID [-m thread|process] [-w first_wait_ms]
 *                     [-g gap_ms] [-n repeat] -d ms[,ms...] [-o log.tsv]
 *
 * thread mode: PTRACE_SEIZE the thread, then for each stall
 *   PTRACE_INTERRUPT, sleep, PTRACE_CONT. Other threads keep running.
 * process mode: SIGSTOP, sleep, SIGCONT to the whole process.
 * The list of durations is run "repeat" times, with gap_ms between stalls.
 * Log columns (CLOCK_MONOTONIC ns): index, requested_ms, t_request,
 * t_stopped, t_resumed, actual_ms (t_resumed - t_stopped).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void sleep_until(int64_t t_ns) {
  struct timespec ts = {.tv_sec = t_ns / 1000000000LL,
                        .tv_nsec = t_ns % 1000000000LL};
  while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {
  }
}

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) {
  (void)sig;
  g_stop = 1;
}

/* Wait until the traced thread is in a ptrace stop. Pass any signal
 * that arrives first back to it. Returns 0 on success, -1 if it exited. */
static int wait_stopped(pid_t tid) {
  for (;;) {
    int status;
    pid_t r = waitpid(tid, &status, __WALL);
    if (r < 0) {
      if (errno == EINTR) {
        continue;
      }
      return -1;
    }
    if (WIFEXITED(status) || WIFSIGNALED(status)) {
      return -1;
    }
    if (!WIFSTOPPED(status)) {
      continue;
    }
    if ((status >> 16) == PTRACE_EVENT_STOP) {
      return 0;
    }
    /* Signal-delivery stop: deliver the signal and keep waiting. */
    ptrace(PTRACE_CONT, tid, NULL, (void *)(intptr_t)WSTOPSIG(status));
  }
}

int main(int argc, char **argv) {
  pid_t tid = 0;
  int process_mode = 0;
  double first_wait_ms = 1000.0, gap_ms = 2000.0;
  int repeat = 1;
  char *dlist = NULL;
  const char *logpath = NULL;
  int opt;
  while ((opt = getopt(argc, argv, "p:m:w:g:n:d:o:")) != -1) {
    switch (opt) {
    case 'p':
      tid = (pid_t)atoi(optarg);
      break;
    case 'm':
      process_mode = (strcmp(optarg, "process") == 0);
      break;
    case 'w':
      first_wait_ms = atof(optarg);
      break;
    case 'g':
      gap_ms = atof(optarg);
      break;
    case 'n':
      repeat = atoi(optarg);
      break;
    case 'd':
      dlist = strdup(optarg);
      break;
    case 'o':
      logpath = optarg;
      break;
    default:
      fprintf(stderr, "usage: %s -p TID [-m thread|process] [-w ms] [-g ms] "
                      "[-n N] -d ms[,ms...] [-o log]\n",
              argv[0]);
      return 2;
    }
  }
  if (tid <= 0 || dlist == NULL) {
    fprintf(stderr, "need -p and -d\n");
    return 2;
  }
  double d_ms[256];
  int nd = 0;
  for (char *s = strtok(dlist, ","); s != NULL && nd < 256;
       s = strtok(NULL, ",")) {
    d_ms[nd++] = atof(s);
  }
  FILE *log = logpath ? fopen(logpath, "w") : stdout;
  if (log == NULL) {
    perror("fopen");
    return 1;
  }
  fprintf(log, "#mode=%s tid=%d\n", process_mode ? "process" : "thread",
          (int)tid);
  fprintf(log, "idx\treq_ms\tt_request\tt_stopped\tt_resumed\tactual_ms\n");
  fflush(log);

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  if (!process_mode) {
    if (ptrace(PTRACE_SEIZE, tid, NULL, NULL) != 0) {
      perror("PTRACE_SEIZE");
      return 1;
    }
  }

  int64_t t_next = now_ns() + (int64_t)(first_wait_ms * 1e6);
  int idx = 0;
  for (int r = 0; r < repeat && !g_stop; r++) {
    for (int k = 0; k < nd && !g_stop; k++) {
      sleep_until(t_next);
      if (g_stop) {
        break;
      }
      const int64_t t_req = now_ns();
      int64_t t_stopped;
      if (process_mode) {
        if (kill(tid, SIGSTOP) != 0) {
          perror("SIGSTOP");
          g_stop = 1;
          break;
        }
        t_stopped = now_ns();
      } else {
        if (ptrace(PTRACE_INTERRUPT, tid, NULL, NULL) != 0 ||
            wait_stopped(tid) != 0) {
          fprintf(stderr, "thread %d gone\n", (int)tid);
          g_stop = 1;
          break;
        }
        t_stopped = now_ns();
      }
      sleep_until(t_stopped + (int64_t)(d_ms[k] * 1e6));
      if (process_mode) {
        kill(tid, SIGCONT);
      } else {
        ptrace(PTRACE_CONT, tid, NULL, NULL);
      }
      const int64_t t_res = now_ns();
      fprintf(log, "%d\t%.3f\t%lld\t%lld\t%lld\t%.3f\n", idx++, d_ms[k],
              (long long)t_req, (long long)t_stopped, (long long)t_res,
              (t_res - t_stopped) / 1e6);
      fflush(log);
      t_next = t_res + (int64_t)(gap_ms * 1e6);
    }
  }

  if (!process_mode) {
    /* Detach: the tracee must be in a ptrace stop. */
    if (ptrace(PTRACE_INTERRUPT, tid, NULL, NULL) == 0 &&
        wait_stopped(tid) == 0) {
      ptrace(PTRACE_DETACH, tid, NULL, NULL);
    }
  }
  if (log != stdout) {
    fclose(log);
  }
  return 0;
}

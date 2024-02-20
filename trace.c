#include "trace.h"
#include <sys/time.h>

struct timeval trace_base_timer;

void trace_start() { gettimeofday(&trace_base_timer, NULL); }

long trace_timedelta() {
  struct timeval stop;
  gettimeofday(&stop, NULL);

  long delta = (stop.tv_sec - trace_base_timer.tv_sec) * 1000 +
               (stop.tv_usec - trace_base_timer.tv_usec) / 1000;
  return delta;
}

void trace_log(FILE *out, const char *file, const char *func, int line,
               pid_t pid, const char *fmt, ...) {

  flockfile(out);
  // header
  fprintf(out, "%d +%lums %s(%s:%d) ", getpid(), trace_timedelta(), func, file,
          line);
  // content
  va_list args;
  va_start(args, fmt);
  vfprintf(out, fmt, args);
  va_end(args);
  fprintf(out, "\n");
  funlockfile(out);
}

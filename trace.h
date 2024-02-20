#ifndef TRACE_H
#define TRACE_H

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#define __FILENAME__                                                           \
  (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#ifdef ENABLE_TRACE
#define TRACE(...)                                                             \
  trace_log(stderr, __FILENAME__, __func__, __LINE__, getpid(), __VA_ARGS__)
#define TRACE_ONLY(prog) prog
#else
#define TRACE(...)
#define TRACE_ONLY(prog)
#endif

void trace_start();

long trace_timedelta();

void trace_log(FILE *out, const char *file, const char *func, int line,
               pid_t pid, const char *fmt, ...);

#endif /* TRACE_H */

#include "../yajava.h"
#include "utest.h"

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>

#include <sys/stat.h>
#include <sys/wait.h>

UTEST_MAIN();

bool _file_exists(const char *path) {
  struct stat st;
  if (path == NULL) {
    return false;
  }

  if (stat(path, &st) != 0) {
    return false;
  }
  return true;
}

UTEST(discovery, path) {
  struct yj_java_runtime *runtimes = NULL;
  size_t runtimes_len = 0;

  if (_file_exists("/opt/jdk")) {
    if (yj_java_discovery("/opt/jdk", &runtimes, &runtimes_len) != YJ_OK) {
      printf("error\n");
      exit(1);
    }
  } else {
    if (yj_java_discovery("/usr/lib/jvm", &runtimes, &runtimes_len) != YJ_OK) {
      printf("error\n");
      exit(1);
    }
  }

  for (int i = 0; i < runtimes_len; i++) {
    struct yj_java_runtime *r = &runtimes[i];
    printf("\nRUNTIME[%d]\n", i + 1);
    printf("  VERSION: %s\n", r->full_version);
    printf("     HOME: %s\n", r->home);
    printf("      JNI: 0x%x\n", r->jni_version);
    printf("      LIB: %s\n", r->libjvm_path);

    if (r->home != NULL) {
      free(r->home);
    }
    if (r->full_version != NULL) {
      free(r->full_version);
    }
    if (r->libjvm_path != NULL) {
      free(r->libjvm_path);
    }
    if (r->name != NULL) {
      free(r->name);
    }
  }
  free(runtimes);
}

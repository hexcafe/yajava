#include "trace.h"
#include "yajava.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libgen.h>
#include <unistd.h>

#include <sys/wait.h>

#define USAGE_TEXT                                                             \
  "%s [command] [options] [java class] ...\n"                                  \
  "\n"                                                                         \
  "commands:\n"                                                                \
  "    <empty>   [options] ... run java application with params passthru\n"    \
  "    run       [options] ... run java application with params passthru\n"    \
  "    discovery [base-path]   discovery java runtime(s) in given base path\n" \
  "    show                    show current activated java runtime\n"
#define DEFAULT_EXEC_NAME "yajava"
#define DEFAULT_CMD "run"
#define CMD_MAXLEN 16

#define DEFAULT_JVM_DISCOVERY_PATH "/usr/lib/jvm"

struct table {
  char *rows[4];
  struct table *next;
};

void print_usages(char *exec);

struct table *table_new();
void table_print(struct table *table, FILE *out);
void table_free(struct table *table);

int main(int argc, char **argv) {

  trace_start();

  char *exec_name = NULL; // DO NOT FREE
  exec_name = basename(argv[0]);

  // parse args
  if (argc <= 1) {
    print_usages(exec_name);
    exit(0);
  }

  // read command
  char *cmd = NULL;
  size_t cmd_len = 0;

  cmd = argv[1];
  if (cmd == NULL) {
    print_usages(exec_name);
    exit(0);
  }

  cmd_len = strlen(cmd);

  if (strncmp(cmd, "run", cmd_len) == 0 || cmd[0] == '-') {

    struct yj_run_args run_args;
    struct yj_java_runtime runtime;
    int arg_count;
    char **arg_start;

    // find runtime
    if (yj_find_runtime(&runtime) != YJ_OK) {
      printf("error: no java runtime found\n");
      exit(1);
    }

    if (cmd[0] == '-') {
      arg_count = argc - 1;
      arg_start = argv + 1;
    } else {
      arg_count = argc - 2;
      arg_start = argv + 2;
    }

    if (yj_parse_run_args(arg_count, arg_start, &run_args) == YJ_OK) {
      yj_run_async(&runtime, &run_args);
    }

    wait(NULL);

    yj_free_runtime(&runtime);
    yj_free_run_args(&run_args);
  } else if (strncmp(cmd, "discovery", cmd_len) == 0) {
    char *base_path;
    if (argc > 2 && argv[2] != NULL) {
      base_path = argv[2];
    } else {
      base_path = DEFAULT_JVM_DISCOVERY_PATH;
    }

    struct yj_java_runtime *out = NULL;
    size_t out_len = 0;
    if (yj_java_discovery(base_path, &out, &out_len) != YJ_OK) {
      printf("discovery failed\n");
      exit(1);
    }

    if (out == NULL || out_len == 0) {
      printf("No jdk found, searched path: %s\n", base_path);
      exit(1);
    }

    struct table *table, *tail;
    char idx_buf[10] = {0};
    char ver_buf[128] = {0};
    struct yj_java_runtime *rt = out;

    tail = table = table_new();

    // HEADER
    tail->rows[0] = strdup("NO");
    tail->rows[1] = strdup("VERSION");
    tail->rows[2] = strdup("FULL_VERSION");
    tail->rows[3] = strdup("HOME");
    for (int i = 0; i < out_len; i++) {
      if (tail->next == NULL) {
        tail->next = table_new();
        tail = tail->next;
      }
      // INDEX
      sprintf(idx_buf, "%d", i + 1);

      tail->rows[0] = strdup(idx_buf);
      tail->rows[1] = rt->version == NULL ? NULL : strdup(rt->version);
      tail->rows[2] =
          rt->full_version == NULL ? NULL : strdup(rt->full_version);
      tail->rows[3] = rt->home == NULL ? NULL : strdup(rt->home);

      yj_free_runtime(rt);
      rt++;
    }

    table_print(table, stdout);
    table_free(table);
    free(out);

  } else if (strncmp(cmd, "show", cmd_len) == 0) {

    struct yj_java_runtime runtime;

    // find runtime
    if (yj_find_runtime(&runtime) != YJ_OK) {
      printf("error: no java runtime found\n");
      exit(1);
    }

    printf("version: %s\n", runtime.version);
    printf("home: %s\n", runtime.home);

    yj_free_runtime(&runtime);
  } else {
    printf("unknown command: %s\n\n", cmd);
    print_usages(exec_name);
    exit(0);
  }
  return 0;
}

void print_usages(char *exec) { printf(USAGE_TEXT, exec); }

struct table *table_new() {
  struct table *table = malloc(sizeof(struct table));
  memset(table, 0, sizeof(struct table));
  return table;
}

void table_print(struct table *table, FILE *out) {

  // calculate max length of each columns
  int c1, c2, c3, c4;
  c1 = c2 = c3 = c4 = 0;
  struct table *tail = table;

#define max_strlen(a, str)                                                     \
  (a) >= (str == NULL ? 0 : strlen(str)) ? (a) : strlen(str)

  while (tail != NULL) {
    c1 = max_strlen(c1, tail->rows[0]);
    c2 = max_strlen(c2, tail->rows[1]);
    c3 = max_strlen(c3, tail->rows[2]);
    c4 = max_strlen(c4, tail->rows[3]);

    tail = tail->next;
  }

#undef max_strlen
  tail = table;

  while (tail != NULL) {
    printf("%-*s %-*s %-*s %-*s\n", c1, tail->rows[0], c2, tail->rows[1], c3,
           tail->rows[2], c4, tail->rows[3]);
    tail = tail->next;
  }
}

void table_free(struct table *table) {
  struct table *tail, *tmp;

  tail = table;
  tmp = NULL;

  while (tail != NULL) {
    if (tail->rows[0] != NULL)
      free(tail->rows[0]);

    if (tail->rows[1] != NULL)
      free(tail->rows[1]);

    if (tail->rows[2] != NULL)
      free(tail->rows[2]);

    if (tail->rows[3] != NULL)
      free(tail->rows[3]);

    tmp = tail->next;
    free(tail);
    tail = tmp;
  }
}

#include "../yajava.h"
#include "utest.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

void print_args(char **arg, size_t len) {
  for (int i = 0; i < len; i++) {
    printf("%s ", arg[i]);
  }
  printf("\n");
}

UTEST_MAIN();

UTEST(args, classpath) {

  struct yj_run_args args;
  char *arg[] = {"--classpath", "abcd:bcedf:ghijk:lmnop:qrst:uvwxyz"};
  print_args(arg, sizeof(arg) / sizeof(char *));
  int res = yj_parse_run_args(sizeof(arg) / sizeof(char *), arg, &args);
  if (res != 0) {
    printf("ERROR");
  }

  printf("  classpath:\n");
  assert(args.classpathes_len == 6);
  for (int i = 0; i < args.classpathes_len; i++) {
    printf("     %d: %s\n", i, args.classpathes[i]);
  }
  yj_free_run_args(&args);
}

UTEST(args, classpath_multiple) {

  struct yj_run_args args;
  char *arg[] = {"--classpath", "abcd:bcedf:ghijk:lmnop:qrst:uvwxyz", "-cp",
                 "1234"};
  print_args(arg, sizeof(arg) / sizeof(char *));
  int res = yj_parse_run_args(sizeof(arg) / sizeof(char *), arg, &args);
  if (res != 0) {
    printf("ERROR");
  }

  printf("  classpath:\n");
  assert(args.classpathes_len == 7);
  for (int i = 0; i < args.classpathes_len; i++) {
    printf("     %d: %s\n", i, args.classpathes[i]);
  }

  ASSERT_STREQ("abcd", args.classpathes[0]);
  ASSERT_STREQ("1234", args.classpathes[6]);
  yj_free_run_args(&args);
}

UTEST(args, classpath_eq) {
  struct yj_run_args args;
  char *arg[] = {"--classpath=abcd:bcedf:ghijk:lmnop:qrst:uvwxyz"};
  print_args(arg, sizeof(arg) / sizeof(char *));
  int res = yj_parse_run_args(sizeof(arg) / sizeof(char *), arg, &args);
  if (res != 0) {
    printf("ERROR");
  }

  printf("  classpath:\n");
  assert(args.classpathes_len == 6);
  for (int i = 0; i < args.classpathes_len; i++) {
    printf("     %d: %s\n", i, args.classpathes[i]);
  }
  yj_free_run_args(&args);
}

UTEST(args, print_help) {
  struct yj_run_args args;
  char *arg[] = {"-?"};
  print_args(arg, sizeof(arg) / sizeof(char *));
  int res = yj_parse_run_args(sizeof(arg) / sizeof(char *), arg, &args);
  if (res != 0) {
    printf("ERROR");
  }

  ASSERT_NE(args.print_help & YA_PRINT_OUT, YA_PRINT_OUT);
  ASSERT_EQ(args.print_help & YA_PRINT_ERR, YA_PRINT_ERR);
  yj_free_run_args(&args);
}

UTEST(args, print_help_h) {
  struct yj_run_args args;
  char *arg[] = {"-h"};
  print_args(arg, sizeof(arg) / sizeof(char *));
  int res = yj_parse_run_args(sizeof(arg) / sizeof(char *), arg, &args);
  if (res != 0) {
    printf("ERROR");
  }
  ASSERT_EQ(args.print_help & YA_PRINT_ERR, YA_PRINT_ERR);
  ASSERT_NE(args.print_help & YA_PRINT_OUT, YA_PRINT_OUT);
  yj_free_run_args(&args);
}

UTEST(args, print_help_full) {
  struct yj_run_args args;
  char *arg[] = {"--help"};
  print_args(arg, sizeof(arg) / sizeof(char *));
  int res = yj_parse_run_args(sizeof(arg) / sizeof(char *), arg, &args);
  if (res != 0) {
    printf("ERROR");
  }

  ASSERT_TRUE(args.print_help & YA_PRINT_OUT);
  ASSERT_TRUE(args.print_help != 0);
  yj_free_run_args(&args);
}

UTEST(args, app_jar) {
  struct yj_run_args args;
  char *arg[] = {"-jar", "app.jar"};
  print_args(arg, sizeof(arg) / sizeof(char *));
  int res = yj_parse_run_args(sizeof(arg) / sizeof(char *), arg, &args);
  if (res != 0) {
    printf("ERROR");
  }

  ASSERT_STREQ("app.jar", args.app_jar);
  yj_free_run_args(&args);
}

UTEST(args, app_cp_main) {
  struct yj_run_args args;
  char *arg[] = {"-cp", "app.jar", "hello.Main"};
  print_args(arg, sizeof(arg) / sizeof(char *));
  int res = yj_parse_run_args(sizeof(arg) / sizeof(char *), arg, &args);
  if (res != 0) {
    printf("ERROR");
  }

  ASSERT_STREQ(args.app_main_class, "hello.Main");
  yj_free_run_args(&args);
}

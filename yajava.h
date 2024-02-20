#ifndef YAJAVA_H
#define YAJAVA_H

#include "jni_md.h"
#include <jni.h>
#include <stdbool.h>

#define YJ_PUBLIC
#define YJ_OK 0

#define YA_PRINT_VERSION 0x80 // 10000000
#define YA_PRINT_HELP 0x40    // 01000000

#define YA_PRINT_OUT 0x01      // 00000001 default
#define YA_PRINT_ERR 0x02      // 00000010
#define YA_PRINT_CONTINUE 0x04 // 00000100
#define YA_PRINT_EXT 0x08      // 00001000

typedef int yj_result;

struct yj_run_args {
  // parameters
  int classpathes_len;
  char **classpathes;
  int module_pathes_len;
  char **module_pathes;
  int upgrade_module_pathes_len;
  char **upgrade_module_pathes;
  int add_modules_len;
  char **add_modules;
  int native_access_modules_len;
  char **native_access_modules;
  int sys_props_len;
  char **sys_props;
  int vmopts_len;
  char **vmopts;
  int agentlibs_len;
  char **agentlibs;
  int agentpathes_len;
  char **agentpathes;
  int javagents_len;
  char **javagents;
  char *splash;
  char *jarfile;

  // application args
  char *app_module;
  char *app_jar;
  char *app_main_class;
  int app_args_len;
  char **app_args;

  // flags
  bool dry_run;
  bool verbose_class;
  bool verbose_module;
  bool verbose_gc;
  bool verbose_jni;

  // actions
  bool list_modules;
  bool validate_modules;
  bool describe_module;
  char *module_name; // for describe module
  bool show_module_resolution;

  int print_help;
  int print_version;

  bool print_module_resolution;
};

struct yj_java_init_fn {
  void *handle;
  jint (*GetDefaultJavaVMInitArgs)(void *args);
  jint (*CreateJavaVM)(JavaVM **pvm, void **penv, void *args);
  jint (*GetCreatedJavaVMs)(JavaVM **vms, jsize s, jsize *);
};

struct yj_java_runtime {
  char *name;
  char *home;
  char *libjvm_path;
  char *version;
  int major_version;
  jint jni_version;
  char *full_version;
};

YJ_PUBLIC yj_result yj_parse_run_args(int argc, char **argv,
                                      struct yj_run_args *args);

YJ_PUBLIC yj_result yj_free_run_args(struct yj_run_args *args);

YJ_PUBLIC yj_result yj_run(struct yj_java_runtime *runtime,
                           struct yj_run_args *args);

YJ_PUBLIC yj_result yj_run_async(struct yj_java_runtime *runtime,
                                 struct yj_run_args *args);

YJ_PUBLIC yj_result yj_create_runtime(char *home,
                                      struct yj_java_runtime *runtime);

YJ_PUBLIC yj_result yj_find_runtime(struct yj_java_runtime *runtime);

YJ_PUBLIC yj_result yj_free_runtime(struct yj_java_runtime * runtime);

YJ_PUBLIC yj_result yj_java_discovery(char *base_path,
                                      struct yj_java_runtime **out,
                                      size_t *out_len);

#endif /* YAJAVA_H */

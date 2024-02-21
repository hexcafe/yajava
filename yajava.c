#include "yajava.h"
#include "trace.h"

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <dlfcn.h>
#include <unistd.h>

#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <jni.h>
#include <jni_md.h>

#define YJ_ERR_NULL -1
#define YJ_ERR_NO_RUNTIME -2
#define YJ_ERR_JNI_ENV -3
#define YJ_ERR_JAVA -4
#define YJ_ERR_NO_FILE -5
#define YJ_ERR_ARGS -6
#define YJ_ERR_RUNTIME -7
#define YJ_ERR_DYN_BIND -8

#define JNI_VERSION_1_1 0x00010001
#define JNI_VERSION_1_2 0x00010002
#define JNI_VERSION_1_4 0x00010004
#define JNI_VERSION_1_6 0x00010006
#define JNI_VERSION_1_8 0x00010008
#define JNI_VERSION_9 0x00090000
#define JNI_VERSION_10 0x000a0000
#define JNI_VERSION_19 0x00130000
#define JNI_VERSION_20 0x00140000
#define JNI_VERSION_21 0x00150000

/*
  some useful java property keys, used by java -XshowSettings

  java.class.path
  java.class.version
  java.home
  java.io.tmpdir
  java.library.path
  java.runtime.name
  java.runtime.version
  java.specification.name
  java.specification.vendor
  java.specification.version
  java.vendor
  java.vendor.url
  java.vendor.url.bug
  java.version
  java.version.date
  java.vm.compressedOopsMode
  java.vm.info
  java.vm.name
  java.vm.specification.name
  java.vm.specification.vendor
  java.vm.specification.version
  java.vm.vendor
  java.vm.version
*/

#ifdef __linux__
// clang-format off
#define LIB_JVM_PATH                                                           \
  "%s/lib/server/libjvm.so",                                                   \
  "%s/lib/amd64/server/libjvm.so",                                             \
  "%s/jre/lib/amd64/server/libjvm.so"
// clang-format on
#else
#error platform not supported yet
#endif

#ifdef _WIN32
#define FILE_PATH_SEPRATOR '\\'
#else
#define FILE_PATH_SEPRATOR '/'
#endif

#define CLASSPATH_BUF_SIZE 32
#define CLASSPATH_SEPRATOR ":"

typedef bool (*list_comparator)(void *list_data, void *user_data);
typedef void (*list_deallocator)(void *data);
struct list_node {
  void *data;
  struct list_node *next;
  struct list_node *prev;
};
struct list {
  size_t len;
  struct list_node *head;
  struct list_node *tail;
};
struct list *list_new();
void list_free(struct list *list, list_deallocator free);
void list_add(struct list *list, void *data);
bool list_contains(struct list *list, void *data, list_comparator comparator);
bool list_compare_str(void *list_data, void *user_data);
#define list_each(var, list, idx, prog)                                        \
  do {                                                                         \
    int idx = -1;                                                              \
    struct list_node *node = NULL;                                             \
    node = list->head;                                                         \
    while (node != NULL) {                                                     \
      idx++;                                                                   \
      var = node->data;                                                        \
      prog;                                                                    \
      node = node->next;                                                       \
    }                                                                          \
  } while (0)

struct jvm_home_path {
  char home[PATH_MAX];
  char lib_path[PATH_MAX];
};
struct jvm_opt_arr {
  JavaVMOption *opts;
  int len;
  int maxlen;
};
struct jvm_ipc_runtime { // runtime helper struct for ipc data exchange
  char name[100];
  char home[PATH_MAX];
  char libjvm_path[PATH_MAX];
  int major_version;
  jint jni_version;
  char full_version[100];
  char version[10];
};

int jvm_runtime_compare(const void *a, const void *b);
int jvm_print_version(JNIEnv *env, int version, struct yj_run_args *args);
bool jvm_find_lib(char *home, char *lib_path, size_t maxlen);
bool jvm_bind_init_fn(struct yj_java_init_fn *fn, char *lib_path);
bool jvm_retrive_version(JNIEnv *env, char **out);
bool jvm_compair_home_lib_pair(void *list_data, void *user_data);
void jvm_exec_main_class(struct yj_run_args *args, JNIEnv *env);
void jvm_print_args(JavaVMInitArgs *args);
bool jvm_opt_arr_add(struct jvm_opt_arr *arr, char *opt, char *extra);
bool jvm_create_runtime(char *home, char *lib_path,
                        struct yj_java_runtime *runtime);
bool jvm_create_runtime_fork(char *home, char *lib_path,
                             struct yj_java_runtime *runtime);
jclass jvm_find_main_class(struct yj_run_args *args, JNIEnv *env);

bool file_exists(const char *path);
bool file_abs(struct dirent *dir, const char *base, char *out, size_t maxlen);
bool file_is_dir(const char *path);
bool file_is_file(const char *path);

struct arg_ctx {
  struct yj_run_args *args;
  int argc;
  char **argv;
  int consumed;
  char *cur_arg;
};

#define arg_match(arg, ...) arg_match_any(arg, __VA_ARGS__, 0)
int arg_parse_run_options(struct arg_ctx *ctx);
int arg_parse_classpathes(char *in, struct list *list);
char *arg_pop(struct arg_ctx *ctx);
char *arg_pop_value(struct arg_ctx *ctx);
void arg_back(struct arg_ctx *ctx);
bool arg_has(struct arg_ctx *ctx);
bool arg_build_java_opts(struct yj_java_runtime *runtime,
                         struct yj_run_args *args, JavaVMInitArgs *out);
bool arg_match_any(char *arg, ...);
bool arg_match_start(char *arg, char *start);
bool arg_is_long(char *arg);

// Utilities
// Some use full macros
#define SAFE_FREE(v)                                                           \
  if ((v) != NULL) {                                                           \
    free(v);                                                                   \
    v = NULL;                                                                  \
  }

#define SAFE_STRCPY(dst, src, len)                                             \
  do {                                                                         \
    if (src == NULL || dst == NULL) {                                          \
      break;                                                                   \
    }                                                                          \
    memcpy(dst, src, len);                                                     \
  } while (0)

#define SAFE_STRDUP(src) ((src) == NULL ? NULL : strdup(src))

#define ERROR_LOG(...) fprintf(stderr, __VA_ARGS__);

// Public functions
// Exposed with yajava.h

yj_result yj_parse_run_args(int argc, char **argv, struct yj_run_args *args) {

  if (argv == 0 || argv == NULL) {
    return YJ_ERR_NULL;
  }
  int err_code = 0;
  memset(args, 0, sizeof(struct yj_run_args));

  TRACE_ONLY({
    TRACE("run args <%d>:", argc);
    for (int i = 0; i < argc; i++) {
      TRACE("  arg[%d]: %s", i, argv[i]);
    }
  })

  struct arg_ctx pc = {0};
  pc.args = args;
  pc.argc = argc;
  pc.argv = argv;
  pc.consumed = 0;

#define OPTS 1
#define MAIN 2
#define ARGS 3

  int pos = OPTS;
  while (arg_has(&pc)) {
    char *arg = arg_pop(&pc);
    pc.cur_arg = arg;

    if (arg == NULL) {
      break;
    }

    if (strlen(arg) == 0) {
      continue; // skip empty argument
    }

    // parse jar
    if (pos == OPTS && arg_match(arg, "-jar")) {
      char *jarfile = arg_pop_value(&pc);
      if (pc.args->jarfile != NULL) {
        free(pc.args->jarfile);
      }
      pc.args->app_jar = strdup(jarfile);
      pos = ARGS;
    } else if (pos == OPTS && arg_match(arg, "-m", "--module")) {
      char *module_name = arg_pop_value(&pc);
      pc.args->app_module = module_name;
      // TODO <module>[/<mainclass>] extract main class
      pos = ARGS;
    } else if (pos == OPTS && arg[0] == '-') { // options
      // class path
      if (arg_match(arg, "-cp", "-classpath", "--class-path", "--classpath")) {

        char *classpath = arg_pop_value(&pc);
        struct list *classpath_list = list_new();

        if (arg_parse_classpathes(classpath, classpath_list) != 0) {
          list_free(classpath_list, free);
          return YJ_ERR_ARGS;
        }

        int last_len = args->classpathes_len;
        args->classpathes_len += classpath_list->len;
        args->classpathes =
            realloc(args->classpathes, sizeof(char *) * args->classpathes_len);

        list_each(char *data, classpath_list, idx, {
          TRACE("add classpath: %d -> %s", idx, data);
          args->classpathes[last_len + idx] = strdup(data);
        });
        list_free(classpath_list, free);

      } else if (arg_match(arg, "-?", "-h", "-help")) {
        TRACE("received help arg: %s\n", arg);
        args->print_help = YA_PRINT_HELP | YA_PRINT_ERR;
      } else if (arg_match(arg, "--help")) {
        args->print_help = YA_PRINT_HELP | YA_PRINT_OUT;
      } else if (arg_match(arg, "-X")) {
        args->print_help = YA_PRINT_HELP | YA_PRINT_ERR; // TODO | YA_PRINT_EXT;
      } else if (arg_match(arg, "--help-extra")) {
        args->print_help = YA_PRINT_HELP | YA_PRINT_OUT; // TODO | YA_PRINT_EXT;
      } else if (arg_match(arg, "-version")) {
        args->print_version = YA_PRINT_VERSION | YA_PRINT_ERR;
      } else if (arg_match(arg, "--version")) {
        args->print_version = YA_PRINT_VERSION | YA_PRINT_OUT;
      } else if (arg_match(arg, "-showversion")) {
        args->print_version =
            YA_PRINT_VERSION | YA_PRINT_ERR | YA_PRINT_CONTINUE;
      } else if (arg_match(arg, "--show-version")) {
        args->print_version =
            YA_PRINT_VERSION | YA_PRINT_OUT | YA_PRINT_CONTINUE;
      } else if (arg_match(arg, "--dry-run")) {
        args->dry_run = true;
      } else if (arg_match(arg, "--validate-modules")) {
        args->validate_modules = true;
      } else if (arg_match(arg, "--list-modules")) {
        args->list_modules = true;
      } else if (arg_match(arg, "--list-modules")) {
        args->list_modules = true;
      } else if (arg_match(arg, "--show-module-resolution")) {
        args->show_module_resolution = true;
      } else if (arg_match_start(arg, "-X")) {
        char *opt = strdup(pc.cur_arg);
        args->vmopts =
            realloc(args->vmopts, (args->vmopts_len + 1) * sizeof(char *));
        args->vmopts[args->vmopts_len++] = opt;
      } else if (arg_match_start(arg, "-D")) {
        char *opt = strdup(pc.cur_arg);
        args->sys_props = realloc(args->sys_props,
                                  (args->sys_props_len + 1) * sizeof(char *));
        args->sys_props[args->sys_props_len++] = opt;
      }
    } else if (pos == ARGS) {
      arg_back(&pc);
      int arg_left = pc.argc - pc.consumed;
      if (arg_left > 0) {
        pc.args->app_args = malloc(arg_left * sizeof(char *));
        pc.args->app_args_len = arg_left;

        for (int i = 0; i < arg_left; i++) {
          pc.args->app_args[i] = strdup(pc.argv[pc.consumed + i]);
        }
        pc.consumed += arg_left;
      }
    } else {
      // main class
      pc.args->app_main_class = strdup(arg);
      pos = ARGS;
    }
  }

#undef OPTS
#undef MAIN
#undef ARGS

err:
  return err_code;
}

yj_result yj_create_runtime(char *home, struct yj_java_runtime *runtime) {
  char lib_path[PATH_MAX] = {0};
  TRACE("try create with home: %s", home);
  if (!jvm_find_lib(home, lib_path, PATH_MAX)) {
    return YJ_ERR_NO_RUNTIME;
  }
  if (jvm_create_runtime_fork(home, lib_path, runtime)) {
    return YJ_OK;
  }
  return YJ_ERR_NO_RUNTIME;
}

yj_result yj_find_runtime(struct yj_java_runtime *runtime) {

  char *java_home;

  // from env JAVA_HOME
  java_home = getenv("JAVA_HOME");
  if (java_home != NULL) {
    return yj_create_runtime(java_home, runtime);
  }

  // from configuration
  return YJ_ERR_NO_RUNTIME;
}

yj_result yj_free_runtime(struct yj_java_runtime *runtime) {
  if (runtime == NULL) {
    return YJ_OK;
  }

  SAFE_FREE(runtime->name);
  SAFE_FREE(runtime->home);
  SAFE_FREE(runtime->version);
  SAFE_FREE(runtime->full_version);
  SAFE_FREE(runtime->libjvm_path);

  return YJ_OK;
}

yj_result yj_run_async(struct yj_java_runtime *runtime,
                       struct yj_run_args *args) {
  pid_t pid;
  pid = fork();
  if (pid == 0) {
    if (yj_run(runtime, args)) {
      exit(0);
    }
    exit(1);
  }
  return YJ_OK;
}

yj_result yj_run(struct yj_java_runtime *runtime, struct yj_run_args *args) {

  struct yj_java_init_fn fn = {0};
  JavaVMInitArgs vm_args = {0};
  JavaVM *vm = NULL;
  JNIEnv *env = NULL;
  jint res = JNI_OK;

  if (!jvm_bind_init_fn(&fn, runtime->libjvm_path)) {
    res = YJ_ERR_DYN_BIND;
    goto err;
  }

  if (!arg_build_java_opts(runtime, args, &vm_args)) {
    res = YJ_ERR_ARGS;
    goto err;
  }

  jvm_print_args(&vm_args);
  if ((res = fn.CreateJavaVM(&vm, (void **)&env, &vm_args)) != JNI_OK) {
    printf("create jvm error,err %d\n", res);
    res = YJ_ERR_RUNTIME;
    goto err;
  }

  if ((args->print_version & YA_PRINT_VERSION) == YA_PRINT_VERSION) {
    jvm_print_version(env, runtime->major_version, args);
    if ((args->print_version & YA_PRINT_CONTINUE) != YA_PRINT_CONTINUE) {
      goto err;
    }
  }

  if (!args->dry_run) {
    if (args->app_jar == NULL && args->app_main_class == NULL) {
      printf("help\n");
    } else {
      jvm_exec_main_class(args, env);
    }
  }

  if ((*vm)->DestroyJavaVM(vm)) {
    goto err;
  }

err:
  for (int i = 0; i < vm_args.nOptions; i++) {
    JavaVMOption opt = vm_args.options[i];
    free(opt.optionString);
  }
  free(vm_args.options);

  if (fn.handle != NULL) {
    dlclose(fn.handle);
  }

  return YJ_OK;
}

yj_result yj_java_discovery(char *path, struct yj_java_runtime **out,
                            size_t *out_len) {

  if (out == NULL || out_len == NULL || path == NULL || strlen(path) == 0) {
    return YJ_ERR_NULL;
  }

  DIR *d = NULL;
  struct dirent *dir = NULL;

  char lib_path[PATH_MAX] = {0};
  char base_path[PATH_MAX] = {0};

  int lib_path_maxlen = PATH_MAX;
  int base_max_maxlen = PATH_MAX;

  struct list *runtime_list = list_new();

  if (!file_exists(path)) {
    return YJ_ERR_NO_FILE;
  }

  if (realpath(path, base_path) == NULL) {
    return YJ_ERR_NO_FILE;
  }

  d = opendir(base_path);
  if (d != NULL) {
    while ((dir = readdir(d)) != NULL) {

      // ignore current dir and parent dir
      if (strcmp(".", dir->d_name) == 0 || strcmp("..", dir->d_name) == 0) {
        continue;
      }

      char path[PATH_MAX] = {0};

      // skip non-directory
      if ((dir->d_type != DT_DIR && dir->d_type != DT_LNK) ||
          !file_abs(dir, base_path, path, PATH_MAX) || !file_is_dir(path)) {
        continue;
      }

      if (!jvm_find_lib(path, lib_path, lib_path_maxlen)) {
        continue;
      }

      struct jvm_home_path pair = {0};
      memcpy(&pair.home, path, strlen(path) * sizeof(char));
      memcpy(&pair.lib_path, lib_path, strlen(lib_path) * sizeof(char));

      if (!list_contains(runtime_list, &pair, jvm_compair_home_lib_pair)) {
        struct jvm_home_path *to_save;
        to_save = malloc(sizeof(struct jvm_home_path));
        memset(to_save, 0, sizeof(struct jvm_home_path));
        memcpy(to_save, &pair, sizeof(struct jvm_home_path));

        list_add(runtime_list, to_save);
      }
    }
    closedir(d);
  }

  if (runtime_list != NULL) {
    struct list_node *node = runtime_list->head;
    struct yj_java_runtime *runtimes =
        malloc(sizeof(struct yj_java_runtime) * runtime_list->len);

    int i = 0;
    while (node != NULL) {
      struct jvm_home_path *pair;
      struct yj_java_runtime *ir;

      pair = node->data;

      ir = &runtimes[i];
      jvm_create_runtime_fork(pair->home, pair->lib_path, ir);

      i++;
      node = node->next;
    }
    *out = runtimes;
    *out_len = runtime_list->len;
    list_free(runtime_list, free);
  }

  qsort(*out, *out_len, sizeof(struct yj_java_runtime), jvm_runtime_compare);

  return YJ_OK;
}

yj_result yj_free_run_args(struct yj_run_args *arg) {

  if (arg == NULL) {
    return YJ_OK;
  }

#define SAFE_FREE_ARR(v)                                                       \
  if ((v) != NULL) {                                                           \
    for (int i = 0; i < v##_len; i++) {                                        \
      free(v[i]);                                                              \
      v[i] = NULL;                                                             \
    }                                                                          \
    free(v);                                                                   \
    v = NULL;                                                                  \
    v##_len = 0;                                                               \
  }

  SAFE_FREE(arg->app_jar);
  SAFE_FREE(arg->app_module)
  SAFE_FREE(arg->app_main_class);
  SAFE_FREE(arg->module_name);

  SAFE_FREE_ARR(arg->app_args);

  SAFE_FREE_ARR(arg->vmopts);

  SAFE_FREE_ARR(arg->classpathes);
  SAFE_FREE_ARR(arg->add_modules);
  SAFE_FREE_ARR(arg->module_pathes);
  SAFE_FREE_ARR(arg->upgrade_module_pathes);
  SAFE_FREE_ARR(arg->agentlibs);
  SAFE_FREE_ARR(arg->agentpathes);
  SAFE_FREE_ARR(arg->javagents);
  SAFE_FREE_ARR(arg->native_access_modules);
  SAFE_FREE_ARR(arg->sys_props);

#undef SAFE_FREE_ARR
  return YJ_OK;
}

// JVM
// jvm related functions
bool jvm_find_lib(char *home, char *lib_path, size_t maxlen) {

  char *patterns[] = {LIB_JVM_PATH};
  char buf[PATH_MAX] = {0};
  int buf_len = PATH_MAX;
  char path_len = 0;
  char *pattern = NULL;

  for (int i = 0; i < sizeof(patterns) / sizeof(char *); i++) {
    pattern = patterns[i];
    memset(buf, 0, buf_len);
    snprintf(buf, buf_len, pattern, home);
    if (file_exists(buf) && file_is_file(buf)) {
      path_len = strnlen(buf, buf_len - 1);
      if (path_len > maxlen) {
        continue; // invalid libjvm.so path
      }
      memset(lib_path, 0, maxlen);
      memcpy(lib_path, buf, path_len);
      return true;
    }
  }
  return false;
}

bool jvm_bind_init_fn(struct yj_java_init_fn *fn, char *lib_path) {

  if (lib_path == NULL) {
    return false;
  }

  void *handle;
  memset(fn, 0, sizeof(struct yj_java_init_fn));

  TRACE("dlopen(%s)", lib_path);
  handle = dlopen(lib_path, RTLD_LOCAL | RTLD_NOW);

  fn->handle = handle;
  fn->GetDefaultJavaVMInitArgs = dlsym(handle, "JNI_GetDefaultJavaVMInitArgs");
  fn->CreateJavaVM = dlsym(handle, "JNI_CreateJavaVM");
  fn->GetCreatedJavaVMs = dlsym(handle, "JNI_GetCreatedJavaVMs");

  return true;
}

bool jvm_compair_home_lib_pair(void *list_data, void *user_data) {
  struct jvm_home_path *lpair = list_data;
  struct jvm_home_path *upair = user_data;

  return strncmp(lpair->home, upair->home, PATH_MAX) == 0;
}

jclass jvm_find_main_class(struct yj_run_args *args, JNIEnv *env) {
  jclass helper = (*env)->FindClass(env, "sun/launcher/LauncherHelper");
  jmethodID method =
      (*env)->GetStaticMethodID(env, helper, "checkAndLoadMain",
                                "(ZILjava/lang/String;)Ljava/lang/Class;");

  jclass main_class = NULL;
  if (args->app_jar != NULL) { // jar mode 2
    jstring jar_str = (*env)->NewStringUTF(env, args->app_jar);
    main_class =
        (*env)->CallStaticObjectMethod(env, helper, method, 0, 2, jar_str);
    (*env)->DeleteLocalRef(env, jar_str);
  } else if (args->app_main_class != NULL) { // class method 1
    jstring class_str = (*env)->NewStringUTF(env, args->app_main_class);
    main_class =
        (*env)->CallStaticObjectMethod(env, helper, method, 0, 1, class_str);
    (*env)->DeleteLocalRef(env, class_str);
  }

  return main_class;
}

void jvm_exec_main_class(struct yj_run_args *args, JNIEnv *env) {

  jclass main_class = jvm_find_main_class(args, env);
  if (main_class == NULL) {
    fprintf(stderr, "main class not found!\n");
    return;
  }

  // find main method
  // public static void main(String[] args) ([Ljava/lang/String;)V
  jmethodID main_method = (*env)->GetStaticMethodID(env, main_class, "main",
                                                    "([Ljava/lang/String;)V");
  jclass string_class = (*env)->FindClass(env, "java/lang/String");
  jobjectArray main_args;
  if (args->app_args != NULL && args->app_args_len > 0) {
    main_args =
        (*env)->NewObjectArray(env, args->app_args_len, string_class, NULL);
    for (int i = 0; i < args->app_args_len; i++) {
      char *arg = args->app_args[i];
      jstring arg_obj = (*env)->NewStringUTF(env, arg);
      (*env)->SetObjectArrayElement(env, main_args, i, arg_obj);
    }
  } else {
    main_args = (*env)->NewObjectArray(env, 0, string_class, NULL);
  }

  // build main args array
  (*env)->CallStaticVoidMethod(env, main_class, main_method, main_args);
}

int jvm_print_version(JNIEnv *env, int version, struct yj_run_args *args) {

  jclass ver;
  jmethodID print;
  TRACE("try print version");

  if (env == NULL) {
    printf("invalid JNIEnv: null\n");
    return YJ_ERR_JNI_ENV;
  }

  char *class_name;
  if (version <= 8) {
    class_name = "sun/misc/Version";
  } else {
    class_name = "java/lang/VersionProps";
  }

  ver = (*env)->FindClass(env, class_name);
  if ((*env)->ExceptionOccurred(env)) {
    (*env)->ExceptionDescribe(env);
    return YJ_ERR_JAVA;
  }

  if (version <= 8) {
    print = (*env)->GetStaticMethodID(env, ver, "print", "()V");
    (*env)->CallStaticVoidMethod(env, ver, print);
  } else {
    print = (*env)->GetStaticMethodID(env, ver, "print", "(Z)V");
    jboolean out_err = (args->print_version & YA_PRINT_ERR) == YA_PRINT_ERR;
    (*env)->CallStaticVoidMethod(env, ver, print, out_err);
  }

  if ((*env)->ExceptionOccurred(env)) {
    (*env)->ExceptionDescribe(env);
    return YJ_ERR_JAVA;
  }
  return YJ_OK;
}

void jvm_print_args(JavaVMInitArgs *args) {

  TRACE("args:");
  TRACE("  options[%d]", args->nOptions);
  if (args->nOptions > 0) {
    for (int i = 0; i < args->nOptions; i++) {
      TRACE("    %d: %s", i, args->options[i].optionString);
    }
  }
  TRACE("  version: %d", args->version);
  TRACE("  ignoreUnrecognized: %d", args->ignoreUnrecognized);
}

bool jvm_create_runtime(char *home, char *lib_path,
                        struct yj_java_runtime *runtime) {

  JavaVM *vm = NULL;
  JNIEnv *env = NULL;
  JavaVMInitArgs args = {0};
  struct yj_java_init_fn fn = {0};
  jint res = 0;

  int major_version = 0;
  jint jni_version = 0;
  char *full_version = NULL;

  TRACE("%s", "try bind");
  if (!jvm_bind_init_fn(&fn, lib_path)) {
    printf("error: can not load libjvm.so with path: %s\n", lib_path);
    return false;
  }

  TRACE("%s", "bind ok");
  args.nOptions = 0;
  args.options = NULL;
  args.ignoreUnrecognized = true;
  args.version = JNI_VERSION_1_2;

  TRACE("CreateJavaVM(%d)", args.nOptions);
  if ((res = fn.CreateJavaVM(&vm, (void **)&env, &args)) != JNI_OK) {
    TRACE("CreateJavaVM failed with status: %d", res);
    printf("create java vm runtime failed\n");
    return false;
  }
  TRACE("CreateJavaVM ok: %p", vm);

  jni_version = (*env)->GetVersion(env);
  TRACE("jni version: 0x%x", jni_version);
  if (!jvm_retrive_version(env, &full_version)) {
    printf("fetch java version failed\n");
    return false;
  }

  TRACE("DestroyJavaVM(%p)", vm);
  (*vm)->DetachCurrentThread(vm);
  (*vm)->DestroyJavaVM(vm);

  dlclose(fn.handle);

  // parse major version
  //  1.8.0  -> 8
  //  17.0.1 -> 17

  char *to_free, *str, *saveptr, *token, *ver;

  to_free = str = strdup(full_version);
  token = strtok_r(str, ".", &saveptr);
  str = NULL;
  if (strcmp("1", token) == 0) {
    ver = strtok_r(NULL, ".", &saveptr);
  } else {
    ver = token;
  }

  major_version = atoi(ver);

  // write output runtime
  memset(runtime, 0, sizeof(struct yj_java_runtime));
  runtime->home = strdup(home);
  runtime->version = strdup(ver);
  runtime->full_version = full_version;
  runtime->libjvm_path = strdup(lib_path);
  runtime->jni_version = jni_version;
  runtime->major_version = major_version;

  free(to_free);
  return true;
}

bool jvm_create_runtime_fork(char *home, char *lib_path,
                             struct yj_java_runtime *ir) {
  pid_t pid;
  int shm_id;

  TRACE("create_runtime(%s, %s)", home, lib_path);
  // SHARED MEM
  shm_id =
      shmget(IPC_PRIVATE, sizeof(struct jvm_ipc_runtime), 0666 | IPC_CREAT);
  TRACE("shmget(%lu)", sizeof(struct jvm_ipc_runtime));
  if (shm_id < 0) {
    printf("error create shared memory: %d\n", shm_id);
    return false;
  }

  TRACE("%s", "fork()");
  pid = fork();
  if (pid == 0) {
    // child process
    struct yj_java_runtime r = {0};
    struct jvm_ipc_runtime * or ;
    if (!jvm_create_runtime(home, lib_path, &r)) {
      printf("create runtime falied\n");
    }

    // ---> SHARED MEM
    // START
    or = shmat(shm_id, NULL, 0);
    if (or == (void *)-1) {
      printf("attach shared memory failed\n");
      exit(0);
    }

    // write memory
    memset(or, 0, sizeof(struct jvm_ipc_runtime));
    SAFE_STRCPY(or->home, r.home, strlen(r.home));
    SAFE_STRCPY(or->libjvm_path, r.libjvm_path, strlen(r.libjvm_path));
    SAFE_STRCPY(or->full_version, r.full_version, strlen(r.full_version));
    SAFE_STRCPY(or->name, r.name, strlen(r.name));
    SAFE_STRCPY(or->version, r.version, strlen(r.version));

    or->major_version = r.major_version;
    or->jni_version = r.jni_version;

    if (shmdt(or) == -1) {
      printf("detach shared memory failed\n");
      exit(0);
    }
    // END
    // ---> SHARED MEM

    // release runtime
    SAFE_FREE(r.home);
    SAFE_FREE(r.libjvm_path);
    SAFE_FREE(r.name);
    SAFE_FREE(r.full_version);
    SAFE_FREE(r.version);

    exit(0);
  } else if (pid == -1) {
    printf("failed to retrive java runtime, err: create sub-process\n");
  } else if (pid > 0) {
    wait(NULL);

    struct jvm_ipc_runtime * or ;
    // ---> SHARED MEM
    // START

    or = shmat(shm_id, NULL, 0);
    if (or == (void *)-1) {
      printf("attach memory failed in main thread\n");
      return false;
    }

    memset(ir, 0, sizeof(struct yj_java_runtime));

    ir->home = SAFE_STRDUP(or->home);
    ir->name = SAFE_STRDUP(or->name);
    ir->version = SAFE_STRDUP(or->version);
    ir->full_version = SAFE_STRDUP(or->full_version);
    ir->libjvm_path = SAFE_STRDUP(or->libjvm_path);
    ir->jni_version = or->jni_version;
    ir->major_version = or->major_version;

    if (shmdt(or) == -1) {
      printf("detach memory failed in main thread\n");
      return false;
    }
    // END
    // ---> SHARED MEM
  }

  if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
    printf("delete shared memory failed\n");
    return false;
  }
  return true;
}

bool jvm_unwrap_str(JNIEnv *env, jstring jstr, char **out) {
  if (env == NULL || jstr == NULL || out == NULL) {
    return false;
  }

  jboolean copy = JNI_FALSE;
  const char *str = (*env)->GetStringUTFChars(env, jstr, &copy);

  *out = strdup(str);
  (*env)->ReleaseStringUTFChars(env, jstr, str);

  return true;
}

char *jvm_get_sys_props(JNIEnv *env, const char *key) {
  static jclass sys_cls;
  jmethodID get_prop_m;

  if (env == NULL) {
    printf("invalid JNIEnv: null\n");
    return NULL;
  }

  sys_cls = (*env)->FindClass(env, "java/lang/System");
  get_prop_m = (*env)->GetStaticMethodID(
      env, sys_cls, "getProperty", "(Ljava/lang/String;)Ljava/lang/String;");

  assert(sys_cls != NULL);
  assert(get_prop_m != NULL);

  jstring jkey = (*env)->NewStringUTF(env, key);
  jstring jvalue =
      (*env)->CallStaticObjectMethod(env, sys_cls, get_prop_m, jkey);

  char *value = NULL;

  if (jvalue != NULL) {
    jvm_unwrap_str(env, jvalue, &value);
    (*env)->DeleteLocalRef(env, jvalue);
  }
  (*env)->DeleteLocalRef(env, jkey);

  return value;
}

bool jvm_retrive_version(JNIEnv *env, char **out) {
  char *version_str = jvm_get_sys_props(env, "java.version");
  if (version_str == NULL) {
    return false;
  }
  *out = version_str;
  return true;
}

int jvm_runtime_compare(const void *a, const void *b) {
  if (a == NULL) {
    return -1;
  }

  if (b == NULL) {
    return 1;
  }

  const struct yj_java_runtime *r1 = a;
  const struct yj_java_runtime *r2 = b;

  // compare major version
  int m1 = r1->major_version;
  int m2 = r1->major_version;

  if (m1 == m2) {
    return strcmp(r1->full_version, r2->full_version);
  }

  return m1 - m2;
}

bool jvm_opt_arr_add(struct jvm_opt_arr *arr, char *opt, char *extra) {

  if (arr == NULL || opt == NULL) {
    return false;
  }

  if (arr->maxlen <= arr->len) {
    arr->maxlen += 5;
    arr->opts = realloc(arr->opts, arr->maxlen * sizeof(JavaVMOption));
  }

  JavaVMOption *o = &arr->opts[arr->len++];
  o->optionString = opt;
  o->extraInfo = extra;

  return true;
}

// FILE
// file utilities
bool file_exists(const char *path) {
  struct stat st;
  if (path == NULL) {
    return false;
  }

  if (stat(path, &st) != 0) {
    return false;
  }
  return true;
}

bool file_abs(struct dirent *dir, const char *base, char *out, size_t maxlen) {

  char path[PATH_MAX] = {0};
  char tmp_path[PATH_MAX] = {0};
  char path_len = 0;
  snprintf(path, PATH_MAX, "%s%c%s", base, FILE_PATH_SEPRATOR, dir->d_name);

  memcpy(tmp_path, path, PATH_MAX);
  if (realpath(path, path) == NULL) {
    return false;
  }

  path_len = strlen(path);
  if (maxlen < path_len) {
    return false;
  }

  memcpy(out, path, path_len);
  out[path_len] = '\0';
  return true;
}

bool file_is_dir(const char *path) {

  struct stat st;
  if (path == NULL) {
    return false;
  }

  if (stat(path, &st) != 0) {
    return false;
  }

  return S_ISDIR(st.st_mode);
}

bool file_is_file(const char *path) {
  struct stat st;
  if (path == NULL) {
    return false;
  }

  if (stat(path, &st) != 0) {
    return false;
  }

  return S_ISREG(st.st_mode);
}

// LIST
// list related functions
struct list *list_new() {
  struct list *list = malloc(sizeof(struct list));
  memset(list, 0, sizeof(struct list));
  return list;
}

void list_free(struct list *list, list_deallocator deallocator) {

  if (list == NULL) {
    return;
  }

  struct list_node *node = list->head;
  while (node != NULL) {
    struct list_node *next = node->next;
    if (deallocator != NULL && node->data != NULL) {
      deallocator(node->data);
    }

    node->data = NULL;
    node->prev = NULL;
    node->next = NULL;
    free(node);
    node = next;
  }

  list->head = NULL;
  list->tail = NULL;
  list->len = 0;
  free(list);
}

void list_add(struct list *list, void *data) {

  if (list == NULL) {
    return;
  }

  struct list_node *nn;
  nn = malloc(sizeof(struct list_node));
  nn->data = data;
  nn->next = NULL;
  nn->prev = NULL;

  if (list->tail == NULL) {
    list->tail = nn;
  } else {
    list->tail->next = nn;
    nn->prev = list->tail;
    list->tail = nn;
  }

  if (list->head == NULL) {
    list->head = list->tail;
  }

  list->len++;
}

bool list_contains(struct list *list, void *data, list_comparator comparator) {
  if (list == NULL) {
    return false;
  }
  struct list_node *node = list->head;
  while (node != NULL) {
    void *ld = node->data;
    if ((comparator == NULL && ld == data) || (comparator(ld, data))) {
      return true;
    }
    node = node->next;
  }
  return false;
}

bool list_compare_str(void *list_data, void *user_data) {
  return strcmp(list_data, user_data) == 0;
}

// ARG
// parsing arguments
bool arg_match_any(char *s, ...) {

  va_list args;
  bool match = false;
  va_start(args, s);

  while (true) {
    char *m = va_arg(args, char *);
    if (m == NULL) {
      break;
    }

    int s_len = strlen(s);
    int m_len = strlen(m);

    // 1. extractly match
    // 2. long options with equal sign --classpath=path/to/jar
    // clang-format off
    if ((s_len == m_len && strncmp(s, m, s_len) == 0) ||
        (s_len > m_len && arg_is_long(m) &&
         arg_match_start(s, m) && s[m_len] == '=')) {
      match = true;
      break;
    }
    // clang-format on
  }

  va_end(args);
  return match;
}

bool arg_match_start(char *s, char *start) {
  return strncmp(s, start, strlen(start)) == 0;
}

int arg_parse_classpathes(char *in, struct list *list) {

  if (in == NULL || list == NULL) {
    return YJ_ERR_NULL;
  }

  char *to_free, *str, *saveptr, *token, *buf;
  int i = 0;
  int token_len = 0;

  to_free = str = strdup(in);
  while (true) {
    token = strtok_r(str, CLASSPATH_SEPRATOR, &saveptr);
    str = NULL;
    if (token == NULL)
      break;

    buf = strdup(token);
    if (!list_contains(list, token, list_compare_str)) {
      list_add(list, buf);
    }
    i++;
  }

  free(to_free);
  return YJ_OK;
}

bool arg_build_java_opts(struct yj_java_runtime *runtime,
                         struct yj_run_args *args, JavaVMInitArgs *out) {

  if (args == NULL || out == NULL) {
    return false;
  }

  // handle classpathes
  static const char format[] = "-Djava.class.path=%s";
  static const int format_len = 18;

  struct jvm_opt_arr opts = {0};

  char *cp = NULL;
  int cp_len = 0;
  if (args->app_jar != NULL && strlen(args->app_jar) > 0) {

    // check file exists
    if (!file_exists(args->app_jar)) {
      printf("jar file not found: %s\n", args->app_jar);
      return false;
    }

    int jar_len = strlen(args->app_jar);
    cp_len = format_len + jar_len + 1;
    cp = malloc((cp_len) * sizeof(char));
    snprintf(cp, cp_len, format, args->app_jar);
  } else if (args->classpathes != NULL && args->classpathes_len > 0) {

    // calculate the totoal classpath string's length
    for (int i = 0; i < args->classpathes_len; i++) {
      char *path = args->classpathes[i];
      size_t path_len = strlen(path);
      if (path == NULL || path_len == 0 || path_len > PATH_MAX) {
        break;
      }

      // check file exists
      if (!file_exists(path)) {
        printf("path not found: %s\n", path);
        return false;
      }

      cp_len += path_len + 1;
    }

    cp = malloc((cp_len + format_len + 1) * sizeof(char));

    memcpy(cp, format, format_len * sizeof(char));
    size_t pos = format_len;
    for (int i = 0; i < args->classpathes_len; i++) {
      char *path = args->classpathes[i];
      size_t path_len = strlen(path);
      if (path == NULL || path_len == 0 || path_len > PATH_MAX) {
        break;
      }
      if (i > 0) {
        cp[pos++] = ':';
      }
      memcpy(cp + pos, path, path_len);
      pos += path_len;
    }
  }

  if (cp != NULL) {
    jvm_opt_arr_add(&opts, cp, NULL);
  }

  if (args->sys_props != NULL && args->sys_props_len > 0) {
    for (int i = 0; i < args->sys_props_len; i++) {
      char *props = args->sys_props[i];
      if (props == NULL || strlen(props) == 0) {
        break;
      }
      char *tmp = strdup(props);
      jvm_opt_arr_add(&opts, tmp, NULL);
    }
  }

  if (args->vmopts != NULL && args->vmopts_len) {
    for (int i = 0; i < args->vmopts_len; i++) {
      char *props = args->vmopts[i];
      if (props == NULL || strlen(props) == 0) {
        break;
      }
      char *tmp = strdup(props);
      jvm_opt_arr_add(&opts, tmp, NULL);
    }
  }

  out->nOptions = opts.len;
  out->options = opts.opts;
  out->version = (runtime == NULL || runtime->jni_version <= 0)
                     ? JNI_VERSION_1_2
                     : runtime->jni_version;
  out->ignoreUnrecognized = false;

  return true;
}

inline bool arg_is_long(char *arg) {
  return strlen(arg) > 2 && arg[0] == '-' & arg[1] == '-';
}

inline char *arg_pop(struct arg_ctx *ctx) { return ctx->argv[ctx->consumed++]; }

inline void arg_back(struct arg_ctx *ctx) {
  if (ctx->consumed > 0) {
    ctx->consumed--;
  }
}

inline char *arg_pop_value(struct arg_ctx *ctx) {
  if (arg_is_long(ctx->cur_arg)) {
    char *v = strchr(ctx->cur_arg, '=');
    if (v != NULL) {
      return v + 1;
    }
  }
  return arg_pop(ctx);
}

inline bool arg_has(struct arg_ctx *ctx) { return ctx->consumed < ctx->argc; }

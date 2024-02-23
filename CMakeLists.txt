cmake_minimum_required(VERSION 3.0)
project(yajava LANGUAGES C VERSION 1.0)

set(CMAKE_C_STANDARD 99)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_TRACE OFF)
option(UNIT_TEST    OFF)

if (${ENABLE_TRACE})
  add_definitions(-DENABLE_TRACE=1)
endif()

# Checks
include(CheckIncludeFile)
macro(require_header INCLUDE_FILE HAVE_FILE)
  check_include_file(${INCLUDE_FILE} ${HAVE_FILE})
  IF(NOT ${HAVE_FILE})
    unset(HAVE_FILE CACHE)
    message( FATAL_ERROR "${INCLUDE_FILE} is not found" )
  ENDIF()
endmacro()

require_header("dlfcn.h" HAS_DLFCN_H)
require_header("libgen.h" HAS_LIBGEN_H)
require_header("dirent.h" HAS_DIRENT_H)
require_header("unistd.h" HAS_UNISTD_H)
require_header("sys/shm.h" HAS_SYS_SHM_H)
require_header("sys/stat.h" HAS_SYS_STAT_H)
require_header("sys/wait.h" HAS_SYS_WAIT_H)

find_package(JNI REQUIRED)
if(${JNI_FOUND})
  include_directories(${JNI_INCLUDE_DIRS})
endif()

add_executable(yajava main.c yajava.c trace.c)

install(TARGETS yajava RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if (${UNIT_TEST})
  include(CTest)
  add_executable(test_arg test/test_arg.c yajava.c trace.c)
  add_test(NAME test_arg COMMAND test_arg)

  add_executable(test_discovery test/test_discovery.c yajava.c trace.c)
  add_test(NAME test_discovery COMMAND test_discovery)

  if (DEFINED ENV{JAVA_HOME})
    set(JAVA_CMD $ENV{JAVA_HOME}/bin/java)
    set(JAVAC_CMD $ENV{JAVA_HOME}/bin/javac)
  else()
    set(JAVA_CMD java)
    set(JAVAC_CMD javac)
  endif()

  add_test(NAME compile_test_class COMMAND ${JAVAC_CMD} -d ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/test/Hello.java)
  add_test(NAME build_test_jar COMMAND jar cfe ${CMAKE_BINARY_DIR}/hello.jar com.example.Hello com/example/Hello.class)
  add_test(NAME test_jar COMMAND $<TARGET_FILE:yajava> -jar hello.jar)
  set_tests_properties(test_jar PROPERTIES DEPENDS "compile_test_class;build_test_jar")
endif()

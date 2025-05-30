/*
  Copyright (C) 2013-2025 Chromebrew Authors

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#ifndef MAIN_H_INCLUDED
#define MAIN_H_INCLUDED

#define _GNU_SOURCE
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <spawn.h>
#include <unistd.h>
#include <gnu/libc-version.h>
#include <linux/limits.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/utsname.h>

#include "legacy-stat.h"

#ifndef SYS_memfd_create
#if defined(__arm__)
#define SYS_memfd_create 385
#elif defined(__i386__)
#define SYS_memfd_create 356
#elif defined(__aarch64__)
#define SYS_memfd_create 279
#elif defined(__x86_64__)
#define SYS_memfd_create 319
#endif
#endif

#ifndef CREW_PREFIX
#define CREW_PREFIX "/usr/local"
#endif

#ifndef CREW_GLIBC_INTERPRETER
#if defined(__arm__) || defined(__aarch64__)
#define CREW_GLIBC_INTERPRETER "/usr/local/opt/glibc-libs/ld-linux-armhf.so.3"
#elif defined(__i386__)
#define CREW_GLIBC_INTERPRETER "/usr/local/opt/glibc-libs/ld-linux.so.2"
#elif defined(__x86_64__)
#define CREW_GLIBC_INTERPRETER "/usr/local/opt/glibc-libs/ld-linux-x86-64.so.2"
#endif
#endif

#ifndef CREW_GLIBC_IS_64BIT
#if defined(__x86_64__)
#define CREW_GLIBC_IS_64BIT true
#else
#define CREW_GLIBC_IS_64BIT false
#endif
#endif

#ifndef PROMPT_NAME
#if defined(__aarch64__) || defined(__x86_64__)
#define PROMPT_NAME "crew-preload (64-bit)"
#else
#define PROMPT_NAME "crew-preload"
#endif
#endif

struct ElfInfo {
  bool is_64bit;
  bool is_dyn_exec;
  int  size;
  char *interpreter;
  void *pt_interp_section;
};

extern char **environ;

extern bool disabled, initialized;

extern int (*orig_execl)(const char *path, const char *arg, ...);
extern int (*orig_execle)(const char *path, const char *arg, ...);
extern int (*orig_execlp)(const char *path, const char *arg, ...);
extern int (*orig_execv)(const char *path, char *const *argv);
extern int (*orig_execve)(const char *path, char *const *argv, char *const *envp);
extern int (*orig_execvp)(const char *file, char *const *argv);
extern int (*orig_execvpe)(const char *file, char *const *argv, char *const *envp);
extern int (*orig_posix_spawn)(pid_t *pid, const char *path,
                               const posix_spawn_file_actions_t *file_actions,
                               const posix_spawnattr_t *attrp,
                               char *const *argv, char *const *envp);
extern int (*orig_posix_spawnp)(pid_t *pid, const char *file,
                                const posix_spawn_file_actions_t *file_actions,
                                const posix_spawnattr_t *attrp,
                                char *const *argv, char *const *envp);

void preload_init(void) __attribute__ ((constructor));
int  count_args(va_list argp);
void va2array(va_list argp, int argc, char **argv);
int  copy2array(char * const* src, char **dest, int offset);
int  exec_wrapper(const char *path_or_name, char *const *argv, char *const *envp,
                  bool perform_path_search, void *pid, const void *file_actions, const void *attrp);

#endif

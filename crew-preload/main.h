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
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <spawn.h>
#include <unistd.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <sys/wait.h>

extern char **environ;

extern bool initialized, compile_hacks, no_crew_shell, no_mold, verbose;
extern char glibc_files[128][PATH_MAX];
extern int  glibc_file_count;

extern const char *ld_executables[];
extern const char *system_exe_path[];

extern int   (*orig_access)(const char *pathname, int mode);
extern int   (*orig_stat)(const char *restrict pathname, struct stat *restrict statbuf);
extern int   (*orig_statx)(int dirfd, const char *restrict pathname, int flags, unsigned int mask, struct statx *restrict statxbuf);
extern int   (*orig_lstat)(const char *restrict pathname, struct stat *restrict statbuf);
extern int   (*orig_open)(const char *pathname, int flags, mode_t mode);
extern int   (*orig_open64)(const char *pathname, int flags, mode_t mode);
extern FILE* (*orig_fopen)(const char *filename, const char *mode);
extern FILE* (*orig_fopen64)(const char *filename, const char *mode);
extern int   (*orig_execve)(const char *pathname, char *const *argv, char *const *envp);
extern int   (*orig_posix_spawn)(pid_t *pid, const char *pathname,
                                 const posix_spawn_file_actions_t *file_actions,
                                 const posix_spawnattr_t *attrp,
                                 char *const *argv, char *const *envp);

void init(void);
void replace_path_if_needed(const char *orig_path, char *final_path);
int  count_args(va_list argp);
void va2array(va_list argp, int argc, char **argv);
int  copy2array(char * const* src, char **dest, int offset);
int  search_in_path(const char *file, char *result);
bool is_dynamic_executable(char *pathname);
int  exec_wrapper(const char *path_or_name, char *const *argv, char *const *envp,
                  bool perform_path_search, void *pid, const void *file_actions, const void *attrp);

#endif

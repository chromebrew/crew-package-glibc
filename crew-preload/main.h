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

void init(void);
int  count_args(va_list argp);
void va2array(va_list argp, int argc, char **argv);
int  copy2array(char * const* src, char **dest, int offset);
int  search_in_path(const char *file, char *result);
bool is_dynamic_executable(char *pathname);
int  exec_wrapper(const char *path_or_name, char *const *argv, char *const *envp,
                  bool perform_path_search, void *pid, const void *file_actions, const void *attrp);

#endif

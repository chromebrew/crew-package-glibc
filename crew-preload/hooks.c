/*
  Copyright (C) 2013-2025 Chromebrew Authors

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#include "./main.h"

int access(const char *pathname, int mode) {
  char final_path[PATH_MAX];

  if (!initialized) init();
  if (compile_hacks) replace_path_if_needed(pathname, final_path);

  return orig_access(compile_hacks ? final_path : pathname, mode);
}

int stat(const char *restrict pathname, struct stat *restrict statbuf) {
  char final_path[PATH_MAX];

  if (!initialized) init();
  if (compile_hacks) replace_path_if_needed(pathname, final_path);

  return orig_stat(compile_hacks ? final_path : pathname, statbuf);
}

int lstat(const char *restrict pathname, struct stat *restrict statbuf) {
  char final_path[PATH_MAX];

  if (!initialized) init();
  if (compile_hacks) replace_path_if_needed(pathname, final_path);

  return orig_lstat(compile_hacks ? final_path : pathname, statbuf);
}

int statx(int dirfd, const char *restrict pathname, int flags, unsigned int mask, struct statx *restrict statxbuf) {
  char final_path[PATH_MAX];

  if (!initialized) init();
  if (compile_hacks) replace_path_if_needed(pathname, final_path);

  return orig_statx(dirfd, compile_hacks ? final_path : pathname, flags, mask, statxbuf);
}

int open(const char *path, int flags, ...) {
  char final_path[PATH_MAX];
  va_list argp;
  va_start(argp, flags);
  mode_t mode = va_arg(argp, mode_t);
  va_end(argp);

  if (!initialized) init();
  if (compile_hacks) replace_path_if_needed(path, final_path);

  return orig_open(compile_hacks ? final_path : path, flags, mode);
}

int open64(const char *path, int flags, ...) {
  char final_path[PATH_MAX];
  va_list argp;
  va_start(argp, flags);
  mode_t mode = va_arg(argp, mode_t);
  va_end(argp);

  if (!initialized) init();
  if (compile_hacks) replace_path_if_needed(path, final_path);

  return orig_open64(compile_hacks ? final_path : path, flags, mode);
}

FILE *fopen(const char *path, const char *mode) {
  char final_path[PATH_MAX];

  if (!initialized) init();
  if (compile_hacks) replace_path_if_needed(path, final_path);

  return orig_fopen(compile_hacks ? final_path : path, mode);
}

FILE *fopen64(const char *path, const char *mode) {
  char final_path[PATH_MAX];

  if (!initialized) init();
  if (compile_hacks) replace_path_if_needed(path, final_path);

  return orig_fopen64(compile_hacks ? final_path : path, mode);
}

int execl(const char *path, const char *arg, ...) {
  char    **argv;
  int     argc;
  va_list argp;

  if (!initialized) init();

  va_start(argp, arg);
  argc    = count_args(argp);
  argv    = alloca(argc * sizeof(char *));
  argv[0] = (char *) arg;

  va2array(argp, argc, argv);
  va_end(argp);

  return exec_wrapper(path, argv, environ, false, NULL, NULL, NULL);
}

int execle(const char *path, const char *arg, ...) {
  char    **argv, **envp;
  int     argc;
  va_list argp;

  if (!initialized) init();

  va_start(argp, arg);
  argc    = count_args(argp);
  argv    = alloca(argc * sizeof(char *));
  argv[0] = (char *) arg;

  va2array(argp, argc, argv);
  envp = va_arg(argp, char **);
  va_end(argp);

  return exec_wrapper(path, argv, envp, false, NULL, NULL, NULL);
}

int execlp(const char *path, const char *arg, ...) {
  char    **argv;
  int     argc;
  va_list argp;

  if (!initialized) init();

  va_start(argp, arg);
  argc    = count_args(argp);
  argv    = alloca(argc * sizeof(char *));
  argv[0] = (char *) arg;

  va2array(argp, argc, argv);
  va_end(argp);

  return exec_wrapper(path, argv, environ, true, NULL, NULL, NULL);
}

int execv(const char *path, char *const *argv) {
  if (!initialized) init();
  return exec_wrapper(path, argv, environ, false, NULL, NULL, NULL);
}

int execve(const char *path, char *const *argv, char *const *envp) {
  if (!initialized) init();
  return exec_wrapper(path, argv, envp, false, NULL, NULL, NULL);
}

int execvp(const char *file, char *const *argv) {
  if (!initialized) init();
  return exec_wrapper(file, argv, environ, true, NULL, NULL, NULL);
}

int execvpe(const char *file, char *const *argv, char *const *envp) {
  if (!initialized) init();
  return exec_wrapper(file, argv, envp, true, NULL, NULL, NULL);
}

int posix_spawn(pid_t *pid, const char *path,
                const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *attrp,
                char *const *argv, char *const *envp) {
  if (!initialized) init();
  return exec_wrapper(path, argv, envp, false, pid, file_actions, attrp);
}

int posix_spawnp(pid_t *pid, const char *file,
                 const posix_spawn_file_actions_t *file_actions,
                 const posix_spawnattr_t *attrp,
                 char *const *argv, char *const *envp) {
  if (!initialized) init();
  return exec_wrapper(file, argv, envp, true, pid, file_actions, attrp);
}

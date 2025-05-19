/*
  Copyright (C) 2013-2025 Chromebrew Authors

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#include "./main.h"

__attribute__ ((visibility("default"))) int execl(const char *path, const char *arg, ...);
__attribute__ ((visibility("default"))) int execle(const char *path, const char *arg, ...);
__attribute__ ((visibility("default"))) int execlp(const char *path, const char *arg, ...);
__attribute__ ((visibility("default"))) int execv(const char *path, char *const *argv);
__attribute__ ((visibility("default"))) int execve(const char *path, char *const *argv, char *const *envp);
__attribute__ ((visibility("default"))) int execvp(const char *file, char *const *argv);
__attribute__ ((visibility("default"))) int execvpe(const char *file, char *const *argv, char *const *envp);
__attribute__ ((visibility("default"))) int posix_spawn(pid_t *pid, const char *path,
                                                        const posix_spawn_file_actions_t *file_actions,
                                                        const posix_spawnattr_t *attrp,
                                                        char *const *argv, char *const *envp);
__attribute__ ((visibility("default"))) int posix_spawnp(pid_t *pid, const char *file,
                                                         const posix_spawn_file_actions_t *file_actions,
                                                         const posix_spawnattr_t *attrp,
                                                         char *const *argv, char *const *envp);

int execl(const char *path, const char *arg, ...) {
  char    **argv;
  int     argc;
  va_list argp;

  if (!initialized) preload_init();

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

  if (!initialized) preload_init();

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

  if (!initialized) preload_init();

  va_start(argp, arg);
  argc    = count_args(argp);
  argv    = alloca(argc * sizeof(char *));
  argv[0] = (char *) arg;

  va2array(argp, argc, argv);
  va_end(argp);

  return exec_wrapper(path, argv, environ, true, NULL, NULL, NULL);
}

int execv(const char *path, char *const *argv) {
  if (!initialized) preload_init();
  return exec_wrapper(path, argv, environ, false, NULL, NULL, NULL);
}

int execve(const char *path, char *const *argv, char *const *envp) {
  if (!initialized) preload_init();
  return exec_wrapper(path, argv, envp, false, NULL, NULL, NULL);
}

int execvp(const char *file, char *const *argv) {
  if (!initialized) preload_init();
  return exec_wrapper(file, argv, environ, true, NULL, NULL, NULL);
}

int execvpe(const char *file, char *const *argv, char *const *envp) {
  if (!initialized) preload_init();
  return exec_wrapper(file, argv, envp, true, NULL, NULL, NULL);
}

int posix_spawn(pid_t *pid, const char *path,
                const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *attrp,
                char *const *argv, char *const *envp) {
  if (!initialized) preload_init();
  return exec_wrapper(path, argv, envp, false, pid, file_actions, attrp);
}

int posix_spawnp(pid_t *pid, const char *file,
                 const posix_spawn_file_actions_t *file_actions,
                 const posix_spawnattr_t *attrp,
                 char *const *argv, char *const *envp) {
  if (!initialized) preload_init();
  return exec_wrapper(file, argv, envp, true, pid, file_actions, attrp);
}

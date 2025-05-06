/*
  Copyright (C) 2013-2025 Chromebrew Authors

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see https://www.gnu.org/licenses/gpl-3.0.html.
*/

/*
  crew-preload.so: A LD_PRELOAD wrapper for Chromebrew

  This wrapper does the following things:
    - Redirect glibc-specific file's open request to CREW_GLIBC_PREFIX
    - Append --dynamic-linker flag to linker commend
    - Replace linker command with mold (can be disabled with CREW_PRELOAD_NO_MOLD)

  Usage: LD_PRELOAD=crew-preload.so <command>

  cc -O3 -fPIC -shared -DCREW_GLIBC_PREFIX=... -DCREW_GLIBC_INTERPRETER=... crew-preload.c -o crew-preload.so
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <string.h>
#include <spawn.h>
#include <unistd.h>
#include <sys/stat.h>
#include <linux/limits.h>

#ifndef CREW_GLIBC_PREFIX
#define CREW_GLIBC_PREFIX "/usr/local/opt/glibc-libs"
#endif

#ifndef CREW_GLIBC_INTERPRETER
#define CREW_GLIBC_INTERPRETER "/usr/local/opt/glibc-libs/ld-linux-x86-64.so.2"
#endif

extern char **environ;

const char *ld_executables[] = {
  "ld",
  "ld.bfd",
  "ld.gold",
  "ld.lld",
  "ld.mold",
  "mold"
};

bool initialized = false, no_mold = false, verbose = false;
char glibc_files[128][PATH_MAX];
int  glibc_file_count = 0;

static int   (*orig_access)(const char *pathname, int mode);
static int   (*orig_open)(const char *pathname, int flags, mode_t mode);
static int   (*orig_open64)(const char *pathname, int flags, mode_t mode);
static FILE* (*orig_fopen)(const char *filename, const char *mode);
static FILE* (*orig_fopen64)(const char *filename, const char *mode);
static int   (*orig_execvpe)(const char *file, char *const *argv, char *const *envp);
static int   (*orig_posix_spawnp)(pid_t *pid, const char *file,
                                  const posix_spawn_file_actions_t *file_actions,
                                  const posix_spawnattr_t *attrp,
                                  char *const *argv, char *const *envp);

void init(void) {
  DIR *dir;
  struct dirent *entry;
  char current_exe[PATH_MAX];
  char *filename;

  if (strcmp(getenv("CREW_PRELOAD_VERBOSE") ?: "0", "1") == 0) verbose = true;
  if (strcmp(getenv("CREW_PRELOAD_NO_MOLD") ?: "0", "1") == 0) no_mold = true;

  orig_access       = dlsym(RTLD_NEXT, "access");
  orig_open         = dlsym(RTLD_NEXT, "open");
  orig_open64       = dlsym(RTLD_NEXT, "open64");
  orig_fopen        = dlsym(RTLD_NEXT, "fopen");
  orig_fopen64      = dlsym(RTLD_NEXT, "fopen64");
  orig_execvpe      = dlsym(RTLD_NEXT, "execvpe");
  orig_posix_spawnp = dlsym(RTLD_NEXT, "posix_spawnp");

  if ((dir = opendir(CREW_GLIBC_PREFIX)) == NULL) return;

  while ((entry = readdir(dir)) != NULL) {
    filename = entry->d_name;

    if (entry->d_type == DT_REG && (strstr(filename, ".o") || strstr(filename, ".a") || strstr(filename, ".so"))) {
      // build a list consisting of all libraries/objects under CREW_GLIBC_PREFIX for later use
      strncpy(glibc_files[glibc_file_count++], filename, PATH_MAX - 1);
    }
  }

  closedir(dir);
  initialized = true;
}

const char *replace_path_if_needed(const char *path) {
  char *filename = basename(path), *new_path;

  // hijack file path if needed
  if (strncmp(path, "/usr/local/lib", 14) == 0 || strncmp(path, "/usr/lib", 8) == 0) {
    for (int i = 0; i < glibc_file_count; i++) {
      if (strcmp(filename, glibc_files[i]) == 0) {
        asprintf(&new_path, "%s/%s", CREW_GLIBC_PREFIX, filename);
        if (verbose) fprintf(stderr, "crew-preload: Replacing %s with %s\n", path, new_path);
        return new_path;
      }
    }
  }

  return path;
}

int count_args(va_list argp) {
  int argc = 0;

  va_list argp_copy;
  va_copy(argp_copy, argp);

  // count number of arguments
  while (va_arg(argp_copy, char *)) argc++;
  va_end(argp_copy);

  return argc;
}

void va2array(va_list argp, int argc, char **argv) {
  // copy all arguments into a char array
  for (int i = 0; i < argc; i++) argv[i] = va_arg(argp, char *);
  argv[argc] = NULL;
}

int exec_wrapper(const char *executable, char *const *argv, char *const *envp,
                 void *pid, const void *file_actions, const void *attrp) {
  bool is_ld      = false;
  char **new_argv = alloca(8192 * sizeof(char *)),
       *filename  = basename(executable);
  int  argc       = 0;

  if (verbose) {
    if (pid == NULL) {
      fprintf(stderr, "crew-preload: exec() called: %s\n", executable);
    } else {
      fprintf(stderr, "crew-preload: posix_spawn() called: %s\n", executable);
    }
  }

  for (int i = 0; i < sizeof(ld_executables) / sizeof(char *); i++) {
    if (strcmp(filename, ld_executables[i]) == 0) {
      is_ld = true;
      break;
    }
  }

  // copy arguments to new array
  for (; argv[argc] != NULL; argc++) asprintf(&new_argv[argc], argv[argc]);
  new_argv[argc] = NULL;

  if (is_ld) {
    const char *final_executable;

    if (strcmp(filename, "mold") != 0 && strcmp(filename, "ld.mold") != 0) {
      if (no_mold) {
        if (verbose) fprintf(stderr, "crew-preload: CREW_PRELOAD_NO_MOLD is set, will NOT modify linker path\n");
        final_executable = executable;
      } else {
        if (verbose) fprintf(stderr, "crew-preload: Linker detected (%s), will use mold linker\n", executable);
        final_executable = "mold";
      }
    } else {
      final_executable = executable;
    }

    if (verbose) fprintf(stderr, "crew-preload: Appending --dynamic-linker flag to the linker...\n");

    new_argv[argc++] = "--dynamic-linker";
    new_argv[argc++] = CREW_GLIBC_INTERPRETER;
    new_argv[argc]   = NULL;

    if (pid == NULL) {
      return orig_execvpe(final_executable, new_argv, envp);
    } else {
      return orig_posix_spawnp((pid_t *) pid, final_executable, (const posix_spawn_file_actions_t *) file_actions,
                               (const posix_spawnattr_t *) attrp, new_argv, envp);
    }
  } else if (pid == NULL) {
    return orig_execvpe(executable, new_argv, envp);
  } else {
    return orig_posix_spawnp((pid_t *) pid, executable, (const posix_spawn_file_actions_t *) file_actions,
                             (const posix_spawnattr_t *) attrp, new_argv, envp);
  }
}

int access(const char *pathname, int mode) {
  if (!initialized) init();
  return orig_access(replace_path_if_needed(pathname), mode);
}

int open(const char *path, int flags, ...) {
  va_list argp;
  va_start(argp, flags);
  mode_t mode = va_arg(argp, mode_t);
  va_end(argp);

  if (!initialized) init();
  return orig_open(replace_path_if_needed(path), flags, mode);
}

int open64(const char *path, int flags, ...) {
  va_list argp;
  va_start(argp, flags);
  mode_t mode = va_arg(argp, mode_t);
  va_end(argp);

  if (!initialized) init();
  return orig_open64(replace_path_if_needed(path), flags, mode);
}

FILE *fopen(const char *path, const char *mode) {
  if (!initialized) init();
  return orig_fopen(replace_path_if_needed(path), mode);
}

FILE *fopen64(const char *path, const char *mode) {
  if (!initialized) init();
  return orig_fopen64(replace_path_if_needed(path), mode);
}

int execl(const char *path, const char *arg, ...) {
  char    **argv;
  int     argc;
  va_list argp;

  if (!initialized) init();

  va_start(argp, arg);
  argc = count_args(argp);
  argv = alloca(argc * sizeof(char *));

  va2array(argp, argc, argv);
  va_end(argp);

  return exec_wrapper(path, argv, environ, NULL, NULL, NULL);
}

int execle(const char *path, const char *arg, ...) {
  char    **argv;
  int     argc;
  va_list argp;

  if (!initialized) init();

  va_start(argp, arg);
  argc = count_args(argp);
  argv = alloca(argc * sizeof(char *));

  va2array(argp, argc, argv);
  va_end(argp);

  return exec_wrapper(path, argv, environ, NULL, NULL, NULL);
}

int execlp(const char *path, const char *arg, ...) {
  char    **argv, **envp;
  int     argc;
  va_list argp;

  if (!initialized) init();

  va_start(argp, arg);
  argc = count_args(argp);
  argv = alloca(argc * sizeof(char *));

  va2array(argp, argc, argv);
  envp = va_arg(argp, char **);
  va_end(argp);

  return exec_wrapper(path, argv, envp, NULL, NULL, NULL);
}

int execv(const char *path, char *const *argv) {
  if (!initialized) init();
  return exec_wrapper(path, argv, environ, NULL, NULL, NULL);
}

int execve(const char *path, char *const *argv, char *const *envp) {
  if (!initialized) init();
  return exec_wrapper(path, argv, envp, NULL, NULL, NULL);
}

int execvp(const char *file, char *const *argv) {
  if (!initialized) init();
  return exec_wrapper(file, argv, environ, NULL, NULL, NULL);
}

int execvpe(const char *file, char *const *argv, char *const *envp) {
  if (!initialized) init();
  return exec_wrapper(file, argv, envp, NULL, NULL, NULL);
}

int posix_spawn(pid_t *pid, const char *path,
                const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *attrp,
                char *const *argv, char *const *envp) {
  if (!initialized) init();
  return exec_wrapper(path, argv, envp, pid, file_actions, attrp);
}

int posix_spawnp(pid_t *pid, const char *file,
                 const posix_spawn_file_actions_t *file_actions,
                 const posix_spawnattr_t *attrp,
                 char *const *argv, char *const *envp) {
  if (!initialized) init();
  return exec_wrapper(file, argv, envp, pid, file_actions, attrp);
}

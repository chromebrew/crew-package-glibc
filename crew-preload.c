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

  cc -O3 -fPIC -shared crew-preload.c -o crew-preload.so
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
#include <unistd.h>
#include <sys/stat.h>
#include <linux/limits.h>

#ifndef CREW_GLIBC_PREFIX
#define CREW_GLIBC_PREFIX "/usr/local/opt/glibc-libs"
#endif

#ifndef CREW_GLIBC_INTERPRETER
#define CREW_GLIBC_INTERPRETER "/usr/local/opt/glibc-libs/ld-linux-x86-64.so.2"
#endif

void init(void) __attribute__((constructor));
bool verbose = false;
char glibc_files[128][PATH_MAX];
int  glibc_file_count = 0;

static int   (*orig_open)(const char *pathname, int flags, mode_t mode);
static int   (*orig_open64)(const char *pathname, int flags, mode_t mode);
static FILE* (*orig_fopen)(const char *filename, const char *mode);
static FILE* (*orig_fopen64)(const char *filename, const char *mode);

void init(void) {
  DIR *dir;
  struct dirent *entry;
  char current_exe[PATH_MAX];
  char *filename;

  if (strcmp(getenv("CREW_PRELOAD_VERBOSE") ?: "0", "1") == 0) verbose = true;

  orig_open    = dlsym(RTLD_NEXT, "open");
  orig_open64  = dlsym(RTLD_NEXT, "open64");
  orig_fopen   = dlsym(RTLD_NEXT, "fopen");
  orig_fopen64 = dlsym(RTLD_NEXT, "fopen64");

  // get current executable path
  current_exe[readlink("/proc/self/exe", current_exe, PATH_MAX)] = '\0';
  filename = basename(current_exe);

  if (getenv("CREW_PRELOAD_REPLACED") == NULL && (strcmp(filename, "ld") == 0 || strcmp(filename, "mold") == 0 || strncmp(filename, "ld.", 3) == 0)) {
    char *cmdline, *argv[1024];
    int  argc = 0, cmdline_fd, cmdline_len;

    if (verbose) fprintf(stderr, "crew-preload: Current executable is a linker, will re-execute with --dynamic-linker flag\n");

    // read command line arguments
    cmdline      = malloc(sysconf(_SC_ARG_MAX));
    cmdline_fd   = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    cmdline_len  = read(cmdline_fd, cmdline, sysconf(_SC_ARG_MAX));
    argv[argc++] = cmdline;

    // parse arguments into an array
    for (char *p = cmdline; p < cmdline + cmdline_len - 1; p++) {
      if (*p == '\0') argv[argc++] = p + 1;
    }

    argv[argc++] = "--dynamic-linker";
    argv[argc++] = CREW_GLIBC_INTERPRETER;
    argv[argc]   = NULL;

    setenv("CREW_PRELOAD_REPLACED", "1", true);

    if (strcmp(getenv("CREW_PRELOAD_NO_MOLD") ?: "0", "1") == 0) {
      if (verbose) fprintf(stderr, "crew-preload: CREW_PRELOAD_NO_MOLD is set, will NOT re-execute as mold linker\n");
      execv(current_exe, argv);
    } else {
      // re-execute current linker arguments with mold
      if (verbose) fprintf(stderr, "crew-preload: Will re-execute as mold linker\n");
      execvp("mold", argv);
    }
  } else {
    unsetenv("CREW_PRELOAD_REPLACED");
  }

  if ((dir = opendir(CREW_GLIBC_PREFIX)) == NULL) return;

  while ((entry = readdir(dir)) != NULL) {
    filename = entry->d_name;

    if (entry->d_type == DT_REG && (strstr(filename, ".o") || strstr(filename, ".a") || strstr(filename, ".so"))) {
      // build a list consisting of all libraries/objects under CREW_GLIBC_PREFIX for later use
      strncpy(glibc_files[glibc_file_count++], filename, PATH_MAX - 1);
    }
  }

  closedir(dir);
}

const char *replace_path_if_needed(const char *path) {
  char *filename = basename(path), *new_path;

  // hijack file path if needed
  for (int i = 0; i < glibc_file_count; i++) {
    if ((strncmp(path, "/usr/local/lib", 14) == 0 || strncmp(path, "/usr/lib", 8) == 0) && strcmp(filename, glibc_files[i]) == 0) {
      asprintf(&new_path, "%s/%s", CREW_GLIBC_PREFIX, filename);
      if (verbose) fprintf(stderr, "crew-preload: Replacing %s with %s\n", path, new_path);
      return new_path;
    }
  }

  return path;
}

int open(const char *path, int flags, ...) {
  va_list argp;
  va_start(argp, flags);
  mode_t mode = va_arg(argp, mode_t);
  va_end(argp);

  return orig_open(replace_path_if_needed(path), flags, mode);
}

int open64(const char *path, int flags, ...) {
  va_list argp;
  va_start(argp, flags);
  mode_t mode = va_arg(argp, mode_t);
  va_end(argp);

  return orig_open64(replace_path_if_needed(path), flags, mode);
}

FILE *fopen(const char *path, const char *mode) {
  return orig_fopen(replace_path_if_needed(path), mode);
}

FILE *fopen64(const char *path, const char *mode) {
  return orig_fopen64(replace_path_if_needed(path), mode);
}

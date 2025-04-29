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
  crew-audit.so: An audit library for glibc that intercepts and modifies library requests.

  Usage: LD_AUDIT=crew-audit.so [CREW_AUDIT_VERBOSE=1] <command>

  cc -O3 -fPIC -shared crew-audit.c -o crew-audit.so
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <link.h>
#include <linux/limits.h>
#include <gnu/libc-version.h>

#ifndef CREW_PREFIX
#define CREW_PREFIX "/usr/local"
#endif

#ifndef CREW_GLIBC_VERSION
#define CREW_GLIBC_VERSION "unknown"
#endif

const char *system_exe_path[] = {
  "/usr/bin/",
  "/usr/sbin/",
  "/bin/",
  "/sbin/"
};

const char *system_lib_path[] = {
  "/usr/lib64",
  "/lib64",
  "/usr/lib",
  "/lib",
};

bool verbose = false, is_crew_glibc = false, is_system_cmd = false;
char crew_glibc_prefix[PATH_MAX], crew_libc_so_path[PATH_MAX];

unsigned int la_version(unsigned int interface_ver) {
  char current_exe[PATH_MAX];
  int  exe_path_len;

  if (strncmp(getenv("CREW_AUDIT_VERBOSE") ?: "0", "1", 1) == 0) verbose = true;

  snprintf(crew_glibc_prefix, PATH_MAX, "%s/opt/glibc-libs", CREW_PREFIX);
  snprintf(crew_libc_so_path, PATH_MAX, "%s/libc.so.6", crew_glibc_prefix);

  if (verbose) fprintf(stderr, "crew-audit: Initialized on glibc %s with interface version %i\n", gnu_get_libc_version(), interface_ver);

  // check if we are running with glibc-standlone or not by comparing the version
  if (strcmp(gnu_get_libc_version(), CREW_GLIBC_VERSION) == 0) {
    if (verbose) fprintf(stderr, "crew-audit: Running with Chromebrew's dynamic linker...\n");
    is_crew_glibc = true;
  } else if (verbose) {
    fprintf(stderr, "crew-audit: Running with ChromeOS's dynamic linker...\n");
  }

  // get current executable path
  current_exe[readlink("/proc/self/exe", current_exe, PATH_MAX)] = '\0';

  // check if the current executable path matches prefixes in the system_exe_path[] array
  for (int i = 0; i < sizeof(system_exe_path) / sizeof(char *); i++) {
    if (strncmp(current_exe, system_exe_path[i], strlen(system_exe_path[i])) == 0) {
      // current executable path is a system command
      is_system_cmd = true;
      break;
    }
  }

  if (verbose && is_system_cmd) {
    fprintf(stderr, "crew-audit: System command detected, libraries provided by Chromebrew will not be used.\n");
  }

  return LAV_CURRENT;
}

char *la_objsearch(const char *soname, uintptr_t *cookie, unsigned int flag) {
  char *new_path = malloc(PATH_MAX);

  // do not modify requests with absolute path
  if (soname[0] == '/') return (char *) soname;

  if (verbose) fprintf(stderr, "crew-audit: Library %s is being requested...\n", soname);

  if (is_crew_glibc) {
    if (strcmp(soname, "libC.so.6") == 0) {
      // replace libC.so.6 requests with libc.so.6
      if (verbose) fprintf(stderr, "crew-audit: libC.so.6 being requested, replacing it with %s...\n", crew_libc_so_path);
      return crew_libc_so_path;
    }

    // always search in ${CREW_PREFIX}/opt/glibc-libs first if running with Chromebrew's dynamic linker
    snprintf(new_path, PATH_MAX, "%s/opt/glibc-libs/%s", CREW_PREFIX, soname);

    if (access(new_path, F_OK) == 0) {
      if (verbose) fprintf(stderr, "crew-audit: Library found in %s, using it instead...\n", new_path);
      return new_path;
    }
  } else if (is_system_cmd) {
    // always search libraries from system path (instead of CREW_LIB_PREFIX) if we are running system commands
    // see https://github.com/chromebrew/chromebrew/issues/5777 for more info
    for (int i = 0; i < sizeof(system_lib_path) / sizeof(char *); i++) {
      snprintf(new_path, PATH_MAX, "%s/%s", system_lib_path[i], soname);

      if (access(new_path, F_OK) == 0) {
        if (verbose) fprintf(stderr, "crew-audit: Library found in %s, using it instead...\n", new_path);
        return new_path;
      }
    }

    // return NULL if not found
    return NULL;
  }

  // return it as-is if no modification is needed
  return (char *) soname;
}

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
  crew-preload.so: A LD_PRELOAD wrapper for Chromebrew environments

  This wrapper does the following things:
    - Fix hardcoded shebang/command path (e.g `#!/usr/bin/perl` will be converted to `#!${CREW_PREFIX}/bin/perl`)
    - Unset LD_LIBRARY_PATH before running any system commands (executables located under /{bin,sbin} or /usr/{bin,sbin})
    - Calls to /bin/{bash,sh} will be redirected to ${CREW_PREFIX}/bin/{bash,sh} (unless CREW_PRELOAD_NO_CREW_SHELL=1)

  If CREW_PRELOAD_ENABLE_COMPILE_HACKS is set, this wrapper will also:
    - Redirect glibc-specific file's open request to CREW_GLIBC_PREFIX
    - Append --dynamic-linker flag to linker commend
    - Replace linker command with mold (can be disabled with CREW_PRELOAD_NO_MOLD)

  Usage: LD_PRELOAD=crew-preload.so <command>

  cc -O3 -fPIC -shared -DCREW_PREFIX=\"...\" -DCREW_GLIBC_PREFIX=\"...\" \
    -DCREW_GLIBC_INTERPRETER=\"...\" -DSYSTEM_GLIBC_INTERPRETER=\"...\" \
    crew-preload.c -o crew-preload.so
*/

#include "./main.h"

#ifndef CREW_PREFIX
#define CREW_PREFIX "/usr/local"
#endif

#ifndef CREW_GLIBC_PREFIX
#define CREW_GLIBC_PREFIX "/usr/local/opt/glibc-libs"
#endif

#ifndef CREW_GLIBC_INTERPRETER
#define CREW_GLIBC_INTERPRETER "/usr/local/opt/glibc-libs/ld-linux-x86-64.so.2"
#endif

#ifndef SYSTEM_GLIBC_INTERPRETER
#define SYSTEM_GLIBC_INTERPRETER "/lib64/ld-linux-x86-64.so.2"
#endif

bool initialized      = false,
     compile_hacks    = false,
     no_crew_shell    = false,
     no_mold          = false,
     verbose          = false;
int  glibc_file_count = 0;

char glibc_files[128][PATH_MAX];

const char *ld_executables[] = {
  "ld",
  "ld.bfd",
  "ld.gold",
  "ld.lld",
  "ld.mold",
  "mold"
};

const char *system_exe_path[] = {
  "/usr/bin/",
  "/usr/sbin/",
  "/bin/",
  "/sbin/"
};

int   (*orig_access)(const char *pathname, int mode);
int   (*orig_stat)(const char *restrict pathname, struct stat *restrict statbuf);
int   (*orig_statx)(int dirfd, const char *restrict pathname, int flags, unsigned int mask, struct statx *restrict statxbuf);
int   (*orig_lstat)(const char *restrict pathname, struct stat *restrict statbuf);
int   (*orig_open)(const char *pathname, int flags, mode_t mode);
int   (*orig_open64)(const char *pathname, int flags, mode_t mode);
FILE* (*orig_fopen)(const char *filename, const char *mode);
FILE* (*orig_fopen64)(const char *filename, const char *mode);
int   (*orig_execve)(const char *pathname, char *const *argv, char *const *envp);
int   (*orig_posix_spawn)(pid_t *pid, const char *pathname,
                          const posix_spawn_file_actions_t *file_actions,
                          const posix_spawnattr_t *attrp,
                          char *const *argv, char *const *envp);

void init(void) {
  DIR *dir;
  struct dirent *entry;
  char current_exe[PATH_MAX];
  char *filename;

  if (strcmp(getenv("CREW_PRELOAD_VERBOSE") ?: "0", "1") == 0)              verbose       = true;
  if (strcmp(getenv("CREW_PRELOAD_ENABLE_COMPILE_HACKS") ?: "0", "1") == 0) compile_hacks = true;
  if (strcmp(getenv("CREW_PRELOAD_NO_CREW_SHELL") ?: "0", "1") == 0)        no_crew_shell = true;
  if (strcmp(getenv("CREW_PRELOAD_NO_MOLD") ?: "0", "1") == 0)              no_mold       = true;

  orig_access      = dlsym(RTLD_NEXT, "access");
  orig_stat        = dlsym(RTLD_NEXT, "stat");
  orig_statx       = dlsym(RTLD_NEXT, "statx");
  orig_lstat       = dlsym(RTLD_NEXT, "lstat");
  orig_open        = dlsym(RTLD_NEXT, "open");
  orig_open64      = dlsym(RTLD_NEXT, "open64");
  orig_fopen       = dlsym(RTLD_NEXT, "fopen");
  orig_fopen64     = dlsym(RTLD_NEXT, "fopen64");
  orig_execve      = dlsym(RTLD_NEXT, "execve");
  orig_posix_spawn = dlsym(RTLD_NEXT, "posix_spawn");

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

void replace_path_if_needed(const char *orig_path, char *final_path) {
  char *filename = basename(orig_path);

  // hijack file path if needed
  if (strncmp(orig_path, "/usr/local/lib", 14) == 0 || strncmp(orig_path, "/usr/lib", 8) == 0) {
    for (int i = 0; i < glibc_file_count; i++) {
      if (strcmp(filename, glibc_files[i]) == 0) {
        snprintf(final_path, PATH_MAX, "%s/%s", CREW_GLIBC_PREFIX, filename);
        if (verbose) fprintf(stderr, "crew-preload: Replacing %s with %s\n", orig_path, final_path);
        return;
      }
    }
  }

  // return the original path as-is if we are still here
  strncpy(final_path, orig_path, PATH_MAX);
}

int count_args(va_list argp) {
  int argc = 1;

  va_list argp_copy;
  va_copy(argp_copy, argp);

  // count number of arguments
  while (va_arg(argp_copy, char *)) argc++;
  va_end(argp_copy);

  return argc;
}

void va2array(va_list argp, int argc, char **argv) {
  // copy all arguments into a char array
  for (int i = 1; i < argc; i++) argv[i] = va_arg(argp, char *);
  argv[argc] = NULL;
}

int copy2array(char * const* src, char **dest, int offset) {
  int i;

  for (i = 0; src[i] != NULL; i++) asprintf(&dest[offset + i], "%s", src[i]);
  dest[offset + i] = NULL;

  return i;
}

int search_in_path(const char *file, char *result) {
  int  return_value = ENOENT;
  char cs_path[PATH_MAX * 100], *path_env, *search_path;

  confstr(_CS_PATH, cs_path, sizeof(cs_path));

  path_env    = getenv("PATH") ?: cs_path,
  search_path = strtok(path_env, ":");

  do {
    snprintf(result, PATH_MAX, "%s/%s", search_path, file);

    if (orig_access(result, X_OK) == 0) {
      // file found in path and it is executable
      return_value = 0;

      if (verbose) fprintf(stderr, "crew-preload: %s => %s\n", file, result);
      break;
    } else if (orig_access(result, F_OK) == 0) {
      // file found in path but it is not executable
      return_value = EACCES;
    }
  } while (search_path = strtok(NULL, ":"));

  return return_value;
}

bool is_dynamic_executable(char *pathname) {
  char *argv[] = { "ld.so", "--verify", pathname, NULL };
  int pid, status;

  status = orig_posix_spawn(&pid, SYSTEM_GLIBC_INTERPRETER, NULL, NULL, argv, environ);

  if (status == 0) {
    waitpid(pid, &status, 0);
    return (WEXITSTATUS(status) == 0) ? true : false;
  } else {
    fprintf(stderr, "crew-preload: posix_spawn(\"ld.so --verify\") failed: %s\n", strerror(status));
    return false;
  }
}

int exec_wrapper(const char *path_or_name, char *const *argv, char *const *envp,
                 bool perform_path_search, void *pid, const void *file_actions, const void *attrp) {
  bool    is_linker   = false,
          is_system   = false,
          is_a_path   = false;
  char    **new_argv  = alloca(8192 * sizeof(char *)),
          *filename   = basename(path_or_name),
          *read_buf   = alloca(PATH_MAX),
          *final_exec = (char *) path_or_name;
  int     argc        = 0;

  if (verbose) {
    if (pid == NULL) {
      if (perform_path_search) {
        fprintf(stderr, "crew-preload: exec*p() called: %s\n", path_or_name);
      } else {
        fprintf(stderr, "crew-preload: exec*() called: %s\n", path_or_name);
      }
    } else {
      if (perform_path_search) {
        fprintf(stderr, "crew-preload: posix_spawn() called: %s\n", path_or_name);
      } else {
        fprintf(stderr, "crew-preload: posix_spawnp() called: %s\n", path_or_name);
      }
    }
  }

  // copy arguments to new array
  argc = copy2array(argv, new_argv, 0);

  // check if path_or_name is a relative or absolute path
  if (!perform_path_search || orig_access(path_or_name, F_OK) == 0 ||
      path_or_name[0] == '/' || strncmp(path_or_name, "./", 2) == 0 ||
      strncmp(path_or_name, "../", 3) == 0) {

    is_a_path = true;
  }

  // search in path if perform_path_search == true and path_or_name is not a relative or absolute path
  if (!is_a_path) {
    int ret = search_in_path(path_or_name, final_exec);
    if (ret != 0) return ret;
  }

  // check if executable is a system command or not
  for (int i = 0; i < sizeof(system_exe_path) / sizeof(char *); i++) {
    if (strncmp(final_exec, system_exe_path[i], strlen(system_exe_path[i])) == 0) {
      is_system = true;
      break;
    }
  }

  // always use Chromebrew version of shell (at ${CREW_PREFIX}/bin/{bash,sh}) when /bin/sh or /bin/bash is called
  if (strcmp(final_exec, "/bin/sh") == 0 || strcmp(final_exec, "/bin/bash") == 0) {
    if (no_crew_shell) {
      if (verbose) fprintf(stderr, "crew-preload: CREW_PRELOAD_NO_CREW_SHELL set, will NOT modify shell path\n", final_exec, filename);
    } else {
      char new_path[PATH_MAX];
      snprintf(new_path, PATH_MAX, "%s%s", CREW_PREFIX, final_exec);

      if (orig_access(new_path, X_OK) == 0) {
        if (verbose) fprintf(stderr, "crew-preload: Shell detected (%s), will use Chromebrew version of %s instead\n", final_exec, filename);
        is_system  = false;
        final_exec = new_path;
      }
    }
  }

  // try searching in CREW_PREFIX if the executable is prefixed with system path and cannot be found
  if (is_system && orig_access(final_exec, F_OK) != 0) {
    asprintf(&final_exec, "%s/bin/%s", CREW_PREFIX, filename);

    if (orig_access(final_exec, F_OK) == 0) {
      if (verbose) fprintf(stderr, "crew-preload: %s => %s\n", path_or_name, final_exec);
      is_system = false;
    } else {
      // return an error if we cannot find any matching executables
      // ENOENT: No such file or directory
      return ENOENT;
    }
  } else if (is_system && is_dynamic_executable(final_exec)) {
    // run system commands with "ld.so --library-path '' <actual command>"
    new_argv[0] = "ld.so";
    new_argv[1] = "--library-path";
    new_argv[2] = "";
    new_argv[3] = final_exec;
    final_exec  = SYSTEM_GLIBC_INTERPRETER;

    if (verbose) fprintf(stderr, "crew-preload: System command detected, will execute with LD_LIBRARY_PATH unset...\n");

    copy2array(argv + 1, new_argv, 4);
  }

  // check for permission first, raise an error if the executable isn't executable
  // EACCES: Permission denied
  if (orig_access(final_exec, X_OK) != 0) return EACCES;

  // parse shebang and re-execute with specified interpreter if the executable is a script
  FILE *exec_fp = fopen(final_exec, "re");

  if (fread(read_buf, 2, 1, exec_fp) == 1 && memcmp(read_buf, "#!", 2) == 0) {
    // get shebang value
    fgets(read_buf, PATH_MAX, exec_fp);
    read_buf[strchr(read_buf, '\n') - read_buf] = '\0'; // remove newline

    if (verbose) fprintf(stderr, "crew-preload: %s is a script with shebang: '#!%s'\n", final_exec, read_buf);

    // extract interpreter path and interpreter argument (if any)
    final_exec  = strtok(read_buf, " ");
    new_argv[0] = final_exec;
    new_argv[1] = strtok(NULL, "\0");
    argc        = copy2array(argv, new_argv, (new_argv[1] == NULL) ? 1 : 2);

    if (verbose) fprintf(stderr, "crew-preload: Will re-execute as: %s %s %s ...\n", new_argv[0], new_argv[1], new_argv[2]);

    return exec_wrapper(final_exec, new_argv, envp, false, pid, file_actions, attrp);
  }

  // check if current executable is a linker
  for (int i = 0; i < sizeof(ld_executables) / sizeof(char *); i++) {
    if (strcmp(filename, ld_executables[i]) == 0) {
      is_linker = true;
      break;
    }
  }

  if (compile_hacks && is_linker) {
    char mold_exec[PATH_MAX];

    if (strcmp(filename, "mold") != 0 && strcmp(filename, "ld.mold") != 0) {
      if (no_mold && verbose) {
        fprintf(stderr, "crew-preload: CREW_PRELOAD_NO_MOLD is set, will NOT modify linker path\n");
      } else {
        if (verbose) fprintf(stderr, "crew-preload: Linker detected (%s), will use mold linker\n", filename);

        int ret = search_in_path("mold", mold_exec);

        if (ret == 0) {
          final_exec = mold_exec;
        } else {
          fprintf(stderr, "crew-preload: Mold linker is not executable (%s), will NOT modify linker path\n", strerror(ret));
        };
      }
    }

    if (verbose) fprintf(stderr, "crew-preload: Appending --dynamic-linker flag to the linker...\n");

    new_argv[argc++] = "--dynamic-linker";
    new_argv[argc++] = CREW_GLIBC_INTERPRETER;
    new_argv[argc]   = NULL;

    if (pid == NULL) {
      return orig_execve(final_exec, new_argv, envp);
    } else {
      return orig_posix_spawn((pid_t *) pid, final_exec, (const posix_spawn_file_actions_t *) file_actions,
                              (const posix_spawnattr_t *) attrp, new_argv, envp);
    }
  } else if (pid == NULL) {
    return orig_execve(final_exec, new_argv, envp);
  } else {
    return orig_posix_spawn((pid_t *) pid, final_exec, (const posix_spawn_file_actions_t *) file_actions,
                            (const posix_spawnattr_t *) attrp, new_argv, envp);
  }
}

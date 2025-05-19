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
    - Shell script with shebang set as /bin/{bash,sh} will be interpreted by ${CREW_PREFIX}/bin/{bash,sh} instead (unless CREW_PRELOAD_NO_CREW_SHELL=1)

  If CREW_PRELOAD_ENABLE_COMPILE_HACKS is set, this wrapper will also:
    - Append --dynamic-linker flag to linker commend
    - Replace linker command with mold (can be disabled with CREW_PRELOAD_NO_MOLD)

  Usage: LD_PRELOAD=crew-preload.so <command>

  cc -O3 -fPIC -shared -fvisibility=hidden -Wl,-soname,crew-preload.so \
    -DCREW_PREFIX=\"...\" -DCREW_GLIBC_PREFIX=\"...\" \
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

bool  compile_hacks = false,
      initialized   = false,
      no_crew_shell = false,
      no_mold       = false,
      verbose       = false;
pid_t pid           = 0;

const char *linkers[] = {
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

static int (*orig_execve)(const char *pathname, char *const *argv, char *const *envp);
static int (*orig_posix_spawn)(pid_t *pid, const char *pathname,
                               const posix_spawn_file_actions_t *file_actions,
                               const posix_spawnattr_t *attrp,
                               char *const *argv, char *const *envp);

void preload_init(void) {
  char *old_library_path = getenv("CREW_PRELOAD_LIBRARY_PATH");

  if (strcmp(getenv("CREW_PRELOAD_ENABLE_COMPILE_HACKS") ?: "0", "1") == 0) compile_hacks           = true;
  if (strcmp(getenv("CREW_PRELOAD_NO_CREW_SHELL") ?: "0", "1") == 0)        no_crew_shell           = true;
  if (strcmp(getenv("CREW_PRELOAD_NO_MOLD") ?: "0", "1") == 0)              no_mold                 = true;
  if (strcmp(getenv("CREW_PRELOAD_VERBOSE") ?: "0", "1") == 0)              verbose                 = true;

  pid              = getpid();
  orig_execve      = dlsym(RTLD_NEXT, "execve");
  orig_posix_spawn = dlsym(RTLD_NEXT, "posix_spawn");
  initialized      = true;

  if (verbose) fprintf(stderr, "[PID %i] crew-preload: Running on glibc version %s\n", pid, gnu_get_libc_version());

  // restore LD_LIBRARY_PATH from CREW_PRELOAD_LIBRARY_PATH if it was unset previously by LD_PRELOAD in the parent process
  if (old_library_path) {
    if (verbose) fprintf(stderr, "[PID %i] crew-preload: LD_LIBRARY_PATH restored (%s)\n", pid, old_library_path);

    setenv("LD_LIBRARY_PATH", old_library_path, true);
    unsetenv("CREW_PRELOAD_LIBRARY_PATH");
  }
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
  // copy2array(): copy all elements from src[i] to dest[offset + i]
  int i;

  for (i = 0; src[i] != NULL; i++) dest[offset + i] = strdup(src[i]);
  dest[offset + i] = NULL;

  return i;
}

int search_in_path(const char *file, char *result) {
  // search_in_path: search given filename in PATH environment variable,
  //                 full path will be written to the memory address that is pointed by the `result` pointer
  const char *path_env;
  char       cs_path[PATH_MAX * 32], strtok_buf[PATH_MAX], *search_path;
  int        return_value;

  confstr(_CS_PATH, cs_path, sizeof(cs_path));

  return_value = ENOENT;
  path_env     = getenv("PATH") ?: cs_path;

  strncpy(strtok_buf, path_env, PATH_MAX);
  search_path = strtok(strtok_buf, ":");

  do {
    snprintf(result, PATH_MAX, "%s/%s", search_path, file);

    if (access(result, X_OK) == 0) {
      // file found in path and it is executable
      return_value = 0;

      if (verbose) fprintf(stderr, "[PID %i] crew-preload: %s => %s\n", pid, file, result);
      break;
    } else if (access(result, F_OK) == 0) {
      // file found in path but it is not executable
      return_value = EACCES;
    }
  } while ((search_path = strtok(NULL, ":")));

  return return_value;
}

bool is_dynamic_executable(char *pathname) {
  // is_dymanic_executable(): Check whether the given file is a dynamically linked ELF executable
  char *argv[] = { SYSTEM_GLIBC_INTERPRETER, "--verify", pathname, NULL };
  int pid, status;

  if ((pid = fork()) == 0) {
    // slience output
    dup2(open("/dev/null", O_RDWR | O_CLOEXEC), STDOUT_FILENO);

    if (orig_execve(SYSTEM_GLIBC_INTERPRETER, argv, environ) == -1) {
      fprintf(stderr, "[PID %i] crew-preload: execve(\"ld.so --verify\") failed (%s)", pid, strerror(errno));
      exit(errno);
    }
  } else if (pid == -1) {
    fprintf(stderr, "[PID %i] crew-preload: fork() failed (%s)", pid, strerror(errno));
    return false;
  }

  waitpid(pid, &status, 0);
  return (WEXITSTATUS(status) == 0) ? true : false;
}

void unsetenvfp(char **envp, char *name) {
  // unsetenvfp: Remove specific environment variable from given environ pointer
  int name_len = strlen(name);

  for (int i = 0; envp[i]; i++) {
    if (strncmp(envp[i], name, name_len) == 0) {
      int j;
      for (j = i + 1; envp[j]; j++) envp[j - 1] = envp[j];
      envp[j] = NULL;
    }
  }
}

int exec_wrapper(const char *path_or_name, char *const *argv, char *const *envp,
                 bool perform_path_search, void *pid_p, const void *file_actions, const void *attrp) {
  bool    is_linker   = false,
          is_system   = false,
          is_a_path   = false;
  char    **new_argv  = alloca(4096 * sizeof(char *)),
          **new_envp  = alloca(4096 * sizeof(char *)),
          *filename   = basename(path_or_name),
          *read_buf   = alloca(PATH_MAX),
          *final_exec = alloca(PATH_MAX);
  int     argc        = copy2array(argv, new_argv, 0),
          envc        = copy2array(envp, new_envp, 0);
  FILE    *exec_fp;

  if (verbose) {
    if (pid_p == NULL) {
      if (perform_path_search) {
        fprintf(stderr, "[PID %i] crew-preload: exec*p() called: %s\n", pid, path_or_name);
      } else {
        fprintf(stderr, "[PID %i] crew-preload: exec*() called: %s\n", pid, path_or_name);
      }
    } else {
      if (perform_path_search) {
        fprintf(stderr, "[PID %i] crew-preload: posix_spawn() called: %s\n", pid, path_or_name);
      } else {
        fprintf(stderr, "[PID %i] crew-preload: posix_spawnp() called: %s\n", pid, path_or_name);
      }
    }
  }

  // check if path_or_name is a relative or absolute path
  if (!perform_path_search || access(path_or_name, F_OK) == 0 ||
      path_or_name[0] == '/' || strncmp(path_or_name, "./", 2) == 0 ||
      strncmp(path_or_name, "../", 3) == 0) {

    is_a_path = true;
  }

  // search in path if perform_path_search == true and path_or_name is not a relative or absolute path
  if (is_a_path) {
    strncpy(final_exec, path_or_name, PATH_MAX);
  } else {
    int ret = search_in_path(path_or_name, final_exec);
    if (ret != 0) return ret;
  }

  // unset LD_PRELOAD/LD_LIBRARY_PATH when executable is libc.so.6, as it will cause segfaults
  if (strcmp(filename, "libc.so.6") == 0) {
    if (verbose) fprintf(stderr, "[PID %i] crew-preload: libc.so.6 detected, will execute with LD_* unset...\n", pid);

    unsetenvfp(new_envp, "LD_LIBRARY_PATH");
    unsetenvfp(new_envp, "LD_PRELOAD");
    envc--;
  }

  // check if executable is a system command or not
  for (int i = 0; i < (int) (sizeof(system_exe_path) / sizeof(char *)); i++) {
    if (strncmp(final_exec, system_exe_path[i], strlen(system_exe_path[i])) == 0) {
      is_system = true;
      break;
    }
  }

  // try searching in CREW_PREFIX if the executable is prefixed with system path and cannot be found
  if (is_system && access(final_exec, F_OK) != 0) {
    asprintf(&final_exec, "%s/bin/%s", CREW_PREFIX, filename);

    if (access(final_exec, F_OK) == 0) {
      if (verbose) fprintf(stderr, "[PID %i] crew-preload: %s => %s\n", pid, path_or_name, final_exec);
      is_system = false;
    } else {
      // return an error if we cannot find any matching executables
      // ENOENT: No such file or directory
      return ENOENT;
    }
  } else if (is_system && is_dynamic_executable(final_exec)) {
    // unset LD_LIBRARY_PATH for system commands, original value will be copied into CREW_PRELOAD_LIBRARY_PATH environment variable
    // (see https://github.com/chromebrew/chromebrew/issues/5777 for more information)
    if (verbose) fprintf(stderr, "[PID %i] crew-preload: System command detected, will execute with LD_LIBRARY_PATH unset...\n", pid);

    unsetenvfp(new_envp, "LD_LIBRARY_PATH");
    asprintf(&new_envp[envc++], "CREW_PRELOAD_LIBRARY_PATH=%s", getenv("LD_LIBRARY_PATH"));
    new_envp[envc] = NULL;
  }

  // check for permission first, raise an error if the executable isn't executable
  // EACCES: Permission denied
  if (access(final_exec, X_OK) != 0) return EACCES;

  // parse shebang and re-execute with specified interpreter if the executable is a script
  exec_fp = fopen(final_exec, "re");

  if (!exec_fp) {
    if (verbose) fprintf(stderr, "[PID %i] crew-preload: Failed to open %s for reading (%s)\n", pid, final_exec, strerror(errno));
  } else if (fread(read_buf, 2, 1, exec_fp) == 1 && memcmp(read_buf, "#!", 2) == 0) {
    char script_path[PATH_MAX], *interpreter_opt;

    strncpy(script_path, final_exec, PATH_MAX);

    // get shebang value
    fgets(read_buf, PATH_MAX, exec_fp);
    read_buf[strchr(read_buf, '\n') - read_buf] = '\0'; // remove newline

    if (verbose) fprintf(stderr, "[PID %i] crew-preload: %s is a script with shebang: '#!%s'\n", pid, final_exec, read_buf);

    // extract interpreter path and interpreter argument (if any)
    strncpy(final_exec, strtok(read_buf, " "), PATH_MAX);

    // always use Chromebrew version of shell (at ${CREW_PREFIX}/bin/{bash,sh}) when the interpreter path is /bin/sh or /bin/bash
    if (strcmp(final_exec, "/bin/sh") == 0 || strcmp(final_exec, "/bin/bash") == 0) {
      if (no_crew_shell) {
        if (verbose) fprintf(stderr, "[PID %i] crew-preload: CREW_PRELOAD_NO_CREW_SHELL set, will NOT modify shell path\n", pid);
      } else {
        char *new_path;
        asprintf(&new_path, "%s%s", CREW_PREFIX, final_exec);

        if (access(new_path, X_OK) == 0) {
          if (verbose) fprintf(stderr, "[PID %i] crew-preload: Shell detected (%s), will use Chromebrew version of %s instead\n", pid, final_exec, basename(final_exec));
          strncpy(final_exec, new_path, PATH_MAX);
        }
      }
    }

    if ((interpreter_opt = strtok(NULL, "\0"))) {
      new_argv[0] = final_exec;
      new_argv[1] = interpreter_opt;
      new_argv[2] = script_path;
      argc        = copy2array(&argv[1], new_argv, 3); // copy all arguments except argv[0]
    } else {
      new_argv[0] = final_exec;
      new_argv[1] = script_path;
      argc        = copy2array(&argv[1], new_argv, 2); // copy all arguments except argv[0]
    }

    if (verbose) fprintf(stderr, "[PID %i] crew-preload: Will re-execute as: %s %s %s ...\n", pid, new_argv[0], new_argv[1], new_argv[2]);

    return exec_wrapper(final_exec, new_argv, new_envp, false, pid_p, file_actions, attrp);
  }

  if (compile_hacks) {
    char mold_exec[PATH_MAX];

    // check if current executable is a linker
    for (int i = 0; i < (int) (sizeof(linkers) / sizeof(char *)); i++) {
      if (strcmp(filename, linkers[i]) == 0) {
        is_linker = true;
        break;
      }
    }

    if (is_linker) {
      if (no_mold) {
        if (verbose) fprintf(stderr, "[PID %i] crew-preload: CREW_PRELOAD_NO_MOLD is set, will NOT modify linker path\n", pid);
      } else {
        if (verbose) fprintf(stderr, "[PID %i] crew-preload: Linker detected (%s), will use mold linker\n", pid, filename);

        int ret = search_in_path("mold", mold_exec);

        if (ret == 0) {
          strncpy(final_exec, mold_exec, PATH_MAX);
        } else {
          fprintf(stderr, "[PID %i] crew-preload: Mold linker is not executable (%s), will NOT modify linker path\n", pid, strerror(ret));
        };
      }

      if (verbose) fprintf(stderr, "[PID %i] crew-preload: Appending --dynamic-linker flag to the linker...\n", pid);

      new_argv[argc++] = "--dynamic-linker";
      new_argv[argc++] = CREW_GLIBC_INTERPRETER;
      new_argv[argc]   = NULL;
    }
  }

  if (pid_p == NULL) {
    return orig_execve(final_exec, new_argv, new_envp);
  } else {
    return orig_posix_spawn((pid_t *) pid_p, final_exec, (const posix_spawn_file_actions_t *) file_actions,
                            (const posix_spawnattr_t *) attrp, new_argv, new_envp);
  }
}

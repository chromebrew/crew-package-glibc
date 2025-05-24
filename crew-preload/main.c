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
    - Redirect /bin/{bash,sh,coreutils} to ${CREW_PREFIX}/bin/{bash,sh,coreutils} instead (unless CREW_PRELOAD_NO_CREW_CMD=1)
    - Run all dynamically linked executables with Chromebrew's glibc/dynamic linker (unless CREW_PRELOAD_NO_CREW_GLIBC=1)

  If CREW_PRELOAD_ENABLE_COMPILE_HACKS is set, this wrapper will also:
    - Append --dynamic-linker flag to linker commend
    - Replace linker command with mold (can be disabled with CREW_PRELOAD_NO_MOLD)

  Usage: LD_PRELOAD=crew-preload.so <command>

  cc -O3 -fPIC -shared -fvisibility=hidden -Wl,-soname,crew-preload.so \
    -DCREW_PREFIX=\"...\" -DCREW_GLIBC_PREFIX=\"...\" \
    -DCREW_GLIBC_INTERPRETER=\"...\" \
    crew-preload.c -o crew-preload.so
*/

#include "./main.h"

bool  compile_hacks = false,
      initialized   = false,
      no_crew_cmd   = false,
      no_crew_glibc = false,
      no_mold       = false,
      verbose       = false;
pid_t pid           = 0;

struct utsname kernel_info;

const char *cmd_override_list[] = {
  "/bin/bash",
  "/bin/sh",
  "/usr/bin/coreutils"
};

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

  if (uname(&kernel_info) == -1) fprintf(stderr, "[PID %-7i] %s: uname() failed (%s)\n", pid, PROMPT_NAME, strerror(errno));

  if (strcmp(getenv("CREW_PRELOAD_ENABLE_COMPILE_HACKS") ?: "0", "1") == 0) compile_hacks = true;
  if (strcmp(getenv("CREW_PRELOAD_NO_CREW_CMD") ?: "0", "1") == 0)          no_crew_cmd   = true;
  if (strcmp(getenv("CREW_PRELOAD_NO_CREW_GLIBC") ?: "0", "1") == 0)        no_crew_glibc = true;
  if (strcmp(getenv("CREW_PRELOAD_NO_MOLD") ?: "0", "1") == 0)              no_mold       = true;
  if (strcmp(getenv("CREW_PRELOAD_VERBOSE") ?: "0", "1") == 0)              verbose       = true;

  pid              = getpid();
  orig_execve      = dlsym(RTLD_NEXT, "execve");
  orig_posix_spawn = dlsym(RTLD_NEXT, "posix_spawn");
  initialized      = true;

  if (verbose) fprintf(stderr, "[PID %-7i] %s: Running on %s kernel, glibc version %s\n", pid, PROMPT_NAME, kernel_info.machine, gnu_get_libc_version());

  // restore LD_LIBRARY_PATH from CREW_PRELOAD_LIBRARY_PATH if it was unset previously by LD_PRELOAD in the parent process
  if (old_library_path) {
    if (verbose) fprintf(stderr, "[PID %-7i] %s: LD_LIBRARY_PATH restored (%s)\n", pid, PROMPT_NAME, old_library_path);

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
  // va2array(): copy all variable arguments into a char array
  for (int i = 1; i < argc; i++) argv[i] = va_arg(argp, char *);
  argv[argc] = va_arg(argp, void *);
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

      if (verbose) fprintf(stderr, "[PID %-7i] %s: %s => %s\n", pid, PROMPT_NAME, file, result);
      break;
    } else if (access(result, F_OK) == 0) {
      // file found in path but it is not executable
      return_value = EACCES;
    }
  } while ((search_path = strtok(NULL, ":")));

  return return_value;
}

void get_elf_information(void *executable, off_t elf_size, struct ElfInfo *output) {
  uint8_t phnum;
  void    *program_header = executable;

  // check if file is ELF
  if (memcmp(executable, "\x7f""ELF", 4) != 0) return;

  // check ELF bitness, 32-bit or 64-bit
  output->is_64bit = (*((uint8_t *) (executable + 4)) == ELFCLASS64) ? true : false;
  if (verbose) fprintf(stderr, "[PID %-7i] %s: %i-bit ELF executable detected\n", pid, PROMPT_NAME, output->is_64bit ? 64 : 32);

  // get number of program headers
  phnum           = output->is_64bit ? ((Elf64_Ehdr *) executable)->e_phnum : ((Elf32_Ehdr *) executable)->e_phnum;
  program_header += output->is_64bit ? ((Elf64_Ehdr *) executable)->e_phoff : ((Elf32_Ehdr *) executable)->e_phoff;

  // parse ELF program header
  for (int i = 1; i < phnum; i++) {
    if (*((uint32_t *) program_header) == 3) {
      // section type 0x03: PT_INTERP
      output->is_dyn_exec       = true;
      output->pt_interp_section = program_header;

      // get memory address of interpreter string
      if (output->is_64bit) {
        output->interpreter = (char *) (executable + ((Elf64_Phdr *) program_header)->p_offset);
      } else {
        output->interpreter = (char *) (executable + ((Elf32_Phdr *) program_header)->p_offset);
      }

      if (verbose) fprintf(stderr, "[PID %-7i] %s: PT_INTERP section found at offset 0x%lx (%s)\n", pid, PROMPT_NAME, program_header - executable, output->interpreter);
      return;
    }

    program_header += output->is_64bit ? sizeof(Elf64_Phdr) : sizeof(Elf32_Phdr);
    if ((program_header + (output->is_64bit ? sizeof(Elf64_Phdr) : sizeof(Elf32_Phdr))) > (executable + elf_size)) break;
  }

  if (verbose) fprintf(stderr, "[PID %-7i] %s: PT_INTERP section not found, probably linked statically\n", pid, PROMPT_NAME);
  output->is_dyn_exec = false;
  return;
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
  bool    is_a_path    = false,
          is_linker    = false,
          is_system    = false;
  char    **new_argv   = alloca(4096 * sizeof(char *)),
          **new_envp   = alloca(4096 * sizeof(char *)),
          *filename    = basename(path_or_name),
          *final_exec  = alloca(PATH_MAX),
          *exec_in_mem = NULL;
  int     argc         = copy2array(argv, new_argv, 0),
          envc         = copy2array(envp, new_envp, 0),
          exec_fd;

  struct ElfInfo elf_info;
  struct stat    file_info;

  memset(&elf_info, 0, sizeof(elf_info));

  if (verbose) {
    if (pid_p == NULL) {
      if (perform_path_search) {
        fprintf(stderr, "[PID %-7i] %s: exec*p() called: %s\n", pid, PROMPT_NAME, path_or_name);
      } else {
        fprintf(stderr, "[PID %-7i] %s: exec*() called: %s\n", pid, PROMPT_NAME, path_or_name);
      }
    } else {
      if (perform_path_search) {
        fprintf(stderr, "[PID %-7i] %s: posix_spawn() called: %s\n", pid, PROMPT_NAME, path_or_name);
      } else {
        fprintf(stderr, "[PID %-7i] %s: posix_spawnp() called: %s\n", pid, PROMPT_NAME, path_or_name);
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

  stat(final_exec, &file_info);

  // check if path is directory
  if (S_ISDIR(file_info.st_mode)) return EISDIR;

  // check for permission first, raise an error if the executable isn't executable
  // EACCES: Permission denied
  if (access(final_exec, X_OK) != 0) return EACCES;

  // for commands listed in cmd_override_list, always use Chromebrew provided one if available
  for (int i = 0; i < (int) (sizeof(cmd_override_list) / sizeof(char *)); i++) {
    if (strcmp(final_exec, cmd_override_list[i]) == 0) {
      char *new_path;

      if (no_crew_cmd) {
        if (verbose) fprintf(stderr, "[PID %-7i] %s: CREW_PRELOAD_NO_CREW_CMD set, will NOT modify command path\n", pid, PROMPT_NAME);
      } else if (asprintf(&new_path, "%s%s", CREW_PREFIX, final_exec) > 0 && access(new_path, X_OK) == 0) {
        if (verbose) fprintf(stderr, "[PID %-7i] %s: Will use Chromebrew version of %s instead...\n", pid, PROMPT_NAME, basename(final_exec));
        strncpy(final_exec, new_path, PATH_MAX);
      }

      break;
    }
  }

  // unset LD_PRELOAD/LD_LIBRARY_PATH when executable is libc.so.6, as it will cause segfaults
  if (strcmp(filename, "libc.so.6") == 0) {
    if (verbose) fprintf(stderr, "[PID %-7i] %s: libc.so.6 detected, will execute with LD_* unset...\n", pid, PROMPT_NAME);

    unsetenvfp(new_envp, "LD_LIBRARY_PATH");
    unsetenvfp(new_envp, "LD_PRELOAD");
    envc--;
  } else {
    // check if executable is a system command or not
    for (int i = 0; i < (int) (sizeof(system_exe_path) / sizeof(char *)); i++) {
      if (strncmp(final_exec, system_exe_path[i], strlen(system_exe_path[i])) == 0) {
        is_system = true;
        break;
      }
    }

    // try searching in CREW_PREFIX if the executable is prefixed with system path and cannot be found
    if (is_system && access(final_exec, F_OK) != 0) {
      if (asprintf(&final_exec, "%s/bin/%s", CREW_PREFIX, filename) > 0 && access(final_exec, F_OK) == 0) {
        if (verbose) fprintf(stderr, "[PID %-7i] %s: %s => %s\n", pid, PROMPT_NAME, path_or_name, final_exec);
        is_system = false;
      } else {
        // return an error if we cannot find any matching executables
        // ENOENT: No such file or directory
        return ENOENT;
      }
    }

    if ((exec_fd = open(final_exec, O_RDONLY | O_CLOEXEC)) == -1) {
      if (verbose) fprintf(stderr, "[PID %-7i] %s: Failed to open %s for reading (%s)\n", pid, PROMPT_NAME, final_exec, strerror(errno));
    } else {
      fstat(exec_fd, &file_info);

      // map the executable into memory region for convenience
      exec_in_mem = mmap(NULL, file_info.st_size + PATH_MAX, PROT_READ | PROT_WRITE, MAP_PRIVATE, exec_fd, 0);
      get_elf_information(exec_in_mem, file_info.st_size, &elf_info);

      // update LD_PRELOAD value if needed
      unsetenvfp(new_envp, "LD_PRELOAD");

      if (elf_info.is_64bit) {
        if (strcmp(kernel_info.machine, "aarch64") != 0 && strcmp(kernel_info.machine, "x86_64") != 0) {
          // return ENOEXEC if the system doesn't support 64-bit executables
          // ENOEXEC: Exec format error
          return ENOEXEC;
        }

        // execute 64-bit binaries with 64-bit version of crew-preload.so
        asprintf(&new_envp[envc - 1], "LD_PRELOAD=%s/lib64/crew-preload.so", CREW_PREFIX);
      } else {
        asprintf(&new_envp[envc - 1], "LD_PRELOAD=%s/lib/crew-preload.so", CREW_PREFIX);
      }

      new_envp[envc] = NULL;

      // unset LD_LIBRARY_PATH for system commands, original value will be copied into CREW_PRELOAD_LIBRARY_PATH environment variable
      // (see https://github.com/chromebrew/chromebrew/issues/5777 for more information)
      if (is_system && elf_info.is_dyn_exec) {
        if (verbose) fprintf(stderr, "[PID %-7i] %s: System command detected, will execute with LD_LIBRARY_PATH unset...\n", pid, PROMPT_NAME);

        unsetenvfp(new_envp, "LD_LIBRARY_PATH");
        asprintf(&new_envp[envc++], "CREW_PRELOAD_LIBRARY_PATH=%s", getenv("LD_LIBRARY_PATH"));
        new_envp[envc] = NULL;
      }

      // parse shebang and re-execute with specified interpreter if the executable is a script
      if (memcmp(exec_in_mem, "#!", 2) == 0) {
        char shebang[PATH_MAX], *script_path, *interpreter_opt;

        strncpy(shebang, exec_in_mem + 2, (strcspn(exec_in_mem + 2, "\n") < PATH_MAX) ? strcspn(exec_in_mem + 2, "\n") : PATH_MAX);

        script_path                             = strdup(final_exec);
        shebang[strcspn(exec_in_mem + 2, "\n")] = '\0';

        if (verbose) fprintf(stderr, "[PID %-7i] %s: %s is a script with shebang: '#!%s'\n", pid, PROMPT_NAME, script_path, shebang);

        // get shebang value
        strncpy(final_exec, strtok(shebang, " "), PATH_MAX);

        // extract interpreter path and interpreter argument (if any)
        if ((interpreter_opt = strtok(NULL, "\n"))) {
          new_argv[0] = final_exec;
          new_argv[1] = interpreter_opt;
          new_argv[2] = script_path;
          argc        = copy2array(&argv[1], new_argv, 3); // copy all arguments except argv[0]
        } else {
          new_argv[0] = final_exec;
          new_argv[1] = script_path;
          argc        = copy2array(&argv[1], new_argv, 2); // copy all arguments except argv[0]
        }

        if (verbose) fprintf(stderr, "[PID %-7i] %s: Will re-execute as: %s %s %.20s ...\n", pid, PROMPT_NAME, new_argv[0], new_argv[1], new_argv[2]);

        return exec_wrapper(final_exec, new_argv, new_envp, false, pid_p, file_actions, attrp);
      }
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
          if (verbose) fprintf(stderr, "[PID %-7i] %s: CREW_PRELOAD_NO_MOLD is set, will NOT modify linker path\n", pid, PROMPT_NAME);
        } else {
          if (verbose) fprintf(stderr, "[PID %-7i] %s: Linker detected (%s), will use mold linker\n", pid, PROMPT_NAME, filename);

          int ret = search_in_path("mold", mold_exec);

          if (ret == 0) {
            strncpy(final_exec, mold_exec, PATH_MAX);
          } else {
            fprintf(stderr, "[PID %-7i] %s: Mold linker is not executable (%s), will NOT modify linker path\n", pid, PROMPT_NAME, strerror(ret));
          };
        }

        if (verbose) fprintf(stderr, "[PID %-7i] %s: Appending --dynamic-linker flag to the linker...\n", pid, PROMPT_NAME);

        new_argv[argc++] = "--dynamic-linker";
        new_argv[argc++] = CREW_GLIBC_INTERPRETER;
        new_argv[argc]   = NULL;
      }
    }

    // modify ELF interpreter path (in-memory only) to Chromebrew's glibc before executing if needed
    if (!no_crew_glibc && exec_in_mem && elf_info.is_dyn_exec &&
        elf_info.is_64bit == CREW_GLIBC_IS_64BIT &&
        strcmp(elf_info.interpreter, CREW_GLIBC_INTERPRETER) != 0) {

      int   memfd                   = syscall(SYS_memfd_create, final_exec, 1),
            old_section_header_size = 0;
      void  *old_section_header     = NULL;

      if (verbose) fprintf(stderr, "[PID %-7i] %s: Will execute %s with Chromebrew's dynamic linker\n", pid, PROMPT_NAME, final_exec);

      if (memfd == -1) {
        // fallback to legacy ld-linux.so way for systems that don't support memfd_create()
        // load and run executable using Chromebrew's dynamic linker
        new_argv[0] = CREW_GLIBC_INTERPRETER;
        new_argv[1] = strdup(final_exec);

        copy2array(&argv[1], new_argv, 2);
        strncpy(final_exec, CREW_GLIBC_INTERPRETER, PATH_MAX);

        if (verbose) fprintf(stderr, "[PID %-7i] %s: Will execute as: %s %s %.20s...\n", pid, PROMPT_NAME, new_argv[0], new_argv[1], new_argv[2]);
      } else {
        // allocate room between the last ELF section and the first section header for our new interpreter's path
        // Before:
        //
        //   | ELF header | Program headers | Sections | Section headers |
        //
        // After:
        //
        //   | ELF header | Program headers | Sections | New interpreter path | Section headers |
        //
        if (verbose) fprintf(stderr, "[PID %-7i] %s: Modifying ELF interpreter path for %s...\n", pid, PROMPT_NAME, final_exec);

        if (elf_info.is_64bit) {
          old_section_header      = exec_in_mem + ((Elf64_Ehdr *) exec_in_mem)->e_shoff;
          old_section_header_size = file_info.st_size - ((Elf64_Ehdr *) exec_in_mem)->e_shoff;

          // update section header offset and point PT_INTERP to our new interpreter string
          ((Elf64_Ehdr *) exec_in_mem)->e_shoff                 = file_info.st_size + sizeof(CREW_GLIBC_INTERPRETER) - old_section_header_size;
          ((Elf64_Phdr *) elf_info.pt_interp_section)->p_offset = file_info.st_size - old_section_header_size;
          ((Elf64_Phdr *) elf_info.pt_interp_section)->p_paddr  = file_info.st_size - old_section_header_size;
          ((Elf64_Phdr *) elf_info.pt_interp_section)->p_vaddr  = file_info.st_size - old_section_header_size;
          ((Elf64_Phdr *) elf_info.pt_interp_section)->p_filesz = sizeof(CREW_GLIBC_INTERPRETER);
          ((Elf64_Phdr *) elf_info.pt_interp_section)->p_memsz  = sizeof(CREW_GLIBC_INTERPRETER);
        } else {
          old_section_header      = exec_in_mem + ((Elf32_Ehdr *) exec_in_mem)->e_shoff;
          old_section_header_size = file_info.st_size - ((Elf32_Ehdr *) exec_in_mem)->e_shoff;

          // update section header offset and point PT_INTERP to our new interpreter string
          ((Elf32_Ehdr *) exec_in_mem)->e_shoff                 = file_info.st_size + sizeof(CREW_GLIBC_INTERPRETER) - old_section_header_size;
          ((Elf32_Phdr *) elf_info.pt_interp_section)->p_offset = file_info.st_size - old_section_header_size;
          ((Elf32_Phdr *) elf_info.pt_interp_section)->p_paddr  = file_info.st_size - old_section_header_size;
          ((Elf32_Phdr *) elf_info.pt_interp_section)->p_vaddr  = file_info.st_size - old_section_header_size;
          ((Elf32_Phdr *) elf_info.pt_interp_section)->p_filesz = sizeof(CREW_GLIBC_INTERPRETER);
          ((Elf32_Phdr *) elf_info.pt_interp_section)->p_memsz  = sizeof(CREW_GLIBC_INTERPRETER);
        }

        if (verbose) fprintf(stderr, "[PID %-7i] %s: New PT_INTERP for %s: %s\n", pid, PROMPT_NAME, final_exec, CREW_GLIBC_INTERPRETER);
        if (verbose) fprintf(stderr, "[PID %-7i] %s: Writing modified executable into memfd %i...\n", pid, PROMPT_NAME, memfd);

        // write modified executable with updated PT_INTERP segment to memfd
        write(memfd, exec_in_mem, file_info.st_size - old_section_header_size);
        write(memfd, CREW_GLIBC_INTERPRETER, sizeof(CREW_GLIBC_INTERPRETER));
        write(memfd, old_section_header, old_section_header_size);

        snprintf(final_exec, PATH_MAX, "/proc/self/fd/%i", memfd);
        if (verbose) fprintf(stderr, "[PID %-7i] %s: New executable path: %s\n", pid, PROMPT_NAME, final_exec);
      }
    }
  }

  if (pid_p == NULL) {
    return orig_execve(final_exec, new_argv, new_envp);
  } else {
    return orig_posix_spawn((pid_t *) pid_p, final_exec, (const posix_spawn_file_actions_t *) file_actions,
                            (const posix_spawnattr_t *) attrp, new_argv, new_envp);
  }
}

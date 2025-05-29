## crew-preload.so: A LD_PRELOAD wrapper for Chromebrew environments

This wrapper does the following things:
  - Fix hardcoded shebang/command path (e.g `#!/usr/bin/perl` will be converted to `#!${CREW_PREFIX}/bin/perl`):
    ```
    $ LD_PRELOAD=crew-preload.so CREW_PRELOAD_VERBOSE=1 bash -c '/usr/bin/file --version'
    crew-preload: exec*() called: /usr/bin/file
    crew-preload: /usr/bin/file => /usr/local/bin/file
    file-5.46
    magic file from /usr/local/share/misc/magic
    ```
  - Unset `LD_LIBRARY_PATH` before running any system commands (executables located under `/{bin,sbin}` or `/usr/{bin,sbin}`):
  - Redirect `/bin/{bash,sh,coreutils}` to `${CREW_PREFIX}/bin/{bash,sh,coreutils}` instead (unless `CREW_PRELOAD_NO_CREW_CMD=1`):
    ```
    $ echo '#!/bin/bash' > test; chmod +x test
    $ LD_PRELOAD=crew-preload.so bash -c 'CREW_PRELOAD_VERBOSE=1 ./test'
    [PID 20327] crew-preload: exec*() called: ./test
    [PID 20327] crew-preload: ./test is a script with shebang: '#!/bin/bash'
    [PID 20327] crew-preload: Will re-execute as: /bin/bash ./test (null) ...
    [PID 20327] crew-preload: exec*() called: /bin/bash
    [PID 20327] crew-preload: Will use Chromebrew version of bash instead...
    ```
  - Run all dynamically linked executables with Chromebrew's glibc/dynamic linker (unless `CREW_PRELOAD_NO_CREW_GLIBC=1`)

If `CREW_PRELOAD_ENABLE_COMPILE_HACKS` is set, this wrapper will also:
  - Append `--dynamic-linker` flag to linker commend
  - Replace linker command with `mold` (can be disabled with `CREW_PRELOAD_NO_MOLD`)

### Available environment variables
|Name                               |Description                                                        |
|:----------------------------------|:------------------------------------------------------------------|
|`CREW_PRELOAD_VERBOSE`             |Enable verbose logging                                             |
|`CREW_PRELOAD_DISABLED`            |Disable all hacks, will not do anything besides initializing       |
|`CREW_PRELOAD_ENABLE_COMPILE_HACKS`|Enable hacks that help compile (see above)                         |
|`CREW_PRELOAD_NO_CREW_CMD`         |Do not redirect `/bin/{bash,sh,coreutils}`                         |
|`CREW_PRELOAD_NO_CREW_GLIBC`       |Do not run executables with Chromebrew's dynamic linker by default |
|`CREW_PRELOAD_NO_MOLD`             |Do not rewrite linker program to `mold`                            |

### Usage
```shell
cc -O3 -fPIC -shared -fvisibility=hidden -Wl,-soname,crew-preload.so \
  -DCREW_PREFIX=\"...\" -DCREW_GLIBC_PREFIX=\"...\" \
  -DCREW_GLIBC_INTERPRETER=\"...\" \
  main.c hooks.c -o crew-preload.so

LD_PRELOAD=crew-preload.so <command>
```

#### Building may require this command on i686:
```shell
cc -O3 -fPIC -shared -fvisibility=hidden -Wl,-soname,crew-preload.so \
   -DCREW_PREFIX=\"...\" -DCREW_GLIBC_PREFIX=\"...\" \
   -DCREW_GLIBC_INTERPRETER=\"...\" \
   -Wl,--no-as-needed -ldl \
   main.c hooks.c -o crew-preload.so

LD_PRELOAD=crew-preload.so <command>
```

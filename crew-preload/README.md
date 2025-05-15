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
  - Calls to `/bin/{bash,sh}` will be redirected to `${CREW_PREFIX}/bin/{bash,sh}` (unless `CREW_PRELOAD_NO_CREW_SHELL=1`):
    ```
    $ LD_PRELOAD=crew-preload.so CREW_PRELOAD_VERBOSE=1 bash -c '/bin/sh -c ""'
    crew-preload: exec*() called: /bin/sh
    crew-preload: Shell detected (/bin/sh), will use Chromebrew version of sh instead
    ```

If `CREW_PRELOAD_ENABLE_COMPILE_HACKS` is set, this wrapper will also:
  - Append `--dynamic-linker` flag to linker commend
  - Replace linker command with `mold` (can be disabled with `CREW_PRELOAD_NO_MOLD`)

### Available environment variables
|Name                               |Description                                   |
|:----------------------------------|:---------------------------------------------|
|`CREW_PRELOAD_VERBOSE`             |Enable verbose logging                        |
|`CREW_PRELOAD_ENABLE_COMPILE_HACKS`|Enable hacks that help compile (see above)    |
|`CREW_PRELOAD_NO_CREW_SHELL`       |Do not rewrite shell path to Chromebrew's one |
|`CREW_PRELOAD_NO_MOLD`             |Do not rewrite linker program to `mold`       |

### Usage
```shell
cc -O3 -fPIC -shared -fvisibility=hidden \
  -DCREW_PREFIX=\"...\" -DCREW_GLIBC_PREFIX=\"...\" \
  -DCREW_GLIBC_INTERPRETER=\"...\" -DSYSTEM_GLIBC_INTERPRETER=\"...\" \
  crew-preload.c -o crew-preload.so

LD_PRELOAD=crew-preload.so <command>
```

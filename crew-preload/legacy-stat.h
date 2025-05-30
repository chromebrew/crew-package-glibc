/*
  Copyright (C) 1991-2016 Free Software Foundation, Inc.
  Copyright (C) 2013-2025 Chromebrew Authors

  This file is part of the GNU C Library.

  The GNU C Library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  The GNU C Library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with the GNU C Library; if not, see
  <http://www.gnu.org/licenses/>.
*/

/*
  legacy-stat.h: Old *stat{at}() function definition (from sys/stat.h in glibc 2.23)

  This file addresses the following error:

  sh: symbol lookup error: /usr/local/lib/crew-preload.so: undefined symbol: stat
*/

#ifndef _LEGACY_STAT_H
#include <sys/stat.h>

#ifndef _STAT_VER
#define _STAT_VER 3
#endif

/* Wrappers for stat and mknod system calls.  */
#ifndef __USE_FILE_OFFSET64
extern int __fxstat (int __ver, int __fildes, struct stat *__stat_buf)
     __THROW __nonnull ((3));
extern int __xstat (int __ver, const char *__filename,
		    struct stat *__stat_buf) __THROW __nonnull ((2, 3));
extern int __lxstat (int __ver, const char *__filename,
		     struct stat *__stat_buf) __THROW __nonnull ((2, 3));
extern int __fxstatat (int __ver, int __fildes, const char *__filename,
		       struct stat *__stat_buf, int __flag)
     __THROW __nonnull ((3, 4));
#else
# ifdef __REDIRECT_NTH
extern int __REDIRECT_NTH (__fxstat, (int __ver, int __fildes,
				      struct stat *__stat_buf), __fxstat64)
     __nonnull ((3));
extern int __REDIRECT_NTH (__xstat, (int __ver, const char *__filename,
				     struct stat *__stat_buf), __xstat64)
     __nonnull ((2, 3));
extern int __REDIRECT_NTH (__lxstat, (int __ver, const char *__filename,
				      struct stat *__stat_buf), __lxstat64)
     __nonnull ((2, 3));
extern int __REDIRECT_NTH (__fxstatat, (int __ver, int __fildes,
					const char *__filename,
					struct stat *__stat_buf, int __flag),
			   __fxstatat64) __nonnull ((3, 4));

# else
#  define __fxstat __fxstat64
#  define __xstat __xstat64
#  define __lxstat __lxstat64
# endif
#endif

#ifdef __USE_LARGEFILE64
extern int __fxstat64 (int __ver, int __fildes, struct stat64 *__stat_buf)
     __THROW __nonnull ((3));
extern int __xstat64 (int __ver, const char *__filename,
		      struct stat64 *__stat_buf) __THROW __nonnull ((2, 3));
extern int __lxstat64 (int __ver, const char *__filename,
		       struct stat64 *__stat_buf) __THROW __nonnull ((2, 3));
extern int __fxstatat64 (int __ver, int __fildes, const char *__filename,
			 struct stat64 *__stat_buf, int __flag)
     __THROW __nonnull ((3, 4));
#endif

#define stat(fname, buf) __xstat (_STAT_VER, fname, buf)
#define lstat(fname, buf)  __lxstat (_STAT_VER, fname, buf)
#define __lstat(fname, buf)  __lxstat (_STAT_VER, fname, buf)
#define lstat64(fname, buf)  __lxstat64 (_STAT_VER, fname, buf)
#define __lstat64(fname, buf)  __lxstat64 (_STAT_VER, fname, buf)
#define stat64(fname, buf) __xstat64 (_STAT_VER, fname, buf)
#define fstat64(fd, buf) __fxstat64 (_STAT_VER, fd, buf)
#define __fstat64(fd, buf) __fxstat64 (_STAT_VER, fd, buf)
#define fstat(fd, buf) __fxstat (_STAT_VER, fd, buf)
#define __fstat(fd, buf) __fxstat (_STAT_VER, fd, buf)
#define __fstatat(dfd, fname, buf, flag) \
  __fxstatat (_STAT_VER, dfd, fname, buf, flag)
#define __fstatat64(dfd, fname, buf, flag) \
  __fxstatat64 (_STAT_VER, dfd, fname, buf, flag)

#define _LEGACY_STAT_H
#endif

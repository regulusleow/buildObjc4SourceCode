/*
 * Copyright (c) 2011 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */
#include <sys/cdefs.h>
#include <sys/types.h>
#include <stdarg.h>
#include <sys/fcntl.h>
#include <sys/errno.h>
#include <sys/content_protection.h>

int __open_dprotected_np(const char* path, int flags, int class, int dpflags, int mode);
int __openat_dprotected_np(int fd, const char* path, int flags, int class, int dpflags, int mode, int authfd);

int
open_dprotected_np(const char *path, int flags, int class, int dpflags, ...)
{
	int mode = 0;

	if (dpflags & O_DP_AUTHENTICATE) {
		errno = EINVAL;
		return -1;
	}

	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, dpflags);
		mode = va_arg(ap, int);
		va_end(ap);
	}
	return __open_dprotected_np(path, flags, class, dpflags, mode);
}

int
openat_dprotected_np(int fd, const char *path, int flags, int class, int dpflags, ...)
{
	int mode = 0;

	if (dpflags & O_DP_AUTHENTICATE) {
		errno = EINVAL;
		return -1;
	}

	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, dpflags);
		mode = va_arg(ap, int);
		va_end(ap);
	}
	return __openat_dprotected_np(fd, path, flags, class, dpflags, mode, AUTH_OPEN_NOAUTHFD);
}

int
openat_authenticated_np(int fd, const char *path, int flags, int authfd)
{
	if (flags & O_CREAT) {
		errno = EINVAL;
		return -1;
	}

	if ((authfd != AUTH_OPEN_NOAUTHFD) && (authfd < 0)) {
		errno = EBADF;
		return -1;
	}

	return __openat_dprotected_np(fd, path, flags, PROTECTION_CLASS_DEFAULT, O_DP_AUTHENTICATE, 0, authfd);
}

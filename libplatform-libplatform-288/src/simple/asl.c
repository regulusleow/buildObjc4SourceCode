/*
 * Copyright (c) 2005, 2006, 2009 Apple Computer, Inc. All rights reserved.
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

#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syslog.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <crt_externs.h>

#include <TargetConditionals.h>

#include <_simple.h>

#include <os/lock.h>
#include <os/lock_private.h>
#include <os/alloc_once_private.h>
#include <platform/string.h>
#include <platform/compat.h>

#if TARGET_OS_DRIVERKIT
// DriverKit processes log directly to kernel log
#include <sys/log_data.h>
OS_ENUM(os_log_type, uint8_t,
	OS_LOG_TYPE_DEFAULT = 0x00,
	OS_LOG_TYPE_INFO    = 0x01,
	OS_LOG_TYPE_DEBUG   = 0x02,
);
#else // !TARGET_OS_DRIVERKIT
#include <os/log_simple_private.h>

#define ASL_LOG_PATH _PATH_LOG

#include "os/internal.h"

extern ssize_t __sendto(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
extern int __gettimeofday(struct timeval *, struct timezone *);

struct ProgramVars
{
    void*       mh;
    int*        NXArgcPtr;
    char***     NXArgvPtr;
    char***     environPtr;
    char**      __prognamePtr;
};

struct asl_context {
	bool asl_enabled;
	const char *progname;
	int asl_fd;
#if TARGET_OS_SIMULATOR && !TARGET_OS_MACCATALYST
	const char *sim_log_path;
	os_unfair_lock sim_connect_lock;
#else
	os_once_t connect_once;
#endif
};

static struct asl_context* _simple_asl_get_context(void);
static void _simple_asl_init_context(void *ctx);
static int _simple_asl_connect(const char *log_path);
int _simple_asl_get_fd(void);

/*
 * Simplified ASL log interface; does not use malloc.  Unfortunately, this
 * requires knowledge of the format used by ASL.
 */

__attribute__((visibility("hidden")))
void
_simple_asl_init(const char *envp[], const struct ProgramVars *vars)
{
	const char *str;
	struct asl_context *ctx = _simple_asl_get_context();
	str = _simple_getenv(envp, "ASL_DISABLE");
	if ((str != NULL) && (!strcmp(str, "1"))) return;
	ctx->asl_enabled = true;
	if (vars && vars->__prognamePtr) {
		ctx->progname = *(vars->__prognamePtr);
#if TARGET_OS_SIMULATOR
	} else {
		const char * progname = *_NSGetProgname();
		if (progname)
			ctx->progname = progname;
#endif
	}
}

static int
_simple_asl_connect(const char *log_path)
{
	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd == -1) return -1;

	fcntl(fd, F_SETFD, FD_CLOEXEC);

	struct sockaddr_un addr;
	addr.sun_family = AF_UNIX;

	size_t amt = strlen(log_path) + 1;
	if (sizeof(addr.sun_path) < amt)
		amt = sizeof(addr.sun_path);
	memmove(addr.sun_path, log_path, amt);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		close(fd);
		return -1;
	}
	return fd;
}

#if !TARGET_OS_SIMULATOR || TARGET_OS_MACCATALYST
static void
_simple_asl_connect_once(void * __unused once_arg)
{
	struct asl_context *ctx = _simple_asl_get_context();
	// Need to check whether we're potentially arriving after
	// _os_log_simple_reinit_4launchd
	if (ctx->asl_fd == -1) {
		ctx->asl_fd = _simple_asl_connect(ASL_LOG_PATH);
	}
}
#endif

void
_os_log_simple_reinit_4launchd(void)
{
#if TARGET_OS_SIMULATOR && !TARGET_OS_MACCATALYST
	// nothing to do; the regular setup flow should work because the connection
	// attempt won't happen until IOS_SIMULATOR_SYSLOG_SOCKET is set in the
	// environment, which launchd doesn't do until after the socket is ready to
	// connect
#else
	struct asl_context *ctx = _simple_asl_get_context();
	if (!ctx->asl_enabled) {
		return;
	}

	if (ctx->asl_fd != -1) {
		__LIBPLATFORM_CLIENT_CRASH__(ctx->asl_fd, "asl fd already initialized");
	}

	ctx->asl_fd = _simple_asl_connect(ASL_LOG_PATH);
#endif
}

int
_simple_asl_get_fd(void)
{
	struct asl_context *ctx = _simple_asl_get_context();
	if (!ctx->asl_enabled) {
		return -1;
	}

#if TARGET_OS_SIMULATOR && !TARGET_OS_MACCATALYST
	os_unfair_lock_lock_with_options(&ctx->sim_connect_lock,
			OS_UNFAIR_LOCK_DATA_SYNCHRONIZATION);
	if (ctx->sim_log_path) {
		// all setup has been done already
		os_unfair_lock_unlock(&ctx->sim_connect_lock);
		return ctx->asl_fd;
	}
	ctx->sim_log_path = _simple_getenv(
			(const char **)*_NSGetEnviron(), "IOS_SIMULATOR_SYSLOG_SOCKET");
	if (ctx->sim_log_path) {
		// the first and only time the envvar is being checked
		// asl_fd procured by the end of this call will be used forever
		int sim_log_fd = _simple_asl_connect(ctx->sim_log_path);
		if (sim_log_fd > -1) {
			// successfully connected to the SIM path
			if (ctx->asl_fd > -1) {
				// close the ASL_LOG_PATH fd
				close(ctx->asl_fd);
			}
			ctx->asl_fd = sim_log_fd;
		}
	}
	if (ctx->asl_fd < 0) {
		// either there is no envvar or it didn't work. fallback to ASL_LOG_PATH
		ctx->asl_fd = _simple_asl_connect(ASL_LOG_PATH);
	}
	os_unfair_lock_unlock(&ctx->sim_connect_lock);
	return ctx->asl_fd;
#else
	os_once(&ctx->connect_once, NULL, _simple_asl_connect_once);
	return ctx->asl_fd;
#endif
}
#endif // !TARGET_OS_DRIVERKIT

static const char *
_simple_asl_escape_key(unsigned char c)
{
	switch(c)
	{
		case '\\': return "\\\\";
		case '[':  return "\\[";
		case ']':  return "\\]";
		case '\n': return "\\n";
		case ' ':  return "\\s";
	}

	return NULL;
}

static const char *
_simple_asl_escape_val(unsigned char c)
{
	switch(c)
	{
		case '\\': return "\\\\";
		case '[':  return "\\[";
		case ']':  return "\\]";
		case '\n': return "\\n";
	}

	return NULL;
}

_SIMPLE_STRING
_simple_asl_msg_new(void)
{
	_SIMPLE_STRING b = _simple_salloc();

	if (b == NULL) return NULL;

	if (_simple_sprintf(b, "         0"))
	{
		_simple_sfree(b);
		return NULL;
	}

	return b;
}

void
_simple_asl_msg_set(_SIMPLE_STRING __b, const char *__key, const char *__val)
{
	if (__b == NULL) return;
	if (__key == NULL) return;

	do
	{
		if (_simple_sprintf(__b, " [")) break;
		if (_simple_esprintf(__b, _simple_asl_escape_key, "%s", __key)) break;
		if (__val != NULL)
		{
			if (_simple_esprintf(__b, _simple_asl_escape_val, " %s", __val)) break;
			if (!strcmp(__key, "Message"))
			{
				char *cp;

				/* remove trailing (escaped) newlines */
				cp = _simple_string(__b);
				cp += strlen(cp);
				for (;;)
				{
					cp -= 2;
					if (strcmp(cp, "\\n") != 0) break;
					*cp = 0;
				}

				_simple_sresize(__b);
			}
		}

		if (_simple_sappend(__b, "]")) break;
		return;
	} while (0);
}

void
_simple_asl_send(_SIMPLE_STRING __b)
{
#if !TARGET_OS_DRIVERKIT
	struct timeval tv;
	int asl_fd = _simple_asl_get_fd();
	if (asl_fd < 0) return;

	__gettimeofday(&tv, NULL);

	do
	{
		char *cp;

		if (_simple_sprintf(__b, " [PID ")) break;
		if (_simple_esprintf(__b, _simple_asl_escape_val, "%u", getpid())) break;
		if (_simple_sprintf(__b, "] [UID ")) break;
		if (_simple_esprintf(__b, _simple_asl_escape_val, "%u", getuid())) break;
		if (_simple_sprintf(__b, "] [GID ")) break;
		if (_simple_esprintf(__b, _simple_asl_escape_val, "%u", getgid())) break;
		if (_simple_sprintf(__b, "] [Time ")) break;
		if (_simple_esprintf(__b, _simple_asl_escape_val, "%lu", tv.tv_sec)) break;
		if (_simple_sappend(__b, "] [TimeNanoSec ")) break;
		if (_simple_esprintf(__b, _simple_asl_escape_val, "%d", tv.tv_usec * 1000)) break;
		if (_simple_sappend(__b, "]\n")) break;

		cp = _simple_string(__b);
		__sendto(asl_fd, cp, strlen(cp), 0, NULL, 0);
    } while (0);
#else // TARGET_OS_DRIVERKIT
	char *cp;
	cp = _simple_string(__b);
	log_data_as_kernel(0, OS_LOG_TYPE_DEFAULT, cp, strlen(cp) + 1);
#endif // TARGET_OS_DRIVERKIT
}

void
_simple_asl_log_prog(int level, const char *facility, const char *message, const char *prog)
{
#if !TARGET_OS_DRIVERKIT
	_os_log_simple_shim(os_log_simple_type_from_asl(level), facility, message);
#else // TARGET_OS_DRIVERKIT
	_SIMPLE_STRING b = _simple_asl_msg_new();
	if (b == NULL) return;
	if (prog) _simple_asl_msg_set(b, "Sender", prog);
	_simple_asl_msg_set(b, "Facility", facility);
	_simple_asl_msg_set(b, "Message", message);

	os_log_type_t type = level > ASL_LEVEL_INFO ? OS_LOG_TYPE_DEFAULT :
			(level > ASL_LEVEL_DEBUG ? OS_LOG_TYPE_INFO : OS_LOG_TYPE_DEBUG);

	char *cp;
	cp = _simple_string(b);
	log_data_as_kernel(0, type, cp, strlen(cp) + 1);
	_simple_sfree(b);
#endif // TARGET_OS_DRIVERKIT
}

void
_simple_asl_log(int level, const char *facility, const char *message)
{
#if !TARGET_OS_DRIVERKIT
	_simple_asl_log_prog(level, facility, message,
			_simple_asl_get_context()->progname);
#else // TARGET_OS_DRIVERKIT
	_simple_asl_log_prog(level, facility, message, NULL);
#endif // TARGET_OS_DRIVERKIT
}

#if !TARGET_OS_DRIVERKIT
static struct asl_context *
_simple_asl_get_context(void)
{
	return os_alloc_once(OS_ALLOC_ONCE_KEY_LIBSYSTEM_PLATFORM_ASL,
			sizeof(struct asl_context), _simple_asl_init_context);
}

static void
_simple_asl_init_context(void *arg)
{
	struct asl_context *ctx = (struct asl_context *)arg;
	// ctx is zero-filled when it comes out of _os_alloc_once
	ctx->progname = "unknown";
	ctx->asl_fd = -1;
}
#endif // !TARGET_OS_DRIVERKIT

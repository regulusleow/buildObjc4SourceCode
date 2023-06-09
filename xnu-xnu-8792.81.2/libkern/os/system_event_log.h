/*
 * Copyright (c) 2021 Apple Inc. All rights reserved.
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

#ifndef __os_system_event_log_h
#define __os_system_event_log_h

#include <Availability.h>
#include <os/object.h>
#include <stdint.h>
#include <sys/event_log.h>

__BEGIN_DECLS

/* uncrustify weirdly indents the first availability line */
/* BEGIN IGNORE CODESTYLE */
__WATCHOS_AVAILABLE(9.0) __OSX_AVAILABLE(13.0) __IOS_AVAILABLE(16.0) __TVOS_AVAILABLE(16.0)
/* END IGNORE CODESTYLE */
OS_EXPORT OS_NOTHROW
void
record_system_event(uint8_t type, uint8_t subsystem, const char *event, const char *format, ...) __printflike(4, 5);

__WATCHOS_AVAILABLE(9.0) __OSX_AVAILABLE(13.0) __IOS_AVAILABLE(16.0) __TVOS_AVAILABLE(16.0)
OS_EXPORT OS_NOTHROW
void
record_system_event_no_varargs(uint8_t type, uint8_t subsystem, const char *event, const char *payload);

__END_DECLS

#endif /* __os_system_event_log_h */

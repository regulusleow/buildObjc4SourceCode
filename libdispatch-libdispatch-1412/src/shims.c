/*
 * Copyright (c) 2013-2016 Apple Inc. All rights reserved.
 *
 * @APPLE_APACHE_LICENSE_HEADER_START@
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @APPLE_APACHE_LICENSE_HEADER_END@
 */

#include "internal.h"
#include "shims.h"

#if !HAVE_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t size)
{
	size_t srclen = strlen(src);
	if (srclen < size) {
		strncpy(dst, src, srclen + 1);
	} else if (size > 0) {
		strncpy(dst, src, size-1);
		dst[size-1] = '\0';
	}
	return srclen;
}
#endif

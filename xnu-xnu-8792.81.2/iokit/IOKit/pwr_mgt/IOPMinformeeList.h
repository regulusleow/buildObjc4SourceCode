/*
 * Copyright (c) 1998-2019 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */
#include <libkern/c++/OSObject.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOReturn.h>

class IOPMinformee;
class IOService;
extern uint32_t gSleepAckTimeout;

class IOPMinformeeList : public OSObject
{
	OSDeclareDefaultStructors(IOPMinformeeList);
	friend class IOPMinformee;

private:
// pointer to first informee in the list
	IOPMinformee       *firstItem;
// how many informees are in the list
	unsigned long       length;

public:
	void initialize( void );
	void free( void ) APPLE_KEXT_OVERRIDE;

	unsigned long numberOfItems( void );

	LIBKERN_RETURNS_NOT_RETAINED IOPMinformee *appendNewInformee( IOService * newObject );

// OBSOLETE
// do not use addToList(); Use appendNewInformee() instead
	IOReturn addToList(LIBKERN_CONSUMED IOPMinformee *   newInformee );
	IOReturn removeFromList( IOService * theItem );

	LIBKERN_RETURNS_NOT_RETAINED IOPMinformee * firstInList( void );
	LIBKERN_RETURNS_NOT_RETAINED IOPMinformee * nextInList( IOPMinformee * currentItem );

	LIBKERN_RETURNS_NOT_RETAINED IOPMinformee * findItem( IOService * driverOrChild );

// This lock must be held while modifying list or length
	static IORecursiveLock * getSharedRecursiveLock( void );
};

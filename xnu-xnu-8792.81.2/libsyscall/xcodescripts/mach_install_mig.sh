#!/bin/sh -xe
#
# Copyright (c) 2010 Apple Inc. All rights reserved.
#
# @APPLE_OSREFERENCE_LICENSE_HEADER_START@
# 
# This file contains Original Code and/or Modifications of Original Code
# as defined in and that are subject to the Apple Public Source License
# Version 2.0 (the 'License'). You may not use this file except in
# compliance with the License. The rights granted to you under the License
# may not be used to create, or enable the creation or redistribution of,
# unlawful or unlicensed copies of an Apple operating system, or to
# circumvent, violate, or enable the circumvention or violation of, any
# terms of an Apple operating system software license agreement.
# 
# Please obtain a copy of the License at
# http://www.opensource.apple.com/apsl/ and read it before using this file.
# 
# The Original Code and all software distributed under the License are
# distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
# EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
# INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
# Please see the License for the specific language governing rights and
# limitations under the License.
# 
# @APPLE_OSREFERENCE_LICENSE_HEADER_END@
#

# build inside OBJROOT
cd $OBJROOT

MIG=`xcrun -sdk "$SDKROOT" -find mig`
MIGCC=`xcrun -sdk "$SDKROOT" -find cc`
export MIGCC
[ -n "$DRIVERKITROOT" ] && MIG_DRIVERKIT_DEFINES="-DDRIVERKIT"
MIG_DEFINES="-DLIBSYSCALL_INTERFACE $MIG_DRIVERKIT_DEFINES"
MIG_PRIVATE_DEFINES="-DPRIVATE"
MIG_HEADER_OBJ="$OBJROOT/mig_hdr/include/mach"
MIG_HEADER_DST="$BUILT_PRODUCTS_DIR/mig_hdr/include/mach"
MIG_PRIVATE_HEADER_DST="$BUILT_PRODUCTS_DIR/mig_hdr/local/include/mach"
SERVER_HEADER_DST="$BUILT_PRODUCTS_DIR/mig_hdr/include/servers"
MACH_HEADER_DST="$BUILT_PRODUCTS_DIR/mig_hdr/include/mach"
MACH_PRIVATE_HEADER_DST="$BUILT_PRODUCTS_DIR/mig_hdr/local/include/mach"
MIG_INTERNAL_HEADER_DST="$BUILT_PRODUCTS_DIR/internal_hdr/include/mach"
MIG_INCFLAGS="-I${SRCROOT}/../osfmk"
SRC="$SRCROOT/mach"
FILTER_MIG="$SRCROOT/xcodescripts/filter_mig.awk"

# MACHINE_ARCH *really* needs to be a 32-bit arch to generate vm_map_internal.h correctly, even if there are no 32-bit targets.
# thread_state_t *really* needs to pick up arm64 over intel because it has a larger struct type.
case "$ARCHS" in
*arm64*)
    MACHINE_ARCH=armv7
    ;;
*x86_64*)
    MACHINE_ARCH=i386
    ;;
*)
    MACHINE_ARCH=`echo $ARCHS | cut -d' ' -f 1`
    ;;
esac

ASROOT=""
if [ `whoami` = "root" ]; then
	ASROOT="-o 0"
fi

# These are covered by ../../osfmk/mach/mach.modulemap.
MIGS="clock.defs
	clock_priv.defs
	clock_reply.defs
	exc.defs
	host_priv.defs
	host_security.defs
	mach_eventlink.defs
	mach_host.defs
	mach_port.defs
	mach_voucher.defs
	memory_entry.defs
	processor.defs
	processor_set.defs
	task.defs
	thread_act.defs
	vm_map.defs"

# These are covered by ../../osfmk/mach/mach_private.modulemap.
MIGS_PRIVATE=""

MIGS_DUAL_PUBLIC_PRIVATE=""

MIGS_PRIVATE_PLATFORMS="iphoneos iphonesimulator tvos tvsimulator appletvos appletvsimulator watchos watchsimulator bridgeos bridgesimulator iphoneosnano iphonenanosimulator"

if ( echo $MIGS_PRIVATE_PLATFORMS | grep -wFq "${PLATFORM_NAME}" )
then
	MIGS_PRIVATE="mach_vm.defs"
else
	MIGS="$MIGS mach_vm.defs"
fi

MIGS_INTERNAL="mach_port.defs
	mach_vm.defs
	task.defs
	thread_act.defs
	vm_map.defs"

SERVER_HDRS="key_defs.h
	ls_defs.h
	netname_defs.h
	nm_defs.h"

# These are covered by ../../osfmk/mach/mach.modulemap.
MACH_HDRS="mach.h
	mach_error.h
	mach_init.h
	mach_interface.h
	mach_right.h
	port_obj.h
	sync.h
	vm_task.h
	vm_page_size.h
	thread_state.h"

# These are covered by ../../osfmk/mach/mach_private.modulemap.
MACH_PRIVATE_HDRS="port_descriptions.h
	mach_right_private.h
	mach_sync_ipc.h"

MIG_FILTER_TEMPLATE="add_attributes_to_mig.txt"

# install /usr/include/server headers 
mkdir -p $SERVER_HEADER_DST
for hdr in $SERVER_HDRS; do
	install $ASROOT -c -m 444 $SRC/servers/$hdr $SERVER_HEADER_DST
done

# install /usr/include/mach headers
mkdir -p $MACH_HEADER_DST
for hdr in $MACH_HDRS; do
	install $ASROOT -c -m 444 $SRC/mach/$hdr $MACH_HEADER_DST
done

# install /usr/local/include/mach headers
mkdir -p $MACH_PRIVATE_HEADER_DST
for hdr in $MACH_PRIVATE_HDRS; do
	install $ASROOT -c -m 444 $SRC/mach/$hdr $MACH_PRIVATE_HEADER_DST
done

# special case because we only have one to do here
$MIG -novouchers -arch $MACHINE_ARCH -cc $MIGCC -header "$SERVER_HEADER_DST/netname.h" $MIG_INCFLAGS $SRC/servers/netname.defs

# install /usr/include/mach mig headers

mkdir -p $MIG_HEADER_DST
mkdir -p $MIG_HEADER_OBJ

for mig in $MIGS $MIGS_DUAL_PUBLIC_PRIVATE; do
	MIG_NAME=`basename $mig .defs`
	$MIG -novouchers -arch $MACHINE_ARCH -cc $MIGCC -header "$MIG_HEADER_OBJ/$MIG_NAME.h" $MIG_DEFINES $MIG_INCFLAGS $SRC/$mig
        $FILTER_MIG $SRC/$MIG_FILTER_TEMPLATE $MIG_HEADER_OBJ/$MIG_NAME.h > $MIG_HEADER_OBJ/$MIG_NAME.tmp.h
        mv $MIG_HEADER_OBJ/$MIG_NAME.tmp.h $MIG_HEADER_OBJ/$MIG_NAME.h
	install $ASROOT -c -m 444 $MIG_HEADER_OBJ/$MIG_NAME.h $MIG_HEADER_DST/$MIG_NAME.h
done

mkdir -p $MIG_PRIVATE_HEADER_DST

for mig in $MIGS_PRIVATE $MIGS_DUAL_PUBLIC_PRIVATE; do
	MIG_NAME=`basename $mig .defs`
	$MIG -novouchers -arch $MACHINE_ARCH -cc $MIGCC -header "$MIG_PRIVATE_HEADER_DST/$MIG_NAME.h" $MIG_DEFINES $MIG_PRIVATE_DEFINES $MIG_INCFLAGS $SRC/$mig
	if [ ! -e "$MIG_HEADER_DST/$MIG_NAME.h" ]; then
		echo "#error $MIG_NAME.h unsupported." > "$MIG_HEADER_DST/$MIG_NAME.h"
	fi
done


# special headers used just for building Libsyscall
# Note: not including -DLIBSYSCALL_INTERFACE to mig so we'll get the proper
#  'internal' version of the headers being built
 
mkdir -p $MIG_INTERNAL_HEADER_DST
 
for mig in $MIGS_INTERNAL; do
	MIG_NAME=`basename $mig .defs`
	$MIG -novouchers -arch $MACHINE_ARCH -cc $MIGCC -header "$MIG_INTERNAL_HEADER_DST/${MIG_NAME}_internal.h" $MIG_INCFLAGS $SRC/$mig
done
 

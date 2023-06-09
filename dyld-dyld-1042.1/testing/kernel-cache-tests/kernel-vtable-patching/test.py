#!/usr/bin/python3

import os
import KernelCollection
from FixupHelpers import *

# This tests verifies that the vtable in bar.kext is patched
# But also that this can be done against a subclass in the kernel, not just 

def check(kernel_cache):
    kernel_cache.buildKernelCollection("x86_64", "/kernel-vtable-patching/main.kc", "/kernel-vtable-patching/main.kernel", "/kernel-vtable-patching/extensions", ["com.apple.bar"], [])
    kernel_cache.analyze("/kernel-vtable-patching/main.kc", ["-layout", "-arch", "x86_64"])

    assert len(kernel_cache.dictionary()["dylibs"]) == 2
    assert kernel_cache.dictionary()["dylibs"][0]["name"] == "com.apple.kernel"
    assert kernel_cache.dictionary()["dylibs"][1]["name"] == "com.apple.bar"

    # Get the addresses for the symbols we are looking at.  This will make it easier to work out the fixup slots
    kernel_cache.analyze("/kernel-vtable-patching/main.kc", ["-symbols", "-arch", "x86_64"])
    
    # From foo, we want to know where the vtable is, and the foo() and fooUsed0() slots in that vtable
    # Foo::foo()
    fooFooVMAddr = findGlobalSymbolVMAddr(kernel_cache, 0, "__ZN3Foo3fooEv")
    # Foo::fooUsed0()
    fooFooUsed0VMAddr = findGlobalSymbolVMAddr(kernel_cache, 0, "__ZN3Foo8fooUsed0Ev")
    
    # From bar, find the vtable and its override of foo()
    # Bar::foo()
    barFooVMAddr = findGlobalSymbolVMAddr(kernel_cache, 1, "__ZN3Bar3fooEv")


    # Check the fixups
    kernel_cache.analyze("/kernel-vtable-patching/main.kc", ["-fixups", "-arch", "x86_64"])
    
    # In vtable for Foo, we match the entry for Foo::foo() by looking for its value on the RHS of the fixup
    fooFooFixupAddr = findFixupVMAddr(kernel_cache, "kc(0) + " + fooFooVMAddr + " : pointer64")
    # Then the following fixup should be to Foo::fooUsed0()
    nextFixupAddr = offsetVMAddr(fooFooFixupAddr, 8)
    assert kernel_cache.dictionary()["fixups"][nextFixupAddr] == "kc(0) + " + fooFooUsed0VMAddr + " : pointer64"

    # Now in bar, again match the entry for its Bar::foo() symbol
    barFooFixupAddr = findFixupVMAddr(kernel_cache, "kc(0) + " + barFooVMAddr)
    # And if the patching was correct, then following entry should be to Foo::fooUsed0()
    nextFixupAddr = offsetVMAddr(barFooFixupAddr, 8)
    assert kernel_cache.dictionary()["fixups"][nextFixupAddr] == "kc(0) + " + fooFooUsed0VMAddr


# [~]> xcrun -sdk macosx.internal cc -arch x86_64 -Wl,-static -mkernel -nostdlib -Wl,-e,__start -Wl,-pie main.cpp foo.cpp -Wl,-pagezero_size,0x0 -o main.kernel -Wl,-image_base,0x10000 -Wl,-segaddr,__HIB,0x4000 -Wl,-add_split_seg_info -Wl,-install_name,/usr/lib/swift/split.seg.v2.hack -iwithsysroot /System/Library/Frameworks/Kernel.framework/Headers -Wl,-sectcreate,__LINKINFO,__symbolsets,SymbolSets.plist -Wl,-segprot,__LINKINFO,r--,r--  -DFOO_USED=1
# [~]> xcrun -sdk macosx.internal cc -arch x86_64 -Wl,-kext -mkernel -nostdlib -Wl,-add_split_seg_info -Wl,-no_data_const bar.cpp -o extensions/bar.kext/bar -iwithsysroot /System/Library/Frameworks/Kernel.framework/Headers
# [~]> rm -r extensions/*.kext/*.ld


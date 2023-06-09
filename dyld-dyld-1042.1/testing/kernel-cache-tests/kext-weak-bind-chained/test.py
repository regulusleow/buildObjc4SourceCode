#!/usr/bin/python3

import os
import KernelCollection

# This verifies that weak binds point to the same symbol

# FIXME: This should be re-enabled once we know how to handle classic relocs combined with split seg v2.

def check(kernel_cache):
    kernel_cache.buildKernelCollection("arm64", "/kext-weak-bind-chained/main.kc", "/kext-weak-bind-chained/main.kernel", "/kext-weak-bind-chained/extensions", ["com.apple.foo", "com.apple.bar"], [])
    kernel_cache.analyze("/kext-weak-bind-chained/main.kc", ["-layout", "-arch", "arm64"])

    assert len(kernel_cache.dictionary()["dylibs"]) == 3
    assert kernel_cache.dictionary()["dylibs"][0]["name"] == "com.apple.kernel"
    assert kernel_cache.dictionary()["dylibs"][1]["name"] == "com.apple.bar"
    assert kernel_cache.dictionary()["dylibs"][2]["name"] == "com.apple.foo"

    # Check the fixups
    kernel_cache.analyze("/kext-weak-bind-chained/main.kc", ["-fixups", "-arch", "arm64"])
    assert len(kernel_cache.dictionary()["fixups"]) == 6
    assert kernel_cache.dictionary()["fixups"]["0x1C000"] == "kc(0) + 0x1C19C"
    assert kernel_cache.dictionary()["fixups"]["0x1C0D0"] == "kc(0) + 0x1C19C"
    assert len(kernel_cache.dictionary()["dylibs"]) == 3
    assert kernel_cache.dictionary()["dylibs"][0]["name"] == "com.apple.kernel"
    assert kernel_cache.dictionary()["dylibs"][0]["fixups"] == "none"
    assert kernel_cache.dictionary()["dylibs"][1]["name"] == "com.apple.bar"
    assert kernel_cache.dictionary()["dylibs"][1]["fixups"] == "none"
    assert kernel_cache.dictionary()["dylibs"][2]["name"] == "com.apple.foo"
    assert kernel_cache.dictionary()["dylibs"][2]["fixups"] == "none"


# [~]> xcrun -sdk iphoneos.internal cc -arch arm64 -Wl,-static -mkernel -nostdlib -Wl,-add_split_seg_info -Wl,-rename_section,__TEXT,__text,__TEXT_EXEC,__text -Wl,-e,__start -Wl,-pagezero_size,0x0 -Wl,-pie main.c -o main.kernel
# [~]> xcrun -sdk iphoneos.internal cc -arch arm64 -Wl,-kext -mkernel -nostdlib -Wl,-add_split_seg_info foo.c -o extensions/foo.kext/foo -Wl,-fixup_chains
# [~]> xcrun -sdk iphoneos.internal cc -arch arm64 -Wl,-kext -mkernel -nostdlib -Wl,-add_split_seg_info bar.c -o extensions/bar.kext/bar -Wl,-fixup_chains
# [~]> rm -r extensions/*.kext/*.ld


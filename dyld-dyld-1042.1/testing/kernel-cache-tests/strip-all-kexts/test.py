#!/usr/bin/python3

import os
import KernelCollection

# Check that we stripped all the symbols from kexts but the kernel is still not strippped

def check(kernel_cache):
    kernel_cache.buildKernelCollection("arm64", "/strip-all-kexts/main.kc", "/strip-all-kexts/main.kernel", "/strip-all-kexts/extensions", ["com.apple.foo", "com.apple.bar"], ["-strip-all-kexts"])
    kernel_cache.analyze("/strip-all-kexts/main.kc", ["-layout", "-arch", "arm64"])

    # Check the symbols
    kernel_cache.analyze("/strip-all-kexts/main.kc", ["-symbols", "-arch", "arm64"])
    assert len(kernel_cache.dictionary()["dylibs"]) == 3
    # kernel
    assert kernel_cache.dictionary()["dylibs"][0]["name"] == "com.apple.kernel"
    assert len(kernel_cache.dictionary()["dylibs"][0]["global-symbols"]) == 2
    assert kernel_cache.dictionary()["dylibs"][0]["global-symbols"][0]["name"] == "__mh_execute_header"
    assert kernel_cache.dictionary()["dylibs"][0]["global-symbols"][1]["name"] == "__start"
    assert kernel_cache.dictionary()["dylibs"][0]["local-symbols"] == "none"
    # bar
    assert kernel_cache.dictionary()["dylibs"][1]["name"] == "com.apple.bar"
    assert kernel_cache.dictionary()["dylibs"][1]["global-symbols"] == "none"
    assert kernel_cache.dictionary()["dylibs"][1]["local-symbols"] == "none"
    # foo
    assert kernel_cache.dictionary()["dylibs"][2]["name"] == "com.apple.foo"
    assert kernel_cache.dictionary()["dylibs"][2]["global-symbols"] == "none"
    assert kernel_cache.dictionary()["dylibs"][2]["local-symbols"] == "none"


# [~]> xcrun -sdk iphoneos.internal cc -arch arm64 -Wl,-static -mkernel -nostdlib -Wl,-add_split_seg_info -Wl,-rename_section,__TEXT,__text,__TEXT_EXEC,__text -Wl,-e,__start -Wl,-pagezero_size,0x0 -Wl,-pie main.c -o main.kernel
# [~]> xcrun -sdk iphoneos.internal cc -arch arm64 -Wl,-kext -mkernel -nostdlib -Wl,-add_split_seg_info foo.c -o extensions/foo.kext/foo
# [~]> xcrun -sdk iphoneos.internal cc -arch arm64 -Wl,-kext -mkernel -nostdlib -Wl,-add_split_seg_info bar.c -o extensions/bar.kext/bar
# [~]> rm -r extensions/*.kext/*.ld


#!/usr/bin/python3

import os
import KernelCollection

# This is the fixups-x86_64 test, but with __CTF inserted so that we can see that CTF doesn't impact the result
# Note the ctf.txt file is 16k just to ensure that if its vm addr wasn't updated, we'd exceed the size of __LINKEDIT with the __CTF

def check(kernel_cache):
    kernel_cache.buildKernelCollection("x86_64", "/ctf-x86_64/main.kc", "/ctf-x86_64/main.kernel", "/ctf-x86_64/extensions", ["com.apple.foo", "com.apple.bar"], [])
    kernel_cache.analyze("/ctf-x86_64/main.kc", ["-layout", "-arch", "x86_64"])

    assert len(kernel_cache.dictionary()["cache-segments"]) == 7
    assert kernel_cache.dictionary()["cache-segments"][0]["name"] == "__TEXT"
    assert kernel_cache.dictionary()["cache-segments"][0]["vmAddr"] == "0xFFFFFF8000200000"
    assert kernel_cache.dictionary()["cache-segments"][1]["name"] == "__PRELINK_TEXT"
    assert kernel_cache.dictionary()["cache-segments"][1]["vmAddr"] == "0xFFFFFF8000204000"
    assert kernel_cache.dictionary()["cache-segments"][2]["name"] == "__TEXT_EXEC"
    assert kernel_cache.dictionary()["cache-segments"][2]["vmAddr"] == "0xFFFFFF8000204000"
    assert kernel_cache.dictionary()["cache-segments"][3]["name"] == "__PRELINK_INFO"
    assert kernel_cache.dictionary()["cache-segments"][3]["vmAddr"] == "0xFFFFFF8000208000"
    assert kernel_cache.dictionary()["cache-segments"][4]["name"] == "__DATA"
    assert kernel_cache.dictionary()["cache-segments"][4]["vmAddr"] == "0xFFFFFF800020C000"
    assert kernel_cache.dictionary()["cache-segments"][5]["name"] == "__HIB"
    assert kernel_cache.dictionary()["cache-segments"][5]["vmAddr"] == "0xFFFFFF8000100000"
    assert kernel_cache.dictionary()["cache-segments"][6]["name"] == "__LINKEDIT"
    assert kernel_cache.dictionary()["cache-segments"][6]["vmAddr"] == "0xFFFFFF8000218000"

    assert len(kernel_cache.dictionary()["dylibs"]) == 3
    # main.kernel
    assert kernel_cache.dictionary()["dylibs"][0]["name"] == "com.apple.kernel"
    assert len(kernel_cache.dictionary()["dylibs"][0]["segments"]) == 5
    assert kernel_cache.dictionary()["dylibs"][0]["segments"][0]["name"] == "__TEXT"
    assert kernel_cache.dictionary()["dylibs"][0]["segments"][0]["vmAddr"] == "0xFFFFFF8000204000"
    assert kernel_cache.dictionary()["dylibs"][0]["segments"][1]["name"] == "__DATA"
    assert kernel_cache.dictionary()["dylibs"][0]["segments"][1]["vmAddr"] == "0xFFFFFF800020C000"
    assert kernel_cache.dictionary()["dylibs"][0]["segments"][2]["name"] == "__HIB"
    assert kernel_cache.dictionary()["dylibs"][0]["segments"][2]["vmAddr"] == "0xFFFFFF8000100000"
    assert kernel_cache.dictionary()["dylibs"][0]["segments"][3]["name"] == "__CTF"
    assert kernel_cache.dictionary()["dylibs"][0]["segments"][3]["vmAddr"] == "0xFFFFFF8000204000"
    assert kernel_cache.dictionary()["dylibs"][0]["segments"][3]["vmSize"] == "0x0"
    assert kernel_cache.dictionary()["dylibs"][0]["segments"][3]["sections"][0]["vmAddr"] == "0xFFFFFF8000204000"
    assert kernel_cache.dictionary()["dylibs"][0]["segments"][3]["sections"][0]["vmSize"] == "0x0"
    assert kernel_cache.dictionary()["dylibs"][0]["segments"][4]["name"] == "__LINKEDIT"
    assert kernel_cache.dictionary()["dylibs"][0]["segments"][4]["vmAddr"] == "0xFFFFFF8000218000"
    # bar.kext
    assert kernel_cache.dictionary()["dylibs"][1]["name"] == "com.apple.bar"
    assert len(kernel_cache.dictionary()["dylibs"][1]["segments"]) == 3
    assert kernel_cache.dictionary()["dylibs"][1]["segments"][0]["name"] == "__TEXT"
    assert kernel_cache.dictionary()["dylibs"][1]["segments"][0]["vmAddr"] == "0xFFFFFF8000205000"
    assert kernel_cache.dictionary()["dylibs"][1]["segments"][1]["name"] == "__DATA"
    assert kernel_cache.dictionary()["dylibs"][1]["segments"][1]["vmAddr"] == "0xFFFFFF8000215000"
    assert kernel_cache.dictionary()["dylibs"][1]["segments"][2]["name"] == "__LINKEDIT"
    assert kernel_cache.dictionary()["dylibs"][1]["segments"][2]["vmAddr"] == "0xFFFFFF8000218000"
    # foo.kext
    assert kernel_cache.dictionary()["dylibs"][2]["name"] == "com.apple.foo"
    assert len(kernel_cache.dictionary()["dylibs"][2]["segments"]) == 3
    assert kernel_cache.dictionary()["dylibs"][2]["segments"][0]["name"] == "__TEXT"
    assert kernel_cache.dictionary()["dylibs"][2]["segments"][0]["vmAddr"] == "0xFFFFFF8000206000"
    assert kernel_cache.dictionary()["dylibs"][2]["segments"][1]["name"] == "__DATA"
    assert kernel_cache.dictionary()["dylibs"][2]["segments"][1]["vmAddr"] == "0xFFFFFF80002150D8"
    assert kernel_cache.dictionary()["dylibs"][2]["segments"][2]["name"] == "__LINKEDIT"
    assert kernel_cache.dictionary()["dylibs"][2]["segments"][2]["vmAddr"] == "0xFFFFFF8000218000"

    # Check the fixups
    kernel_cache.analyze("/ctf-x86_64/main.kc", ["-fixups", "-arch", "x86_64"])
    assert len(kernel_cache.dictionary()["fixups"]) == 16
    # main.kernel: S s = { &func, &func, &g, &func, &g };
    # _s is at 0xFFFFFF8000208000 which is offset 0x108000 from __HIB
    assert kernel_cache.dictionary()["fixups"]["0x10C000"] == "kc(0) + 0xFFFFFF8000204FF0 : pointer64"
    assert kernel_cache.dictionary()["fixups"]["0x10C008"] == "kc(0) + 0xFFFFFF8000204FF0 : pointer64"
    assert kernel_cache.dictionary()["fixups"]["0x10C010"] == "kc(0) + 0xFFFFFF800021402C : pointer64"
    assert kernel_cache.dictionary()["fixups"]["0x110000"] == "kc(0) + 0xFFFFFF8000204FF0 : pointer64"
    assert kernel_cache.dictionary()["fixups"]["0x110008"] == "kc(0) + 0xFFFFFF800021402C : pointer64"
    # main.kernel: PackedS ps = { 0, &func, &func, 0, &g, 0, &g };
    # _ps is at 0xFFFFFF8000210000 which is offset 0x110000 from __HIB
    assert kernel_cache.dictionary()["fixups"]["0x114004"] == "kc(0) + 0xFFFFFF8000204FF0 : pointer64"
    assert kernel_cache.dictionary()["fixups"]["0x11400C"] == "kc(0) + 0xFFFFFF8000204FF0 : pointer64"
    assert kernel_cache.dictionary()["fixups"]["0x114018"] == "kc(0) + 0xFFFFFF800021402C : pointer64"
    assert kernel_cache.dictionary()["fixups"]["0x114021"] == "kc(0) + 0xFFFFFF800021402C : pointer64"
    # bar.kext: __typeof(&bar) barPtr = &bar;
    # _barPtr is at 0xFFFFFF8000210030 which is offset 0x1150C8 from __HIB
    assert kernel_cache.dictionary()["fixups"]["0x1150C8"] == "kc(0) + 0xFFFFFF8000205FE0"
    # foo.kext: int* gPtr = &g;
    # _gPtr is at 0xFFFFFF8000210040 which is offset 0x1151A0 from __HIB
    assert kernel_cache.dictionary()["fixups"]["0x1151A0"] == "kc(0) + 0xFFFFFF80002151A8"
    # main.kernel: movl _foo, %esp
    # The _foo reloc is at 0xFFFFFF8000100002 which is offset 0x2 from __HIB
    assert kernel_cache.dictionary()["fixups"]["0x2"] == "kc(0) + 0x100000 : pointer32"
    assert len(kernel_cache.dictionary()["dylibs"]) == 3
    assert kernel_cache.dictionary()["dylibs"][0]["name"] == "com.apple.kernel"
    assert kernel_cache.dictionary()["dylibs"][0]["fixups"] == "none"
    assert kernel_cache.dictionary()["dylibs"][1]["name"] == "com.apple.bar"
    assert kernel_cache.dictionary()["dylibs"][1]["fixups"] == "none"
    assert kernel_cache.dictionary()["dylibs"][2]["name"] == "com.apple.foo"
    assert kernel_cache.dictionary()["dylibs"][2]["fixups"] == "none"

# [~]> xcrun -sdk macosx.internal cc -arch x86_64 -Wl,-static -mkernel -nostdlib -Wl,-kernel -Wl,-e,__start -Wl,-pie main.cpp -Wl,-pagezero_size,0x0 -o main.kernel -Wl,-image_base,0xffffff8000200000 -Wl,-segaddr,__HIB,0xffffff8000100000 -Wl,-add_split_seg_info -Wl,-install_name,/usr/lib/swift/split.seg.v2.hack
# [~]> xcrun -sdk macosx.internal ctf_insert main.kernel -arch x86_64 ctf.txt -o main.kernel
# [~]> xcrun -sdk macosx.internal cc -arch x86_64 -Wl,-kext -mkernel -nostdlib -Wl,-add_split_seg_info foo.c -o extensions/foo.kext/foo
# [~]> xcrun -sdk macosx.internal cc -arch x86_64 -Wl,-kext -mkernel -nostdlib -Wl,-add_split_seg_info bar.c -o extensions/bar.kext/bar


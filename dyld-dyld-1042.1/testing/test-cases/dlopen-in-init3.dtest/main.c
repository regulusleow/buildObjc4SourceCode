

// BUILD:  $CC bar.c -ltest_support -dynamiclib -o $BUILD_DIR/libbar.dylib -install_name $RUN_DIR/libbar.dylib -DRUN_DIR="$RUN_DIR"
// BUILD:  $CC baz.c -ltest_support -dynamiclib -o $BUILD_DIR/libbaz.dylib -install_name $RUN_DIR/libbaz.dylib
// BUILD:  $CC foo.c -ltest_support -dynamiclib -o $BUILD_DIR/libfoo.dylib -install_name $RUN_DIR/libfoo.dylib $BUILD_DIR/libbar.dylib $BUILD_DIR/libbaz.dylib
// BUILD:  $CC main.c -o $BUILD_DIR/dlopen-in-init3.exe -DRUN_DIR="$RUN_DIR"

// RUN:  ./dlopen-in-init3.exe

// This test uses dlopen to jump ahead in the initializer graph
// main doesn't directly link any of the libraries here, but dlopen's libfoo which links libbar and libbaz.
// We should run initializers in the order libbar, libbaz, libfoo.
// However, libbar has a static init with a dlopen of libbaz and so libbaz needs to be initialized by libbar instead of by libfoo

#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>

#include "test_support.h"

int main(int argc, const char* argv[], const char* envp[], const char* apple[]) {
    void* fooHandle = dlopen(RUN_DIR "/libfoo.dylib", 0);
    if ( fooHandle == NULL ) {
        FAIL("dlopen-in-init3, dlopen libfoo.dylib: %s", dlerror());
    }
    void* fooSymbol = dlsym(RTLD_DEFAULT, "foo");
    if ( fooSymbol == NULL ) {
        FAIL("dlsym libfoo.dylib");
    }
    if ( ((int(*)())fooSymbol)() != 0 ) {
        FAIL("fooSymbol() should return 0");
    }
    PASS("Success");
}


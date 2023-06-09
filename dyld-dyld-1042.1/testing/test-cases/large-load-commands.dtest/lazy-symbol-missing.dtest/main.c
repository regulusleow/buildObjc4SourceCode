
// BUILD:  $CC foo.c -dynamiclib -o $BUILD_DIR/libfoo.dylib         -install_name $RUN_DIR/libfoo.dylib
// BUILD:  $CC foo.c -dynamiclib -o $BUILD_DIR/libfoo-present.dylib  -install_name $RUN_DIR/libfoo.dylib -DHAS_SYMBOL=1
// BUILD:  $CC foo.c -dynamiclib -o $BUILD_DIR/libbar-missing.dylib  -install_name $RUN_DIR/libbar-missing.dylib -DHAS_SYMBOL=1
// BUILD:  $CC main.c            -o $BUILD_DIR/lazy-symbol-missing.exe        $BUILD_DIR/libfoo-present.dylib -Os
// BUILD(macos):  $CC main.c     -o $BUILD_DIR/lazy-symbol-missing-flat.exe   -undefined dynamic_lookup  -Wl,-no_fixup_chains     -Os -DFLAT=1
// BUILD:  $CC main-call.c       -o $BUILD_DIR/lazy-symbol-missing-called.exe $BUILD_DIR/libfoo-present.dylib -Os
// BUILD:  $CC main-call.c       -o $BUILD_DIR/lazy-symbol-missing-called-weak-lib.exe $BUILD_DIR/libbar-missing.dylib -Os -DWEAK=1
// BUILD:  $CXX runner.cpp         -o $BUILD_DIR/lazy-symbol-runner.exe -DRUN_DIR="$RUN_DIR"
// BUILD:  $CXX runner.cpp         -o $BUILD_DIR/lazy-symbol-runner-weak-lib.exe -DRUN_DIR="$RUN_DIR" -DWEAK=1

// BUILD: $SKIP_INSTALL $BUILD_DIR/libfoo-present.dylib
// BUILD: $SKIP_INSTALL $BUILD_DIR/libbar-missing.dylib

// NO_CRASH_LOG: lazy-symbol-missing-called.exe
// NO_CRASH_LOG: lazy-symbol-missing-called-weak-lib.exe

// RUN:  ./lazy-symbol-missing.exe
// RUN:  ./lazy-symbol-runner.exe
// RUN:  ./lazy-symbol-missing-flat.exe
// RUN:  ./lazy-symbol-runner-weak-lib.exe


#include <stdio.h>
#include <stdbool.h>
#include <mach-o/getsect.h>

#include "test_support.h"

#if __LP64__
extern struct mach_header_64 __dso_handle;
#else
extern struct mach_header __dso_handle;
#endif

extern int slipperySymbol();

#ifdef FLAT
  #define TESTNAME "lazy-symbol-missing-flat"
#else
  #define TESTNAME "lazy-symbol-missing"
#endif

int main(int argc, const char* argv[], const char* envp[], const char* apple[]) {
#if __arm64e__
    // arm64e always uses chained binds which does not support lazy binding
    bool supportsLazyBinding = false;
#else
    // other architectures may or may not use lazy binding
    unsigned long sectSize = 0;
    bool supportsLazyBinding = (getsectiondata(&__dso_handle, "__DATA", "__la_symbol_ptr", &sectSize) != NULL);
  #if __ARM_ARCH_7K__
    // armv7 has two names for lazy pointers section
    if ( !supportsLazyBinding )
        supportsLazyBinding = (getsectiondata(&__dso_handle, "__DATA", "__lazy_symbol", &sectSize) != NULL);
  #endif
#endif

    if ( supportsLazyBinding ) {
        // add runtime check that results in the function never being called
        if ( argc < 0 )
            slipperySymbol();
    }
    PASS("%s", TESTNAME);
}


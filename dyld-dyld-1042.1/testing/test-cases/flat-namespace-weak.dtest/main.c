// BUILD(macos):  $CC bar.c -dynamiclib -o $BUILD_DIR/libbar.dylib -install_name $RUN_DIR/libbar.dylib
// BUILD(macos):  $CC foo.c -dynamiclib -o $BUILD_DIR/libfoo.dylib -install_name $RUN_DIR/libfoo.dylib -undefined dynamic_lookup -Wl,-no_fixup_chains
// BUILD(macos):  $CC main.c -o $BUILD_DIR/flat-namespace-weak.exe -DRUN_DIR="$RUN_DIR"

// BUILD(ios,tvos,watchos,bridgeos):

// RUN:    ./flat-namespace-weak.exe


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "test_support.h"


// Test that a flat-namespace lookup can be a weak-import and missing

int main(int argc, const char* argv[], const char* envp[], const char* apple[])
{
    // to test in dlopen (instead of at launch) to make error handling easier
    void* handle = dlopen(RUN_DIR "/libfoo.dylib", RTLD_LAZY);
    if ( handle == NULL ) {
        FAIL("dlopen(\"%s\") failed with: %s", RUN_DIR "/libfoo.dylib", dlerror());
        return 0;
    }

    PASS("Success");
    return 0;
}

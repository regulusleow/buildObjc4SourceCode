
#include "../kmod.h"

int startKext() {
	return 0;
}
int endKext() {
	return 0;
}

KMOD_EXPLICIT_DECL(com.apple.foo, "1.0.0", startKext, endKext)

// This is the symbol xnu now exports
extern int symbol_from_xnu() __asm("__ZN15OSMetaClassBase8DispatchE5IORPC");

// And this is the old symbol it needs to implicitly alias to the above symbol
extern int symbol_from_xnu_implicit_alias() __asm("__ZN15OSMetaClassBase25_RESERVEDOSMetaClassBase3Ev");

int foo() {
	return symbol_from_xnu() + symbol_from_xnu_implicit_alias();
}

#include <string.h>
#include <stdio.h>

extern void setState(const char* from);
extern void c(const char* from);

void d(const char* from) {
    char buffer[100];
    snprintf(buffer, 100, "d() from %s", from);
    setState(buffer);
}

void __attribute__((constructor))
initD()
{
    c("initD");
}




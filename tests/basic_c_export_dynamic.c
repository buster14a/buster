// ctypes.pythonapi is dlopen(NULL) + dlsym on the interpreter's own
// definitions, and it works only because configure passes the linker
// `-export-dynamic`: every defined global lands in .dynsym.  The flag was
// ignored, and every pythonapi lookup died with "undefined symbol".  Both
// spellings must work and the default must stay unexported -- the third
// probe in the driver test asserts that separately.
#include <stdio.h>
#include <dlfcn.h>

int basic_c_exported_answer(void)
{
    return 42;
}

int main(void)
{
    void* self = dlopen(0, RTLD_NOW);
    int (*found)(void) = (int (*)(void))dlsym(self, "basic_c_exported_answer");
    if (!found || found() != 42)
    {
        return 1;
    }
    printf("export dynamic ok\n");
    return 0;
}

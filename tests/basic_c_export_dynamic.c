// ctypes.pythonapi is dlopen(NULL) + dlsym on the interpreter's own
// definitions, and it works only because configure passes the linker
// `-export-dynamic`: every defined global lands in .dynsym.  The flag was
// ignored, and every pythonapi lookup died with "undefined symbol".  Both
// spellings must work and the default must stay unexported -- the third
// probe in the driver test asserts that separately.  Linux x86-64 only,
// and dlfcn.h is the one system header these invocations can rely on
// there: they name no sysroot, which is why every other fixture here
// declares what it needs instead of including it.
#include <dlfcn.h>

int basic_c_exported_answer(void)
{
    return 42;
}

int main(void)
{
    void* self = dlopen(0, RTLD_NOW);
    int (*found)(void) = (int (*)(void))dlsym(self, "basic_c_exported_answer");
    return !found || found() != 42;
}

// Loads basic_c_elf_shared_library.c's library at run time: the library's
// thread-local block is then allocated dynamically, and the references it
// makes into this executable resolve only through -rdynamic exports.
#include <dlfcn.h>

int application_value = 7;

int application_callback(int value)
{
    return value + 1;
}

int main(int argc, char** argv)
{
    int failed = argc < 2;
    void* library = failed ? 0 : dlopen(argv[1], RTLD_NOW);
    failed |= (library == 0) << 1;
    if (library)
    {
        int (*add)(int) = (int (*)(int))dlsym(library, "shared_add");
        int (*read_application)(void) = (int (*)(void))dlsym(library, "shared_read_application");
        int (*call_application)(int) = (int (*)(int))dlsym(library, "shared_call_application");
        int* counter = (int*)dlsym(library, "shared_counter");
        failed |= (!add || !read_application || !call_application || !counter) << 2;
        failed |= (!failed && (*counter != 41 || add(1) != 1 + 41 + 2 + 6)) << 3;
        failed |= (!failed && (read_application() != 7 || call_application(3) != 5)) << 4;
        failed |= dlclose(library) << 5;
    }
    return failed;
}

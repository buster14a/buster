// Drive libtcc 0.9.27 (embedded in the pytcc wheel) to compile one C file to an object.
// usage: tccdrv <libtcc.so> <tcc include dir> <out.o> <-g|-g0> <in.c> [-DNAME[=V]] [-Idir]...
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct TCCState TCCState;
int main(int argc, char** argv)
{
    if (!dlopen("libpython3.10.so.1", RTLD_NOW | RTLD_GLOBAL)) { fprintf(stderr, "py: %s\n", dlerror()); return 2; }
    void* h = dlopen(argv[1], RTLD_NOW);
    if (!h) { fprintf(stderr, "tcc: %s\n", dlerror()); return 2; }
    TCCState* (*tcc_new)(void) = dlsym(h, "tcc_new");
    int (*set_output_type)(TCCState*, int) = dlsym(h, "tcc_set_output_type");
    int (*add_sysinclude)(TCCState*, const char*) = dlsym(h, "tcc_add_sysinclude_path");
    int (*add_include)(TCCState*, const char*) = dlsym(h, "tcc_add_include_path");
    void (*define_symbol)(TCCState*, const char*, const char*) = dlsym(h, "tcc_define_symbol");
    int (*add_file)(TCCState*, const char*) = dlsym(h, "tcc_add_file");
    int (*output_file)(TCCState*, const char*) = dlsym(h, "tcc_output_file");
    void (*set_lib_path)(TCCState*, const char*) = dlsym(h, "tcc_set_lib_path");
    void (*set_options)(TCCState*, const char*) = dlsym(h, "tcc_set_options");
    TCCState* s = tcc_new();
    set_lib_path(s, argv[2]);
    if (strcmp(argv[4], "-g") == 0) set_options(s, "-g");
    add_sysinclude(s, argv[2]);
    add_sysinclude(s, "/usr/local/include");
    add_sysinclude(s, "/usr/include/x86_64-linux-gnu");
    add_sysinclude(s, "/usr/include");
    for (int i = 6; i < argc; i += 1)
    {
        if (!strncmp(argv[i], "-D", 2))
        {
            char* name = strdup(argv[i] + 2); char* eq = strchr(name, '=');
            if (eq) { *eq = 0; define_symbol(s, name, eq + 1); } else define_symbol(s, name, "1");
        }
        else if (!strncmp(argv[i], "-I", 2)) add_include(s, argv[i] + 2);
    }
    set_output_type(s, 4); // TCC_OUTPUT_OBJ in 0.9.27
    if (add_file(s, argv[5]) == -1) return 1;
    return output_file(s, argv[3]) == -1 ? 1 : 0;
}

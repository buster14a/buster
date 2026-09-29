/* Test broker's fail-closed static ELF inspection on exact compiled files.
 * Usage: credential-gate-elf-test STATIC_GATE DYNAMIC_GATE */
#define _GNU_SOURCE 1
#define main bq_broker_original_main
#include "systemd_broker.c"
#undef main

int main(int argc, char** argv)
{
    bool ok = argc == 3;
    for (int index = 1; ok && index < argc; index += 1)
    {
        int descriptor = open(argv[index], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        struct stat info = {0};
        ok = descriptor >= 0 && fstat(descriptor, &info) == 0 &&
             bq_broker_static_elf(descriptor, info.st_size) == (index == 1);
        if (descriptor >= 0) close(descriptor);
    }
    int null = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (null >= 0)
    {
        ok = ok && !bq_broker_static_elf(null, 0);
        close(null);
    }
    printf("Bq static ELF probe result=%s\n", ok ? "pass" : "fail");
    return ok ? 0 : 1;
}

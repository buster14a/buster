#define _CRT_SECURE_NO_WARNINGS
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#if defined(__linux__) || defined(__APPLE__)
#include <sys/utsname.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#pragma error
#endif

static _Bool string_compare(const char* s1, const char* s2)
{
    return strcmp(s1, s2) == 0;
}

static void file_write_string(FILE* file, const char* s)
{
    size_t length = strlen(s);
    size_t result = fwrite(s, 1, length, file);
    if (result != length)
    {
        printf("fwrite failed\n");
        exit(1);
    }
}

int main(int argc, const char* argv[])
{
    if (argc != 2)
    {
        printf("Wrong argument count");
        return 1;
    }

    const char* operating_system;
#if defined (__linux__)
    operating_system = "linux";
#elif defined (__APPLE__)
    operating_system = "macos"; // TODO: detect ios and other Apple operating systems
#elif defined (_WIN32)
    operating_system = "windows";
#else
#pragma error
#endif
    size_t operating_system_length = strlen(operating_system);
    char architecture_buffer[4096];

#if defined(__linux__) || defined(__APPLE__)
    struct utsname uname_struct;
    if (uname(&uname_struct) != 0)
    {
        printf("uname syscall failed\n");
        return 1;
    }
    memcpy(architecture_buffer, uname_struct.machine, sizeof(uname_struct.machine));
#else
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    WORD processor_architecture = system_info.wProcessorArchitecture;
    switch (processor_architecture)
    {
        break; case PROCESSOR_ARCHITECTURE_AMD64: memcpy(architecture_buffer, "x86_64", sizeof("x86_64"));
        break; case PROCESSOR_ARCHITECTURE_ARM: memcpy(architecture_buffer, "arm", sizeof("arm"));
        break; case PROCESSOR_ARCHITECTURE_ARM64: memcpy(architecture_buffer, "aarch64", sizeof("aarch64"));
        break; default: printf("Invalid cpu architecture: %d\n", processor_architecture); return 1;
    }
#endif

    const char* cpu_architecture = architecture_buffer;
    if (string_compare(cpu_architecture, "amd64"))
    {
        cpu_architecture = "x86_64";
    }
    else if (string_compare(cpu_architecture, "arm64"))
    {
        cpu_architecture = "aarch64";
    }

    if (!(string_compare(cpu_architecture, "x86_64") || string_compare(cpu_architecture, "aarch64")))
    {
        printf("invalid cpu architecture: %s\n", cpu_architecture);
        return 1;
    }

    const char* file_path = argv[1];
    FILE* file = fopen(file_path, "w");
    if (!file)
    {
        printf("fopen failed\n");
        return 1;
    }

#if defined(_WIN32)
    file_write_string(file, "@echo off\n");
    file_write_string(file, "set \"BUSTER_ARCH=");
    file_write_string(file, cpu_architecture);
    file_write_string(file, "\"\nset \"BUSTER_OS=windows\"\n");
#else
    file_write_string(file, "#!/usr/bin/env bash\n");
    file_write_string(file, "set -eu\n");
    file_write_string(file, "BUSTER_ARCH=");
    file_write_string(file, cpu_architecture);
    file_write_string(file, "\nBUSTER_OS=");
    file_write_string(file, operating_system);
    file_write_string(file, "\n");
#endif

    if (fclose(file) != 0)
    {
        printf("fclose failed\n");
        return 1;
    }

    return 0;
}

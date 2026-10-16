#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <assert.h>

#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#define BUSTER_COMPILE_TIME_STRING_LENGTH(strlit) (BUSTER_ARRAY_LENGTH(strlit) - 1)
#define BUSTER_S8_INIT(strlit) {.pointer = (char*)(strlit), .length = BUSTER_COMPILE_TIME_STRING_LENGTH(strlit)}
#define BUSTER_S8(strlit) ((String8)BUSTER_S8_INIT(strlit))

// OS-dependent includes
#include <time.h>
#include <unistd.h>

// CPU architecture dependent includes
#include <immintrin.h>

typedef struct String8 String8;
struct String8
{
    char* pointer;
    uint64_t length;
};

static void string_printf(String8 string)
{
    assert(string.length <= INT_MAX);
    printf("%.*s", (int)string.length, string.pointer);
}

typedef struct CompileOptions CompileOptions;
struct CompileOptions
{
    uint32_t foo;
};

typedef struct CompileResult CompileResult;
struct CompileResult
{
    bool success;
};

static CompileResult compile(String8 path, CompileOptions options)
{
    CompileResult result = {0};
    printf("Compiling ");
    string_printf(path);
    printf("...\n");
    result.success = true;
    return result;
}

int main(void)
{
    printf("Hello world\n");
    compile(BUSTER_S8(__FILE__), (CompileOptions){0});
    return 0;
}

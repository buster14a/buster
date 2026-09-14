#include <stdio.h>
#if defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__))
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// LLVM's hosted Windows Arm64 package does not ship the compiler-rt builtins
// library that normally owns this helper. The differential checks argument
// evaluation and register liveness without modifying executable code, but its
// nonempty ranges still have to exercise Windows' real cache-maintenance API.
void __clear_cache(void *begin, void *end)
{
    SIZE_T size = (SIZE_T)((char *)end - (char *)begin);
    if (!FlushInstructionCache(GetCurrentProcess(), begin, size))
    {
        ExitProcess(1);
    }
}
#endif
void clear_range(char *, char *);
int clear_arguments(char *, int, int *, int *);
unsigned long long clear_live(char *, unsigned long long, unsigned long long, unsigned long long,
    unsigned long long, unsigned long long, unsigned long long, unsigned long long);
int main(void)
{
    char bytes[256] = {0};
    int result = 0;
    for (int start = 0; start < 128; start += 1)
    {
        int first = start;
        int second = 5;
        clear_range(bytes + start, bytes + start);
        clear_range(bytes + start, bytes + start + 2);
        int value = clear_arguments(bytes + start, 65, &first, &second);
        unsigned long long live = clear_live(bytes, 1, 2, 3, 4, 5, 6, 7);
        printf("%d %d %d %llu\n", first, second, value, live);
        result |= first != start + 1 || second != 6 || value != start + 43 || live != 406;
    }
    return result;
}

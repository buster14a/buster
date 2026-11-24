#include <stdio.h>
#include <stdint.h>
__attribute__((section("aaa"), aligned(16), used)) int a0 = 1;
__attribute__((section("aaa"), aligned(4), used)) int a1 = 2;
__attribute__((section("bbb"), aligned(4), used)) int b0 = 3;
__attribute__((section("bbb"), aligned(16), used)) int b1 = 4;
extern char __start_aaa[], __stop_aaa[], __start_bbb[], __stop_bbb[];
int main(void)
{
    printf("aaa start%%16=%lu span=%ld; bbb start%%16=%lu span=%ld\n",
           (unsigned long)((uintptr_t)__start_aaa % 16),
           (long)(__stop_aaa - __start_aaa),
           (unsigned long)((uintptr_t)__start_bbb % 16),
           (long)(__stop_bbb - __start_bbb));
    return 0;
}

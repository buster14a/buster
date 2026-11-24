#include <stdio.h>
struct entry { long value; };
static __attribute__((section("st_sec"), aligned(16))) struct entry s0 = { 7 };
static __attribute__((section("st_sec"), aligned(16))) struct entry s1 = { 8 };
__attribute__((section("st_sec"), aligned(16))) struct entry g0 = { 9 };
extern struct entry __start_st_sec[], __stop_st_sec[];
int main(void)
{
    long total = 0;
    for (struct entry* e = __start_st_sec; e < __stop_st_sec; e += 1) total += e->value;
    printf("%ld %ld\n", total, (long)(__stop_st_sec - __start_st_sec));
    return 0;
}

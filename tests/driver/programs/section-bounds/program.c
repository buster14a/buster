#include <stdio.h>
struct entry { long value; const char* name; };
__attribute__((section("bst_sec"), aligned(16))) struct entry e0 = { 11, "a" };
__attribute__((aligned(16))) long between = 99;
__attribute__((section("bst_sec"), aligned(16))) struct entry e1 = { 22, "b" };
__attribute__((section("bst_sec"), aligned(16))) struct entry e2 = { 33, "c" };
__attribute__((section("bst_one"))) int one = 5;
extern struct entry __start_bst_sec[], __stop_bst_sec[];
extern int __start_bst_one[], __stop_bst_one[];
extern int __start_bst_missing[] __attribute__((weak));
int main(void)
{
    long total = 0;
    for (struct entry* e = __start_bst_sec; e < __stop_bst_sec; e += 1) total += e->value;
    printf("%ld %ld %d %d %ld %d\n", total, (long)(__stop_bst_sec - __start_bst_sec), one, (int)(__stop_bst_one - __start_bst_one),
           between, __start_bst_missing == 0);
    return 0;
}

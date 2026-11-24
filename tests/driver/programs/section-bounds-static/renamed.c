#include <stdio.h>
__attribute__((section("mreg"))) static int a = 1;
__attribute__((section("mreg"))) static int b = 2;
extern int begin[] __asm__("__start_mreg");
extern int end[] __asm__("__stop_mreg");
int main(void)
{
    int total = 0;
    for (int* p = begin; p < end; p++) total += *p;
    printf("%d %d %td\n", a, total, end - begin);
    return 0;
}

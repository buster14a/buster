#include <stdio.h>
__attribute__((section("mysec"))) int in_sec = 5;
extern int __start_mysec[], __stop_mysec[];
int main(void) { printf("%d %d\n", in_sec, (int)(__stop_mysec - __start_mysec)); return 0; }

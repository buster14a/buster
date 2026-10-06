/* gen_a64 K M: one function with K forward conditional branches that all jump
   over M volatile stores to a common label.  K drives relaxation count,
   M drives code size / machine-row count independently. */
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    long k = atol(argv[1]), m = atol(argv[2]);
    printf("int f(volatile int* p, int x)\n{\n");
    for (long i = 0; i < k; i += 1) printf("    if (x == %ld) goto out;\n", i);
    for (long i = 0; i < m; i += 1) printf("    p[%ld] = x;\n", i & 1023);
    printf("out:\n    return x;\n}\n");
    return 0;
}

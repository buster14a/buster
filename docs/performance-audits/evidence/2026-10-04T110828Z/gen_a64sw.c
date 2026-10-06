/* gen_a64sw K M FAR: switch with K small sparse cases plus one case holding
   M volatile stores.  FAR=1 puts the big case first (every small case's
   compare-chain b.eq must jump over it -> K branch relaxations); FAR=0 puts
   it last (control: same code, no relaxation of the small cases). */
#include <stdio.h>
#include <stdlib.h>
static void big(long m) { printf("    case 1: {\n"); for (long i = 0; i < m; i += 1) printf("        p[%ld] = x;\n", i & 1023); printf("        break; }\n"); }
int main(int argc, char** argv)
{
    if (argc != 4) return 2;
    long k = atol(argv[1]), m = atol(argv[2]), far = atol(argv[3]);
    printf("int f(volatile int* p, int x)\n{\n    switch (x)\n    {\n");
    if (far) big(m);
    for (long i = 0; i < k; i += 1) printf("    case %ld: p[%ld] = %ld; break;\n", 1000003L + i * 7919L, i & 1023, i);
    if (!far) big(m);
    printf("    default: break;\n    }\n    return x;\n}\n");
    return 0;
}

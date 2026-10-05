/* gen_prep P C: one function with P parenthesized-assignment conditions
   (each becomes a prepared control expression) and C direct calls. */
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    long p = atol(argv[1]), c = atol(argv[2]);
    printf("int g(int);\nint f(int* v)\n{\n    int s = 0;\n    int t;\n");
    for (long i = 0; i < p; i += 1) printf("    if ((t = v[%ld]) != 0) s += t;\n", i & 1023);
    for (long i = 0; i < c; i += 1) printf("    s += g(%ld);\n", i);
    printf("    return s;\n}\n");
    return 0;
}

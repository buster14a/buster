// A temporary probe's input generator (not part of the compiler): writes a C translation unit that stresses one
// construct, sized by N, on standard output, for timing the compiler's stages at N, 2N, 4N and 8N on GitHub's hosted
// runners. `gen MODE N [DIRECTORY]`; the includes mode writes its headers into DIRECTORY.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned permute(unsigned i, unsigned n)
{
    // A permutation of 0..n-1 when n isn't a multiple of 7919.
    return (unsigned)(((unsigned long long)i * 7919ull + 13ull) % n);
}

static void functions(unsigned n, const char* prefix)
{
    for (unsigned i = 0; i < n; i++)
        printf("int %s_%u(int x)\n{\n    int y = x * %u + %u;\n    if (y > 100)\n        y -= 7;\n    return y ^ %u;\n}\n", prefix, i, i % 13 + 1, i, i);
    printf("int main(void)\n{\n    int s = 0;\n");
    for (unsigned i = 0; i < n; i += n / 16 + 1)
        printf("    s += %s_%u(s);\n", prefix, i);
    printf("    return s & 1;\n}\n");
}

static void statements(unsigned n)
{
    printf("int f(int x)\n{\n    int a = x;\n");
    for (unsigned i = 0; i < n; i++)
    {
        switch (i % 4)
        {
        case 0: printf("    a = a * 3 + %u;\n", i); break;
        case 1: printf("    a ^= a >> 3;\n"); break;
        case 2: printf("    a += %u;\n", i); break;
        default: printf("    if (a > %u)\n        a -= %u;\n", i, i / 2 + 1); break;
        }
    }
    printf("    return a;\n}\n");
}

static void locals(unsigned n)
{
    printf("int f(int x)\n{\n    int v_0 = x;\n");
    for (unsigned i = 1; i < n; i++)
        printf("    int v_%u = v_%u * 3 + %u;\n", i, i - 1, i);
    printf("    return v_%u;\n}\n", n - 1);
}

static void objects(unsigned n)
{
    printf("int f(int x)\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    int a_%u[4];\n    a_%u[x & 3] = x + %u;\n    x += a_%u[(x + 1) & 3];\n", i, i, i, i);
    printf("    return x;\n}\n");
}

static void elseif(unsigned n)
{
    printf("int f(int x)\n{\n    if (x == 0)\n        return 1;\n");
    for (unsigned i = 1; i < n; i++)
        printf("    else if (x == %u)\n        return %u;\n", i, i * 3 + 1);
    printf("    else\n        return -1;\n}\n");
}

static void cases(unsigned n, unsigned stride)
{
    printf("int f(int x)\n{\n    int y = 0;\n    switch (x)\n    {\n");
    for (unsigned i = 0; i < n; i++)
        printf("    case %u:\n        y = %u;\n        break;\n", i * stride, i * 7 + 1);
    printf("    default:\n        y = -1;\n        break;\n    }\n    return y;\n}\n");
}

static void labels(unsigned n)
{
    printf("int f(int x)\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("L_%u:\n    x = x * 3 + %u;\n    if (x < 0)\n        goto L_%u;\n", i, i, i / 2);
    printf("    return x;\n}\n");
}

static void forward_gotos(unsigned n)
{
    printf("int f(int x)\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    if (x == %u)\n        goto L_%u;\n", i, i);
    printf("    return 0;\n");
    for (unsigned i = 0; i < n; i++)
        printf("L_%u:\n    return %u;\n", i, i * 7 + 1);
    printf("}\n");
}

static void members(unsigned n)
{
    printf("struct S\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    int m_%u;\n", i);
    printf("};\nvoid set(struct S* s, int x)\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    s->m_%u = x + %u;\n", permute(i, n), i);
    printf("}\n");
}

static void typed_members(unsigned n)
{
    // n record types, each with a pointer to it, and four member accesses per type: the table grows with n, and so does
    // the number of accesses.
    for (unsigned i = 0; i < n; i++)
        printf("struct T_%u\n{\n    int a;\n    int b;\n    long c;\n};\n", i);
    for (unsigned i = 0; i < n; i++)
        printf("long get_%u(struct T_%u* t)\n{\n    return t->a + t->b + t->c + t->a;\n}\n", i, i);
}

static void label_values(unsigned n)
{
    printf("int f(int x)\n{\n    static void* const table[] =\n    {\n");
    for (unsigned i = 0; i < n; i++)
        printf("        &&L_%u,\n", i);
    printf("    };\n    goto *table[x %% %u];\n", n);
    for (unsigned i = 0; i < n; i++)
        printf("L_%u:\n    x = x * 3 + %u;\n", i, i);
    printf("    return x;\n}\n");
}

static void static_locals(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("int counter_%u(void)\n{\n    static int count = %u;\n    return count++;\n}\n", i, i);
    printf("int main(void)\n{\n    return counter_%u() & 1;\n}\n", n / 2);
}

static void bitfields(unsigned n)
{
    printf("struct B\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    unsigned b_%u : %u;\n", i, i % 7 + 1);
    printf("};\nvoid set(struct B* s, unsigned x)\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    s->b_%u = x + %u;\n", permute(i, n), i);
    printf("}\n");
}

static void enumerators(unsigned n)
{
    printf("enum E\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    E_%u,\n", i);
    printf("};\nstatic const int t[] =\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    E_%u,\n", permute(i, n));
    printf("};\nint get(int i)\n{\n    return t[i];\n}\n");
}

static void integers(unsigned n)
{
    printf("static const int t[] =\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("%s%u,%s", i % 12 == 0 ? "    " : " ", i * 2654435761u >> 8, i % 12 == 11 ? "\n" : "");
    printf("\n};\nint get(int i)\n{\n    return t[i];\n}\n");
}

static void bytes(unsigned n)
{
    printf("static const unsigned char d[] =\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("%s0x%02x,%s", i % 12 == 0 ? "    " : " ", (i * 2654435761u >> 13) & 0xff, i % 12 == 11 ? "\n" : "");
    printf("\n};\nint get(int i)\n{\n    return d[i];\n}\n");
}

static void floats(unsigned n)
{
    printf("static const double t[] =\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    %u.%03ue-%u,\n", i, i % 1000, i % 30);
    printf("};\ndouble get(int i)\n{\n    return t[i];\n}\n");
}

static void designated(unsigned n)
{
    printf("static int t[%u] =\n{\n", n);
    for (unsigned i = 0; i < n; i++)
        printf("    [%u] = %u,\n", n - 1 - i, i + 1);
    printf("};\nint get(int i)\n{\n    return t[i];\n}\n");
}

static void records(unsigned n)
{
    printf("struct P\n{\n    int a;\n    const char* s;\n    double d;\n    short h[2];\n};\nstatic const struct P t[] =\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    { %u, \"s%u\", %u.5, { %u, %u } },\n", i, i, i, i & 0x7fff, (i * 3) & 0x7fff);
    printf("};\nint get(int i)\n{\n    return t[i].a + t[i].h[1];\n}\n");
}

static void strings(unsigned n, int distinct)
{
    printf("static const char* const t[] =\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    \"string number %u of the table\",\n", distinct ? i : 7u);
    printf("};\nconst char* get(int i)\n{\n    return t[i];\n}\n");
}

static void concatenation(unsigned n)
{
    printf("static const char text[] =\n");
    for (unsigned i = 0; i < n; i++)
        printf("    \"piece %u of a long literal\\n\"\n", i);
    printf("    ;\nint get(int i)\n{\n    return text[i];\n}\n");
}

static void macros(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("#define M_%u (%u * 3 + 1)\n", i, i);
    printf("static const int t[] =\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    M_%u,\n", permute(i, n));
    printf("};\nint get(int i)\n{\n    return t[i];\n}\n");
}

static void macro_chain(unsigned n)
{
    printf("#define A_0 1\n");
    for (unsigned i = 1; i < n; i++)
        printf("#define A_%u (A_%u + 1)\n", i, i - 1);
    printf("int get(void)\n{\n    return A_%u;\n}\n", n - 1);
}

static void macro_calls(unsigned n)
{
    printf("#define F_0(x) (x)\n");
    for (unsigned i = 1; i < n; i++)
        printf("#define F_%u(x) F_%u((x) + %u)\n", i, i - 1, i);
    printf("int get(int y)\n{\n    return F_%u(y);\n}\n", n - 1);
}

static void sums(unsigned n)
{
    printf("int f(int a, int b)\n{\n    return (a * 1)");
    for (unsigned i = 1; i < n; i++)
        printf("%s(%c * %u)", i % 8 == 0 ? "\n        + " : " + ", i % 2 ? 'b' : 'a', i + 1);
    printf(";\n}\n");
}

static void ternaries(unsigned n)
{
    printf("int f(int x)\n{\n    return");
    for (unsigned i = 0; i < n; i++)
        printf("\n        x == %u ? %u :", i, i * 3 + 1);
    printf(" -1;\n}\n");
}

static void commas(unsigned n)
{
    printf("int f(int a)\n{\n    return (a += 1");
    for (unsigned i = 1; i < n; i++)
        printf(",\n        a %s= %u", i % 3 == 0 ? "^" : i % 3 == 1 ? "*" : "+", i % 97 + 1);
    printf(");\n}\n");
}

static void calls(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("extern int ext_%u(int);\n", i);
    printf("int f(int x)\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    x = ext_%u(x);\n", i);
    printf("    return x;\n}\n");
}

static void globals(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("int g_%u = %u;\n", i, i);
    printf("int sum(void)\n{\n    int s = 0;\n");
    for (unsigned i = 0; i < n; i++)
        printf("    s += g_%u;\n", permute(i, n));
    printf("    return s;\n}\n");
}

static void pointers(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("int g_%u = %u;\n", i, i);
    printf("int* const p[] =\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    &g_%u,\n", permute(i, n));
    printf("};\nint get(int i)\n{\n    return *p[i];\n}\n");
}

static void typedefs(unsigned n)
{
    printf("typedef int T_0;\n");
    for (unsigned i = 1; i < n; i++)
        printf("typedef T_%u T_%u;\n", i - 1, i);
    printf("T_%u get(T_%u x)\n{\n    return x + 1;\n}\n", n - 1, n / 2);
}

static void structs(unsigned n)
{
    printf("struct S_0\n{\n    int a;\n};\n");
    for (unsigned i = 1; i < n; i++)
        printf("struct S_%u\n{\n    int a;\n    struct S_%u* p;\n    struct S_%u* q;\n};\n", i, i - 1, i / 2);
    printf("int get(struct S_%u* s)\n{\n    return s->a + s->p->a;\n}\n", n - 1);
}

static void prototypes(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("int proto_%u(int a, long b, const char* c, double d);\n", i);
    printf("int main(void)\n{\n    return 0;\n}\n");
}

static void inlines(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("static inline int h_%u(int x)\n{\n    return x * %u + 1;\n}\n", i, i % 13 + 1);
    printf("int main(void)\n{\n    return h_%u(1) & 1;\n}\n", n / 2);
}

static void loops(unsigned n)
{
    printf("int f(int x)\n{\n    int s = 0;\n");
    for (unsigned i = 0; i < n; i++)
        printf("    for (int i_%u = 0; i_%u < x; i_%u++)\n        s += i_%u ^ %u;\n", i, i, i, i, i);
    printf("    return s;\n}\n");
}

static void includes(unsigned n, const char* directory)
{
    char path[4096];
    for (unsigned i = 0; i < n; i++)
    {
        snprintf(path, sizeof(path), "%s/h_%u.h", directory, i);
        FILE* file = fopen(path, "w");
        if (file == 0)
        {
            perror(path);
            exit(1);
        }
        fprintf(file, "#ifndef H_%u\n#define H_%u\nint h_%u(int);\n#define HM_%u %u\n#endif\n", i, i, i, i, i);
        fclose(file);
    }
    for (unsigned round = 0; round < 2; round++)
        for (unsigned i = 0; i < n; i++)
            printf("#include \"h_%u.h\"\n", i);
    printf("int main(void)\n{\n    return HM_%u & 1;\n}\n", n / 2);
}

static void identifiers(unsigned n)
{
    char prefix[300];
    memset(prefix, 'q', 240);
    prefix[240] = 0;
    functions(n, prefix);
}

int main(int count, char** arguments)
{
    if (count < 3)
    {
        fprintf(stderr, "usage: gen MODE N [DIRECTORY]\n");
        return 1;
    }
    const char* mode = arguments[1];
    const unsigned n = (unsigned)strtoul(arguments[2], 0, 10);
    int status = 0;
    if (strcmp(mode, "functions") == 0) functions(n, "f");
    else if (strcmp(mode, "statements") == 0) statements(n);
    else if (strcmp(mode, "locals") == 0) locals(n);
    else if (strcmp(mode, "objects") == 0) objects(n);
    else if (strcmp(mode, "elseif") == 0) elseif(n);
    else if (strcmp(mode, "switch") == 0) cases(n, 1);
    else if (strcmp(mode, "sparse-switch") == 0) cases(n, 977);
    else if (strcmp(mode, "labels") == 0) labels(n);
    else if (strcmp(mode, "forward-gotos") == 0) forward_gotos(n);
    else if (strcmp(mode, "members") == 0) members(n);
    else if (strcmp(mode, "bitfields") == 0) bitfields(n);
    else if (strcmp(mode, "typed-members") == 0) typed_members(n);
    else if (strcmp(mode, "label-values") == 0) label_values(n);
    else if (strcmp(mode, "static-locals") == 0) static_locals(n);
    else if (strcmp(mode, "enum") == 0) enumerators(n);
    else if (strcmp(mode, "integers") == 0) integers(n);
    else if (strcmp(mode, "bytes") == 0) bytes(n);
    else if (strcmp(mode, "floats") == 0) floats(n);
    else if (strcmp(mode, "designated") == 0) designated(n);
    else if (strcmp(mode, "records") == 0) records(n);
    else if (strcmp(mode, "strings") == 0) strings(n, 1);
    else if (strcmp(mode, "same-strings") == 0) strings(n, 0);
    else if (strcmp(mode, "concatenation") == 0) concatenation(n);
    else if (strcmp(mode, "macros") == 0) macros(n);
    else if (strcmp(mode, "macro-chain") == 0) macro_chain(n);
    else if (strcmp(mode, "macro-calls") == 0) macro_calls(n);
    else if (strcmp(mode, "sums") == 0) sums(n);
    else if (strcmp(mode, "ternaries") == 0) ternaries(n);
    else if (strcmp(mode, "commas") == 0) commas(n);
    else if (strcmp(mode, "calls") == 0) calls(n);
    else if (strcmp(mode, "globals") == 0) globals(n);
    else if (strcmp(mode, "pointers") == 0) pointers(n);
    else if (strcmp(mode, "typedefs") == 0) typedefs(n);
    else if (strcmp(mode, "structs") == 0) structs(n);
    else if (strcmp(mode, "prototypes") == 0) prototypes(n);
    else if (strcmp(mode, "inlines") == 0) inlines(n);
    else if (strcmp(mode, "loops") == 0) loops(n);
    else if (strcmp(mode, "includes") == 0 && count > 3) includes(n, arguments[3]);
    else if (strcmp(mode, "identifiers") == 0) identifiers(n);
    else
    {
        fprintf(stderr, "gen: unknown mode %s\n", mode);
        status = 1;
    }
    return status;
}

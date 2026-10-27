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

static void string_calls(unsigned n)
{
    printf("int puts(const char*);\nvoid f(void)\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    puts(\"string number %u\");\n", i);
    printf("}\n");
}

static void string_declarations(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("const char* p_%u = \"string number %u\";\n", i, i);
}

static void char_arrays(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("static const char s_%u[] = \"string number %u\";\n", i, i);
    printf("const char* get(int i)\n{\n    return i ? s_0 : s_%u;\n}\n", n - 1);
}

static void wide_strings(unsigned n)
{
    printf("typedef int wchar_t;\nstatic const wchar_t* const t[] =\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    L\"wide %u\",\n", i);
    printf("};\nconst wchar_t* get(int i)\n{\n    return t[i];\n}\n");
}

static void nested_arrays(unsigned n)
{
    printf("static const int t[][4] =\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    { %u, %u, %u, %u },\n", i, i + 1, i + 2, i + 3);
    printf("};\nint get(int i)\n{\n    return t[i][i & 3];\n}\n");
}

static void nested_designators(unsigned n)
{
    printf("struct In\n{\n    int a;\n    int b[4];\n};\nstruct Out\n{\n    struct In in;\n    long c;\n};\nstatic struct Out t[%u] =\n{\n", n);
    for (unsigned i = 0; i < n; i++)
        printf("    [%u].in.b[%u] = %u,\n    [%u].c = %u,\n", n - 1 - i, i & 3, i, n - 1 - i, i);
    printf("};\nlong get(int i)\n{\n    return t[i].c + t[i].in.b[1];\n}\n");
}

static void compound_literals(unsigned n)
{
    printf("struct P\n{\n    int x;\n    int y;\n};\nint f(int v)\n{\n    int s = 0;\n");
    for (unsigned i = 0; i < n; i++)
        printf("    s += ((struct P){ v, %u }).y;\n", i);
    printf("    return s;\n}\n");
}

static void static_asserts(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("_Static_assert(%u + 1 > %u, \"assert %u\");\n", i, i, i);
    printf("int main(void)\n{\n    return 0;\n}\n");
}

static void generics(unsigned n)
{
    printf("int f(int x, double y)\n{\n    int s = 0;\n");
    for (unsigned i = 0; i < n; i++)
        printf("    s += _Generic(%s, int: %u, double: %u, default: 0);\n", i % 2 ? "x" : "y", i, i + 1);
    printf("    return s;\n}\n");
}

static void parameters(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
    {
        printf("int g_%u(", i);
        for (unsigned p = 0; p < 24; p++)
            printf("%sint a%u", p ? ", " : "", p);
        printf(")\n{\n    return a0 + a23 + %u;\n}\n", i);
    }
    printf("int main(void)\n{\n    int s = 0;\n");
    for (unsigned i = 0; i < n; i++)
    {
        printf("    s += g_%u(", i);
        for (unsigned p = 0; p < 24; p++)
            printf("%s%u", p ? ", " : "", p + i);
        printf(");\n");
    }
    printf("    return s & 1;\n}\n");
}

static void variadic_calls(unsigned n)
{
    printf("int printf(const char*, ...);\nvoid f(int a, double b, const char* c)\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    printf(\"%%d %%f %%s %u\\n\", a + %u, b, c);\n", i, i);
    printf("}\n");
}

static void pragma_packs(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("#pragma pack(push, %u)\nstruct K_%u\n{\n    char a;\n    int b;\n};\n#pragma pack(pop)\n", 1u << (i % 4), i);
    printf("int get(struct K_%u* k)\n{\n    return k->b;\n}\n", n - 1);
}

static void conditionals(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("#if defined(X_%u) || %u > %u\nint c_%u = %u;\n#elif %u\nint c_%u = 0;\n#else\nint c_%u = 1;\n#endif\n", i, i % 7, i % 5, i, i, i % 3, i, i);
}

static void line_directives(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("#line %u \"file_%u.c\"\nint l_%u = %u;\n", i + 1, i % 64, i, i);
}

static void function_table(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("static int h_%u(int x)\n{\n    return x + %u;\n}\n", i, i);
    printf("int (*const table[])(int) =\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    h_%u,\n", i);
    printf("};\n");
}

static void shadowing(unsigned n)
{
    printf("int f(int x)\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    {\n        int x_%u = x + %u;\n        int v = x_%u * 3;\n        x ^= v;\n    }\n", i % 64, i, i % 64);
    printf("    return x;\n}\n");
}

static void casts(unsigned n)
{
    printf("long f(long x)\n{\n    long s = 0;\n");
    for (unsigned i = 0; i < n; i++)
        printf("    s += (long)(short)(unsigned char)(int)(x + %u);\n", i);
    printf("    return s;\n}\n");
}

static void assembly(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
    {
        printf("    .text\n    .globl f_%u\n    .type f_%u, @function\nf_%u:\n    pushq %%rbp\n    movq %%rsp, %%rbp\n", i, i, i);
        printf("    movl %%edi, %%eax\n    imull $3, %%eax, %%eax\n    addl $%u, %%eax\n    cmpl $100, %%eax\n    jle .L_%u\n", i, i);
        printf("    subl $7, %%eax\n.L_%u:\n    call f_%u\n    popq %%rbp\n    ret\n    .data\nd_%u:\n    .quad f_%u\n", i, i / 2, i, i);
    }
}

static void x_macros(unsigned n, const char* directory)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/list.h", directory);
    FILE* file = fopen(path, "w");
    if (file == 0)
    {
        perror(path);
        exit(1);
    }
    for (unsigned i = 0; i < 1000; i++)
        fprintf(file, "X(item_%u, %u)\n", i, i * 3);
    fclose(file);
    for (unsigned round = 0; round < n; round++)
        printf("#define X(name, value) int round_%u_##name = value;\n#include \"list.h\"\n#undef X\n", round);
}

static void warnings(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("#warning \"warning number %u\"\n", i);
    printf("int main(void)\n{\n    return 0;\n}\n");
}

static void redefinitions(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("#define R %u\n", i);
    printf("int get(void)\n{\n    return R;\n}\n");
}

static void local_initializer(unsigned n)
{
    printf("int f(int i)\n{\n    int a[%u] =\n    {\n", n);
    for (unsigned k = 0; k < n; k++)
        printf("%s%u,%s", k % 12 == 0 ? "        " : " ", k * 7 + 1, k % 12 == 11 ? "\n" : "");
    printf("\n    };\n    return a[i];\n}\n");
}

static void huge_literal(unsigned n)
{
    printf("static const char text[] = \"");
    for (unsigned k = 0; k < n; k++)
        putchar('a' + (int)(k % 26));
    printf("\";\nint get(int i)\n{\n    return text[i];\n}\n");
}

static void embed(unsigned n, const char* directory)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/blob.bin", directory);
    FILE* file = fopen(path, "wb");
    if (file == 0)
    {
        perror(path);
        exit(1);
    }
    for (unsigned k = 0; k < n; k++)
        fputc((int)((k * 2654435761u) >> 13) & 0xff, file);
    fclose(file);
    printf("static const unsigned char blob[] =\n{\n#embed \"blob.bin\"\n};\nint get(int i)\n{\n    return blob[i];\n}\n");
}

static void pastes(unsigned n)
{
    printf("#define CAT2(a, b) a##b\n#define CAT(a, b) CAT2(a, b)\n#define STR2(x) #x\n#define STR(x) STR2(x)\n");
    for (unsigned i = 0; i < n; i++)
        printf("int CAT(name_, %u) = sizeof(STR(CAT(name_, %u)));\n", i, i);
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


static void enum_chain(unsigned n)
{
    printf("enum\n{\n    E0 = 1,\n");
    for (unsigned i = 1; i < n; i++)
        printf("    E%u = E%u + 1,\n", i, i - 1);
    printf("};\nint get(void)\n{\n    return E%u;\n}\n", n - 1);
}

static void right_deep(unsigned n)
{
    printf("int f(int a)\n{\n    return ");
    for (unsigned i = 0; i < n; i++)
        printf("a + (");
    printf("a");
    for (unsigned i = 0; i < n; i++)
        printf(")");
    printf(";\n}\n");
}

static void left_deep(unsigned n)
{
    printf("int f(int a)\n{\n    return ");
    for (unsigned i = 0; i < n; i++)
        printf("(");
    printf("a");
    for (unsigned i = 0; i < n; i++)
        printf(" + %u)", i);
    printf(";\n}\n");
}

static void nested_blocks(unsigned n)
{
    printf("int f(int x)\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("{ x += %u;\n", i % 7);
    for (unsigned i = 0; i < n; i++)
        printf("}\n");
    printf("    return x;\n}\n");
}

static void nested_ifs(unsigned n)
{
    printf("int f(int x)\n{\n    int y = 0;\n");
    for (unsigned i = 0; i < n; i++)
        printf("if (x > %u) { y += %u;\n", i, i % 5);
    for (unsigned i = 0; i < n; i++)
        printf("}\n");
    printf("    return y;\n}\n");
}

static void nested_loops(unsigned n)
{
    printf("int f(int x)\n{\n    int y = 0;\n");
    for (unsigned i = 0; i < n; i++)
        printf("while (x > %u) { x -= 1;\n", i);
    printf("y += x;\n");
    for (unsigned i = 0; i < n; i++)
        printf("}\n");
    printf("    return y;\n}\n");
}

static void case_ranges(unsigned n)
{
    printf("int f(int x)\n{\n    switch (x)\n    {\n");
    for (unsigned i = 0; i < n; i++)
        printf("    case %u ... %u: return %u;\n", 10 * i, 10 * i + 5, i);
    printf("    default: return -1;\n    }\n}\n");
}

static void if_expression(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("#define D%u %u\n", i, i);
    printf("#if 0");
    for (unsigned i = 0; i < n; i++)
        printf(" + D%u", i);
    printf(" > 0 && (defined(D0)");
    for (unsigned i = 1; i < n; i++)
        printf(" || defined(D%u)", i);
    printf(")\nint x = 1;\n#endif\nint get(void)\n{\n    return x;\n}\n");
}

static void one_line(unsigned n)
{
    printf("int a[] = {");
    for (unsigned i = 0; i < n; i++)
        printf("%u,", i * 7 + 1);
    printf("};\nint get(int i)\n{\n    return a[i];\n}\n");
}

static void splice_macro(unsigned n)
{
    printf("#define BIG \\\n");
    for (unsigned i = 0; i < n; i++)
        printf("    + %u \\\n", i % 10);
    printf("    + 0\nint x = 0 BIG;\nint get(void)\n{\n    return x;\n}\n");
}

static void utf8_identifiers(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("int caf\xc3\xa9_%u(int x\xc3\xa9)\n{\n    return x\xc3\xa9 + %u;\n}\n", i, i);
}

static void syntax_errors(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("int x_%u = ;\n", i);
}

static void semantic_errors(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("int f_%u(void)\n{\n    return undeclared_%u;\n}\n", i, i);
}

static void nested_calls(unsigned n)
{
    printf("int g(int x)\n{\n    return x + 1;\n}\nint f(int a)\n{\n    return ");
    for (unsigned i = 0; i < n; i++)
        printf("g(");
    printf("a");
    for (unsigned i = 0; i < n; i++)
        printf(")");
    printf(";\n}\n");
}

static void unary_chain(unsigned n)
{
    printf("int f(int a)\n{\n    return ");
    for (unsigned i = 0; i < n; i++)
        printf(i % 2 ? "-" : "~");
    printf("a;\n}\n");
}

static void cast_chain(unsigned n)
{
    printf("int f(int a)\n{\n    return ");
    for (unsigned i = 0; i < n; i++)
        printf(i % 2 ? "(int)" : "(long)");
    printf("a;\n}\n");
}

static void index_chain(unsigned n)
{
    printf("int a[16];\nint f(void)\n{\n    return ");
    for (unsigned i = 0; i < n; i++)
        printf("a[");
    printf("0");
    for (unsigned i = 0; i < n; i++)
        printf(" & 15]");
    printf(";\n}\n");
}

static void assign_chain(unsigned n)
{
    printf("int f(void)\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    int v%u;\n", i);
    printf("    ");
    for (unsigned i = 0; i < n; i++)
        printf("v%u = ", i);
    printf("7;\n    return v0;\n}\n");
}

static void many_args(unsigned n)
{
    printf("int sum(int n, ...);\nint f(void)\n{\n    return sum(%u", n);
    for (unsigned i = 0; i < n; i++)
        printf(", %u", i);
    printf(");\n}\n");
}

static void long_double_ops(unsigned n)
{
    printf("long double f(long double a, long double b)\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    a = a * 1.5L + b / %u.0L;\n", i + 1);
    printf("    return a;\n}\n");
}

static void int128_ops(unsigned n)
{
    printf("__int128 f(__int128 a, __int128 b)\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    a = a * 3 + (b >> %u);\n", i % 100);
    printf("    return a;\n}\n");
}

static void double_ops(unsigned n)
{
    printf("double f(double a, double b)\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    a = a * 1.5 + b / %u.0;\n", i + 1);
    printf("    return a;\n}\n");
}

static void atomic_ops(unsigned n)
{
    printf("_Atomic int counter;\nint f(int x)\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    counter += x + %u;\n", i);
    printf("    return counter;\n}\n");
}

static void struct_returns(unsigned n)
{
    printf("struct P { long a, b, c; };\n");
    for (unsigned i = 0; i < n; i++)
        printf("struct P r_%u(struct P p)\n{\n    p.a += %u;\n    return p;\n}\n", i, i);
    printf("long f(struct P p)\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    p = r_%u(p);\n", i);
    printf("    return p.a;\n}\n");
}

static void kr_functions(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("int k_%u(a, b, c)\n    int a;\n    long b;\n    char c;\n{\n    return a + (int)b + c + %u;\n}\n", i, i);
}

static void pointer_depth(unsigned n)
{
    printf("int f(int ");
    for (unsigned i = 0; i < n; i++)
        printf("*");
    printf("p)\n{\n    return ");
    for (unsigned i = 0; i < n; i++)
        printf("*");
    printf("p;\n}\n");
}

static void function_pointer_parameters(unsigned n)
{
    printf("typedef int (*fp)(int");
    for (unsigned i = 1; i < n; i++)
        printf(", int");
    printf(");\nint f(fp g)\n{\n    return g(0");
    for (unsigned i = 1; i < n; i++)
        printf(", %u", i);
    printf(");\n}\n");
}

static void nested_switches(unsigned n)
{
    printf("int f(int x)\n{\n    int y = 0;\n");
    for (unsigned i = 0; i < n; i++)
        printf("switch (x & %u) { case 0: y += %u;\n", i % 7 + 1, i % 3);
    for (unsigned i = 0; i < n; i++)
        printf("break; default: y -= 1; }\n");
    printf("    return y;\n}\n");
}

static void stringify(unsigned n)
{
    printf("#define S(...) #__VA_ARGS__\nconst char* get(void)\n{\n    return S(");
    for (unsigned i = 0; i < n; i++)
        printf("word%u, ", i);
    printf("end);\n}\n");
}


static void declarator_depth(unsigned n)
{
    printf("int ");
    for (unsigned i = 0; i < n; i++)
        printf("(");
    printf("x");
    for (unsigned i = 0; i < n; i++)
        printf(")");
    printf(";\nint get(void)\n{\n    return x;\n}\n");
}

static void typedef_chain(unsigned n)
{
    printf("typedef int T0;\n");
    for (unsigned i = 1; i < n; i++)
        printf("typedef T%u T%u;\n", i - 1, i);
    printf("T%u get(T%u a)\n{\n    return a + 1;\n}\n", n - 1, n / 2);
}

static void diagnostic_pragmas(unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        printf("#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored \"-Wunused-value\"\nint f_%u(int x)\n{\n    x + %u;\n    return x;\n}\n#pragma GCC diagnostic pop\n", i, i);
}

static void inline_asm(unsigned n)
{
    printf("int f(int x)\n{\n");
    for (unsigned i = 0; i < n; i++)
        printf("    __asm__(\"addl $%u, %%0\" : \"+r\"(x));\n", i % 100);
    printf("    return x;\n}\n");
}

static void offsetofs(unsigned n)
{
    printf("struct S\n{\n");
    for (unsigned i = 0; i < 512; i++)
        printf("    int m%u;\n", i);
    printf("};\nunsigned long f(void)\n{\n    unsigned long s = 0;\n");
    for (unsigned i = 0; i < n; i++)
        printf("    s += __builtin_offsetof(struct S, m%u);\n", (i * 7) % 512);
    printf("    return s;\n}\n");
}

static void builtins_heavy(unsigned n)
{
    printf("int f(int x)\n{\n    int y = 0;\n");
    for (unsigned i = 0; i < n; i++)
        printf("    if (__builtin_expect(x > %u, 0)) y += __builtin_popcount((unsigned)x + %u) + __builtin_clz((unsigned)x | 1u);\n", i, i);
    printf("    return y;\n}\n");
}


// N statements that each call one builtin, by name.
static int builtin_statements(const char* name, unsigned n)
{
    static const struct { const char* name; const char* format; } forms[] = {
        { "expect", "    if (__builtin_expect(x > %u, 0)) y += 1;\n" },
        { "popcount", "    y += __builtin_popcount((unsigned)x + %uu);\n" },
        { "clz", "    y += __builtin_clz((unsigned)x | %uu | 1u);\n" },
        { "ctz", "    y += __builtin_ctz((unsigned)x | %uu | 1u);\n" },
        { "bswap", "    y += (int)__builtin_bswap32((unsigned)x + %uu);\n" },
        { "overflow", "    { int r; if (__builtin_add_overflow(x, %u, &r)) y += 1; else y += r; }\n" },
        { "constant", "    y += __builtin_constant_p(x + %u);\n" },
        { "abs", "    y += __builtin_abs(x - %u);\n" },
    };
    int found = 0;
    for (unsigned f = 0; f < sizeof(forms) / sizeof(forms[0]); f++)
    {
        if (strcmp(forms[f].name, name) == 0)
        {
            printf("int f(int x)\n{\n    int y = 0;\n");
            for (unsigned i = 0; i < n; i++)
                printf(forms[f].format, i);
            printf("    return y;\n}\n");
            found = 1;
        }
    }
    return found;
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
    else if (strcmp(mode, "string-calls") == 0) string_calls(n);
    else if (strcmp(mode, "string-declarations") == 0) string_declarations(n);
    else if (strcmp(mode, "char-arrays") == 0) char_arrays(n);
    else if (strcmp(mode, "wide-strings") == 0) wide_strings(n);
    else if (strcmp(mode, "nested-arrays") == 0) nested_arrays(n);
    else if (strcmp(mode, "nested-designators") == 0) nested_designators(n);
    else if (strcmp(mode, "compound-literals") == 0) compound_literals(n);
    else if (strcmp(mode, "static-asserts") == 0) static_asserts(n);
    else if (strcmp(mode, "generics") == 0) generics(n);
    else if (strcmp(mode, "parameters") == 0) parameters(n);
    else if (strcmp(mode, "variadic-calls") == 0) variadic_calls(n);
    else if (strcmp(mode, "pragma-packs") == 0) pragma_packs(n);
    else if (strcmp(mode, "conditionals") == 0) conditionals(n);
    else if (strcmp(mode, "line-directives") == 0) line_directives(n);
    else if (strcmp(mode, "function-table") == 0) function_table(n);
    else if (strcmp(mode, "shadowing") == 0) shadowing(n);
    else if (strcmp(mode, "casts") == 0) casts(n);
    else if (strcmp(mode, "assembly") == 0) assembly(n);
    else if (strcmp(mode, "x-macros") == 0 && count > 3) x_macros(n, arguments[3]);
    else if (strcmp(mode, "warnings") == 0) warnings(n);
    else if (strcmp(mode, "redefinitions") == 0) redefinitions(n);
    else if (strcmp(mode, "local-initializer") == 0) local_initializer(n);
    else if (strcmp(mode, "huge-literal") == 0) huge_literal(n);
    else if (strcmp(mode, "embed") == 0 && count > 3) embed(n, arguments[3]);
    else if (strcmp(mode, "pastes") == 0) pastes(n);
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
    else if (strcmp(mode, "enum-chain") == 0) enum_chain(n);
    else if (strcmp(mode, "right-deep") == 0) right_deep(n);
    else if (strcmp(mode, "left-deep") == 0) left_deep(n);
    else if (strcmp(mode, "nested-blocks") == 0) nested_blocks(n);
    else if (strcmp(mode, "nested-ifs") == 0) nested_ifs(n);
    else if (strcmp(mode, "nested-loops") == 0) nested_loops(n);
    else if (strcmp(mode, "case-ranges") == 0) case_ranges(n);
    else if (strcmp(mode, "if-expression") == 0) if_expression(n);
    else if (strcmp(mode, "one-line") == 0) one_line(n);
    else if (strcmp(mode, "splice-macro") == 0) splice_macro(n);
    else if (strcmp(mode, "utf8-identifiers") == 0) utf8_identifiers(n);
    else if (strcmp(mode, "syntax-errors") == 0) syntax_errors(n);
    else if (strcmp(mode, "semantic-errors") == 0) semantic_errors(n);
    else if (strcmp(mode, "nested-calls") == 0) nested_calls(n);
    else if (strcmp(mode, "unary-chain") == 0) unary_chain(n);
    else if (strcmp(mode, "cast-chain") == 0) cast_chain(n);
    else if (strcmp(mode, "index-chain") == 0) index_chain(n);
    else if (strcmp(mode, "assign-chain") == 0) assign_chain(n);
    else if (strcmp(mode, "many-args") == 0) many_args(n);
    else if (strcmp(mode, "long-double-ops") == 0) long_double_ops(n);
    else if (strcmp(mode, "int128-ops") == 0) int128_ops(n);
    else if (strcmp(mode, "double-ops") == 0) double_ops(n);
    else if (strcmp(mode, "atomic-ops") == 0) atomic_ops(n);
    else if (strcmp(mode, "struct-returns") == 0) struct_returns(n);
    else if (strcmp(mode, "kr-functions") == 0) kr_functions(n);
    else if (strcmp(mode, "pointer-depth") == 0) pointer_depth(n);
    else if (strcmp(mode, "function-pointer-parameters") == 0) function_pointer_parameters(n);
    else if (strcmp(mode, "nested-switches") == 0) nested_switches(n);
    else if (strcmp(mode, "stringify") == 0) stringify(n);
    else if (strcmp(mode, "declarator-depth") == 0) declarator_depth(n);
    else if (strcmp(mode, "typedef-chain") == 0) typedef_chain(n);
    else if (strcmp(mode, "diagnostic-pragmas") == 0) diagnostic_pragmas(n);
    else if (strcmp(mode, "inline-asm") == 0) inline_asm(n);
    else if (strcmp(mode, "offsetofs") == 0) offsetofs(n);
    else if (strcmp(mode, "builtins-heavy") == 0) builtins_heavy(n);
    else if (strncmp(mode, "builtin-", 8) == 0 && builtin_statements(mode + 8, n)) status = 0;
    else
    {
        fprintf(stderr, "gen: unknown mode %s\n", mode);
        status = 1;
    }
    return status;
}

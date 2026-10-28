// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU's cleanup attribute (#147, c_check_cleanup_input.c), on original fixtures. Its rules against clang's: the declarations
// it applies to and those it is ignored on, its argument's lookup, its function's parameter, and the jumps into and out of
// a cleanup's scope that clang's JumpScopeChecker rejects. Then programs of each exit from a cleanup's scope, run natively
// against clang -O0's build of the same source on both paths: blocks' ends, breaks and continues of every loop and a for
// statement's own variables, gotos, returns of each class of value (kept before their cleanups) and the named return value
// (after them, as clang's NRVO has it), statement expressions, switches, and the functions' kinds the lowering takes.
// Include after c_check_program_test.c and c_check_vla_test.c.
#ifndef BUSTER_C_CHECK_CLEANUP_TEST_INCLUDED
#define BUSTER_C_CHECK_CLEANUP_TEST_INCLUDED

static __attribute__((noinline)) void check_test_cleanup_sources(CheckTest* test, const char* clang)
{
    static const CheckSourceFixture fixtures[] =
    {
        { .name = "cleanup on an automatic variable", .source = "void f(int *p);\nvoid g(void) { int a __attribute__((cleanup(f))); }\n" },
        { .name = "cleanup on a register variable", .source = "void f(int *p);\nvoid g(void) { register int a __attribute__((cleanup(f))) = 1; }\n" },
        { .name = "cleanup of a const variable discards its qualifier", .source = "void f(int *p);\nvoid g(void) { const int a __attribute__((cleanup(f))) = 1; }\n" },
        { .name = "cleanup taking a pointer to void", .source = "void f(void *p);\nvoid g(void) { long a __attribute__((cleanup(f))); }\n" },
        { .name = "cleanup taking a qualified pointer", .source = "void f(const volatile int *p);\nvoid g(void) { int a __attribute__((cleanup(f))); }\n" },
        { .name = "variadic cleanup of one named parameter", .source = "void f(int *p, ...);\nvoid g(void) { int a __attribute__((cleanup(f))); }\n" },
        { .name = "cleanup function of a result", .source = "int f(int *p);\nvoid g(void) { int a __attribute__((cleanup(f))); }\n" },
        { .name = "cleanup function through a typedef", .source = "typedef void F(int *);\nF f;\nvoid g(void) { int a __attribute__((cleanup(f))); }\n" },
        { .name = "cleanup of a pointer variable", .source = "void f(char **p);\nvoid g(void) { char *a __attribute__((cleanup(f))) = 0; }\n" },
        { .name = "cleanup of a struct variable", .source = "struct S { int x; };\nvoid f(struct S *p);\nvoid g(void) { struct S s __attribute__((cleanup(f))); }\n" },
        { .name = "cleanup of a variable-length array", .source = "void f(int (*p)[]);\nvoid g(int n) { int a[n] __attribute__((cleanup(f))); }\n" },
        { .name = "cleanup's function named like its variable", .source = "void a(int *p);\nvoid g(void) { int a __attribute__((cleanup(a))); }\n" },
        { .name = "cleanups of a specifier's declarators", .source = "void f(int *p);\nvoid g(void) { __attribute__((cleanup(f))) int a, b; }\n" },
        { .name = "the last of two cleanups", .source = "void f(int *p);\nvoid h(long *p);\nvoid g(void) { int a __attribute__((cleanup(h), cleanup(f))); }\n" },
        { .name = "cleanup in a for statement", .source = "void f(int *p);\nvoid g(void) { for (int a __attribute__((cleanup(f))) = 0; a < 3; a++) { } }\n" },
        { .name = "cleanup in a statement expression", .source = "void f(int *p);\nint g(int n) { return ({ int a __attribute__((cleanup(f))) = n; a; }); }\n" },
        { .name = "cleanup ignored on a file-scope variable", .source = "void f(int *p);\nint a __attribute__((cleanup(f)));\n" },
        { .name = "cleanup ignored on a static local", .source = "void f(int *p);\nvoid g(void) { static int a __attribute__((cleanup(f))); }\n" },
        { .name = "cleanup ignored on an extern local", .source = "void f(int *p);\nvoid g(void) { extern int a __attribute__((cleanup(f))); }\n" },
        { .name = "cleanup ignored on a typedef", .source = "void f(int *p);\nvoid g(void) { typedef int T __attribute__((cleanup(f))); }\n" },
        { .name = "cleanup ignored on a function", .source = "void f(int *p);\nvoid g(void) { int h(void) __attribute__((cleanup(f))); }\n" },
        { .name = "cleanup ignored on a parameter", .source = "void f(int *p);\nvoid g(int a __attribute__((cleanup(f)))) { }\n" },
        { .name = "goto out of a cleanup's scope", .source = "void f(int *p);\nvoid g(int n) { { int a __attribute__((cleanup(f))); if (n) goto out; } out:; }\n" },
        { .name = "goto within a cleanup's scope", .source = "void f(int *p);\nvoid g(int n) { int a __attribute__((cleanup(f))); again: if (n--) goto again; }\n" },
        { .name = "case in a block of a cleanup", .source = "void f(int *p);\nvoid g(int n) { switch (n) { case 1: { int a __attribute__((cleanup(f))); break; } case 2:; } }\n" },
        { .name = "computed goto inside a cleanup's scope", .source = "void f(int *p);\nvoid g(int n) { int a __attribute__((cleanup(f))); void *t[] = { &&l }; goto *t[0]; l:; }\n" },
        { .name = "cleanup of an undeclared function", .source = "void g(void) {\nint a __attribute__((cleanup(missing)));\n}\n", .rejected = true },
        { .name = "cleanup undeclared on a file-scope variable", .source = "int a\n__attribute__((cleanup(missing)));\n", .rejected = true },
        { .name = "cleanup naming its own variable", .source = "void g(void) {\nint a __attribute__((cleanup(a)));\n}\n", .rejected = true },
        { .name = "cleanup of an object", .source = "int f;\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", .rejected = true },
        { .name = "cleanup of a function pointer", .source = "void f(int *p);\nvoid (*h)(int *) = f;\nvoid g(void) {\nint a __attribute__((cleanup(h)));\n}\n", .rejected = true },
        { .name = "cleanup of a type", .source = "typedef int T;\nvoid g(void) {\nint a __attribute__((cleanup(T)));\n}\n", .rejected = true },
        { .name = "cleanup of a parenthesized function", .source = "void f(int *p);\nvoid g(void) {\nint a __attribute__((cleanup((f))));\n}\n", .rejected = true },
        { .name = "cleanup of a string", .source = "void f(int *p);\nvoid g(void) {\nint a __attribute__((cleanup(\"f\")));\n}\n", .rejected = true },
        { .name = "cleanup without an argument", .source = "void f(int *p);\nvoid g(void) {\nint a __attribute__((cleanup));\n}\n", .rejected = true },
        { .name = "cleanup of two arguments", .source = "void f(int *p);\nvoid g(void) {\nint a __attribute__((cleanup(f, f)));\n}\n", .rejected = true },
        { .name = "cleanup function without a prototype", .source = "void f();\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", .rejected = true },
        { .name = "cleanup function of two parameters", .source = "void f(int *p, int q);\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", .rejected = true },
        { .name = "cleanup function of no parameter", .source = "void f(void);\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", .rejected = true },
        { .name = "cleanup of another pointee", .source = "void f(long *p);\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", .rejected = true },
        { .name = "cleanup of another signedness", .source = "void f(int *p);\nvoid g(void) {\nunsigned a __attribute__((cleanup(f)));\n}\n", .rejected = true },
        { .name = "cleanup of a character pointer", .source = "void f(char *p);\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", .rejected = true },
        { .name = "cleanup of a pointer to a pointer to void", .source = "void f(void **p);\nvoid g(void) {\nint *a __attribute__((cleanup(f)));\n}\n", .rejected = true },
        { .name = "cleanup of an integer parameter", .source = "void f(int p);\nvoid g(void) {\nint a __attribute__((cleanup(f)));\n}\n", .rejected = true },
        { .name = "cleanup of an element's pointer", .source = "void f(int *p);\nvoid g(int n) {\nint a[n] __attribute__((cleanup(f)));\n}\n", .rejected = true },
        { .name = "goto into a cleanup's scope", .source = "void f(int *p);\nvoid g(int n) {\ngoto l;\nint a __attribute__((cleanup(f)));\nl:;\n}\n", .rejected = true },
        { .name = "goto into a nested cleanup's scope", .source = "void f(int *p);\nvoid g(int n) {\ngoto l;\n{ int a __attribute__((cleanup(f))); l:; }\n}\n", .rejected = true },
        { .name = "switch into a cleanup's scope", .source = "void f(int *p);\nvoid g(int n) { switch (n) {\nint a __attribute__((cleanup(f)));\ncase 1:;\n} }\n", .rejected = true },
        { .name = "computed goto out of a cleanup's scope", .source = "void f(int *p);\nvoid g(int n) { void *t[] = { &&l };\n{ int a __attribute__((cleanup(f)));\ngoto *t[0]; }\nl:;\n}\n", .rejected = true },
        { .name = "computed goto into a cleanup's scope", .source = "void f(int *p);\nvoid g(int n) { void *t[] = { &&l };\n{ int a __attribute__((cleanup(f)));\nl:; }\ngoto *t[0];\n}\n", .rejected = true },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-checker-cleanup-XXXXXX";
    bool ready = mkdtemp(directory) != 0;
    char paths[FILE_COUNT][160];
    char* pointers[FILE_COUNT];
    ParseTestClang outcomes[FILE_COUNT];
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    const char* arguments[] = { "-std=gnu17" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, FILE_COUNT, outcomes);
    check_test_expect(test, ready, "cleanup Clang oracle could not run");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        char description[192];
        snprintf(description, sizeof(description), "%s: expectation differs from Clang", fixtures[i].name);
        check_test_expect(test, outcomes[i].rejected == fixtures[i].rejected, description);
        check_test_source(test, &fixtures[i], paths[i], &outcomes[i]);
    }
    for (uint32_t i = 0; i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    if (directory[0] != 0)
    {
        rmdir(directory);
    }
}

// Each program's cleanups log the values they see into `trace`, which tested folds into its result.
#define BUSTER_CHECK_TEST_CLEANUP_TRACE \
    "static unsigned trace;static void note(int*p){trace=trace*31u+(unsigned)*p;}" \
    "static void bump(int*p){trace=trace*31u+(unsigned)*p;*p+=100;}"

static __attribute__((noinline)) void check_test_cleanup_programs(CheckTest* test, const char* clang)
{
    check_test_program_source(test, BUSTER_CHECK_TEST_CLEANUP_TRACE
        "int tested(int n){trace=0;int r=0;{int a __attribute__((cleanup(note)))=n;int b __attribute__((cleanup(note)))=n+1;"
        "{int c __attribute__((cleanup(note)))=n+2;r+=c;}__attribute__((cleanup(note))) int d=n+3,e=n+4;int f __attribute__((cleanup(note)));"
        "f=n*7;r+=a+b+d+e;}return (int)trace^r;}", "cleanup-blocks", clang);
    check_test_program_source(test, BUSTER_CHECK_TEST_CLEANUP_TRACE
        "int tested(int n){trace=0;int r=0;for(int i=0;i<n;i++){int a __attribute__((cleanup(note)))=i;if(i==1)continue;if(i==4)break;r+=a;}"
        "int j=0;while(j<n){int b __attribute__((cleanup(note)))=10+j;j++;if(j==2)continue;if(j==5)break;}"
        "do{int c __attribute__((cleanup(note)))=20+j;j++;if(j==7)continue;if(j==9)break;}while(j<n+4);"
        "for(int d __attribute__((cleanup(note)))=30,e __attribute__((cleanup(note)))=31;d<33+n%3;d++){int f __attribute__((cleanup(note)))=40+d;"
        "if(d==31)continue;if(d==33)break;}return (int)trace^r;}", "cleanup-loops", clang);
    check_test_program_source(test, BUSTER_CHECK_TEST_CLEANUP_TRACE
        "int tested(int n){trace=0;int count=0;again:{int a __attribute__((cleanup(note)))=50+count;{int b __attribute__((cleanup(note)))=60+count;"
        "count++;if(count<(n&3))goto again;if(count==(n&3))goto out;}}out:{int c __attribute__((cleanup(note)))=70;if(n>5)goto end;"
        "{int d __attribute__((cleanup(note)))=71;goto end;}}end:return (int)trace^count;}", "cleanup-gotos", clang);
    check_test_program_source(test, BUSTER_CHECK_TEST_CLEANUP_TRACE
        "static void noted(double*p){trace=trace*31u+(unsigned)(int)*p;*p=-1;}static void noted_ld(long double*p){trace=trace*31u+(unsigned)(int)*p;*p=-1;}"
        "static void noted_i(__int128*p){trace=trace*31u+(unsigned)(*p>>64);*p=-1;}static void noted_c(char*p){trace=trace*31u+(unsigned)*p;*p=9;}"
        "static int returns_int(int n){int a __attribute__((cleanup(bump)))=n;{int b __attribute__((cleanup(note)))=70;if(n>2)return a+b;}return a;}"
        "static double returns_double(int n){double d __attribute__((cleanup(noted)))=n*1.5;return d;}"
        "static long double returns_ld(int n){long double d __attribute__((cleanup(noted_ld)))=n*2.5L;return d;}"
        "static __int128 returns_wide(int n){__int128 v __attribute__((cleanup(noted_i)))=((__int128)n<<64)|5;return v;}"
        "static char returns_char(int n){char c __attribute__((cleanup(noted_c)))=(char)n;return c;}"
        "static void returns_void(int n){int a __attribute__((cleanup(note)))=80+n;if(n&1)return;a+=1;}"
        "int tested(int n){trace=0;int r=returns_int(n)+(int)returns_double(n)+(int)returns_ld(n)+(int)(returns_wide(n)>>64)+returns_char(n);"
        "returns_void(n);return (int)trace^r;}", "cleanup-returns", clang);
    // The named return value (Sema's NRVO): every return after its declaration in its scope names it, and the cleanups'
    // writes reach the caller; any other return's value is kept before them.
    check_test_program_source(test, BUSTER_CHECK_TEST_CLEANUP_TRACE
        "typedef struct S{int a,b;}S;typedef struct L{long a,b,c,d;}L;static void clobber(S*p){trace=trace*31u+(unsigned)p->a;p->a+=1000;}"
        "static void clobber_l(L*p){trace=trace*31u+(unsigned)p->a;p->a+=1000;}static S*other;static void clobber_other(int*p){if(other)other->a+=5000;}"
        "static S fa(int b){S x __attribute__((cleanup(clobber)))={b,2};if(b)return x;return x;}"
        "static S fb(int b){S x __attribute__((cleanup(clobber)))={b,2};S y={3,4};if(b)return x;return y;}"
        "static S fc(int b){S x __attribute__((cleanup(clobber)))={b,2};if(b){S y __attribute__((cleanup(clobber)))={3,4};return y;}return x;}"
        "static S fd(int b){S x __attribute__((cleanup(clobber)))={b,2};if(b)return x;S y __attribute__((cleanup(clobber)))={3,4};return y;}"
        "static S fe(int b){S x __attribute__((cleanup(clobber),aligned(64)))={b,2};if(b>1)return(x);return x;}"
        "static S ff(S p){int k __attribute__((cleanup(clobber_other)))=0;other=&p;return p;}"
        "static S fg(int b){S x={b,2};other=&x;{int k __attribute__((cleanup(clobber_other)))=0;if(b)return x;}return x;}"
        "static S fh(int b){S x __attribute__((cleanup(clobber)))={b,2};return({x;});}"
        "static L fl(int b){L x __attribute__((cleanup(clobber_l)))={b,2,3,4};L y={5};if(b&1)return y;return x;}"
        "static L fm(int b){L x __attribute__((cleanup(clobber_l)))={b,2,3,4};for(;;)return x;}"
        "int tested(int n){trace=0;S p={n,1};int r=fa(n).a+fb(n).a+fc(n&1).a+fd(n&1).a+fe(n).a+ff(p).a;other=0;r+=fg(n&1).a;other=0;"
        "r+=fh(n).a+(int)fl(n).a+(int)fm(n).a;return (int)trace^r;}", "cleanup-named-return-values", clang);
    check_test_program_source(test, BUSTER_CHECK_TEST_CLEANUP_TRACE
        "typedef struct S{int a,b;}S;static void clobber(S*p){trace=trace*31u+(unsigned)p->a;p->a+=1000;}"
        "int tested(int n){trace=0;int r=({int a __attribute__((cleanup(bump)))=n;a+1;});"
        "S s=({S t __attribute__((cleanup(clobber)))={n,1};t;});"
        "for(int i=0;i<3;i++){r+=({int b __attribute__((cleanup(note)))=i;if(i==n)break;b;});}"
        "return (int)trace^(r*10+s.a+s.b);}", "cleanup-statement-expressions", clang);
    check_test_program_source(test, BUSTER_CHECK_TEST_CLEANUP_TRACE
        "int tested(int n){trace=0;int r=0;switch(n%4){case 1:{int a __attribute__((cleanup(note)))=90;r=1;break;}"
        "case 2:{int b __attribute__((cleanup(note)))=91;r=2;}default:{int c __attribute__((cleanup(note)))=92;r+=10;if(n>8)break;r+=1;}}"
        "return (int)trace^r;}", "cleanup-switches", clang);
    check_test_program_source(test, BUSTER_CHECK_TEST_CLEANUP_TRACE
        "static int noted_int(int*p){trace=trace*31u+(unsigned)*p+1000u;return 7;}static void noted_void(void*p){trace=trace*31u+(unsigned)*(int*)p+2000u;}"
        "static void noted_const(const int*p){trace=trace*31u+(unsigned)*p+3000u;}static void noted_variadic(int*p,...){trace=trace*31u+(unsigned)*p+4000u;}"
        "static void noted_long(long*p){trace=trace*31u+(unsigned)*p+5000u;}static void noted_vla(int(*p)[]){trace=trace*31u+(unsigned)(*p)[0]+6000u;}"
        "static void noted_array(int(*p)[3]){trace=trace*31u+(unsigned)((*p)[0]+(*p)[2]);}static void noted_pointer(int**p){trace=trace*31u+(unsigned)**p;}"
        "static int kinds(int n){int a __attribute__((cleanup(noted_int)))=n;int b __attribute__((cleanup(noted_void)))=n+1;"
        "const int d __attribute__((cleanup(noted_const)))=n+3;int e __attribute__((cleanup(noted_variadic)))=n+4;"
        "long f __attribute__((cleanup(noted_long)))=n+5;int g[(n&3)+1] __attribute__((cleanup(noted_vla)));g[0]=n+6;"
        "int h[3] __attribute__((cleanup(noted_array)))={n,2,n*3};int*i __attribute__((cleanup(noted_pointer)))=&a;"
        "{register int j __attribute__((cleanup(note)))=n*5;}return a+b+d+e+(int)f+g[0]+h[1];}"
        "int tested(int n){trace=0;int r=kinds(n);return (int)trace^r;}", "cleanup-kinds", clang);
    check_test_program_build_release();
}

static __attribute__((noinline)) void check_test_cleanups(CheckTest* test, const char* clang)
{
    check_test_cleanup_sources(test, clang);
    check_test_cleanup_programs(test, clang);
}

#endif

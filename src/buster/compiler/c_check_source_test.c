// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original declaration/statement fixtures run through both production parser/checker schedules and external Clang.
// Include after check_test_source; no Clang implementation or external fixture is incorporated.

static void check_test_source_extensions(CheckTest* test, const char* clang)
{
    const CheckSourceFixture fixtures[] =
    {
        {
            "callback parameter",
            "int f(int (*cb)(int), int x) { return cb(x); }\n",
            false,
            false
        },
        {
            "function parameter adjustment",
            "int f(int cb(int), int x) { return cb(x); }\n",
            false,
            false
        },
        {
            "function returning callback",
            "int (*f(int (*cb)(int)))(int) { return cb; }\n",
            false,
            false
        },
        {
            "array of callbacks",
            "int (*callbacks[2])(int); int f(void) { return callbacks[1](3); }\n",
            false,
            false
        },
        {
            "callback static initializer",
            "int f(int x) { return x; } int (*p)(int) = f;\n",
            false,
            false
        },
        {
            "parenthesized declarator",
            "int (((x))) = 7;\n",
            false,
            false
        },
        {
            "abstract callback array",
            "_Static_assert(sizeof(int (*[2])(int)) == 16, \"callbacks\");\n",
            false,
            false
        },
        {
            "array pointer binding",
            "int a[2][3]; int (*p)[3] = &a[1]; _Static_assert(sizeof(*p) == 12, \"row\");\n",
            false,
            false
        },
        {
            "ordered typedef declarators",
            "typedef int I, F(I); F f; int g(void) { return f(3); }\n",
            false,
            false
        },
        {
            "ordered initializer declarators",
            "int f(void) { int x=1, (*p)(int)=0, y=x; return y; }\n",
            false,
            false
        },
        {
            "compatible block function redeclaration",
            "int g(int); int f(void) { int g(int); return g(3); }\n",
            false,
            false
        },
        {
            "conflicting block function redeclaration",
            "int g(int); int f(void) { int g(double); return 0; }\n",
            true,
            false
        },
        {
            "linked object and block function conflict",
            "int f; int g(void) { int f(void); return 0; }\n",
            true,
            false
        },
        {
            "compatible disjoint block prototypes",
            "int g(void) { { int f(int); } { int f(int); return f(3); } }\n",
            false,
            false
        },
        {
            "incompatible disjoint block prototypes",
            "int g(void) { { int f(int); } { int f(double); } return 0; }\n",
            true,
            false
        },
        {
            "block prototype before conflicting file declaration",
            "int g(void) { int f(int); return 0; } int f(double);\n",
            true,
            false
        },
        {
            "conflicting prototypes in different functions",
            "int g(void) { int f(int); return 0; } int h(void) { int f(double); return 0; }\n",
            true,
            false
        },
        {
            "local shadow preserves linked conflict",
            "int f(int); int g(void) { int f; { int f(double); } return 0; }\n",
            true,
            false
        },
        {
            "expired prototype does not strengthen later declaration",
            "int g(void) { { int f(int); } return 0; } int f(); int h(void) { return f(); }\n",
            false,
            false
        },
        {
            "inner prototype does not strengthen outer declaration",
            "int f(); int g(void) { { int f(int); } return f(); }\n",
            false,
            false
        },
        {
            "hidden prototype conflicts with parameterless definition",
            "int g(void) { { int f(int); } return 0; } int f() { return 0; }\n",
            true,
            false
        },
        {
            "GNU17 prototype after parameterless definition",
            "int f() { return 0; } int g(void) { int f(int); return 0; }\n",
            false,
            false
        },
        {
            "nested duplicate parameter",
            "int f(int x, int (*cb)(int x), int x);\n",
            true,
            false
        },
        {
            "const callback parameter",
            "int f(int (*const cb)(int)) { cb=0; return 0; }\n",
            true,
            false
        },
        {
            "invalid callback argument",
            "int f(int (*cb)(int)) { return cb(); }\n",
            true,
            false
        },
        {
            "function cannot return array",
            "int f(void)[3];\n",
            true,
            false
        },
        {
            "declaration ICE bounds",
            "int a[(2+3)*4]; _Static_assert(sizeof(a)==80, \"bounds\");\n",
            false,
            false
        },
        {
            "declaration ICE unevaluated fault",
            "int a[1 || (1/0)]; _Static_assert(sizeof(a)==4, \"bound\");\n",
            false,
            false
        },
        {
            "invalid negative bound",
            "int a[-1];\n",
            true,
            false
        },
        {
            "enum arithmetic and bounds",
            "enum E { A=-2, B=A+4, C, N=1<<3 }; int a[N+1]; _Static_assert(sizeof(a)==36 && B==2 && C==3, \"enum\");\n",
            false,
            false
        },
        {
            "enum scope restoration",
            "enum E { A=1 }; int f(void) { { enum F { A=2 }; } return A; }\n",
            false,
            false
        },
        {
            "duplicate enumerator",
            "enum E { A=1, A=2 };\n",
            true,
            false
        },
        {
            "enum is not assignable",
            "enum E { A=1 }; int f(void) { A=2; return A; }\n",
            true,
            false
        },
        {
            "record member access",
            "struct S { int x; long y; }; int f(struct S *p) { return p->x+p->y; }\n",
            false,
            false
        },
        {
            "record forward completion",
            "struct S; struct S { int x; }; struct S x; _Static_assert(sizeof(x)==4, \"record\");\n",
            false,
            false
        },
        {
            "record recursion",
            "struct S { int x; struct S *next; }; _Static_assert(sizeof(struct S)==16, \"record\");\n",
            false,
            false
        },
        {
            "nested record layout",
            "struct O { char c; struct I { long x; } i; unsigned b:3; }; _Static_assert(sizeof(struct O)==24, \"layout\"); int f(struct O *p) { return p->i.x; }\n",
            false,
            false
        },
        {
            "union layout",
            "union U { int i; long l; }; _Static_assert(sizeof(union U)==8, \"union\");\n",
            false,
            false
        },
        {
            "record assignment and return",
            "struct S { int x; }; struct S f(struct S x) { struct S y=x; y=x; return y; }\n",
            false,
            false
        },
        {
            "record tag shadow",
            "struct S { int x; }; int f(void) { struct S { long x; }; struct S s; return sizeof(s); }\n",
            false,
            false
        },
        {
            "duplicate member across nested tag",
            "struct O { int x; struct I { int x; } i; int x; };\n",
            true,
            false
        },
        {
            "tag kind mismatch",
            "struct S; union S *p;\n",
            true,
            false
        },
        {
            "missing record member",
            "struct S { int x; }; int f(struct S *p) { return p->y; }\n",
            true,
            false
        },
        {
            "record const member assignment",
            "struct S { const int x; }; void f(struct S a, struct S b) { a=b; }\n",
            true,
            false
        },
        {
            "incomplete by-value member",
            "struct S { struct S x; };\n",
            true,
            false
        },
        {
            "incomplete tentative object",
            "struct S; struct S x;\n",
            true,
            false
        },
        {
            "sizeof before completion",
            "struct S; unsigned long x=sizeof(struct S); struct S { int x; };\n",
            true,
            false
        },
        {
            "prior incomplete-pointer expression remains unsupported",
            "struct S; struct S *p; int f(void) { return p!=0; } struct S { int x; };\n",
            true,
            true
        },
        {
            "flexible member",
            "struct S { int n; int data[]; }; _Static_assert(sizeof(struct S)==4, \"flexible\");\n",
            false,
            false
        },
        {
            "sole flexible member GNU extension rejected",
            "struct S { int data[]; };\n",
            true,
            false,
            true
        },
        {
            "unnamed zero-width field is not a flexible member predecessor",
            "struct S { int :0; int data[]; };\n",
            true,
            false,
            true
        },
        {
            "unnamed bitfield is not a flexible member predecessor",
            "struct S { int :1; int data[]; };\n",
            true,
            false,
            true
        },
        {
            "flexible member after named field and unnamed padding",
            "struct S { int n; int :0; int data[]; }; _Static_assert(sizeof(struct S)==4, \"flexible padding\");\n",
            false,
            false
        },
        {
            "union flexible member GNU extension rejected",
            "union U { int n; int data[]; };\n",
            true,
            false,
            true
        },
        {
            "flexible member must be last",
            "struct S { int n; int data[]; int tail; };\n",
            true,
            false
        },
        {
            "named zero-width bitfield",
            "struct S { int x:0; };\n",
            true,
            false
        },
        {
            "bitfield update",
            "struct S { unsigned x:3; }; int f(struct S *p) { p->x+=8; return p->x++; }\n",
            false,
            false
        },
        {
            "bitfield has no address",
            "struct S { unsigned x:3; }; void f(struct S *p) { &p->x; }\n",
            true,
            false
        },
        {
            "all compound operators",
            "int f(int x,int y) { x+=y; x-=y; x*=y; x/=y; x%=y; x<<=y; x>>=y; x&=y; x^=y; x|=y; return x; }\n",
            false,
            false
        },
        {
            "prefix and postfix updates",
            "int f(int x) { ++x; --x; x++; x--; return x; }\n",
            false,
            false
        },
        {
            "mixed compound assignment",
            "int f(int x, double y) { x+=y; return x; }\n",
            false,
            false
        },
        {
            "narrow compound assignment",
            "unsigned char f(unsigned char x) { x+=256; return x; }\n",
            false,
            false
        },
        {
            "pointer updates",
            "int *f(int *p) { ++p; p--; p-=2; return p; }\n",
            false,
            false
        },
        {
            "invalid pointer compound difference",
            "void f(int *p,int *q) { p-=q; }\n",
            true,
            false
        },
        {
            "invalid const update",
            "int f(const int x) { return x++; }\n",
            true,
            false
        },
        {
            "invalid rvalue update",
            "int f(void) { return ++1; }\n",
            true,
            false
        },
        {
            "invalid array update",
            "int f(void) { int a[2]; a++; return 0; }\n",
            true,
            false
        },
        {
            "for declaration scope",
            "int f(int n) { int x=0; for(int i=0;i<n;i++) { if(i) continue; x+=i; if(x) break; } return x; }\n",
            false,
            false
        },
        {
            "empty for condition",
            "void f(void) { for(;;) break; }\n",
            false,
            false
        },
        {
            "comma for condition",
            "int f(int x) { for(;x=0,x;) x++; return x; }\n",
            false,
            false
        },
        {
            "for scope expires",
            "int f(void) { for(int i=0;i<1;i++) {} return i; }\n",
            true,
            false
        },
        {
            "record condition rejected",
            "struct S { int x; }; void f(struct S x) { for(;x;) {} }\n",
            true,
            false
        },
        {
            "while and do controls",
            "int f(int x) { while(x) { if(x) break; continue; } do { x--; if(x) continue; break; } while(x); return x; }\n",
            false,
            false
        },
        {
            "retained packing pragma is unsupported",
            "#pragma pack(1)\nstruct S { char c; int x; };\n",
            true,
            true
        },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-checker-source-XXXXXX";
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
    check_test_expect(test, ready, "extended source clang oracle could not run");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        char description[192];
        snprintf(description, sizeof(description), "%s: extended fixture differs from clang", fixtures[i].name);
        check_test_expect(test, outcomes[i].rejected == (fixtures[i].rejected && !fixtures[i].unsupported && !fixtures[i].extension), description);
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

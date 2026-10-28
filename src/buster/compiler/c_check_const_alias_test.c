// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original typed-read and pointer-provenance controls, checked independently by external Clang.
// Include after CheckSourceFixture and check_test_source; no native-program helpers are needed.

static const CheckSourceFixture check_const_alias_fixtures[] =
{
    {
        .name = "literal bytes retain their actual plain char element type",
        .source = "const char* const p = \"abc\";\nenum { E = p[0] };\n_Static_assert(E == 97, \"a\");\n",
    },
    {
        .name = "literal byte storage cannot supply a typed int alias",
        .source = "const char a[] = \"abc\";\nenum { E = *(const int*)a };\n",
        .rejected = true,
    },
    {
        .name = "signed char pointer casts do not retag literal bytes",
        .source = "const signed char* const p = (const signed char*)\"abc\";\nenum { E = p[0] };\n_Static_assert(E == 97, \"a\");\n",
        .rejected = true,
    },
    {
        .name = "unsigned char arrays cannot be read through plain char casts",
        .source = "const unsigned char a[] = \"abc\";\nenum { E = *(const char*)a };\n_Static_assert(E == 97, \"a\");\n",
        .rejected = true,
    },
    {
        .name = "literal initialization gives an unsigned char array its declared type",
        .source = "const unsigned char a[] = \"abc\";\nenum { E = a[0] };\n_Static_assert(E == 97, \"a\");\n",
    },
    {
        .name = "unsigned char pointer casts do not retag literal bytes",
        .source = "const unsigned char* const p = (const unsigned char*)\"abc\";\nenum { E = p[0] };\n_Static_assert(E == 97, \"a\");\n",
        .rejected = true,
    },
    {
        .name = "implicit union zeroes do not permit a first-member pointer cast",
        .source = "union U { int x; float f; };\nconst union U u = {};\nenum { E = *(const int*)&u };\n",
        .rejected = true,
    },
    {
        .name = "implicit integer zeroes cannot supply a byte alias",
        .source = "struct P { int x; };\nconst struct P p = {};\nenum { E = *(const unsigned char*)&p.x };\n_Static_assert(E == 0, \"zero\");\n",
        .rejected = true,
    },
    {
        .name = "character pointer arithmetic cannot recover a typed member read",
        .source = "struct P { char x; int y; };\nconst struct P p = {};\nenum { E = *(const int*)((const char*)&p + 4) };\n",
        .rejected = true,
    },
    {
        .name = "first-member layout equivalence does not prove a constant read",
        .source = "struct P { int x; };\nconst struct P p = {};\nenum { E = *(const int*)&p };\n_Static_assert(E == 0, \"zero\");\n",
        .rejected = true,
    },
    {
        .name = "a float pointer roundtrip cannot erase a type-changing cast",
        .source = "struct P { int x; };\nconst struct P p = {};\nenum { E = *(const int*)(const float*)&p.x };\n_Static_assert(E == 0, \"zero\");\n",
        .rejected = true,
    },
    {
        .name = "implicit void pointer storage preserves a read-blocking roundtrip",
        .source = "struct P { int x; };\nconst struct P p = {};\nconst void* const v = &p.x;\nconst int* const q = v;\nenum { E = *q };\n",
        .rejected = true,
    },
    {
        .name = "an inactive member cannot borrow active zeroes through a cast",
        .source = "union U { int x; float f; };\nconst union U u = {};\nenum { E = (int)*(const float*)&u.x };\n",
        .rejected = true,
    },
    {
        .name = "implicit zero coverage does not make an inactive union member readable",
        .source = "union U { int x; float f; };\nconst union U u = {};\nenum { E = (int)u.f };\n",
        .rejected = true,
    },
    {
        .name = "a const access type cannot make a mutable defining object constant",
        .source = "struct P { int x; };\nstruct P p = {};\nenum { E = *(const int*)&p.x };\n",
        .rejected = true,
    },
    {
        .name = "zero padding cannot be consumed as an unsigned char constant",
        .source = "struct P { char x; int y; };\nconst struct P p = {};\nenum { E = *((const unsigned char*)&p + 1) };\n",
        .rejected = true,
    },
    {
        .name = "implicit float zero uses the selected member's actual type",
        .source = "struct P { float x; };\nconst struct P p = {};\nenum { E = (int)p.x };\n_Static_assert(E == 0, \"zero\");\n",
    },
    {
        .name = "implicit integer zero uses the selected member's actual type",
        .source = "struct P { int x; };\nconst struct P p = {};\nenum { E = p.x };\n_Static_assert(E == 0, \"zero\");\n",
    },
    {
        .name = "a same-type pointer cast permits the actual const integer member read",
        .source = "struct P { int x; };\nconst struct P p = {};\nenum { E = *(const int*)&p.x };\n_Static_assert(E == 0, \"zero\");\n",
    },
    {
        .name = "pointer qualifier removal does not change the actual member type",
        .source = "struct P { int x; };\nconst struct P p = {};\nenum { E = *(int*)&p.x };\n_Static_assert(E == 0, \"zero\");\n",
    },
    {
        .name = "const pointer-object storage retains a wrong float access type",
        .source = "struct P { int x; };\nconst struct P p = {};\nconst float* const q = (const float*)&p.x;\nenum { E = (int)*q };\n",
        .rejected = true,
    },
    {
        .name = "an explicit void pointer roundtrip cannot erase a type-changing cast",
        .source = "struct P { int x; };\nconst struct P p = {};\nenum { E = *(const int*)(const void*)&p.x };\n_Static_assert(E == 0, \"zero\");\n",
        .rejected = true,
    },
    {
        .name = "casting away a member's volatile qualification cannot permit folding",
        .source = "struct P { volatile int x; };\nconst struct P p = {};\nenum { E = *(const int*)&p.x };\n",
        .rejected = true,
    },
    {
        .name = "implicit integer zero is not a float object representation",
        .source = "struct P { int x; };\nconst struct P p = {};\nenum { E = (int)*(const float*)&p.x };\n_Static_assert(E == 0, \"zero\");\n",
        .rejected = true,
    },
    {
        .name = "implicit integer zero is not a pointer object representation",
        .source = "struct P { long x; };\nconst struct P p = {};\nenum { E = *(int*const*)&p.x == 0 };\n_Static_assert(E == 1, \"null\");\n",
        .rejected = true,
    },
    {
        .name = "implicit integer zero is not a short subobject",
        .source = "struct P { int x; };\nconst struct P p = {};\nenum { E = *(const short*)&p.x };\n_Static_assert(E == 0, \"zero\");\n",
        .rejected = true,
    },
    {
        .name = "implicit signed integer zero is not an unsigned integer subobject",
        .source = "struct P { int x; };\nconst struct P p = {};\nenum { E = *(const unsigned int*)&p.x };\n_Static_assert(E == 0, \"zero\");\n",
        .rejected = true,
    },
    {
        .name = "a typed array value cannot supply a byte alias",
        .source = "const unsigned short a[] = { 0 };\nenum { E = *(const unsigned char*)a };\n",
        .rejected = true,
    },
    {
        .name = "an explicit integer zero cannot supply a byte alias",
        .source = "const int k = 0;\nenum { E = *(const unsigned char*)&k };\n_Static_assert(E == 0, \"zero\");\n",
        .rejected = true,
    },
    {
        .name = "an explicit integer zero is not a float object representation",
        .source = "struct P { int x; };\nconst struct P p = { 0 };\nenum { E = (int)*(const float*)&p.x };\n_Static_assert(E == 0, \"zero\");\n",
        .rejected = true,
    },
    {
        .name = "implicit void roundtrips still publish legal symbolic addresses",
        .source = "const int k=0;\nconst void* const v=&k;\nconst int* const p=v;\nconst int* q=p;\n",
    },
    {
        .name = "explicit void roundtrips still publish legal symbolic addresses",
        .source = "const int k=0;\nconst int* const p=(const int*)(const void*)&k;\nconst int* q=p;\n",
    },
    {
        .name = "wrong access types still publish legal symbolic addresses",
        .source = "const int k=0;\nconst float* const p=(const float*)&k;\nconst float* q=p;\n",
    },
    {
        .name = "type-changing pointer roundtrips still publish legal symbolic addresses",
        .source = "const int k=0;\nconst int* const p=(const int*)(const float*)&k;\nconst int* q=p;\n",
    },
    {
        .name = "first-member pointer casts still publish legal symbolic addresses",
        .source = "struct P{int x;};\nconst struct P p={};\nconst int* const q=(const int*)&p;\nconst int* r=q;\n",
    },
    {
        .name = "pointer arithmetic after a void roundtrip still publishes an address",
        .source = "const int a[2]={0,1};\nconst int* const p=(const int*)(const void*)a;\nconst int* q=p+1;\n",
    },
    {
        .name = "stored pointer arithmetic cannot erase a void-roundtrip read restriction",
        .source = "const int a[2]={0,1};\nconst int* const p=(const int*)(const void*)a;\nconst int* const q=p+1;\nenum{E=*q};\n",
        .rejected = true,
    },
    {
        .name = "implicit void conversion survives repeated pointer storage and arithmetic",
        .source = "const int a[2]={3,4};\nconst void* const v=a;\nconst int* const q=v;\nconst int* const r=q+1;\nenum{E=*r};\n",
        .rejected = true,
    },
    {
        .name = "stored pointer arithmetic cannot erase literal signedness provenance",
        .source = "const unsigned char* const p=(const unsigned char*)\"abc\";\nconst unsigned char* const q=p+1;\nenum{E=*q};\n",
        .rejected = true,
    },
    {
        .name = "same-type pointer storage and arithmetic retain readable array provenance",
        .source = "const int a[2]={7,9};\nconst int* const p=(const int*)a;\nconst int* const q=p+1;\nenum{E=*q};\n_Static_assert(E==9,\"same type\");\n",
    },
    {
        .name = "qualifier-only pointer storage and arithmetic retain readable array provenance",
        .source = "const int a[2]={7,9};\nint* const p=(int*)a;\nint* const q=p+1;\nenum{E=*q};\n_Static_assert(E==9,\"qualification\");\n",
    },
    {
        .name = "a discarded comma operand cannot taint the selected pointer path",
        .source = "const int k=3;\nconst int* const p=((const void*)&k,&k);\nenum{E=*p};\n_Static_assert(E==3,\"pointer path\");\n",
    },
    {
        .name = "a discarded index operand cannot taint the selected array path",
        .source = "const int a[2]={3,4};\nenum{E=a[((const void*)&a,1)]};\n_Static_assert(E==4,\"index path\");\n",
    },
    {
        .name = "tail padding cannot give a flexible member a readable array bound",
        .source = "struct P{_Alignas(16) int x;int a[];};\nconst struct P p={1};\nenum{E=p.a[0]};\n",
        .rejected = true,
    },
    {
        .name = "flexible initializer storage does not give a source member a known array bound",
        .source = "struct P{_Alignas(16) int x;int a[];};\nconst struct P p={1,{2,3}};\nenum{E=p.a[0]};\n",
        .rejected = true,
    },
    {
        .name = "an enclosing tail cannot give a nested flexible member a readable array bound",
        .source = "struct I{int x;int a[];};\nstruct P{struct I i;int tail;};\nconst struct P p={{0},0};\nenum{E=p.i.a[0]};\n",
        .rejected = true,
    },
    {
        .name = "a discarded inactive-member address cannot taint the selected pointer path",
        .source = "union U{int x,y;};const union U u={.x=1};const int k=3;\nconst int* const p=(&u.y,&k);\nenum{E=*p};\n_Static_assert(E==3,\"discarded address\");\n",
    },
    {
        .name = "a discarded inactive-member address cannot taint the selected array path",
        .source = "union U{int x,y;};const union U u={.x=1};const int a[2]={3,4};\nenum{E=a[(&u.y,1)]};\n_Static_assert(E==4,\"discarded index address\");\n",
    },
};

static void check_test_const_alias_sources(CheckTest* test, const char* clang)
{
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(check_const_alias_fixtures) };
    char directory[] = "/tmp/buster-checker-const-alias-XXXXXX";
    const bool created = mkdtemp(directory) != NULL;
    bool ready = created;
    char paths[FILE_COUNT][160];
    char* pointers[FILE_COUNT];
    ParseTestClang outcomes[FILE_COUNT];
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], check_const_alias_fixtures[i].source, strlen(check_const_alias_fixtures[i].source));
    }
    const char* arguments[] = { "-std=gnu17" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, FILE_COUNT, outcomes);
    check_test_expect(test, ready, "typed const-alias source Clang oracle is available");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        char description[256];
        snprintf(description, sizeof(description), "%s: GNU17 acceptance agrees with Clang", check_const_alias_fixtures[i].name);
        check_test_expect(test, outcomes[i].rejected == check_const_alias_fixtures[i].rejected, description);
        check_test_source(test, check_const_alias_fixtures + i, paths[i], outcomes + i);
    }
    for (uint32_t i = 0; created && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    if (created)
    {
        rmdir(directory);
    }
}

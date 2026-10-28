// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original declaration-time expression fixtures, checked by both parser/checker paths and installed Clang.
// The FILE-sized arithmetic is authored from the target sizes; no system header or external fixture is copied.

static void check_test_constant_parameter_names(CheckTest* test)
{
    enum { COUNT = 1024 };
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t bytes = COUNT * sizeof(CheckConstantParameter);
    const uint64_t readable = (bytes + page - 1) / page * page;
    uint8_t* mapping = mmap(0, readable + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool ready = mapping != MAP_FAILED && mprotect(mapping + readable, page, PROT_NONE) == 0;
    check_test_expect(test, ready, "prototype name tree guarded records");
    if (ready)
    {
        CheckConstantParameter* parameters = (CheckConstantParameter*)(mapping + readable - bytes);
        CheckConstantPart functions[2] = { 0 };
        uint8_t owners[COUNT];
        for (uint32_t i = 0; i < COUNT - 2; i += 1)
        {
            // The first 33 names force every bit decision, including bit 31; later names mix duplicates,
            // unnamed parameters and two interleaved prototype scopes. The independent oracle scans only here.
            const uint32_t name = i < 32 ? ~(UINT32_C(1) << i) : i == 32 ? UINT32_MAX : (i * 73) % 193;
            owners[i] = i < 33 ? 0 : i & 1;
            parameters[i] = (CheckConstantParameter){ .name = name };
            bool expected = true;
            for (uint32_t previous = 0; previous < i; previous += 1)
            {
                expected &= name == 0 || owners[previous] != owners[i] || parameters[previous].name != name;
            }
            const bool unique = check_constant_parameter_unique(&functions[owners[i]], parameters, i);
            check_test_expect(test, unique == expected, "prototype name tree differs from independent membership");
        }
        parameters[COUNT - 1] = (CheckConstantParameter){ .name = UINT32_MAX };
        check_test_expect(test, !check_constant_parameter_unique(&functions[0], parameters, COUNT - 1), "prototype name tree full-depth duplicate");
        CheckConstantPart empty = { 0 };
        parameters[COUNT - 1] = (CheckConstantParameter){ 0 };
        check_test_expect(test, check_constant_parameter_unique(&empty, parameters, COUNT - 1) && empty.parameter_names == 0,
                          "unnamed parameter leaves prototype tree empty");
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, readable + page);
    }
}

static __attribute__((noinline)) void check_test_constant_parameter_sources(CheckTest* test, const char* clang)
{
    const uint32_t counts[] = {
        0,
        1,
        15,
        16,
        17,
        31,
        32,
        33,
        256,
        4096,
    };
    char directory[] = "/tmp/buster-checker-parameters-XXXXXX";
    const bool created = mkdtemp(directory) != 0;
    check_test_expect(test, created, "prototype scaling fixtures' directory");
    for (uint32_t c = 0; created && c < BUSTER_ARRAY_LENGTH(counts); c += 1)
    {
        const uint32_t count = counts[c];
        for (uint32_t form = 0; form < (count >= 256 ? 4u : 2u); form += 1)
        {
            Arena text = arena_reserve((uint64_t)count * 32 + 256);
            const uint64_t capacity = (uint64_t)count * 32 + 256;
            char* source = arena_push(&text, capacity, 1);
            uint64_t length = 0;
            if (source != 0)
            {
                length = (uint64_t)snprintf(source, capacity, "enum { A=sizeof(int (*)(");
                for (uint32_t i = 0; i < count; i += 1)
                {
                    const uint32_t name = form == 2 && i == 1 ? 0 : form == 3 && i + 1 == count ? 0 : i;
                    length += form == 1 ? (uint64_t)snprintf(source + length, capacity - length, "%sint", i == 0 ? "" : ",") :
                                          (uint64_t)snprintf(source + length, capacity - length, "%sint p%u", i == 0 ? "" : ",", name);
                }
                length += (uint64_t)snprintf(source + length, capacity - length, "%s)) }; _Static_assert(A==8,\"prototype pointer\");\n", count == 0 ? "void" : "");
            }
            char path[160];
            snprintf(path, sizeof(path), "%s/%u-%u.c", directory, count, form);
            char* paths[] = { path };
            const char* arguments[] = { "-std=gnu17" };
            ParseTestClang outcome = { 0 };
            const bool ready = source != 0 && length < capacity && parse_test_write(path, source, length) &&
                               parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), paths, 1, &outcome);
            check_test_expect(test, ready && outcome.rejected == (form >= 2), "prototype scaling fixture Clang expectation");
            if (ready)
            {
                const CheckSourceFixture fixture = {
                    .name = "generated prototype parameter names",
                    .source = source,
                    .rejected = form >= 2,
                };
                check_test_source(test, &fixture, path, &outcome);
            }
            unlink(path);
            arena_release(&text);
        }
    }
    if (created)
    {
        rmdir(directory);
    }
}

static __attribute__((noinline)) void check_test_constant_sources(CheckTest* test, const char* clang)
{
    check_test_constant_parameter_names(test);
    check_test_constant_parameter_sources(test, clang);
    // Clang 23 tests only the low 64 bits of this GNU condition; GCC and the full-width C integer truth rule
    // select 7. Keep the original counterexample as a direct checker regression and an explicit oracle probe.
    const char* wide_truth_source = "enum { N=__builtin_choose_expr(((__int128)1<<100),7,0) }; _Static_assert(N==7, \"full-width truth\");\n";
    char nested_selections[4][8192];
    for (uint32_t shape = 0; shape < BUSTER_ARRAY_LENGTH(nested_selections); shape += 1)
    {
        // Clang's ICE validation stalls on deep choose-expression enums; runtime syntax is a bounded oracle.
        uint32_t used = (uint32_t)snprintf(nested_selections[shape], sizeof(nested_selections[shape]), shape < 2 ? "int f(void){return " : "enum { N=");
        const char* opening = shape == 0 ? "__builtin_choose_expr(1," : shape == 1 ? "__builtin_choose_expr(0," :
                              shape == 2 ? "_Generic(0,int:" : "_Generic(0,default:";
        const char* closing = shape < 2 ? ",0)" : shape == 2 ? ",default:0)" : ",int:0)";
        for (uint32_t depth = 0; depth < 128; depth += 1)
        {
            used += (uint32_t)snprintf(nested_selections[shape] + used, sizeof(nested_selections[shape]) - used, "%s", opening);
        }
        used += (uint32_t)snprintf(nested_selections[shape] + used, sizeof(nested_selections[shape]) - used, "7");
        for (uint32_t depth = 0; depth < 128; depth += 1)
        {
            used += (uint32_t)snprintf(nested_selections[shape] + used, sizeof(nested_selections[shape]) - used, "%s", closing);
        }
        if (shape < 2)
        {
            snprintf(nested_selections[shape] + used, sizeof(nested_selections[shape]) - used, ";}\n");
        }
        else
        {
            snprintf(nested_selections[shape] + used, sizeof(nested_selections[shape]) - used,
                     " }; _Static_assert(N==%u, \"nested selection\");\n", shape % 2 == 0 ? 7u : 0u);
        }
    }
    char nested_source[4096];
    uint32_t length = (uint32_t)snprintf(nested_source, sizeof(nested_source), "enum { N=");
    for (uint32_t i = 0; i < 96; i += 1)
    {
        length += (uint32_t)snprintf(nested_source + length, sizeof(nested_source) - length, "sizeof(char[");
    }
    length += (uint32_t)snprintf(nested_source + length, sizeof(nested_source) - length, "1");
    for (uint32_t i = 0; i < 96; i += 1)
    {
        length += (uint32_t)snprintf(nested_source + length, sizeof(nested_source) - length, "])");
    }
    snprintf(nested_source + length, sizeof(nested_source) - length, " }; _Static_assert(N==1, \"nested type queries\");\n");
    char nested_attributes[8192];
    length = (uint32_t)snprintf(nested_attributes, sizeof(nested_attributes), "struct S { long value; }; enum { N=");
    for (uint32_t i = 0; i < 96; i += 1)
    {
        length += (uint32_t)snprintf(nested_attributes + length, sizeof(nested_attributes) - length, "sizeof(struct __attribute__((aligned(");
    }
    length += (uint32_t)snprintf(nested_attributes + length, sizeof(nested_attributes) - length, "8");
    for (uint32_t i = 0; i < 96; i += 1)
    {
        length += (uint32_t)snprintf(nested_attributes + length, sizeof(nested_attributes) - length, "))) S)");
    }
    snprintf(nested_attributes + length, sizeof(nested_attributes) - length, " }; _Static_assert(N==8, \"nested tag attribute jobs\");\n");
    char long_shuffle[4096];
    length = (uint32_t)snprintf(long_shuffle, sizeof(long_shuffle), "typedef int V __attribute__((vector_size(16))); V a; enum { N=sizeof(__builtin_shufflevector(a,a");
    for (uint32_t i = 0; i < 512; i += 1)
    {
        length += (uint32_t)snprintf(long_shuffle + length, sizeof(long_shuffle) - length, ",%u", i % 8);
    }
    snprintf(long_shuffle + length, sizeof(long_shuffle) - length, ")) }; _Static_assert(N==2048, \"long shuffle operand slice\");\n");
    char raw_shuffle[4096];
    length = (uint32_t)snprintf(raw_shuffle, sizeof(raw_shuffle), "typedef int V __attribute__((vector_size(16))); V a; typedef int T [[gnu::aligned(sizeof(__builtin_shufflevector(a,a");
    for (uint32_t i = 0; i < 512; i += 1)
    {
        length += (uint32_t)snprintf(raw_shuffle + length, sizeof(raw_shuffle) - length, ",%u", i % 8);
    }
    snprintf(raw_shuffle + length, sizeof(raw_shuffle) - length, ")))]]; _Static_assert(_Alignof(T)==2048, \"raw long shuffle operand slice\");\n");
    const CheckSourceFixture fixtures[] =
    {
        { .name = "deep selected choose expressions", .source = nested_selections[0] },
        { .name = "deep unselected choose expressions", .source = nested_selections[1] },
        { .name = "deep selected generic expressions", .source = nested_selections[2] },
        { .name = "deep unselected generic expressions", .source = nested_selections[3] },
        {
            .name = "nested selections retain prefix, infix and comma roots",
            .source = "enum { A=__builtin_choose_expr(1,-(2+3),0), B=_Generic(0,int:2*3+1,default:0), C=__builtin_choose_expr(0,0,_Generic(0,int:(2,7),default:0)) }; _Static_assert(A==-5 && B==7 && C==7,\"selection roots\");\n",
        },
        {
            .name = "nested selections retain vector values and lvalues",
            .source = "typedef int V __attribute__((vector_size(16))); V a,b; V f(void){return _Generic(0,int:__builtin_choose_expr(1,a,b),default:b);} int g(void){_Generic(0,int:__builtin_choose_expr(0,b,a),default:b)[0]=9; return a[0];}\n",
        },
        {
            .name = "unselected nested operands remain type checked in source order",
            .source = "int f(void){return __builtin_choose_expr(1,7,_Generic(0,int:missing,default:0));}\n",
            .rejected = true,
        },
        { .name = "bitcast declaration integer representation", .source = "enum { N=__builtin_bit_cast(int,0xffffffffu), P=__builtin_bit_cast(unsigned, -1) }; _Static_assert(N==-1 && P==0xffffffffu,\"integer representation\");\n" },
        { .name = "bitcast raw attribute is not an integer constant expression", .source = "typedef int T [[gnu::aligned(__builtin_bit_cast(unsigned,16))]];\n", .rejected = true },
        { .name = "bitcast half operand is not promoted", .source = "__fp16 x; enum { N=sizeof(__builtin_bit_cast(unsigned short,x)), M=sizeof(__builtin_bit_cast(__fp16,(unsigned short)0x3c00)) }; _Static_assert(N==2 && M==2,\"half representation\");\n" },
        { .name = "bitcast raw array operand is not decayed", .source = "int a[2]; enum { N=sizeof(__builtin_bit_cast(int[2],a)) }; _Static_assert(N==8,\"array representation\");\n" },
        { .name = "bitcast rejects mismatched raw sizes", .source = "enum { N=sizeof(__builtin_bit_cast(long,1)) };\n", .rejected = true },
        { .name = "bitcast rejects atomic source storage", .source = "_Atomic(int) x; enum { N=sizeof(__builtin_bit_cast(int,x)) };\n", .rejected = true },
        { .name = "bitcast rejects evaluated invalid bool representation", .source = "enum { N=__builtin_bit_cast(_Bool,(unsigned char)2) };\n", .rejected = true },
        { .name = "bitcast destination nested type job", .source = "enum { N=__builtin_bit_cast(__typeof__(__builtin_bit_cast(unsigned,1)),16) }; _Static_assert(N==16,\"nested bitcast\");\n" },
        { .name = "abstract function ABI prefix and grouped attributes", .source = "typedef int (__attribute__((ms_abi)) *M)(int); _Static_assert(__builtin_types_compatible_p(M,int __attribute__((ms_abi)) (*)(int)),\"prefix\"); typedef int (*N)(int) __attribute__((ms_abi)); _Static_assert(__builtin_types_compatible_p(M,N),\"suffix\"); _Static_assert(!__builtin_types_compatible_p(M,int(*)(int)),\"distinct ABI\");\n" },
        { .name = "abstract function ABI raw cast context", .source = "#define O __attribute__((overloadable))\nint f(int) O __attribute__((ms_abi)); double f(double) O; typedef int T [[gnu::aligned(sizeof((int (__attribute__((ms_abi)) *)(int))f))]]; _Static_assert(_Alignof(T)==8,\"raw ABI target\");\n" },
        { .name = "abstract function ABI preserves explicit typedef conflict", .source = "typedef int F(int) __attribute__((sysv_abi)); enum { N=sizeof(F __attribute__((ms_abi)) *) };\n", .rejected = true },
        { .name = "half storage type in declaration query", .source = "enum { N=sizeof(__fp16) }; _Static_assert(N==2,\"half storage\"); typedef int T [[gnu::aligned(sizeof(__fp16))]]; _Static_assert(_Alignof(T)==2,\"raw half\");\n" },
        { .name = "raw abstract function pointer query", .source = "typedef int T [[gnu::aligned(sizeof(int(*)(int)))]]; _Static_assert(_Alignof(T)==8,\"raw function pointer\");\n" },
        { .name = "raw abstract function query", .source = "typedef int T [[gnu::aligned(sizeof(int(int)))]]; _Static_assert(_Alignof(T)==1,\"raw function\");\n" },
        { .name = "constant query saturates floating casts only in proof", .source = "enum { A=__builtin_constant_p((int)1.0e100), B=__builtin_constant_p((int)1.0e100+1), C=__builtin_constant_p((unsigned)-1.0e100), D=__builtin_constant_p((int)__builtin_nan(\"\")), E=__builtin_constant_p(__builtin_popcount((int)1.0e100)), F=__builtin_constant_p(1?(int)1.0e100:0), G=__builtin_constant_p(_Generic(0,int:(int)1.0e100,default:0)), H=__builtin_constant_p((__int128)1.0e100+1) }; _Static_assert(A&&B&&C&&D&&E&&F&&G&&H,\"proof\"); int g(int x){_Static_assert(!__builtin_constant_p(x+++(int)1.0e100),\"effects\");return x;}\n" },
        { .name = "raw constant query proof propagates saturated conversion", .source = "typedef int T [[gnu::aligned(__builtin_constant_p((int)1.0e100+1)*16)]]; _Static_assert(_Alignof(T)==16,\"raw proof\");\n" },
        { .name = "overloaded argument cannot hide inside folded query", .source = "#define O __attribute__((overloadable))\nint f(int) O; double f(double) O;\n_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_constant_p(f)),int),\"ambiguous\");\n", .rejected = true },
        { .name = "earlier incomplete query precedes later missing name", .source = "struct S { char bytes[sizeof(struct Missing) -\nsizeof(missing)]; };\n", .rejected = true },
        { .name = "earlier missing name precedes later incomplete query", .source = "struct S { char bytes[sizeof(missing) -\nsizeof(struct Missing)]; };\n", .rejected = true },
        { .name = "raw earlier incomplete query precedes later missing name", .source = "typedef int T [[gnu::aligned(sizeof(struct Missing) +\nsizeof(missing))]];\n", .rejected = true },
        {
            .name = "declaration vector builtin exact type queries",
            .source = "typedef int I4 __attribute__((vector_size(16))); typedef float F4 __attribute__((vector_size(16))); I4 a; __typeof__(__builtin_convertvector(a,F4)) b; enum { N=sizeof(__builtin_elementwise_abs(a)), R=sizeof(__builtin_reduce_add(a)), C=sizeof(__builtin_convertvector(a,F4)) }; _Static_assert(N==16 && R==4 && C==16 && __builtin_types_compatible_p(__typeof__(b),F4), \"vector builtin types\");\n",
        },
        {
            .name = "declaration vector conversion nested destination query",
            .source = "typedef int I4 __attribute__((vector_size(16))); typedef float F4 __attribute__((vector_size(16))); I4 a; enum { N=sizeof(__builtin_convertvector(a,__typeof__(__builtin_convertvector(a,F4)))) }; _Static_assert(N==16, \"nested destination type job\");\n",
        },
        {
            .name = "declaration vector conversion attributed abstract destination",
            .source = "typedef int V __attribute__((vector_size(16))); V a; enum { N=sizeof(__builtin_convertvector(a,float __attribute__((vector_size(16))))) }; typedef int T [[gnu::aligned(sizeof(__builtin_convertvector(a,float __attribute__((vector_size(16))))))]]; _Static_assert(N==16 && _Alignof(T)==16, \"abstract vector destination\");\n",
        },
        {
            .name = "declaration vector builtin arbitrary argument slice",
            .source = long_shuffle,
        },
        {
            .name = "declaration vector shuffle GNU lexical index",
            .source = "typedef int V __attribute__((vector_size(16))); V a; enum { N=sizeof(__builtin_shufflevector(a,a,(2147483647+1)<0,1,2,3)) }; typedef int T [[gnu::aligned(sizeof(__builtin_shufflevector(a,a,(2147483647+1)<0,1,2,3)))]]; _Static_assert(N==16 && _Alignof(T)==16,\"GNU lexical shuffle index\");\n",
        },
        {
            .name = "raw attribute vector builtin arbitrary argument slice",
            .source = raw_shuffle,
        },
        {
            .name = "raw attribute vector builtin destination type",
            .source = "typedef int I4 __attribute__((vector_size(16))); typedef float F4 __attribute__((vector_size(16))); I4 a; typedef int T [[gnu::aligned(sizeof(__builtin_convertvector(a,F4)))]]; _Static_assert(_Alignof(T)==16, \"raw convert destination\");\n",
        },
        {
            .name = "raw attribute nested vector builtin destination type",
            .source = "typedef int I4 __attribute__((vector_size(16))); typedef float F4 __attribute__((vector_size(16))); I4 a; typedef int T [[gnu::aligned(sizeof(__builtin_convertvector(a,__typeof__(__builtin_convertvector(a,F4)))))]]; _Static_assert(_Alignof(T)==16, \"raw nested destination\");\n",
        },
        {
            .name = "declaration vector memory builtin type query",
            .source = "typedef int V __attribute__((vector_size(16))); V a; int n; enum { A=sizeof(__builtin_nontemporal_load(&a)), B=sizeof(__builtin_nontemporal_store(a,&a)), C=sizeof(__builtin_nondeterministic_value(++n)) }; _Static_assert(A==16 && B==1 && C==4, \"vector memory types\");\n",
        },
        {
            .name = "declaration vector conversion rejects lane count mismatch",
            .source = "typedef int V __attribute__((vector_size(16))); typedef double D __attribute__((vector_size(16))); V a; enum { N=sizeof(__builtin_convertvector(a,D)) };\n",
            .rejected = true,
        },
        {
            .name = "declaration vector conversion rejects scalar destination",
            .source = "typedef int V __attribute__((vector_size(16))); V a; enum { N=sizeof(__builtin_convertvector(a,int)) };\n",
            .rejected = true,
        },
        {
            .name = "declaration vector shuffle rejects out of range lane",
            .source = "typedef int V __attribute__((vector_size(16))); V a; enum { N=sizeof(__builtin_shufflevector(a,a,0,1,2,8)) };\n",
            .rejected = true,
        },
        {
            .name = "declaration vector shuffle rejects nonconstant lane",
            .source = "typedef int V __attribute__((vector_size(16))); V a; int i; enum { N=sizeof(__builtin_shufflevector(a,a,0,1,2,i)) };\n",
            .rejected = true,
        },
        {
            .name = "declaration sizeof arithmetic",
            .source = "struct Buffer { char spare[12 * sizeof(int) - 5 * sizeof(void *)]; }; _Static_assert(sizeof(struct Buffer)==8, \"bound\");\n",
        },
        {
            .name = "deeply nested tag attribute queries",
            .source = nested_attributes,
        },
        {
            .name = "tag attribute source ordered identity",
            .source = "enum { A=8, N=sizeof(struct __attribute__((aligned(A))) S*) }; _Static_assert(N==8, \"tag activation after attribute use\");\n",
        },
        {
            .name = "tag attribute nested new identity",
            .source = "enum { N=sizeof(struct __attribute__((aligned(sizeof(struct S*)))) S*) }; _Static_assert(N==8, \"nested tag identity\");\n",
        },
        {
            .name = "tag attribute invalid alignment",
            .source = "struct S { int x; }; enum { N=sizeof(struct __attribute__((aligned(3))) S) };\n",
            .rejected = true,
        },
        {
            .name = "tag attribute evaluated fault",
            .source = "struct S { int x; }; enum { N=sizeof(struct __attribute__((aligned(1/0))) S) };\n",
            .rejected = true,
        },
        {
            .name = "raw attribute nested tag query jobs",
            .source = "struct S { long x; }; typedef int T [[gnu::aligned(sizeof(struct __attribute__((aligned(sizeof(struct S)))) S))]]; _Static_assert(_Alignof(T)==8, \"raw nested tag attributes\");\n",
        },
        {
            .name = "nested type array bound",
            .source = "enum { N=sizeof(int[2+sizeof(char[3])]) }; _Static_assert(N==20, \"nested\");\n",
        },
        {
            .name = "integer rank and cast in array bound",
            .source = "int a[(unsigned char)258]; _Static_assert(sizeof a==8, \"cast\");\n",
        },
        {
            .name = "real literal immediate integer cast",
            .source = "enum { N=(int)3.75 }; int a[N]; _Static_assert(sizeof a==12, \"real cast\");\n",
        },
        {
            .name = "constant type names retain repeated modifier semantics",
            .source = "enum { A=(unsigned unsigned)3, B=sizeof(short short), C=sizeof(_Complex _Complex double), D=sizeof(_Atomic _Atomic int) }; _Static_assert(A==3 && B==2 && C==16 && D==4, \"duplicate modifiers\");\n",
        },
        {
            .name = "GNU real expression folds in integer constant context",
            .source = "enum { N=(int)(1.5+2.5) };\n",
        },
        {
            .name = "declaration conditional",
            .source = "enum { N=1?7:1/0 }; int a[N]; _Static_assert(sizeof a==28, \"conditional\");\n",
        },
        {
            .name = "declaration GNU omitted conditional",
            .source = "enum { N=7?:1/0 }; int a[N]; _Static_assert(sizeof a==28, \"conditional\");\n",
        },
        {
            .name = "declaration short circuit",
            .source = "enum { N=(0 && (1/0)) + (1 || (1/0)) }; int a[N]; _Static_assert(sizeof a==4, \"short circuit\");\n",
        },
        {
            .name = "declaration evaluated fault",
            .source = "enum { N=0?7:1/0 };\n",
            .rejected = true,
        },
        {
            .name = "declaration typedef and object source order",
            .source = "typedef short T; T object; enum { N=sizeof(T)+sizeof object }; int a[N]; _Static_assert(sizeof a==16, \"bindings\");\n",
        },
        {
            .name = "declaration prior enumerator",
            .source = "enum { A=3, B=sizeof(int[A]), C=(unsigned char)B }; _Static_assert(C==12, \"enumerator\");\n",
        },
        {
            .name = "declaration future enumerator rejected",
            .source = "enum { A=sizeof(int[B]), B=3 };\n",
            .rejected = true,
        },
        {
            .name = "declaration object shadow",
            .source = "int x; void f(void) { char x; enum { N=sizeof x }; _Static_assert(N==1, \"inner\"); } enum { N=sizeof x }; _Static_assert(N==4, \"outer\");\n",
        },
        {
            .name = "declaration object is not ICE",
            .source = "const int x=2; enum { N=x };\n",
            .rejected = true,
            .extension = true,
        },
        {
            .name = "declaration alignof callback array",
            .source = "enum { A=_Alignof(double), B=sizeof(int (*[2])(int)) }; _Static_assert(A==8 && B==16, \"types\");\n",
        },
        {
            .name = "declaration sizeof object expressions",
            .source = "struct S { int a[3]; }; struct S s; int f(int); enum { A=sizeof s.a[1], B=sizeof f(3), C=sizeof(1/0) }; _Static_assert(A+B+C==12, \"unevaluated\");\n",
        },
        {
            .name = "declaration GNU void and function queries",
            .source = "int f(void); enum { A=sizeof(void), B=sizeof f, C=__alignof__(void), D=__alignof__(f) }; _Static_assert(A==1 && B==1 && C==1 && D==4, \"GNU queries\");\n",
        },
        {
            .name = "declaration sizeof pointer member",
            .source = "struct S { int a[3]; }; enum { A=sizeof(((struct S*)0)->a) }; _Static_assert(A==12, \"member\");\n",
        },
        {
            .name = "declaration sizeof comma expression",
            .source = "enum { A=sizeof((1,2.0)) }; _Static_assert(A==8, \"comma\");\n",
        },
        {
            .name = "declaration unknown sizeof object",
            .source = "enum { A=sizeof missing };\n",
            .rejected = true,
        },
        {
            .name = "declaration string query",
            .source = "enum { A=sizeof(\"ab\" \"cd\"), B=sizeof(L\"hi\"), C=sizeof(u\"hi\") }; _Static_assert(A==5 && B==12 && C==6, \"strings\");\n",
        },
        {
            .name = "declaration bit field sizeof rejected",
            .source = "struct S { int x:3; }; struct S s; enum { A=sizeof s.x };\n",
            .rejected = true,
        },
        {
            .name = "declaration object alignment",
            .source = "int x __attribute__((aligned(32))); enum { A=__alignof__(x), B=__alignof__((x)) }; _Static_assert(A==32 && B==32, \"object alignment\");\n",
        },
        {
            .name = "declaration bit field alignof rejected",
            .source = "struct S { int x:3; }; struct S s; enum { A=__alignof__(s.x) };\n",
            .rejected = true,
        },
        {
            .name = "declaration incomplete sizeof rejected",
            .source = "struct S; enum { A=sizeof(struct S) };\n",
            .rejected = true,
        },
        {
            .name = "declaration type compatibility",
            .source = "typedef int I; enum { A=__builtin_types_compatible_p(const I,int), B=__builtin_types_compatible_p(int*,const int*) }; _Static_assert(A==1 && B==0, \"compatibility\");\n",
        },
        {
            .name = "declaration nested typeof",
            .source = "int x; enum { A=sizeof(__typeof__(x)), B=sizeof(__typeof__(int[3])), C=sizeof(__typeof__((x+1))) }; _Static_assert(A==4 && B==12 && C==4, \"typeof\");\n",
        },
        {
            .name = "declaration typeof preserves array",
            .source = "int a[3]; enum { N=sizeof(__typeof__(a)) }; _Static_assert(N==12, \"typeof array\");\n",
        },
        {
            .name = "typeof statement expression effects are unevaluated",
            .source = "int f(int n) { int x=0; __typeof__(({x+=50;n;})) t=1; _Static_assert(sizeof t==sizeof n, \"statement result\"); return x+t; }\n",
        },
        {
            .name = "nested statement expression type query",
            .source = "int f(int n) { __typeof__(({({n;});})) t=1; _Static_assert(sizeof t==sizeof n, \"nested statement result\"); return t; }\n",
        },
        {
            .name = "statement expression local declaration type query",
            .source = "int f(int n) { __typeof__(({int x=n; x;})) t=1; _Static_assert(sizeof t==sizeof n, \"local result\"); return t; }\n",
        },
        {
            .name = "statement expression multiple declarator type query",
            .source = "int f(int n) { __typeof__(({int x=n,y=x+1; y;})) t=1; return t; }\n",
        },
        {
            .name = "statement expression nested declaration type query",
            .source = "int f(int n) { __typeof__(({int x=n; __typeof__(({long y=x; y;})) z=1; z;})) t=1; _Static_assert(sizeof t==sizeof(long), \"nested local result\"); return t; }\n",
        },
        {
            .name = "statement expression aggregate declaration type query",
            .source = "int f(int n) { __typeof__(({int x[2]={n,n+1}; x[0];})) t=1; return t; }\n",
        },
        {
            .name = "statement expression control type query",
            .source = "int f(int n) { __typeof__(({int x=n; if(x){x++;} else x--; while(x)x--; x;})) t=1; return t; }\n",
        },
        {
            .name = "statement expression loop type query",
            .source = "int f(int n) { __typeof__(({int x=n; for(int i=0;i<2;i++)x++; do { x--; } while(x); x;})) t=1; return t; }\n",
        },
        {
            .name = "statement expression nested block type query",
            .source = "int f(int n) { __typeof__(({int x=n; {int y=x; x=y;} x;})) t=1; return t; }\n",
        },
        {
            .name = "statement expression control constraints remain checked",
            .source = "struct S{int x;}; int f(void) { __typeof__(({struct S s; if(s){} 1;})) t=1; return t; }\n",
            .rejected = true,
        },
        {
            .name = "statement expression final declaration has void type",
            .source = "int f(int n) { __typeof__(({int x=n;})) t; return 0; }\n",
            .rejected = true,
        },
        {
            .name = "statement expression initializer constraints remain checked",
            .source = "int f(void) { __typeof__(({int *x=1; x;})) t=0; return 0; }\n",
            .rejected = true,
        },
        {
            .name = "statement expression initializer reports its actual line",
            .source = "int f(void) {\n __typeof__(({\n int *x=1;\n x;\n })) t=0;\n return 0;\n}\n",
            .rejected = true,
        },
        {
            .name = "statement expression size is constant",
            .source = "void f(int n) { enum { N=sizeof(({n++;n;})) }; _Static_assert(N==sizeof n, \"unevaluated statement size\"); }\n",
        },
        {
            .name = "GNU statement expression folds in integer constant context",
            .source = "void f(void) { enum { N=({3;}) }; }\n",
        },
        {
            .name = "declaration atomic type names",
            .source = "enum { A=sizeof(_Atomic(int)), B=sizeof(_Atomic int), C=sizeof(const _Atomic(int)), D=sizeof(int * _Atomic) }; _Static_assert(A==4 && B==4 && C==4 && D==8, \"atomic\");\n",
        },
        {
            .name = "declaration invalid atomic type",
            .source = "enum { A=sizeof(_Atomic(const int)) };\n",
            .rejected = true,
        },
        {
            .name = "declaration complex type names",
            .source = "enum { A=sizeof(double _Complex), B=sizeof(_Complex), C=sizeof(long double _Complex), D=sizeof(_Complex long) }; _Static_assert(A==16 && B==16 && C==32 && D==16, \"complex\");\n",
        },
        {
            .name = "declaration bit integer type name",
            .source = "enum { A=sizeof(unsigned _BitInt(17)), B=sizeof(_BitInt(1+16)), C=sizeof(unsigned _BitInt(1)) }; _Static_assert(A==4 && B==4 && C==1, \"bit int\");\n",
        },
        {
            .name = "declaration named callback type",
            .source = "enum { A=sizeof(int (*)(register int x,int y)) }; _Static_assert(A==8, \"callback\");\n",
        },
        {
            .name = "declaration duplicate callback parameter",
            .source = "enum { A=sizeof(int (*)(int x,int x)) };\n",
            .rejected = true,
        },
        {
            .name = "type name duplicate separated by unnamed parameters",
            .source = "enum { A=sizeof(int (*)(int x,int,int,int x)) };\n",
            .rejected = true,
        },
        {
            .name = "type name nested prototypes reuse outer names",
            .source = "enum { A=sizeof(int (*)(int x,int (*cb)(int x,int y),int y)) }; _Static_assert(A==8,\"nested scopes\");\n",
        },
        {
            .name = "type name sibling prototypes reuse names",
            .source = "enum { A=sizeof(int (*)(int (*a)(int x,int y),int (*b)(int x,int y))) }; _Static_assert(A==8,\"sibling scopes\");\n",
        },
        {
            .name = "type name outer duplicate survives nested prototype",
            .source = "enum { A=sizeof(int (*)(int x,int (*cb)(int x),int x)) };\n",
            .rejected = true,
        },
        {
            .name = "type name nested prototype duplicate",
            .source = "enum { A=sizeof(int (*)(int x,int (*cb)(int y,int y),int z)) };\n",
            .rejected = true,
        },
        {
            .name = "separate type names reuse prototype names",
            .source = "enum { A=sizeof(int (*)(int x,int y))+sizeof(int (*)(int x,int y)) }; _Static_assert(A==16,\"separate prototypes\");\n",
        },
        {
            .name = "nested type name parameter does not hide outer typedef",
            .source = "typedef int T; enum { A=sizeof(int (*)(int (*cb)(int T),T x)) }; _Static_assert(A==8,\"typedef restored\");\n",
        },
        {
            .name = "type name unnamed parameters do not collide",
            .source = "enum { A=sizeof(int (*)(int,int,int,int)) }; _Static_assert(A==8,\"unnamed parameters\");\n",
        },
        {
            .name = "type name duplicate precedes later invalid declaration",
            .source = "enum { A=sizeof(int (*)(int x,int x)) };\nint f(void){return missing;}\n",
            .rejected = true,
        },
        {
            .name = "type name invalid parameter precedes later duplicate",
            .source = "enum { A=sizeof(int (*)(void bad,\nint x,int x)) };\n",
            .rejected = true,
        },
        {
            .name = "body expression named callback type",
            .source = "unsigned long f(void){return sizeof(int (*)(int p0,int p1,int p2,int p3));}\n",
        },
        {
            .name = "declaration member offset",
            .source = "struct S { char c; int a[3]; }; enum { A=__builtin_offsetof(struct S,a[2]) }; _Static_assert(A==12, \"offset\");\n",
        },
        {
            .name = "declaration nested member offset",
            .source = "struct I { char c; int x; }; struct S { int n; struct I a[2]; }; enum { A=__builtin_offsetof(struct S,a[1].x) }; _Static_assert(A==16, \"offset\");\n",
        },
        {
            .name = "declaration bit field offsetof rejected",
            .source = "struct S { unsigned x:3; }; enum { A=__builtin_offsetof(struct S,x) };\n",
            .rejected = true,
        },
        {
            .name = "declaration missing offsetof member",
            .source = "struct S { int x; }; enum { A=__builtin_offsetof(struct S,missing) };\n",
            .rejected = true,
        },
        {
            .name = "source type compatibility builtin",
            .source = "typedef int I; _Static_assert(__builtin_types_compatible_p(const I,int), \"same\"); _Static_assert(!__builtin_types_compatible_p(int*,const int*), \"different\");\n",
        },
        {
            .name = "source member offset builtin",
            .source = "struct S { char c; int a[3]; }; _Static_assert(__builtin_offsetof(struct S,a[2])==12, \"offset\");\n",
        },
        {
            .name = "source member offset binding identity",
            .source = "enum { N=1 }; struct S { int a[3]; }; int f(void) { enum { N=2 }; return __builtin_offsetof(struct S,a[N]); } _Static_assert(__builtin_offsetof(struct S,a[N])==4, \"outer enum\");\n",
        },
        {
            .name = "source anonymous record offset retains member typedef identity",
            .source = "typedef long Time; int f(int select) { return select ? sizeof(Time) : __builtin_offsetof(struct { char c; Time value; },value); } _Static_assert(__builtin_offsetof(struct {char c; Time value;},value)==8, \"anonymous offset\");\n",
        },
        {
            .name = "source anonymous record offset retains nested layout",
            .source = "typedef short Element; enum { N=3 }; _Static_assert(__builtin_offsetof(struct {char c; struct {Element a[N]; long x;} inner;},inner.x)==16, \"nested offset\");\n",
        },
        {
            .name = "initializer designator bindings remain source ordered",
            .source = "enum { A=1, B=3, V=7 }; int values[]={[A]=V,[B]=V+1}; _Static_assert(sizeof values==16, \"inferred after initializer\"); int f(void){enum {A=2,B=4}; int local[]={[A]=V,[B]=V}; return sizeof local==20;}\n",
        },
        {
            .name = "incomplete array remains incomplete inside its own initializer",
            .source = "int values[]={1,\nsizeof values};\n",
            .rejected = true,
        },
        {
            .name = "declaration iterative nested type queries",
            .source = nested_source,
        },
        {
            .name = "declaration generic selection",
            .source = "enum { N=_Generic(1,int:7,default:1/0) }; _Static_assert(N==7, \"generic\");\n",
        },
        {
            .name = "generic aligned nominal duplicate",
            .source = "struct S{int x;}; typedef struct S T __attribute__((aligned(16))); int f(struct S x){return _Generic(x,struct S:1,T:2);}\n",
            .rejected = true,
        },
        {
            .name = "generic enum underlying duplicate",
            .source = "enum E{E0}; enum{N=_Generic(0,enum E:1,unsigned int:2,default:0)};\n",
            .rejected = true,
        },
        {
            .name = "generic compatible record array pointers",
            .source = "struct S{int x;}; int f(void){return _Generic(0,struct S(*)[]:1,struct S(*)[3]:2,default:0);}\n",
            .rejected = true,
        },
        {
            .name = "generic compatible function pointers",
            .source = "enum{N=_Generic(0,int(*)():1,int(*)(double):2,default:0)};\n",
            .rejected = true,
        },
        {
            .name = "generic nominal qualifiers retain exact checks",
            .source = "struct S; typedef struct S *P; enum{N=_Generic((P)0,const struct S*:1,P:7)}; _Static_assert(N==7,\"selected\"); int f(P x){return _Generic(x,const struct S*:1,P:7);}\n",
        },
        {
            .name = "generic distinct pointer shapes share a nominal key",
            .source = "struct S{int x;}; int f(struct S*x){return _Generic(x,struct S*:7,struct S**:2,struct S(*)[3]:3);}\n",
        },
        {
            .name = "generic transparent function parameter duplicate",
            .source = "struct S; union U{struct S*p;void*q;} __attribute__((transparent_union)); int f(void){return _Generic(0,int(*)(union U):1,int(*)(struct S*):2,default:0);}\n",
            .rejected = true,
        },
        {
            .name = "generic later transparent attribute is inactive",
            .source = "struct S; union U{struct S*p;void*q;}; enum{N=_Generic(0,int(*)(union U):1,int(*)(struct S*):2,default:7)}; typedef union U TU __attribute__((transparent_union)); _Static_assert(N==7,\"source time\");\n",
        },
        {
            .name = "declaration nested generic selection",
            .source = "typedef short T; enum { N=_Generic((T)0,T:_Generic(1.0,double:3,default:8),default:9) }; _Static_assert(N==3, \"nested generic\");\n",
        },
        {
            .name = "declaration generic duplicate association",
            .source = "typedef int I; enum { N=_Generic(1,int:2,I:3) };\n",
            .rejected = true,
        },
        {
            .name = "declaration generic duplicate default",
            .source = "enum { N=_Generic(1,default:2,default:3) };\n",
            .rejected = true,
        },
        {
            .name = "declaration generic no match",
            .source = "enum { N=_Generic(1,double:2) };\n",
            .rejected = true,
        },
        {
            .name = "declaration generic incomplete association",
            .source = "struct S; enum { N=_Generic(1,int:1,struct S:2) }; struct S { int x; };\n",
            .rejected = true,
            .extension = true,
        },
        {
            .name = "declaration GNU choose expression",
            .source = "enum { N=__builtin_choose_expr(1,7,1/0) }; _Static_assert(N==7, \"choose\");\n",
        },
        {
            .name = "declaration GNU choose retains wrapped ICE form",
            .source = "enum { A=__builtin_choose_expr(1<<32,7,0), B=__builtin_choose_expr(1<<-1,0,7), C=__builtin_choose_expr(2147483647+1,7,0) }; _Static_assert(A==7 && B==7 && C==7, \"wrapped ICE\");\n",
        },
        {
            .name = "declaration GNU choose rejects computed floating cast",
            .source = "enum { N=__builtin_choose_expr((int)(1.5+2.5),7,0) };\n",
            .rejected = true,
        },
        {
            .name = "declaration GNU choose rejects comma with wrapped integer",
            .source = "enum { N=__builtin_choose_expr((0,1<<32),7,0) };\n",
            .rejected = true,
        },
        {
            .name = "declaration GNU choose rejects statement with wrapped integer",
            .source = "void f(void) { enum { N=__builtin_choose_expr(({1<<32;}),7,0) }; }\n",
            .rejected = true,
        },
        {
            .name = "declaration GNU choose wide integer condition",
            .source = "enum { N=__builtin_choose_expr(((__int128)1<<100)|1,7,0) }; _Static_assert(N==7, \"wide condition\");\n",
        },
        {
            .name = "declaration GNU choose full-width truth oracle divergence",
            .source = wide_truth_source,
            .extension = true,
        },
        {
            .name = "declaration GNU choose nonconstant condition",
            .source = "int x; enum { N=__builtin_choose_expr(x,7,2) };\n",
            .rejected = true,
        },
        {
            .name = "generic exact selected lvalue",
            .source = "int x; int f(void) { _Generic(1,int:x,default:0)=3; return x; }\n",
        },
        {
            .name = "generic array and function conversion",
            .source = "int a[2]; int f(void); _Static_assert(_Generic(a,int*:1,default:0), \"array\"); _Static_assert(_Generic(f,int(*)(void):1,default:0), \"function\");\n",
        },
        {
            .name = "generic qualifier conversion",
            .source = "const int x=1; _Atomic(int) a; _Static_assert(_Generic(x,int:1,default:0), \"const\"); _Static_assert(_Generic(a,int:1,default:0), \"atomic\");\n",
        },
        {
            .name = "generic exact binding after scopes close",
            .source = "int x; int f(void) { double x; _Static_assert(_Generic(x,double:1,default:0), \"inner identity\"); return 1; } _Static_assert(_Generic(x,int:1,default:0), \"outer identity\");\n",
        },
        {
            .name = "generic source type compatibility duplicate",
            .source = "typedef int I; int f(void) { return _Generic(1,int:1,I:2); }\n",
            .rejected = true,
        },
        {
            .name = "generic source incomplete association",
            .source = "struct S; int f(void) { return _Generic(1,int:1,struct S:2); } struct S { int x; };\n",
            .rejected = true,
            .extension = true,
        },
        {
            .name = "generic rejects invalid unselected expression",
            .source = "int f(void) { return _Generic(1,int:1,default:missing); }\n",
            .rejected = true,
        },
        {
            .name = "GNU choose exact selected array",
            .source = "int a[3]; _Static_assert(sizeof(__builtin_choose_expr(1,a,0))==12, \"array\");\n",
        },
        {
            .name = "GNU choose exact selected lvalue",
            .source = "int x; int f(void) { __builtin_choose_expr(1,x,0)=3; return x; }\n",
        },
        {
            .name = "GNU choose rejected nonconstant source condition",
            .source = "int f(int x) { return __builtin_choose_expr(x,1,2); }\n",
            .rejected = true,
        },
        {
            .name = "generic selected fault",
            .source = "_Static_assert(_Generic(1,int:1/0,default:1), \"fault\");\n",
            .rejected = true,
        },
        {
            .name = "generic unselected fault",
            .source = "_Static_assert(_Generic(1,int:1,default:1/0), \"selected\");\n",
        },
        {
            .name = "declaration integer builtin constant",
            .source = "enum { A=__builtin_popcount(7), B=__builtin_clz(1), C=__builtin_ctz(8), D=__builtin_parity(7), E=__builtin_ffs(8) }; _Static_assert(A==3 && B==31 && C==3 && D==1 && E==4, \"integer builtins\");\n",
        },
        {
            .name = "declaration builtin array bound",
            .source = "int a[__builtin_ctz(8)]; _Static_assert(sizeof a==12, \"builtin bound\");\n",
        },
        {
            .name = "declaration nested builtin constant",
            .source = "enum { A=__builtin_popcount(__builtin_bswap32(0x10203040)), B=__builtin_bswap16(0x1234) }; _Static_assert(A==5 && B==0x3412, \"nested builtin\");\n",
        },
        {
            .name = "declaration builtin converted constants",
            .source = "enum { A=__builtin_popcount(1.5), B=__builtin_popcount((1,7)) }; _Static_assert(A==1 && B==3, \"builtin conversions\");\n",
        },
        {
            .name = "declaration builtin invalid zero evaluated",
            .source = "enum { A=__builtin_ctz(0) };\n",
            .rejected = true,
        },
        {
            .name = "declaration builtin invalid zero unevaluated",
            .source = "enum { A=sizeof(__builtin_ctz(0)), B=1?3:__builtin_clz(0) }; _Static_assert(A==4 && B==3, \"builtin faults\");\n",
        },
        {
            .name = "declaration builtin incorrect arity",
            .source = "enum { A=__builtin_popcount(1,2) };\n",
            .rejected = true,
        },
        {
            .name = "declaration overflow builtin sizeof",
            .source = "int x; enum { A=sizeof(__builtin_add_overflow(1,2,&x)) }; _Static_assert(A==1, \"overflow type\");\n",
        },
        {
            .name = "declaration overflow builtin is not ICE",
            .source = "int x; enum { A=__builtin_add_overflow(1,2,&x) };\n",
            .rejected = true,
        },
        {
            .name = "C23 raw attribute arithmetic",
            .source = "typedef int T [[gnu::aligned(1 << 4)]]; _Static_assert(_Alignof(T)==16, \"raw arithmetic\");\n",
        },
        {
            .name = "C23 raw attribute enum binding",
            .source = "enum { A=4 }; typedef int T [[gnu::aligned(A << 2)]]; _Static_assert(_Alignof(T)==16, \"raw binding\");\n",
        },
        {
            .name = "C23 raw attribute cast and conditional",
            .source = "typedef int T [[gnu::aligned((unsigned)(1?16:1/0))]]; _Static_assert(_Alignof(T)==16, \"raw conditional\");\n",
        },
        {
            .name = "C23 raw attribute nested query",
            .source = "typedef int T [[gnu::aligned(sizeof(int[2+sizeof(char[2])]))]]; _Static_assert(_Alignof(T)==16, \"raw type\");\n",
        },
        {
            .name = "C23 raw attribute typedef query",
            .source = "typedef long double T; typedef int I [[gnu::aligned(sizeof(T))]]; _Static_assert(_Alignof(I)==16, \"raw typedef\");\n",
        },
        {
            .name = "C23 raw attribute typeof query",
            .source = "typedef int T [[gnu::aligned(sizeof(__typeof__(long double)))]]; _Static_assert(_Alignof(T)==16, \"raw typeof\");\n",
        },
        {
            .name = "C23 raw attribute object alignment",
            .source = "int x __attribute__((aligned(32))); typedef int T [[gnu::aligned(__alignof__((x)))]]; _Static_assert(_Alignof(T)==32, \"raw object\");\n",
        },
        {
            .name = "C23 raw attribute integer builtin",
            .source = "typedef int T [[gnu::aligned(1 << __builtin_ctz(16))]]; _Static_assert(_Alignof(T)==16, \"raw builtin\");\n",
        },
        {
            .name = "C23 raw attribute invalid alignment",
            .source = "int x [[gnu::aligned(1+2)]];\n",
            .rejected = true,
        },
        {
            .name = "C23 raw attribute evaluated fault",
            .source = "int x [[gnu::aligned(0?16:1/0)]];\n",
            .rejected = true,
        },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-checker-constant-XXXXXX";
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
    check_test_expect(test, ready, "declaration-time constant Clang oracle could not run");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        char description[192];
        snprintf(description, sizeof(description), "%s: expectation differs from Clang", fixtures[i].name);
        if (fixtures[i].source == wide_truth_source)
        {
            printf("test-checker: full-width choose_expr oracle probe: Clang %s the GCC-confirmed checker regression\n", outcomes[i].rejected ? "rejects" : "accepts");
        }
        else
        {
            check_test_expect(test, outcomes[i].rejected == (fixtures[i].rejected && !fixtures[i].extension), description);
        }
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

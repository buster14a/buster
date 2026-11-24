// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1372's access units: a bit-field's read, store or update through `.` or `->` loads and stores the access unit clang
// gives its run of bit-fields (check_lower_bit_unit_place), so that the fields that share one are all accessed at one
// address and width, and a read after a store forwards from it. Each fixture function's lowered entries, by both paths,
// are evaluated over symbolic addresses (check_test_bit_trace): every LOAD and SET of an integer class through its
// record's address gives an offset and a width, and the distinct ones, in the order they first come, must be the unit's,
// as clang 23's -O0 code reads and writes them. Units that are no integer (a packed run, a run before a member in
// what would be the integer's last byte) are accessed as pieces of their bytes; a union's bit-fields, past the units'
// model, keep the bytes that hold each field's bits. The constant addresses of a local's and a global's bit-fields
// (CheckValue.offset, a field's first byte) are their units' too (check_lower_bit_constant_address). Tier 0 compiles each
// function by both its paths to the same records. Natively against clang: the fixtures' records written, updated and
// read, with their neighbors.

// A value of the evaluation: a constant (`base` 1), an address `offset` bytes past base `base` (2 or more: a local's
// value on entry, a local's address or a symbol's), or another (0), which it doesn't follow.
typedef struct CheckTestBitValue CheckTestBitValue;
struct CheckTestBitValue
{
    uint64_t base;
    int64_t offset;
};

#define BUSTER_CHECK_TEST_BIT_CONSTANT 1u
#define BUSTER_CHECK_TEST_BIT_ENTRY 2u
#define BUSTER_CHECK_TEST_BIT_FRAME (UINT64_C(1) << 32)
#define BUSTER_CHECK_TEST_BIT_SYMBOL (UINT64_C(1) << 48)

// A function's accesses: the distinct reads ("r", offset, ':', bytes) and writes ("w") of an integer class through one
// base, in the order they first come, separated by spaces; "?" for an access through another base or none followed, and
// "!" for an entry this straight-line evaluation doesn't take (a call, a branch).
typedef struct CheckTestBitTrace CheckTestBitTrace;
struct CheckTestBitTrace
{
    char text[160];
    uint32_t accesses;
};

static uint32_t check_test_bit_class_bytes(uint8_t class)
{
    uint32_t bytes = 0;
    switch (class)
    {
        case BUSTER_TYPED_CLASS_I8: case BUSTER_TYPED_CLASS_U8: bytes = 1; break;
        case BUSTER_TYPED_CLASS_I16: case BUSTER_TYPED_CLASS_U16: bytes = 2; break;
        case BUSTER_TYPED_CLASS_I32: case BUSTER_TYPED_CLASS_U32: bytes = 4; break;
        case BUSTER_TYPED_CLASS_I64: case BUSTER_TYPED_CLASS_U64: bytes = 8; break;
        default: break;
    }
    return bytes;
}

static void check_test_bit_append(CheckTestBitTrace* trace, const char* token)
{
    const size_t length = strlen(trace->text);
    const size_t needed = strlen(token) + (length != 0 ? 1 : 0);
    if (length + needed < sizeof(trace->text))
    {
        snprintf(trace->text + length, sizeof(trace->text) - length, "%s%s", length != 0 ? " " : "", token);
    }
}

// The access of `bytes` bytes at `address`: its token added where it is new, through the first base seen.
static void check_test_bit_access(CheckTestBitTrace* trace, uint64_t* base, CheckTestBitValue address, uint32_t bytes, bool write)
{
    char token[32];
    const bool followed = address.base >= BUSTER_CHECK_TEST_BIT_ENTRY && (*base == 0 || *base == address.base);
    *base = followed && *base == 0 ? address.base : *base;
    if (followed)
    {
        snprintf(token, sizeof(token), "%c%lld:%u", write ? 'w' : 'r', (long long)address.offset, bytes);
    }
    else
    {
        snprintf(token, sizeof(token), "?");
    }
    // A token is new where no token of the trace, between spaces, is the same.
    bool seen = false;
    const size_t length = strlen(token);
    for (const char* at = trace->text; *at != 0 && !seen;)
    {
        const char* end = strchr(at, ' ');
        const size_t span = end != NULL ? (size_t)(end - at) : strlen(at);
        seen = span == length && memcmp(at, token, length) == 0;
        at += span + (end != NULL ? 1 : 0);
    }
    if (!seen)
    {
        check_test_bit_append(trace, token);
    }
    trace->accesses += 1;
}

static CheckTestBitTrace check_test_bit_trace(const Tier0Function* function, Arena* arena)
{
    CheckTestBitTrace trace = { 0 };
    CheckTestBitValue* stack = BUSTER_ARENA_PUSH_ARRAY(arena, CheckTestBitValue, (uint64_t)function->count + 1);
    CheckTestBitValue* locals = BUSTER_ARENA_PUSH_ARRAY(arena, CheckTestBitValue, (uint64_t)function->local_count + 1);
    bool valid = stack != NULL && locals != NULL;
    for (uint32_t l = 0; valid && l < function->local_count; l += 1)
    {
        locals[l] = (CheckTestBitValue){ .base = BUSTER_CHECK_TEST_BIT_ENTRY + l, .offset = 0 };
    }
    uint64_t base = 0;
    uint32_t depth = 0;
    const CheckTestBitValue other = { 0 };
    for (uint32_t i = 0; valid && i < function->count; i += 1)
    {
        const uint8_t kind = function->kinds[i];
        const uint8_t class = function->classes[i];
        const uint64_t value = function->values[i];
        const bool wide = class == BUSTER_TYPED_CLASS_I64 || class == BUSTER_TYPED_CLASS_U64;
        // The operands this entry pops, and whether it pushes a result.
        uint32_t pops = 0;
        bool pushes = false;
        switch (kind)
        {
            case BUSTER_TYPED_CONST: case BUSTER_TYPED_LOCAL: case BUSTER_TYPED_LOCAL_ADDRESS: case BUSTER_TYPED_GLOBAL: case BUSTER_TYPED_GLOBAL_ADDRESS:
            case BUSTER_TYPED_DUP: pushes = true; break;
            case BUSTER_TYPED_NEG: case BUSTER_TYPED_NOT: case BUSTER_TYPED_CONVERT: case BUSTER_TYPED_LOAD: case BUSTER_TYPED_STORE_LOCAL:
            case BUSTER_TYPED_STORE_GLOBAL: pops = 1; pushes = true; break;
            case BUSTER_TYPED_SET_LOCAL: case BUSTER_TYPED_SET_GLOBAL: case BUSTER_TYPED_DISCARD: pops = 1; break;
            case BUSTER_TYPED_RETURN: pops = class != BUSTER_TYPED_CLASS_VOID ? 1 : 0; break;
            case BUSTER_TYPED_STORE: pops = 2; pushes = true; break;
            case BUSTER_TYPED_SET: pops = 2; break;
            default:
                pops = 2;
                pushes = true;
                valid = (kind >= BUSTER_TYPED_ADD && kind <= BUSTER_TYPED_PTR_ADD) || (kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE);
                break;
        }
        valid = valid && depth >= pops + (kind == BUSTER_TYPED_DUP ? 1u : 0u);
        if (valid)
        {
            const CheckTestBitValue last = pops >= 1 ? stack[depth - 1] : other;
            const CheckTestBitValue first = pops >= 2 ? stack[depth - 2] : last;
            depth -= pops;
            CheckTestBitValue result = other;
            if (kind == BUSTER_TYPED_CONST)
            {
                result = (CheckTestBitValue){ .base = BUSTER_CHECK_TEST_BIT_CONSTANT, .offset = (int64_t)value };
            }
            else if (kind == BUSTER_TYPED_LOCAL)
            {
                result = value < function->local_count ? locals[value] : other;
            }
            else if (kind == BUSTER_TYPED_LOCAL_ADDRESS)
            {
                result = (CheckTestBitValue){ .base = BUSTER_CHECK_TEST_BIT_ENTRY + BUSTER_CHECK_TEST_BIT_FRAME + value, .offset = 0 };
            }
            else if (kind == BUSTER_TYPED_GLOBAL_ADDRESS)
            {
                result = (CheckTestBitValue){ .base = BUSTER_CHECK_TEST_BIT_ENTRY + BUSTER_CHECK_TEST_BIT_SYMBOL + value, .offset = 0 };
            }
            else if (kind == BUSTER_TYPED_DUP)
            {
                result = stack[depth - 1];
            }
            else if (kind == BUSTER_TYPED_CONVERT)
            {
                result = wide ? last : other;
            }
            else if (kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL)
            {
                if (value < function->local_count)
                {
                    locals[value] = last;
                }
                result = last;
            }
            else if (kind == BUSTER_TYPED_STORE_GLOBAL)
            {
                result = last;
            }
            else if ((kind == BUSTER_TYPED_PTR_ADD || (kind == BUSTER_TYPED_ADD && wide)) && first.base >= BUSTER_CHECK_TEST_BIT_ENTRY &&
                     last.base == BUSTER_CHECK_TEST_BIT_CONSTANT)
            {
                const int64_t scale = kind == BUSTER_TYPED_PTR_ADD ? (int64_t)value : 1;
                result = (CheckTestBitValue){ .base = first.base, .offset = first.offset + last.offset * scale };
            }
            const uint32_t bytes = check_test_bit_class_bytes(class);
            if (kind == BUSTER_TYPED_LOAD && bytes != 0)
            {
                check_test_bit_access(&trace, &base, last, bytes, false);
            }
            if ((kind == BUSTER_TYPED_STORE || kind == BUSTER_TYPED_SET) && bytes != 0)
            {
                check_test_bit_access(&trace, &base, first, bytes, true);
            }
            if (pushes)
            {
                stack[depth] = result;
                depth += 1;
            }
        }
    }
    if (!valid)
    {
        check_test_bit_append(&trace, "!");
    }
    return trace;
}

static void check_test_program_bit_units(CheckTest* test, const char* clang)
{
    // The records, then the functions in source order, each with what it must lower to.
    static const char records[] = "struct B{unsigned x:3,y:7,z:13,w:9;};struct G{unsigned x:3;char guard;};struct T{unsigned a:24;char c;};"
                                  "struct __attribute__((packed)) P{unsigned char k:4;unsigned long long x:44;};struct Q{unsigned a:8,b:8,c:8;};"
                                  "struct U3{unsigned char a:8,b:8,c:8;};union V{unsigned a:3;unsigned b:12;};"
                                  "struct A{char c;struct{unsigned x:4,y:12;};char d;};struct N{int i;struct B b;};struct C{unsigned a:8,b:8,c:16;};"
                                  "struct C gc;struct F{unsigned a:24;char tail[];};struct E{};struct M{unsigned a:24;struct E e;};"
                                  "struct __attribute__((packed)) W{char c;unsigned:3;long long f:63;};"
                                  "struct __attribute__((packed)) X{char a:3;unsigned long long g:64;};"
                                  "union U17{unsigned x:3;unsigned y:17;unsigned long long z:40;};union __attribute__((packed)) U3B{unsigned x:17;};"
                                  "struct __attribute__((ms_struct)) MS{unsigned x:3;unsigned y:7;unsigned short z:5;unsigned char w:4;};"
                                  "struct __attribute__((packed)) R{char c;unsigned long long a:60;unsigned b:20;};"
                                  "struct __attribute__((packed)) Q12{unsigned a:30,b:30,c:30;};";
    static const char functions[] = "unsigned issue(struct B*b){b->y+=b->x;b->z^=b->w;return b->x+b->y+b->z+b->w;}"
                                    "void guard(struct G*p,unsigned v){p->x=v;p->x++;}"
                                    "void clipped(struct T*p){p->a+=1;}"
                                    "void packed(struct P*p){p->x+=p->k;}"
                                    "void quad(struct Q*p){p->b=p->a+p->c;}"
                                    "void split(struct U3*p){p->c=p->a+p->b;}"
                                    "void uni(union V*p){p->b=p->a;}"
                                    "void anon(struct A*p){p->y=p->x;}"
                                    "void nested(struct N*p){p->b.z=p->b.x;}"
                                    "unsigned local(unsigned n){struct C s;s.a=n;s.b=n;s.c=n;return s.c+s.b;}"
                                    "void global(unsigned n){gc.c=n;_Generic(0,int:gc.b)=n;gc.a++;}"
                                    "void flexible(struct F*p){p->a+=1;}"
                                    "void empty(struct M*p){p->a+=1;}"
                                    "long long wide(struct W*p){p->f=p->f*3;return p->f;}"
                                    "void whole(struct X*p){p->g+=2;}"
                                    "void union17(union U17*p){p->y=p->x;p->z+=1;}"
                                    "void union3(union U3B*p){p->x+=1;}"
                                    "void discrete(struct MS*p){p->y=p->x;p->w=p->z;}"
                                    "void spans(struct R*p){p->a=p->b;}"
                                    "void twelve(struct Q12*p){p->c=p->a;}"
                                    "unsigned initialized(unsigned n){struct B v={n,n+1,n+2,n+3};return v.w;}";
    typedef struct CheckTestBitCase CheckTestBitCase;
    struct CheckTestBitCase
    {
        const char* name;
        const char* trace;
    };
    // clang's units: B's 32 bits are one i32 at 0 (where the bytes of each field's bits are 2 at 0, 1 and 2); G's x an
    // i8, before its guard; T's a, before its char in an i32's last byte, a [3 x i8] (pieces of 2 and 1); P's packed run
    // a [6 x i8] (4 and 2) for both its fields; Q's 24 bits an i32 at 0, its tail padding; U3's a and b an i16, whose
    // i24 of all three takes 4 bytes the record hasn't, and c an i8 at 2; V's fields their own bytes; A's anonymous
    // struct's fields an i16 at the struct's 4; N's b's an i32 at 4; C's an i32 at 0, through a local's and a global's
    // constant addresses and a _Generic selection; F's a [3 x i8] before its flexible array member, which clang stores
    // (its first element is in what would be an i32's last byte), and M's an i32 past its member of an empty struct,
    // which clang doesn't. W's packed 63 bits from bit 11 and X's 64 from bit 3 lie in 9 bytes (#2999), an i72 that
    // clang's -O0 code reads and writes as an i64 and an i8 after it. A union's fields are an integer of their width's
    // power of two bytes (U17's 17 bits an i32, 40 an i64), or its bytes where they are fewer (U3B's i24, as pieces). An
    // ms_struct record's are its runs' declared types: MS's x and y an i32 at 0, z an i16 at 4 and w an i8 at 6. R's
    // packed run is an i80 at 1 and Q12's an i96 at 0, which clang's code moves as an i64 and the rest's pieces after
    // it; a field in one of the two takes that one (R's a its first 8 bytes, Q12's a too), where clang's code moves both,
    // and one across them both (R's b, Q12's c) takes both. A local's initializer stores its fields through their unit,
    // after the object's zeros, as clang's does.
    static const CheckTestBitCase cases[] = {
        { "issue", "r0:4 w0:4" },
        { "guard", "r0:1 w0:1" },
        { "clipped", "r0:2 r2:1 w0:2 w2:1" },
        { "packed", "r0:4 r4:2 w0:4 w4:2" },
        { "quad", "r0:4 w0:4" },
        { "split", "r0:2 r2:1 w2:1" },
        { "uni", "r0:1 r0:2 w0:2" },
        { "anon", "r4:2 w4:2" },
        { "nested", "r4:4 w4:4" },
        { "local", "r0:4 w0:4" },
        { "global", "r0:4 w0:4" },
        { "flexible", "r0:2 r2:1 w0:2 w2:1" },
        { "empty", "r0:4 w0:4" },
        { "wide", "r1:8 r9:1 w1:8 w9:1" },
        { "whole", "r0:8 r8:1 w0:8 w8:1" },
        { "union17", "r0:1 r0:4 w0:4 r0:8 w0:8" },
        { "union3", "r0:2 r2:1 w0:2 w2:1" },
        { "discrete", "r0:4 w0:4 r4:2 r6:1 w6:1" },
        { "spans", "r1:8 r9:2 w1:8" },
        { "twelve", "r0:8 w0:8 r8:4 w8:4" },
        { "initialized", "w0:4 r0:4" },
    };
    const uint32_t count = BUSTER_ARRAY_LENGTH(cases);
    char source[sizeof(records) + sizeof(functions)];
    snprintf(source, sizeof(source), "%s%s", records, functions);
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions preprocessing = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &preprocessing, &identifiers) &&
                              pp_run_source(&pp, "bit-units", (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "bit-field units: the source preprocesses");
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 26);
        Arena output = arena_reserve(UINT64_C(1) << 26);
        Arena bytes = arena_reserve(UINT64_C(1) << 22);
        Arena scratch = arena_reserve(UINT64_C(1) << 26);
        // Tier 0's records and the traces' columns, apart from `scratch`: each lowered function's access column
        // (Tier0Function.symbol_access) is the emission's, in what it left of `scratch` (check_program_access).
        Arena work = arena_reserve(UINT64_C(1) << 26);
        const ParseOptions parsing = { .scalar = path == 0 };
        const CheckOptions checking = { 0 };
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers plans = { 0 };
        CheckAsms asms = { 0 };
        CheckProgram program = { 0 };
        CheckProgramCode code = { 0 };
        bool ready = input_arena.base != 0 && output.base != 0 && bytes.base != 0 && scratch.base != 0 && work.base != 0 && check_test_types_open(&types) &&
                     parse_initialize(&parser, &parsing, preprocessing.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0 &&
                     check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
                     check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &checking, &stream) && stream.report_count == 0 &&
                     check_initializers_run(&plans, &input, &stream, &parser, &types.table, &input_arena, &scratch) &&
                     check_asms_run(&asms, &input, &stream, &types.table, &input_arena, &scratch);
        const CheckProgramOptions options = { .initializers = &plans, .asms = &asms, .identifiers = &identifiers, .scalar = path == 0 };
        ready = ready && check_program(&program, &parser, &input, &stream, &types.table, options, &input_arena, &scratch) &&
                check_program_emit(&code, &program, &parser, &identifiers, (LinkCapacity){ 0 }, path == 0, &output, &bytes, &scratch, NULL, NULL, false, true);
        const char* const way = path == 0 ? "scalar" : "vector";
        char description[256];
        snprintf(description, sizeof(description), "bit-field units: the %u functions lower by the %s path", count, way);
        check_test_expect(test, ready && code.lowered.count == count, description);
        for (uint32_t f = 0; ready && f < count && f < code.lowered.count; f += 1)
        {
            const Tier0Function* function = &code.lowered.functions[f];
            const Tier0Code vector = tier0_compile(function, &work);
            const bool compiled = vector.error == TIER0_OK && x86_validate(vector.records, vector.count) == vector.count &&
                                  tier0_validate(function).error == TIER0_OK;
            const CheckTestBitTrace trace = check_test_bit_trace(function, &work);
            snprintf(description, sizeof(description), "bit-field units (%s path): %s's %u accesses are \"%s\" (clang's \"%s\"), compiled by Tier 0's two paths alike",
                     way, cases[f].name, trace.accesses, trace.text, cases[f].trace);
            check_test_expect(test, compiled && strcmp(trace.text, cases[f].trace) == 0, description);
        }
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&work);
        arena_release(&scratch);
        arena_release(&bytes);
        arena_release(&output);
        arena_release(&input_arena);
    }
    pp_release(&pp);
    intern_release(&identifiers);
    // Natively against clang: each record's fields written, updated and read, through pointers, locals and a global, an
    // anonymous struct's and a nested record's, with their neighbors (G's guard, T's and A's chars, N's int) kept.
    check_test_program_source(test,
                              "struct B{unsigned x:3,y:7,z:13,w:9;};struct G{unsigned x:3;char guard;};struct T{unsigned a:24;char c;};"
                              "struct __attribute__((packed)) P{unsigned char k:4;unsigned long long x:44;};struct Q{unsigned a:8,b:8,c:8;};"
                              "struct U3{unsigned char a:8,b:8,c:8;};union V{unsigned a:3;unsigned b:12;};"
                              "struct A{char c;struct{unsigned x:4,y:12;};char d;};struct N{int i;struct B b;};struct C{unsigned a:8,b:8,c:16;};"
                              "struct C gc;struct __attribute__((packed)) W{char c;unsigned:3;long long f:63;char d;};"
                              "struct __attribute__((packed)) X{char a:3;unsigned long long g:64;char e;};"
                              "union U17{unsigned x:3;unsigned y:17;unsigned long long z:40;};union __attribute__((packed)) U3B{unsigned x:17;};"
                              "struct __attribute__((ms_struct)) MS{unsigned x:3;unsigned y:7;unsigned short z:5;unsigned char w:4;char d;};"
                              "struct __attribute__((packed)) R{char c;unsigned long long a:60;unsigned b:20;char d;};"
                              "struct __attribute__((packed)) Q12{unsigned a:30,b:30,c:30;char d;};"
                              "int extra(int n){union U17 u17;u17.z=0x123456789aULL;u17.x=n;int r=(int)(u17.z%1009);u17.y+=n*1000;r+=(int)(u17.z%1013)+u17.x;"
                              "union U3B u3;u3.x=0x1ffff;u3.x+=n;r+=u3.x;struct MS ms={5,100,30,15,'m'};ms.y+=ms.x+n;ms.w^=n;ms.z--;"
                              "r+=ms.x+ms.y*3+ms.z*5+ms.w*7+ms.d;struct R rr={'r',0xfffffffffffffffULL,0xfffff,'d'};rr.b+=n;rr.a-=n;"
                              "r+=rr.c+(int)(rr.a%1019)+(int)rr.b+rr.d;struct Q12 q={n,2*n,3*n,'q'};q.c+=q.a*0x10000;q.b^=0x3fffffff;"
                              "r+=(int)(q.a%1021)+(int)(q.b%1031)+(int)(q.c%1033)+q.d;struct MS mi={n,n,n,n,'i'};struct Q12 qi={n,n,n,'j'};"
                              "return r+mi.x+mi.y+mi.z+mi.w+mi.d+(int)(qi.c%1039)+qi.d;}"
                              "int tested(int n){struct B b={1,2,3,4};struct B*pb=&b;pb->y+=pb->x+n;pb->z^=pb->w*n;b.w=n;b.x--;"
                              "struct G g={5,'g'};g.x=n;g.x+=3;struct T t={0x123456,'t'};t.a+=n*1000;"
                              "struct P p={7,0x123456789ULL};p.x+=n;p.k^=n;struct Q q={1,2,3};q.b=q.a+q.c+n;q.c++;"
                              "struct U3 u={4,5,6};u.c=u.a+u.b+n;u.a=n;union V v;v.b=0xabc;v.a=n;"
                              "struct A a={'a',{.x=3,.y=n},'d'};a.y=a.x+n;a.x=n;struct N m={9,{1,2,3,4}};struct N*pm=&m;pm->b.z=pm->b.x+n;pm->b.w--;"
                              "struct C c;c.a=n;c.b=n*3;c.c=n*5;gc.c=n*7;_Generic(0,int:gc.b)=n;gc.a=gc.c+1;"
                              "struct W w={'w',-5,'d'};struct W*pw=&w;pw->f=pw->f*n-3;w.f--;w.c^=n;"
                              "struct X x={2,~0ULL,'e'};x.g+=n+1;x.a=x.a+n;int xg=x.g==(unsigned long long)n;"
                              "return b.x+b.y*3+b.z*5+b.w*7+g.x+g.guard+(int)(t.a%1009)+t.c+(int)(p.x%997)+p.k+q.a+q.b*3+q.c*5+u.a+u.b*3+u.c*5+v.b+"
                              "a.c+a.x+a.y*3+a.d+m.i+m.b.x+m.b.y+m.b.z+m.b.w+c.a+c.b+c.c+gc.a+gc.b+gc.c+w.c+(int)(w.f%1013)+w.d+x.a+xg+x.e+(int)(x.g>>60)+"
                              "extra(n);}",
                              "bit-field-units", clang);
    // Natively against clang: the layouts and initialized bits of records with zero-width fields (ordinary, ms_struct,
    // packed and a union's), an ms_struct union, a packed run of 16 bytes (an i128 unit) and a record that nests them in
    // an array and a union. image() hashes an object's bytes under a mask that has every named field's bits set, made by
    // storing all ones to each field of a zeroed object, so the hash compares each field's bits where they lie and none
    // of the padding's; sizes and offsets are compared too. A volatile object's fields are compared by value.
    check_test_program_source(test,
                              "struct Z{unsigned a:5;unsigned:0;unsigned b:7;unsigned char c:3;char d;};"
                              "struct __attribute__((ms_struct)) MZ{unsigned a:3;unsigned:0;unsigned b:5;unsigned short c:4,d:12;char e;};"
                              "struct __attribute__((packed)) PZ{char c;unsigned a:9;unsigned:0;unsigned long long b:40;char d;};"
                              "union __attribute__((ms_struct)) MU{unsigned char a:5;unsigned b:20;};union UZ{unsigned a:3;unsigned:0;unsigned long long b:33;};"
                              "struct __attribute__((packed)) R16{unsigned long long a:60,b:60;unsigned c:8;char d;};"
                              "struct NZ{char x;struct Z z[2];union UZ u;struct R16 r;};"
                              "static void zero(void*p,int s){unsigned char*c=p;for(int i=0;i<s;i++)c[i]=0;}"
                              "static unsigned image(const void*p,const void*m,int s){const unsigned char*a=p,*k=m;unsigned h=0;"
                              "for(int i=0;i<s;i++)h=h*31+(unsigned)(a[i]&k[i]);return h;}"
                              "static void zones(struct Z*m){zero(m,sizeof*m);m->a=~0u;m->b=~0u;m->c=7;m->d=-1;}"
                              "static void runs(struct R16*m){zero(m,sizeof*m);m->a=~0ULL;m->b=~0ULL;m->c=~0u;m->d=-1;}"
                              "int tested(int n){unsigned h=0;struct Z mz;zones(&mz);struct Z z={n,n*3,n+1,'z'};h=h*7+image(&z,&mz,sizeof z);"
                              "z.b+=n;z.a^=z.c;h=h*7+image(&z,&mz,sizeof z);"
                              "struct MZ mm;zero(&mm,sizeof mm);mm.a=~0u;mm.b=~0u;mm.c=15;mm.d=4095;mm.e=-1;struct MZ ms={n,n*2,n+5,n*100,'m'};"
                              "h=h*7+image(&ms,&mm,sizeof ms);ms.d+=ms.c*n;ms.a=ms.b;h=h*7+image(&ms,&mm,sizeof ms);"
                              "struct PZ mp;zero(&mp,sizeof mp);mp.c=-1;mp.a=~0u;mp.b=~0ULL;mp.d=-1;struct PZ p={'p',n*5,n*0x10001ULL,'q'};"
                              "h=h*7+image(&p,&mp,sizeof p);p.b-=p.a;h=h*7+image(&p,&mp,sizeof p);"
                              "union MU mu={.b=n*4097};union MU mk;zero(&mk,sizeof mk);mk.b=~0u;h=h*7+image(&mu,&mk,sizeof mu);mu.a+=n;h=h*7+mu.b;"
                              "union UZ ua={n};union UZ ka;zero(&ka,sizeof ka);ka.a=~0u;h=h*7+image(&ua,&ka,sizeof ua);"
                              "union UZ ub={.b=0x1abcdef12ULL*n};union UZ kb;zero(&kb,sizeof kb);kb.b=~0ULL;h=h*7+image(&ub,&kb,sizeof ub);"
                              "struct R16 mr;runs(&mr);struct R16 r={0xfffffffffffffffULL-n,0x123456789abcdefULL*n,n+7,'r'};h=h*7+image(&r,&mr,sizeof r);"
                              "r.b+=r.a;r.c^=n;r.a=r.b>>4;h=h*7+image(&r,&mr,sizeof r);"
                              "struct NZ mn;zero(&mn,sizeof mn);mn.x=-1;zones(&mn.z[0]);zones(&mn.z[1]);mn.u.b=~0ULL;runs(&mn.r);"
                              "struct NZ nz={'x',{{n,1,2,3},{4,n,6,7}},{.b=n*3ULL},{n,n*5ULL,n,'s'}};h=h*7+image(&nz,&mn,sizeof nz);"
                              "nz.z[1].b+=nz.z[0].a;nz.r.b^=nz.z[1].c;h=h*7+image(&nz,&mn,sizeof nz);"
                              "volatile struct Z vz={n,2,3,4};vz.b+=n;h=h*7+vz.a+vz.b*3+vz.c*5+vz.d*7;"
                              "return (int)(h%100003)+(int)(sizeof(struct Z)+sizeof(struct MZ)*3+sizeof(struct PZ)*5+sizeof(union MU)*7+"
                              "sizeof(union UZ)*11+sizeof(struct R16)*13+sizeof(struct NZ)*17+__builtin_offsetof(struct PZ,d)*19+"
                              "__builtin_offsetof(struct MZ,e)*23+__builtin_offsetof(struct NZ,r)*29+__builtin_offsetof(struct Z,d)*31)*100003;}",
                              "bit-field-units-layouts", clang);
}

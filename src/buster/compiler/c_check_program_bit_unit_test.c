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
                                  "struct C gc;struct F{unsigned a:24;char tail[];};struct E{};struct M{unsigned a:24;struct E e;};";
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
                                    "void empty(struct M*p){p->a+=1;}";
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
    // which clang doesn't.
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
        const CheckOptions checking = { .scalar = path == 0 };
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
            const Tier0Code scalar = tier0_compile_scalar(function, &work);
            const Tier0Code vector = tier0_compile(function, &work);
            const bool compiled = scalar.error == TIER0_OK && vector.error == TIER0_OK && scalar.count == vector.count &&
                                  memcmp(scalar.records, vector.records, (uint64_t)scalar.count * sizeof(X86Instruction)) == 0 &&
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
                              "struct C gc;"
                              "int tested(int n){struct B b={1,2,3,4};struct B*pb=&b;pb->y+=pb->x+n;pb->z^=pb->w*n;b.w=n;b.x--;"
                              "struct G g={5,'g'};g.x=n;g.x+=3;struct T t={0x123456,'t'};t.a+=n*1000;"
                              "struct P p={7,0x123456789ULL};p.x+=n;p.k^=n;struct Q q={1,2,3};q.b=q.a+q.c+n;q.c++;"
                              "struct U3 u={4,5,6};u.c=u.a+u.b+n;u.a=n;union V v;v.b=0xabc;v.a=n;"
                              "struct A a={'a',{.x=3,.y=n},'d'};a.y=a.x+n;a.x=n;struct N m={9,{1,2,3,4}};struct N*pm=&m;pm->b.z=pm->b.x+n;pm->b.w--;"
                              "struct C c;c.a=n;c.b=n*3;c.c=n*5;gc.c=n*7;_Generic(0,int:gc.b)=n;gc.a=gc.c+1;"
                              "return b.x+b.y*3+b.z*5+b.w*7+g.x+g.guard+(int)(t.a%1009)+t.c+(int)(p.x%997)+p.k+q.a+q.b*3+q.c*5+u.a+u.b*3+u.c*5+v.b+"
                              "a.c+a.x+a.y*3+a.d+m.i+m.b.x+m.b.y+m.b.z+m.b.w+c.a+c.b+c.c+gc.a+gc.b+gc.c;}",
                              "bit-field-units", clang);
}

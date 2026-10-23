// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original contextual-address tests. Clang checks the handwritten source decisions; direct tests additionally
// inspect the selected declaration/object, immutable source-time snapshots, malformed chains and every scratch
// capacity. Source consumers retain unresolved heads until the actual context; this helper never guesses one.
#ifndef BUSTER_CHECK_OVERLOAD_ADDRESS_TEST_IMPLEMENTATION
#define BUSTER_CHECK_OVERLOAD_ADDRESS_TEST_IMPLEMENTATION 1

typedef struct CheckOverloadAddressFixture CheckOverloadAddressFixture;
struct CheckOverloadAddressFixture
{
    const char* name;
    const char* source;
    TypeId types[3];
    TypeId target;
    uint32_t count;
    uint32_t unavailable;
    CheckOverloadStatus status;
    uint32_t selected;
};

static void check_test_overload_address_oracle(CheckTest* test, TypeTable* table, const char* clang)
{
    const TypeId integer = BUSTER_TYPE_INT;
    const TypeId floating = BUSTER_TYPE_DOUBLE;
    const TypeId longer = BUSTER_TYPE_LONG;
    const TypeId unsign = BUSTER_TYPE_UINT;
    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 133941);
    check_test_expect(test, type_enum_complete(table, enumeration, unsign, 0), "address oracle enum has a compatible integer type");
    const TypeId ip = type_pointer(table, integer);
    const TypeId vp = type_pointer(table, BUSTER_TYPE_VOID);
    const TypeMember members[] = {
        { .name = 1, .type = ip, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
        { .name = 2, .type = vp, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
    };
    const TypeId transparent = type_tag(table, BUSTER_TYPE_KIND_UNION, 133942);
    check_test_expect(test, type_complete_record(table, transparent, members, BUSTER_ARRAY_LENGTH(members), (TypeRecordAttributes){ 0 }) &&
                           type_mark_transparent_union(table, transparent).status == TYPE_TRANSPARENT_UNION_APPLIED,
                      "address oracle transparent union has ordinary parameter compatibility");
    const TypeId fi = type_function(table, integer, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
    const TypeId fd = type_function(table, floating, &floating, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
    const TypeId fl = type_function(table, integer, &longer, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
    const TypeId fe = type_function(table, integer, &enumeration, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
    const TypeId fu = type_function(table, integer, &unsign, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
    const TypeId fr = type_function(table, enumeration, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
    const TypeId fur = type_function(table, unsign, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
    const TypeId ft = type_function(table, integer, &transparent, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
    const TypeId fp = type_function(table, integer, &ip, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
    const TypeId fv = type_function(table, integer, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE | BUSTER_TYPE_FLAG_VARIADIC);
    const TypeId old = type_function(table, integer, 0, 0, 0);
    const TypeId pi = type_pointer(table, fi);
    const TypeId pd = type_pointer(table, fd);
    const TypeId pl = type_pointer(table, fl);
    const TypeId pu = type_pointer(table, fu);
    check_test_expect(test, type_compatible(table, fe, fu) == TYPE_ANSWER_YES && type_same_canonical(table, fe, fu) == TYPE_ANSWER_NO &&
                           type_compatible(table, ft, fp) == TYPE_ANSWER_YES && type_same_canonical(table, ft, fp) == TYPE_ANSWER_NO,
                      "ordinary compatibility controls differ from canonical address matching");
    const CheckOverloadAddressFixture fixtures[] = {
        { .name = "context selects earlier function", .source = "int f(int) O;double f(double) O;int(*p)(int)=f;", .types = {fi,fd}, .target = pi, .count = 2, .selected = 0 },
        { .name = "context selects latest function", .source = "int f(int) O;double f(double) O;double(*p)(double)=f;", .types = {fi,fd}, .target = pd, .count = 2, .selected = 1 },
        { .name = "address operator preserves contextual set", .source = "int f(int) O;double f(double) O;int(*p)(int)=&f;", .types = {fi,fd}, .target = pi, .count = 2 },
        { .name = "grouped designator preserves contextual set", .source = "int f(int) O;double f(double) O;int(*p)(int)=((f));", .types = {fi,fd}, .target = pi, .count = 2 },
        { .name = "cast selects exact object before conversion", .source = "int f(int) O;double f(double) O;int(*p)(int)=(int(*)(int))f;", .types = {fi,fd}, .target = pi, .count = 2 },
        { .name = "ordinary parameter supplies target", .source = "int f(int) O;double f(double) O;void take(int(*)(int));void g(void){take(f);}", .types = {fi,fd}, .target = pi, .count = 2 },
        { .name = "return supplies target", .source = "int f(int) O;double f(double) O;int(*g(void))(int){return f;}", .types = {fi,fd}, .target = pi, .count = 2 },
        { .name = "assignment supplies target", .source = "int f(int) O;double f(double) O;void g(void){int(*p)(int);p=f;}", .types = {fi,fd}, .target = pi, .count = 2 },
        { .name = "selected generic association receives target", .source = "int f(int) O;double f(double) O;int(*p)(int)=_Generic(0,int:f,default:0);", .types = {fi,fd}, .target = pi, .count = 2 },
        { .name = "no exact function target", .source = "int f(int) O;double f(double) O;int(*p)(long)=f;", .types = {fi,fd}, .target = pl, .count = 2, .status = CHECK_OVERLOAD_NO_MATCH },
        { .name = "cast does not bypass missing exact match", .source = "int f(int) O;double f(double) O;int(*p)(long)=(int(*)(long))f;", .types = {fi,fd}, .target = pl, .count = 2, .status = CHECK_OVERLOAD_NO_MATCH },
        { .name = "typeof has no target for multiple identities", .source = "int f(int) O;double f(double) O;__typeof__(f)*p;", .types = {fi,fd}, .count = 2, .status = CHECK_OVERLOAD_AMBIGUOUS },
        { .name = "generic control has no target for multiple identities", .source = "int f(int) O;double f(double) O;int n=_Generic(f,int(*)(int):1,default:0);", .types = {fi,fd}, .count = 2, .status = CHECK_OVERLOAD_AMBIGUOUS },
        { .name = "void target does not select an overload", .source = "int f(int) O;double f(double) O;void*p=(void*)f;", .types = {fi,fd}, .target = vp, .count = 2, .status = CHECK_OVERLOAD_AMBIGUOUS },
        { .name = "singleton typeof resolves without target", .source = "int f(int) O;__typeof__(f)*p=f;", .types = {fi}, .count = 1 },
        { .name = "singleton is selected before ordinary cast", .source = "int f(int) O;void*p=(void*)f;", .types = {fi}, .target = vp, .count = 1 },
        { .name = "unavailable singleton address rejects", .source = "int f(int) O U;int(*p)(int)=f;", .types = {fi}, .target = pi, .count = 1, .unavailable = 1, .status = CHECK_OVERLOAD_UNAVAILABLE },
        { .name = "unavailable singleton typeof rejects", .source = "int f(int) O U;__typeof__(f)*p;", .types = {fi}, .count = 1, .unavailable = 1, .status = CHECK_OVERLOAD_UNAVAILABLE },
        { .name = "contextual unavailable address remains selected", .source = "int f(int) O U;double f(double) O;int(*p)(int)=f;", .types = {fi,fd}, .target = pi, .count = 2, .unavailable = 1 },
        { .name = "unselected unavailable address has no effect", .source = "int f(int) O;double f(double) O U;int(*p)(int)=f;", .types = {fi,fd}, .target = pi, .count = 2, .unavailable = 2 },
        { .name = "compatible enum signature is not exact in a set", .source = "enum E{A};int f(enum E) O;double f(double) O;int(*p)(unsigned)=f;", .types = {fe,fd}, .target = pu, .count = 2, .status = CHECK_OVERLOAD_NO_MATCH },
        { .name = "canonical integer selects separately from enum", .source = "enum E{A};int f(enum E) O;int f(unsigned) O;int(*p)(unsigned)=f;", .types = {fe,fu}, .target = pu, .count = 2, .selected = 1 },
        { .name = "singleton compatible enum uses ordinary conversion", .source = "enum E{A};int f(enum E) O;int(*p)(unsigned)=f;", .types = {fe}, .target = pu, .count = 1 },
        { .name = "return type also requires canonical identity", .source = "enum E{A};enum E f(int) O;double f(double) O;unsigned(*p)(int)=f;", .types = {fr,fd}, .target = type_pointer(table, fur), .count = 2, .status = CHECK_OVERLOAD_NO_MATCH },
        { .name = "transparent parameter compatibility is not exact", .source = "typedef union __attribute__((transparent_union)){int*p;void*q;}T;int f(T) O;double f(double) O;int(*p)(int*)=f;", .types = {ft,fd}, .target = type_pointer(table, fp), .count = 2, .status = CHECK_OVERLOAD_NO_MATCH },
        { .name = "old-style compatible target is not exact in a set", .source = "int f(int) O;double f(double) O;int(*p)()=f;", .types = {fi,fd}, .target = type_pointer(table, old), .count = 2, .status = CHECK_OVERLOAD_NO_MATCH },
        { .name = "singleton old-style target uses ordinary conversion", .source = "int f(int) O;int(*p)()=f;", .types = {fi}, .target = type_pointer(table, old), .count = 1 },
        { .name = "variadic signature has an exact target", .source = "int f(int,...) O;double f(double) O;int(*p)(int,...)=f;", .types = {fv,fd}, .target = type_pointer(table, fv), .count = 2 },
        { .name = "variadic signature differs from fixed target", .source = "int f(int,...) O;double f(double) O;int(*p)(int)=f;", .types = {fv,fd}, .target = pi, .count = 2, .status = CHECK_OVERLOAD_NO_MATCH },
    };
    Arena scratch = arena_reserve(65536);
    char directory[] = "/tmp/buster-check-overload-address-XXXXXX";
    bool ready = mkdtemp(directory) != 0;
    char paths[BUSTER_ARRAY_LENGTH(fixtures)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(fixtures)];
    ParseTestClang outcomes[BUSTER_ARRAY_LENGTH(fixtures)];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        char source[2048];
        const int length = snprintf(source, sizeof(source), "#define O __attribute__((overloadable))\n#define U __attribute__((unavailable))\n%s\n", fixtures[i].source);
        ready &= length > 0 && (uint32_t)length < sizeof(source) && parse_test_write(paths[i], source, (uint32_t)length);
    }
    const char* mode = "-std=gnu17";
    const bool oracle = ready && parse_test_clang(clang, &mode, 1, pointers, BUSTER_ARRAY_LENGTH(fixtures), outcomes);
    check_test_expect(test, oracle, "independent contextual address Clang oracle runs");
    const CheckOverloadContext context = { .source_position = 100 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        const CheckOverloadAddressFixture* fixture = fixtures + i;
        CheckOverloadCandidate candidates[3] = { 0 };
        for (uint32_t c = 0; c < fixture->count; c += 1)
        {
            candidates[c] = (CheckOverloadCandidate){
                .type = fixture->types[c],
                .declaration = 10 + c * 10,
                .canonical = 10 + c * 10,
                .object = 100 + c,
                .previous = c == 0 ? UINT32_MAX : c - 1,
                .scope = 1,
                .position = 11 + c * 10,
                .flags = BUSTER_CHECK_OVERLOADABLE | (((fixture->unavailable >> c) & 1) != 0 ? BUSTER_CHECK_OVERLOAD_UNAVAILABLE : 0),
            };
        }
        const CheckOverloadResult result = check_overload_address(table, candidates, fixture->count, fixture->count - 1, fixture->target, &context, &scratch);
        check_test_expect(test, result.status == fixture->status && scratch.length == 0, fixture->name);
        if (result.status == CHECK_OVERLOAD_OK || result.status == CHECK_OVERLOAD_UNAVAILABLE)
        {
            const CheckOverloadCandidate* selected = candidates + fixture->selected;
            check_test_expect(test, result.candidate == fixture->selected && result.declaration == selected->declaration && result.canonical == selected->canonical &&
                                   result.object == selected->object && result.type == selected->type && result.flags == selected->flags, fixture->name);
        }
        else
        {
            check_test_expect(test, result.candidate == UINT32_MAX && result.declaration == UINT32_MAX && result.canonical == UINT32_MAX && result.type == 0 &&
                                   result.object == 0 && result.flags == 0, "failed address resolution publishes no guessed identity");
        }
        if (oracle)
        {
            check_test_expect(test, outcomes[i].rejected == (fixture->status != CHECK_OVERLOAD_OK), fixture->name);
        }
        unlink(paths[i]);
    }
    rmdir(directory);
    arena_release(&scratch);
}

static void check_test_overload_address_history(CheckTest* test, TypeTable* table)
{
    const TypeId integer = BUSTER_TYPE_INT;
    const TypeId floating = BUSTER_TYPE_DOUBLE;
    const TypeId fi = type_function(table, integer, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
    const TypeId fd = type_function(table, floating, &floating, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
    const TypeId pi = type_pointer(table, fi);
    CheckOverloadCandidate candidates[4] = {
        { .type = fi, .declaration = 10, .canonical = 10, .object = 100, .previous = UINT32_MAX, .scope = 1, .position = 11, .flags = BUSTER_CHECK_OVERLOADABLE },
        { .type = fi, .declaration = 20, .canonical = 10, .object = 100, .previous = 0, .scope = 1, .position = 21, .flags = BUSTER_CHECK_OVERLOADABLE | BUSTER_CHECK_OVERLOAD_UNAVAILABLE },
        { .type = fd, .declaration = 30, .canonical = 30, .object = 300, .previous = 1, .scope = 1, .position = 31, .flags = BUSTER_CHECK_OVERLOADABLE },
        { .type = fi, .declaration = 40, .canonical = 10, .object = 100, .previous = 2, .scope = 1, .position = 41, .flags = BUSTER_CHECK_OVERLOADABLE | BUSTER_CHECK_OVERLOAD_UNAVAILABLE },
    };
    const CheckOverloadContext context = { .source_position = 50 };
    Arena scratch = arena_reserve(65536);
    CheckOverloadResult result = check_overload_address(table, candidates, 4, 0, 0, &context, &scratch);
    check_test_expect(test, result.status == CHECK_OVERLOAD_OK && result.candidate == 0 && result.flags == BUSTER_CHECK_OVERLOADABLE,
                      "earlier head excludes future overloads and unavailable redeclarations");
    result = check_overload_address(table, candidates, 4, 1, 0, &context, &scratch);
    check_test_expect(test, result.status == CHECK_OVERLOAD_UNAVAILABLE && result.candidate == 1 && result.declaration == 20 && result.object == 100,
                      "redeclared singleton uses its latest visible availability snapshot");
    result = check_overload_address(table, candidates, 4, 3, pi, &context, &scratch);
    check_test_expect(test, result.status == CHECK_OVERLOAD_OK && result.candidate == 3 && result.declaration == 40 && result.canonical == 10 &&
                           result.object == 100 && (result.flags & BUSTER_CHECK_OVERLOAD_UNAVAILABLE) != 0,
                      "multiple-set canonical deduplication retains the latest selected snapshot and exact object");
    result = check_overload_address(table, candidates, 4, 3, 0, &context, &scratch);
    check_test_expect(test, result.status == CHECK_OVERLOAD_AMBIGUOUS, "repeated identity does not erase a different overload");
    const CheckOverloadContext earlier = { .source_position = 40 };
    result = check_overload_address(table, candidates, 4, 3, pi, &earlier, &scratch);
    check_test_expect(test, result.status == CHECK_OVERLOAD_INVALID, "future candidate cannot enter an earlier context");
    const CheckOverloadContext final = { 0 };
    result = check_overload_address(table, candidates, 4, 3, pi, &final, &scratch);
    check_test_expect(test, result.status == CHECK_OVERLOAD_OK && result.candidate == 3, "zero context cutoff preserves explicit final-table semantics");
    const CheckOverloadCandidate saved = candidates[0];
    for (uint32_t variant = 0; variant < 7; variant += 1)
    {
        candidates[0] = saved;
        if (variant == 0)
        {
            candidates[0].previous = 0;
        }
        else if (variant == 1)
        {
            candidates[0].previous = UINT32_MAX - 1;
        }
        else if (variant == 2)
        {
            candidates[0].scope = 2;
        }
        else if (variant == 3)
        {
            candidates[0].type = BUSTER_TYPE_INT;
        }
        else if (variant == 4)
        {
            candidates[0].canonical = UINT32_MAX;
        }
        else if (variant == 5)
        {
            candidates[0].declaration = UINT32_MAX;
        }
        else
        {
            candidates[0].flags |= 0x80;
        }
        result = check_overload_address(table, candidates, 4, 3, pi, &context, &scratch);
        check_test_expect(test, result.status == CHECK_OVERLOAD_INVALID && result.candidate == UINT32_MAX && scratch.length == 0,
                          "whole-chain validation rejects corruption even behind a previously matched latest declaration");
    }
    candidates[0] = saved;
    check_test_expect(test, check_overload_address(table, 0, 0, UINT32_MAX, pi, &context, &scratch).status == CHECK_OVERLOAD_NO_MATCH &&
                           check_overload_address(table, candidates, 4, 4, pi, &context, &scratch).status == CHECK_OVERLOAD_INVALID &&
                           check_overload_address(table, 0, 4, 0, pi, &context, &scratch).status == CHECK_OVERLOAD_INVALID &&
                           check_overload_address(table, candidates, 4, 3, pi, 0, &scratch).status == CHECK_OVERLOAD_INVALID &&
                           check_overload_address(table, candidates, 4, 3, pi, &context, 0).status == CHECK_OVERLOAD_INVALID &&
                           check_overload_address(table, candidates, 4, 3, UINT32_MAX, &context, &scratch).status == CHECK_OVERLOAD_INVALID,
                      "empty chains and invalid API inputs have explicit non-selection results");
    candidates[2].type = fi;
    result = check_overload_address(table, candidates, 4, 3, pi, &context, &scratch);
    check_test_expect(test, result.status == CHECK_OVERLOAD_AMBIGUOUS && result.candidate == UINT32_MAX,
                      "two distinct canonical identities with the same function type never choose by declaration order");
    arena_release(&scratch);
}

static void check_test_overload_address_capacity(CheckTest* test, TypeTable* table)
{
    enum { COUNT = 129 };
    CheckOverloadCandidate candidates[COUNT];
    for (uint32_t i = 0; i < COUNT; i += 1)
    {
        const TypeId parameter = type_pointer(table, type_array(table, BUSTER_TYPE_INT, i + 1));
        candidates[i] = (CheckOverloadCandidate){
            .type = type_function(table, BUSTER_TYPE_INT, &parameter, 1, BUSTER_TYPE_FLAG_PROTOTYPE),
            .declaration = i + 1,
            .canonical = (i + 1) * 512,
            .object = i + 101,
            .previous = i == 0 ? UINT32_MAX : i - 1,
            .scope = 1,
            .position = i + 1,
            .flags = BUSTER_CHECK_OVERLOADABLE,
        };
    }
    CheckOverloadCandidate original[COUNT];
    memcpy(original, candidates, sizeof(original));
    const CheckOverloadContext context = { .source_position = COUNT };
    const TypeId target = type_pointer(table, candidates[0].type);
    Arena scratch = arena_reserve(65536);
    const uint64_t capacity = scratch.capacity;
    uint8_t* bytes = arena_push(&scratch, capacity, 1);
    check_test_expect(test, bytes != 0, "address scratch capacity fixture allocates");
    if (bytes != 0)
    {
        memset(bytes, 0xa5, capacity);
        uint32_t full = 0;
        uint32_t complete = 0;
        for (uint64_t room = 0; room <= 4096; room += 1)
        {
            scratch.length = 31;
            scratch.capacity = 31 + room;
            const CheckOverloadResult result = check_overload_address(table, candidates, COUNT, COUNT - 1, target, &context, &scratch);
            full += result.status == CHECK_OVERLOAD_FULL;
            complete += result.status == CHECK_OVERLOAD_OK;
            const bool selected = result.status == CHECK_OVERLOAD_OK && result.candidate == 0 && result.declaration == 1 && result.canonical == 512 && result.object == 101;
            check_test_expect(test, (result.status == CHECK_OVERLOAD_FULL || selected) && scratch.length == 31 && bytes[30] == 0xa5 &&
                                   memcmp(candidates, original, sizeof(original)) == 0,
                              "every poisoned scratch capacity either selects the exact oldest colliding identity or reports exhaustion and rolls back");
        }
        check_test_expect(test, full != 0 && complete != 0, "address scratch controls exercise exhaustion and success");
        scratch.capacity = capacity;
        scratch.length = 31;
        const TypeId parameter = type_pointer(table, type_aligned(table, BUSTER_TYPE_INT, 4));
        const TypeId plain = type_pointer(table, BUSTER_TYPE_INT);
        candidates[0].type = type_function(table, BUSTER_TYPE_INT, &parameter, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        const TypeId plain_function = type_function(table, BUSTER_TYPE_INT, &plain, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        const TypeId aligned_target = type_pointer(table, plain_function);
        Scratch canonical = scratch_begin(0);
        const uint64_t canonical_length = canonical.arena->length;
        const uint64_t canonical_capacity = canonical.arena->capacity;
        canonical.arena->capacity = canonical_length;
        const CheckOverloadResult exhausted = check_overload_address(table, candidates, COUNT, COUNT - 1, aligned_target, &context, &scratch);
        check_test_expect(test, exhausted.status == CHECK_OVERLOAD_FULL && exhausted.candidate == UINT32_MAX && scratch.length == 31 &&
                               canonical.arena->length == canonical_length,
                          "canonical type-walk exhaustion propagates without exposing a partial address selection");
        canonical.arena->capacity = canonical_capacity;
        scratch_end(canonical);
        const CheckOverloadResult aligned = check_overload_address(table, candidates, COUNT, COUNT - 1, aligned_target, &context, &scratch);
        check_test_expect(test, aligned.status == CHECK_OVERLOAD_OK && aligned.candidate == 0 && aligned.type == candidates[0].type && scratch.length == 31,
                          "canonical address matching ignores nested typedef alignment and keeps the original selected function type");
    }
    scratch.capacity = capacity;
    arena_release(&scratch);
}

static void check_test_overload_addresses(CheckTest* test, const char* clang)
{
    TypeTestTable storage;
    const bool ready = type_test_table_open(&storage, 8192);
    check_test_expect(test, ready, "contextual overload address type storage initializes");
    if (ready)
    {
        check_test_overload_address_oracle(test, &storage.table, clang);
        check_test_overload_address_history(test, &storage.table);
        check_test_overload_address_capacity(test, &storage.table);
        type_test_table_close(&storage);
    }
}

#endif

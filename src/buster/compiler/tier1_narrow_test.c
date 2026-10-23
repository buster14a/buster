// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent, bounded native tests of the byte/word IR contract. Include after tier1_emit_test.c's helpers and
// invoke tier1_narrow_tests. Original IR fixtures are not C-source acceptance. Expected semantic values are frozen
// before selection. Both encoder outputs execute unmodified; a second copy observes whole registers immediately
// after narrow definitions, so a later extension cannot hide noncanonical bits. These test-only probes use reserved
// R11 and MOV exclusively, preserving allocated registers, flags and stack layout. Guard pages and complete data-page
// comparisons check exact-width memory accesses. The child unblocks its alarm; the parent independently enforces
// a monotonic deadline and bounded kill/reap cleanup for native faults and hangs in this fixed suite.

#include <errno.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <time.h>

#define BUSTER_TIER1_NARROW_NODES 192u
#define BUSTER_TIER1_NARROW_CONSTANTS 64u
#define BUSTER_TIER1_NARROW_SAMPLES 20u
#define BUSTER_TIER1_NARROW_TERMS 24u
#define BUSTER_TIER1_NARROW_MARKER UINT64_C(0x8a1164c0a5e5f001)
#define BUSTER_TIER1_NARROW_TIMEOUT_MS 60000u
#define BUSTER_TIER1_NARROW_REAP_MS 5000u

typedef enum Tier1NarrowKind
{
    TIER1_NARROW_LOAD,
    TIER1_NARROW_TRUNCATE,
    TIER1_NARROW_WIDEN,
    TIER1_NARROW_SELECT,
    TIER1_NARROW_PRESSURE,
    TIER1_NARROW_CONSTANT_PRESSURE,
    TIER1_NARROW_BOOLEAN,
    TIER1_NARROW_RETRUNCATE,
    TIER1_NARROW_WORD,
    TIER1_NARROW_DWORD,
    TIER1_NARROW_BSWAP,
} Tier1NarrowKind;

typedef struct Tier1NarrowFixture Tier1NarrowFixture;
struct Tier1NarrowFixture
{
    IrFunction function;
    Tier1NarrowKind kind;
    IrType narrow;
    IrType input_type;
    IrType result_type;
    bool sign;
    uint32_t memory_bytes;
    uint32_t nodes;
    uint32_t constants;
};

typedef struct Tier1NarrowExpected Tier1NarrowExpected;
struct Tier1NarrowExpected
{
    uint64_t input[BUSTER_TIER1_NARROW_SAMPLES];
    uint64_t initial[BUSTER_TIER1_NARROW_SAMPLES];
    uint64_t result[BUSTER_TIER1_NARROW_SAMPLES];
    uint64_t memory[BUSTER_TIER1_NARROW_SAMPLES];
    uint64_t values[BUSTER_TIER1_NARROW_SAMPLES][BUSTER_TIER1_NARROW_NODES];
};

static uint32_t tier1_narrow_bits(uint32_t type)
{
    const uint32_t bits = type == IR_I1 ? 1 : type == IR_I8 ? 8 : type == IR_I16 ? 16 : type == IR_I32 ? 32 : type == IR_I64 || type == IR_PTR ? 64 : 0;
    return bits;
}

static uint64_t tier1_narrow_mask(uint32_t bits)
{
    return bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
}

static uint32_t tier1_narrow_node(Tier1NarrowFixture* fixture, IrOp op, IrType type, uint32_t a, uint32_t b)
{
    const uint32_t node = fixture->nodes++;
    if (node < BUSTER_TIER1_NARROW_NODES)
    {
        tier1_emit_test_node(&fixture->function, node, op, type, a, b);
    }
    return node;
}

static uint32_t tier1_narrow_constant(Tier1NarrowFixture* fixture, IrType type, uint64_t bits)
{
    const uint32_t constant = fixture->constants++;
    if (constant < BUSTER_TIER1_NARROW_CONSTANTS)
    {
        fixture->function.constants[constant].low = bits & tier1_narrow_mask(tier1_narrow_bits(type));
    }
    return tier1_narrow_node(fixture, IR_CONST, type, constant, BUSTER_IR_NONE);
}

static bool tier1_narrow_fixture(Tier1EmitTest* test, Tier1NarrowFixture* fixture)
{
    const IrSizes sizes = {
        .instruction_count = BUSTER_TIER1_NARROW_NODES,
        .block_count = 1,
        .constant_count = BUSTER_TIER1_NARROW_CONSTANTS,
        .parameter_count = 2,
    };
    bool ready = ir_create(&test->input, &sizes, &fixture->function) == IR_OK;
    if (ready)
    {
        IrFunction* function = &fixture->function;
        function->parameter_types[0] = IR_PTR;
        function->parameter_types[1] = (uint8_t)fixture->input_type;
        function->result_type = (uint8_t)fixture->result_type;
        tier1_narrow_node(fixture, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE);
        const uint32_t input = tier1_narrow_node(fixture, IR_PARAM, fixture->input_type, 1, BUSTER_IR_NONE);
        uint32_t value = input;
        fixture->memory_bytes = fixture->narrow == IR_I16 || fixture->kind == TIER1_NARROW_WIDEN ? 2 : 1;
        if (fixture->kind == TIER1_NARROW_LOAD)
        {
            value = tier1_narrow_node(fixture, IR_LOAD, fixture->narrow, 0, BUSTER_IR_NONE);
            function->flags[value] = BUSTER_IR_VOLATILE;
        }
        else if (fixture->kind == TIER1_NARROW_BOOLEAN)
        {
            const uint32_t zero = tier1_narrow_constant(fixture, IR_I64, 0);
            value = tier1_narrow_node(fixture, IR_ICMP, IR_I1, input, zero);
            function->c[value] = IR_NE;
            value = tier1_narrow_node(fixture, IR_ZEXT, fixture->narrow, value, BUSTER_IR_NONE);
        }
        else if (fixture->kind == TIER1_NARROW_DWORD)
        {
            value = tier1_narrow_node(fixture, IR_TRUNC, IR_I32, input, BUSTER_IR_NONE);
        }
        else if (fixture->kind == TIER1_NARROW_RETRUNCATE)
        {
            value = tier1_narrow_node(fixture, IR_TRUNC, IR_I16, input, BUSTER_IR_NONE);
            value = tier1_narrow_node(fixture, IR_TRUNC, IR_I8, value, BUSTER_IR_NONE);
        }
        else if (fixture->kind == TIER1_NARROW_BSWAP)
        {
            // This is the existing BSWAP16 adapter's U64 mask/shift shape, ending in its U16 conversion.
            // The independent oracle also checks the direct two-byte permutation, not just this recipe.
            value = tier1_narrow_node(fixture, IR_TRUNC, IR_I16, input, BUSTER_IR_NONE);
            value = tier1_narrow_node(fixture, IR_ZEXT, IR_I64, value, BUSTER_IR_NONE);
            const uint32_t input_mask = tier1_narrow_constant(fixture, IR_I64, UINT16_MAX);
            value = tier1_narrow_node(fixture, IR_AND, IR_I64, value, input_mask);
            const uint64_t masks[] = {
                UINT64_C(0x00ff00ff00ff00ff),
                UINT64_C(0x0000ffff0000ffff),
                UINT64_C(0x00000000ffffffff),
            };
            for (uint32_t stage = 0; stage < 3; stage += 1)
            {
                const uint32_t shift = tier1_narrow_constant(fixture, IR_I64, 8u << stage);
                const uint32_t mask = tier1_narrow_constant(fixture, IR_I64, masks[stage]);
                const uint32_t right = tier1_narrow_node(fixture, IR_LSHR, IR_I64, value, shift);
                const uint32_t low = tier1_narrow_node(fixture, IR_AND, IR_I64, right, mask);
                const uint32_t high = tier1_narrow_node(fixture, IR_AND, IR_I64, value, mask);
                const uint32_t left = tier1_narrow_node(fixture, IR_SHL, IR_I64, high, shift);
                value = tier1_narrow_node(fixture, IR_OR, IR_I64, left, low);
            }
            const uint32_t shift = tier1_narrow_constant(fixture, IR_I64, 48);
            value = tier1_narrow_node(fixture, IR_LSHR, IR_I64, value, shift);
            value = tier1_narrow_node(fixture, IR_TRUNC, IR_I16, value, BUSTER_IR_NONE);
        }
        else if (fixture->kind == TIER1_NARROW_PRESSURE || fixture->kind == TIER1_NARROW_CONSTANT_PRESSURE)
        {
            uint32_t terms[BUSTER_TIER1_NARROW_TERMS];
            const uint32_t loaded = tier1_narrow_node(fixture, IR_LOAD, fixture->narrow, 0, BUSTER_IR_NONE);
            function->flags[loaded] = BUSTER_IR_VOLATILE;
            for (uint32_t term = 0; term < BUSTER_TIER1_NARROW_TERMS; term += 1)
            {
                if (fixture->kind == TIER1_NARROW_CONSTANT_PRESSURE)
                {
                    terms[term] = tier1_narrow_constant(fixture, fixture->narrow, UINT64_C(0x1081) * (term + 1));
                }
                else
                {
                    const uint32_t constant = tier1_narrow_constant(fixture, IR_I64, UINT64_C(0x1081) * (term + 1));
                    const uint32_t sum = tier1_narrow_node(fixture, IR_ADD, IR_I64, input, constant);
                    terms[term] = tier1_narrow_node(fixture, IR_TRUNC, fixture->narrow, sum, BUSTER_IR_NONE);
                }
            }
            tier1_narrow_node(fixture, IR_STORE, IR_VOID, 0, terms[BUSTER_TIER1_NARROW_TERMS - 1]);
            value = tier1_narrow_node(fixture, IR_ZEXT, IR_I64, loaded, BUSTER_IR_NONE);
            for (uint32_t term = 0; term < BUSTER_TIER1_NARROW_TERMS; term += 1)
            {
                // COPY makes a reloaded narrow value directly observable before an extension can normalize it.
                const uint32_t copied = tier1_narrow_node(fixture, IR_COPY, fixture->narrow, terms[term], BUSTER_IR_NONE);
                const uint32_t extended = tier1_narrow_node(fixture, term & 1 ? IR_SEXT : IR_ZEXT, IR_I64, copied, BUSTER_IR_NONE);
                value = tier1_narrow_node(fixture, IR_ADD, IR_I64, value, extended);
            }
        }
        else if (fixture->kind != TIER1_NARROW_WORD)
        {
            value = tier1_narrow_node(fixture, IR_TRUNC, fixture->narrow, input, BUSTER_IR_NONE);
            if (fixture->kind == TIER1_NARROW_WIDEN)
            {
                value = tier1_narrow_node(fixture, fixture->sign ? IR_SEXT : IR_ZEXT, IR_I16, value, BUSTER_IR_NONE);
            }
            else if (fixture->kind == TIER1_NARROW_SELECT)
            {
                const uint32_t zero = tier1_narrow_constant(fixture, IR_I64, 0);
                const uint32_t condition = tier1_narrow_node(fixture, IR_ICMP, IR_I1, input, zero);
                function->c[condition] = IR_NE;
                const uint32_t alternate = tier1_narrow_constant(fixture, fixture->narrow, fixture->narrow == IR_I8 ? 0xa5 : 0xa55a);
                const uint32_t selected = tier1_narrow_node(fixture, IR_SELECT, fixture->narrow, condition, value);
                function->c[selected] = alternate;
                value = tier1_narrow_node(fixture, IR_COPY, fixture->narrow, selected, BUSTER_IR_NONE);
            }
        }
        if (fixture->kind != TIER1_NARROW_LOAD && fixture->kind != TIER1_NARROW_PRESSURE && fixture->kind != TIER1_NARROW_WORD && fixture->kind != TIER1_NARROW_DWORD &&
            (function->type[value] == IR_I8 || function->type[value] == IR_I16))
        {
            const uint32_t stored = tier1_narrow_node(fixture, IR_STORE, IR_VOID, 0, value);
            function->flags[stored] = BUSTER_IR_VOLATILE;
        }
        if (function->type[value] != fixture->result_type)
        {
            const bool sign = fixture->sign && fixture->kind != TIER1_NARROW_WIDEN;
            value = tier1_narrow_node(fixture, sign ? IR_SEXT : IR_ZEXT, fixture->result_type, value, BUSTER_IR_NONE);
        }
        tier1_narrow_node(fixture, IR_RET, IR_VOID, value, BUSTER_IR_NONE);
        function->instruction_count = fixture->nodes;
        function->constant_count = fixture->constants;
        function->block_offsets[1] = fixture->nodes;
        ready = fixture->nodes <= BUSTER_TIER1_NARROW_NODES && fixture->constants <= BUSTER_TIER1_NARROW_CONSTANTS;
        ready = ready && ir_verify_scalar(function, &test->scratch, 0).error == IR_OK;
    }
    return ready;
}

static bool tier1_narrow_expected(const Tier1NarrowFixture* fixture, Tier1NarrowExpected* expected)
{
    // Scalar arithmetic only: no selector rules, machine widths, allocation locations or emitted records.
    const uint64_t edges[] = {
        UINT64_C(0),
        UINT64_C(1),
        UINT64_C(0x7f),
        UINT64_C(0x80),
        UINT64_C(0xff),
        UINT64_C(0x100),
        UINT64_C(0x7fff),
        UINT64_C(0x8000),
        UINT64_C(0xffff),
        UINT64_C(0x10000),
        UINT64_C(0x7fffffff),
        UINT64_C(0x80000000),
        UINT64_C(0xffffffff),
        UINT64_C(0xdeadbeef80008080),
        UINT64_MAX,
        UINT64_C(0x8000000000000000),
    };
    const IrFunction* function = &fixture->function;
    uint64_t random = UINT64_C(0xa31d2f16b83084c5);
    bool ready = true;
    memset(expected, 0, sizeof(*expected));
    for (uint32_t sample = 0; ready && sample < BUSTER_TIER1_NARROW_SAMPLES; sample += 1)
    {
        uint64_t* values = expected->values[sample];
        expected->input[sample] = sample < sizeof(edges) / sizeof(edges[0]) ? edges[sample] : tier1_emit_test_random(&random);
        expected->initial[sample] = expected->input[sample] & tier1_narrow_mask(fixture->memory_bytes * 8);
        uint64_t memory = expected->initial[sample];
        for (uint32_t node = 0; ready && node < function->instruction_count; node += 1)
        {
            const uint32_t a = function->a[node];
            const uint32_t b = function->b[node];
            const uint32_t c = function->c[node];
            const uint64_t left = a < node ? values[a] : 0;
            const uint64_t right = b < node ? values[b] : 0;
            uint64_t value = 0;
            switch (function->op[node])
            {
            case IR_PARAM: value = a == 1 ? expected->input[sample] : 0; break;
            case IR_CONST: value = function->constants[a].low; break;
            case IR_LOAD:
                ready = a == 0;
                value = memory;
                break;
            case IR_STORE:
                ready = a == 0;
                memory = right;
                break;
            case IR_ADD: value = left + right; break;
            case IR_AND: value = left & right; break;
            case IR_OR: value = left | right; break;
            case IR_SHL:
                ready = right < tier1_narrow_bits(function->type[node]);
                value = ready ? left << right : 0;
                break;
            case IR_LSHR:
                ready = right < tier1_narrow_bits(function->type[node]);
                value = ready ? left >> right : 0;
                break;
            case IR_COPY:
            case IR_TRUNC:
            case IR_ZEXT: value = left; break;
            case IR_SEXT:
            {
                const uint64_t sign = UINT64_C(1) << (tier1_narrow_bits(function->type[a]) - 1);
                value = (left ^ sign) - sign;
                break;
            }
            case IR_ICMP:
                ready = c == IR_NE;
                value = left != right;
                break;
            case IR_SELECT: value = left ? right : values[c]; break;
            case IR_RET: expected->result[sample] = left; break;
            default: ready = false; break;
            }
            values[node] = value & tier1_narrow_mask(tier1_narrow_bits(function->type[node]));
        }
        expected->memory[sample] = memory;
        if (fixture->kind == TIER1_NARROW_BSWAP)
        {
            const uint64_t input = expected->input[sample];
            const uint64_t swapped = ((input & 255) << 8) | ((input >> 8) & 255);
            ready = ready && expected->result[sample] == swapped && memory == swapped;
        }
    }
    return ready;
}

static uint32_t tier1_narrow_register(const Tier1EmitResult* emitted, uint32_t value)
{
    const Tier1AllocationInput* input = &emitted->prepared.allocation;
    const Tier1Allocation* allocation = &emitted->allocation;
    const uint32_t position = value * 2 + 1;
    uint32_t reg = UINT32_MAX;
    for (uint32_t interval = 0; interval < input->count; interval += 1)
    {
        for (uint32_t fragment = allocation->fragment_offsets[interval]; input->values[interval] == value && fragment < allocation->fragment_offsets[interval + 1]; fragment += 1)
        {
            if (allocation->starts[fragment] <= position && allocation->ends[fragment] > position && allocation->kinds[fragment] == BUSTER_TIER1_ALLOCATE_REGISTER)
            {
                reg = allocation->registers[fragment];
            }
        }
    }
    return reg;
}

static bool tier1_narrow_records(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted, uint64_t* observed, bool probes, X86Instruction* records, uint32_t* count)
{
    const IrFunction* function = plan->function;
    bool ready = emitted->code.branch_count == 0 && emitted->reference_count == 0;
    uint32_t written = 0;
    uint32_t node = 0;
    for (uint32_t record = 0; ready && record <= emitted->code.count; record += 1)
    {
        while (node < function->instruction_count && emitted->code.node_first[node + 1] == record)
        {
            if (probes && !plan->covered[node] && (function->type[node] == IR_I8 || function->type[node] == IR_I16))
            {
                const uint32_t reg = tier1_narrow_register(emitted, node);
                ready = ready && reg < 16 && reg != X86_REGISTER_R11;
                const uint64_t address = (uintptr_t)&observed[node];
                records[written++] = (X86Instruction){
                    .form = X86_FORM_MOV_R64_IMM64,
                    .rm = X86_REGISTER_R11,
                    .immediate = (int32_t)address,
                    .displacement = (int32_t)(address >> 32),
                };
                records[written++] = tier1_frame_emit_memory(X86_FORM_MOV_RM64_R64, (uint8_t)reg, X86_REGISTER_R11, 0);
            }
            node += 1;
        }
        if (record < emitted->code.count)
        {
            records[written++] = emitted->code.records[record];
        }
    }
    *count = written;
    return ready && node == function->instruction_count;
}

static void tier1_narrow_execute(Tier1EmitTest* test, const Tier1NarrowFixture* fixture, const Tier1NarrowExpected* expected, const Tier1SelectPlan* plan, const Tier1EmitResult* emitted)
{
    const long queried_page = sysconf(_SC_PAGESIZE);
    const uint64_t page = queried_page > 0 ? (uint64_t)queried_page : 4096;
    uint8_t* guard = mmap(0, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* page_expected = malloc(page);
    const uint32_t record_capacity = emitted->code.count + fixture->nodes * 2;
    X86Instruction* records = malloc((uint64_t)record_capacity * sizeof(*records));
    bool ready = guard != MAP_FAILED && page_expected != 0 && records != 0;
    ready = ready && mprotect(guard + page, page, PROT_READ | PROT_WRITE) == 0;
    tier1_emit_test_expect(test, ready, "narrow memory has inaccessible neighbors and an independent expected page");
    uint64_t observed[BUSTER_TIER1_NARROW_NODES];
    for (uint32_t probes = 0; ready && probes < 2; probes += 1)
    {
        uint32_t count = 0;
        bool encoded = tier1_narrow_records(plan, emitted, observed, probes != 0, records, &count);
        tier1_emit_test_expect(test, encoded, "canonical-register probes preserve straight-line records and use reserved R11 only");
        const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(count);
        const uint64_t mapping_bytes = (capacity + page - 1) / page * page;
        uint8_t* native[2] = {
            mmap(0, mapping_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0),
            mmap(0, mapping_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0),
        };
        encoded = encoded && native[0] != MAP_FAILED && native[1] != MAP_FAILED;
        encoded = encoded && x86_validate(records, count) == count && x86_target_validate(test->allowed, records, count) == count;
        int64_t lengths[2] = {0};
        if (encoded)
        {
            lengths[0] = x86_encode_scalar(records, count, native[0], capacity, 0, 0);
            lengths[1] = x86_encode(records, count, native[1], capacity, 0, 0);
            encoded = lengths[0] > 0 && lengths[1] > 0 && (uint64_t)lengths[0] <= capacity && (uint64_t)lengths[1] <= capacity;
            encoded = encoded && lengths[0] == lengths[1] && memcmp(native[0], native[1], (size_t)lengths[0]) == 0;
            encoded = encoded && mprotect(native[0], mapping_bytes, PROT_READ | PROT_EXEC) == 0 && mprotect(native[1], mapping_bytes, PROT_READ | PROT_EXEC) == 0;
        }
        tier1_emit_test_expect(test, encoded, "both encoders agree on baseline narrow records and create executable mappings");
        for (uint32_t encoder = 0; encoded && encoder < 2; encoder += 1)
        {
            for (uint32_t placement = 0; placement < 2; placement += 1)
            {
                // The boundary placement faults an over-wide access. The interior word is deliberately unaligned;
                // comparing the whole page also catches stores before or after the requested one/two bytes.
                const uint64_t offset = placement == 0 ? page - fixture->memory_bytes : page - 35;
                uint8_t* data = guard + page;
                for (uint32_t sample = 0; sample < BUSTER_TIER1_NARROW_SAMPLES; sample += 1)
                {
                    memset(data, 0xa7, page);
                    memset(page_expected, 0xa7, page);
                    memset(observed, 0xcc, sizeof(observed));
                    for (uint32_t byte = 0; byte < fixture->memory_bytes; byte += 1)
                    {
                        data[offset + byte] = (uint8_t)(expected->initial[sample] >> (byte * 8));
                        page_expected[offset + byte] = (uint8_t)(expected->memory[sample] >> (byte * 8));
                    }
                    const uint64_t actual = tier1_emit_test_call(native[encoder], (uintptr_t)(data + offset), expected->input[sample]);
                    tier1_emit_test_expect(test, actual == expected->result[sample], "native narrow result has the independent exact canonical 32/64-bit value");
                    tier1_emit_test_expect(test, memcmp(data, page_expected, page) == 0, "narrow memory value and every adjacent byte match the frozen oracle");
                    for (uint32_t node = 0; probes && node < fixture->nodes; node += 1)
                    {
                        if (!plan->covered[node] && (fixture->function.type[node] == IR_I8 || fixture->function.type[node] == IR_I16))
                        {
                            tier1_emit_test_expect(test, observed[node] == expected->values[sample][node], "narrow producer/reload COPY holds zero-extended canonical bits before a later conversion");
                        }
                    }
                }
            }
        }
        for (uint32_t encoder = 0; encoder < 2; encoder += 1)
        {
            if (native[encoder] != MAP_FAILED)
            {
                munmap(native[encoder], mapping_bytes);
            }
        }
    }
    if (guard != MAP_FAILED)
    {
        munmap(guard, page * 3);
    }
    free(page_expected);
    free(records);
}

static void tier1_narrow_compile(Tier1EmitTest* test, Tier1NarrowFixture* fixture)
{
    test->terms = (uint32_t)fixture->kind;
    test->width = tier1_narrow_bits(fixture->narrow);
    test->selects = fixture->sign;
    Tier1NarrowExpected expected;
    bool ready = tier1_narrow_fixture(test, fixture);
    tier1_emit_test_expect(test, ready, "original narrow fixture verifies as semantic IR");
    ready = ready && tier1_narrow_expected(fixture, &expected);
    tier1_emit_test_expect(test, ready, "freeze independent input, per-node, memory and result oracles before selection");
    for (uint32_t vector = 0; ready && vector < 2; vector += 1)
    {
        uint32_t ordinary_spills = 0;
        for (uint32_t remat = 0; remat < 2; remat += 1)
        {
            Tier1SelectPlan plan;
            uint32_t node = BUSTER_IR_NONE;
            const Tier1SelectError selected = tier1_select_plan(&fixture->function, 0, &test->output, &test->scratch, vector != 0, &plan, &node);
            tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "narrow memory and conversion IR selects");
            if (selected == TIER1_SELECT_OK)
            {
                const Tier1EmitOptions options = {
                    .register_mask = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RCX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI),
                    .frame_flags = remat ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
                    .vector = vector != 0,
                    .rematerialize = remat != 0,
                };
                Tier1EmitResult emitted;
                const uint64_t scratch_mark = test->scratch.length;
                const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node);
                tier1_emit_test_expect(test, error == TIER1_EMIT_OK, "narrow liveness, allocation, frame and emission succeed");
                tier1_emit_test_expect(test, test->scratch.length == scratch_mark, "narrow emission restores scratch");
                if (error != TIER1_EMIT_OK && test->failures < 20)
                {
                    fprintf(stderr, "  narrow error=%u node=%u kind=%u source=%u result=%u vector=%u remat=%u\n", error, node, fixture->kind, fixture->input_type, fixture->result_type, vector, remat);
                }
                if (error == TIER1_EMIT_OK)
                {
                    tier1_emit_test_expect(test, tier1_allocation_verify(&emitted.prepared.allocation, &emitted.allocation), "narrow allocation passes independent interference checks");
                    if (fixture->kind == TIER1_NARROW_PRESSURE || fixture->kind == TIER1_NARROW_CONSTANT_PRESSURE)
                    {
                        uint32_t narrow_spills = 0;
                        for (uint32_t slot = 0; slot < emitted.allocation.spill_count; slot += 1)
                        {
                            const uint32_t value = emitted.allocation.spill_values[slot];
                            if (fixture->function.type[value] == fixture->narrow)
                            {
                                narrow_spills += 1;
                                tier1_emit_test_expect(test, emitted.prepared.sizes[value] == 4 && emitted.prepared.alignments[value] == 4, "narrow spills occupy canonical four-byte slots");
                            }
                        }
                        if (fixture->kind == TIER1_NARROW_PRESSURE || remat == 0)
                        {
                            tier1_emit_test_expect(test, narrow_spills != 0 && emitted.spill_stores != 0 && emitted.spill_loads != 0, "narrow pressure really spills and reloads byte/word values");
                        }
                        if (fixture->kind == TIER1_NARROW_CONSTANT_PRESSURE && remat != 0)
                        {
                            const Tier1AllocationInput* input = &emitted.prepared.allocation;
                            const Tier1Allocation* allocation = &emitted.allocation;
                            uint32_t narrow_remats = 0;
                            for (uint32_t interval = 0; interval < input->count; interval += 1)
                            {
                                const uint32_t value = input->values[interval];
                                for (uint32_t fragment = allocation->fragment_offsets[interval]; fragment < allocation->fragment_offsets[interval + 1]; fragment += 1)
                                {
                                    narrow_remats += fixture->function.type[value] == fixture->narrow && fixture->function.op[value] == IR_CONST && allocation->kinds[fragment] == BUSTER_TIER1_ALLOCATE_REMATERIALIZE;
                                }
                            }
                            tier1_emit_test_expect(test, narrow_remats != 0 && emitted.rematerializations != 0, "narrow constants actually leave registers and are recomputed before native use");
                            tier1_emit_test_expect(test, emitted.allocation.spill_count < ordinary_spills, "actual narrow constant rematerialization removes spill slots");
                        }
                    }
                    if (remat == 0)
                    {
                        ordinary_spills = emitted.allocation.spill_count;
                    }
                    tier1_narrow_execute(test, fixture, &expected, &plan, &emitted);
                }
            }
            arena_pop_to(&test->output, 0);
        }
    }
    arena_pop_to(&test->input, 0);
}

static void tier1_narrow_failures(Tier1EmitTest* test)
{
    for (uint32_t narrow = 0; narrow < 2; narrow += 1)
    {
        Tier1NarrowFixture fixture = {
            .kind = TIER1_NARROW_LOAD,
            .narrow = narrow ? IR_I16 : IR_I8,
            .input_type = IR_I64,
            .result_type = IR_I64,
        };
        Tier1SelectPlan plan;
        uint32_t node;
        bool ready = tier1_narrow_fixture(test, &fixture);
        ready = ready && tier1_select_plan(&fixture.function, 0, &test->output, &test->scratch, true, &plan, &node) == TIER1_SELECT_OK;
        tier1_emit_test_expect(test, ready, "narrow refusal fixture selects before target/arena failures");
        if (ready)
        {
            const uint64_t output_mark = test->output.length;
            const uint64_t scratch_mark = test->scratch.length;
            const Tier1EmitOptions options = {
                .register_mask = tier1_liveness_register_mask(),
                .vector = true,
            };
            Tier1EmitResult emitted;
            ready = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) == TIER1_EMIT_OK;
            const uint64_t required = test->output.length;
            arena_pop_to(&test->output, output_mark);
            tier1_emit_test_expect(test, ready, "narrow refusal fixture has a successful retained allocation");
            uint8_t* denied = malloc(X86_FORM_COUNT);
            tier1_emit_test_expect(test, denied != 0, "narrow target denial storage");
            if (denied != 0)
            {
                memcpy(denied, test->allowed, X86_FORM_COUNT);
                denied[narrow ? X86_FORM_MOVZX_R32_RM16 : X86_FORM_MOVZX_R32_RM8] = 0;
            }
            for (uint32_t failure = 0; ready && denied != 0 && failure < 4; failure += 1)
            {
                Arena output = test->output;
                Arena scratch = test->scratch;
                if (failure == 1 || failure == 3)
                {
                    output.capacity = failure == 1 ? output_mark + 1 : required - 1;
                }
                if (failure == 2)
                {
                    scratch.capacity = scratch_mark + 1;
                }
                Tier1EmitResult result;
                Tier1EmitResult before;
                memset(&result, 0xa5, sizeof(result));
                memcpy(&before, &result, sizeof(result));
                const Tier1EmitError error = tier1_emit_function(&plan, &options, failure == 0 ? denied : test->allowed, &output, &scratch, &result, &node);
                tier1_emit_test_expect(test, error == (failure == 0 ? TIER1_EMIT_TARGET : TIER1_EMIT_MEMORY), "narrow form denial and early/late arena failures refuse explicitly");
                tier1_emit_test_expect(test, output.length == output_mark && scratch.length == scratch_mark && memcmp(&before, &result, sizeof(result)) == 0, "failed narrow emission preserves result and both arena lengths");
            }
            free(denied);
        }
        arena_pop_to(&test->output, 0);
        arena_pop_to(&test->input, 0);
    }
    for (uint32_t failure = 0; failure < 11; failure += 1)
    {
        const IrType narrow = failure & 1 ? IR_I16 : IR_I8;
        const IrSizes sizes = {
            .instruction_count = 6,
            .block_count = 1,
            .extra_count = failure == 7 ? 1 : 0,
            .constant_count = 1,
            .call_count = failure == 7 || failure == 8 ? 1 : 0,
            .call_type_count = failure == 7 ? 1 : 0,
            .parameter_count = 1,
        };
        IrFunction function;
        bool ready = ir_create(&test->input, &sizes, &function) == IR_OK;
        if (ready)
        {
            function.result_type = IR_I64;
            function.parameter_types[0] = failure < 2 ? (uint8_t)narrow : IR_I64;
            tier1_emit_test_node(&function, 0, IR_PARAM, (IrType)function.parameter_types[0], 0, BUSTER_IR_NONE);
            tier1_emit_test_node(&function, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE);
            uint32_t count = 2;
            uint32_t value = 0;
            if (failure >= 2)
            {
                tier1_emit_test_node(&function, count, IR_TRUNC, narrow, 0, BUSTER_IR_NONE);
                value = count++;
            }
            if (failure == 2 || failure == 3)
            {
                function.result_type = (uint8_t)narrow;
            }
            else if (failure >= 4 && failure <= 6)
            {
                const IrOp op = failure == 4 ? IR_ADD : failure == 5 ? IR_SHL : IR_ICMP;
                tier1_emit_test_node(&function, count, op, failure == 6 ? IR_I1 : narrow, value, value);
                function.c[count] = failure == 6 ? IR_NE : BUSTER_IR_NONE;
                value = count++;
            }
            else if (failure == 7 || failure == 8)
            {
                function.extra_count = failure == 7;
                if (failure == 7)
                {
                    function.extra[0] = value;
                    function.call_types[0] = (uint8_t)narrow;
                }
                function.calls[0] = (IrCall){
                    .argument_count = failure == 7,
                    .parameter_count = failure == 7,
                    .result_type = failure == 8 ? (uint8_t)narrow : IR_I64,
                };
                tier1_emit_test_node(&function, count, IR_CALL, (IrType)function.calls[0].result_type, 1, 0);
                value = count++;
            }
            else if (failure == 9)
            {
                tier1_emit_test_node(&function, count, IR_ICMP, IR_I1, 0, 1);
                function.c[count] = IR_NE;
                value = count++;
                tier1_emit_test_node(&function, count, IR_SEXT, IR_I64, value, BUSTER_IR_NONE);
                value = count++;
            }
            else if (failure == 10)
            {
                tier1_emit_test_node(&function, count, IR_TRUNC, IR_I1, 0, BUSTER_IR_NONE);
                value = count++;
            }
            if (function.type[value] != function.result_type)
            {
                tier1_emit_test_node(&function, count, IR_ZEXT, (IrType)function.result_type, value, BUSTER_IR_NONE);
                value = count++;
            }
            tier1_emit_test_node(&function, count++, IR_RET, IR_VOID, value, BUSTER_IR_NONE);
            function.instruction_count = count;
            function.block_offsets[1] = count;
            ready = count <= sizes.instruction_count && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK;
            tier1_emit_test_expect(test, ready, "unsupported narrow ABI/arithmetic/conversion fixture is valid IR");
        }
        for (uint32_t vector = 0; ready && vector < 2; vector += 1)
        {
            Tier1SelectPlan result;
            Tier1SelectPlan before;
            memset(&result, 0xa5, sizeof(result));
            memcpy(&before, &result, sizeof(result));
            const uint64_t output_mark = test->output.length;
            const uint64_t scratch_mark = test->scratch.length;
            uint32_t node;
            const Tier1SelectError error = tier1_select_plan(&function, 0, &test->output, &test->scratch, vector != 0, &result, &node);
            tier1_emit_test_expect(test, error == TIER1_SELECT_ERROR_UNSUPPORTED, "narrow PARAM/RET/CALL/arithmetic/compare/shift and I1 SEXT/TRUNC remain unsupported");
            tier1_emit_test_expect(test, test->output.length == output_mark && test->scratch.length == scratch_mark && memcmp(&before, &result, sizeof(result)) == 0, "unsupported narrow selection publishes no partial plan");
        }
        arena_pop_to(&test->output, 0);
        arena_pop_to(&test->input, 0);
    }
}

static void tier1_narrow_cases(Tier1EmitTest* test)
{
    for (uint32_t width = 0; width < 2; width += 1)
    {
        const IrType narrow = width ? IR_I16 : IR_I8;
        for (uint32_t destination = 0; destination < 2; destination += 1)
        {
            for (uint32_t sign = 0; sign < 2; sign += 1)
            {
                for (uint32_t source = 0; source < 3; source += 1)
                {
                    Tier1NarrowFixture fixture = {
                        .kind = source == 0 ? TIER1_NARROW_LOAD : TIER1_NARROW_TRUNCATE,
                        .narrow = narrow,
                        .input_type = source == 1 ? IR_I32 : IR_I64,
                        .result_type = destination ? IR_I64 : IR_I32,
                        .sign = sign != 0,
                    };
                    tier1_narrow_compile(test, &fixture);
                }
            }
            Tier1NarrowFixture selected = {
                .kind = TIER1_NARROW_SELECT,
                .narrow = narrow,
                .input_type = IR_I64,
                .result_type = destination ? IR_I64 : IR_I32,
            };
            tier1_narrow_compile(test, &selected);
            for (uint32_t sign = 0; width == 0 && sign < 2; sign += 1)
            {
                Tier1NarrowFixture widened = {
                    .kind = TIER1_NARROW_WIDEN,
                    .narrow = IR_I8,
                    .input_type = IR_I64,
                    .result_type = destination ? IR_I64 : IR_I32,
                    .sign = sign != 0,
                };
                tier1_narrow_compile(test, &widened);
            }
        }
        Tier1NarrowFixture pressure = {
            .kind = TIER1_NARROW_PRESSURE,
            .narrow = narrow,
            .input_type = IR_I64,
            .result_type = IR_I64,
        };
        tier1_narrow_compile(test, &pressure);
        Tier1NarrowFixture constants = {
            .kind = TIER1_NARROW_CONSTANT_PRESSURE,
            .narrow = narrow,
            .input_type = IR_I64,
            .result_type = IR_I64,
        };
        tier1_narrow_compile(test, &constants);
        Tier1NarrowFixture boolean = {
            .kind = TIER1_NARROW_BOOLEAN,
            .narrow = narrow,
            .input_type = IR_I64,
            .result_type = IR_I64,
        };
        tier1_narrow_compile(test, &boolean);
        Tier1NarrowFixture word = {
            .kind = TIER1_NARROW_WORD,
            .narrow = narrow,
            .input_type = IR_I32,
            .result_type = IR_I64,
            .sign = width != 0,
        };
        tier1_narrow_compile(test, &word);
    }
    Tier1NarrowFixture dword = {
        .kind = TIER1_NARROW_DWORD,
        .narrow = IR_I8,
        .input_type = IR_I64,
        .result_type = IR_I32,
    };
    tier1_narrow_compile(test, &dword);
    for (uint32_t destination = 0; destination < 2; destination += 1)
    {
        Tier1NarrowFixture retruncated = {
            .kind = TIER1_NARROW_RETRUNCATE,
            .narrow = IR_I8,
            .input_type = IR_I64,
            .result_type = destination ? IR_I64 : IR_I32,
        };
        tier1_narrow_compile(test, &retruncated);
        Tier1NarrowFixture boolean = {
            .kind = TIER1_NARROW_BOOLEAN,
            .narrow = destination ? IR_I64 : IR_I32,
            .input_type = IR_I64,
            .result_type = destination ? IR_I64 : IR_I32,
        };
        tier1_narrow_compile(test, &boolean);
    }
    Tier1NarrowFixture bswap = {
        .kind = TIER1_NARROW_BSWAP,
        .narrow = IR_I16,
        .input_type = IR_I64,
        .result_type = IR_I64,
    };
    tier1_narrow_compile(test, &bswap);
    tier1_narrow_failures(test);
}

typedef struct Tier1NarrowChild Tier1NarrowChild;
struct Tier1NarrowChild
{
    int status;
    bool completed;
    bool reaped;
};

static uint64_t tier1_narrow_milliseconds(void)
{
    struct timespec time;
    const bool valid = clock_gettime(CLOCK_MONOTONIC, &time) == 0;
    return valid ? (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000 : UINT64_MAX;
}

static Tier1NarrowChild tier1_narrow_wait(pid_t child)
{
    Tier1NarrowChild result = {0};
    const struct timespec delay = {
        .tv_nsec = 10000000,
    };
    const uint64_t start = tier1_narrow_milliseconds();
    bool waiting = start != UINT64_MAX;
    bool owned = true;
    while (waiting)
    {
        const uint64_t now = tier1_narrow_milliseconds();
        if (now == UINT64_MAX || now < start || now - start >= BUSTER_TIER1_NARROW_TIMEOUT_MS)
        {
            break;
        }
        const pid_t waited = waitpid(child, &result.status, WNOHANG);
        if (waited == child)
        {
            result.completed = true;
            result.reaped = true;
            waiting = false;
        }
        else if (waited < 0 && errno != EINTR)
        {
            owned = false;
            waiting = false;
        }
        else
        {
            nanosleep(&delay, 0);
        }
    }
    if (!result.reaped && owned)
    {
        // The fixture creates no descendants. Kill its one child and bound reaping separately, even when the
        // monotonic clock fails. Neither timeout nor unsuccessful cleanup can publish a completed test report.
        const int killed = kill(child, SIGKILL);
        const bool signalled = killed == 0 || errno == ESRCH;
        const uint64_t cleanup = tier1_narrow_milliseconds();
        for (uint32_t poll = 0; signalled && !result.reaped && poll < BUSTER_TIER1_NARROW_REAP_MS / 10; poll += 1)
        {
            const pid_t waited = waitpid(child, &result.status, WNOHANG);
            if (waited == child)
            {
                result.reaped = true;
            }
            else if (waited < 0 && errno != EINTR)
            {
                break;
            }
            else
            {
                const uint64_t now = tier1_narrow_milliseconds();
                if (cleanup != UINT64_MAX && now != UINT64_MAX && (now < cleanup || now - cleanup >= BUSTER_TIER1_NARROW_REAP_MS))
                {
                    break;
                }
                nanosleep(&delay, 0);
            }
        }
    }
    return result;
}

static void tier1_narrow_tests(Tier1EmitTest* test)
{
    uint64_t* report = mmap(0, 3 * sizeof(uint64_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    const pid_t parent = getpid();
    const pid_t child = report != MAP_FAILED ? fork() : -1;
    if (child == 0)
    {
        const struct rlimit core = {
            .rlim_cur = 0,
            .rlim_max = 0,
        };
        sigset_t unblocked;
        const bool child_ready = setrlimit(RLIMIT_CORE, &core) == 0 && prctl(PR_SET_PDEATHSIG, SIGKILL) == 0 && getppid() == parent &&
            signal(SIGALRM, SIG_DFL) != SIG_ERR && sigemptyset(&unblocked) == 0 && sigaddset(&unblocked, SIGALRM) == 0 && sigprocmask(SIG_UNBLOCK, &unblocked, 0) == 0;
        if (!child_ready)
        {
            _exit(2);
        }
        alarm(BUSTER_TIER1_NARROW_TIMEOUT_MS / 1000);
        const uint64_t checks = test->checks;
        const uint64_t failures = test->failures;
        tier1_narrow_cases(test);
        report[0] = test->checks - checks;
        report[1] = test->failures - failures;
        report[2] = BUSTER_TIER1_NARROW_MARKER;
        fflush(0);
        _exit(report[1] != 0);
    }
    Tier1NarrowChild waited = {0};
    if (child > 0)
    {
        waited = tier1_narrow_wait(child);
    }
    const bool finished = child > 0 && waited.completed && waited.reaped && WIFEXITED(waited.status) && WEXITSTATUS(waited.status) <= 1 && report[2] == BUSTER_TIER1_NARROW_MARKER;
    tier1_emit_test_expect(test, finished, "bounded guarded narrow child completes without signal or timeout");
    tier1_emit_test_expect(test, child > 0 && waited.reaped, "guarded narrow child is reaped within its separate cleanup bound");
    if (finished)
    {
        test->checks += report[0];
        test->failures += report[1];
        tier1_emit_test_expect(test, WEXITSTATUS(waited.status) == (report[1] != 0), "narrow child status agrees with its completed report");
    }
    else
    {
        fprintf(stderr, "tier1 narrow child failed (status=%d, completed=%u, reaped=%u)\n", waited.status, waited.completed, waited.reaped);
    }
    if (report != MAP_FAILED)
    {
        munmap(report, 3 * sizeof(uint64_t));
    }
}

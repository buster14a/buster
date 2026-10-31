// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original normalized-machine fixtures; no imported implementation or corpus.
// Include after tier1_certificate_oracle.c, or build standalone as documented.

#ifdef BUSTER_CERTIFICATE_STANDALONE
#include "tier1_certificate.c"
#include "tier1_certificate_oracle.c"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

typedef struct Tier1CertFixture Tier1CertFixture;
struct Tier1CertFixture
{
    Tier1CertProgram program;
    Tier1CertLocation locations[20];
    Tier1CertValue values[16];
    Tier1CertBlock blocks[4];
    Tier1CertSource source[4][16];
    Tier1CertInstruction code[4][64];
    Tier1CertFact facts[4][16];
    Tier1CertEdge edges[4][3];
    Tier1CertRename renames[4][3][16];
    Tier1CertInstruction moves[4][3][16];
    Tier1CertFact entry[16];
    Tier1CertFact exit[16];
};

typedef struct Tier1CertTest Tier1CertTest;
struct Tier1CertTest
{
    void* fast;
    void* slow;
    size_t capacity;
    uint64_t checks;
    uint64_t failures;
};

static Tier1CertOperand tier1_cert_test_operand(uint32_t value)
{
    const Tier1CertOperand result = {
        .value = value,
        .allowed_registers = UINT32_MAX,
    };
    return result;
}

static Tier1CertInstruction tier1_cert_test_edit(uint8_t op, uint32_t a, uint32_t b, uint32_t c)
{
    const Tier1CertInstruction result = {
        .a = a,
        .b = b,
        .c = c,
        .op = op,
    };
    return result;
}

static void tier1_cert_test_init(Tier1CertFixture* fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    for (uint32_t i = 0; i < 4; i += 1)
    {
        fixture->locations[i] = (Tier1CertLocation){
            .unit = 8 * i,
            .width = 8,
            .extent = 8,
            .class = TIER1_CERT_GPR,
            .reg = (uint8_t)i,
        };
    }
    fixture->locations[4] = (Tier1CertLocation){
        .unit = 32,
        .width = 8,
        .extent = 8,
        .class = TIER1_CERT_STACK,
    };
    fixture->locations[5] = (Tier1CertLocation){
        .unit = 40,
        .width = 8,
        .extent = 8,
        .class = TIER1_CERT_STACK,
    };
    fixture->locations[6] = (Tier1CertLocation){
        .unit = 0,
        .width = 1,
        .extent = 1,
        .class = TIER1_CERT_GPR,
    };
    fixture->locations[7] = (Tier1CertLocation){
        .unit = 0,
        .width = 4,
        .extent = 8,
        .class = TIER1_CERT_GPR,
    };
    fixture->locations[8] = (Tier1CertLocation){
        .unit = 48,
        .width = 1,
        .extent = 1,
        .class = TIER1_CERT_FLAGS,
    };
    fixture->locations[9] = (Tier1CertLocation){
        .unit = 64,
        .width = 16,
        .extent = 16,
        .class = TIER1_CERT_VECTOR,
    };
    fixture->locations[10] = (Tier1CertLocation){
        .unit = 80,
        .width = 16,
        .extent = 16,
        .class = TIER1_CERT_VECTOR,
        .reg = 1,
    };
    fixture->locations[11] = (Tier1CertLocation){
        .unit = 96,
        .width = 2,
        .extent = 8,
        .class = TIER1_CERT_MASK,
    };
    fixture->locations[12] = (Tier1CertLocation){
        .unit = 104,
        .width = 2,
        .extent = 8,
        .class = TIER1_CERT_MASK,
        .reg = 1,
    };
    fixture->locations[13] = (Tier1CertLocation){
        .unit = 112,
        .width = 2,
        .extent = 8,
        .class = TIER1_CERT_MASK,
        .reg = 2,
    };
    fixture->locations[14] = (Tier1CertLocation){
        .unit = 16,
        .width = 1,
        .extent = 1,
        .class = TIER1_CERT_GPR,
        .reg = 2,
    };
    fixture->locations[15] = (Tier1CertLocation){
        .unit = 36,
        .width = 4,
        .extent = 4,
        .class = TIER1_CERT_STACK,
    };
    for (uint32_t i = 0; i < 16; i += 1)
    {
        fixture->values[i] = (Tier1CertValue){
            .width = 8,
            .class = TIER1_CERT_GPR,
        };
    }
    for (uint32_t b = 0; b < 4; b += 1)
    {
        fixture->blocks[b] = (Tier1CertBlock){
            .source = fixture->source[b],
            .code = fixture->code[b],
            .certificate = fixture->facts[b],
            .edges = fixture->edges[b],
        };
        for (uint32_t e = 0; e < 3; e += 1)
        {
            fixture->edges[b][e] = (Tier1CertEdge){
                .renames = fixture->renames[b][e],
                .moves = fixture->moves[b][e],
            };
        }
    }
    fixture->entry[0] = (Tier1CertFact){
        .value = 0,
        .location = 0,
    };
    fixture->entry[1] = (Tier1CertFact){
        .value = 1,
        .location = 1,
    };
    fixture->facts[0][0] = fixture->entry[0];
    fixture->facts[0][1] = fixture->entry[1];
    fixture->blocks[0].certificate_count = 2;
    fixture->program = (Tier1CertProgram){
        .locations = fixture->locations,
        .values = fixture->values,
        .blocks = fixture->blocks,
        .entry = fixture->entry,
        .exit = fixture->exit,
        .unit_count = 128,
        .location_count = 16,
        .value_count = 8,
        .block_count = 1,
        .entry_count = 2,
        .flags_location = 8,
        .features = BUSTER_CERT_AVX512F | BUSTER_CERT_AVX512BW | BUSTER_CERT_AVX512DQ | BUSTER_CERT_AVX2,
    };
}

static void tier1_cert_test_use(Tier1CertFixture* fixture, uint32_t block, uint32_t value, uint32_t location, bool memory)
{
    const uint32_t source_index = fixture->blocks[block].source_count++;
    const uint32_t code_index = fixture->blocks[block].code_count++;
    fixture->source[block][source_index] = (Tier1CertSource){
        .opcode = 77,
        .use_count = 1,
    };
    fixture->source[block][source_index].operands[0] = tier1_cert_test_operand(value);
    fixture->source[block][source_index].operands[0].flags = memory ? BUSTER_CERT_MEMORY : 0;
    fixture->code[block][code_index] = (Tier1CertInstruction){
        .opcode = 77,
        .locations = { location },
        .operand_count = 1,
        .op = TIER1_CERT_ORIGINAL,
    };
}

static void tier1_cert_test_def(Tier1CertFixture* fixture, uint32_t block, uint32_t value, uint32_t location)
{
    const uint32_t source_index = fixture->blocks[block].source_count++;
    const uint32_t code_index = fixture->blocks[block].code_count++;
    fixture->source[block][source_index] = (Tier1CertSource){
        .opcode = 88,
        .def_count = 1,
    };
    fixture->source[block][source_index].operands[0] = tier1_cert_test_operand(value);
    fixture->code[block][code_index] = (Tier1CertInstruction){
        .opcode = 88,
        .locations = { location },
        .operand_count = 1,
        .op = TIER1_CERT_ORIGINAL,
    };
}

static void tier1_cert_test_expect(Tier1CertTest* test, bool condition, const char* name)
{
    test->checks += 1;
    if (!condition)
    {
        if (test->failures < 20)
        {
            fprintf(stderr, "test-tier1-certificate: %s\n", name);
        }
        test->failures += 1;
    }
}

static bool tier1_cert_test_pair(Tier1CertTest* test, const Tier1CertFixture* fixture, int expected, const char* name)
{
    Tier1CertReport fast = { 0 };
    Tier1CertReport slow = { 0 };
    const size_t required = tier1_certificate_scratch_size(&fixture->program);
    const size_t capacity = required < test->capacity ? required : test->capacity;
    const Tier1CertError a = tier1_certificate_check(&fixture->program, test->fast, capacity, &fast);
    const Tier1CertError b = tier1_certificate_oracle(&fixture->program, test->slow, test->capacity, &slow);
    const bool accepted = a == TIER1_CERT_OK;
    const bool valid = accepted == (b == TIER1_CERT_OK) && (expected < 0 || accepted == (expected != 0)) && (!accepted || fast.scratch_bytes <= required);
    if (!valid && test->failures < 20)
    {
        fprintf(stderr, "%s: fast=%u at %u/%u/%u, slow=%u at %u/%u/%u\n", name, a, fast.block, fast.edge, fast.instruction, b, slow.block, slow.edge, slow.instruction);
    }
    tier1_cert_test_expect(test, valid, name);
    return accepted;
}

static void tier1_cert_test_exchange(Tier1CertFixture* fixture, uint32_t a, uint32_t b)
{
    fixture->program.block_count = 2;
    fixture->blocks[0].edge_count = 1;
    fixture->edges[0][0].destination = 1;
    fixture->edges[0][0].move_count = 1;
    fixture->moves[0][0][0] = tier1_cert_test_edit(TIER1_CERT_XCHG, a, b, 0);
    fixture->facts[1][0] = (Tier1CertFact){
        .value = 0,
        .location = b,
    };
    fixture->facts[1][1] = (Tier1CertFact){
        .value = 1,
        .location = a,
    };
    fixture->blocks[1].certificate_count = 2;
    tier1_cert_test_use(fixture, 1, 0, b, false);
    tier1_cert_test_use(fixture, 1, 1, a, false);
}

static void tier1_cert_test_xors(Tier1CertInstruction* code, uint32_t a, uint32_t b)
{
    code[0] = tier1_cert_test_edit(TIER1_CERT_XOR, a, a, b);
    code[1] = tier1_cert_test_edit(TIER1_CERT_XOR, b, b, a);
    code[2] = tier1_cert_test_edit(TIER1_CERT_XOR, a, a, b);
}

static void tier1_cert_test_cases(Tier1CertTest* test)
{
    Tier1CertFixture f;
    tier1_cert_test_init(&f);
    tier1_cert_test_exchange(&f, 0, 1);
    tier1_cert_test_xors(f.moves[0][0], 0, 1);
    f.edges[0][0].move_count = 3;
    f.locations[16] = f.locations[0];
    f.locations[17] = f.locations[1];
    f.program.location_count = 18;
    f.moves[0][0][1] = tier1_cert_test_edit(TIER1_CERT_XOR, 17, 17, 16);
    f.moves[0][0][2] = tier1_cert_test_edit(TIER1_CERT_XOR, 16, 16, 17);
    tier1_cert_test_pair(test, &f, 1, "equal physical views need not share a descriptor id");

    tier1_cert_test_init(&f);
    f.values[0].width = 4;
    f.values[1].width = 4;
    f.locations[16] = f.locations[7];
    f.locations[16].unit = 8;
    f.locations[16].reg = 1;
    f.program.location_count = 17;
    f.entry[0].location = f.facts[0][0].location = 7;
    f.entry[1].location = f.facts[0][1].location = 16;
    tier1_cert_test_exchange(&f, 7, 16);
    tier1_cert_test_xors(f.moves[0][0], 7, 16);
    f.edges[0][0].move_count = 3;
    tier1_cert_test_pair(test, &f, 1, "same-width r32 XOR exchange");
    f.moves[0][0][1] = tier1_cert_test_edit(TIER1_CERT_XOR, 1, 1, 0);
    tier1_cert_test_pair(test, &f, 0, "mixed-width XOR network outside the supported recipe family");
    tier1_cert_test_init(&f);
    tier1_cert_test_use(&f, 0, 0, 0, false);
    tier1_cert_test_use(&f, 0, 1, 1, false);
    tier1_cert_test_pair(test, &f, 1, "ABI-backed straight line");
    f.code[0][0].operand_count = 0;
    tier1_cert_test_pair(test, &f, 0, "omitted original use");
    f.code[0][0].operand_count = 1;
    f.code[0][0].opcode += 1;
    tier1_cert_test_pair(test, &f, 0, "changed opcode");
    f.code[0][0].opcode -= 1;
    f.code[0][0].immediate = 9;
    tier1_cert_test_pair(test, &f, 0, "changed immediate");
    f.code[0][0].immediate = 0;
    f.code[0][0].effect = 9;
    tier1_cert_test_pair(test, &f, 0, "changed original memory effect");
    f.code[0][0].effect = 0;
    f.blocks[0].code_count -= 1;
    tier1_cert_test_pair(test, &f, 0, "omitted original instruction");

    tier1_cert_test_init(&f);
    f.blocks[0].edge_count = 1;
    f.edges[0][0].destination = 0;
    f.program.entry_count = 0;
    tier1_cert_test_pair(test, &f, 0, "uninitialized cyclic certificate");
    f.program.entry_count = 2;
    tier1_cert_test_pair(test, &f, 1, "initialized self loop");

    tier1_cert_test_init(&f);
    tier1_cert_test_exchange(&f, 0, 1);
    tier1_cert_test_pair(test, &f, 1, "scratch-free GPR exchange");
    f.edges[0][0].move_count = 2;
    f.moves[0][0][0] = tier1_cert_test_edit(TIER1_CERT_MOVE, 0, 1, 0);
    f.moves[0][0][1] = tier1_cert_test_edit(TIER1_CERT_MOVE, 1, 0, 0);
    tier1_cert_test_pair(test, &f, 0, "destructive sequential cycle");
    f.moves[0][0][0] = tier1_cert_test_edit(TIER1_CERT_MOVE, 0, 2, 0);
    tier1_cert_test_pair(test, &f, 0, "uninitialized temporary");
    f.moves[0][0][0].b = f.program.location_count;
    tier1_cert_test_pair(test, &f, 0, "undeclared temporary");

    tier1_cert_test_init(&f);
    tier1_cert_test_exchange(&f, 0, 1);
    f.edges[0][0].move_count = 3;
    tier1_cert_test_xors(f.moves[0][0], 0, 1);
    tier1_cert_test_pair(test, &f, 1, "concrete scratch-free XOR exchange");
    f.moves[0][0][1].c = 1;
    tier1_cert_test_pair(test, &f, 0, "corrupt XOR middle instruction");
    f.moves[0][0][1].c = 0;
    f.values[2].width = 1;
    f.values[2].class = TIER1_CERT_FLAGS;
    f.entry[2] = (Tier1CertFact){
        .value = 2,
        .location = 8,
    };
    f.facts[0][2] = f.entry[2];
    f.program.entry_count = 3;
    f.blocks[0].certificate_count = 3;
    f.facts[1][2] = f.entry[2];
    f.blocks[1].certificate_count = 3;
    tier1_cert_test_use(&f, 1, 2, 8, false);
    tier1_cert_test_pair(test, &f, 0, "GPR XOR cannot preserve live flags");
    f.edges[0][0].move_count = 1;
    f.moves[0][0][0] = tier1_cert_test_edit(TIER1_CERT_XCHG, 0, 1, 0);
    tier1_cert_test_pair(test, &f, 1, "XCHG preserves live flags");

    for (uint32_t class = 0; class < 2; class += 1)
    {
        tier1_cert_test_init(&f);
        const uint32_t a = class == 0 ? 9 : 11;
        const uint32_t b = a + 1;
        for (uint32_t v = 0; v < 2; v += 1)
        {
            f.values[v].class = class == 0 ? TIER1_CERT_VECTOR : TIER1_CERT_MASK;
            f.values[v].width = class == 0 ? 16 : 2;
            f.entry[v].location = a + v;
            f.facts[0][v] = f.entry[v];
        }
        tier1_cert_test_exchange(&f, a, b);
        f.edges[0][0].move_count = 3;
        tier1_cert_test_xors(f.moves[0][0], a, b);
        tier1_cert_test_pair(test, &f, 1, class == 0 ? "vector XOR exchange" : "mask XOR exchange");
        f.moves[0][0][1].c = b;
        tier1_cert_test_pair(test, &f, 0, "vector/mask corrupted XOR");
        if (class != 0)
        {
            f.moves[0][0][0] = tier1_cert_test_edit(TIER1_CERT_MOVE, 13, a, 0);
            f.moves[0][0][1] = tier1_cert_test_edit(TIER1_CERT_MOVE, a, b, 0);
            f.moves[0][0][2] = tier1_cert_test_edit(TIER1_CERT_MOVE, b, 13, 0);
            tier1_cert_test_pair(test, &f, 1, "KMOV through free mask temporary");
            f.program.features = 0;
            tier1_cert_test_pair(test, &f, 0, "mask transition requires target feature");
        }
    }

    tier1_cert_test_init(&f);
    f.program.block_count = 2;
    f.blocks[0].edge_count = 1;
    f.edges[0][0].destination = 1;
    f.blocks[1].certificate_count = 1;
    f.facts[1][0] = (Tier1CertFact){
        .value = 0,
        .location = 4,
    };
    tier1_cert_test_use(&f, 1, 0, 4, true);
    tier1_cert_test_pair(test, &f, 0, "forged valid spill home");
    f.edges[0][0].move_count = 1;
    f.moves[0][0][0] = tier1_cert_test_edit(TIER1_CERT_MOVE, 4, 0, 0);
    tier1_cert_test_pair(test, &f, 1, "actual store establishes home");
    f.code[1][0].locations[0] = 0;
    tier1_cert_test_pair(test, &f, 0, "uncertified register starts unknown");

    tier1_cert_test_init(&f);
    f.values[2].width = 1;
    f.entry[2] = (Tier1CertFact){
        .value = 2,
        .location = 14,
    };
    f.facts[0][2] = f.entry[2];
    f.program.entry_count = 3;
    f.blocks[0].certificate_count = 3;
    f.code[0][f.blocks[0].code_count++] = tier1_cert_test_edit(TIER1_CERT_MOVE, 6, 14, 0);
    tier1_cert_test_use(&f, 0, 0, 0, false);
    tier1_cert_test_pair(test, &f, 0, "partial-register corruption");

    tier1_cert_test_init(&f);
    f.values[2].width = 4;
    tier1_cert_test_def(&f, 0, 2, 7);
    tier1_cert_test_use(&f, 0, 0, 0, false);
    tier1_cert_test_pair(test, &f, 0, "r32 write invalidates upper r64 bytes");

    tier1_cert_test_init(&f);
    f.source[0][0] = (Tier1CertSource){
        .opcode = 99,
        .use_count = 2,
        .def_count = 1,
    };
    f.source[0][0].operands[0] = tier1_cert_test_operand(0);
    f.source[0][0].operands[1] = tier1_cert_test_operand(1);
    f.source[0][0].operands[2] = tier1_cert_test_operand(2);
    f.source[0][0].operands[2].tie = 1;
    f.code[0][0] = (Tier1CertInstruction){
        .opcode = 99,
        .locations = { 0, 1, 0 },
        .operand_count = 3,
    };
    f.blocks[0].source_count = 1;
    f.blocks[0].code_count = 1;
    tier1_cert_test_use(&f, 0, 0, 0, false);
    tier1_cert_test_pair(test, &f, 0, "live tied source without its copy");
    f.code[0][2] = f.code[0][1];
    f.code[0][1] = f.code[0][0];
    f.code[0][1].locations[0] = 2;
    f.code[0][1].locations[2] = 2;
    f.code[0][0] = tier1_cert_test_edit(TIER1_CERT_MOVE, 2, 0, 0);
    f.blocks[0].code_count = 3;
    tier1_cert_test_pair(test, &f, 1, "copy protects live tied source");

    tier1_cert_test_init(&f);
    f.source[0][0] = (Tier1CertSource){
        .opcode = 100,
        .after = { 0 },
        .after_count = 1,
    };
    f.code[0][0] = (Tier1CertInstruction){
        .opcode = 100,
    };
    f.blocks[0].source_count = 1;
    f.blocks[0].code_count = 1;
    tier1_cert_test_use(&f, 0, 0, 0, false);
    tier1_cert_test_pair(test, &f, 0, "call clobbers survivor");
    f.source[0][1].operands[0].allowed_registers = 4;
    f.code[0][2] = f.code[0][1];
    f.code[0][2].locations[0] = 1;
    f.code[0][1] = f.code[0][0];
    f.code[0][0] = tier1_cert_test_edit(TIER1_CERT_MOVE, 1, 0, 0);
    f.blocks[0].code_count = 3;
    tier1_cert_test_pair(test, &f, 0, "call survivor violates restricted mask");
    f.code[0][0].a = 2;
    f.code[0][2].locations[0] = 2;
    tier1_cert_test_pair(test, &f, 1, "call survivor in allowed nonclobbered register");
    f.source[0][0].before[0] = 2;
    f.source[0][0].before_count = 1;
    tier1_cert_test_pair(test, &f, 0, "implicit early clobber");

    tier1_cert_test_init(&f);
    f.code[0][f.blocks[0].code_count++] = tier1_cert_test_edit(TIER1_CERT_MOVE, 4, 0, 0);
    f.code[0][f.blocks[0].code_count++] = tier1_cert_test_edit(TIER1_CERT_MOVE, 4, 1, 0);
    tier1_cert_test_use(&f, 0, 0, 4, true);
    tier1_cert_test_pair(test, &f, 0, "reused stack slot invalidates previous value");
    f.code[0][1].a = 5;
    tier1_cert_test_pair(test, &f, 1, "nonaliasing stack slots");
    f.source[0][0].before[0] = 15;
    f.source[0][0].before_count = 1;
    tier1_cert_test_pair(test, &f, 0, "original memory effect overlaps partial stack home");

    tier1_cert_test_init(&f);
    f.code[0][f.blocks[0].code_count++] = tier1_cert_test_edit(TIER1_CERT_MOVE, 4, 0, 0);
    tier1_cert_test_def(&f, 0, 0, 0);
    tier1_cert_test_use(&f, 0, 0, 4, true);
    tier1_cert_test_pair(test, &f, 0, "redefinition cannot reuse earlier incarnation's home");

    tier1_cert_test_init(&f);
    f.values[2].recipe = TIER1_CERT_INTEGER_RECIPE;
    f.values[2].payload = 42;
    f.facts[0][2] = (Tier1CertFact){
        .value = 2,
        .location = BUSTER_CERT_NONE,
    };
    f.blocks[0].certificate_count = 3;
    f.code[0][0] = (Tier1CertInstruction){
        .immediate = 42,
        .a = 2,
        .value = 2,
        .op = TIER1_CERT_CONSTANT,
    };
    f.blocks[0].code_count = 1;
    tier1_cert_test_use(&f, 0, 2, 2, false);
    tier1_cert_test_pair(test, &f, 1, "constant recipe actually materialized");
    f.code[0][0] = f.code[0][1];
    f.blocks[0].code_count = 1;
    tier1_cert_test_pair(test, &f, 0, "valid recipe without materializing instruction");
    f.code[0][1] = f.code[0][0];
    f.code[0][0] = (Tier1CertInstruction){
        .immediate = 43,
        .a = 2,
        .value = 2,
        .op = TIER1_CERT_CONSTANT,
    };
    f.blocks[0].code_count = 2;
    tier1_cert_test_pair(test, &f, 0, "wrong materialized constant");
    f.values[2].recipe = TIER1_CERT_FRAME_RECIPE;
    f.values[2].dependency = 0;
    f.values[2].stable = true;
    f.values[2].payload = (uint64_t)(int64_t)-32;
    f.code[0][0] = (Tier1CertInstruction){
        .immediate = (uint64_t)(int64_t)-32,
        .a = 2,
        .b = 0,
        .value = 2,
        .op = TIER1_CERT_FRAME_ADDRESS,
    };
    tier1_cert_test_pair(test, &f, 1, "stable frame recipe with established base");
    f.code[0][0].b = 1;
    tier1_cert_test_pair(test, &f, 0, "frame recipe with wrong dependency");
    f.code[0][0].b = 0;
    f.values[2].stable = false;
    tier1_cert_test_pair(test, &f, 0, "unstable frame recipe");

    tier1_cert_test_init(&f);
    f.program.exit_count = 1;
    f.exit[0] = f.entry[0];
    tier1_cert_test_def(&f, 0, 2, 0);
    tier1_cert_test_pair(test, &f, 0, "callee-saved ABI exit obligation");
    f.code[0][1] = f.code[0][0];
    f.code[0][0] = tier1_cert_test_edit(TIER1_CERT_MOVE, 4, 0, 0);
    f.code[0][2] = tier1_cert_test_edit(TIER1_CERT_MOVE, 0, 4, 0);
    f.blocks[0].code_count = 3;
    tier1_cert_test_pair(test, &f, 1, "callee-saved register restored");

    tier1_cert_test_init(&f);
    f.source[0][0] = (Tier1CertSource){
        .opcode = 101,
        .use_count = 1,
        .def_count = 1,
        .copy = true,
    };
    f.source[0][0].operands[0] = tier1_cert_test_operand(0);
    f.source[0][0].operands[1] = tier1_cert_test_operand(2);
    f.code[0][0] = (Tier1CertInstruction){
        .opcode = 101,
        .locations = { 0, 2 },
        .operand_count = 2,
    };
    f.blocks[0].source_count = 1;
    f.blocks[0].code_count = 1;
    tier1_cert_test_use(&f, 0, 2, 0, false);
    tier1_cert_test_use(&f, 0, 0, 2, false);
    tier1_cert_test_pair(test, &f, 1, "semantic copies preserve relational aliases");

    tier1_cert_test_init(&f);
    f.program.block_count = 2;
    f.blocks[0].edge_count = 1;
    f.edges[0][0].destination = 1;
    f.edges[0][0].rename_count = 2;
    f.renames[0][0][0] = (Tier1CertRename){
        .destination = 2,
        .source = 0,
    };
    f.renames[0][0][1] = (Tier1CertRename){
        .destination = 3,
        .source = 0,
    };
    f.blocks[1].certificate_count = 2;
    f.facts[1][0] = (Tier1CertFact){
        .value = 2,
        .location = 0,
    };
    f.facts[1][1] = (Tier1CertFact){
        .value = 3,
        .location = 0,
    };
    tier1_cert_test_use(&f, 1, 2, 0, false);
    tier1_cert_test_use(&f, 1, 3, 0, false);
    tier1_cert_test_pair(test, &f, 1, "two phi names may alias one location");
    f.renames[0][0][1].source = 1;
    tier1_cert_test_pair(test, &f, 0, "certificate cannot assert unrelated aliases");
    f.renames[0][0][1].destination = 2;
    tier1_cert_test_pair(test, &f, 0, "duplicate phi destination");

    tier1_cert_test_init(&f);
    f.program.block_count = 3;
    f.blocks[0].edge_count = 1;
    f.edges[0][0].destination = 1;
    f.edges[0][0].rename_count = 2;
    f.renames[0][0][0] = (Tier1CertRename){
        .destination = 2,
        .source = 0,
    };
    f.renames[0][0][1] = (Tier1CertRename){
        .destination = 3,
        .source = 1,
    };
    for (uint32_t b = 1; b <= 2; b += 1)
    {
        f.blocks[b].certificate_count = 2;
        f.facts[b][0] = (Tier1CertFact){
            .value = 2,
            .location = 0,
        };
        f.facts[b][1] = (Tier1CertFact){
            .value = 3,
            .location = 1,
        };
        tier1_cert_test_use(&f, b, 2, 0, false);
        tier1_cert_test_use(&f, b, 3, 1, false);
    }
    f.blocks[1].edge_count = 2;
    f.edges[1][0].destination = 1;
    f.edges[1][0].rename_count = 2;
    f.renames[1][0][0] = (Tier1CertRename){
        .destination = 2,
        .source = 3,
    };
    f.renames[1][0][1] = (Tier1CertRename){
        .destination = 3,
        .source = 2,
    };
    f.edges[1][0].move_count = 1;
    f.moves[1][0][0] = tier1_cert_test_edit(TIER1_CERT_XCHG, 0, 1, 0);
    f.edges[1][1].destination = 2;
    tier1_cert_test_pair(test, &f, 1, "loop with simultaneous phi cycle and exit");
    f.edges[1][0].move_count = 0;
    tier1_cert_test_pair(test, &f, 0, "swapped phi edge without exchange");

    tier1_cert_test_init(&f);
    f.program.block_count = 3;
    f.blocks[0].edge_count = 2;
    f.edges[0][0].destination = 1;
    f.edges[0][0].move_count = 1;
    f.moves[0][0][0] = tier1_cert_test_edit(TIER1_CERT_XCHG, 0, 1, 0);
    f.edges[0][1].destination = 2;
    f.blocks[1].edge_count = 1;
    f.edges[1][0].destination = 2;
    f.edges[1][0].move_count = 1;
    f.moves[1][0][0] = tier1_cert_test_edit(TIER1_CERT_XCHG, 0, 1, 0);
    for (uint32_t b = 1; b <= 2; b += 1)
    {
        f.blocks[b].certificate_count = 2;
        for (uint32_t v = 0; v < 2; v += 1)
        {
            const uint32_t location = b == 1 ? 1 - v : v;
            f.facts[b][v] = (Tier1CertFact){
                .value = v,
                .location = location,
            };
            tier1_cert_test_use(&f, b, v, location, false);
        }
    }
    tier1_cert_test_pair(test, &f, 1, "critical edge and isolated successor overlays");
    f.edges[1][0].move_count = 0;
    tier1_cert_test_pair(test, &f, 0, "critical-edge predecessor missing restore");
    f.program.block_count = 4;
    tier1_cert_test_pair(test, &f, 0, "unreachable cyclic assertions are not trusted");
}

static uint32_t tier1_cert_test_random(uint64_t* seed)
{
    *seed = *seed * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return (uint32_t)(*seed >> 32);
}

static void tier1_cert_test_fuzz(Tier1CertTest* test)
{
    uint64_t seed = UINT64_C(0x939140);
    Tier1CertFixture f;
    for (uint32_t round = 0; round < 1000; round += 1)
    {
        tier1_cert_test_init(&f);
        f.program.entry_count = 4;
        f.blocks[0].certificate_count = 4;
        for (uint32_t v = 0; v < 4; v += 1)
        {
            f.entry[v] = (Tier1CertFact){
                .value = v,
                .location = v,
            };
            f.facts[0][v] = f.entry[v];
        }
        f.program.block_count = 2;
        f.blocks[0].edge_count = 1;
        f.edges[0][0].destination = 1;
        f.edges[0][0].move_count = 12;
        uint32_t contents[] = { 0, 1, 2, 3 };
        for (uint32_t i = 0; i < 12; i += 1)
        {
            const uint32_t a = tier1_cert_test_random(&seed) % 4;
            const uint32_t b = (a + 1 + tier1_cert_test_random(&seed) % 3) % 4;
            const bool exchange = (tier1_cert_test_random(&seed) & 1) != 0;
            f.moves[0][0][i] = tier1_cert_test_edit(exchange ? TIER1_CERT_XCHG : TIER1_CERT_MOVE, a, b, 0);
            const uint32_t old = contents[a];
            contents[a] = contents[b];
            if (exchange)
            {
                contents[b] = old;
            }
        }
        uint32_t seen = 0;
        for (uint32_t r = 0; r < 4; r += 1)
        {
            const uint32_t v = contents[r];
            if (((seen >> v) & 1) == 0)
            {
                f.facts[1][f.blocks[1].certificate_count++] = (Tier1CertFact){
                    .value = v,
                    .location = r,
                };
                tier1_cert_test_use(&f, 1, v, r, false);
                seen |= 1u << v;
            }
        }
        tier1_cert_test_pair(test, &f, 1, "generated transition with independently tracked contents");
        const uint32_t edit = tier1_cert_test_random(&seed) % 12;
        f.moves[0][0][edit].b = tier1_cert_test_random(&seed) % 4;
        tier1_cert_test_pair(test, &f, -1, "mutated transition agrees with independent fixpoint");
    }
}

static void tier1_cert_test_bounds(Tier1CertTest* test)
{
    Tier1CertFixture f;
    tier1_cert_test_init(&f);
    tier1_cert_test_use(&f, 0, 0, 0, false);
    Tier1CertReport report = { 0 };
    tier1_certificate_check(&f.program, test->fast, test->capacity, &report);
    const size_t required = (size_t)report.scratch_bytes;
    unsigned char* const bytes = test->fast;
    bool valid = true;
    for (size_t capacity = 0; capacity <= required + 32; capacity += 1)
    {
        memset(bytes, 0xa5, required + 64);
        const Tier1CertError result = tier1_certificate_check(&f.program, bytes + 1, capacity, &report);
        valid = valid && (result == TIER1_CERT_CAPACITY || result == TIER1_CERT_OK);
        valid = valid && bytes[0] == 0xa5 && bytes[capacity + 1] == 0xa5;
        if (capacity >= required + 16)
        {
            valid = valid && result == TIER1_CERT_OK;
        }
    }
    tier1_cert_test_expect(test, valid, "unaligned scratch capacity sweep and canaries");
    f.code[0][0].locations[0] = UINT32_MAX;
    tier1_cert_test_pair(test, &f, 0, "bad operand index fails before state access");
    f.code[0][0].locations[0] = 0;
    f.blocks[0].certificate_count = 3;
    f.facts[0][2] = f.facts[0][0];
    tier1_cert_test_pair(test, &f, 0, "duplicate one-location assertion");
    f.blocks[0].certificate_count = 2;
    f.program.location_count = UINT32_MAX;
    tier1_cert_test_pair(test, &f, 0, "oversized domain rejected");
    tier1_cert_test_expect(test, tier1_certificate_check(0, test->fast, test->capacity, &report) == TIER1_CERT_INVALID, "null source rejected");
    tier1_cert_test_expect(test, tier1_certificate_oracle(0, test->slow, test->capacity, &report) == TIER1_CERT_INVALID, "oracle null source rejected");

    tier1_cert_test_init(&f);
    f.program.entry_count = 0;
    f.blocks[0].certificate_count = 0;
    f.program.unit_count = 0;
    f.program.location_count = 0;
    f.program.value_count = 0;
    f.program.flags_location = BUSTER_CERT_NONE;
    tier1_cert_test_pair(test, &f, 1, "empty function with zero storage and values");
}

static int run_tier1_certificate_tests(void)
{
    Tier1CertTest test = {
        .capacity = 4u << 20,
    };
    test.fast = malloc(test.capacity);
    test.slow = malloc(test.capacity);
    if (test.fast == 0 || test.slow == 0)
    {
        test.failures += 1;
    }
    else
    {
        tier1_cert_test_cases(&test);
        tier1_cert_test_fuzz(&test);
        tier1_cert_test_bounds(&test);
    }
    free(test.slow);
    free(test.fast);
    printf("test-tier1-certificate: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    return test.failures == 0 ? 0 : 1;
}

#ifdef BUSTER_CERTIFICATE_STANDALONE
int main(void)
{
    return run_tier1_certificate_tests();
}
#endif

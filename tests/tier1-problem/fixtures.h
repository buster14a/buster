// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Original synthetic fixtures; no LLVM output or third-party corpus is embedded.

typedef struct ProblemFixture ProblemFixture;
struct ProblemFixture
{
    Tier1Problem problem;
    uint32_t blocks[2][8];
    uint32_t edges[2][8];
    uint32_t instructions[4][8];
    uint32_t values[3][4];
    uint32_t operands[10][10];
    uint32_t registers[4][8];
    uint32_t classes[2][5];
    uint32_t members[6];
    uint32_t units[4];
    uint32_t phis[2][3];
    uint32_t forms[1][4];
};

static void problem_fixture(ProblemFixture* f)
{
    static const char* const metadata[BUSTER_TP_META_COUNT] =
    {
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
        "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
        "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
        "x86_64-unknown-linux-gnu",
        "x86-64",
        "",
        "-O2 -stop-before=phi-node-elimination",
        "sysv-x86_64",
        "synthetic frequencies; NOT an LLVM capture",
        "original-synthetic-fixture",
        "three-values",
    };
    memset(f, 0, sizeof(*f));
    Tier1Problem* p = &f->problem;
    p->flags = BUSTER_TP_PRE_PHI | BUSTER_TP_FROZEN_ORDER | BUSTER_TP_SYNTHETIC;
    p->original_operands = 7;
    p->unit_count = 3;
    for (uint32_t m = 0; m < BUSTER_TP_META_COUNT; m += 1)
    {
        p->metadata[m].data = (const uint8_t*)metadata[m];
        p->metadata[m].size = (uint32_t)strlen(metadata[m]);
    }
    const void* rows[BUSTER_TP_TABLE_COUNT] =
    {
        f->blocks,
        f->edges,
        f->instructions,
        f->values,
        f->operands,
        f->registers,
        f->classes,
        f->members,
        f->units,
        f->phis,
        f->forms,
    };
    const uint32_t counts[BUSTER_TP_TABLE_COUNT] = { 1, 0, 4, 3, 7, 3, 1, 3, 3, 0, 0 };
    for (uint32_t t = 0; t < BUSTER_TP_TABLE_COUNT; t += 1)
    {
        p->tables[t].data = rows[t];
        p->tables[t].count = counts[t];
    }
    f->blocks[0][2] = 4;
    f->blocks[0][5] = 1024;
    for (uint32_t r = 0; r < 3; r += 1)
    {
        f->registers[r][0] = r + 100;
        f->registers[r][2] = r;
        f->registers[r][3] = 64;
        f->registers[r][4] = r;
        f->registers[r][5] = 1;
        f->members[r] = r;
        f->units[r] = r;
        f->values[r][0] = r + 200;
        f->values[r][2] = 64;
    }
    f->registers[2][6] = BUSTER_TP_RESERVED | BUSTER_TP_SCRATCH;
    f->classes[0][0] = 300;
    f->classes[0][2] = 64;
    f->classes[0][4] = 3;
    const uint32_t begins[4] = { 0, 1, 3, 6 };
    const uint32_t lengths[4] = { 1, 2, 3, 1 };
    for (uint32_t i = 0; i < 4; i += 1)
    {
        f->instructions[i][0] = i;
        f->instructions[i][2] = 400 + i;
        f->instructions[i][3] = begins[i];
        f->instructions[i][4] = lengths[i];
    }
    const uint32_t value_ids[7] = { 0, 1, 0, 2, 0, 1, 2 };
    for (uint32_t o = 0; o < 7; o += 1)
    {
        const bool definition = o == 0 || o == 1 || o == 3;
        f->operands[o][1] = value_ids[o];
        f->operands[o][2] = definition ? 3 : 0;
        f->operands[o][3] = definition ? BUSTER_TP_WRITE : BUSTER_TP_READ;
        f->operands[o][5] = BUSTER_TP_NONE;
        f->operands[o][6] = 64;
    }
}

// Adds a second class: bank-1 128-bit vectors, one member encoded number 31.
static void problem_fixture_vector(ProblemFixture* f)
{
    problem_fixture(f);
    f->problem.tables[BUSTER_TP_REGISTER].count = 4;
    f->problem.tables[BUSTER_TP_CLASS].count = 2;
    f->problem.tables[BUSTER_TP_MEMBER].count = 4;
    f->problem.tables[BUSTER_TP_UNIT].count = 4;
    f->problem.unit_count = 4;
    f->registers[3][0] = 103;
    f->registers[3][1] = 1;
    f->registers[3][2] = 31;
    f->registers[3][3] = 128;
    f->registers[3][4] = 3;
    f->registers[3][5] = 1;
    f->classes[1][0] = 301;
    f->classes[1][1] = 1;
    f->classes[1][2] = 128;
    f->classes[1][3] = 3;
    f->classes[1][4] = 1;
    f->members[3] = 3;
    f->units[3] = 3;
}

static void problem_fixture_cfg(ProblemFixture* f)
{
    problem_fixture(f);
    f->problem.tables[BUSTER_TP_BLOCK].count = 2;
    f->problem.tables[BUSTER_TP_EDGE].count = 2;
    f->problem.tables[BUSTER_TP_PHI].count = 1;
    f->blocks[0][2] = 1;
    f->blocks[0][4] = 1;
    f->blocks[1][0] = 1;
    f->blocks[1][1] = 1;
    f->blocks[1][2] = 3;
    f->blocks[1][3] = 1;
    f->blocks[1][4] = 1;
    f->blocks[1][5] = 512;
    f->edges[0][1] = 1;
    f->edges[0][3] = 1;
    f->edges[0][4] = 512;
    f->edges[0][6] = 1;
    f->edges[0][7] = 1;
    f->edges[1][0] = 1;
    f->edges[1][2] = 1;
    f->edges[1][4] = 256;
    f->edges[1][6] = 1;
    f->edges[1][7] = 1;
    f->phis[0][1] = 1;
    for (uint32_t i = 1; i < 4; i += 1)
    {
        f->instructions[i][1] = 1;
    }
}

static void problem_fixture_form(ProblemFixture* f)
{
    problem_fixture(f);
    f->problem.tables[BUSTER_TP_FORM].count = 1;
    f->problem.tables[BUSTER_TP_OPERAND].count = 9;
    f->instructions[1][7] = 1;
    f->instructions[2][6] = 1;
    f->instructions[3][6] = 1;
    f->forms[0][0] = 1;
    f->forms[0][1] = 900;
    f->forms[0][2] = 7;
    f->forms[0][3] = 2;
    memcpy(f->operands[7], f->operands[1], sizeof(f->operands[7]));
    memcpy(f->operands[8], f->operands[2], sizeof(f->operands[8]));
}

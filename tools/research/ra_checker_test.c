/* Independently authored allocation-checker attack fixtures.
 * fixture_* writes source and allocation records with absolute expectations.
 * run_attacks checks rejection/coverage boundaries; concrete_witnesses executes
 * integer traces without checker state, joins, or certificate transfer code.
 * run_bench constructs outside timing and reports seven raw hosted-run samples.
 * Standalone research executable; never registered in production or admission.
 */
#define _POSIX_C_SOURCE 200809L
#include "ra_checker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TEST_ROWS 16u
#define TEST_EDITS 16u
#define TEST_BLOCKS 4u
#define TEST_EDGES 5u

typedef struct Fixture
{
    RaCheckProgram program;
    RaCheckLocation locations[6];
    RaCheckRow rows[TEST_ROWS];
    RaCheckAllocatedRow allocated[TEST_ROWS];
    RaCheckEdit edits[TEST_EDITS];
    RaCheckBlock blocks[TEST_BLOCKS];
    RaCheckEdge edges[TEST_EDGES];
    RaCheckBinding bindings[4];
    uint8_t parameters[TEST_BLOCKS][3];
    uint8_t arguments[TEST_EDGES][3];
} Fixture;

typedef struct TestCounts
{
    uint32_t passed;
    uint32_t failed;
} TestCounts;

static void fixture_init(Fixture *fixture, uint32_t row_count)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->program = (RaCheckProgram){
        .version = RA_CHECK_VERSION, .symbol_count = 16, .location_count = 6,
        .locations = fixture->locations, .row_count = row_count, .rows = fixture->rows,
        .allocated_row_count = row_count, .allocated_rows = fixture->allocated,
        .edits = fixture->edits, .block_count = 1, .blocks = fixture->blocks,
        .edges = fixture->edges, .bindings = fixture->bindings, .frame_size = 16,
    };
    for (uint32_t location = 0; location < 6; location += 1)
    {
        fixture->locations[location] = (RaCheckLocation){
            .kind = location < 4 ? RA_CHECK_GPR : RA_CHECK_STACK,
            .index = location < 4 ? location : 8u * (location - 4u), .width_bits = 64,
        };
    }
    for (uint32_t row = 0; row < TEST_ROWS; row += 1)
    {
        fixture->rows[row].definition = RA_CHECK_NONE;
        fixture->rows[row].fixed_definition = RA_CHECK_NONE;
        fixture->rows[row].tied_use = RA_CHECK_NONE;
        fixture->allocated[row].definition_location = RA_CHECK_NONE;
        for (uint32_t use = 0; use < 3; use += 1)
        {
            fixture->rows[row].uses[use] = RA_CHECK_NONE;
            fixture->rows[row].fixed_uses[use] = RA_CHECK_NONE;
            fixture->allocated[row].use_locations[use] = RA_CHECK_NONE;
        }
    }
    for (uint32_t block = 0; block < TEST_BLOCKS; block += 1)
    {
        fixture->blocks[block].parameters = fixture->parameters[block];
    }
    for (uint32_t edge = 0; edge < TEST_EDGES; edge += 1)
    {
        fixture->edges[edge].arguments = fixture->arguments[edge];
    }
    fixture->blocks[0].row_count = row_count;
}

static void fixture_row(Fixture *fixture, uint32_t row, uint32_t opcode,
                        uint8_t definition, uint8_t definition_location,
                        uint8_t use_count, uint8_t first_use, uint8_t first_location,
                        uint8_t second_use, uint8_t second_location, uint64_t payload)
{
    fixture->rows[row].opcode = opcode;
    fixture->rows[row].width_bits = 64;
    fixture->rows[row].payload = payload;
    fixture->rows[row].definition = definition;
    fixture->rows[row].use_count = use_count;
    fixture->rows[row].uses[0] = first_use;
    fixture->rows[row].uses[1] = second_use;
    fixture->allocated[row].opcode = opcode;
    fixture->allocated[row].width_bits = 64;
    fixture->allocated[row].payload = payload;
    fixture->allocated[row].definition_location = definition_location;
    fixture->allocated[row].use_locations[0] = first_location;
    fixture->allocated[row].use_locations[1] = second_location;
}

static void fixture_const(Fixture *fixture, uint32_t row, uint8_t definition, uint8_t location, uint64_t value)
{
    fixture_row(fixture, row, RA_CHECK_CONST64, definition, location, 0,
                RA_CHECK_NONE, RA_CHECK_NONE, RA_CHECK_NONE, RA_CHECK_NONE, value);
}

static void fixture_observe(Fixture *fixture, uint32_t row, uint8_t symbol, uint8_t location)
{
    fixture_row(fixture, row, RA_CHECK_OBSERVE64, RA_CHECK_NONE, RA_CHECK_NONE, 1,
                symbol, location, RA_CHECK_NONE, RA_CHECK_NONE, 0);
}

static void fixture_edit(Fixture *fixture, uint32_t index, uint32_t kind,
                         uint32_t phase, uint32_t owner, uint8_t source, uint8_t destination)
{
    fixture->edits[index] = (RaCheckEdit){
        .kind = kind, .phase = phase, .owner = owner, .width_bits = 64,
        .source_location = source, .destination_location = destination,
    };
}

static void fixture_spill(Fixture *fixture)
{
    fixture_init(fixture, 3);
    fixture_const(fixture, 0, 0, 0, 7);
    fixture_const(fixture, 1, 1, 0, 11);
    fixture_observe(fixture, 2, 0, 1);
    fixture_edit(fixture, 0, RA_CHECK_SPILL64, RA_CHECK_AFTER, 0, 0, 4);
    fixture_edit(fixture, 1, RA_CHECK_RELOAD64, RA_CHECK_BEFORE, 2, 4, 1);
    fixture->program.edit_count = 2;
    fixture->allocated[0].after_count = 1;
    fixture->allocated[2].first_before = 1;
    fixture->allocated[2].before_count = 1;
}

static void fixture_call(Fixture *fixture, uint32_t safe)
{
    fixture_init(fixture, 3);
    fixture_const(fixture, 0, 0, 0, 7);
    fixture_row(fixture, 1, RA_CHECK_CALL64, RA_CHECK_NONE, RA_CHECK_NONE, 0,
                RA_CHECK_NONE, RA_CHECK_NONE, RA_CHECK_NONE, RA_CHECK_NONE, 0);
    fixture->rows[1].call_clobber_gprs = UINT64_C(1);
    fixture_observe(fixture, 2, 0, 0);
    if (safe != 0)
    {
        fixture_edit(fixture, 0, RA_CHECK_SPILL64, RA_CHECK_AFTER, 0, 0, 4);
        fixture_edit(fixture, 1, RA_CHECK_RELOAD64, RA_CHECK_BEFORE, 2, 4, 0);
        fixture->program.edit_count = 2;
        fixture->allocated[0].after_count = 1;
        fixture->allocated[2].first_before = 1;
        fixture->allocated[2].before_count = 1;
    }
}

static void fixture_swap(Fixture *fixture, uint32_t safe)
{
    fixture_init(fixture, 4);
    fixture_const(fixture, 0, 0, 0, 7);
    fixture_const(fixture, 1, 1, 1, 11);
    fixture_observe(fixture, 2, 2, 1);
    fixture_observe(fixture, 3, 3, 0);
    fixture->program.block_count = 2;
    fixture->blocks[0].row_count = 2;
    fixture->blocks[1].first_row = 2;
    fixture->blocks[1].row_count = 2;
    fixture->blocks[1].parameter_count = 2;
    fixture->parameters[1][0] = 2;
    fixture->parameters[1][1] = 3;
    fixture->program.edge_count = 1;
    fixture->edges[0].destination_block = 1;
    fixture->edges[0].argument_count = 2;
    fixture->arguments[0][0] = 0;
    fixture->arguments[0][1] = 1;
    if (safe != 0)
    {
        fixture_edit(fixture, 0, RA_CHECK_MOVE64, RA_CHECK_EDGE, 0, 0, 2);
        fixture_edit(fixture, 1, RA_CHECK_MOVE64, RA_CHECK_EDGE, 0, 1, 0);
        fixture_edit(fixture, 2, RA_CHECK_MOVE64, RA_CHECK_EDGE, 0, 2, 1);
        fixture->program.edit_count = 3;
        fixture->edges[0].edit_count = 3;
    }
    else
    {
        fixture_edit(fixture, 0, RA_CHECK_MOVE64, RA_CHECK_EDGE, 0, 0, 1);
        fixture_edit(fixture, 1, RA_CHECK_MOVE64, RA_CHECK_EDGE, 0, 1, 0);
        fixture->program.edit_count = 2;
        fixture->edges[0].edit_count = 2;
    }
}

static void fixture_join(Fixture *fixture)
{
    fixture_init(fixture, 4);
    fixture_const(fixture, 0, 0, 0, 7);
    fixture_const(fixture, 1, 1, 1, 11);
    fixture_const(fixture, 2, 2, 1, 13);
    fixture_observe(fixture, 3, 3, 1);
    fixture->program.block_count = 4;
    for (uint32_t block = 0; block < 4; block += 1)
    {
        fixture->blocks[block].first_row = block;
        fixture->blocks[block].row_count = 1;
    }
    fixture->blocks[3].parameter_count = 1;
    fixture->parameters[3][0] = 3;
    fixture->program.edge_count = 4;
    fixture->edges[0].destination_block = 1;
    fixture->edges[1].destination_block = 2;
    fixture->edges[2].source_block = 1;
    fixture->edges[2].destination_block = 3;
    fixture->edges[2].argument_count = 1;
    fixture->arguments[2][0] = 1;
    fixture->edges[3].source_block = 2;
    fixture->edges[3].destination_block = 3;
    fixture->edges[3].argument_count = 1;
    fixture->arguments[3][0] = 2;
}

static void fixture_loop(Fixture *fixture)
{
    fixture_init(fixture, 5);
    fixture_const(fixture, 0, 0, 0, 7);
    fixture_const(fixture, 1, 3, 1, 1);
    fixture_observe(fixture, 2, 1, 0);
    fixture_row(fixture, 3, RA_CHECK_ADD64, 2, 0, 2, 1, 0, 3, 1, 0);
    fixture->rows[3].tied_use = 0;
    fixture_observe(fixture, 4, 2, 0);
    fixture->program.block_count = 3;
    fixture->blocks[0].row_count = 2;
    fixture->blocks[1].first_row = 2;
    fixture->blocks[1].row_count = 2;
    fixture->blocks[1].parameter_count = 1;
    fixture->parameters[1][0] = 1;
    fixture->blocks[2].first_row = 4;
    fixture->blocks[2].row_count = 1;
    fixture->program.edge_count = 3;
    fixture->edges[0].destination_block = 1;
    fixture->edges[0].argument_count = 1;
    fixture->arguments[0][0] = 0;
    fixture->edges[1].source_block = 1;
    fixture->edges[1].destination_block = 1;
    fixture->edges[1].argument_count = 1;
    fixture->arguments[1][0] = 2;
    fixture->edges[2].source_block = 1;
    fixture->edges[2].destination_block = 2;
}

static void fixture_mutable_stale(Fixture *fixture, uint32_t safe)
{
    fixture_init(fixture, 5);
    fixture_const(fixture, 0, 0, 0, 7);
    fixture_const(fixture, 1, 1, 1, 1);
    fixture_row(fixture, 2, RA_CHECK_ADD64, 0, 0, 2, 0, 0, 1, 1, 0);
    fixture->rows[2].tied_use = 0;
    fixture_observe(fixture, 3, 0, 2);
    fixture_observe(fixture, 4, 0, 0);
    fixture_edit(fixture, 0, RA_CHECK_SPILL64, RA_CHECK_AFTER, 0, 0, 4);
    fixture_edit(fixture, 1, RA_CHECK_RELOAD64, RA_CHECK_BEFORE, 3, 4, 2);
    fixture->program.edit_count = 2;
    fixture->allocated[0].after_count = 1;
    fixture->allocated[3].first_before = 1;
    fixture->allocated[3].before_count = 1;
    if (safe != 0)
    {
        fixture_edit(fixture, 1, RA_CHECK_SPILL64, RA_CHECK_AFTER, 2, 0, 4);
        fixture_edit(fixture, 2, RA_CHECK_RELOAD64, RA_CHECK_BEFORE, 3, 4, 2);
        fixture->allocated[2].first_after = 1;
        fixture->allocated[2].after_count = 1;
        fixture->allocated[3].first_before = 2;
        fixture->program.edit_count = 3;
    }
    fixture->program.block_count = 3;
    fixture->blocks[0].row_count = 2;
    fixture->blocks[1].first_row = 2;
    fixture->blocks[1].row_count = 2;
    fixture->blocks[2].first_row = 4;
    fixture->blocks[2].row_count = 1;
    fixture->program.edge_count = 3;
    fixture->edges[0].destination_block = 1;
    fixture->edges[1].source_block = 1;
    fixture->edges[1].destination_block = 1;
    fixture->edges[2].source_block = 1;
    fixture->edges[2].destination_block = 2;
}

static const char *status_name(RaCheckStatus status)
{
    const char *name;
    switch (status)
    {
        case RA_CHECK_VALID: name = "VALID"; break;
        case RA_CHECK_INVALID: name = "INVALID"; break;
        case RA_CHECK_NOT_PROVEN: name = "NOT_PROVEN"; break;
        case RA_CHECK_UNCOVERED: name = "UNCOVERED"; break;
        case RA_CHECK_CERTIFICATE_REJECTED: name = "CERTIFICATE_REJECTED"; break;
        default: name = "BAD_STATUS"; break;
    }
    return name;
}

static const char *expected_reason(const char *name, RaCheckStatus status)
{
    const char *reason;
    if (status == RA_CHECK_VALID)
    {
        reason = "validated supported allocation contract";
    }
    else if (status == RA_CHECK_NOT_PROVEN)
    {
        reason = strcmp(name, "remat-before-original-definition") == 0
            ? "literal rematerialization precedes its reaching definition"
            : "source location does not prove the required reaching value";
    }
    else if (status == RA_CHECK_UNCOVERED)
    {
        reason = "partial, vector, mask or unknown resource";
        if (strcmp(name, "partial-register-operation-uncovered") == 0)
        {
            reason = "non-full-width scalar instruction";
        }
        else if (strcmp(name, "unknown-instruction-uncovered") == 0)
        {
            reason = "opcode lacks independent transfer semantics";
        }
        else if (strcmp(name, "remat-recipe-for-redefined-symbol") == 0)
        {
            reason = "rematerialization recipe symbol is not immutable";
        }
    }
    else if (status == RA_CHECK_CERTIFICATE_REJECTED)
    {
        reason = "missing or malformed certificate";
        if (strcmp(name, "truncated-certificate") == 0 || strcmp(name, "trailing-certificate-state") == 0)
        {
            reason = "certificate must cover exactly every original block";
        }
        else if (strcmp(name, "weak-certificate-misses-required-fact") == 0)
        {
            reason = "certificate insufficient to prove a supported reaching-value use";
        }
        else if (strcmp(name, "false-certificate-invents-register-fact") == 0 || strcmp(name, "malformed-certificate-symbol-bit") == 0)
        {
            reason = "entry certificate asserts facts outside ABI contract";
        }
        else if (strcmp(name, "false-entry-loop-certificate") == 0 || strcmp(name, "false-loop-header-certificate") == 0)
        {
            reason = "edge does not establish claimed destination invariant";
        }
    }
    else
    {
        reason = "malformed counts, version, pointers or row coverage";
        if (strcmp(name, "tied-destination-mismatch") == 0)
        {
            reason = "tied-operand constraint violated";
        }
        else if (strcmp(name, "early-clobber-input-alias") == 0)
        {
            reason = "early output aliases an untied source";
        }
        else if (strcmp(name, "allocated-opcode-substitution") == 0 || strcmp(name, "allocated-literal-substitution") == 0)
        {
            reason = "opcode, payload, operand role or effect correspondence failed";
        }
        else if (strcmp(name, "unconsumed-edit-record") == 0)
        {
            reason = "unconsumed edit or unsupported edit phase";
        }
        else if (strcmp(name, "wrong-edit-owner") == 0)
        {
            reason = "edit has duplicate, wrong phase or wrong owner";
        }
        else if (strcmp(name, "reload-claimed-as-spill") == 0)
        {
            reason = "edit resource class mismatches operation";
        }
        else if (strcmp(name, "fixed-use-mismatch") == 0)
        {
            reason = "source or fixed-use location malformed";
        }
        else if (strcmp(name, "fixed-definition-mismatch") == 0)
        {
            reason = "fixed-definition constraint violated";
        }
        else if (strcmp(name, "truncated-edge-argument-list") == 0)
        {
            reason = "edge arguments do not cover destination parameters";
        }
        else if (strcmp(name, "forged-remat-literal") == 0)
        {
            reason = "rematerialization is not bound to an original literal recipe";
        }
        else if (strcmp(name, "unowned-source-row") == 0)
        {
            reason = "unconsumed original rows";
        }
        else if (strcmp(name, "out-of-bounds-definition-location") == 0)
        {
            reason = "definition or physical output location malformed";
        }
        else if (strcmp(name, "frame-range-overflow") == 0)
        {
            reason = "resource id or eight-byte frame extent out of bounds";
        }
    }
    return reason;
}

static void expect_result(TestCounts *counts, const char *name, RaCheckResult result, RaCheckStatus expected)
{
    const char *reason = expected_reason(name, expected);
    uint32_t pass = result.status == expected && strcmp(result.reason, reason) == 0;
    counts->passed += pass;
    counts->failed += 1u - pass;
    printf("test=%s expected=%s actual=%s pass=%u reason=%s expected_reason=%s\n", name,
           status_name(expected), status_name(result.status), pass, result.reason, reason);
}

static void expect_program(TestCounts *counts, const char *name, Fixture *fixture, RaCheckStatus expected)
{
    RaCheckWorkspace workspace;
    RaCheckResult result = ra_check(&fixture->program, &workspace);
    expect_result(counts, name, result, expected);
}

static void expect_certificate(TestCounts *counts, const char *name, Fixture *fixture,
                               const RaCheckCertificate *certificate, RaCheckStatus expected)
{
    RaCheckWorkspace workspace;
    RaCheckResult result = ra_check_certificate(&fixture->program, &workspace, certificate);
    expect_result(counts, name, result, expected);
}

static void concrete_witnesses(TestCounts *counts)
{
    /* Literal integer execution; no symbolic sets or checker calls. */
    uint64_t good_swap[3] = {7, 11, 0};
    good_swap[2] = good_swap[0];
    good_swap[0] = good_swap[1];
    good_swap[1] = good_swap[2];
    uint64_t bad_swap[2] = {7, 11};
    bad_swap[1] = bad_swap[0];
    bad_swap[0] = bad_swap[1];
    uint32_t swap_pass = good_swap[0] == 11 && good_swap[1] == 7 && bad_swap[0] == 7 && bad_swap[1] == 7;
    counts->passed += swap_pass;
    counts->failed += 1u - swap_pass;
    printf("witness=swap required=(11,7) safe=(%llu,%llu) bad=(%llu,%llu) pass=%u\n",
           (unsigned long long)good_swap[0], (unsigned long long)good_swap[1],
           (unsigned long long)bad_swap[0], (unsigned long long)bad_swap[1], swap_pass);
    uint64_t current = 7;
    uint64_t stale_home = current;
    uint32_t stale_pass = 1;
    for (uint32_t iteration = 0; iteration < 3; iteration += 1)
    {
        current += 1;
        uint64_t bad_reload = stale_home;
        uint64_t good_home = current;
        uint64_t good_reload = good_home;
        stale_pass &= bad_reload != current && good_reload == current;
        printf("witness=mutable-loop iteration=%u required=%llu safe=%llu stale=%llu\n", iteration,
               (unsigned long long)current, (unsigned long long)good_reload, (unsigned long long)bad_reload);
    }
    counts->passed += stale_pass;
    counts->failed += 1u - stale_pass;
    uint64_t copy_registers[2] = {7, 0};
    copy_registers[1] = copy_registers[0];
    uint64_t copied_value = copy_registers[0];
    copy_registers[0] = copied_value;
    uint32_t copy_pass = copy_registers[1] == 7;
    counts->passed += copy_pass;
    counts->failed += 1u - copy_pass;
    printf("witness=correct-copy-alias required=7 actual=%llu pass=%u\n",
           (unsigned long long)copy_registers[1], copy_pass);
}

static TestCounts run_attacks(void)
{
    TestCounts counts = {0};
    Fixture fixture;
    fixture_init(&fixture, 4);
    fixture_const(&fixture, 0, 0, 0, 7);
    fixture_const(&fixture, 1, 1, 1, 11);
    fixture_row(&fixture, 2, RA_CHECK_ADD64, 2, 0, 2, 0, 0, 1, 1, 0);
    fixture.rows[2].tied_use = 0;
    fixture_observe(&fixture, 3, 2, 0);
    expect_program(&counts, "good-destructive-add", &fixture, RA_CHECK_VALID);
    fixture.allocated[2].use_locations[1] = 2;
    expect_program(&counts, "wrong-source-register", &fixture, RA_CHECK_NOT_PROVEN);
    fixture.allocated[2].use_locations[1] = 1;
    fixture.allocated[2].definition_location = 2;
    expect_program(&counts, "tied-destination-mismatch", &fixture, RA_CHECK_INVALID);
    fixture.allocated[2].definition_location = 0;
    fixture.rows[2].early_clobber = 1;
    fixture.rows[2].tied_use = RA_CHECK_NONE;
    fixture.rows[2].opcode = RA_CHECK_EARLY64;
    fixture.allocated[2].opcode = RA_CHECK_EARLY64;
    expect_program(&counts, "early-clobber-input-alias", &fixture, RA_CHECK_INVALID);
    fixture.rows[2].early_clobber = 0;
    fixture.rows[2].tied_use = 0;
    fixture.rows[2].opcode = RA_CHECK_ADD64;
    fixture.allocated[2].opcode = RA_CHECK_SUB64;
    expect_program(&counts, "allocated-opcode-substitution", &fixture, RA_CHECK_INVALID);
    fixture.allocated[2].opcode = RA_CHECK_ADD64;
    fixture.allocated[0].payload = 8;
    expect_program(&counts, "allocated-literal-substitution", &fixture, RA_CHECK_INVALID);

    fixture_spill(&fixture);
    expect_program(&counts, "good-spill-reload", &fixture, RA_CHECK_VALID);
    fixture.edits[1].source_location = 5;
    expect_program(&counts, "reload-wrong-slot", &fixture, RA_CHECK_NOT_PROVEN);
    fixture_spill(&fixture);
    fixture.edits[0].phase = RA_CHECK_BEFORE;
    fixture.allocated[0].after_count = 0;
    fixture.allocated[0].before_count = 1;
    expect_program(&counts, "spill-before-value-defined", &fixture, RA_CHECK_NOT_PROVEN);
    fixture_spill(&fixture);
    fixture_edit(&fixture, 1, RA_CHECK_SPILL64, RA_CHECK_AFTER, 1, 0, 4);
    fixture_edit(&fixture, 2, RA_CHECK_RELOAD64, RA_CHECK_BEFORE, 2, 4, 1);
    fixture.allocated[1].first_after = 1;
    fixture.allocated[1].after_count = 1;
    fixture.allocated[2].first_before = 2;
    fixture.program.edit_count = 3;
    expect_program(&counts, "overwritten-spill-home", &fixture, RA_CHECK_NOT_PROVEN);
    fixture.locations[5].index = 4;
    fixture.edits[1].destination_location = 5;
    expect_program(&counts, "overlapping-eight-byte-spill-home", &fixture, RA_CHECK_NOT_PROVEN);
    fixture_spill(&fixture);
    fixture.allocated[2].before_count = 0;
    fixture.program.edit_count = 1;
    expect_program(&counts, "missing-reload", &fixture, RA_CHECK_NOT_PROVEN);
    fixture_spill(&fixture);
    fixture.allocated[2].before_count = 0;
    expect_program(&counts, "unconsumed-edit-record", &fixture, RA_CHECK_INVALID);
    fixture_spill(&fixture);
    fixture.edits[1].owner = 1;
    expect_program(&counts, "wrong-edit-owner", &fixture, RA_CHECK_INVALID);
    fixture_spill(&fixture);
    fixture.edits[1].kind = RA_CHECK_SPILL64;
    expect_program(&counts, "reload-claimed-as-spill", &fixture, RA_CHECK_INVALID);

    fixture_call(&fixture, 0);
    expect_program(&counts, "call-clobbered-reaching-value", &fixture, RA_CHECK_NOT_PROVEN);
    fixture_call(&fixture, 1);
    expect_program(&counts, "good-call-preserving-spill", &fixture, RA_CHECK_VALID);
    fixture_call(&fixture, 1);
    fixture.rows[2].fixed_uses[0] = 1;
    expect_program(&counts, "fixed-use-mismatch", &fixture, RA_CHECK_INVALID);
    fixture_call(&fixture, 1);
    fixture.rows[0].fixed_definition = 1;
    expect_program(&counts, "fixed-definition-mismatch", &fixture, RA_CHECK_INVALID);

    fixture_swap(&fixture, 1);
    expect_program(&counts, "good-parallel-edge-cycle", &fixture, RA_CHECK_VALID);
    fixture_swap(&fixture, 0);
    expect_program(&counts, "parallel-swap-serialized-destructively", &fixture, RA_CHECK_NOT_PROVEN);
    fixture_swap(&fixture, 1);
    fixture.arguments[0][1] = 0;
    expect_program(&counts, "wrong-parallel-edge-source", &fixture, RA_CHECK_NOT_PROVEN);
    fixture_swap(&fixture, 1);
    fixture.edges[0].argument_count = 1;
    expect_program(&counts, "truncated-edge-argument-list", &fixture, RA_CHECK_INVALID);
    fixture_join(&fixture);
    expect_program(&counts, "good-two-path-parameter-join", &fixture, RA_CHECK_VALID);
    fixture_join(&fixture);
    fixture.allocated[2].definition_location = 2;
    expect_program(&counts, "join-one-predecessor-wrong-location", &fixture, RA_CHECK_NOT_PROVEN);
    fixture_loop(&fixture);
    expect_program(&counts, "good-loop-parallel-renaming", &fixture, RA_CHECK_VALID);
    fixture_mutable_stale(&fixture, 0);
    expect_program(&counts, "stale-mutable-loop-spill", &fixture, RA_CHECK_NOT_PROVEN);
    fixture_mutable_stale(&fixture, 1);
    expect_program(&counts, "good-mutable-loop-spill-refresh", &fixture, RA_CHECK_VALID);

    /* An entry self-edge cannot reset its clobbered ABI binding each sweep. */
    fixture_init(&fixture, 2);
    fixture.program.binding_count = 1;
    fixture.bindings[0] = (RaCheckBinding){.symbol = 0, .location = 0};
    fixture_observe(&fixture, 0, 0, 0);
    fixture_const(&fixture, 1, 1, 0, 11);
    fixture.program.edge_count = 1;
    expect_program(&counts, "entry-backedge-clobbers-abi-binding", &fixture, RA_CHECK_NOT_PROVEN);

    /* Correct concrete output can still lack a fact in the chosen abstraction. */
    fixture_init(&fixture, 2);
    fixture.program.binding_count = 1;
    fixture.bindings[0] = (RaCheckBinding){.symbol = 0, .location = 0};
    fixture_row(&fixture, 0, RA_CHECK_COPY64, 1, 0, 1, 0, 0,
                RA_CHECK_NONE, RA_CHECK_NONE, 0);
    fixture_observe(&fixture, 1, 1, 1);
    fixture_edit(&fixture, 0, RA_CHECK_MOVE64, RA_CHECK_BEFORE, 0, 0, 1);
    fixture.program.edit_count = 1;
    fixture.allocated[0].before_count = 1;
    expect_program(&counts, "correct-copy-alias-not-proven", &fixture, RA_CHECK_NOT_PROVEN);

    fixture_init(&fixture, 2);
    fixture_const(&fixture, 0, 0, 0, 7);
    fixture_observe(&fixture, 1, 0, 1);
    fixture_edit(&fixture, 0, RA_CHECK_REMATERIALIZE64, RA_CHECK_BEFORE, 1, RA_CHECK_NONE, 1);
    fixture.edits[0].recipe_row = 0;
    fixture.edits[0].payload = 7;
    fixture.program.edit_count = 1;
    fixture.allocated[1].before_count = 1;
    expect_program(&counts, "good-source-bound-constant-remat", &fixture, RA_CHECK_VALID);
    fixture.edits[0].payload = 8;
    expect_program(&counts, "forged-remat-literal", &fixture, RA_CHECK_INVALID);
    fixture.edits[0].payload = 7;
    fixture.edits[0].owner = 0;
    fixture.allocated[1].before_count = 0;
    fixture.allocated[0].before_count = 1;
    expect_program(&counts, "remat-before-original-definition", &fixture, RA_CHECK_NOT_PROVEN);
    fixture_mutable_stale(&fixture, 0);
    fixture_edit(&fixture, 1, RA_CHECK_REMATERIALIZE64, RA_CHECK_BEFORE, 3, RA_CHECK_NONE, 2);
    fixture.edits[1].recipe_row = 0;
    fixture.edits[1].payload = 7;
    expect_program(&counts, "remat-recipe-for-redefined-symbol", &fixture, RA_CHECK_UNCOVERED);

    fixture_init(&fixture, 1);
    fixture_const(&fixture, 0, 0, 0, 7);
    fixture.rows[0].width_bits = 32;
    fixture.allocated[0].width_bits = 32;
    expect_program(&counts, "partial-register-operation-uncovered", &fixture, RA_CHECK_UNCOVERED);
    fixture_init(&fixture, 1);
    fixture_const(&fixture, 0, 0, 0, 7);
    fixture.locations[0].kind = 2;
    expect_program(&counts, "vector-alias-resource-uncovered", &fixture, RA_CHECK_UNCOVERED);
    fixture.locations[0].kind = 3;
    expect_program(&counts, "mask-alias-resource-uncovered", &fixture, RA_CHECK_UNCOVERED);
    fixture_init(&fixture, 1);
    fixture_const(&fixture, 0, 0, 0, 7);
    fixture.rows[0].opcode = 999;
    fixture.allocated[0].opcode = 999;
    expect_program(&counts, "unknown-instruction-uncovered", &fixture, RA_CHECK_UNCOVERED);
    fixture_init(&fixture, 1);
    fixture_const(&fixture, 0, 0, 0, 7);
    fixture.program.allocated_row_count = 0;
    expect_program(&counts, "missing-allocated-row", &fixture, RA_CHECK_INVALID);
    fixture.program.allocated_row_count = 2;
    expect_program(&counts, "extra-allocated-row", &fixture, RA_CHECK_INVALID);
    fixture.program.allocated_row_count = 1;
    fixture.program.rows = NULL;
    expect_program(&counts, "null-source-row-storage", &fixture, RA_CHECK_INVALID);
    fixture_init(&fixture, 1);
    fixture_const(&fixture, 0, 0, 0, 7);
    fixture.blocks[0].row_count = 0;
    expect_program(&counts, "unowned-source-row", &fixture, RA_CHECK_INVALID);
    fixture_init(&fixture, 1);
    fixture_const(&fixture, 0, 0, 0, 7);
    fixture.allocated[0].definition_location = 6;
    expect_program(&counts, "out-of-bounds-definition-location", &fixture, RA_CHECK_INVALID);
    fixture_init(&fixture, 1);
    fixture_const(&fixture, 0, 0, 0, 7);
    fixture.locations[5].index = 12;
    expect_program(&counts, "frame-range-overflow", &fixture, RA_CHECK_INVALID);

    /* The valid certificate is handwritten from the ABI input contract. */
    fixture_init(&fixture, 1);
    fixture.program.binding_count = 1;
    fixture.bindings[0] = (RaCheckBinding){.symbol = 0, .location = 0};
    fixture_observe(&fixture, 0, 0, 0);
    RaCheckState entries[TEST_BLOCKS] = {0};
    entries[0].facts[0] = UINT64_C(1);
    entries[0].available_symbols = UINT64_C(1);
    RaCheckCertificate certificate = {
        .present = 1, .version = RA_CHECK_VERSION, .state_count = 1, .block_entries = entries,
    };
    expect_certificate(&counts, "handwritten-valid-entry-certificate", &fixture, &certificate, RA_CHECK_VALID);
    expect_certificate(&counts, "missing-certificate-pointer", &fixture, NULL, RA_CHECK_CERTIFICATE_REJECTED);
    certificate.present = 0;
    expect_certificate(&counts, "missing-certificate-present-flag", &fixture, &certificate, RA_CHECK_CERTIFICATE_REJECTED);
    certificate.present = 2;
    expect_certificate(&counts, "malformed-certificate-present-flag", &fixture, &certificate, RA_CHECK_CERTIFICATE_REJECTED);
    certificate.present = 1;
    certificate.version = 999;
    expect_certificate(&counts, "unknown-certificate-version", &fixture, &certificate, RA_CHECK_CERTIFICATE_REJECTED);
    certificate.version = RA_CHECK_VERSION;
    certificate.state_count = 0;
    expect_certificate(&counts, "truncated-certificate", &fixture, &certificate, RA_CHECK_CERTIFICATE_REJECTED);
    certificate.state_count = 2;
    expect_certificate(&counts, "trailing-certificate-state", &fixture, &certificate, RA_CHECK_CERTIFICATE_REJECTED);
    certificate.state_count = 1;
    certificate.block_entries = NULL;
    expect_certificate(&counts, "null-certificate-state-storage", &fixture, &certificate, RA_CHECK_CERTIFICATE_REJECTED);
    certificate.block_entries = entries;
    entries[0].facts[0] = 0;
    expect_certificate(&counts, "weak-certificate-misses-required-fact", &fixture, &certificate, RA_CHECK_CERTIFICATE_REJECTED);
    entries[0].facts[0] = UINT64_C(1);
    entries[0].facts[1] = UINT64_C(1);
    expect_certificate(&counts, "false-certificate-invents-register-fact", &fixture, &certificate, RA_CHECK_CERTIFICATE_REJECTED);
    entries[0].facts[1] = UINT64_C(1) << 63;
    expect_certificate(&counts, "malformed-certificate-symbol-bit", &fixture, &certificate, RA_CHECK_CERTIFICATE_REJECTED);
    entries[0].facts[1] = 0;
    fixture.program.edge_count = 1;
    fixture_row(&fixture, 0, RA_CHECK_CALL64, RA_CHECK_NONE, RA_CHECK_NONE, 1, 0, 0,
                RA_CHECK_NONE, RA_CHECK_NONE, 0);
    fixture.rows[0].call_clobber_gprs = UINT64_C(1);
    expect_certificate(&counts, "false-entry-loop-certificate", &fixture, &certificate, RA_CHECK_CERTIFICATE_REJECTED);

    /* Absolute loop invariant: each arrival has its current parameter in r0,
     * and the constant increment remains in r1. No solver entries are copied. */
    fixture_loop(&fixture);
    memset(entries, 0, sizeof(entries));
    entries[1].facts[0] = UINT64_C(1) << 1;
    entries[1].facts[1] = UINT64_C(1) << 3;
    entries[1].available_symbols = (UINT64_C(1) << 1) | (UINT64_C(1) << 3);
    entries[2].facts[0] = UINT64_C(1) << 2;
    entries[2].facts[1] = UINT64_C(1) << 3;
    entries[2].available_symbols = entries[1].available_symbols | (UINT64_C(1) << 2);
    certificate.state_count = 3;
    expect_certificate(&counts, "handwritten-valid-loop-certificate", &fixture, &certificate, RA_CHECK_VALID);
    /* v0 exists at the first arrival, but r0 no longer holds it on a backedge. */
    entries[1].facts[0] |= UINT64_C(1);
    entries[1].available_symbols |= UINT64_C(1);
    expect_certificate(&counts, "false-loop-header-certificate", &fixture, &certificate, RA_CHECK_CERTIFICATE_REJECTED);
    concrete_witnesses(&counts);
    return counts;
}

static double elapsed_ns(struct timespec begin, struct timespec end)
{
    double elapsed = (double)(end.tv_sec - begin.tv_sec) * 1000000000.0 + (double)(end.tv_nsec - begin.tv_nsec);
    return elapsed;
}

static uint32_t run_bench(void)
{
    uint32_t failed = 0;
    const uint32_t sizes[] = {64, 256, 1024, RA_CHECK_MAX_ROWS};
    RaCheckRow *rows = calloc(RA_CHECK_MAX_ROWS, sizeof(*rows));
    RaCheckAllocatedRow *allocated = calloc(RA_CHECK_MAX_ROWS, sizeof(*allocated));
    RaCheckWorkspace *workspace = malloc(sizeof(*workspace));
    if (rows == NULL || allocated == NULL || workspace == NULL)
    {
        failed = 1;
    }
    else
    {
        RaCheckLocation location = {.kind = RA_CHECK_GPR, .index = 0, .width_bits = 64};
        RaCheckBinding binding = {.symbol = 0, .location = 0};
        for (uint32_t row = 0; row < RA_CHECK_MAX_ROWS; row += 1)
        {
            rows[row] = (RaCheckRow){
                .opcode = RA_CHECK_OBSERVE64, .width_bits = 64,
                .definition = RA_CHECK_NONE, .use_count = 1,
                .uses = {0, RA_CHECK_NONE, RA_CHECK_NONE}, .fixed_definition = RA_CHECK_NONE,
                .fixed_uses = {RA_CHECK_NONE, RA_CHECK_NONE, RA_CHECK_NONE}, .tied_use = RA_CHECK_NONE,
            };
            allocated[row] = (RaCheckAllocatedRow){
                .opcode = RA_CHECK_OBSERVE64, .width_bits = 64, .definition_location = RA_CHECK_NONE,
                .use_locations = {0, RA_CHECK_NONE, RA_CHECK_NONE},
            };
        }
        for (uint32_t size_index = 0; size_index < sizeof(sizes) / sizeof(sizes[0]); size_index += 1)
        {
            uint32_t size = sizes[size_index];
            RaCheckBlock block = {.row_count = size};
            RaCheckProgram program = {
                .version = RA_CHECK_VERSION, .symbol_count = 1, .location_count = 1, .locations = &location,
                .row_count = size, .rows = rows, .allocated_row_count = size, .allocated_rows = allocated,
                .block_count = 1, .blocks = &block, .binding_count = 1, .bindings = &binding,
            };
            RaCheckState entry = {0};
            entry.facts[0] = UINT64_C(1);
            entry.available_symbols = UINT64_C(1);
            RaCheckCertificate certificate = {
                .present = 1, .version = RA_CHECK_VERSION, .state_count = 1, .block_entries = &entry,
            };
            for (uint32_t mode = 0; mode < 2; mode += 1)
            {
                RaCheckResult warmup;
                if (mode == 0)
                {
                    warmup = ra_check(&program, workspace);
                }
                else
                {
                    warmup = ra_check_certificate(&program, workspace, &certificate);
                }
                failed |= warmup.status != RA_CHECK_VALID;
                for (uint32_t sample = 0; sample < 7 && failed == 0; sample += 1)
                {
                    struct timespec begin;
                    struct timespec end;
                    int clock_begin = clock_gettime(CLOCK_MONOTONIC, &begin);
                    RaCheckResult result;
                    if (mode == 0)
                    {
                        result = ra_check(&program, workspace);
                    }
                    else
                    {
                        result = ra_check_certificate(&program, workspace, &certificate);
                    }
                    int clock_end = clock_gettime(CLOCK_MONOTONIC, &end);
                    failed |= clock_begin != 0 || clock_end != 0 || result.status != RA_CHECK_VALID;
                    if (failed == 0)
                    {
                        double nanoseconds = elapsed_ns(begin, end);
                        size_t input_bytes = sizeof(program) + (size_t)size * (sizeof(*rows) + sizeof(*allocated)) + sizeof(location) + sizeof(binding) + sizeof(block);
                        size_t certificate_bytes = mode == 0 ? 0 : sizeof(certificate) + sizeof(entry);
                        printf("bench mode=%s original_rows=%u locations=1 symbols=1 blocks=1 edges=0 edits=0 input_bytes=%zu certificate_bytes=%zu workspace_bytes=%zu sample=%u elapsed_ns=%.0f ns_per_original_row=%.3f transfer_rows=%llu transfer_edits=%llu edge_transfers=%llu sweeps=%llu\n",
                               mode == 0 ? "fixedpoint" : "certificate", size, input_bytes, certificate_bytes, sizeof(*workspace), sample, nanoseconds, nanoseconds / (double)size,
                               (unsigned long long)result.transfer_rows, (unsigned long long)result.transfer_edits,
                               (unsigned long long)result.edge_transfers, (unsigned long long)result.sweeps);
                    }
                }
            }
        }
    }
    free(rows);
    free(allocated);
    free(workspace);
    return failed;
}

int main(int argument_count, char **arguments)
{
    TestCounts counts = run_attacks();
    uint32_t failed = counts.failed != 0;
    printf("attacks passed=%u failed=%u\n", counts.passed, counts.failed);
    if (argument_count == 2 && strcmp(arguments[1], "--bench") == 0)
    {
        failed |= run_bench();
    }
    else if (argument_count != 1)
    {
        fprintf(stderr, "usage: ra_checker_test [--bench]\n");
        failed = 1;
    }
    return failed != 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

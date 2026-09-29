/* Research-only Linux/unity overlay for #53. Never included by normal source. */
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <stdint.h>

typedef struct DetUnit DetUnit;
struct DetUnit
{
    u64 base;
    u64 position_before;
    u64 position_after;
    u32 lane;
    u32 claimed;
    u32 started;
    u32 finished;
};

BUSTER_GLOBAL_LOCAL DetUnit det_units[64];
BUSTER_GLOBAL_LOCAL u32 det_clock;
BUSTER_GLOBAL_LOCAL u32 det_schedule;
BUSTER_GLOBAL_LOCAL u32 det_placement;
BUSTER_GLOBAL_LOCAL u32 det_history;
BUSTER_GLOBAL_LOCAL u32 det_prior_count;
BUSTER_GLOBAL_LOCAL u32 det_prior_workers[3];
BUSTER_GLOBAL_LOCAL u32 det_prior_errors[3];
BUSTER_GLOBAL_LOCAL bool det_enabled;
BUSTER_GLOBAL_LOCAL bool det_active;
BUSTER_GLOBAL_LOCAL bool det_tracing;
BUSTER_GLOBAL_LOCAL bool det_bad;

BUSTER_GLOBAL_LOCAL unsigned det_option(char const* name)
{
    char const* text = getenv(name);
    unsigned result = 0;
    if (text)
    {
        char* end = 0;
        unsigned long parsed = strtoul(text, &end, 10);
        if (!text[0] || *end || parsed > 16)
        {
            fprintf(stderr, "HARNESS_ERROR invalid %s\n", name);
            exit(90);
        }
        result = (unsigned)parsed;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void det_bytes(FILE* stream, void const* bytes, size_t size)
{
    if (size && fwrite(bytes, 1, size, stream) != size)
    {
        fprintf(stderr, "HARNESS_ERROR writing evidence\n");
        exit(90);
    }
}

BUSTER_GLOBAL_LOCAL void det_u64(FILE* stream, u64 value)
{
    u8 bytes[8];
    for (unsigned index = 0; index < 8; index += 1)
    {
        bytes[index] = (u8)(value >> (index * 8));
    }
    det_bytes(stream, bytes, sizeof(bytes));
}

BUSTER_GLOBAL_LOCAL void det_string(FILE* stream, String8 text)
{
    det_u64(stream, text.length);
    det_bytes(stream, text.pointer, (size_t)text.length);
}

BUSTER_GLOBAL_LOCAL void det_location(FILE* stream, CompilerDiagnosticLocation location)
{
    det_string(stream, location.path);
    det_string(stream, location.original_path);
    det_u64(stream, location.has_range);
    det_u64(stream, location.range.source.value);
    det_u64(stream, location.range.offset);
    det_u64(stream, location.range.length);
    det_u64(stream, location.position.source);
    det_u64(stream, location.position.offset);
    det_u64(stream, location.position.line);
    det_u64(stream, location.position.column);
    det_u64(stream, location.original_position.source);
    det_u64(stream, location.original_position.offset);
    det_u64(stream, location.original_position.line);
    det_u64(stream, location.original_position.column);
}

BUSTER_GLOBAL_LOCAL void det_prepare(void)
{
    det_enabled = det_option("DET_ENABLE") != 0;
    if (det_enabled)
    {
        det_schedule = det_option("DET_SCHEDULE");
        det_placement = det_option("DET_PLACEMENT");
        det_history = det_option("DET_HISTORY");
        det_tracing = det_option("DET_TRACE") != 0;
        /* Each prior is independent, uses distinct immutable inputs/output,
           and releases its result arena. The thread context/gang stays live. */
        for (u32 step = 0; det_history && step < 3; step += 1)
        {
            u32 which = det_history == 1 ? step : 2 - step;
            String8 responses[] = {S8("@warm-x86.rsp"), S8("@warm-arm.rsp"), S8("@warm-bad.rsp")};
            String8 argument = responses[which];
            Arena* prior_arena = arena_create((ArenaCreation){.reserved_size = COMPILER_DRIVER_C_TRANSLATION_UNIT_RESERVED_SIZE});
            if (!prior_arena)
            {
                fprintf(stderr, "HARNESS_ERROR prior arena\n");
                exit(90);
            }
            CompilerDriverInvocation prior = compiler_driver_parse_arguments(prior_arena, (SliceString8){.pointer = &argument, .length = 1});
            CompilerDriverResult result = compiler_driver_execute_invocation(prior_arena, prior);
            det_prior_workers[step] = result.compilation_workers;
            det_prior_errors[step] = (u32)result.error;
            bool expected_failure = which == 2;
            det_bad = det_bad || ((result.error != COMPILER_DRIVER_ERROR_NONE) != expected_failure);
            det_prior_count += 1;
            arena_destroy(prior_arena, 1);
        }
        det_active = true;
    }
}

BUSTER_GLOBAL_LOCAL void det_unit_begin(u64 slot, Arena* arena)
{
    if (det_active)
    {
        if (slot >= BUSTER_ARRAY_LENGTH(det_units))
        {
            fprintf(stderr, "HARNESS_ERROR more than 64 input slots\n");
            exit(90);
        }
        DetUnit* unit = &det_units[slot];
        unit->lane = (u32)lane_index();
        unit->base = (u64)(uintptr_t)arena;
        unit->position_before = arena->position;
        unit->claimed = __atomic_add_fetch(&det_clock, 1, __ATOMIC_RELAXED);
        if (det_placement)
        {
            /* Consume unrelated, valid aligned storage. Do not overwrite live
               bytes or bypass arena zeroing/dirty-position bookkeeping. */
            u64 size = 4096 + (slot + 1) * 64 + det_placement * 256;
            u8* pad = arena_allocate(arena, u8, size);
            memset(pad, 0x50 + (int)det_placement, (size_t)size);
        }
        unit->position_after = arena->position;
        if (det_schedule && lane_count() > 1)
        {
            u32 rank = det_schedule == 1 ? unit->lane : (u32)lane_count() - 1 - unit->lane;
            struct timespec delay = {.tv_sec = 0, .tv_nsec = (long)rank * 30000000L};
            while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
            {
            }
        }
        unit->started = __atomic_add_fetch(&det_clock, 1, __ATOMIC_RELAXED);
    }
}

BUSTER_GLOBAL_LOCAL void det_unit_end(u64 slot)
{
    if (det_active)
    {
        det_units[slot].finished = __atomic_add_fetch(&det_clock, 1, __ATOMIC_RELAXED);
    }
}

BUSTER_GLOBAL_LOCAL void det_finish(CompilerDriverResult const* result)
{
    if (det_enabled)
    {
        det_active = false;
        FILE* reach = fopen("det-reach.tsv", "wb");
        FILE* records = fopen("det-diag.bin", "wb");
        if (!reach || !records)
        {
            fprintf(stderr, "HARNESS_ERROR opening evidence\n");
            exit(90);
        }
        fprintf(reach, "RESULT workers=%u history=%u priors=%u placement=%u schedule=%u trace=%u bad=%u\n",
                result->compilation_workers, det_history, det_prior_count, det_placement, det_schedule, det_tracing, det_bad);
        for (u32 index = 0; index < det_prior_count; index += 1)
        {
            fprintf(reach, "PRIOR step=%u workers=%u error=%u\n", index, det_prior_workers[index], det_prior_errors[index]);
        }
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(det_units); index += 1)
        {
            DetUnit unit = det_units[index];
            if (unit.claimed)
            {
                fprintf(reach, "UNIT slot=%u lane=%u base=%llx before=%llu after=%llu claimed=%u started=%u finished=%u\n",
                        index, unit.lane, (unsigned long long)unit.base, (unsigned long long)unit.position_before,
                        (unsigned long long)unit.position_after, unit.claimed, unit.started, unit.finished);
            }
        }
        det_string(records, S8("DET_DIAGNOSTICS_V1"));
        det_u64(records, (u32)result->error);
        det_u64(records, (u32)result->codegen_error);
        det_u64(records, (u32)result->object_error);
        det_u64(records, result->tokenizer_error_count);
        det_u64(records, result->tokenizer_warning_count);
        det_u64(records, result->parser_diagnostic_count);
        det_u64(records, result->analysis_diagnostic_count);
        det_string(records, result->warning);
        det_string(records, result->diagnostic);
        det_u64(records, result->diagnostic_count);
        for (u32 index = 0; index < result->diagnostic_count; index += 1)
        {
            CompilerDiagnostic const* diagnostic = &result->diagnostics[index];
            det_u64(records, (u32)diagnostic->severity);
            det_string(records, diagnostic->code);
            det_string(records, diagnostic->symbol);
            det_string(records, diagnostic->message);
            det_location(records, diagnostic->primary);
            det_u64(records, diagnostic->note_count);
            for (u32 note = 0; note < diagnostic->note_count; note += 1)
            {
                det_string(records, diagnostic->notes[note].message);
                det_location(records, diagnostic->notes[note].location);
            }
            det_u64(records, diagnostic->backend != 0);
            if (diagnostic->backend)
            {
                CompilerDiagnosticBackend const* backend = diagnostic->backend;
                det_string(records, backend->target);
                det_string(records, backend->allocator);
                det_string(records, backend->function);
                det_string(records, backend->opcode);
                det_string(records, backend->operation);
                det_string(records, backend->reason);
                det_string(records, backend->referenced_symbol);
                det_u64(records, backend->error_id);
                det_u64(records, backend->function_id);
                det_u64(records, backend->instruction_id);
                det_u64(records, backend->opcode_id);
                det_u64(records, backend->operation_id);
            }
        }
        if (fclose(reach) != 0 || fclose(records) != 0 || det_bad)
        {
            fprintf(stderr, "HARNESS_ERROR invalid history or evidence I/O\n");
            exit(90);
        }
    }
}

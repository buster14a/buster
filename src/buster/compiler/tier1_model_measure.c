// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #181 measurement preparation from the actual selector's origin=plan manifest. This separate diagnostic TU does
// not add observations to x86_zen5.c. It reuses the existing counter, loop-count and encoder machinery; it reports
// original rows and a hash of the complete input before constructing bounded register-renamed experiments.
// Layout: read/normalise -> classify -> fixture/program -> verify/list -> approved-host measurement.
// Memory accesses, flag consumers and control flow require dedicated experiments and remain refused here. A
// missing dependency path is omitted, never reported as zero latency. Retired macro-ops never establish uops/pipes.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "buster/compiler/x86_64_encoder.c"
#include "buster/compiler/x86_semantics.c"
#include "buster/compiler/x86_target.c"
#include "buster/compiler/x86_target_forms.c"
#include "buster/compiler/x86_zen5.c"
#include "buster/compiler/x86_zen5_measure.c"
#pragma clang diagnostic pop

#define BUSTER_TIER1_MODEL_MAX_CASES 4096
#define BUSTER_TIER1_MODEL_LINE_BYTES 1024
#define BUSTER_TIER1_MODEL_FIELDS 20
#define BUSTER_TIER1_MODEL_STREAMS 8

typedef enum Tier1ModelExperiment
{
    TIER1_MODEL_EIGHT,
    TIER1_MODEL_FOUR,
    TIER1_MODEL_DESTINATION,
    TIER1_MODEL_SOURCE,
    TIER1_MODEL_INDEX,
    TIER1_MODEL_MIX_ADD,
    TIER1_MODEL_MIX_IMUL,
    TIER1_MODEL_EXPERIMENT_COUNT,
} Tier1ModelExperiment;

static const char* const tier1_model_experiments[TIER1_MODEL_EXPERIMENT_COUNT] =
{
    "independent-eight",
    "independent-four",
    "chain-destination",
    "chain-source",
    "chain-index",
    "mix-add-four",
    "mix-imul-four",
};

typedef struct Tier1ModelCase Tier1ModelCase;
struct Tier1ModelCase
{
    X86Instruction record;
    X86Instruction key;
    uint64_t first;
    uint64_t occurrences;
    uint8_t cost_id;
    uint8_t experiments;
    const char* refusal;
};

typedef struct Tier1ModelManifest Tier1ModelManifest;
struct Tier1ModelManifest
{
    Tier1ModelCase cases[BUSTER_TIER1_MODEL_MAX_CASES];
    uint64_t rows[3];
    uint64_t hash;
    uint64_t bytes;
    uint64_t missing[X86_COST_COLUMN_COUNT];
    uint64_t unknown;
    uint32_t count;
};

static void tier1_model_classify(Tier1ModelCase* entry);

static uint64_t tier1_model_hash(uint64_t hash, const void* data, uint64_t count)
{
    const uint8_t* bytes = data;
    for (uint64_t i = 0; i < count; i += 1)
    {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

// Equality of operand roles is retained; numeric physical register names are the only normalized fields. Actual
// examples remain verbatim in record. The measurements explicitly describe this renaming, not an extrapolation.
static X86Instruction tier1_model_key(X86Instruction record)
{
    const X86FormDescription* form = x86_form_description(record.form);
    uint8_t* fields[] = { &record.reg, &record.rm, &record.index };
    const bool memory = record.mode != BUSTER_X86_REGISTER_DIRECT;
    const bool active[] = {form->reg != BUSTER_X86_OPERAND_NONE, form->rm != BUSTER_X86_OPERAND_NONE || (memory && (record.mode & BUSTER_X86_MODE_BASE)), memory && (record.mode & BUSTER_X86_MODE_INDEX)};
    uint8_t originals[3] = {0};
    uint8_t assigned[3] = {0};
    uint8_t next = 0;
    // High-byte registers alias their low-byte parent and have distinct encoding constraints. Keep such rows
    // verbatim until classification refuses them; normalizing first would erase precisely that distinction.
    const bool high_byte = ((record.reg | record.rm | record.index | record.vvvv) & BUSTER_X86_REGISTER_HIGH_BYTE) != 0;
    const bool forced_displacement = x86_effects(record.form)->operation == X86_OPERATION_LEA && record.displacement == 0 && (record.rm == X86_REGISTER_RBP || record.rm == X86_REGISTER_R13);
    for (uint32_t field = 0; !high_byte && !forced_displacement && field < 3; field += 1)
    {
        if (active[field])
        {
            uint8_t number = next;
            for (uint32_t previous = 0; previous < field; previous += 1)
            {
                if (active[previous] && originals[previous] == *fields[field])
                {
                    number = assigned[previous];
                }
            }
            originals[field] = *fields[field];
            assigned[field] = number;
            next += number == next;
            *fields[field] = number;
        }
    }
    return record;
}

static bool tier1_model_integer(const char* text, int64_t minimum, int64_t maximum, int64_t* result)
{
    errno = 0;
    char* end;
    const long long parsed = strtoll(text, &end, 10);
    const bool valid = text[0] != 0 && end != text && *end == 0 && errno == 0 && parsed >= minimum && parsed <= maximum && text[0] != '+' && text[0] != ' ';
    if (valid)
    {
        *result = parsed;
    }
    return valid;
}

static bool tier1_model_footer(char* fields, const uint64_t* rows)
{
    static const char* const names[] = {"plan=", "expansion=", "wrapper="};
    bool valid = true;
    char* cursor = fields;
    for (uint32_t origin = 0; valid && origin < 3; origin += 1)
    {
        char* separator = strchr(cursor, ',');
        valid = (separator != 0) == (origin < 2);
        if (separator != 0)
        {
            *separator = 0;
        }
        const uint64_t prefix = strlen(names[origin]);
        int64_t number = 0;
        valid = valid && strncmp(cursor, names[origin], prefix) == 0 && tier1_model_integer(cursor + prefix, 0, INT64_MAX, &number) && (uint64_t)number == rows[origin];
        cursor = separator != 0 ? separator + 1 : cursor;
    }
    return valid;
}

static bool tier1_model_read(const char* path, const uint8_t* allowed, Tier1ModelManifest* manifest)
{
    static const char header[] = "origin,record,form,mnemonic,mode,reg,rm,index,vvvv,decorations,immediate,displacement,cost_id,gpr_latency,throughput,retired_ops,flags_latency,load_extra,executed_uops,pipes\n";
    static const char footer[] = "# tier1-manifest-end,version=1,";
    FILE* file = fopen(path, "rb");
    bool valid = file != 0;
    char line[BUSTER_TIER1_MODEL_LINE_BYTES];
    uint64_t line_number = 0;
    bool complete = false;
    memset(manifest, 0, sizeof(*manifest));
    manifest->hash = UINT64_C(14695981039346656037);
    while (valid && fgets(line, sizeof(line), file) != 0)
    {
        line_number += 1;
        const uint64_t length = strlen(line);
        manifest->hash = tier1_model_hash(manifest->hash, line, length);
        manifest->bytes += length;
        valid = length != 0 && line[length - 1] == '\n';
        if (line_number == 1)
        {
            valid &= strcmp(line, header) == 0;
        }
        else if (valid && !complete && strncmp(line, footer, sizeof(footer) - 1) == 0)
        {
            line[length - 1] = 0;
            valid = tier1_model_footer(line + sizeof(footer) - 1, manifest->rows);
            complete = valid;
        }
        else if (valid && !complete)
        {
            line[length - 1] = 0;
            char* fields[BUSTER_TIER1_MODEL_FIELDS] = {line};
            uint32_t count = 1;
            for (uint64_t byte = 0; byte + 1 < length; byte += 1)
            {
                if (line[byte] == ',')
                {
                    valid &= count < BUSTER_TIER1_MODEL_FIELDS;
                    if (valid)
                    {
                        line[byte] = 0;
                        fields[count++] = line + byte + 1;
                    }
                }
            }
            valid &= count == BUSTER_TIER1_MODEL_FIELDS;
            int64_t values[BUSTER_TIER1_MODEL_FIELDS] = {0};
            for (uint32_t field = 1; valid && field < BUSTER_TIER1_MODEL_FIELDS; field += 1)
            {
                if (field != 3)
                {
                    const int64_t minimum = field == 10 || field == 11 ? INT32_MIN : 0;
                    const int64_t maximum = field == 1 ? INT64_MAX : field == 10 || field == 11 ? INT32_MAX : field >= 4 && field <= 9 ? UINT8_MAX : UINT16_MAX;
                    valid = tier1_model_integer(fields[field], minimum, maximum, &values[field]);
                }
            }
            const uint32_t origin = strcmp(fields[0], "plan") == 0 ? 0 : strcmp(fields[0], "expansion") == 0 ? 1 : strcmp(fields[0], "wrapper") == 0 ? 2 : 3;
            valid &= origin < 3;
            X86Instruction record =
            {
                .immediate = (int32_t)values[10],
                .displacement = (int32_t)values[11],
                .reg = (uint8_t)values[5],
                .rm = (uint8_t)values[6],
                .index = (uint8_t)values[7],
                .mode = (uint8_t)values[4],
                .form = (uint16_t)values[2],
                .vvvv = (uint8_t)values[8],
                .decorations = (uint8_t)values[9],
            };
            if (valid)
            {
                valid = (uint64_t)values[1] == manifest->rows[origin] + 1 && x86_target_validate(allowed, &record, 1) == 1;
            }
            uint8_t id = 0;
            if (valid)
            {
                id = x86_zen5_id(&record);
                valid = id == values[12] && strcmp(x86_form_description(record.form)->mnemonic, fields[3]) == 0;
                for (uint32_t column = 0; valid && column < X86_COST_COLUMN_COUNT; column += 1)
                {
                    valid = x86_zen5_columns[column][id] == values[13 + column];
                }
            }
            if (valid)
            {
                manifest->rows[origin] += 1;
                if (origin == 0)
                {
                    manifest->unknown += id == 0;
                    for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
                    {
                        manifest->missing[column] += values[13 + column] == BUSTER_X86_COST_UNKNOWN;
                    }
                    Tier1ModelCase candidate = { .record = record, .key = tier1_model_key(record), .first = (uint64_t)values[1], .cost_id = id };
                    // Classify each original row before grouping. A later refused or unknown row must not inherit
                    // the first row's eligibility or model ID just because its ordinary register names normalize.
                    tier1_model_classify(&candidate);
                    uint32_t index = 0;
                    bool matched = false;
                    while (index < manifest->count && !matched)
                    {
                        const Tier1ModelCase* existing = &manifest->cases[index];
                        const bool refusal_matches = candidate.refusal == 0 ? existing->refusal == 0 : existing->refusal != 0 && strcmp(candidate.refusal, existing->refusal) == 0;
                        matched = memcmp(&candidate.key, &existing->key, sizeof(candidate.key)) == 0 && candidate.cost_id == existing->cost_id && candidate.experiments == existing->experiments && refusal_matches;
                        index += !matched;
                    }
                    valid = index < BUSTER_TIER1_MODEL_MAX_CASES;
                    if (valid && index == manifest->count)
                    {
                        manifest->cases[index] = candidate;
                        manifest->count += 1;
                    }
                    if (valid)
                    {
                        manifest->cases[index].occurrences += 1;
                    }
                }
            }
        }
        else
        {
            valid = false;
        }
    }
    if (file != 0)
    {
        valid &= !ferror(file);
        fclose(file);
    }
    valid &= complete && line_number != 0 && manifest->rows[0] != 0;
    if (!valid)
    {
        fprintf(stderr, "tier1-model: invalid/incompatible manifest near line %" PRIu64 "; no records measured\n", line_number);
    }
    return valid;
}

// Preserve aliases inside each record. Source/index chains change a distinct read operand to the previous result;
// destination chains keep the existing read/write register. Thus MOV-immediate and zero idioms cannot fake a chain.
static X86Instruction tier1_model_fixture(const Tier1ModelCase* entry, uint8_t destination, uint8_t source, Tier1ModelExperiment experiment)
{
    X86Instruction record = entry->record;
    const X86FormDescription* form = x86_form_description(record.form);
    if (x86_effects(record.form)->operation == X86_OPERATION_LEA)
    {
        const bool base_alias = record.reg == record.rm;
        const bool index_alias = record.reg == record.index;
        const bool inputs_alias = record.rm == record.index;
        record.reg = destination;
        record.rm = base_alias ? destination : experiment == TIER1_MODEL_SOURCE ? source : X86_REGISTER_R11;
        if ((record.mode & BUSTER_X86_MODE_INDEX) != 0)
        {
            record.index = index_alias ? destination : inputs_alias ? record.rm : experiment == TIER1_MODEL_INDEX ? source : X86_REGISTER_R14;
        }
    }
    else if (form->syntax == X86_SYNTAX_REG_RM || form->syntax == X86_SYNTAX_REG_RM_IMM)
    {
        const bool alias = record.reg == record.rm;
        record.reg = destination;
        record.rm = alias ? destination : source;
    }
    else if (form->syntax == X86_SYNTAX_RM_REG)
    {
        const bool alias = record.reg == record.rm;
        record.rm = destination;
        record.reg = alias ? destination : source;
    }
    else if (form->syntax != X86_SYNTAX_ACC_IMM)
    {
        record.rm = destination;
    }
    return record;
}

static void tier1_model_classify(Tier1ModelCase* entry)
{
    const X86Instruction record = entry->record;
    const X86Effects* effects = x86_effects(record.form);
    const X86FormDescription* form = x86_form_description(record.form);
    uint64_t reads;
    uint64_t writes;
    x86_instruction_effects_scalar(&record, &reads, &writes);
    const uint64_t prohibited = BUSTER_X86_RESOURCE_MEMORY | BUSTER_X86_RESOURCE_CONTROL | BUSTER_X86_RESOURCE_CALL | BUSTER_X86_RESOURCE_VECTORS | BUSTER_X86_RESOURCE_MXCSR;
    const bool lea = effects->operation == X86_OPERATION_LEA;
    bool operation = false;
    switch (effects->operation)
    {
    case X86_OPERATION_ADD:
    case X86_OPERATION_SUB:
    case X86_OPERATION_AND:
    case X86_OPERATION_OR:
    case X86_OPERATION_XOR:
    case X86_OPERATION_CMP:
    case X86_OPERATION_TEST:
    case X86_OPERATION_MOV:
    case X86_OPERATION_MOVZX:
    case X86_OPERATION_LEA:
    case X86_OPERATION_IMUL:
    case X86_OPERATION_INC:
    case X86_OPERATION_DEC:
    case X86_OPERATION_NEG:
    case X86_OPERATION_NOT:
    case X86_OPERATION_SHL:
    case X86_OPERATION_SHR:
    case X86_OPERATION_SAR:
        operation = true;
        break;
    default:
        break;
    }
    const bool syntax = form->syntax == X86_SYNTAX_REG_RM || form->syntax == X86_SYNTAX_REG_RM_IMM || form->syntax == X86_SYNTAX_RM_REG || form->syntax == X86_SYNTAX_RM_IMM || form->syntax == X86_SYNTAX_RM || form->syntax == X86_SYNTAX_RM_ONE || form->syntax == X86_SYNTAX_ACC_IMM;
    const bool implicit = ((effects->implicit_reads | effects->implicit_writes) & BUSTER_X86_RESOURCE_GPRS) != 0 && form->syntax != X86_SYNTAX_ACC_IMM;
    const bool high_byte = ((record.reg | record.rm | record.index | record.vvvv) & BUSTER_X86_REGISTER_HIGH_BYTE) != 0;
    entry->refusal = high_byte ? "high-byte-register" : !operation || !syntax ? "operation-or-syntax" : effects->width != 4 && effects->width != 8 ? "operand-width" : ((reads | writes) & prohibited) != 0 ? "memory-control-or-special" : (reads & BUSTER_X86_RESOURCE_FLAGS) != 0 ? "flags-consumer" : implicit ? "implicit-register" : record.decorations != 0 || record.vvvv != 0 ? "decorated" : 0;
    if (entry->refusal == 0 && lea && (record.mode != BUSTER_X86_MEMORY_BASE && (record.mode & ~BUSTER_X86_MODE_SCALE) != BUSTER_X86_MEMORY_BASE_INDEX(0)))
    {
        entry->refusal = "address-shape";
    }
    if (entry->refusal == 0 && (record.rm == X86_REGISTER_RBP || record.rm == X86_REGISTER_R13) && lea && record.displacement == 0)
    {
        entry->refusal = "forced-zero-displacement";
    }
    if (entry->refusal == 0)
    {
        const bool accumulator = form->syntax == X86_SYNTAX_ACC_IMM;
        entry->experiments = accumulator ? 0 : (1u << TIER1_MODEL_EIGHT) | (1u << TIER1_MODEL_FOUR) | (1u << TIER1_MODEL_MIX_ADD) | (1u << TIER1_MODEL_MIX_IMUL);
        for (uint32_t experiment = TIER1_MODEL_DESTINATION; experiment <= TIER1_MODEL_INDEX; experiment += 1)
        {
            const bool destination_path = experiment == TIER1_MODEL_DESTINATION;
            const uint8_t destination = X86_REGISTER_RAX;
            const uint8_t source = destination_path ? X86_REGISTER_R11 : X86_REGISTER_RDX;
            const X86Instruction fixture = tier1_model_fixture(entry, destination, source, (Tier1ModelExperiment)experiment);
            x86_instruction_effects_scalar(&fixture, &reads, &writes);
            const uint8_t dependency = destination_path ? destination : source;
            const bool eligible = (experiment != TIER1_MODEL_INDEX || lea) && (!accumulator || destination_path) && (reads & (UINT64_C(1) << dependency)) != 0 && (writes & (UINT64_C(1) << destination)) != 0;
            entry->experiments |= eligible << experiment;
        }
        if (entry->experiments == 0)
        {
            entry->refusal = "no-independent-or-dependent-path";
        }
    }
}

static bool tier1_model_program(const Tier1ModelCase* entry, Tier1ModelExperiment experiment, X86CostProgram* program)
{
    static const uint8_t registers[BUSTER_TIER1_MODEL_STREAMS] = {X86_REGISTER_RAX, X86_REGISTER_RDX, X86_REGISTER_R8, X86_REGISTER_R9, X86_REGISTER_R10, X86_REGISTER_RCX, X86_REGISTER_RDI, X86_REGISTER_RSI};
    X86Instruction records[BUSTER_X86_COST_RECORDS];
    uint32_t offsets[BUSTER_X86_COST_RECORDS];
    uint32_t count = 0;
    memset(program, 0, sizeof(*program));
    bool valid = entry == 0 || (entry->experiments & (1u << experiment)) != 0;
    records[count++] = (X86Instruction){ .form = X86_FORM_PUSH_R64, .rm = X86_REGISTER_R12 };
    records[count++] = (X86Instruction){ .form = X86_FORM_PUSH_R64, .rm = X86_REGISTER_R14 };
    records[count++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_R12, .rm = X86_REGISTER_RDI };
    records[count++] = (X86Instruction){ .form = X86_FORM_MOV_R32_IMM32, .rm = X86_REGISTER_R11, .immediate = 3 };
    records[count++] = (X86Instruction){ .form = X86_FORM_MOV_R32_IMM32, .rm = X86_REGISTER_R14, .immediate = 5 };
    for (uint32_t i = 0; i < BUSTER_TIER1_MODEL_STREAMS; i += 1)
    {
        records[count++] = (X86Instruction){ .form = X86_FORM_MOV_R32_IMM32, .rm = registers[i], .immediate = 7 };
    }
    uint8_t prefix[512];
    const int64_t prefix_length = x86_encode_scalar(records, count, prefix, sizeof(prefix), 0, 0);
    valid &= prefix_length > 0;
    if (valid)
    {
        for (uint32_t i = 0; i < (64 - prefix_length % 64) % 64; i += 1)
        {
            records[count++] = (X86Instruction){ .form = X86_FORM_NOP };
        }
        const uint32_t loop = count;
        const bool mix = experiment == TIER1_MODEL_MIX_ADD || experiment == TIER1_MODEL_MIX_IMUL;
        const bool destination_path = experiment == TIER1_MODEL_DESTINATION;
        const bool source_path = experiment == TIER1_MODEL_SOURCE || experiment == TIER1_MODEL_INDEX;
        const uint32_t streams = mix || experiment == TIER1_MODEL_FOUR ? 4 : BUSTER_TIER1_MODEL_STREAMS;
        for (uint32_t i = 0; valid && entry != 0 && i < BUSTER_X86_COST_BLOCK; i += 1)
        {
            const uint8_t destination = registers[destination_path ? 0 : source_path ? (i + 1) % BUSTER_TIER1_MODEL_STREAMS : i % streams];
            const uint8_t source = source_path ? registers[i % BUSTER_TIER1_MODEL_STREAMS] : X86_REGISTER_R11;
            const X86Instruction fixture = tier1_model_fixture(entry, destination, source, experiment);
            const X86Instruction key = tier1_model_key(fixture);
            uint64_t reads;
            uint64_t writes;
            x86_instruction_effects_scalar(&fixture, &reads, &writes);
            valid = memcmp(&key, &entry->key, sizeof(key)) == 0 && x86_validate(&fixture, 1) == 1;
            if (destination_path || source_path)
            {
                const uint8_t dependency = destination_path ? destination : source;
                valid &= (reads & (UINT64_C(1) << dependency)) != 0 && (writes & (UINT64_C(1) << destination)) != 0;
            }
            records[count++] = fixture;
            if (mix)
            {
                const uint8_t blocker = registers[4 + i % 4];
                records[count++] = experiment == TIER1_MODEL_MIX_ADD ? (X86Instruction){ .form = X86_FORM_ADD_RM64_R64, .rm = blocker, .reg = X86_REGISTER_R11 } : (X86Instruction){ .form = X86_FORM_IMUL_R64_RM64, .reg = blocker, .rm = X86_REGISTER_R11 };
            }
        }
        program->body_count = count - loop;
        records[count++] = (X86Instruction){ .form = X86_FORM_DEC_RM64, .rm = X86_REGISTER_R12 };
        const uint32_t branch = count;
        records[count++] = (X86Instruction){ .form = X86_FORM_JNE_REL32 };
        records[count++] = (X86Instruction){ .form = X86_FORM_POP_R64, .rm = X86_REGISTER_R14 };
        records[count++] = (X86Instruction){ .form = X86_FORM_POP_R64, .rm = X86_REGISTER_R12 };
        records[count++] = (X86Instruction){ .form = X86_FORM_RET };
        valid &= count <= BUSTER_X86_COST_RECORDS && x86_validate(records, count) == count;
        if (valid)
        {
            program->code = mmap(0, BUSTER_X86_COST_CODE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            valid = program->code != MAP_FAILED;
            if (!valid)
            {
                program->code = 0;
            }
        }
        if (valid)
        {
            int64_t length = x86_encode_scalar(records, count, program->code, BUSTER_X86_COST_CODE_SIZE, offsets, 0);
            valid = length > 0 && offsets[loop] % 64 == 0;
            if (valid)
            {
                records[branch].displacement = (int32_t)offsets[loop] - (int32_t)offsets[branch + 1];
                length = x86_encode_scalar(records, count, program->code, BUSTER_X86_COST_CODE_SIZE, offsets, 0);
                uint8_t vector[BUSTER_X86_COST_CODE_SIZE];
                const int64_t vector_length = x86_encode(records, count, vector, sizeof(vector), 0, 0);
                valid = vector_length == length && length > 0 && memcmp(vector, program->code, length) == 0;
            }
            if (valid)
            {
                program->length = (uint32_t)length;
                program->hash = tier1_model_hash(UINT64_C(14695981039346656037), program->code, program->length);
                valid = mprotect(program->code, BUSTER_X86_COST_CODE_SIZE, PROT_READ | PROT_EXEC) == 0;
            }
        }
    }
    if (!valid && program->code != 0)
    {
        munmap(program->code, BUSTER_X86_COST_CODE_SIZE);
        program->code = 0;
    }
    return valid;
}

static bool tier1_model_cpu(char* brand, uint32_t* signature)
{
    uint32_t eax, ebx, ecx, edx;
    __cpuid(1, eax, ebx, ecx, edx);
    *signature = eax;
    for (uint32_t i = 0; i < 3; i += 1)
    {
        uint32_t words[4];
        __cpuid(0x80000002 + i, words[0], words[1], words[2], words[3]);
        memcpy(brand + 16 * i, words, 16);
    }
    brand[48] = 0;
    return *signature == 0xb40f40 && strstr(brand, "AMD Ryzen 7 9700X") != 0;
}

static uint64_t tier1_model_time(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC_RAW, &time);
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
}

static bool tier1_model_measure_case(uint32_t id, const Tier1ModelCase* entry, uint64_t iterations, uint32_t passes, bool verify, const X86CostCounters* counters, const X86CostProgram* baseline)
{
    X86CostProgram programs[TIER1_MODEL_EXPERIMENT_COUNT] = {0};
    uint64_t cycles[TIER1_MODEL_EXPERIMENT_COUNT][BUSTER_X86_COST_MAX_PASSES] = {0};
    uint64_t times[TIER1_MODEL_EXPERIMENT_COUNT][BUSTER_X86_COST_MAX_PASSES] = {0};
    uint64_t operations[TIER1_MODEL_EXPERIMENT_COUNT][BUSTER_X86_COST_MAX_PASSES] = {0};
    uint64_t instructions[TIER1_MODEL_EXPERIMENT_COUNT][BUSTER_X86_COST_MAX_PASSES] = {0};
    bool valid = true;
    for (uint32_t experiment = 0; valid && experiment < TIER1_MODEL_EXPERIMENT_COUNT; experiment += 1)
    {
        if ((entry->experiments & (1u << experiment)) != 0)
        {
            valid = tier1_model_program(entry, (Tier1ModelExperiment)experiment, &programs[experiment]);
            if (valid)
            {
                x86_zen5_execute(&programs[experiment], verify ? 2 : iterations);
            }
        }
    }
    for (uint32_t pass = 0; valid && !verify && pass < passes; pass += 1)
    {
        X86CostSample empty = {0};
        valid = x86_zen5_sample(baseline, iterations, counters, &empty);
        for (uint32_t offset = 0; valid && offset < TIER1_MODEL_EXPERIMENT_COUNT; offset += 1)
        {
            const uint32_t experiment = (pass + offset) % TIER1_MODEL_EXPERIMENT_COUNT;
            if (programs[experiment].code != 0)
            {
                X86CostSample sample = {0};
                const uint64_t start = tier1_model_time();
                valid = x86_zen5_sample(&programs[experiment], iterations, counters, &sample);
                const uint64_t elapsed = tier1_model_time() - start;
                valid &= sample.values[2] >= empty.values[2] && sample.values[1] >= empty.values[1];
                cycles[experiment][pass] = sample.values[0];
                times[experiment][pass] = elapsed;
                operations[experiment][pass] = sample.values[2] - empty.values[2];
                instructions[experiment][pass] = sample.values[1] - empty.values[1];
            }
        }
    }
    for (uint32_t experiment = 0; experiment < TIER1_MODEL_EXPERIMENT_COUNT; experiment += 1)
    {
        if (programs[experiment].code != 0)
        {
            if (valid && !verify)
            {
                const uint64_t median = x86_zen5_median(cycles[experiment], passes);
                const uint64_t elapsed = x86_zen5_median(times[experiment], passes);
                const uint64_t retired = x86_zen5_median(operations[experiment], passes);
                const uint64_t issued = x86_zen5_median(instructions[experiment], passes);
                const double denominator = (double)iterations * BUSTER_X86_COST_BLOCK;
                printf("%u\t%" PRIu64 "\t%s\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%016" PRIx64 "\n", id, entry->first, tier1_model_experiments[experiment], median / denominator, cycles[experiment][0] / denominator, cycles[experiment][passes - 1] / denominator, elapsed / denominator, retired / denominator, issued / denominator, programs[experiment].hash);
            }
            munmap(programs[experiment].code, BUSTER_X86_COST_CODE_SIZE);
        }
    }
    if (!valid)
    {
        fprintf(stderr, "tier1-model: fixture/measurement failed for case %u from plan record %" PRIu64 "\n", id, entry->first);
    }
    return valid;
}

// argv[0] is the command name, matching the separate benchmark TUs. --list and --verify never produce timings.
int run_tier1_model_measure(int argc, char** argv);
int run_tier1_model_measure(int argc, char** argv)
{
    const char* path = 0;
    uint64_t only = UINT64_MAX;
    uint64_t iterations = 10000;
    uint64_t passes = 7;
    bool list = false;
    bool verify = false;
    bool valid = true;
    for (int i = 1; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--manifest") == 0 && i + 1 < argc)
        {
            path = argv[++i];
        }
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            valid = x86_zen5_number(argv[++i], 0, BUSTER_TIER1_MODEL_MAX_CASES - 1, &only);
        }
        else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc)
        {
            valid = x86_zen5_number(argv[++i], 1, 10000000, &iterations);
        }
        else if (strcmp(argv[i], "--passes") == 0 && i + 1 < argc)
        {
            valid = x86_zen5_number(argv[++i], 3, BUSTER_X86_COST_MAX_PASSES, &passes) && (passes & 1) != 0;
        }
        else if (strcmp(argv[i], "--list") == 0)
        {
            list = true;
        }
        else if (strcmp(argv[i], "--verify") == 0)
        {
            verify = true;
        }
        else
        {
            valid = false;
        }
    }
    valid &= path != 0 && !(list && verify);
    Tier1ModelManifest* manifest = 0;
    uint8_t* allowed = 0;
    if (valid)
    {
        manifest = malloc(sizeof(*manifest));
        allowed = malloc(X86_FORM_COUNT);
        valid = manifest != 0 && allowed != 0 && x86_encoder_initialize() && x86_semantics_initialize() && x86_zen5_initialize();
    }
    if (valid)
    {
        x86_target_forms(&(X86Target){ .architecture = X86_ARCH_BASELINE }, allowed);
        valid = tier1_model_read(path, allowed, manifest);
    }
    uint64_t eligible_records = 0;
    uint32_t eligible_cases = 0;
    uint32_t unknown_cases = 0;
    if (valid)
    {
        for (uint32_t i = 0; i < manifest->count; i += 1)
        {
            const Tier1ModelCase* entry = &manifest->cases[i];
            eligible_cases += entry->refusal == 0;
            eligible_records += entry->refusal == 0 ? entry->occurrences : 0;
            unknown_cases += entry->cost_id == 0;
        }
        valid = only == UINT64_MAX || (only < manifest->count && manifest->cases[only].refusal == 0);
        printf("# manifest_fnv1a64=%016" PRIx64 " bytes=%" PRIu64 " plan_rows=%" PRIu64 " expansion_rows=%" PRIu64 " wrapper_rows=%" PRIu64 "\n", manifest->hash, manifest->bytes, manifest->rows[0], manifest->rows[1], manifest->rows[2]);
        printf("# cases=%u unknown_cases=%u unknown_records=%" PRIu64 " eligible_cases=%u refused_cases=%u eligible_records=%" PRIu64 " refused_records=%" PRIu64 "\n", manifest->count, unknown_cases, manifest->unknown, eligible_cases, manifest->count - eligible_cases, eligible_records, manifest->rows[0] - eligible_records);
        printf("# missing_columns gpr_latency=%" PRIu64 " throughput=%" PRIu64 " retired_ops=%" PRIu64 " flags_latency=%" PRIu64 " load_extra=%" PRIu64 " executed_uops=%" PRIu64 " pipes=%" PRIu64 "\n", manifest->missing[0], manifest->missing[1], manifest->missing[2], manifest->missing[3], manifest->missing[4], manifest->missing[5], manifest->missing[6]);
        printf("# register-renamed exact form/immediate/address/alias cases; flags latency, load increments, executed uops and pipes remain unknown; no model update\n");
    }
    if (valid && list)
    {
        printf("case,first_plan_record,occurrences,form,mnemonic,mode,reg,rm,index,vvvv,decorations,immediate,displacement,cost_id,experiment_mask,refusal\n");
        for (uint32_t i = 0; i < manifest->count; i += 1)
        {
            const Tier1ModelCase* entry = &manifest->cases[i];
            const X86Instruction r = entry->record;
            printf("%u,%" PRIu64 ",%" PRIu64 ",%u,%s,%u,%u,%u,%u,%u,%u,%d,%d,%u,%u,%s\n", i, entry->first, entry->occurrences, r.form, x86_form_description(r.form)->mnemonic, r.mode, r.reg, r.rm, r.index, r.vvvv, r.decorations, r.immediate, r.displacement, entry->cost_id, entry->experiments, entry->refusal != 0 ? entry->refusal : "");
        }
    }
    X86CostCounters counters = { .fd = {-1, -1, -1} };
    X86CostProgram baseline = {0};
    if (valid && !list && !verify)
    {
        char brand[49];
        uint32_t signature;
        valid = tier1_model_cpu(brand, &signature);
        if (!valid)
        {
            fprintf(stderr, "measure-tier1-model requires the approved 9700X; --list/--verify are construction diagnostics\n");
        }
        else
        {
            printf("# CPU=%s CPUID=%08x compiler=%s iterations=%" PRIu64 " passes=%" PRIu64 " block=%u\n", brand, signature, __clang_version__, iterations, passes, BUSTER_X86_COST_BLOCK);
            printf("# cycles are core cycles without subtraction; retired_ops/instructions subtract a matched empty loop; ns includes counter-control overhead; mixes are per target+blocker pair\n");
            printf("case\tfirst_plan_record\texperiment\tcycles\tmin\tmax\tns\tretired_ops\tinstructions\tcode_hash\n");
            valid = x86_zen5_counters(&counters) && tier1_model_program(0, TIER1_MODEL_EIGHT, &baseline);
        }
    }
    uint32_t completed = 0;
    for (uint32_t i = 0; valid && !list && i < manifest->count; i += 1)
    {
        const Tier1ModelCase* entry = &manifest->cases[i];
        if (entry->refusal == 0 && (only == i || (only == UINT64_MAX && (verify || entry->cost_id == 0))))
        {
            valid = tier1_model_measure_case(i, entry, iterations, (uint32_t)passes, verify, &counters, &baseline);
            completed += valid;
        }
    }
    if (valid && verify)
    {
        printf("# verified=%u cases; native construction/encoding/loop smoke only, no target measurements\n", completed);
    }
    if (baseline.code != 0)
    {
        munmap(baseline.code, BUSTER_X86_COST_CODE_SIZE);
    }
    for (uint32_t i = 0; i < 3; i += 1)
    {
        if (counters.fd[i] >= 0)
        {
            close(counters.fd[i]);
        }
    }
    free(allowed);
    free(manifest);
    if (!valid)
    {
        fprintf(stderr, "usage: measure-tier1-model --manifest CSV [--list|--verify] [--only CASE] [--iterations N] [--passes ODD]\n");
    }
    return valid ? 0 : 1;
}

#include "buster/compiler/tier1_model_measure_test.c"

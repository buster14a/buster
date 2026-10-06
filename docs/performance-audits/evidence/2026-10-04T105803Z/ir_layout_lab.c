// Canonical IR layout lab (audit 2026-10-04T105803Z, issue #2612).
//
// A standalone unity program over the production C frontend and ir.c. It
// lowers one real translation unit with the real frontend, runs the real
// ir_prepare_canonical_module (timed), then copies the published module into
// three alternative physical layouts and runs equal-work kernels over all of
// them. Every kernel returns a checksum that must agree across layouts, so a
// faster layout cannot have skipped work.
//
//   A  current rows: IrInstruction (64 B, three payload pointers), IrValue
//      (16 B), operand/target/immediate slices behind the row pointers.
//      A_pub is the published block-span order consumers see; A_con is the
//      pre-publication construction order the preparation passes see, with
//      the `next` chain in the row (reconstructed from instruction_remap).
//   B  split hot/cold: 16-byte hot row {result, type, operand_offset,
//      operand_count:16, opcode, flags} + parallel 32-byte cold row + u32
//      pool offsets; `next` is a separate u32 array for construction order.
//   C  compact indexed record: 32-byte row with two inline operand ids, one
//      merged sub-operation byte, `next`, and a u32 extension index into a
//      24-byte cold table present only for rows that carry targets,
//      immediates, a symbol, a local or memory orders. Operands beyond two
//      live in an overflow pool addressed by the first inline slot.
//   D  chunked stable storage: the unchanged 64-byte IrInstruction in 64-row
//      chunks behind a chunk directory; ids never move and appends never copy.
//
// Kernels (per layout, equal work, shared checksum):
//   census          opcode + result presence over every row (narrow sweep)
//   uses            per-value use counts over every operand (DCE / selector)
//   defs_hot        operand -> values[].definition -> defining row's opcode
//                   and operand_count (both hot everywhere)
//   defs_cold       as defs_hot but reads the defining row's immediate_count
//                   (cold in B, extension in C)
//   chain           `next`-only ownership walk in construction order; `span`
//                   is the published dense-span equivalent
//   validate        validation-shaped per-row check in chain order: opcode,
//                   type bound, operand bounds, result binding, targets and
//                   immediate presence
//   compact         ir_rewrite_compact-shaped removal of a fixed 12% mask:
//                   instruction/value maps, remapped operands, copied rows
//   append          from-trace construction into a fresh function
//
// Output is `IR_LAYOUT_LAB_* key=value` lines. Nothing here is a whole
// compiler timing; see the audit for the reading rule.

#define BUSTER_USE_GRAPHICS 0

#include <buster/lib/base.h>
#include <buster/lib/entry_point.h>
#include <buster/lib/time.h>
#include <buster/lib/arena.h>
#include <buster/lib/os.h>
#include <buster/lib/system_headers.h>
#include <buster/lib/file.h>
#include <buster/lib/string.h>
#include <buster/lib/target.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/compiler/ir/ir_construction.h>
#include <buster/lib/compiler/ir/ir_append.h>

#include <buster/lib/byte_writer.c>
#include <buster/lib/arena.c>
#include <buster/lib/integer.c>
#include <buster/lib/os.c>
#include <buster/lib/string.c>
#include <buster/lib/entry_point.c>
#include <buster/lib/target.c>
#include <buster/lib/simd.c>
#include <buster/lib/file.c>
#include <buster/lib/time.c>
#include <buster/lib/float.c>
#include <buster/lib/compiler/frontend/c/c.c>
#include <buster/lib/compiler/ir/ir.c>
#include <buster/lib/compiler/diagnostic.c>
#include <buster/lib/hash.c>

#if BUSTER_COMPILER_MSVC
#define LAB_NOINLINE __declspec(noinline)
#else
#define LAB_NOINLINE __attribute__((noinline))
#endif

#define LAB_INVALID UINT32_MAX
#define LAB_CHUNK_SHIFT 6
#define LAB_CHUNK_ROWS (1u << LAB_CHUNK_SHIFT)
#define LAB_REMOVAL_PERMILLE 120
#define LAB_MAX_REPEAT 64

// ---------------------------------------------------------------------------
// Program state and arguments.

typedef struct LabProgram LabProgram;
struct LabProgram
{
    ProgramState state;
    String8 source_path;
    String8* include_paths;
    String8* system_include_paths;
    CPreprocessorDefinition* definitions;
    u32 include_path_count;
    u32 system_include_path_count;
    u32 definition_count;
    u32 repeat;
    bool disable_direct_ssa;
    bool skip_frontend_timing;
};

BUSTER_GLOBAL_LOCAL LabProgram lab = {.repeat = 9};
BUSTER_V_IMPL ProgramState* program_state = &lab.state;

ProcessResult process_arguments(void)
{
    SliceString8 arguments = program_state->input.arguments;
    Arena* arena = program_state->arena;
    lab.include_paths = arena_allocate(arena, String8, arguments.length + 1);
    lab.system_include_paths = arena_allocate(arena, String8, arguments.length + 8);
    lab.definitions = arena_allocate(arena, CPreprocessorDefinition, arguments.length + 1);
    ProcessResult result = PROCESS_RESULT_SUCCESS;
    for (u64 index = 1; index < arguments.length && result == PROCESS_RESULT_SUCCESS; index += 1)
    {
        String8 argument = arguments.pointer[index];
        if (string_starts_with_sequence(argument, S8("-I")))
        {
            lab.include_paths[lab.include_path_count++] = string_slice(argument, 2, argument.length);
        }
        else if (string_starts_with_sequence(argument, S8("-isystem=")))
        {
            lab.system_include_paths[lab.system_include_path_count++] = string_slice(argument, 9, argument.length);
        }
        else if (string_starts_with_sequence(argument, S8("-D")))
        {
            String8 operand = string_slice(argument, 2, argument.length);
            u64 equal = string_first_code_unit(operand, '=');
            CPreprocessorDefinition definition = {.name = operand, .value = S8("1")};
            if (equal != BUSTER_STRING_NO_MATCH)
            {
                definition.name = string_slice(operand, 0, equal);
                definition.value = string_slice(operand, equal + 1, operand.length);
            }
            lab.definitions[lab.definition_count++] = definition;
        }
        else if (string_starts_with_sequence(argument, S8("--repeat=")))
        {
            u64 value = 0;
            String8 digits = string_slice(argument, 9, argument.length);
            for (u64 digit = 0; digit < digits.length; digit += 1)
            {
                value = value * 10 + (u64)(digits.pointer[digit] - '0');
            }
            lab.repeat = (u32)BUSTER_MIN(value ? value : 1, (u64)LAB_MAX_REPEAT);
        }
        else if (string_equal(argument, S8("--no-frontend-ssa")))
        {
            lab.disable_direct_ssa = true;
        }
        else if (string_starts_with_sequence(argument, S8("-")))
        {
            string_print(S8("ir_layout_lab: unknown option {S8}\n"), argument);
            result = PROCESS_RESULT_FAILED;
        }
        else
        {
            lab.source_path = argument;
        }
    }
    if (result == PROCESS_RESULT_SUCCESS && !lab.source_path.length)
    {
        string_print(S8("usage: ir_layout_lab [-Idir]... [-isystem=dir]... [-DNAME[=VALUE]]... [--repeat=N] [--no-frontend-ssa] source.c\n"));
        result = PROCESS_RESULT_FAILED;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Alternative layouts.

typedef struct LabHotRow LabHotRow;
struct LabHotRow
{
    u32 result;
    u32 canonical_type;
    u32 operand_offset;
    u16 operand_count;
    u8 opcode;
    u8 flags; // bit 0 volatile_access, bit 1 immediate_is_negative, bit 2 atomic_signal_fence, bit 3 wide operand count (count in pool)
};
BUSTER_CT_CHECK(sizeof(LabHotRow) == 16);

typedef struct LabColdRow LabColdRow;
struct LabColdRow
{
    u32 target_offset;
    u32 immediate_offset;
    u32 symbol;
    u32 canonical_local;
    u16 target_count;
    u16 immediate_count;
    u8 conversion_operation;
    u8 unary_operation;
    u8 binary_operation;
    u8 memory_order;
    u8 failure_memory_order;
    u8 atomic_operation;
    u8 simd_operation;
    u8 reserved[5];
};
BUSTER_CT_CHECK(sizeof(LabColdRow) == 32);

typedef struct LabCompactRow LabCompactRow;
struct LabCompactRow
{
    u32 result;
    u32 canonical_type;
    // operand_count <= 2: the operand ids. Otherwise a is the offset of all
    // operands in the overflow pool and b is unused.
    u32 a;
    u32 b;
    u32 extension; // LAB_INVALID when the row carries no cold payload
    u32 next;
    u16 operand_count;
    u8 opcode;
    // The one sub-operation the opcode selects (conversion, unary, binary,
    // atomic or SIMD), stored in one byte because they are mutually exclusive.
    u8 operation;
    u8 flags;
    u8 reserved[3];
};
BUSTER_CT_CHECK(sizeof(LabCompactRow) == 32);

typedef struct LabExtension LabExtension;
struct LabExtension
{
    u32 target_offset;
    u32 immediate_offset;
    u32 symbol;
    u32 canonical_local;
    u16 target_count;
    u16 immediate_count;
    u8 memory_order;
    u8 failure_memory_order;
    u8 reserved[2];
};
BUSTER_CT_CHECK(sizeof(LabExtension) == 24);

// One lowered function in every layout. Published (block-span) order is the
// identity the consumers see; construction order is the order the
// preparation passes saw before publication permuted the rows.
typedef struct LabFunction LabFunction;
struct LabFunction
{
    IrFunction* function;
    u32 row_count;
    u32 value_count;
    u32 block_count;
    u64 operand_total;
    // Construction order: con_of_pub[q] = construction index of published row q,
    // pub_of_con[p] = published row at construction index p.
    u32* con_of_pub;
    u32* pub_of_con;
    // A_con: the 64-byte rows in construction order with `next` chains;
    // block_first_con/block_last_con are the chain heads/tails.
    IrInstruction* rows_con;
    u32* block_first_con;
    u32* block_last_con;
    // B, published order plus a construction-order copy with a separate next array.
    LabHotRow* hot;
    LabColdRow* cold;
    LabHotRow* hot_con;
    LabColdRow* cold_con;
    u32* next_con; // indexed by construction position (B and D)
    u32* b_operands;
    u32* b_targets;
    u64* b_immediates;
    // C, published and construction order.
    LabCompactRow* compact;
    LabCompactRow* compact_con;
    LabExtension* extensions;
    u32* c_operands;
    u32 extension_count;
    u64 c_overflow_count;
    // D: construction-order chunks of the 64-byte row (the stable-identity
    // design keeps construction order forever, so that is the order measured).
    IrInstruction** chunks;
    u32 chunk_count;
};

typedef struct LabModule LabModule;
struct LabModule
{
    LabFunction* functions;
    u32 function_count;
    u64 row_total;
    u64 value_total;
    u64 block_total;
    u64 operand_total;
    u64 target_total;
    u64 immediate_total;
    u64 result_rows;
    u64 extension_rows;
    u64 operand_histogram[5]; // 0, 1, 2, 3..8, >8
    u64 rows_with_targets;
    u64 rows_with_immediates;
    u64 rows_with_symbol;
    u64 rows_with_local;
    u64 moved_rows;
    u64 def_distance_near; // |definition row - using row| <= 64 in published order
};

BUSTER_GLOBAL_LOCAL u64 lab_mix(u64 value)
{
    value ^= value >> 33;
    value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 33;
    return value;
}

BUSTER_GLOBAL_LOCAL bool lab_row_has_extension(IrInstruction const* row)
{
    return row->target_count || row->immediate_count || row->symbol.value != IR_ID_UNDERLYING_INVALID ||
           row->canonical_local.value != IR_ID_UNDERLYING_INVALID || row->memory_order != IR_MEMORY_ORDER_COUNT ||
           row->failure_memory_order != IR_MEMORY_ORDER_COUNT;
}

BUSTER_GLOBAL_LOCAL u8 lab_row_operation(IrInstruction const* row)
{
    u8 result = 0;
    switch (row->opcode)
    {
    case IR_OPCODE_CAST: result = row->conversion_operation; break;
    case IR_OPCODE_UNARY: result = row->unary_operation; break;
    case IR_OPCODE_BINARY: result = row->binary_operation; break;
    case IR_OPCODE_ATOMIC_READ_MODIFY_WRITE: result = row->atomic_operation; break;
    case IR_OPCODE_SIMD: result = row->simd_operation; break;
    default: break;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u8 lab_row_flags(IrInstruction const* row)
{
    return (u8)((row->volatile_access ? 1 : 0) | (row->immediate_is_negative ? 2 : 0) | (row->atomic_signal_fence ? 4 : 0));
}

BUSTER_GLOBAL_LOCAL void lab_build_function(Arena* arena, LabModule* module, LabFunction* lab_function, IrFunction* function)
{
    u32 count = function->instruction_count;
    IrPublishedCfg const* cfg = function->published_cfg;
    lab_function->function = function;
    lab_function->row_count = count;
    lab_function->value_count = function->value_count;
    lab_function->block_count = function->block_count;
    lab_function->con_of_pub = arena_allocate(arena, u32, count);
    lab_function->pub_of_con = arena_allocate(arena, u32, count);
    if (cfg->instruction_remap)
    {
        for (u32 pre = 0; pre < count; pre += 1)
        {
            u32 published = cfg->instruction_remap[pre].value;
            lab_function->con_of_pub[published] = pre;
            lab_function->pub_of_con[pre] = published;
            module->moved_rows += published != pre;
        }
    }
    else
    {
        for (u32 index = 0; index < count; index += 1)
        {
            lab_function->con_of_pub[index] = index;
            lab_function->pub_of_con[index] = index;
        }
    }
    u64 operand_total = 0;
    u64 target_total = 0;
    u64 immediate_total = 0;
    u64 overflow_total = 0;
    u32 extension_count = 0;
    for (u32 index = 0; index < count; index += 1)
    {
        IrInstruction const* row = function->instructions + index;
        operand_total += row->operand_count;
        target_total += row->target_count;
        immediate_total += row->immediate_count;
        overflow_total += row->operand_count > 2 ? row->operand_count : 0;
        extension_count += lab_row_has_extension(row);
        module->result_rows += row->result.value != IR_ID_UNDERLYING_INVALID;
        module->rows_with_targets += row->target_count != 0;
        module->rows_with_immediates += row->immediate_count != 0;
        module->rows_with_symbol += row->symbol.value != IR_ID_UNDERLYING_INVALID;
        module->rows_with_local += row->canonical_local.value != IR_ID_UNDERLYING_INVALID;
        module->operand_histogram[row->operand_count == 0 ? 0 : row->operand_count == 1 ? 1 : row->operand_count == 2 ? 2 : row->operand_count <= 8 ? 3 : 4] += 1;
        for (u32 operand = 0; operand < row->operand_count; operand += 1)
        {
            u32 definition = function->values[row->operands[operand].value].definition.value;
            module->def_distance_near += definition < count && (definition > index ? definition - index : index - definition) <= 64;
        }
    }
    lab_function->operand_total = operand_total;
    lab_function->c_overflow_count = overflow_total;
    lab_function->extension_count = extension_count;
    module->row_total += count;
    module->value_total += function->value_count;
    module->block_total += function->block_count;
    module->operand_total += operand_total;
    module->target_total += target_total;
    module->immediate_total += immediate_total;
    module->extension_rows += extension_count;

    // A_con: rows permuted back into construction order, chained by `next`.
    lab_function->rows_con = (IrInstruction*)arena_allocate_bytes(arena, arena_array_size(sizeof(IrInstruction), count), 64);
    lab_function->block_first_con = arena_allocate(arena, u32, function->block_count);
    lab_function->block_last_con = arena_allocate(arena, u32, function->block_count);
    lab_function->next_con = arena_allocate(arena, u32, count);
    for (u32 published = 0; published < count; published += 1)
    {
        u32 construction = lab_function->con_of_pub[published];
        lab_function->rows_con[construction] = function->instructions[published];
        lab_function->rows_con[construction].next = IR_INSTRUCTION_ID_INVALID;
        lab_function->next_con[construction] = LAB_INVALID;
    }
    for (u32 block = 0; block < function->block_count; block += 1)
    {
        IrCfgBlock const* span = cfg->blocks + block;
        u32 previous = LAB_INVALID;
        lab_function->block_first_con[block] = LAB_INVALID;
        lab_function->block_last_con[block] = LAB_INVALID;
        for (u32 offset = 0; offset < span->instruction_count; offset += 1)
        {
            u32 construction = lab_function->con_of_pub[span->first_instruction + offset];
            if (previous == LAB_INVALID)
            {
                lab_function->block_first_con[block] = construction;
            }
            else
            {
                lab_function->rows_con[previous].next.value = construction;
                lab_function->next_con[previous] = construction;
            }
            lab_function->block_last_con[block] = construction;
            previous = construction;
        }
    }

    // B: hot/cold with pools in published order.
    lab_function->hot = arena_allocate(arena, LabHotRow, count);
    lab_function->cold = arena_allocate(arena, LabColdRow, count);
    lab_function->hot_con = arena_allocate(arena, LabHotRow, count);
    lab_function->cold_con = arena_allocate(arena, LabColdRow, count);
    lab_function->b_operands = arena_allocate(arena, u32, operand_total + count);
    lab_function->b_targets = arena_allocate(arena, u32, target_total);
    lab_function->b_immediates = arena_allocate(arena, u64, immediate_total);
    // C: compact rows, extensions and overflow operands in published order.
    lab_function->compact = arena_allocate(arena, LabCompactRow, count);
    lab_function->compact_con = arena_allocate(arena, LabCompactRow, count);
    lab_function->extensions = arena_allocate(arena, LabExtension, extension_count ? extension_count : 1);
    lab_function->c_operands = arena_allocate(arena, u32, overflow_total ? overflow_total : 1);
    u64 b_operand_cursor = 0;
    u64 target_cursor = 0;
    u64 immediate_cursor = 0;
    u64 c_operand_cursor = 0;
    u32 extension_cursor = 0;
    for (u32 published = 0; published < count; published += 1)
    {
        IrInstruction const* row = function->instructions + published;
        LabHotRow hot = {.result = row->result.value, .canonical_type = row->canonical_type.value, .opcode = row->opcode, .flags = lab_row_flags(row)};
        if (row->operand_count < UINT16_MAX)
        {
            hot.operand_count = (u16)row->operand_count;
            hot.operand_offset = (u32)b_operand_cursor;
        }
        else
        {
            // Wide escape: the count precedes the operands in the pool.
            hot.operand_count = UINT16_MAX;
            hot.flags |= 8;
            hot.operand_offset = (u32)b_operand_cursor;
            lab_function->b_operands[b_operand_cursor++] = row->operand_count;
        }
        for (u32 operand = 0; operand < row->operand_count; operand += 1)
        {
            lab_function->b_operands[b_operand_cursor++] = row->operands[operand].value;
        }
        LabColdRow cold = {.target_offset = (u32)target_cursor, .immediate_offset = (u32)immediate_cursor, .symbol = row->symbol.value,
                           .canonical_local = row->canonical_local.value, .target_count = row->target_count, .immediate_count = row->immediate_count,
                           .conversion_operation = row->conversion_operation, .unary_operation = row->unary_operation,
                           .binary_operation = row->binary_operation, .memory_order = row->memory_order,
                           .failure_memory_order = row->failure_memory_order, .atomic_operation = row->atomic_operation,
                           .simd_operation = row->simd_operation};
        for (u32 target = 0; target < row->target_count; target += 1)
        {
            lab_function->b_targets[target_cursor++] = row->targets[target].value;
        }
        for (u32 immediate = 0; immediate < row->immediate_count; immediate += 1)
        {
            lab_function->b_immediates[immediate_cursor++] = row->immediates[immediate];
        }
        lab_function->hot[published] = hot;
        lab_function->cold[published] = cold;

        LabCompactRow compact = {.result = row->result.value, .canonical_type = row->canonical_type.value, .extension = LAB_INVALID,
                                 .next = LAB_INVALID, .opcode = row->opcode, .operation = lab_row_operation(row), .flags = lab_row_flags(row),
                                 .operand_count = (u16)BUSTER_MIN(row->operand_count, (u32)UINT16_MAX)};
        if (row->operand_count <= 2)
        {
            compact.a = row->operand_count > 0 ? row->operands[0].value : LAB_INVALID;
            compact.b = row->operand_count > 1 ? row->operands[1].value : LAB_INVALID;
        }
        else
        {
            compact.a = (u32)c_operand_cursor;
            compact.b = row->operand_count; // exact count beside the offset, so a wide row needs no escape
            for (u32 operand = 0; operand < row->operand_count; operand += 1)
            {
                lab_function->c_operands[c_operand_cursor++] = row->operands[operand].value;
            }
        }
        if (lab_row_has_extension(row))
        {
            compact.extension = extension_cursor;
            lab_function->extensions[extension_cursor++] = (LabExtension){.target_offset = cold.target_offset, .immediate_offset = cold.immediate_offset,
                                                                          .symbol = cold.symbol, .canonical_local = cold.canonical_local,
                                                                          .target_count = cold.target_count, .immediate_count = cold.immediate_count,
                                                                          .memory_order = cold.memory_order, .failure_memory_order = cold.failure_memory_order};
        }
        lab_function->compact[published] = compact;
    }
    for (u32 published = 0; published < count; published += 1)
    {
        u32 construction = lab_function->con_of_pub[published];
        lab_function->hot_con[construction] = lab_function->hot[published];
        lab_function->cold_con[construction] = lab_function->cold[published];
        lab_function->compact_con[construction] = lab_function->compact[published];
        lab_function->compact_con[construction].next = lab_function->next_con[construction];
    }

    // D: chunks in construction order (stable identity never reorders).
    lab_function->chunk_count = (count + LAB_CHUNK_ROWS - 1) >> LAB_CHUNK_SHIFT;
    lab_function->chunks = arena_allocate(arena, IrInstruction*, lab_function->chunk_count ? lab_function->chunk_count : 1);
    for (u32 chunk = 0; chunk < lab_function->chunk_count; chunk += 1)
    {
        lab_function->chunks[chunk] = (IrInstruction*)arena_allocate_bytes(arena, sizeof(IrInstruction) * LAB_CHUNK_ROWS, 64);
        for (u32 slot = 0; slot < LAB_CHUNK_ROWS; slot += 1)
        {
            u32 construction = (chunk << LAB_CHUNK_SHIFT) + slot;
            lab_function->chunks[chunk][slot] = construction < count ? lab_function->rows_con[construction] : (IrInstruction){0};
        }
    }
}

// ---------------------------------------------------------------------------
// Kernels. Each returns a checksum over the work it performed.

// census: opcode and result presence, every row.
LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_census_a(LabModule* module, bool construction_order)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        IrInstruction const* rows = construction_order ? lf->rows_con : lf->function->instructions;
        u32 count = lf->row_count;
        for (u32 i = 0; i < count; i += 1)
        {
            acc += (u64)rows[i].opcode * 0x9E37u + (rows[i].result.value != IR_ID_UNDERLYING_INVALID);
        }
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_census_b(LabModule* module)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        LabHotRow const* rows = lf->hot;
        u32 count = lf->row_count;
        for (u32 i = 0; i < count; i += 1)
        {
            acc += (u64)rows[i].opcode * 0x9E37u + (rows[i].result != IR_ID_UNDERLYING_INVALID);
        }
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_census_c(LabModule* module)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        LabCompactRow const* rows = lf->compact;
        u32 count = lf->row_count;
        for (u32 i = 0; i < count; i += 1)
        {
            acc += (u64)rows[i].opcode * 0x9E37u + (rows[i].result != IR_ID_UNDERLYING_INVALID);
        }
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_census_d(LabModule* module)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        u32 count = lf->row_count;
        for (u32 chunk = 0; chunk < lf->chunk_count; chunk += 1)
        {
            IrInstruction const* rows = lf->chunks[chunk];
            u32 base = chunk << LAB_CHUNK_SHIFT;
            u32 end = BUSTER_MIN(count - base, LAB_CHUNK_ROWS);
            for (u32 i = 0; i < end; i += 1)
            {
                acc += (u64)rows[i].opcode * 0x9E37u + (rows[i].result.value != IR_ID_UNDERLYING_INVALID);
            }
        }
    }
    return acc;
}

// uses: per-value use counts from every operand.
BUSTER_GLOBAL_LOCAL u64 lab_uses_checksum(u32 const* uses, u32 count)
{
    u64 acc = 0;
    for (u32 v = 0; v < count; v += 1)
    {
        acc += (u64)uses[v] * (v + 1);
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_uses_a(LabModule* module, u32* uses)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        IrInstruction const* rows = lf->function->instructions;
        u32 count = lf->row_count;
        memset(uses, 0, sizeof(*uses) * lf->value_count);
        for (u32 i = 0; i < count; i += 1)
        {
            IrInstruction const* row = rows + i;
            for (u32 k = 0; k < row->operand_count; k += 1)
            {
                uses[row->operands[k].value] += 1;
            }
        }
        acc += lab_uses_checksum(uses, lf->value_count);
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_uses_b(LabModule* module, u32* uses)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        LabHotRow const* rows = lf->hot;
        u32 const* pool = lf->b_operands;
        u32 count = lf->row_count;
        memset(uses, 0, sizeof(*uses) * lf->value_count);
        for (u32 i = 0; i < count; i += 1)
        {
            u32 operand_count = rows[i].operand_count;
            u32 const* operands = pool + rows[i].operand_offset;
            if (operand_count == UINT16_MAX)
            {
                operand_count = operands[0];
                operands += 1;
            }
            for (u32 k = 0; k < operand_count; k += 1)
            {
                uses[operands[k]] += 1;
            }
        }
        acc += lab_uses_checksum(uses, lf->value_count);
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_uses_c(LabModule* module, u32* uses)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        LabCompactRow const* rows = lf->compact;
        u32 const* pool = lf->c_operands;
        u32 count = lf->row_count;
        memset(uses, 0, sizeof(*uses) * lf->value_count);
        for (u32 i = 0; i < count; i += 1)
        {
            LabCompactRow const* row = rows + i;
            if (row->operand_count <= 2)
            {
                if (row->operand_count > 0) uses[row->a] += 1;
                if (row->operand_count > 1) uses[row->b] += 1;
            }
            else
            {
                u32 const* operands = pool + row->a;
                for (u32 k = 0; k < row->b; k += 1)
                {
                    uses[operands[k]] += 1;
                }
            }
        }
        acc += lab_uses_checksum(uses, lf->value_count);
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_uses_d(LabModule* module, u32* uses)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        u32 count = lf->row_count;
        memset(uses, 0, sizeof(*uses) * lf->value_count);
        for (u32 chunk = 0; chunk < lf->chunk_count; chunk += 1)
        {
            IrInstruction const* rows = lf->chunks[chunk];
            u32 base = chunk << LAB_CHUNK_SHIFT;
            u32 end = BUSTER_MIN(count - base, LAB_CHUNK_ROWS);
            for (u32 i = 0; i < end; i += 1)
            {
                for (u32 k = 0; k < rows[i].operand_count; k += 1)
                {
                    uses[rows[i].operands[k].value] += 1;
                }
            }
        }
        acc += lab_uses_checksum(uses, lf->value_count);
    }
    return acc;
}

// defs: for every operand, follow values[].definition to the defining row.
// `cold` reads the defining row's immediate_count instead of operand_count.
LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_defs_a(LabModule* module, bool cold)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        IrInstruction const* rows = lf->function->instructions;
        IrValue const* values = lf->function->values;
        u32 count = lf->row_count;
        for (u32 i = 0; i < count; i += 1)
        {
            IrInstruction const* row = rows + i;
            for (u32 k = 0; k < row->operand_count; k += 1)
            {
                u32 definition = values[row->operands[k].value].definition.value;
                if (definition < count)
                {
                    IrInstruction const* source = rows + definition;
                    acc += (u64)source->opcode + (cold ? source->immediate_count : source->operand_count);
                }
            }
        }
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_defs_b(LabModule* module, bool cold)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        LabHotRow const* rows = lf->hot;
        LabColdRow const* colds = lf->cold;
        IrValue const* values = lf->function->values;
        u32 const* pool = lf->b_operands;
        u32 count = lf->row_count;
        for (u32 i = 0; i < count; i += 1)
        {
            u32 operand_count = rows[i].operand_count;
            u32 const* operands = pool + rows[i].operand_offset;
            if (operand_count == UINT16_MAX)
            {
                operand_count = operands[0];
                operands += 1;
            }
            for (u32 k = 0; k < operand_count; k += 1)
            {
                u32 definition = values[operands[k]].definition.value;
                if (definition < count)
                {
                    u32 width = rows[definition].operand_count;
                    if (width == UINT16_MAX) width = pool[rows[definition].operand_offset];
                    acc += (u64)rows[definition].opcode + (cold ? colds[definition].immediate_count : width);
                }
            }
        }
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_defs_c(LabModule* module, bool cold)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        LabCompactRow const* rows = lf->compact;
        LabExtension const* extensions = lf->extensions;
        IrValue const* values = lf->function->values;
        u32 const* pool = lf->c_operands;
        u32 count = lf->row_count;
        for (u32 i = 0; i < count; i += 1)
        {
            LabCompactRow const* row = rows + i;
            u32 inline_count = row->operand_count <= 2 ? row->operand_count : 0;
            u32 const* operands = row->operand_count <= 2 ? &row->a : pool + row->a;
            u32 operand_count = row->operand_count <= 2 ? inline_count : row->b;
            for (u32 k = 0; k < operand_count; k += 1)
            {
                u32 definition = values[operands[k]].definition.value;
                if (definition < count)
                {
                    LabCompactRow const* source = rows + definition;
                    u32 width = source->operand_count <= 2 ? source->operand_count : source->b;
                    u32 immediates = source->extension != LAB_INVALID ? extensions[source->extension].immediate_count : 0;
                    acc += (u64)source->opcode + (cold ? immediates : width);
                }
            }
        }
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_defs_d(LabModule* module, bool cold)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        IrValue const* values = lf->function->values;
        IrInstruction* const* chunks = lf->chunks;
        u32 const* con_of_pub = lf->con_of_pub;
        u32 count = lf->row_count;
        for (u32 chunk = 0; chunk < lf->chunk_count; chunk += 1)
        {
            IrInstruction const* rows = chunks[chunk];
            u32 base = chunk << LAB_CHUNK_SHIFT;
            u32 end = BUSTER_MIN(count - base, LAB_CHUNK_ROWS);
            for (u32 i = 0; i < end; i += 1)
            {
                for (u32 k = 0; k < rows[i].operand_count; k += 1)
                {
                    u32 definition = values[rows[i].operands[k].value].definition.value;
                    if (definition < count)
                    {
                        // Definitions are published ids; D keeps construction
                        // positions, so the chase goes through the id map the
                        // stable design would carry.
                        u32 position = con_of_pub[definition];
                        IrInstruction const* source = chunks[position >> LAB_CHUNK_SHIFT] + (position & (LAB_CHUNK_ROWS - 1));
                        acc += (u64)source->opcode + (cold ? source->immediate_count : source->operand_count);
                    }
                }
            }
        }
    }
    return acc;
}

// chain: ownership walk through `next`, construction order.
LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_chain_a(LabModule* module, u32* owners)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        IrInstruction const* rows = lf->rows_con;
        for (u32 block = 0; block < lf->block_count; block += 1)
        {
            u32 id = lf->block_first_con[block];
            while (id != LAB_INVALID)
            {
                owners[id] = block;
                acc += id;
                id = rows[id].next.value;
            }
        }
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_chain_next_array(LabModule* module, u32* owners)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        u32 const* next = lf->next_con;
        for (u32 block = 0; block < lf->block_count; block += 1)
        {
            u32 id = lf->block_first_con[block];
            while (id != LAB_INVALID)
            {
                owners[id] = block;
                acc += id;
                id = next[id];
            }
        }
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_chain_c(LabModule* module, u32* owners)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        LabCompactRow const* rows = lf->compact_con;
        for (u32 block = 0; block < lf->block_count; block += 1)
        {
            u32 id = lf->block_first_con[block];
            while (id != LAB_INVALID)
            {
                owners[id] = block;
                acc += id;
                id = rows[id].next;
            }
        }
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_chain_d(LabModule* module, u32* owners)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        IrInstruction* const* chunks = lf->chunks;
        for (u32 block = 0; block < lf->block_count; block += 1)
        {
            u32 id = lf->block_first_con[block];
            while (id != LAB_INVALID)
            {
                owners[id] = block;
                acc += id;
                id = chunks[id >> LAB_CHUNK_SHIFT][id & (LAB_CHUNK_ROWS - 1)].next.value;
            }
        }
    }
    return acc;
}

// span: the published dense-span ownership walk (what publication buys).
LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_span(LabModule* module, u32* owners)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        IrPublishedCfg const* cfg = lf->function->published_cfg;
        u32 const* con_of_pub = lf->con_of_pub;
        for (u32 block = 0; block < lf->block_count; block += 1)
        {
            IrCfgBlock const* span = cfg->blocks + block;
            for (u32 offset = 0; offset < span->instruction_count; offset += 1)
            {
                u32 id = con_of_pub[span->first_instruction + offset];
                owners[id] = block;
                acc += id;
            }
        }
    }
    return acc;
}

// validate: the validator's per-row shape in chain order.
LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_validate_a(LabModule* module, u32 type_count)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        IrInstruction const* rows = lf->rows_con;
        IrValue const* values = lf->function->values;
        u32 const* pub_of_con = lf->pub_of_con;
        u32 value_count = lf->value_count;
        u32 block_count = lf->block_count;
        for (u32 block = 0; block < block_count; block += 1)
        {
            u32 id = lf->block_first_con[block];
            while (id != LAB_INVALID)
            {
                IrInstruction const* row = rows + id;
                bool ok = row->opcode < IR_OPCODE_COUNT && row->canonical_type.value < type_count &&
                          !(row->operand_count && !row->operands) && !(row->target_count && !row->targets) && !(row->immediate_count && !row->immediates);
                for (u32 k = 0; ok && k < row->operand_count; k += 1)
                {
                    ok = row->operands[k].value < value_count;
                }
                for (u32 k = 0; ok && k < row->target_count; k += 1)
                {
                    ok = row->targets[k].value < block_count;
                }
                if (ok && row->result.value != IR_ID_UNDERLYING_INVALID)
                {
                    ok = row->result.value < value_count && values[row->result.value].definition.value == pub_of_con[id] &&
                         values[row->result.value].canonical_type.value == row->canonical_type.value;
                }
                acc += ok;
                id = row->next.value;
            }
        }
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_validate_b(LabModule* module, u32 type_count)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        LabHotRow const* rows = lf->hot_con;
        LabColdRow const* colds = lf->cold_con;
        u32 const* next = lf->next_con;
        u32 const* operand_pool = lf->b_operands;
        u32 const* target_pool = lf->b_targets;
        IrValue const* values = lf->function->values;
        u32 const* pub_of_con = lf->pub_of_con;
        u32 value_count = lf->value_count;
        u32 block_count = lf->block_count;
        u64 operand_pool_count = lf->operand_total + lf->row_count;
        for (u32 block = 0; block < block_count; block += 1)
        {
            u32 id = lf->block_first_con[block];
            while (id != LAB_INVALID)
            {
                LabHotRow const* row = rows + id;
                LabColdRow const* cold = colds + id;
                u32 operand_count = row->operand_count;
                u32 const* operands = operand_pool + row->operand_offset;
                if (operand_count == UINT16_MAX)
                {
                    operand_count = operands[0];
                    operands += 1;
                }
                bool ok = row->opcode < IR_OPCODE_COUNT && row->canonical_type < type_count && row->operand_offset + operand_count <= operand_pool_count &&
                          cold->target_offset + cold->target_count <= lf->function->published_cfg->target_count &&
                          cold->immediate_offset + cold->immediate_count <= lf->function->published_cfg->immediate_count;
                for (u32 k = 0; ok && k < operand_count; k += 1)
                {
                    ok = operands[k] < value_count;
                }
                for (u32 k = 0; ok && k < cold->target_count; k += 1)
                {
                    ok = target_pool[cold->target_offset + k] < block_count;
                }
                if (ok && row->result != IR_ID_UNDERLYING_INVALID)
                {
                    ok = row->result < value_count && values[row->result].definition.value == pub_of_con[id] &&
                         values[row->result].canonical_type.value == row->canonical_type;
                }
                acc += ok;
                id = next[id];
            }
        }
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_validate_c(LabModule* module, u32 type_count)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        LabCompactRow const* rows = lf->compact_con;
        LabExtension const* extensions = lf->extensions;
        u32 const* operand_pool = lf->c_operands;
        u32 const* target_pool = lf->b_targets;
        IrValue const* values = lf->function->values;
        u32 const* pub_of_con = lf->pub_of_con;
        u32 value_count = lf->value_count;
        u32 block_count = lf->block_count;
        for (u32 block = 0; block < block_count; block += 1)
        {
            u32 id = lf->block_first_con[block];
            while (id != LAB_INVALID)
            {
                LabCompactRow const* row = rows + id;
                u32 const* operands = row->operand_count <= 2 ? &row->a : operand_pool + row->a;
                u32 operand_count = row->operand_count <= 2 ? row->operand_count : row->b;
                bool ok = row->opcode < IR_OPCODE_COUNT && row->canonical_type < type_count &&
                          (row->operand_count <= 2 || row->a + row->b <= lf->c_overflow_count);
                for (u32 k = 0; ok && k < operand_count; k += 1)
                {
                    ok = operands[k] < value_count;
                }
                if (ok && row->extension != LAB_INVALID)
                {
                    LabExtension const* extension = extensions + row->extension;
                    ok = extension->target_offset + extension->target_count <= lf->function->published_cfg->target_count &&
                         extension->immediate_offset + extension->immediate_count <= lf->function->published_cfg->immediate_count;
                    for (u32 k = 0; ok && k < extension->target_count; k += 1)
                    {
                        ok = target_pool[extension->target_offset + k] < block_count;
                    }
                }
                if (ok && row->result != IR_ID_UNDERLYING_INVALID)
                {
                    ok = row->result < value_count && values[row->result].definition.value == pub_of_con[id] &&
                         values[row->result].canonical_type.value == row->canonical_type;
                }
                acc += ok;
                id = row->next;
            }
        }
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_validate_d(LabModule* module, u32 type_count)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        IrInstruction* const* chunks = lf->chunks;
        IrValue const* values = lf->function->values;
        u32 const* pub_of_con = lf->pub_of_con;
        u32 value_count = lf->value_count;
        u32 block_count = lf->block_count;
        for (u32 block = 0; block < block_count; block += 1)
        {
            u32 id = lf->block_first_con[block];
            while (id != LAB_INVALID)
            {
                IrInstruction const* row = chunks[id >> LAB_CHUNK_SHIFT] + (id & (LAB_CHUNK_ROWS - 1));
                bool ok = row->opcode < IR_OPCODE_COUNT && row->canonical_type.value < type_count &&
                          !(row->operand_count && !row->operands) && !(row->target_count && !row->targets) && !(row->immediate_count && !row->immediates);
                for (u32 k = 0; ok && k < row->operand_count; k += 1)
                {
                    ok = row->operands[k].value < value_count;
                }
                for (u32 k = 0; ok && k < row->target_count; k += 1)
                {
                    ok = row->targets[k].value < block_count;
                }
                if (ok && row->result.value != IR_ID_UNDERLYING_INVALID)
                {
                    ok = row->result.value < value_count && values[row->result.value].definition.value == pub_of_con[id] &&
                         values[row->result.value].canonical_type.value == row->canonical_type.value;
                }
                acc += ok;
                id = row->next.value;
            }
        }
    }
    return acc;
}

// compact: remove the rows the mask selects, remap values and operands, and
// write the surviving rows densely into fresh storage. The mask marks rows in
// construction order, as ir_rewrite_compact's `removed` does.
typedef struct LabCompactScratch LabCompactScratch;
struct LabCompactScratch
{
    u8* removed;             // per construction position
    u32* instruction_map;    // construction position -> new row index
    u32* value_map;          // old value id -> new value id
    IrInstruction* rows_out; // A/D: 64-byte rows
    IrValueId* operands_out; // A/D operand pool
    IrValue* values_out;
    LabHotRow* hot_out;
    LabColdRow* cold_out;
    u32* pool_out;           // B operand pool / C overflow pool
    LabCompactRow* compact_out;
    LabExtension* extensions_out;
};

BUSTER_GLOBAL_LOCAL u32 lab_compact_maps(LabFunction* lf, LabCompactScratch* scratch, u32 f, u32* value_count_out)
{
    u32 count = lf->row_count;
    u32 kept = 0;
    memset(scratch->value_map, 0xff, sizeof(u32) * lf->value_count);
    for (u32 p = 0; p < count; p += 1)
    {
        IrInstruction const* row = lf->rows_con + p;
        bool candidate = row->result.value != IR_ID_UNDERLYING_INVALID && row->target_count == 0 &&
                         (row->opcode == IR_OPCODE_CAST || row->opcode == IR_OPCODE_CONSTANT_INTEGER || row->opcode == IR_OPCODE_BINARY ||
                          row->opcode == IR_OPCODE_FIELD || row->opcode == IR_OPCODE_ADDRESS_OF);
        bool removed = candidate && (lab_mix((u64)f << 32 | p) % 1000) < LAB_REMOVAL_PERMILLE;
        scratch->removed[p] = removed;
        scratch->instruction_map[p] = removed ? LAB_INVALID : kept++;
        if (!removed && row->result.value != IR_ID_UNDERLYING_INVALID)
        {
            scratch->value_map[row->result.value] = 0;
        }
    }
    // Block parameters keep their values; a removed row's result keeps its
    // old id so the operand remap below stays total (a real pass substitutes
    // a replacement; the work is the same lookup).
    for (u32 v = 0; v < lf->value_count; v += 1)
    {
        if (lf->function->values[v].definition.value == IR_ID_UNDERLYING_INVALID)
        {
            scratch->value_map[v] = 0;
        }
    }
    u32 value_count = 0;
    for (u32 v = 0; v < lf->value_count; v += 1)
    {
        scratch->value_map[v] = scratch->value_map[v] != LAB_INVALID ? value_count++ : v;
    }
    *value_count_out = value_count;
    return kept;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_compact_a(LabModule* module, LabCompactScratch* scratch)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        u32 value_count = 0;
        lab_compact_maps(lf, scratch, f, &value_count);
        u64 operand_cursor = 0;
        for (u32 p = 0; p < lf->row_count; p += 1)
        {
            if (!scratch->removed[p])
            {
                IrInstruction copy = lf->rows_con[p];
                IrValueId* old_operands = copy.operands;
                copy.operands = copy.operand_count ? scratch->operands_out + operand_cursor : 0;
                for (u32 k = 0; k < copy.operand_count; k += 1)
                {
                    copy.operands[k].value = scratch->value_map[old_operands[k].value];
                }
                operand_cursor += copy.operand_count;
                copy.result.value = copy.result.value != IR_ID_UNDERLYING_INVALID ? scratch->value_map[copy.result.value] : IR_ID_UNDERLYING_INVALID;
                copy.next.value = copy.next.value != IR_ID_UNDERLYING_INVALID ? scratch->instruction_map[copy.next.value] : IR_ID_UNDERLYING_INVALID;
                scratch->rows_out[scratch->instruction_map[p]] = copy;
                acc += (u64)copy.opcode + copy.result.value + (copy.operand_count ? copy.operands[0].value : 0);
            }
        }
        for (u32 v = 0; v < lf->value_count; v += 1)
        {
            u32 destination = scratch->value_map[v];
            if (destination < value_count)
            {
                IrValue copy = lf->function->values[v];
                copy.definition.value = copy.definition.value != IR_ID_UNDERLYING_INVALID ? scratch->instruction_map[lf->con_of_pub[copy.definition.value]] : IR_ID_UNDERLYING_INVALID;
                scratch->values_out[destination] = copy;
            }
        }
        acc += value_count;
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_compact_b(LabModule* module, LabCompactScratch* scratch)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        u32 value_count = 0;
        lab_compact_maps(lf, scratch, f, &value_count);
        u64 pool_cursor = 0;
        for (u32 p = 0; p < lf->row_count; p += 1)
        {
            if (!scratch->removed[p])
            {
                LabHotRow hot = lf->hot_con[p];
                u32 operand_count = hot.operand_count;
                u32 const* operands = lf->b_operands + hot.operand_offset;
                if (operand_count == UINT16_MAX)
                {
                    operand_count = operands[0];
                    operands += 1;
                    scratch->pool_out[pool_cursor++] = operand_count;
                }
                hot.operand_offset = (u32)pool_cursor;
                for (u32 k = 0; k < operand_count; k += 1)
                {
                    scratch->pool_out[pool_cursor + k] = scratch->value_map[operands[k]];
                }
                pool_cursor += operand_count;
                hot.result = hot.result != IR_ID_UNDERLYING_INVALID ? scratch->value_map[hot.result] : IR_ID_UNDERLYING_INVALID;
                u32 destination = scratch->instruction_map[p];
                scratch->hot_out[destination] = hot;
                scratch->cold_out[destination] = lf->cold_con[p];
                acc += (u64)hot.opcode + hot.result + (operand_count ? scratch->pool_out[hot.operand_offset + (hot.operand_count == UINT16_MAX)] : 0);
            }
        }
        for (u32 v = 0; v < lf->value_count; v += 1)
        {
            u32 destination = scratch->value_map[v];
            if (destination < value_count)
            {
                IrValue copy = lf->function->values[v];
                copy.definition.value = copy.definition.value != IR_ID_UNDERLYING_INVALID ? scratch->instruction_map[lf->con_of_pub[copy.definition.value]] : IR_ID_UNDERLYING_INVALID;
                scratch->values_out[destination] = copy;
            }
        }
        acc += value_count;
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_compact_c(LabModule* module, LabCompactScratch* scratch)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        u32 value_count = 0;
        lab_compact_maps(lf, scratch, f, &value_count);
        u64 pool_cursor = 0;
        u32 extension_cursor = 0;
        for (u32 p = 0; p < lf->row_count; p += 1)
        {
            if (!scratch->removed[p])
            {
                LabCompactRow row = lf->compact_con[p];
                u32 first = LAB_INVALID;
                if (row.operand_count <= 2)
                {
                    if (row.operand_count > 0) row.a = scratch->value_map[row.a];
                    if (row.operand_count > 1) row.b = scratch->value_map[row.b];
                    first = row.operand_count ? row.a : LAB_INVALID;
                }
                else
                {
                    u32 const* operands = lf->c_operands + row.a;
                    row.a = (u32)pool_cursor;
                    for (u32 k = 0; k < row.b; k += 1)
                    {
                        scratch->pool_out[pool_cursor + k] = scratch->value_map[operands[k]];
                    }
                    pool_cursor += row.b;
                    first = scratch->pool_out[row.a];
                }
                if (row.extension != LAB_INVALID)
                {
                    scratch->extensions_out[extension_cursor] = lf->extensions[row.extension];
                    row.extension = extension_cursor++;
                }
                row.result = row.result != IR_ID_UNDERLYING_INVALID ? scratch->value_map[row.result] : IR_ID_UNDERLYING_INVALID;
                row.next = row.next != LAB_INVALID ? scratch->instruction_map[row.next] : LAB_INVALID;
                scratch->compact_out[scratch->instruction_map[p]] = row;
                acc += (u64)row.opcode + row.result + (first != LAB_INVALID ? first : 0);
            }
        }
        for (u32 v = 0; v < lf->value_count; v += 1)
        {
            u32 destination = scratch->value_map[v];
            if (destination < value_count)
            {
                IrValue copy = lf->function->values[v];
                copy.definition.value = copy.definition.value != IR_ID_UNDERLYING_INVALID ? scratch->instruction_map[lf->con_of_pub[copy.definition.value]] : IR_ID_UNDERLYING_INVALID;
                scratch->values_out[destination] = copy;
            }
        }
        acc += value_count;
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_compact_d(LabModule* module, LabCompactScratch* scratch)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        u32 value_count = 0;
        lab_compact_maps(lf, scratch, f, &value_count);
        u64 operand_cursor = 0;
        IrInstruction* const* chunks = lf->chunks;
        for (u32 p = 0; p < lf->row_count; p += 1)
        {
            if (!scratch->removed[p])
            {
                IrInstruction copy = chunks[p >> LAB_CHUNK_SHIFT][p & (LAB_CHUNK_ROWS - 1)];
                IrValueId* old_operands = copy.operands;
                copy.operands = copy.operand_count ? scratch->operands_out + operand_cursor : 0;
                for (u32 k = 0; k < copy.operand_count; k += 1)
                {
                    copy.operands[k].value = scratch->value_map[old_operands[k].value];
                }
                operand_cursor += copy.operand_count;
                copy.result.value = copy.result.value != IR_ID_UNDERLYING_INVALID ? scratch->value_map[copy.result.value] : IR_ID_UNDERLYING_INVALID;
                copy.next.value = copy.next.value != IR_ID_UNDERLYING_INVALID ? scratch->instruction_map[copy.next.value] : IR_ID_UNDERLYING_INVALID;
                // Stable storage compacts into fresh chunks; the output array
                // stands in for them (same bytes written, same 64-byte rows).
                scratch->rows_out[scratch->instruction_map[p]] = copy;
                acc += (u64)copy.opcode + copy.result.value + (copy.operand_count ? copy.operands[0].value : 0);
            }
        }
        for (u32 v = 0; v < lf->value_count; v += 1)
        {
            u32 destination = scratch->value_map[v];
            if (destination < value_count)
            {
                IrValue copy = lf->function->values[v];
                copy.definition.value = copy.definition.value != IR_ID_UNDERLYING_INVALID ? scratch->instruction_map[lf->con_of_pub[copy.definition.value]] : IR_ID_UNDERLYING_INVALID;
                scratch->values_out[destination] = copy;
            }
        }
        acc += value_count;
    }
    return acc;
}

// append: rebuild every function from its construction-order trace.
//   a_reserved  exact-capacity 64-byte rows + per-row operand slices (the
//               production row-stream shape)
//   a_doubling  the ir.c growth path from 16 rows (hand-built/promotion shape)
//   b, c        reserved hot/cold or compact rows plus pools
//   d           chunks allocated on demand, no copies
LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_append_a(LabModule* module, Arena* arena, bool doubling)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        u32 count = lf->row_count;
        IrFunction fresh = {0};
        if (!doubling)
        {
            fresh.instruction_capacity = count;
            fresh.instructions = (IrInstruction*)arena_allocate_bytes(arena, arena_array_size(sizeof(IrInstruction), count), 64);
            fresh.instruction_canonical_sources = arena_allocate(arena, IrSourceRange, count);
        }
        fresh.opcode_summary = IR_OPCODE_SUMMARY_KNOWN;
        IrSourceRange source = {0};
        for (u32 p = 0; p < count; p += 1)
        {
            IrInstruction row = lf->rows_con[p];
            if (row.operand_count)
            {
                IrValueId* operands = arena_allocate(arena, IrValueId, row.operand_count);
                memcpy(operands, row.operands, sizeof(*operands) * row.operand_count);
                row.operands = operands;
            }
            source.offset = p;
            IrInstructionId id = doubling ? ir_function_add_instruction(arena, &fresh, row, source) : ir_instruction_append_trusted(arena, &fresh, row, source);
            acc += id.value;
        }
        acc += fresh.instruction_count;
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_append_b(LabModule* module, Arena* arena)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        u32 count = lf->row_count;
        LabHotRow* hot = arena_allocate(arena, LabHotRow, count);
        LabColdRow* cold = arena_allocate(arena, LabColdRow, count);
        u32* next = arena_allocate(arena, u32, count);
        u32* pool = arena_allocate(arena, u32, lf->operand_total + count);
        IrSourceRange* sources = arena_allocate(arena, IrSourceRange, count);
        u64 pool_cursor = 0;
        for (u32 p = 0; p < count; p += 1)
        {
            LabHotRow row = lf->hot_con[p];
            u32 operand_count = row.operand_count;
            u32 const* operands = lf->b_operands + row.operand_offset;
            if (operand_count == UINT16_MAX)
            {
                operand_count = operands[0];
                operands += 1;
                pool[pool_cursor++] = operand_count;
            }
            row.operand_offset = (u32)pool_cursor;
            memcpy(pool + pool_cursor, operands, sizeof(u32) * operand_count);
            pool_cursor += operand_count;
            hot[p] = row;
            cold[p] = lf->cold_con[p];
            next[p] = LAB_INVALID;
            sources[p] = (IrSourceRange){.offset = p};
            acc += p;
        }
        acc += count;
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_append_c(LabModule* module, Arena* arena)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        u32 count = lf->row_count;
        LabCompactRow* rows = arena_allocate(arena, LabCompactRow, count);
        LabExtension* extensions = arena_allocate(arena, LabExtension, lf->extension_count ? lf->extension_count : 1);
        u32* pool = arena_allocate(arena, u32, lf->c_overflow_count ? lf->c_overflow_count : 1);
        IrSourceRange* sources = arena_allocate(arena, IrSourceRange, count);
        u64 pool_cursor = 0;
        u32 extension_cursor = 0;
        for (u32 p = 0; p < count; p += 1)
        {
            LabCompactRow row = lf->compact_con[p];
            if (row.operand_count > 2)
            {
                memcpy(pool + pool_cursor, lf->c_operands + row.a, sizeof(u32) * row.b);
                row.a = (u32)pool_cursor;
                pool_cursor += row.b;
            }
            if (row.extension != LAB_INVALID)
            {
                extensions[extension_cursor] = lf->extensions[row.extension];
                row.extension = extension_cursor++;
            }
            rows[p] = row;
            sources[p] = (IrSourceRange){.offset = p};
            acc += p;
        }
        acc += count;
    }
    return acc;
}

LAB_NOINLINE BUSTER_GLOBAL_LOCAL u64 lab_kernel_append_d(LabModule* module, Arena* arena)
{
    u64 acc = 0;
    for (u32 f = 0; f < module->function_count; f += 1)
    {
        LabFunction* lf = module->functions + f;
        u32 count = lf->row_count;
        u32 chunk_capacity = 4;
        IrInstruction** chunks = arena_allocate(arena, IrInstruction*, chunk_capacity);
        IrSourceRange** source_chunks = arena_allocate(arena, IrSourceRange*, chunk_capacity);
        u32 chunk_count = 0;
        for (u32 p = 0; p < count; p += 1)
        {
            u32 chunk = p >> LAB_CHUNK_SHIFT;
            if (chunk == chunk_count)
            {
                if (chunk_count == chunk_capacity)
                {
                    // The directory itself doubles; the rows never move.
                    IrInstruction** grown = arena_allocate(arena, IrInstruction*, chunk_capacity * 2);
                    IrSourceRange** grown_sources = arena_allocate(arena, IrSourceRange*, chunk_capacity * 2);
                    memcpy(grown, chunks, sizeof(*chunks) * chunk_count);
                    memcpy(grown_sources, source_chunks, sizeof(*source_chunks) * chunk_count);
                    chunks = grown;
                    source_chunks = grown_sources;
                    chunk_capacity *= 2;
                }
                chunks[chunk_count] = (IrInstruction*)arena_allocate_bytes(arena, sizeof(IrInstruction) * LAB_CHUNK_ROWS, 64);
                source_chunks[chunk_count] = arena_allocate(arena, IrSourceRange, LAB_CHUNK_ROWS);
                chunk_count += 1;
            }
            IrInstruction row = lf->rows_con[p];
            if (row.operand_count)
            {
                IrValueId* operands = arena_allocate(arena, IrValueId, row.operand_count);
                memcpy(operands, row.operands, sizeof(*operands) * row.operand_count);
                row.operands = operands;
            }
            chunks[chunk][p & (LAB_CHUNK_ROWS - 1)] = row;
            source_chunks[chunk][p & (LAB_CHUNK_ROWS - 1)] = (IrSourceRange){.offset = p};
            acc += p;
        }
        acc += count;
    }
    return acc;
}

// ---------------------------------------------------------------------------
// Measurement harness.

typedef struct LabSample LabSample;
struct LabSample
{
    u64 ns[LAB_MAX_REPEAT];
    u64 checksum;
    u32 count;
};

BUSTER_GLOBAL_LOCAL void lab_sort(u64* values, u32 count)
{
    for (u32 index = 1; index < count; index += 1)
    {
        u64 value = values[index];
        u32 insertion = index;
        while (insertion && values[insertion - 1] > value)
        {
            values[insertion] = values[insertion - 1];
            insertion -= 1;
        }
        values[insertion] = value;
    }
}

BUSTER_GLOBAL_LOCAL void lab_report(String8 kernel, String8 layout, LabSample* sample, u64 units, String8 unit_name)
{
    u64 sorted[LAB_MAX_REPEAT];
    memcpy(sorted, sample->ns, sizeof(u64) * sample->count);
    lab_sort(sorted, sample->count);
    u64 median = sorted[sample->count / 2];
    u64 minimum = sorted[0];
    u64 ps_per_unit = units ? (median * 1000) / units : 0;
    string_print(S8("IR_LAYOUT_LAB_KERNEL kernel={S8} layout={S8} {S8}={u64} repeats={u32} min_ns={u64} median_ns={u64} max_ns={u64} ps_per_unit={u64} checksum={u64}\n"),
                 kernel, layout, unit_name, units, sample->count, minimum, median, sorted[sample->count - 1], ps_per_unit, sample->checksum);
}

#define LAB_TIME(sample, expression)                                                                                                  \
    do                                                                                                                                \
    {                                                                                                                                 \
        TimeDataType lab_start = timestamp_take();                                                                                    \
        u64 lab_checksum = (expression);                                                                                              \
        TimeDataType lab_end = timestamp_take();                                                                                      \
        (sample)->ns[(sample)->count++] = timestamp_ns_between(lab_start, lab_end);                                                  \
        (sample)->checksum = lab_checksum;                                                                                            \
    } while (0)

BUSTER_GLOBAL_LOCAL bool lab_checksums_agree(LabSample* samples, u32 count, String8 kernel)
{
    bool agree = true;
    for (u32 index = 1; index < count; index += 1)
    {
        agree &= samples[index].checksum == samples[0].checksum;
    }
    if (!agree)
    {
        string_print(S8("IR_LAYOUT_LAB_ERROR kernel={S8} checksums differ\n"), kernel);
    }
    return agree;
}

// Per-layout storage, computed exactly from the counts (bytes retained for
// the rows and pools; shared tables such as IrValue, sources and CFG are
// reported separately because they are common to every layout).
BUSTER_GLOBAL_LOCAL void lab_report_bytes(LabModule* module)
{
    u64 rows = module->row_total;
    u64 a_rows = rows * sizeof(IrInstruction);
    u64 a_pools = module->operand_total * sizeof(IrValueId) + module->target_total * sizeof(IrBlockId) + module->immediate_total * sizeof(u64);
    u64 b_rows = rows * (sizeof(LabHotRow) + sizeof(LabColdRow)) + rows * sizeof(u32);
    u64 b_pools = module->operand_total * sizeof(u32) + module->target_total * sizeof(u32) + module->immediate_total * sizeof(u64);
    u64 c_overflow = 0;
    for (u32 f = 0; f < module->function_count; f += 1) c_overflow += module->functions[f].c_overflow_count;
    u64 c_rows = rows * sizeof(LabCompactRow) + module->extension_rows * sizeof(LabExtension);
    u64 c_pools = c_overflow * sizeof(u32) + module->target_total * sizeof(u32) + module->immediate_total * sizeof(u64);
    u64 d_rows = 0;
    for (u32 f = 0; f < module->function_count; f += 1) d_rows += (u64)module->functions[f].chunk_count * LAB_CHUNK_ROWS * sizeof(IrInstruction) + module->functions[f].chunk_count * sizeof(void*);
    u64 shared = module->value_total * sizeof(IrValue) + rows * sizeof(IrSourceRange);
    string_print(S8("IR_LAYOUT_LAB_BYTES layout=A rows={u64} row_bytes={u64} pool_bytes={u64} shared_bytes={u64}\n"), rows, a_rows, a_pools, shared);
    string_print(S8("IR_LAYOUT_LAB_BYTES layout=B rows={u64} row_bytes={u64} pool_bytes={u64} shared_bytes={u64}\n"), rows, b_rows, b_pools, shared);
    string_print(S8("IR_LAYOUT_LAB_BYTES layout=C rows={u64} row_bytes={u64} pool_bytes={u64} shared_bytes={u64}\n"), rows, c_rows, c_pools, shared);
    string_print(S8("IR_LAYOUT_LAB_BYTES layout=D rows={u64} row_bytes={u64} pool_bytes={u64} shared_bytes={u64}\n"), rows, d_rows, a_pools, shared);
}

BUSTER_GLOBAL_LOCAL void lab_report_census(LabModule* module, IrProgram* program)
{
    string_print(S8("IR_LAYOUT_LAB_CENSUS functions={u32} rows={u64} values={u64} blocks={u64} operands={u64} targets={u64} immediates={u64} "
                    "result_rows={u64} rows_with_targets={u64} rows_with_immediates={u64} rows_with_symbol={u64} rows_with_local={u64} "
                    "extension_rows={u64} moved_rows={u64} def_distance_le64={u64} types={u32}\n"),
                 module->function_count, module->row_total, module->value_total, module->block_total, module->operand_total, module->target_total,
                 module->immediate_total, module->result_rows, module->rows_with_targets, module->rows_with_immediates, module->rows_with_symbol,
                 module->rows_with_local, module->extension_rows, module->moved_rows, module->def_distance_near, program->types.count);
    string_print(S8("IR_LAYOUT_LAB_OPERANDS zero={u64} one={u64} two={u64} three_to_eight={u64} more={u64}\n"), module->operand_histogram[0],
                 module->operand_histogram[1], module->operand_histogram[2], module->operand_histogram[3], module->operand_histogram[4]);
}

ProcessResult entry_point(void)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    Arena* source_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(256)});
    Arena* work_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_GB(32)});
    Arena* lab_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_GB(16)});
    Arena* scratch_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_GB(16)});
    CPreprocessResult preprocess = {0};
    if (source_arena && work_arena && lab_arena && scratch_arena)
    {
        ByteSlice bytes = file_read(source_arena, lab.source_path, (FileReadOptions){0});
        if (bytes.length)
        {
            c_prewarm();
            String8* system_paths = lab.system_include_paths;
            u32 system_count = lab.system_include_path_count;
#if BUSTER_LINUX
            system_paths[system_count++] = S8("/usr/local/include");
#if BUSTER_CPU_ARCH_X86_64
            system_paths[system_count++] = S8("/usr/include/x86_64-linux-gnu");
#else
            system_paths[system_count++] = S8("/usr/include/aarch64-linux-gnu");
#endif
            system_paths[system_count++] = S8("/usr/include");
#endif
            TimeDataType frontend_start = timestamp_take();
            preprocess = c_preprocess(work_arena, BYTE_SLICE_TO_STRING(8, bytes),
                                      (CPreprocessOptions){.definitions = lab.definitions, .definition_count = lab.definition_count,
                                                           .include_paths = lab.include_paths, .include_path_count = lab.include_path_count,
                                                           .system_include_paths = system_paths, .system_include_path_count = system_count,
                                                           .source_path = lab.source_path, .target = target_native,
                                                           .data_layout = target_data_layout(target_native), .expansion_limit = BUSTER_MB(64),
                                                           .include_depth_limit = 64});
            IrProgram* program = 0;
            bool certified = false;
            if (!preprocess.error_count)
            {
                CParserResult syntax = c_parse_ast(work_arena, preprocess);
                if (!syntax.diagnostic_count)
                {
                    CIRLowerResult lower = c_analyze_with_options(work_arena, lab.source_path, preprocess, syntax, target_native,
                                                                  (CIRLowerOptions){.disable_direct_ssa = lab.disable_direct_ssa, .omit_debug_locals = true});
                    if (!lower.diagnostic_count)
                    {
                        program = lower.program;
                        certified = lower.canonical_ir_certified;
                    }
                    else
                    {
                        string_print(S8("IR_LAYOUT_LAB_ERROR lowering diagnostics={u32} first={S8}\n"), lower.diagnostic_count, lower.diagnostics[0].message);
                    }
                }
                else
                {
                    string_print(S8("IR_LAYOUT_LAB_ERROR syntax diagnostics={u32}\n"), syntax.diagnostic_count);
                }
            }
            else
            {
                string_print(S8("IR_LAYOUT_LAB_ERROR preprocess errors={u64}\n"), preprocess.error_count);
            }
            TimeDataType frontend_end = timestamp_take();
            if (program && program->module_count)
            {
                IrModule* module = program->modules;
                program->fast_passes = IR_FAST_ALL;
                program->measure_fast_passes = true;
                string_print(S8("IR_LAYOUT_LAB_FRONTEND source={S8} bytes={u64} tokens={u64} frontend_ns={u64} certified={u32} functions={u32}\n"), lab.source_path,
                             bytes.length, preprocess.token_count, timestamp_ns_between(frontend_start, frontend_end), (u32)certified, module->function_count);
                // Production validator on the construction-order chains, before
                // any transform: the state validation #1 sees in a checked build.
                LabSample validate_pre = {0};
                for (u32 repeat = 0; repeat < lab.repeat; repeat += 1)
                {
                    LAB_TIME(&validate_pre, (u64)ir_validate_canonical_module(program, module).error);
                }
                TimeDataType prepare_start = timestamp_take();
                IrValidationResult prepared = ir_prepare_canonical_module(program, module, certified);
                TimeDataType prepare_end = timestamp_take();
                if (prepared.error == IR_VALIDATION_NONE)
                {
                    LabSample validate_post = {0};
                    for (u32 repeat = 0; repeat < lab.repeat; repeat += 1)
                    {
                        LAB_TIME(&validate_post, (u64)ir_validate_canonical_module(program, module).error);
                    }
                    IrFastStatistics fast = module->fast;
                    string_print(S8("IR_LAYOUT_LAB_PREPARE prepare_ns={u64} promoted_locals={u64} instructions_before={u64} instructions_after={u64} "
                                    "fast_fold_ns={u64} fast_address_ns={u64} fast_dce_ns={u64} fast_parameters_ns={u64} fast_compact_ns={u64} "
                                    "fast_changes={u64}\n"),
                                 timestamp_ns_between(prepare_start, prepare_end), module->local_promotion.promoted_locals, module->local_promotion.instructions_before,
                                 fast.instructions_after, fast.passes[IR_FAST_FOLD].nanoseconds, fast.passes[IR_FAST_ADDRESS].nanoseconds,
                                 fast.passes[IR_FAST_DCE].nanoseconds, fast.passes[IR_FAST_PARAMETERS].nanoseconds, fast.compact_nanoseconds,
                                 fast.passes[0].changes + fast.passes[1].changes + fast.passes[2].changes + fast.passes[3].changes);

                    // Build the lab layouts from the published module.
                    LabModule lab_module = {0};
                    lab_module.functions = arena_allocate(lab_arena, LabFunction, module->function_count);
                    u32 max_values = 0;
                    u32 max_rows = 0;
                    u64 max_operands = 0;
                    u64 max_extensions = 0;
                    for (u32 index = 0; index < module->function_count; index += 1)
                    {
                        IrFunction* function = module->functions + index;
                        if (function->state == IR_FUNCTION_LOWERED && function->published_cfg)
                        {
                            LabFunction* lf = lab_module.functions + lab_module.function_count++;
                            lab_build_function(lab_arena, &lab_module, lf, function);
                            max_values = BUSTER_MAX(max_values, lf->value_count);
                            max_rows = BUSTER_MAX(max_rows, lf->row_count);
                            max_operands = BUSTER_MAX(max_operands, lf->operand_total + lf->row_count);
                            max_extensions = BUSTER_MAX(max_extensions, (u64)lf->extension_count);
                        }
                    }
                    lab_report_census(&lab_module, program);
                    lab_report_bytes(&lab_module);
                    lab_report(S8("validate_production_pre"), S8("A_con"), &validate_pre, lab_module.row_total, S8("rows"));
                    lab_report(S8("validate_production_post"), S8("A_pub"), &validate_post, lab_module.row_total, S8("rows"));

                    u32* uses = arena_allocate(lab_arena, u32, max_values ? max_values : 1);
                    u32* owners = arena_allocate(lab_arena, u32, max_rows ? max_rows : 1);
                    LabCompactScratch scratch = {
                        .removed = arena_allocate(lab_arena, u8, max_rows ? max_rows : 1),
                        .instruction_map = arena_allocate(lab_arena, u32, max_rows ? max_rows : 1),
                        .value_map = arena_allocate(lab_arena, u32, max_values ? max_values : 1),
                        .rows_out = (IrInstruction*)arena_allocate_bytes(lab_arena, arena_array_size(sizeof(IrInstruction), max_rows ? max_rows : 1), 64),
                        .operands_out = arena_allocate(lab_arena, IrValueId, max_operands ? max_operands : 1),
                        .values_out = arena_allocate(lab_arena, IrValue, max_values ? max_values : 1),
                        .hot_out = arena_allocate(lab_arena, LabHotRow, max_rows ? max_rows : 1),
                        .cold_out = arena_allocate(lab_arena, LabColdRow, max_rows ? max_rows : 1),
                        .pool_out = arena_allocate(lab_arena, u32, max_operands ? max_operands : 1),
                        .compact_out = arena_allocate(lab_arena, LabCompactRow, max_rows ? max_rows : 1),
                        .extensions_out = arena_allocate(lab_arena, LabExtension, max_extensions ? max_extensions : 1),
                    };
                    u32 type_count = program->types.count;
                    bool valid = true;

                    // Alternate layouts within each repeat so drift lands on all of them.
                    LabSample census[5] = {0};
                    LabSample uses_samples[4] = {0};
                    LabSample defs_hot[4] = {0};
                    LabSample defs_cold[4] = {0};
                    LabSample chain[5] = {0};
                    LabSample validate[4] = {0};
                    LabSample compact[4] = {0};
                    LabSample append[5] = {0};
                    for (u32 repeat = 0; repeat < lab.repeat; repeat += 1)
                    {
                        bool reverse = repeat & 1;
                        for (u32 step = 0; step < 5; step += 1)
                        {
                            u32 layout = reverse ? 4 - step : step;
                            switch (layout)
                            {
                            case 0: LAB_TIME(&census[0], lab_kernel_census_a(&lab_module, false)); break;
                            case 1: LAB_TIME(&census[1], lab_kernel_census_a(&lab_module, true)); break;
                            case 2: LAB_TIME(&census[2], lab_kernel_census_b(&lab_module)); break;
                            case 3: LAB_TIME(&census[3], lab_kernel_census_c(&lab_module)); break;
                            default: LAB_TIME(&census[4], lab_kernel_census_d(&lab_module)); break;
                            }
                        }
                        for (u32 step = 0; step < 4; step += 1)
                        {
                            u32 layout = reverse ? 3 - step : step;
                            switch (layout)
                            {
                            case 0: LAB_TIME(&uses_samples[0], lab_kernel_uses_a(&lab_module, uses)); break;
                            case 1: LAB_TIME(&uses_samples[1], lab_kernel_uses_b(&lab_module, uses)); break;
                            case 2: LAB_TIME(&uses_samples[2], lab_kernel_uses_c(&lab_module, uses)); break;
                            default: LAB_TIME(&uses_samples[3], lab_kernel_uses_d(&lab_module, uses)); break;
                            }
                        }
                        for (u32 step = 0; step < 4; step += 1)
                        {
                            u32 layout = reverse ? 3 - step : step;
                            switch (layout)
                            {
                            case 0: LAB_TIME(&defs_hot[0], lab_kernel_defs_a(&lab_module, false)); break;
                            case 1: LAB_TIME(&defs_hot[1], lab_kernel_defs_b(&lab_module, false)); break;
                            case 2: LAB_TIME(&defs_hot[2], lab_kernel_defs_c(&lab_module, false)); break;
                            default: LAB_TIME(&defs_hot[3], lab_kernel_defs_d(&lab_module, false)); break;
                            }
                        }
                        for (u32 step = 0; step < 4; step += 1)
                        {
                            u32 layout = reverse ? 3 - step : step;
                            switch (layout)
                            {
                            case 0: LAB_TIME(&defs_cold[0], lab_kernel_defs_a(&lab_module, true)); break;
                            case 1: LAB_TIME(&defs_cold[1], lab_kernel_defs_b(&lab_module, true)); break;
                            case 2: LAB_TIME(&defs_cold[2], lab_kernel_defs_c(&lab_module, true)); break;
                            default: LAB_TIME(&defs_cold[3], lab_kernel_defs_d(&lab_module, true)); break;
                            }
                        }
                        for (u32 step = 0; step < 5; step += 1)
                        {
                            u32 layout = reverse ? 4 - step : step;
                            switch (layout)
                            {
                            case 0: LAB_TIME(&chain[0], lab_kernel_chain_a(&lab_module, owners)); break;
                            case 1: LAB_TIME(&chain[1], lab_kernel_chain_next_array(&lab_module, owners)); break;
                            case 2: LAB_TIME(&chain[2], lab_kernel_chain_c(&lab_module, owners)); break;
                            case 3: LAB_TIME(&chain[3], lab_kernel_chain_d(&lab_module, owners)); break;
                            default: LAB_TIME(&chain[4], lab_kernel_span(&lab_module, owners)); break;
                            }
                        }
                        for (u32 step = 0; step < 4; step += 1)
                        {
                            u32 layout = reverse ? 3 - step : step;
                            switch (layout)
                            {
                            case 0: LAB_TIME(&validate[0], lab_kernel_validate_a(&lab_module, type_count)); break;
                            case 1: LAB_TIME(&validate[1], lab_kernel_validate_b(&lab_module, type_count)); break;
                            case 2: LAB_TIME(&validate[2], lab_kernel_validate_c(&lab_module, type_count)); break;
                            default: LAB_TIME(&validate[3], lab_kernel_validate_d(&lab_module, type_count)); break;
                            }
                        }
                        for (u32 step = 0; step < 4; step += 1)
                        {
                            u32 layout = reverse ? 3 - step : step;
                            switch (layout)
                            {
                            case 0: LAB_TIME(&compact[0], lab_kernel_compact_a(&lab_module, &scratch)); break;
                            case 1: LAB_TIME(&compact[1], lab_kernel_compact_b(&lab_module, &scratch)); break;
                            case 2: LAB_TIME(&compact[2], lab_kernel_compact_c(&lab_module, &scratch)); break;
                            default: LAB_TIME(&compact[3], lab_kernel_compact_d(&lab_module, &scratch)); break;
                            }
                        }
                        for (u32 step = 0; step < 5; step += 1)
                        {
                            u32 layout = reverse ? 4 - step : step;
                            u64 position = scratch_arena->position;
                            switch (layout)
                            {
                            case 0: LAB_TIME(&append[0], lab_kernel_append_a(&lab_module, scratch_arena, false)); break;
                            case 1: LAB_TIME(&append[1], lab_kernel_append_a(&lab_module, scratch_arena, true)); break;
                            case 2: LAB_TIME(&append[2], lab_kernel_append_b(&lab_module, scratch_arena)); break;
                            case 3: LAB_TIME(&append[3], lab_kernel_append_c(&lab_module, scratch_arena)); break;
                            default: LAB_TIME(&append[4], lab_kernel_append_d(&lab_module, scratch_arena)); break;
                            }
                            // Rewind without decommitting, so every layout appends into warm pages.
                            arena_set_position(scratch_arena, position);
                        }
                    }
                    valid &= lab_checksums_agree(census, 5, S8("census"));
                    valid &= lab_checksums_agree(uses_samples, 4, S8("uses"));
                    valid &= lab_checksums_agree(defs_hot, 4, S8("defs_hot"));
                    valid &= lab_checksums_agree(defs_cold, 4, S8("defs_cold"));
                    valid &= lab_checksums_agree(chain, 5, S8("chain"));
                    valid &= lab_checksums_agree(validate, 4, S8("validate"));
                    valid &= lab_checksums_agree(compact, 4, S8("compact"));
                    u64 rows = lab_module.row_total;
                    u64 operands = lab_module.operand_total;
                    lab_report(S8("census"), S8("A_pub"), &census[0], rows, S8("rows"));
                    lab_report(S8("census"), S8("A_con"), &census[1], rows, S8("rows"));
                    lab_report(S8("census"), S8("B"), &census[2], rows, S8("rows"));
                    lab_report(S8("census"), S8("C"), &census[3], rows, S8("rows"));
                    lab_report(S8("census"), S8("D"), &census[4], rows, S8("rows"));
                    lab_report(S8("uses"), S8("A_pub"), &uses_samples[0], operands, S8("operands"));
                    lab_report(S8("uses"), S8("B"), &uses_samples[1], operands, S8("operands"));
                    lab_report(S8("uses"), S8("C"), &uses_samples[2], operands, S8("operands"));
                    lab_report(S8("uses"), S8("D"), &uses_samples[3], operands, S8("operands"));
                    lab_report(S8("defs_hot"), S8("A_pub"), &defs_hot[0], operands, S8("operands"));
                    lab_report(S8("defs_hot"), S8("B"), &defs_hot[1], operands, S8("operands"));
                    lab_report(S8("defs_hot"), S8("C"), &defs_hot[2], operands, S8("operands"));
                    lab_report(S8("defs_hot"), S8("D"), &defs_hot[3], operands, S8("operands"));
                    lab_report(S8("defs_cold"), S8("A_pub"), &defs_cold[0], operands, S8("operands"));
                    lab_report(S8("defs_cold"), S8("B"), &defs_cold[1], operands, S8("operands"));
                    lab_report(S8("defs_cold"), S8("C"), &defs_cold[2], operands, S8("operands"));
                    lab_report(S8("defs_cold"), S8("D"), &defs_cold[3], operands, S8("operands"));
                    lab_report(S8("chain"), S8("A_con"), &chain[0], rows, S8("rows"));
                    lab_report(S8("chain"), S8("B_next_array"), &chain[1], rows, S8("rows"));
                    lab_report(S8("chain"), S8("C_con"), &chain[2], rows, S8("rows"));
                    lab_report(S8("chain"), S8("D_con"), &chain[3], rows, S8("rows"));
                    lab_report(S8("chain"), S8("span_published"), &chain[4], rows, S8("rows"));
                    lab_report(S8("validate"), S8("A_con"), &validate[0], rows, S8("rows"));
                    lab_report(S8("validate"), S8("B_con"), &validate[1], rows, S8("rows"));
                    lab_report(S8("validate"), S8("C_con"), &validate[2], rows, S8("rows"));
                    lab_report(S8("validate"), S8("D_con"), &validate[3], rows, S8("rows"));
                    lab_report(S8("compact"), S8("A_con"), &compact[0], rows, S8("rows"));
                    lab_report(S8("compact"), S8("B_con"), &compact[1], rows, S8("rows"));
                    lab_report(S8("compact"), S8("C_con"), &compact[2], rows, S8("rows"));
                    lab_report(S8("compact"), S8("D_con"), &compact[3], rows, S8("rows"));
                    lab_report(S8("append"), S8("A_reserved"), &append[0], rows, S8("rows"));
                    lab_report(S8("append"), S8("A_doubling"), &append[1], rows, S8("rows"));
                    lab_report(S8("append"), S8("B_reserved"), &append[2], rows, S8("rows"));
                    lab_report(S8("append"), S8("C_reserved"), &append[3], rows, S8("rows"));
                    lab_report(S8("append"), S8("D_chunked"), &append[4], rows, S8("rows"));
                    string_print(S8("IR_LAYOUT_LAB_RESULT valid={u32} lab_arena_bytes={u64} work_arena_bytes={u64}\n"), (u32)valid, lab_arena->position, work_arena->position);
                    result = valid ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
                }
                else
                {
                    string_print(S8("IR_LAYOUT_LAB_ERROR prepare failed error={u32} function={u32} block={u32} instruction={u32}\n"), (u32)prepared.error,
                                 prepared.function.value, prepared.block.value, prepared.instruction.value);
                }
            }
        }
        else
        {
            string_print(S8("IR_LAYOUT_LAB_ERROR could not read {S8}\n"), lab.source_path);
        }
    }
    if (preprocess.recovery)
    {
        if (preprocess.recovery->spelling_arena) arena_destroy(preprocess.recovery->spelling_arena, 1);
        if (preprocess.recovery->token_arena) arena_destroy(preprocess.recovery->token_arena, 1);
        if (preprocess.recovery->token_shape_arena) arena_destroy(preprocess.recovery->token_shape_arena, 1);
    }
    if (scratch_arena) arena_destroy(scratch_arena, 1);
    if (lab_arena) arena_destroy(lab_arena, 1);
    if (work_arena) arena_destroy(work_arena, 1);
    if (source_arena) arena_destroy(source_arena, 1);
    return result;
}

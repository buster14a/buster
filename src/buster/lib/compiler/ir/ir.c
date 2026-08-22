// Canonical typed IR construction, ABI classification, and validation
// (model.h owns the record shapes, ir.h the API). The construction
// functions (ir_program_initialize, ir_program_add_*, ir_module_add_*,
// ir_function_add_*) are thin capacity-checked appends; the substance here
// is what sits between the frontend and the backends: source-map lookup
// and canonical source recovery for diagnostics, label-provenance
// propagation for computed goto (ir_label_provenance_*), the per-target
// ABI classification the frontend and codegen both consume
// (ir_system_v_abi_classes, ir_homogeneous_float_abi,
// ir_classify_abi_value, ir_prepare_program_abi), and the module validator
// (ir_validate_canonical_module) that every producer runs before machine
// selection or Wasm emission so a diagnosed frontend failure cannot leak a
// half-built function into codegen.

#include <buster/lib/compiler/ir/ir.h>

#include <buster/lib/file.h>
#include <buster/lib/simd.h>
#include <buster/lib/string.h>

IrType* ir_type_from_id(IrTypeTable* table, IrTypeId id)
{
    IrType* result;
    if (!table || id.value >= table->count)
    {
        result = 0;
    }
    else
    {
        result = table->types + id.value;
    }

    return result;
}

IrSymbol* ir_symbol_from_id(IrSymbolTable* table, IrSymbolId id)
{
    IrSymbol* result;
    if (!table || id.value >= table->count)
    {
        result = 0;
    }
    else
    {
        result = table->symbols + id.value;
    }

    return result;
}

IrSource* ir_source_from_id(IrSourceTable* table, IrSourceId id)
{
    IrSource* result;
    if (!table || id.value >= table->count)
    {
        result = 0;
    }
    else
    {
        result = table->sources + id.value;
    }

    return result;
}

// The two source-map searches, and the one shape they share: the greatest
// index in `[low, high)` whose key is <= the probe, over a non-decreasing key
// array whose first entry already satisfies that.  Both were bracketed by a
// page index long before this, so the span reaching here is short -- 1,43
// steps per region search and 2,04 per checkpoint search, measured over the
// self-host unit -- and the branch deciding each step is a coin flip no
// predictor can learn: the two loops held 5,07% of all stage-1 branch misses
// in the `2026-08-22T082003Z` survey.
//
// A 16-lane dword compare answers a span of up to 16 keys with no branch at
// all: every key <= the probe sets its mask bit, and because the array is
// non-decreasing those bits are the low run of the mask, so their population
// count is the distance from `low` to the answer.  A masked load supplies the
// short spans, so nothing is read past the end of the array.  The bisection
// stays for the tail above one vector, written branchlessly for the same
// reason -- as a mask off the sign bit of a subtraction, because Clang
// if-converts both `probe : low` and `half & -(u32)(a <= b)` straight back
// into a `ja` over a `mov`.  The subtraction is done in 64 bits so two u32
// operands cannot wrap into the sign.
#if BUSTER_SIMD_512
BUSTER_GLOBAL_LOCAL u32 ir_source_search_vector(void const* words, u32 low, u32 span, u32 stride_words, u32 probe)
{
    // `stride_words` is 1 for a dense u32 array and 2 for the region keys,
    // whose `start` is every other dword of an 8-byte record; the lane mask
    // drops the interleaved `source` dwords.
    u32 lanes = span * stride_words;
    Mask64 valid = mask64_prefix(lanes);
    Mask64 bytes_valid = mask64_prefix(lanes * 4);
    Simd512 loaded = simd512_load_masked(words, bytes_valid);
    // `simd512_less_word` is strict, so the covered lanes are the complement
    // of "probe < key" inside the valid lanes.
    Mask64 covered = valid & ~simd512_less_word(simd512_splat_word(probe), loaded);
    if (stride_words == 2)
    {
        covered &= (Mask64)0x5555;
    }
    u32 count = mask64_count(covered);
    return low + count - (count != 0);
}
#endif

BUSTER_GLOBAL_LOCAL u32 ir_source_search_offsets(u32 const* offsets, u32 low, u32 high, u32 probe)
{
    u32 span = high - low;
#if BUSTER_SIMD_512
    while (span > 16)
    {
        u32 half = span >> 1;
        u32 middle = low + half;
        low += half & (u32)(((s64)offsets[middle] - (s64)probe - 1) >> 63);
        span -= half;
    }
    if (span)
    {
        low = ir_source_search_vector(offsets + low, low, span, 1, probe);
    }
#else
    while (span > 1)
    {
        u32 half = span >> 1;
        u32 middle = low + half;
        low += half & (u32)(((s64)offsets[middle] - (s64)probe - 1) >> 63);
        span -= half;
    }
#endif
    return low;
}

BUSTER_GLOBAL_LOCAL u32 ir_source_search_keys(IrSourceRegionKey const* keys, u32 low, u32 high, u32 probe)
{
    u32 span = high - low;
#if BUSTER_SIMD_512
    while (span > 8)
    {
        u32 half = span >> 1;
        u32 middle = low + half;
        low += half & (u32)(((s64)keys[middle].start - (s64)probe - 1) >> 63);
        span -= half;
    }
    if (span)
    {
        low = ir_source_search_vector(keys + low, low, span, 2, probe);
    }
#else
    while (span > 1)
    {
        u32 half = span >> 1;
        u32 middle = low + half;
        low += half & (u32)(((s64)keys[middle].start - (s64)probe - 1) >> 63);
        span -= half;
    }
#endif
    return low;
}

// Greatest region whose start is <= offset; keys[0] starts at 0, so the
// search always lands. The page index brackets it to the regions starting
// within one page, so a miss costs one load and a couple of steps.
BUSTER_GLOBAL_LOCAL u32 ir_source_map_find(IrSourceMap const* map, u32 offset)
{
    IrSourceRegionKey const* keys = map->keys;
    u32 low = 0;
    u32 high = map->count;
    if (map->page_count)
    {
        u32 page = offset >> IR_SOURCE_MAP_PAGE_SHIFT;
        if (page >= map->page_count)
        {
            page = map->page_count - 1;
        }
        low = map->pages[page];
        if (page + 1 < map->page_count)
        {
            u32 bracket = map->pages[page + 1] + 1;
            high = bracket < high ? bracket : high;
        }
    }
    return ir_source_search_keys(keys, low, high, offset);
}

BUSTER_GLOBAL_LOCAL bool ir_source_map_key_contains(IrSourceRegionKey const* keys, u32 index, u32 offset)
{
    return keys[index].start <= offset && keys[index + 1].start > offset;
}

// The region an offset falls in, remembered in the cursor. Text and stamp
// regions get a slot each: a line's macro-expanded output sits at the tail of
// the space while the file bytes around it sit low, so one slot would miss on
// every interleave. The stamp slot is checked first: it is the narrower of
// the two, so a hit there is decisive, and asking the other way round
// measured `+1 M` on the self-host stage.
BUSTER_GLOBAL_LOCAL u32 ir_source_map_region(IrSourceMap const* map, u32 offset, IrSourceMapCursor* cursor)
{
    IrSourceRegionKey const* keys = map->keys;
    if (ir_source_map_key_contains(keys, cursor->stamp_region, offset))
    {
        return cursor->stamp_region;
    }
    if (ir_source_map_key_contains(keys, cursor->text_region, offset))
    {
        return cursor->text_region;
    }
    u32 index = ir_source_map_find(map, offset);
    if (map->regions[index].kind == IR_SOURCE_REGION_STAMP)
    {
        cursor->stamp_region = index;
    }
    else
    {
        cursor->text_region = index;
        cursor->checkpoint = 0;
    }
    return index;
}

BUSTER_GLOBAL_LOCAL IrSourcePosition ir_source_region_position(IrSourceRegion const* region, u32 offset, u32* checkpoint_cursor)
{
    if (region->kind == IR_SOURCE_REGION_STAMP)
    {
        return region->stamp;
    }
    if (!region->checkpoint_count)
    {
        return (IrSourcePosition){.source = region->source};
    }
    u32 count = region->checkpoint_count;
    u32 const* offsets = region->checkpoint_offsets;
    u32 local = offset - region->base;
    u32 cursor = checkpoint_cursor ? *checkpoint_cursor : 0;
    if (cursor >= count || offsets[cursor] > local || (cursor + 1 < count && offsets[cursor + 1] <= local))
    {
        // Searched, not stepped. A consumer emitting a line table asks about
        // one statement after another, which reads like a forward walk, but
        // the distance is a whole statement's worth of lines: advancing the
        // cursor up to 16 checkpoints before falling back to the search cost
        // 4 M steps to skip 88 k searches, `+44 M` on the self-host stage
        // (`+26 M` at a limit of 4). The search is already the right shape.
        u32 low = 0;
        u32 high = count;
        if (region->checkpoint_page_count)
        {
            u32 page = local >> IR_SOURCE_CHECKPOINT_PAGE_SHIFT;
            if (page >= region->checkpoint_page_count)
            {
                page = region->checkpoint_page_count - 1;
            }
            low = region->checkpoint_pages[page];
            if (page + 1 < region->checkpoint_page_count)
            {
                u32 bracket = region->checkpoint_pages[page + 1] + 1;
                high = bracket < high ? bracket : high;
            }
        }
        cursor = ir_source_search_offsets(offsets, low, high, local);
        if (checkpoint_cursor)
        {
            *checkpoint_cursor = cursor;
        }
    }
    IrSourceCheckpoint checkpoint = region->checkpoints[cursor];
    u32 delta = local - offsets[cursor];
    s64 line = (s64)checkpoint.line + region->line_delta;
    if (line < 1)
    {
        line = 1;
    }
    if (line > (s64)UINT32_MAX)
    {
        line = (s64)UINT32_MAX;
    }
    return (IrSourcePosition){
        .source = region->source,
        .offset = checkpoint.offset + delta,
        .line = (u32)line,
        .column = checkpoint.column + delta,
    };
}

// The source alone, for producers that only need to name the file a range
// belongs to. It is the region search without the per-line checkpoint search
// that a full position runs after it — the difference between the two is paid
// once per lowered instruction, so the short answer has its own entry point,
// and the key it reads carries the source beside the start it matched on.
u32 ir_source_map_source(IrSourceMap const* map, u32 offset, IrSourceMapCursor* cursor)
{
    u32 result;
    if (!map || !map->keys)
    {
        result = 0;
    }
    else
    {
        IrSourceMapCursor local_cursor = IR_SOURCE_MAP_CURSOR_EMPTY;
        result = map->keys[ir_source_map_region(map, offset, cursor ? cursor : &local_cursor)].source;
    }

    return result;
}

IrSourcePosition ir_source_map_position(IrSourceMap const* map, u32 offset, IrSourceMapCursor* cursor)
{
    if (!map || !map->keys)
    {
        return (IrSourcePosition){0};
    }
    if (!cursor)
    {
        // No cursor to amortize through and no memo to keep: search outright.
        u32 index = ir_source_map_find(map, offset);
        return ir_source_region_position(map->regions + index, offset, 0);
    }
    if (offset == cursor->memo_offset)
    {
        return cursor->memo_position;
    }
    u32 index = ir_source_map_region(map, offset, cursor);
    IrSourcePosition position = ir_source_region_position(map->regions + index, offset, &cursor->checkpoint);
    cursor->memo_offset = offset;
    cursor->memo_position = position;
    return position;
}

IrSourcePosition ir_source_text_position(String8 text, u32 source, u32 offset, IrSourceMapCursor* cursor)
{
    IrSourcePosition result;
    if (!text.pointer || offset > text.length)
    {
        result = (IrSourcePosition){.source = source};
    }
    else
    {
        IrSourceMapCursor local_cursor = IR_SOURCE_MAP_CURSOR_EMPTY;
        if (!cursor)
        {
            cursor = &local_cursor;
        }
        // The cursor's memo doubles as the scan's resume point: `memo_position`
        // is the line and line start reached at `memo_offset`, so an ascending
        // walk over one source counts every byte once across all its queries. A
        // zero line means no position was ever recorded, which is also what an
        // all-zero cursor reads as, so an uninitialized one simply rescans.
        u32 scanned = 0;
        u32 line = 1;
        u32 line_start = 0;
        if (cursor->memo_position.line && cursor->memo_position.source == source && cursor->memo_offset <= offset)
        {
            scanned = cursor->memo_offset;
            line = cursor->memo_position.line;
            line_start = cursor->memo_offset + 1 - cursor->memo_position.column;
        }
        for (; scanned < offset; scanned += 1)
        {
            if (text.pointer[scanned] == '\n')
            {
                line += 1;
                line_start = scanned + 1;
            }
        }
        IrSourcePosition position = {
            .source = source,
            .offset = offset,
            .line = line,
            .column = offset - line_start + 1,
        };
        cursor->memo_offset = offset;
        cursor->memo_position = position;
        result = position;
    }

    return result;
}



BUSTER_GLOBAL_LOCAL u32 ir_canonical_inline_assembly_type_class(IrType* type)
{
    if (type && type->layout.resolved && type->layout.size && type->layout.size <= 8)
    {
        switch (type->kind)
        {
        case IR_TYPE_BOOLEAN:
        case IR_TYPE_INTEGER:
        case IR_TYPE_ENUM:
            return IR_INLINE_ASSEMBLY_OPERAND_CLASS_INTEGER;
        case IR_TYPE_POINTER:
            return IR_INLINE_ASSEMBLY_OPERAND_CLASS_POINTER;
        case IR_TYPE_VOID:
        case IR_TYPE_FLOAT:
        case IR_TYPE_VA_LIST:
        case IR_TYPE_SLICE:
        case IR_TYPE_ARRAY:
        case IR_TYPE_VECTOR:
        case IR_TYPE_FUNCTION:
        case IR_TYPE_RANGE:
        case IR_TYPE_STRUCT:
        case IR_TYPE_UNION:
        case IR_TYPE_COUNT:
            break;
        }
    }

    return IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID;
}

IrSimdShape ir_simd_operation_shape(IrSimdOperation operation)
{
    switch (operation)
    {
    case IR_SIMD_LOAD:
        return (IrSimdShape){.operand_count = 1, .has_result = true};
    case IR_SIMD_LOAD_MASKED:
        return (IrSimdShape){.operand_count = 2, .has_result = true};
    case IR_SIMD_STORE:
        return (IrSimdShape){.operand_count = 2};
    case IR_SIMD_STORE_MASKED:
        return (IrSimdShape){.operand_count = 3};
    case IR_SIMD_SPLAT_BYTE:
    case IR_SIMD_SPLAT_WORD:
        return (IrSimdShape){.operand_count = 1, .has_result = true};
    case IR_SIMD_COMPARE_EQUAL_BYTE:
    case IR_SIMD_COMPARE_LESS_BYTE:
    case IR_SIMD_TEST_MASK_BYTE:
    case IR_SIMD_COMPARE_EQUAL_WORD:
    case IR_SIMD_COMPARE_LESS_WORD:
        return (IrSimdShape){.operand_count = 2, .has_result = true};
    case IR_SIMD_SIGN_MASK_BYTE:
        return (IrSimdShape){.operand_count = 1, .has_result = true};
    case IR_SIMD_PERMUTE2_BYTE:
        return (IrSimdShape){.operand_count = 4, .has_result = true};
    case IR_SIMD_COMPRESS_BYTE:
        return (IrSimdShape){.operand_count = 2, .has_result = true};
    case IR_SIMD_COMPRESS_STORE_BYTE:
        return (IrSimdShape){.operand_count = 3};
    case IR_SIMD_WIDEN_BYTE_TO_WORD:
    case IR_SIMD_SHIFT_LEFT_WORD:
        return (IrSimdShape){.operand_count = 1, .immediate_count = 1, .has_result = true};
    case IR_SIMD_TERNARY_WORD:
        return (IrSimdShape){.operand_count = 3, .immediate_count = 1, .has_result = true};
    case IR_SIMD_COUNT:
        break;
    }
    return (IrSimdShape){0};
}

String8 ir_simd_operation_name(IrSimdOperation operation)
{
    switch (operation)
    {
    case IR_SIMD_LOAD:
        return S8("simd.load");
    case IR_SIMD_LOAD_MASKED:
        return S8("simd.load_masked");
    case IR_SIMD_STORE:
        return S8("simd.store");
    case IR_SIMD_STORE_MASKED:
        return S8("simd.store_masked");
    case IR_SIMD_SPLAT_BYTE:
        return S8("simd.splat_byte");
    case IR_SIMD_COMPARE_EQUAL_BYTE:
        return S8("simd.compare_equal_byte");
    case IR_SIMD_COMPARE_LESS_BYTE:
        return S8("simd.compare_less_byte");
    case IR_SIMD_SIGN_MASK_BYTE:
        return S8("simd.sign_mask_byte");
    case IR_SIMD_TEST_MASK_BYTE:
        return S8("simd.test_mask_byte");
    case IR_SIMD_PERMUTE2_BYTE:
        return S8("simd.permute2_byte");
    case IR_SIMD_COMPRESS_BYTE:
        return S8("simd.compress_byte");
    case IR_SIMD_COMPRESS_STORE_BYTE:
        return S8("simd.compress_store_byte");
    case IR_SIMD_WIDEN_BYTE_TO_WORD:
        return S8("simd.widen_byte_to_word");
    case IR_SIMD_SHIFT_LEFT_WORD:
        return S8("simd.shift_left_word");
    case IR_SIMD_TERNARY_WORD:
        return S8("simd.ternary_word");
    case IR_SIMD_COMPARE_EQUAL_WORD:
        return S8("simd.compare_equal_word");
    case IR_SIMD_SPLAT_WORD:
        return S8("simd.splat_word");
    case IR_SIMD_COMPARE_LESS_WORD:
        return S8("simd.compare_less_word");
    case IR_SIMD_COUNT:
        break;
    }
    return S8("simd.invalid");
}

BUSTER_GLOBAL_LOCAL bool ir_inline_assembly_constraint_shape_valid(u64 constraint, u32 operand_index, u32 operand_count, u32* match_index_out)
{
    if ((constraint & ~IR_INLINE_ASSEMBLY_CONSTRAINT_KNOWN_MASK) || (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) >= IR_INLINE_ASSEMBLY_CONSTRAINT_COUNT)
    {
        return false;
    }
    bool output = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) != 0;
    bool read_write = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) != 0;
    bool matching = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0;
    u64 match_bits = constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX_MASK;
    if ((read_write && !output) || (matching && (output || read_write)))
    {
        return false;
    }
    if (!matching)
    {
        return match_bits == 0;
    }
    u32 match_index = IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(constraint);
    if (match_index >= operand_index || match_index >= operand_count)
    {
        return false;
    }
    if (match_index_out)
    {
        *match_index_out = match_index;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_canonical_inline_assembly_types_compatible(IrType* output, IrType* input)
{
    u32 output_class = ir_canonical_inline_assembly_type_class(output);
    u32 input_class = ir_canonical_inline_assembly_type_class(input);
    return output_class != IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID && output_class == input_class && output->layout.size == input->layout.size;
}

// The three operand shapes the 512-bit vocabulary speaks in. A vector is any
// 64-byte IR vector — the element type carries the lane width the frontend
// chose and never reaches the encoding — a mask is a u64, and an address is
// any pointer.
BUSTER_GLOBAL_LOCAL bool ir_simd_type_is_vector(IrProgram* program, IrTypeId id)
{
    IrType* type = ir_type_from_id(&program->types, id);
    return type && type->kind == IR_TYPE_VECTOR && type->layout.resolved && type->layout.size == 64;
}

BUSTER_GLOBAL_LOCAL bool ir_simd_type_is_mask(IrProgram* program, IrTypeId id)
{
    IrType* type = ir_type_from_id(&program->types, id);
    return type && type->kind == IR_TYPE_INTEGER && type->bit_width == 64;
}

BUSTER_GLOBAL_LOCAL bool ir_simd_type_is_byte(IrProgram* program, IrTypeId id)
{
    IrType* type = ir_type_from_id(&program->types, id);
    return type && type->kind == IR_TYPE_INTEGER && type->bit_width == 8;
}

BUSTER_GLOBAL_LOCAL bool ir_simd_type_is_word(IrProgram* program, IrTypeId id)
{
    IrType* type = ir_type_from_id(&program->types, id);
    return type && type->kind == IR_TYPE_INTEGER && type->bit_width == 32;
}

BUSTER_GLOBAL_LOCAL bool ir_simd_type_is_address(IrProgram* program, IrTypeId id)
{
    IrType* type = ir_type_from_id(&program->types, id);
    return type && type->kind == IR_TYPE_POINTER;
}

BUSTER_GLOBAL_LOCAL bool ir_canonical_simd_valid(IrProgram* program, IrFunction* function, IrInstruction* instruction)
{
    IrSimdOperation operation = (IrSimdOperation)instruction->simd_operation;
    IrSimdShape shape = ir_simd_operation_shape(operation);
    if (operation < IR_SIMD_COUNT && instruction->operand_count == shape.operand_count && instruction->target_count == 0 &&
        instruction->immediate_count == shape.immediate_count && shape.has_result == (instruction->result.value != IR_ID_UNDERLYING_INVALID))
    {
        // Four is the widest shape in the table (vpermt2b's mask plus three
        // vectors); a fifth would have to widen this and every switch arm below.
        IrTypeId operands[4] = {0};
        if (instruction->operand_count > BUSTER_ARRAY_LENGTH(operands))
        {
            return false;
        }
        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
        {
            IrValueId operand = instruction->operands[operand_index];
            if (operand.value >= function->value_count || function->values[operand.value].category != IR_VALUE_VALUE)
            {
                return false;
            }
            operands[operand_index] = function->values[operand.value].canonical_type;
        }
        IrTypeId result_type = instruction->canonical_type;
        IrType* result = ir_type_from_id(&program->types, result_type);
        if (!result || (shape.has_result && function->values[instruction->result.value].canonical_type.value != result_type.value))
        {
            return false;
        }
        u64 immediate = instruction->immediate_count ? instruction->immediates[0] : 0;
        switch (operation)
        {
        case IR_SIMD_LOAD:
            return ir_simd_type_is_address(program, operands[0]) && ir_simd_type_is_vector(program, result_type);
        case IR_SIMD_LOAD_MASKED:
            return ir_simd_type_is_address(program, operands[0]) && ir_simd_type_is_mask(program, operands[1]) && ir_simd_type_is_vector(program, result_type);
        case IR_SIMD_STORE:
            return ir_simd_type_is_address(program, operands[0]) && ir_simd_type_is_vector(program, operands[1]) && result->kind == IR_TYPE_VOID;
        case IR_SIMD_STORE_MASKED:
        case IR_SIMD_COMPRESS_STORE_BYTE:
            return ir_simd_type_is_address(program, operands[0]) && ir_simd_type_is_mask(program, operands[1]) &&
                   ir_simd_type_is_vector(program, operands[2]) && result->kind == IR_TYPE_VOID;
        case IR_SIMD_SPLAT_BYTE:
            return ir_simd_type_is_byte(program, operands[0]) && ir_simd_type_is_vector(program, result_type);
        case IR_SIMD_SPLAT_WORD:
            return ir_simd_type_is_word(program, operands[0]) && ir_simd_type_is_vector(program, result_type);
        case IR_SIMD_COMPARE_EQUAL_BYTE:
        case IR_SIMD_COMPARE_LESS_BYTE:
        case IR_SIMD_TEST_MASK_BYTE:
        case IR_SIMD_COMPARE_EQUAL_WORD:
        case IR_SIMD_COMPARE_LESS_WORD:
            return ir_simd_type_is_vector(program, operands[0]) && ir_simd_type_is_vector(program, operands[1]) && ir_simd_type_is_mask(program, result_type);
        case IR_SIMD_SIGN_MASK_BYTE:
            return ir_simd_type_is_vector(program, operands[0]) && ir_simd_type_is_mask(program, result_type);
        case IR_SIMD_PERMUTE2_BYTE:
            return ir_simd_type_is_mask(program, operands[0]) && ir_simd_type_is_vector(program, operands[1]) &&
                   ir_simd_type_is_vector(program, operands[2]) && ir_simd_type_is_vector(program, operands[3]) && ir_simd_type_is_vector(program, result_type);
        case IR_SIMD_COMPRESS_BYTE:
            return ir_simd_type_is_mask(program, operands[0]) && ir_simd_type_is_vector(program, operands[1]) && ir_simd_type_is_vector(program, result_type);
        case IR_SIMD_WIDEN_BYTE_TO_WORD:
            return ir_simd_type_is_vector(program, operands[0]) && ir_simd_type_is_vector(program, result_type) && immediate < 4;
        case IR_SIMD_SHIFT_LEFT_WORD:
            return ir_simd_type_is_vector(program, operands[0]) && ir_simd_type_is_vector(program, result_type) && immediate < 32;
        case IR_SIMD_TERNARY_WORD:
            return ir_simd_type_is_vector(program, operands[0]) && ir_simd_type_is_vector(program, operands[1]) &&
                   ir_simd_type_is_vector(program, operands[2]) && ir_simd_type_is_vector(program, result_type) && immediate < 256;
        case IR_SIMD_COUNT:
            break;
        }
    }

    return false;
}

BUSTER_GLOBAL_LOCAL bool ir_canonical_inline_assembly_valid(IrProgram* program, IrFunction* function, IrInstruction* instruction)
{
    IrInstructionExtra extra = ir_instruction_extra(function, ir_instruction_self_id(function, instruction));
    bool valid = instruction->canonical_type.value < program->types.count && ir_type_from_id(&program->types, instruction->canonical_type)->kind == IR_TYPE_VOID &&
                 instruction->operand_count == instruction->immediate_count && (instruction->target_count == 0 || instruction->target_count >= 2) &&
                 extra.label_name_count == (instruction->target_count ? (u32)(instruction->target_count - 1) : 0) &&
                 (!extra.label_name_count || extra.label_names) && extra.operand_name_count == instruction->operand_count &&
                 (!extra.operand_name_count || extra.operand_names) && (!extra.clobber_count || extra.clobbers) &&
                 instruction->result.value == IR_ID_UNDERLYING_INVALID;
    for (u32 target_index = 0; valid && target_index < instruction->target_count; target_index += 1)
    {
        valid = instruction->targets && instruction->targets[target_index].value < function->block_count;
    }
    for (u32 operand_index = 0; valid && operand_index < instruction->operand_count; operand_index += 1)
    {
        IrValueId operand_id = instruction->operands ? instruction->operands[operand_index] : IR_VALUE_ID_INVALID;
        valid = operand_id.value < function->value_count;
        if (!valid)
        {
            break;
        }
        u64 constraint = instruction->immediates ? instruction->immediates[operand_index] : UINT64_MAX;
        bool output = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) != 0;
        valid = ir_inline_assembly_constraint_shape_valid(constraint, operand_index, instruction->operand_count, 0) &&
                function->values[operand_id.value].category == (output ? IR_VALUE_PLACE : IR_VALUE_VALUE);
        if (!valid)
        {
            break;
        }
        IrType* operand_type = ir_type_from_id(&program->types, function->values[operand_id.value].canonical_type);
        if ((constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0)
        {
            valid = ir_canonical_inline_assembly_type_class(operand_type) != IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID;
            if (!valid)
            {
                break;
            }
            u32 match_index = IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(constraint);
            IrValueId output_id = instruction->operands[match_index];
            u64 output_constraint = instruction->immediates[match_index];
            IrType* output_type = ir_type_from_id(&program->types, function->values[output_id.value].canonical_type);
            valid &= (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) != 0 &&
                     (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) == 0 &&
                     (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) &&
                     ir_canonical_inline_assembly_types_compatible(output_type, operand_type);
            for (u32 previous_index = 0; valid && previous_index < operand_index; previous_index += 1)
            {
                u64 previous_constraint = instruction->immediates[previous_index];
                valid &= !(previous_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) ||
                         IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(previous_constraint) != match_index;
            }
        }
    }
    return valid;
}

IrValueLabelMetadata* ir_value_label_metadata_find(IrFunction* function, IrValueId value)
{
    if (function && function->label_metadata_count)
    {
        u32 low = 0;
        u32 high = function->label_metadata_count;
        while (low < high)
        {
            u32 middle = low + (high - low) / 2;
            if (function->label_metadata_values[middle].value < value.value)
            {
                low = middle + 1;
            }
            else
            {
                high = middle;
            }
        }
        if (low < function->label_metadata_count && function->label_metadata_values[low].value == value.value)
        {
            return function->label_metadata + low;
        }
    }

    return 0;
}

IrValueLabelMetadata ir_value_label_metadata(IrFunction* function, IrValueId value)
{
    IrValueLabelMetadata* entry = ir_value_label_metadata_find(function, value);
    IrValueLabelMetadata zero = {0};
    return entry ? *entry : zero;
}

IrInstructionExtra* ir_instruction_extra_find(IrFunction* function, IrInstructionId instruction)
{
    if (function && function->extra_count)
    {
        u32 low = 0;
        u32 high = function->extra_count;
        while (low < high)
        {
            u32 middle = low + (high - low) / 2;
            if (function->extra_instructions[middle].value < instruction.value)
            {
                low = middle + 1;
            }
            else
            {
                high = middle;
            }
        }
        if (low < function->extra_count && function->extra_instructions[low].value == instruction.value)
        {
            return function->extras + low;
        }
    }

    return 0;
}

IrInstructionExtra ir_instruction_extra(IrFunction* function, IrInstructionId instruction)
{
    IrInstructionExtra* entry = ir_instruction_extra_find(function, instruction);
    IrInstructionExtra zero = {0};
    return entry ? *entry : zero;
}

IrInstructionExtra* ir_instruction_extra_ensure(Arena* arena, IrFunction* function, IrInstructionId instruction)
{
    IrInstructionExtra* result;
    if (!arena || !function)
    {
        result = 0;
    }
    else
    {
        u32 low = 0;
        u32 high = function->extra_count;
        while (low < high)
        {
            u32 middle = low + (high - low) / 2;
            if (function->extra_instructions[middle].value < instruction.value)
            {
                low = middle + 1;
            }
            else
            {
                high = middle;
            }
        }
        if (low >= function->extra_count || function->extra_instructions[low].value != instruction.value)
        {
            if (function->extra_count == function->extra_capacity)
            {
                u32 capacity = function->extra_capacity ? function->extra_capacity * 2 : 8;
                IrInstructionId* instructions = arena_allocate(arena, IrInstructionId, capacity);
                IrInstructionExtra* extras = arena_allocate(arena, IrInstructionExtra, capacity);
                if (function->extra_count)
                {
                    memcpy(instructions, function->extra_instructions, sizeof(*instructions) * function->extra_count);
                    memcpy(extras, function->extras, sizeof(*extras) * function->extra_count);
                }
                function->extra_instructions = instructions;
                function->extras = extras;
                function->extra_capacity = capacity;
            }
            for (u32 move = function->extra_count; move > low; move -= 1)
            {
                function->extra_instructions[move] = function->extra_instructions[move - 1];
                function->extras[move] = function->extras[move - 1];
            }
            function->extra_instructions[low] = instruction;
            function->extras[low] = (IrInstructionExtra){0};
            function->extra_count += 1;
        }
        result = function->extras + low;
    }

    return result;
}

IrSourceRange ir_instruction_canonical_source(IrFunction* function, IrInstructionId instruction)
{
    IrSourceRange result;
    if (!function || !function->instruction_canonical_sources || instruction.value >= function->instruction_count)
    {
        result = (IrSourceRange){0};
    }
    else
    {
        result = function->instruction_canonical_sources[instruction.value];
    }

    return result;
}

IrSourcePosition ir_source_position(IrProgram* program, IrSourceRange range)
{
    if (!program)
    {
        return (IrSourcePosition){0};
    }
    if (program->source_map.count)
    {
        return ir_source_map_position(&program->source_map, range.offset, &program->source_cursor);
    }
    IrSource* source = ir_source_from_id(&program->sources, range.source);
    IrSourcePosition result;
    if (!source || !source->text.length)
    {
        result = (IrSourcePosition){.source = range.source.value};
    }
    else
    {
        result = ir_source_text_position(source->text, range.source.value, range.offset, &program->source_cursor);
    }

    return result;
}

IrValueLabelMetadata* ir_value_label_metadata_ensure(Arena* arena, IrFunction* function, IrValueId value)
{
    IrValueLabelMetadata* result;
    if (!arena || !function)
    {
        result = 0;
    }
    else
    {
        u32 low = 0;
        u32 high = function->label_metadata_count;
        while (low < high)
        {
            u32 middle = low + (high - low) / 2;
            if (function->label_metadata_values[middle].value < value.value)
            {
                low = middle + 1;
            }
            else
            {
                high = middle;
            }
        }
        if (low >= function->label_metadata_count || function->label_metadata_values[low].value != value.value)
        {
            if (function->label_metadata_count == function->label_metadata_capacity)
            {
                u32 new_capacity = function->label_metadata_capacity ? function->label_metadata_capacity * 2 : 8;
                IrValueId* new_values = arena_allocate(arena, IrValueId, new_capacity);
                IrValueLabelMetadata* new_entries = arena_allocate(arena, IrValueLabelMetadata, new_capacity);
                if (function->label_metadata_count)
                {
                    memcpy(new_values, function->label_metadata_values, sizeof(*new_values) * function->label_metadata_count);
                    memcpy(new_entries, function->label_metadata, sizeof(*new_entries) * function->label_metadata_count);
                }
                function->label_metadata_values = new_values;
                function->label_metadata = new_entries;
                function->label_metadata_capacity = new_capacity;
            }
            u32 tail = function->label_metadata_count - low;
            if (tail)
            {
                memmove(function->label_metadata_values + low + 1, function->label_metadata_values + low, sizeof(*function->label_metadata_values) * tail);
                memmove(function->label_metadata + low + 1, function->label_metadata + low, sizeof(*function->label_metadata) * tail);
            }
            function->label_metadata_values[low] = value;
            function->label_metadata[low] = (IrValueLabelMetadata){0};
            function->label_metadata_count += 1;
        }
        result = function->label_metadata + low;
    }

    return result;
}

bool ir_label_provenance_valid(IrValueLabelMetadata* value)
{
    if (!value || !value->is_label_value || value->has_label_provenance || value->has_non_label_provenance || !value->label_block_count || !value->label_blocks)
    {
        return false;
    }
    for (u32 left = 0; left < value->label_block_count; left += 1)
    {
        for (u32 right = left + 1; right < value->label_block_count; right += 1)
        {
            if (value->label_blocks[left].value == value->label_blocks[right].value)
            {
                return false;
            }
        }
    }
    return true;
}

bool ir_label_storage_provenance_valid(IrValueLabelMetadata* value)
{
    if (!value || !value->has_label_provenance || value->is_label_value || !value->label_block_count || !value->label_blocks)
    {
        return false;
    }
    for (u32 left = 0; left < value->label_block_count; left += 1)
    {
        for (u32 right = left + 1; right < value->label_block_count; right += 1)
        {
            if (value->label_blocks[left].value == value->label_blocks[right].value)
            {
                return false;
            }
        }
    }
    return true;
}

bool ir_block_id_array_unique(IrBlockId* blocks, u32 count)
{
    if (count && !blocks)
    {
        return false;
    }
    for (u32 left = 0; left < count; left += 1)
    {
        for (u32 right = left + 1; right < count; right += 1)
        {
            if (blocks[left].value == blocks[right].value)
            {
                return false;
            }
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_canonical_void_pointer_type(IrProgram* program, IrTypeId type_id)
{
    IrType* type = ir_type_from_id(&program->types, type_id);
    IrType* element = type && type->kind == IR_TYPE_POINTER ? ir_type_from_id(&program->types, type->element_type) : 0;
    return element && element->kind == IR_TYPE_VOID;
}

BUSTER_GLOBAL_LOCAL IrFunction* ir_module_function_for_symbol(IrModule* module, IrSymbolId symbol)
{
    if (module && module->functions)
    {
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            if (function->symbol.value == symbol.value)
            {
                return function;
            }
        }
    }

    return 0;
}

BUSTER_GLOBAL_LOCAL bool ir_label_block_set_contains(IrValueLabelMetadata* value, IrBlockId block)
{
    if (value && value->label_blocks)
    {
        for (u32 index = 0; index < value->label_block_count; index += 1)
        {
            if (value->label_blocks[index].value == block.value)
            {
                return true;
            }
        }
    }

    return false;
}

BUSTER_GLOBAL_LOCAL bool ir_label_path_contains_block(IrLabelProvenancePath* path, IrBlockId block)
{
    if (path && path->label_blocks)
    {
        for (u32 index = 0; index < path->label_block_count; index += 1)
        {
            if (path->label_blocks[index].value == block.value)
            {
                return true;
            }
        }
    }

    return false;
}

BUSTER_GLOBAL_LOCAL bool ir_label_block_sets_equal(IrValueLabelMetadata* left, IrValueLabelMetadata* right)
{
    if (!left || !right || left->label_block_count != right->label_block_count)
    {
        return false;
    }
    for (u32 index = 0; index < left->label_block_count; index += 1)
    {
        if (!ir_label_block_set_contains(right, left->label_blocks[index]))
        {
            return false;
        }
    }
    for (u32 index = 0; index < right->label_block_count; index += 1)
    {
        if (!ir_label_block_set_contains(left, right->label_blocks[index]))
        {
            return false;
        }
    }
    return true;
}

bool ir_label_metadata_shape_valid(IrProgram* program, IrFunction* function, IrValueId value_id)
{
    IrValue* value_slot = function && value_id.value < function->value_count ? function->values + value_id.value : 0;
    if (function && !function->label_metadata_count)
    {
        // With no metadata anywhere in the function every label-specific
        // clause below is vacuous; only the value/type-layout requirements
        // remain.
        IrType* empty_value_type = program && value_slot ? ir_type_from_id(&program->types, value_slot->canonical_type) : 0;
        return value_slot && (!program || (empty_value_type && empty_value_type->layout.resolved));
    }
    IrValueLabelMetadata metadata = ir_value_label_metadata(function, value_id);
    IrValueLabelMetadata* value = value_slot ? &metadata : 0;
    IrType* value_type = program && value_slot ? ir_type_from_id(&program->types, value_slot->canonical_type) : 0;
    u64 pointer_size = program ? program->data_layout.pointer.size : 0;
    bool valid = function && value && (!program || (value_type && value_type->layout.resolved)) && (value->label_block_count != 0) == (value->label_blocks != 0) &&
           (value->label_path_count != 0) == (value->label_paths != 0) &&
           (!value->is_label_value || value->label_block_count != 0) &&
           (!value->has_label_provenance || value->label_block_count != 0) &&
           (!value->label_block_count || value->is_label_value || value->has_label_provenance) &&
           ((!value->is_label_value && !value->has_label_provenance && value->label_block_count == 0) ||
            value->is_label_value || value->has_label_provenance || value->has_non_label_provenance) &&
           !(value->is_label_value && value->has_label_provenance);
    for (u32 index = 0; valid && index < value->label_block_count; index += 1)
    {
        valid = value->label_blocks[index].value < function->block_count;
        for (u32 previous = 0; valid && previous < index; previous += 1)
        {
            valid = value->label_blocks[previous].value != value->label_blocks[index].value;
        }
    }
    bool path_has_non_label = false;
    bool path_has_label = false;
    for (u32 index = 0; valid && index < value->label_path_count; index += 1)
    {
        IrLabelProvenancePath* path = value->label_paths + index;
        valid = path->size != 0 && path->offset <= UINT64_MAX - path->size && (!program || path->offset + path->size <= value_type->layout.size) &&
                (path->label_block_count != 0) == (path->label_blocks != 0) &&
                (!path->is_non_label || path->label_block_count == 0) && (path->is_non_label || path->label_block_count != 0);
        if (valid && !path->is_non_label)
        {
            valid = !program || (pointer_size != 0 && path->size == pointer_size);
        }
        path_has_non_label |= path->is_non_label;
        path_has_label |= !path->is_non_label;
        for (u32 block_index = 0; valid && block_index < path->label_block_count; block_index += 1)
        {
            valid = path->label_blocks[block_index].value < function->block_count && ir_label_block_set_contains(value, path->label_blocks[block_index]);
            for (u32 previous = 0; valid && previous < block_index; previous += 1)
            {
                valid = path->label_blocks[previous].value != path->label_blocks[block_index].value;
            }
        }
        for (u32 previous_index = 0; valid && previous_index < index; previous_index += 1)
        {
            IrLabelProvenancePath* previous = value->label_paths + previous_index;
            u64 previous_end = previous->offset + previous->size;
            u64 path_end = path->offset + path->size;
            valid = !(previous->offset < path_end && path->offset < previous_end);
        }
    }
    if (valid && value->label_path_count)
    {
        valid = !value->is_label_value && path_has_label == value->has_label_provenance && (!path_has_non_label || value->has_non_label_provenance);
    }
    if (valid && value->has_label_provenance)
    {
        for (u32 block_index = 0; block_index < value->label_block_count; block_index += 1)
        {
            bool found = false;
            for (u32 path_index = 0; path_index < value->label_path_count; path_index += 1)
            {
                IrLabelProvenancePath* path = value->label_paths + path_index;
                found |= !path->is_non_label && ir_label_path_contains_block(path, value->label_blocks[block_index]);
            }
            valid &= found;
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ir_label_block_set_subset(IrValueLabelMetadata* subset, IrValueLabelMetadata* superset)
{
    if (!subset || !superset)
    {
        return false;
    }
    for (u32 index = 0; index < subset->label_block_count; index += 1)
    {
        if (!ir_label_block_set_contains(superset, subset->label_blocks[index]))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_value_has_non_label_path(IrValueLabelMetadata* value)
{
    if (!value)
    {
        return false;
    }
    bool result = value->has_non_label_provenance;
    for (u32 path_index = 0; path_index < value->label_path_count; path_index += 1)
    {
        result |= value->label_paths[path_index].is_non_label;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ir_label_path_blocks_subset(IrLabelProvenancePath* subset, IrLabelProvenancePath* superset)
{
    if (!subset || !superset || subset->is_non_label || superset->is_non_label)
    {
        return subset && superset && subset->is_non_label == superset->is_non_label;
    }
    for (u32 block_index = 0; block_index < subset->label_block_count; block_index += 1)
    {
        if (!ir_label_path_contains_block(superset, subset->label_blocks[block_index]))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_label_path_blocks_equal(IrLabelProvenancePath* left, IrLabelProvenancePath* right)
{
    return left && right && ir_label_path_blocks_subset(left, right) && ir_label_path_blocks_subset(right, left);
}

BUSTER_GLOBAL_LOCAL bool ir_label_metadata_paths_transfer_exact(IrValueLabelMetadata* result, IrValueLabelMetadata* source, u64 base_offset, u64 base_size)
{
    if (!result || !source || base_offset > UINT64_MAX - base_size)
    {
        return false;
    }
    u64 base_end = base_offset + base_size;
    for (u32 source_index = 0; source_index < source->label_path_count; source_index += 1)
    {
        IrLabelProvenancePath* source_path = source->label_paths + source_index;
        if (source_path->offset > UINT64_MAX - source_path->size)
        {
            return false;
        }
        u64 source_end = source_path->offset + source_path->size;
        if (source_path->offset < base_offset || source_end > base_end)
        {
            continue;
        }
        bool found = false;
        for (u32 result_index = 0; result_index < result->label_path_count; result_index += 1)
        {
            IrLabelProvenancePath* result_path = result->label_paths + result_index;
            if (result_path->offset == source_path->offset - base_offset && result_path->size == source_path->size &&
                ir_label_path_blocks_equal(source_path, result_path))
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            return false;
        }
    }
    for (u32 result_index = 0; result_index < result->label_path_count; result_index += 1)
    {
        IrLabelProvenancePath* result_path = result->label_paths + result_index;
        bool found = false;
        for (u32 source_index = 0; source_index < source->label_path_count; source_index += 1)
        {
            IrLabelProvenancePath* source_path = source->label_paths + source_index;
            if (source_path->offset >= base_offset && source_path->offset <= UINT64_MAX - source_path->size &&
                source_path->offset + source_path->size <= base_end && source_path->offset - base_offset == result_path->offset &&
                source_path->size == result_path->size && ir_label_path_blocks_equal(source_path, result_path))
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_constant_index_value(IrFunction* function, IrValueId value, u64* index_out)
{
    if (!function || !index_out || value.value >= function->value_count)
    {
        return false;
    }
    IrInstructionId definition = function->values[value.value].definition;
    if (definition.value >= function->instruction_count)
    {
        return false;
    }
    IrInstruction* instruction = function->instructions + definition.value;
    if (instruction->opcode != IR_OPCODE_CONSTANT_INTEGER || instruction->immediate_count != 1 || !instruction->immediates || instruction->immediate_is_negative)
    {
        return false;
    }
    *index_out = instruction->immediates[0];
    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_label_metadata_has_label(IrValueLabelMetadata* value)
{
    if (value)
    {
        if (value->is_label_value || value->has_label_provenance)
        {
            return true;
        }
        for (u32 path_index = 0; path_index < value->label_path_count; path_index += 1)
        {
            if (!value->label_paths[path_index].is_non_label && value->label_paths[path_index].label_block_count)
            {
                return true;
            }
        }
    }

    return false;
}

BUSTER_GLOBAL_LOCAL bool ir_label_metadata_storage_transfer_valid(IrProgram* program, IrValueLabelMetadata* result, IrValueLabelMetadata* source)
{
    BUSTER_UNUSED(program);
    if (!result || !source || result->is_label_value)
    {
        return false;
    }
    if (ir_label_metadata_has_label(result) || ir_label_metadata_has_label(source))
    {
        if (ir_label_metadata_has_label(result) && !ir_label_metadata_has_label(source))
        {
            return false;
        }
        if (result->has_label_provenance && !ir_label_block_set_subset(result, source))
        {
            return false;
        }
        if (result->label_path_count && !source->label_path_count)
        {
            return false;
        }
    }

    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_label_metadata_dynamic_index_transfer_valid(IrValueLabelMetadata* result, IrValueLabelMetadata* source, u64 array_size, u64 element_size)
{
    if (!result || !source)
    {
        return false;
    }
    if (ir_label_metadata_has_label(result) || ir_label_metadata_has_label(source))
    {
        if (!element_size || array_size < element_size || !source->label_path_count)
        {
            return !ir_label_metadata_has_label(result);
        }
        bool source_non_label = source->has_non_label_provenance;
        bool source_label = false;
        for (u32 source_index = 0; source_index < source->label_path_count; source_index += 1)
        {
            IrLabelProvenancePath* source_path = source->label_paths + source_index;
            if (source_path->offset > UINT64_MAX - source_path->size || source_path->offset + source_path->size > array_size)
            {
                return false;
            }
            source_non_label |= source_path->is_non_label;
            source_label |= !source_path->is_non_label;
            bool found = false;
            for (u32 result_index = 0; result_index < result->label_path_count; result_index += 1)
            {
                IrLabelProvenancePath* result_path = result->label_paths + result_index;
                if (result_path->offset != 0 || result_path->size != element_size)
                {
                    continue;
                }
                if (source_path->is_non_label)
                {
                    found |= result_path->is_non_label || result->has_non_label_provenance;
                }
                else
                {
                    bool blocks_match = !result_path->is_non_label;
                    for (u32 block_index = 0; blocks_match && block_index < source_path->label_block_count; block_index += 1)
                    {
                        blocks_match = ir_label_path_contains_block(result_path, source_path->label_blocks[block_index]);
                    }
                    found |= blocks_match;
                }
            }
            if (!found)
            {
                return false;
            }
        }
        for (u32 result_index = 0; result_index < result->label_path_count; result_index += 1)
        {
            IrLabelProvenancePath* result_path = result->label_paths + result_index;
            if (result_path->offset != 0 || result_path->size != element_size)
            {
                return false;
            }
            if (result_path->is_non_label)
            {
                if (!source_non_label)
                {
                    return false;
                }
            }
            else
            {
                for (u32 block_index = 0; block_index < result_path->label_block_count; block_index += 1)
                {
                    bool found = false;
                    for (u32 source_index = 0; source_index < source->label_path_count; source_index += 1)
                    {
                        IrLabelProvenancePath* source_path = source->label_paths + source_index;
                        found |= !source_path->is_non_label && source_path->offset <= UINT64_MAX - source_path->size &&
                                 source_path->offset + source_path->size <= array_size &&
                                 ir_label_path_contains_block(source_path, result_path->label_blocks[block_index]);
                    }
                    if (!found)
                    {
                        return false;
                    }
                }
            }
        }
        if (source_non_label && !result->has_non_label_provenance)
        {
            return false;
        }
        if (source_label && !ir_label_metadata_has_label(result))
        {
            return false;
        }
        for (u32 source_index = 0; source_index < source->label_block_count; source_index += 1)
        {
            if (!ir_label_block_set_contains(result, source->label_blocks[source_index]))
            {
                return false;
            }
        }
    }

    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_label_metadata_aggregate_transfer_valid(IrProgram* program, IrFunction* function, IrInstruction* definition,
                                                                     IrValueLabelMetadata* result)
{
    if (!program || !function || !definition || !result || result->is_label_value)
    {
        return false;
    }
    bool result_has_label = ir_label_metadata_has_label(result);
    bool source_has_label = false;
    for (u32 operand_index = 0; operand_index < definition->operand_count; operand_index += 1)
    {
        if (!definition->operands || definition->operands[operand_index].value >= function->value_count)
        {
            return false;
        }
        IrValueLabelMetadata source = ir_value_label_metadata(function, definition->operands[operand_index]);
        source_has_label |= ir_label_metadata_has_label(&source);
    }
    if (result_has_label || source_has_label)
    {
        IrType* aggregate = ir_type_from_id(&program->types, definition->canonical_type);
        if (!aggregate || (definition->opcode == IR_OPCODE_ARRAY && aggregate->kind != IR_TYPE_ARRAY && aggregate->kind != IR_TYPE_VECTOR) ||
            (definition->opcode == IR_OPCODE_AGGREGATE && aggregate->kind != IR_TYPE_STRUCT && aggregate->kind != IR_TYPE_UNION))
        {
            return false;
        }
        bool source_non_label = false;
        bool source_label = false;
        for (u32 operand_index = 0; operand_index < definition->operand_count; operand_index += 1)
        {
            if (!definition->operands || definition->operands[operand_index].value >= function->value_count)
            {
                return false;
            }
            IrValueLabelMetadata source_metadata = ir_value_label_metadata(function, definition->operands[operand_index]);
            IrValueLabelMetadata* source = &source_metadata;
            IrType* source_type = ir_type_from_id(&program->types, function->values[definition->operands[operand_index].value].canonical_type);
            u64 base_offset = 0;
            u64 source_size = 0;
            if (definition->opcode == IR_OPCODE_ARRAY)
            {
                IrType* element = ir_type_from_id(&program->types, aggregate->element_type);
                if (!element || !element->layout.resolved || operand_index >= aggregate->element_count ||
                    operand_index > UINT64_MAX / element->layout.size)
                {
                    return false;
                }
                base_offset = operand_index * element->layout.size;
                source_size = element->layout.size;
            }
            else
            {
                u64 field_index = definition->immediate_count == definition->operand_count && definition->immediates ? definition->immediates[operand_index] : UINT64_MAX;
                if (field_index >= aggregate->field_count)
                {
                    return false;
                }
                IrField* field = aggregate->fields + field_index;
                IrType* field_type = ir_type_from_id(&program->types, field->type);
                if (!field_type || !field_type->layout.resolved)
                {
                    return false;
                }
                base_offset = field->offset;
                source_size = field_type->layout.size;
            }
            if (!source_type || !source_type->layout.resolved || base_offset > aggregate->layout.size || source_size > aggregate->layout.size - base_offset)
            {
                return false;
            }
            source_non_label |= ir_value_has_non_label_path(source);
            source_label |= ir_label_metadata_has_label(source);
            for (u32 path_index = 0; path_index < source->label_path_count; path_index += 1)
            {
                IrLabelProvenancePath* source_path = source->label_paths + path_index;
                if (source_path->offset > UINT64_MAX - source_path->size || source_path->offset + source_path->size > source_size ||
                    base_offset > UINT64_MAX - source_path->offset)
                {
                    return false;
                }
                u64 expected_offset = base_offset + source_path->offset;
                bool found = false;
                for (u32 result_index = 0; result_index < result->label_path_count; result_index += 1)
                {
                    IrLabelProvenancePath* result_path = result->label_paths + result_index;
                    if (result_path->offset != expected_offset || result_path->size != source_path->size)
                    {
                        continue;
                    }
                    if (source_path->is_non_label)
                    {
                        found |= result_path->is_non_label || result->has_non_label_provenance;
                    }
                    else
                    {
                        bool blocks_match = !result_path->is_non_label;
                        for (u32 block_index = 0; blocks_match && block_index < source_path->label_block_count; block_index += 1)
                        {
                            blocks_match = ir_label_path_contains_block(result_path, source_path->label_blocks[block_index]);
                        }
                        found |= blocks_match;
                    }
                }
                if (!found)
                {
                    return false;
                }
            }
            if (source->is_label_value)
            {
                bool found = false;
                for (u32 result_index = 0; result_index < result->label_path_count; result_index += 1)
                {
                    IrLabelProvenancePath* result_path = result->label_paths + result_index;
                    if (result_path->offset != base_offset || result_path->size != source_size || result_path->is_non_label)
                    {
                        continue;
                    }
                    bool blocks_match = source->label_block_count <= result_path->label_block_count;
                    for (u32 block_index = 0; blocks_match && block_index < source->label_block_count; block_index += 1)
                    {
                        blocks_match = ir_label_path_contains_block(result_path, source->label_blocks[block_index]);
                    }
                    found |= blocks_match;
                }
                if (!found)
                {
                    return false;
                }
            }
        }
        if (source_non_label && !result->has_non_label_provenance)
        {
            return false;
        }
        if (source_label && !ir_label_metadata_has_label(result))
        {
            return false;
        }
        for (u32 result_index = 0; result_index < result->label_path_count; result_index += 1)
        {
            IrLabelProvenancePath* result_path = result->label_paths + result_index;
            bool found = false;
            for (u32 operand_index = 0; operand_index < definition->operand_count && !found; operand_index += 1)
            {
                IrValueLabelMetadata source_metadata = ir_value_label_metadata(function, definition->operands[operand_index]);
                IrValueLabelMetadata* source = &source_metadata;
                u64 base_offset = 0;
                u64 source_size = 0;
                if (definition->opcode == IR_OPCODE_ARRAY)
                {
                    IrType* element = ir_type_from_id(&program->types, aggregate->element_type);
                    base_offset = operand_index * element->layout.size;
                    source_size = element->layout.size;
                }
                else
                {
                    IrField* field = aggregate->fields + definition->immediates[operand_index];
                    IrType* field_type = ir_type_from_id(&program->types, field->type);
                    base_offset = field->offset;
                    source_size = field_type->layout.size;
                }
                for (u32 path_index = 0; path_index < source->label_path_count; path_index += 1)
                {
                    IrLabelProvenancePath* source_path = source->label_paths + path_index;
                    if (source_path->offset <= UINT64_MAX - source_path->size && source_path->offset + source_path->size <= source_size &&
                        base_offset <= UINT64_MAX - source_path->offset && result_path->offset == base_offset + source_path->offset &&
                        result_path->size == source_path->size)
                    {
                        if (source_path->is_non_label)
                        {
                            found |= result_path->is_non_label || result->has_non_label_provenance;
                        }
                        else
                        {
                            bool blocks_match = !result_path->is_non_label;
                            for (u32 block_index = 0; blocks_match && block_index < result_path->label_block_count; block_index += 1)
                            {
                                blocks_match = ir_label_path_contains_block(source_path, result_path->label_blocks[block_index]);
                            }
                            found |= blocks_match;
                        }
                    }
                }
                if (source->is_label_value && result_path->offset == base_offset && result_path->size == source_size && !result_path->is_non_label)
                {
                    bool blocks_match = source->label_block_count <= result_path->label_block_count;
                    for (u32 block_index = 0; blocks_match && block_index < source->label_block_count; block_index += 1)
                    {
                        blocks_match &= ir_label_path_contains_block(result_path, source->label_blocks[block_index]);
                    }
                    found |= blocks_match;
                }
            }
            if (!found)
            {
                return false;
            }
        }
    }

    return true;
}

BUSTER_GLOBAL_LOCAL bool ir_label_transfer_valid_address_of(IrValueLabelMetadata* result)
{
    return !result->is_label_value && !result->has_label_provenance && !result->label_blocks &&
           !result->label_block_count && !result->label_paths && !result->label_path_count;
}

BUSTER_GLOBAL_LOCAL bool ir_label_transfer_valid_load(IrProgram* program, IrValue* first_slot, IrValueLabelMetadata* result, IrValueLabelMetadata* first)
{
    bool valid;
    if (!first)
    {
        valid = false;
    }
    else if (result->is_label_value)
    {
        valid = first->has_label_provenance && !first->has_non_label_provenance && !ir_value_has_non_label_path(first) && !result->label_path_count &&
               ir_label_block_sets_equal(result, first);
    }
    else if (!ir_label_metadata_storage_transfer_valid(program, result, first))
    {
        valid = false;
    }
    else if (!ir_label_metadata_has_label(first) && !ir_label_metadata_has_label(result))
    {
        valid = true;
    }
    else
    {
        IrType* load_source_type = program ? ir_type_from_id(&program->types, first_slot->canonical_type) : 0;
        valid = !program || !load_source_type || !load_source_type->layout.resolved ||
                ir_label_metadata_paths_transfer_exact(result, first, 0, load_source_type->layout.size);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ir_label_transfer_valid_cast(
    IrProgram* program, IrInstruction* definition, IrValue* result_slot, IrValue* first_slot, IrValueLabelMetadata* result, IrValueLabelMetadata* first)
{
    bool valid;
    // A label that survives a cast may only pass through an identity cast
    // between the same void pointer type, which is what the conjunction here
    // says: the outer test selects the labelled case, the inner one rejects it.
    if (!first)
    {
        valid = false;
    }
    else if ((ir_label_metadata_has_label(first) || ir_label_metadata_has_label(result)) &&
             (!program || !ir_canonical_void_pointer_type(program, first_slot->canonical_type) ||
              !ir_canonical_void_pointer_type(program, result_slot->canonical_type) ||
              first_slot->canonical_type.value != result_slot->canonical_type.value ||
              definition->conversion_operation != IR_CONVERSION_IDENTITY))
    {
        valid = false;
    }
    else if (result->is_label_value)
    {
        valid = first->is_label_value && !first->has_non_label_provenance && !ir_value_has_non_label_path(first) && !result->label_path_count &&
                ir_label_block_sets_equal(result, first);
    }
    else if (!ir_label_metadata_storage_transfer_valid(program, result, first))
    {
        valid = false;
    }
    else if (!ir_label_metadata_has_label(first) && !ir_label_metadata_has_label(result))
    {
        valid = true;
    }
    else
    {
        IrType* cast_source_type = program ? ir_type_from_id(&program->types, first_slot->canonical_type) : 0;
        valid = !program || !cast_source_type || !cast_source_type->layout.resolved ||
                ir_label_metadata_paths_transfer_exact(result, first, 0, cast_source_type->layout.size);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ir_label_transfer_valid_field(IrProgram* program, IrInstruction* definition, IrValue* first_slot,
                                                       IrValueLabelMetadata* result, IrValueLabelMetadata* first)
{
    bool valid;
    if (!first || result->is_label_value)
    {
        valid = false;
    }
    else if (!ir_label_metadata_has_label(first) && !ir_label_metadata_has_label(result))
    {
        valid = true;
    }
    else if (!program)
    {
        valid = !ir_label_metadata_has_label(result) || (ir_label_metadata_has_label(first) && ir_label_block_set_subset(result, first));
    }
    else
    {
        IrType* aggregate = ir_type_from_id(&program->types, first_slot->canonical_type);
        u64 field_index = definition->immediate_count == 1 && definition->immediates ? definition->immediates[0] : UINT64_MAX;
        bool addressable = aggregate && (aggregate->kind == IR_TYPE_STRUCT || aggregate->kind == IR_TYPE_UNION) && field_index < aggregate->field_count;
        IrField* field = addressable ? aggregate->fields + field_index : 0;
        IrType* field_type = field ? ir_type_from_id(&program->types, field->type) : 0;
        if (!field_type || !field_type->layout.resolved)
        {
            // A field this pass cannot resolve carries no label through it.
            valid = !ir_label_metadata_has_label(result) && !result->label_path_count;
        }
        else if (aggregate->kind == IR_TYPE_UNION && ir_label_metadata_has_label(first) &&
                 !(field_type->kind == IR_TYPE_POINTER && ir_canonical_void_pointer_type(program, field->type)))
        {
            valid = false;
        }
        else if (!ir_label_metadata_storage_transfer_valid(program, result, first))
        {
            valid = false;
        }
        else if (ir_label_metadata_has_label(result) && !first->label_path_count)
        {
            valid = false;
        }
        else
        {
            valid = ir_label_metadata_paths_transfer_exact(result, first, field->offset, field_type->layout.size);
        }
    }
    return valid;
}

// The constant-index and dynamic-index cases differ enough to be worth naming;
// both start from the element type the base resolves to.
BUSTER_GLOBAL_LOCAL bool ir_label_transfer_valid_constant_index(IrValueLabelMetadata* result, IrValueLabelMetadata* first, IrType* base_type,
                                                                IrType* element_type, u64 index)
{
    bool valid;
    if ((base_type->kind != IR_TYPE_ARRAY && base_type->kind != IR_TYPE_VECTOR) || index >= base_type->element_count ||
        index > UINT64_MAX / element_type->layout.size)
    {
        valid = !ir_label_metadata_has_label(result) && !result->label_path_count;
    }
    else
    {
        u64 offset = index * element_type->layout.size;
        if (offset > base_type->layout.size || element_type->layout.size > base_type->layout.size - offset)
        {
            valid = false;
        }
        else if (ir_label_metadata_has_label(result) && !first->label_path_count)
        {
            valid = false;
        }
        else
        {
            valid = ir_label_metadata_paths_transfer_exact(result, first, offset, element_type->layout.size);
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ir_label_transfer_valid_dynamic_index(IrValueLabelMetadata* result, IrValueLabelMetadata* first, IrType* base_type,
                                                               IrType* element_type)
{
    bool valid;
    // Only read once the two tests above have ruled out the overflow that
    // would make it meaningless.
    u64 array_size = element_type->layout.size * base_type->element_count;
    if (base_type->kind != IR_TYPE_ARRAY && base_type->kind != IR_TYPE_VECTOR)
    {
        valid = !ir_label_metadata_has_label(result) && !result->label_path_count;
    }
    else if (!base_type->element_count || element_type->layout.size > UINT64_MAX / base_type->element_count)
    {
        valid = !ir_label_metadata_has_label(result) && !result->label_path_count;
    }
    else if (array_size > base_type->layout.size)
    {
        valid = false;
    }
    else if (element_type->kind == IR_TYPE_ARRAY || element_type->kind == IR_TYPE_STRUCT || element_type->kind == IR_TYPE_UNION)
    {
        // A dynamic aggregate element may contain label-bearing subobjects at
        // offsets that are not representable by the scalar transfer below. Do
        // not accept either a forged result or a silently incomplete transfer:
        // the frontend rejects this case until the metadata model grows a
        // wildcard/subobject representation.
        valid = !ir_label_metadata_has_label(first) && !ir_label_metadata_has_label(result);
    }
    else
    {
        valid = ir_label_metadata_dynamic_index_transfer_valid(result, first, array_size, element_type->layout.size);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ir_label_transfer_valid_index(IrProgram* program, IrFunction* function, IrInstruction* definition, IrValue* first_slot,
                                                       IrValueLabelMetadata* result, IrValueLabelMetadata* first)
{
    bool valid;
    if (!first || result->is_label_value)
    {
        valid = false;
    }
    else if (!ir_label_metadata_has_label(first) && !ir_label_metadata_has_label(result))
    {
        valid = true;
    }
    else if (!program)
    {
        valid = !ir_label_metadata_has_label(result) || (ir_label_metadata_has_label(first) && ir_label_block_set_subset(result, first));
    }
    else
    {
        IrType* base_type = ir_type_from_id(&program->types, first_slot->canonical_type);
        IrType* element_type = base_type ? ir_type_from_id(&program->types, base_type->element_type) : 0;
        u64 index = 0;
        bool constant = definition->operand_count == 2 && definition->operands && ir_constant_index_value(function, definition->operands[1], &index);
        if (!base_type || !element_type || !element_type->layout.resolved || !element_type->layout.size)
        {
            valid = !ir_label_metadata_has_label(result) && !result->label_path_count;
        }
        else if (base_type->kind == IR_TYPE_POINTER && ir_label_metadata_has_label(first))
        {
            valid = false;
        }
        else if (!ir_label_metadata_storage_transfer_valid(program, result, first))
        {
            valid = false;
        }
        else if (constant)
        {
            valid = ir_label_transfer_valid_constant_index(result, first, base_type, element_type, index);
        }
        else
        {
            valid = ir_label_transfer_valid_dynamic_index(result, first, base_type, element_type);
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ir_label_transfer_valid_dereference(IrValueLabelMetadata* result, IrValueLabelMetadata* first)
{
    return first && !ir_label_metadata_has_label(first) && !ir_label_metadata_has_label(result);
}

BUSTER_GLOBAL_LOCAL bool ir_label_transfer_valid_aggregate(IrProgram* program, IrFunction* function, IrInstruction* definition,
                                                           IrValueLabelMetadata* result)
{
    bool valid;
    if (program)
    {
        valid = ir_label_metadata_aggregate_transfer_valid(program, function, definition, result);
    }
    else
    {
        // Without a program to resolve types through, the check reduces to
        // provenance: every block the result names must come from an operand.
        valid = true;
        for (u32 block_index = 0; block_index < result->label_block_count && valid; block_index += 1)
        {
            bool found = false;
            for (u32 operand_index = 0; operand_index < definition->operand_count && !found; operand_index += 1)
            {
                if (definition->operands && definition->operands[operand_index].value < function->value_count)
                {
                    IrValueLabelMetadata operand_metadata = ir_value_label_metadata(function, definition->operands[operand_index]);
                    found = ir_label_block_set_contains(&operand_metadata, result->label_blocks[block_index]);
                }
            }
            valid = found;
        }
        valid = valid && !result->is_label_value;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ir_label_transfer_valid_label_address(IrProgram* program, IrInstruction* definition, IrValueLabelMetadata* result)
{
    return (!program || ir_canonical_void_pointer_type(program, definition->canonical_type)) && ir_label_provenance_valid(result) &&
           definition->target_count == 1 && definition->targets && result->label_block_count == 1 &&
           result->label_blocks[0].value == definition->targets[0].value;
}

BUSTER_GLOBAL_LOCAL bool ir_label_transfer_valid_local(IrValueLabelMetadata* result)
{
    return !result->is_label_value;
}

BUSTER_GLOBAL_LOCAL bool ir_label_transfer_valid_default(IrValueLabelMetadata* result)
{
    return !result->is_label_value && !result->has_label_provenance && !result->label_block_count && !result->label_blocks && !result->label_path_count &&
           !result->label_paths;
}
// With no metadata anywhere in the function, the full checks below reduce to
// the operand-existence requirements of each transfer rule (and LABEL_ADDRESS
// can never validate a metadata-free result).
BUSTER_GLOBAL_LOCAL bool ir_label_transfer_valid_without_metadata(IrProgram* program, IrFunction* function, IrInstruction* definition, IrValue* first_slot)
{
    bool valid;
    switch (definition->opcode)
    {
    case IR_OPCODE_LOAD:
    case IR_OPCODE_ATOMIC_LOAD:
    case IR_OPCODE_CAST:
    case IR_OPCODE_FIELD:
    case IR_OPCODE_INDEX:
    case IR_OPCODE_DEREFERENCE:
        valid = first_slot != 0;
        break;
    case IR_OPCODE_ARRAY:
    case IR_OPCODE_AGGREGATE:
        valid = true;
        for (u32 operand_index = 0; program && operand_index < definition->operand_count && valid; operand_index += 1)
        {
            valid = definition->operands && definition->operands[operand_index].value < function->value_count;
        }
        break;
    case IR_OPCODE_LABEL_ADDRESS:
        valid = false;
        break;
    default:
        valid = true;
        break;
    }
    return valid;
}

bool ir_label_metadata_transfer_valid(IrProgram* program, IrFunction* function, IrValueId value_id)
{
    bool valid = false;
    IrValue* result_slot = function && value_id.value < function->value_count ? function->values + value_id.value : 0;
    if (function && result_slot)
    {
        if (result_slot->definition.value >= function->instruction_count)
        {
            valid = true;
        }
        else
        {
            IrInstruction* definition = function->instructions + result_slot->definition.value;
            IrValue* first_slot = definition->operand_count && definition->operands && definition->operands[0].value < function->value_count
                                      ? function->values + definition->operands[0].value
                                      : 0;
            if (!function->label_metadata_count)
            {
                valid = ir_label_transfer_valid_without_metadata(program, function, definition, first_slot);
            }
            else
            {
                IrValueLabelMetadata result_metadata = ir_value_label_metadata(function, value_id);
                IrValueLabelMetadata* result = &result_metadata;
                IrValueLabelMetadata first_metadata =
                    first_slot ? ir_value_label_metadata(function, definition->operands[0]) : (IrValueLabelMetadata){0};
                IrValueLabelMetadata* first = first_slot ? &first_metadata : 0;
                switch (definition->opcode)
                {
            case IR_OPCODE_ADDRESS_OF:
                valid = ir_label_transfer_valid_address_of(result);
                break;
            case IR_OPCODE_LOAD:
            case IR_OPCODE_ATOMIC_LOAD:
                valid = ir_label_transfer_valid_load(program, first_slot, result, first);
                break;
            case IR_OPCODE_CAST:
                valid = ir_label_transfer_valid_cast(program, definition, result_slot, first_slot, result, first);
                break;
            case IR_OPCODE_FIELD:
                valid = ir_label_transfer_valid_field(program, definition, first_slot, result, first);
                break;
            case IR_OPCODE_INDEX:
                valid = ir_label_transfer_valid_index(program, function, definition, first_slot, result, first);
                break;
            case IR_OPCODE_DEREFERENCE:
                valid = ir_label_transfer_valid_dereference(result, first);
                break;
            case IR_OPCODE_ARRAY:
            case IR_OPCODE_AGGREGATE:
                valid = ir_label_transfer_valid_aggregate(program, function, definition, result);
                break;
            case IR_OPCODE_LABEL_ADDRESS:
                valid = ir_label_transfer_valid_label_address(program, definition, result);
                break;
            case IR_OPCODE_LOCAL:
            case IR_OPCODE_GLOBAL:
                valid = ir_label_transfer_valid_local(result);
                break;
            default:
                valid = ir_label_transfer_valid_default(result);
                break;
                }
            }
        }
    }
    return valid;
}

bool ir_label_block_parameter_provenance_valid(IrFunction* function, IrBlockParameter* parameter)
{
    if (!function || !parameter || parameter->value.value >= function->value_count)
    {
        return false;
    }
    IrValueLabelMetadata destination_metadata = ir_value_label_metadata(function, parameter->value);
    IrValueLabelMetadata* destination = &destination_metadata;
    bool incoming_non_label = false;
    bool incoming_label = false;
    bool all_incoming_pure_labels = true;
    bool incoming_paths = false;
    u32 incoming_block_count = 0;
    for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
    {
        if (incoming->value.value >= function->value_count)
        {
            return false;
        }
        IrValueLabelMetadata source_metadata = ir_value_label_metadata(function, incoming->value);
        IrValueLabelMetadata* source = &source_metadata;
        bool source_non_label = source->has_non_label_provenance;
        for (u32 path_index = 0; path_index < source->label_path_count; path_index += 1)
        {
            source_non_label |= source->label_paths[path_index].is_non_label;
        }
        bool source_label = source->is_label_value || source->has_label_provenance || source->label_block_count != 0 || source->label_path_count != 0;
        incoming_label |= source_label;
        incoming_non_label |= source_non_label;
        incoming_paths |= source->label_path_count != 0;
        all_incoming_pure_labels &= source->is_label_value && !source->has_label_provenance && !source_non_label && !source->label_path_count;
        for (u32 block_index = 0; block_index < source->label_block_count; block_index += 1)
        {
            bool found = false;
            for (IrIncoming* previous = parameter->first_incoming; previous && previous != incoming; previous = previous->next)
            {
                IrValueLabelMetadata previous_metadata = ir_value_label_metadata(function, previous->value);
                found |= previous->value.value < function->value_count && ir_label_block_set_contains(&previous_metadata, source->label_blocks[block_index]);
            }
            if (!found)
            {
                incoming_block_count += 1;
            }
        }
        for (u32 path_index = 0; path_index < source->label_path_count; path_index += 1)
        {
            IrLabelProvenancePath* source_path = source->label_paths + path_index;
            bool found = false;
            for (u32 destination_path_index = 0; destination_path_index < destination->label_path_count; destination_path_index += 1)
            {
                IrLabelProvenancePath* destination_path = destination->label_paths + destination_path_index;
                bool blocks_match = source_path->is_non_label ||
                                     (!destination_path->is_non_label && source_path->label_block_count <= destination_path->label_block_count);
                if (blocks_match && destination_path->offset == source_path->offset && destination_path->size == source_path->size)
                {
                    for (u32 block_index = 0; blocks_match && block_index < source_path->label_block_count; block_index += 1)
                    {
                        blocks_match = ir_label_path_contains_block(destination_path, source_path->label_blocks[block_index]);
                    }
                    found |= blocks_match;
                }
            }
            if (!found)
            {
                return false;
            }
        }
    }
    bool destination_has_labels = destination->label_block_count != 0 || destination->is_label_value || destination->has_label_provenance || destination->label_path_count != 0;
    bool destination_non_label = destination->has_non_label_provenance;
    if (destination->is_label_value)
    {
        if (!all_incoming_pure_labels || destination_non_label || destination->has_label_provenance || destination->label_path_count != 0)
        {
            return false;
        }
    }
    else if (destination_has_labels != incoming_label || destination->has_label_provenance != (incoming_block_count != 0) ||
             destination_non_label != incoming_non_label)
    {
        return false;
    }
    if (destination->label_block_count != incoming_block_count)
    {
        return false;
    }
    for (u32 destination_block_index = 0; destination_block_index < destination->label_block_count; destination_block_index += 1)
    {
        bool found = false;
        for (IrIncoming* incoming = parameter->first_incoming; incoming && !found; incoming = incoming->next)
        {
            IrValueLabelMetadata source_metadata = ir_value_label_metadata(function, incoming->value);
            found = incoming->value.value < function->value_count && ir_label_block_set_contains(&source_metadata, destination->label_blocks[destination_block_index]);
        }
        if (!found)
        {
            return false;
        }
    }
    if (!incoming_paths && destination->label_path_count)
    {
        return false;
    }
    for (u32 destination_path_index = 0; destination_path_index < destination->label_path_count; destination_path_index += 1)
    {
        IrLabelProvenancePath* destination_path = destination->label_paths + destination_path_index;
        bool incoming_path = false;
        bool incoming_non_label_path = false;
        bool incoming_label_path = false;
        for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
        {
            IrValueLabelMetadata source = incoming->value.value < function->value_count ? ir_value_label_metadata(function, incoming->value)
                                                                                        : (IrValueLabelMetadata){0};
            for (u32 path_index = 0; path_index < source.label_path_count; path_index += 1)
            {
                IrLabelProvenancePath* source_path = source.label_paths + path_index;
                if (source_path->offset == destination_path->offset && source_path->size == destination_path->size)
                {
                    incoming_path = true;
                    incoming_non_label_path |= source_path->is_non_label;
                    incoming_label_path |= !source_path->is_non_label && source_path->label_block_count != 0;
                }
            }
        }
        if (!incoming_path || (destination_path->is_non_label ? !incoming_non_label_path || incoming_label_path : !incoming_label_path))
        {
            return false;
        }
        if (!destination_path->is_non_label)
        {
            for (u32 block_index = 0; block_index < destination_path->label_block_count; block_index += 1)
            {
                bool found = false;
                for (IrIncoming* incoming = parameter->first_incoming; incoming && !found; incoming = incoming->next)
                {
                    IrValueLabelMetadata source = incoming->value.value < function->value_count ? ir_value_label_metadata(function, incoming->value)
                                                                                                : (IrValueLabelMetadata){0};
                    for (u32 path_index = 0; path_index < source.label_path_count; path_index += 1)
                    {
                        IrLabelProvenancePath* source_path = source.label_paths + path_index;
                        found |= source_path->offset == destination_path->offset && source_path->size == destination_path->size && !source_path->is_non_label &&
                                 ir_label_path_contains_block(source_path, destination_path->label_blocks[block_index]);
                    }
                }
                if (!found)
                {
                    return false;
                }
            }
        }
    }
    return true;
}

bool ir_label_provenance_contains(IrValueLabelMetadata* value, IrBlockId block)
{
    if (ir_label_provenance_valid(value))
    {
        for (u32 index = 0; index < value->label_block_count; index += 1)
        {
            if (value->label_blocks[index].value == block.value)
            {
                return true;
            }
        }
    }

    return false;
}

void ir_label_provenance_union(Arena* arena, IrFunction* function, IrValueId destination_id, IrValueId source_id)
{
    if (arena && function && destination_id.value < function->value_count && source_id.value < function->value_count)
    {
        IrValueLabelMetadata source = ir_value_label_metadata(function, source_id);
        bool source_blocks_present = source.is_label_value && source.label_block_count && source.label_blocks;
        if (source.has_non_label_provenance || source_blocks_present)
        {
            IrValueLabelMetadata* destination = ir_value_label_metadata_ensure(arena, function, destination_id);
            if (destination)
            {
                destination->has_non_label_provenance |= source.has_non_label_provenance;
                if (!source_blocks_present)
                {
                    return;
                }
                u32 existing_count = destination->is_label_value && destination->label_blocks ? destination->label_block_count : 0;
                u32 capacity = existing_count + source.label_block_count;
                IrBlockId* blocks = arena_allocate(arena, IrBlockId, capacity);
                u32 count = 0;
                for (u32 index = 0; index < existing_count; index += 1)
                {
                    blocks[count++] = destination->label_blocks[index];
                }
                for (u32 source_index = 0; source_index < source.label_block_count; source_index += 1)
                {
                    IrBlockId block = source.label_blocks[source_index];
                    bool found = false;
                    for (u32 index = 0; index < count; index += 1)
                    {
                        found |= blocks[index].value == block.value;
                    }
                    if (!found)
                    {
                        blocks[count++] = block;
                    }
                }
                bool has_non_label = destination->has_non_label_provenance || source.has_non_label_provenance;
                destination->is_label_value = count != 0;
                destination->has_label_provenance = false;
                destination->has_non_label_provenance = has_non_label;
                destination->label_blocks = blocks;
                destination->label_block_count = count;
                destination->label_paths = 0;
                destination->label_path_count = 0;
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void ir_label_storage_union_source(Arena* arena, IrFunction* function, IrValueId destination_id, IrValueId source_id);

void ir_label_provenance_copy(Arena* arena, IrFunction* function, IrValueId destination_id, IrValueId source_id)
{
    if (function && destination_id.value < function->value_count)
    {
        IrValueLabelMetadata* existing = ir_value_label_metadata_find(function, destination_id);
        if (existing)
        {
            *existing = (IrValueLabelMetadata){0};
        }
        if (source_id.value >= function->value_count)
        {
            return;
        }
        IrValueLabelMetadata source = ir_value_label_metadata(function, source_id);
        if (source.has_non_label_provenance)
        {
            IrValueLabelMetadata* destination = arena ? ir_value_label_metadata_ensure(arena, function, destination_id) : existing;
            if (destination)
            {
                destination->has_non_label_provenance = true;
            }
        }
        if (source.has_label_provenance || source.label_path_count)
        {
            ir_label_storage_union_source(arena, function, destination_id, source_id);
        }
        else
        {
            ir_label_provenance_union(arena, function, destination_id, source_id);
        }
    }
}

BUSTER_GLOBAL_LOCAL void ir_label_storage_path_append(Arena* arena, IrValueLabelMetadata* destination, IrLabelProvenancePath* source_path)
{
    if (arena && destination && source_path)
    {
        for (u32 path_index = 0; path_index < destination->label_path_count; path_index += 1)
        {
            IrLabelProvenancePath* destination_path = destination->label_paths + path_index;
            if (destination_path->offset != source_path->offset || destination_path->size != source_path->size)
            {
                continue;
            }
            if (destination_path->is_non_label && source_path->is_non_label)
            {
                return;
            }
            if (destination_path->is_non_label != source_path->is_non_label)
            {
                destination->has_non_label_provenance = true;
                if (destination_path->is_non_label)
                {
                    destination_path->is_non_label = false;
                    destination_path->label_blocks = arena_allocate(arena, IrBlockId, source_path->label_block_count);
                    destination_path->label_block_count = 0;
                }
            }
            u32 existing_count = destination_path->label_block_count;
            IrBlockId* merged = arena_allocate(arena, IrBlockId, existing_count + source_path->label_block_count);
            u32 merged_count = 0;
            for (u32 block_index = 0; block_index < existing_count; block_index += 1)
            {
                merged[merged_count++] = destination_path->label_blocks[block_index];
            }
            for (u32 block_index = 0; block_index < source_path->label_block_count; block_index += 1)
            {
                bool found = false;
                for (u32 merged_index = 0; merged_index < merged_count; merged_index += 1)
                {
                    found |= merged[merged_index].value == source_path->label_blocks[block_index].value;
                }
                if (!found)
                {
                    merged[merged_count++] = source_path->label_blocks[block_index];
                }
            }
            destination_path->label_blocks = merged_count ? merged : 0;
            destination_path->label_block_count = merged_count;
            return;
        }
        IrLabelProvenancePath* paths = arena_allocate(arena, IrLabelProvenancePath, destination->label_path_count + 1);
        for (u32 path_index = 0; path_index < destination->label_path_count; path_index += 1)
        {
            paths[path_index] = destination->label_paths[path_index];
        }
        paths[destination->label_path_count] = *source_path;
        destination->label_paths = paths;
        destination->label_path_count += 1;
    }
}

BUSTER_GLOBAL_LOCAL void ir_label_storage_union_source(Arena* arena, IrFunction* function, IrValueId destination_id, IrValueId source_id)
{
    if (arena && function && destination_id.value < function->value_count && source_id.value < function->value_count)
    {
        IrValueLabelMetadata source = ir_value_label_metadata(function, source_id);
        bool source_blocks_present = (source.is_label_value || source.has_label_provenance) && source.label_block_count && source.label_blocks;
        bool source_has_content = source.has_non_label_provenance || source.label_path_count || source_blocks_present;
        IrValueLabelMetadata* destination =
            source_has_content ? ir_value_label_metadata_ensure(arena, function, destination_id) : ir_value_label_metadata_find(function, destination_id);
        if (destination)
        {
            // Preserve ordinary-pointer alternatives even when the source contributes
            // no label blocks; otherwise a later load can become falsely label-only.
            destination->has_non_label_provenance |= source.has_non_label_provenance;
            for (u32 path_index = 0; path_index < source.label_path_count; path_index += 1)
            {
                IrLabelProvenancePath* source_path = source.label_paths + path_index;
                destination->has_non_label_provenance |= source_path->is_non_label;
                ir_label_storage_path_append(arena, destination, source_path);
            }
            if (!source_blocks_present)
            {
                if (destination->is_label_value && destination->has_non_label_provenance)
                {
                    destination->is_label_value = false;
                    destination->has_label_provenance = destination->label_block_count != 0;
                }
                return;
            }
            u32 existing_count = destination->has_label_provenance && destination->label_blocks ? destination->label_block_count
                                                                                                   : destination->is_label_value && destination->label_blocks
                                                                                                         ? destination->label_block_count
                                                                                                         : 0;
            IrBlockId* blocks = arena_allocate(arena, IrBlockId, existing_count + source.label_block_count);
            u32 count = 0;
            for (u32 index = 0; index < existing_count; index += 1)
            {
                blocks[count++] = destination->label_blocks[index];
            }
            for (u32 source_index = 0; source_index < source.label_block_count; source_index += 1)
            {
                IrBlockId block = source.label_blocks[source_index];
                bool found = false;
                for (u32 index = 0; index < count; index += 1)
                {
                    found |= blocks[index].value == block.value;
                }
                if (!found)
                {
                    blocks[count++] = block;
                }
            }
            destination->is_label_value = false;
            destination->has_label_provenance = count != 0;
            destination->label_blocks = blocks;
            destination->label_block_count = count;
        }
    }
}

void ir_label_storage_provenance_union(Arena* arena, IrFunction* function, IrValueId destination_id, IrValueId source_id)
{
    ir_label_storage_union_source(arena, function, destination_id, source_id);
}

void ir_label_storage_provenance_copy(Arena* arena, IrFunction* function, IrValueId destination_id, IrValueId source_id)
{
    if (!function || destination_id.value >= function->value_count)
    {
        return;
    }
    IrValueLabelMetadata* existing = ir_value_label_metadata_find(function, destination_id);
    if (existing)
    {
        *existing = (IrValueLabelMetadata){0};
    }
    ir_label_storage_union_source(arena, function, destination_id, source_id);
}

void ir_label_provenance_load(Arena* arena, IrFunction* function, IrValueId destination_id, IrValueId source_id)
{
    if (function && destination_id.value < function->value_count)
    {
        IrValueLabelMetadata* existing = ir_value_label_metadata_find(function, destination_id);
        if (existing)
        {
            *existing = (IrValueLabelMetadata){0};
        }
        if (source_id.value < function->value_count)
        {
            IrValueLabelMetadata source = ir_value_label_metadata(function, source_id);
            if (source.has_non_label_provenance)
            {
                IrValueLabelMetadata* non_label_destination = arena ? ir_value_label_metadata_ensure(arena, function, destination_id) : existing;
                if (non_label_destination)
                {
                    non_label_destination->has_non_label_provenance = true;
                }
            }
            if (arena && source.label_block_count && source.label_blocks)
            {
                bool scalar_label_paths = !source.has_non_label_provenance;
                for (u32 path_index = 0; scalar_label_paths && path_index < source.label_path_count; path_index += 1)
                {
                    IrLabelProvenancePath* path = source.label_paths + path_index;
                    scalar_label_paths = !path->is_non_label && path->offset == 0 && path->label_block_count != 0;
                }
                if (scalar_label_paths)
                {
                    IrValueLabelMetadata* destination = ir_value_label_metadata_ensure(arena, function, destination_id);
                    if (!destination)
                    {
                        return;
                    }
                    destination->is_label_value = true;
                    destination->label_blocks = arena_allocate(arena, IrBlockId, source.label_block_count);
                    memcpy(destination->label_blocks, source.label_blocks, sizeof(IrBlockId) * source.label_block_count);
                    destination->label_block_count = source.label_block_count;
                }
                else
                {
                    ir_label_storage_provenance_copy(arena, function, destination_id, source_id);
                }
            }
        }
    }
}

u32 ir_inline_assembly_label_operand_base(IrInstruction* instruction)
{
    if (!instruction || instruction->operand_count != instruction->immediate_count ||
        (instruction->operand_count && !instruction->immediates))
    {
        return UINT32_MAX;
    }
    u64 result = instruction->operand_count;
    for (u32 index = 0; index < instruction->operand_count; index += 1)
    {
        u64 constraint = instruction->immediates[index];
        if ((constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) && (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE))
        {
            result += 1;
        }
    }
    return result > UINT32_MAX ? UINT32_MAX : (u32)result;
}

bool ir_inline_assembly_jump_target(IrFunction* function, IrInstruction* instruction, String8 literal, String8 prefix, u32* target_index_out)
{
    IrInstructionExtra extra = ir_instruction_extra(function, ir_instruction_self_id(function, instruction));
    if (!instruction || !target_index_out || !literal.pointer || !prefix.pointer || literal.length <= prefix.length || instruction->target_count < 2)
    {
        return false;
    }
    for (u64 index = 0; index < prefix.length; index += 1)
    {
        if (literal.pointer[index] != prefix.pointer[index])
        {
            return false;
        }
    }
    u64 suffix_start = prefix.length;
    u64 suffix_length = literal.length - suffix_start;
    u32 label_count = instruction->target_count - 1;
    if (suffix_length >= 3 && literal.pointer[suffix_start] == '[' && literal.pointer[literal.length - 1] == ']')
    {
        if (extra.label_name_count != label_count || !extra.label_names)
        {
            return false;
        }
        String8 name = {
            .pointer = literal.pointer + suffix_start + 1,
            .length = suffix_length - 2,
        };
        for (u32 label_index = 0; label_index < extra.label_name_count; label_index += 1)
        {
            if (string_equal(name, extra.label_names[label_index]))
            {
                *target_index_out = label_index + 1;
                return true;
            }
        }
        return false;
    }
    u32 operand_base = ir_inline_assembly_label_operand_base(instruction);
    if (operand_base == UINT32_MAX)
    {
        return false;
    }
    u64 operand_index = 0;
    for (u64 index = suffix_start; index < literal.length; index += 1)
    {
        u8 digit = (u8)literal.pointer[index];
        if (digit < '0' || digit > '9' || operand_index > (UINT64_MAX - (digit - '0')) / 10)
        {
            return false;
        }
        operand_index = operand_index * 10 + (digit - '0');
    }
    if (operand_index < operand_base)
    {
        return false;
    }
    u64 label_index = operand_index - operand_base;
    if (label_index >= label_count)
    {
        return false;
    }
    *target_index_out = 1 + (u32)label_index;
    return true;
}

IrAbiConvention ir_abi_convention_for_target(Target target)
{
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        return target.os == OPERATING_SYSTEM_WINDOWS || target.os == OPERATING_SYSTEM_UEFI ? IR_ABI_CONVENTION_WIN64_X86_64
                                                                                           : IR_ABI_CONVENTION_SYSTEMV_X86_64;
    }
    if (target.cpu_arch == CPU_ARCH_AARCH64)
    {
        if (target.os == OPERATING_SYSTEM_WINDOWS)
        {
            return IR_ABI_CONVENTION_WINDOWS_AARCH64;
        }
        if (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS)
        {
            return IR_ABI_CONVENTION_DARWIN_AARCH64;
        }
        return IR_ABI_CONVENTION_AAPCS64;
    }
    return IR_ABI_CONVENTION_SYSTEMV_X86_64;
}

IrProgram ir_program_initialize(Arena* arena, u32 module_count, u32 type_capacity, u32 symbol_capacity, u32 source_capacity)
{
    IrProgram program = {0};
    if (arena)
    {
        program.arena = arena;
        program.data_layout = target_data_layout(target_native);
        program.modules = arena_allocate(arena, IrModule, module_count);
        program.module_count = module_count;
        program.types = (IrTypeTable){
            .types = arena_allocate(arena, IrType, type_capacity),
            .capacity = type_capacity,
        };
        program.symbols = (IrSymbolTable){
            .symbols = arena_allocate(arena, IrSymbol, symbol_capacity),
            .capacity = symbol_capacity,
        };
        program.sources = (IrSourceTable){
            .sources = arena_allocate(arena, IrSource, source_capacity),
            .capacity = source_capacity,
        };
        for (u32 index = 0; index < module_count; index += 1)
        {
            program.modules[index] = (IrModule){0};
        }
    }

    return program;
}

typedef struct IrAbiClassificationTask IrAbiClassificationTask;
struct IrAbiClassificationTask
{
    IrTypeId type;
    u64 offset;
};

BUSTER_GLOBAL_LOCAL bool ir_system_v_abi_class_is_x87(IrAbiClass abi_class)
{
    return abi_class == IR_ABI_CLASS_X87 || abi_class == IR_ABI_CLASS_X87_UP;
}

// Merge one field's class into an eightbyte using the System V AMD64
// post-merge precedence.  The existing IR model has one SSE-like FLOAT class
// (rather than separate SSE/SSEUP classes); preserve that behavior while
// making x87 classes explicit.  An x87 class may only merge with the same
// x87 class.  Any other overlap is MEMORY, which also invalidates an X87_UP
// tail that no longer accompanies X87.
BUSTER_GLOBAL_LOCAL IrAbiClass ir_system_v_abi_class_merge(IrAbiClass left, IrAbiClass right)
{
    if (left == IR_ABI_CLASS_MEMORY || right == IR_ABI_CLASS_MEMORY)
    {
        return IR_ABI_CLASS_MEMORY;
    }
    if (left == IR_ABI_CLASS_NONE)
    {
        return right;
    }
    if (right == IR_ABI_CLASS_NONE)
    {
        return left;
    }
    if (left == right)
    {
        return left;
    }
    if (ir_system_v_abi_class_is_x87(left) || ir_system_v_abi_class_is_x87(right))
    {
        return IR_ABI_CLASS_MEMORY;
    }
    // Preserve the previous INTEGER-over-FLOAT merge for all existing
    // scalar/vector aggregates.  There are no SSEUP classes in this model.
    return IR_ABI_CLASS_INTEGER;
}

BUSTER_GLOBAL_LOCAL bool ir_system_v_abi_classes(IrProgram* program, IrTypeId root_type, IrAbiClass classes[2])
{
    IrType* root = ir_type_from_id(&program->types, root_type);
    bool result;
    if (!root || !root->layout.resolved || !root->layout.size || root->layout.size > 16)
    {
        result = false;
    }
    else
    {
        TemporalArena temporary = scratch_begin(0, 0);
        u32 capacity = BUSTER_MAX(program->types.count * 16, 16);
        IrAbiClassificationTask* tasks = arena_allocate(temporary.arena, IrAbiClassificationTask, capacity);
        u32 count = 1;
        tasks[0] = (IrAbiClassificationTask){
            .type = root_type,
        };
        bool valid = true;
        while (count && valid)
        {
            IrAbiClassificationTask task = tasks[--count];
            IrType* type = ir_type_from_id(&program->types, task.type);
            if (!type || !type->layout.resolved || task.offset + type->layout.size > 16 || (type->layout.alignment && task.offset % type->layout.alignment))
            {
                valid = false;
                break;
            }
            if (type->kind == IR_TYPE_STRUCT || type->kind == IR_TYPE_UNION)
            {
                if (count + type->field_count > capacity)
                {
                    valid = false;
                    break;
                }
                for (u32 index = 0; index < type->field_count; index += 1)
                {
                    IrField* field = type->fields + index;
                    tasks[count++] = (IrAbiClassificationTask){
                        .type = field->type,
                        .offset = task.offset + field->offset,
                    };
                }
                continue;
            }
            if (type->kind == IR_TYPE_ARRAY)
            {
                IrType* element = ir_type_from_id(&program->types, type->element_type);
                if (!element || !element->layout.resolved || type->element_count > capacity - count)
                {
                    valid = false;
                    break;
                }
                for (u64 index = 0; index < type->element_count; index += 1)
                {
                    tasks[count++] = (IrAbiClassificationTask){
                        .type = type->element_type,
                        .offset = task.offset + index * element->layout.size,
                    };
                }
                continue;
            }
            if (type->kind == IR_TYPE_FLOAT && type->bit_width == 80 && type->layout.size == 16)
            {
                // SysV's 80-bit long double occupies two eightbytes: X87 for the
                // value and X87_UP for the trailing storage/padding.  The layout
                // alignment check above requires the X87 value to begin on a
                // 16-byte boundary, as the target ABI does.
                u32 first = (u32)(task.offset / 8);
                if (first >= 2 || first + 1 >= 2)
                {
                    valid = false;
                    break;
                }
                classes[first] = ir_system_v_abi_class_merge(classes[first], IR_ABI_CLASS_X87);
                classes[first + 1] = ir_system_v_abi_class_merge(classes[first + 1], IR_ABI_CLASS_X87_UP);
                continue;
            }
            // GCC and clang deviate from a literal psABI reading for vectors
            // smaller than an eightbyte: a 1-, 2- or 4-byte vector is INTEGER
            // wherever it sits, so a struct wrapping one rides a
            // general-purpose register exactly as the bare vector does.
            IrAbiClass abi_class = type->kind == IR_TYPE_FLOAT || (type->kind == IR_TYPE_VECTOR && type->layout.size >= 8)
                                       ? IR_ABI_CLASS_FLOAT
                                       : IR_ABI_CLASS_INTEGER;
            if (type->kind == IR_TYPE_VECTOR && type->layout.size == 8 && type->element_count == 1)
            {
                // A single-lane double vector field carries GCC's and clang's
                // MEMORY class for <1 x double> into the aggregate: the whole
                // value goes to memory in both directions (clang returns the
                // wrapping struct through a hidden pointer even though the bare
                // vector returns in XMM0).
                IrType* element = ir_type_from_id(&program->types, type->element_type);
                if (element && element->kind == IR_TYPE_FLOAT && element->bit_width == 64)
                {
                    abi_class = IR_ABI_CLASS_MEMORY;
                }
            }
            bool scalar = type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_FLOAT || type->kind == IR_TYPE_POINTER ||
                          type->kind == IR_TYPE_FUNCTION || type->kind == IR_TYPE_VECTOR || type->kind == IR_TYPE_ENUM;
            if (!scalar)
            {
                valid = false;
                break;
            }
            u32 first = (u32)(task.offset / 8);
            u32 last = (u32)((task.offset + BUSTER_MAX(type->layout.size, (u64)1) - 1) / 8);
            for (u32 part = first; part <= last; part += 1)
            {
                if (part >= 2)
                {
                    valid = false;
                    break;
                }
                classes[part] = ir_system_v_abi_class_merge(classes[part], abi_class);
            }
        }
        scratch_end(temporary);
        result = valid;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool ir_homogeneous_float_abi(IrProgram* program, IrTypeId root_type, IrTypeId* element_out, u32* count_out)
{
    TemporalArena temporary = scratch_begin(0, 0);
    u32 capacity = BUSTER_MAX(program->types.count * 16, 16);
    IrTypeId* tasks = arena_allocate(temporary.arena, IrTypeId, capacity);
    u32 task_count = 1;
    u32 count = 0;
    IrTypeId element = IR_TYPE_ID_INVALID;
    tasks[0] = root_type;
    bool valid = true;
    while (task_count && valid)
    {
        IrTypeId type_id = tasks[--task_count];
        IrType* type = ir_type_from_id(&program->types, type_id);
        if (!type)
        {
            valid = false;
        }
        else if (type->kind == IR_TYPE_FLOAT)
        {
            if (element.value != IR_ID_UNDERLYING_INVALID && element.value != type_id.value)
            {
                valid = false;
                break;
            }
            element = type_id;
            count += 1;
            if (count > IR_ABI_MAX_PARTS)
            {
                valid = false;
            }
        }
        else if (type->kind == IR_TYPE_ARRAY)
        {
            if (!type->element_count || type->element_count > capacity - task_count)
            {
                valid = false;
                break;
            }
            for (u64 index = 0; index < type->element_count; index += 1)
            {
                tasks[task_count++] = type->element_type;
            }
        }
        else if (type->kind == IR_TYPE_STRUCT && type->field_count && type->field_count <= capacity - task_count)
        {
            for (u32 index = 0; index < type->field_count; index += 1)
            {
                tasks[task_count++] = type->fields[index].type;
            }
        }
        else
        {
            valid = false;
        }
    }
    scratch_end(temporary);
    IrType* root = ir_type_from_id(&program->types, root_type);
    IrType* element_type = element.value != IR_ID_UNDERLYING_INVALID ? ir_type_from_id(&program->types, element) : 0;
    // Padding disqualifies: clang only treats the aggregate as homogeneous
    // when its size is exactly the members' — `{ _Alignas(8) float a, b; }`
    // classifies as an integer pair, not an HFA, and both sides of a call
    // must agree with that reading. A gap-free homogeneous aggregate also
    // means every member sits at its packed offset, so the parts the
    // consumer builds below describe the real layout.
    if (valid && count && (!root || !element_type || !root->layout.resolved || root->layout.size != (u64)count * element_type->layout.size))
    {
        valid = false;
    }
    bool result;
    if (!valid || !count)
    {
        result = false;
    }
    else
    {
        *element_out = element;
        *count_out = count;
        result = true;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL IrAbiValue ir_classify_abi_value(IrProgram* program, IrTypeId type_id, IrAbiConvention convention, bool is_result,
                                                      bool variadic_argument)
{
    IrAbiValue value = {0};
    IrType* type = ir_type_from_id(&program->types, type_id);
    if (type && type->layout.resolved && convention < IR_ABI_CONVENTION_COUNT)
    {
        u64 size = type->layout.size;
        bool aggregate = type->kind == IR_TYPE_STRUCT || type->kind == IR_TYPE_UNION || type->kind == IR_TYPE_ARRAY || type->kind == IR_TYPE_VA_LIST;
        if (type->kind != IR_TYPE_VOID)
        {
            if (!aggregate && type->kind != IR_TYPE_VECTOR)
            {
                if (type->kind == IR_TYPE_FLOAT)
                {
                    if (convention == IR_ABI_CONVENTION_SYSTEMV_X86_64 && type->bit_width == 80 && size == 16)
                    {
                        if (is_result)
                        {
                            // A scalar long double returns directly in ST0.  Keep
                            // both eightbyte classes in the neutral ABI model so a
                            // consumer can validate the x87 result shape without
                            // inventing a hidden result pointer.
                            value.part_count = 2;
                            value.parts[0] = (IrAbiPart){
                                .abi_class = IR_ABI_CLASS_X87,
                                .value_offset = 0,
                                .size = 8,
                            };
                            value.parts[1] = (IrAbiPart){
                                .abi_class = IR_ABI_CLASS_X87_UP,
                                .value_offset = 8,
                                .size = 8,
                            };
                        }
                        else
                        {
                            // SysV arguments (including variadic arguments) carry
                            // long double by value in a 16-byte-aligned memory slot;
                            // this is not an indirect/sret pointer argument.
                            value.part_count = 1;
                            value.memory = true;
                            value.parts[0] = (IrAbiPart){
                                .abi_class = IR_ABI_CLASS_MEMORY,
                                .size = (u32)size,
                            };
                        }
                        return value;
                    }
                    if (type->bit_width > 64)
                    {
                        value.part_count = 1;
                        value.indirect = is_result;
                        value.memory = !is_result;
                        value.parts[0] = (IrAbiPart){
                            .abi_class = is_result ? IR_ABI_CLASS_POINTER : IR_ABI_CLASS_MEMORY,
                            .size = is_result ? 8 : (u32)size,
                        };
                    }
                    else
                    {
                        value.part_count = 1;
                        value.parts[0] = (IrAbiPart){
                            .abi_class = convention == IR_ABI_CONVENTION_WINDOWS_AARCH64 && variadic_argument ? IR_ABI_CLASS_INTEGER : IR_ABI_CLASS_FLOAT,
                            .size = (u32)size,
                        };
                    }
                    return value;
                }
                if (type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_FUNCTION)
                {
                    value.part_count = 1;
                    value.parts[0] = (IrAbiPart){.abi_class = IR_ABI_CLASS_POINTER, .size = (u32)size};
                    return value;
                }
                if (type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_ENUM)
                {
                    if (size <= 8)
                    {
                        value.part_count = 1;
                        value.parts[0] = (IrAbiPart){.abi_class = IR_ABI_CLASS_INTEGER, .size = (u32)size};
                        return value;
                    }
                    if (size <= 16 && convention != IR_ABI_CONVENTION_WIN64_X86_64)
                    {
                        value.part_count = (u32)((size + 7) / 8);
                        for (u32 part = 0; part < value.part_count; part += 1)
                        {
                            value.parts[part] = (IrAbiPart){
                                .abi_class = IR_ABI_CLASS_INTEGER,
                                .value_offset = part * 8,
                                .size = (u32)BUSTER_MIN((u64)8, size - (u64)part * 8),
                            };
                        }
                        return value;
                    }
                    if (convention == IR_ABI_CONVENTION_WIN64_X86_64 && size == 16 && is_result)
                    {
                        // Win64 __int128 follows clang (MSVC has none, so
                        // clang's choice is the de-facto ABI): the argument
                        // goes by reference like any Win64 value wider than
                        // eight bytes -- the indirect shape below -- and the
                        // result comes back by value in XMM0.
                        value.part_count = 1;
                        value.parts[0] = (IrAbiPart){
                            .abi_class = IR_ABI_CLASS_VECTOR,
                            .size = 16,
                        };
                        return value;
                    }
                    value.part_count = 1;
                    value.indirect = true;
                    value.parts[0] = (IrAbiPart){
                        .abi_class = IR_ABI_CLASS_POINTER,
                        .size = 8,
                    };
                    return value;
                }
                return value;
            }
            if (type->kind == IR_TYPE_VECTOR)
            {
                bool aarch64 = convention == IR_ABI_CONVENTION_AAPCS64 || convention == IR_ABI_CONVENTION_DARWIN_AARCH64 ||
                               convention == IR_ABI_CONVENTION_WINDOWS_AARCH64;
                if (convention == IR_ABI_CONVENTION_WIN64_X86_64)
                {
                    // Win64 vector shapes follow clang; MSVC has no generic
                    // vector extension, so clang's answers are the de-facto
                    // ABI (measured against clang 22 by tests/c_abi_*):
                    //   - A single-lane vector travels like its scalar
                    //     element in both directions: integer lanes ride the
                    //     positional GPR, float lanes the positional XMM
                    //     register.
                    //   - A multi-lane vector argument goes by reference to a
                    //     sixteen-byte-aligned temporary at every width
                    //     (clang's callee loads it with MOVDQA even below
                    //     sixteen bytes, so the alignment is contractual).
                    //   - A multi-lane vector result comes back by value in
                    //     XMM0, YMM0, or ZMM0 at every width from 2 to 64
                    //     bytes, split across consecutive registers when the
                    //     model lacks the wide register -- the backend's
                    //     question, not this one. Past 64 bytes clang's
                    //     answer depends on the CPU model, which this
                    //     classification cannot see -- and the C frontend
                    //     refuses vector signature types past 64 bytes anyway
                    //     -- so those stay by reference.
                    //   - A width past the model's widest vector register
                    //     keeps the model-independent halves here and lets
                    //     the canonical backend finish the contract per
                    //     model: the argument's single reference becomes one
                    //     reference per register-sized piece
                    //     (codegen_canonical_x64_windows_vector_argument_pieces)
                    //     and the result's becomes up to four direct
                    //     registers (codegen_canonical_x64_windows_vector_result).
                    IrType* element = ir_type_from_id(&program->types, type->element_type);
                    if (type->element_count == 1 && element && size <= 8)
                    {
                        value.part_count = 1;
                        value.parts[0] = (IrAbiPart){
                            .abi_class = element->kind == IR_TYPE_FLOAT ? IR_ABI_CLASS_VECTOR : IR_ABI_CLASS_INTEGER,
                            .size = (u32)size,
                        };
                    }
                    else if (!is_result || size > 64)
                    {
                        value.part_count = 1;
                        value.indirect = true;
                        value.parts[0] = (IrAbiPart){
                            .abi_class = IR_ABI_CLASS_POINTER,
                            .size = 8,
                        };
                    }
                    else
                    {
                        value.part_count = 1;
                        value.parts[0] = (IrAbiPart){
                            .abi_class = IR_ABI_CLASS_VECTOR,
                            .size = (u32)size,
                        };
                    }
                    return value;
                }
                if ((aarch64 && size > 16) || (convention == IR_ABI_CONVENTION_SYSTEMV_X86_64 && size > 64))
                {
                    value.part_count = 1;
                    value.indirect = is_result;
                    value.memory = !is_result;
                    value.parts[0] = (IrAbiPart){
                        .abi_class = is_result ? IR_ABI_CLASS_POINTER : IR_ABI_CLASS_MEMORY,
                        .size = is_result ? 8 : (u32)size,
                    };
                    return value;
                }
                if (convention == IR_ABI_CONVENTION_WINDOWS_AARCH64 && variadic_argument)
                {
                    value.part_count = (u32)((size + 7) / 8);
                    for (u32 part = 0; part < value.part_count; part += 1)
                    {
                        value.parts[part] = (IrAbiPart){
                            .abi_class = IR_ABI_CLASS_INTEGER,
                            .value_offset = part * 8,
                            .size = (u32)BUSTER_MIN((u64)8, size - (u64)part * 8),
                        };
                    }
                    return value;
                }
                if (convention == IR_ABI_CONVENTION_SYSTEMV_X86_64 && variadic_argument && size > 16)
                {
                    value.part_count = 1;
                    value.memory = true;
                    value.parts[0] = (IrAbiPart){
                        .abi_class = IR_ABI_CLASS_MEMORY,
                        .size = (u32)size,
                    };
                    return value;
                }
                if (convention == IR_ABI_CONVENTION_SYSTEMV_X86_64 && size < 8)
                {
                    // GCC passes 1-, 2- and 4-byte vectors in general-purpose
                    // registers and clang follows, so INTEGER is the convention
                    // here; it is also what lets the canonical emitter carry the
                    // part, whose vector moves start at four bytes.
                    value.part_count = 1;
                    value.parts[0] = (IrAbiPart){.abi_class = IR_ABI_CLASS_INTEGER, .size = (u32)size};
                    return value;
                }
                if (convention == IR_ABI_CONVENTION_SYSTEMV_X86_64 && !is_result && size == 8 && type->element_count == 1)
                {
                    // GCC passes a single-lane double vector argument in memory
                    // (an MMX-era shape that survived into the de-facto ABI) and
                    // clang follows; the bare value still returns in XMM0, so
                    // only the argument direction leaves the vector default. A
                    // wrapping aggregate goes to memory in both directions
                    // instead, which the aggregate walk models by giving the
                    // field a MEMORY class.
                    IrType* element = ir_type_from_id(&program->types, type->element_type);
                    if (element && element->kind == IR_TYPE_FLOAT && element->bit_width == 64)
                    {
                        value.part_count = 1;
                        value.memory = true;
                        value.parts[0] = (IrAbiPart){
                            .abi_class = IR_ABI_CLASS_MEMORY,
                            .size = (u32)size,
                        };
                        return value;
                    }
                }
                value.part_count = 1;
                value.parts[0] = (IrAbiPart){.abi_class = IR_ABI_CLASS_VECTOR, .size = (u32)size};
                return value;
            }
            if (convention == IR_ABI_CONVENTION_WIN64_X86_64)
            {
                value.part_count = 1;
                if (size == 1 || size == 2 || size == 4 || size == 8)
                {
                    value.parts[0] = (IrAbiPart){.abi_class = IR_ABI_CLASS_INTEGER, .size = (u32)size};
                }
                else
                {
                    value.indirect = true;
                    value.parts[0] = (IrAbiPart){.abi_class = IR_ABI_CLASS_POINTER, .size = 8};
                }
                return value;
            }
            bool aarch64 = convention == IR_ABI_CONVENTION_AAPCS64 || convention == IR_ABI_CONVENTION_DARWIN_AARCH64 ||
                           convention == IR_ABI_CONVENTION_WINDOWS_AARCH64;
            if (aarch64)
            {
                IrTypeId element = IR_TYPE_ID_INVALID;
                u32 count = 0;
                if (!(convention == IR_ABI_CONVENTION_WINDOWS_AARCH64 && variadic_argument) && ir_homogeneous_float_abi(program, type_id, &element, &count))
                {
                    IrType* element_type = ir_type_from_id(&program->types, element);
                    value.part_count = count;
                    for (u32 part = 0; part < count; part += 1)
                    {
                        value.parts[part] = (IrAbiPart){
                            .abi_class = IR_ABI_CLASS_FLOAT,
                            .value_offset = part * (u32)element_type->layout.size,
                            .size = (u32)element_type->layout.size,
                        };
                    }
                }
                else if (size <= 16)
                {
                    value.part_count = (u32)((size + 7) / 8);
                    for (u32 part = 0; part < value.part_count; part += 1)
                    {
                        value.parts[part] = (IrAbiPart){
                            .abi_class = IR_ABI_CLASS_INTEGER,
                            .value_offset = part * 8,
                            .size = (u32)BUSTER_MIN((u64)8, size - (u64)part * 8),
                        };
                    }
                }
                else
                {
                    value.part_count = 1;
                    value.indirect = true;
                    value.parts[0] = (IrAbiPart){.abi_class = IR_ABI_CLASS_POINTER, .size = 8};
                }
                return value;
            }
            if (size > 16)
            {
                value.part_count = 1;
                value.indirect = is_result;
                value.memory = !is_result;
                value.parts[0] = (IrAbiPart){
                    .abi_class = is_result ? IR_ABI_CLASS_POINTER : IR_ABI_CLASS_MEMORY,
                    .size = is_result ? 8 : (u32)size,
                };
                return value;
            }
            IrAbiClass classes[2] = {0};
            if (!ir_system_v_abi_classes(program, type_id, classes))
            {
                value.part_count = 1;
                value.indirect = is_result;
                value.memory = !is_result;
                value.parts[0] = (IrAbiPart){
                    .abi_class = is_result ? IR_ABI_CLASS_POINTER : IR_ABI_CLASS_MEMORY,
                    .size = is_result ? 8 : (u32)size,
                };
                return value;
            }
            bool has_x87 = false;
            bool has_memory = false;
            for (u32 part = 0; part < 2; part += 1)
            {
                has_x87 |= ir_system_v_abi_class_is_x87(classes[part]);
                has_memory |= classes[part] == IR_ABI_CLASS_MEMORY;
            }
            if (has_memory || (has_x87 && !is_result))
            {
                // X87/X87_UP arguments are memory-class values in SysV, as are
                // aggregates whose fields merged incompatibly with an x87 class.
                // Keep `indirect` clear for arguments: only a result uses a hidden
                // pointer when the aggregate cannot be returned directly.
                value.part_count = 1;
                value.indirect = is_result;
                value.memory = !is_result;
                value.parts[0] = (IrAbiPart){
                    .abi_class = is_result ? IR_ABI_CLASS_POINTER : IR_ABI_CLASS_MEMORY,
                    .size = is_result ? 8 : (u32)size,
                };
                return value;
            }
            if (has_x87 && (classes[0] != IR_ABI_CLASS_X87 || classes[1] != IR_ABI_CLASS_X87_UP || size != 16))
            {
                // X87_UP is meaningful only as the second half of the canonical
                // long-double pair.  A malformed/incompatible aggregate is therefore
                // returned indirectly rather than exposing a register shape that no
                // SysV caller can consume.
                value.part_count = 1;
                value.indirect = is_result;
                value.memory = !is_result;
                value.parts[0] = (IrAbiPart){
                    .abi_class = is_result ? IR_ABI_CLASS_POINTER : IR_ABI_CLASS_MEMORY,
                    .size = is_result ? 8 : (u32)size,
                };
                return value;
            }
            value.part_count = (u32)((size + 7) / 8);
            for (u32 part = 0; part < value.part_count; part += 1)
            {
                value.parts[part] = (IrAbiPart){
                    .abi_class = classes[part] == IR_ABI_CLASS_NONE ? IR_ABI_CLASS_INTEGER : classes[part],
                    .value_offset = part * 8,
                    .size = (u32)BUSTER_MIN((u64)8, size - (u64)part * 8),
                };
            }
        }
    }

    return value;
}

BUSTER_GLOBAL_LOCAL void ir_resolve_type_abi(IrProgram* program, IrTypeId type_id, IrAbiConvention convention)
{
    IrType* type = program ? ir_type_from_id(&program->types, type_id) : 0;
    if (!type || !program->arena || convention >= IR_ABI_CONVENTION_COUNT || !type->layout.resolved)
    {
        return;
    }
    if (!type->abi)
    {
        type->abi = arena_allocate(program->arena, IrTypeAbi, 1);
        *type->abi = (IrTypeAbi){0};
    }
    type->abi->values[convention][IR_ABI_USE_ARGUMENT] = ir_classify_abi_value(program, type_id, convention, false, false);
    type->abi->values[convention][IR_ABI_USE_RESULT] = ir_classify_abi_value(program, type_id, convention, true, false);
    type->abi->values[convention][IR_ABI_USE_VARIADIC_ARGUMENT] =
        convention == IR_ABI_CONVENTION_WINDOWS_AARCH64 ? ir_classify_abi_value(program, type_id, convention, false, true)
                                                        : type->abi->values[convention][IR_ABI_USE_ARGUMENT];
    type->abi->resolved[convention] = true;
}

void ir_prepare_program_abi(IrProgram* program, IrAbiConvention convention)
{
    if (!program || convention >= IR_ABI_CONVENTION_COUNT)
    {
        return;
    }
    for (u32 type_index = 0; type_index < program->types.count; type_index += 1)
    {
        IrType* type = program->types.types + type_index;
        if (!type->abi || !type->abi->resolved[convention])
        {
            ir_resolve_type_abi(program, type->id, convention);
        }
    }
}

IrAbiValue ir_type_abi_value(IrProgram* program, IrTypeId type_id, IrAbiConvention convention, IrAbiUse use)
{
    IrType* type = program ? ir_type_from_id(&program->types, type_id) : 0;
    IrAbiValue result;
    if (!type || convention >= IR_ABI_CONVENTION_COUNT || use >= IR_ABI_USE_COUNT)
    {
        result = (IrAbiValue){0};
    }
    else
    {
        if (!type->abi || !type->abi->resolved[convention])
        {
            ir_resolve_type_abi(program, type_id, convention);
        }
        result = type->abi && type->abi->resolved[convention] ? type->abi->values[convention][use] : (IrAbiValue){0};
    }

    return result;
}

IrTypeId ir_program_add_type(IrProgram* program, IrType type)
{
    IrTypeId result;
    if (!program || program->types.count >= program->types.capacity)
    {
        result = IR_TYPE_ID_INVALID;
    }
    else
    {
        IrTypeId id = {
            .value = program->types.count++,
        };
        type.id = id;
        program->types.types[id.value] = type;
        result = id;
    }

    return result;
}

IrSymbolId ir_program_add_symbol(IrProgram* program, IrSymbol symbol)
{
    IrSymbolId result;
    if (!program || program->symbols.count >= program->symbols.capacity)
    {
        result = IR_SYMBOL_ID_INVALID;
    }
    else
    {
        IrSymbolId id = {
            .value = program->symbols.count++,
        };
        symbol.id = id;
        program->symbols.symbols[id.value] = symbol;
        result = id;
    }

    return result;
}

IrSourceId ir_program_add_source(IrProgram* program, IrSource source)
{
    IrSourceId result;
    if (!program || program->sources.count >= program->sources.capacity)
    {
        result = IR_SOURCE_ID_INVALID;
    }
    else
    {
        IrSourceId id = {
            .value = program->sources.count++,
        };
        source.id = id;
        program->sources.sources[id.value] = source;
        result = id;
    }

    return result;
}

IrFunction* ir_module_add_function(Arena* arena, IrModule* module, IrFunction function)
{
    IrFunction* result;
    if (!arena || !module)
    {
        result = 0;
    }
    else
    {
        if (module->function_count >= module->function_capacity)
        {
            u32 capacity = module->function_capacity ? module->function_capacity * 2 : 8;
            IrFunction* functions = arena_allocate(arena, IrFunction, capacity);
            if (module->function_count)
            {
                memcpy(functions, module->functions, sizeof(IrFunction) * module->function_count);
            }
            module->functions = functions;
            module->function_capacity = capacity;
        }
        function.id = (IrFunctionId){
            .value = module->function_count,
        };
        // A function built through this entry point takes its rows through
        // ir_function_add_instruction, which is what makes a cleared summary
        // bit mean the opcode is absent rather than merely unrecorded.
        function.opcode_summary |= IR_OPCODE_SUMMARY_KNOWN;
        module->functions[module->function_count++] = function;
        result = &module->functions[module->function_count - 1];
    }

    return result;
}

IrGlobal* ir_module_add_global(Arena* arena, IrModule* module, IrGlobal global)
{
    if (!arena || !module)
    {
        return 0;
    }
    if (module->global_count >= module->global_capacity)
    {
        u32 old_capacity = module->global_capacity;
        u32 capacity = old_capacity ? old_capacity * 2 : 8;
        IrGlobal* globals = arena_allocate(arena, IrGlobal, capacity);
        if (module->global_count)
        {
            memcpy(globals, module->globals, (u64)module->global_count * sizeof(*globals));
        }
        module->globals = globals;
        module->global_capacity = capacity;
    }
    IrGlobal* result = module->globals + module->global_count++;
    *result = global;
    // Counted here because this is where a global's relocations enter the
    // module and they are still hot from being written; the alternative is
    // every consumer of label provenance rediscovering the answer by walking
    // the whole global table per query.
    for (u32 relocation_index = 0; global.relocations && relocation_index < global.relocation_count; relocation_index += 1)
    {
        module->label_address_relocation_count += global.relocations[relocation_index].is_label_address ? 1 : 0;
    }
    return result;
}

IrBlock* ir_function_add_block(Arena* arena, IrFunction* function, IrBlock block)
{
    IrBlock* result;
    if (!arena || !function)
    {
        result = 0;
    }
    else
    {
        if (function->block_count >= function->block_capacity)
        {
            u32 capacity = function->block_capacity ? function->block_capacity * 2 : 8;
            IrBlock* blocks = arena_allocate(arena, IrBlock, capacity);
            if (function->block_count)
            {
                memcpy(blocks, function->blocks, sizeof(IrBlock) * function->block_count);
            }
            function->blocks = blocks;
            function->block_capacity = capacity;
        }
        block.id = (IrBlockId){
            .value = function->block_count,
        };
        function->blocks[function->block_count++] = block;
        result = &function->blocks[function->block_count - 1];
    }

    return result;
}

IrValueId ir_function_add_value(Arena* arena, IrFunction* function, IrValue value)
{
    IrValueId result;
    if (!arena || !function)
    {
        result = IR_VALUE_ID_INVALID;
    }
    else
    {
        if (function->value_count >= function->value_capacity)
        {
            u32 capacity = function->value_capacity ? function->value_capacity * 2 : 16;
            IrValue* values = arena_allocate(arena, IrValue, capacity);
            if (function->value_count)
            {
                memcpy(values, function->values, sizeof(IrValue) * function->value_count);
            }
            function->values = values;
            function->value_capacity = capacity;
        }
        IrValueId id = {
            .value = function->value_count++,
        };
        function->values[id.value] = value;
        result = id;
    }

    return result;
}

IrInstructionId ir_instruction_self_id(IrFunction* function, IrInstruction* instruction)
{
    IrInstructionId result;
    if (!function || !instruction || instruction < function->instructions || instruction >= function->instructions + function->instruction_count)
    {
        result = IR_INSTRUCTION_ID_INVALID;
    }
    else
    {
        result = (IrInstructionId){.value = (IrIdUnderlying)(instruction - function->instructions)};
    }

    return result;
}

bool ir_function_may_contain_opcodes(IrFunction* function, u64 mask)
{
    // Querying an opcode the summary never records would read as a
    // confident absence; the tracked list is the contract.
    BUSTER_CHECK((mask & ~(u64)IR_OPCODE_SUMMARY_TRACKED) == 0);
    return !function || !(function->opcode_summary & IR_OPCODE_SUMMARY_KNOWN) || (function->opcode_summary & mask) != 0;
}

IrInstructionId ir_function_add_instruction(Arena* arena, IrFunction* function, IrInstruction instruction, IrSourceRange canonical_source)
{
    IrInstructionId result;
    if (!arena || !function)
    {
        result = IR_INSTRUCTION_ID_INVALID;
    }
    else
    {
        if (function->instruction_count >= function->instruction_capacity)
        {
            u32 capacity = function->instruction_capacity ? function->instruction_capacity * 2 : 16;
            IrInstruction* instructions = arena_allocate(arena, IrInstruction, capacity);
            if (function->instruction_count)
            {
                memcpy(instructions, function->instructions, sizeof(IrInstruction) * function->instruction_count);
            }
            {
                IrSourceRange* canonical_sources = arena_allocate(arena, IrSourceRange, capacity);
                memset(canonical_sources, 0, sizeof(*canonical_sources) * capacity);
                if (function->instruction_canonical_sources)
                {
                    memcpy(canonical_sources, function->instruction_canonical_sources, sizeof(*canonical_sources) * function->instruction_count);
                }
                function->instruction_canonical_sources = canonical_sources;
            }
            function->instructions = instructions;
            function->instruction_capacity = capacity;
        }
        IrInstructionId id = {
            .value = function->instruction_count++,
        };
        function->instructions[id.value] = instruction;
        if ((IR_OPCODE_SUMMARY_TRACKED >> instruction.opcode) & 1)
        {
            function->opcode_summary |= IR_OPCODE_BIT(instruction.opcode);
        }
        if (function->instruction_canonical_sources)
        {
            function->instruction_canonical_sources[id.value] = canonical_source;
        }
        result = id;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL IrValidationResult ir_validation_error(IrValidationError error, IrFunction* function, IrBlockId block, IrInstructionId instruction)
{
    return (IrValidationResult){
        .error = error,
        .function = function->id,
        .block = block,
        .instruction = instruction,
    };
}

IrInstructionOwnership ir_function_instruction_owners(IrFunction* function, IrBlockId* owners)
{
    IrInstructionOwnership result = {
        .error = IR_VALIDATION_NONE,
        .block = IR_BLOCK_ID_INVALID,
        .instruction = IR_INSTRUCTION_ID_INVALID,
    };
    if (!function || (function->instruction_count && !owners))
    {
        result.error = IR_VALIDATION_INVALID_ID;
        return result;
    }
    // IR_ID_UNDERLYING_INVALID is UINT32_MAX, so the unowned marker is a byte
    // fill; the walk below is the only part that costs a pass.
    memset(owners, 0xff, sizeof(*owners) * function->instruction_count);
    u32 owned_count = 0;
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        IrInstructionId tail = IR_INSTRUCTION_ID_INVALID;
        IrInstructionId id = block->first_instruction;
        while (id.value != IR_ID_UNDERLYING_INVALID)
        {
            if (id.value >= function->instruction_count)
            {
                result.error = IR_VALIDATION_INVALID_ID;
                result.block = block->id;
                result.instruction = id;
                return result;
            }
            // Reaching an instruction twice is either a cycle in this chain or
            // a chain shared with an earlier block. Both give the instruction
            // two owners, and refusing the second visit is also what bounds
            // the walk: every step past this point consumes a fresh id, so the
            // whole function costs one pass rather than a per-block counter
            // that only notices a cycle after re-walking every instruction.
            if (owners[id.value].value != IR_ID_UNDERLYING_INVALID)
            {
                result.error = IR_VALIDATION_INSTRUCTION_OWNERSHIP;
                result.block = block->id;
                result.instruction = id;
                return result;
            }
            owners[id.value] = block->id;
            owned_count += 1;
            tail = id;
            id = function->instructions[id.value].next;
        }
        // last_instruction is what the appenders extend and what the emitters
        // treat as the terminator slot, so a value the chain never reaches
        // would let both walk different instruction sequences.
        if (tail.value != block->last_instruction.value)
        {
            result.error = IR_VALIDATION_INSTRUCTION_OWNERSHIP;
            result.block = block->id;
            result.instruction = block->last_instruction;
            return result;
        }
    }
    if (owned_count != function->instruction_count)
    {
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            if (owners[instruction_index].value == IR_ID_UNDERLYING_INVALID)
            {
                result.error = IR_VALIDATION_INSTRUCTION_OWNERSHIP;
                result.instruction = (IrInstructionId){
                    .value = instruction_index,
                };
                return result;
            }
        }
    }
    return result;
}

// Runs the ownership proof over every lowered function ahead of the
// per-instruction checks, so those can walk `next` without a cycle guard and
// can trust that block->last_instruction really terminates its chain. One
// scratch array sized to the largest function serves the whole module.
BUSTER_GLOBAL_LOCAL IrValidationResult ir_validate_module_ownership(IrModule* module)
{
    IrValidationResult result = {
        .function = IR_FUNCTION_ID_INVALID,
        .block = IR_BLOCK_ID_INVALID,
        .instruction = IR_INSTRUCTION_ID_INVALID,
    };
    u32 capacity = 0;
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        if (function->state == IR_FUNCTION_LOWERED)
        {
            capacity = BUSTER_MAX(capacity, function->instruction_count);
        }
    }
    TemporalArena scratch = scratch_begin(0, 0);
    IrBlockId* owners = arena_allocate(scratch.arena, IrBlockId, capacity);
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        if (function->state != IR_FUNCTION_LOWERED)
        {
            continue;
        }
        IrInstructionOwnership ownership = ir_function_instruction_owners(function, owners);
        if (ownership.error != IR_VALIDATION_NONE)
        {
            result = ir_validation_error(ownership.error, function, ownership.block, ownership.instruction);
            break;
        }
    }
    scratch_end(scratch);
    return result;
}

BUSTER_GLOBAL_LOCAL bool ir_canonical_conversion_valid(IrType* source, IrType* destination, IrConversionOperation operation)
{
    if (source && destination)
    {
        if (source->id.value == destination->id.value)
        {
            return operation == IR_CONVERSION_IDENTITY;
        }
        if (source->kind == IR_TYPE_BOOLEAN && destination->kind == IR_TYPE_INTEGER)
        {
            return operation == IR_CONVERSION_INTEGER_ZERO_EXTEND;
        }
        if (source->kind == IR_TYPE_INTEGER && destination->kind == IR_TYPE_INTEGER)
        {
            IrConversionOperation expected = source->bit_width < destination->bit_width
                                                 ? (source->is_signed ? IR_CONVERSION_INTEGER_SIGN_EXTEND : IR_CONVERSION_INTEGER_ZERO_EXTEND)
                                             : source->bit_width > destination->bit_width ? IR_CONVERSION_INTEGER_TRUNCATE
                                                                                          : IR_CONVERSION_INTEGER_REINTERPRET;
            return operation == expected;
        }
        if (source->kind == IR_TYPE_FLOAT && destination->kind == IR_TYPE_FLOAT)
        {
            return operation == (source->bit_width < destination->bit_width   ? IR_CONVERSION_FLOAT_EXTEND
                                 : source->bit_width > destination->bit_width ? IR_CONVERSION_FLOAT_TRUNCATE
                                                                              : IR_CONVERSION_IDENTITY);
        }
        if (source->kind == IR_TYPE_INTEGER && destination->kind == IR_TYPE_FLOAT)
        {
            return operation == (source->is_signed ? IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT : IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT);
        }
        if (source->kind == IR_TYPE_FLOAT && destination->kind == IR_TYPE_INTEGER)
        {
            return operation == (destination->is_signed ? IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER : IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER);
        }
        if (source->kind == IR_TYPE_POINTER && destination->kind == IR_TYPE_POINTER)
        {
            return operation == IR_CONVERSION_POINTER_REINTERPRET;
        }
        if (source->kind == IR_TYPE_POINTER && destination->kind == IR_TYPE_INTEGER)
        {
            return operation == IR_CONVERSION_POINTER_TO_INTEGER;
        }
        if (source->kind == IR_TYPE_INTEGER && destination->kind == IR_TYPE_POINTER)
        {
            return operation == IR_CONVERSION_INTEGER_TO_POINTER;
        }
    }

    return false;
}

BUSTER_GLOBAL_LOCAL bool ir_canonical_float_constant_valid(IrType* type, IrInstruction* instruction)
{
    if (!type || !instruction || type->kind != IR_TYPE_FLOAT || instruction->operand_count != 0 || instruction->target_count != 0 ||
        instruction->result.value == IR_ID_UNDERLYING_INVALID || !instruction->immediates)
    {
        return false;
    }
    if (type->bit_width == 80)
    {
        // The IR carries only the ten semantic x87 bytes.  The six ABI
        // padding bytes are materialized by the interpreter/runtime and are
        // never accepted as an additional immediate or payload bits.
        return type->layout.resolved && type->layout.size == 16 && type->layout.alignment == 16 && instruction->immediate_count == 2 &&
               (instruction->immediates[1] & ~UINT64_C(0xffff)) == 0;
    }
    return (type->bit_width == 32 || type->bit_width == 64) && instruction->immediate_count == 1;
}

BUSTER_GLOBAL_LOCAL IrValidationResult ir_validation_ok(void)
{
    return (IrValidationResult){
        .error = IR_VALIDATION_NONE,
        .function = IR_FUNCTION_ID_INVALID,
        .block = IR_BLOCK_ID_INVALID,
        .instruction = IR_INSTRUCTION_ID_INVALID,
    };
}

// One global's alignment, initializer, and relocation table. Nothing here names
// a function, a block or an instruction, so the caller keeps the invalid ids the
// module-level result already carries and only the error kind travels back.
BUSTER_GLOBAL_LOCAL IrValidationError ir_validate_global(IrProgram* program, IrModule* module, IrGlobal* global)
{
    IrValidationError error = IR_VALIDATION_NONE;
    IrSymbol* symbol = ir_symbol_from_id(&program->symbols, global->symbol);
    IrType* type = ir_type_from_id(&program->types, global->type);
    if (type && global->alignment &&
        ((global->alignment & (global->alignment - 1)) || !type->layout.resolved || global->alignment < type->layout.alignment))
    {
        error = IR_VALIDATION_ALIGNMENT;
    }
    else
    {
        bool initializer_valid = false;
        if (symbol && type && type->layout.resolved && symbol->kind == IR_SYMBOL_DATA && symbol->is_definition && symbol->type.value == global->type.value)
        {
            switch (global->initializer_kind)
            {
            case IR_GLOBAL_INITIALIZER_ZERO:
            {
                initializer_valid = true;
            }
            break;
            case IR_GLOBAL_INITIALIZER_INTEGER:
            {
                initializer_valid = type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_ENUM;
            }
            break;
            case IR_GLOBAL_INITIALIZER_FLOAT:
            {
                // The compact initializer_bits field carries only one u64;
                // f80 globals must use an explicit 16-byte BYTES initializer
                // until the frontend/codegen can carry their full payload.
                initializer_valid = type->kind == IR_TYPE_FLOAT && (type->bit_width == 32 || type->bit_width == 64);
            }
            break;
            case IR_GLOBAL_INITIALIZER_BYTES:
            {
                initializer_valid = global->bytes.length == type->layout.size;
            }
            break;
            case IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS:
            {
                initializer_valid = type->kind == IR_TYPE_POINTER && ir_symbol_from_id(&program->symbols, global->initializer_symbol);
            }
            break;
            case IR_GLOBAL_INITIALIZER_NONE:
            case IR_GLOBAL_INITIALIZER_COUNT:
            {
                initializer_valid = false;
            }
            break;
            }
        }
        if (!initializer_valid)
        {
            error = IR_VALIDATION_OPERATION;
        }
        else if (global->relocation_count && !global->relocations)
        {
            error = IR_VALIDATION_OPERATION;
        }
        else
        {
            u64 pointer_size = program->data_layout.pointer.size;
            for (u32 relocation_index = 0; relocation_index < global->relocation_count && error == IR_VALIDATION_NONE; relocation_index += 1)
            {
                IrGlobalRelocation* relocation = global->relocations + relocation_index;
                IrSymbol* relocation_symbol = ir_symbol_from_id(&program->symbols, relocation->symbol);
                bool offset_valid = pointer_size != 0 && relocation->offset <= type->layout.size && pointer_size <= type->layout.size - relocation->offset;
                bool bytes_valid = global->bytes.pointer && global->bytes.length == type->layout.size && relocation->offset <= global->bytes.length &&
                                   pointer_size <= global->bytes.length - relocation->offset;
                bool overlap_free = true;
                for (u32 previous_index = 0; previous_index < relocation_index; previous_index += 1)
                {
                    IrGlobalRelocation* previous = global->relocations + previous_index;
                    bool previous_end_valid = previous->offset <= UINT64_MAX - pointer_size;
                    bool relocation_end_valid = relocation->offset <= UINT64_MAX - pointer_size;
                    overlap_free &= previous_end_valid && relocation_end_valid &&
                                    (previous->offset >= relocation->offset + pointer_size || relocation->offset >= previous->offset + pointer_size);
                }
                if (!relocation_symbol || !offset_valid || !bytes_valid || !overlap_free)
                {
                    error = IR_VALIDATION_OPERATION;
                }
                else if (relocation->is_label_address)
                {
                    IrFunction* owner = ir_module_function_for_symbol(module, relocation->symbol);
                    if (relocation_symbol->kind != IR_SYMBOL_FUNCTION || !relocation_symbol->is_definition || !owner ||
                        owner->state != IR_FUNCTION_LOWERED || relocation->label_block.value >= owner->block_count || relocation->addend != 0)
                    {
                        error = IR_VALIDATION_OPERATION;
                    }
                }
            }
        }
    }
    return error;
}

// Every value's type, label provenance and alignment. These faults name the
// instruction that defined the value rather than a block.
BUSTER_GLOBAL_LOCAL IrValidationResult ir_validate_function_values(IrProgram* program, IrFunction* function)
{
    IrValidationResult result = ir_validation_ok();
    for (u32 value_index = 0; value_index < function->value_count && result.error == IR_VALIDATION_NONE; value_index += 1)
    {
        IrValue* value = function->values + value_index;
        IrValueId value_id = {.value = value_index};
        IrType* value_type = ir_type_from_id(&program->types, value->canonical_type);
        if (!value_type || value->definition.value >= function->instruction_count)
        {
            result = ir_validation_error(IR_VALIDATION_INVALID_ID, function, IR_BLOCK_ID_INVALID, value->definition);
        }
        else
        {
            IrValueLabelMetadata metadata = ir_value_label_metadata(function, value_id);
            bool transfer_valid = ir_label_metadata_transfer_valid(program, function, value_id);
            bool shape_valid = ir_label_metadata_shape_valid(program, function, value_id);
            if ((metadata.is_label_value && !metadata.has_label_provenance && !metadata.has_non_label_provenance && !ir_label_provenance_valid(&metadata)) ||
                (metadata.has_label_provenance && !ir_label_storage_provenance_valid(&metadata)) || !transfer_valid || !shape_valid)
            {
                result = ir_validation_error(IR_VALIDATION_OPERATION, function, IR_BLOCK_ID_INVALID, value->definition);
            }
            else if (ir_label_provenance_valid(&metadata) || ir_label_storage_provenance_valid(&metadata))
            {
                for (u32 label_index = 0; label_index < metadata.label_block_count && result.error == IR_VALIDATION_NONE; label_index += 1)
                {
                    if (metadata.label_blocks[label_index].value >= function->block_count)
                    {
                        result = ir_validation_error(IR_VALIDATION_BRANCH_TARGET, function, IR_BLOCK_ID_INVALID, value->definition);
                    }
                }
            }
            if (result.error == IR_VALIDATION_NONE && value->alignment &&
                ((value->alignment & (value->alignment - 1)) || !value_type->layout.resolved || value->alignment < value_type->layout.alignment))
            {
                result = ir_validation_error(IR_VALIDATION_ALIGNMENT, function, IR_BLOCK_ID_INVALID, value->definition);
            }
        }
    }
    return result;
}

// A block's parameters against its predecessors: one incoming value per
// predecessor, in the same order, at the parameter's type.
BUSTER_GLOBAL_LOCAL IrValidationResult ir_validate_block_parameters(IrFunction* function, IrBlock* block)
{
    IrValidationResult result = ir_validation_ok();
    for (IrBlockParameter* parameter = block->first_parameter; parameter && result.error == IR_VALIDATION_NONE; parameter = parameter->next)
    {
        if (parameter->value.value >= function->value_count || parameter->incoming_count != block->predecessor_count ||
            function->values[parameter->value.value].canonical_type.value != parameter->canonical_type.value)
        {
            result = ir_validation_error(IR_VALIDATION_BLOCK_PARAMETER, function, block->id, IR_INSTRUCTION_ID_INVALID);
        }
        else
        {
            IrIncoming* incoming = parameter->first_incoming;
            IrPredecessor* predecessor = block->first_predecessor;
            while (incoming && predecessor && result.error == IR_VALIDATION_NONE)
            {
                if (incoming->predecessor.value != predecessor->block.value || incoming->value.value >= function->value_count ||
                    function->values[incoming->value.value].canonical_type.value != parameter->canonical_type.value)
                {
                    result = ir_validation_error(IR_VALIDATION_BLOCK_PARAMETER, function, block->id, IR_INSTRUCTION_ID_INVALID);
                }
                else
                {
                    incoming = incoming->next;
                    predecessor = predecessor->next;
                }
            }
            if (result.error == IR_VALIDATION_NONE && (incoming || predecessor || !ir_label_block_parameter_provenance_valid(function, parameter)))
            {
                result = ir_validation_error(IR_VALIDATION_BLOCK_PARAMETER, function, block->id, IR_INSTRUCTION_ID_INVALID);
            }
        }
    }
    return result;
}

// The per-opcode obligations: operand counts and types, immediates, targets and
// result shape. Every fault here names the instruction the caller is holding, so
// only the kind comes back.
BUSTER_GLOBAL_LOCAL IrValidationError ir_validate_instruction_operation(IrProgram* program, IrFunction* function, IrType* signature,
                                                                        IrInstruction* instruction)
{
    IrValidationError error = IR_VALIDATION_NONE;
    if (instruction->opcode == IR_OPCODE_ARGUMENT)
    {
        u64 argument_index = instruction->immediate_count == 1 ? instruction->immediates[0] : UINT64_MAX;
        if (argument_index >= signature->parameter_count || signature->parameter_types[argument_index].value != instruction->canonical_type.value ||
            instruction->operand_count != 0 || instruction->result.value == IR_ID_UNDERLYING_INVALID)
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_LOCAL)
    {
        IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
        if (!type || instruction->operand_count != 0 || instruction->result.value == IR_ID_UNDERLYING_INVALID ||
            function->values[instruction->result.value].category != IR_VALUE_PLACE || instruction->canonical_local.value >= function->local_count)
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_STACK_ALLOCATE)
    {
        IrType* pointer = ir_type_from_id(&program->types, instruction->canonical_type);
        IrType* size_type =
            instruction->operand_count == 1 ? ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type) : 0;
        u64 alignment = instruction->immediate_count == 1 ? instruction->immediates[0] : 0;
        if (!pointer || pointer->kind != IR_TYPE_POINTER || !size_type || size_type->kind != IR_TYPE_INTEGER ||
            instruction->result.value == IR_ID_UNDERLYING_INVALID || function->values[instruction->result.value].category != IR_VALUE_VALUE ||
            !alignment || alignment > UINT32_MAX || (alignment & (alignment - 1)))
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_STACK_SAVE)
    {
        IrType* pointer = ir_type_from_id(&program->types, instruction->canonical_type);
        if (!pointer || pointer->kind != IR_TYPE_POINTER || instruction->operand_count != 0 || instruction->immediate_count != 0 ||
            instruction->result.value == IR_ID_UNDERLYING_INVALID || function->values[instruction->result.value].category != IR_VALUE_VALUE)
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_STACK_RESTORE)
    {
        IrType* restored =
            instruction->operand_count == 1 ? ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type) : 0;
        IrType* result_type = ir_type_from_id(&program->types, instruction->canonical_type);
        if (!restored || restored->kind != IR_TYPE_POINTER || !result_type || result_type->kind != IR_TYPE_VOID ||
            instruction->immediate_count != 0 || instruction->result.value != IR_ID_UNDERLYING_INVALID)
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_GLOBAL)
    {
        IrSymbol* symbol = ir_symbol_from_id(&program->symbols, instruction->symbol);
        if (!symbol || symbol->kind != IR_SYMBOL_DATA || symbol->type.value != instruction->canonical_type.value ||
            instruction->operand_count != 0 || instruction->result.value == IR_ID_UNDERLYING_INVALID ||
            function->values[instruction->result.value].category != IR_VALUE_PLACE)
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_LOAD)
    {
        IrValue* place = instruction->operand_count ? function->values + instruction->operands[0].value : 0;
        if (!place || place->category != IR_VALUE_PLACE || place->canonical_type.value != instruction->canonical_type.value ||
            instruction->operand_count != 1 || instruction->result.value == IR_ID_UNDERLYING_INVALID)
        {
            error = IR_VALIDATION_OPERAND_TYPE;
        }
    }
    else if (instruction->opcode == IR_OPCODE_STORE)
    {
        IrValue* place = instruction->operand_count ? function->values + instruction->operands[0].value : 0;
        IrValue* value = instruction->operand_count == 2 ? function->values + instruction->operands[1].value : 0;
        IrType* instruction_type = ir_type_from_id(&program->types, instruction->canonical_type);
        if (!place || !value || place->category != IR_VALUE_PLACE || value->category != IR_VALUE_VALUE ||
            place->canonical_type.value != value->canonical_type.value || !instruction_type || instruction_type->kind != IR_TYPE_VOID ||
            instruction->result.value != IR_ID_UNDERLYING_INVALID)
        {
            error = IR_VALIDATION_OPERAND_TYPE;
        }
    }
    else if (instruction->opcode == IR_OPCODE_CONSTANT_INTEGER)
    {
        IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
        if (!type || (type->kind != IR_TYPE_INTEGER && type->kind != IR_TYPE_BOOLEAN && type->kind != IR_TYPE_ENUM) ||
            instruction->immediate_count != 1 || instruction->operand_count != 0 || instruction->result.value == IR_ID_UNDERLYING_INVALID)
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_CONSTANT_FLOAT)
    {
        IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
        if (!ir_canonical_float_constant_valid(type, instruction))
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_FUNCTION)
    {
        IrSymbol* symbol = ir_symbol_from_id(&program->symbols, instruction->symbol);
        IrType* reference_type = ir_type_from_id(&program->types, instruction->canonical_type);
        bool type_matches =
            symbol && (symbol->type.value == instruction->canonical_type.value ||
                       (reference_type && reference_type->kind == IR_TYPE_POINTER && reference_type->element_type.value == symbol->type.value));
        if (!symbol || symbol->kind != IR_SYMBOL_FUNCTION || !type_matches || instruction->operand_count != 0 ||
            instruction->result.value == IR_ID_UNDERLYING_INVALID)
        {
            error = IR_VALIDATION_CALL_TARGET;
        }
    }
    else if (instruction->opcode == IR_OPCODE_ATOMIC_LOAD)
    {
        IrValue* place = instruction->operand_count ? function->values + instruction->operands[0].value : 0;
        IrType* place_type = place ? ir_type_from_id(&program->types, place->canonical_type) : 0;
        bool valid_order = instruction->memory_order == IR_MEMORY_ORDER_RELAXED || instruction->memory_order == IR_MEMORY_ORDER_CONSUME ||
                           instruction->memory_order == IR_MEMORY_ORDER_ACQUIRE || instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL;
        if (!place || place->category != IR_VALUE_PLACE || !place_type || !place_type->is_atomic ||
            place_type->unqualified_type.value != instruction->canonical_type.value || instruction->operand_count != 1 ||
            instruction->result.value == IR_ID_UNDERLYING_INVALID || !valid_order)
        {
            error = IR_VALIDATION_OPERAND_TYPE;
        }
    }
    else if (instruction->opcode == IR_OPCODE_ATOMIC_STORE)
    {
        IrValue* place = instruction->operand_count ? function->values + instruction->operands[0].value : 0;
        IrValue* value = instruction->operand_count == 2 ? function->values + instruction->operands[1].value : 0;
        IrType* place_type = place ? ir_type_from_id(&program->types, place->canonical_type) : 0;
        IrType* instruction_type = ir_type_from_id(&program->types, instruction->canonical_type);
        bool valid_order = instruction->memory_order == IR_MEMORY_ORDER_RELAXED || instruction->memory_order == IR_MEMORY_ORDER_RELEASE ||
                           instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL;
        if (!place || !value || place->category != IR_VALUE_PLACE || value->category != IR_VALUE_VALUE || !place_type || !place_type->is_atomic ||
            place_type->unqualified_type.value != value->canonical_type.value || !instruction_type || instruction_type->kind != IR_TYPE_VOID ||
            instruction->result.value != IR_ID_UNDERLYING_INVALID || !valid_order)
        {
            error = IR_VALIDATION_OPERAND_TYPE;
        }
    }
    else if (instruction->opcode == IR_OPCODE_ATOMIC_READ_MODIFY_WRITE)
    {
        IrValue* place = instruction->operand_count ? function->values + instruction->operands[0].value : 0;
        IrValue* value = instruction->operand_count == 2 ? function->values + instruction->operands[1].value : 0;
        IrType* place_type = place ? ir_type_from_id(&program->types, place->canonical_type) : 0;
        IrType* value_type = value ? ir_type_from_id(&program->types, value->canonical_type) : 0;
        IrType* result_type = ir_type_from_id(&program->types, instruction->canonical_type);
        bool valid_order = instruction->memory_order < IR_MEMORY_ORDER_COUNT;
        bool pointer_arithmetic = result_type && result_type->kind == IR_TYPE_POINTER &&
                                  (instruction->atomic_operation == IR_ATOMIC_ADD || instruction->atomic_operation == IR_ATOMIC_SUBTRACT);
        if (!place || !value || place->category != IR_VALUE_PLACE || value->category != IR_VALUE_VALUE || !place_type || !place_type->is_atomic ||
            place_type->unqualified_type.value != instruction->canonical_type.value || !value_type ||
            (!pointer_arithmetic && value->canonical_type.value != instruction->canonical_type.value) ||
            (pointer_arithmetic && (value_type->kind != IR_TYPE_INTEGER || !result_type || value_type->layout.size != result_type->layout.size)) ||
            (!pointer_arithmetic && value_type->kind != IR_TYPE_INTEGER &&
             (instruction->atomic_operation != IR_ATOMIC_EXCHANGE ||
              (value_type->kind != IR_TYPE_BOOLEAN && value_type->kind != IR_TYPE_POINTER))) ||
            instruction->atomic_operation >= IR_ATOMIC_OPERATION_COUNT || instruction->operand_count != 2 ||
            instruction->result.value == IR_ID_UNDERLYING_INVALID || !valid_order)
        {
            error = IR_VALIDATION_OPERAND_TYPE;
        }
    }
    else if (instruction->opcode == IR_OPCODE_ATOMIC_COMPARE_EXCHANGE)
    {
        IrValue* place = instruction->operand_count ? function->values + instruction->operands[0].value : 0;
        IrValue* expected = instruction->operand_count == 3 ? function->values + instruction->operands[1].value : 0;
        IrValue* desired = instruction->operand_count == 3 ? function->values + instruction->operands[2].value : 0;
        IrType* place_type = place ? ir_type_from_id(&program->types, place->canonical_type) : 0;
        IrType* value_type = expected ? ir_type_from_id(&program->types, expected->canonical_type) : 0;
        IrMemoryOrder success = instruction->memory_order;
        IrMemoryOrder failure = instruction->failure_memory_order;
        bool valid_failure = failure == IR_MEMORY_ORDER_RELAXED || failure == IR_MEMORY_ORDER_CONSUME || failure == IR_MEMORY_ORDER_ACQUIRE ||
                             failure == IR_MEMORY_ORDER_SEQUENTIAL;
        bool compatible_orders =
            success == IR_MEMORY_ORDER_SEQUENTIAL || (success == IR_MEMORY_ORDER_ACQUIRE_RELEASE && failure != IR_MEMORY_ORDER_SEQUENTIAL) ||
            (success == IR_MEMORY_ORDER_ACQUIRE && failure != IR_MEMORY_ORDER_SEQUENTIAL) ||
            (success == IR_MEMORY_ORDER_CONSUME && (failure == IR_MEMORY_ORDER_RELAXED || failure == IR_MEMORY_ORDER_CONSUME)) ||
            (success == IR_MEMORY_ORDER_RELEASE && failure == IR_MEMORY_ORDER_RELAXED) ||
            (success == IR_MEMORY_ORDER_RELAXED && failure == IR_MEMORY_ORDER_RELAXED);
        if (!place || !expected || !desired || place->category != IR_VALUE_PLACE || expected->category != IR_VALUE_VALUE ||
            desired->category != IR_VALUE_VALUE || !place_type || !place_type->is_atomic ||
            place_type->unqualified_type.value != expected->canonical_type.value ||
            expected->canonical_type.value != desired->canonical_type.value ||
            instruction->canonical_type.value != expected->canonical_type.value || !value_type ||
            (value_type->kind != IR_TYPE_INTEGER && value_type->kind != IR_TYPE_POINTER) || instruction->operand_count != 3 ||
            instruction->result.value == IR_ID_UNDERLYING_INVALID || !valid_failure || !compatible_orders)
        {
            error = IR_VALIDATION_OPERAND_TYPE;
        }
    }
    else if (instruction->opcode == IR_OPCODE_ATOMIC_FENCE)
    {
        IrType* instruction_type = ir_type_from_id(&program->types, instruction->canonical_type);
        if (!instruction_type || instruction_type->kind != IR_TYPE_VOID || instruction->operand_count != 0 ||
            instruction->result.value != IR_ID_UNDERLYING_INVALID || instruction->memory_order >= IR_MEMORY_ORDER_COUNT)
        {
            error = IR_VALIDATION_OPERAND_TYPE;
        }
    }
    else if (instruction->opcode == IR_OPCODE_CLEAR_INSTRUCTION_CACHE)
    {
        IrType* instruction_type = ir_type_from_id(&program->types, instruction->canonical_type);
        IrValue* begin = instruction->operand_count == 2 ? function->values + instruction->operands[0].value : 0;
        IrValue* end = instruction->operand_count == 2 ? function->values + instruction->operands[1].value : 0;
        IrType* begin_type = begin ? ir_type_from_id(&program->types, begin->canonical_type) : 0;
        IrType* end_type = end ? ir_type_from_id(&program->types, end->canonical_type) : 0;
        if (!instruction_type || instruction_type->kind != IR_TYPE_VOID || !begin_type || begin_type->kind != IR_TYPE_POINTER || !end_type ||
            end_type->kind != IR_TYPE_POINTER || instruction->operand_count != 2 || instruction->result.value != IR_ID_UNDERLYING_INVALID)
        {
            error = IR_VALIDATION_OPERAND_TYPE;
        }
    }
    else if (instruction->opcode == IR_OPCODE_CALL)
    {
        IrValue* callee = instruction->operand_count ? function->values + instruction->operands[0].value : 0;
        IrType* callee_type = callee ? ir_type_from_id(&program->types, callee->canonical_type) : 0;
        bool indirect = callee_type && callee_type->kind == IR_TYPE_POINTER;
        IrType* signature_type = indirect ? ir_type_from_id(&program->types, callee_type->element_type) : callee_type;
        IrInstruction* reference =
            callee && callee->definition.value < function->instruction_count ? function->instructions + callee->definition.value : 0;
        if (!signature_type || signature_type->kind != IR_TYPE_FUNCTION ||
            (!signature_type->is_variadic && instruction->operand_count != signature_type->parameter_count + 1) ||
            (signature_type->is_variadic && instruction->operand_count < signature_type->parameter_count + 1) ||
            signature_type->return_type.value != instruction->canonical_type.value || !reference ||
            (!indirect && (reference->opcode != IR_OPCODE_FUNCTION || reference->symbol.value != instruction->symbol.value)) ||
            (indirect && instruction->symbol.value != IR_ID_UNDERLYING_INVALID) ||
            ((ir_type_from_id(&program->types, signature_type->return_type)->kind == IR_TYPE_VOID) !=
             (instruction->result.value == IR_ID_UNDERLYING_INVALID)))
        {
            error = IR_VALIDATION_CALL_SIGNATURE;
        }
        for (u32 argument_index = 0; argument_index < signature_type->parameter_count && error == IR_VALIDATION_NONE; argument_index += 1)
        {
            if (function->values[instruction->operands[argument_index + 1].value].canonical_type.value !=
                signature_type->parameter_types[argument_index].value)
            {
                error = IR_VALIDATION_CALL_SIGNATURE;
            }
        }
    }
    else if (instruction->opcode == IR_OPCODE_ADDRESS_OF)
    {
        IrValue* object = instruction->operand_count == 1 ? function->values + instruction->operands[0].value : 0;
        IrType* pointer = ir_type_from_id(&program->types, instruction->canonical_type);
        IrValue* result_value = instruction->result.value < function->value_count ? function->values + instruction->result.value : 0;
        IrValueLabelMetadata result_metadata = result_value ? ir_value_label_metadata(function, instruction->result) : (IrValueLabelMetadata){0};
        if (!object || object->category != IR_VALUE_PLACE || !pointer || pointer->kind != IR_TYPE_POINTER ||
            pointer->element_type.value != object->canonical_type.value || instruction->result.value == IR_ID_UNDERLYING_INVALID || !result_value ||
            (result_metadata.is_label_value || result_metadata.has_label_provenance || result_metadata.label_blocks || result_metadata.label_block_count))
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_DEREFERENCE)
    {
        IrValue* address = instruction->operand_count == 1 ? function->values + instruction->operands[0].value : 0;
        IrType* pointer = address ? ir_type_from_id(&program->types, address->canonical_type) : 0;
        IrValue* place = instruction->result.value < function->value_count ? function->values + instruction->result.value : 0;
        IrValueLabelMetadata address_metadata = address ? ir_value_label_metadata(function, instruction->operands[0]) : (IrValueLabelMetadata){0};
        IrValueLabelMetadata place_metadata = place ? ir_value_label_metadata(function, instruction->result) : (IrValueLabelMetadata){0};
        if (!address || address->category != IR_VALUE_VALUE || !pointer || pointer->kind != IR_TYPE_POINTER ||
            pointer->element_type.value != instruction->canonical_type.value || !place || place->category != IR_VALUE_PLACE ||
            ir_label_metadata_has_label(&address_metadata) || ir_label_metadata_has_label(&place_metadata))
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_UNARY)
    {
        IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
        IrType* vector_element = type && type->kind == IR_TYPE_VECTOR ? ir_type_from_id(&program->types, type->element_type) : 0;
        bool valid_operation =
            (type && type->kind == IR_TYPE_INTEGER &&
             (instruction->unary_operation == IR_UNARY_INTEGER_NEGATE || instruction->unary_operation == IR_UNARY_INTEGER_BITWISE_NOT ||
              instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS ||
              instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS ||
              instruction->unary_operation == IR_UNARY_INTEGER_POPULATION_COUNT)) ||
            (type && type->kind == IR_TYPE_FLOAT && instruction->unary_operation == IR_UNARY_FLOAT_NEGATE) ||
            (type && type->kind == IR_TYPE_BOOLEAN && instruction->unary_operation == IR_UNARY_BOOLEAN_NOT) ||
            (vector_element && vector_element->kind == IR_TYPE_INTEGER &&
             (instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_NEGATE ||
              instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_BITWISE_NOT)) ||
            (vector_element && vector_element->kind == IR_TYPE_FLOAT && instruction->unary_operation == IR_UNARY_VECTOR_FLOAT_NEGATE);
        if (!type || instruction->operand_count != 1 ||
            function->values[instruction->operands[0].value].canonical_type.value != instruction->canonical_type.value || !valid_operation ||
            instruction->result.value == IR_ID_UNDERLYING_INVALID)
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_BINARY)
    {
        IrType* result_type = ir_type_from_id(&program->types, instruction->canonical_type);
        IrValue* left = instruction->operand_count == 2 ? function->values + instruction->operands[0].value : 0;
        IrValue* right = instruction->operand_count == 2 ? function->values + instruction->operands[1].value : 0;
        IrType* operand_type = left ? ir_type_from_id(&program->types, left->canonical_type) : 0;
        bool arithmetic =
            instruction->binary_operation <= IR_BINARY_FLOAT_DIVIDE ||
            (instruction->binary_operation >= IR_BINARY_SIGNED_REMAINDER && instruction->binary_operation <= IR_BINARY_INTEGER_BITWISE_XOR);
        bool comparison =
            instruction->binary_operation == IR_BINARY_INTEGER_EQUAL || instruction->binary_operation == IR_BINARY_INTEGER_NOT_EQUAL ||
            instruction->binary_operation == IR_BINARY_FLOAT_EQUAL || instruction->binary_operation == IR_BINARY_FLOAT_NOT_EQUAL ||
            (instruction->binary_operation >= IR_BINARY_SIGNED_LESS && instruction->binary_operation <= IR_BINARY_FLOAT_GREATER_EQUAL);
        bool vector_operation =
            instruction->binary_operation >= IR_BINARY_VECTOR_INTEGER_ADD && instruction->binary_operation <= IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL;
        bool vector_comparison = instruction->binary_operation >= IR_BINARY_VECTOR_INTEGER_EQUAL &&
                                 instruction->binary_operation <= IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL;
        bool matching_operands = left && right && left->canonical_type.value == right->canonical_type.value;
        bool valid_arithmetic = arithmetic && result_type && (result_type->kind == IR_TYPE_INTEGER || result_type->kind == IR_TYPE_FLOAT) &&
                                matching_operands && left->canonical_type.value == instruction->canonical_type.value;
        bool valid_comparison = comparison && result_type && result_type->kind == IR_TYPE_BOOLEAN && matching_operands && operand_type &&
                                (operand_type->kind == IR_TYPE_INTEGER || operand_type->kind == IR_TYPE_FLOAT);
        IrType* operand_element =
            operand_type && operand_type->kind == IR_TYPE_VECTOR ? ir_type_from_id(&program->types, operand_type->element_type) : 0;
        IrType* result_element =
            result_type && result_type->kind == IR_TYPE_VECTOR ? ir_type_from_id(&program->types, result_type->element_type) : 0;
        bool vector_float_operation =
            (instruction->binary_operation >= IR_BINARY_VECTOR_FLOAT_ADD && instruction->binary_operation <= IR_BINARY_VECTOR_FLOAT_DIVIDE) ||
            (instruction->binary_operation >= IR_BINARY_VECTOR_FLOAT_EQUAL &&
             instruction->binary_operation <= IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL);
        bool valid_vector_result = !vector_comparison ? result_type == operand_type
                                                      : result_type && operand_type && operand_element && result_element &&
                                                            result_element->kind == IR_TYPE_INTEGER && result_element->is_signed &&
                                                            result_type->element_count == operand_type->element_count &&
                                                            result_type->layout.size == operand_type->layout.size &&
                                                            result_element->bit_width == operand_element->bit_width;
        bool valid_vector_operation = vector_operation && matching_operands && operand_type && operand_type->kind == IR_TYPE_VECTOR &&
                                      operand_element &&
                                      ((vector_float_operation && operand_element->kind == IR_TYPE_FLOAT) ||
                                       (!vector_float_operation && operand_element->kind == IR_TYPE_INTEGER)) &&
                                      valid_vector_result;
        bool valid_pointer_comparison =
            (instruction->binary_operation == IR_BINARY_POINTER_EQUAL || instruction->binary_operation == IR_BINARY_POINTER_NOT_EQUAL) &&
            result_type && result_type->kind == IR_TYPE_BOOLEAN && matching_operands && operand_type && operand_type->kind == IR_TYPE_POINTER;
        if ((!valid_arithmetic && !valid_comparison && !valid_vector_operation && !valid_pointer_comparison) ||
            instruction->result.value == IR_ID_UNDERLYING_INVALID)
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_CAST)
    {
        IrType* destination = ir_type_from_id(&program->types, instruction->canonical_type);
        IrValue* operand_slot = instruction->operand_count == 1 ? function->values + instruction->operands[0].value : 0;
        IrType* source = operand_slot ? ir_type_from_id(&program->types, operand_slot->canonical_type) : 0;
        bool result_in_range = instruction->result.value < function->value_count;
        IrValueLabelMetadata operand_metadata =
            operand_slot ? ir_value_label_metadata(function, instruction->operands[0]) : (IrValueLabelMetadata){0};
        IrValueLabelMetadata result_metadata = result_in_range ? ir_value_label_metadata(function, instruction->result) : (IrValueLabelMetadata){0};
        IrValueLabelMetadata* operand = operand_slot ? &operand_metadata : 0;
        IrValueLabelMetadata* label_result = result_in_range ? &result_metadata : 0;
        bool label_conversion_valid = true;
        if ((operand && ir_label_metadata_has_label(operand)) || (label_result && ir_label_metadata_has_label(label_result)))
        {
            label_conversion_valid = operand && source && destination && ir_canonical_void_pointer_type(program, operand_slot->canonical_type) &&
                                     ir_canonical_void_pointer_type(program, instruction->canonical_type) && source->id.value == destination->id.value &&
                                     instruction->conversion_operation == IR_CONVERSION_IDENTITY && label_result &&
                                     ir_label_provenance_valid(operand) && ir_label_provenance_valid(label_result) &&
                                     label_result->label_block_count == operand->label_block_count;
            for (u32 label_index = 0; label_conversion_valid && label_index < operand->label_block_count; label_index += 1)
            {
                label_conversion_valid = ir_label_provenance_contains(label_result, operand->label_blocks[label_index]);
            }
        }
        if (!ir_canonical_conversion_valid(source, destination, instruction->conversion_operation) ||
            instruction->result.value == IR_ID_UNDERLYING_INVALID || !label_conversion_valid)
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_ARRAY)
    {
        IrType* array = ir_type_from_id(&program->types, instruction->canonical_type);
        bool valid = array && (array->kind == IR_TYPE_ARRAY || array->kind == IR_TYPE_VECTOR) &&
                     instruction->operand_count == array->element_count && instruction->immediate_count == 0 &&
                     instruction->result.value != IR_ID_UNDERLYING_INVALID;
        for (u32 operand_index = 0; valid && operand_index < instruction->operand_count; operand_index += 1)
        {
            valid = function->values[instruction->operands[operand_index].value].canonical_type.value == array->element_type.value;
        }
        if (!valid)
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_AGGREGATE)
    {
        IrType* aggregate = ir_type_from_id(&program->types, instruction->canonical_type);
        bool valid = aggregate && (aggregate->kind == IR_TYPE_STRUCT || aggregate->kind == IR_TYPE_UNION) &&
                     instruction->operand_count == instruction->immediate_count && instruction->result.value != IR_ID_UNDERLYING_INVALID &&
                     (aggregate->kind == IR_TYPE_UNION ? instruction->operand_count <= 1 : instruction->operand_count == aggregate->field_count);
        for (u32 operand_index = 0; valid && operand_index < instruction->operand_count; operand_index += 1)
        {
            u64 field_index = instruction->immediates[operand_index];
            valid = field_index < aggregate->field_count &&
                    function->values[instruction->operands[operand_index].value].canonical_type.value == aggregate->fields[field_index].type.value;
            for (u32 previous = 0; valid && previous < operand_index; previous += 1)
            {
                valid = instruction->immediates[previous] != field_index;
            }
        }
        if (!valid)
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_FIELD)
    {
        IrValue* base = instruction->operand_count == 1 ? function->values + instruction->operands[0].value : 0;
        IrType* base_type = base ? ir_type_from_id(&program->types, base->canonical_type) : 0;
        u64 field_index = instruction->immediate_count == 1 ? instruction->immediates[0] : UINT64_MAX;
        bool valid_field = base_type && (base_type->kind == IR_TYPE_STRUCT || base_type->kind == IR_TYPE_UNION) &&
                           field_index < base_type->field_count && instruction->result.value != IR_ID_UNDERLYING_INVALID &&
                           function->values[instruction->result.value].category == IR_VALUE_PLACE &&
                           instruction->canonical_type.value == base_type->fields[field_index].type.value;
        if (!valid_field)
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_INDEX)
    {
        IrValue* base = instruction->operand_count == 2 ? function->values + instruction->operands[0].value : 0;
        IrValue* index = instruction->operand_count == 2 ? function->values + instruction->operands[1].value : 0;
        IrType* base_type = base ? ir_type_from_id(&program->types, base->canonical_type) : 0;
        IrType* index_type = index ? ir_type_from_id(&program->types, index->canonical_type) : 0;
        bool valid_index =
            base_type && (base_type->kind == IR_TYPE_ARRAY || base_type->kind == IR_TYPE_VECTOR || base_type->kind == IR_TYPE_POINTER) &&
            index_type && index_type->kind == IR_TYPE_INTEGER && instruction->immediate_count == 0 &&
            instruction->result.value != IR_ID_UNDERLYING_INVALID && function->values[instruction->result.value].category == IR_VALUE_PLACE &&
            instruction->canonical_type.value == base_type->element_type.value;
        if (!valid_index)
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_VA_START || instruction->opcode == IR_OPCODE_VA_COPY || instruction->opcode == IR_OPCODE_VA_END ||
             instruction->opcode == IR_OPCODE_VA_ARG)
    {
        IrType* result_type = ir_type_from_id(&program->types, instruction->canonical_type);
        IrType* function_type = ir_type_from_id(&program->types, function->canonical_type);
        bool start = instruction->opcode == IR_OPCODE_VA_START;
        bool end = instruction->opcode == IR_OPCODE_VA_END;
        bool valid = result_type && function_type && function_type->kind == IR_TYPE_FUNCTION && instruction->immediate_count == 0;
        if (start)
        {
            valid &= function_type->is_variadic && result_type->kind == IR_TYPE_VA_LIST && instruction->operand_count == 0 &&
                     instruction->result.value != IR_ID_UNDERLYING_INVALID;
        }
        else
        {
            IrValue* operand = instruction->operand_count == 1 ? &function->values[instruction->operands[0].value] : 0;
            IrType* pointer = operand ? ir_type_from_id(&program->types, operand->canonical_type) : 0;
            IrType* pointee = pointer && pointer->kind == IR_TYPE_POINTER ? ir_type_from_id(&program->types, pointer->element_type) : 0;
            valid &= operand && pointer && pointee && pointee->kind == IR_TYPE_VA_LIST;
            if (end)
            {
                valid &= result_type->kind == IR_TYPE_VOID && instruction->result.value == IR_ID_UNDERLYING_INVALID;
            }
            else
            {
                valid &= instruction->result.value != IR_ID_UNDERLYING_INVALID;
                if (instruction->opcode == IR_OPCODE_VA_COPY)
                {
                    valid &= result_type->kind == IR_TYPE_VA_LIST;
                }
                else
                {
                    valid &= result_type->kind != IR_TYPE_VOID;
                }
            }
        }
        if (!valid)
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY)
    {
        if (!ir_canonical_inline_assembly_valid(program, function, instruction))
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_SIMD)
    {
        if (!ir_canonical_simd_valid(program, function, instruction))
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_LABEL_ADDRESS)
    {
        IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
        bool result_in_range = instruction->result.value < function->value_count;
        IrValueLabelMetadata result_metadata = result_in_range ? ir_value_label_metadata(function, instruction->result) : (IrValueLabelMetadata){0};
        IrValueLabelMetadata* label_result = result_in_range ? &result_metadata : 0;
        bool valid = type && ir_canonical_void_pointer_type(program, instruction->canonical_type) && instruction->operand_count == 0 && instruction->target_count == 1 &&
                     instruction->targets && instruction->targets[0].value < function->block_count && instruction->immediate_count == 0 &&
                     label_result && instruction->result.value != IR_ID_UNDERLYING_INVALID && !label_result->has_non_label_provenance &&
                     !label_result->has_label_provenance && !label_result->label_paths && !label_result->label_path_count && ir_label_provenance_valid(label_result) &&
                     label_result->label_block_count == 1 && label_result->label_blocks && label_result->label_blocks[0].value == instruction->targets[0].value;
        if (!valid)
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_DEBUG_TRAP)
    {
        IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
        if (!type || type->kind != IR_TYPE_VOID || instruction->operand_count != 0 || instruction->immediate_count != 0 ||
            instruction->result.value != IR_ID_UNDERLYING_INVALID)
        {
            error = IR_VALIDATION_OPERATION;
        }
    }
    else if (instruction->opcode == IR_OPCODE_BRANCH)
    {
        if (instruction->operand_count != 0 || instruction->target_count != 1 || instruction->targets[0].value >= function->block_count ||
            instruction->result.value != IR_ID_UNDERLYING_INVALID)
        {
            error = IR_VALIDATION_BRANCH_TARGET;
        }
    }
    else if (instruction->opcode == IR_OPCODE_BRANCH_IF)
    {
        IrValue* condition = instruction->operand_count == 1 ? function->values + instruction->operands[0].value : 0;
        IrType* condition_type = condition ? ir_type_from_id(&program->types, condition->canonical_type) : 0;
        if (!condition_type || condition_type->kind != IR_TYPE_BOOLEAN || instruction->target_count != 2 ||
            instruction->targets[0].value >= function->block_count || instruction->targets[1].value >= function->block_count ||
            instruction->result.value != IR_ID_UNDERLYING_INVALID)
        {
            error = IR_VALIDATION_BRANCH_TARGET;
        }
    }
    else if (instruction->opcode == IR_OPCODE_INDIRECT_BRANCH)
    {
        IrValue* target_slot = instruction->operand_count == 1 && instruction->operands && instruction->operands[0].value < function->value_count
                                   ? function->values + instruction->operands[0].value
                                   : 0;
        IrValueLabelMetadata target_metadata = target_slot ? ir_value_label_metadata(function, instruction->operands[0]) : (IrValueLabelMetadata){0};
        IrValueLabelMetadata* target = target_slot ? &target_metadata : 0;
        IrType* target_type = target_slot ? ir_type_from_id(&program->types, target_slot->canonical_type) : 0;
        bool valid = target && target_type && ir_canonical_void_pointer_type(program, target_slot->canonical_type) && !target->has_non_label_provenance &&
                     ir_label_provenance_valid(target) && instruction->target_count == target->label_block_count &&
                     instruction->operand_count == 1 && instruction->target_count != 0 && instruction->targets &&
                     instruction->result.value == IR_ID_UNDERLYING_INVALID && ir_block_id_array_unique(instruction->targets, instruction->target_count);
        bool label_targets = valid;
        for (u32 label_index = 0; label_targets && label_index < target->label_block_count; label_index += 1)
        {
            bool found = false;
            for (u32 target_index = 0; target_index < instruction->target_count; target_index += 1)
            {
                found |= instruction->targets[target_index].value == target->label_blocks[label_index].value;
            }
            label_targets &= target->label_blocks[label_index].value < function->block_count && found;
        }
        for (u32 target_index = 0; valid && target_index < instruction->target_count; target_index += 1)
        {
            bool found = false;
            for (u32 label_index = 0; label_index < target->label_block_count; label_index += 1)
            {
                found |= instruction->targets[target_index].value == target->label_blocks[label_index].value;
            }
            valid &= found;
        }
        valid &= label_targets;
        if (!valid)
        {
            error = IR_VALIDATION_BRANCH_TARGET;
        }
    }
    else if (instruction->opcode == IR_OPCODE_SWITCH)
    {
        IrValue* switched = instruction->operand_count == 1 ? function->values + instruction->operands[0].value : 0;
        IrType* switched_type = switched ? ir_type_from_id(&program->types, switched->canonical_type) : 0;
        bool valid_targets = instruction->target_count == instruction->immediate_count + 1;
        for (u32 target_index = 0; valid_targets && target_index < instruction->target_count; target_index += 1)
        {
            valid_targets = instruction->targets[target_index].value < function->block_count;
        }
        if (!switched_type || switched_type->kind != IR_TYPE_INTEGER || !valid_targets || instruction->result.value != IR_ID_UNDERLYING_INVALID)
        {
            error = IR_VALIDATION_BRANCH_TARGET;
        }
    }
    else if (instruction->opcode == IR_OPCODE_RETURN)
    {
        IrType* return_type = ir_type_from_id(&program->types, signature->return_type);
        bool valid_return =
            return_type && (return_type->kind == IR_TYPE_VOID
                                ? instruction->operand_count == 0
                                : instruction->operand_count == 1 &&
                                      function->values[instruction->operands[0].value].canonical_type.value == signature->return_type.value);
        if (!valid_return)
        {
            error = IR_VALIDATION_RETURN_TYPE;
        }
    }
    return error;
}

// One block's instruction chain. ir_validate_module_ownership already proved
// the chain is a simple path of in-range ids, which is why there is no range or
// revisit guard here.
BUSTER_GLOBAL_LOCAL IrValidationResult ir_validate_block_instructions(IrProgram* program, IrFunction* function, IrType* signature, IrBlock* block)
{
    IrValidationResult result = ir_validation_ok();
    IrInstructionId instruction_id = block->first_instruction;
    bool terminated = false;
    while (instruction_id.value != IR_ID_UNDERLYING_INVALID && result.error == IR_VALIDATION_NONE)
    {
        IrInstruction* instruction = function->instructions + instruction_id.value;
        IrValidationError error = IR_VALIDATION_NONE;
        if (terminated || instruction->opcode >= IR_OPCODE_COUNT || !ir_type_from_id(&program->types, instruction->canonical_type))
        {
            error = terminated ? IR_VALIDATION_INSTRUCTION_AFTER_TERMINATOR : IR_VALIDATION_INVALID_ID;
        }
        else if ((instruction->operand_count && !instruction->operands) || (instruction->target_count && !instruction->targets) ||
                 (instruction->immediate_count && !instruction->immediates))
        {
            error = IR_VALIDATION_OPERATION;
        }
        else
        {
            for (u32 operand_index = 0; operand_index < instruction->operand_count && error == IR_VALIDATION_NONE; operand_index += 1)
            {
                if (instruction->operands[operand_index].value >= function->value_count)
                {
                    error = IR_VALIDATION_INVALID_ID;
                }
            }
            for (u32 target_index = 0; target_index < instruction->target_count && error == IR_VALIDATION_NONE; target_index += 1)
            {
                if (instruction->targets[target_index].value >= function->block_count)
                {
                    error = IR_VALIDATION_BRANCH_TARGET;
                }
            }
            if (error == IR_VALIDATION_NONE && instruction->result.value != IR_ID_UNDERLYING_INVALID &&
                (instruction->result.value >= function->value_count ||
                 function->values[instruction->result.value].definition.value != instruction_id.value ||
                 function->values[instruction->result.value].canonical_type.value != instruction->canonical_type.value))
            {
                error = IR_VALIDATION_RESULT_TYPE;
            }
            if (error == IR_VALIDATION_NONE)
            {
                error = ir_validate_instruction_operation(program, function, signature, instruction);
            }
        }
        if (error != IR_VALIDATION_NONE)
        {
            result = ir_validation_error(error, function, block->id, instruction_id);
        }
        else
        {
            terminated = instruction->opcode == IR_OPCODE_BRANCH || instruction->opcode == IR_OPCODE_BRANCH_IF || instruction->opcode == IR_OPCODE_SWITCH ||
                         instruction->opcode == IR_OPCODE_INDIRECT_BRANCH || instruction->opcode == IR_OPCODE_RETURN ||
                         instruction->opcode == IR_OPCODE_UNREACHABLE || (instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY && instruction->target_count != 0);
            instruction_id = instruction->next;
        }
    }
    if (result.error == IR_VALIDATION_NONE && !terminated)
    {
        result = ir_validation_error(IR_VALIDATION_UNTERMINATED_BLOCK, function, block->id, block->last_instruction);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL IrValidationResult ir_validate_function_blocks(IrProgram* program, IrFunction* function, IrType* signature)
{
    IrValidationResult result = ir_validation_ok();
    for (u32 block_index = 0; block_index < function->block_count && result.error == IR_VALIDATION_NONE; block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        if (!block->terminated || !block->sealed)
        {
            result = ir_validation_error(!block->terminated ? IR_VALIDATION_UNTERMINATED_BLOCK : IR_VALIDATION_BLOCK_PARAMETER, function, block->id,
                                         block->last_instruction);
        }
        else
        {
            result = ir_validate_block_parameters(function, block);
            if (result.error == IR_VALIDATION_NONE)
            {
                result = ir_validate_block_instructions(program, function, signature, block);
            }
        }
    }
    return result;
}

IrValidationResult ir_validate_canonical_module(IrProgram* program, IrModule* module)
{
    IrValidationResult result = ir_validation_ok();
    if (!program || !module)
    {
        result.error = IR_VALIDATION_INVALID_ID;
    }
    else
    {
        result = ir_validate_module_ownership(module);
        for (u32 global_index = 0; global_index < module->global_count && result.error == IR_VALIDATION_NONE; global_index += 1)
        {
            result.error = ir_validate_global(program, module, module->globals + global_index);
        }
        for (u32 function_index = 0; function_index < module->function_count && result.error == IR_VALIDATION_NONE; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            if (function->state != IR_FUNCTION_LOWERED)
            {
                continue;
            }
            IrType* signature = ir_type_from_id(&program->types, function->canonical_type);
            if (!signature || signature->kind != IR_TYPE_FUNCTION || function->entry.value >= function->block_count)
            {
                result = ir_validation_error(IR_VALIDATION_INVALID_ID, function, IR_BLOCK_ID_INVALID, IR_INSTRUCTION_ID_INVALID);
            }
            else
            {
                result = ir_validate_function_values(program, function);
                if (result.error == IR_VALIDATION_NONE)
                {
                    result = ir_validate_function_blocks(program, function, signature);
                }
            }
        }
    }
    return result;
}

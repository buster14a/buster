// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Checked static objects to the existing Link representation. Include after c_check_initializer.c and link.c.
// check_data_emit consumes a batch of selected definitions, not raw declarations: the source adapter/driver owns
// redeclaration merging, tentative-definition selection and name/linkage resolution. Each object has one existing,
// undefined OBJECT/TLS symbol (source NONE or PROGRAM) in object_symbols; address constants use that same opaque map. Symbols
// for external references and functions may already exist too. This stage never invents object identity or performs
// a missing initializer conversion. Checked subobject plans describe aggregates, bit-fields, zero filling and
// string copies. Literal records become STRINGS for byte characters and RODATA for wide characters; TLS symbols
// select TDATA/TBSS. Unsupported scalar formats and address constants referring to TLS fail explicitly.
//
// All inputs are immutable bounded arrays with their natural alignment and no readable padding. objects has count
// records; object_symbols has object_count entries, with zero meaning unmapped. stream values/diagnostics have count
// entries of their own. Link tables must be initialized and exclusively owned. Its existing unrelated records are
// the caller's responsibility; link_check validates the complete unit after its other producers finish.
//
// bytes and scratch are distinct, disjoint caller-owned arenas. Their newly allocated suffixes must not overlap
// inputs or Link tables; live input prefixes in either stable-address arena are permitted.
// The byte arena owns published atom bytes and must outlive Link consumers. Plans build directly in its unpublished
// suffix; scratch holds sorted final relocations and a symbol bitset. If a candidate does not fit the byte arena,
// scratch supplies temporary bytes so classification and exact capacity demand still complete. Such a payload is
// never published. Zero-fill candidates release their byte suffix. All type, constant, symbol, duplicate, binding and
// capacity checks precede Link publication. A failed call leaves Link (including its error), symbols and byte length
// unchanged. Empty batches accept null input arrays. Records may be partitioned into batches only when local
// forward-reference definitions remain in the same batch or were published earlier.
//
// The batch boundary preserves independent object work and avoids reparsing, interning or name lookup. Planning
// encodes each subobject once, applies source-order replacements and counts final output; publication uses existing
// Link adds without another payload copy. Only writable/TLS objects without live relocations need a zero scan;
// that scan stops at the first nonzero block and uses masked tails within the logical input.
#ifndef BUSTER_C_CHECK_DATA_INCLUDED
#define BUSTER_C_CHECK_DATA_INCLUDED

#include <float.h>
#include <limits.h>
#include <math.h>

typedef struct CheckDataObject CheckDataObject;
struct CheckDataObject
{
    TypeId type;
    uint32_t object;
    // Final checked INITIALIZE result, or BUSTER_CHECK_NONE for an implicit zero initializer.
    uint32_t initializer;
    uint32_t token;
    const CheckInitializer* initializer_plan;
    const uint8_t* literal_bytes;
    uint64_t literal_size;
    bool string_pool;
    // Declaration alignment is separate from the ordinary compatible type. Zero keeps its natural alignment.
    uint8_t alignment_log2;
};

typedef enum CheckDataStatus
{
    CHECK_DATA_OK,
    CHECK_DATA_INVALID,
    CHECK_DATA_CHECK_FAILED,
    CHECK_DATA_UNSUPPORTED,
    CHECK_DATA_NON_CONSTANT,
    CHECK_DATA_SYMBOL,
    CHECK_DATA_RESOURCE,
} CheckDataStatus;

typedef struct CheckDataResult CheckDataResult;
struct CheckDataResult
{
    CheckDataStatus status;
    uint32_t failed_record;
    uint32_t failed_token;
    // Counts are the exact demand after successful planning, including on publication-capacity failure.
    // They remain zero when planning fails. atom_begin is nonzero only on successful nonempty emission.
    AtomId atom_begin;
    uint32_t atom_count;
    uint32_t fixup_count;
    uint32_t wide_addend_count;
    uint64_t byte_count;
};

typedef struct CheckDataScalar CheckDataScalar;
struct CheckDataScalar
{
    uint8_t bytes[32];
    SymbolId symbol;
    uint32_t object;
    int64_t addend;
    uint8_t size;
    uint8_t alignment_log2;
};

typedef struct CheckDataFixup CheckDataFixup;
struct CheckDataFixup
{
    uint32_t offset;
    uint32_t object;
    SymbolId target;
    int64_t addend;
};

typedef struct CheckDataPlan CheckDataPlan;
struct CheckDataPlan
{
    uint8_t* bytes;
    uint64_t size;
    CheckDataFixup* fixups;
    uint32_t fixup_count;
    SymbolId symbol;
    uint8_t alignment_log2;
    uint8_t class;
};

static bool check_data_emit(const TypeTable* types, const CheckStream* stream, const CheckDataObject* objects, uint32_t count,
                            const SymbolId* object_symbols, uint32_t object_count, Link* link, uint32_t unit,
                            Arena* bytes, Arena* scratch, CheckDataResult* result);

#include "c_check_constant_bytes.c"

static CheckDataStatus check_data_plan_scalar(const TypeTable* types, const CheckStream* stream, const CheckDataObject* object,
                                             const SymbolId* object_symbols, uint32_t object_count, const Link* link, CheckDataScalar* plan)
{
    *plan = (CheckDataScalar){ 0 };
    CheckDataStatus status = CHECK_DATA_OK;
    if (!type_is_valid(types, object->type) || !type_is_complete(types, object->type))
    {
        status = CHECK_DATA_INVALID;
    }
    TypeId base = 0;
    uint32_t width = 0;
    bool is_signed = false;
    bool integer = false;
    bool floating = false;
    bool complex = false;
    bool pointer = false;
    if (status == CHECK_DATA_OK)
    {
        const TypeId stripped = type_strip_aligned(types, object->type);
        base = type_unqualified(stripped);
        const uint8_t kind = type_kind(types, base);
        integer = check_action_integer_type(types, base, &width, &is_signed);
        floating = base == BUSTER_TYPE_FLOAT || base == BUSTER_TYPE_DOUBLE || base == BUSTER_TYPE_LONG_DOUBLE;
        complex = kind == BUSTER_TYPE_KIND_COMPLEX;
        pointer = kind == BUSTER_TYPE_KIND_POINTER;
        const uint64_t size = type_size(types, object->type);
        const uint32_t natural = type_alignment_log2(types, object->type);
        const uint32_t alignment = object->alignment_log2 > natural ? object->alignment_log2 : natural;
        const uint64_t scalar_size = type_size(types, base);
        const bool shape = size != 0 && size <= sizeof(plan->bytes) && size == scalar_size && alignment <= BUSTER_LINK_MAX_ALIGNMENT_LOG2;
        if ((!integer && !floating && !complex && !pointer) || !shape || (pointer && size != 8) || (integer && width > size * 8))
        {
            status = CHECK_DATA_UNSUPPORTED;
        }
        else
        {
            plan->size = (uint8_t)size;
            plan->alignment_log2 = (uint8_t)alignment;
        }
    }
    if (status == CHECK_DATA_OK)
    {
        if (object->object == 0 || object->object >= object_count || object_symbols[object->object] == 0 || object_symbols[object->object] >= link->symbols.count)
        {
            status = CHECK_DATA_SYMBOL;
        }
        else
        {
            plan->symbol = object_symbols[object->object];
            const uint8_t symbol_type = link->symbols.flags[plan->symbol] & BUSTER_LINK_SYMBOL_TYPE;
            if ((symbol_type != BUSTER_LINK_SYMBOL_OBJECT && symbol_type != BUSTER_LINK_SYMBOL_TLS) ||
                     (link->symbols.source[plan->symbol] != BUSTER_LINK_SOURCE_PROGRAM && link->symbols.source[plan->symbol] != BUSTER_LINK_SOURCE_NONE) ||
                     link->symbols.atom[plan->symbol] != 0)
            {
                status = CHECK_DATA_SYMBOL;
            }
        }
    }
    if (status == CHECK_DATA_OK && object->initializer != BUSTER_CHECK_NONE)
    {
        if (stream == 0 || object->initializer >= stream->count || stream->values == 0 || stream->diagnostics == 0)
        {
            status = CHECK_DATA_INVALID;
        }
        else if (stream->status != CHECK_STATUS_OK || stream->report_count != 0 || stream->diagnostics[object->initializer] != CHECK_DIAG_NONE)
        {
            status = CHECK_DATA_CHECK_FAILED;
        }
        else
        {
            const CheckValue* value = &stream->values[object->initializer];
            const bool valid_type = type_is_valid(types, value->type);
            const TypeId value_type = valid_type ? type_unqualified(type_strip_aligned(types, value->type)) : 0;
            TypeAnswer compatible = value_type == base ? TYPE_ANSWER_YES : TYPE_ANSWER_NO;
            if (valid_type && pointer && value_type != base && type_kind(types, value_type) == BUSTER_TYPE_KIND_POINTER)
            {
                // A later compatible declaration can complete an array/function pointee after this initializer
                // was checked. Its pointer representation is unchanged; scalar arithmetic conversions are exact.
                compatible = type_compatible(types, base, value_type);
            }
            if (compatible == TYPE_ANSWER_FULL)
            {
                status = CHECK_DATA_RESOURCE;
            }
            else if (!valid_type || compatible != TYPE_ANSWER_YES || value->category != CHECK_RVALUE)
            {
                status = CHECK_DATA_INVALID;
            }
            else if (value->fault != CHECK_FAULT_NONE || (value->flags & BUSTER_CHECK_SIDE_EFFECT) != 0)
            {
                status = CHECK_DATA_NON_CONSTANT;
            }
            else if (integer && value->constant_kind == CHECK_CONSTANT_INTEGER)
            {
                const unsigned __int128 mask = check_action_mask(width);
                if ((value->integer & ~mask) != 0 || (base == BUSTER_TYPE_BOOL && value->integer > 1))
                {
                    status = CHECK_DATA_INVALID;
                }
                else
                {
                    // The x86-64 _BitInt object representation extends a signed value through its storage unit.
                    const bool extend = is_signed && width < plan->size * 8u && (value->integer >> (width - 1)) != 0;
                    const unsigned __int128 bits = value->integer | (extend ? ~mask : 0);
                    check_data_integer_bytes(plan->bytes, bits, plan->size);
                }
            }
            else if (floating && value->constant_kind == CHECK_CONSTANT_FLOATING)
            {
                const long double rounded = check_action_round_float(base, value->floating);
                if (!isnan(value->floating) && (rounded != value->floating || signbit(rounded) != signbit(value->floating)))
                {
                    status = CHECK_DATA_INVALID;
                }
                else
                {
                    check_data_float_bytes(plan->bytes, base, value->floating);
                }
            }
            else if (complex && value->constant_kind == CHECK_CONSTANT_COMPLEX)
            {
                const TypeId element = type_element(types, base);
                const uint32_t component_size = (uint32_t)type_size(types, element);
                uint32_t component_width = 0;
                bool component_signed = false;
                if (component_size == 0 || (uint64_t)component_size * 2 != plan->size)
                {
                    status = CHECK_DATA_UNSUPPORTED;
                }
                else if (check_action_float_supported(types, element))
                {
                    const long double real = check_action_round_float(element, value->floating);
                    const long double imaginary = check_action_round_float(element, value->imaginary);
                    const bool real_valid = isnan(value->floating) || (real == value->floating && signbit(real) == signbit(value->floating));
                    const bool imaginary_valid = isnan(value->imaginary) || (imaginary == value->imaginary && signbit(imaginary) == signbit(value->imaginary));
                    if (!real_valid || !imaginary_valid)
                    {
                        status = CHECK_DATA_INVALID;
                    }
                    else
                    {
                        check_data_float_bytes(plan->bytes, element, value->floating);
                        check_data_float_bytes(plan->bytes + component_size, element, value->imaginary);
                    }
                }
                else if (check_action_integer_type(types, element, &component_width, &component_signed) && component_width <= 64)
                {
                    const unsigned __int128 mask = check_action_mask(component_width);
                    const unsigned __int128 real = value->integer & UINT64_MAX;
                    const unsigned __int128 imaginary = value->integer >> 64;
                    if ((real & ~mask) != 0 || (imaginary & ~mask) != 0)
                    {
                        status = CHECK_DATA_INVALID;
                    }
                    else
                    {
                        check_data_integer_bytes(plan->bytes, real, component_size);
                        check_data_integer_bytes(plan->bytes + component_size, imaginary, component_size);
                    }
                }
                else
                {
                    status = CHECK_DATA_UNSUPPORTED;
                }
            }
            else if ((pointer || (integer && width == 64 && plan->size == 8)) && value->constant_kind == CHECK_CONSTANT_ADDRESS)
            {
                if (value->object == 0)
                {
                    check_data_integer_bytes(plan->bytes, (uint64_t)value->offset, 8);
                }
                else if ((value->flags & BUSTER_CHECK_STATIC_OBJECT) == 0)
                {
                    status = CHECK_DATA_NON_CONSTANT;
                }
                else if (value->object >= object_count)
                {
                    status = CHECK_DATA_SYMBOL;
                }
                else
                {
                    plan->object = value->object;
                    plan->addend = value->offset;
                }
            }
            else
            {
                status = CHECK_DATA_NON_CONSTANT;
            }
        }
    }
    return status;
}

static bool check_data_nonzero(const uint8_t* bytes, uint64_t size)
{
    bool nonzero = false;
    uint64_t byte = 0;
    for (; !nonzero && size - byte >= 64; byte += 64)
    {
        const __m512i block = _mm512_loadu_si512(bytes + byte);
        nonzero = _mm512_test_epi64_mask(block, block) != 0;
    }
    if (!nonzero && byte != size)
    {
        const __mmask64 mask = (UINT64_C(1) << (size - byte)) - 1;
        const __m512i tail = _mm512_maskz_loadu_epi8(mask, bytes + byte);
        nonzero = _mm512_test_epi64_mask(tail, tail) != 0;
    }
    return nonzero;
}

static uint8_t* check_data_payload(Arena* bytes, Arena* scratch, uint64_t size)
{
    uint8_t* storage = arena_push(bytes, size, 1);
    if (storage == 0)
    {
        // A writable candidate may still be BSS. Otherwise the final byte-count preflight rejects this batch,
        // after validation has retained the same exact demand and diagnostics as a sufficiently large arena.
        storage = arena_push(scratch, size, 1);
    }
    return storage;
}

static bool check_data_sort_fixups(CheckDataPlan* plan, Arena* scratch)
{
    CheckDataFixup* temporary = plan->fixup_count > 1 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckDataFixup, plan->fixup_count) : 0;
    const bool valid = plan->fixup_count <= 1 || temporary != 0;
    CheckDataFixup* references = plan->fixups;
    for (uint32_t shift = 0; valid && plan->fixup_count > 1 && shift < 32; shift += 8)
    {
        uint32_t counts[256] = { 0 };
        for (uint32_t i = 0; i < plan->fixup_count; i += 1)
        {
            counts[(references[i].offset >> shift) & 255] += 1;
        }
        uint32_t total = 0;
        for (uint32_t bucket = 0; bucket < 256; bucket += 1)
        {
            const uint32_t count = counts[bucket];
            counts[bucket] = total;
            total += count;
        }
        for (uint32_t i = 0; i < plan->fixup_count; i += 1)
        {
            const uint32_t bucket = (references[i].offset >> shift) & 255;
            temporary[counts[bucket]] = references[i];
            counts[bucket] += 1;
        }
        CheckDataFixup* swap = references;
        references = temporary;
        temporary = swap;
    }
    return valid;
}

static CheckDataStatus check_data_plan_object(const TypeTable* types, const CheckStream* stream, const CheckDataObject* object,
                                             const SymbolId* object_symbols, uint32_t object_count, const Link* link,
                                             Arena* bytes, Arena* scratch, CheckDataPlan* plan)
{
    *plan = (CheckDataPlan){ 0 };
    const uint64_t byte_start = bytes->length;
    CheckDataStatus status = CHECK_DATA_OK;
    if (!type_is_valid(types, object->type) || !type_is_complete(types, object->type))
    {
        status = CHECK_DATA_INVALID;
    }
    const TypeId base = status == CHECK_DATA_OK ? type_strip_aligned(types, object->type) : 0;
    const uint8_t kind = status == CHECK_DATA_OK ? type_kind(types, base) : 0;
    const bool aggregate = kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION ||
                           kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR;
    if (status == CHECK_DATA_OK)
    {
        const uint32_t natural = type_alignment_log2(types, object->type);
        const uint32_t alignment = object->alignment_log2 > natural ? object->alignment_log2 : natural;
        plan->size = type_size(types, object->type);
        if (object->initializer_plan != 0 && object->initializer_plan->object_size != 0)
        {
            if (object->initializer_plan->object_size < plan->size)
            {
                status = CHECK_DATA_INVALID;
            }
            else
            {
                plan->size = object->initializer_plan->object_size;
            }
        }
        if (alignment > BUSTER_LINK_MAX_ALIGNMENT_LOG2 || plan->size > UINT64_MAX / 8)
        {
            status = CHECK_DATA_UNSUPPORTED;
        }
        else
        {
            plan->alignment_log2 = (uint8_t)alignment;
        }
    }
    if (status == CHECK_DATA_OK)
    {
        if (object->object == 0 || object->object >= object_count || object_symbols[object->object] == 0 || object_symbols[object->object] >= link->symbols.count)
        {
            status = CHECK_DATA_SYMBOL;
        }
        else
        {
            plan->symbol = object_symbols[object->object];
            const uint8_t symbol_type = link->symbols.flags[plan->symbol] & BUSTER_LINK_SYMBOL_TYPE;
            if ((symbol_type != BUSTER_LINK_SYMBOL_OBJECT && symbol_type != BUSTER_LINK_SYMBOL_TLS) ||
                (link->symbols.source[plan->symbol] != BUSTER_LINK_SOURCE_PROGRAM && link->symbols.source[plan->symbol] != BUSTER_LINK_SOURCE_NONE) ||
                link->symbols.atom[plan->symbol] != 0)
            {
                status = CHECK_DATA_SYMBOL;
            }
        }
    }
    const CheckInitializer* initializer = object->initializer_plan;
    uint8_t* live_parts = 0;
    uint32_t live_count = 0;
    if (status == CHECK_DATA_OK && initializer != 0)
    {
        const TypeAnswer compatible = type_compatible(types, type_unqualified(object->type), type_unqualified(initializer->type));
        if (initializer->status != CHECK_INIT_OK || (initializer->count != 0 && initializer->parts == 0))
        {
            status = CHECK_DATA_CHECK_FAILED;
        }
        else if (compatible != TYPE_ANSWER_YES)
        {
            status = compatible == TYPE_ANSWER_FULL ? CHECK_DATA_RESOURCE : CHECK_DATA_INVALID;
        }
        else if (initializer->count != 0)
        {
            const CheckInitStatus references = check_initializer_live(types, initializer, scratch, &live_parts, &live_count);
            status = references == CHECK_INIT_OK ? CHECK_DATA_OK : references == CHECK_INIT_RESOURCE ? CHECK_DATA_RESOURCE : CHECK_DATA_INVALID;
            plan->fixups = status == CHECK_DATA_OK && live_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckDataFixup, live_count) : 0;
            plan->bytes = check_data_payload(bytes, scratch, plan->size);
            if ((live_count != 0 && plan->fixups == 0) || (plan->size != 0 && plan->bytes == 0))
            {
                status = CHECK_DATA_RESOURCE;
            }
            else if (plan->size != 0)
            {
                memset(plan->bytes, 0, plan->size);
            }
        }
    }
    if (status == CHECK_DATA_OK && object->string_pool)
    {
        const uint8_t symbol_type = link->symbols.flags[plan->symbol] & BUSTER_LINK_SYMBOL_TYPE;
        if (initializer != 0 || object->literal_size != plan->size || (plan->size != 0 && object->literal_bytes == 0) ||
            kind != BUSTER_TYPE_KIND_ARRAY || symbol_type != BUSTER_LINK_SYMBOL_OBJECT)
        {
            status = CHECK_DATA_INVALID;
        }
        else if (plan->size != 0)
        {
            plan->bytes = check_data_payload(bytes, scratch, plan->size);
            if (plan->bytes == 0)
            {
                status = CHECK_DATA_RESOURCE;
            }
            else
            {
                memcpy(plan->bytes, object->literal_bytes, plan->size);
            }
        }
    }
    else if (status == CHECK_DATA_OK && initializer == 0 && (!aggregate || object->initializer != BUSTER_CHECK_NONE))
    {
        CheckDataScalar scalar = { 0 };
        status = check_data_plan_scalar(types, stream, object, object_symbols, object_count, link, &scalar);
        if (status == CHECK_DATA_OK)
        {
            plan->bytes = check_data_payload(bytes, scratch, plan->size);
            if (plan->bytes == 0)
            {
                status = CHECK_DATA_RESOURCE;
            }
            else
            {
                memcpy(plan->bytes, scalar.bytes, plan->size);
            }
            if (status == CHECK_DATA_OK && scalar.object != 0)
            {
                plan->fixups = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckDataFixup, 1);
                if (plan->fixups == 0)
                {
                    status = CHECK_DATA_RESOURCE;
                }
                else
                {
                    plan->fixups[0] = (CheckDataFixup){
                        .object = scalar.object,
                        .addend = scalar.addend,
                    };
                    plan->fixup_count = 1;
                }
            }
        }
    }
    for (uint32_t i = 0; status == CHECK_DATA_OK && initializer != 0 && i < initializer->count; i += 1)
    {
        const CheckInitPart* part = &initializer->parts[i];
        const uint64_t offset = part->bit_offset / 8;
        const uint32_t shift = (uint32_t)(part->bit_offset % 8);
        const bool valid_type = type_is_valid(types, part->type);
        uint64_t size = part->kind == CHECK_INIT_VALUE && valid_type ? type_size(types, part->type) : part->byte_count;
        if (part->bit_width != 0)
        {
            size = ((uint64_t)shift + part->bit_width + 7) / 8;
        }
        if (!valid_type || offset > plan->size || size > plan->size - offset ||
            (part->bit_width == 0 && shift != 0) || (part->kind == CHECK_INIT_BYTES && part->bit_width != 0))
        {
            status = CHECK_DATA_INVALID;
        }
        else if (part->kind == CHECK_INIT_CLEAR || part->kind == CHECK_INIT_BYTES)
        {
            if (part->kind == CHECK_INIT_BYTES && size != 0 && part->bytes == 0)
            {
                status = CHECK_DATA_INVALID;
            }
            else if (size != 0)
            {
                if (part->kind == CHECK_INIT_CLEAR && part->bit_width != 0)
                {
                    for (uint32_t bit = 0; bit < part->bit_width; bit += 1)
                    {
                        const uint64_t destination = part->bit_offset + bit;
                        plan->bytes[destination / 8] &= (uint8_t)~(1u << (destination % 8));
                    }
                }
                else if (part->kind == CHECK_INIT_CLEAR)
                {
                    memset(plan->bytes + offset, 0, size);
                }
                else
                {
                    memcpy(plan->bytes + offset, part->bytes, size);
                }
            }
        }
        else if (part->kind == CHECK_INIT_VALUE)
        {
            CheckDiagnostic diagnostic = CHECK_DIAG_NONE;
            CheckValue value = part->value;
            const CheckStream scalar_stream = {
                .values = &value,
                .diagnostics = &diagnostic,
                .count = 1,
            };
            const CheckDataObject scalar_object = {
                .type = part->type,
                .object = object->object,
                .initializer = 0,
                .token = part->token,
            };
            CheckDataScalar scalar = { 0 };
            status = check_data_plan_scalar(types, &scalar_stream, &scalar_object, object_symbols, object_count, link, &scalar);
            if (status == CHECK_DATA_OK && part->bit_width != 0)
            {
                uint32_t width = 0;
                bool is_signed = false;
                if (scalar.object != 0 || !check_action_integer_type(types, part->type, &width, &is_signed) || part->bit_width > width || part->bit_width > 128)
                {
                    status = CHECK_DATA_INVALID;
                }
                else
                {
                    for (uint32_t bit = 0; bit < part->bit_width; bit += 1)
                    {
                        const uint64_t destination = part->bit_offset + bit;
                        const uint8_t mask = (uint8_t)(1u << (destination % 8));
                        const uint8_t value_bit = (uint8_t)((scalar.bytes[bit / 8] >> (bit % 8)) & 1);
                        plan->bytes[destination / 8] = (uint8_t)((plan->bytes[destination / 8] & ~mask) | (value_bit != 0 ? mask : 0));
                    }
                }
            }
            else if (status == CHECK_DATA_OK)
            {
                memcpy(plan->bytes + offset, scalar.bytes, size);
                if (scalar.object != 0 && live_parts[i])
                {
                    if (offset > UINT32_MAX)
                    {
                        status = CHECK_DATA_UNSUPPORTED;
                    }
                    else
                    {
                        plan->fixups[plan->fixup_count] = (CheckDataFixup){
                            .offset = (uint32_t)offset,
                            .object = scalar.object,
                            .addend = scalar.addend,
                        };
                        plan->fixup_count += 1;
                    }
                }
            }
        }
        else
        {
            status = CHECK_DATA_INVALID;
        }
    }
    if (status == CHECK_DATA_OK && !check_data_sort_fixups(plan, scratch))
    {
        status = CHECK_DATA_RESOURCE;
    }
    for (uint32_t i = 0; status == CHECK_DATA_OK && i < plan->fixup_count; i += 1)
    {
        CheckDataFixup* reference = &plan->fixups[i];
        const SymbolId target = object_symbols[reference->object];
        if (target == 0 || target >= link->symbols.count)
        {
            status = CHECK_DATA_SYMBOL;
        }
        else if ((link->symbols.flags[target] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_TLS)
        {
            status = CHECK_DATA_UNSUPPORTED;
        }
        else
        {
            reference->target = target;
        }
    }
    if (status == CHECK_DATA_OK)
    {
        const bool tls = (link->symbols.flags[plan->symbol] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_TLS;
        const bool readonly = (type_qualifiers(base) & BUSTER_TYPE_CONST) != 0;
        const bool scan = !object->string_pool && (tls || !readonly) && plan->fixup_count == 0 && plan->bytes != 0;
        const bool initialized = plan->fixup_count != 0 || (scan && check_data_nonzero(plan->bytes, plan->size));
        plan->class = tls ? (initialized ? BUSTER_LINK_CLASS_TDATA : BUSTER_LINK_CLASS_TBSS) :
                      readonly ? (plan->fixup_count != 0 ? BUSTER_LINK_CLASS_DATA_RELRO : BUSTER_LINK_CLASS_RODATA) :
                      initialized ? BUSTER_LINK_CLASS_DATA : BUSTER_LINK_CLASS_BSS;
        if (object->string_pool)
        {
            plan->class = type_size(types, type_element(types, base)) == 1 ? BUSTER_LINK_CLASS_STRINGS : BUSTER_LINK_CLASS_RODATA;
        }
        if (link_class_is_zero_fill(plan->class))
        {
            arena_pop_to(bytes, byte_start);
            plan->bytes = 0;
        }
        else if (plan->bytes == 0 && plan->size != 0)
        {
            // An implicit readonly zero object needs no temporary image to determine its class or demand.
            // If it does not fit, the batch's final byte-capacity preflight reports the exact count.
            plan->bytes = arena_push(bytes, plan->size, 1);
            if (plan->bytes != 0)
            {
                memset(plan->bytes, 0, plan->size);
            }
        }
        if (plan->size == 0)
        {
            plan->bytes = 0;
        }
    }
    return status;
}

static bool check_data_emit(const TypeTable* types, const CheckStream* stream, const CheckDataObject* objects, uint32_t count,
                            const SymbolId* object_symbols, uint32_t object_count, Link* link, uint32_t unit,
                            Arena* bytes, Arena* scratch, CheckDataResult* result)
{
    *result = (CheckDataResult){
        .failed_record = BUSTER_CHECK_NONE,
        .failed_token = BUSTER_CHECK_NONE,
    };
    const uint64_t scratch_start = scratch != 0 ? scratch->length : 0;
    const uint64_t byte_start = bytes != 0 ? bytes->length : 0;
    if (types == 0 || link == 0 || bytes == 0 || scratch == 0 || bytes == scratch || count > (1u << 30) ||
        (bytes != 0 && bytes->length > bytes->capacity) || (scratch != 0 && scratch->length > scratch->capacity) ||
        (count != 0 && (objects == 0 || object_symbols == 0)) ||
        link->error != LINK_ERROR_NONE || link->atoms.count == 0 || link->symbols.count == 0 || link->fixups.count == 0 || link->fixups.wide_count == 0 ||
        link->atoms.count > link->atoms.capacity || link->symbols.count > link->symbols.capacity ||
        link->fixups.count > link->fixups.capacity || link->fixups.wide_count > link->fixups.wide_capacity)
    {
        result->status = CHECK_DATA_INVALID;
    }
    if (result->status == CHECK_DATA_OK && stream != 0 && (stream->status != CHECK_STATUS_OK || stream->report_count != 0))
    {
        result->status = CHECK_DATA_CHECK_FAILED;
    }
    CheckDataPlan* plans = 0;
    uint64_t* definitions = 0;
    uint64_t byte_count = 0;
    uint32_t fixup_count = 0;
    uint32_t wide_count = 0;
    if (result->status == CHECK_DATA_OK && count != 0)
    {
        plans = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckDataPlan, count);
        const uint64_t words = ((uint64_t)link->symbols.count + 63) / 64;
        definitions = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, words);
        if (plans == 0 || definitions == 0)
        {
            result->status = CHECK_DATA_RESOURCE;
        }
        else
        {
            memset(definitions, 0, words * sizeof(*definitions));
        }
    }
    for (uint32_t i = 0; result->status == CHECK_DATA_OK && i < count; i += 1)
    {
        CheckDataPlan* plan = &plans[i];
        result->status = check_data_plan_object(types, stream, &objects[i], object_symbols, object_count, link, bytes, scratch, plan);
        if (result->status == CHECK_DATA_OK)
        {
            const uint64_t bit = UINT64_C(1) << (plan->symbol % 64);
            if ((definitions[plan->symbol / 64] & bit) != 0)
            {
                result->status = CHECK_DATA_SYMBOL;
            }
            else
            {
                definitions[plan->symbol / 64] |= bit;
                const uint64_t payload = !link_class_is_zero_fill(plan->class) ? plan->size : 0;
                if (payload > UINT64_MAX - byte_count || plan->fixup_count > UINT32_MAX - fixup_count)
                {
                    result->status = CHECK_DATA_RESOURCE;
                }
                else
                {
                    byte_count += payload;
                    fixup_count += plan->fixup_count;
                    for (uint32_t fixup = 0; fixup < plan->fixup_count; fixup += 1)
                    {
                        wide_count += plan->fixups[fixup].addend < INT32_MIN || plan->fixups[fixup].addend > INT32_MAX;
                    }
                }
            }
        }
        if (result->status != CHECK_DATA_OK)
        {
            result->failed_record = i;
            result->failed_token = objects[i].token;
        }
    }
    for (uint32_t i = 0; result->status == CHECK_DATA_OK && i < count; i += 1)
    {
        for (uint32_t fixup = 0; result->status == CHECK_DATA_OK && fixup < plans[i].fixup_count; fixup += 1)
        {
            const SymbolId target = plans[i].fixups[fixup].target;
            if ((link->symbols.flags[target] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL && link->symbols.source[target] != BUSTER_LINK_SOURCE_ABSOLUTE)
            {
                const AtomId atom = link->symbols.atom[target];
                const bool defined_here = (definitions[target / 64] & (UINT64_C(1) << (target % 64))) != 0;
                const bool same_unit = atom != 0 && atom < link->atoms.count && link->atoms.unit[atom] == unit;
                if (!defined_here && !same_unit)
                {
                    result->status = CHECK_DATA_SYMBOL;
                    result->failed_record = i;
                    result->failed_token = objects[i].token;
                }
            }
        }
    }
    if (result->status == CHECK_DATA_OK)
    {
        result->atom_count = count;
        result->fixup_count = fixup_count;
        result->wide_addend_count = wide_count;
        result->byte_count = byte_count;
        if (count > link->atoms.capacity - link->atoms.count || fixup_count > link->fixups.capacity - link->fixups.count ||
            wide_count > link->fixups.wide_capacity - link->fixups.wide_count || byte_count > bytes->capacity - byte_start)
        {
            result->status = CHECK_DATA_RESOURCE;
        }
    }
    // Every condition checked by the Link adds was proved above. No fallible allocation remains after this point.
    // Publishing in record order preserves atom order, each atom's contiguous fixups and source identities.
    if (result->status == CHECK_DATA_OK && count != 0)
    {
        assert(bytes->length - byte_start == byte_count);
        result->atom_begin = link->atoms.count;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const CheckDataPlan* plan = &plans[i];
            const AtomId atom = link_add_atom(link, plan->class, plan->alignment_log2, plan->size, plan->bytes, unit);
            const bool defined = link_define_symbol(link, plan->symbol, atom, 0, plan->size);
            assert(atom != 0 && defined);
            link->symbols.source[plan->symbol] = BUSTER_LINK_SOURCE_PROGRAM;
            for (uint32_t i_fixup = 0; i_fixup < plan->fixup_count; i_fixup += 1)
            {
                const CheckDataFixup* reference = &plan->fixups[i_fixup];
                const FixupId fixup = link_add_fixup(link, atom, reference->offset, BUSTER_LINK_FIXUP_ABS64, reference->target, reference->addend);
                assert(fixup != 0);
            }
        }
    }
    if (scratch != 0)
    {
        arena_pop_to(scratch, scratch_start);
    }
    if (result->status != CHECK_DATA_OK && bytes != 0)
    {
        arena_pop_to(bytes, byte_start);
    }
    return result->status == CHECK_DATA_OK;
}

#endif

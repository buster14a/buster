// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The unit's immutable x87 literals (#1387), prepared before any function lane runs. Keys are the ten meaningful
// bytes, read as integer fields; neither host floating arithmetic nor padding participates in sorting or equality.
// Existing initializer symbols and atoms own the images, so object output and the in-process linker share them.

static bool check_lower_x87_literal_type(const TypeTable* types, TypeId type)
{
    const TypeId plain = type_conversion_operand(types, type);
    return plain == BUSTER_TYPE_LONG_DOUBLE || plain == BUSTER_TYPE_COMPLEX_LONG_DOUBLE;
}

// Ordinary streams classify 16 type IDs at a time; only a derived wrapper needs the type rules. The dense stream is
// the hand-built tests' representation and uses the exact scalar classifier. Nothing is allocated for a unit without
// a long-double expression or converted initializer. A runtime ++ or classifier also needs synthesized literals.
static bool check_lower_x87_literals_present(const CheckProgram* program, bool scalar)
{
    (void)scalar;

    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const uint32_t count = stream != NULL && check_stream_has_values(stream) ? stream->count : 0;
    bool found = false;
    uint32_t first = 0;
    if (BUSTER_VECTOR_PATH(!scalar) && stream != NULL && stream->dense_values == NULL)
    {
        for (; first < count && !found; first += 16)
        {
            const uint32_t remaining = count - first;
            const __mmask16 active = remaining < 16 ? (__mmask16)((1u << remaining) - 1) : UINT16_MAX;
            const __m512i ids = _mm512_and_si512(_mm512_maskz_loadu_epi32(active, stream->value_types + first), _mm512_set1_epi32(~BUSTER_TYPE_QUALIFIERS));
            found = (_mm512_mask_cmpeq_epi32_mask(active, ids, _mm512_set1_epi32(BUSTER_TYPE_LONG_DOUBLE)) |
                     _mm512_mask_cmpeq_epi32_mask(active, ids, _mm512_set1_epi32(BUSTER_TYPE_COMPLEX_LONG_DOUBLE))) != 0;
            uint32_t derived = _mm512_mask_cmpge_epu32_mask(active, ids, _mm512_set1_epi32(BUSTER_TYPE_BUILTIN_COUNT << BUSTER_TYPE_INDEX_SHIFT));
            while (derived != 0 && !found)
            {
                const uint32_t node = first + (uint32_t)__builtin_ctz(derived);
                const TypeId type = stream->value_types[node];
                const uint8_t kind = type_kind(types, type);
                found = (kind >= BUSTER_TYPE_KIND_WRAPPERS || kind == BUSTER_TYPE_KIND_ATOMIC || kind == BUSTER_TYPE_KIND_COMPLEX) &&
                        check_lower_x87_literal_type(types, type);
                derived &= derived - 1;
            }
        }
    }
    else
    {
        for (; first < count && !found; first += 1)
        {
            found = check_lower_x87_literal_type(types, check_stream_type(stream, first));
        }
    }
    const uint32_t initializers = program->initializers != NULL ? program->initializers->count : 0;
    for (uint32_t i = 0; i < initializers && !found; i += 1)
    {
        const CheckInitializer* initializer = program->initializers->plans + i;
        for (uint32_t part = 0; part < initializer->count && !found; part += 1)
        {
            const CheckInitPart* value = initializer->parts + part;
            const bool floating = value->value.constant_kind == CHECK_CONSTANT_FLOATING || value->value.constant_kind == CHECK_CONSTANT_COMPLEX;
            found = value->kind == CHECK_INIT_VALUE && floating && check_lower_x87_literal_type(types, value->value.type);
        }
    }
    return found;
}

static uint64_t check_lower_x87_literal_value(const TypeTable* types, const CheckValue* value, uint64_t* keys, uint64_t count)
{
    const bool complex = value->constant_kind == CHECK_CONSTANT_COMPLEX;
    const bool floating = value->constant_kind == CHECK_CONSTANT_FLOATING || complex;
    if (floating && check_lower_x87_literal_type(types, value->type))
    {
        if (keys != NULL)
        {
            keys[2 * count] = value->floating_storage[0];
            keys[2 * count + 1] = value->floating_storage[1] & UINT64_C(0xffff);
        }
        count += 1;
        if (complex)
        {
            if (keys != NULL)
            {
                keys[2 * count] = value->imaginary_storage[0];
                keys[2 * count + 1] = value->imaginary_storage[1] & UINT64_C(0xffff);
            }
            count += 1;
        }
    }
    return count;
}

// Sparse payload rows contain every nonzero floating constant. +0 is supplied explicitly, and initializer parts
// supply converted constants which need not be the graph's original value. Two passes first count exact room, then
// populate it; the rows and initializer plans are frozen throughout both passes.
static uint64_t check_lower_x87_literal_keys(const CheckProgram* program, uint64_t* keys)
{
    const CheckStream* stream = program->stream;
    const CheckValue* values = stream != NULL ? (stream->dense_values != NULL ? stream->dense_values : stream->payloads) : NULL;
    const uint32_t rows = stream != NULL ? (stream->dense_values != NULL ? stream->count : stream->payload_count) : 0;
    uint64_t count = 0;
    for (uint32_t row = 0; row < rows && count <= UINT32_MAX; row += 1)
    {
        count = check_lower_x87_literal_value(program->types, values + row, keys, count);
    }
    const uint32_t initializers = program->initializers != NULL ? program->initializers->count : 0;
    for (uint32_t i = 0; i < initializers && count <= UINT32_MAX; i += 1)
    {
        const CheckInitializer* initializer = program->initializers->plans + i;
        for (uint32_t part = 0; part < initializer->count && count <= UINT32_MAX; part += 1)
        {
            if (initializer->parts[part].kind == CHECK_INIT_VALUE)
            {
                count = check_lower_x87_literal_value(program->types, &initializer->parts[part].value, keys, count);
            }
        }
    }
    // The lowering synthesizes +0, +1, +infinity and the smallest normal for initialization, updates and strict
    // classification. Spell their target bits, not host floating expressions or comparisons.
    static const uint64_t generated[8] = {
        0,
        0,
        UINT64_C(0x8000000000000000),
        UINT64_C(0x3fff),
        UINT64_C(0x8000000000000000),
        UINT64_C(0x7fff),
        UINT64_C(0x8000000000000000),
        1,
    };
    const bool fits = count <= UINT32_MAX - 4;
    if (keys != NULL && fits)
    {
        memcpy(keys + 2 * count, generated, sizeof(generated));
    }
    return fits ? count + 4 : (uint64_t)UINT32_MAX + 1;
}

static bool check_lower_x87_literal_pools(CheckLowerInitializers* pools, const CheckProgram* program, Arena* output, Arena* scratch, bool scalar)
{
    const uint64_t output_mark = output->length;
    const bool present = check_lower_x87_literals_present(program, scalar);
    const uint64_t count64 = present ? check_lower_x87_literal_keys(program, NULL) : 0;
    const uint64_t mark = scratch->length;
    bool valid = count64 <= UINT32_MAX;
    const uint32_t count = valid ? (uint32_t)count64 : 0;
    uint64_t* keys = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, 2 * count64) : NULL;
    uint64_t* order = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, count64) : NULL;
    uint64_t* temporary = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, count64) : NULL;
    valid = valid && (count == 0 || (keys != NULL && order != NULL && temporary != NULL));
    if (valid && count != 0)
    {
        valid = check_lower_x87_literal_keys(program, keys) == count;
        for (uint32_t i = 0; i < count; i += 1)
        {
            order[i] = i;
        }
        // Stable radix passes over low 32, high 32, then sign/exponent. Reuse the existing SIMD/scalar kernel;
        // original ordinals travel in the low half. Sorting ten significant bytes is independent of payload kind.
        for (uint32_t word = 0; word < 3; word += 1)
        {
            uint32_t any = 0;
            uint32_t every = UINT32_MAX;
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t ordinal = (uint32_t)order[i];
                const uint32_t key = (uint32_t)(keys[2 * (uint64_t)ordinal + (word == 2)] >> (word == 1 ? 32 : 0));
                order[i] = (uint64_t)key << 32 | ordinal;
                any |= key;
                every &= key;
            }
            uint64_t* sorted = c_bindings_sort(order, temporary, count, !BUSTER_SCALAR_PATH(scalar), any ^ every);
            temporary = sorted == order ? temporary : order;
            order = sorted;
        }
    }
    uint32_t distinct = 0;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const uint32_t here = (uint32_t)order[i];
        const uint32_t prior = i != 0 ? (uint32_t)order[i - 1] : here;
        distinct += i == 0 || keys[2 * (uint64_t)here] != keys[2 * (uint64_t)prior] || keys[2 * (uint64_t)here + 1] != keys[2 * (uint64_t)prior + 1];
    }
    const uint64_t total = (uint64_t)pools->count + distinct;
    valid = valid && total <= UINT32_MAX;
    CheckLowerInitPool* items = valid && distinct != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckLowerInitPool, total) : NULL;
    uint64_t* images = valid && distinct != 0 ? arena_push(output, (uint64_t)distinct * 16, 16) : NULL;
    valid = valid && (distinct == 0 || (items != NULL && images != NULL));
    if (valid && distinct != 0)
    {
        if (pools->count != 0)
        {
            memcpy(items, pools->items, (uint64_t)pools->count * sizeof(CheckLowerInitPool));
        }
        uint32_t written = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t here = (uint32_t)order[i];
            const uint64_t low = keys[2 * (uint64_t)here];
            const uint64_t high = keys[2 * (uint64_t)here + 1];
            if (written == 0 || low != images[2 * (uint64_t)(written - 1)] || high != images[2 * (uint64_t)(written - 1) + 1])
            {
                uint8_t* image = (uint8_t*)(images + 2 * (uint64_t)written);
                memset(image, 0, 16);
                check_data_integer_bytes(image, low, 8);
                check_data_integer_bytes(image + 8, high, 2);
                items[pools->count + written] = (CheckLowerInitPool){
                    .bytes = image,
                    .size = 16,
                    .part = BUSTER_CHECK_NONE,
                    .alignment_log2 = 4,
                };
                written += 1;
            }
        }
        pools->x87_first = pools->count;
        pools->x87_count = distinct;
        pools->x87_keys = images;
        pools->items = items;
        pools->count = (uint32_t)total;
    }
    arena_pop_to(scratch, mark);
    if (!valid)
    {
        arena_pop_to(output, output_mark);
    }
    return valid;
}

// All keys come from the immutable payloads above. A newly synthesized value an optional pool doesn't know leaves
// its whole function on the established materialization path; an uninitialized constant home is never published.
static SymbolId check_lower_x87_literal_symbol(const CheckLowerInitializers* pools, uint64_t low, uint64_t high)
{
    const uint64_t* keys = pools->x87_keys;
    const uint32_t count = pools->x87_count;
    high &= UINT64_C(0xffff);
    uint32_t first = 0;
    uint32_t end = count;
    while (first < end)
    {
        const uint32_t middle = first + (end - first) / 2;
        const uint64_t upper = keys[2 * (uint64_t)middle + 1];
        if (upper < high || (upper == high && keys[2 * (uint64_t)middle] < low))
        {
            first = middle + 1;
        }
        else
        {
            end = middle;
        }
    }
    const bool found = first < count && keys[2 * (uint64_t)first] == low && keys[2 * (uint64_t)first + 1] == high;
    return found ? pools->symbol_base + pools->x87_first + first : 0;
}

// A symbol-backed CONST performs FLD/FSTP at its definition instead of constructing bytes with integer stores. Keep
// signaling NaNs and unsupported encodings on the original path, whose first x87 use determines their exception
// point. Infinity and quiet NaNs stay there too until their transfer contract is qualified independently. Canonical
// finite values, including signed zeros and m80 subnormals, incur no format conversion or extra rounding.
static bool check_lower_x87_literal_load_safe(uint64_t low, uint64_t high)
{
    const uint32_t exponent = (uint32_t)high & 0x7fff;
    const bool integer = (low >> 63) != 0;
    return exponent == 0 ? !integer : exponent != 0x7fff && integer;
}

#include <buster/lib/compiler/assembly/aarch64_exact_bridge.h>
#include <buster/lib/compiler/assembly/generated/aarch64-exact-crosswalk.generated.h>

BUSTER_GLOBAL_LOCAL bool a64_exact_string_equal(String8 left, String8 right)
{
    return left.length == right.length && (!left.length || memcmp(left.pointer, right.pointer, left.length) == 0);
}

BUSTER_GLOBAL_LOCAL bool a64_exact_crosswalk_row(u32 index, BusterAarch64ExactCrosswalkRow const** result)
{
    if (!result || index >= BUSTER_AARCH64_EXACT_CROSSWALK_COUNT)
    {
        return false;
    }
    *result = &buster_aarch64_exact_crosswalk_rows[index];
    return true;
}

u32 a64_exact_crosswalk_count(void)
{
    return BUSTER_AARCH64_EXACT_CROSSWALK_COUNT;
}

bool a64_exact_crosswalk(u32 index, A64ExactCrosswalkEntry* result)
{
    BusterAarch64ExactCrosswalkRow const* row = 0;
    if (!result || !a64_exact_crosswalk_row(index, &row))
    {
        return false;
    }
    *result = (A64ExactCrosswalkEntry){
        .llvm_name = row->llvm_name,
        .llvm_form_id = row->llvm_form_id,
        .llvm_source_hash = row->llvm_source_hash,
        .canonical = {.form_index = row->canonical_form_index, .row_digest = row->arm_row_digest},
        .llvm_field_count = row->llvm_field_count,
        .canonical_field_count = row->canonical_field_count,
    };
    return true;
}

bool a64_exact_lookup(String8 llvm_name, A64ExactFormKey* result)
{
    if (!result || !llvm_name.pointer || !llvm_name.length)
    {
        return false;
    }
    for (u32 index = 0; index < BUSTER_AARCH64_EXACT_CROSSWALK_COUNT; index += 1)
    {
        BusterAarch64ExactCrosswalkRow const* row = &buster_aarch64_exact_crosswalk_rows[index];
        if (a64_exact_string_equal(row->llvm_name, llvm_name))
        {
            *result = (A64ExactFormKey){.form_index = row->canonical_form_index, .row_digest = row->arm_row_digest};
            return true;
        }
    }
    return false;
}

bool a64_exact_key(u32 index, A64ExactFormKey* result)
{
    BusterAarch64ExactCrosswalkRow const* row = 0;
    if (!result || !a64_exact_crosswalk_row(index, &row))
    {
        return false;
    }
    *result = (A64ExactFormKey){.form_index = row->canonical_form_index, .row_digest = row->arm_row_digest};
    return true;
}

bool a64_exact_key_valid(A64ExactFormKey key)
{
    BusterAarch64CanonicalFormInfo form = {0};
    return key.row_digest != 0 && buster_aarch64_canonical_form(key.form_index, &form) && form.arm_row_digest == key.row_digest;
}

BUSTER_GLOBAL_LOCAL bool a64_exact_copy_normalized(u32 const* source, u32 source_count, u32* destination, u32 capacity,
                                                    u32* count)
{
    if (!count || source_count > 8 || source_count > capacity || (source_count && (!source || !destination)))
    {
        return false;
    }
    for (u32 index = 0; index < source_count; index += 1)
    {
        destination[index] = source[index];
    }
    *count = source_count;
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_exact_split_addsub_immediate(u32 const* source, u32 source_count, u32* destination, u32 capacity,
                                                          u32* count)
{
    // LLVM's ADDXri/SUBSXri plan stores {imm12,sh} as one fourteen-bit
    // source field.  Arm canonical rows expose imm12 and sh separately.
    if (!source || source_count != 3 || !destination || capacity < 4 || !count || (source[0] & ~31u) || (source[1] & ~31u) ||
        (source[2] & ~UINT32_C(0x1fff)))
    {
        return false;
    }
    destination[0] = source[0];
    destination[1] = source[1];
    destination[2] = (source[2] & UINT32_C(0x0fff));
    destination[3] = source[2] >> 12;
    *count = 4;
    return true;
}

bool a64_exact_normalize_ubfm_wri(u32 const* source, u32 source_count, u32* destination, u32 capacity, u32* count)
{
    // LLVM's UBFMWri is already the architectural Rd,Rn,imms,immr order;
    // unlike the alias spelling UBFIZ, no lsb/width projection is attempted.
    if (!source || source_count != 4 || !destination || capacity < 4 || !count)
    {
        return false;
    }
    for (u32 index = 0; index < 4; index += 1)
    {
        // UBFM's W form is constrained to a 32-bit element: both immediates
        // are five-bit values even though the architectural field is six bits.
        if (source[index] > 31u)
        {
            return false;
        }
        destination[index] = source[index];
    }
    *count = 4;
    return true;
}

bool a64_exact_normalize_subs_xri(u32 const* source, u32 source_count, u32* destination, u32 capacity, u32* count)
{
    return a64_exact_split_addsub_immediate(source, source_count, destination, capacity, count);
}

bool a64_exact_normalize_add_xri(u32 const* source, u32 source_count, u32* destination, u32 capacity, u32* count)
{
    return a64_exact_split_addsub_immediate(source, source_count, destination, capacity, count);
}

bool a64_exact_normalize(u32 crosswalk_index, u32 const* source, u32 source_count, u32* destination, u32 capacity, u32* count)
{
    BusterAarch64ExactCrosswalkRow const* row = 0;
    if (!a64_exact_crosswalk_row(crosswalk_index, &row))
    {
        return false;
    }
    switch ((BusterAarch64ExactNormalizer)row->normalizer)
    {
    case BUSTER_AARCH64_EXACT_NORMALIZE_UBFM_WRI:
        return a64_exact_normalize_ubfm_wri(source, source_count, destination, capacity, count);
    case BUSTER_AARCH64_EXACT_NORMALIZE_SUBS_XRI:
        return a64_exact_normalize_subs_xri(source, source_count, destination, capacity, count);
    case BUSTER_AARCH64_EXACT_NORMALIZE_ADD_XRI:
        return a64_exact_normalize_add_xri(source, source_count, destination, capacity, count);
    case BUSTER_AARCH64_EXACT_NORMALIZE_IDENTITY:
        // The LLVM production plan packs the shift opcode's imm6 and shift
        // kind into one eight-bit source field.  Arm's canonical row keeps
        // those as separate fields (and places Rm between them).
        if (row->canonical_field_count == 5 && source_count == 4)
        {
            if (!source || !destination || capacity < 5 || !count || (source[0] & ~31u) || (source[1] & ~31u) ||
                (source[2] & ~UINT32_C(0xff)) || (source[3] & ~31u))
            {
                return false;
            }
            destination[0] = source[0];
            destination[1] = source[1];
            destination[2] = source[2] & UINT32_C(0x3f);
            destination[3] = source[3];
            destination[4] = source[2] >> 6;
            *count = 5;
            return true;
        }
        return a64_exact_copy_normalized(source, source_count, destination, capacity, count);
    default:
        return false;
    }
}

bool a64_exact_emit(A64ExactFormKey key, u32 const* normalized_fields, u32 field_count, u32* word)
{
    if (!word || !a64_exact_key_valid(key))
    {
        return false;
    }
    BusterAarch64CanonicalFormInfo form = {0};
    if (!buster_aarch64_canonical_form(key.form_index, &form) || form.arm_row_digest != key.row_digest ||
        field_count != form.field_count)
    {
        return false;
    }
    return buster_aarch64_canonical_raw_encode(key.form_index, normalized_fields, field_count, word);
}

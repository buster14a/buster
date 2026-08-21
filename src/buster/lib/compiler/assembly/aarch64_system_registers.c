#include <buster/lib/compiler/assembly/aarch64_system_registers.h>
#include <buster/lib/integer.h>
#include <buster/lib/compiler/assembly/generated/aarch64-system-registers.generated.h>

BUSTER_GLOBAL_LOCAL bool a64_sysreg_parameter_encoding(BusterA64SysregGeneratedRow row, u32 index, u16* result);

BUSTER_GLOBAL_LOCAL bool a64_sysreg_string_valid(String8 value)
{
    return value.length == 0 || value.pointer != 0;
}

BUSTER_GLOBAL_LOCAL bool a64_sysreg_encoding_valid(u16 packed)
{
    return packed != (u16)0xffffu;
}

BUSTER_GLOBAL_LOCAL u8 a64_sysreg_mode_or(u8 left, u8 right)
{
    return (u8)(left | right);
}

BUSTER_GLOBAL_LOCAL bool a64_sysreg_string_at(u32 offset, String8* result)
{
    if (!result || offset >= BUSTER_A64_SYSREG_GENERATED_POOL_SIZE) return false;
    u32 end = offset;
    while (end < BUSTER_A64_SYSREG_GENERATED_POOL_SIZE && buster_a64_sysreg_generated_pool[end]) end += 1;
    if (end >= BUSTER_A64_SYSREG_GENERATED_POOL_SIZE) return false;
    *result = (String8){.pointer = (char8*)buster_a64_sysreg_generated_pool + offset, .length = end - offset};
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_sysreg_row(u32 index, Aarch64SystemRegister* result)
{
    if (!result || index >= BUSTER_A64_SYSREG_GENERATED_ROW_COUNT) return false;
    BusterA64SysregGeneratedRow row = buster_a64_sysreg_generated_rows[index];
    if (!a64_sysreg_encoding_valid(row.packed_encoding)) return false;
    Aarch64SystemRegister value = {0};
    if (!a64_sysreg_string_at(row.name_offset, &value.name) || !a64_sysreg_string_at(row.target_offset, &value.accessor_target) ||
        !a64_sysreg_string_at(row.source_file_offset, &value.source_file) ||
        !a64_sysreg_string_at(row.accessor_offset, &value.accessor)) return false;
    value.feature_profile_id = row.feature_profile_id;
    value.packed_encoding = row.packed_encoding;
    value.mechanism = row.mechanism;
    value.mode = row.mechanism == AARCH64_SYSTEM_REGISTER_MRS || row.mechanism == AARCH64_SYSTEM_REGISTER_MRRS ?
                     AARCH64_SYSTEM_REGISTER_MODE_READ : AARCH64_SYSTEM_REGISTER_MODE_WRITE;
    value.parameter_start = row.array_start;
    value.parameter_end = row.array_end;
    value.feature_digest = row.feature_digest;
    value.source_digest = row.source_digest;
    value.access_digest = row.access_digest;
    value.accessor_alias = (row.flags & BUSTER_A64_SYSREG_ROW_FLAG_ALIAS) != 0;
    value.parameterized = (row.flags & BUSTER_A64_SYSREG_ROW_FLAG_PARAMETERIZED) != 0;
    value.raw_s3 = (row.flags & BUSTER_A64_SYSREG_ROW_FLAG_RAW_S3) != 0;
    *result = value;
    return true;
}

u32 aarch64_system_register_count(void)
{
    return BUSTER_A64_SYSREG_GENERATED_ROW_COUNT;
}

bool aarch64_system_register_at(u32 index, Aarch64SystemRegister* result)
{
    return a64_sysreg_row(index, result);
}

Aarch64SystemRegisterCensus aarch64_system_register_census(void)
{
    return (Aarch64SystemRegisterCensus){
        .relevant_mechanism_count = BUSTER_A64_SYSREG_RELEVANT_MECHANISM_COUNT,
        .accepted_mechanism_count = BUSTER_A64_SYSREG_ACCEPTED_MECHANISM_COUNT,
        .fixed_count = BUSTER_A64_SYSREG_FIXED_COUNT,
        .parameterized_count = BUSTER_A64_SYSREG_PARAMETERIZED_COUNT,
        .fixed_target_name_count = BUSTER_A64_SYSREG_FIXED_TARGET_NAME_COUNT,
        .fixed_encoding_count = BUSTER_A64_SYSREG_FIXED_ENCODING_COUNT,
        .readable_fixed_name_count = BUSTER_A64_SYSREG_READABLE_FIXED_NAME_COUNT,
        .writable_fixed_name_count = BUSTER_A64_SYSREG_WRITABLE_FIXED_NAME_COUNT,
        .both_fixed_name_count = BUSTER_A64_SYSREG_BOTH_FIXED_NAME_COUNT,
        .source_fixed_row_count = BUSTER_A64_SYSREG_FIXED_COUNT,
        .source_parameterized_row_count = BUSTER_A64_SYSREG_SOURCE_PARAMETERIZED_ROW_COUNT,
        .source_raw_s3_row_count = BUSTER_A64_SYSREG_SOURCE_RAW_S3_ROW_COUNT,
    };
}

BUSTER_GLOBAL_LOCAL bool a64_sysreg_name_equal(String8 left, String8 right)
{
    if (!a64_sysreg_string_valid(left) || !a64_sysreg_string_valid(right) || left.length != right.length) return false;
    for (u64 index = 0; index < left.length; index += 1)
    {
        char8 a = left.pointer[index], b = right.pointer[index];
        if (a >= 'a' && a <= 'z') a = (char8)(a - ('a' - 'A'));
        if (b >= 'a' && b <= 'z') b = (char8)(b - ('a' - 'A'));
        if (a != b) return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_sysreg_name_less(String8 left, String8 right)
{
    bool result;
    if (!a64_sysreg_string_valid(left) || !a64_sysreg_string_valid(right))
    {
        result = false;
    }
    else
    {
        u64 count = BUSTER_MIN(left.length, right.length);
        int cmp = 0;
        for (u64 index = 0; index < count; index += 1)
        {
            char8 a = left.pointer[index], b = right.pointer[index];
            if (a >= 'a' && a <= 'z') a = (char8)(a - ('a' - 'A'));
            if (b >= 'a' && b <= 'z') b = (char8)(b - ('a' - 'A'));
            if (a != b) { cmp = a < b ? -1 : 1; break; }
        }
        result = cmp < 0 || (cmp == 0 && left.length < right.length);
    }

    return result;
}

bool aarch64_system_register_lookup_name(String8 name, Aarch64SystemRegisterLookup* result)
{
    if (!result || !name.length || !a64_sysreg_string_valid(name)) return false;
    Aarch64SystemRegisterLookup value = {0};
    String8 direct_name = {0};
    u16 direct_encoding = 0;
    u8 direct_mode = AARCH64_SYSTEM_REGISTER_MODE_NONE;
    u8 direct_count = 0;
    bool found_direct = false;
    for (u32 index = 0; index < aarch64_system_register_count(); index += 1)
    {
        Aarch64SystemRegister row = {0};
        if (!a64_sysreg_row(index, &row) || row.raw_s3 || row.parameterized || !a64_sysreg_name_equal(row.name, name)) continue;
        // Accessor aliases describe another spelling's provenance.  They must
        // never select or add modes to an ordinary direct-name lookup.
        if (row.accessor_alias) continue;
        if (!found_direct)
        {
            direct_name = row.name;
            direct_encoding = row.packed_encoding;
            direct_mode = row.mode;
            direct_count = 1;
            found_direct = true;
        }
        else if (row.packed_encoding != direct_encoding)
        {
            // A name with more than one direct encoding is ambiguous.  Keep
            // the caller's result untouched and fail closed.
            return false;
        }
        else
        {
            direct_mode = a64_sysreg_mode_or(direct_mode, row.mode);
            if (direct_count != UINT8_MAX) direct_count = (u8)(direct_count + 1u);
        }
    }
    if (!found_direct) return false;
    value.canonical_name = direct_name;
    value.packed_encoding = direct_encoding;
    value.mode = direct_mode;
    value.mechanism_count = direct_count;
    *result = value;
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_sysreg_match_expanded_family(String8 family, String8 expanded, u32* index)
{
    if (!family.length || !expanded.length || !index || !a64_sysreg_string_valid(family) || !a64_sysreg_string_valid(expanded)) return false;
    u64 left = 0, right = 0; bool saw_parameter = false; u32 value = 0;
    while (left < family.length)
    {
        if (family.pointer[left] != '<')
        {
            if (right >= expanded.length) return false;
            char8 a = family.pointer[left], b = expanded.pointer[right];
            if (a >= 'a' && a <= 'z') a = (char8)(a - ('a' - 'A'));
            if (b >= 'a' && b <= 'z') b = (char8)(b - ('a' - 'A'));
            if (a != b) return false;
            left += 1; right += 1; continue;
        }
        u64 end = left + 1; while (end < family.length && family.pointer[end] != '>') end += 1;
        if (end >= family.length || right >= expanded.length) return false;
        u64 start = right; u32 parsed = 0;
        while (right < expanded.length && expanded.pointer[right] >= '0' && expanded.pointer[right] <= '9')
        {
            if (parsed > (UINT32_MAX - (u32)(expanded.pointer[right] - '0')) / 10u) return false;
            parsed = parsed * 10u + (u32)(expanded.pointer[right] - '0'); right += 1;
        }
        if (right == start) return false;
        if (!saw_parameter) { value = parsed; saw_parameter = true; }
        else if (value != parsed) return false;
        left = end + 1;
    }
    if (right != expanded.length || !saw_parameter) return false;
    *index = value;
    return true;
}

bool aarch64_system_register_lookup_expanded_name(String8 name, Aarch64SystemRegisterLookup* result)
{
    if (!result || !name.length || !a64_sysreg_string_valid(name)) return false;
    Aarch64SystemRegisterLookup value = {0};
    u16 direct_encoding = 0;
    u8 direct_mode = AARCH64_SYSTEM_REGISTER_MODE_NONE;
    u8 direct_count = 0;
    bool found_direct = false;
    for (u32 i = 0; i < aarch64_system_register_count(); i += 1)
    {
        BusterA64SysregGeneratedRow row = buster_a64_sysreg_generated_rows[i];
        if (!(row.flags & BUSTER_A64_SYSREG_ROW_FLAG_PARAMETERIZED) || (row.flags & BUSTER_A64_SYSREG_ROW_FLAG_RAW_S3)) continue;
        String8 family = {0}, target = {0};
        if (!a64_sysreg_string_at(row.name_offset, &family) || !a64_sysreg_string_at(row.target_offset, &target)) continue;
        u32 index = 0;
        if (!a64_sysreg_match_expanded_family(family, name, &index) && !a64_sysreg_match_expanded_family(target, name, &index)) continue;
        u16 packed = 0;
        if (!a64_sysreg_parameter_encoding(row, index, &packed)) continue;
        // Parameterized accessor aliases are provenance only.  If direct
        // families disagree, no alias may hide that ambiguity.
        if (row.flags & BUSTER_A64_SYSREG_ROW_FLAG_ALIAS) continue;
        Aarch64SystemRegisterMode mode = row.mechanism == AARCH64_SYSTEM_REGISTER_MRS || row.mechanism == AARCH64_SYSTEM_REGISTER_MRRS ?
                                             AARCH64_SYSTEM_REGISTER_MODE_READ : AARCH64_SYSTEM_REGISTER_MODE_WRITE;
        if (!found_direct)
        {
            value.canonical_name = name;
            value.packed_encoding = packed;
            value.mode = (u8)mode;
            value.mechanism_count = 1;
            value.parameterized = 1;
            direct_encoding = packed;
            direct_mode = (u8)mode;
            direct_count = 1;
            found_direct = true;
        }
        else if (direct_encoding == packed)
        {
            direct_mode = a64_sysreg_mode_or(direct_mode, (u8)mode);
            if (direct_count != UINT8_MAX) direct_count = (u8)(direct_count + 1u);
        }
        else
        {
            // Multiple direct encodings for one expanded spelling are
            // ambiguous regardless of source-row order.
            return false;
        }
    }
    if (!found_direct) return false;
    value.mode = direct_mode;
    value.mechanism_count = direct_count;
    *result = value;
    return true;
}

bool aarch64_system_register_lookup_encoding(u16 packed_encoding, Aarch64SystemRegisterLookup* result)
{
    if (!result || !a64_sysreg_encoding_valid(packed_encoding)) return false;
    Aarch64SystemRegisterLookup value = {0};
    bool found = false;
    bool selected_alias = true;
    for (u32 index = 0; index < aarch64_system_register_count(); index += 1)
    {
        Aarch64SystemRegister row = {0};
        if (!a64_sysreg_row(index, &row) || row.raw_s3 || row.parameterized || row.packed_encoding != packed_encoding) continue;
        if (!found)
        {
            value.canonical_name = row.name;
            value.packed_encoding = packed_encoding;
            value.mode = row.mode;
            value.alias_count = row.accessor_alias ? 1 : 0;
            value.mechanism_count = 1;
            selected_alias = row.accessor_alias;
            found = true;
        }
        else
        {
            value.mode = a64_sysreg_mode_or(value.mode, row.mode);
            if (value.mechanism_count != UINT8_MAX) value.mechanism_count += 1;
            if (row.accessor_alias && value.alias_count != UINT8_MAX) value.alias_count += 1;
            // Prefer a direct (non-accessor-alias) spelling, then lexical order.
            if ((!row.accessor_alias && selected_alias) || (row.accessor_alias == selected_alias && a64_sysreg_name_less(row.name, value.canonical_name)))
            {
                value.canonical_name = row.name;
                selected_alias = row.accessor_alias;
            }
        }
    }
    if (found) *result = value;
    return found;
}

bool aarch64_system_register_name_is_eligible(String8 name, Aarch64SystemRegisterMode mode)
{
    if (!a64_sysreg_string_valid(name) || !name.length) return false;
    if (mode != AARCH64_SYSTEM_REGISTER_MODE_READ && mode != AARCH64_SYSTEM_REGISTER_MODE_WRITE && mode != AARCH64_SYSTEM_REGISTER_MODE_READ_WRITE)
        return false;
    Aarch64SystemRegisterLookup lookup = {0};
    if (!aarch64_system_register_lookup_name(name, &lookup) && !aarch64_system_register_lookup_expanded_name(name, &lookup)) return false;
    return (lookup.mode & mode) == mode;
}

BUSTER_GLOBAL_LOCAL bool a64_sysreg_decimal(String8 text, u32* value)
{
    if (!text.length || !value || !a64_sysreg_string_valid(text)) return false;
    u32 result = 0;
    for (u64 index = 0; index < text.length; index += 1)
    {
        char8 c = text.pointer[index];
        if (c < '0' || c > '9' || result > (UINT32_MAX - (u32)(c - '0')) / 10u) return false;
        result = result * 10u + (u32)(c - '0');
    }
    *value = result;
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_sysreg_match_prefix(String8 text, u64* position, char const* prefix)
{
    if (!a64_sysreg_string_valid(text) || !position || !prefix) return false;
    String8 p = string_from_pointer((char8*)prefix);
    if (*position > text.length || p.length > text.length - *position || !a64_sysreg_name_equal(string_slice(text, *position, *position + p.length), p)) return false;
    *position += p.length;
    return true;
}

bool aarch64_system_register_parse_raw_s3(String8 text, u16* packed_encoding)
{
    if (!packed_encoding || !text.length || !a64_sysreg_string_valid(text)) return false;
    u64 at = 0; u32 op1 = 0, crn = 0, crm = 0, op2 = 0;
    if (!a64_sysreg_match_prefix(text, &at, "S3_")) return false;
    u64 start = at; while (at < text.length && text.pointer[at] >= '0' && text.pointer[at] <= '9') at += 1;
    if (!a64_sysreg_decimal(string_slice(text, start, at), &op1) || op1 > 7 || !a64_sysreg_match_prefix(text, &at, "_C")) return false;
    start = at; while (at < text.length && text.pointer[at] >= '0' && text.pointer[at] <= '9') at += 1;
    if (!a64_sysreg_decimal(string_slice(text, start, at), &crn) || crn > 15 || !a64_sysreg_match_prefix(text, &at, "_C")) return false;
    start = at; while (at < text.length && text.pointer[at] >= '0' && text.pointer[at] <= '9') at += 1;
    if (!a64_sysreg_decimal(string_slice(text, start, at), &crm) || crm > 15 || !a64_sysreg_match_prefix(text, &at, "_")) return false;
    start = at; while (at < text.length && text.pointer[at] >= '0' && text.pointer[at] <= '9') at += 1;
    if (!a64_sysreg_decimal(string_slice(text, start, at), &op2) || op2 > 7 || at != text.length) return false;
    u16 packed = (u16)((3u << 14) | (op1 << 11) | (crn << 7) | (crm << 3) | op2);
    if (!a64_sysreg_encoding_valid(packed)) return false;
    *packed_encoding = packed;
    return true;
}

bool aarch64_system_register_format_raw_s3(Arena* arena, u16 packed_encoding, String8* result)
{
    if (!arena || !result || !a64_sysreg_encoding_valid(packed_encoding) || ((packed_encoding >> 14) & 3u) != 3u) return false;
    u32 op1 = (packed_encoding >> 11) & 7u, crn = (packed_encoding >> 7) & 15u, crm = (packed_encoding >> 3) & 15u, op2 = packed_encoding & 7u;
    u64 mark = arena->position;
    String8 text = string_format(arena, S8("S3_{u32}_C{u32}_C{u32}_{u32}"), op1, crn, crm, op2);
    if (!text.pointer)
    {
        arena_set_position(arena, mark);
        return false;
    }
    *result = text;
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_sysreg_parameter_encoding(BusterA64SysregGeneratedRow row, u32 index, u16* result)
{
    if (!result || !(row.flags & BUSTER_A64_SYSREG_ROW_FLAG_PARAMETERIZED) || (row.flags & BUSTER_A64_SYSREG_ROW_FLAG_RAW_S3) ||
        index < row.array_start || index > row.array_end)
        return false;
    u32 offset = row.parameter_encoding_offset + (index - row.array_start);
    if (offset >= BUSTER_A64_SYSREG_PARAMETER_ENCODING_COUNT || index - row.array_start >= row.parameter_encoding_count) return false;
    u16 packed = buster_a64_sysreg_generated_parameter_encodings[offset];
    if (!a64_sysreg_encoding_valid(packed)) return false;
    *result = packed;
    return true;
}

BUSTER_GLOBAL_LOCAL u32 a64_sysreg_decimal_length(u32 value)
{
    u32 result = 1;
    while (value >= 10u) { value /= 10u; result += 1; }
    return result;
}

BUSTER_GLOBAL_LOCAL char8* a64_sysreg_decimal_write(char8* destination, u32 value)
{
    char8 digits[10]; u32 count = 0;
    do { digits[count++] = (char8)('0' + value % 10u); value /= 10u; } while (value);
    while (count) *destination++ = digits[--count];
    return destination;
}

bool aarch64_system_register_expand_name_encoding(Arena* arena, String8 family, u32 index, String8* result, u16* packed_encoding)
{
    if (!arena || !result || !packed_encoding || !family.length || !a64_sysreg_string_valid(family)) return false;
    BusterA64SysregGeneratedRow selected = {0}; bool found = false;
    for (u32 i = 0; i < aarch64_system_register_count(); i += 1)
    {
        BusterA64SysregGeneratedRow row = buster_a64_sysreg_generated_rows[i];
        String8 name = {0}, target = {0};
        if (!a64_sysreg_string_at(row.name_offset, &name) || !a64_sysreg_string_at(row.target_offset, &target) ||
            !(row.flags & BUSTER_A64_SYSREG_ROW_FLAG_PARAMETERIZED) || (row.flags & BUSTER_A64_SYSREG_ROW_FLAG_RAW_S3)) continue;
        if (a64_sysreg_name_equal(name, family) || a64_sysreg_name_equal(target, family)) { selected = row; found = true; break; }
    }
    if (!found || index < selected.array_start || index > selected.array_end) return false;
    u16 packed = 0;
    if (!a64_sysreg_parameter_encoding(selected, index, &packed)) return false;
    if (!a64_sysreg_encoding_valid(packed)) return false;
    u32 decimal_length = a64_sysreg_decimal_length(index);
    u64 capacity = family.length;
    for (u64 i = 0; i < family.length; i += 1)
    {
        if (family.pointer[i] != '<') continue;
        u64 end = i + 1; while (end < family.length && family.pointer[end] != '>') end += 1;
        if (end >= family.length) return false;
        u64 token_length = end - i + 1;
        if (decimal_length >= token_length)
        {
            u64 delta = decimal_length - token_length;
            if (capacity > UINT64_MAX - delta) return false;
            capacity += delta;
        }
        else
        {
            u64 delta = token_length - decimal_length;
            if (capacity < delta) return false;
            capacity -= delta;
        }
        i = end;
    }
    u64 mark = arena->position;
    char8* destination = arena_allocate(arena, char8, capacity);
    if (!destination) { arena_set_position(arena, mark); return false; }
    char8* cursor = destination;
    for (u64 i = 0; i < family.length; i += 1)
    {
        if (family.pointer[i] == '<')
        {
            u64 end = i + 1; while (end < family.length && family.pointer[end] != '>') end += 1;
            if (end >= family.length) { arena_set_position(arena, mark); return false; }
            cursor = a64_sysreg_decimal_write(cursor, index); i = end;
        }
        else *cursor++ = family.pointer[i];
    }
    *result = (String8){.pointer = destination, .length = (u64)(cursor - destination)};
    *packed_encoding = packed;
    return true;
}

bool aarch64_system_register_expand_name(Arena* arena, String8 family, u32 index, String8* result)
{
    if (!a64_sysreg_string_valid(family) || !family.length) return false;
    u16 ignored = 0;
    return aarch64_system_register_expand_name_encoding(arena, family, index, result, &ignored);
}

BUSTER_GLOBAL_LOCAL bool a64_sysreg_encode_common(u16 packed, u32 rt, bool read, u32* word)
{
    bool result;
    if (!word || !a64_sysreg_encoding_valid(packed) || rt > 31u || ((packed >> 14) & 3u) < 2u)
    {
        result = false;
    }
    else
    {
        u32 op0 = (packed >> 14) & 3u, op1 = (packed >> 11) & 7u, crn = (packed >> 7) & 15u, crm = (packed >> 3) & 15u, op2 = packed & 7u;
        u32 value = 0xd5000000u | (1u << 20) | (read ? (1u << 21) : 0u) | ((op0 - 2u) << 19) | (op1 << 16) | (crn << 12) |
                    (crm << 8) | (op2 << 5) | rt;
        *word = value;
        result = true;
    }

    return result;
}

bool aarch64_system_register_encode_mrs(u16 packed_encoding, u32 rt, u32* word)
{
    return a64_sysreg_encode_common(packed_encoding, rt, true, word);
}

bool aarch64_system_register_encode_msr(u16 packed_encoding, u32 rt, u32* word)
{
    return a64_sysreg_encode_common(packed_encoding, rt, false, word);
}

BUSTER_GLOBAL_LOCAL bool a64_sysreg_encode_pair_common(u16 packed, u32 rt, bool read, u32* word)
{
    // MRRS/MSRR name an even/odd consecutive register pair.  X31 is not a
    // legal member of that pair, and an odd first register cannot describe the
    // architectural pair encoding.
    if (!a64_sysreg_encoding_valid(packed) || rt > 30u || (rt & 1u) || ((packed >> 14) & 3u) != 3u) return false;
    if (!a64_sysreg_encode_common(packed, rt, read, word)) return false;
    *word |= 1u << 22;
    return true;
}

bool aarch64_system_register_encode_mrrs(u16 packed_encoding, u32 rt, u32* word)
{
    return a64_sysreg_encode_pair_common(packed_encoding, rt, true, word);
}

bool aarch64_system_register_encode_msrr(u16 packed_encoding, u32 rt, u32* word)
{
    return a64_sysreg_encode_pair_common(packed_encoding, rt, false, word);
}

bool aarch64_system_register_decode_word(u32 word, bool* is_read, u16* packed_encoding, u32* rt)
{
    if (!is_read || !packed_encoding || !rt) return false;
    u32 top = word & 0xfff00000u;
    bool read = top == 0xd5300000u;
    bool write = top == 0xd5100000u;
    if (!read && !write) return false;
    u32 op0 = 2u + ((word >> 19) & 1u), op1 = (word >> 16) & 7u, crn = (word >> 12) & 15u, crm = (word >> 8) & 15u, op2 = (word >> 5) & 7u;
    u16 packed = (u16)((op0 << 14) | (op1 << 11) | (crn << 7) | (crm << 3) | op2);
    if (!a64_sysreg_encoding_valid(packed)) return false;
    *is_read = read;
    *packed_encoding = packed;
    *rt = word & 31u;
    return true;
}

bool aarch64_system_register_decode_pair_word(u32 word, bool* is_read, u16* packed_encoding, u32* rt, u32* rt2)
{
    if (!is_read || !packed_encoding || !rt || !rt2) return false;
    u32 top = word & 0xfff00000u;
    bool read = top == 0xd5700000u;
    bool write = top == 0xd5500000u;
    if (!read && !write) return false;
    u32 first = word & 31u;
    if (first > 30u || (first & 1u)) return false;
    u32 op0 = 2u + ((word >> 19) & 1u), op1 = (word >> 16) & 7u, crn = (word >> 12) & 15u,
        crm = (word >> 8) & 15u, op2 = (word >> 5) & 7u;
    if (op0 != 3u) return false;
    u16 packed = (u16)((op0 << 14) | (op1 << 11) | (crn << 7) | (crm << 3) | op2);
    if (!a64_sysreg_encoding_valid(packed)) return false;
    *is_read = read;
    *packed_encoding = packed;
    *rt = first;
    *rt2 = first + 1u;
    return true;
}

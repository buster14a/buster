#include <stdlib.h>

/*
 * Bounded, dependency-free importer for the official Arm A64 XML snapshot.
 *
 * This file is included by build.c.  It deliberately does not share the LLVM
 * importer records: the Arm XML is an independent canonical inventory and its
 * rows must retain predicates, aliases, and unresolved bit constraints.
 */

typedef struct ArmA64CanonicalImportOptions ArmA64CanonicalImportOptions;
struct ArmA64CanonicalImportOptions
{
    String8 source_directory;
    String8 output_directory;
    bool output_directory_set;
};

typedef enum ArmA64XmlTagKind
{
    ARM_A64_XML_TAG_OPEN,
    ARM_A64_XML_TAG_CLOSE,
} ArmA64XmlTagKind;

typedef struct ArmA64XmlAttr ArmA64XmlAttr;
struct ArmA64XmlAttr
{
    String8 name;
    String8 value;
};

typedef struct ArmA64XmlTag ArmA64XmlTag;
struct ArmA64XmlTag
{
    ArmA64XmlTagKind kind;
    String8 name;
    ArmA64XmlAttr attrs[16];
    u32 attr_count;
    u64 start;
    u64 end;
    u64 content_start;
    bool self_closing;
};

typedef struct ArmA64XmlCursor ArmA64XmlCursor;
struct ArmA64XmlCursor
{
    String8 text;
    u64 position;
};

typedef enum ArmA64BitKind
{
    ARM_A64_BIT_UNRESOLVED,
    ARM_A64_BIT_FIXED,
    ARM_A64_BIT_FIELD,
} ArmA64BitKind;

typedef struct ArmA64Bit ArmA64Bit;
struct ArmA64Bit
{
    ArmA64BitKind kind;
    u8 value;
    String8 field;
};

typedef struct ArmA64BoxConstraint ArmA64BoxConstraint;
struct ArmA64BoxConstraint
{
    String8 name;
    String8 constraint;
    String8 pattern;
    u8 hibit;
    u8 width;
};

typedef struct ArmA64AliasPreference ArmA64AliasPreference;
struct ArmA64AliasPreference
{
    String8 alias_file;
    String8 alias_id;
    String8 condition;
    u32 rank;
};

typedef struct ArmA64Layout ArmA64Layout;
struct ArmA64Layout
{
    ArmA64Bit bits[32];
    ArmA64BoxConstraint boxes[64];
    u32 box_count;
};

typedef struct ArmA64Page ArmA64Page;
struct ArmA64Page
{
    String8 file;
    String8 id;
    String8 type;
};

typedef struct ArmA64Segment ArmA64Segment;
struct ArmA64Segment
{
    u8 instruction_lsb;
    u8 width;
    u8 value_lsb;
};

typedef struct ArmA64Field ArmA64Field;
struct ArmA64Field
{
    String8 name;
    u32 bit_count;
    u8 bits[32];
    u32 segment_count;
    ArmA64Segment segments[32];
};

typedef struct ArmA64CanonicalRow ArmA64CanonicalRow;
struct ArmA64CanonicalRow
{
    String8 encoding_name;
    String8 canonical_id;
    String8 page_id;
    String8 iform_file;
    String8 iform_id;
    String8 iclass_id;
    String8 iclass_name;
    String8 kind;
    String8 alias_to_file;
    String8 alias_to_id;
    String8 alias_to_encoding_id;
    String8 alias_preference_condition;
    u32 alias_preference_rank;
    String8 assembly;
    String8 equivalent;
    String8 alias_condition;
    String8 instr_class;
    String8 feature_expression;
    String8 feature_tags;
    String8 constraints;
    String8 status;
    u32 fixed_mask;
    u32 fixed_value;
    u32 field_mask;
    u32 unresolved_mask;
    u32 explicit_unresolved_mask;
    /* Raw layout uncertainty before the profile-scoped named-box promotion.
       This is importer audit state only and is intentionally not emitted. */
    u32 source_unresolved_mask;
    u32 field_count;
    ArmA64Field fields[32];
    u32 box_count;
    ArmA64BoxConstraint boxes[64];
    u32 alias_preference_count;
    ArmA64AliasPreference alias_preferences[32];
    bool apple_m1;
    bool system;
    u64 digest;
};

typedef struct ArmA64CanonicalRows ArmA64CanonicalRows;
struct ArmA64CanonicalRows
{
    ArmA64CanonicalRow* pointer;
    u32 count;
    u32 capacity;
};

static u8* arm_a64_arena_pointer(Arena* arena, u64 position)
{
    return arena_get_byte_pointer_at_position(arena, position);
}

static bool arm_a64_string_equal_c(String8 a, const char* b)
{
    return string_equal(a, string_from_pointer((char8*)b));
}

static String8 arm_a64_trim(String8 text)
{
    u64 first = 0;
    u64 last = text.length;
    while (first < last && character_is_space(text.pointer[first])) first += 1;
    while (last > first && character_is_space(text.pointer[last - 1])) last -= 1;
    return string_slice(text, first, last);
}

static bool arm_a64_is_name_char(char8 c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == ':' || c == '-';
}

static String8 arm_a64_tag_attr(ArmA64XmlTag tag, String8 wanted)
{
    String8 result = {0};
    // First match wins, and an attribute value may itself be empty, so the scan
    // stops on `found` rather than on `result` having become non-empty.
    bool found = false;
    for (u32 index = 0; index < tag.attr_count && !found; index += 1)
    {
        found = string_equal(tag.attrs[index].name, wanted);
        if (found) result = tag.attrs[index].value;
    }

    return result;
}

static bool arm_a64_xml_next(ArmA64XmlCursor* cursor, ArmA64XmlTag* result)
{
    String8 text = cursor->text;
    u64 position = cursor->position;
    while (position < text.length)
    {
        u64 open = position;
        while (open < text.length && text.pointer[open] != '<') open += 1;
        if (open == text.length)
        {
            cursor->position = open;
            return false;
        }
        if (string_slice(text, open, BUSTER_MIN(open + 4, text.length)).length == 4 &&
            memcmp(text.pointer + open, "<!--", 4) == 0)
        {
            u64 close = string_first_sequence(string_slice(text, open + 4, text.length), S8("-->"));
            if (close == BUSTER_STRING_NO_MATCH) return false;
            position = open + 4 + close + 3;
            continue;
        }
        if (open + 2 < text.length && text.pointer[open + 1] == '!' && text.pointer[open + 2] != '-')
        {
            u64 close = open + 2;
            bool quoted = false;
            char8 quote = 0;
            while (close < text.length)
            {
                char8 c = text.pointer[close];
                if (quoted)
                {
                    if (c == quote) quoted = false;
                }
                else if (c == '\'' || c == '"')
                {
                    quoted = true;
                    quote = c;
                }
                else if (c == '>') break;
                close += 1;
            }
            if (close == text.length) return false;
            position = close + 1;
            continue;
        }
        if (open + 1 < text.length && text.pointer[open + 1] == '?')
        {
            u64 close = string_first_sequence(string_slice(text, open + 2, text.length), S8("?>"));
            if (close == BUSTER_STRING_NO_MATCH) return false;
            position = open + 2 + close + 2;
            continue;
        }
        u64 close = open + 1;
        bool quoted = false;
        char8 quote = 0;
        while (close < text.length)
        {
            char8 c = text.pointer[close];
            if (quoted)
            {
                if (c == quote) quoted = false;
            }
            else if (c == '\'' || c == '"')
            {
                quoted = true;
                quote = c;
            }
            else if (c == '>') break;
            close += 1;
        }
        if (close == text.length) return false;
        ArmA64XmlTag tag = {0};
        tag.start = open;
        tag.end = close + 1;
        u64 scan = open + 1;
        if (text.pointer[scan] == '/')
        {
            tag.kind = ARM_A64_XML_TAG_CLOSE;
            scan += 1;
        }
        while (scan < close && arm_a64_is_name_char(text.pointer[scan])) scan += 1;
        tag.name = string_slice(text, (text.pointer[open + 1] == '/' ? open + 2 : open + 1), scan);
        if (tag.kind == ARM_A64_XML_TAG_CLOSE)
        {
            cursor->position = close + 1;
            *result = tag;
            return true;
        }
        tag.content_start = scan;
        u64 attr_scan = scan;
        while (attr_scan < close)
        {
            while (attr_scan < close && character_is_space(text.pointer[attr_scan])) attr_scan += 1;
            if (attr_scan >= close || text.pointer[attr_scan] == '/') break;
            u64 name_start = attr_scan;
            while (attr_scan < close && arm_a64_is_name_char(text.pointer[attr_scan])) attr_scan += 1;
            if (name_start == attr_scan) return false;
            String8 name = string_slice(text, name_start, attr_scan);
            while (attr_scan < close && character_is_space(text.pointer[attr_scan])) attr_scan += 1;
            if (attr_scan >= close || text.pointer[attr_scan] != '=') return false;
            attr_scan += 1;
            while (attr_scan < close && character_is_space(text.pointer[attr_scan])) attr_scan += 1;
            if (attr_scan >= close || (text.pointer[attr_scan] != '\'' && text.pointer[attr_scan] != '"')) return false;
            char8 attr_quote = text.pointer[attr_scan++];
            u64 value_start = attr_scan;
            while (attr_scan < close && text.pointer[attr_scan] != attr_quote) attr_scan += 1;
            if (attr_scan >= close || tag.attr_count >= BUSTER_ARRAY_LENGTH(tag.attrs)) return false;
            tag.attrs[tag.attr_count++] = (ArmA64XmlAttr){.name = name, .value = string_slice(text, value_start, attr_scan)};
            attr_scan += 1;
        }
        tag.self_closing = close > open && text.pointer[close - 1] == '/';
        cursor->position = close + 1;
        *result = tag;
        return true;
    }
    return false;
}

static bool arm_a64_tag_name_is(ArmA64XmlTag tag, const char* name)
{
    return arm_a64_string_equal_c(tag.name, name);
}

static bool arm_a64_find_element_end(String8 text, u64 open_start, String8 name, u64* end_out)
{
    ArmA64XmlCursor cursor = {.text = text, .position = open_start};
    ArmA64XmlTag tag = {0};
    u32 depth = 0;
    bool started = false;
    while (arm_a64_xml_next(&cursor, &tag))
    {
        if (!string_equal(tag.name, name)) continue;
        if (tag.kind == ARM_A64_XML_TAG_OPEN && !tag.self_closing)
        {
            depth += 1;
            started = true;
        }
        else if (tag.kind == ARM_A64_XML_TAG_CLOSE)
        {
            if (!started || !depth) return false;
            depth -= 1;
            if (!depth)
            {
                *end_out = tag.end;
                return true;
            }
        }
        else if (tag.kind == ARM_A64_XML_TAG_OPEN && tag.self_closing && !started)
        {
            *end_out = tag.end;
            return true;
        }
    }
    return false;
}

static String8 arm_a64_element_inner(String8 text, ArmA64XmlTag tag, u64 end)
{
    if (tag.self_closing || end < tag.end) return (String8){0};
    u64 closing_length = tag.name.length + 3; /* </name> */
    if (end < closing_length) return (String8){0};
    u64 inner_end = end - closing_length;
    return string_slice(text, tag.end, inner_end);
}

static bool arm_a64_parse_u32(String8 text, u32* out)
{
    if (!text.length) return false;
    u64 value = 0;
    for (u64 index = 0; index < text.length; index += 1)
    {
        char8 c = text.pointer[index];
        if (c < '0' || c > '9') return false;
        value = value * 10 + (u64)(c - '0');
        if (value > UINT32_MAX) return false;
    }
    *out = (u32)value;
    return true;
}

static bool arm_a64_hex_append(Arena* output, u64 value)
{
    static char8 digits[] = "0123456789abcdef";
    char8 buffer[16];
    for (u32 index = 0; index < 16; index += 1) buffer[15 - index] = digits[(value >> (index * 4)) & 0xf];
    u32 first = 0;
    while (first < 15 && buffer[first] == '0') first += 1;
    arena_append_string8(output, S8("0x"));
    arena_append_string8(output, string_slice((String8){.pointer = buffer, .length = 16}, first, 16));
    return true;
}

static void arm_a64_hex32_append(Arena* output, u32 value)
{
    static char8 digits[] = "0123456789abcdef";
    char8 buffer[8];
    for (u32 index = 0; index < 8; index += 1) buffer[7 - index] = digits[(value >> (index * 4)) & 0xf];
    arena_append_string8(output, S8("0x"));
    arena_append_string8(output, (String8){.pointer = buffer, .length = 8});
}

static void arm_a64_append_u64(Arena* output, u64 value)
{
    char8 buffer[32];
    u32 count = 0;
    do
    {
        buffer[count++] = (char8)('0' + value % 10);
        value /= 10;
    } while (value);
    while (count) arena_append_char8(output, buffer[--count]);
}

static bool arm_a64_json_string(Arena* output, String8 value)
{
    arena_append_json_string(output, value);
    return true;
}

static void arm_a64_json_hex(Arena* output, u64 value)
{
    arena_append_char8(output, '"');
    arm_a64_hex_append(output, value);
    arena_append_char8(output, '"');
}

static void arm_a64_json_hex32(Arena* output, u32 value)
{
    arena_append_char8(output, '"');
    arm_a64_hex32_append(output, value);
    arena_append_char8(output, '"');
}

static void arm_a64_json_sha256(Arena* output, const u8 digest[32])
{
    static const char8 digits[] = "0123456789abcdef";
    char8 buffer[64];
    for (u32 index = 0; index < 32; index += 1)
    {
        buffer[index * 2] = digits[digest[index] >> 4];
        buffer[index * 2 + 1] = digits[digest[index] & 0xf];
    }
    arena_append_char8(output, '"');
    arena_append_string8(output, (String8){.pointer = buffer, .length = sizeof(buffer)});
    arena_append_char8(output, '"');
}

static bool arm_a64_c_string(Arena* output, String8 value)
{
    arena_append_char8(output, '"');
    for (u64 index = 0; index < value.length; index += 1)
    {
        char8 character = value.pointer[index];
        if (character == '"' || character == '\\') arena_append_char8(output, '\\');
        arena_append_char8(output, character);
    }
    arena_append_char8(output, '"');
    return true;
}

static bool arm_a64_parse_index(Arena* arena, String8 source, ArmA64Page* pages, u32 capacity, u32* count_out)
{
    String8 path = path_join(arena, source, S8("index.xml"));
    ByteSlice bytes = file_read(arena, path, (FileReadOptions){.end_padding = 1});
    if (!bytes.pointer || !bytes.length) return false;
    String8 text = {.pointer = (char8*)bytes.pointer, .length = bytes.length};
    ArmA64XmlCursor cursor = {.text = text};
    ArmA64XmlTag tag = {0};
    u32 count = 0;
    while (arm_a64_xml_next(&cursor, &tag))
    {
        if (tag.kind != ARM_A64_XML_TAG_OPEN || !arm_a64_tag_name_is(tag, "iform")) continue;
        String8 file = arm_a64_tag_attr(tag, S8("iformfile"));
        String8 id = arm_a64_tag_attr(tag, S8("id"));
        if (!file.length || !id.length || file.length > 255 || id.length > 255 || count >= capacity)
        {
            return false;
        }
        if (!string_ends_with_sequence_insensitive(file, S8(".xml"))) return false;
        for (u32 index = 0; index < count; index += 1)
        {
            if (string_equal(file, pages[index].file) || string_equal(id, pages[index].id)) return false;
        }
        pages[count++] = (ArmA64Page){.file = string_duplicate_arena(arena, file, true), .id = string_duplicate_arena(arena, id, true)};
    }
    *count_out = count;
    return count == 516;
}

static int arm_a64_page_compare(const void* left_pointer, const void* right_pointer)
{
    const ArmA64Page* left = left_pointer;
    const ArmA64Page* right = right_pointer;
    u64 count = BUSTER_MIN(left->file.length, right->file.length);
    int result = count ? memcmp(left->file.pointer, right->file.pointer, count) : 0;
    return result ? result : (left->file.length > right->file.length) - (left->file.length < right->file.length);
}

typedef struct ArmA64SourceFile ArmA64SourceFile;
struct ArmA64SourceFile
{
    String8 relative;
};

static int arm_a64_source_file_compare(const void* left_pointer, const void* right_pointer)
{
    const ArmA64SourceFile* left = left_pointer;
    const ArmA64SourceFile* right = right_pointer;
    u64 count = BUSTER_MIN(left->relative.length, right->relative.length);
    int result = count ? memcmp(left->relative.pointer, right->relative.pointer, count) : 0;
    return result ? result : (left->relative.length > right->relative.length) - (left->relative.length < right->relative.length);
}

static bool arm_a64_source_path_excluded(String8 relative)
{
    /* XHTML is a generated presentation tree, not a source input.  Keep this
       exclusion explicit and stable in the manifest's digest scope. */
    return relative.length >= 5 && string_slice(relative, 0, 5).pointer[0] == 'x' &&
           string_slice(relative, 0, 5).pointer[1] == 'h' && string_slice(relative, 0, 5).pointer[2] == 't' &&
           string_slice(relative, 0, 5).pointer[3] == 'm' && string_slice(relative, 0, 5).pointer[4] == 'l' &&
           (relative.length == 5 || relative.pointer[5] == '/');
}

static bool arm_a64_source_file_add(Arena* arena, String8 relative, ArmA64SourceFile* files, u32* count, u32 capacity)
{
    if (arm_a64_source_path_excluded(relative) || !relative.length || *count >= capacity) return false;
    for (u32 index = 0; index < *count; index += 1)
    {
        if (string_equal(files[index].relative, relative)) return false;
    }
    files[(*count)++].relative = string_duplicate_arena(arena, relative, true);
    return true;
}

static bool arm_a64_source_tree_collect(Arena* arena, Arena* scratch, String8 source, String8 relative,
                                        ArmA64SourceFile* files, u32* count, u32 capacity)
{
    String8 directory_path = relative.length ? path_join(scratch, source, relative) : source;
#if BUSTER_WINDOWS
    String8 pattern = path_join(scratch, directory_path, S8("*"));
    String16 pattern_w = string16_from_string8(scratch, pattern, true);
    WIN32_FIND_DATAW find_data;
    HANDLE find = FindFirstFileW(pattern_w.pointer, &find_data);
    if (find == INVALID_HANDLE_VALUE) return false;
    bool valid = true;
    do
    {
        String8 name = string8_from_string16(scratch,
                                              (String16){.pointer = find_data.cFileName, .length = string16_length(find_data.cFileName)}, true);
        if (string_equal(name, S8(".")) || string_equal(name, S8(".."))) continue;
        String8 child = relative.length ? string_format_z(scratch, S8("{S8}/{S8}"), relative, name) : name;
        if (arm_a64_source_path_excluded(child)) continue;
        bool directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool reparse = (find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        if (reparse) { valid = false; break; }
        if (directory)
        {
            /* The importer consumes the archive's top-level source inputs.
               Nested presentation/diff trees are intentionally outside this
               identity scope; in particular xhtml/ is generated output. */
            continue;
        }
        else
        {
            valid = arm_a64_source_file_add(arena, child, files, count, capacity);
        }
        if (!valid) break;
    } while (FindNextFileW(find, &find_data));
    if (valid && GetLastError() != ERROR_NO_MORE_FILES) valid = false;
    FindClose(find);
    return valid;
#else
    String8 directory_z = string_duplicate_arena(scratch, directory_path, true);
    DIR* directory = opendir((const char*)directory_z.pointer);
    if (!directory) return false;
    bool valid = true;
    errno = 0;
    struct dirent* entry = 0;
    while ((entry = readdir(directory)) != 0)
    {
        String8 name = string_from_pointer((char8*)entry->d_name);
        if (string_equal(name, S8(".")) || string_equal(name, S8(".."))) continue;
        String8 child = relative.length ? string_format_z(scratch, S8("{S8}/{S8}"), relative, name) : name;
        if (arm_a64_source_path_excluded(child)) continue;
        String8 child_path = path_join(scratch, source, child);
        String8 child_z = string_duplicate_arena(scratch, child_path, true);
        struct stat info;
        if (lstat((const char*)child_z.pointer, &info) != 0) { valid = false; break; }
        if (S_ISLNK(info.st_mode)) { valid = false; break; }
        if (S_ISDIR(info.st_mode))
        {
            /* See the Windows branch: only top-level regular source inputs
               participate in the pinned identity digest. */
            continue;
        }
        else if (S_ISREG(info.st_mode))
        {
            valid = arm_a64_source_file_add(arena, child, files, count, capacity);
        }
        if (!valid) break;
    }
    if (errno != 0) valid = false;
    closedir(directory);
    return valid;
#endif
}

static u64 arm_a64_fnv1a_update(u64 hash, const u8* pointer, u64 length)
{
    for (u64 index = 0; index < length; index += 1)
    {
        hash ^= pointer[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

typedef struct ArmA64Sha256 ArmA64Sha256;
struct ArmA64Sha256
{
    u32 state[8];
    u64 byte_count;
    u32 block_count;
    u8 block[64];
};

static u32 arm_a64_sha256_rotr(u32 value, u32 amount)
{
    return (value >> amount) | (value << (32 - amount));
}

static void arm_a64_sha256_transform(ArmA64Sha256* context)
{
    static const u32 constants[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };
    u32 words[64];
    for (u32 index = 0; index < 16; index += 1)
    {
        u32 offset = index * 4;
        words[index] = ((u32)context->block[offset] << 24) | ((u32)context->block[offset + 1] << 16) |
                       ((u32)context->block[offset + 2] << 8) | context->block[offset + 3];
    }
    for (u32 index = 16; index < 64; index += 1)
    {
        u32 first = arm_a64_sha256_rotr(words[index - 15], 7) ^ arm_a64_sha256_rotr(words[index - 15], 18) ^ (words[index - 15] >> 3);
        u32 second = arm_a64_sha256_rotr(words[index - 2], 17) ^ arm_a64_sha256_rotr(words[index - 2], 19) ^ (words[index - 2] >> 10);
        words[index] = words[index - 16] + first + words[index - 7] + second;
    }
    u32 a = context->state[0], b = context->state[1], c = context->state[2], d = context->state[3];
    u32 e = context->state[4], f = context->state[5], g = context->state[6], h = context->state[7];
    for (u32 index = 0; index < 64; index += 1)
    {
        u32 sigma1 = arm_a64_sha256_rotr(e, 6) ^ arm_a64_sha256_rotr(e, 11) ^ arm_a64_sha256_rotr(e, 25);
        u32 choice = (e & f) ^ ((~e) & g);
        u32 temp1 = h + sigma1 + choice + constants[index] + words[index];
        u32 sigma0 = arm_a64_sha256_rotr(a, 2) ^ arm_a64_sha256_rotr(a, 13) ^ arm_a64_sha256_rotr(a, 22);
        u32 majority = (a & b) ^ (a & c) ^ (b & c);
        u32 temp2 = sigma0 + majority;
        h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
    }
    context->state[0] += a; context->state[1] += b; context->state[2] += c; context->state[3] += d;
    context->state[4] += e; context->state[5] += f; context->state[6] += g; context->state[7] += h;
}

static void arm_a64_sha256_init(ArmA64Sha256* context)
{
    memset(context, 0, sizeof(*context));
    context->state[0] = 0x6a09e667; context->state[1] = 0xbb67ae85; context->state[2] = 0x3c6ef372; context->state[3] = 0xa54ff53a;
    context->state[4] = 0x510e527f; context->state[5] = 0x9b05688c; context->state[6] = 0x1f83d9ab; context->state[7] = 0x5be0cd19;
}

static void arm_a64_sha256_update(ArmA64Sha256* context, const u8* pointer, u64 length)
{
    context->byte_count += length;
    while (length)
    {
        u32 available = 64 - context->block_count;
        u32 amount = (u32)BUSTER_MIN((u64)available, length);
        memcpy(context->block + context->block_count, pointer, amount);
        context->block_count += amount;
        pointer += amount;
        length -= amount;
        if (context->block_count == 64)
        {
            arm_a64_sha256_transform(context);
            context->block_count = 0;
        }
    }
}

static void arm_a64_sha256_final(ArmA64Sha256* context, u8 output[32])
{
    u64 bit_count = context->byte_count * 8;
    context->block[context->block_count++] = 0x80;
    if (context->block_count > 56)
    {
        memset(context->block + context->block_count, 0, 64 - context->block_count);
        arm_a64_sha256_transform(context);
        context->block_count = 0;
    }
    memset(context->block + context->block_count, 0, 56 - context->block_count);
    for (u32 index = 0; index < 8; index += 1) context->block[56 + index] = (u8)(bit_count >> (56 - index * 8));
    arm_a64_sha256_transform(context);
    for (u32 index = 0; index < 8; index += 1)
    {
        output[index * 4] = (u8)(context->state[index] >> 24);
        output[index * 4 + 1] = (u8)(context->state[index] >> 16);
        output[index * 4 + 2] = (u8)(context->state[index] >> 8);
        output[index * 4 + 3] = (u8)context->state[index];
    }
}

static void arm_a64_sha256_bytes(const u8* bytes, u64 length, u8 output[32])
{
    ArmA64Sha256 context;
    arm_a64_sha256_init(&context);
    arm_a64_sha256_update(&context, bytes, length);
    arm_a64_sha256_final(&context, output);
}

static bool arm_a64_sha256_self_test(void)
{
    static const u8 expected[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    ArmA64Sha256 context;
    u8 actual[32];
    arm_a64_sha256_init(&context);
    arm_a64_sha256_update(&context, (const u8*)"abc", 3);
    arm_a64_sha256_final(&context, actual);
    return memcmp(actual, expected, sizeof(expected)) == 0;
}

static bool arm_a64_source_tree_digest(Arena* arena, String8 source, u32* file_count_out, u64* digest_out, u8 sha256_out[32])
{
    TemporalArena scratch_scope = scratch_begin(&arena, 1);
    Arena* scratch = scratch_scope.arena;
    ArmA64SourceFile* files = arena_allocate(arena, ArmA64SourceFile, 5000);
    u32 file_count = 0;
    if (!arm_a64_source_tree_collect(arena, scratch, source, (String8){0}, files, &file_count, 5000))
    {
        scratch_end(scratch_scope);
        return false;
    }
    qsort(files, file_count, sizeof(files[0]), arm_a64_source_file_compare);
    u64 digest = UINT64_C(14695981039346656037);
    ArmA64Sha256 sha256;
    arm_a64_sha256_init(&sha256);
    for (u32 index = 0; index < file_count; index += 1)
    {
        ArmA64SourceFile file = files[index];
        digest = arm_a64_fnv1a_update(digest, (u8*)file.relative.pointer, file.relative.length);
        digest = arm_a64_fnv1a_update(digest, (u8*)"\0", 1);
        arm_a64_sha256_update(&sha256, (u8*)file.relative.pointer, file.relative.length);
        arm_a64_sha256_update(&sha256, (u8*)"\0", 1);
        String8 path = path_join(scratch, source, file.relative);
        ByteSlice bytes = file_read(scratch, path, (FileReadOptions){.end_padding = 1});
        if (!bytes.pointer && bytes.length) { scratch_end(scratch_scope); return false; }
        u64 length = bytes.length;
        u8 length_bytes[8];
        for (u32 byte = 0; byte < 8; byte += 1) length_bytes[byte] = (u8)(length >> (byte * 8));
        digest = arm_a64_fnv1a_update(digest, length_bytes, 8);
        digest = arm_a64_fnv1a_update(digest, bytes.pointer, bytes.length);
        arm_a64_sha256_update(&sha256, length_bytes, 8);
        arm_a64_sha256_update(&sha256, bytes.pointer, bytes.length);
        arena_set_position(scratch, scratch_scope.position);
    }
    *file_count_out = file_count;
    *digest_out = digest;
    arm_a64_sha256_final(&sha256, sha256_out);
    scratch_end(scratch_scope);
    return true;
}

/* Read just enough of a source document to decide whether it is one of the
   release's instructionsection pages.  The release directory also contains
   indexes, stylesheets, and other XML support files; only pages with this
   root element belong in the canonical inventory. */
static bool arm_a64_page_metadata(Arena* arena, Arena* scratch, String8 path, ArmA64Page* result)
{
    ByteSlice bytes = file_read(scratch, path, (FileReadOptions){.end_padding = 1});
    if (bytes.pointer && bytes.length)
    {
        String8 text = {.pointer = (char8*)bytes.pointer, .length = bytes.length};
        ArmA64XmlCursor cursor = {.text = text};
        ArmA64XmlTag tag = {0};
        while (arm_a64_xml_next(&cursor, &tag))
        {
            if (tag.kind == ARM_A64_XML_TAG_OPEN && arm_a64_tag_name_is(tag, "instructionsection"))
            {
                String8 id = arm_a64_tag_attr(tag, S8("id"));
                String8 type = arm_a64_tag_attr(tag, S8("type"));
                if (!id.length || !type.length) return false;
                result->id = string_duplicate_arena(arena, id, true);
                result->type = string_duplicate_arena(arena, type, true);
                return true;
            }
        }
    }

    return false;
}

static bool arm_a64_page_add(Arena* arena, Arena* scratch, String8 source, String8 file, ArmA64Page* pages, u32* count, u32 capacity)
{
    if (!file.length || !string_ends_with_sequence_insensitive(file, S8(".xml")) || *count >= capacity) return false;
    for (u32 index = 0; index < *count; index += 1)
    {
        if (string_equal(pages[index].file, file)) return false;
    }
    String8 path = path_join(scratch, source, file);
    ArmA64Page page = {0};
    if (!arm_a64_page_metadata(arena, scratch, path, &page)) return true; /* support XML, not a page */
    page.file = string_duplicate_arena(arena, file, true);
    for (u32 index = 0; index < *count; index += 1)
    {
        if (string_equal(pages[index].id, page.id)) return false;
    }
    pages[(*count)++] = page;
    return true;
}

static bool arm_a64_enumerate_pages(Arena* arena, String8 source, ArmA64Page* pages, u32 capacity, u32* count_out)
{
    TemporalArena scratch_scope = scratch_begin(&arena, 1);
    Arena* scratch = scratch_scope.arena;
    String8 source_z = string_duplicate_arena(arena, source, true);
    u32 count = 0;
#if BUSTER_WINDOWS
    String8 pattern = path_join(arena, source_z, S8("*"));
    String16 pattern_w = string16_from_string8(arena, pattern, true);
    WIN32_FIND_DATAW find_data;
    HANDLE find = FindFirstFileW(pattern_w.pointer, &find_data);
    if (find == INVALID_HANDLE_VALUE) return false;
    bool valid = true;
    do
    {
        String8 name = string8_from_string16(arena,
                                              (String16){.pointer = find_data.cFileName, .length = string16_length(find_data.cFileName)}, true);
        if (string_equal(name, S8(".")) || string_equal(name, S8(".."))) continue;
        if (find_data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        if (!string_ends_with_sequence_insensitive(name, S8(".xml"))) continue;
        u64 page_mark = scratch->position;
        valid = arm_a64_page_add(arena, scratch, source_z, name, pages, &count, capacity);
        arena_set_position(scratch, page_mark);
        if (!valid) break;
    } while (FindNextFileW(find, &find_data));
    FindClose(find);
    if (!valid) return false;
#else
    DIR* directory = opendir((const char*)source_z.pointer);
    if (!directory) return false;
    struct dirent* entry = 0;
    bool valid = true;
    while ((entry = readdir(directory)) != 0)
    {
        String8 name = string_from_pointer((char8*)entry->d_name);
        if (string_equal(name, S8(".")) || string_equal(name, S8("..")) ||
            !string_ends_with_sequence_insensitive(name, S8(".xml")))
            continue;
        String8 path = path_join(scratch, source_z, name);
        String8 path_z = string_duplicate_arena(scratch, path, true);
        struct stat info;
        if (lstat((const char*)path_z.pointer, &info) != 0) { valid = false; break; }
        if (!S_ISREG(info.st_mode)) continue;
        u64 page_mark = scratch->position;
        valid = arm_a64_page_add(arena, scratch, source_z, name, pages, &count, capacity);
        arena_set_position(scratch, page_mark);
        if (!valid) break;
    }
    closedir(directory);
    if (!valid) return false;
#endif
    qsort(pages, count, sizeof(pages[0]), arm_a64_page_compare);
    *count_out = count;
    scratch_end(scratch_scope);
    return true;
}

static void arm_a64_layout_clear(ArmA64Layout* layout)
{
    memset(layout, 0, sizeof(*layout));
    for (u32 bit = 0; bit < 32; bit += 1) layout->bits[bit].kind = ARM_A64_BIT_UNRESOLVED;
}

static void arm_a64_layout_apply_value(ArmA64Layout* layout, u32 bit, String8 value, String8 field, bool overlay)
{
    if (bit >= 32) return;
    value = arm_a64_trim(value);
    if (!value.length)
    {
        /* Encoding cells are a sparse overlay over the class regdiagram.  An
           empty cell carries no new fixed value.  A named empty cell denotes
           the source field carried by the base box (the historical XML form
           used by BIC/ORR and similar split encodings), while an unnamed cell
           inherits the base bit exactly. */
        if (!overlay)
        {
            layout->bits[bit].kind = field.length ? ARM_A64_BIT_FIELD : ARM_A64_BIT_UNRESOLVED;
            layout->bits[bit].field = field;
        }
        else if (field.length && layout->bits[bit].kind == ARM_A64_BIT_UNRESOLVED)
        {
            layout->bits[bit].kind = ARM_A64_BIT_FIELD;
            layout->bits[bit].field = field;
        }
        return;
    }
    if (value.length == 1 && (value.pointer[0] == '0' || value.pointer[0] == '1'))
    {
        layout->bits[bit].kind = ARM_A64_BIT_FIXED;
        layout->bits[bit].value = (u8)(value.pointer[0] - '0');
        layout->bits[bit].field = (String8){0};
    }
    else
    {
        layout->bits[bit].kind = ARM_A64_BIT_UNRESOLVED;
        /* Keep the named field even when the cell is an x/constraint.  The
           mask remains unresolved, while the functional field identity is
           retained for later operand semantics (for example BTI.op2). */
        layout->bits[bit].field = field;
    }
}

static u32 arm_a64_layout_unresolved_mask(ArmA64Layout layout)
{
    u32 result = 0;
    for (u32 bit = 0; bit < 32; bit += 1)
    {
        if (layout.bits[bit].kind == ARM_A64_BIT_UNRESOLVED) result |= UINT32_C(1) << bit;
    }
    return result;
}

static void arm_a64_layout_promote_named_symbols(ArmA64Layout* layout)
{
    for (u32 bit = 0; bit < 32; bit += 1)
    {
        ArmA64Bit* value = &layout->bits[bit];
        /* The XML's x/N/Z/constraint tokens are variable field bits when
           their box has a name.  Unnamed symbolic cells remain unresolved so
           a later semantic pass can account for them explicitly. */
        if (value->kind == ARM_A64_BIT_UNRESOLVED && value->field.length) value->kind = ARM_A64_BIT_FIELD;
    }
}

static String8 arm_a64_xml_decode(Arena* arena, String8 value);

static bool arm_a64_layout_apply_constraint(ArmA64Layout* layout, u32 bit_cursor, u32 colspan, String8 raw, String8 field, bool overlay)
{
    /* A constraint cell such as `!= 11xxx` describes one value constraint on
       the whole box, not a literal cell whose text should be copied to every
       bit.  Keep the constraint on ArmA64BoxConstraint, while assigning one
       symbolic source bit per colspan position.  This also makes the
       named-box promotion below independent of the textual constraint. */
    if (!string_starts_with_sequence(raw, S8("!="))) return false;
    String8 pattern = arm_a64_trim(string_slice(raw, 2, raw.length));
    if (pattern.length != colspan) return false;
    for (u32 offset = 0; offset < colspan; offset += 1)
    {
        arm_a64_layout_apply_value(layout, bit_cursor - offset, S8("x"), field, overlay);
    }
    return true;
}

static bool arm_a64_layout_box(Arena* arena, String8 text, ArmA64XmlTag box, ArmA64Layout* layout, bool overlay)
{
    u32 hibit = 0;
    String8 width_raw = arm_a64_tag_attr(box, S8("width"));
    if (!arm_a64_parse_u32(arm_a64_tag_attr(box, S8("hibit")), &hibit) || hibit >= 32) return false;
    u32 width = 1;
    if (width_raw.length && !arm_a64_parse_u32(width_raw, &width)) return false;
    if (!width || width > 32 || hibit + 1 < width) return false;
    String8 field = arm_a64_tag_attr(box, S8("name"));
    u32 box_index = UINT32_MAX;
    if (overlay)
    {
        for (u32 index = 0; index < layout->box_count; index += 1)
        {
            if (layout->boxes[index].hibit == hibit && layout->boxes[index].width == width &&
                string_equal(layout->boxes[index].name, field))
            {
                box_index = index;
                break;
            }
        }
    }
    if (box_index == UINT32_MAX)
    {
        if (layout->box_count >= BUSTER_ARRAY_LENGTH(layout->boxes)) return false;
        box_index = layout->box_count++;
    }
    ArmA64BoxConstraint* box_constraint = &layout->boxes[box_index];
    memset(box_constraint, 0, sizeof(*box_constraint));
    box_constraint->name = field;
    box_constraint->constraint = arm_a64_tag_attr(box, S8("constraint"));
    box_constraint->hibit = (u8)hibit;
    box_constraint->width = (u8)width;
    u64 pattern_mark = arena ? arena->position : 0;
    bool pattern_first = true;
    u64 box_end = box.end;
    if (!box.self_closing && !arm_a64_find_element_end(text, box.start, S8("box"), &box_end)) return false;
    ArmA64XmlCursor cursor = {.text = text, .position = box.end};
    ArmA64XmlTag ctag = {0};
    u32 bit_cursor = hibit;
    u32 consumed = 0;
    while (bit_cursor + 1 >= 1 && consumed < width && arm_a64_xml_next(&cursor, &ctag))
    {
        if (ctag.start >= box_end) break;
        if (ctag.kind != ARM_A64_XML_TAG_OPEN || !arm_a64_tag_name_is(ctag, "c")) continue;
        u32 colspan = 1;
        String8 colspan_raw = arm_a64_tag_attr(ctag, S8("colspan"));
        if (colspan_raw.length && !arm_a64_parse_u32(colspan_raw, &colspan)) return false;
        if (!colspan || consumed + colspan > width) return false;
        String8 raw = {0};
        if (!ctag.self_closing)
        {
            u64 c_end = 0;
            if (!arm_a64_find_element_end(text, ctag.start, S8("c"), &c_end)) return false;
            raw = arm_a64_trim(arm_a64_element_inner(text, ctag, c_end));
        }
        if (arena)
        {
            if (!pattern_first) arena_append_char8(arena, '|');
            if (raw.length)
            {
                /* arm_a64_xml_decode appends the normalized cell text to the
                   destination arena; do not append the returned slice again. */
                arm_a64_xml_decode(arena, raw);
            }
            else
            {
                for (u32 offset = 0; offset < colspan; offset += 1) arena_append_char8(arena, 'x');
            }
            pattern_first = false;
        }
        if (!raw.length)
        {
            for (u32 offset = 0; offset < colspan; offset += 1) arm_a64_layout_apply_value(layout, bit_cursor - offset, (String8){0}, field, overlay);
        }
        else if (raw.length == 3 && raw.pointer[0] == '(' && (raw.pointer[1] == '0' || raw.pointer[1] == '1') && raw.pointer[2] == ')')
        {
            String8 fixed = string_slice(raw, 1, 2);
            for (u32 offset = 0; offset < colspan; offset += 1) arm_a64_layout_apply_value(layout, bit_cursor - offset, fixed, field, overlay);
        }
        else if (string_starts_with_sequence(raw, S8("!=")))
        {
            if (!arm_a64_layout_apply_constraint(layout, bit_cursor, colspan, raw, field, overlay)) return false;
        }
        else if (raw.length == 1)
        {
            for (u32 offset = 0; offset < colspan; offset += 1) arm_a64_layout_apply_value(layout, bit_cursor - offset, raw, field, overlay);
        }
        else
        {
            if (raw.length != colspan) return false;
            for (u32 offset = 0; offset < colspan; offset += 1)
            {
                String8 one = string_slice(raw, offset, offset + 1);
                arm_a64_layout_apply_value(layout, bit_cursor - offset, one, field, overlay);
            }
        }
        bit_cursor -= colspan;
        consumed += colspan;
    }
    if (arena && !pattern_first)
    {
        box_constraint->pattern = (String8){.pointer = (char8*)arm_a64_arena_pointer(arena, pattern_mark), .length = arena->position - pattern_mark};
    }
    return consumed == width;
}

static bool arm_a64_layout_regdiagram(Arena* arena, String8 text, u64 start, u64 end, ArmA64Layout* layout, bool overlay)
{
    ArmA64XmlCursor cursor = {.text = text, .position = start};
    ArmA64XmlTag tag = {0};
    while (arm_a64_xml_next(&cursor, &tag))
    {
        if (tag.start >= end) break;
        if (tag.kind == ARM_A64_XML_TAG_OPEN && arm_a64_tag_name_is(tag, "box"))
        {
            if (!arm_a64_layout_box(arena, text, tag, layout, overlay)) return false;
        }
    }
    return true;
}

static bool arm_a64_layout_self_test(void)
{
    ArmA64Layout layout;
    arm_a64_layout_clear(&layout);

    /* Base diagram values are retained by sparse encoding overlays. */
    arm_a64_layout_apply_value(&layout, 31, S8("1"), (String8){0}, false);
    arm_a64_layout_apply_value(&layout, 30, S8("x"), S8("Rn"), false);
    arm_a64_layout_apply_value(&layout, 29, S8("x"), (String8){0}, false);
    arm_a64_layout_apply_value(&layout, 31, (String8){0}, (String8){0}, true);
    arm_a64_layout_apply_value(&layout, 30, (String8){0}, (String8){0}, true);
    arm_a64_layout_apply_value(&layout, 29, (String8){0}, (String8){0}, true);
    if (layout.bits[31].kind != ARM_A64_BIT_FIXED || layout.bits[31].value != 1 ||
        layout.bits[30].kind != ARM_A64_BIT_UNRESOLVED || !string_equal(layout.bits[30].field, S8("Rn")) ||
        layout.bits[29].kind != ARM_A64_BIT_UNRESOLVED || layout.bits[29].field.length)
        return false;

    /* Binary overlay cells are authoritative, including when the base bit
       was symbolic.  A named symbolic cell is promotable to a source field;
       unnamed symbols deliberately stay unresolved. */
    arm_a64_layout_apply_value(&layout, 30, S8("0"), S8("Rn"), true);
    arm_a64_layout_apply_value(&layout, 29, S8("x"), S8("Rm"), true);
    if (layout.bits[30].kind != ARM_A64_BIT_FIXED || layout.bits[30].value != 0 ||
        layout.bits[29].kind != ARM_A64_BIT_UNRESOLVED || !string_equal(layout.bits[29].field, S8("Rm")))
        return false;

    /* `!=` constraints are expanded one source bit per colspan position and
       remain a constraint rather than becoming fixed bits. */
    if (!arm_a64_layout_apply_constraint(&layout, 15, 4, S8("!= 11xx"), S8("cond"), true)) return false;
    for (u32 offset = 0; offset < 4; offset += 1)
    {
        ArmA64Bit bit = layout.bits[15 - offset];
        if (bit.kind != ARM_A64_BIT_UNRESOLVED || !string_equal(bit.field, S8("cond"))) return false;
    }
    arm_a64_layout_promote_named_symbols(&layout);
    for (u32 offset = 0; offset < 4; offset += 1)
    {
        if (layout.bits[15 - offset].kind != ARM_A64_BIT_FIELD) return false;
    }
    if (layout.bits[29].kind != ARM_A64_BIT_FIELD || layout.bits[29].field.length != 2 || layout.bits[29].field.pointer[0] != 'R') return false;
    return layout.bits[28].kind == ARM_A64_BIT_UNRESOLVED;
}

static String8 arm_a64_slice_normalize(Arena* arena, String8 text)
{
    u64 mark = arena->position;
    text = arm_a64_trim(text);
    bool space = false;
    for (u64 index = 0; index < text.length; index += 1)
    {
        char8 c = text.pointer[index];
        if (character_is_space(c))
        {
            space = true;
        }
        else
        {
            if (space && arena->position > mark) arena_append_char8(arena, ' ');
            arena_append_char8(arena, c);
            space = false;
        }
    }
    return (String8){.pointer = (char8*)arm_a64_arena_pointer(arena, mark), .length = arena->position - mark};
}

static bool arm_a64_collect_docvar(String8 text, u64 start, u64 end, String8 key, String8* value)
{
    u32 depth = 0;
    bool direct_docvars = false;
    ArmA64XmlCursor cursor = {.text = text, .position = start};
    ArmA64XmlTag tag = {0};
    while (arm_a64_xml_next(&cursor, &tag))
    {
        if (tag.start >= end) break;
        if (tag.kind == ARM_A64_XML_TAG_CLOSE)
        {
            if (direct_docvars && arm_a64_tag_name_is(tag, "docvars") && depth == 1) direct_docvars = false;
            if (depth) depth -= 1;
        }
        else
        {
            if (direct_docvars && arm_a64_tag_name_is(tag, "docvar") && depth == 1 && string_equal(arm_a64_tag_attr(tag, S8("key")), key))
            {
                *value = arm_a64_tag_attr(tag, S8("value"));
                return true;
            }
            if (arm_a64_tag_name_is(tag, "docvars") && depth == 0) direct_docvars = !tag.self_closing;
            if (!tag.self_closing) depth += 1;
        }
    }
    return true;
}

static String8 arm_a64_xml_decode(Arena* arena, String8 value)
{
    u64 mark = arena->position;
    for (u64 index = 0; index < value.length; index += 1)
    {
        if (index + 5 <= value.length && memcmp(value.pointer + index, "&amp;", 5) == 0)
        {
            arena_append_char8(arena, '&');
            index += 4;
        }
        else if (index + 4 <= value.length && memcmp(value.pointer + index, "&lt;", 4) == 0)
        {
            arena_append_char8(arena, '<');
            index += 3;
        }
        else if (index + 4 <= value.length && memcmp(value.pointer + index, "&gt;", 4) == 0)
        {
            arena_append_char8(arena, '>');
            index += 3;
        }
        else if (index + 6 <= value.length && memcmp(value.pointer + index, "&quot;", 6) == 0)
        {
            arena_append_char8(arena, '"');
            index += 5;
        }
        else if (index + 6 <= value.length && memcmp(value.pointer + index, "&apos;", 6) == 0)
        {
            arena_append_char8(arena, '\'');
            index += 5;
        }
        else
        {
            arena_append_char8(arena, value.pointer[index]);
        }
    }
    return (String8){.pointer = (char8*)arm_a64_arena_pointer(arena, mark), .length = arena->position - mark};
}

static String8 arm_a64_xml_functional_text(Arena* arena, String8 xml)
{
    // Keep only text nodes from alias equivalence templates. This retains the
    // functional assembly spelling and alias condition while dropping source
    // markup and descriptive attributes such as hover prose.
    u64 raw_mark = arena->position;
    u64 text_start = 0;
    ArmA64XmlCursor cursor = {.text = xml};
    ArmA64XmlTag tag = {0};
    while (arm_a64_xml_next(&cursor, &tag))
    {
        if (tag.start > text_start)
        {
            arm_a64_xml_decode(arena, string_slice(xml, text_start, tag.start));
        }
        text_start = tag.end;
    }
    if (text_start < xml.length)
    {
        arm_a64_xml_decode(arena, string_slice(xml, text_start, xml.length));
    }
    String8 raw = {.pointer = (char8*)arm_a64_arena_pointer(arena, raw_mark), .length = arena->position - raw_mark};
    return arm_a64_slice_normalize(arena, raw);
}

static bool arm_a64_xml_first_functional_element(Arena* arena, String8 text, u64 start, u64 end, const char* name,
                                                  String8* result, u64* element_end)
{
    ArmA64XmlCursor cursor = {.text = text, .position = start};
    ArmA64XmlTag tag = {0};
    while (arm_a64_xml_next(&cursor, &tag))
    {
        if (tag.start >= end) break;
        if (tag.kind != ARM_A64_XML_TAG_OPEN || !arm_a64_tag_name_is(tag, name)) continue;
        u64 close = tag.end;
        if (!tag.self_closing && !arm_a64_find_element_end(text, tag.start, tag.name, &close)) return false;
        *result = tag.self_closing ? (String8){0} : arm_a64_xml_functional_text(arena, arm_a64_element_inner(text, tag, close));
        if (element_end) *element_end = close;
        return true;
    }
    return false;
}

static bool arm_a64_xml_first_href_fragment(Arena* arena, String8 text, u64 start, u64 end, String8* result)
{
    ArmA64XmlCursor cursor = {.text = text, .position = start};
    ArmA64XmlTag tag = {0};
    while (arm_a64_xml_next(&cursor, &tag))
    {
        if (tag.start >= end) break;
        if (tag.kind != ARM_A64_XML_TAG_OPEN || !arm_a64_tag_name_is(tag, "a")) continue;
        String8 href = arm_a64_tag_attr(tag, S8("href"));
        if (!href.length) return false;
        String8 decoded = arm_a64_xml_decode(arena, href);
        u64 hash = BUSTER_STRING_NO_MATCH;
        for (u64 index = 0; index < decoded.length; index += 1)
        {
            if (decoded.pointer[index] == '#') { hash = index; break; }
        }
        if (hash == BUSTER_STRING_NO_MATCH || hash + 1 >= decoded.length)
        {
            return false;
        }
        String8 fragment = string_slice(decoded, hash + 1, decoded.length);
        for (u64 index = 0; index < fragment.length; index += 1)
        {
            char8 c = fragment.pointer[index];
            bool valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
            if (!valid)
            {
                return false;
            }
        }
        *result = string_duplicate_arena(arena, fragment, true);
        return result->length != 0;
    }
    return false;
}

/* Return true only for an <arch_variant> directly contained by an
   <arch_variants> block directly contained by the requested scope.  Class
   ranges contain their encodings, and encodings may carry their own variant
   block; keeping the nesting test here prevents those predicates from being
   accidentally ORed into the class predicate. */
static bool arm_a64_feature_variant_selected(ArmA64XmlTag tag, u32* depth, bool* direct_variants)
{
    if (tag.kind == ARM_A64_XML_TAG_CLOSE)
    {
        if (*direct_variants && arm_a64_tag_name_is(tag, "arch_variants") && *depth == 1) *direct_variants = false;
        if (*depth) *depth -= 1;
        return false;
    }
    bool selected = *direct_variants && arm_a64_tag_name_is(tag, "arch_variant") && *depth == 1;
    if (arm_a64_tag_name_is(tag, "arch_variants") && *depth == 0) *direct_variants = !tag.self_closing;
    if (!tag.self_closing) *depth += 1;
    return selected;
}

static bool arm_a64_collect_features(Arena* arena, String8 text, u64 start, u64 end, String8* expression, String8* tags)
{
    u32 count = 0;
    u32 depth = 0;
    bool direct_variants = false;
    ArmA64XmlCursor cursor = {.text = text, .position = start};
    ArmA64XmlTag tag = {0};
    while (arm_a64_xml_next(&cursor, &tag))
    {
        if (tag.start >= end) break;
        if (arm_a64_feature_variant_selected(tag, &depth, &direct_variants))
        {
            if (!arm_a64_tag_attr(tag, S8("feature")).length) return false;
            count += 1;
        }
    }
    if (!count)
    {
        *expression = (String8){0};
        *tags = (String8){0};
        return true;
    }

    u64 expression_mark = arena->position;
    bool first = true;
    depth = 0;
    direct_variants = false;
    cursor.position = start;
    while (arm_a64_xml_next(&cursor, &tag))
    {
        if (tag.start >= end) break;
        if (!arm_a64_feature_variant_selected(tag, &depth, &direct_variants)) continue;
        if (!first) arena_append_string8(arena, S8(" || "));
        if (count > 1) arena_append_char8(arena, '(');
        String8 feature = arm_a64_tag_attr(tag, S8("feature"));
        for (u64 index = 0; index < feature.length; index += 1)
        {
            if (index + 5 <= feature.length && memcmp(feature.pointer + index, "&amp;", 5) == 0)
            {
                arena_append_char8(arena, '&');
                index += 4;
            }
            else if (index + 4 <= feature.length && memcmp(feature.pointer + index, "&lt;", 4) == 0)
            {
                arena_append_char8(arena, '<');
                index += 3;
            }
            else if (index + 4 <= feature.length && memcmp(feature.pointer + index, "&gt;", 4) == 0)
            {
                arena_append_char8(arena, '>');
                index += 3;
            }
            else
            {
                arena_append_char8(arena, feature.pointer[index]);
            }
        }
        if (count > 1) arena_append_char8(arena, ')');
        first = false;
    }
    *expression = (String8){.pointer = (char8*)arm_a64_arena_pointer(arena, expression_mark), .length = arena->position - expression_mark};

    u64 tags_mark = arena->position;
    first = true;
    depth = 0;
    direct_variants = false;
    cursor.position = start;
    while (arm_a64_xml_next(&cursor, &tag))
    {
        if (tag.start >= end) break;
        if (!arm_a64_feature_variant_selected(tag, &depth, &direct_variants)) continue;
        if (!first) arena_append_char8(arena, ',');
        String8 feature = arm_a64_tag_attr(tag, S8("feature"));
        char8 decoded_buffer[512];
        u64 decoded_count = 0;
        for (u64 index = 0; index < feature.length; index += 1)
        {
            if (index + 5 <= feature.length && memcmp(feature.pointer + index, "&amp;", 5) == 0)
            {
                if (decoded_count < BUSTER_ARRAY_LENGTH(decoded_buffer)) decoded_buffer[decoded_count++] = '&';
                index += 4;
            }
            else if (index + 4 <= feature.length && memcmp(feature.pointer + index, "&lt;", 4) == 0)
            {
                if (decoded_count < BUSTER_ARRAY_LENGTH(decoded_buffer)) decoded_buffer[decoded_count++] = '<';
                index += 3;
            }
            else if (index + 4 <= feature.length && memcmp(feature.pointer + index, "&gt;", 4) == 0)
            {
                if (decoded_count < BUSTER_ARRAY_LENGTH(decoded_buffer)) decoded_buffer[decoded_count++] = '>';
                index += 3;
            }
            else
            {
                if (decoded_count < BUSTER_ARRAY_LENGTH(decoded_buffer)) decoded_buffer[decoded_count++] = feature.pointer[index];
            }
        }
        arena_append_json_string(arena, (String8){.pointer = decoded_buffer, .length = decoded_count});
        first = false;
    }
    *tags = (String8){.pointer = (char8*)arm_a64_arena_pointer(arena, tags_mark), .length = arena->position - tags_mark};
    return true;
}

static String8 arm_a64_join_feature_expressions(Arena* arena, String8 left, String8 right)
{
    if (!left.length) return right;
    if (!right.length) return left;
    u64 mark = arena->position;
    arena_append_char8(arena, '(');
    arena_append_string8(arena, left);
    arena_append_string8(arena, S8(") && ("));
    arena_append_string8(arena, right);
    arena_append_char8(arena, ')');
    return (String8){.pointer = (char8*)arm_a64_arena_pointer(arena, mark), .length = arena->position - mark};
}

static String8 arm_a64_join_feature_tags(Arena* arena, String8 left, String8 right)
{
    if (!left.length) return right;
    if (!right.length) return left;
    u64 mark = arena->position;
    arena_append_string8(arena, left);
    arena_append_char8(arena, ',');
    arena_append_string8(arena, right);
    return (String8){.pointer = (char8*)arm_a64_arena_pointer(arena, mark), .length = arena->position - mark};
}

static bool arm_a64_feature_known(String8 name)
{
    /* Complete FEAT_* atom catalog observed in the pinned 2026-06 release.
       This is deliberately separate from the M1 support closure below:
       known-but-disabled features evaluate false rather than becoming
       unknown parse errors. */
    static String8 known[] = {
        S8_INITIALIZER("FEAT_AES"), S8_INITIALIZER("FEAT_ASMv8p2"), S8_INITIALIZER("FEAT_ATS1A"), S8_INITIALIZER("FEAT_AdvSIMD"),
        S8_INITIALIZER("FEAT_BF16"), S8_INITIALIZER("FEAT_BRBE"), S8_INITIALIZER("FEAT_BTI"), S8_INITIALIZER("FEAT_CHK"),
        S8_INITIALIZER("FEAT_CLRBHB"), S8_INITIALIZER("FEAT_CMH"), S8_INITIALIZER("FEAT_CMPBR"), S8_INITIALIZER("FEAT_CPA"),
        S8_INITIALIZER("FEAT_CRC32"), S8_INITIALIZER("FEAT_CSSC"), S8_INITIALIZER("FEAT_D128"), S8_INITIALIZER("FEAT_DGH"),
        S8_INITIALIZER("FEAT_DIT"), S8_INITIALIZER("FEAT_DPB"), S8_INITIALIZER("FEAT_DPB2"), S8_INITIALIZER("FEAT_DotProd"),
        S8_INITIALIZER("FEAT_EBEP"), S8_INITIALIZER("FEAT_F16F32DOT"), S8_INITIALIZER("FEAT_F16F32MM"), S8_INITIALIZER("FEAT_F16MM"),
        S8_INITIALIZER("FEAT_F32MM"), S8_INITIALIZER("FEAT_F64MM"), S8_INITIALIZER("FEAT_F8F16MM"), S8_INITIALIZER("FEAT_F8F32MM"),
        S8_INITIALIZER("FEAT_FAMINMAX"), S8_INITIALIZER("FEAT_FCMA"), S8_INITIALIZER("FEAT_FHM"), S8_INITIALIZER("FEAT_FP"),
        S8_INITIALIZER("FEAT_FP16"), S8_INITIALIZER("FEAT_FP8"), S8_INITIALIZER("FEAT_FP8DOT2"), S8_INITIALIZER("FEAT_FP8DOT4"),
        S8_INITIALIZER("FEAT_FP8FMA"), S8_INITIALIZER("FEAT_FPRCVT"), S8_INITIALIZER("FEAT_FRINTTS"), S8_INITIALIZER("FEAT_FlagM"),
        S8_INITIALIZER("FEAT_FlagM2"), S8_INITIALIZER("FEAT_GCIE"), S8_INITIALIZER("FEAT_GCS"), S8_INITIALIZER("FEAT_HBC"),
        S8_INITIALIZER("FEAT_HINTE"), S8_INITIALIZER("FEAT_I8MM"), S8_INITIALIZER("FEAT_ITE"), S8_INITIALIZER("FEAT_JSCVT"),
        S8_INITIALIZER("FEAT_LOR"), S8_INITIALIZER("FEAT_LRCPC"), S8_INITIALIZER("FEAT_LRCPC2"), S8_INITIALIZER("FEAT_LRCPC3"),
        S8_INITIALIZER("FEAT_LS64"), S8_INITIALIZER("FEAT_LS64_ACCDATA"), S8_INITIALIZER("FEAT_LS64_V"), S8_INITIALIZER("FEAT_LSCP"),
        S8_INITIALIZER("FEAT_LSE"), S8_INITIALIZER("FEAT_LSE128"), S8_INITIALIZER("FEAT_LSFE"), S8_INITIALIZER("FEAT_LSUI"),
        S8_INITIALIZER("FEAT_LUT"), S8_INITIALIZER("FEAT_MEC"), S8_INITIALIZER("FEAT_MOPS"), S8_INITIALIZER("FEAT_MTE"),
        S8_INITIALIZER("FEAT_MTE2"), S8_INITIALIZER("FEAT_MTETC"), S8_INITIALIZER("FEAT_NMI"), S8_INITIALIZER("FEAT_OCCMO"),
        S8_INITIALIZER("FEAT_PAN"), S8_INITIALIZER("FEAT_PAN2"), S8_INITIALIZER("FEAT_PAuth"), S8_INITIALIZER("FEAT_PAuth_LR"),
        S8_INITIALIZER("FEAT_PCDPHINT"), S8_INITIALIZER("FEAT_PRFMSLC"), S8_INITIALIZER("FEAT_PoPS"), S8_INITIALIZER("FEAT_RAS"),
        S8_INITIALIZER("FEAT_RDM"), S8_INITIALIZER("FEAT_RME"), S8_INITIALIZER("FEAT_RME_GPC3"), S8_INITIALIZER("FEAT_RPRFM"),
        S8_INITIALIZER("FEAT_SB"), S8_INITIALIZER("FEAT_SHA1"), S8_INITIALIZER("FEAT_SHA256"), S8_INITIALIZER("FEAT_SHA3"),
        S8_INITIALIZER("FEAT_SHA512"), S8_INITIALIZER("FEAT_SM3"), S8_INITIALIZER("FEAT_SM4"), S8_INITIALIZER("FEAT_SME"),
        S8_INITIALIZER("FEAT_SME2"), S8_INITIALIZER("FEAT_SME2p1"), S8_INITIALIZER("FEAT_SME2p2"), S8_INITIALIZER("FEAT_SME2p3"),
        S8_INITIALIZER("FEAT_SME_B16B16"), S8_INITIALIZER("FEAT_SME_F16F16"), S8_INITIALIZER("FEAT_SME_F64F64"),
        S8_INITIALIZER("FEAT_SME_F8F16"), S8_INITIALIZER("FEAT_SME_F8F32"), S8_INITIALIZER("FEAT_SME_I16I64"),
        S8_INITIALIZER("FEAT_SME_LUTv2"), S8_INITIALIZER("FEAT_SME_MOP4"), S8_INITIALIZER("FEAT_SME_TMOP"), S8_INITIALIZER("FEAT_SPE"),
        S8_INITIALIZER("FEAT_SPECRES"), S8_INITIALIZER("FEAT_SPECRES2"), S8_INITIALIZER("FEAT_SPE_EXC"), S8_INITIALIZER("FEAT_SSBS"),
        S8_INITIALIZER("FEAT_SSVE_FEXPA"), S8_INITIALIZER("FEAT_SSVE_FP8DOT2"), S8_INITIALIZER("FEAT_SSVE_FP8DOT4"),
        S8_INITIALIZER("FEAT_SSVE_FP8FMA"), S8_INITIALIZER("FEAT_SVE"), S8_INITIALIZER("FEAT_SVE2"), S8_INITIALIZER("FEAT_SVE2p1"),
        S8_INITIALIZER("FEAT_SVE2p2"), S8_INITIALIZER("FEAT_SVE2p3"), S8_INITIALIZER("FEAT_SVE_AES"), S8_INITIALIZER("FEAT_SVE_AES2"),
        S8_INITIALIZER("FEAT_SVE_B16B16"), S8_INITIALIZER("FEAT_SVE_B16MM"), S8_INITIALIZER("FEAT_SVE_BFSCALE"),
        S8_INITIALIZER("FEAT_SVE_BitPerm"), S8_INITIALIZER("FEAT_SVE_F16F32MM"), S8_INITIALIZER("FEAT_SVE_PMULL128"),
        S8_INITIALIZER("FEAT_SVE_SHA3"), S8_INITIALIZER("FEAT_SVE_SM4"), S8_INITIALIZER("FEAT_SYSINSTR128"), S8_INITIALIZER("FEAT_SYSREG128"),
        S8_INITIALIZER("FEAT_THE"), S8_INITIALIZER("FEAT_TLBID"), S8_INITIALIZER("FEAT_TLBIOS"), S8_INITIALIZER("FEAT_TLBIRANGE"),
        S8_INITIALIZER("FEAT_TLBIW"), S8_INITIALIZER("FEAT_TRBE_EXC"), S8_INITIALIZER("FEAT_TRF"), S8_INITIALIZER("FEAT_UAO"),
        S8_INITIALIZER("FEAT_WFxT"), S8_INITIALIZER("FEAT_XS"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(known); index += 1) if (string_equal(name, known[index])) return true;
    return false;
}

static bool arm_a64_feature_m1_supported(String8 name)
{
    static String8 supported[] = {
        S8_INITIALIZER("FEAT_AES"), S8_INITIALIZER("FEAT_AdvSIMD"), S8_INITIALIZER("FEAT_CRC32"), S8_INITIALIZER("FEAT_DotProd"),
        S8_INITIALIZER("FEAT_FCMA"), S8_INITIALIZER("FEAT_FHM"), S8_INITIALIZER("FEAT_FP"), S8_INITIALIZER("FEAT_FP16"),
        S8_INITIALIZER("FEAT_FRINTTS"), S8_INITIALIZER("FEAT_FlagM"), S8_INITIALIZER("FEAT_FlagM2"), S8_INITIALIZER("FEAT_JSCVT"),
        S8_INITIALIZER("FEAT_LOR"), S8_INITIALIZER("FEAT_LSE"), S8_INITIALIZER("FEAT_LRCPC"), S8_INITIALIZER("FEAT_LRCPC2"),
        S8_INITIALIZER("FEAT_PAuth"), S8_INITIALIZER("FEAT_RAS"), S8_INITIALIZER("FEAT_RDM"), S8_INITIALIZER("FEAT_SB"),
        S8_INITIALIZER("FEAT_SHA1"), S8_INITIALIZER("FEAT_SHA256"), S8_INITIALIZER("FEAT_SHA3"), S8_INITIALIZER("FEAT_SHA512"),
        S8_INITIALIZER("FEAT_SPECRES"), S8_INITIALIZER("FEAT_TRF"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(supported); index += 1) if (string_equal(name, supported[index])) return true;
    return false;
}

typedef enum ArmA64FeatureOperator
{
    ARM_A64_FEATURE_NOT,
    ARM_A64_FEATURE_AND,
    ARM_A64_FEATURE_OR,
    ARM_A64_FEATURE_LPAREN,
} ArmA64FeatureOperator;

static void arm_a64_feature_skip_space(String8 text, u64* position)
{
    while (*position < text.length && character_is_space(text.pointer[*position])) *position += 1;
}

static bool arm_a64_feature_apply_operator(ArmA64FeatureOperator operation, bool* values, u32* value_count)
{
    if (operation == ARM_A64_FEATURE_NOT)
    {
        if (!*value_count) return false;
        values[*value_count - 1] = !values[*value_count - 1];
        return true;
    }
    if (*value_count < 2) return false;
    bool rhs = values[--*value_count];
    bool lhs = values[--*value_count];
    values[(*value_count)++] = operation == ARM_A64_FEATURE_AND ? (lhs && rhs) : (lhs || rhs);
    return true;
}

static u32 arm_a64_feature_operator_precedence(ArmA64FeatureOperator operation)
{
    return operation == ARM_A64_FEATURE_OR ? 1 : (operation == ARM_A64_FEATURE_AND ? 2 : (operation == ARM_A64_FEATURE_NOT ? 3 : 0));
}

static bool arm_a64_feature_expression_evaluate(String8 expression, bool* valid_out)
{
    *valid_out = true;
    if (!expression.length) return true;
    /* Shunting-yard evaluation keeps parser stack usage bounded even for a
       hostile expression with thousands of nested parentheses/not operators. */
    enum { ARM_A64_FEATURE_STACK_CAPACITY = 64 };
    ArmA64FeatureOperator operators[ARM_A64_FEATURE_STACK_CAPACITY];
    bool values[ARM_A64_FEATURE_STACK_CAPACITY];
    u32 operator_count = 0;
    u32 value_count = 0;
    u64 position = 0;
    bool expect_operand = true;
#define ARM_A64_FEATURE_INVALID() do { *valid_out = false; return false; } while (0)
    while (position < expression.length)
    {
        arm_a64_feature_skip_space(expression, &position);
        if (position >= expression.length) break;
        char8 c = expression.pointer[position];
        if (expect_operand)
        {
            if (c == '!')
            {
                if (operator_count >= ARM_A64_FEATURE_STACK_CAPACITY) ARM_A64_FEATURE_INVALID();
                operators[operator_count++] = ARM_A64_FEATURE_NOT;
                position += 1;
                continue;
            }
            if (c == '(')
            {
                if (operator_count >= ARM_A64_FEATURE_STACK_CAPACITY) ARM_A64_FEATURE_INVALID();
                operators[operator_count++] = ARM_A64_FEATURE_LPAREN;
                position += 1;
                continue;
            }
            u64 start = position;
            while (position < expression.length)
            {
                char8 atom = expression.pointer[position];
                bool name_char = (atom >= 'A' && atom <= 'Z') || (atom >= 'a' && atom <= 'z') || (atom >= '0' && atom <= '9') || atom == '_';
                if (!name_char) break;
                position += 1;
            }
            if (start == position || value_count >= ARM_A64_FEATURE_STACK_CAPACITY) ARM_A64_FEATURE_INVALID();
            String8 name = string_slice(expression, start, position);
            arm_a64_feature_skip_space(expression, &position);
            bool parameterized = false;
            if (position + 1 < expression.length &&
                ((expression.pointer[position] == '=' && expression.pointer[position + 1] == '=') ||
                 (expression.pointer[position] == '!' && expression.pointer[position + 1] == '=') ||
                 (expression.pointer[position] == '<' && expression.pointer[position + 1] == '=') ||
                 (expression.pointer[position] == '>' && expression.pointer[position + 1] == '=')))
            {
                parameterized = true;
                position += 2;
                arm_a64_feature_skip_space(expression, &position);
                if (position < expression.length && (expression.pointer[position] == '\'' || expression.pointer[position] == '"'))
                {
                    char8 quote = expression.pointer[position++];
                    u64 value_start = position;
                    while (position < expression.length && expression.pointer[position] != quote) position += 1;
                    if (position >= expression.length || position == value_start) ARM_A64_FEATURE_INVALID();
                    position += 1;
                }
                else
                {
                    u64 value_start = position;
                    while (position < expression.length && expression.pointer[position] != ')' && expression.pointer[position] != '&' &&
                           expression.pointer[position] != '|')
                        position += 1;
                    if (value_start == position) ARM_A64_FEATURE_INVALID();
                }
            }
            /* Parameterized predicates are syntactically known but cannot be
               evaluated without an encoding-variable environment.  Unknown
               atoms, including unknown FEAT_* names, are invalid even under !. */
            bool atom_value = false;
            if (parameterized)
            {
                if (string_starts_with_sequence(name, S8("FEAT_")) && !arm_a64_feature_known(name)) ARM_A64_FEATURE_INVALID();
                atom_value = false;
            }
            else if (string_starts_with_sequence(name, S8("FEAT_")) && arm_a64_feature_known(name))
            {
                atom_value = arm_a64_feature_m1_supported(name);
            }
            else
            {
                ARM_A64_FEATURE_INVALID();
            }
            values[value_count++] = atom_value;
            expect_operand = false;
            while (operator_count && operators[operator_count - 1] == ARM_A64_FEATURE_NOT)
            {
                if (!arm_a64_feature_apply_operator(ARM_A64_FEATURE_NOT, values, &value_count)) ARM_A64_FEATURE_INVALID();
                operator_count -= 1;
            }
            continue;
        }
        if (c == ')')
        {
            bool found = false;
            while (operator_count)
            {
                ArmA64FeatureOperator operation = operators[--operator_count];
                if (operation == ARM_A64_FEATURE_LPAREN) { found = true; break; }
                if (!arm_a64_feature_apply_operator(operation, values, &value_count)) ARM_A64_FEATURE_INVALID();
            }
            if (!found || !value_count) ARM_A64_FEATURE_INVALID();
            position += 1;
            while (operator_count && operators[operator_count - 1] == ARM_A64_FEATURE_NOT)
            {
                if (!arm_a64_feature_apply_operator(ARM_A64_FEATURE_NOT, values, &value_count)) ARM_A64_FEATURE_INVALID();
                operator_count -= 1;
            }
            continue;
        }
        ArmA64FeatureOperator operation;
        if (position + 1 < expression.length && expression.pointer[position] == '&' && expression.pointer[position + 1] == '&')
        {
            operation = ARM_A64_FEATURE_AND;
        }
        else if (position + 1 < expression.length && expression.pointer[position] == '|' && expression.pointer[position + 1] == '|')
        {
            operation = ARM_A64_FEATURE_OR;
        }
        else
        {
            ARM_A64_FEATURE_INVALID();
        }
        position += 2;
        while (operator_count && operators[operator_count - 1] != ARM_A64_FEATURE_LPAREN &&
               arm_a64_feature_operator_precedence(operators[operator_count - 1]) >= arm_a64_feature_operator_precedence(operation))
        {
            ArmA64FeatureOperator pending = operators[--operator_count];
            if (!arm_a64_feature_apply_operator(pending, values, &value_count)) ARM_A64_FEATURE_INVALID();
        }
        if (operator_count >= ARM_A64_FEATURE_STACK_CAPACITY) ARM_A64_FEATURE_INVALID();
        operators[operator_count++] = operation;
        expect_operand = true;
    }
    if (expect_operand) ARM_A64_FEATURE_INVALID();
    while (operator_count)
    {
        ArmA64FeatureOperator operation = operators[--operator_count];
        if (operation == ARM_A64_FEATURE_LPAREN || !arm_a64_feature_apply_operator(operation, values, &value_count)) ARM_A64_FEATURE_INVALID();
    }
#undef ARM_A64_FEATURE_INVALID
    if (value_count != 1) { *valid_out = false; return false; }
    return values[0];
}

static bool arm_a64_feature_expression_allowed(String8 expression)
{
    bool valid = false;
    bool value = arm_a64_feature_expression_evaluate(expression, &valid);
    return valid && value;
}

static bool arm_a64_feature_parser_self_test(void)
{
    bool valid = false;
    if (!arm_a64_feature_expression_allowed(S8("FEAT_FP && FEAT_AdvSIMD"))) return false;
    if (!arm_a64_feature_expression_allowed(S8("FEAT_SME || FEAT_FP"))) return false;
    if (!arm_a64_feature_expression_allowed(S8("!FEAT_SME"))) return false;
    if (!arm_a64_feature_expression_allowed(S8("FEAT_FP || FEAT_SME && FEAT_SVE"))) return false;
    if (arm_a64_feature_expression_allowed(S8("FEAT_SME || FEAT_FP && FEAT_SVE"))) return false;
    if (arm_a64_feature_expression_allowed(S8("!FEAT_UNKNOWN_IN_2026_06"))) return false;
    if (arm_a64_feature_expression_allowed(S8("FEAT_UNKNOWN_IN_2026_06 == 'x'"))) return false;
    if (arm_a64_feature_expression_evaluate(S8("FEAT_UNKNOWN_IN_2026_06"), &valid) || valid) return false;
    if (arm_a64_feature_expression_evaluate(S8("FEAT_UNKNOWN_IN_2026_06 == 'x'"), &valid) || valid) return false;
    if (arm_a64_feature_expression_evaluate(S8("FEAT_FP &&"), &valid) || valid) return false;
    if (arm_a64_feature_expression_evaluate(S8("FEAT_SME"), &valid) || !valid) return false;
    char8 nested[256];
    u32 cursor = 0;
    for (u32 depth = 0; depth < 64; depth += 1) nested[cursor++] = '(';
    String8 atom = S8("FEAT_FP");
    memcpy(nested + cursor, atom.pointer, atom.length);
    cursor += (u32)atom.length;
    for (u32 depth = 0; depth < 64; depth += 1) nested[cursor++] = ')';
    if (!arm_a64_feature_expression_allowed((String8){.pointer = nested, .length = cursor})) return false;
    cursor = 0;
    for (u32 depth = 0; depth < 65; depth += 1) nested[cursor++] = '(';
    memcpy(nested + cursor, atom.pointer, atom.length);
    cursor += (u32)atom.length;
    for (u32 depth = 0; depth < 65; depth += 1) nested[cursor++] = ')';
    if (arm_a64_feature_expression_allowed((String8){.pointer = nested, .length = cursor})) return false;
    return true;
}

static void arm_a64_collect_fields(Arena* arena, ArmA64CanonicalRow* row, ArmA64Layout layout)
{
    for (u32 bit = 0; bit < 32; bit += 1)
    {
        ArmA64Bit value = layout.bits[bit];
        if (value.kind == ARM_A64_BIT_FIXED)
        {
            row->fixed_mask |= UINT32_C(1) << bit;
            row->fixed_value |= (u32)value.value << bit;
        }
        else if (value.kind == ARM_A64_BIT_FIELD)
        {
            row->field_mask |= UINT32_C(1) << bit;
        }
        else
        {
            row->unresolved_mask |= UINT32_C(1) << bit;
            row->explicit_unresolved_mask |= UINT32_C(1) << bit;
        }
        if ((value.kind == ARM_A64_BIT_FIELD || value.kind == ARM_A64_BIT_UNRESOLVED) && value.field.length)
        {
            u32 field_index = UINT32_MAX;
            for (u32 index = 0; index < row->field_count; index += 1)
            {
                if (string_equal(row->fields[index].name, value.field))
                {
                    field_index = index;
                    break;
                }
            }
            if (field_index == UINT32_MAX && row->field_count < BUSTER_ARRAY_LENGTH(row->fields))
            {
                field_index = row->field_count++;
                row->fields[field_index].name = string_duplicate_arena(arena, value.field, true);
            }
            if (field_index != UINT32_MAX)
            {
                ArmA64Field* field = &row->fields[field_index];
                if (field->bit_count < BUSTER_ARRAY_LENGTH(field->bits)) field->bits[field->bit_count++] = (u8)bit;
            }
        }
    }
    for (u32 field_index = 0; field_index < row->field_count; field_index += 1)
    {
        ArmA64Field* field = &row->fields[field_index];
        u32 start = 0;
        while (start < field->bit_count)
        {
            u32 end = start + 1;
            while (end < field->bit_count && field->bits[end] == field->bits[end - 1] + 1) end += 1;
            field->segments[field->segment_count++] = (ArmA64Segment){.instruction_lsb = field->bits[start],
                                                                       .width = (u8)(end - start),
                                                                       .value_lsb = (u8)start};
            start = end;
        }
    }
    row->box_count = BUSTER_MIN(layout.box_count, BUSTER_ARRAY_LENGTH(row->boxes));
    for (u32 index = 0; index < row->box_count; index += 1)
    {
        row->boxes[index] = layout.boxes[index];
        if (row->boxes[index].name.length) row->boxes[index].name = string_duplicate_arena(arena, row->boxes[index].name, true);
        if (row->boxes[index].constraint.length) row->boxes[index].constraint = string_duplicate_arena(arena, row->boxes[index].constraint, true);
        if (row->boxes[index].pattern.length) row->boxes[index].pattern = string_duplicate_arena(arena, row->boxes[index].pattern, true);
    }
}

static int arm_a64_row_compare(const void* left_pointer, const void* right_pointer)
{
    const ArmA64CanonicalRow* left = left_pointer;
    const ArmA64CanonicalRow* right = right_pointer;
    u64 count = BUSTER_MIN(left->canonical_id.length, right->canonical_id.length);
    int result = count ? memcmp(left->canonical_id.pointer, right->canonical_id.pointer, count) : 0;
    return result ? result : (left->canonical_id.length > right->canonical_id.length) - (left->canonical_id.length < right->canonical_id.length);
}

static bool arm_a64_validate_rows(ArmA64CanonicalRows rows)
{
    for (u32 index = 0; index < rows.count; index += 1)
    {
        ArmA64CanonicalRow* row = &rows.pointer[index];
        if (index && string_equal(rows.pointer[index - 1].canonical_id, row->canonical_id)) return false;
        for (u32 prior = 0; prior < index; prior += 1)
        {
            if (rows.pointer[prior].digest == row->digest) return false;
        }
        if (string_equal(row->kind, S8("alias")))
        {
            if (!row->alias_to_file.length || !row->alias_to_id.length || !row->alias_to_encoding_id.length) return false;
            u32 matches = 0;
            for (u32 target = 0; target < rows.count; target += 1)
            {
                ArmA64CanonicalRow* candidate = &rows.pointer[target];
                if (string_equal(candidate->kind, S8("canonical")) && string_equal(candidate->iform_file, row->alias_to_file) &&
                    string_equal(candidate->iform_id, row->alias_to_id) && string_equal(candidate->encoding_name, row->alias_to_encoding_id))
                    matches += 1;
            }
            if (matches != 1) return false;
        }
    }
    return true;
}

typedef struct ArmA64GprProjectionOperand ArmA64GprProjectionOperand;
struct ArmA64GprProjectionOperand
{
    u8 width;
    u8 bit_lsb;
    u8 register31_role;
};

typedef struct ArmA64GprProjectionRow ArmA64GprProjectionRow;
struct ArmA64GprProjectionRow
{
    ArmA64CanonicalRow* row;
    String8 mnemonic;
    ArmA64GprProjectionOperand operands[4];
    u32 operand_count;
};

static bool arm_a64_gpr_operand_token(String8 token, u8* width, u8* register31_role, String8* base)
{
    token = arm_a64_trim(token);
    u64 separator = BUSTER_STRING_NO_MATCH;
    for (u64 index = 0; index < token.length; index += 1)
    {
        if (token.pointer[index] == '|')
        {
            if (separator != BUSTER_STRING_NO_MATCH) return false;
            separator = index;
        }
    }
    String8 first = separator == BUSTER_STRING_NO_MATCH ? token : string_slice(token, 0, separator);
    String8 second = separator == BUSTER_STRING_NO_MATCH ? (String8){0} : string_slice(token, separator + 1, token.length);
    if (first.length != 2 || (first.pointer[0] != 'W' && first.pointer[0] != 'X') ||
        (first.pointer[1] != 'd' && first.pointer[1] != 'n' && first.pointer[1] != 'm' && first.pointer[1] != 'a' && first.pointer[1] != 's' &&
         first.pointer[1] != 't'))
    {
        return false;
    }
    if (second.length && !string_equal(second, S8("SP"))) return false;
    if (width) *width = first.pointer[0] == 'W' ? 32 : 64;
    if (register31_role) *register31_role = second.length ? 1 : 0;
    if (base) *base = first;
    return true;
}

static bool arm_a64_gpr_row_parse(ArmA64CanonicalRow* row, ArmA64GprProjectionRow* result)
{
    if (!row || !result || !row->apple_m1 || row->system || !string_equal(row->kind, S8("canonical")) || !string_equal(row->status, S8("defined")) ||
        row->unresolved_mask ||
        !row->assembly.length || row->field_count > 4 || row->fixed_value & ~row->fixed_mask || row->field_mask != ~row->fixed_mask)
    {
        return false;
    }
    String8 assembly = row->assembly;
    u64 position = 0;
    while (position < assembly.length && !character_is_space(assembly.pointer[position])) position += 1;
    String8 mnemonic = arm_a64_trim(string_slice(assembly, 0, position));
    if (!mnemonic.length) return false;
    ArmA64GprProjectionRow projection = {.row = row, .mnemonic = mnemonic};
    u32 field_used = 0;
    while (position < assembly.length)
    {
        while (position < assembly.length && character_is_space(assembly.pointer[position])) position += 1;
        if (position >= assembly.length) break;
        if (assembly.pointer[position] != '<' || projection.operand_count == 4) return false;
        u64 token_start = ++position;
        while (position < assembly.length && assembly.pointer[position] != '>') position += 1;
        if (position >= assembly.length) return false;
        String8 token = string_slice(assembly, token_start, position++);
        u8 width = 0, register31_role = 0;
        String8 base = {0};
        if (!arm_a64_gpr_operand_token(token, &width, &register31_role, &base)) return false;
        String8 wanted_name = {0};
        char8 wanted_buffer[4] = {'R', base.pointer[1], 0, 0};
        wanted_name = (String8){.pointer = wanted_buffer, .length = 2};
        u32 field_index = UINT32_MAX;
        for (u32 index = 0; index < row->field_count; index += 1)
        {
            if (string_equal(row->fields[index].name, wanted_name))
            {
                field_index = index;
                break;
            }
        }
        if (field_index == UINT32_MAX || (field_used & (UINT32_C(1) << field_index))) return false;
        ArmA64Field field = row->fields[field_index];
        if (field.segment_count != 1 || field.bit_count != 5 || field.segments[0].width != 5 || field.segments[0].value_lsb != 0)
        {
            return false;
        }
        field_used |= UINT32_C(1) << field_index;
        projection.operands[projection.operand_count++] = (ArmA64GprProjectionOperand){
            .width = width,
            .bit_lsb = field.segments[0].instruction_lsb,
            .register31_role = register31_role,
        };
        while (position < assembly.length && character_is_space(assembly.pointer[position])) position += 1;
        if (position >= assembly.length) break;
        if (assembly.pointer[position++] != ',') return false;
        while (position < assembly.length && character_is_space(assembly.pointer[position])) position += 1;
        if (position >= assembly.length || assembly.pointer[position] != '<') return false;
    }
    if (!projection.operand_count || projection.operand_count != row->field_count || field_used != (UINT32_C(1) << row->field_count) - 1u)
    {
        return false;
    }
    // The parser above consumes every visible token. Any literal text left in
    // the template would be an immediate, memory, label, alias, or system
    // operand and therefore cannot enter this direct-GPR projection.
    u32 used_bits = row->fixed_mask;
    for (u32 index = 0; index < projection.operand_count; index += 1)
    {
        ArmA64GprProjectionOperand operand = projection.operands[index];
        if (operand.bit_lsb > 27 || used_bits & (UINT32_C(0x1f) << operand.bit_lsb)) return false;
        used_bits |= UINT32_C(0x1f) << operand.bit_lsb;
    }
    if (used_bits != UINT32_MAX || (row->field_mask & ~used_bits)) return false;
    if (!(row->feature_expression.length == 0 || string_equal(row->feature_expression, S8("FEAT_CRC32")) ||
          string_equal(row->feature_expression, S8("FEAT_FlagM")) || string_equal(row->feature_expression, S8("FEAT_PAuth"))))
    {
        return false;
    }
    *result = projection;
    return true;
}

static bool arm_a64_gpr_projection(Arena* arena, ArmA64CanonicalRows rows, ArmA64GprProjectionRow** rows_out, u32* count_out)
{
    ArmA64GprProjectionRow* projection = arena_allocate(arena, ArmA64GprProjectionRow, 80);
    u32 count = 0;
    for (u32 index = 0; index < rows.count; index += 1)
    {
        ArmA64CanonicalRow* row = &rows.pointer[index];
        if (!row->apple_m1 || row->system || !string_equal(row->kind, S8("canonical"))) continue;
        ArmA64GprProjectionRow candidate = {0};
        if (!arm_a64_gpr_row_parse(row, &candidate)) continue;
        // Production lookup is ASCII-case-insensitive, but keep the checked-in
        // projection's spelling canonical and deterministic (lowercase).
        u64 mnemonic_mark = arena->position;
        for (u64 character_index = 0; character_index < candidate.mnemonic.length; character_index += 1)
        {
            char8 character = candidate.mnemonic.pointer[character_index];
            if (character >= 'A' && character <= 'Z') character = (char8)(character + ('a' - 'A'));
            arena_append_char8(arena, character);
        }
        candidate.mnemonic = (String8){.pointer = (char8*)arm_a64_arena_pointer(arena, mnemonic_mark), .length = candidate.mnemonic.length};
        if (count >= 80) return false;
        projection[count++] = candidate;
    }
    if (count != 80) return false;
    u32 arity[5] = {0}, feature[4] = {0};
    u32 mnemonic_count = 0;
    for (u32 index = 0; index < count; index += 1)
    {
        ArmA64GprProjectionRow candidate = projection[index];
        arity[candidate.operand_count] += 1;
        u32 feature_index = 3;
        if (!candidate.row->feature_expression.length) feature_index = 0;
        else if (string_equal(candidate.row->feature_expression, S8("FEAT_CRC32"))) feature_index = 1;
        else if (string_equal(candidate.row->feature_expression, S8("FEAT_FlagM"))) feature_index = 2;
        feature[feature_index] += 1;
        bool seen = false;
        for (u32 prior = 0; prior < index; prior += 1) seen |= string_equal(projection[prior].mnemonic, candidate.mnemonic);
        if (!seen) mnemonic_count += 1;
    }
    if (arity[1] != 18 || arity[2] != 23 || arity[3] != 31 || arity[4] != 8 || feature[0] != 43 || feature[1] != 8 || feature[2] != 2 ||
        feature[3] != 27 || mnemonic_count != 63)
    {
        return false;
    }
    *rows_out = projection;
    *count_out = count;
    return true;
}

/* The scalar-integer projection is intentionally recognized from the XML
   templates and field diagrams, rather than from a checked-in row list.  The
   suffixes below select instruction families; every candidate is then
   validated against its architectural field set, widths, and assembly shape. */
typedef enum ArmA64ScalarRecipe
{
    ARM_A64_SCALAR_RECIPE_ADD_SUB_EXT,
    ARM_A64_SCALAR_RECIPE_ADD_SUB_IMM,
    ARM_A64_SCALAR_RECIPE_ADD_SUB_SHIFT,
    ARM_A64_SCALAR_RECIPE_LOGICAL_IMM,
    ARM_A64_SCALAR_RECIPE_LOGICAL_SHIFT,
    ARM_A64_SCALAR_RECIPE_BITFIELD,
    ARM_A64_SCALAR_RECIPE_EXTRACT,
    ARM_A64_SCALAR_RECIPE_MOVEWIDE,
    ARM_A64_SCALAR_RECIPE_COND_CMP_IMM,
    ARM_A64_SCALAR_RECIPE_COND_CMP_REG,
    ARM_A64_SCALAR_RECIPE_RMIF,
    ARM_A64_SCALAR_RECIPE_UDF,
} ArmA64ScalarRecipe;

typedef enum ArmA64ScalarOperandKind
{
    ARM_A64_SCALAR_OPERAND_REGISTER,
    ARM_A64_SCALAR_OPERAND_IMMEDIATE,
} ArmA64ScalarOperandKind;

typedef enum ArmA64ScalarRegister31Role
{
    ARM_A64_SCALAR_REGISTER31_ZR,
    ARM_A64_SCALAR_REGISTER31_SP,
    ARM_A64_SCALAR_REGISTER31_ANY,
} ArmA64ScalarRegister31Role;

typedef struct ArmA64ScalarProjectionOperand ArmA64ScalarProjectionOperand;
struct ArmA64ScalarProjectionOperand
{
    u8 kind;
    u8 width;
    u8 register31_role;
};

typedef struct ArmA64ScalarProjectionRow ArmA64ScalarProjectionRow;
struct ArmA64ScalarProjectionRow
{
    ArmA64CanonicalRow* row;
    String8 mnemonic;
    u8 recipe;
    u8 width;
    u8 operand_count;
    ArmA64ScalarProjectionOperand operands[4];
};

static String8 arm_a64_gpr_feature_name(String8 expression);

static bool arm_a64_scalar_name_in(String8 value, const char* const* names, u32 count)
{
    bool result = false;
    for (u32 index = 0; index < count && !result; index += 1)
    {
        result = string_equal(value, string_from_pointer((char8*)names[index]));
    }

    return result;
}

static ArmA64Field* arm_a64_scalar_field(ArmA64CanonicalRow* row, const char* name)
{
    String8 wanted = string_from_pointer((char8*)name);
    for (u32 index = 0; index < row->field_count; index += 1)
    {
        if (string_equal(row->fields[index].name, wanted)) return &row->fields[index];
    }
    return 0;
}

static bool arm_a64_scalar_field_shape(ArmA64CanonicalRow* row, const char* name, u32 bit_count, u32 instruction_lsb)
{
    ArmA64Field* field = arm_a64_scalar_field(row, name);
    return field && field->bit_count == bit_count && field->segment_count == 1 && field->segments[0].width == bit_count &&
           field->segments[0].value_lsb == 0 && field->segments[0].instruction_lsb == instruction_lsb;
}

static bool arm_a64_scalar_field_names(ArmA64CanonicalRow* row, const char* const* names, u32 count)
{
    if (row->field_count != count) return false;
    for (u32 index = 0; index < count; index += 1)
    {
        if (!arm_a64_scalar_field(row, names[index])) return false;
    }
    return true;
}

static bool arm_a64_scalar_template_contains(ArmA64CanonicalRow* row, const char* text)
{
    return string_first_sequence(row->assembly, string_from_pointer((char8*)text)) != BUSTER_STRING_NO_MATCH;
}

static bool arm_a64_scalar_append_mnemonic(Arena* arena, ArmA64CanonicalRow* row, String8* result)
{
    if (!row->assembly.length || !result) return false;
    u64 end = 0;
    while (end < row->assembly.length && !character_is_space(row->assembly.pointer[end])) end += 1;
    if (!end) return false;
    u64 mark = arena->position;
    for (u64 index = 0; index < end; index += 1)
    {
        char8 c = row->assembly.pointer[index];
        if (c >= 'A' && c <= 'Z') c = (char8)(c + ('a' - 'A'));
        arena_append_char8(arena, c);
    }
    *result = (String8){.pointer = (char8*)arm_a64_arena_pointer(arena, mark), .length = end};
    return true;
}

static bool arm_a64_scalar_set_register(ArmA64ScalarProjectionRow* projection, ArmA64CanonicalRow* row, const char* name, u8 width,
                                        u8 register31_role)
{
    if (projection->operand_count >= BUSTER_ARRAY_LENGTH(projection->operands) || !arm_a64_scalar_field_shape(row, name, 5, name[1] == 'd' ? 0 :
                                                                                           name[1] == 'n' ? 5 : 16))
    {
        return false;
    }
    projection->operands[projection->operand_count++] = (ArmA64ScalarProjectionOperand){
        .kind = ARM_A64_SCALAR_OPERAND_REGISTER, .width = width, .register31_role = register31_role};
    return true;
}

static bool arm_a64_scalar_set_immediate(ArmA64ScalarProjectionRow* projection)
{
    bool result;
    if (projection->operand_count >= BUSTER_ARRAY_LENGTH(projection->operands))
    {
        result = false;
    }
    else
    {
        projection->operands[projection->operand_count++] = (ArmA64ScalarProjectionOperand){
            .kind = ARM_A64_SCALAR_OPERAND_IMMEDIATE, .width = 0, .register31_role = ARM_A64_SCALAR_REGISTER31_ANY};
        result = true;
    }

    return result;
}

static bool arm_a64_scalar_row_parse(Arena* arena, ArmA64CanonicalRow* row, ArmA64ScalarProjectionRow* result)
{
    if (!row || !result || !row->apple_m1 || row->system || !string_equal(row->kind, S8("canonical")) || row->unresolved_mask ||
        !row->assembly.length || (row->status.length && !string_equal(row->status, S8("defined")) &&
                                  !(string_equal(row->encoding_name, S8("UDF_only_perm_undef")) && string_equal(row->status, S8("undefined")))) ||
        (row->feature_expression.length && !string_equal(row->feature_expression, S8("FEAT_FlagM"))) ||
        row->fixed_value & ~row->fixed_mask || row->field_mask != ~row->fixed_mask)
    {
        return false;
    }
    ArmA64ScalarProjectionRow projection = {.row = row};
    if (!arm_a64_scalar_append_mnemonic(arena, row, &projection.mnemonic)) return false;
    static const char* const addsub[] = {"add", "adds", "sub", "subs"};
    static const char* const logical[] = {"and", "ands", "bic", "bics", "eon", "eor", "orn", "orr"};
    static const char* const bitfield[] = {"bfm", "sbfm", "ubfm"};
    static const char* const movewide[] = {"movk", "movn", "movz"};
    static const char* const condcmp[] = {"ccmn", "ccmp"};
    String8 suffix = {0};
    if (string_equal(row->encoding_name, S8("RMIF_only_rmif")))
    {
        projection.recipe = ARM_A64_SCALAR_RECIPE_RMIF;
        projection.width = 64;
        if (!arm_a64_scalar_template_contains(row, "#<shift>, #<mask>") || !string_equal(projection.mnemonic, S8("rmif")) ||
            !string_equal(row->feature_expression, S8("FEAT_FlagM")) ||
            !string_equal(row->instr_class, S8("general")) || !string_equal(row->status, S8("defined")) ||
            !arm_a64_scalar_field_names(row, (const char* const[]){"mask", "Rn", "imm6"}, 3) ||
            !arm_a64_scalar_field_shape(row, "mask", 4, 0) || !arm_a64_scalar_field_shape(row, "Rn", 5, 5) ||
            !arm_a64_scalar_field_shape(row, "imm6", 6, 15) || !arm_a64_scalar_set_register(&projection, row, "Rn", 64, ARM_A64_SCALAR_REGISTER31_ZR) ||
            !arm_a64_scalar_set_immediate(&projection) || !arm_a64_scalar_set_immediate(&projection))
        {
            return false;
        }
    }
    else if (string_equal(row->encoding_name, S8("UDF_only_perm_undef")))
    {
        projection.recipe = ARM_A64_SCALAR_RECIPE_UDF;
        projection.width = 32;
        if (!arm_a64_scalar_template_contains(row, "#<imm>") || !string_equal(projection.mnemonic, S8("udf")) ||
            !string_equal(row->instr_class, S8("general")) ||
            !string_equal(row->status, S8("undefined")) || !arm_a64_scalar_field_names(row, (const char* const[]){"imm16"}, 1) ||
            !arm_a64_scalar_field_shape(row, "imm16", 16, 0) || !arm_a64_scalar_set_immediate(&projection))
        {
            return false;
        }
    }
    else
    {
        static const char* const suffixes[] = {"_addsub_ext",  "_addsub_imm",  "_addsub_shift", "_log_imm",     "_log_shift",
                                                "_bitfield",    "_extract",     "_movewide",     "_condcmp_imm", "_condcmp_reg"};
        u32 suffix_index = UINT32_MAX;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(suffixes); index += 1)
        {
            String8 candidate = string_from_pointer((char8*)suffixes[index]);
            if (string_ends_with_sequence(row->encoding_name, candidate))
            {
                suffix = candidate;
                suffix_index = index;
                break;
            }
        }
        if (!suffix.length || !string_equal(row->instr_class, S8("general")) || !string_equal(row->status, S8("defined"))) return false;
        if (suffix_index <= 2 && !arm_a64_scalar_name_in(projection.mnemonic, addsub, BUSTER_ARRAY_LENGTH(addsub))) return false;
        if (suffix_index >= 3 && suffix_index <= 4 && !arm_a64_scalar_name_in(projection.mnemonic, logical, BUSTER_ARRAY_LENGTH(logical))) return false;
        if (suffix_index == 5 && !arm_a64_scalar_name_in(projection.mnemonic, bitfield, BUSTER_ARRAY_LENGTH(bitfield))) return false;
        if (suffix_index == 6 && !string_equal(projection.mnemonic, S8("extr"))) return false;
        if (suffix_index == 7 && !arm_a64_scalar_name_in(projection.mnemonic, movewide, BUSTER_ARRAY_LENGTH(movewide))) return false;
        if (suffix_index >= 8 && !arm_a64_scalar_name_in(projection.mnemonic, condcmp, BUSTER_ARRAY_LENGTH(condcmp))) return false;
        bool is32 = string_first_sequence(row->encoding_name, S8("_32")) != BUSTER_STRING_NO_MATCH;
        bool is64 = string_first_sequence(row->encoding_name, S8("_64")) != BUSTER_STRING_NO_MATCH;
        if (is32 == is64) return false;
        projection.width = is32 ? 32 : 64;
        switch (suffix_index)
        {
            case 0:
                projection.recipe = ARM_A64_SCALAR_RECIPE_ADD_SUB_EXT;
                if (!arm_a64_scalar_template_contains(row, "<extend>") || !arm_a64_scalar_template_contains(row, "#<amount>") ||
                    !arm_a64_scalar_field_names(row, (const char* const[]){"Rd", "Rn", "imm3", "option", "Rm"}, 5) ||
                    !arm_a64_scalar_field_shape(row, "Rd", 5, 0) || !arm_a64_scalar_field_shape(row, "Rn", 5, 5) ||
                    !arm_a64_scalar_field_shape(row, "imm3", 3, 10) || !arm_a64_scalar_field_shape(row, "option", 3, 13) ||
                    !arm_a64_scalar_field_shape(row, "Rm", 5, 16) ||
                    !arm_a64_scalar_set_register(&projection, row, "Rd", projection.width,
                                                 string_equal(projection.mnemonic, S8("add")) || string_equal(projection.mnemonic, S8("sub"))
                                                     ? ARM_A64_SCALAR_REGISTER31_SP
                                                     : ARM_A64_SCALAR_REGISTER31_ZR) ||
                    !arm_a64_scalar_set_register(&projection, row, "Rn", projection.width, ARM_A64_SCALAR_REGISTER31_SP))
                {
                    return false;
                }
                if (projection.operand_count >= BUSTER_ARRAY_LENGTH(projection.operands)) return false;
                projection.operands[projection.operand_count++] = (ArmA64ScalarProjectionOperand){
                    .kind = ARM_A64_SCALAR_OPERAND_REGISTER, .width = 0, .register31_role = ARM_A64_SCALAR_REGISTER31_ZR};
                break;
            case 1:
                projection.recipe = ARM_A64_SCALAR_RECIPE_ADD_SUB_IMM;
                if (!arm_a64_scalar_template_contains(row, "#<imm>") || !arm_a64_scalar_template_contains(row, "<shift>") ||
                    !arm_a64_scalar_field_names(row, (const char* const[]){"Rd", "Rn", "imm12", "sh"}, 4) ||
                    !arm_a64_scalar_field_shape(row, "Rd", 5, 0) || !arm_a64_scalar_field_shape(row, "Rn", 5, 5) ||
                    !arm_a64_scalar_field_shape(row, "imm12", 12, 10) || !arm_a64_scalar_field_shape(row, "sh", 1, 22) ||
                    !arm_a64_scalar_set_register(&projection, row, "Rd", projection.width,
                                                 string_equal(projection.mnemonic, S8("add")) || string_equal(projection.mnemonic, S8("sub"))
                                                     ? ARM_A64_SCALAR_REGISTER31_SP
                                                     : ARM_A64_SCALAR_REGISTER31_ZR) ||
                    !arm_a64_scalar_set_register(&projection, row, "Rn", projection.width, ARM_A64_SCALAR_REGISTER31_SP) ||
                    !arm_a64_scalar_set_immediate(&projection))
                {
                    return false;
                }
                break;
            case 2:
                projection.recipe = ARM_A64_SCALAR_RECIPE_ADD_SUB_SHIFT;
                if (!arm_a64_scalar_template_contains(row, "<shift> #<amount>") ||
                    !arm_a64_scalar_field_names(row, (const char* const[]){"Rd", "Rn", "imm6", "Rm", "shift"}, 5) ||
                    !arm_a64_scalar_field_shape(row, "Rd", 5, 0) || !arm_a64_scalar_field_shape(row, "Rn", 5, 5) ||
                    !arm_a64_scalar_field_shape(row, "imm6", 6, 10) || !arm_a64_scalar_field_shape(row, "Rm", 5, 16) ||
                    !arm_a64_scalar_field_shape(row, "shift", 2, 22) || !arm_a64_scalar_set_register(&projection, row, "Rd", projection.width,
                                                                                                      ARM_A64_SCALAR_REGISTER31_ZR) ||
                    !arm_a64_scalar_set_register(&projection, row, "Rn", projection.width, ARM_A64_SCALAR_REGISTER31_ZR) ||
                    !arm_a64_scalar_set_register(&projection, row, "Rm", projection.width, ARM_A64_SCALAR_REGISTER31_ZR))
                {
                    return false;
                }
                break;
            case 3:
                projection.recipe = ARM_A64_SCALAR_RECIPE_LOGICAL_IMM;
                if (!arm_a64_scalar_template_contains(row, "#<imm>") ||
                    (projection.width == 64 ? !arm_a64_scalar_field_names(row, (const char* const[]){"Rd", "Rn", "imms", "immr", "N"}, 5) :
                                             !arm_a64_scalar_field_names(row, (const char* const[]){"Rd", "Rn", "imms", "immr"}, 4)) ||
                    !arm_a64_scalar_field_shape(row, "Rd", 5, 0) || !arm_a64_scalar_field_shape(row, "Rn", 5, 5) ||
                    !arm_a64_scalar_field_shape(row, "imms", 6, 10) || !arm_a64_scalar_field_shape(row, "immr", 6, 16) ||
                    (projection.width == 64 && !arm_a64_scalar_field_shape(row, "N", 1, 22)) ||
                    !arm_a64_scalar_set_register(&projection, row, "Rd", projection.width,
                                                 string_equal(projection.mnemonic, S8("ands")) ? ARM_A64_SCALAR_REGISTER31_ZR :
                                                                                               ARM_A64_SCALAR_REGISTER31_SP) ||
                    !arm_a64_scalar_set_register(&projection, row, "Rn", projection.width, ARM_A64_SCALAR_REGISTER31_ZR) ||
                    !arm_a64_scalar_set_immediate(&projection))
                {
                    return false;
                }
                break;
            case 4:
                projection.recipe = ARM_A64_SCALAR_RECIPE_LOGICAL_SHIFT;
                if (!arm_a64_scalar_template_contains(row, "<shift> #<amount>") ||
                    !arm_a64_scalar_field_names(row, (const char* const[]){"Rd", "Rn", "imm6", "Rm", "shift"}, 5) ||
                    !arm_a64_scalar_field_shape(row, "Rd", 5, 0) || !arm_a64_scalar_field_shape(row, "Rn", 5, 5) ||
                    !arm_a64_scalar_field_shape(row, "imm6", 6, 10) || !arm_a64_scalar_field_shape(row, "Rm", 5, 16) ||
                    !arm_a64_scalar_field_shape(row, "shift", 2, 22) || !arm_a64_scalar_set_register(&projection, row, "Rd", projection.width,
                                                                                                      ARM_A64_SCALAR_REGISTER31_ZR) ||
                    !arm_a64_scalar_set_register(&projection, row, "Rn", projection.width, ARM_A64_SCALAR_REGISTER31_ZR) ||
                    !arm_a64_scalar_set_register(&projection, row, "Rm", projection.width, ARM_A64_SCALAR_REGISTER31_ZR))
                {
                    return false;
                }
                break;
            case 5:
                projection.recipe = ARM_A64_SCALAR_RECIPE_BITFIELD;
                if (!arm_a64_scalar_template_contains(row, "#<immr>, #<imms>") ||
                    !arm_a64_scalar_field_names(row, (const char* const[]){"Rd", "Rn", "imms", "immr"}, 4) ||
                    !arm_a64_scalar_field_shape(row, "Rd", 5, 0) || !arm_a64_scalar_field_shape(row, "Rn", 5, 5) ||
                    !arm_a64_scalar_field_shape(row, "imms", 6, 10) || !arm_a64_scalar_field_shape(row, "immr", 6, 16) ||
                    !arm_a64_scalar_set_register(&projection, row, "Rd", projection.width, ARM_A64_SCALAR_REGISTER31_ZR) ||
                    !arm_a64_scalar_set_register(&projection, row, "Rn", projection.width, ARM_A64_SCALAR_REGISTER31_ZR) ||
                    !arm_a64_scalar_set_immediate(&projection) || !arm_a64_scalar_set_immediate(&projection))
                {
                    return false;
                }
                break;
            case 6:
                projection.recipe = ARM_A64_SCALAR_RECIPE_EXTRACT;
                if (!arm_a64_scalar_template_contains(row, "#<lsb>") ||
                    !arm_a64_scalar_field_names(row, (const char* const[]){"Rd", "Rn", "imms", "Rm"}, 4) ||
                    !arm_a64_scalar_field_shape(row, "Rd", 5, 0) || !arm_a64_scalar_field_shape(row, "Rn", 5, 5) ||
                    !arm_a64_scalar_field_shape(row, "imms", projection.width == 64 ? 6 : 5, 10) || !arm_a64_scalar_field_shape(row, "Rm", 5, 16) ||
                    !arm_a64_scalar_set_register(&projection, row, "Rd", projection.width, ARM_A64_SCALAR_REGISTER31_ZR) ||
                    !arm_a64_scalar_set_register(&projection, row, "Rn", projection.width, ARM_A64_SCALAR_REGISTER31_ZR) ||
                    !arm_a64_scalar_set_register(&projection, row, "Rm", projection.width, ARM_A64_SCALAR_REGISTER31_ZR) ||
                    !arm_a64_scalar_set_immediate(&projection))
                {
                    return false;
                }
                break;
            case 7:
                projection.recipe = ARM_A64_SCALAR_RECIPE_MOVEWIDE;
                if (!arm_a64_scalar_template_contains(row, "#<imm>") || !arm_a64_scalar_template_contains(row, "LSL #<shift>") ||
                    !arm_a64_scalar_field_names(row, (const char* const[]){"Rd", "imm16", "hw"}, 3) ||
                    !arm_a64_scalar_field_shape(row, "Rd", 5, 0) || !arm_a64_scalar_field_shape(row, "imm16", 16, 5) ||
                    !arm_a64_scalar_field_shape(row, "hw", projection.width == 32 ? 1 : 2, 21) ||
                    !arm_a64_scalar_set_register(&projection, row, "Rd", projection.width, ARM_A64_SCALAR_REGISTER31_ZR) ||
                    !arm_a64_scalar_set_immediate(&projection))
                {
                    return false;
                }
                break;
            case 8:
            case 9:
                projection.recipe = suffix_index == 8 ? ARM_A64_SCALAR_RECIPE_COND_CMP_IMM : ARM_A64_SCALAR_RECIPE_COND_CMP_REG;
                if (!arm_a64_scalar_template_contains(row, "#<nzcv>, <cond>") ||
                    !arm_a64_scalar_field_shape(row, "nzcv", 4, 0) || !arm_a64_scalar_field_shape(row, "Rn", 5, 5) ||
                    !arm_a64_scalar_field_shape(row, "cond", 4, 12) || !arm_a64_scalar_set_register(&projection, row, "Rn", projection.width,
                                                                                                       ARM_A64_SCALAR_REGISTER31_ZR) ||
                    !arm_a64_scalar_set_immediate(&projection) ||
                    (suffix_index == 8 && (!arm_a64_scalar_field_names(row, (const char* const[]){"nzcv", "Rn", "cond", "imm5"}, 4) ||
                                           !arm_a64_scalar_field_shape(row, "imm5", 5, 16))) ||
                    (suffix_index == 9 && (!arm_a64_scalar_field_names(row, (const char* const[]){"nzcv", "Rn", "cond", "Rm"}, 4) ||
                                           !arm_a64_scalar_field_shape(row, "Rm", 5, 16))))
                {
                    return false;
                }
                if (suffix_index == 9)
                {
                    projection.operand_count -= 1;
                    if (!arm_a64_scalar_set_register(&projection, row, "Rm", projection.width, ARM_A64_SCALAR_REGISTER31_ZR)) return false;
                }
                if (!arm_a64_scalar_set_immediate(&projection) || !arm_a64_scalar_set_immediate(&projection)) return false;
                break;
        }
    }
    if (!projection.operand_count || projection.operand_count > 4) return false;
    if ((projection.recipe == ARM_A64_SCALAR_RECIPE_RMIF && projection.operand_count != 3) ||
        (projection.recipe == ARM_A64_SCALAR_RECIPE_UDF && projection.operand_count != 1) ||
        (projection.recipe >= ARM_A64_SCALAR_RECIPE_ADD_SUB_EXT && projection.recipe <= ARM_A64_SCALAR_RECIPE_LOGICAL_SHIFT &&
         projection.operand_count != 3) ||
        (projection.recipe == ARM_A64_SCALAR_RECIPE_BITFIELD && projection.operand_count != 4) ||
        (projection.recipe == ARM_A64_SCALAR_RECIPE_EXTRACT && projection.operand_count != 4) ||
        (projection.recipe == ARM_A64_SCALAR_RECIPE_MOVEWIDE && projection.operand_count != 2) ||
        (projection.recipe >= ARM_A64_SCALAR_RECIPE_COND_CMP_IMM && projection.recipe <= ARM_A64_SCALAR_RECIPE_COND_CMP_REG &&
         projection.operand_count != 4))
    {
        return false;
    }
    *result = projection;
    return true;
}

static void arm_a64_scalar_hex_bare_append(Arena* output, u64 value)
{
    static char8 digits[] = "0123456789abcdef";
    char8 buffer[16];
    for (u32 index = 0; index < 16; index += 1) buffer[15 - index] = digits[(value >> (index * 4)) & 0xf];
    u32 first = 0;
    while (first < 15 && buffer[first] == '0') first += 1;
    arena_append_string8(output, string_slice((String8){.pointer = buffer, .length = 16}, first, 16));
}

static bool arm_a64_scalar_projection(Arena* arena, ArmA64CanonicalRows rows, ArmA64ScalarProjectionRow** rows_out, u32* count_out)
{
    ArmA64ScalarProjectionRow* projection = arena_allocate(arena, ArmA64ScalarProjectionRow, 128);
    u32 count = 0;
    for (u32 index = 0; index < rows.count; index += 1)
    {
        ArmA64CanonicalRow* row = &rows.pointer[index];
        if (!row->apple_m1 || row->system || !string_equal(row->kind, S8("canonical"))) continue;
        static const char* const structural_suffixes[] = {"_addsub_ext",  "_addsub_imm",  "_addsub_shift", "_log_imm",     "_log_shift",
                                                           "_bitfield",    "_extract",     "_movewide",     "_condcmp_imm", "_condcmp_reg"};
        bool structural_candidate = string_equal(row->encoding_name, S8("RMIF_only_rmif")) || string_equal(row->encoding_name, S8("UDF_only_perm_undef"));
        for (u32 suffix_index = 0; suffix_index < BUSTER_ARRAY_LENGTH(structural_suffixes); suffix_index += 1)
            structural_candidate |= string_ends_with_sequence(row->encoding_name, string_from_pointer((char8*)structural_suffixes[suffix_index]));
        if (structural_candidate && row->feature_expression.length && !string_equal(row->feature_expression, S8("FEAT_FlagM"))) return false;
        ArmA64ScalarProjectionRow candidate = {0};
        if (!arm_a64_scalar_row_parse(arena, row, &candidate)) continue;
        if (count >= 128) return false;
        projection[count++] = candidate;
    }
    if (count != 72) return false;
    u32 arity[5] = {0}, recipe[12] = {0}, feature[2] = {0}, mnemonic_count = 0;
    ArmA64Sha256 identity;
    arm_a64_sha256_init(&identity);
    for (u32 index = 0; index < count; index += 1)
    {
        ArmA64ScalarProjectionRow candidate = projection[index];
        if (candidate.operand_count > 4) return false;
        arity[candidate.operand_count] += 1;
        recipe[candidate.recipe] += 1;
        feature[string_equal(candidate.row->feature_expression, S8("FEAT_FlagM")) ? 1 : 0] += 1;
        for (u32 prior = 0; prior < index; prior += 1)
        {
            if (string_equal(projection[prior].mnemonic, candidate.mnemonic)) break;
            if (prior + 1 == index) mnemonic_count += 1;
        }
        if (!index) mnemonic_count += 1;
        arm_a64_sha256_update(&identity, (u8*)candidate.row->canonical_id.pointer, candidate.row->canonical_id.length);
        arm_a64_sha256_update(&identity, (const u8*)" ", 1);
        arm_a64_sha256_update(&identity, (const u8*)"0x", 2);
        char8 digest[16];
        for (u32 nibble = 0; nibble < 16; nibble += 1)
        {
            u8 value = (u8)((candidate.row->digest >> ((15 - nibble) * 4)) & 0xf);
            digest[nibble] = (char8)(value < 10 ? '0' + value : 'a' + value - 10);
        }
        u32 first = 0;
        while (first < 15 && digest[first] == '0') first += 1;
        arm_a64_sha256_update(&identity, (u8*)digest + first, 16 - first);
        arm_a64_sha256_update(&identity, (const u8*)"\n", 1);
    }
    u8 identity_sha[32] = {0};
    arm_a64_sha256_final(&identity, identity_sha);
    static const u8 expected_identity[32] = {
        0x44, 0x29, 0xd9, 0xab, 0x06, 0x4a, 0x8e, 0x98, 0x56, 0x1c, 0x79, 0x4e, 0x8c, 0x54, 0x08, 0xa3,
        0x92, 0x2b, 0xc6, 0xa7, 0xf0, 0x7a, 0x32, 0x01, 0x5c, 0xaa, 0xa9, 0x93, 0x2b, 0xa2, 0xc4, 0x84,
    };
    if (mnemonic_count != 23 || arity[1] != 1 || arity[2] != 6 || arity[3] != 49 || arity[4] != 16 || feature[0] != 71 || feature[1] != 1 ||
        recipe[ARM_A64_SCALAR_RECIPE_ADD_SUB_EXT] != 8 || recipe[ARM_A64_SCALAR_RECIPE_ADD_SUB_IMM] != 8 ||
        recipe[ARM_A64_SCALAR_RECIPE_ADD_SUB_SHIFT] != 8 || recipe[ARM_A64_SCALAR_RECIPE_LOGICAL_IMM] != 8 ||
        recipe[ARM_A64_SCALAR_RECIPE_LOGICAL_SHIFT] != 16 || recipe[ARM_A64_SCALAR_RECIPE_BITFIELD] != 6 ||
        recipe[ARM_A64_SCALAR_RECIPE_EXTRACT] != 2 || recipe[ARM_A64_SCALAR_RECIPE_MOVEWIDE] != 6 ||
        recipe[ARM_A64_SCALAR_RECIPE_COND_CMP_IMM] != 4 || recipe[ARM_A64_SCALAR_RECIPE_COND_CMP_REG] != 4 ||
        recipe[ARM_A64_SCALAR_RECIPE_RMIF] != 1 || recipe[ARM_A64_SCALAR_RECIPE_UDF] != 1 || memcmp(identity_sha, expected_identity, sizeof(identity_sha)) != 0)
    {
        return false;
    }
    *rows_out = projection;
    *count_out = count;
    return true;
}

static String8 arm_a64_scalar_recipe_name(u8 recipe)
{
    static const char* names[] = {
        "BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_EXT",   "BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_IMM",
        "BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_SHIFT", "BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_IMM",
        "BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT", "BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_BITFIELD",
        "BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_EXTRACT",       "BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_MOVEWIDE",
        "BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_IMM",  "BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_REG",
        "BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_RMIF",          "BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_UDF",
    };
    return recipe < BUSTER_ARRAY_LENGTH(names) ? string_from_pointer((char8*)names[recipe]) : (String8){0};
}

static String8 arm_a64_scalar_operand_kind_name(u8 kind)
{
    return kind == ARM_A64_SCALAR_OPERAND_REGISTER ? S8("BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER")
                                                   : S8("BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE");
}

static String8 arm_a64_scalar_role_name(u8 role)
{
    static const char* names[] = {
        "BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR", "BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP",
        "BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY",
    };
    return role < BUSTER_ARRAY_LENGTH(names) ? string_from_pointer((char8*)names[role]) : (String8){0};
}

static bool arm_a64_append_scalar_header(Arena* output, ArmA64ScalarProjectionRow* rows, u32 count)
{
    arena_append_string8(output, S8("/* Generated structurally from the pinned Arm A64 XML; do not edit. */\n"
                                   "#ifndef BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_GENERATED_H\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_GENERATED_H\n"
                                   "#include <buster/lib/base.h>\n"
                                   "#include <buster/lib/target.h>\n\n"
                                   "typedef enum BusterAarch64ArmM1ScalarIntegerRecipe {\n"
                                   "    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_EXT,\n"
                                   "    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_IMM,\n"
                                   "    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_ADD_SUB_SHIFT,\n"
                                   "    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_IMM,\n"
                                   "    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_LOGICAL_SHIFT,\n"
                                   "    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_BITFIELD,\n"
                                   "    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_EXTRACT,\n"
                                   "    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_MOVEWIDE,\n"
                                   "    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_IMM,\n"
                                   "    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COND_CMP_REG,\n"
                                   "    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_RMIF,\n"
                                   "    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_UDF,\n"
                                   "    BUSTER_AARCH64_ARM_M1_SCALAR_RECIPE_COUNT,\n"
                                   "} BusterAarch64ArmM1ScalarIntegerRecipe;\n\n"
                                   "typedef enum BusterAarch64ArmM1ScalarIntegerOperandKind {\n"
                                   "    BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_REGISTER,\n"
                                   "    BUSTER_AARCH64_ARM_M1_SCALAR_OPERAND_IMMEDIATE,\n"
                                   "} BusterAarch64ArmM1ScalarIntegerOperandKind;\n\n"
                                   "typedef enum BusterAarch64ArmM1ScalarIntegerRegister31Role {\n"
                                   "    BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ZR,\n"
                                   "    BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_SP,\n"
                                   "    BUSTER_AARCH64_ARM_M1_SCALAR_REGISTER31_ANY,\n"
                                   "} BusterAarch64ArmM1ScalarIntegerRegister31Role;\n\n"
                                   "typedef struct BusterAarch64ArmM1ScalarIntegerGeneratedOperand BusterAarch64ArmM1ScalarIntegerGeneratedOperand;\n"
                                   "struct BusterAarch64ArmM1ScalarIntegerGeneratedOperand {\n"
                                   "    u8 kind;\n    u8 width;\n    u8 register31_role;\n    u8 reserved;\n};\n\n"
                                   "typedef struct BusterAarch64ArmM1ScalarIntegerGeneratedForm BusterAarch64ArmM1ScalarIntegerGeneratedForm;\n"
                                   "struct BusterAarch64ArmM1ScalarIntegerGeneratedForm {\n"
                                   "    const char* mnemonic;\n    const char* arm_row_id;\n    u64 arm_row_digest;\n    u32 fixed_mask;\n    u32 fixed_value;\n    TargetCpuFeature required_feature;\n    u8 recipe;\n    u8 width;\n    u8 operand_count;\n    u8 reserved;\n    BusterAarch64ArmM1ScalarIntegerGeneratedOperand operands[4];\n};\n\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_FORM_COUNT 72u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_MNEMONIC_COUNT 23u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_ARITY_1_COUNT 1u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_ARITY_2_COUNT 6u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_ARITY_3_COUNT 49u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_ARITY_4_COUNT 16u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_BASELINE_COUNT 71u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_FLAGM_COUNT 1u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_ADD_SUB_EXT_COUNT 8u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_ADD_SUB_IMM_COUNT 8u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_ADD_SUB_SHIFT_COUNT 8u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_LOGICAL_IMM_COUNT 8u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_LOGICAL_SHIFT_COUNT 16u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_BITFIELD_COUNT 6u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_EXTRACT_COUNT 2u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_MOVEWIDE_COUNT 6u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_COND_CMP_IMM_COUNT 4u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_COND_CMP_REG_COUNT 4u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_RMIF_COUNT 1u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_RECIPE_UDF_COUNT 1u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_IDENTITY_SHA256 \"4429d9ab064a8e98561c794e8c5408a3922bc6a7f07a32015caaa9932ba2c484\"\n\n"
                                   "static const BusterAarch64ArmM1ScalarIntegerGeneratedForm buster_aarch64_arm_m1_scalar_integer_generated_forms[] = {\n"));
    for (u32 index = 0; index < count; index += 1)
    {
        ArmA64ScalarProjectionRow candidate = rows[index];
        arena_append_string8(output, S8("    {\n        .mnemonic = "));
        arm_a64_c_string(output, candidate.mnemonic);
        arena_append_string8(output, S8(", .arm_row_id = "));
        arm_a64_c_string(output, candidate.row->canonical_id);
        arena_append_string8(output, S8(", .arm_row_digest = UINT64_C("));
        arm_a64_hex_append(output, candidate.row->digest);
        arena_append_string8(output, S8("),\n        .fixed_mask = UINT32_C("));
        arm_a64_hex32_append(output, candidate.row->fixed_mask);
        arena_append_string8(output, S8("), .fixed_value = UINT32_C("));
        arm_a64_hex32_append(output, candidate.row->fixed_value);
        arena_append_string8(output, S8("),\n        .required_feature = "));
        arena_append_string8(output, arm_a64_gpr_feature_name(candidate.row->feature_expression));
        arena_append_string8(output, S8(", .recipe = "));
        arena_append_string8(output, arm_a64_scalar_recipe_name(candidate.recipe));
        arena_append_string8(output, S8(", .width = "));
        arm_a64_append_u64(output, candidate.width);
        arena_append_string8(output, S8("u, .operand_count = "));
        arm_a64_append_u64(output, candidate.operand_count);
        arena_append_string8(output, S8("u,\n        .operands = {\n"));
        for (u32 operand_index = 0; operand_index < candidate.operand_count; operand_index += 1)
        {
            ArmA64ScalarProjectionOperand operand = candidate.operands[operand_index];
            arena_append_string8(output, S8("            {.kind = "));
            arena_append_string8(output, arm_a64_scalar_operand_kind_name(operand.kind));
            arena_append_string8(output, S8(", .width = "));
            arm_a64_append_u64(output, operand.width);
            arena_append_string8(output, S8("u, .register31_role = "));
            arena_append_string8(output, arm_a64_scalar_role_name(operand.register31_role));
            arena_append_string8(output, S8("},\n"));
        }
        arena_append_string8(output, S8("        },\n    },\n"));
    }
    arena_append_string8(output, S8("};\nBUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(buster_aarch64_arm_m1_scalar_integer_generated_forms) == BUSTER_AARCH64_ARM_M1_SCALAR_INTEGER_FORM_COUNT);\n#endif\n"));
    return true;
}

static String8 arm_a64_gpr_feature_name(String8 expression)
{
    String8 result;
    if (!expression.length)
    {
        result = S8("TARGET_CPU_FEATURE_NONE");
    }
    else if (string_equal(expression, S8("FEAT_CRC32")))
    {
        result = S8("TARGET_CPU_FEATURE_AARCH64_CRC");
    }
    else if (string_equal(expression, S8("FEAT_FlagM")))
    {
        result = S8("TARGET_CPU_FEATURE_AARCH64_FLAGM");
    }
    else if (string_equal(expression, S8("FEAT_PAuth")))
    {
        result = S8("TARGET_CPU_FEATURE_AARCH64_PAUTH");
    }
    else
    {
        result = (String8){0};
    }

    return result;
}

static bool arm_a64_append_gpr_header(Arena* output, ArmA64GprProjectionRow* rows, u32 count)
{
    arena_append_string8(output, S8("/* Generated by import_arm_a64_metadata from the pinned Arm XML; do not edit. */\n"
                                   "#ifndef BUSTER_AARCH64_ARM_M1_GPR_GENERATED_H\n"
                                   "#define BUSTER_AARCH64_ARM_M1_GPR_GENERATED_H\n"
                                   "#include <buster/lib/base.h>\n"
                                   "#include <buster/lib/target.h>\n\n"
                                   "typedef enum BusterAarch64ArmM1GprRegister31Role {\n"
                                   "    BUSTER_AARCH64_ARM_M1_GPR_31_ZR,\n"
                                   "    BUSTER_AARCH64_ARM_M1_GPR_31_SP,\n"
                                   "} BusterAarch64ArmM1GprRegister31Role;\n\n"
                                   "typedef struct BusterAarch64ArmM1GprGeneratedOperand BusterAarch64ArmM1GprGeneratedOperand;\n"
                                   "struct BusterAarch64ArmM1GprGeneratedOperand {\n"
                                   "    u8 width;\n    u8 bit_lsb;\n    u8 register31_role;\n    u8 reserved;\n};\n\n"
                                   "typedef struct BusterAarch64ArmM1GprGeneratedForm BusterAarch64ArmM1GprGeneratedForm;\n"
                                   "struct BusterAarch64ArmM1GprGeneratedForm {\n"
                                   "    const char* mnemonic;\n    const char* arm_row_id;\n    u64 arm_row_digest;\n    u32 fixed_mask;\n    u32 fixed_value;\n    TargetCpuFeature required_feature;\n    u8 operand_count;\n    u8 reserved[3];\n    BusterAarch64ArmM1GprGeneratedOperand operands[4];\n};\n\n"
                                   "#define BUSTER_AARCH64_ARM_M1_GPR_FORM_COUNT 80u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_GPR_MNEMONIC_COUNT 63u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_GPR_ARITY_1_COUNT 18u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_GPR_ARITY_2_COUNT 23u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_GPR_ARITY_3_COUNT 31u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_GPR_ARITY_4_COUNT 8u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_GPR_BASELINE_COUNT 43u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_GPR_CRC32_COUNT 8u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_GPR_FLAGM_COUNT 2u\n"
                                   "#define BUSTER_AARCH64_ARM_M1_GPR_PAUTH_COUNT 27u\n\n"
                                   "static const BusterAarch64ArmM1GprGeneratedForm buster_aarch64_arm_m1_gpr_generated_forms[] = {\n"));
    for (u32 index = 0; index < count; index += 1)
    {
        ArmA64GprProjectionRow candidate = rows[index];
        arena_append_string8(output, S8("    {\n        .mnemonic = "));
        arm_a64_c_string(output, candidate.mnemonic);
        arena_append_string8(output, S8(", .arm_row_id = "));
        arm_a64_c_string(output, candidate.row->canonical_id);
        arena_append_string8(output, S8(", .arm_row_digest = UINT64_C("));
        arm_a64_hex_append(output, candidate.row->digest);
        arena_append_string8(output, S8("),\n        .fixed_mask = UINT32_C("));
        arm_a64_hex32_append(output, candidate.row->fixed_mask);
        arena_append_string8(output, S8("), .fixed_value = UINT32_C("));
        arm_a64_hex32_append(output, candidate.row->fixed_value);
        arena_append_string8(output, S8("),\n        .required_feature = "));
        arena_append_string8(output, arm_a64_gpr_feature_name(candidate.row->feature_expression));
        arena_append_string8(output, S8(", .operand_count = "));
        arm_a64_append_u64(output, candidate.operand_count);
        arena_append_string8(output, S8("u,\n        .operands = {\n"));
        for (u32 operand_index = 0; operand_index < candidate.operand_count; operand_index += 1)
        {
            ArmA64GprProjectionOperand operand = candidate.operands[operand_index];
            arena_append_string8(output, S8("            {.width = ")); arm_a64_append_u64(output, operand.width);
            arena_append_string8(output, S8("u, .bit_lsb = ")); arm_a64_append_u64(output, operand.bit_lsb);
            arena_append_string8(output, operand.register31_role ? S8("u, .register31_role = BUSTER_AARCH64_ARM_M1_GPR_31_SP},\n")
                                                                  : S8("u, .register31_role = BUSTER_AARCH64_ARM_M1_GPR_31_ZR},\n"));
        }
        arena_append_string8(output, S8("        },\n    },\n"));
    }
    arena_append_string8(output, S8("};\nBUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(buster_aarch64_arm_m1_gpr_generated_forms) == BUSTER_AARCH64_ARM_M1_GPR_FORM_COUNT);\n#endif\n"));
    return true;
}

static bool arm_a64_validate_symmetric_difference(ArmA64CanonicalRows rows)
{
    static const char* excluded[] = {
        "AUTIA171615_64LR_dp_1src", "AUTIASPPCR_64LRR_dp_1src", "AUTIASPPC_only_dp_1src_imm", "AUTIB171615_64LR_dp_1src",
        "AUTIBSPPCR_64LRR_dp_1src", "AUTIBSPPC_only_dp_1src_imm", "PACIA171615_64LR_dp_1src", "PACIASPPC_64LR_dp_1src",
        "PACIB171615_64LR_dp_1src", "PACIBSPPC_64LR_dp_1src", "PACM_HI_hints", "PACNBIASPPC_64LR_dp_1src",
        "PACNBIBSPPC_64LR_dp_1src", "RETAASPPCR_64M_branch_reg", "RETABSPPCR_64M_branch_reg", "RETAASPPC_only_miscbranch",
        "RETABSPPC_only_miscbranch",
    };
    static const char* included[] = {
        "ESB_HI_hints", "CFP_SYS_CR_systeminstrs", "CPP_SYS_CR_systeminstrs", "DVP_SYS_CR_systeminstrs", "SHA1C_QSV_cryptosha3",
        "SHA1H_SS_cryptosha2", "SHA1M_QSV_cryptosha3", "SHA1P_QSV_cryptosha3", "SHA1SU0_VVV_cryptosha3", "SHA1SU1_VV_cryptosha2",
        "SHA512H2_QQV_cryptosha512_3", "SHA512H_QQV_cryptosha512_3", "SHA512SU0_VV2_cryptosha512_2", "SHA512SU1_VVV2_cryptosha512_3",
        "AXFLAG_M_pstate", "XAFLAG_M_pstate",
    };
    for (u32 group = 0; group < 2; group += 1)
    {
        const char** names = group ? included : excluded;
        u32 count = group ? BUSTER_ARRAY_LENGTH(included) : BUSTER_ARRAY_LENGTH(excluded);
        for (u32 index = 0; index < count; index += 1)
        {
            u32 matches = 0;
            bool selected = false;
            for (u32 row_index = 0; row_index < rows.count; row_index += 1)
            {
                if (string_equal(rows.pointer[row_index].encoding_name, string_from_pointer((char8*)names[index])))
                {
                    matches += 1;
                    selected = rows.pointer[row_index].apple_m1;
                }
            }
            if (matches != 1 || selected != (group != 0)) return false;
        }
    }
    return true;
}

static void arm_a64_row_digest(Arena* arena, ArmA64CanonicalRow* row)
{
    u64 mark = arena->position;
    arena_append_string8(arena, row->canonical_id);
    arena_append_char8(arena, '|');
    arm_a64_hex_append(arena, row->fixed_mask);
    arena_append_char8(arena, '|');
    arm_a64_hex_append(arena, row->fixed_value);
    arena_append_char8(arena, '|');
    for (u32 index = 0; index < row->field_count; index += 1)
    {
        arena_append_string8(arena, row->fields[index].name);
        for (u32 segment = 0; segment < row->fields[index].segment_count; segment += 1)
        {
            ArmA64Segment part = row->fields[index].segments[segment];
            arena_append_char8(arena, ':'); arm_a64_append_u64(arena, part.instruction_lsb);
            arena_append_char8(arena, ','); arm_a64_append_u64(arena, part.width);
            arena_append_char8(arena, ','); arm_a64_append_u64(arena, part.value_lsb);
        }
        arena_append_char8(arena, ';');
    }
    arena_append_string8(arena, row->feature_expression);
    arena_append_char8(arena, '|');
    arena_append_string8(arena, row->constraints);
    arena_append_char8(arena, '|');
    arena_append_string8(arena, row->assembly);
    arena_append_char8(arena, '|');
    arena_append_string8(arena, row->equivalent);
    arena_append_char8(arena, '|');
    arena_append_string8(arena, row->alias_condition);
    arena_append_char8(arena, '|');
    arena_append_string8(arena, row->alias_to_encoding_id);
    arena_append_char8(arena, '|');
    arena_append_string8(arena, row->alias_preference_condition);
    arena_append_char8(arena, ':');
    arm_a64_append_u64(arena, row->alias_preference_rank);
    for (u32 preference_index = 0; preference_index < row->alias_preference_count; preference_index += 1)
    {
        ArmA64AliasPreference preference = row->alias_preferences[preference_index];
        arena_append_char8(arena, '|');
        arm_a64_append_u64(arena, preference.rank);
        arena_append_char8(arena, ':'); arena_append_string8(arena, preference.alias_file);
        arena_append_char8(arena, ':'); arena_append_string8(arena, preference.alias_id);
        arena_append_char8(arena, ':'); arena_append_string8(arena, preference.condition);
    }
    for (u32 index = 0; index < row->box_count; index += 1)
    {
        ArmA64BoxConstraint box = row->boxes[index];
        arena_append_char8(arena, '|');
        arena_append_string8(arena, box.name);
        arena_append_char8(arena, ':'); arm_a64_append_u64(arena, box.hibit);
        arena_append_char8(arena, ','); arm_a64_append_u64(arena, box.width);
        arena_append_char8(arena, ':'); arena_append_string8(arena, box.constraint);
        arena_append_char8(arena, ':'); arena_append_string8(arena, box.pattern);
    }
    row->digest = buster_hash_64(arm_a64_arena_pointer(arena, mark), arena->position - mark);
    arena_set_position(arena, mark);
}

typedef struct ArmA64LayoutResolutionStats ArmA64LayoutResolutionStats;
struct ArmA64LayoutResolutionStats
{
    u32 selected_canonical;
    u32 promoted_rows;
    u32 source_m1_canonical_nonzero;
    u32 source_m1_alias_nonzero;
    u32 source_non_m1_canonical_nonzero;
    u32 source_non_m1_alias_nonzero;
    u8 changed_id_mask_sha256[32];
};

static void arm_a64_sha256_update_hex32(ArmA64Sha256* context, u32 value)
{
    static const char8 digits[] = "0123456789abcdef";
    char8 buffer[10] = {'0', 'x', 0};
    for (u32 index = 0; index < 8; index += 1) buffer[9 - index] = digits[(value >> (index * 4)) & 0xf];
    arm_a64_sha256_update(context, (u8*)buffer, sizeof(buffer));
}

static bool arm_a64_validate_layout_resolution(ArmA64CanonicalRows rows, ArmA64LayoutResolutionStats* stats)
{
    memset(stats, 0, sizeof(*stats));
    ArmA64Sha256 changed_sha;
    arm_a64_sha256_init(&changed_sha);
    u32 changed_count = 0;
    for (u32 index = 0; index < rows.count; index += 1)
    {
        ArmA64CanonicalRow const* row = &rows.pointer[index];
        bool canonical = string_equal(row->kind, S8("canonical"));
        bool in_scope = row->apple_m1 && canonical;
        bool source_nonzero = row->source_unresolved_mask != 0;
        if (row->apple_m1 && canonical) stats->selected_canonical += 1;
        if (source_nonzero)
        {
            if (row->apple_m1)
            {
                if (canonical) stats->source_m1_canonical_nonzero += 1;
                else stats->source_m1_alias_nonzero += 1;
            }
            else if (canonical) stats->source_non_m1_canonical_nonzero += 1;
            else stats->source_non_m1_alias_nonzero += 1;
        }
        if (!in_scope)
        {
            /* Aliases and non-M1 canonical rows retain the raw XML mask and
               its explicit/unresolved distinction byte-for-byte. */
            if (row->unresolved_mask != row->source_unresolved_mask || row->explicit_unresolved_mask != row->source_unresolved_mask)
                return false;
        }
        else
        {
            if (row->unresolved_mask || row->explicit_unresolved_mask) return false;
            if (source_nonzero)
            {
                stats->promoted_rows += 1;
                if (changed_count) arm_a64_sha256_update(&changed_sha, (const u8*)"\n", 1);
                changed_count += 1;
                arm_a64_sha256_update(&changed_sha, (u8*)row->canonical_id.pointer, row->canonical_id.length);
                arm_a64_sha256_update(&changed_sha, (const u8*)" ", 1);
                arm_a64_sha256_update_hex32(&changed_sha, row->source_unresolved_mask);
            }
        }
    }
    arm_a64_sha256_final(&changed_sha, stats->changed_id_mask_sha256);
    static const u8 expected_changed_sha256[32] = {
        0xc6, 0x06, 0x24, 0x07, 0xc2, 0x84, 0xfe, 0xb7, 0x74, 0x6a, 0x91, 0x21, 0x4c, 0x67, 0xa7, 0x39,
        0xdc, 0xb7, 0x0c, 0x8b, 0x76, 0x39, 0x62, 0xae, 0x1f, 0xf3, 0x53, 0x03, 0x6a, 0x23, 0x15, 0x43,
    };
    return stats->selected_canonical == 1523 && stats->promoted_rows == 133 && stats->source_m1_canonical_nonzero == 133 &&
           stats->source_m1_alias_nonzero == 22 && stats->source_non_m1_canonical_nonzero == 129 && stats->source_non_m1_alias_nonzero == 3 &&
           memcmp(stats->changed_id_mask_sha256, expected_changed_sha256, sizeof(expected_changed_sha256)) == 0;
}

static bool arm_a64_link_alias_preferences(Arena* arena, ArmA64CanonicalRows rows)
{
    for (u32 index = 0; index < rows.count; index += 1)
    {
        ArmA64CanonicalRow* alias = &rows.pointer[index];
        if (!string_equal(alias->kind, S8("alias"))) continue;
        ArmA64CanonicalRow* target = 0;
        u32 target_count = 0;
        for (u32 target_index = 0; target_index < rows.count; target_index += 1)
        {
            ArmA64CanonicalRow* candidate = &rows.pointer[target_index];
            if (string_equal(candidate->kind, S8("canonical")) && string_equal(candidate->iform_file, alias->alias_to_file) &&
                string_equal(candidate->iform_id, alias->alias_to_id) && string_equal(candidate->encoding_name, alias->alias_to_encoding_id))
            {
                target = candidate;
                target_count += 1;
            }
        }
        if (target_count != 1) return false;
        u32 preference_count = 0;
        ArmA64AliasPreference preference = {0};
        for (u32 preference_index = 0; preference_index < target->alias_preference_count; preference_index += 1)
        {
            ArmA64AliasPreference candidate = target->alias_preferences[preference_index];
            if (string_equal(candidate.alias_file, alias->iform_file) && string_equal(candidate.alias_id, alias->iform_id))
            {
                preference = candidate;
                preference_count += 1;
            }
        }
        if (preference_count != 1) return false;
        alias->alias_preference_condition = string_duplicate_arena(arena, preference.condition, true);
        alias->alias_preference_rank = preference.rank;
    }
    for (u32 index = 0; index < rows.count; index += 1) arm_a64_row_digest(arena, &rows.pointer[index]);
    return true;
}

static bool arm_a64_append_row_json(Arena* output, ArmA64CanonicalRow* row)
{
    arena_append_string8(output, S8("{\"schema_version\":2,\"id\":")); arm_a64_json_string(output, row->canonical_id);
    arena_append_string8(output, S8(",\"encoding_name\":")); arm_a64_json_string(output, row->encoding_name);
    arena_append_string8(output, S8(",\"page_id\":")); arm_a64_json_string(output, row->page_id);
    arena_append_string8(output, S8(",\"iform_file\":")); arm_a64_json_string(output, row->iform_file);
    arena_append_string8(output, S8(",\"iform_id\":")); arm_a64_json_string(output, row->iform_id);
    arena_append_string8(output, S8(",\"iclass_id\":")); arm_a64_json_string(output, row->iclass_id);
    arena_append_string8(output, S8(",\"iclass_name\":")); arm_a64_json_string(output, row->iclass_name);
    arena_append_string8(output, S8(",\"kind\":")); arm_a64_json_string(output, row->kind);
    arena_append_string8(output, S8(",\"alias_to\":{\"file\":"));
    if (row->alias_to_file.length) arm_a64_json_string(output, row->alias_to_file); else arena_append_string8(output, S8("null"));
    arena_append_string8(output, S8(",\"id\":"));
    if (row->alias_to_id.length) arm_a64_json_string(output, row->alias_to_id); else arena_append_string8(output, S8("null"));
    arena_append_string8(output, S8(",\"encoding_id\":"));
    if (row->alias_to_encoding_id.length) arm_a64_json_string(output, row->alias_to_encoding_id); else arena_append_string8(output, S8("null"));
    arena_append_string8(output, S8("}"));
    arena_append_string8(output, S8(",\"alias_preference_condition\":"));
    if (row->alias_preference_condition.length) arm_a64_json_string(output, row->alias_preference_condition); else arena_append_string8(output, S8("null"));
    arena_append_string8(output, S8(",\"alias_preference_rank\":"));
    if (row->alias_preference_condition.length) arm_a64_append_u64(output, row->alias_preference_rank); else arena_append_string8(output, S8("null"));
    arena_append_string8(output, S8(",\"alias_preferences\":["));
    for (u32 preference_index = 0; preference_index < row->alias_preference_count; preference_index += 1)
    {
        if (preference_index) arena_append_char8(output, ',');
        ArmA64AliasPreference preference = row->alias_preferences[preference_index];
        arena_append_string8(output, S8("{\"rank\":")); arm_a64_append_u64(output, preference.rank);
        arena_append_string8(output, S8(",\"alias_file\":")); arm_a64_json_string(output, preference.alias_file);
        arena_append_string8(output, S8(",\"alias_id\":")); arm_a64_json_string(output, preference.alias_id);
        arena_append_string8(output, S8(",\"condition\":")); arm_a64_json_string(output, preference.condition);
        arena_append_char8(output, '}');
    }
    arena_append_string8(output, S8("]"));
    arena_append_string8(output, S8(",\"assembly\":"));
    if (row->assembly.length) arm_a64_json_string(output, row->assembly); else arena_append_string8(output, S8("null"));
    arena_append_string8(output, S8(",\"equivalent\":"));
    if (row->equivalent.length) arm_a64_json_string(output, row->equivalent); else arena_append_string8(output, S8("null"));
    arena_append_string8(output, S8(",\"alias_condition\":"));
    if (row->alias_condition.length) arm_a64_json_string(output, row->alias_condition); else arena_append_string8(output, S8("null"));
    arena_append_string8(output, S8(",\"instr_class\":")); arm_a64_json_string(output, row->instr_class);
    arena_append_string8(output, S8(",\"system\":")); arena_append_string8(output, row->system ? S8("true") : S8("false"));
    arena_append_string8(output, S8(",\"feature_expression\":"));
    if (row->feature_expression.length) arm_a64_json_string(output, row->feature_expression); else arena_append_string8(output, S8("null"));
    arena_append_string8(output, S8(",\"feature_tags\":["));
    if (row->feature_tags.length) arena_append_string8(output, row->feature_tags);
    arena_append_string8(output, S8("]"));
    arena_append_string8(output, S8(",\"constraints\":"));
    if (row->constraints.length) arm_a64_json_string(output, row->constraints); else arena_append_string8(output, S8("null"));
    arena_append_string8(output, S8(",\"fixed_mask\":")); arm_a64_json_hex32(output, row->fixed_mask);
    arena_append_string8(output, S8(",\"fixed_value\":")); arm_a64_json_hex32(output, row->fixed_value);
    arena_append_string8(output, S8(",\"field_mask\":")); arm_a64_json_hex32(output, row->field_mask);
    arena_append_string8(output, S8(",\"unresolved_mask\":")); arm_a64_json_hex32(output, row->unresolved_mask);
    arena_append_string8(output, S8(",\"explicit_unresolved_mask\":")); arm_a64_json_hex32(output, row->explicit_unresolved_mask);
    arena_append_string8(output, S8(",\"fields\":["));
    for (u32 index = 0; index < row->field_count; index += 1)
    {
        if (index) arena_append_char8(output, ',');
        ArmA64Field field = row->fields[index];
        arena_append_string8(output, S8("{\"name\":")); arm_a64_json_string(output, field.name);
        arena_append_string8(output, S8(",\"segments\":["));
        for (u32 segment = 0; segment < field.segment_count; segment += 1)
        {
            if (segment) arena_append_char8(output, ',');
            ArmA64Segment part = field.segments[segment];
            arena_append_string8(output, S8("{\"instruction_lsb\":")); arm_a64_append_u64(output, part.instruction_lsb);
            arena_append_string8(output, S8(",\"width\":")); arm_a64_append_u64(output, part.width);
            arena_append_string8(output, S8(",\"value_lsb\":")); arm_a64_append_u64(output, part.value_lsb);
            arena_append_char8(output, '}');
        }
        arena_append_string8(output, S8("]}"));
    }
    arena_append_string8(output, S8("]"));
    arena_append_string8(output, S8(",\"box_constraints\":["));
    for (u32 index = 0; index < row->box_count; index += 1)
    {
        if (index) arena_append_char8(output, ',');
        ArmA64BoxConstraint box = row->boxes[index];
        arena_append_string8(output, S8("{\"name\":"));
        if (box.name.length) arm_a64_json_string(output, box.name); else arena_append_string8(output, S8("null"));
        arena_append_string8(output, S8(",\"hibit\":")); arm_a64_append_u64(output, box.hibit);
        arena_append_string8(output, S8(",\"width\":")); arm_a64_append_u64(output, box.width);
        arena_append_string8(output, S8(",\"constraint\":"));
        if (box.constraint.length) arm_a64_json_string(output, box.constraint); else arena_append_string8(output, S8("null"));
        arena_append_string8(output, S8(",\"pattern\":"));
        if (box.pattern.length) arm_a64_json_string(output, box.pattern); else arena_append_string8(output, S8("null"));
        arena_append_char8(output, '}');
    }
    arena_append_string8(output, S8("]"));
    arena_append_string8(output, S8(",\"mask_semantics\":\"fixed_mask|field_mask|unresolved_mask partition all 32 instruction bits\""));
    arena_append_string8(output, S8(",\"status\":")); arm_a64_json_string(output, row->status);
    arena_append_string8(output, S8(",\"apple_m1\":")); arena_append_string8(output, row->apple_m1 ? S8("true") : S8("false"));
    arena_append_string8(output, S8(",\"digest\":")); arm_a64_json_hex(output, row->digest);
    arena_append_string8(output, S8("}\n"));
    return true;
}

static bool arm_a64_parse_page(Arena* arena, Arena* scratch, String8 source, ArmA64Page page, ArmA64CanonicalRows* rows, u32* iclass_count,
                               u32* regdiagram_count, u32* alias_count)
{
    String8 path = path_join(scratch, source, page.file);
    ByteSlice bytes = file_read(scratch, path, (FileReadOptions){.end_padding = 1});
    if (!bytes.pointer || !bytes.length) return false;
    String8 text = {.pointer = (char8*)bytes.pointer, .length = bytes.length};
    ArmA64XmlCursor root_cursor = {.text = text};
    ArmA64XmlTag root = {0};
    bool root_found = false;
    while (arm_a64_xml_next(&root_cursor, &root))
    {
        if (root.kind == ARM_A64_XML_TAG_OPEN && arm_a64_tag_name_is(root, "instructionsection"))
        {
            if (root_found) return false;
            root_found = true;
            break;
        }
    }
    if (!root_found || !string_equal(arm_a64_tag_attr(root, S8("id")), page.id) ||
        (page.type.length && !string_equal(arm_a64_tag_attr(root, S8("type")), page.type)))
        return false;
    u64 root_end = 0;
    if (!arm_a64_find_element_end(text, root.start, S8("instructionsection"), &root_end)) return false;
    String8 page_type = arm_a64_tag_attr(root, S8("type"));
    String8 page_instr_class = {0};
    if (!arm_a64_collect_docvar(text, root.end, root_end, S8("instr-class"), &page_instr_class)) return false;
    String8 alias_to_file = {0};
    String8 alias_to_id = {0};
    ArmA64AliasPreference page_alias_preferences[32] = {0};
    u32 page_alias_preference_count = 0;
    ArmA64XmlCursor root_scan = {.text = text, .position = root.end};
    ArmA64XmlTag scan_tag = {0};
    while (arm_a64_xml_next(&root_scan, &scan_tag))
    {
        if (scan_tag.start >= root_end) break;
        if (scan_tag.kind == ARM_A64_XML_TAG_OPEN && arm_a64_tag_name_is(scan_tag, "aliasto"))
        {
            alias_to_file = arm_a64_tag_attr(scan_tag, S8("refiform"));
            alias_to_id = arm_a64_tag_attr(scan_tag, S8("iformid"));
        }
        else if (scan_tag.kind == ARM_A64_XML_TAG_OPEN && arm_a64_tag_name_is(scan_tag, "aliasref"))
        {
            if (page_alias_preference_count >= BUSTER_ARRAY_LENGTH(page_alias_preferences)) return false;
            u64 aliasref_end = scan_tag.end;
            if (!scan_tag.self_closing && !arm_a64_find_element_end(text, scan_tag.start, S8("aliasref"), &aliasref_end)) return false;
            ArmA64AliasPreference* preference = &page_alias_preferences[page_alias_preference_count];
            preference->alias_file = arm_a64_tag_attr(scan_tag, S8("aliasfile"));
            preference->alias_id = arm_a64_tag_attr(scan_tag, S8("aliaspageid"));
            if (!preference->alias_file.length || !preference->alias_id.length ||
                !arm_a64_xml_first_functional_element(arena, text, scan_tag.end, aliasref_end, "aliaspref", &preference->condition, 0))
                return false;
            preference->rank = page_alias_preference_count++;
            root_scan.position = aliasref_end;
        }
    }
    *alias_count += string_equal(page_type, S8("alias"));
    /* Count classes and parse each class independently. */
    ArmA64XmlCursor class_cursor = {.text = text, .position = root.end};
    ArmA64XmlTag class_tag = {0};
    while (arm_a64_xml_next(&class_cursor, &class_tag))
    {
        if (class_tag.start >= root_end) break;
        if (class_tag.kind != ARM_A64_XML_TAG_OPEN || !arm_a64_tag_name_is(class_tag, "iclass")) continue;
        u64 class_end = 0;
        if (!arm_a64_find_element_end(text, class_tag.start, S8("iclass"), &class_end)) return false;
        (*iclass_count) += 1;
        ArmA64Layout base_layout;
        arm_a64_layout_clear(&base_layout);
        ArmA64XmlCursor class_scan = {.text = text, .position = class_tag.end};
        ArmA64XmlTag child = {0};
        bool diagram_found = false;
        while (arm_a64_xml_next(&class_scan, &child))
        {
            if (child.start >= class_end) break;
            if (child.kind == ARM_A64_XML_TAG_OPEN && arm_a64_tag_name_is(child, "regdiagram"))
            {
                if (diagram_found) return false;
                u64 diagram_end = 0;
                if (!arm_a64_find_element_end(text, child.start, S8("regdiagram"), &diagram_end) ||
                    !arm_a64_layout_regdiagram(arena, text, child.end, diagram_end, &base_layout, false))
                    return false;
                diagram_found = true;
                (*regdiagram_count) += 1;
            }
        }
        if (!diagram_found) return false;
        String8 class_instr = arm_a64_tag_attr(class_tag, S8("name"));
        String8 class_id = arm_a64_tag_attr(class_tag, S8("id"));
        String8 class_features = {0};
        String8 class_tags = {0};
        if (!arm_a64_collect_features(arena, text, class_tag.end, class_end, &class_features, &class_tags)) return false;
        ArmA64XmlCursor encoding_cursor = {.text = text, .position = class_tag.end};
        ArmA64XmlTag encoding_tag = {0};
        while (arm_a64_xml_next(&encoding_cursor, &encoding_tag))
        {
            if (encoding_tag.start >= class_end) break;
            if (encoding_tag.kind != ARM_A64_XML_TAG_OPEN || !arm_a64_tag_name_is(encoding_tag, "encoding")) continue;
            u64 encoding_end = 0;
            if (!arm_a64_find_element_end(text, encoding_tag.start, S8("encoding"), &encoding_end)) return false;
            if (rows->count >= rows->capacity) return false;
            ArmA64CanonicalRow* row = &rows->pointer[rows->count++];
            memset(row, 0, sizeof(*row));
            row->encoding_name = string_duplicate_arena(arena, arm_a64_tag_attr(encoding_tag, S8("name")), true);
            if (!row->encoding_name.length) return false;
            row->page_id = page.id;
            row->iform_file = page.file;
            row->iform_id = page.id;
            row->iclass_id = string_duplicate_arena(arena, class_id, true);
            row->iclass_name = arm_a64_xml_decode(arena, class_instr);
            row->kind = string_equal(page_type, S8("alias")) ? S8("alias") : S8("canonical");
            row->alias_to_file = string_duplicate_arena(arena, alias_to_file, true);
            row->alias_to_id = string_duplicate_arena(arena, alias_to_id, true);
            row->alias_preference_count = page_alias_preference_count;
            for (u32 preference_index = 0; preference_index < page_alias_preference_count; preference_index += 1)
            {
                row->alias_preferences[preference_index] = page_alias_preferences[preference_index];
                row->alias_preferences[preference_index].alias_file = string_duplicate_arena(arena, page_alias_preferences[preference_index].alias_file, true);
                row->alias_preferences[preference_index].alias_id = string_duplicate_arena(arena, page_alias_preferences[preference_index].alias_id, true);
                row->alias_preferences[preference_index].condition = string_duplicate_arena(arena, page_alias_preferences[preference_index].condition, true);
            }
            row->instr_class = string_duplicate_arena(arena, page_instr_class, true);
            row->system = string_equal(page_instr_class, S8("system"));
            u64 id_mark = arena->position;
            arena_append_string8(arena, S8("arm-a64@2026-06:"));
            arena_append_string8(arena, row->encoding_name);
            row->canonical_id = (String8){.pointer = (char8*)arm_a64_arena_pointer(arena, id_mark), .length = arena->position - id_mark};
            row->constraints = arm_a64_xml_decode(arena, arm_a64_tag_attr(encoding_tag, S8("bitdiffs")));
            row->assembly = (String8){0};
            row->equivalent = (String8){0};
            row->alias_condition = (String8){0};
            ArmA64Layout layout = base_layout;
            ArmA64XmlCursor local_scan = {.text = text, .position = encoding_tag.end};
            ArmA64XmlTag local = {0};
            while (arm_a64_xml_next(&local_scan, &local))
            {
                if (local.start >= encoding_end) break;
                if (local.kind == ARM_A64_XML_TAG_OPEN && arm_a64_tag_name_is(local, "asmtemplate") && !row->assembly.length)
                {
                    u64 assembly_end = local.end;
                    if (!local.self_closing && !arm_a64_find_element_end(text, local.start, local.name, &assembly_end)) return false;
                    row->assembly = local.self_closing ? (String8){0} : arm_a64_xml_functional_text(arena, arm_a64_element_inner(text, local, assembly_end));
                    local_scan.position = assembly_end;
                }
                else if (local.kind == ARM_A64_XML_TAG_OPEN && arm_a64_tag_name_is(local, "box"))
                {
                    if (!arm_a64_layout_box(arena, text, local, &layout, true)) return false;
                }
                else if (local.kind == ARM_A64_XML_TAG_OPEN && arm_a64_tag_name_is(local, "equivalent_to"))
                {
                    u64 equivalent_end = 0;
                    if (!arm_a64_find_element_end(text, local.start, S8("equivalent_to"), &equivalent_end)) return false;
                    if (!arm_a64_xml_first_functional_element(arena, text, local.end, equivalent_end, "asmtemplate", &row->equivalent, 0) ||
                        !arm_a64_xml_first_functional_element(arena, text, local.end, equivalent_end, "aliascond", &row->alias_condition, 0))
                    {
                        return false;
                    }
                    if (!arm_a64_xml_first_href_fragment(arena, text, local.end, equivalent_end, &row->alias_to_encoding_id)) return false;
                    local_scan.position = equivalent_end;
                }
            }
            if (!row->assembly.length) return false;
            String8 local_features = {0};
            String8 local_tags = {0};
            if (!arm_a64_collect_features(arena, text, encoding_tag.end, encoding_end, &local_features, &local_tags)) return false;
            row->feature_expression = arm_a64_join_feature_expressions(arena, class_features, local_features);
            row->feature_tags = arm_a64_join_feature_tags(arena, class_tags, local_tags);
            row->status = string_starts_with_sequence(row->encoding_name, S8("UNALLOCATED")) ? S8("unallocated") :
                          (string_starts_with_sequence(row->encoding_name, S8("UDF_")) ? S8("undefined") : S8("defined"));
            bool feature_expression_valid = false;
            row->apple_m1 = arm_a64_feature_expression_evaluate(row->feature_expression, &feature_expression_valid);
            if (!feature_expression_valid) return false;
            row->source_unresolved_mask = arm_a64_layout_unresolved_mask(layout);
            if (row->apple_m1 && string_equal(row->kind, S8("canonical"))) arm_a64_layout_promote_named_symbols(&layout);
            arm_a64_collect_fields(arena, row, layout);
            if ((row->fixed_mask | row->field_mask | row->unresolved_mask) != UINT32_MAX) return false;
            arm_a64_row_digest(arena, row);
            if ((row->fixed_mask & row->field_mask) || (row->fixed_mask & row->unresolved_mask) || (row->field_mask & row->unresolved_mask) ||
                (row->fixed_mask | row->field_mask | row->unresolved_mask) != UINT32_MAX)
                return false;
        }
    }
    return true;
}

static bool arm_a64_append_manifest(Arena* output, u64 source_tree_digest, const u8 source_tree_sha256[32], u32 source_file_count, u64 artifact_hash,
                                    u64 artifact_bytes, u64 fixed_header_hash, u64 fixed_header_bytes,
                                    u64 gpr_header_hash, u64 gpr_header_bytes, u32 gpr_form_count, u32 gpr_mnemonic_count,
                                    const u32 gpr_arity_counts[4], const u32 gpr_feature_counts[4],
                                    u64 scalar_header_hash, u64 scalar_header_bytes, u32 scalar_form_count, u32 scalar_mnemonic_count,
                                    const u32 scalar_arity_counts[4], const u32 scalar_feature_counts[2],
                                    const u32 scalar_recipe_counts[12], const char* scalar_identity_sha256,
                                    u64 decoder_header_hash, u64 decoder_header_bytes, u64 decoder_audit_hash, u64 decoder_audit_bytes,
                                    ArmA64LayoutResolutionStats layout_resolution,
                                    ArmA64CanonicalRows rows, u32 page_count, u32 instruction_pages, u32 alias_pages, u32 pseudocode_pages,
                                    u32 iclass_count, u32 regdiagram_count, u32 selected, u32 selected_canonical,
                                    u32 selected_alias, u32 selected_system, u32 selected_system_canonical, u32 selected_system_alias,
                                    u32 selected_non_system, u32 selected_non_system_canonical, u32 selected_non_system_alias)
{
    arena_append_string8(output, S8("{\n  \"schema_version\": 2,\n  \"artifact_schema_version\": 2,\n  \"status\": \"provisional-canonical-foundation\",\n  \"acceptance\": \"blocked\",\n"));
    arena_append_string8(output,
                         S8("  \"source\": {\"release\": \"2026-06\", \"source_url\": \"https://developer.arm.com/-/cdn-downloads/permalink/Exploration-Tools-A64-ISA/ISA_A64/ISA_A64_xml_A_profile-2026-06.tar.gz\", \"archive_sha256\": \"63a01a1696483bbe2edfef9e0f0cd053d6c1c619ec0587876cb7a60bb344f354\", \"archive_sha256_role\": \"external-pinned-archive-identity; archive bytes are not read by this importer\", \"archive_identity_verified\": false, \"extracted_tree_sha256_verified\": true, \"tree_digest_algorithm\": \"sha256-v1 (FNV-1a64 convenience digest also recorded)\", \"tree_digest_scope\": \"sorted top-level regular filenames + NUL + little-endian uint64 byte length + bytes; subdirectories including xhtml/ generated presentation are excluded\", \"raw_xml_policy\": \"non-vendored-license-restricted\"},\n"));
    arena_append_string8(output, S8("  \"source_tree\": {\"regular_file_count\": ")); arm_a64_append_u64(output, source_file_count);
    arena_append_string8(output, S8(", \"fnv1a64\": ")); arm_a64_json_hex(output, source_tree_digest);
    arena_append_string8(output, S8(", \"sha256\": ")); arm_a64_json_sha256(output, source_tree_sha256);
    arena_append_string8(output, S8(", \"pinned_sha256\": \"0ee17fd2fe7ed165adda377d90f8f284d009e14d2300577f231c87ca6a45916d\"},\n"));
    arena_append_string8(output, S8("  \"inventory\": {\"pages\": ")); arm_a64_append_u64(output, page_count);
    arena_append_string8(output, S8(", \"instruction_pages\": ")); arm_a64_append_u64(output, instruction_pages);
    arena_append_string8(output, S8(", \"alias_pages\": ")); arm_a64_append_u64(output, alias_pages);
    arena_append_string8(output, S8(", \"pseudocode_pages\": ")); arm_a64_append_u64(output, pseudocode_pages);
    arena_append_string8(output, S8(", \"iclasses\": ")); arm_a64_append_u64(output, iclass_count);
    arena_append_string8(output, S8(", \"regdiagrams\": ")); arm_a64_append_u64(output, regdiagram_count);
    arena_append_string8(output, S8(", \"encodings\": ")); arm_a64_append_u64(output, rows.count); arena_append_string8(output, S8("},\n"));
    arena_append_string8(output, S8("  \"apple_m1_closure\": {\"selected\": ")); arm_a64_append_u64(output, selected);
    arena_append_string8(output, S8(", \"canonical\": ")); arm_a64_append_u64(output, selected_canonical);
    arena_append_string8(output, S8(", \"alias\": ")); arm_a64_append_u64(output, selected_alias);
    arena_append_string8(output, S8(", \"system\": ")); arm_a64_append_u64(output, selected_system);
    arena_append_string8(output, S8(", \"system_breakdown\": {\"canonical\": ")); arm_a64_append_u64(output, selected_system_canonical);
    arena_append_string8(output, S8(", \"alias\": ")); arm_a64_append_u64(output, selected_system_alias);
    arena_append_string8(output, S8("}, \"excluding_system\": ")); arm_a64_append_u64(output, selected_non_system);
    arena_append_string8(output, S8(", \"excluding_system_breakdown\": {\"canonical\": ")); arm_a64_append_u64(output, selected_non_system_canonical);
    arena_append_string8(output, S8(", \"alias\": ")); arm_a64_append_u64(output, selected_non_system_alias);
    arena_append_string8(output, S8("}"));
    arena_append_string8(output, S8(", \"derivation\": \"LLVM AppleA14/HasV8_4aOps feature closure cross-checked against Arm XML; not a silicon claim\"},\n"));
    arena_append_string8(output, S8("  \"layout_resolution\": {\"scope\": \"row.kind == canonical && row.apple_m1\", \"promoted_rows\": "));
    arm_a64_append_u64(output, layout_resolution.promoted_rows);
    arena_append_string8(output, S8(", \"selected_canonical\": "));
    arm_a64_append_u64(output, layout_resolution.selected_canonical);
    arena_append_string8(output, S8(", \"source_nonzero\": {\"m1_canonical\": "));
    arm_a64_append_u64(output, layout_resolution.source_m1_canonical_nonzero);
    arena_append_string8(output, S8(", \"m1_alias\": "));
    arm_a64_append_u64(output, layout_resolution.source_m1_alias_nonzero);
    arena_append_string8(output, S8(", \"non_m1_canonical\": "));
    arm_a64_append_u64(output, layout_resolution.source_non_m1_canonical_nonzero);
    arena_append_string8(output, S8(", \"non_m1_alias\": "));
    arm_a64_append_u64(output, layout_resolution.source_non_m1_alias_nonzero);
    arena_append_string8(output, S8("}, \"changed_id_mask_sha256\": "));
    arm_a64_json_sha256(output, layout_resolution.changed_id_mask_sha256);
    arena_append_string8(output, S8(", \"empty_overlay\": \"inherit\", \"named_symbol\": \"field\", \"unnamed_symbol\": \"unresolved\", \"colspan_constraints\": \"per-bit\"},\n"));
    arena_append_string8(output, S8("  \"artifact\": {\"file\": \"arm-a64-canonical.generated.jsonl\", \"bytes\": ")); arm_a64_append_u64(output, artifact_bytes);
    arena_append_string8(output, S8(", \"xxh64\": ")); arm_a64_json_hex(output, artifact_hash); arena_append_string8(output, S8("},\n"));
    arena_append_string8(output, S8("  \"fixed_spelling_artifact\": {\"file\": \"arm-a64-m1-fixed.generated.h\", \"bytes\": "));
    arm_a64_append_u64(output, fixed_header_bytes);
    arena_append_string8(output, S8(", \"xxh64\": ")); arm_a64_json_hex(output, fixed_header_hash);
    arena_append_string8(output, S8(", \"count\": 34, \"canonical\": 32, \"alias\": 2, \"system\": 17, \"non_system\": 17},\n"));
    arena_append_string8(output, S8("  \"direct_gpr_artifact\": {\"file\": \"arm-a64-m1-gpr.generated.h\", \"bytes\": "));
    arm_a64_append_u64(output, gpr_header_bytes);
    arena_append_string8(output, S8(", \"xxh64\": ")); arm_a64_json_hex(output, gpr_header_hash);
    arena_append_string8(output, S8(", \"forms\": ")); arm_a64_append_u64(output, gpr_form_count);
    arena_append_string8(output, S8(", \"mnemonics\": ")); arm_a64_append_u64(output, gpr_mnemonic_count);
    arena_append_string8(output, S8(", \"arity\": {\"1\": ")); arm_a64_append_u64(output, gpr_arity_counts[0]);
    arena_append_string8(output, S8(", \"2\": ")); arm_a64_append_u64(output, gpr_arity_counts[1]);
    arena_append_string8(output, S8(", \"3\": ")); arm_a64_append_u64(output, gpr_arity_counts[2]);
    arena_append_string8(output, S8(", \"4\": ")); arm_a64_append_u64(output, gpr_arity_counts[3]);
    arena_append_string8(output, S8("}, \"features\": {\"baseline\": ")); arm_a64_append_u64(output, gpr_feature_counts[0]);
    arena_append_string8(output, S8(", \"crc32\": ")); arm_a64_append_u64(output, gpr_feature_counts[1]);
    arena_append_string8(output, S8(", \"flagm\": ")); arm_a64_append_u64(output, gpr_feature_counts[2]);
    arena_append_string8(output, S8(", \"pauth\": ")); arm_a64_append_u64(output, gpr_feature_counts[3]);
    arena_append_string8(output, S8("}, \"unresolved_mask\": \"0x00000000\"},\n"));
    arena_append_string8(output, S8("  \"scalar_integer_artifact\": {\"file\": \"arm-a64-m1-scalar-integer.generated.h\", \"bytes\": "));
    arm_a64_append_u64(output, scalar_header_bytes);
    arena_append_string8(output, S8(", \"xxh64\": ")); arm_a64_json_hex(output, scalar_header_hash);
    arena_append_string8(output, S8(", \"forms\": ")); arm_a64_append_u64(output, scalar_form_count);
    arena_append_string8(output, S8(", \"mnemonics\": ")); arm_a64_append_u64(output, scalar_mnemonic_count);
    arena_append_string8(output, S8(", \"arity\": {\"1\": ")); arm_a64_append_u64(output, scalar_arity_counts[0]);
    arena_append_string8(output, S8(", \"2\": ")); arm_a64_append_u64(output, scalar_arity_counts[1]);
    arena_append_string8(output, S8(", \"3\": ")); arm_a64_append_u64(output, scalar_arity_counts[2]);
    arena_append_string8(output, S8(", \"4\": ")); arm_a64_append_u64(output, scalar_arity_counts[3]);
    arena_append_string8(output, S8("}, \"features\": {\"baseline\": ")); arm_a64_append_u64(output, scalar_feature_counts[0]);
    arena_append_string8(output, S8(", \"flagm\": ")); arm_a64_append_u64(output, scalar_feature_counts[1]);
    arena_append_string8(output, S8("}, \"recipes\": {\"add_sub_ext\": ")); arm_a64_append_u64(output, scalar_recipe_counts[0]);
    arena_append_string8(output, S8(", \"add_sub_imm\": ")); arm_a64_append_u64(output, scalar_recipe_counts[1]);
    arena_append_string8(output, S8(", \"add_sub_shift\": ")); arm_a64_append_u64(output, scalar_recipe_counts[2]);
    arena_append_string8(output, S8(", \"logical_imm\": ")); arm_a64_append_u64(output, scalar_recipe_counts[3]);
    arena_append_string8(output, S8(", \"logical_shift\": ")); arm_a64_append_u64(output, scalar_recipe_counts[4]);
    arena_append_string8(output, S8(", \"bitfield\": ")); arm_a64_append_u64(output, scalar_recipe_counts[5]);
    arena_append_string8(output, S8(", \"extract\": ")); arm_a64_append_u64(output, scalar_recipe_counts[6]);
    arena_append_string8(output, S8(", \"movewide\": ")); arm_a64_append_u64(output, scalar_recipe_counts[7]);
    arena_append_string8(output, S8(", \"condcmp_imm\": ")); arm_a64_append_u64(output, scalar_recipe_counts[8]);
    arena_append_string8(output, S8(", \"condcmp_reg\": ")); arm_a64_append_u64(output, scalar_recipe_counts[9]);
    arena_append_string8(output, S8(", \"rmif\": ")); arm_a64_append_u64(output, scalar_recipe_counts[10]);
    arena_append_string8(output, S8(", \"udf\": ")); arm_a64_append_u64(output, scalar_recipe_counts[11]);
    arena_append_string8(output, S8("}, \"normalized_identity_sha256\": "));
    arm_a64_json_string(output, string_from_pointer((char8*)scalar_identity_sha256));
    arena_append_string8(output, S8(", \"unresolved_mask\": \"0x00000000\"},\n"));
    arena_append_string8(output, S8("  \"canonical_decoder_artifact\": {\"file\": \"aarch64-canonical-decoder.generated.h\", \"bytes\": "));
    arm_a64_append_u64(output, decoder_header_bytes);
    arena_append_string8(output, S8(", \"xxh64\": "));
    arm_a64_json_hex(output, decoder_header_hash);
    arena_append_string8(output, S8(", \"forms\": 1523, \"aliases\": 0, \"constraint_programs\": 215, \"feature_programs\": 24},\n"));
    arena_append_string8(output, S8("  \"canonical_decoder_audit_artifact\": {\"file\": \"aarch64-canonical-decoder-audit.json\", \"bytes\": "));
    arm_a64_append_u64(output, decoder_audit_bytes);
    arena_append_string8(output, S8(", \"xxh64\": "));
    arm_a64_json_hex(output, decoder_audit_hash);
    arena_append_string8(output, S8(", \"forms\": 1523, \"representative_collision_pairs\": 22},\n"));
    arena_append_string8(output, S8("  \"symmetric_difference\": {\"count\": 33, \"excluded_pauth_lr\": 17, \"included_special\": 16, \"inventory\": ["));
    static const char* excluded[] = {"AUTIA171615_64LR_dp_1src","AUTIASPPCR_64LRR_dp_1src","AUTIASPPC_only_dp_1src_imm","AUTIB171615_64LR_dp_1src","AUTIBSPPCR_64LRR_dp_1src","AUTIBSPPC_only_dp_1src_imm","PACIA171615_64LR_dp_1src","PACIASPPC_64LR_dp_1src","PACIB171615_64LR_dp_1src","PACIBSPPC_64LR_dp_1src","PACM_HI_hints","PACNBIASPPC_64LR_dp_1src","PACNBIBSPPC_64LR_dp_1src","RETAASPPCR_64M_branch_reg","RETABSPPCR_64M_branch_reg","RETAASPPC_only_miscbranch","RETABSPPC_only_miscbranch"};
    static const char* included[] = {"ESB_HI_hints","CFP_SYS_CR_systeminstrs","CPP_SYS_CR_systeminstrs","DVP_SYS_CR_systeminstrs","SHA1C_QSV_cryptosha3","SHA1H_SS_cryptosha2","SHA1M_QSV_cryptosha3","SHA1P_QSV_cryptosha3","SHA1SU0_VVV_cryptosha3","SHA1SU1_VV_cryptosha2","SHA512H2_QQV_cryptosha512_3","SHA512H_QQV_cryptosha512_3","SHA512SU0_VV2_cryptosha512_2","SHA512SU1_VVV2_cryptosha512_3","AXFLAG_M_pstate","XAFLAG_M_pstate"};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(excluded); index += 1)
    {
        if (index) arena_append_char8(output, ',');
        arm_a64_json_string(output, string_from_pointer((char8*)excluded[index]));
    }
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(included); index += 1)
    {
        arena_append_char8(output, ',');
        arm_a64_json_string(output, string_from_pointer((char8*)included[index]));
    }
    arena_append_string8(output, S8("]},\n  \"unresolved\": \"semantic operands, system-register dictionary, and LLVM cross-references remain incomplete\"\n}\n"));
    return true;
}

static bool arm_a64_check_mode_enabled(void)
{
    const char* value = getenv("BUSTER_ARM_A64_CHECK");
    return value && value[0] == '1' && value[1] == 0;
}

static bool arm_a64_check_file(Arena* scratch, String8 path, String8 expected, const char* label)
{
    ByteSlice actual = file_read(scratch, path, (FileReadOptions){0});
    if (actual.length != expected.length || (expected.length && (!actual.pointer || memcmp(actual.pointer, expected.pointer, expected.length) != 0)))
    {
        fprintf(stderr, "error: Arm A64 checked-in %s drifted or is missing (%.*s)\n", label, (int)path.length, path.pointer);
        return false;
    }
    return true;
}

/* The canonical decoder table is generated from the same pinned JSONL by the
   checked-in bounded generator under tools/.  Keep the importer independent
   of Python at runtime: import copies the deterministic snapshot into a
   requested output directory, while check mode compares those bytes. */
static bool arm_a64_copy_decoder_snapshot(Arena* scratch, Arena* output, const char* file_name, String8* result)
{
    if (!result || !file_name)
    {
        return false;
    }
    /* The repository snapshot is the only trusted source.  In particular,
       never read the requested output directory here: check mode must compare
       that directory against these pinned bytes rather than copy a corrupt
       output back into the would-be import. */
    String8 path = path_join(scratch, S8("src/buster/lib/compiler/assembly/generated"), string_from_pointer((char8*)file_name));
    ByteSlice bytes = file_read(scratch, path, (FileReadOptions){0});
    if (!bytes.pointer || !bytes.length)
    {
        fprintf(stderr, "error: missing repository-pinned canonical decoder snapshot (%s)\n", file_name);
        return false;
    }
    u64 mark = output->position;
    arena_append_string8(output, (String8){.pointer = (char8*)bytes.pointer, .length = bytes.length});
    *result = (String8){.pointer = (char8*)arm_a64_arena_pointer(output, mark), .length = bytes.length};
    return true;
}

/* The checked-in decoder is an optional developer-generated snapshot, not a
   runtime dependency.  Import still validates its identity and the rows it
   claims to cover before copying it, so changing either the pinned snapshot or
   the canonical source requires an intentional table/audit update. */
static bool arm_a64_validate_decoder_snapshot(String8 artifact, String8 decoder_header, String8 decoder_audit, ArmA64CanonicalRows rows)
{
    static const u8 expected_artifact_sha256[32] = {
        0x84, 0x85, 0xc5, 0xc6, 0x18, 0x35, 0xd5, 0x39, 0x4d, 0x32, 0x57, 0x57, 0xab, 0x29, 0x64, 0x89,
        0x0e, 0x8b, 0xdf, 0xea, 0x30, 0x4c, 0x6f, 0xaa, 0x8f, 0xd4, 0xc2, 0x3e, 0x4c, 0x7a, 0xab, 0xec,
    };
    u8 artifact_sha256[32] = {0};
    arm_a64_sha256_bytes((u8*)artifact.pointer, artifact.length, artifact_sha256);
    u32 selected_canonical = 0;
    for (u32 index = 0; index < rows.count; index += 1)
    {
        ArmA64CanonicalRow* row = rows.pointer + index;
        if (row->apple_m1 && string_equal(row->kind, S8("canonical")))
        {
            selected_canonical += 1;
            if ((row->fixed_mask | row->field_mask | row->unresolved_mask) != UINT32_MAX) return false;
        }
    }
    if (memcmp(artifact_sha256, expected_artifact_sha256, sizeof(expected_artifact_sha256)) != 0 || selected_canonical != 1523 ||
        decoder_header.length != 1034723 || buster_hash_64((u8*)decoder_header.pointer, decoder_header.length) != 0x4932ef7e53365e52ULL ||
        decoder_audit.length != 384965 || buster_hash_64((u8*)decoder_audit.pointer, decoder_audit.length) != 0x76d833aa59165f16ULL)
    {
        return false;
    }
    static const char* required_header_markers[] = {
        "#define BUSTER_AARCH64_CANONICAL_DECODER_FORM_COUNT 1523u",
        "#define BUSTER_AARCH64_CANONICAL_DECODER_FIELD_COUNT 5387u",
        "#define BUSTER_AARCH64_CANONICAL_DECODER_CONSTRAINT_PROGRAM_COUNT 215u",
        "#define BUSTER_AARCH64_CANONICAL_DECODER_FEATURE_PROGRAM_COUNT 24u",
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(required_header_markers); index += 1)
    {
        if (string_first_sequence(decoder_header, string_from_pointer((char8*)required_header_markers[index])) == BUSTER_STRING_NO_MATCH) return false;
    }
    static const char* required_audit_markers[] = {
        "\"form_count\": 1523",
        "\"constraint_program_count\": 215",
        "\"feature_program_count\": 24",
        "\"constrained_pairs\": 22",
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(required_audit_markers); index += 1)
    {
        if (string_first_sequence(decoder_audit, string_from_pointer((char8*)required_audit_markers[index])) == BUSTER_STRING_NO_MATCH) return false;
    }
    return true;
}

static bool arm_a64_readme_canonical_section_digest(ByteSlice readme, u8 digest[32])
{
    String8 text = {.pointer = (char8*)readme.pointer, .length = readme.length};
    String8 begin_marker = S8("<!-- arm-a64-canonical-check:start -->");
    String8 end_marker = S8("<!-- arm-a64-canonical-check:end -->");
    u64 begin = string_first_sequence(text, begin_marker);
    if (begin == BUSTER_STRING_NO_MATCH) return false;
    u64 end_relative = string_first_sequence(string_slice(text, begin + begin_marker.length, text.length), end_marker);
    if (end_relative == BUSTER_STRING_NO_MATCH) return false;
    u64 end = begin + begin_marker.length + end_relative + end_marker.length;
    arm_a64_sha256_bytes((u8*)text.pointer + begin, end - begin, digest);
    return true;
}

static bool arm_a64_check_existing_outputs(Arena* arena, String8 output_directory, String8 artifact, String8 manifest, String8 fixed_header, String8 gpr_header,
                                           String8 scalar_header, String8 decoder_header, String8 decoder_audit)
{
    TemporalArena scratch_scope = scratch_begin(&arena, 1);
    Arena* scratch = scratch_scope.arena;
    String8 artifact_path = path_join(scratch, output_directory, S8("arm-a64-canonical.generated.jsonl"));
    String8 manifest_path = path_join(scratch, output_directory, S8("arm-a64-canonical-manifest.json"));
    String8 fixed_header_path = path_join(scratch, output_directory, S8("arm-a64-m1-fixed.generated.h"));
    String8 gpr_header_path = path_join(scratch, output_directory, S8("arm-a64-m1-gpr.generated.h"));
    String8 scalar_header_path = path_join(scratch, output_directory, S8("arm-a64-m1-scalar-integer.generated.h"));
    String8 decoder_header_path = path_join(scratch, output_directory, S8("aarch64-canonical-decoder.generated.h"));
    String8 decoder_audit_path = path_join(scratch, output_directory, S8("aarch64-canonical-decoder-audit.json"));
    String8 readme_path = path_join(scratch, output_directory, S8("README.md"));
    bool valid = arm_a64_check_file(scratch, artifact_path, artifact, "canonical artifact") &&
                 arm_a64_check_file(scratch, manifest_path, manifest, "canonical manifest") &&
                 arm_a64_check_file(scratch, fixed_header_path, fixed_header, "M1 fixed-spelling header") &&
                 arm_a64_check_file(scratch, gpr_header_path, gpr_header, "M1 direct-GPR header") &&
                 arm_a64_check_file(scratch, scalar_header_path, scalar_header, "M1 scalar-integer header") &&
                 arm_a64_check_file(scratch, decoder_header_path, decoder_header, "canonical decoder header") &&
                 arm_a64_check_file(scratch, decoder_audit_path, decoder_audit, "canonical decoder audit");
    ByteSlice readme = file_read(scratch, readme_path, (FileReadOptions){0});
    static const u8 expected_readme_section_sha256[32] = {
        0xb8, 0x5c, 0x9b, 0x71, 0xa9, 0x85, 0x27, 0x76, 0xc3, 0x80, 0x4e, 0x0f, 0xd7, 0xf5, 0x5e, 0x91,
        0xd9, 0xa8, 0x4d, 0xb8, 0x95, 0xce, 0xf3, 0x21, 0xc6, 0x55, 0x2c, 0xee, 0x23, 0x6f, 0x13, 0x98,
    };
    u8 readme_section_sha256[32] = {0};
    if (!readme.pointer || !readme.length || !arm_a64_readme_canonical_section_digest(readme, readme_section_sha256) ||
        memcmp(readme_section_sha256, expected_readme_section_sha256, sizeof(expected_readme_section_sha256)) != 0)
    {
        fprintf(stderr, "error: Arm A64 checked-in README.md canonical section drifted or is missing (%.*s)\n", (int)readme_path.length, readme_path.pointer);
        valid = false;
    }
    scratch_end(scratch_scope);
    return valid;
}

static String8 arm_a64_fixed_target_feature_name(String8 expression)
{
    String8 result;
    if (!expression.length)
    {
        result = S8("TARGET_CPU_FEATURE_NONE");
    }
    else if (string_equal(expression, S8("FEAT_FlagM")))
    {
        result = S8("TARGET_CPU_FEATURE_AARCH64_FLAGM");
    }
    else if (string_equal(expression, S8("FEAT_FlagM2")))
    {
        result = S8("TARGET_CPU_FEATURE_AARCH64_ALTNZCV");
    }
    else if (string_equal(expression, S8("FEAT_PAuth")))
    {
        result = S8("TARGET_CPU_FEATURE_AARCH64_PAUTH");
    }
    else if (string_equal(expression, S8("FEAT_RAS")))
    {
        result = S8("TARGET_CPU_FEATURE_AARCH64_RAS");
    }
    else if (string_equal(expression, S8("FEAT_SB")))
    {
        result = S8("TARGET_CPU_FEATURE_AARCH64_SB");
    }
    else if (string_equal(expression, S8("FEAT_TRF")))
    {
        result = S8("TARGET_CPU_FEATURE_AARCH64_TRACEV8_4");
    }
    else
    {
        result = (String8){0};
    }

    return result;
}

static bool arm_a64_append_fixed_header(Arena* output, ArmA64CanonicalRows rows)
{
    u32 fixed_count = 0;
    u32 canonical_count = 0;
    u32 alias_count = 0;
    u32 system_count = 0;
    u32 non_system_count = 0;
    for (u32 index = 0; index < rows.count; index += 1)
    {
        ArmA64CanonicalRow const* row = &rows.pointer[index];
        if (!row->apple_m1 || row->fixed_mask != UINT32_MAX || row->field_mask != 0 || row->unresolved_mask != 0) continue;
        if (!row->assembly.length || !row->canonical_id.length || !row->digest ||
            !arm_a64_fixed_target_feature_name(row->feature_expression).length ||
            (!string_equal(row->kind, S8("canonical")) && !string_equal(row->kind, S8("alias"))))
        {
            return false;
        }
        fixed_count += 1;
        canonical_count += string_equal(row->kind, S8("canonical"));
        alias_count += string_equal(row->kind, S8("alias"));
        system_count += row->system;
        non_system_count += !row->system;
    }
    if (fixed_count != 34 || canonical_count != 32 || alias_count != 2 || system_count != 17 || non_system_count != 17) return false;

    arena_append_string8(output, S8("/* Generated by build import_arm_a64_metadata from arm-a64-canonical.generated.jsonl; do not edit. */\n"
                                   "#ifndef BUSTER_AARCH64_ARM_M1_FIXED_GENERATED_H\n"
                                   "#define BUSTER_AARCH64_ARM_M1_FIXED_GENERATED_H\n"
                                   "#include <buster/lib/base.h>\n"
                                   "#include <buster/lib/target.h>\n\n"
                                   "typedef struct BusterAarch64ArmM1GeneratedFixedRow BusterAarch64ArmM1GeneratedFixedRow;\n"
                                   "struct BusterAarch64ArmM1GeneratedFixedRow\n"
                                   "{\n"
                                   "    const char* spelling;\n"
                                   "    const char* arm_row_id;\n"
                                   "    u32 word;\n"
                                   "    u64 arm_row_digest;\n"
                                   "    TargetCpuFeature required_feature;\n"
                                   "    bool canonical;\n"
                                   "    bool alias;\n"
                                   "    bool system;\n"
                                   "    u8 reserved;\n"
                                   "};\n\n"
                                   "#define BUSTER_AARCH64_ARM_M1_FIXED_SPELLING_COUNT "));
    arm_a64_append_u64(output, fixed_count);
    arena_append_string8(output, S8("u\n#define BUSTER_AARCH64_ARM_M1_FIXED_CANONICAL_COUNT "));
    arm_a64_append_u64(output, canonical_count);
    arena_append_string8(output, S8("u\n#define BUSTER_AARCH64_ARM_M1_FIXED_ALIAS_COUNT "));
    arm_a64_append_u64(output, alias_count);
    arena_append_string8(output, S8("u\n#define BUSTER_AARCH64_ARM_M1_FIXED_SYSTEM_COUNT "));
    arm_a64_append_u64(output, system_count);
    arena_append_string8(output, S8("u\n#define BUSTER_AARCH64_ARM_M1_FIXED_NON_SYSTEM_COUNT "));
    arm_a64_append_u64(output, non_system_count);
    arena_append_string8(output, S8("u\n#define BUSTER_AARCH64_ARM_M1_FIXED_MASK UINT32_C(0xffffffff)\n\n"
                                   "static const BusterAarch64ArmM1GeneratedFixedRow buster_aarch64_arm_m1_generated_fixed_rows[] = {\n"));

    u32 emitted = 0;
    for (u32 index = 0; index < rows.count; index += 1)
    {
        ArmA64CanonicalRow const* row = &rows.pointer[index];
        if (!row->apple_m1 || row->fixed_mask != UINT32_MAX || row->field_mask != 0 || row->unresolved_mask != 0) continue;
        arena_append_string8(output, S8("    {"));
        arm_a64_c_string(output, row->assembly);
        arena_append_string8(output, S8(", "));
        arm_a64_c_string(output, row->canonical_id);
        arena_append_string8(output, S8(", UINT32_C("));
        arm_a64_hex32_append(output, row->fixed_value);
        arena_append_string8(output, S8("), UINT64_C("));
        arm_a64_hex_append(output, row->digest);
        arena_append_string8(output, S8("), "));
        arena_append_string8(output, arm_a64_fixed_target_feature_name(row->feature_expression));
        arena_append_string8(output, row->kind.length && string_equal(row->kind, S8("canonical")) ? S8(", true, false, ") : S8(", false, true, "));
        arena_append_string8(output, row->system ? S8("true, 0},\n") : S8("false, 0},\n"));
        emitted += 1;
    }
    if (emitted != fixed_count) return false;
    arena_append_string8(output, S8("};\nBUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(buster_aarch64_arm_m1_generated_fixed_rows) == BUSTER_AARCH64_ARM_M1_FIXED_SPELLING_COUNT);\n\n#endif\n"));
    return true;
}

static ProcessResult arm_a64_canonical_import_run(Arena* arena, ArmA64CanonicalImportOptions options)
{
    if (!options.source_directory.length || !options.output_directory.length) return PROCESS_RESULT_FAILED;
    if (!arm_a64_layout_self_test())
    {
        string_print(S8("error: Arm A64 layout overlay self-test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    if (!arm_a64_sha256_self_test())
    {
        string_print(S8("error: Arm A64 SHA-256 self-test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    if (!arm_a64_feature_parser_self_test())
    {
        string_print(S8("error: Arm A64 feature parser self-test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    u32 source_file_count = 0;
    u64 source_tree_digest = 0;
    u8 source_tree_sha256[32] = {0};
    static const u8 expected_source_tree_sha256[32] = {
        0x0e, 0xe1, 0x7f, 0xd2, 0xfe, 0x7e, 0xd1, 0x65, 0xad, 0xda, 0x37, 0x7d, 0x90, 0xf8, 0xf2, 0x84,
        0xd0, 0x09, 0xe1, 0x4d, 0x23, 0x00, 0x57, 0x7f, 0x23, 0x1c, 0x87, 0xca, 0x6a, 0x45, 0x91, 0x6d,
    };
    if (!arm_a64_source_tree_digest(arena, options.source_directory, &source_file_count, &source_tree_digest, source_tree_sha256) ||
        source_file_count != 2316 || source_tree_digest != UINT64_C(0xba0c8fc560297896) ||
        memcmp(source_tree_sha256, expected_source_tree_sha256, sizeof(expected_source_tree_sha256)) != 0)
    {
        string_print(S8("error: Arm A64 source tree identity mismatch files={u32} digest={u64} expected-files=2316 expected-fnv=0xba0c8fc560297896\n"),
                     source_file_count, source_tree_digest);
        return PROCESS_RESULT_FAILED;
    }
    TemporalArena scratch_scope = scratch_begin(&arena, 1);
    Arena* scratch = scratch_scope.arena;
    ArmA64Page* index_pages = arena_allocate(arena, ArmA64Page, 600);
    u32 index_page_count = 0;
    if (!arm_a64_parse_index(arena, options.source_directory, index_pages, 600, &index_page_count))
    {
        string_print(S8("error: official Arm A64 index.xml is malformed or incomplete\n"));
        return PROCESS_RESULT_FAILED;
    }
    ArmA64Page* pages = arena_allocate(arena, ArmA64Page, 4096);
    u32 page_count = 0;
    if (!arm_a64_enumerate_pages(arena, options.source_directory, pages, 4096, &page_count))
    {
        string_print(S8("error: official Arm A64 source directory contains an invalid instructionsection page\n"));
        return PROCESS_RESULT_FAILED;
    }
    for (u32 index = 0; index < index_page_count; index += 1)
    {
        bool found = false;
        for (u32 page_index = 0; page_index < page_count; page_index += 1)
        {
            if (string_equal(index_pages[index].file, pages[page_index].file) && string_equal(index_pages[index].id, pages[page_index].id))
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            string_print(S8("error: index.xml iform is not present in the instructionsection inventory\n"));
            return PROCESS_RESULT_FAILED;
        }
    }
    ArmA64CanonicalRows rows = {.pointer = arena_allocate(arena, ArmA64CanonicalRow, 5000), .capacity = 5000};
    u32 iclass_count = 0;
    u32 regdiagram_count = 0;
    u32 alias_pages = 0;
    u32 instruction_pages = 0;
    u32 pseudocode_pages = 0;
    for (u32 page_index = 0; page_index < page_count; page_index += 1)
    {
        if (!pages[page_index].id.length || !pages[page_index].type.length) return PROCESS_RESULT_FAILED;
        u64 scratch_mark = scratch->position;
        String8 path = path_join(scratch, options.source_directory, pages[page_index].file);
        ByteSlice page_bytes = file_read(scratch, path, (FileReadOptions){.end_padding = 1});
        if (!page_bytes.pointer || !page_bytes.length) return PROCESS_RESULT_FAILED;
        if (!arm_a64_parse_page(arena, scratch, options.source_directory, pages[page_index], &rows, &iclass_count, &regdiagram_count, &alias_pages))
        {
            fprintf(stderr, "error: failed to parse Arm A64 page %.*s (%.*s)\n", (int)pages[page_index].file.length,
                    pages[page_index].file.pointer, (int)pages[page_index].id.length, pages[page_index].id.pointer);
            return PROCESS_RESULT_FAILED;
        }
        arena_set_position(scratch, scratch_mark);
        instruction_pages += string_equal(pages[page_index].type, S8("instruction"));
        pseudocode_pages += string_equal(pages[page_index].type, S8("pseudocode"));
    }
    if (page_count != 2292 || instruction_pages != 2121 || pseudocode_pages != 1 || alias_pages != 170 || iclass_count != 3502 || regdiagram_count != 3502 || rows.count != 4623)
    {
        string_print(S8("error: Arm A64 inventory mismatch pages={u32} iclasses={u32} regdiagrams={u32} encodings={u32}\n"), page_count, iclass_count,
                     regdiagram_count, rows.count);
        return PROCESS_RESULT_FAILED;
    }
    qsort(rows.pointer, rows.count, sizeof(rows.pointer[0]), arm_a64_row_compare);
    ArmA64LayoutResolutionStats layout_resolution = {0};
    if (!arm_a64_validate_layout_resolution(rows, &layout_resolution))
    {
        string_print(S8("error: Arm A64 profile-scoped layout resolution census or changed-mask digest mismatch\n"));
        return PROCESS_RESULT_FAILED;
    }
    if (!arm_a64_validate_rows(rows))
    {
        string_print(S8("error: Arm A64 canonical IDs/digests or alias target links are not unique\n"));
        return PROCESS_RESULT_FAILED;
    }
    if (!arm_a64_validate_symmetric_difference(rows))
    {
        string_print(S8("error: Arm A64 LLVM symmetric-difference membership gate failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    if (!arm_a64_link_alias_preferences(arena, rows) || !arm_a64_validate_rows(rows))
    {
        string_print(S8("error: Arm A64 alias preference links are not unique\n"));
        return PROCESS_RESULT_FAILED;
    }
    Arena* output_arena = arena;
    u64 artifact_mark = output_arena->position;
    for (u32 index = 0; index < rows.count; index += 1) arm_a64_append_row_json(output_arena, &rows.pointer[index]);
    String8 artifact = {.pointer = (char8*)arm_a64_arena_pointer(output_arena, artifact_mark), .length = output_arena->position - artifact_mark};
    u32 selected = 0, selected_canonical = 0, selected_alias = 0, selected_system = 0, selected_system_canonical = 0, selected_system_alias = 0;
    for (u32 index = 0; index < rows.count; index += 1)
    {
        ArmA64CanonicalRow* row = &rows.pointer[index];
        if (row->apple_m1)
        {
            selected += 1;
            selected_canonical += string_equal(row->kind, S8("canonical"));
            selected_alias += string_equal(row->kind, S8("alias"));
            selected_system += row->system;
            selected_system_canonical += row->system && string_equal(row->kind, S8("canonical"));
            selected_system_alias += row->system && string_equal(row->kind, S8("alias"));
        }
    }
    if (selected != 1695 || selected_canonical != 1523 || selected_alias != 172 || selected_system != 42 || selected_system_canonical != 33 ||
        selected_system_alias != 9 || selected_canonical - selected_system_canonical != 1490 || selected_alias - selected_system_alias != 163)
    {
        string_print(S8("error: Apple M1 Arm closure mismatch selected={u32} canonical={u32} alias={u32} system={u32} system-canonical={u32} system-alias={u32}\n"),
                     selected, selected_canonical, selected_alias, selected_system, selected_system_canonical, selected_system_alias);
        return PROCESS_RESULT_FAILED;
    }
    u64 fixed_header_mark = output_arena->position;
    if (!arm_a64_append_fixed_header(output_arena, rows))
    {
        string_print(S8("error: Arm A64 M1 fixed-spelling closure mismatch\n"));
        return PROCESS_RESULT_FAILED;
    }
    String8 fixed_header = (String8){.pointer = (char8*)arm_a64_arena_pointer(output_arena, fixed_header_mark), .length = output_arena->position - fixed_header_mark};
    ArmA64GprProjectionRow* gpr_rows = 0;
    u32 gpr_form_count = 0;
    if (!arm_a64_gpr_projection(arena, rows, &gpr_rows, &gpr_form_count))
    {
        string_print(S8("error: Arm A64 direct-GPR projection census or header emission mismatch\n"));
        return PROCESS_RESULT_FAILED;
    }
    u64 gpr_header_mark = output_arena->position;
    if (!arm_a64_append_gpr_header(output_arena, gpr_rows, gpr_form_count))
    {
        string_print(S8("error: Arm A64 direct-GPR projection header emission mismatch\n"));
        return PROCESS_RESULT_FAILED;
    }
    String8 gpr_header = (String8){.pointer = (char8*)arm_a64_arena_pointer(output_arena, gpr_header_mark), .length = output_arena->position - gpr_header_mark};
    u32 gpr_arity_counts[4] = {0};
    u32 gpr_feature_counts[4] = {0};
    u32 gpr_mnemonic_count = 0;
    for (u32 gpr_index = 0; gpr_index < gpr_form_count; gpr_index += 1)
    {
        ArmA64GprProjectionRow candidate = gpr_rows[gpr_index];
        if (candidate.operand_count >= 1 && candidate.operand_count <= 4) gpr_arity_counts[candidate.operand_count - 1] += 1;
        bool seen_mnemonic = false;
        for (u32 prior = 0; prior < gpr_index; prior += 1)
        {
            if (string_equal(gpr_rows[prior].mnemonic, candidate.mnemonic))
            {
                seen_mnemonic = true;
                break;
            }
        }
        if (!seen_mnemonic) gpr_mnemonic_count += 1;
        u32 feature_index = 3;
        if (!candidate.row->feature_expression.length) feature_index = 0;
        else if (string_equal(candidate.row->feature_expression, S8("FEAT_CRC32"))) feature_index = 1;
        else if (string_equal(candidate.row->feature_expression, S8("FEAT_FlagM"))) feature_index = 2;
        gpr_feature_counts[feature_index] += 1;
    }
    ArmA64ScalarProjectionRow* scalar_rows = 0;
    u32 scalar_form_count = 0;
    if (!arm_a64_scalar_projection(arena, rows, &scalar_rows, &scalar_form_count))
    {
        string_print(S8("error: Arm A64 scalar-integer projection census or identity gate failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    u64 scalar_header_mark = output_arena->position;
    if (!arm_a64_append_scalar_header(output_arena, scalar_rows, scalar_form_count))
    {
        string_print(S8("error: Arm A64 scalar-integer projection header emission mismatch\n"));
        return PROCESS_RESULT_FAILED;
    }
    String8 scalar_header = (String8){.pointer = (char8*)arm_a64_arena_pointer(output_arena, scalar_header_mark), .length = output_arena->position - scalar_header_mark};
    u32 scalar_arity_counts[4] = {0};
    u32 scalar_feature_counts[2] = {0};
    u32 scalar_recipe_counts[12] = {0};
    u32 scalar_mnemonic_count = 0;
    for (u32 scalar_index = 0; scalar_index < scalar_form_count; scalar_index += 1)
    {
        ArmA64ScalarProjectionRow candidate = scalar_rows[scalar_index];
        if (candidate.operand_count >= 1 && candidate.operand_count <= 4) scalar_arity_counts[candidate.operand_count - 1] += 1;
        scalar_feature_counts[string_equal(candidate.row->feature_expression, S8("FEAT_FlagM")) ? 1 : 0] += 1;
        scalar_recipe_counts[candidate.recipe] += 1;
        bool seen_mnemonic = false;
        for (u32 prior = 0; prior < scalar_index; prior += 1)
        {
            if (string_equal(scalar_rows[prior].mnemonic, candidate.mnemonic))
            {
                seen_mnemonic = true;
                break;
            }
        }
        if (!seen_mnemonic) scalar_mnemonic_count += 1;
    }
    String8 decoder_header = {0};
    String8 decoder_audit = {0};
    if (!arm_a64_copy_decoder_snapshot(scratch, output_arena, "aarch64-canonical-decoder.generated.h", &decoder_header) ||
        !arm_a64_copy_decoder_snapshot(scratch, output_arena, "aarch64-canonical-decoder-audit.json", &decoder_audit))
    {
        string_print(S8("error: canonical decoder snapshot is missing; run tools/gen_aarch64_canonical_decoder.py first\n"));
        return PROCESS_RESULT_FAILED;
    }
    if (!arm_a64_validate_decoder_snapshot(artifact, decoder_header, decoder_audit, rows))
    {
        string_print(S8("error: canonical decoder snapshot identity or invariant gate failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    u64 manifest_mark = output_arena->position;
    arm_a64_append_manifest(output_arena, source_tree_digest, source_tree_sha256, source_file_count,
                            buster_hash_64((u8*)artifact.pointer, artifact.length), artifact.length,
                            buster_hash_64((u8*)fixed_header.pointer, fixed_header.length), fixed_header.length,
                            buster_hash_64((u8*)gpr_header.pointer, gpr_header.length), gpr_header.length, gpr_form_count, gpr_mnemonic_count,
                            gpr_arity_counts, gpr_feature_counts,
                            buster_hash_64((u8*)scalar_header.pointer, scalar_header.length), scalar_header.length, scalar_form_count, scalar_mnemonic_count,
                            scalar_arity_counts, scalar_feature_counts, scalar_recipe_counts,
                            "4429d9ab064a8e98561c794e8c5408a3922bc6a7f07a32015caaa9932ba2c484",
                            buster_hash_64((u8*)decoder_header.pointer, decoder_header.length), decoder_header.length,
                            buster_hash_64((u8*)decoder_audit.pointer, decoder_audit.length), decoder_audit.length,
                            layout_resolution, rows, page_count,
                            instruction_pages, alias_pages, pseudocode_pages, iclass_count, regdiagram_count, selected, selected_canonical,
                            selected_alias, selected_system, selected_system_canonical, selected_system_alias, selected - selected_system,
                            selected_canonical - selected_system_canonical, selected_alias - selected_system_alias);
    String8 manifest = {.pointer = (char8*)arm_a64_arena_pointer(output_arena, manifest_mark), .length = output_arena->position - manifest_mark};
    String8 artifact_path = path_join(arena, options.output_directory, S8("arm-a64-canonical.generated.jsonl"));
    String8 manifest_path = path_join(arena, options.output_directory, S8("arm-a64-canonical-manifest.json"));
    String8 fixed_header_path = path_join(arena, options.output_directory, S8("arm-a64-m1-fixed.generated.h"));
    String8 gpr_header_path = path_join(arena, options.output_directory, S8("arm-a64-m1-gpr.generated.h"));
    String8 scalar_header_path = path_join(arena, options.output_directory, S8("arm-a64-m1-scalar-integer.generated.h"));
    String8 decoder_header_path = path_join(arena, options.output_directory, S8("aarch64-canonical-decoder.generated.h"));
    String8 decoder_audit_path = path_join(arena, options.output_directory, S8("aarch64-canonical-decoder-audit.json"));
    if (arm_a64_check_mode_enabled())
    {
        if (!arm_a64_check_existing_outputs(arena, options.output_directory, artifact, manifest, fixed_header, gpr_header, scalar_header, decoder_header, decoder_audit)) return PROCESS_RESULT_FAILED;
        string_print(S8("Arm A64 checked-in canonical artifacts, M1 fixed spellings, direct-GPR/scalar-integer forms, canonical decoder snapshot, and README.md canonical section match deterministic import output: {S8}\n"),
                     options.output_directory);
        return PROCESS_RESULT_SUCCESS;
    }
    make_directory_recursive(arena, options.output_directory);
    if (!file_write(artifact_path, BUSTER_SLICE_TO_BYTE_SLICE(artifact)) || !file_write(manifest_path, BUSTER_SLICE_TO_BYTE_SLICE(manifest)) ||
        !file_write(fixed_header_path, BUSTER_SLICE_TO_BYTE_SLICE(fixed_header)) || !file_write(gpr_header_path, BUSTER_SLICE_TO_BYTE_SLICE(gpr_header)) ||
        !file_write(scalar_header_path, BUSTER_SLICE_TO_BYTE_SLICE(scalar_header)) ||
        !file_write(decoder_header_path, BUSTER_SLICE_TO_BYTE_SLICE(decoder_header)) ||
        !file_write(decoder_audit_path, BUSTER_SLICE_TO_BYTE_SLICE(decoder_audit)))
    {
        string_print(S8("error: failed to write Arm A64 canonical artifacts\n"));
        return PROCESS_RESULT_FAILED;
    }
    string_print(S8("Imported official Arm A64 canonical inventory: pages={u32} iclasses={u32} encodings={u32} M1={u32} -> {S8}\n"), page_count,
                 iclass_count, rows.count, selected, options.output_directory);
    return PROCESS_RESULT_SUCCESS;
}

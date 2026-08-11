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

typedef struct ArmA64Layout ArmA64Layout;
struct ArmA64Layout
{
    ArmA64Bit bits[32];
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
    u32 field_count;
    ArmA64Field fields[32];
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
    for (u32 index = 0; index < tag.attr_count; index += 1)
    {
        if (string_equal(tag.attrs[index].name, wanted)) return tag.attrs[index].value;
    }
    return (String8){0};
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

/* Read just enough of a source document to decide whether it is one of the
   release's instructionsection pages.  The release directory also contains
   indexes, stylesheets, and other XML support files; only pages with this
   root element belong in the canonical inventory. */
static bool arm_a64_page_metadata(Arena* arena, Arena* scratch, String8 path, ArmA64Page* result)
{
    ByteSlice bytes = file_read(scratch, path, (FileReadOptions){.end_padding = 1});
    if (!bytes.pointer || !bytes.length) return false;
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
        if (!overlay || layout->bits[bit].kind != ARM_A64_BIT_FIXED)
        {
            layout->bits[bit].kind = field.length ? ARM_A64_BIT_FIELD : ARM_A64_BIT_UNRESOLVED;
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
        layout->bits[bit].field = (String8){0};
    }
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
        if (!raw.length)
        {
            for (u32 offset = 0; offset < colspan; offset += 1) arm_a64_layout_apply_value(layout, bit_cursor - offset, (String8){0}, field, overlay);
        }
        else if (raw.length == 3 && raw.pointer[0] == '(' && (raw.pointer[1] == '0' || raw.pointer[1] == '1') && raw.pointer[2] == ')')
        {
            String8 fixed = string_slice(raw, 1, 2);
            for (u32 offset = 0; offset < colspan; offset += 1) arm_a64_layout_apply_value(layout, bit_cursor - offset, fixed, field, overlay);
        }
        else if (raw.length == 1 || string_starts_with_sequence(raw, S8("!=")))
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
    return consumed == width;
}

static bool arm_a64_layout_regdiagram(String8 text, u64 start, u64 end, ArmA64Layout* layout, bool overlay)
{
    ArmA64XmlCursor cursor = {.text = text, .position = start};
    ArmA64XmlTag tag = {0};
    while (arm_a64_xml_next(&cursor, &tag))
    {
        if (tag.start >= end) break;
        if (tag.kind == ARM_A64_XML_TAG_OPEN && arm_a64_tag_name_is(tag, "box"))
        {
            if (!arm_a64_layout_box(0, text, tag, layout, overlay)) return false;
        }
    }
    return true;
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

static bool arm_a64_feature_allowed(String8 name)
{
    static String8 allowed[] = {
        S8_INITIALIZER("FEAT_AES"), S8_INITIALIZER("FEAT_AdvSIMD"), S8_INITIALIZER("FEAT_CRC32"), S8_INITIALIZER("FEAT_DotProd"),
        S8_INITIALIZER("FEAT_FCMA"), S8_INITIALIZER("FEAT_FHM"), S8_INITIALIZER("FEAT_FP"), S8_INITIALIZER("FEAT_FP16"),
        S8_INITIALIZER("FEAT_FRINTTS"), S8_INITIALIZER("FEAT_FlagM"), S8_INITIALIZER("FEAT_FlagM2"), S8_INITIALIZER("FEAT_JSCVT"),
        S8_INITIALIZER("FEAT_LOR"), S8_INITIALIZER("FEAT_LSE"), S8_INITIALIZER("FEAT_LRCPC"), S8_INITIALIZER("FEAT_LRCPC2"),
        S8_INITIALIZER("FEAT_PAuth"), S8_INITIALIZER("FEAT_RAS"), S8_INITIALIZER("FEAT_RDM"), S8_INITIALIZER("FEAT_SB"),
        S8_INITIALIZER("FEAT_SHA1"), S8_INITIALIZER("FEAT_SHA256"), S8_INITIALIZER("FEAT_SHA3"), S8_INITIALIZER("FEAT_SHA512"),
        S8_INITIALIZER("FEAT_SPECRES"), S8_INITIALIZER("FEAT_TRF"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(allowed); index += 1) if (string_equal(name, allowed[index])) return true;
    return false;
}

typedef struct ArmA64FeatureParser ArmA64FeatureParser;
struct ArmA64FeatureParser
{
    String8 text;
    u64 position;
    bool valid;
};

static void arm_a64_feature_parser_skip_space(ArmA64FeatureParser* parser)
{
    while (parser->position < parser->text.length && character_is_space(parser->text.pointer[parser->position])) parser->position += 1;
}

static bool arm_a64_feature_parse_or(ArmA64FeatureParser* parser, bool* result);

static bool arm_a64_feature_parse_primary(ArmA64FeatureParser* parser, bool* result)
{
    arm_a64_feature_parser_skip_space(parser);
    if (parser->position >= parser->text.length) { parser->valid = false; return false; }
    char8 c = parser->text.pointer[parser->position];
    if (c == '!')
    {
        parser->position += 1;
        bool value = false;
        if (!arm_a64_feature_parse_primary(parser, &value)) return false;
        *result = !value;
        return true;
    }
    if (c == '(')
    {
        parser->position += 1;
        if (!arm_a64_feature_parse_or(parser, result)) return false;
        arm_a64_feature_parser_skip_space(parser);
        if (parser->position >= parser->text.length || parser->text.pointer[parser->position] != ')') { parser->valid = false; return false; }
        parser->position += 1;
        return true;
    }
    u64 start = parser->position;
    while (parser->position < parser->text.length && ((parser->text.pointer[parser->position] >= 'A' && parser->text.pointer[parser->position] <= 'Z') ||
                                                      (parser->text.pointer[parser->position] >= 'a' && parser->text.pointer[parser->position] <= 'z') ||
                                                      (parser->text.pointer[parser->position] >= '0' && parser->text.pointer[parser->position] <= '9') ||
                                                      parser->text.pointer[parser->position] == '_'))
        parser->position += 1;
    if (start == parser->position) { parser->valid = false; return false; }
    String8 name = string_slice(parser->text, start, parser->position);
    bool value = string_starts_with_sequence(name, S8("FEAT_")) ? arm_a64_feature_allowed(name) : false;
    arm_a64_feature_parser_skip_space(parser);
    /* Parameterized predicates such as sz == '0' are consumed but evaluate
       false because the M1 closure has no symbolic encoding-variable values. */
    if (parser->position + 1 < parser->text.length &&
        ((parser->text.pointer[parser->position] == '=' && parser->text.pointer[parser->position + 1] == '=') ||
         (parser->text.pointer[parser->position] == '!' && parser->text.pointer[parser->position + 1] == '=') ||
         (parser->text.pointer[parser->position] == '<' && parser->text.pointer[parser->position + 1] == '=') ||
         (parser->text.pointer[parser->position] == '>' && parser->text.pointer[parser->position + 1] == '=')))
    {
        parser->position += 2;
        arm_a64_feature_parser_skip_space(parser);
        if (parser->position < parser->text.length && (parser->text.pointer[parser->position] == '\'' || parser->text.pointer[parser->position] == '"'))
        {
            char8 quote = parser->text.pointer[parser->position++];
            while (parser->position < parser->text.length && parser->text.pointer[parser->position] != quote) parser->position += 1;
            if (parser->position < parser->text.length) parser->position += 1;
        }
        else
        {
            while (parser->position < parser->text.length && parser->text.pointer[parser->position] != ')' &&
                   parser->text.pointer[parser->position] != '&' && parser->text.pointer[parser->position] != '|')
                parser->position += 1;
        }
        value = false;
    }
    *result = value;
    return true;
}

static bool arm_a64_feature_parse_and(ArmA64FeatureParser* parser, bool* result)
{
    if (!arm_a64_feature_parse_primary(parser, result)) return false;
    for (;;)
    {
        arm_a64_feature_parser_skip_space(parser);
        if (parser->position + 1 >= parser->text.length || parser->text.pointer[parser->position] != '&' ||
            parser->text.pointer[parser->position + 1] != '&')
            break;
        parser->position += 2;
        bool rhs = false;
        if (!arm_a64_feature_parse_primary(parser, &rhs)) return false;
        *result = *result && rhs;
    }
    return true;
}

static bool arm_a64_feature_parse_or(ArmA64FeatureParser* parser, bool* result)
{
    if (!arm_a64_feature_parse_and(parser, result)) return false;
    for (;;)
    {
        arm_a64_feature_parser_skip_space(parser);
        if (parser->position + 1 >= parser->text.length || parser->text.pointer[parser->position] != '|' ||
            parser->text.pointer[parser->position + 1] != '|')
            break;
        parser->position += 2;
        bool rhs = false;
        if (!arm_a64_feature_parse_and(parser, &rhs)) return false;
        *result = *result || rhs;
    }
    return true;
}

static bool arm_a64_feature_expression_allowed(String8 expression)
{
    if (!expression.length) return true;
    ArmA64FeatureParser parser = {.text = expression, .valid = true};
    bool result = false;
    if (!arm_a64_feature_parse_or(&parser, &result)) return false;
    arm_a64_feature_parser_skip_space(&parser);
    return parser.valid && parser.position == parser.text.length && result;
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
        else if (value.kind == ARM_A64_BIT_FIELD && value.field.length)
        {
            row->field_mask |= UINT32_C(1) << bit;
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
                field->bits[field->bit_count++] = (u8)bit;
            }
        }
        else
        {
            row->unresolved_mask |= UINT32_C(1) << bit;
            row->explicit_unresolved_mask |= UINT32_C(1) << bit;
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
}

static int arm_a64_row_compare(const void* left_pointer, const void* right_pointer)
{
    const ArmA64CanonicalRow* left = left_pointer;
    const ArmA64CanonicalRow* right = right_pointer;
    u64 count = BUSTER_MIN(left->canonical_id.length, right->canonical_id.length);
    int result = count ? memcmp(left->canonical_id.pointer, right->canonical_id.pointer, count) : 0;
    return result ? result : (left->canonical_id.length > right->canonical_id.length) - (left->canonical_id.length < right->canonical_id.length);
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
    row->digest = buster_hash_64(arm_a64_arena_pointer(arena, mark), arena->position - mark);
    arena_set_position(arena, mark);
}

static bool arm_a64_append_row_json(Arena* output, ArmA64CanonicalRow* row)
{
    arena_append_string8(output, S8("{\"id\":")); arm_a64_json_string(output, row->canonical_id);
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
    arena_append_string8(output, S8("}"));
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
                    !arm_a64_layout_regdiagram(text, child.end, diagram_end, &base_layout, false))
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
            row->iclass_name = string_duplicate_arena(arena, class_instr, true);
            row->kind = string_equal(page_type, S8("alias")) ? S8("alias") : S8("canonical");
            row->alias_to_file = string_duplicate_arena(arena, alias_to_file, true);
            row->alias_to_id = string_duplicate_arena(arena, alias_to_id, true);
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
            arm_a64_collect_fields(arena, row, layout);
            if ((row->fixed_mask | row->field_mask | row->unresolved_mask) != UINT32_MAX) return false;
            row->apple_m1 = arm_a64_feature_expression_allowed(row->feature_expression);
            arm_a64_row_digest(arena, row);
        }
    }
    return true;
}

static bool arm_a64_append_manifest(Arena* output, u64 source_hash, u64 artifact_hash, u64 artifact_bytes,
                                    ArmA64CanonicalRows rows, u32 page_count, u32 instruction_pages, u32 alias_pages,
                                    u32 iclass_count, u32 regdiagram_count, u32 selected, u32 selected_canonical,
                                    u32 selected_alias, u32 selected_system, u32 selected_system_canonical, u32 selected_system_alias,
                                    u32 selected_non_system, u32 selected_non_system_canonical, u32 selected_non_system_alias)
{
    arena_append_string8(output, S8("{\n  \"schema_version\": 1,\n  \"status\": \"provisional-canonical-foundation\",\n  \"acceptance\": \"blocked\",\n"));
    arena_append_string8(output,
                         S8("  \"source\": {\"release\": \"2026-06\", \"source_url\": \"https://developer.arm.com/-/cdn-downloads/permalink/Exploration-Tools-A64-ISA/ISA_A64/ISA_A64_xml_A_profile-2026-06.tar.gz\", \"archive_sha256\": \"63a01a1696483bbe2edfef9e0f0cd053d6c1c619ec0587876cb7a60bb344f354\", \"raw_xml_policy\": \"non-vendored-license-restricted\"},\n"));
    arena_append_string8(output, S8("  \"source_hash\": ")); arm_a64_json_hex(output, source_hash); arena_append_string8(output, S8(",\n"));
    arena_append_string8(output, S8("  \"inventory\": {\"pages\": ")); arm_a64_append_u64(output, page_count);
    arena_append_string8(output, S8(", \"instruction_pages\": ")); arm_a64_append_u64(output, instruction_pages);
    arena_append_string8(output, S8(", \"alias_pages\": ")); arm_a64_append_u64(output, alias_pages);
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
    arena_append_string8(output, S8("  \"artifact\": {\"file\": \"arm-a64-canonical.generated.jsonl\", \"bytes\": ")); arm_a64_append_u64(output, artifact_bytes);
    arena_append_string8(output, S8(", \"xxh64\": ")); arm_a64_json_hex(output, artifact_hash); arena_append_string8(output, S8("},\n"));
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

static ProcessResult arm_a64_canonical_import_run(Arena* arena, ArmA64CanonicalImportOptions options)
{
    if (!options.source_directory.length || !options.output_directory.length) return PROCESS_RESULT_FAILED;
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
    u64 source_hash = 1469598103934665603ULL;
    for (u32 page_index = 0; page_index < page_count; page_index += 1)
    {
        if (!pages[page_index].id.length || !pages[page_index].type.length) return PROCESS_RESULT_FAILED;
        u64 scratch_mark = scratch->position;
        String8 path = path_join(scratch, options.source_directory, pages[page_index].file);
        ByteSlice page_bytes = file_read(scratch, path, (FileReadOptions){.end_padding = 1});
        if (!page_bytes.pointer || !page_bytes.length) return PROCESS_RESULT_FAILED;
        source_hash = buster_hash_64(page_bytes.pointer, page_bytes.length) ^ (source_hash * UINT64_C(1099511628211));
        if (!arm_a64_parse_page(arena, scratch, options.source_directory, pages[page_index], &rows, &iclass_count, &regdiagram_count, &alias_pages))
        {
            fprintf(stderr, "error: failed to parse Arm A64 page %.*s (%.*s)\n", (int)pages[page_index].file.length,
                    pages[page_index].file.pointer, (int)pages[page_index].id.length, pages[page_index].id.pointer);
            return PROCESS_RESULT_FAILED;
        }
        arena_set_position(scratch, scratch_mark);
        instruction_pages += !string_equal(pages[page_index].type, S8("alias"));
    }
    if (page_count != 2292 || instruction_pages != 2122 || alias_pages != 170 || iclass_count != 3502 || regdiagram_count != 3502 || rows.count != 4623)
    {
        string_print(S8("error: Arm A64 inventory mismatch pages={u32} iclasses={u32} regdiagrams={u32} encodings={u32}\n"), page_count, iclass_count,
                     regdiagram_count, rows.count);
        return PROCESS_RESULT_FAILED;
    }
    qsort(rows.pointer, rows.count, sizeof(rows.pointer[0]), arm_a64_row_compare);
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
    String8 manifest = {0};
    u64 manifest_mark = output_arena->position;
    arm_a64_append_manifest(output_arena, source_hash, buster_hash_64((u8*)artifact.pointer, artifact.length), artifact.length,
                            rows, page_count, instruction_pages, alias_pages, iclass_count, regdiagram_count, selected, selected_canonical,
                            selected_alias, selected_system, selected_system_canonical, selected_system_alias, selected - selected_system,
                            selected_canonical - selected_system_canonical, selected_alias - selected_system_alias);
    manifest = (String8){.pointer = (char8*)arm_a64_arena_pointer(output_arena, manifest_mark), .length = output_arena->position - manifest_mark};
    make_directory_recursive(arena, options.output_directory);
    String8 artifact_path = path_join(arena, options.output_directory, S8("arm-a64-canonical.generated.jsonl"));
    String8 manifest_path = path_join(arena, options.output_directory, S8("arm-a64-canonical-manifest.json"));
    if (!file_write(artifact_path, BUSTER_SLICE_TO_BYTE_SLICE(artifact)) || !file_write(manifest_path, BUSTER_SLICE_TO_BYTE_SLICE(manifest)))
    {
        string_print(S8("error: failed to write Arm A64 canonical artifacts\n"));
        return PROCESS_RESULT_FAILED;
    }
    string_print(S8("Imported official Arm A64 canonical inventory: pages={u32} iclasses={u32} encodings={u32} M1={u32} -> {S8}\n"), page_count,
                 iclass_count, rows.count, selected, options.output_directory);
    return PROCESS_RESULT_SUCCESS;
}

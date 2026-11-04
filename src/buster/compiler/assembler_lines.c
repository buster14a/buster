// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// GNU as's line table (#1115): what GCC's and Clang's -g output leaves to the assembler. `.file N` fills the file and
// directory tables and `.loc` makes rows, which give .debug_line its DWARF line program after layout, byte for byte as
// GNU as's dwarf2dbg writes it (binutils 2.42 and 2.47 alike for compiler output), with its strings in
// .debug_line_str; and GCC's view numbers (`.loc ... view .LVU3`, then `.uleb128 .LVU3`), which GNU as assigns. The
// rules follow GNU as's behavior, read from its source and checked against its objects; no code of it is here.

#define ASM_LINE_IS_STMT 1
#define ASM_LINE_BASIC_BLOCK 2
#define ASM_LINE_PROLOGUE_END 4
#define ASM_LINE_EPILOGUE_BEGIN 8

// What a `.loc`'s `view` says: nothing, a symbol that takes the row's view number, `view 0` (which asserts it), or
// `view -0` (which resets it to 0).
#define ASM_VIEW_NONE 0
#define ASM_VIEW_SYMBOL 1
#define ASM_VIEW_ZERO 2
#define ASM_VIEW_RESET 3

static bool asm_line_tables(AsmParser* parser, uint32_t location)
{
    if (parser->line_files == 0)
    {
        parser->line_files = BUSTER_ARENA_PUSH_ARRAY(parser->arena, AsmLineFile, parser->instruction_capacity + 1);
        parser->line_directories = BUSTER_ARENA_PUSH_ARRAY(parser->arena, String8, 2 * parser->instruction_capacity + 2);
        parser->line_rows = BUSTER_ARENA_PUSH_ARRAY(parser->arena, AsmLineRow, parser->instruction_capacity);
        if (parser->line_files == 0 || parser->line_directories == 0 || parser->line_rows == 0)
        {
            asm_fail(parser, location, "arena exhausted allocating line tables");
        }
        else
        {
            parser->line = (AsmLineState){ .file = 1, .line = 1, .flags = ASM_LINE_IS_STMT };
        }
    }
    return parser->result.error == 0;
}

// GNU as's get_basename: the name after the last '/', but the whole of "/name".
static uint64_t asm_line_basename(String8 path)
{
    uint64_t result = 0;
    for (uint64_t i = 0; i < path.length; i += 1)
    {
        result = path.pointer[i] == '/' ? i + 1 : result;
    }
    return result == 1 ? 0 : result;
}

static bool asm_line_same(String8 a, String8 b)
{
    return a.length == b.length && memcmp(a.pointer, b.pointer, a.length) == 0;
}

// A directory's index in the table, or the table's count where it isn't there; its name without a trailing '/'.
static uint32_t asm_line_find_directory(const AsmParser* parser, String8* name)
{
    name->length = name->length != 0 && name->pointer[name->length - 1] == '/' ? name->length - 1 : name->length;
    uint32_t result = parser->line_directory_count;
    for (uint32_t d = 0; d < parser->line_directory_count && result == parser->line_directory_count; d += 1)
    {
        result = parser->line_directories[d].pointer != 0 && asm_line_same(parser->line_directories[d], *name) ? d : result;
    }
    return result;
}

// GNU as's get_directory_table_entry: the directory's index, which the table takes if it's new. Only `.file 0` may
// take index 0, whose directory DWARF 5 makes the compilation's: another directory first puts that one there.
static uint32_t asm_line_directory(AsmParser* parser, String8 name, String8 directory_zero, uint64_t length, bool zero)
{
    String8 wanted = { .pointer = name.pointer, .length = length };
    uint32_t result = asm_line_find_directory(parser, &wanted);
    if (wanted.length == 0)
    {
        result = 0;
    }
    else if (result == parser->line_directory_count)
    {
        if (zero && (parser->line_directory_count == 0 || parser->line_directories[0].pointer == 0))
        {
            // GNU as compares the whole of the name it was given, a file's path, so `.file 0 "/d" "/d/x.c"` gives "/d"
            // twice. The working directory is `.file 0`'s directory, which a compiler's always names; where the table
            // has it already, it stays where it is.
            if (parser->line_dwarf5 && !asm_line_same(name, directory_zero))
            {
                String8 compilation = directory_zero;
                if (asm_line_find_directory(parser, &compilation) == parser->line_directory_count && compilation.length != 0)
                {
                    parser->line_directories[0] = compilation;
                    parser->line_directory_count = parser->line_directory_count > 1 ? parser->line_directory_count : 1;
                }
                result = parser->line_directory_count;
            }
            else
            {
                result = 0;
            }
        }
        else if (!zero && result == 0)
        {
            result = 1;
        }
        for (uint32_t d = parser->line_directory_count; d < result; d += 1)
        {
            parser->line_directories[d] = (String8){ 0 };
        }
        parser->line_directories[result] = wanted;
        parser->line_directory_count = result + 1 > parser->line_directory_count ? result + 1 : parser->line_directory_count;
    }
    return result;
}

// GNU as's allocate_filename_to_slot: `.file N ["directory"] "name" [md5 0x...]`. A slot taken again must name the
// same file, and may fill in its directory.
static void asm_line_slot(AsmParser* parser, uint32_t location, uint64_t number, String8 directory, String8 name, const uint8_t* md5)
{
    if (number < parser->line_file_count && parser->line_files[number].assigned)
    {
        AsmLineFile* file = &parser->line_files[number];
        const String8 old = parser->line_directories != 0 && file->directory < parser->line_directory_count ? parser->line_directories[file->directory] : (String8){ 0 };
        bool same = md5 == 0 || memcmp(md5, file->md5, 16) == 0;
        if (same && directory.pointer != 0)
        {
            same = (old.pointer == 0 || asm_line_same(old, directory)) && asm_line_same(name, file->name);
            if (same && old.pointer == 0)
            {
                parser->line_directories[file->directory] = directory;
            }
        }
        else if (same && old.pointer != 0)
        {
            same = name.length == old.length + 1 + file->name.length && memcmp(name.pointer, old.pointer, old.length) == 0 && name.pointer[old.length] == '/' &&
                   memcmp(name.pointer + old.length + 1, file->name.pointer, file->name.length) == 0;
        }
        else if (same)
        {
            const uint64_t base = asm_line_basename(name);
            same = asm_line_same((String8){ .pointer = name.pointer + base, .length = name.length - base }, file->name);
            if (same && base != 0)
            {
                parser->line_directories[file->directory] = (String8){ .pointer = name.pointer, .length = base - 1 };
            }
        }
        if (!same)
        {
            asm_fail(parser, location, "file table slot is already occupied by a different file");
        }
    }
    else if (number >= parser->instruction_capacity)
    {
        asm_fail(parser, location, "file number is too big");
    }
    else
    {
        // `.file 0`'s directory is the compilation's, and its name may be in another directory. Another file's
        // directory is the one given, or its name's.
        String8 place = name;
        uint64_t length = 0;
        uint64_t base = asm_line_basename(name);
        const String8 zero = number == 0 ? directory : (String8){ 0 };
        if (number == 0 && directory.pointer != 0 && base == 0)
        {
            place = directory;
            length = directory.length;
        }
        else if (number != 0 && directory.pointer != 0)
        {
            place = directory;
            length = directory.length;
            base = 0;
        }
        else
        {
            length = base;
        }
        if (zero.pointer == 0 && number == 0 && length != 0)
        {
            asm_fail(parser, location, "`.file 0` without its directory, which GNU as takes as its working directory");
        }
        const uint32_t d = parser->result.error == 0 ? asm_line_directory(parser, place, zero, length, number == 0) : 0;
        for (uint64_t i = parser->line_file_count; i < number; i += 1)
        {
            parser->line_files[i] = (AsmLineFile){ 0 };
        }
        parser->line_files[number] = (AsmLineFile){ .name = { .pointer = name.pointer + base, .length = name.length - base }, .directory = d, .assigned = true };
        if (md5 != 0)
        {
            memcpy(parser->line_files[number].md5, md5, 16);
        }
        parser->line_file_count = number + 1 > parser->line_file_count ? (uint32_t)number + 1 : parser->line_file_count;
    }
}

// An MD5 as GNU as reads one: a number past 64 bits, kept little-endian, of which a hexadecimal one is the compilers'.
static bool asm_line_md5(String8 text, uint8_t* md5)
{
    bool valid = text.length > 2 && text.pointer[0] == '0' && (text.pointer[1] == 'x' || text.pointer[1] == 'X');
    uint32_t significant = 0;
    memset(md5, 0, 16);
    for (uint64_t i = text.length; valid && i > 2; i -= 1)
    {
        const char c = text.pointer[i - 1];
        const uint32_t digit = c >= '0' && c <= '9' ? (uint32_t)(c - '0') : c >= 'a' && c <= 'f' ? (uint32_t)(c - 'a' + 10) : c >= 'A' && c <= 'F' ? (uint32_t)(c - 'A' + 10) : 16;
        const uint64_t position = text.length - i;
        valid = digit < 16;
        if (valid && position < 32)
        {
            md5[position / 2] |= (uint8_t)(digit << (position % 2 * 4));
        }
        significant = valid && digit != 0 ? (uint32_t)position + 1 : significant;
    }
    return valid && significant > 16;
}

static void asm_line_file(AsmParser* parser, uint32_t location, uint32_t end)
{
    const __int128 number = asm_absolute(parser, end);
    String8 first = { 0 };
    String8 second = { 0 };
    uint8_t md5[16];
    bool hashed = false;
    if (parser->result.error == 0 && asm_line_tables(parser, location))
    {
        parser->line_dwarf5 |= number == 0;
        if (number < 0)
        {
            asm_fail(parser, location, "file number less than one");
        }
        else if (parser->at >= end || parser->tokens[parser->at].kind != BUSTER_ASM_STRING)
        {
            asm_fail(parser, location, "expected a quoted file name");
        }
        else
        {
            first = asm_name_text(parser, parser->at++);
            if (parser->at < end && parser->tokens[parser->at].kind == BUSTER_ASM_STRING)
            {
                second = asm_name_text(parser, parser->at++);
            }
            if (parser->at < end && asm_is(parser, parser->at, "md5"))
            {
                parser->at += 1;
                hashed = parser->at < end && asm_line_md5(asm_token_text(parser, parser->at), md5);
                parser->at += 1;
                if (!hashed)
                {
                    asm_fail(parser, location, "md5 value too small or not a constant");
                }
            }
            if (parser->at < end)
            {
                asm_fail(parser, parser->tokens[parser->at].start, "junk at end of .file");
            }
        }
    }
    if (parser->result.error == 0)
    {
        const bool directory = second.pointer != 0;
        asm_line_slot(parser, location, (uint64_t)number, directory ? first : (String8){ 0 }, directory ? second : first, hashed ? md5 : 0);
    }
}

// GNU as's dwarf2_emit_insn: the latest `.loc`'s row, at the current place. A row of line 0 is none (clang's `.loc 0 0
// 5`), nor one outside a loaded code section; either way the `.loc` is used.
static void asm_line_row(AsmParser* parser, uint32_t location)
{
    AsmLineState* state = &parser->line;
    const AsmSection* section = &parser->sections[parser->section];
    const bool code = (section->object.flags & (SHF_ALLOC | SHF_EXECINSTR)) == (SHF_ALLOC | SHF_EXECINSTR) && section->object.type == SHT_PROGBITS;
    if (state->line != 0 && code && parser->line_row_count < parser->instruction_capacity)
    {
        const uint32_t row = parser->line_row_count++;
        const uint32_t previous = parser->sections[parser->section].line_last;
        parser->line_rows[row] = (AsmLineRow){
            .fragment = asm_fragment(parser, ASM_LABEL, location),
            .previous = previous,
            .file = state->file,
            .line = state->line,
            .column = state->column,
            .isa = state->isa,
            .discriminator = state->discriminator,
            .flags = state->flags,
            .view_kind = state->view_kind,
        };
        if (previous != 0)
        {
            parser->line_rows[previous - 1].next = row + 1;
        }
        parser->sections[parser->section].line_last = row + 1;
        if (state->view_symbol != 0)
        {
            AsmSymbol* symbol = &parser->symbols[state->view_symbol];
            symbol->view = row + 1;
            symbol->defined = true;
            symbol->section_start = false;
            symbol->fragment = parser->line_rows[row].fragment;
        }
    }
    else if (state->line != 0 && code)
    {
        asm_fail(parser, location, "line row capacity exhausted");
    }
    state->pending = false;
    state->flags &= ~(ASM_LINE_BASIC_BLOCK | ASM_LINE_PROLOGUE_END | ASM_LINE_EPILOGUE_BEGIN);
    state->discriminator = 0;
    state->view_symbol = 0;
    state->view_kind = ASM_VIEW_NONE;
}

// An instruction, or a data16 or rex64 statement, which GNU as reads as one: the row a `.loc` waits for.
static void asm_line_instruction(AsmParser* parser, uint32_t location)
{
    if (parser->line.pending)
    {
        asm_line_row(parser, location);
    }
}

// `.loc file line [column] [basic_block] [prologue_end] [epilogue_begin] [is_stmt 0|1] [isa N] [discriminator N]
// [view V]`, as GNU as's dwarf2_directive_loc reads it: a column, is_stmt and isa last until another `.loc` changes
// them. A row waits for the next instruction, or the next `.loc`, but with a view it is made at once.
static void asm_line_loc(AsmParser* parser, uint32_t location, uint32_t end)
{
    if (asm_line_tables(parser, location) && parser->line.pending)
    {
        asm_line_row(parser, location);
    }
    const __int128 file = parser->result.error == 0 ? asm_absolute(parser, end) : 0;
    const __int128 line = parser->result.error == 0 ? asm_absolute(parser, end) : 0;
    AsmLineState* state = &parser->line;
    if (parser->result.error == 0)
    {
        parser->line_dwarf5 |= file == 0;
        if (file < 0 || file >= parser->line_file_count || !parser->line_files[file].assigned)
        {
            asm_fail(parser, location, "unassigned file number");
        }
        else if (line < 0 || line > UINT32_MAX)
        {
            asm_fail(parser, location, "line number out of range");
        }
        else
        {
            state->file = (uint32_t)file;
            state->line = (uint32_t)line;
            state->discriminator = 0;
        }
    }
    if (parser->result.error == 0 && parser->at < end && parser->tokens[parser->at].kind == BUSTER_ASM_WORD && asm_token_text(parser, parser->at).pointer[0] >= '0' &&
        asm_token_text(parser, parser->at).pointer[0] <= '9')
    {
        const __int128 column = asm_absolute(parser, end);
        state->column = column >= 0 && column <= UINT32_MAX ? (uint32_t)column : state->column;
        if (column < 0 || column > UINT32_MAX)
        {
            asm_fail(parser, location, "column number out of range");
        }
    }
    while (parser->result.error == 0 && parser->at < end && parser->tokens[parser->at].kind == BUSTER_ASM_WORD)
    {
        const uint32_t option = parser->at++;
        const uint32_t at = parser->tokens[option].start;
        if (asm_is(parser, option, "basic_block") || asm_is(parser, option, "prologue_end") || asm_is(parser, option, "epilogue_begin"))
        {
            state->flags |= asm_is(parser, option, "basic_block") ? ASM_LINE_BASIC_BLOCK : asm_is(parser, option, "prologue_end") ? ASM_LINE_PROLOGUE_END : ASM_LINE_EPILOGUE_BEGIN;
        }
        else if (asm_is(parser, option, "is_stmt") || asm_is(parser, option, "isa") || asm_is(parser, option, "discriminator"))
        {
            const __int128 value = asm_absolute(parser, end);
            if (asm_is(parser, option, "is_stmt") && value != 0 && value != 1)
            {
                asm_fail(parser, at, "is_stmt value not 0 or 1");
            }
            else if (value < 0 || value > UINT32_MAX)
            {
                asm_fail(parser, at, asm_is(parser, option, "isa") ? "isa number less than zero" : "discriminator less than zero");
            }
            else if (asm_is(parser, option, "is_stmt"))
            {
                state->flags = (uint8_t)(value != 0 ? state->flags | ASM_LINE_IS_STMT : state->flags & ~ASM_LINE_IS_STMT);
            }
            else if (asm_is(parser, option, "isa"))
            {
                state->isa = (uint32_t)value;
            }
            else
            {
                state->discriminator = (uint32_t)value;
            }
        }
        else if (asm_is(parser, option, "view") && parser->at < end && (parser->tokens[parser->at].kind == '-' || parser->tokens[parser->at].kind == BUSTER_ASM_WORD))
        {
            const String8 text = asm_token_text(parser, parser->at);
            const bool reset = parser->tokens[parser->at].kind == '-';
            if (reset || (text.pointer[0] >= '0' && text.pointer[0] <= '9'))
            {
                if (asm_absolute(parser, end) != 0)
                {
                    asm_fail(parser, at, "numeric view can only be asserted to zero");
                }
                state->view_kind = reset ? ASM_VIEW_RESET : ASM_VIEW_ZERO;
                state->view_symbol = 0;
            }
            else
            {
                const uint32_t id = asm_symbol(parser, asm_name_text(parser, parser->at), parser->tokens[parser->at].start);
                parser->at += 1;
                if (parser->symbols[id].defined || parser->symbols[id].absolute || parser->symbols[id].alias)
                {
                    asm_fail(parser, at, "symbol is already defined");
                }
                state->view_kind = ASM_VIEW_SYMBOL;
                state->view_symbol = id;
            }
        }
        else
        {
            asm_fail(parser, at, "unknown .loc sub-directive");
        }
    }
    if (parser->result.error == 0 && parser->at < end)
    {
        asm_fail(parser, parser->tokens[parser->at].start, "junk at end of .loc");
    }
    if (parser->result.error == 0)
    {
        state->pending = true;
        if (state->view_kind != ASM_VIEW_NONE)
        {
            asm_line_row(parser, location);
        }
    }
}

// Whether a section holds bytes, as GNU as's seg_not_empty_p sees them: an instruction or data, not only labels, fill
// or alignment.
static bool asm_line_holds(const AsmParser* parser, uint32_t section)
{
    bool result = false;
    for (uint32_t i = 0; section != 0 && !result && i < parser->fragment_count; i += 1)
    {
        const AsmFragment* fragment = &parser->fragments[i];
        result = fragment->section == section &&
                 (fragment->kind == ASM_INSTRUCTION || fragment->kind == ASM_ULEB || fragment->kind == ASM_SLEB ||
                  ((fragment->kind == ASM_VALUE || fragment->kind == ASM_BYTES) && fragment->size != 0));
    }
    return result;
}

// A debug section the table writes, as GNU as makes it when the source has none: .debug_line of no flags, and DWARF
// 5's .debug_line_str of merged strings.
static uint32_t asm_line_section(AsmParser* parser, const char* name, bool strings)
{
    const String8 spelling = { .pointer = (char*)name, .length = strlen(name) };
    uint32_t id = asm_find_section(parser, spelling);
    if (id == 0 && parser->section_count >= parser->section_capacity)
    {
        asm_fail(parser, 0, "section capacity exhausted");
    }
    else if (id == 0)
    {
        id = parser->section_count++;
        parser->sections[id] = (AsmSection){
            .object = {
                .name = spelling,
                .type = SHT_PROGBITS,
                .flags = strings ? SHF_MERGE | SHF_STRINGS : 0,
                .entry_size = strings ? 1 : 0,
            },
            .class = strings ? BUSTER_LINK_CLASS_STRINGS : BUSTER_LINK_CLASS_RODATA,
            .declared = true,
        };
        asm_section_index(parser, id);
    }
    return id;
}

// A line string of .debug_line_str: its label, then its bytes and a terminating zero.
static uint32_t asm_line_string(AsmParser* parser, String8 text)
{
    const uint32_t label = asm_dot(parser, 0);
    const uint32_t fragment = asm_fragment(parser, ASM_BYTES, 0);
    char* bytes = arena_push(parser->arena, text.length + 1, 1);
    if (bytes == 0)
    {
        asm_fail(parser, 0, "arena exhausted writing line strings");
    }
    else if (parser->result.error == 0)
    {
        memcpy(bytes, text.pointer, text.length);
        bytes[text.length] = 0;
        parser->fragments[fragment].bytes = (uint8_t*)bytes;
        parser->fragments[fragment].size = (uint32_t)text.length + 1;
    }
    return label;
}

// After the source, as GNU as's dwarf2_finish: whether the object has a line table, and its strings and the fragment
// its bytes fill. GNU as writes one where there are rows or a .debug_info section of bytes, and .debug_line is empty;
// without a .debug_info it writes its own DWARF too, which isn't done here, and rows beside a .debug_line of bytes
// are its error.
static void asm_line_finish(AsmParser* parser)
{
    const uint32_t info = asm_find_section(parser, BUSTER_S8(".debug_info"));
    const bool described = asm_line_holds(parser, info);
    const uint32_t existing = asm_find_section(parser, BUSTER_S8(".debug_line"));
    const bool written = asm_line_holds(parser, existing);
    if (parser->line_row_count != 0 && !described)
    {
        asm_fail(parser, 0, "line rows without a .debug_info, for which GNU as writes its own DWARF, are unsupported");
    }
    else if (parser->line_row_count != 0 && written)
    {
        asm_fail(parser, 0, "duplicate .debug_line sections");
    }
    else if (described && !written)
    {
        const uint32_t previous = parser->section;
        const uint32_t files = parser->line_file_count;
        const uint32_t directories = parser->line_directory_count;
        if (parser->line_dwarf5 && (directories == 0 || parser->line_directories[0].pointer == 0) && (directories != 0 || files != 0))
        {
            asm_fail(parser, 0, "a DWARF 5 line table without `.file 0`'s directory, which GNU as takes as its working directory");
        }
        for (uint32_t i = parser->line_dwarf5 ? 0 : 1; i < files && parser->result.error == 0; i += 1)
        {
            if (!parser->line_files[i].assigned)
            {
                asm_fail(parser, 0, "unassigned file number");
            }
        }
        for (uint32_t i = 1; i < directories && parser->result.error == 0; i += 1)
        {
            if (parser->line_directories[i].pointer == 0)
            {
                asm_fail(parser, 0, "unassigned directory number");
            }
        }
        if (parser->result.error == 0 && parser->line_dwarf5 && files + directories != 0)
        {
            // GNU as appends each directory's and file's name to .debug_line_str, after the source's own strings.
            parser->line_strings = BUSTER_ARENA_PUSH_ARRAY(parser->arena, uint32_t, files + directories);
            parser->section = asm_line_section(parser, ".debug_line_str", true);
            for (uint32_t i = 0; parser->line_strings != 0 && i < directories && parser->result.error == 0; i += 1)
            {
                parser->line_strings[i] = asm_line_string(parser, parser->line_directories[i]);
            }
            for (uint32_t i = 0; parser->line_strings != 0 && i < files && parser->result.error == 0; i += 1)
            {
                parser->line_strings[directories + i] = asm_line_string(parser, parser->line_files[i].name);
            }
            if (parser->line_strings == 0)
            {
                asm_fail(parser, 0, "arena exhausted writing line strings");
            }
        }
        if (parser->result.error == 0)
        {
            parser->line_section = asm_line_section(parser, ".debug_line", false);
            parser->section = parser->line_section;
            parser->line_fragment = asm_fragment(parser, ASM_BYTES, 0);
        }
        parser->section = previous;
    }
}

// Each row's view number, from its fragment's place: 0 at its section's first row and after `view -0`, 0 again where
// the address has moved past the previous row's, and otherwise one more than the previous row's (GNU as's
// set_or_check_view). Layout calls this between its passes, so that a .uleb128 of a view takes its size, and at its
// end, where `view 0` must hold.
static void asm_line_views(AsmParser* parser, bool final)
{
    for (uint32_t r = 0; r < parser->line_row_count; r += 1)
    {
        AsmLineRow* row = &parser->line_rows[r];
        const AsmLineRow* previous = row->previous != 0 ? &parser->line_rows[row->previous - 1] : 0;
        const bool moved = previous == 0 || parser->fragments[row->fragment].offset > parser->fragments[previous->fragment].offset;
        row->view = row->view_kind == ASM_VIEW_RESET || moved ? 0 : previous->view + 1;
        if (final && row->view_kind == ASM_VIEW_ZERO && row->view != 0)
        {
            asm_fail(parser, parser->fragments[row->fragment].location, "view number mismatch");
        }
    }
}

// Whether GNU as's x86 relaxes the instruction, ending its frag after the opcode: a jmp or jcc to a label.
static bool asm_line_relaxed(const AsmParser* parser, const AsmFragment* fragment)
{
    const X86FormDescription* form = x86_form_description(parser->instructions[fragment->instruction].record.form);
    return form->relative_bytes != 0 && form->mnemonic[0] == 'j' && strcmp(form->mnemonic, "jrcxz") != 0 && strcmp(form->mnemonic, "jecxz") != 0 &&
           strcmp(form->mnemonic, "jcxz") != 0;
}

static void asm_line_fixup(AsmParser* parser, AsmCfiWriter* writer, uint32_t symbol, uint8_t kind, uint32_t count)
{
    parser->line_fixups[parser->line_fixup_count++] = (AsmLineFixup){ .offset = parser->fragments[parser->line_fragment].offset + writer->size, .symbol = symbol, .kind = kind };
    // A field of zeros under the relocation, four bytes at a time (asm_cfi_integer writes at most a uint32_t's).
    for (uint32_t i = 0; i < count; i += 4)
    {
        asm_cfi_integer(writer, 0, 4);
    }
}

static uint32_t asm_line_leb_size(uint64_t value)
{
    uint32_t size = 1;
    while (value >= 0x80)
    {
        value >>= 7;
        size += 1;
    }
    return size;
}

// GNU as's emit_inc_line_addr: a row's line and address advance, the smallest of a special opcode, DW_LNS_const_add_pc
// and one, or DW_LNS_advance_pc and a special opcode or DW_LNS_copy, after DW_LNS_advance_line for a line out of a
// special opcode's range; or the end of a sequence (`end`), after the address's advance.
static void asm_line_advance(AsmCfiWriter* writer, int64_t line, uint64_t address, bool end)
{
    if (end)
    {
        if (address == 17)
        {
            asm_cfi_integer(writer, 8, 1);
        }
        else if (address != 0)
        {
            asm_cfi_integer(writer, 2, 1);
            asm_cfi_leb(writer, (int64_t)address, false);
        }
        asm_cfi_integer(writer, 0, 1);
        asm_cfi_integer(writer, 1, 1);
        asm_cfi_integer(writer, 1, 1);
    }
    else
    {
        uint32_t bias = (uint32_t)line + 5;
        bool copy = false;
        if (bias >= 14)
        {
            asm_cfi_integer(writer, 3, 1);
            asm_cfi_leb(writer, line, true);
            line = 0;
            bias = 5;
            copy = true;
        }
        if (line == 0 && address == 0)
        {
            asm_cfi_integer(writer, 1, 1);
        }
        else
        {
            bias += 13;
            const uint64_t special = bias + address * 14;
            const uint64_t added = bias + (address - 17) * 14;
            if (address < 256 + 17 && special <= 255)
            {
                asm_cfi_integer(writer, (uint32_t)special, 1);
            }
            else if (address < 256 + 17 && added <= 255)
            {
                asm_cfi_integer(writer, 8, 1);
                asm_cfi_integer(writer, (uint32_t)added, 1);
            }
            else
            {
                asm_cfi_integer(writer, 2, 1);
                asm_cfi_leb(writer, (int64_t)address, false);
                asm_cfi_integer(writer, copy ? 1 : bias, 1);
            }
        }
    }
}

// The header's and tables' strings, after DWARF 3's or in DWARF 5's .debug_line_str (its labels' ABS32s).
static void asm_line_header(AsmParser* parser, AsmCfiWriter* writer)
{
    const uint32_t files = parser->line_file_count;
    const uint32_t directories = parser->line_directory_count;
    const bool five = parser->line_dwarf5;
    static const uint8_t lengths[] = { 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1 };
    asm_cfi_integer(writer, 0, 4);
    asm_cfi_integer(writer, five ? 5 : 3, 2);
    if (five)
    {
        asm_cfi_integer(writer, 8, 1);
        asm_cfi_integer(writer, 0, 1);
    }
    asm_cfi_integer(writer, 0, 4);
    const uint32_t prologue = writer->size;
    asm_cfi_integer(writer, 1, 1);
    if (five)
    {
        asm_cfi_integer(writer, 1, 1);
    }
    asm_cfi_integer(writer, 1, 1);
    asm_cfi_integer(writer, 0xfb, 1);
    asm_cfi_integer(writer, 14, 1);
    asm_cfi_integer(writer, 13, 1);
    for (uint32_t i = 0; i < sizeof(lengths); i += 1)
    {
        asm_cfi_integer(writer, lengths[i], 1);
    }
    if (five)
    {
        // One column of directories, DW_LNCT_path as DW_FORM_line_strp; files' path, DW_LNCT_directory_index as
        // DW_FORM_udata, and DW_LNCT_MD5 as DW_FORM_data16 where a file's first byte of one isn't 0, as GNU as checks.
        bool md5 = false;
        for (uint32_t i = 0; i < files; i += 1)
        {
            md5 |= parser->line_files[i].md5[0] != 0;
        }
        asm_cfi_integer(writer, 1, 1);
        asm_cfi_leb(writer, 1, false);
        asm_cfi_leb(writer, 0x1f, false);
        asm_cfi_leb(writer, directories, false);
        for (uint32_t i = 0; i < directories; i += 1)
        {
            asm_line_fixup(parser, writer, parser->line_strings[i], BUSTER_LINK_FIXUP_ABS32, 4);
        }
        asm_cfi_integer(writer, md5 ? 3 : 2, 1);
        asm_cfi_leb(writer, 1, false);
        asm_cfi_leb(writer, 0x1f, false);
        asm_cfi_leb(writer, 2, false);
        asm_cfi_leb(writer, 0x0f, false);
        if (md5)
        {
            asm_cfi_leb(writer, 5, false);
            asm_cfi_leb(writer, 0x1e, false);
        }
        asm_cfi_leb(writer, files, false);
        for (uint32_t i = 0; i < files; i += 1)
        {
            asm_line_fixup(parser, writer, parser->line_strings[directories + i], BUSTER_LINK_FIXUP_ABS32, 4);
            asm_cfi_leb(writer, parser->line_files[i].directory, false);
            for (uint32_t b = 0; md5 && b < 16; b += 1)
            {
                asm_cfi_integer(writer, parser->line_files[i].md5[b], 1);
            }
        }
    }
    else
    {
        // DWARF 3: the directories after the compilation's, then the files from 1 with no time or size, each list
        // ending in a 0.
        for (uint32_t i = 1; i < directories; i += 1)
        {
            for (uint64_t c = 0; c < parser->line_directories[i].length; c += 1)
            {
                asm_cfi_integer(writer, (uint8_t)parser->line_directories[i].pointer[c], 1);
            }
            asm_cfi_integer(writer, 0, 1);
        }
        asm_cfi_integer(writer, 0, 1);
        for (uint32_t i = 1; i < files; i += 1)
        {
            for (uint64_t c = 0; c < parser->line_files[i].name.length; c += 1)
            {
                asm_cfi_integer(writer, (uint8_t)parser->line_files[i].name.pointer[c], 1);
            }
            asm_cfi_integer(writer, 0, 1);
            asm_cfi_leb(writer, parser->line_files[i].directory, false);
            asm_cfi_integer(writer, 0, 2);
        }
        asm_cfi_integer(writer, 0, 1);
    }
    if (!writer->failed)
    {
        const uint32_t length = writer->size - prologue;
        memcpy(writer->bytes + prologue - 4, &length, 4);
    }
}

// After layout, .debug_line's bytes as GNU as's out_debug_line writes them: the header, then each code section's
// sequence in the order of its first row. A sequence starts at its first row's DW_LNE_set_address (an ABS64 of a
// label there) and ends at its section's end; each row sets what changed of file, column, discriminator, isa and
// is_stmt, its flags, then its advance. GNU as states the address again for a `view -0` row its frags can't tell
// apart from the previous row's place (process_entries): one in the same frag at the same offset, or at its frag's
// start where the previous row ended its frag's fixed bytes. GNU as's frags end at an alignment, a `.skip`, a symbolic
// LEB128 and after a jmp's or jcc's opcode, which are modeled here; they also end where its memory chunks fill, which
// isn't, so such a row can take an advance here where GNU as states its address, the same row either way.
static void asm_line_emit(AsmParser* parser)
{
    if (parser->line_section != 0 && parser->result.error == 0)
    {
        Scratch scratch = scratch_begin(parser->arena);
        uint32_t* serial = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, parser->section_count);
        uint8_t* fixed = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, parser->section_count);
        uint32_t* last = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, parser->section_count);
        uint32_t* row_serial = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, parser->line_row_count + 1);
        uint8_t* fixed_after = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, parser->line_row_count + 1);
        uint32_t* labels = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, parser->line_row_count + 1);
        const uint64_t bound = 128 + 4 * (uint64_t)parser->line_directory_count + 64 * (uint64_t)parser->line_file_count + 80 * (uint64_t)parser->line_row_count;
        uint8_t* bytes = bound <= BUSTER_ASM_MAX_SECTION ? arena_push(parser->arena, bound, 8) : 0;
        parser->line_fixups = BUSTER_ARENA_PUSH_ARRAY(parser->arena, AsmLineFixup, parser->line_directory_count + parser->line_file_count + parser->line_row_count + 1);
        if (serial == 0 || fixed == 0 || last == 0 || row_serial == 0 || fixed_after == 0 || labels == 0 || bytes == 0 || parser->line_fixups == 0)
        {
            asm_fail(parser, 0, "arena exhausted writing the line table");
        }
        else
        {
            memset(serial, 0, parser->section_count * sizeof(uint32_t));
            memset(fixed, 0, parser->section_count);
            memset(last, 0, parser->section_count * sizeof(uint32_t));
            // GNU as's frags, from the fragments in source order: which rows state their address (labels[r]).
            uint32_t cursor = 0;
            for (uint32_t id = 0; id < parser->fragment_count && parser->result.error == 0; id += 1)
            {
                const AsmFragment* fragment = &parser->fragments[id];
                const uint32_t section = fragment->section;
                if (cursor < parser->line_row_count && parser->line_rows[cursor].fragment == id)
                {
                    const AsmLineRow* row = &parser->line_rows[cursor];
                    const uint32_t previous = row->previous;
                    bool states = previous == 0;
                    if (!states && row->view_kind == ASM_VIEW_RESET)
                    {
                        states = row_serial[previous - 1] == serial[section] ? !fixed_after[previous - 1] : !fixed_after[previous - 1] && !fixed[section];
                    }
                    labels[cursor] = 0;
                    if (states && parser->symbol_count < parser->capacity)
                    {
                        labels[cursor] = parser->symbol_count++;
                        parser->symbols[labels[cursor]] = (AsmSymbol){ .fragment = id, .location = fragment->location, .defined = true, .internal = true };
                    }
                    else if (states)
                    {
                        asm_fail(parser, fragment->location, "symbol capacity exhausted");
                    }
                    row_serial[cursor] = serial[section];
                    fixed_after[cursor] = 0;
                    last[section] = cursor + 1;
                    cursor += 1;
                }
                const bool instruction = fragment->kind == ASM_INSTRUCTION;
                const bool symbolic = (fragment->kind == ASM_ULEB || fragment->kind == ASM_SLEB) && (fragment->expression.plus != 0 || fragment->expression.minus != 0);
                const bool bytes_fixed = instruction || ((fragment->kind == ASM_VALUE || fragment->kind == ASM_BYTES || fragment->kind == ASM_ULEB || fragment->kind == ASM_SLEB) &&
                                                         !symbolic && fragment->size != 0);
                const bool boundary = (fragment->kind == ASM_ALIGN && fragment->alignment > 1) || (fragment->kind == ASM_FILL && fragment->size != 0) || symbolic ||
                                      (instruction && asm_line_relaxed(parser, fragment));
                if (bytes_fixed)
                {
                    fixed[section] = 1;
                    if (last[section] != 0 && row_serial[last[section] - 1] == serial[section])
                    {
                        fixed_after[last[section] - 1] = 1;
                    }
                }
                if (boundary)
                {
                    serial[section] += 1;
                    fixed[section] = 0;
                }
            }
            AsmCfiWriter writer = { .bytes = bytes, .capacity = (uint32_t)bound };
            asm_line_header(parser, &writer);
            for (uint32_t first = 0; first < parser->line_row_count && parser->result.error == 0; first += 1)
            {
                if (parser->line_rows[first].previous != 0)
                {
                    continue;
                }
                uint32_t file = 1;
                uint32_t line = 1;
                uint32_t column = 0;
                uint32_t isa = 0;
                uint8_t flags = ASM_LINE_IS_STMT;
                uint32_t previous = 0;
                for (uint32_t r = first + 1; r != 0; r = parser->line_rows[r - 1].next)
                {
                    const AsmLineRow* row = &parser->line_rows[r - 1];
                    if (file != row->file)
                    {
                        file = row->file;
                        asm_cfi_integer(&writer, 4, 1);
                        asm_cfi_leb(&writer, file, false);
                    }
                    if (column != row->column)
                    {
                        column = row->column;
                        asm_cfi_integer(&writer, 5, 1);
                        asm_cfi_leb(&writer, column, false);
                    }
                    if (row->discriminator != 0)
                    {
                        asm_cfi_integer(&writer, 0, 1);
                        asm_cfi_leb(&writer, 1 + asm_line_leb_size(row->discriminator), true);
                        asm_cfi_integer(&writer, 4, 1);
                        asm_cfi_leb(&writer, row->discriminator, false);
                    }
                    if (isa != row->isa)
                    {
                        isa = row->isa;
                        asm_cfi_integer(&writer, 12, 1);
                        asm_cfi_leb(&writer, isa, false);
                    }
                    if (((row->flags ^ flags) & ASM_LINE_IS_STMT) != 0)
                    {
                        flags = row->flags;
                        asm_cfi_integer(&writer, 6, 1);
                    }
                    if ((row->flags & ASM_LINE_BASIC_BLOCK) != 0)
                    {
                        asm_cfi_integer(&writer, 7, 1);
                    }
                    if ((row->flags & ASM_LINE_PROLOGUE_END) != 0)
                    {
                        asm_cfi_integer(&writer, 10, 1);
                    }
                    if ((row->flags & ASM_LINE_EPILOGUE_BEGIN) != 0)
                    {
                        asm_cfi_integer(&writer, 11, 1);
                    }
                    const int64_t delta = (int64_t)row->line - (int64_t)line;
                    const uint32_t offset = parser->fragments[row->fragment].offset;
                    if (labels[r - 1] != 0)
                    {
                        asm_cfi_integer(&writer, 0, 1);
                        asm_cfi_integer(&writer, 9, 1);
                        asm_cfi_integer(&writer, 2, 1);
                        asm_line_fixup(parser, &writer, labels[r - 1], BUSTER_LINK_FIXUP_ABS64, 8);
                        asm_line_advance(&writer, delta, 0, false);
                    }
                    else
                    {
                        asm_line_advance(&writer, delta, offset - parser->fragments[parser->line_rows[previous - 1].fragment].offset, false);
                    }
                    line = row->line;
                    previous = r;
                }
                const AsmLineRow* tail = &parser->line_rows[previous - 1];
                const uint32_t section = parser->fragments[tail->fragment].section;
                asm_line_advance(&writer, 0, parser->sections[section].size - parser->fragments[tail->fragment].offset, true);
            }
            if (!writer.failed)
            {
                const uint32_t length = writer.size - 4;
                memcpy(bytes, &length, 4);
            }
            if (writer.failed)
            {
                asm_fail(parser, 0, "line table exceeds its bounded output");
            }
            else if (parser->result.error == 0)
            {
                AsmFragment* fragment = &parser->fragments[parser->line_fragment];
                fragment->bytes = bytes;
                fragment->size = writer.size;
                parser->sections[parser->line_section].size = fragment->offset + writer.size;
            }
        }
        scratch_end(scratch);
    }
}

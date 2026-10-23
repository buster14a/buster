// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The foreign inputs' tests (foreign.c), for the test-foreign command:
//
//   build/compiler test-foreign [--seed N] [--functions N] [--directory PATH] [--cross-check] [--keep]
//
// main.c includes this file after foreign.c, and after link_test.c (link_test_program_options and its sets' names),
// object_writer_test.c (object_test_run, object_test_read_file, object_test_source_name) and runtime.c (the system's C
// runtime, runtime_linker_name). Every resolution runs by both paths and with groups of 1, 2, 3 and 8 lanes, which
// must describe it alike, byte for byte (foreign_test_describe: the loaded objects, every name's resolution and
// winner, every symbol's name id and every group's fate):
//   - Programs: test-link's sets, each unit's object written by the object writer, resolved four ways: every unit an
//     object; the units after the first in an archive after it; split between an archive and a thin archive; and an
//     archive before the first unit, which only lld's back references load. Every name must be defined, but the
//     linker's (runtime_linker_name), the weak references to nothing, and for the sets that call libc, libc's, which
//     bind to libc.so.6 through the system's libc.so script, with the crt objects and libgcc as GCC's driver gives them.
//   - Compiled programs: sources cc compiles (and g++, for COMDAT groups from inline functions and templates), in cases
//     of commons, GNU ld's common rule, weak and strong definitions, archives (two of one name, a thin one, a whole one,
//     -u, back references), a shared library beside an archive in each order, a duplicate, undefined names and a PIE
//     linked with libc as GCC's driver links one: each with what its resolution must be (foreign_test_outcomes).
//   - With --cross-check, which needs ld.bfd, ld.lld, cc and (for COMDAT groups) g++: each case linked by GNU ld and by
//     lld, whose members (--trace twice, --why-extract), winners (the output's .symtab against the map's input
//     sections), groups (the map) and -y lines must be the resolution's, and whose failures must be its undefined and
//     duplicate names; a case says where a linker differs, as foreign.c documents, and those differences are counted
//     apart.
//   - Malformed inputs, each refused naming its place: one field of a compiled object changed (a symbol's binding,
//     type, section index, alignment or name, the symbol table's first global, a relocation section's link, target or
//     type), a broken archive member (refused only when it loads), a thin archive's missing member, a member that isn't
//     an object, a script that doesn't parse, a -l name no directory has, a missing file and an unknown option.
//   - Page edges: inputs given in memory, ending at a PROT_NONE page: every prefix of an object and of an archive, and
//     the object with each byte set to 0, 0xff and a random value; both paths must resolve each alike or refuse it
//     alike, and a read past the end faults.
// The inputs go in a new directory under --directory ($TMPDIR, or /tmp), removed at the end unless --keep. The exit
// status is 1 if the arguments are invalid or a check fails.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <elf.h>

// OS-dependent includes
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>

// ---------------------------------------------------------------------------------------------------------------------
// The tally

#define BUSTER_FOREIGN_TEST_REPORTED 20
#define BUSTER_FOREIGN_TEST_PATH_BYTES 4096
#define BUSTER_FOREIGN_TEST_ARENA_BYTES (1ull << 34)
#define BUSTER_FOREIGN_TEST_MAX_ARGUMENTS 4096
typedef char ForeignTestPath[BUSTER_FOREIGN_TEST_PATH_BYTES];
// The lane counts every resolution runs with.
static const uint32_t foreign_test_lanes[] = {
    1,
    2,
    3,
    8,
};

typedef struct ForeignTestTally ForeignTestTally;
struct ForeignTestTally
{
    const char* check;
    uint64_t checked;
    uint64_t failures;
    // A tally of differences the case documents, counted but not printed.
    bool quiet;
};

static void foreign_test_expect(ForeignTestTally* tally, bool condition, const char* what, const char* detail)
{
    tally->checked += 1;
    if (!condition)
    {
        if (tally->failures < BUSTER_FOREIGN_TEST_REPORTED && !tally->quiet)
        {
            printf("  %s: %s%s%s\n", tally->check, what, detail != 0 ? ": " : "", detail != 0 ? detail : "");
        }
        tally->failures += 1;
    }
}

static bool foreign_test_report(const ForeignTestTally* tally)
{
    printf("%s: %llu checks, %llu failures\n", tally->check, (unsigned long long)tally->checked, (unsigned long long)tally->failures);
    return tally->failures == 0;
}

static const char* const foreign_test_errors[] = {
    [FOREIGN_ERROR_NONE] = "none",
    [FOREIGN_ERROR_ARGUMENTS] = "arguments",
    [FOREIGN_ERROR_FILE] = "file",
    [FOREIGN_ERROR_NOT_FOUND] = "not found",
    [FOREIGN_ERROR_FORMAT] = "format",
    [FOREIGN_ERROR_MALFORMED] = "malformed",
    [FOREIGN_ERROR_UNSUPPORTED] = "unsupported",
    [FOREIGN_ERROR_MEMORY] = "memory",
    [FOREIGN_ERROR_LANES] = "lanes",
};

static const char* const foreign_test_resolutions[FOREIGN_RESOLUTIONS] = {
    [FOREIGN_RESOLUTION_UNDEFINED] = "undefined",
    [FOREIGN_RESOLUTION_STRONG] = "strong",
    [FOREIGN_RESOLUTION_COMMON] = "common",
    [FOREIGN_RESOLUTION_WEAK] = "weak",
    [FOREIGN_RESOLUTION_SHARED] = "shared",
    [FOREIGN_RESOLUTION_WEAK_UNDEFINED] = "weak-undefined",
};

// ---------------------------------------------------------------------------------------------------------------------
// Descriptions
//
// A resolution's description is text that holds everything it found: each input's kind, each loaded object (its
// position, input and member), and each name's resolution, definition, counts, common size and alignment, visibility
// and flags, with each symbol's name id and each group's fate. Two resolutions are the same when their descriptions are,
// byte for byte, which is how the paths and the lane counts are compared.

typedef struct ForeignTestText ForeignTestText;
struct ForeignTestText
{
    char* bytes;
    uint64_t length;
    uint64_t capacity;
};

static void foreign_test_print(ForeignTestText* text, const char* format, ...) __attribute__((format(printf, 2, 3)));

static void foreign_test_print(ForeignTestText* text, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const uint64_t room = text->capacity - text->length;
    const int written = vsnprintf(text->bytes + text->length, room, format, arguments);
    va_end(arguments);
    text->length += written > 0 && (uint64_t)written < room ? (uint64_t)written : 0;
}

// An object's name as lld prints it in its map and traces: its path, or archive(member).
static void foreign_test_object_name(const Foreign* foreign, uint32_t id, char* buffer, uint64_t size)
{
    const ForeignObject* object = &foreign->objects[id];
    if (id == 0)
    {
        snprintf(buffer, size, "(command line)");
    }
    else if (object->member == 0)
    {
        snprintf(buffer, size, "%.*s", (int)foreign->inputs[object->input].path.length, foreign->inputs[object->input].path.pointer);
    }
    else
    {
        const ForeignInput* input = &foreign->inputs[object->input];
        const String8 member = foreign->archives[input->item].members[object->member - 1].name;
        snprintf(buffer, size, "%.*s(%.*s)", (int)input->path.length, input->path.pointer, (int)member.length, member.pointer);
    }
}

// A thin archive's member that is a member of another archive (ImportMember.nested): its name there, from its header in
// that archive (its file's mapping, the object's), up to the '/' that ends it; false for any other object.
static bool foreign_test_nested_name(const Foreign* foreign, uint32_t id, String8* name)
{
    const ForeignObject* object = &foreign->objects[id];
    const ImportMember* member = id != 0 && object->member != 0 ? &foreign->archives[foreign->inputs[object->input].item].members[object->member - 1] : 0;
    const bool result = member != 0 && member->nested != 0 && object->mapping.length >= member->nested - 1 + 16;
    if (result)
    {
        const char* field = object->mapping.pointer + member->nested - 1;
        const char* slash = memchr(field, '/', 16);
        name->pointer = (char*)field;
        name->length = slash != 0 ? (uint64_t)(slash - field) : 16;
    }
    return result;
}

// Whether an object is a thin archive's member of its own file, which GNU ld names by its path alone (the test's thin
// archives name their members by absolute paths).
static bool foreign_test_thin_member(const Foreign* foreign, uint32_t id)
{
    const ForeignObject* object = &foreign->objects[id];
    String8 nested = { 0 };
    return id != 0 && object->member != 0 && foreign->archives[foreign->inputs[object->input].item].thin && !foreign_test_nested_name(foreign, id, &nested);
}

// An object's name as a linker prints it: lld's, or GNU ld's, which is a thin archive member's path alone, or for a
// member of another archive that archive's path (as the thin archive names it) and the member's name there.
static void foreign_test_linker_name(const Foreign* foreign, uint32_t id, bool bfd, char* buffer, uint64_t size)
{
    const ForeignObject* object = &foreign->objects[id];
    String8 nested = { 0 };
    if (bfd && foreign_test_thin_member(foreign, id))
    {
        const String8 member = foreign->archives[foreign->inputs[object->input].item].members[object->member - 1].name;
        snprintf(buffer, size, "%.*s", (int)member.length, member.pointer);
    }
    else if (bfd && foreign_test_nested_name(foreign, id, &nested))
    {
        const String8 member = foreign->archives[foreign->inputs[object->input].item].members[object->member - 1].name;
        snprintf(buffer, size, "%.*s(%.*s)", (int)member.length, member.pointer, (int)nested.length, nested.pointer);
    }
    else
    {
        foreign_test_object_name(foreign, id, buffer, size);
    }
}

// The name of a section of an object, or COMMON or ABS for those symbols' sections.
static String8 foreign_test_section_name(const Foreign* foreign, uint32_t id, uint32_t section)
{
    const ForeignObject* object = &foreign->objects[id];
    String8 result = BUSTER_S8("?");
    if (section == BUSTER_FOREIGN_SECTION_COMMON)
    {
        result = BUSTER_S8("COMMON");
    }
    else if (section == BUSTER_FOREIGN_SECTION_ABS)
    {
        result = BUSTER_S8("ABS");
    }
    else if (section != SHN_UNDEF && section < object->elf.section_count)
    {
        const Elf64_Shdr header = import_section_header(&object->elf, section);
        import_section_name(&object->elf, &header, &result);
    }
    return result;
}

static void foreign_test_describe(const Foreign* foreign, ForeignTestText* text)
{
    text->length = 0;
    char name[BUSTER_FOREIGN_TEST_PATH_BYTES];
    foreign_test_print(text, "failure %d input %u member %u import %d %u %u %llu\n", foreign->failure.error, foreign->failure.input, foreign->failure.member,
                       foreign->failure.import.error, foreign->failure.import.section, foreign->failure.import.symbol, (unsigned long long)foreign->failure.import.offset);
    for (uint32_t i = 0; foreign->failure.error == FOREIGN_ERROR_NONE && i < foreign->input_count; i += 1)
    {
        const ForeignInput* input = &foreign->inputs[i];
        foreign_test_print(text, "input %u %d %.*s\n", i, input->kind, (int)input->path.length, input->path.pointer);
    }
    for (uint32_t id = 0; foreign->failure.error == FOREIGN_ERROR_NONE && id < foreign->object_count; id += 1)
    {
        const ForeignObject* object = &foreign->objects[id];
        foreign_test_object_name(foreign, id, name, sizeof(name));
        foreign_test_print(text, "object %u %llx %s globals %u+%u groups %u+%u relocations %u\n", id, (unsigned long long)object->position, name, object->global_begin,
                           object->global_count, object->group_begin, object->group_count, object->relocation_count);
    }
    const ForeignNames* names = &foreign->names;
    for (uint32_t n = 0; foreign->failure.error == FOREIGN_ERROR_NONE && n < names->count; n += 1)
    {
        const uint8_t resolution = names->resolution[n];
        const bool defined = resolution == FOREIGN_RESOLUTION_STRONG || resolution == FOREIGN_RESOLUTION_COMMON || resolution == FOREIGN_RESOLUTION_WEAK;
        name[0] = 0;
        if (defined)
        {
            foreign_test_object_name(foreign, foreign_symbol_object(foreign, names->symbol[n]), name, sizeof(name));
        }
        foreign_test_print(text, "name %u %.*s %s %u %u %s defs %u second %u common %llu/%u vis %u flags %u\n", n, (int)names->length[n], names->bytes[n],
                           resolution < FOREIGN_RESOLUTIONS ? foreign_test_resolutions[resolution] : "?", names->symbol[n], names->library[n], name, names->definitions[n],
                           names->second[n], (unsigned long long)names->common_size[n], names->common_alignment[n], names->visibility[n], names->flags[n]);
    }
    for (uint32_t r = 0; foreign->failure.error == FOREIGN_ERROR_NONE && r < foreign->round_count; r += 1)
    {
        const ForeignRound* round = &foreign->rounds[r];
        foreign_test_print(text, "round %u objects %u-%u symbols %u+%u groups %u+%u\n", r, round->first_object, round->end_object, round->first_symbol, round->symbol_count,
                           round->first_group, round->group_count);
        for (uint32_t s = 0; s < round->symbol_count; s += 1)
        {
            foreign_test_print(text, "%u ", round->symbols.name_id[s]);
        }
        foreign_test_print(text, "\n");
        for (uint32_t g = 0; g < round->group_count; g += 1)
        {
            foreign_test_print(text, "%u", round->groups.kept[g]);
        }
        foreign_test_print(text, "\n");
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Resolving
//
// A case is a command line (the arguments foreign_arguments reads). foreign_test_resolve resolves it by both paths and
// with every lane count, checks that every description is the first's, and keeps the exact path's on one lane in
// `kept` for the caller's checks. Its partitions aim at BUSTER_FOREIGN_TEST_PARTITION_RECORDS records each, so that
// the cases' few records spread over many partitions, and so over every lane.

#define BUSTER_FOREIGN_TEST_PARTITION_RECORDS 16

static bool foreign_test_command(ForeignCommand* command, char** arguments, uint32_t count, Arena* arena)
{
    return foreign_arguments(command, (int)count, arguments, arena).error == FOREIGN_ERROR_NONE;
}

static void foreign_test_resolve(ForeignTestTally* tally, const char* what, const ForeignCommand* given, Foreign* kept, Arena* arena)
{
    ForeignCommand spread = *given;
    spread.partition_records = BUSTER_FOREIGN_TEST_PARTITION_RECORDS;
    const ForeignCommand* command = &spread;
    const uint64_t length = arena->length;
    ForeignTestText reference = {
        .capacity = 64ull << 20,
    };
    ForeignTestText text = reference;
    reference.bytes = arena_push(arena, reference.capacity, 64);
    text.bytes = arena_push(arena, text.capacity, 64);
    bool first = true;
    *kept = (Foreign){ 0 };
    for (uint32_t path = 0; reference.bytes != 0 && text.bytes != 0 && path < 2; path += 1)
    {
        for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(foreign_test_lanes); l += 1)
        {
            Foreign foreign;
            foreign_run(&foreign, command, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, foreign_test_lanes[l], 0);
            foreign_test_describe(&foreign, first ? &reference : &text);
            if (!first)
            {
                char detail[128];
                snprintf(detail, sizeof(detail), "the %s path on %u lanes differs from the exact path on one", path == 0 ? "exact" : "vector", foreign_test_lanes[l]);
                foreign_test_expect(tally, text.length == reference.length && memcmp(text.bytes, reference.bytes, text.length) == 0, what, detail);
            }
            if (first)
            {
                *kept = foreign;
            }
            else
            {
                foreign_release(&foreign);
            }
            first = false;
        }
    }
    foreign_test_expect(tally, reference.bytes != 0 && text.bytes != 0, what, "room for the descriptions");
    arena_pop_to(arena, length);
}

// The name id of `text` in a resolution, plus 1, or 0.
static uint32_t foreign_test_name(const Foreign* foreign, const char* text)
{
    const uint64_t length = strlen(text);
    return foreign_find_name(foreign, (const uint8_t*)text, (uint32_t)length, link_hash((const uint8_t*)text, length));
}

// ---------------------------------------------------------------------------------------------------------------------
// Files
//
// The test writes its inputs into a directory of its own: the generated programs' objects (the object writer's), the C
// programs' (cc's), and archives of them, which it writes itself (foreign_test_write_archive): GNU's format, the symbol
// index of every member's global definitions (commons, weak and absolute ones among them) in member order, as GNU ar
// writes it, long names in "//", and for a thin archive, the members' headers alone with their paths as names.

#define BUSTER_FOREIGN_TEST_MAX_MEMBERS 1024

static String8 foreign_test_read(Arena* arena, const char* path)
{
    return object_test_read_file(arena, path);
}

static bool foreign_test_write(const char* path, const void* bytes, uint64_t size)
{
    return object_write_file(path, bytes, size) == OBJECT_ERROR_NONE;
}

// The global symbols an object defines, as GNU ar indexes them: their names' offsets in `names` (a copy, NUL after each).
static uint32_t foreign_test_defined(String8 bytes, char* names, uint64_t capacity, uint64_t* used)
{
    ImportElf elf;
    ImportSymbols symbols;
    uint32_t result = 0;
    if (import_elf(&elf, bytes, ET_REL).error == IMPORT_ERROR_NONE && import_symbols(&elf, &symbols).error == IMPORT_ERROR_NONE && symbols.section != 0)
    {
        const uint32_t first = import_section_header(&elf, symbols.section).sh_info;
        for (uint32_t i = first; i < symbols.count; i += 1)
        {
            const Elf64_Sym symbol = import_symbol(&symbols, i);
            const uint32_t binding = ELF64_ST_BIND(symbol.st_info);
            String8 name = { 0 };
            if (symbol.st_shndx != SHN_UNDEF && (binding == STB_GLOBAL || binding == STB_WEAK || binding == STB_GNU_UNIQUE) && import_string(symbols.names, symbol.st_name, &name) &&
                *used + name.length + 1 <= capacity)
            {
                memcpy(names + *used, name.pointer, name.length);
                names[*used + name.length] = 0;
                *used += name.length + 1;
                result += 1;
            }
        }
    }
    return result;
}

static void foreign_test_header(char* header, const char* name, uint64_t size)
{
    char text[80];
    memset(header, ' ', BUSTER_IMPORT_ARCHIVE_HEADER_BYTES);
    memcpy(header, name, strlen(name) < 16 ? strlen(name) : 16);
    snprintf(text, sizeof(text), "%-12s%-6s%-6s%-8s%-10llu", "0", "0", "0", "644", (unsigned long long)size);
    memcpy(header + 16, text, 42);
    header[58] = '`';
    header[59] = '\n';
}

// Writes an archive of the objects at `members` (paths) to `path`; a thin one names them by their paths as given.
static bool foreign_test_write_archive(const char* path, char* const* members, uint32_t count, bool thin, Arena* arena)
{
    const uint64_t mark = arena->length;
    String8* contents = BUSTER_ARENA_PUSH_ARRAY(arena, String8, count + 1);
    const uint64_t names_capacity = 16ull << 20;
    char* names = arena_push(arena, names_capacity, 1);
    uint32_t* member_symbols = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count + 1);
    bool result = contents != 0 && names != 0 && member_symbols != 0 && count <= BUSTER_FOREIGN_TEST_MAX_MEMBERS;
    uint64_t used = 0;
    uint64_t symbols = 0;
    uint64_t long_names = 0;
    for (uint32_t m = 0; result && m < count; m += 1)
    {
        contents[m] = foreign_test_read(arena, members[m]);
        result = contents[m].pointer != 0;
        member_symbols[m] = result ? foreign_test_defined(contents[m], names, names_capacity, &used) : 0;
        symbols += member_symbols[m];
        const char* slash = strrchr(members[m], '/');
        long_names += strlen(thin || slash == 0 ? members[m] : slash + 1) + 2;
    }
    // The index's size, the long names' size and every member's place, then the bytes.
    const uint64_t index_size = 4 + 4 * symbols + used;
    uint64_t size = 8 + (symbols != 0 ? BUSTER_IMPORT_ARCHIVE_HEADER_BYTES + index_size + (index_size & 1) : 0) + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES + long_names + (long_names & 1);
    uint64_t* places = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, count + 1);
    result = result && places != 0;
    for (uint32_t m = 0; result && m < count; m += 1)
    {
        const uint64_t data = thin ? 0 : contents[m].length;
        places[m] = size;
        size += BUSTER_IMPORT_ARCHIVE_HEADER_BYTES + data + (data & 1);
    }
    char* bytes = result ? arena_push(arena, size, 1) : 0;
    result = result && bytes != 0 && size <= UINT32_MAX;
    if (result)
    {
        memset(bytes, '\n', size);
        memcpy(bytes, thin ? BUSTER_IMPORT_THIN_ARCHIVE_MAGIC : BUSTER_IMPORT_ARCHIVE_MAGIC, 8);
        uint64_t at = 8;
        if (symbols != 0)
        {
            foreign_test_header(bytes + at, "/", index_size);
            char* index = bytes + at + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES;
            index[0] = (char)(symbols >> 24);
            index[1] = (char)(symbols >> 16);
            index[2] = (char)(symbols >> 8);
            index[3] = (char)symbols;
            uint64_t entry = 0;
            for (uint32_t m = 0; m < count; m += 1)
            {
                for (uint32_t k = 0; k < member_symbols[m]; k += 1)
                {
                    char* field = index + 4 + 4 * entry;
                    field[0] = (char)(places[m] >> 24);
                    field[1] = (char)(places[m] >> 16);
                    field[2] = (char)(places[m] >> 8);
                    field[3] = (char)places[m];
                    entry += 1;
                }
            }
            memcpy(index + 4 + 4 * symbols, names, used);
            at += BUSTER_IMPORT_ARCHIVE_HEADER_BYTES + index_size + (index_size & 1);
        }
        foreign_test_header(bytes + at, "//", long_names);
        char* table = bytes + at + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES;
        uint64_t offset = 0;
        for (uint32_t m = 0; m < count; m += 1)
        {
            // A thin archive's member is named by its path; another by its file's name, as ar names it.
            char name[32];
            const char* slash = strrchr(members[m], '/');
            const char* member = thin || slash == 0 ? members[m] : slash + 1;
            const uint64_t length = strlen(member);
            memcpy(table + offset, member, length);
            memcpy(table + offset + length, "/\n", 2);
            snprintf(name, sizeof(name), "/%llu", (unsigned long long)offset);
            offset += length + 2;
            foreign_test_header(bytes + places[m], name, contents[m].length);
            if (!thin)
            {
                memcpy(bytes + places[m] + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES, contents[m].pointer, contents[m].length);
            }
        }
        result = foreign_test_write(path, bytes, size);
    }
    arena_pop_to(arena, mark);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The linkers
//
// With --cross-check, each case's command line is linked by GNU ld (--trace twice, -Map and -y) and by lld
// (--why-extract, -Map and -y), and what each did is compared with the resolution:
//   - the members loaded: GNU ld's trace lists each as (archive)member, lld's --why-extract as archive(member);
//   - each name defined by a loaded object: its symbol's value in the output's .symtab falls in the input section, in
//     the map, of the object and section the resolution chose (a common one's in the object's COMMON);
//   - each COMDAT group of a loaded object: kept exactly when its first section is in the map's memory map;
//   - for up to BUSTER_FOREIGN_TEST_TRACED names, -y's lines about loaded objects: a definition (a common one too) or a
//     reference per symbol of the name, a definition in a discarded group being a reference;
//   - a link that fails names each name the resolution leaves undefined, or defined twice.
// A case says which linker's result the resolution's must be (both, or one where they differ: GNU ld's common rule,
// which lld lacks; lld's back references, which GNU ld doesn't follow).

#define BUSTER_FOREIGN_TEST_TRACED 24

typedef struct ForeignTestLinked ForeignTestLinked;
struct ForeignTestLinked
{
    int status;
    String8 output;
    String8 errors;
    String8 map;
    String8 why;
    String8 executable;
};

// Links `count` arguments by `linker` ("ld.bfd" or "ld.lld") into `directory`/`name`.<linker>, with the map, the trace
// and -y for `traced` names.
static void foreign_test_link(ForeignTestLinked* linked, const char* linker, char** arguments, uint32_t count, const char* const* traced, uint32_t traced_count,
                              const char* directory, const char* name, Arena* arena)
{
    const bool bfd = strcmp(linker, "ld.bfd") == 0;
    char output[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char map[BUSTER_FOREIGN_TEST_PATH_BYTES + 16];
    char map_path[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char why[BUSTER_FOREIGN_TEST_PATH_BYTES + 16];
    char why_path[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char stdout_path[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char stderr_path[BUSTER_FOREIGN_TEST_PATH_BYTES];
    snprintf(output, sizeof(output), "%s/%s.%s", directory, name, linker);
    snprintf(map_path, sizeof(map_path), "%s/%s.%s.map", directory, name, linker);
    snprintf(why_path, sizeof(why_path), "%s/%s.%s.why", directory, name, linker);
    snprintf(stdout_path, sizeof(stdout_path), "%s/%s.%s.out", directory, name, linker);
    snprintf(stderr_path, sizeof(stderr_path), "%s/%s.%s.err", directory, name, linker);
    snprintf(map, sizeof(map), "-Map=%s", map_path);
    snprintf(why, sizeof(why), "--why-extract=%s", why_path);
    char* argv[BUSTER_FOREIGN_TEST_MAX_ARGUMENTS + 2 * BUSTER_FOREIGN_TEST_TRACED + 16];
    uint32_t at = 0;
    argv[at++] = (char*)(bfd ? "ld.bfd" : "ld.lld");
    argv[at++] = (char*)"-o";
    argv[at++] = output;
    argv[at++] = map;
    if (bfd)
    {
        argv[at++] = (char*)"--no-demangle";
        argv[at++] = (char*)"--trace";
        argv[at++] = (char*)"--trace";
    }
    else
    {
        argv[at++] = why;
    }
    for (uint32_t t = 0; t < traced_count; t += 1)
    {
        argv[at++] = (char*)"-y";
        argv[at++] = (char*)traced[t];
    }
    for (uint32_t i = 0; i < count && at < BUSTER_ARRAY_LENGTH(argv) - 1; i += 1)
    {
        argv[at++] = arguments[i];
    }
    argv[at] = 0;
    unlink(map_path);
    unlink(why_path);
    unlink(output);
    *linked = (ForeignTestLinked){ 0 };
    linked->status = object_test_run(argv, stdout_path, stderr_path);
    linked->output = foreign_test_read(arena, stdout_path);
    linked->errors = foreign_test_read(arena, stderr_path);
    linked->map = foreign_test_read(arena, map_path);
    linked->why = bfd ? (String8){ 0 } : foreign_test_read(arena, why_path);
    linked->executable = linked->status == 0 ? foreign_test_read(arena, output) : (String8){ 0 };
}

// Text's lines: the next line from *at, without its newline; false at the end.
static bool foreign_test_line(String8 text, uint64_t* at, String8* line)
{
    const bool result = text.pointer != 0 && *at < text.length;
    if (result)
    {
        const char* start = text.pointer + *at;
        const char* end = memchr(start, '\n', text.length - *at);
        const uint64_t length = end != 0 ? (uint64_t)(end - start) : text.length - *at;
        line->pointer = (char*)start;
        line->length = length;
        *at += length + (end != 0 ? 1 : 0);
    }
    return result;
}

static bool foreign_test_equal(String8 string, const char* text)
{
    return string.length == strlen(text) && memcmp(string.pointer, text, string.length) == 0;
}

// Whether `line` is in `text` as a line of its own.
static bool foreign_test_has_line(String8 text, const char* line)
{
    uint64_t at = 0;
    String8 next;
    bool result = false;
    while (!result && foreign_test_line(text, &at, &next))
    {
        result = foreign_test_equal(next, line);
    }
    return result;
}

// Whether a member line (archive(member)) is in a linker's list of the members it loaded: GNU ld's trace, whose lines
// are (archive)member, or lld's --why-extract, whose second field is archive(member).
static bool foreign_test_linker_loaded(const ForeignTestLinked* linked, bool bfd, const char* member)
{
    uint64_t at = 0;
    String8 line;
    bool result = false;
    char text[BUSTER_FOREIGN_TEST_PATH_BYTES * 2];
    while (!result && foreign_test_line(bfd ? linked->output : linked->why, &at, &line))
    {
        if (bfd && line.length > 2 && line.pointer[0] == '(')
        {
            const char* close = memchr(line.pointer, ')', line.length);
            if (close != 0)
            {
                const uint64_t archive = (uint64_t)(close - line.pointer - 1);
                snprintf(text, sizeof(text), "%.*s(%.*s)", (int)archive, line.pointer + 1, (int)(line.length - archive - 2), close + 1);
                result = strcmp(text, member) == 0;
            }
        }
        else if (!bfd)
        {
            const char* tab = memchr(line.pointer, '\t', line.length);
            const char* second = tab != 0 ? memchr(tab + 1, '\t', line.length - (uint64_t)(tab + 1 - line.pointer)) : 0;
            if (second != 0)
            {
                snprintf(text, sizeof(text), "%.*s", (int)(second - tab - 1), tab + 1);
                result = strcmp(text, member) == 0;
            }
        }
    }
    return result;
}

// How many members a linker loaded.
static uint32_t foreign_test_linker_member_count(const ForeignTestLinked* linked, bool bfd)
{
    uint64_t at = 0;
    String8 line;
    uint32_t result = 0;
    bool header = !bfd;
    while (foreign_test_line(bfd ? linked->output : linked->why, &at, &line))
    {
        result += bfd ? (line.length > 2 && line.pointer[0] == '(' ? 1 : 0) : (header || line.length == 0 ? 0 : 1);
        header = false;
    }
    return result;
}

// An input section in a map: its object's name (a path, or archive(member)), its name, and where it is in the output.
typedef struct ForeignTestRange ForeignTestRange;
struct ForeignTestRange
{
    String8 file;
    String8 section;
    uint64_t start;
    uint64_t size;
};

static bool foreign_test_hex(String8 token, uint64_t* value)
{
    bool result = token.length > 2 && token.pointer[0] == '0' && token.pointer[1] == 'x';
    uint64_t number = 0;
    for (uint64_t i = 2; result && i < token.length; i += 1)
    {
        const char c = token.pointer[i];
        const bool digit = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        result = digit;
        number = number * 16 + (uint64_t)(c >= 'a' ? c - 'a' + 10 : c - '0');
    }
    *value = number;
    return result;
}

static bool foreign_test_plain_hex(String8 token, uint64_t* value)
{
    bool result = token.length != 0;
    uint64_t number = 0;
    for (uint64_t i = 0; result && i < token.length; i += 1)
    {
        const char c = token.pointer[i];
        result = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        number = number * 16 + (uint64_t)(c >= 'a' ? c - 'a' + 10 : c - '0');
    }
    *value = number;
    return result;
}

// The whitespace-separated tokens of a line, up to `capacity`.
static uint32_t foreign_test_tokens(String8 line, String8* tokens, uint32_t capacity)
{
    uint32_t count = 0;
    uint64_t i = 0;
    while (i < line.length && count < capacity)
    {
        while (i < line.length && line.pointer[i] == ' ')
        {
            i += 1;
        }
        const uint64_t start = i;
        while (i < line.length && line.pointer[i] != ' ')
        {
            i += 1;
        }
        if (i > start)
        {
            tokens[count] = (String8){
                .pointer = line.pointer + start,
                .length = i - start,
            };
            count += 1;
        }
    }
    return count;
}

// A map's input sections. GNU ld's memory map (after "Linker script and memory map") has a line per input section, a
// space, its name, its address, size and object, the name alone on a line of its own when it is long and the rest on
// the next; lld's has VMA, LMA, size and alignment columns, then the input section indented by 8 as object:(name).
static uint32_t foreign_test_ranges(String8 map, bool bfd, ForeignTestRange* ranges, uint32_t capacity)
{
    uint64_t at = 0;
    String8 line;
    uint32_t count = 0;
    bool memory = !bfd;
    String8 pending = { 0 };
    while (foreign_test_line(map, &at, &line) && count < capacity)
    {
        String8 tokens[8];
        if (bfd && foreign_test_equal(line, "Linker script and memory map"))
        {
            memory = true;
        }
        else if (bfd && memory)
        {
            const uint32_t n = foreign_test_tokens(line, tokens, 8);
            const bool section_line = line.length > 1 && line.pointer[0] == ' ' && line.pointer[1] != ' ' && line.pointer[1] != '*';
            uint64_t start = 0;
            uint64_t size = 0;
            if (section_line && n == 1)
            {
                pending = tokens[0];
            }
            else if (section_line && n == 4 && foreign_test_hex(tokens[1], &start) && foreign_test_hex(tokens[2], &size))
            {
                ranges[count] = (ForeignTestRange){ tokens[3], tokens[0], start, size };
                count += 1;
                pending = (String8){ 0 };
            }
            else if (!section_line && pending.length != 0 && n == 3 && foreign_test_hex(tokens[0], &start) && foreign_test_hex(tokens[1], &size))
            {
                ranges[count] = (ForeignTestRange){ tokens[2], pending, start, size };
                count += 1;
                pending = (String8){ 0 };
            }
            else
            {
                pending = (String8){ 0 };
            }
        }
        else if (!bfd)
        {
            // The text after the four columns, 8 spaces in for an input section.
            const uint32_t n = foreign_test_tokens(line, tokens, 8);
            uint64_t start = 0;
            uint64_t size = 0;
            const char* text = n >= 5 ? tokens[4].pointer : 0;
            const uint64_t indent = text != 0 ? (uint64_t)(text - (tokens[3].pointer + tokens[3].length)) - 1 : 0;
            if (n == 5 && indent == 8 && foreign_test_plain_hex(tokens[0], &start) && foreign_test_plain_hex(tokens[2], &size))
            {
                const String8 input = tokens[4];
                const char* colon = 0;
                for (uint64_t i = 0; i + 1 < input.length; i += 1)
                {
                    colon = input.pointer[i] == ':' && input.pointer[i + 1] == '(' ? input.pointer + i : colon;
                }
                if (colon != 0 && input.pointer[input.length - 1] == ')')
                {
                    const uint64_t file = (uint64_t)(colon - input.pointer);
                    ranges[count] = (ForeignTestRange){
                        .file = { input.pointer, file },
                        .section = { (char*)colon + 2, input.length - file - 3 },
                        .start = start,
                        .size = size,
                    };
                    count += 1;
                }
            }
        }
    }
    return count;
}

// A global or weak symbol's address in an executable, from its .symtab: false if it has none of that name. A TLS symbol's
// value is its offset in the TLS segment, whose address (PT_TLS's) it adds.
static bool foreign_test_symbol_value(String8 executable, const char* name, uint64_t* value)
{
    Elf64_Ehdr header;
    bool result = false;
    uint64_t tls = 0;
    if (executable.length >= sizeof(header))
    {
        memcpy(&header, executable.pointer, sizeof(header));
        for (uint32_t p = 0; header.e_phoff != 0 && p < header.e_phnum && header.e_phoff + ((uint64_t)p + 1) * sizeof(Elf64_Phdr) <= executable.length; p += 1)
        {
            Elf64_Phdr segment;
            memcpy(&segment, executable.pointer + header.e_phoff + (uint64_t)p * sizeof(Elf64_Phdr), sizeof(segment));
            tls = segment.p_type == PT_TLS ? segment.p_vaddr : tls;
        }
        for (uint32_t s = 0; !result && header.e_shoff != 0 && s < header.e_shnum && header.e_shoff + ((uint64_t)s + 1) * sizeof(Elf64_Shdr) <= executable.length; s += 1)
        {
            Elf64_Shdr table;
            memcpy(&table, executable.pointer + header.e_shoff + (uint64_t)s * sizeof(Elf64_Shdr), sizeof(table));
            Elf64_Shdr strings = { 0 };
            if (table.sh_type == SHT_SYMTAB && table.sh_link < header.e_shnum)
            {
                memcpy(&strings, executable.pointer + header.e_shoff + (uint64_t)table.sh_link * sizeof(Elf64_Shdr), sizeof(strings));
            }
            const uint64_t count = table.sh_type == SHT_SYMTAB && table.sh_offset + table.sh_size <= executable.length ? table.sh_size / sizeof(Elf64_Sym) : 0;
            for (uint64_t i = 0; !result && i < count; i += 1)
            {
                Elf64_Sym symbol;
                memcpy(&symbol, executable.pointer + table.sh_offset + i * sizeof(Elf64_Sym), sizeof(symbol));
                const uint32_t binding = ELF64_ST_BIND(symbol.st_info);
                const bool named = symbol.st_name < strings.sh_size && strings.sh_offset + strings.sh_size <= executable.length &&
                                   strcmp(executable.pointer + strings.sh_offset + symbol.st_name, name) == 0;
                result = named && (binding == STB_GLOBAL || binding == STB_WEAK || binding == STB_GNU_UNIQUE) && symbol.st_shndx != SHN_UNDEF;
                *value = symbol.st_value + (ELF64_ST_TYPE(symbol.st_info) == STT_TLS ? tls : 0);
            }
        }
    }
    return result;
}

// Whether the map has an input section of `file` named `section` holding `value` (its start up to its end).
static bool foreign_test_in_range(const ForeignTestRange* ranges, uint32_t count, const char* file, String8 section, uint64_t value)
{
    bool result = false;
    for (uint32_t r = 0; !result && r < count; r += 1)
    {
        const ForeignTestRange* range = &ranges[r];
        result = foreign_test_equal(range->file, file) && range->section.length == section.length && memcmp(range->section.pointer, section.pointer, section.length) == 0 &&
                 value >= range->start && value <= range->start + range->size;
    }
    return result;
}

static bool foreign_test_has_section(const ForeignTestRange* ranges, uint32_t count, const char* file, String8 section)
{
    bool result = false;
    for (uint32_t r = 0; !result && r < count; r += 1)
    {
        result = foreign_test_equal(ranges[r].file, file) && ranges[r].section.length == section.length && memcmp(ranges[r].section.pointer, section.pointer, section.length) == 0;
    }
    return result;
}

// The columns of a global symbol, and its place in them.
static const ForeignSymbols* foreign_test_symbols(const Foreign* foreign, uint32_t symbol, uint32_t* at)
{
    const ForeignRound* round = foreign_symbol_round(foreign, symbol);
    *at = symbol - round->first_symbol;
    return &round->symbols;
}

// Whether a symbol's definition was discarded with its group.
static bool foreign_test_discarded(const Foreign* foreign, uint32_t symbol)
{
    uint32_t at = 0;
    const ForeignSymbols* symbols = foreign_test_symbols(foreign, symbol, &at);
    const ForeignObject* object = &foreign->objects[foreign_symbol_object(foreign, symbol)];
    const uint32_t section = symbols->section[at];
    return object->discarded != 0 && section != SHN_UNDEF && section < object->elf.section_count && object->discarded[section] != 0;
}

// The names to trace with -y: those with symbols in two loaded objects or more, but the linker's, up to
// BUSTER_FOREIGN_TEST_TRACED, by id.
static uint32_t foreign_test_traced(const Foreign* foreign, const char** traced, char* storage, uint64_t storage_capacity)
{
    uint32_t count = 0;
    uint64_t used = 0;
    Scratch scratch = scratch_begin(0);
    uint32_t* seen = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, (uint64_t)foreign->names.count + 1);
    if (seen != 0)
    {
        memset(seen, 0, ((uint64_t)foreign->names.count + 1) * sizeof(uint32_t));
        for (uint32_t r = 0; r < foreign->round_count; r += 1)
        {
            const ForeignRound* round = &foreign->rounds[r];
            for (uint32_t s = 0; s < round->symbol_count; s += 1)
            {
                seen[round->symbols.name_id[s]] += 1;
            }
        }
        for (uint32_t n = 0; n < foreign->names.count && count < BUSTER_FOREIGN_TEST_TRACED; n += 1)
        {
            // GNU ld credits the first input with a name it defines itself (_GLOBAL_OFFSET_TABLE_).
            const bool linker = runtime_linker_name(RUNTIME_KIND_STATIC, foreign->names.bytes[n], foreign->names.length[n]) ||
                                runtime_linker_name(RUNTIME_KIND_DYNAMIC, foreign->names.bytes[n], foreign->names.length[n]);
            if (seen[n] >= 2 && !linker && used + foreign->names.length[n] + 1 <= storage_capacity)
            {
                memcpy(storage + used, foreign->names.bytes[n], foreign->names.length[n]);
                storage[used + foreign->names.length[n]] = 0;
                traced[count] = storage + used;
                used += foreign->names.length[n] + 1;
                count += 1;
            }
        }
    }
    scratch_end(scratch);
    return count;
}

// Whether -y's lines in a linker's output about a name are the resolution's. The resolution's: for each loaded object's
// symbol of the name, "object: definition of NAME" or "object: reference to NAME" (a definition in a discarded group
// being a reference), but the command line's (-u). GNU ld prints a line for every symbol of the name in every object
// it loads, after its own path and ": ", so its lines about loaded objects must be the resolution's, every one. lld
// prints a definition only when it takes the name's symbol over (and "common definition" for a common one), so its
// lines about loaded objects must each be one of the resolution's. Lines about other files (shared libraries, lld's
// lazy definitions of members it doesn't load) aren't about loaded objects.
#define BUSTER_FOREIGN_TEST_TRACE_LINES 256

static bool foreign_test_same_trace(const Foreign* foreign, const ForeignTestLinked* linked, bool bfd, const char* traced, Arena* arena)
{
    const uint64_t mark = arena->length;
    char object[BUSTER_FOREIGN_TEST_PATH_BYTES];
    ForeignTestPath* ours = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignTestPath, BUSTER_FOREIGN_TEST_TRACE_LINES);
    bool* matched = BUSTER_ARENA_PUSH_ARRAY(arena, bool, BUSTER_FOREIGN_TEST_TRACE_LINES);
    uint32_t our_count = 0;
    bool result = ours != 0 && matched != 0;
    const uint32_t name = foreign_test_name(foreign, traced);
    // The earliest regular definition of the name (strong or weak, not common): GNU ld skips a weak definition after
    // one, before it would trace it, so that one's line is the resolution's but GNU ld needn't print it.
    uint64_t earliest = BUSTER_FOREIGN_NO_POSITION;
    for (uint32_t r = 0; name != 0 && r < foreign->round_count; r += 1)
    {
        const ForeignRound* round = &foreign->rounds[r];
        for (uint32_t s = 0; s < round->symbol_count; s += 1)
        {
            const uint32_t symbol = round->first_symbol + s;
            const uint32_t section = round->symbols.section[s];
            const bool regular = section != SHN_UNDEF && section != BUSTER_FOREIGN_SECTION_COMMON && !foreign_test_discarded(foreign, symbol);
            const uint64_t position = foreign->objects[foreign_symbol_object(foreign, symbol)].position;
            earliest = round->symbols.name_id[s] == name - 1 && regular && position < earliest ? position : earliest;
        }
    }
    for (uint32_t r = 0; result && name != 0 && r < foreign->round_count; r += 1)
    {
        const ForeignRound* round = &foreign->rounds[r];
        for (uint32_t s = 0; result && s < round->symbol_count; s += 1)
        {
            const uint32_t symbol = round->first_symbol + s;
            const uint32_t id = foreign_symbol_object(foreign, symbol);
            if (round->symbols.name_id[s] == name - 1 && id != 0 && our_count < BUSTER_FOREIGN_TEST_TRACE_LINES)
            {
                const bool definition = round->symbols.section[s] != SHN_UNDEF && !foreign_test_discarded(foreign, symbol);
                const bool weak = ELF64_ST_BIND(round->symbols.info[s]) == STB_WEAK && round->symbols.section[s] != BUSTER_FOREIGN_SECTION_COMMON;
                foreign_test_linker_name(foreign, id, bfd, object, sizeof(object));
                snprintf(ours[our_count], sizeof(ours[0]), "%s: %s %s", object, definition ? "definition of" : "reference to", traced);
                matched[our_count] = bfd && definition && weak && foreign->objects[id].position > earliest;
                our_count += 1;
            }
        }
    }
    // Their lines, from both outputs: each about one of the resolution's objects must be one of its lines.
    const String8 texts[2] = {
        linked->output,
        linked->errors,
    };
    char line[BUSTER_FOREIGN_TEST_PATH_BYTES * 2];
    for (uint32_t t = 0; result && t < 2; t += 1)
    {
        uint64_t at = 0;
        String8 next;
        while (result && foreign_test_line(texts[t], &at, &next))
        {
            // GNU ld's lines start with its own path and ": ".
            String8 rest = next;
            const char* colon = bfd ? memchr(next.pointer, ':', next.length) : 0;
            if (bfd && colon != 0 && (uint64_t)(colon - next.pointer) + 2 <= next.length && colon[1] == ' ')
            {
                rest.pointer = (char*)colon + 2;
                rest.length = next.length - (uint64_t)(colon - next.pointer) - 2;
            }
            snprintf(line, sizeof(line), "%.*s", (int)rest.length, rest.pointer);
            char* common = strstr(line, ": common definition of ");
            if (common != 0)
            {
                memmove(common + 2, common + 9, strlen(common + 9) + 1);
            }
            const char* kind = strstr(line, ": definition of ");
            kind = kind != 0 ? kind : strstr(line, ": reference to ");
            const uint64_t file_length = kind != 0 ? (uint64_t)(kind - line) : 0;
            const char* traced_name = kind != 0 ? strchr(kind + 2, ' ') : 0;
            traced_name = traced_name != 0 ? strchr(traced_name + 1, ' ') : 0;
            const bool about = kind != 0 && traced_name != 0 && strcmp(traced_name + 1, traced) == 0;
            // About one of the resolution's objects: a line of the resolution's for some symbol of it.
            bool ours_object = false;
            for (uint32_t i = 0; about && i < our_count; i += 1)
            {
                ours_object = ours_object || (strncmp(ours[i], line, file_length) == 0 && ours[i][file_length] == ':');
            }
            bool found = !about || !ours_object;
            for (uint32_t i = 0; !found && i < our_count; i += 1)
            {
                found = strcmp(ours[i], line) == 0;
                matched[i] = matched[i] || found;
            }
            result = found;
        }
    }
    // GNU ld's lines are every one of the resolution's.
    for (uint32_t i = 0; result && bfd && i < our_count; i += 1)
    {
        result = matched[i];
    }
    arena_pop_to(arena, mark);
    return result;
}

// Whether a section of an object is SHF_MERGE: linkers merge such sections' contents, so a symbol in one is in no input
// section of the map.
static bool foreign_test_merged(const Foreign* foreign, uint32_t id, uint32_t section)
{
    const ForeignObject* object = &foreign->objects[id];
    return section != SHN_UNDEF && section < object->elf.section_count && (import_section_header(&object->elf, section).sh_flags & SHF_MERGE) != 0;
}

// The cross-check of one case: the linkers' links of `linker_arguments`, against `foreign`'s resolution. With `bfd` and
// `lld`, the linker's result must be the resolution's; a linker the case leaves out is still run, and its differences
// are counted apart, quietly: they are the documented ones, and the count shows they still are.
static void foreign_test_against(ForeignTestTally* tally, const char* what, const Foreign* foreign, char** linker_arguments, uint32_t count, bool bfd, bool lld,
                                 const char* directory, Arena* arena)
{
    const uint64_t mark = arena->length;
    const char* traced[BUSTER_FOREIGN_TEST_TRACED];
    char storage[BUSTER_FOREIGN_TEST_TRACED * 256];
    const uint32_t traced_count = foreign_test_traced(foreign, traced, storage, sizeof(storage));
    char name[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char detail[BUSTER_FOREIGN_TEST_PATH_BYTES * 2];
    // A name the resolution leaves undefined, but those the linkers define (runtime_linker_name), or defines twice.
    char undefined[512] = "";
    bool duplicate = false;
    for (uint32_t n = 0; n < foreign->names.count; n += 1)
    {
        const bool linker_name = runtime_linker_name(RUNTIME_KIND_STATIC, foreign->names.bytes[n], foreign->names.length[n]) ||
                                 runtime_linker_name(RUNTIME_KIND_DYNAMIC, foreign->names.bytes[n], foreign->names.length[n]);
        if (foreign->names.resolution[n] == FOREIGN_RESOLUTION_UNDEFINED && !linker_name && undefined[0] == 0)
        {
            snprintf(undefined, sizeof(undefined), "%.*s", (int)foreign->names.length[n], foreign->names.bytes[n]);
        }
        duplicate = duplicate || foreign->names.definitions[n] > 1;
    }
    for (uint32_t which = 0; which < 2; which += 1)
    {
        const bool is_bfd = which == 0;
        const bool counted = is_bfd ? bfd : lld;
        ForeignTestTally local = {
            .check = tally->check,
            .quiet = true,
        };
        ForeignTestTally* checks = counted ? tally : &local;
        ForeignTestLinked linked;
        char case_name[64];
        snprintf(case_name, sizeof(case_name), "case%llu", (unsigned long long)(tally->checked + which));
        foreign_test_link(&linked, is_bfd ? "ld.bfd" : "ld.lld", linker_arguments, count, traced, traced_count, directory, case_name, arena);
        const char* linker = is_bfd ? "GNU ld" : "lld";
        if (linked.status != 0)
        {
            // A link that fails must be one whose resolution leaves a name undefined or defines one twice.
            snprintf(detail, sizeof(detail), "%s fails (%.*s) where the resolution has no undefined or duplicate name", linker,
                     (int)(linked.errors.length < 300 ? linked.errors.length : 300), linked.errors.pointer != 0 ? linked.errors.pointer : "");
            foreign_test_expect(checks, undefined[0] != 0 || duplicate, what, detail);
        }
        else
        {
            snprintf(detail, sizeof(detail), "%s links what the resolution leaves undefined (%s) or defines twice (%s)", linker, undefined, duplicate ? "yes" : "no");
            foreign_test_expect(checks, undefined[0] == 0 && !duplicate, what, detail);
            // The members: GNU ld traces a whole archive's too, and names a thin archive's by its path alone; lld's
            // --why-extract lists the members references extracted.
            uint32_t members = 0;
            for (uint32_t id = 1; id < foreign->object_count; id += 1)
            {
                const ForeignObject* object = &foreign->objects[id];
                const bool whole = object->member != 0 && foreign->archives[foreign->inputs[object->input].item].whole;
                const bool thin = foreign_test_thin_member(foreign, id);
                if (object->member != 0 && (is_bfd || !whole))
                {
                    foreign_test_linker_name(foreign, id, is_bfd, name, sizeof(name));
                    const bool loaded = is_bfd && thin ? foreign_test_has_line(linked.output, name) : foreign_test_linker_loaded(&linked, is_bfd, name);
                    snprintf(detail, sizeof(detail), "%s doesn't load %s", linker, name);
                    foreign_test_expect(checks, loaded, what, detail);
                    members += is_bfd && thin ? 0 : 1;
                }
            }
            snprintf(detail, sizeof(detail), "%s loads %u members, the resolution %u", linker, foreign_test_linker_member_count(&linked, is_bfd), members);
            foreign_test_expect(checks, foreign_test_linker_member_count(&linked, is_bfd) == members, what, detail);
            // The winners, by where their symbols are in the output.
            ForeignTestRange* ranges = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignTestRange, 1u << 20);
            const uint32_t range_count = ranges != 0 ? foreign_test_ranges(linked.map, is_bfd, ranges, 1u << 20) : 0;
            for (uint32_t n = 0; ranges != 0 && n < foreign->names.count; n += 1)
            {
                const uint8_t resolution = foreign->names.resolution[n];
                if (resolution == FOREIGN_RESOLUTION_STRONG || resolution == FOREIGN_RESOLUTION_COMMON || resolution == FOREIGN_RESOLUTION_WEAK)
                {
                    const uint32_t symbol = foreign->names.symbol[n];
                    const uint32_t id = foreign_symbol_object(foreign, symbol);
                    uint32_t at = 0;
                    const ForeignSymbols* symbols = foreign_test_symbols(foreign, symbol, &at);
                    const uint32_t section = symbols->section[at];
                    const String8 section_name = foreign_test_section_name(foreign, id, section);
                    char text[1024];
                    snprintf(text, sizeof(text), "%.*s", (int)foreign->names.length[n], foreign->names.bytes[n]);
                    uint64_t value = 0;
                    foreign_test_linker_name(foreign, id, is_bfd, name, sizeof(name));
                    if (section != BUSTER_FOREIGN_SECTION_ABS && !foreign_test_merged(foreign, id, section) && foreign_test_symbol_value(linked.executable, text, &value))
                    {
                        snprintf(detail, sizeof(detail), "%s puts %s (0x%llx) outside %s's %.*s", linker, text, (unsigned long long)value, name, (int)section_name.length,
                                 section_name.pointer);
                        foreign_test_expect(checks, foreign_test_in_range(ranges, range_count, name, section_name, value), what, detail);
                    }
                }
            }
            // The groups.
            for (uint32_t id = 1; ranges != 0 && id < foreign->object_count; id += 1)
            {
                const ForeignObject* object = &foreign->objects[id];
                const ForeignRound* round = object->group_count != 0 ? foreign_group_round(foreign, object->group_begin) : 0;
                foreign_test_linker_name(foreign, id, is_bfd, name, sizeof(name));
                for (uint32_t g = 0; g < object->group_count; g += 1)
                {
                    const ImportGroup* group = &object->group_list[g];
                    const bool kept = round->groups.kept[object->group_begin - round->first_group + g] != 0;
                    const String8 first = group->count != 0 ? foreign_test_section_name(foreign, id, object->group_sections[group->first]) : BUSTER_S8("");
                    snprintf(detail, sizeof(detail), "%s %s %s's group %.*s", linker, kept ? "drops" : "keeps", name, (int)group->signature.length, group->signature.pointer);
                    foreign_test_expect(checks, group->count == 0 || foreign_test_has_section(ranges, range_count, name, first) == kept, what, detail);
                }
            }
            // -y's lines.
            for (uint32_t t = 0; t < traced_count; t += 1)
            {
                snprintf(detail, sizeof(detail), "%s's -y %s isn't the resolution's", linker, traced[t]);
                foreign_test_expect(checks, foreign_test_same_trace(foreign, &linked, is_bfd, traced[t], arena), what, detail);
            }
        }
        if (!counted)
        {
            printf("  %s: %s: %s differs, as documented (%llu differences)\n", tally->check, what, linker, (unsigned long long)local.failures);
        }
    }
    arena_pop_to(arena, mark);
}

// ---------------------------------------------------------------------------------------------------------------------
// Generated programs
//
// test-link's sets of options, each program written as objects (one per unit, by the object writer) and resolved four
// ways: every unit an object on the command line; the units after the first in an archive after it; split between an
// archive and a thin archive; and an archive before the objects, which needs lld's back references. Each resolution's
// every name must be defined (but libc's, for the sets that call it, which bind to libc.so.6), every unit's object
// loaded where an archive holds it and something refers to it, and with --cross-check, the linkers must agree: the
// static programs linked -static -e _start, those that call libc as a PIE against the system's crt objects and libc.

typedef struct ForeignTestSystem ForeignTestSystem;
struct ForeignTestSystem
{
    // The crt objects (Scrt1.o, crti.o, crtbeginS.o, crtendS.o, crtn.o) and the -L directories GCC's driver searches.
    char objects[RUNTIME_OBJECTS][BUSTER_FOREIGN_TEST_PATH_BYTES];
    char directories[3][BUSTER_FOREIGN_TEST_PATH_BYTES + 2];
    bool found;
};

// The system's C runtime, as runtime.c finds it.
static void foreign_test_system(ForeignTestSystem* system)
{
    Runtime runtime;
    *system = (ForeignTestSystem){ 0 };
    if (runtime_import(&runtime) == RUNTIME_ERROR_NONE)
    {
        for (uint32_t i = 0; i < RUNTIME_OBJECTS; i += 1)
        {
            snprintf(system->objects[i], sizeof(system->objects[i]), "%.*s", (int)runtime.object_paths[i].length, runtime.object_paths[i].pointer);
        }
        const String8 libc = foreign_directory(runtime.libc_script);
        snprintf(system->directories[0], sizeof(system->directories[0]), "-L%.*s", (int)runtime.gcc_directory.length, runtime.gcc_directory.pointer);
        snprintf(system->directories[1], sizeof(system->directories[1]), "-L%.*s", (int)libc.length, libc.pointer);
        snprintf(system->directories[2], sizeof(system->directories[2]), "-L/lib");
        system->found = true;
    }
    runtime_release(&runtime);
}

// A command line: the arguments for the resolution, and the linkers' (the same with their own options first).
typedef struct ForeignTestLine ForeignTestLine;
struct ForeignTestLine
{
    char* ours[BUSTER_FOREIGN_TEST_MAX_ARGUMENTS];
    uint32_t our_count;
    char* theirs[BUSTER_FOREIGN_TEST_MAX_ARGUMENTS];
    uint32_t their_count;
};

static void foreign_test_add(ForeignTestLine* line, char* argument, bool ours, bool theirs)
{
    if (ours && line->our_count < BUSTER_FOREIGN_TEST_MAX_ARGUMENTS)
    {
        line->ours[line->our_count] = argument;
        line->our_count += 1;
    }
    if (theirs && line->their_count < BUSTER_FOREIGN_TEST_MAX_ARGUMENTS)
    {
        line->theirs[line->their_count] = argument;
        line->their_count += 1;
    }
}

// GCC's driver's line around a PIE's inputs: the linkers' options and the crt objects before them...
static void foreign_test_pie_begin(ForeignTestLine* line, ForeignTestSystem* system)
{
    foreign_test_add(line, (char*)"-pie", false, true);
    foreign_test_add(line, (char*)"-dynamic-linker", false, true);
    foreign_test_add(line, (char*)"/lib64/ld-linux-x86-64.so.2", false, true);
    for (uint32_t i = 0; i < RUNTIME_OBJECT_END; i += 1)
    {
        foreign_test_add(line, system->objects[i], true, true);
    }
}

// ... and libgcc, libc and the crt objects after them.
static void foreign_test_pie_end(ForeignTestLine* line, ForeignTestSystem* system)
{
    static char* const libraries[] = {
        "-lgcc",
        "--as-needed",
        "-lgcc_s",
        "--no-as-needed",
        "-lc",
        "-lgcc",
        "--as-needed",
        "-lgcc_s",
        "--no-as-needed",
    };
    for (uint32_t d = 0; d < 3; d += 1)
    {
        foreign_test_add(line, system->directories[d], true, true);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(libraries); i += 1)
    {
        foreign_test_add(line, libraries[i], true, true);
    }
    for (uint32_t i = RUNTIME_OBJECT_END; i < RUNTIME_OBJECTS; i += 1)
    {
        foreign_test_add(line, system->objects[i], true, true);
    }
}

// Writes each unit's object of a generated program into `directory` as <name>_u<unit>.o.
static bool foreign_test_write_program(const Link* link, uint32_t unit_count, const char* directory, const char* name, ForeignTestPath* paths, Arena* arena)
{
    const uint64_t length = arena->length;
    ObjectUnits units;
    ObjectSymbols symbols;
    bool result = object_prepare(&units, link, unit_count, arena) && object_map_symbols(&symbols, link, arena);
    for (uint32_t unit = 0; result && unit < unit_count; unit += 1)
    {
        snprintf(paths[unit], BUSTER_FOREIGN_TEST_PATH_BYTES, "%s/%s_u%u.o", directory, name, unit);
        const uint64_t before = arena->length;
        char source[32];
        const ObjectOptions options = { 0 };
        const Object object = object_build(link, &units, &symbols, unit, options, object_test_source_name(source, sizeof(source), unit), arena);
        result = object.error == OBJECT_ERROR_NONE && object_write_file(paths[unit], object.bytes, object.size) == OBJECT_ERROR_NONE;
        arena_pop_to(arena, before);
    }
    arena_pop_to(arena, length);
    return result;
}

// The most units of a program the test writes: test-link's sets have 16 at most.
#define BUSTER_FOREIGN_TEST_MAX_UNITS 64

// The ways a generated program's units go on the command line.
typedef enum ForeignTestLayout
{
    FOREIGN_TEST_OBJECTS,
    FOREIGN_TEST_ARCHIVE,
    FOREIGN_TEST_THIN,
    FOREIGN_TEST_BACK,
    FOREIGN_TEST_LAYOUTS,
} ForeignTestLayout;

static const char* const foreign_test_layout_names[FOREIGN_TEST_LAYOUTS] = {
    [FOREIGN_TEST_OBJECTS] = "objects",
    [FOREIGN_TEST_ARCHIVE] = "an archive after the first unit",
    [FOREIGN_TEST_THIN] = "an archive and a thin archive",
    [FOREIGN_TEST_BACK] = "an archive before the first unit",
};

// The checks every generated program's resolution passes: no failure, every name defined but libc's (bound to a shared
// library), the weak references to nothing the programs and the crt objects have, and the names the linker defines
// (runtime_linker_name: the arrays' bounds, _GLOBAL_OFFSET_TABLE_), which the layout will; and no duplicate.
static void foreign_test_program_checks(ForeignTestTally* tally, const char* what, const Foreign* foreign, bool libc)
{
    char detail[256];
    snprintf(detail, sizeof(detail), "fails: %s", foreign_test_errors[foreign->failure.error]);
    foreign_test_expect(tally, foreign->failure.error == FOREIGN_ERROR_NONE, what, detail);
    for (uint32_t n = 0; foreign->failure.error == FOREIGN_ERROR_NONE && n < foreign->names.count; n += 1)
    {
        const uint8_t resolution = foreign->names.resolution[n];
        const bool linker = runtime_linker_name(libc ? RUNTIME_KIND_DYNAMIC : RUNTIME_KIND_STATIC, foreign->names.bytes[n], foreign->names.length[n]);
        const bool allowed = resolution == FOREIGN_RESOLUTION_STRONG || resolution == FOREIGN_RESOLUTION_COMMON || resolution == FOREIGN_RESOLUTION_WEAK ||
                             resolution == FOREIGN_RESOLUTION_WEAK_UNDEFINED || (libc && resolution == FOREIGN_RESOLUTION_SHARED) ||
                             (resolution == FOREIGN_RESOLUTION_UNDEFINED && linker);
        snprintf(detail, sizeof(detail), "%.*s is %s", (int)foreign->names.length[n], foreign->names.bytes[n], foreign_test_resolutions[resolution]);
        foreign_test_expect(tally, allowed && foreign->names.definitions[n] <= 1, what, detail);
    }
}

static bool foreign_test_programs(uint64_t seed, uint32_t functions, bool cross_check, const char* directory, ForeignTestSystem* system, Arena* arena)
{
    ForeignTestTally tally = {
        .check = "programs",
    };
    LinkProgramOptions options[BUSTER_LINK_TEST_PROGRAMS];
    link_test_program_options(options, seed, functions);
    static char archives[3][BUSTER_FOREIGN_TEST_PATH_BYTES];
    ForeignTestPath* paths = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignTestPath, BUSTER_FOREIGN_TEST_MAX_UNITS);
    ForeignTestLine* line = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignTestLine, 1);
    for (uint32_t i = 0; paths != 0 && line != 0 && i < BUSTER_ARRAY_LENGTH(options); i += 1)
    {
        LinkProgram program;
        const bool generated = link_program_generate(&program, options[i]);
        const uint32_t units = options[i].units < BUSTER_FOREIGN_TEST_MAX_UNITS ? options[i].units : BUSTER_FOREIGN_TEST_MAX_UNITS;
        char name[32];
        snprintf(name, sizeof(name), "p%u", i);
        const bool written = generated && foreign_test_write_program(&program.link, units, directory, name, paths, arena);
        foreign_test_expect(&tally, written, link_test_program_names[i], "the program's objects written");
        const bool libc = options[i].libc;
        for (uint32_t layout = 0; written && (!libc || system->found) && layout < FOREIGN_TEST_LAYOUTS; layout += 1)
        {
            char what[256];
            snprintf(what, sizeof(what), "%s, %s", link_test_program_names[i], foreign_test_layout_names[layout]);
            *line = (ForeignTestLine){ 0 };
            if (libc)
            {
                foreign_test_pie_begin(line, system);
            }
            else
            {
                foreign_test_add(line, (char*)"-static", false, true);
                foreign_test_add(line, (char*)"-e", false, true);
                foreign_test_add(line, (char*)"_start", false, true);
            }
            // The archives: the units after the first, all in one, or split in half, the second half in a thin one.
            const uint32_t split = layout == FOREIGN_TEST_THIN ? 1 + (units - 1) / 2 : units;
            snprintf(archives[0], sizeof(archives[0]), "%s/%s_%u.a", directory, name, layout);
            snprintf(archives[1], sizeof(archives[1]), "%s/%s_%u_thin.a", directory, name, layout);
            char* members[BUSTER_FOREIGN_TEST_MAX_UNITS];
            for (uint32_t u = 0; u < units; u += 1)
            {
                members[u] = paths[u];
            }
            bool made = true;
            if (layout != FOREIGN_TEST_OBJECTS && units > 1)
            {
                made = foreign_test_write_archive(archives[0], members + 1, split - 1, false, arena);
                made = made && (split == units || foreign_test_write_archive(archives[1], members + split, units - split, true, arena));
            }
            foreign_test_expect(&tally, made, what, "the archives written");
            if (layout == FOREIGN_TEST_BACK && units > 1)
            {
                foreign_test_add(line, archives[0], true, true);
            }
            foreign_test_add(line, paths[0], true, true);
            for (uint32_t u = 1; layout == FOREIGN_TEST_OBJECTS && u < units; u += 1)
            {
                foreign_test_add(line, paths[u], true, true);
            }
            if ((layout == FOREIGN_TEST_ARCHIVE || layout == FOREIGN_TEST_THIN) && units > 1)
            {
                foreign_test_add(line, archives[0], true, true);
            }
            if (layout == FOREIGN_TEST_THIN && split < units)
            {
                foreign_test_add(line, archives[1], true, true);
            }
            if (libc)
            {
                foreign_test_pie_end(line, system);
            }
            ForeignCommand command;
            Foreign foreign = { 0 };
            if (made && foreign_test_command(&command, line->ours, line->our_count, arena))
            {
                foreign_test_resolve(&tally, what, &command, &foreign, arena);
                foreign_test_program_checks(&tally, what, &foreign, libc);
                if (cross_check)
                {
                    // GNU ld doesn't follow back references: only lld's result must be the resolution's there.
                    foreign_test_against(&tally, what, &foreign, line->theirs, line->their_count, layout != FOREIGN_TEST_BACK || units == 1, true, directory, arena);
                }
                foreign_release(&foreign);
            }
        }
        link_program_release(&program);
    }
    return foreign_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Compiled programs
//
// Small C (and, where g++ exists, C++) sources that cc compiles, each case a command line of their objects, archives of
// them (thin ones too), shared libraries cc links from them, and -u, linked without libc (-e main) unless a case says:
//   - commons (-fcommon): common symbols of two sizes (the larger wins, its alignment the largest), a common beside a
//     strong definition (the strong one wins) and beside a weak one (the common wins);
//   - GNU ld's common rule: a common symbol that an archive member defines as global data loads the member, one that
//     members define as a function, weakly or as another common doesn't; lld loads none of them (documented);
//   - weak and strong: a weak definition keeps an archive's strong one out, a weak reference loads nothing, a strong
//     reference loads the member whose other definitions then win;
//   - archives: a name two archives define (the first loads), a member that needs another archive's, a member that
//     needs an earlier member of its own archive, a thin archive, --whole-archive, and -u;
//   - back references: an archive before the object that needs it, which lld follows and GNU ld doesn't (documented);
//   - shared libraries: a name a shared library and an archive both define, in every order of the two and the object
//     that refers to it, which GNU ld and lld resolve alike but where the archive comes before the object;
//   - duplicates and undefined names, which both linkers refuse;
//   - COMDAT groups from g++'s inline functions and templates, in objects and in archive members loaded in member order;
//     and one member loaded after a later one, whose group GNU ld and lld keep (they load it first) where this linker
//     keeps the earlier member's (documented);
//   - a C program linked as GCC's driver links a PIE: the crt objects, libgcc, and libc through libc.so's script.

typedef struct ForeignTestSource ForeignTestSource;
struct ForeignTestSource
{
    const char* name;
    const char* flags;
    const char* text;
};

static const ForeignTestSource foreign_test_sources[] = {
    { "cm_main.c", "-fcommon", "int com_int; long com_array[2]; int com_strong; int com_weak;\nint main(void) { return (int)(com_int + com_array[0] + com_strong + com_weak); }\n" },
    { "cm_bigger.c", "-fcommon", "long com_array[8] __attribute__((aligned(32))); int com_strong = 3;\n" },
    { "cm_weak.c", "", "__attribute__((weak)) int com_weak = 5; __attribute__((weak)) int com_int = 7;\n" },
    { "rule_main.c", "-fcommon", "int rule_data; int rule_function; int rule_weak; int rule_common;\nint main(void) { return rule_data + rule_function + rule_weak + rule_common; }\n" },
    { "rule_data.c", "", "int rule_data = 9;\n" },
    { "rule_function.c", "", "int rule_function(void) { return 1; }\n" },
    { "rule_weak.c", "", "__attribute__((weak)) int rule_weak = 2;\n" },
    { "rule_common.c", "-fcommon", "int rule_common; int rule_common_other(void) { return 0; }\n" },
    { "wk_main.c", "",
      "extern int weak_only(void) __attribute__((weak)); extern int strong_in_archive(void); int kept_out(void) __attribute__((weak));\n"
      "int kept_out(void) { return 1; }\n"
      "int main(void) { return strong_in_archive() + kept_out() + (weak_only ? weak_only() : 0); }\n" },
    { "wk_m1.c", "", "extern int m2_name(void); int strong_in_archive(void) { return m2_name(); } __attribute__((weak)) int shared_weak(void) { return 1; }\n" },
    { "wk_m2.c", "", "int m2_name(void) { return 2; } int weak_only(void) { return 3; } int shared_weak(void) { return 2; }\n" },
    { "wk_m3.c", "", "int kept_out(void) { return 4; }\n" },
    { "ar_main.c", "", "extern int first(void); extern int needs_earlier(void);\nint main(void) { return first() + needs_earlier(); }\n" },
    { "ar_first_a.c", "", "extern int second(void); int first(void) { return second(); }\n" },
    { "ar_first_b.c", "", "int first(void) { return 100; }\n" },
    { "ar_second.c", "", "int second(void) { return 2; }\n" },
    { "ar_early.c", "", "int early(void) { return 5; }\n" },
    { "ar_late.c", "", "extern int early(void); int needs_earlier(void) { return early(); }\n" },
    { "ar_undefined.c", "", "int forced(void) { return 6; }\n" },
    { "sh_main.c", "", "extern int shared_name(void); int main(void) { return shared_name(); }\n" },
    { "sh_archive.c", "", "int shared_name(void) { return 1; }\n" },
    { "sh_library.c", "-fPIC", "int shared_name(void) { return 2; } int library_only(void) { return 3; }\n" },
    { "dup_a.c", "", "int duplicate(void) { return 1; } int main(void) { return duplicate(); }\n" },
    { "dup_b.c", "", "int duplicate(void) { return 2; }\n" },
    { "undef_main.c", "", "extern int nowhere(void); extern int weak_nowhere(void) __attribute__((weak));\nint main(void) { return nowhere() + (weak_nowhere ? 1 : 0); }\n" },
    { "hello.c", "", "#include <stdio.h>\nint main(void) { puts(\"hello\"); return 0; }\n" },
    { "co_main.cc", "",
      "#include \"co.h\"\nint a_function(int); int b_function(int);\nint main() { return a_function(1) + b_function(2) + twice(3) + thrice<long>(4) - 39; }\n" },
    { "co_a.cc", "", "#include \"co.h\"\nint a_function(int v) { return twice(v) + thrice(v) + Counter<int>::count(); }\n" },
    { "co_b.cc", "", "#include \"co.h\"\nint c_function(int); int b_function(int v) { return twice(v) + thrice<long>(v) + c_function(v); }\n" },
    { "co_c.cc", "", "#include \"co.h\"\nint c_function(int v) { return thrice(v) + Counter<int>::count(); }\n" },
    { "co_early.cc", "", "#include \"co.h\"\nint early_function(int v) { return twice(v) + 1; }\n" },
    { "co_late.cc", "", "#include \"co.h\"\nint early_function(int); int late_function(int v) { return twice(v) + early_function(v); }\n" },
    { "co_back_main.cc", "", "int late_function(int); int main() { return late_function(1); }\n" },
    { "pb_main.c", "", "extern int probed(void); int main(void) { return probed(); }\n" },
    { "pb_strong.c", "", "int probed(void) { return 1; }\n" },
    { "pb_weak.c", "", "__attribute__((weak)) int probed(void) { return 2; }\n" },
    { "pb_late.c", "", "int probed(void) { return 3; }\n" },
    { "dep_main.c", "", "extern int dependent(void); int main(void) { return dependent(); }\n" },
    { "dep_plain.c", "", "int main(void) { return 0; }\n" },
    { "dep_library.c", "-fPIC", "extern int dep_foo(void); int dependent(void) { return dep_foo(); }\n" },
    { "dep_foo.c", "", "int dep_foo(void) { return 4; }\n" },
};

static const char foreign_test_header_text[] =
    "inline int twice(int v) { return v * 2; }\n"
    "template <typename T> T thrice(T v) { return v * 3; }\n"
    "template <typename T> struct Counter { static int count() { static int calls; calls += 1; return calls; } };\n";

// Compiles the sources into `directory`: each one's object there, by cc or (for .cc) g++. Returns whether C and C++
// each compiled.
static void foreign_test_compile(const char* directory, bool* c, bool* cplusplus, Arena* arena)
{
    char path[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char object[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char header[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char include[BUSTER_FOREIGN_TEST_PATH_BYTES + 2];
    char output[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char errors[BUSTER_FOREIGN_TEST_PATH_BYTES];
    snprintf(header, sizeof(header), "%s/co.h", directory);
    snprintf(include, sizeof(include), "-I%s", directory);
    snprintf(output, sizeof(output), "%s/compile.out", directory);
    snprintf(errors, sizeof(errors), "%s/compile.err", directory);
    *c = true;
    *cplusplus = foreign_test_write(header, foreign_test_header_text, strlen(foreign_test_header_text));
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(foreign_test_sources); i += 1)
    {
        const ForeignTestSource* source = &foreign_test_sources[i];
        const bool is_cplusplus = strstr(source->name, ".cc") != 0;
        snprintf(path, sizeof(path), "%s/%s", directory, source->name);
        snprintf(object, sizeof(object), "%s/%.*s.o", directory, (int)(strrchr(source->name, '.') - source->name), source->name);
        // C++ at -O0, so that its inline functions stay out of line, each in its group.
        char* arguments[] = {
            (char*)(is_cplusplus ? "g++" : "cc"),
            (char*)"-c",
            (char*)(is_cplusplus ? "-O0" : "-O1"),
            include,
            path,
            (char*)"-o",
            object,
            source->flags[0] != 0 ? (char*)source->flags : 0,
            0,
        };
        const bool written = foreign_test_write(path, source->text, strlen(source->text));
        const bool compiled = written && object_test_run(arguments, output, errors) == 0;
        *c = *c && (is_cplusplus || compiled);
        *cplusplus = *cplusplus && (!is_cplusplus || compiled);
    }
    (void)arena;
}

// A path in the test's directory.
static char* foreign_test_path(const char* directory, const char* name, Arena* arena)
{
    const uint64_t length = strlen(directory) + 1 + strlen(name) + 1;
    char* result = arena_push(arena, length, 1);
    if (result != 0)
    {
        snprintf(result, length, "%s/%s", directory, name);
    }
    return result;
}

// An archive in the test's directory of the objects named (without directory), its path.
static char* foreign_test_archive(const char* directory, const char* name, const char* const* objects, uint32_t count, bool thin, Arena* arena)
{
    char* members[16];
    char* result = foreign_test_path(directory, name, arena);
    for (uint32_t i = 0; i < count && i < 16; i += 1)
    {
        members[i] = foreign_test_path(directory, objects[i], arena);
    }
    return result != 0 && foreign_test_write_archive(result, members, count, thin, arena) ? result : 0;
}

// A case: its name, its command line (names in the test's directory, or options as they are, with "@pie" for GCC's
// driver's line around them), whose results the linkers' must be, and checks of the resolution of its own.
typedef struct ForeignTestCase ForeignTestCase;
struct ForeignTestCase
{
    const char* name;
    const char* arguments[24];
    bool bfd;
    bool lld;
    bool cplusplus;
};

// Each case's command line names files the test makes: the objects (x.o), the archives below, libsh.so, and options.
static const ForeignTestCase foreign_test_cases[] = {
    { "commons", { "cm_main.o", "cm_bigger.o", "cm_weak.o" }, true, true, false },
    { "GNU ld's common rule", { "rule_main.o", "librule.a" }, true, false, false },
    { "weak and strong", { "wk_main.o", "libwk.a" }, true, true, false },
    { "the first archive", { "ar_main.o", "libfirst_a.a", "libfirst_b.a", "libsecond.a", "liblate.a" }, true, true, false },
    { "the first archive, swapped", { "ar_main.o", "libfirst_b.a", "libfirst_a.a", "libsecond.a", "liblate.a" }, true, true, false },
    { "a thin archive", { "ar_main.o", "libfirst_thin.a", "libsecond.a", "liblate.a" }, true, true, false },
    { "whole archives", { "ar_main.o", "--whole-archive", "libfirst_a.a", "--no-whole-archive", "libsecond.a", "liblate.a", "libfirst_b.a" }, true, true, false },
    { "-u", { "-u", "forced", "ar_main.o", "libfirst_a.a", "libsecond.a", "liblate.a", "libforced.a" }, true, true, false },
    { "back references", { "libsecond.a", "liblate.a", "ar_main.o", "libfirst_a.a" }, false, true, false },
    { "shared: object, archive, library", { "sh_main.o", "libsh.a", "libsh.so" }, true, true, false },
    { "shared: object, library, archive", { "sh_main.o", "libsh.so", "libsh.a" }, true, true, false },
    { "shared: archive, object, library", { "libsh.a", "sh_main.o", "libsh.so" }, false, true, false },
    { "shared: archive, library, object", { "libsh.a", "libsh.so", "sh_main.o" }, true, true, false },
    { "shared: library, archive, object", { "libsh.so", "libsh.a", "sh_main.o" }, true, true, false },
    { "a duplicate", { "dup_a.o", "dup_b.o" }, true, true, false },
    { "undefined names", { "undef_main.o" }, true, true, false },
    { "a PIE with libc", { "@pie", "hello.o" }, true, true, false },
    { "COMDAT groups", { "co_main.o", "co_a.o", "libco.a" }, true, true, true },
    { "COMDAT groups, members loaded later first", { "co_back_main.o", "libco_back.a" }, false, false, true },
    { "a later weak definition", { "pb_main.o", "libpb.a", "pb_weak.o" }, true, true, false },
    { "a later strong definition", { "pb_main.o", "libpb.a", "pb_late.o" }, true, true, false },
    { "a library's reference", { "dep_main.o", "libdep.so", "libdepfoo.a" }, true, true, false },
    { "an as-needed library's reference", { "dep_main.o", "--as-needed", "libdep.so", "--no-as-needed", "libdepfoo.a" }, true, true, false },
    { "an as-needed library nothing needs", { "dep_plain.o", "--as-needed", "libdep.so", "--no-as-needed", "libdepfoo.a" }, true, false, false },
    { "a thin archive of another archive's member", { "ar_main.o", "libnest.a", "libsecond.a", "liblate.a" }, true, false, false },
};

// What a compiled case's resolution must be, whatever the linkers say: names, each with its resolution and where its
// definition is (the end of its object's name, or of its library's path), a common one's size and alignment, how
// many strong definitions; and the members it loads, each by the end of its name, and how many.
typedef struct ForeignTestExpected ForeignTestExpected;
struct ForeignTestExpected
{
    const char* name;
    uint8_t resolution;
    const char* where;
    uint64_t common_size;
    uint8_t common_alignment;
    uint32_t definitions;
};

typedef struct ForeignTestOutcome ForeignTestOutcome;
struct ForeignTestOutcome
{
    const char* name;
    ForeignTestExpected names[8];
    const char* members[8];
    int32_t member_count;
};

static const ForeignTestOutcome foreign_test_outcomes[] = {
    {
        "commons",
        {
            { "com_int", FOREIGN_RESOLUTION_COMMON, "cm_main.o", 4, 2, 0 },
            { "com_array", FOREIGN_RESOLUTION_COMMON, "cm_bigger.o", 64, 5, 0 },
            { "com_strong", FOREIGN_RESOLUTION_STRONG, "cm_bigger.o", 0, 0, 1 },
            { "com_weak", FOREIGN_RESOLUTION_COMMON, "cm_main.o", 4, 2, 0 },
        },
        { 0 },
        0,
    },
    {
        "GNU ld's common rule",
        {
            { "rule_data", FOREIGN_RESOLUTION_STRONG, "librule.a(rule_data.o)", 0, 0, 1 },
            { "rule_function", FOREIGN_RESOLUTION_COMMON, "rule_main.o", 4, 2, 0 },
            { "rule_weak", FOREIGN_RESOLUTION_COMMON, "rule_main.o", 4, 2, 0 },
            { "rule_common", FOREIGN_RESOLUTION_COMMON, "rule_main.o", 4, 2, 0 },
        },
        { "librule.a(rule_data.o)" },
        1,
    },
    {
        "weak and strong",
        {
            { "strong_in_archive", FOREIGN_RESOLUTION_STRONG, "libwk.a(wk_m1.o)", 0, 0, 1 },
            { "weak_only", FOREIGN_RESOLUTION_STRONG, "libwk.a(wk_m2.o)", 0, 0, 1 },
            { "kept_out", FOREIGN_RESOLUTION_WEAK, "wk_main.o", 0, 0, 0 },
            { "shared_weak", FOREIGN_RESOLUTION_STRONG, "libwk.a(wk_m2.o)", 0, 0, 1 },
        },
        { "libwk.a(wk_m1.o)", "libwk.a(wk_m2.o)" },
        2,
    },
    {
        "the first archive",
        {
            { "first", FOREIGN_RESOLUTION_STRONG, "libfirst_a.a(ar_first_a.o)", 0, 0, 1 },
            { "second", FOREIGN_RESOLUTION_STRONG, "libsecond.a(ar_second.o)", 0, 0, 1 },
            { "early", FOREIGN_RESOLUTION_STRONG, "liblate.a(ar_early.o)", 0, 0, 1 },
        },
        { "libfirst_a.a(ar_first_a.o)", "libsecond.a(ar_second.o)", "liblate.a(ar_late.o)", "liblate.a(ar_early.o)" },
        4,
    },
    {
        "the first archive, swapped",
        {
            { "first", FOREIGN_RESOLUTION_STRONG, "libfirst_b.a(ar_first_b.o)", 0, 0, 1 },
        },
        { "libfirst_b.a(ar_first_b.o)", "liblate.a(ar_late.o)", "liblate.a(ar_early.o)" },
        3,
    },
    {
        "a thin archive",
        {
            { "first", FOREIGN_RESOLUTION_STRONG, "ar_first_a.o)", 0, 0, 1 },
        },
        { "ar_first_a.o)", "libsecond.a(ar_second.o)" },
        4,
    },
    {
        "whole archives",
        {
            { "first", FOREIGN_RESOLUTION_STRONG, "libfirst_a.a(ar_first_a.o)", 0, 0, 1 },
        },
        { "libsecond.a(ar_second.o)", "libfirst_a.a(ar_first_a.o)" },
        4,
    },
    {
        "-u",
        {
            { "forced", FOREIGN_RESOLUTION_STRONG, "libforced.a(ar_undefined.o)", 0, 0, 1 },
        },
        { "libforced.a(ar_undefined.o)" },
        5,
    },
    {
        "back references",
        {
            { "first", FOREIGN_RESOLUTION_STRONG, "libfirst_a.a(ar_first_a.o)", 0, 0, 1 },
            { "second", FOREIGN_RESOLUTION_STRONG, "libsecond.a(ar_second.o)", 0, 0, 1 },
        },
        { "libsecond.a(ar_second.o)", "liblate.a(ar_late.o)", "liblate.a(ar_early.o)" },
        4,
    },
    {
        "shared: object, archive, library",
        {
            { "shared_name", FOREIGN_RESOLUTION_STRONG, "libsh.a(sh_archive.o)", 0, 0, 1 },
        },
        { "libsh.a(sh_archive.o)" },
        1,
    },
    {
        "shared: object, library, archive",
        {
            { "shared_name", FOREIGN_RESOLUTION_SHARED, "libsh.so", 0, 0, 0 },
        },
        { 0 },
        0,
    },
    {
        "shared: archive, object, library",
        {
            { "shared_name", FOREIGN_RESOLUTION_STRONG, "libsh.a(sh_archive.o)", 0, 0, 1 },
        },
        { "libsh.a(sh_archive.o)" },
        1,
    },
    {
        "shared: archive, library, object",
        {
            { "shared_name", FOREIGN_RESOLUTION_SHARED, "libsh.so", 0, 0, 0 },
        },
        { 0 },
        0,
    },
    {
        "shared: library, archive, object",
        {
            { "shared_name", FOREIGN_RESOLUTION_SHARED, "libsh.so", 0, 0, 0 },
        },
        { 0 },
        0,
    },
    {
        "a duplicate",
        {
            { "duplicate", FOREIGN_RESOLUTION_STRONG, "dup_a.o", 0, 0, 2 },
        },
        { 0 },
        0,
    },
    {
        "undefined names",
        {
            { "nowhere", FOREIGN_RESOLUTION_UNDEFINED, "", 0, 0, 0 },
            { "weak_nowhere", FOREIGN_RESOLUTION_WEAK_UNDEFINED, "", 0, 0, 0 },
        },
        { 0 },
        0,
    },
    {
        "a PIE with libc",
        {
            { "puts", FOREIGN_RESOLUTION_SHARED, "libc.so.6", 0, 0, 0 },
            { "main", FOREIGN_RESOLUTION_STRONG, "hello.o", 0, 0, 1 },
        },
        { 0 },
        -1,
    },
    {
        "COMDAT groups",
        {
            { "_Z5twicei", FOREIGN_RESOLUTION_WEAK, "co_main.o", 0, 0, 0 },
            { "_Z6thriceIiET_S0_", FOREIGN_RESOLUTION_WEAK, "co_a.o", 0, 0, 0 },
            { "_Z6thriceIlET_S0_", FOREIGN_RESOLUTION_WEAK, "co_main.o", 0, 0, 0 },
        },
        { "libco.a(co_b.o)", "libco.a(co_c.o)" },
        2,
    },
    {
        "COMDAT groups, members loaded later first",
        {
            { "_Z5twicei", FOREIGN_RESOLUTION_WEAK, "libco_back.a(co_early.o)", 0, 0, 0 },
        },
        { "libco_back.a(co_early.o)", "libco_back.a(co_late.o)" },
        2,
    },
    {
        "a later weak definition",
        {
            { "probed", FOREIGN_RESOLUTION_STRONG, "libpb.a(pb_strong.o)", 0, 0, 1 },
        },
        { "libpb.a(pb_strong.o)" },
        1,
    },
    {
        "a later strong definition",
        {
            { "probed", FOREIGN_RESOLUTION_STRONG, "libpb.a(pb_strong.o)", 0, 0, 2 },
        },
        { "libpb.a(pb_strong.o)" },
        1,
    },
    {
        "a library's reference",
        {
            { "dependent", FOREIGN_RESOLUTION_SHARED, "libdep.so", 0, 0, 0 },
            { "dep_foo", FOREIGN_RESOLUTION_STRONG, "libdepfoo.a(dep_foo.o)", 0, 0, 1 },
        },
        { "libdepfoo.a(dep_foo.o)" },
        1,
    },
    {
        "an as-needed library's reference",
        {
            { "dependent", FOREIGN_RESOLUTION_SHARED, "libdep.so", 0, 0, 0 },
            { "dep_foo", FOREIGN_RESOLUTION_STRONG, "libdepfoo.a(dep_foo.o)", 0, 0, 1 },
        },
        { "libdepfoo.a(dep_foo.o)" },
        1,
    },
    {
        "an as-needed library nothing needs",
        {
            { "main", FOREIGN_RESOLUTION_STRONG, "dep_plain.o", 0, 0, 1 },
        },
        { 0 },
        0,
    },
    {
        "a thin archive of another archive's member",
        {
            { "first", FOREIGN_RESOLUTION_STRONG, "libnest_inner.a)", 0, 0, 1 },
        },
        { "libnest_inner.a)", "libsecond.a(ar_second.o)" },
        4,
    },
};

static bool foreign_test_ends_with(const char* text, const char* end)
{
    const uint64_t length = strlen(text);
    const uint64_t end_length = strlen(end);
    return length >= end_length && strcmp(text + length - end_length, end) == 0;
}

static void foreign_test_compiled_checks(ForeignTestTally* tally, const ForeignTestCase* test, const Foreign* foreign)
{
    char detail[BUSTER_FOREIGN_TEST_PATH_BYTES * 2];
    char where[BUSTER_FOREIGN_TEST_PATH_BYTES];
    snprintf(detail, sizeof(detail), "fails: %s", foreign_test_errors[foreign->failure.error]);
    foreign_test_expect(tally, foreign->failure.error == FOREIGN_ERROR_NONE, test->name, detail);
    const ForeignTestOutcome* outcome = 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(foreign_test_outcomes); i += 1)
    {
        outcome = strcmp(foreign_test_outcomes[i].name, test->name) == 0 ? &foreign_test_outcomes[i] : outcome;
    }
    for (uint32_t k = 0; outcome != 0 && foreign->failure.error == FOREIGN_ERROR_NONE && k < 8 && outcome->names[k].name != 0; k += 1)
    {
        const ForeignTestExpected* expected = &outcome->names[k];
        const uint32_t name = foreign_test_name(foreign, expected->name);
        bool same = name != 0;
        where[0] = 0;
        if (same)
        {
            const uint32_t n = name - 1;
            const uint8_t resolution = foreign->names.resolution[n];
            if (resolution == FOREIGN_RESOLUTION_STRONG || resolution == FOREIGN_RESOLUTION_COMMON || resolution == FOREIGN_RESOLUTION_WEAK)
            {
                foreign_test_object_name(foreign, foreign_symbol_object(foreign, foreign->names.symbol[n]), where, sizeof(where));
            }
            else if (resolution == FOREIGN_RESOLUTION_SHARED)
            {
                const ForeignInput* input = &foreign->inputs[foreign->libraries[foreign->names.library[n]].input];
                snprintf(where, sizeof(where), "%.*s", (int)input->path.length, input->path.pointer);
            }
            same = resolution == expected->resolution && foreign_test_ends_with(where, expected->where) && foreign->names.definitions[n] == expected->definitions &&
                   (resolution != FOREIGN_RESOLUTION_COMMON || (foreign->names.common_size[n] == expected->common_size && foreign->names.common_alignment[n] == expected->common_alignment));
            snprintf(detail, sizeof(detail), "%s is %s in %s with %u definitions, common %llu/%u", expected->name, foreign_test_resolutions[resolution], where,
                     foreign->names.definitions[n], (unsigned long long)foreign->names.common_size[n], foreign->names.common_alignment[n]);
        }
        else
        {
            snprintf(detail, sizeof(detail), "%s isn't a name", expected->name);
        }
        foreign_test_expect(tally, same, test->name, detail);
    }
    uint32_t members = 0;
    for (uint32_t id = 1; outcome != 0 && foreign->failure.error == FOREIGN_ERROR_NONE && id < foreign->object_count; id += 1)
    {
        members += foreign->objects[id].member != 0 ? 1 : 0;
    }
    for (uint32_t m = 0; outcome != 0 && foreign->failure.error == FOREIGN_ERROR_NONE && m < 8 && outcome->members[m] != 0; m += 1)
    {
        bool loaded = false;
        for (uint32_t id = 1; id < foreign->object_count; id += 1)
        {
            foreign_test_object_name(foreign, id, where, sizeof(where));
            loaded = loaded || (foreign->objects[id].member != 0 && foreign_test_ends_with(where, outcome->members[m]));
        }
        snprintf(detail, sizeof(detail), "%s isn't loaded", outcome->members[m]);
        foreign_test_expect(tally, loaded, test->name, detail);
    }
    snprintf(detail, sizeof(detail), "%u members loaded, not %d", members, outcome != 0 ? outcome->member_count : 0);
    foreign_test_expect(tally, outcome == 0 || outcome->member_count < 0 || (int32_t)members == outcome->member_count, test->name, detail);
    foreign_test_expect(tally, outcome != 0, test->name, "the case's outcome");
}

static bool foreign_test_compiled(bool cross_check, const char* directory, ForeignTestSystem* system, Arena* arena)
{
    ForeignTestTally tally = {
        .check = "compiled",
    };
    bool c = false;
    bool cplusplus = false;
    foreign_test_compile(directory, &c, &cplusplus, arena);
    if (!c)
    {
        printf("compiled: cc doesn't compile the sources, so the compiled programs aren't checked\n");
    }
    // The archives.
    static const char* const rule[] = { "rule_data.o", "rule_function.o", "rule_weak.o", "rule_common.o" };
    static const char* const weak[] = { "wk_m1.o", "wk_m2.o", "wk_m3.o" };
    static const char* const first_a[] = { "ar_first_a.o" };
    static const char* const first_b[] = { "ar_first_b.o" };
    static const char* const second[] = { "ar_second.o" };
    static const char* const late[] = { "ar_early.o", "ar_late.o" };
    static const char* const forced[] = { "ar_undefined.o" };
    static const char* const shared[] = { "sh_archive.o" };
    static const char* const comdat[] = { "co_b.o", "co_c.o" };
    static const char* const comdat_back[] = { "co_early.o", "co_late.o" };
    static const char* const probe[] = { "pb_strong.o" };
    static const char* const dependency[] = { "dep_foo.o" };
    bool made = c;
    made = made && foreign_test_archive(directory, "librule.a", rule, 4, false, arena) != 0;
    made = made && foreign_test_archive(directory, "libwk.a", weak, 3, false, arena) != 0;
    made = made && foreign_test_archive(directory, "libfirst_a.a", first_a, 1, false, arena) != 0;
    made = made && foreign_test_archive(directory, "libfirst_b.a", first_b, 1, false, arena) != 0;
    made = made && foreign_test_archive(directory, "libfirst_thin.a", first_a, 1, true, arena) != 0;
    made = made && foreign_test_archive(directory, "libsecond.a", second, 1, false, arena) != 0;
    made = made && foreign_test_archive(directory, "liblate.a", late, 2, false, arena) != 0;
    made = made && foreign_test_archive(directory, "libforced.a", forced, 1, false, arena) != 0;
    made = made && foreign_test_archive(directory, "libsh.a", shared, 1, false, arena) != 0;
    made = made && foreign_test_archive(directory, "libpb.a", probe, 1, false, arena) != 0;
    made = made && foreign_test_archive(directory, "libdepfoo.a", dependency, 1, false, arena) != 0;
    if (cplusplus)
    {
        made = made && foreign_test_archive(directory, "libco.a", comdat, 2, false, arena) != 0;
        made = made && foreign_test_archive(directory, "libco_back.a", comdat_back, 2, false, arena) != 0;
    }
    char library[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char library_object[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char output[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char errors[BUSTER_FOREIGN_TEST_PATH_BYTES];
    snprintf(library, sizeof(library), "%s/libsh.so", directory);
    snprintf(library_object, sizeof(library_object), "%s/sh_library.o", directory);
    snprintf(output, sizeof(output), "%s/library.out", directory);
    snprintf(errors, sizeof(errors), "%s/library.err", directory);
    char* library_arguments[] = {
        (char*)"cc",
        (char*)"-shared",
        (char*)"-o",
        library,
        library_object,
        0,
    };
    made = made && object_test_run(library_arguments, output, errors) == 0;
    // A library that refers to a name an archive after it defines.
    char dependency_library[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char dependency_object[BUSTER_FOREIGN_TEST_PATH_BYTES];
    snprintf(dependency_library, sizeof(dependency_library), "%s/libdep.so", directory);
    snprintf(dependency_object, sizeof(dependency_object), "%s/dep_library.o", directory);
    char* dependency_arguments[] = {
        (char*)"cc",
        (char*)"-shared",
        (char*)"-o",
        dependency_library,
        dependency_object,
        0,
    };
    made = made && object_test_run(dependency_arguments, output, errors) == 0;
    // A thin archive of another archive's member, as GNU ar writes one when given the archive (a member named "/N:M"),
    // the other archive GNU ar's too, so that its member's name is in its header (the test reads it there).
    char nested[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char nested_inner[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char nested_member[BUSTER_FOREIGN_TEST_PATH_BYTES];
    snprintf(nested, sizeof(nested), "%s/libnest.a", directory);
    snprintf(nested_inner, sizeof(nested_inner), "%s/libnest_inner.a", directory);
    snprintf(nested_member, sizeof(nested_member), "%s/ar_first_a.o", directory);
    char* inner_arguments[] = {
        (char*)"ar",
        (char*)"crs",
        nested_inner,
        nested_member,
        0,
    };
    char* nested_arguments[] = {
        (char*)"ar",
        (char*)"crsT",
        nested,
        nested_inner,
        0,
    };
    made = made && object_test_run(inner_arguments, output, errors) == 0 && object_test_run(nested_arguments, output, errors) == 0;
    foreign_test_expect(&tally, !c || made, "the archives and the libraries", 0);
    ForeignTestLine* line = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignTestLine, 1);
    for (uint32_t k = 0; made && line != 0 && k < BUSTER_ARRAY_LENGTH(foreign_test_cases); k += 1)
    {
        const ForeignTestCase* test = &foreign_test_cases[k];
        const uint64_t mark = arena->length;
        *line = (ForeignTestLine){ 0 };
        bool pie = false;
        for (uint32_t a = 0; test->arguments[a] != 0; a += 1)
        {
            pie = pie || strcmp(test->arguments[a], "@pie") == 0;
        }
        if (test->cplusplus && !cplusplus)
        {
            continue;
        }
        if (pie && !system->found)
        {
            continue;
        }
        if (pie)
        {
            foreign_test_pie_begin(line, system);
        }
        else
        {
            foreign_test_add(line, (char*)"-e", false, true);
            foreign_test_add(line, (char*)"main", false, true);
        }
        for (uint32_t a = 0; test->arguments[a] != 0; a += 1)
        {
            const char* argument = test->arguments[a];
            if (strcmp(argument, "@pie") != 0)
            {
                char* value = argument[0] == '-' || (a > 0 && strcmp(test->arguments[a - 1], "-u") == 0) ? (char*)argument : foreign_test_path(directory, argument, arena);
                foreign_test_add(line, value, true, true);
            }
        }
        if (pie)
        {
            foreign_test_pie_end(line, system);
        }
        if (test->cplusplus)
        {
            // libstdc++'s names aren't needed: the sources call nothing of it.
        }
        ForeignCommand command;
        Foreign foreign = { 0 };
        if (foreign_test_command(&command, line->ours, line->our_count, arena))
        {
            foreign_test_resolve(&tally, test->name, &command, &foreign, arena);
            foreign_test_compiled_checks(&tally, test, &foreign);
            if (cross_check)
            {
                foreign_test_against(&tally, test->name, &foreign, line->theirs, line->their_count, test->bfd, test->lld, directory, arena);
            }
            foreign_release(&foreign);
        }
        arena_pop_to(arena, mark);
    }
    return foreign_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Malformed inputs and page edges
//
// Inputs the resolution must refuse, each naming its place (the failure's error, input, member, and the reader's
// error, section, symbol and offset), made from a compiled object by changing one field: a local symbol among the
// global ones; a global SECTION symbol; a section index past the table; a common symbol aligned to 3; a name past the
// string table; the symbol table's first global past its count; a relocation section linked to another section than
// the symbol table, two for one section, and an SHT_REL section; an archive member's header broken (refused when the
// member loads, not before), a thin archive's member missing, and a member that isn't an object; a script that doesn't
// parse, a -l name no directory has, a missing file and an unknown option. Then page edges: each input given to the
// resolution in memory, ending at a PROT_NONE page: every prefix of an object and of an archive, and the object with
// each byte set to 0, 0xff and a random value, which each path must resolve alike or refuse alike, without reading
// past the end (a read there faults).

// Room for an input that ends at a page's end, before a PROT_NONE page.
typedef struct ForeignTestEdge ForeignTestEdge;
struct ForeignTestEdge
{
    uint8_t* mapping;
    uint64_t mapping_size;
    uint8_t* end;
};

static bool foreign_test_edge(ForeignTestEdge* edge, uint64_t capacity)
{
    const uint64_t page = 4096;
    const uint64_t pages = (capacity + page - 1) / page + 1;
    *edge = (ForeignTestEdge){ 0 };
    uint8_t* mapping = mmap(0, (pages + 1) * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool result = mapping != MAP_FAILED && mprotect(mapping + pages * page, page, PROT_NONE) == 0;
    if (result)
    {
        edge->mapping = mapping;
        edge->mapping_size = (pages + 1) * page;
        edge->end = mapping + pages * page;
    }
    return result;
}

static void foreign_test_edge_release(ForeignTestEdge* edge)
{
    if (edge->mapping != 0)
    {
        munmap(edge->mapping, edge->mapping_size);
    }
    *edge = (ForeignTestEdge){ 0 };
}

// Resolves a command line of given inputs (`count` of them, names and bytes) by both paths on one lane, and returns
// whether the two agree; *failure gets the exact path's failure.
static bool foreign_test_given(const char* const* names, const String8* inputs, uint32_t count, ForeignFailure* failure, Arena* arena)
{
    const uint64_t mark = arena->length;
    ForeignArgument arguments[8];
    ForeignCommand command = {
        .arguments = arguments,
        .argument_count = count,
    };
    for (uint32_t i = 0; i < count; i += 1)
    {
        arguments[i] = (ForeignArgument){
            .name = foreign_string(names[i]),
            .bytes = inputs[i],
        };
    }
    ForeignTestText texts[2];
    bool result = true;
    for (uint32_t path = 0; path < 2; path += 1)
    {
        texts[path] = (ForeignTestText){
            .bytes = arena_push(arena, 1u << 20, 64),
            .capacity = 1u << 20,
        };
        Foreign foreign;
        const ForeignFailure got = foreign_run(&foreign, &command, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, 1, 0);
        if (path == 0)
        {
            *failure = got;
        }
        if (texts[path].bytes != 0)
        {
            foreign_test_describe(&foreign, &texts[path]);
        }
        result = result && texts[path].bytes != 0;
        foreign_release(&foreign);
    }
    result = result && texts[0].length == texts[1].length && memcmp(texts[0].bytes, texts[1].bytes, texts[0].length) == 0;
    arena_pop_to(arena, mark);
    return result;
}

// The place of an object's global symbol named `name`: its index and its entry's offset in the file.
static bool foreign_test_find_symbol(String8 bytes, const char* name, uint32_t* index, uint64_t* offset, ImportSymbols* table)
{
    ImportElf elf;
    bool result = import_elf(&elf, bytes, ET_REL).error == IMPORT_ERROR_NONE && import_symbols(&elf, table).error == IMPORT_ERROR_NONE && table->section != 0;
    bool found = false;
    for (uint32_t i = result ? import_section_header(&elf, table->section).sh_info : 0; result && !found && i < table->count; i += 1)
    {
        const Elf64_Sym symbol = import_symbol(table, i);
        String8 text = { 0 };
        found = import_string(table->names, symbol.st_name, &text) && foreign_test_equal(text, name);
        *index = i;
        *offset = (uint64_t)(table->entries.pointer - bytes.pointer) + (uint64_t)i * sizeof(Elf64_Sym);
    }
    return result && found;
}

// The offset of the header of an object's first section of `type`, and its index.
static bool foreign_test_find_section(String8 bytes, uint32_t type, uint32_t* index, uint64_t* offset)
{
    ImportElf elf;
    bool found = false;
    if (import_elf(&elf, bytes, ET_REL).error == IMPORT_ERROR_NONE)
    {
        for (uint32_t s = 1; !found && s < elf.section_count; s += 1)
        {
            found = import_section_header(&elf, s).sh_type == type;
            *index = s;
            *offset = elf.section_offset + (uint64_t)s * sizeof(Elf64_Shdr);
        }
    }
    return found;
}

// The check the watchdog names if a resolution's lanes wait for each other for good, which is how lanes that decide
// differently show: one waits at a sync that another skipped.
#define BUSTER_FOREIGN_TEST_TIMEOUT_SECONDS 60
static const char* volatile foreign_test_watched;

static void foreign_test_report_hang(int signal_number)
{
    (void)signal_number;
    char message[256];
    const int length = snprintf(message, sizeof(message), "test-foreign: %s didn't finish in %d s\n", foreign_test_watched, BUSTER_FOREIGN_TEST_TIMEOUT_SECONDS);
    if (length > 0 && write(STDOUT_FILENO, message, (size_t)length) < 0)
    {
        _exit(1);
    }
    _exit(1);
}

// Resolves a command on `lanes` lanes with lane `slow` (plus 1) slow at each decision (ForeignCommand.slow_lane), under
// the watchdog: its failure.
static ForeignFailure foreign_test_slow(const ForeignCommand* given, uint32_t lanes, uint32_t slow, const char* what)
{
    ForeignCommand command = *given;
    command.slow_lane = slow;
    foreign_test_watched = what;
    alarm(BUSTER_FOREIGN_TEST_TIMEOUT_SECONDS);
    Foreign foreign;
    const ForeignFailure result = foreign_run(&foreign, &command, FOREIGN_PATH_VECTOR, lanes, 0);
    alarm(0);
    foreign_release(&foreign);
    return result;
}

static bool foreign_test_same_failure(const ForeignFailure* a, const ForeignFailure* b)
{
    return a->error == b->error && a->input == b->input && a->member == b->member && a->position == b->position && a->system_error == b->system_error &&
           a->import.error == b->import.error && a->import.section == b->import.section && a->import.symbol == b->import.symbol && a->import.offset == b->import.offset;
}

// A failing command on two and three lanes, each lane the slow one in turn, so that the others decide first and go on
// to the next phase, where one fails while the slow one still decides: every lane must decide alike (the watchdog
// catches one that waits for good), and the failure must be the one lane's.
static void foreign_test_decisions(ForeignTestTally* tally, const char* what, const ForeignCommand* command, const ForeignFailure* expected)
{
    for (uint32_t lanes = 2; lanes <= 3; lanes += 1)
    {
        for (uint32_t slow = 1; slow <= lanes; slow += 1)
        {
            const ForeignFailure got = foreign_test_slow(command, lanes, slow, what);
            char detail[160];
            snprintf(detail, sizeof(detail), "on %u lanes with lane %u slow: %s, not %s", lanes, slow - 1, foreign_test_errors[got.error], foreign_test_errors[expected->error]);
            foreign_test_expect(tally, foreign_test_same_failure(&got, expected), what, detail);
        }
    }
}

// One malformed object: a copy of `base` with `width` bytes at `offset` set to `value`, given alone, which must fail as
// `expected` (the failure's error, and the reader's error, section, symbol and offset).
static void foreign_test_malformed(ForeignTestTally* tally, const char* what, String8 base, uint64_t offset, uint32_t width, uint64_t value, ForeignError error, ImportResult expected,
                                   Arena* arena)
{
    const uint64_t mark = arena->length;
    char* copy = arena_push(arena, base.length, 64);
    if (copy != 0 && offset + width <= base.length)
    {
        memcpy(copy, base.pointer, base.length);
        memcpy(copy + offset, &value, width);
        const char* names[] = {
            "malformed.o",
        };
        const String8 inputs[] = {
            { copy, base.length },
        };
        ForeignFailure failure;
        const bool agree = foreign_test_given(names, inputs, 1, &failure, arena);
        char detail[256];
        snprintf(detail, sizeof(detail), "%s (%s), import error %d, section %u, symbol %u, type %u, offset %llu", foreign_test_errors[failure.error], agree ? "the paths agree" : "the paths differ",
                 failure.import.error, failure.import.section, failure.import.symbol, failure.import.type, (unsigned long long)failure.import.offset);
        foreign_test_expect(tally, agree && failure.error == error && failure.import.error == expected.error && failure.import.section == expected.section &&
                                       failure.import.symbol == expected.symbol && failure.import.offset == expected.offset && failure.input == 0 && failure.member == 0 &&
                                       (expected.type == 0 || failure.import.type == expected.type),
                            what, detail);
        ForeignArgument argument = {
            .name = foreign_string(names[0]),
            .bytes = inputs[0],
        };
        const ForeignCommand command = {
            .arguments = &argument,
            .argument_count = 1,
        };
        foreign_test_decisions(tally, what, &command, &failure);
    }
    else
    {
        foreign_test_expect(tally, false, what, "the copy");
    }
    arena_pop_to(arena, mark);
}

static ImportResult foreign_test_place(ImportError error, uint32_t section, uint32_t symbol, uint32_t type, uint64_t offset)
{
    const ImportResult result = {
        .error = error,
        .section = section,
        .symbol = symbol,
        .type = type,
        .offset = offset,
    };
    return result;
}

// A command line's failure, resolved as it is (files on disk): its error. With a tally, the same failure on more
// lanes, each slow in turn (foreign_test_decisions).
static ForeignFailure foreign_test_failure_on(ForeignTestTally* tally, const char* what, char** arguments, uint32_t count, Arena* arena)
{
    ForeignCommand command;
    ForeignFailure result = foreign_arguments(&command, (int)count, arguments, arena);
    if (result.error == FOREIGN_ERROR_NONE)
    {
        Foreign foreign;
        foreign_test_watched = what != 0 ? what : "a refusal";
        alarm(BUSTER_FOREIGN_TEST_TIMEOUT_SECONDS);
        result = foreign_run(&foreign, &command, FOREIGN_PATH_VECTOR, 1, 0);
        alarm(0);
        foreign_release(&foreign);
        if (tally != 0)
        {
            foreign_test_decisions(tally, what, &command, &result);
        }
    }
    return result;
}

static ForeignFailure foreign_test_failure(char** arguments, uint32_t count, Arena* arena)
{
    return foreign_test_failure_on(0, 0, arguments, count, arena);
}

// lane_run starting no lane: a child limited to the processes it has can't start a thread, and the resolution must
// fail as FOREIGN_ERROR_LANES, with CPUs given and without, rather than succeed having done nothing. RLIMIT_NPROC
// doesn't limit root.
static void foreign_test_unstarted(ForeignTestTally* tally, char** arguments, uint32_t count, Arena* arena)
{
    ForeignCommand command;
    const bool parsed = foreign_arguments(&command, (int)count, arguments, arena).error == FOREIGN_ERROR_NONE;
    if (geteuid() == 0)
    {
        printf("  %s: lanes that don't start aren't checked as root, whom RLIMIT_NPROC doesn't limit\n", tally->check);
    }
    else
    {
        fflush(stdout);
        const pid_t child = parsed ? fork() : -1;
        if (child == 0)
        {
            const struct rlimit limit = {
                .rlim_cur = 0,
                .rlim_max = 0,
            };
            const uint32_t cpus[2] = { 0, 1 };
            const bool limited = setrlimit(RLIMIT_NPROC, &limit) == 0;
            Foreign foreign;
            const ForeignFailure pinned = foreign_run(&foreign, &command, FOREIGN_PATH_VECTOR, 2, cpus);
            foreign_release(&foreign);
            const ForeignFailure unpinned = foreign_run(&foreign, &command, FOREIGN_PATH_VECTOR, 2, 0);
            foreign_release(&foreign);
            _exit(limited && pinned.error == FOREIGN_ERROR_LANES && unpinned.error == FOREIGN_ERROR_LANES ? 0 : 1);
        }
        int status = 0;
        const bool waited = child > 0 && waitpid(child, &status, 0) == child;
        foreign_test_expect(tally, waited && WIFEXITED(status) && WEXITSTATUS(status) == 0, "lanes that don't start", "FOREIGN_ERROR_LANES with CPUs given and without");
    }
}

static bool foreign_test_refusals(const char* directory, Arena* arena)
{
    ForeignTestTally tally = {
        .check = "malformed",
    };
    char path[BUSTER_FOREIGN_TEST_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/rule_main.o", directory);
    const String8 object = foreign_test_read(arena, path);
    uint32_t index = 0;
    uint64_t offset = 0;
    ImportSymbols table;
    const bool found = object.pointer != 0 && foreign_test_find_symbol(object, "rule_data", &index, &offset, &table);
    foreign_test_expect(&tally, found, "rule_main.o and its symbol rule_data", 0);
    if (found)
    {
        const uint64_t info = offsetof(Elf64_Sym, st_info);
        const uint64_t shndx = offsetof(Elf64_Sym, st_shndx);
        const uint64_t value = offsetof(Elf64_Sym, st_value);
        const uint64_t name = offsetof(Elf64_Sym, st_name);
        foreign_test_malformed(&tally, "a local symbol among the global ones", object, offset + info, 1, ELF64_ST_INFO(STB_LOCAL, STT_OBJECT), FOREIGN_ERROR_MALFORMED,
                               foreign_test_place(IMPORT_ERROR_BOUNDS, 0, index, 0, offset), arena);
        foreign_test_malformed(&tally, "a global SECTION symbol", object, offset + info, 1, ELF64_ST_INFO(STB_GLOBAL, STT_SECTION), FOREIGN_ERROR_MALFORMED,
                               foreign_test_place(IMPORT_ERROR_UNSUPPORTED, 0, index, STT_SECTION, offset), arena);
        foreign_test_malformed(&tally, "a binding no symbol has", object, offset + info, 1, ELF64_ST_INFO(STB_HIPROC, STT_OBJECT), FOREIGN_ERROR_MALFORMED,
                               foreign_test_place(IMPORT_ERROR_UNSUPPORTED, 0, index, STB_HIPROC, offset), arena);
        foreign_test_malformed(&tally, "a section index past the table", object, offset + shndx, 2, 0xfeff, FOREIGN_ERROR_MALFORMED,
                               foreign_test_place(IMPORT_ERROR_BOUNDS, 0, index, 0, offset), arena);
        foreign_test_malformed(&tally, "a reserved section index", object, offset + shndx, 2, SHN_LOPROC, FOREIGN_ERROR_MALFORMED,
                               foreign_test_place(IMPORT_ERROR_UNSUPPORTED, 0, index, STT_OBJECT, offset), arena);
        foreign_test_malformed(&tally, "a common symbol aligned to 3", object, offset + value, 8, 3, FOREIGN_ERROR_MALFORMED,
                               foreign_test_place(IMPORT_ERROR_BOUNDS, 0, index, 0, offset), arena);
        foreign_test_malformed(&tally, "a name past the string table", object, offset + name, 4, table.names.length, FOREIGN_ERROR_MALFORMED,
                               foreign_test_place(IMPORT_ERROR_BOUNDS, 0, index, 0, offset), arena);
        foreign_test_malformed(&tally, "an empty name", object, offset + name, 4, 0, FOREIGN_ERROR_MALFORMED, foreign_test_place(IMPORT_ERROR_BOUNDS, 0, index, 0, offset), arena);
        uint32_t symtab = 0;
        uint64_t symtab_header = 0;
        if (foreign_test_find_section(object, SHT_SYMTAB, &symtab, &symtab_header))
        {
            foreign_test_malformed(&tally, "the first global past the symbol table", object, symtab_header + offsetof(Elf64_Shdr, sh_info), 4, table.count + 1, FOREIGN_ERROR_MALFORMED,
                                   foreign_test_place(IMPORT_ERROR_BOUNDS, symtab, 0, 0, symtab_header), arena);
        }
        uint32_t rela = 0;
        uint64_t rela_header = 0;
        if (foreign_test_find_section(object, SHT_RELA, &rela, &rela_header))
        {
            foreign_test_malformed(&tally, "a relocation section linked to another section", object, rela_header + offsetof(Elf64_Shdr, sh_link), 4, rela, FOREIGN_ERROR_MALFORMED,
                                   foreign_test_place(IMPORT_ERROR_BOUNDS, rela, 0, 0, rela_header), arena);
            foreign_test_malformed(&tally, "a relocation section for no section", object, rela_header + offsetof(Elf64_Shdr, sh_info), 4, 0, FOREIGN_ERROR_MALFORMED,
                                   foreign_test_place(IMPORT_ERROR_BOUNDS, rela, 0, 0, rela_header), arena);
            foreign_test_malformed(&tally, "an SHT_REL section", object, rela_header + offsetof(Elf64_Shdr, sh_type), 4, SHT_REL, FOREIGN_ERROR_MALFORMED,
                                   foreign_test_place(IMPORT_ERROR_UNSUPPORTED, rela, 0, SHT_REL, rela_header), arena);
        }
    }
    // Files and command lines.
    char missing[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char script[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char directory_option[BUSTER_FOREIGN_TEST_PATH_BYTES + 2];
    snprintf(missing, sizeof(missing), "%s/missing.o", directory);
    snprintf(script, sizeof(script), "%s/broken.so", directory);
    snprintf(directory_option, sizeof(directory_option), "-L%s", directory);
    static const char broken[] = "GROUP ( libc.so.6 \n /* unclosed";
    foreign_test_write(script, broken, sizeof(broken) - 1);
    char* missing_file[] = { missing };
    char* no_library[] = { directory_option, (char*)"-lnothing_by_this_name" };
    char* bad_script[] = { script };
    char* bad_option[] = { (char*)"--no-such-option" };
    ForeignFailure failure = foreign_test_failure_on(&tally, "a missing file", missing_file, 1, arena);
    foreign_test_expect(&tally, failure.error == FOREIGN_ERROR_FILE && failure.system_error == ENOENT && failure.input == 0, "a missing file", foreign_test_errors[failure.error]);
    failure = foreign_test_failure_on(&tally, "a -l name no directory has", no_library, 2, arena);
    foreign_test_expect(&tally, failure.error == FOREIGN_ERROR_NOT_FOUND && failure.input == 0, "a -l name no directory has", foreign_test_errors[failure.error]);
    // The unclosed comment is the failure's place.
    failure = foreign_test_failure_on(&tally, "a script that doesn't parse", bad_script, 1, arena);
    foreign_test_expect(&tally, failure.error == FOREIGN_ERROR_MALFORMED && failure.import.error == IMPORT_ERROR_SCRIPT && failure.import.offset == (uint64_t)(strstr(broken, "/*") - broken),
                        "a script that doesn't parse", foreign_test_errors[failure.error]);
    failure = foreign_test_failure(bad_option, 1, arena);
    foreign_test_expect(&tally, failure.error == FOREIGN_ERROR_ARGUMENTS && failure.argument == 0, "an unknown option", foreign_test_errors[failure.error]);
    // A FIFO: the open mustn't wait for a writer, and the input isn't a regular file.
    char fifo[BUSTER_FOREIGN_TEST_PATH_BYTES];
    snprintf(fifo, sizeof(fifo), "%s/input.fifo", directory);
    unlink(fifo);
    char* fifo_line[] = { fifo };
    const bool made_fifo = mkfifo(fifo, 0600) == 0;
    foreign_test_expect(&tally, made_fifo, "the FIFO", 0);
    if (made_fifo)
    {
        failure = foreign_test_failure_on(&tally, "a FIFO", fifo_line, 1, arena);
        foreign_test_expect(&tally, failure.error == FOREIGN_ERROR_FILE && failure.system_error == EINVAL && failure.input == 0, "a FIFO", foreign_test_errors[failure.error]);
    }
    // The review's case: an input of four bytes, the ELF magic alone, which the lane that opens it refuses, and lanes
    // that don't start.
    char truncated[4] = { 0x7f, 'E', 'L', 'F' };
    ForeignArgument truncated_argument = {
        .name = foreign_string("truncated.o"),
        .bytes = {
            .pointer = truncated,
            .length = sizeof(truncated),
        },
    };
    const ForeignCommand truncated_command = {
        .arguments = &truncated_argument,
        .argument_count = 1,
    };
    Foreign truncated_foreign;
    failure = foreign_run(&truncated_foreign, &truncated_command, FOREIGN_PATH_VECTOR, 1, 0);
    foreign_release(&truncated_foreign);
    foreign_test_expect(&tally, failure.error == FOREIGN_ERROR_FORMAT && failure.input == 0, "a truncated ELF header", foreign_test_errors[failure.error]);
    foreign_test_decisions(&tally, "a truncated ELF header", &truncated_command, &failure);
    char unstarted_object[BUSTER_FOREIGN_TEST_PATH_BYTES];
    snprintf(unstarted_object, sizeof(unstarted_object), "%s/rule_main.o", directory);
    char* unstarted_line[] = { unstarted_object };
    foreign_test_unstarted(&tally, unstarted_line, 1, arena);
    // Archives: a broken member refused only when it loads, a thin archive's missing member, a member that isn't one.
    char archive[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char member[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char main_object[BUSTER_FOREIGN_TEST_PATH_BYTES];
    snprintf(archive, sizeof(archive), "%s/broken.a", directory);
    snprintf(member, sizeof(member), "%s/ar_first_a.o", directory);
    snprintf(main_object, sizeof(main_object), "%s/ar_main.o", directory);
    char* members[] = { member };
    if (found && foreign_test_write_archive(archive, members, 1, false, arena))
    {
        String8 bytes = foreign_test_read(arena, archive);
        ImportArchiveIndex index;
        const bool indexed = bytes.pointer != 0 && import_archive_lazy_scalar(&index, bytes, arena).error == IMPORT_ERROR_NONE && index.member_count == 1;
        foreign_test_expect(&tally, indexed, "the archive to break", 0);
        if (indexed)
        {
            // The member's size field made letters: the archive reads, the member doesn't.
            memcpy(bytes.pointer + index.member_offsets[0] + 48, "xx", 2);
            foreign_test_write(archive, bytes.pointer, bytes.length);
            char* unused[] = { archive };
            failure = foreign_test_failure(unused, 1, arena);
            foreign_test_expect(&tally, failure.error == FOREIGN_ERROR_NONE, "a broken member nothing loads", foreign_test_errors[failure.error]);
            char* loads[] = { main_object, archive };
            failure = foreign_test_failure_on(&tally, "a broken member that loads", loads, 2, arena);
            foreign_test_expect(&tally, failure.error == FOREIGN_ERROR_MALFORMED && failure.input == 1 && failure.member == 1 && failure.import.offset == index.member_offsets[0],
                                "a broken member that loads", foreign_test_errors[failure.error]);
        }
    }
    char thin[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char moved[BUSTER_FOREIGN_TEST_PATH_BYTES];
    snprintf(thin, sizeof(thin), "%s/thin_missing.a", directory);
    snprintf(moved, sizeof(moved), "%s/ar_first_moved.o", directory);
    char* thin_members[] = { moved };
    String8 copied = foreign_test_read(arena, member);
    if (found && copied.pointer != 0 && foreign_test_write(moved, copied.pointer, copied.length) && foreign_test_write_archive(thin, thin_members, 1, true, arena) && unlink(moved) == 0)
    {
        char* loads[] = { main_object, thin };
        failure = foreign_test_failure_on(&tally, "a thin archive's missing member", loads, 2, arena);
        foreign_test_expect(&tally, failure.error == FOREIGN_ERROR_FILE && failure.system_error == ENOENT && failure.input == 1 && failure.member == 1, "a thin archive's missing member",
                            foreign_test_errors[failure.error]);
    }
    char odd_archive[BUSTER_FOREIGN_TEST_PATH_BYTES];
    snprintf(odd_archive, sizeof(odd_archive), "%s/odd.a", directory);
    // An archive whose member is an archive, which the index lists as defining first.
    char inner[BUSTER_FOREIGN_TEST_PATH_BYTES];
    snprintf(inner, sizeof(inner), "%s/inner.a", directory);
    char* inner_members[] = { member };
    if (found && foreign_test_write_archive(inner, inner_members, 1, false, arena))
    {
        String8 outer = foreign_test_read(arena, inner);
        ImportArchiveIndex index;
        if (outer.pointer != 0 && import_archive_lazy_scalar(&index, outer, arena).error == IMPORT_ERROR_NONE && index.member_count == 1)
        {
            // The member's bytes made an archive's magic.
            memcpy(outer.pointer + index.member_offsets[0] + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES, BUSTER_IMPORT_ARCHIVE_MAGIC, 8);
            foreign_test_write(odd_archive, outer.pointer, outer.length);
            char* loads[] = { main_object, odd_archive };
            failure = foreign_test_failure_on(&tally, "a member that isn't an object", loads, 2, arena);
            foreign_test_expect(&tally, failure.error == FOREIGN_ERROR_UNSUPPORTED && failure.input == 1 && failure.member == 1, "a member that isn't an object",
                                foreign_test_errors[failure.error]);
        }
    }
    return foreign_test_report(&tally);
}

// Page edges: each input in memory, ending at a PROT_NONE page.
static bool foreign_test_edges(const char* directory, uint64_t seed, Arena* arena)
{
    ForeignTestTally tally = {
        .check = "page edges",
    };
    char path[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char archive_path[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char member[BUSTER_FOREIGN_TEST_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/rule_main.o", directory);
    snprintf(archive_path, sizeof(archive_path), "%s/edge.a", directory);
    snprintf(member, sizeof(member), "%s/rule_data.o", directory);
    char* members[] = { member };
    const String8 object = foreign_test_read(arena, path);
    const bool archived = foreign_test_write_archive(archive_path, members, 1, false, arena);
    const String8 archive = archived ? foreign_test_read(arena, archive_path) : (String8){ 0 };
    // Zeroed, so that both release whether or not their mappings were made (&& skips the second after the first fails).
    ForeignTestEdge edge = { 0 };
    ForeignTestEdge second = { 0 };
    const bool mapped = object.pointer != 0 && archive.pointer != 0 && foreign_test_edge(&edge, object.length) && foreign_test_edge(&second, archive.length);
    foreign_test_expect(&tally, mapped, "the inputs and their pages", 0);
    uint64_t state = seed ^ 0x6a09e667f3bcc909ull;
    uint64_t prefixes = 0;
    uint64_t changes = 0;
    for (uint64_t length = 0; mapped && length <= object.length; length += 1)
    {
        // Every prefix of the object, alone and before the whole archive.
        memcpy(edge.end - length, object.pointer, length);
        memcpy(second.end - archive.length, archive.pointer, archive.length);
        const char* names[] = { "edge.o", "edge.a" };
        const String8 inputs[] = {
            { (char*)edge.end - length, length },
            { (char*)second.end - archive.length, archive.length },
        };
        ForeignFailure failure;
        char detail[64];
        snprintf(detail, sizeof(detail), "a prefix of %llu bytes", (unsigned long long)length);
        foreign_test_expect(&tally, foreign_test_given(names, inputs, 2, &failure, arena), "the paths differ", detail);
        prefixes += 1;
    }
    for (uint64_t length = 0; mapped && length <= archive.length; length += 1)
    {
        // Every prefix of the archive, after the whole object, which needs its member.
        memcpy(edge.end - object.length, object.pointer, object.length);
        memcpy(second.end - length, archive.pointer, length);
        const char* names[] = { "edge.o", "edge.a" };
        const String8 inputs[] = {
            { (char*)edge.end - object.length, object.length },
            { (char*)second.end - length, length },
        };
        ForeignFailure failure;
        char detail[64];
        snprintf(detail, sizeof(detail), "an archive's prefix of %llu bytes", (unsigned long long)length);
        foreign_test_expect(&tally, foreign_test_given(names, inputs, 2, &failure, arena), "the paths differ", detail);
        prefixes += 1;
    }
    for (uint64_t at = 0; mapped && at < object.length; at += 1)
    {
        for (uint32_t kind = 0; kind < 3; kind += 1)
        {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            memcpy(edge.end - object.length, object.pointer, object.length);
            edge.end[(int64_t)at - (int64_t)object.length] = kind == 0 ? 0 : kind == 1 ? 0xff : (uint8_t)(state >> 56);
            const char* names[] = { "edge.o" };
            const String8 inputs[] = {
                { (char*)edge.end - object.length, object.length },
            };
            ForeignFailure failure;
            char detail[64];
            snprintf(detail, sizeof(detail), "byte %llu changed", (unsigned long long)at);
            foreign_test_expect(&tally, foreign_test_given(names, inputs, 1, &failure, arena), "the paths differ", detail);
            changes += 1;
        }
    }
    printf("page edges: %llu prefixes, %llu changed bytes\n", (unsigned long long)prefixes, (unsigned long long)changes);
    foreign_test_edge_release(&edge);
    foreign_test_edge_release(&second);
    return foreign_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

// ---------------------------------------------------------------------------------------------------------------------
// Extended section indices
//
// An object made by hand of 65,527 sections, past SHN_LORESERVE (so e_shnum and e_shstrndx are in section 0), its
// global symbols in sections 65522 and 65521 through SHN_XINDEX, the values of SHN_COMMON and SHN_ABS, beside an absolute
// symbol and a common one: each must be what its st_shndx says, the first two ordinary definitions. With a second
// object that defines the first strongly too, it is a duplicate.

typedef struct ForeignTestSymbol ForeignTestSymbol;
struct ForeignTestSymbol
{
    const char* name;
    uint8_t info;
    uint16_t shndx;
    uint32_t extended;
    uint64_t value;
    uint64_t size;
};

// Writes a relocatable object of `section_count` sections: empty PROGBITS but for the last four, its symbol table, its
// strings, its extended indices and the section names, with `symbols` after the null symbol, all global.
static bool foreign_test_write_sections(const char* path, uint32_t section_count, const ForeignTestSymbol* symbols, uint32_t symbol_count, Arena* arena)
{
    const uint64_t mark = arena->length;
    static const char section_names[] = "\0.x\0.symtab\0.strtab\0.symtab_shndx\0.shstrtab";
    const uint32_t symtab = section_count - 4;
    const uint32_t strtab = section_count - 3;
    const uint32_t shndx = section_count - 2;
    const uint32_t shstrtab = section_count - 1;
    uint64_t string_bytes = 1;
    for (uint32_t i = 0; i < symbol_count; i += 1)
    {
        string_bytes += strlen(symbols[i].name) + 1;
    }
    const uint64_t names_at = sizeof(Elf64_Ehdr);
    const uint64_t strings_at = names_at + sizeof(section_names);
    const uint64_t symbols_at = (strings_at + string_bytes + 7) & ~7ull;
    const uint64_t shndx_at = symbols_at + (uint64_t)(symbol_count + 1) * sizeof(Elf64_Sym);
    const uint64_t headers_at = (shndx_at + (uint64_t)(symbol_count + 1) * sizeof(uint32_t) + 7) & ~7ull;
    const uint64_t size = headers_at + (uint64_t)section_count * sizeof(Elf64_Shdr);
    uint8_t* bytes = arena_push(arena, size, 64);
    bool result = bytes != 0;
    if (result)
    {
        memset(bytes, 0, size);
        Elf64_Ehdr header = { 0 };
        memcpy(header.e_ident, ELFMAG, SELFMAG);
        header.e_ident[EI_CLASS] = ELFCLASS64;
        header.e_ident[EI_DATA] = ELFDATA2LSB;
        header.e_ident[EI_VERSION] = EV_CURRENT;
        header.e_type = ET_REL;
        header.e_machine = EM_X86_64;
        header.e_version = EV_CURRENT;
        header.e_shoff = headers_at;
        header.e_ehsize = sizeof(Elf64_Ehdr);
        header.e_shentsize = sizeof(Elf64_Shdr);
        header.e_shnum = section_count < SHN_LORESERVE ? (uint16_t)section_count : 0;
        header.e_shstrndx = section_count < SHN_LORESERVE ? (uint16_t)shstrtab : SHN_XINDEX;
        memcpy(bytes, &header, sizeof(header));
        memcpy(bytes + names_at, section_names, sizeof(section_names));
        uint64_t string_at = 1;
        for (uint32_t i = 0; i < symbol_count; i += 1)
        {
            const uint64_t length = strlen(symbols[i].name);
            memcpy(bytes + strings_at + string_at, symbols[i].name, length);
            const Elf64_Sym symbol = {
                .st_name = (uint32_t)string_at,
                .st_info = symbols[i].info,
                .st_other = STV_DEFAULT,
                .st_shndx = symbols[i].shndx,
                .st_value = symbols[i].value,
                .st_size = symbols[i].size,
            };
            memcpy(bytes + symbols_at + (uint64_t)(i + 1) * sizeof(Elf64_Sym), &symbol, sizeof(symbol));
            memcpy(bytes + shndx_at + (uint64_t)(i + 1) * sizeof(uint32_t), &symbols[i].extended, sizeof(uint32_t));
            string_at += length + 1;
        }
        // Section 0 holds the count and the names' index past SHN_LORESERVE; the others are empty PROGBITS, but the tables.
        for (uint32_t i = 0; i < section_count; i += 1)
        {
            Elf64_Shdr section = { 0 };
            if (i == 0)
            {
                section.sh_size = section_count < SHN_LORESERVE ? 0 : section_count;
                section.sh_link = section_count < SHN_LORESERVE ? 0 : shstrtab;
            }
            else if (i < symtab)
            {
                section.sh_name = 1;
                section.sh_type = SHT_PROGBITS;
                section.sh_flags = SHF_ALLOC;
                section.sh_addralign = 1;
            }
            else if (i == symtab)
            {
                section.sh_name = 4;
                section.sh_type = SHT_SYMTAB;
                section.sh_offset = symbols_at;
                section.sh_size = (uint64_t)(symbol_count + 1) * sizeof(Elf64_Sym);
                section.sh_link = strtab;
                section.sh_info = 1;
                section.sh_addralign = 8;
                section.sh_entsize = sizeof(Elf64_Sym);
            }
            else if (i == strtab)
            {
                section.sh_name = 12;
                section.sh_type = SHT_STRTAB;
                section.sh_offset = strings_at;
                section.sh_size = string_bytes;
                section.sh_addralign = 1;
            }
            else if (i == shndx)
            {
                section.sh_name = 20;
                section.sh_type = SHT_SYMTAB_SHNDX;
                section.sh_offset = shndx_at;
                section.sh_size = (uint64_t)(symbol_count + 1) * sizeof(uint32_t);
                section.sh_link = symtab;
                section.sh_addralign = 4;
                section.sh_entsize = sizeof(uint32_t);
            }
            else
            {
                section.sh_name = 34;
                section.sh_type = SHT_STRTAB;
                section.sh_offset = names_at;
                section.sh_size = sizeof(section_names);
                section.sh_addralign = 1;
            }
            memcpy(bytes + headers_at + (uint64_t)i * sizeof(Elf64_Shdr), &section, sizeof(section));
        }
        result = foreign_test_write(path, bytes, size);
    }
    arena_pop_to(arena, mark);
    return result;
}

// A name's resolution and its counts in a resolution, as the test expects them.
static void foreign_test_expect_name(ForeignTestTally* tally, const char* what, const Foreign* foreign, const char* text, uint8_t resolution, uint32_t definitions)
{
    const uint32_t id = foreign_test_name(foreign, text);
    char detail[256];
    snprintf(detail, sizeof(detail), "%s: %s with %u strong definitions", text, id != 0 ? foreign_test_resolutions[foreign->names.resolution[id - 1]] : "none",
             id != 0 ? foreign->names.definitions[id - 1] : 0);
    foreign_test_expect(tally, id != 0 && foreign->names.resolution[id - 1] == resolution && foreign->names.definitions[id - 1] == definitions, what, detail);
}

static bool foreign_test_extended(const char* directory, Arena* arena)
{
    ForeignTestTally tally = {
        .check = "extended sections",
    };
    static const ForeignTestSymbol many[] = {
        { "extended", ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT), SHN_XINDEX, SHN_COMMON, 0, 4 },
        { "extended_abs_number", ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT), SHN_XINDEX, SHN_ABS, 0, 4 },
        { "true_abs", ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE), SHN_ABS, 0, 42, 0 },
        { "true_common", ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT), SHN_COMMON, 0, 8, 4 },
    };
    static const ForeignTestSymbol again[] = {
        { "extended", ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT), 1, 0, 0, 4 },
    };
    char path[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char second[BUSTER_FOREIGN_TEST_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/extended.o", directory);
    snprintf(second, sizeof(second), "%s/extended_again.o", directory);
    const bool written = foreign_test_write_sections(path, SHN_COMMON + 5, many, BUSTER_ARRAY_LENGTH(many), arena) &&
                         foreign_test_write_sections(second, 6, again, BUSTER_ARRAY_LENGTH(again), arena);
    foreign_test_expect(&tally, written, "the objects", 0);
    char* alone[] = { path };
    char* both[] = { path, second };
    ForeignCommand command;
    if (written && foreign_test_command(&command, alone, 1, arena))
    {
        Foreign foreign = { 0 };
        foreign_test_resolve(&tally, "an object of 65,527 sections", &command, &foreign, arena);
        foreign_test_expect(&tally, foreign.failure.error == FOREIGN_ERROR_NONE, "an object of 65,527 sections", foreign_test_errors[foreign.failure.error]);
        foreign_test_expect_name(&tally, "an object of 65,527 sections", &foreign, "extended", FOREIGN_RESOLUTION_STRONG, 1);
        foreign_test_expect_name(&tally, "an object of 65,527 sections", &foreign, "extended_abs_number", FOREIGN_RESOLUTION_STRONG, 1);
        foreign_test_expect_name(&tally, "an object of 65,527 sections", &foreign, "true_abs", FOREIGN_RESOLUTION_STRONG, 1);
        foreign_test_expect_name(&tally, "an object of 65,527 sections", &foreign, "true_common", FOREIGN_RESOLUTION_COMMON, 0);
        foreign_release(&foreign);
    }
    if (written && foreign_test_command(&command, both, 2, arena))
    {
        Foreign foreign = { 0 };
        foreign_test_resolve(&tally, "a second definition", &command, &foreign, arena);
        foreign_test_expect_name(&tally, "a second definition", &foreign, "extended", FOREIGN_RESOLUTION_STRONG, 2);
        foreign_release(&foreign);
    }
    return foreign_test_report(&tally);
}

#include "buster/compiler/foreign_link_test.c"
#include "buster/compiler/foreign_dynamic_test.c"
#include "buster/compiler/foreign_stack_test.c"
#include "buster/compiler/foreign_unwind_test.c"
#include "buster/compiler/foreign_unwind_runtime_test.c"
#include "buster/compiler/foreign_gc_test.c"
#include "buster/compiler/foreign_gc_link_test.c"
#include "buster/compiler/foreign_strings_test.c"
#include "buster/compiler/foreign_strings_output_test.c"
#include "buster/compiler/foreign_strings_benchmark.c"

static int run_foreign_tests(int argc, char** argv)
{
    uint64_t seed = 1;
    uint32_t functions = 2000;
    bool cross_check = false;
    bool keep = false;
    const char* base = getenv("TMPDIR") != 0 ? getenv("TMPDIR") : "/tmp";
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        const bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--seed") == 0 && has_value)
        {
            seed = strtoull(argv[i + 1], 0, 0);
            i += 1;
        }
        else if (strcmp(argv[i], "--functions") == 0 && has_value)
        {
            functions = (uint32_t)strtoul(argv[i + 1], 0, 0);
            i += 1;
        }
        else if (strcmp(argv[i], "--directory") == 0 && has_value)
        {
            base = argv[i + 1];
            i += 1;
        }
        else if (strcmp(argv[i], "--cross-check") == 0)
        {
            cross_check = true;
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else
        {
            valid = false;
        }
    }
    char directory[BUSTER_FOREIGN_TEST_PATH_BYTES];
    snprintf(directory, sizeof(directory), "%s/buster-foreign-XXXXXX", base);
    Arena arena = arena_reserve(BUSTER_FOREIGN_TEST_ARENA_BYTES);
    struct sigaction watchdog = { .sa_handler = foreign_test_report_hang };
    bool passed = valid;
    if (!valid)
    {
        printf("usage: test-foreign [--seed N] [--functions N] [--directory PATH] [--cross-check] [--keep]\n");
    }
    else if (arena.base == 0 || mkdtemp(directory) == 0)
    {
        printf("test-foreign: no room for the test's arena or directory\n");
        passed = false;
    }
    else if (sigaction(SIGALRM, &watchdog, 0) != 0)
    {
        printf("test-foreign: the watchdog's signal handler couldn't be installed\n");
        passed = false;
    }
    else
    {
        ForeignTestSystem system;
        foreign_test_system(&system);
        if (!system.found)
        {
            printf("test-foreign: the system's C runtime isn't where GCC's driver finds it, so the programs that call libc aren't resolved\n");
        }
        passed = foreign_test_programs(seed, functions, cross_check, directory, &system, &arena) && passed;
        passed = foreign_test_compiled(cross_check, directory, &system, &arena) && passed;
        passed = foreign_test_refusals(directory, &arena) && passed;
        passed = foreign_test_edges(directory, seed, &arena) && passed;
        passed = foreign_test_extended(directory, &arena) && passed;
        passed = foreign_link_test(directory, &arena, cross_check) && passed;
        passed = foreign_dynamic_test(directory, &arena, cross_check, &system) && passed;
        passed = foreign_stack_test(directory, &arena) && passed;
        passed = foreign_unwind_test() && passed;
        passed = foreign_unwind_runtime_test(directory, &arena, cross_check, &system) && passed;
        passed = foreign_gc_test() && passed;
        passed = foreign_gc_link_test(directory, &arena, cross_check) && passed;
        passed = foreign_strings_test(seed) && passed;
        passed = foreign_strings_output_test(directory, &arena, cross_check) && passed;
        if (!keep)
        {
            char* remove[] = {
                (char*)"rm",
                (char*)"-rf",
                directory,
                0,
            };
            char output[BUSTER_FOREIGN_TEST_PATH_BYTES + 16];
            snprintf(output, sizeof(output), "%s.rm", directory);
            object_test_run(remove, "/dev/null", "/dev/null");
            (void)output;
        }
        else
        {
            printf("test-foreign: kept %s\n", directory);
        }
    }
    arena_release(&arena);
    return passed ? 0 : 1;
}

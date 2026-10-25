// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #129's oracle: the functions and objects an ELF relocatable object defines, to compare what -c lowers with what
// clang -O0 -c's object defines. clang emits no unreferenced static or inline function, at any level, nor an object
// that only such a function refers to; and it emits every static local of a function it emits.
//
// lowering_symbols_read reads the object with the import path's reader (import_object) into a Link of its own. A
// symbol counts when it is defined in a section the reader keeps and is a function, an object, a thread-local or an
// IFUNC, with its binding, but not when its name starts with ".L". Those are assembler-local labels, which an object
// keeps only where a relocation into a mergeable section needs one: clang's string literals (.L.str) and ours
// (.Lbuster_string_N). A local's name loses a final ".N" (decimal digits after a dot that isn't its first byte):
// LLVM makes a repeated local name unique so, by one counter over the whole module, string literals' names among
// them, which another compiler doesn't reproduce; check_program_emit adds its own.
//
// The reader also lists the strings of the object's mergeable string sections (BUSTER_LINK_CLASS_STRINGS, which the
// reader gives sections of 1-byte entries), each once: the string literals it emits. clang's -O0 code drops a
// constant condition's dead arm, and a literal only that arm refers to, where ours keeps both, so only fixtures that
// have no such arm compare them (lowering_symbols_compare_strings).
//
// lowering_symbols_compare takes both lists, sorted by name, type and binding, and gives the entries that one of them
// has more of than the other, the two multisets' differences, as records and as text. This is test tooling outside the compile path, with no
// vector path. main.c includes it after import.c and object_writer_test.c, whose file reader it takes.

#ifndef BUSTER_C_LOWERING_SYMBOLS_INCLUDED
#define BUSTER_C_LOWERING_SYMBOLS_INCLUDED 1

typedef struct LoweringSymbol LoweringSymbol;
struct LoweringSymbol
{
    // The name as compared (in the reading link's name bytes), without a local's uniquing suffix.
    const uint8_t* name;
    uint32_t length;
    // BUSTER_LINK_SYMBOL_FUNCTION, _OBJECT, _TLS or _IFUNC, and _LOCAL, _GLOBAL or _WEAK.
    uint8_t type;
    uint8_t binding;
};

typedef struct LoweringSymbols LoweringSymbols;
struct LoweringSymbols
{
    LoweringSymbol* items;
    uint32_t count;
    // The string literals, sorted, each once.
    String8* strings;
    uint32_t string_count;
    // What the reader said, for an object it couldn't read.
    ImportResult import;
};

static int lowering_symbols_string_order(const void* a, const void* b)
{
    const String8* x = a;
    const String8* y = b;
    const uint64_t common = x->length < y->length ? x->length : y->length;
    int result = common != 0 ? memcmp(x->pointer, y->pointer, common) : 0;
    return result != 0 ? result : (x->length > y->length) - (x->length < y->length);
}

static int lowering_symbols_order(const void* a, const void* b)
{
    const LoweringSymbol* x = a;
    const LoweringSymbol* y = b;
    const uint32_t common = x->length < y->length ? x->length : y->length;
    int result = common != 0 ? memcmp(x->name, y->name, common) : 0;
    result = result != 0 ? result : (x->length > y->length) - (x->length < y->length);
    result = result != 0 ? result : (int)x->type - (int)y->type;
    result = result != 0 ? result : (int)x->binding - (int)y->binding;
    return result;
}

// The entities the object `bytes` defines, sorted, into `result`, every record in `arena`: whether it could be read.
// On a failure the result is empty but for `import`, the reader's error, and the arena keeps what was pushed.
static bool lowering_symbols_read(LoweringSymbols* result, String8 bytes, Arena* arena)
{
    *result = (LoweringSymbols){ 0 };
    LinkCapacity capacity = { 0 };
    Link link = { 0 };
    ImportObject object = { 0 };
    result->import = import_object_capacity(bytes, &capacity);
    bool valid = result->import.error == IMPORT_ERROR_NONE && link_initialize(&link, arena, arena, capacity);
    if (valid)
    {
        result->import = import_object(&link, bytes, 0, BUSTER_LINK_SOURCE_FOREIGN_OBJECT, arena, &object);
        valid = result->import.error == IMPORT_ERROR_NONE;
    }
    result->items = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, LoweringSymbol, link.symbols.count) : 0;
    valid = valid && result->items != 0;
    for (SymbolId symbol = 1; valid && symbol < link.symbols.count; symbol += 1)
    {
        const uint8_t flags = link.symbols.flags[symbol];
        const uint8_t type = flags & BUSTER_LINK_SYMBOL_TYPE;
        const uint8_t binding = flags & BUSTER_LINK_SYMBOL_BINDING;
        const NameId name = link.symbols.name[symbol];
        const uint8_t* spelling = name != 0 ? link.names.bytes[name] : 0;
        uint32_t length = name != 0 ? link.names.length[name] : 0;
        const bool entity = type == BUSTER_LINK_SYMBOL_FUNCTION || type == BUSTER_LINK_SYMBOL_OBJECT || type == BUSTER_LINK_SYMBOL_TLS ||
                            type == BUSTER_LINK_SYMBOL_IFUNC;
        const bool label = length >= 2 && spelling[0] == '.' && spelling[1] == 'L';
        if (link.symbols.atom[symbol] != 0 && entity && length != 0 && !label)
        {
            uint32_t digits = 0;
            while (binding == BUSTER_LINK_SYMBOL_LOCAL && digits < length && spelling[length - 1 - digits] >= '0' && spelling[length - 1 - digits] <= '9')
            {
                digits += 1;
            }
            // "name.N" keeps "name"; a name that is only ".N" or "N", or digits after anything but a dot, stays whole.
            if (digits != 0 && digits + 1 < length && spelling[length - 1 - digits] == '.')
            {
                length -= digits + 1;
            }
            result->items[result->count++] = (LoweringSymbol){ .name = spelling, .length = length, .type = type, .binding = binding };
        }
    }
    // Every byte of a string section may end a string, so there are at most as many strings as bytes.
    uint64_t string_bytes = 0;
    for (AtomId atom = 1; valid && atom < link.atoms.count; atom += 1)
    {
        string_bytes += link.atoms.class[atom] == BUSTER_LINK_CLASS_STRINGS ? link.atoms.size[atom] : 0;
    }
    result->strings = valid && string_bytes != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, String8, string_bytes) : 0;
    valid = valid && (string_bytes == 0 || result->strings != 0);
    for (AtomId atom = 1; valid && atom < link.atoms.count; atom += 1)
    {
        const uint8_t* bytes_of_atom = link.atoms.bytes[atom];
        const uint64_t size = link.atoms.size[atom];
        uint64_t start = 0;
        for (uint64_t i = 0; link.atoms.class[atom] == BUSTER_LINK_CLASS_STRINGS && i < size; i += 1)
        {
            // A string runs to its NUL; bytes after the last NUL (which a valid section hasn't) make no string.
            if (bytes_of_atom[i] == 0)
            {
                result->strings[result->string_count++] = (String8){ .pointer = (char*)bytes_of_atom + start, .length = i - start };
                start = i + 1;
            }
        }
    }
    if (valid)
    {
        qsort(result->items, result->count, sizeof(LoweringSymbol), lowering_symbols_order);
        qsort(result->strings, result->string_count, sizeof(String8), lowering_symbols_string_order);
        uint32_t unique = 0;
        for (uint32_t i = 0; i < result->string_count; i += 1)
        {
            const bool repeated = unique != 0 && lowering_symbols_string_order(result->strings + unique - 1, result->strings + i) == 0;
            result->strings[unique] = result->strings[i];
            unique += !repeated;
        }
        result->string_count = unique;
    }
    else
    {
        *result = (LoweringSymbols){ .import = result->import };
    }
    return valid;
}

// lowering_symbols_read of the file at `path`; an unreadable file gives IMPORT_ERROR_BOUNDS.
static bool lowering_symbols_read_file(LoweringSymbols* result, const char* path, Arena* arena)
{
    const String8 bytes = object_test_read_file(arena, path);
    bool valid = bytes.pointer != 0;
    if (valid)
    {
        valid = lowering_symbols_read(result, bytes, arena);
    }
    else
    {
        *result = (LoweringSymbols){ .import = { .error = IMPORT_ERROR_BOUNDS } };
    }
    return valid;
}

// A difference: an entry of the first list (`first`) or of the second that the other hasn't.
typedef struct LoweringDifference LoweringDifference;
struct LoweringDifference
{
    const LoweringSymbol* symbol;
    bool first;
};

static const char* lowering_symbols_type_name(uint8_t type)
{
    return type == BUSTER_LINK_SYMBOL_FUNCTION ? "FUNC" : type == BUSTER_LINK_SYMBOL_OBJECT ? "OBJECT" : type == BUSTER_LINK_SYMBOL_TLS ? "TLS" : "IFUNC";
}

static const char* lowering_symbols_binding_name(uint8_t binding)
{
    return binding == BUSTER_LINK_SYMBOL_LOCAL ? "LOCAL" : binding == BUSTER_LINK_SYMBOL_GLOBAL ? "GLOBAL" : "WEAK";
}

// The entries of `a` and `b` (each sorted by lowering_symbols_read) that one has more of than the other: how many. The
// first `difference_capacity` of them go into `differences` (which may be null for none), and each that fits in
// `report`'s `capacity` bytes goes there as "<label> only: TYPE BINDING name" on a line of its own, NUL-terminated (a
// capacity of 0 lists none).
static uint32_t lowering_symbols_compare(const LoweringSymbols* a, const char* a_label, const LoweringSymbols* b, const char* b_label,
                                         LoweringDifference* differences_out, uint32_t difference_capacity, char* report, uint64_t capacity)
{
    uint32_t differences = 0;
    uint64_t written = 0;
    uint32_t i = 0;
    uint32_t j = 0;
    if (capacity != 0)
    {
        report[0] = 0;
    }
    while (i < a->count || j < b->count)
    {
        const int order = i == a->count ? 1 : j == b->count ? -1 : lowering_symbols_order(a->items + i, b->items + j);
        if (order == 0)
        {
            i += 1;
            j += 1;
        }
        else
        {
            const LoweringSymbol* only = order < 0 ? a->items + i : b->items + j;
            const char* label = order < 0 ? a_label : b_label;
            if (differences < difference_capacity)
            {
                differences_out[differences] = (LoweringDifference){ .symbol = only, .first = order < 0 };
            }
            i += order < 0;
            j += order > 0;
            differences += 1;
            if (written + 1 < capacity)
            {
                const int length = snprintf(report + written, capacity - written, "%s only: %s %s %.*s\n", label, lowering_symbols_type_name(only->type),
                                            lowering_symbols_binding_name(only->binding), (int)only->length, (const char*)only->name);
                written += length > 0 ? (uint64_t)length : 0;
                written = written < capacity ? written : capacity - 1;
            }
        }
    }
    return differences;
}

// The strings `a` or `b` has and the other hasn't: how many, listed in `report` as lowering_symbols_compare lists its
// entries, each as "<label> only: string \"...\"" (bytes outside printable ASCII as \xNN).
static uint32_t lowering_symbols_compare_strings(const LoweringSymbols* a, const char* a_label, const LoweringSymbols* b, const char* b_label,
                                                 char* report, uint64_t capacity)
{
    uint32_t differences = 0;
    uint64_t written = 0;
    uint32_t i = 0;
    uint32_t j = 0;
    if (capacity != 0)
    {
        report[0] = 0;
    }
    while (i < a->string_count || j < b->string_count)
    {
        const int order = i == a->string_count ? 1 : j == b->string_count ? -1 : lowering_symbols_string_order(a->strings + i, b->strings + j);
        if (order == 0)
        {
            i += 1;
            j += 1;
        }
        else
        {
            const String8 only = order < 0 ? a->strings[i] : b->strings[j];
            const char* label = order < 0 ? a_label : b_label;
            i += order < 0;
            j += order > 0;
            differences += 1;
            int length = written + 1 < capacity ? snprintf(report + written, capacity - written, "%s only: string \"", label) : 0;
            written += length > 0 ? (uint64_t)length : 0;
            for (uint64_t k = 0; k < only.length && written + 1 < capacity; k += 1)
            {
                const uint8_t c = (uint8_t)only.pointer[k];
                length = c >= 0x20 && c < 0x7f && c != '"' && c != '\\' ? snprintf(report + written, capacity - written, "%c", c) :
                                                                          snprintf(report + written, capacity - written, "\\x%02x", c);
                written += length > 0 ? (uint64_t)length : 0;
            }
            length = written + 1 < capacity ? snprintf(report + written, capacity - written, "\"\n") : 0;
            written += length > 0 ? (uint64_t)length : 0;
            written = written < capacity ? written : (capacity != 0 ? capacity - 1 : 0);
        }
    }
    return differences;
}

#endif

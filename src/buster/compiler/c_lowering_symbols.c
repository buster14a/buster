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
// (.Lbuster_string_N).
//
// Names compare exactly, but for the ones a compiler makes and makes unique. A block-scope static is "function.name"
// and a file-scope compound literal ".compoundliteral" (LoweringNames, from the checked source), and a name made a
// second time gets ".N": LLVM's N comes from one counter over the whole module, which string literals' names advance
// too, and check_program_emit's from a counter of its own, so the numbers differ. A local named by one of the source's
// bases, then "." and N (decimal digits from 1 up), is compared as the base and the fact that it had a suffix, unless
// one of the source's asm labels spells it: a label is the user's name, which must be exact. An identifier has no
// dot, so only a label could spell such a name. Without the source's names (test-c-testsuite, whose clang objects
// make no name twice), every name compares exactly.
//
// The reader also lists the strings of the object's mergeable string sections (BUSTER_LINK_CLASS_STRINGS, which the
// reader gives sections of 1-byte entries), each once: the string literals it emits. clang's -O0 code drops a
// constant condition's dead arm, and a literal only that arm refers to, where ours keeps both, so only fixtures that
// have no such arm compare them (lowering_symbols_compare_strings).
//
// lowering_symbols_compare takes both lists, sorted by name, suffix, type and binding, and gives the entries that one
// of them has more of than the other, the two multisets' differences, as records and as text. This is test tooling
// outside the compile path, with no vector path. main.c includes it after c_check_program.c, import.c and
// object_writer_test.c, whose file reader it takes.

#ifndef BUSTER_C_LOWERING_SYMBOLS_INCLUDED
#define BUSTER_C_LOWERING_SYMBOLS_INCLUDED 1

typedef struct LoweringSymbol LoweringSymbol;
struct LoweringSymbol
{
    // The name as compared (in the reading link's name bytes): a made name without its uniquing suffix, which
    // `uniqued` says it had, and any other whole. `spelled` is its length with the suffix, for reports.
    const uint8_t* name;
    uint32_t length;
    uint32_t spelled;
    // BUSTER_LINK_SYMBOL_FUNCTION, _OBJECT, _TLS or _IFUNC, and _LOCAL, _GLOBAL or _WEAK.
    uint8_t type;
    uint8_t binding;
    bool uniqued;
};

// What a unit's source says about its objects' names: the asm labels it spells, and the bases of the names a compiler
// makes for its local definitions that have none of their own, each sorted by lowering_symbols_string_order.
typedef struct LoweringNames LoweringNames;
struct LoweringNames
{
    String8* labels;
    uint32_t label_count;
    String8* bases;
    uint32_t base_count;
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
    result = result != 0 ? result : (int)x->uniqued - (int)y->uniqued;
    result = result != 0 ? result : (int)x->type - (int)y->type;
    result = result != 0 ? result : (int)x->binding - (int)y->binding;
    return result;
}

// How the name `spelling` of `length` bytes, a symbol's of binding `binding`, compares: without a final ".N" when it
// is a local's and `names` shows that a compiler made it (a base, then the suffix, and no label), with `uniqued` set,
// and whole otherwise: its length as compared. N has no leading zero, as neither counter writes one.
static uint32_t lowering_symbols_compared(const LoweringNames* names, const uint8_t* spelling, uint32_t length, uint8_t binding, bool* uniqued)
{
    uint32_t digits = 0;
    while (names != 0 && binding == BUSTER_LINK_SYMBOL_LOCAL && digits < length && spelling[length - 1 - digits] >= '0' &&
           spelling[length - 1 - digits] <= '9')
    {
        digits += 1;
    }
    const uint32_t base = digits != 0 && digits + 1 < length && spelling[length - 1 - digits] == '.' && spelling[length - digits] != '0' ?
                          length - 1 - digits : length;
    const String8 key = { .pointer = (char*)spelling, .length = base };
    const String8 whole = { .pointer = (char*)spelling, .length = length };
    const bool made = names != 0 && base != length && names->base_count != 0 &&
                      bsearch(&key, names->bases, names->base_count, sizeof(String8), lowering_symbols_string_order) != 0 &&
                      (names->label_count == 0 || bsearch(&whole, names->labels, names->label_count, sizeof(String8), lowering_symbols_string_order) == 0);
    *uniqued = made;
    return made ? base : length;
}

// The entities the object `bytes` defines, sorted, into `result`, every record in `arena`: whether it could be read.
// `names` are its source's (lowering_names_of_input), or null to compare every name whole. On a failure the result is
// empty but for `import`, the reader's error, and the arena keeps what was pushed.
static bool lowering_symbols_read(LoweringSymbols* result, String8 bytes, const LoweringNames* names, Arena* arena)
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
        const uint32_t length = name != 0 ? link.names.length[name] : 0;
        const bool entity = type == BUSTER_LINK_SYMBOL_FUNCTION || type == BUSTER_LINK_SYMBOL_OBJECT || type == BUSTER_LINK_SYMBOL_TLS ||
                            type == BUSTER_LINK_SYMBOL_IFUNC;
        const bool label = length >= 2 && spelling[0] == '.' && spelling[1] == 'L';
        if (link.symbols.atom[symbol] != 0 && entity && length != 0 && !label)
        {
            bool uniqued = false;
            const uint32_t compared = lowering_symbols_compared(names, spelling, length, binding, &uniqued);
            result->items[result->count++] = (LoweringSymbol){ .name = spelling, .length = compared, .spelled = length, .type = type,
                                                               .binding = binding, .uniqued = uniqued };
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
static bool lowering_symbols_read_file(LoweringSymbols* result, const char* path, const LoweringNames* names, Arena* arena)
{
    const String8 bytes = object_test_read_file(arena, path);
    bool valid = bytes.pointer != 0;
    if (valid)
    {
        valid = lowering_symbols_read(result, bytes, names, arena);
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
// capacity of 0 lists none), the name as the object spells it.
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
                                            lowering_symbols_binding_name(only->binding), (int)only->spelled, (const char*)only->name);
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

// The names of the unit `input` checked (LoweringNames), every record in `arena`: whether there was room. A label is
// an asm label of a declaration that isn't a register variable's and that the checker didn't ignore. A block-scope
// static without a label of its own gets the base "function.name": its function's label, from any of its
// declarations, or its identifier, then its own, as check_program_emit names it (CheckProgramNames). A unit with a
// compound literal gets ".compoundliteral".
static bool lowering_names_of_input(LoweringNames* result, const CheckInput* input, const Parser* parser, const InternTable* identifiers, Arena* arena)
{
    *result = (LoweringNames){ 0 };
    const uint32_t count = input->token_count;
    uint64_t labels = 0;
    bool compound = false;
    for (uint32_t token = 0; token < count; token += 1)
    {
        const CheckInputAttributes* attributes = input->declarations[token] == token && !input->registers[token] ? input->attributes[token] : 0;
        for (uint32_t i = 0; attributes != 0 && i < attributes->count; i += 1)
        {
            labels += attributes->records[i].kind == CHECK_ATTRIBUTE_ASM_LABEL && !attributes->records[i].ignored;
        }
        compound |= input->compound_objects[token] != 0;
    }
    // Each object's label, by its identity (its canonical declaration's token + 1).
    String8* object_labels = BUSTER_ARENA_PUSH_ARRAY(arena, String8, (uint64_t)count + 2);
    result->labels = labels != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, String8, labels) : 0;
    result->bases = BUSTER_ARENA_PUSH_ARRAY(arena, String8, (uint64_t)count + 1);
    bool valid = object_labels != 0 && (labels == 0 || result->labels != 0) && result->bases != 0;
    if (valid)
    {
        memset(object_labels, 0, ((uint64_t)count + 2) * sizeof(String8));
    }
    for (uint32_t token = 0; valid && token < count; token += 1)
    {
        const CheckInputAttributes* attributes = input->declarations[token] == token && !input->registers[token] ? input->attributes[token] : 0;
        for (uint32_t i = 0; attributes != 0 && i < attributes->count; i += 1)
        {
            const CheckInputAttribute* attribute = attributes->records + i;
            if (attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL && !attribute->ignored)
            {
                result->labels[result->label_count++] = attribute->text;
                object_labels[input->objects[token] <= count + 1 ? input->objects[token] : 0] = attribute->text;
            }
        }
    }
    for (uint32_t token = 0; valid && token < count; token += 1)
    {
        const uint32_t opening = input->declaration_bodies[token];
        const uint32_t object = input->objects[token] <= count + 1 ? input->objects[token] : 0;
        const bool block_static = input->declarations[token] == token && input->declaration_kinds[token] == C_BINDING_OBJECT && input->static_objects[token] &&
                                  input->linkage[token] == 0 && !input->compound_objects[token] && opening < count && object != 0 &&
                                  object_labels[object].length == 0;
        const uint32_t function = block_static ? input->body_declarations[opening] : BUSTER_CHECK_NONE;
        if (function < count)
        {
            const uint32_t function_object = input->objects[function] <= count + 1 ? input->objects[function] : 0;
            const String8 label = function_object != 0 ? object_labels[function_object] : (String8){ 0 };
            const IdentId function_name = parser->tokens.names[function];
            const IdentId own_name = parser->tokens.names[token];
            const uint8_t* context = label.length != 0 ? (const uint8_t*)label.pointer : intern_bytes(identifiers, function_name);
            const uint64_t context_length = label.length != 0 ? label.length : intern_length(identifiers, function_name);
            const uint64_t own_length = intern_length(identifiers, own_name);
            uint8_t* base = arena_push(arena, context_length + 1 + own_length, 1);
            valid = base != 0;
            if (valid)
            {
                memcpy(base, context, context_length);
                base[context_length] = '.';
                memcpy(base + context_length + 1, intern_bytes(identifiers, own_name), own_length);
                result->bases[result->base_count++] = (String8){ .pointer = (char*)base, .length = context_length + 1 + own_length };
            }
        }
    }
    if (valid && compound)
    {
        result->bases[result->base_count++] = BUSTER_S8(".compoundliteral");
    }
    if (valid)
    {
        qsort(result->labels, result->label_count, sizeof(String8), lowering_symbols_string_order);
        qsort(result->bases, result->base_count, sizeof(String8), lowering_symbols_string_order);
    }
    else
    {
        *result = (LoweringNames){ 0 };
    }
    return valid;
}

#endif

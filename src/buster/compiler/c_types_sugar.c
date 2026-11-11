// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Type sugar (#1447, C4b): what a declaration wrote about its type beyond the canonical type, as clang's sugar nodes keep
// it (AttributedType, MacroQualifiedType, ParenType), for the diagnostics that print a type as written. c_types.c includes
// this file after the printer.
//
// A sugar id (uint32_t, 0 for none) is never a TypeId: the canonical types stay the only identity, and nothing that
// compares, lays out, converts or lowers a type reads sugar. Each node records the canonical type it stands for
// (TypeSugarTable.canonical), which is the TypeId it travels with, so the a.k.a. is that one printed. Nodes are
// hash-consed: a sugar written by many declarations is one node.
//
// Kinds, after clang's:
//   - POINTER, ARRAY: a structural level, made only to carry sugar below it (inner, never 0): `void ((*))(void)`'s pointer.
//   - PAREN: a declarator's parentheses (ParenType) around the type built inside them, made only over sugar, or over an array
//     itself (inner 0) in a unit that clears padding (#1842's 1b, check_input_declarator_sugar), whose walk treats such an
//     array as one scalar.
//   - ATTRIBUTED: a calling convention attribute (AttributedType), word a TypeSugarAttribute. modified is the canonical
//     type the attribute was written on (clang's modified type: a function's without its convention where the attribute
//     sits in a group before `*`, `void (MS *p)(void)`), and inner that type's sugar, 0 for none.
//   - MACRO: the macro an ATTRIBUTED came from (MacroQualifiedType), inner that ATTRIBUTED; word is the attribute list's
//     first token plus one, whose macro the caller resolves when it prints (TypeNames.sugar_macro_*), since only rendering reads
//     the expansion history. One the caller doesn't resolve prints as its ATTRIBUTED.
//
// Entry points: type_sugar_initialize, type_sugar_make, type_sugar_print (clang's TypePrinter over the nodes, each
// sugar-free part printed by type_print), type_sugar_macro_tokens (the tokens a print resolves).

#define BUSTER_TYPE_SUGAR_POINTER 1
#define BUSTER_TYPE_SUGAR_ARRAY 2
#define BUSTER_TYPE_SUGAR_PAREN 3
#define BUSTER_TYPE_SUGAR_ATTRIBUTED 4
#define BUSTER_TYPE_SUGAR_MACRO 5
// The deepest chain a print follows: a declarator's chunks, which clang's own nesting limit bounds well below it.
#define BUSTER_TYPE_SUGAR_LEVELS 64
// The placeholder a sugar-free part prints with, where its type sits inside another level (clang's non-empty
// placeholder): a byte no type's spelling has.
#define BUSTER_TYPE_SUGAR_MARK 1

// An ATTRIBUTED node's attribute: clang 23's spelling of its AttributedType (TypePrinter::printAttributedAfter), which is
// the attribute's however it was written (`__regcall`, `[[gnu::ms_abi]]`, `__ms_abi__`).
typedef enum TypeSugarAttribute
{
    TYPE_SUGAR_SYSV_ABI,
    TYPE_SUGAR_CDECL,
    TYPE_SUGAR_MS_ABI,
    TYPE_SUGAR_VECTORCALL,
    TYPE_SUGAR_REGCALL,
    TYPE_SUGAR_PRESERVE_MOST,
    TYPE_SUGAR_PRESERVE_ALL,
    TYPE_SUGAR_PRESERVE_NONE,
    TYPE_SUGAR_INTEL_OCL,
    TYPE_SUGAR_SWIFT,
    TYPE_SUGAR_SWIFT_ASYNC,
    TYPE_SUGAR_ATTRIBUTE_COUNT,
} TypeSugarAttribute;

typedef struct TypeSugarTable TypeSugarTable;
struct TypeSugarTable
{
    uint8_t* kind;
    uint32_t* word;
    uint32_t* inner;
    TypeId* canonical;
    TypeId* modified;
    // Open addressing over the nodes, a slot each (0 empty), twice the capacity rounded to a power of two.
    uint32_t* slots;
    uint32_t slot_mask;
    // Node 0 is unused, so that 0 is no sugar.
    uint32_t count;
    uint32_t capacity;
};

// Pushes the columns from `arena` with room for `capacity` nodes. False, with the arena as it was, where it can't.
static __attribute__((unused)) bool type_sugar_initialize(TypeSugarTable* sugar, Arena* arena, uint32_t capacity)
{
    const uint64_t mark = arena->length;
    uint32_t slot_count = 16;
    while (slot_count < 2ull * ((uint64_t)capacity + 1) && slot_count < (1u << 30))
    {
        slot_count *= 2;
    }
    const bool fits = capacity < (1u << 28);
    const uint64_t rows = (uint64_t)capacity + 1;
    uint8_t* kind = fits ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, rows) : NULL;
    uint32_t* word = kind != NULL ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, rows) : NULL;
    uint32_t* inner = word != NULL ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, rows) : NULL;
    TypeId* canonical = inner != NULL ? BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, rows) : NULL;
    TypeId* modified = canonical != NULL ? BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, rows) : NULL;
    uint32_t* slots = modified != NULL ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slot_count) : NULL;
    const bool valid = slots != NULL;
    if (valid)
    {
        memset(slots, 0, slot_count * sizeof(uint32_t));
        *sugar = (TypeSugarTable){
            .kind = kind,
            .word = word,
            .inner = inner,
            .canonical = canonical,
            .modified = modified,
            .slots = slots,
            .slot_mask = slot_count - 1,
            .count = 1,
            .capacity = capacity,
        };
    }
    else
    {
        arena_pop_to(arena, mark);
    }
    return valid;
}

static uint64_t type_sugar_hash(uint8_t kind, uint32_t word, uint32_t inner, TypeId canonical, TypeId modified)
{
    uint64_t hash = ((uint64_t)kind << 56) ^ ((uint64_t)word * 0x9e3779b97f4a7c15ull);
    hash ^= ((uint64_t)inner << 32 | canonical) * 0xc2b2ae3d27d4eb4full;
    hash ^= (uint64_t)modified * 0x165667b19e3779f9ull;
    return hash ^ (hash >> 29);
}

// The table again with room for twice its nodes, from `arena` (the old columns stay where they were, unused). False,
// with the table as it was, where the arena can't hold it.
static bool type_sugar_grow(TypeSugarTable* sugar, Arena* arena)
{
    TypeSugarTable grown = { 0 };
    const uint64_t capacity = 2ull * sugar->capacity + 64;
    const bool valid = capacity < (1u << 28) && type_sugar_initialize(&grown, arena, (uint32_t)capacity);
    for (uint32_t node = 1; valid && node < sugar->count; node += 1)
    {
        grown.kind[node] = sugar->kind[node];
        grown.word[node] = sugar->word[node];
        grown.inner[node] = sugar->inner[node];
        grown.canonical[node] = sugar->canonical[node];
        grown.modified[node] = sugar->modified[node];
        uint32_t slot = (uint32_t)type_sugar_hash(sugar->kind[node], sugar->word[node], sugar->inner[node], sugar->canonical[node], sugar->modified[node]) &
                        grown.slot_mask;
        while (grown.slots[slot] != 0)
        {
            slot = (slot + 1) & grown.slot_mask;
        }
        grown.slots[slot] = node;
    }
    if (valid)
    {
        grown.count = sugar->count;
        *sugar = grown;
    }
    return valid;
}

// The node of these fields, made if the table has none, the table grown from `arena` where it is full (with no arena, a
// full table makes none). 0 where it can't: the caller then keeps no sugar, and the type prints canonically.
static __attribute__((unused)) uint32_t type_sugar_make(TypeSugarTable* sugar, Arena* arena, uint8_t kind, uint32_t word, uint32_t inner, TypeId canonical,
                                                        TypeId modified)
{
    uint32_t result = 0;
    const uint64_t hash = type_sugar_hash(kind, word, inner, canonical, modified);
    uint32_t slot = (uint32_t)hash & sugar->slot_mask;
    bool probing = sugar->slots != NULL;
    while (probing)
    {
        const uint32_t node = sugar->slots[slot];
        const bool same = node != 0 && sugar->kind[node] == kind && sugar->word[node] == word && sugar->inner[node] == inner &&
                          sugar->canonical[node] == canonical && sugar->modified[node] == modified;
        result = same ? node : 0;
        probing = node != 0 && !same;
        slot = probing ? (slot + 1) & sugar->slot_mask : slot;
    }
    const bool room = result == 0 && sugar->slots != NULL && (sugar->count <= sugar->capacity || (arena != NULL && type_sugar_grow(sugar, arena)));
    if (room)
    {
        // (A grown table's slots are others: the probe again.)
        slot = (uint32_t)hash & sugar->slot_mask;
        while (sugar->slots[slot] != 0)
        {
            slot = (slot + 1) & sugar->slot_mask;
        }
        result = sugar->count;
        sugar->count += 1;
        sugar->kind[result] = kind;
        sugar->word[result] = word;
        sugar->inner[result] = inner;
        sugar->canonical[result] = canonical;
        sugar->modified[result] = modified;
        sugar->slots[slot] = result;
    }
    return result;
}

// One level of a print: a node, a structural level the printed type adds above its sugar (a `&f`'s pointer), or the
// sugar-free type under the last node (leaf).
typedef struct TypeSugarLevel TypeSugarLevel;
struct TypeSugarLevel
{
    TypeId type;
    uint32_t node;
    uint8_t kind;
    bool leaf;
    // Whether clang's placeholder is empty where this level prints (TypePrinter's HasEmptyPlaceHolder).
    bool empty;
    // Whether a calling convention attribute is being printed around this level (TypePrinter's InsideCCAttribute).
    bool inside_cc;
};

static bool type_sugar_same(const TypeTable* table, TypeId left, TypeId right)
{
    return type_is_valid(table, left) && type_is_valid(table, right) &&
           type_unqualified(type_strip_aligned(table, left)) == type_unqualified(type_strip_aligned(table, right));
}

// The levels a print of `type` takes from `node` (type_sugar_print), outermost first, with the MACRO nodes `names`
// resolves (every one with `macros`): `node` where its canonical type is
// `type`'s; a node under it where `type` is a level of its type (a dereference's, `*pp`); `type`'s own pointer and array
// levels above it where `node`'s type is one of `type`'s (an address's, `&f`). 0 where `type` has none of these, which
// the caller prints canonically.
static String8 type_sugar_macro_text(const TypeNames* names, uint32_t token);
static uint32_t type_sugar_levels(const TypeTable* table, const TypeSugarTable* sugar, const TypeNames* names, bool macros, uint32_t node, TypeId type,
                                  TypeSugarLevel* levels)
{
    uint32_t count = 0;
    uint32_t start = node < sugar->count ? node : 0;
    // Down the node's structural levels to `type`'s.
    for (uint32_t steps = 0; start != 0 && steps < BUSTER_TYPE_SUGAR_LEVELS && !type_sugar_same(table, sugar->canonical[start], type); steps += 1)
    {
        const uint8_t kind = sugar->kind[start];
        start = kind == BUSTER_TYPE_SUGAR_POINTER || kind == BUSTER_TYPE_SUGAR_ARRAY || kind == BUSTER_TYPE_SUGAR_PAREN || kind == BUSTER_TYPE_SUGAR_MACRO ?
                sugar->inner[start] : 0;
    }
    if (start == 0 && node != 0 && node < sugar->count)
    {
        // Up `type`'s pointers and arrays to the node's.
        TypeId level = type;
        uint32_t above = 0;
        for (; above < 8 && type_is_valid(table, level) && !type_sugar_same(table, level, sugar->canonical[node]); above += 1)
        {
            const TypeId bare = type_unqualified(type_strip_aligned(table, level));
            const uint8_t kind = type_kind(table, bare);
            level = kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY ?
                    type_element(table, type_strip_aligned(table, level)) : 0;
        }
        const bool found = type_is_valid(table, level) && type_sugar_same(table, level, sugar->canonical[node]);
        level = type;
        for (uint32_t i = 0; found && i < above; i += 1)
        {
            const TypeId bare = type_strip_aligned(table, level);
            levels[count] = (TypeSugarLevel){
                .type = bare,
                .kind = type_kind(table, type_unqualified(bare)) == BUSTER_TYPE_KIND_POINTER ? BUSTER_TYPE_SUGAR_POINTER : BUSTER_TYPE_SUGAR_ARRAY,
            };
            count += 1;
            level = type_element(table, bare);
        }
        start = found ? node : 0;
    }
    // The node's chain, to its sugar-free leaf.
    TypeId leaf = 0;
    for (uint32_t current = start; current != 0 && count + 1 < BUSTER_TYPE_SUGAR_LEVELS;)
    {
        const uint8_t kind = sugar->kind[current];
        // The first level prints the qualifiers `type` has (an lvalue conversion's drop theirs).
        const TypeId own = count == 0 ? type_qualify(table, type_unqualified(sugar->canonical[current]), type_qualifiers(type)) : sugar->canonical[current];
        uint32_t next = sugar->inner[current];
        // A macro its caller didn't resolve prints as its ATTRIBUTED does, whose levels follow.
        const bool resolved = kind == BUSTER_TYPE_SUGAR_MACRO && (macros || type_sugar_macro_text(names, sugar->word[current]).length != 0);
        if (kind == BUSTER_TYPE_SUGAR_MACRO && !resolved)
        {
            leaf = 0;
        }
        else if (kind == BUSTER_TYPE_SUGAR_MACRO)
        {
            // clang's MacroQualifiedType prints the macro, then its attribute's modified type: the ATTRIBUTED's level
            // isn't printed (its inner, or its modified type as the leaf).
            levels[count] = (TypeSugarLevel){ .type = own, .node = current, .kind = kind };
            count += 1;
            const uint32_t attributed = next;
            next = attributed != 0 ? sugar->inner[attributed] : 0;
            leaf = attributed != 0 && next == 0 ? sugar->modified[attributed] : 0;
        }
        else
        {
            levels[count] = (TypeSugarLevel){ .type = own, .node = current, .kind = kind };
            count += 1;
            // (An array's element takes the qualifiers the array's TypeId holds.)
            const TypeId stripped = type_strip_aligned(table, sugar->canonical[current]);
            leaf = next != 0 ? 0 : kind == BUSTER_TYPE_SUGAR_ATTRIBUTED ? sugar->modified[current] :
                   kind == BUSTER_TYPE_SUGAR_POINTER || kind == BUSTER_TYPE_SUGAR_ARRAY ? type_element(table, stripped) : sugar->canonical[current];
        }
        current = next;
    }
    if (count != 0 && leaf != 0)
    {
        levels[count] = (TypeSugarLevel){ .type = leaf, .leaf = true };
        count += 1;
    }
    return count != 0 && levels[count - 1].leaf ? count : 0;
}

// The tokens (plus one) of the MACRO nodes a print of `type` from `node` meets (type_sugar_levels), for the caller to resolve into
// TypeNames.sugar_macro_*: at most `capacity`, and how many.
static __attribute__((unused)) uint32_t type_sugar_macro_tokens(const TypeTable* table, const TypeSugarTable* sugar, uint32_t node, TypeId type, uint32_t* tokens,
                                                                uint32_t capacity)
{
    TypeSugarLevel levels[BUSTER_TYPE_SUGAR_LEVELS];
    const uint32_t count = sugar != NULL ? type_sugar_levels(table, sugar, NULL, true, node, type, levels) : 0;
    uint32_t result = 0;
    for (uint32_t i = 0; i < count && result < capacity; i += 1)
    {
        if (levels[i].kind == BUSTER_TYPE_SUGAR_MACRO)
        {
            tokens[result] = sugar->word[levels[i].node];
            result += 1;
        }
    }
    return result;
}

static String8 type_sugar_macro_text(const TypeNames* names, uint32_t token)
{
    String8 result = { 0 };
    for (uint32_t i = 0; names != NULL && i < names->sugar_macro_count && result.length == 0; i += 1)
    {
        result = names->sugar_macro_tokens[i] == token ? names->sugar_macro_texts[i] : result;
    }
    return result;
}

// A String8 of `before`, then `middle`, then `after`, in `arena`; empty where it can't.
static String8 type_sugar_join(Arena* arena, String8 before, String8 middle, String8 after)
{
    const uint64_t length = before.length + middle.length + after.length;
    char* bytes = length != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, char, length) : NULL;
    if (bytes != NULL)
    {
        // An empty String8's pointer may be null, which memcpy doesn't take even for no bytes.
        if (before.length != 0)
        {
            memcpy(bytes, before.pointer, before.length);
        }
        if (middle.length != 0)
        {
            memcpy(bytes + before.length, middle.pointer, middle.length);
        }
        if (after.length != 0)
        {
            memcpy(bytes + before.length + middle.length, after.pointer, after.length);
        }
    }
    return bytes != NULL ? (String8){ .pointer = bytes, .length = length } : (String8){ 0 };
}

static String8 type_sugar_text(const char* text)
{
    return (String8){ .pointer = (char*)text, .length = strlen(text) };
}

// A pointer level's qualifiers after its `*`, as clang's Qualifiers::print, with a space after them where the placeholder
// isn't empty (`int *const p`).
static String8 type_sugar_qualifiers(Arena* arena, uint32_t qualifiers, bool empty)
{
    String8 result = { 0 };
    const char* const words[] = { "const", "volatile", "restrict" };
    const uint32_t bits[] = { BUSTER_TYPE_CONST, BUSTER_TYPE_VOLATILE, BUSTER_TYPE_RESTRICT };
    for (uint32_t i = 0; i < 3; i += 1)
    {
        if ((qualifiers & bits[i]) != 0)
        {
            result = type_sugar_join(arena, result, result.length != 0 ? type_sugar_text(" ") : (String8){ 0 }, type_sugar_text(words[i]));
        }
    }
    return result.length != 0 && !empty ? type_sugar_join(arena, result, type_sugar_text(" "), (String8){ 0 }) : result;
}

// `type` as `node`'s sugar writes it, with `name` as its placeholder (empty for a diagnostic's type), as clang's
// TypePrinter prints a type with AttributedType, MacroQualifiedType and ParenType nodes: each sugar-free part by
// type_print, with `names` and `options` (its parentheses ignored: the nodes keep them). Empty where the nodes don't
// describe `type` (type_sugar_levels) or a part can't print; the caller then prints `type` canonically.
static __attribute__((unused)) String8 type_sugar_print(const TypeTable* table, const TypeSugarTable* sugar, Arena* arena, uint32_t node, TypeId type, String8 name,
                                                        const TypeNames* names, uint32_t options)
{
    static const char* const spellings[] = {
        "sysv_abi",
        "cdecl",
        "ms_abi",
        "vectorcall",
        "regcall",
        "preserve_most",
        "preserve_all",
        "preserve_none",
        "inteloclbicc",
        "swiftcall",
        "swiftasynccall",
    };
    _Static_assert(sizeof(spellings) / sizeof(spellings[0]) == TYPE_SUGAR_ATTRIBUTE_COUNT, "every attribute has a spelling");
    TypeSugarLevel levels[BUSTER_TYPE_SUGAR_LEVELS];
    const uint32_t count = sugar != NULL ? type_sugar_levels(table, sugar, names, false, node, type, levels) : 0;
    bool valid = count != 0;
    // The placeholder's state, outermost first: a pointer's makes its pointee's non-empty, as do qualifiers printed after
    // a level (clang's hasAfterQuals); and whether a convention's attribute surrounds the level.
    bool empty = name.length == 0;
    bool inside_cc = false;
    for (uint32_t i = 0; i < count; i += 1)
    {
        levels[i].empty = empty;
        levels[i].inside_cc = inside_cc;
        const uint8_t kind = levels[i].kind;
        empty = empty && !(kind == BUSTER_TYPE_SUGAR_POINTER && !levels[i].leaf);
        // Qualifiers on a parenthesized, attributed or macro level (clang's hasAfterQuals there) aren't printed by this
        // printer: such a type prints canonically.
        // (Qualifiers the level under it has too are that one's, which prints them: an attribute's on a `* const`.)
        const uint32_t qualifiers = levels[i].leaf ? 0 : type_qualifiers(levels[i].type);
        const uint32_t below = i + 1 < count ? type_qualifiers(levels[i + 1].type) : 0;
        valid = valid && (levels[i].leaf || kind == BUSTER_TYPE_SUGAR_POINTER || kind == BUSTER_TYPE_SUGAR_ARRAY || (qualifiers & ~below) == 0);
        inside_cc |= kind == BUSTER_TYPE_SUGAR_ATTRIBUTED;
    }
    // Each level's text before the placeholder and after it, innermost first: the leaf's from type_print, split at the mark.
    String8 before = { 0 };
    String8 after = { 0 };
    TypeNames leaf_names = names != NULL ? *names : (TypeNames){ 0 };
    leaf_names.parentheses = NULL;
    leaf_names.parenthesis_count = 0;
    for (uint32_t k = count; valid && k > 0; k -= 1)
    {
        const TypeSugarLevel* level = &levels[k - 1];
        const TypeSugarLevel* child = k < count ? &levels[k] : NULL;
        const uint8_t child_kind = child != NULL && child->leaf ? type_kind(table, type_unqualified(type_strip_aligned(table, child->type))) : BUSTER_TYPE_KIND_NONE;
        const bool child_array = child != NULL && (child->leaf ? type_kind_is_array(child_kind) : child->kind == BUSTER_TYPE_SUGAR_ARRAY);
        const bool child_function = child != NULL && child->leaf && child_kind == BUSTER_TYPE_KIND_FUNCTION;
        if (level->leaf)
        {
            const char mark[1] = { BUSTER_TYPE_SUGAR_MARK };
            const String8 placeholder = level->empty ? (String8){ 0 } : (String8){ .pointer = (char*)mark, .length = 1 };
            // A leaf is a canonical type, whose convention prints as clang's canonical function type spells it (a macro's
            // modified type: `'IO void (void) __attribute__((intel_ocl_bicc))'`), or not at all inside an attribute's.
            const uint32_t leaf_options = options | (level->inside_cc ? BUSTER_TYPE_PRINT_INSIDE_CC : BUSTER_TYPE_PRINT_CANONICAL_CC);
            const String8 text = type_print(table, arena, level->type, placeholder, names != NULL ? &leaf_names : NULL, leaf_options);
            uint64_t split = text.length;
            for (uint64_t b = 0; !level->empty && b < text.length && split == text.length; b += 1)
            {
                split = text.pointer[b] == BUSTER_TYPE_SUGAR_MARK ? b : split;
            }
            valid = text.length != 0 && (level->empty || split < text.length);
            before = (String8){ .pointer = text.pointer, .length = split };
            after = split < text.length ? (String8){ .pointer = text.pointer + split + 1, .length = text.length - split - 1 } : (String8){ 0 };
        }
        else if (level->kind == BUSTER_TYPE_SUGAR_POINTER)
        {
            // printPointerBefore: the pointee, `(` before an array's, `*`; then the qualifiers after it.
            const String8 qualifiers = type_sugar_qualifiers(arena, type_qualifiers(level->type), level->empty);
            before = type_sugar_join(arena, before, type_sugar_text(child_array ? "(*" : "*"), qualifiers);
            after = type_sugar_join(arena, type_sugar_text(child_array ? ")" : ""), after, (String8){ 0 });
        }
        else if (level->kind == BUSTER_TYPE_SUGAR_ARRAY)
        {
            const TypeId bare = type_unqualified(type_strip_aligned(table, level->type));
            const uint8_t kind = type_kind(table, bare);
            char bound[32] = { 0 };
            const int written = kind == BUSTER_TYPE_KIND_ARRAY ? snprintf(bound, sizeof(bound), "[%" PRIu64 "]", table->types.extent[type_index(bare)]) :
                                snprintf(bound, sizeof(bound), "[]");
            // A variable array's bound is its caller's (TypeNames.bound_texts): such a level prints canonically.
            valid = kind != BUSTER_TYPE_KIND_VARIABLE_ARRAY && written > 0;
            after = type_sugar_join(arena, (String8){ .pointer = bound, .length = (uint64_t)written }, after, (String8){ 0 });
        }
        else if (level->kind == BUSTER_TYPE_SUGAR_PAREN)
        {
            // printParenBefore: `(` where the placeholder isn't empty and the inner type isn't a function's.
            const bool parenthesized = !level->empty && !child_function;
            before = type_sugar_join(arena, before, type_sugar_text(parenthesized ? "(" : ""), (String8){ 0 });
            after = type_sugar_join(arena, type_sugar_text(parenthesized ? ")" : ""), after, (String8){ 0 });
        }
        else if (level->kind == BUSTER_TYPE_SUGAR_ATTRIBUTED)
        {
            // clang 23 prints the preserve conventions before the type, a space more where the placeholder isn't empty
            // (spaceBeforePlaceHolder), and every other after it.
            const uint32_t attribute = sugar->word[level->node];
            const bool preserve = attribute == TYPE_SUGAR_PRESERVE_MOST || attribute == TYPE_SUGAR_PRESERVE_ALL || attribute == TYPE_SUGAR_PRESERVE_NONE;
            valid = attribute < TYPE_SUGAR_ATTRIBUTE_COUNT;
            const String8 spelling = valid ? type_sugar_text(spellings[attribute]) : (String8){ 0 };
            const String8 written = type_sugar_join(arena, type_sugar_text(preserve ? "__attribute__((" : " __attribute__(("), spelling,
                                                    type_sugar_text(!preserve ? "))" : level->empty ? ")) " : "))  "));
            before = preserve ? type_sugar_join(arena, written, before, (String8){ 0 }) : before;
            after = preserve ? after : type_sugar_join(arena, after, written, (String8){ 0 });
        }
        else if (level->kind == BUSTER_TYPE_SUGAR_MACRO)
        {
            // printMacroQualifiedBefore: the macro's name and a space. One the caller didn't resolve prints as its
            // attribute does (the ATTRIBUTED's levels again, from type_sugar_levels' node).
            const String8 macro = type_sugar_macro_text(names, sugar->word[level->node]);
            valid = macro.length != 0;
            before = type_sugar_join(arena, macro, type_sugar_text(" "), before);
        }
        valid = valid && (before.length != 0 || after.length != 0);
    }
    return valid ? type_sugar_join(arena, before, name, after) : (String8){ 0 };
}

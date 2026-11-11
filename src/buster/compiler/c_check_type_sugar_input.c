// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A declarator's type sugar (#1447, c_types_sugar.c), as clang's GetFullTypeForDeclarator builds its sugar nodes: a
// calling convention attribute's AttributedType where the attribute is written, and the ParenType and the pointers and
// arrays that carry it. c_check_input.c includes this file before its declarator's end, which calls
// check_input_declarator_sugar once a declaration's type is final.
//
// The canonical type takes every convention on the function nearest the name (check_input_declarator_abi); its sugar
// keeps where each was written:
//   - a C23 list before the specifiers is the whole type's: an ATTRIBUTED over it, modified the type without it;
//   - the specifiers' other ones and the trailing ones are distributed to that function's chunk (distributeFunctionTypeAttr): an
//     ATTRIBUTED over the function, its modified type the function with the convention (`void (*p)(void) PM` prints
//     `'__attribute__((preserve_most))  void ((*))(void)'`);
//   - one after a `*` is the pointer's: an ATTRIBUTED over the pointer, modified the pointer to the function without it;
//   - one at a group's start, before its `*`, is the group's: an ATTRIBUTED over its parentheses' type, modified the
//     function without it (`void (MS *p)(void)` prints `'MS void (*)(void)'`, the attribute dropped with its macro).
// Each attribute's ATTRIBUTED is under a MACRO of its list, which the renderer prints by the list's macro where one
// writes it all (check_driver_attribute_macro), and as its attribute otherwise.
//
// Only a declaration's own declarator (not a parameter's) of a list that writes a convention makes sugar; a declarator
// whose levels have an _Atomic pointer, an address space or an aligned typedef keeps none (its type prints as before).
// The nodes and the map of declarations to them (CheckInput.sugar_tokens) are made from the output arena when the unit
// first needs them. On a lane the unit goes
// to source order, whose reader alone makes sugar (its nodes then need no merge).

// The deepest declarator this keeps sugar for: more chunks keep none.
#define BUSTER_CHECK_SUGAR_PARTS 64

// A convention attribute's sugar, or TYPE_SUGAR_ATTRIBUTE_COUNT for none (no_caller_saved_registers, a declaration
// attribute of clang's; x86-32's ignored ones).
static uint32_t check_input_sugar_attribute(CheckInputReader* reader, const CheckInputAttribute* attribute)
{
    uint32_t result = TYPE_SUGAR_ATTRIBUTE_COUNT;
    if (attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && !attribute->ignored && !attribute->specifier_ignored)
    {
        const String8 name = check_input_attribute_name(reader, attribute->name);
        const bool cdecl = name.length == 5 && memcmp(name.pointer, "cdecl", 5) == 0;
        switch ((CheckInputCallingConvention)attribute->value)
        {
            case CHECK_CALL_SYSV: result = cdecl ? TYPE_SUGAR_CDECL : TYPE_SUGAR_SYSV_ABI; break;
            case CHECK_CALL_MICROSOFT: result = TYPE_SUGAR_MS_ABI; break;
            case CHECK_CALL_VECTOR: result = TYPE_SUGAR_VECTORCALL; break;
            case CHECK_CALL_REG: result = TYPE_SUGAR_REGCALL; break;
            case CHECK_CALL_PRESERVE_MOST: result = TYPE_SUGAR_PRESERVE_MOST; break;
            case CHECK_CALL_PRESERVE_ALL: result = TYPE_SUGAR_PRESERVE_ALL; break;
            case CHECK_CALL_PRESERVE_NONE: result = TYPE_SUGAR_PRESERVE_NONE; break;
            case CHECK_CALL_INTEL_OCL: result = TYPE_SUGAR_INTEL_OCL; break;
            case CHECK_CALL_SWIFT: result = TYPE_SUGAR_SWIFT; break;
            case CHECK_CALL_SWIFT_ASYNC: result = TYPE_SUGAR_SWIFT_ASYNC; break;
            default: break;
        }
    }
    return result;
}

// Whether a list writes a convention that has sugar: almost none does, which one test of its merged convention says.
static bool check_input_sugar_list(const CheckInputAttributes* attributes)
{
    return attributes != NULL && attributes->calling_convention != CHECK_CALL_DEFAULT;
}

// The token a MACRO node resolves (TypeSugarTable.word) plus one: a GNU list's `__attribute__` keyword, or a keyword's
// own token (`__regcall`); 0 for a C23 list, whose macro clang 23 doesn't name.
static uint32_t check_input_sugar_list_token(const Parser* parser, const CheckInputAttribute* attribute)
{
    uint32_t result = attribute->keyword ? attribute->first + 1 : 0;
    for (uint32_t t = attribute->first + 1, steps = 0; !attribute->keyword && !attribute->standard && result == 0 && t > 0 && steps < 64; steps += 1)
    {
        t -= 1;
        result = parser->tokens.kinds[t] == PARSE_KIND_ATTRIBUTE ? t + 1 : 0;
    }
    return result;
}

// Whether a list writes no_caller_saved_registers, with which clang rebuilds the declaration's function type
// (adjustFunctionType), which then keeps none of its conventions' AttributedTypes.
static bool check_input_sugar_rebuilt(const CheckInputAttributes* attributes)
{
    bool result = false;
    for (uint32_t a = 0; attributes != NULL && a < attributes->count; a += 1)
    {
        result |= attributes->records[a].kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && attributes->records[a].value == CHECK_CALL_NO_CALLER_SAVED;
    }
    return result;
}

// An ATTRIBUTED node over `inner` (canonical `canonical`, its modified type `modified`), under its list's MACRO.
static uint32_t check_input_sugar_attributed(CheckInputReader* reader, uint32_t inner, uint32_t attribute, uint32_t list, TypeId canonical, TypeId modified)
{
    TypeSugarTable* sugar = &reader->output->sugar;
    const uint32_t attributed = type_sugar_make(sugar, reader->arena, BUSTER_TYPE_SUGAR_ATTRIBUTED, attribute, inner, canonical, modified);
    const uint32_t macro = attributed != 0 && list != 0 ? type_sugar_make(sugar, reader->arena, BUSTER_TYPE_SUGAR_MACRO, list, attributed, canonical, 0) : 0;
    return macro != 0 ? macro : attributed;
}

// Whether `attribute` is among the attribute lists at the start of the group whose `(` is `open`, before its first `*`.
static bool check_input_sugar_leading(const Parser* parser, uint32_t open, uint32_t first)
{
    const uint32_t close = parser->tree.parents[open];
    uint32_t t = open + 1;
    while (t < close && t < first && check_input_attribute_starts(parser, t, close))
    {
        t = check_input_skip_attributes(parser, t, close);
    }
    return first < t;
}

// The groups around `token` in the declarator, outermost first in `groups` (at most `capacity`), and how many: each
// innermost `(` of the next (check_input_attribute_group_open), back to the declarator's start.
static uint32_t check_input_sugar_groups(const Parser* parser, uint32_t token, uint32_t start, uint32_t* groups, uint32_t capacity)
{
    uint32_t inner[8];
    uint32_t count = 0;
    for (uint32_t open = check_input_attribute_group_open(parser, token); open != UINT32_MAX && open >= start && count < 8;
         open = check_input_attribute_group_open(parser, open))
    {
        inner[count] = open;
        count += 1;
    }
    for (uint32_t i = 0; i < count && i < capacity; i += 1)
    {
        groups[i] = inner[count - 1 - i];
    }
    return count < capacity ? count : capacity;
}

// Sets the sugar of the declaration at `token` (CheckInput.sugar_tokens), the map grown from the output arena past half
// full; a map that can't grow keeps the token without sugar (its type prints as before).
static void check_input_set_sugar(CheckInputReader* reader, uint32_t token, uint32_t node)
{
    CheckInput* output = reader->output;
    if (output->sugar_mask == 0 || 2 * (output->sugar_entries + 1) > output->sugar_mask + 1)
    {
        const uint32_t slots = output->sugar_mask == 0 ? 64 : 2 * (output->sugar_mask + 1);
        uint32_t* tokens = slots < (1u << 30) ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, slots) : NULL;
        uint32_t* nodes = tokens != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, slots) : NULL;
        if (nodes != NULL)
        {
            memset(tokens, 0, slots * sizeof(uint32_t));
            for (uint32_t old = 0; output->sugar_mask != 0 && old <= output->sugar_mask; old += 1)
            {
                const uint32_t key = output->sugar_tokens[old];
                uint32_t slot = key != 0 ? ((key - 1) * 0x9e3779b1u) & (slots - 1) : 0;
                while (key != 0 && tokens[slot] != 0)
                {
                    slot = (slot + 1) & (slots - 1);
                }
                if (key != 0)
                {
                    tokens[slot] = key;
                    nodes[slot] = output->sugar_nodes[old];
                }
            }
            output->sugar_tokens = tokens;
            output->sugar_nodes = nodes;
            output->sugar_mask = slots - 1;
        }
        output->resource |= nodes == NULL;
    }
    // A full map that couldn't grow takes no new token.
    uint32_t slot = output->sugar_mask != 0 ? (token * 0x9e3779b1u) & output->sugar_mask : 0;
    bool probing = output->sugar_mask != 0;
    while (probing && output->sugar_tokens[slot] != 0 && output->sugar_tokens[slot] != token + 1)
    {
        slot = (slot + 1) & output->sugar_mask;
    }
    const bool room = output->sugar_mask != 0 && 2 * (output->sugar_entries + 1) <= output->sugar_mask + 1;
    if (probing && (output->sugar_tokens[slot] == token + 1 || room))
    {
        output->sugar_entries += output->sugar_tokens[slot] == 0 ? 1 : 0;
        output->sugar_tokens[slot] = token + 1;
        output->sugar_nodes[slot] = node;
    }
}

static __attribute__((noinline, cold)) uint32_t check_input_declarator_sugar(CheckInputReader* reader, const CheckDeclaratorFrame* frame, const CheckInputAttributes* common,
                                             const CheckDeclaratorPart* parts, uint32_t order, TypeId type, bool attributes)
{
    const Parser* parser = reader->parser;
    const TypeTable* types = reader->types;
    CheckInput* output = reader->output;
    // Without `attributes`, a declarator's groups over arrays alone (#1842's 1b), and none of its conventions.
    const CheckInputAttributes* own = attributes ? frame->attributes : NULL;
    common = attributes ? common : NULL;
    uint32_t result = 0;
    check_input_lane_refuse(reader, reader->lane, "a declarator's type sugar is the source order's");
    // The parts in the order they apply, from the base out, and the type each makes in the final type.
    uint32_t chain[BUSTER_CHECK_SUGAR_PARTS];
    TypeId levels[BUSTER_CHECK_SUGAR_PARTS];
    uint32_t count = 0;
    bool valid = !reader->lane && frame->name != BUSTER_CHECK_NONE && type != 0 && !check_input_sugar_rebuilt(common) && !check_input_sugar_rebuilt(own);
    for (uint32_t p = order; valid && p != BUSTER_CHECK_NONE; p = parts[p].next)
    {
        valid = count < BUSTER_CHECK_SUGAR_PARTS && !parts[p].atomic;
        if (valid && parts[p].kind != BUSTER_TYPE_KIND_NONE)
        {
            chain[count] = p;
            count += 1;
        }
    }
    TypeId level = type;
    uint32_t function = BUSTER_CHECK_NONE;
    for (uint32_t i = count; valid && i > 0; i -= 1)
    {
        const uint8_t kind = type_kind(types, level);
        valid = kind < BUSTER_TYPE_KIND_WRAPPERS;
        levels[i - 1] = level;
        const uint8_t part = parts[chain[i - 1]].kind;
        function = function == BUSTER_CHECK_NONE && part == BUSTER_TYPE_KIND_FUNCTION ? i - 1 : function;
        level = !valid ? 0 : part == BUSTER_TYPE_KIND_FUNCTION ? type_return(types, level) : type_element(types, level);
    }
    const TypeId base = level;
    // The table, once a unit first needs it.
    if (valid && output->sugar.slots == NULL)
    {
        valid = type_sugar_initialize(&output->sugar, reader->arena, 64);
        output->resource |= !valid;
    }
    uint32_t current = 0;
    uint32_t depth = 0;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const CheckDeclaratorPart* part = &parts[chain[i]];
        const TypeId below = i > 0 ? levels[i - 1] : base;
        const TypeId written = i > 0 ? parts[chain[i - 1]].type : frame->base;
        // The groups this part is inside that the last wasn't: each a ParenType around the type so far, outermost first,
        // then its leading attributes.
        uint32_t groups[8];
        const uint32_t group_count = check_input_sugar_groups(parser, part->token, frame->start, groups, 8);
        // A group around an array is sugar of its own, in a unit that clears padding (#1842's 1b: clang's PaddingClearer
        // doesn't look through a ParenType): a PAREN over the canonical array, where it has none beneath.
        const bool array_group = reader->padding_sugar && type_kind_is_array(type_kind(types, type_strip_aligned(types, below)));
        for (uint32_t g = depth; g < group_count; g += 1)
        {
            current = current != 0 || array_group ? type_sugar_make(&output->sugar, reader->arena, BUSTER_TYPE_SUGAR_PAREN, 0, current, below, 0) : 0;
            for (uint32_t a = 0; own != NULL && a < own->count; a += 1)
            {
                const CheckInputAttribute* attribute = own->records + a;
                const uint32_t sugar = check_input_sugar_attribute(reader, attribute);
                if (sugar < TYPE_SUGAR_ATTRIBUTE_COUNT && attribute->first < frame->name && check_input_attribute_group_open(parser, attribute->first) == groups[g] &&
                    check_input_sugar_leading(parser, groups[g], attribute->first))
                {
                    current = check_input_sugar_attributed(reader, current, sugar, check_input_sugar_list_token(parser, attribute), below, written);
                }
            }
        }
        depth = group_count;
        if (part->kind == BUSTER_TYPE_KIND_POINTER)
        {
            current = current != 0 ? type_sugar_make(&output->sugar, reader->arena, BUSTER_TYPE_SUGAR_POINTER, 0, current, levels[i], 0) : 0;
            // Its own attributes: after its `*`, before the next part's token in its group.
            const uint32_t group = check_input_attribute_group_open(parser, part->token);
            for (uint32_t a = 0; own != NULL && a < own->count; a += 1)
            {
                const CheckInputAttribute* attribute = own->records + a;
                const uint32_t sugar = check_input_sugar_attribute(reader, attribute);
                bool mine = sugar < TYPE_SUGAR_ATTRIBUTE_COUNT && attribute->first > part->token && attribute->first < frame->name &&
                            check_input_attribute_group_open(parser, attribute->first) == group;
                for (uint32_t j = 0; mine && j < count; j += 1)
                {
                    const uint32_t star = parts[chain[j]].token;
                    mine = !(parts[chain[j]].kind == BUSTER_TYPE_KIND_POINTER && star > part->token && star < attribute->first);
                }
                current = mine ? check_input_sugar_attributed(reader, current, sugar, check_input_sugar_list_token(parser, attribute), levels[i], part->type) : current;
            }
        }
        else if (part->kind == BUSTER_TYPE_KIND_FUNCTION)
        {
            // A function level keeps none of its return type's sugar yet.
            current = 0;
            for (uint32_t pass = 0; i == function && pass < 2; pass += 1)
            {
                // The specifiers' conventions, then the trailing ones, distributed to the function nearest the name.
                const CheckInputAttributes* list = pass == 0 ? common : own;
                for (uint32_t a = 0; list != NULL && (pass == 0 || list != common) && a < list->count; a += 1)
                {
                    const CheckInputAttribute* attribute = list->records + a;
                    const uint32_t sugar = check_input_sugar_attribute(reader, attribute);
                    if (sugar < TYPE_SUGAR_ATTRIBUTE_COUNT && (pass == 0 ? !attribute->standard : attribute->first > frame->name))
                    {
                        current = check_input_sugar_attributed(reader, current, sugar, check_input_sugar_list_token(parser, attribute), levels[i], levels[i]);
                    }
                }
            }
        }
        else
        {
            current = current != 0 ? type_sugar_make(&output->sugar, reader->arena, BUSTER_TYPE_SUGAR_ARRAY, 0, current, levels[i], 0) : 0;
        }
    }
    // A C23 list before the declaration's specifiers appertains to its whole type (an ATTRIBUTED over it, as clang's
    // `[[gnu::ms_abi]] void (*p)(void)` has it), its modified type the declarator's own.
    for (uint32_t a = 0; valid && count != 0 && common != NULL && a < common->count; a += 1)
    {
        const CheckInputAttribute* attribute = common->records + a;
        const uint32_t sugar = check_input_sugar_attribute(reader, attribute);
        if (sugar < TYPE_SUGAR_ATTRIBUTE_COUNT && attribute->standard)
        {
            current = check_input_sugar_attributed(reader, current, sugar, 0, type, parts[chain[count - 1]].type);
        }
    }
    result = valid && current != 0 && output->sugar.canonical[current] == type ? current : 0;
    if (frame->name != BUSTER_CHECK_NONE && (result != 0 || output->sugar_entries != 0))
    {
        check_input_set_sugar(reader, frame->name, result);
    }
    return result;
}

// A declaration's sugar remade over its final type (#1447): its specifiers' convention applies to the function nearest
// the name once the declarator has ended (check_input_declaration's check_input_declarator_abi), after its sugar was made
// over the declarator's type. The chain is remade from the top: each structural and parenthesized level takes the final
// type's, and a distributed ATTRIBUTED (whose modified type was its own) the function's; below an attribute written on a
// pointer or a group the levels stay as they were. 0 where the levels don't follow the type (its type then prints as
// before).
static __attribute__((noinline, cold)) uint32_t check_input_sugar_retype(CheckInputReader* reader, uint32_t sugar, TypeId type)
{
    TypeSugarTable* table = &reader->output->sugar;
    const TypeTable* types = reader->types;
    uint32_t nodes[BUSTER_TYPE_SUGAR_LEVELS];
    TypeId levels[BUSTER_TYPE_SUGAR_LEVELS];
    uint32_t count = 0;
    TypeId level = type;
    bool valid = sugar != 0 && sugar < table->count;
    for (uint32_t node = sugar; valid && node != 0 && count < BUSTER_TYPE_SUGAR_LEVELS;)
    {
        const uint8_t kind = table->kind[node];
        valid = type_kind(types, level) < BUSTER_TYPE_KIND_WRAPPERS;
        nodes[count] = node;
        levels[count] = level;
        count += 1;
        const bool distributed = kind == BUSTER_TYPE_SUGAR_ATTRIBUTED && table->modified[node] == table->canonical[node];
        level = kind == BUSTER_TYPE_SUGAR_POINTER || kind == BUSTER_TYPE_SUGAR_ARRAY ? type_element(types, level) : level;
        // Below a pointer's or a group's attribute the levels are its modified type's, which the specifiers' convention
        // doesn't change: the chain stops there, kept as it is.
        node = kind == BUSTER_TYPE_SUGAR_ATTRIBUTED && !distributed ? 0 : table->inner[node];
    }
    uint32_t result = 0;
    for (uint32_t i = count; valid && i > 0; i -= 1)
    {
        const uint32_t node = nodes[i - 1];
        const uint8_t kind = table->kind[node];
        const bool distributed = kind == BUSTER_TYPE_SUGAR_ATTRIBUTED && table->modified[node] == table->canonical[node];
        const bool kept = kind == BUSTER_TYPE_SUGAR_ATTRIBUTED && !distributed;
        const uint32_t inner = i == count ? table->inner[node] : result;
        const TypeId modified = distributed ? levels[i - 1] : table->modified[node];
        result = type_sugar_make(table, reader->arena, kind, table->word[node], kept && i == count ? table->inner[node] : inner, levels[i - 1], modified);
        valid = result != 0;
    }
    return valid ? result : 0;
}

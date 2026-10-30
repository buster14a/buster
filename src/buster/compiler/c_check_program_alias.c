// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU's alias, ifunc and weakref attributes (#147): the object of the unit that each one's name names, and what its own
// symbol is, as clang's code generation makes them (CodeGenModule::EmitAliasDefinition, emitIFuncDefinition,
// GetWeakRefReference and checkAliases). Included by c_check_program.c before check_program, which calls
// check_program_aliases with the file-scope declarations its static objects' pass found with one
// (CheckProgramTargetEvents.alias_keys); the selection and the emission read the entries (CheckProgram.aliases).
//
// A name is a symbol's: an object's asm label, from any of its declarations, or else its identifier, of a file-scope
// declaration (or a block-scope one with linkage, which declares the file-scope entity) that isn't a weakref's, whose
// own name is no symbol. Clang finds the target by that name among the module's globals, so a block-scope static's,
// whose name is its function's and its own, is none, nor is an overloadable function's identifier, which its mangled name
// replaces.
//
// An alias's aliasee is the first object along the chain of aliases from its target that isn't an alias: a function, a
// variable or an ifunc (GlobalAlias::getAliaseeObject); an ifunc's is its resolver's, the same way. The aliasee must be
// defined: a function's body, a variable's definition or tentative definition, or an ifunc; an ifunc's must be a
// function returning a pointer. An alias's symbol is at its aliasee's definition, or an aliased ifunc's resolver's, with
// that definition's size and its own name, binding and visibility; its type is the definition's, but IFUNC when an ifunc
// is on the way. An ifunc's symbol is at its resolver, an IFUNC and always global (clang gives every ifunc external
// linkage, weak or static as it is declared). A weakref's references are its target's: to the unit's object of that
// name, whose undefined symbol is weak where nothing else refers to it (check_program_alias_number), or to a symbol of
// its own of that name, weak.
//
// Cost: nothing in a unit without these attributes, which check_program's static pass leaves without events. A unit
// with them resolves its entries' names in two scans of its static declarations, 64 tokens at a time
// (check_input_scan_nonzero), against its entries' names sorted by their identifiers' ids and their bytes' hashes: one
// search per declaration, and per asm label, rather than per entry; and their chains by pointer jumping, in at most 33
// rounds over the entries. musl's units have one to a few; sqlite3.c and this compiler have none.

#ifndef BUSTER_CHECK_PROGRAM_ALIAS_INCLUDED
#define BUSTER_CHECK_PROGRAM_ALIAS_INCLUDED 1

// The entry of `object`'s alias, ifunc or weakref, plus one, or 0: a search of the entries, which are in object order.
static uint32_t check_program_alias_of(const CheckProgramAlias* aliases, uint32_t count, uint32_t object)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        low = aliases[middle].object < object ? middle + 1 : low;
        high = aliases[middle].object < object ? high : middle;
    }
    return low < count && aliases[low].object == object ? low + 1 : 0;
}

// The object whose symbol a reference to `object` takes: a weakref's target's, where the unit has an object of its name,
// and otherwise its own.
static uint32_t check_program_alias_redirect(const CheckProgram* program, uint32_t object)
{
    const uint32_t entry = program->alias_count != 0 ? check_program_alias_of(program->aliases, program->alias_count, object) : 0;
    const CheckProgramAlias* alias = entry != 0 ? program->aliases + entry - 1 : NULL;
    return alias != NULL && alias->kind == CHECK_PROGRAM_WEAKREF && alias->target_object != 0 ? alias->target_object : object;
}

// Whether `object` is a weakref's whose own symbol is a weak reference to its name: one whose target isn't an object of
// the unit.
static bool check_program_alias_external(const CheckProgram* program, uint32_t object)
{
    const uint32_t entry = check_program_alias_of(program->aliases, program->alias_count, object);
    return entry != 0 && program->aliases[entry - 1].kind == CHECK_PROGRAM_WEAKREF && program->aliases[entry - 1].target_object == 0;
}

// Whether `object` is a weakref's target that the program refers to through weakrefs alone (its state's
// BUSTER_CHECK_PROGRAM_ALIAS_WEAK): a weak reference to its name where the unit doesn't define it, even where its own
// declarations give it internal linkage (`static int f(void);`), as clang's object has it. A search of the entries,
// which only an undefined symbol of internal linkage, a rare one, asks for.
static bool check_program_alias_weak_target(const CheckProgram* program, uint32_t object)
{
    bool weak = false;
    for (uint32_t k = 0; !weak && k < program->alias_count; k += 1)
    {
        weak = program->aliases[k].kind == CHECK_PROGRAM_WEAKREF && program->aliases[k].target_object == object &&
               (program->alias_states[k] & BUSTER_CHECK_PROGRAM_ALIAS_WEAK) != 0;
    }
    return weak;
}

// The first search position of `key` among `count` sorted keys' high halves.
static uint32_t check_program_alias_lower(const uint64_t* keys, uint32_t count, uint32_t key)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        low = (uint32_t)(keys[middle] >> 32) < key ? middle + 1 : low;
        high = (uint32_t)(keys[middle] >> 32) < key ? high : middle;
    }
    return low;
}

static bool check_program_alias_same(String8 a, String8 b)
{
    return a.length == b.length && (a.length == 0 || memcmp(a.pointer, b.pointer, a.length) == 0);
}

// A declaration's asm label (not a register variable's), or an empty one.
static String8 check_program_alias_label(const CheckInput* input, uint32_t token)
{
    const CheckInputAttributes* attributes = input->registers[token] ? NULL : input->attributes[token];
    String8 label = { 0 };
    for (uint32_t i = 0; attributes != NULL && label.length == 0 && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        label = !attribute->ignored && attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL ? attribute->text : label;
    }
    return label;
}

// Whether a declaration's name may be one an alias's, ifunc's or weakref's names: a declaring token of a static object
// with linkage (at file scope, or declaring the file-scope entity), neither a register variable's nor a compound
// literal's, whose object isn't a weakref's.
static bool check_program_alias_named(const CheckProgram* program, const CheckProgramAlias* aliases, uint32_t count, uint32_t token)
{
    const CheckInput* input = program->input;
    const uint32_t object = input->objects[token];
    const uint32_t entry = input->declarations[token] == token && input->static_objects[token] && input->linkage[token] != 0 && !input->registers[token] &&
                           !input->compound_objects[token] && object != 0 && object < program->object_count ? check_program_alias_of(aliases, count, object) :
                                                                                                                UINT32_MAX;
    return entry == 0 || (entry != UINT32_MAX && aliases[entry - 1].kind != CHECK_PROGRAM_WEAKREF);
}

// The entries' names' objects (CheckProgramAlias.target_object), with their definitions: one per distinct name, a
// group, which `groups` gives each entry. A group's object is its earliest declaration's that the name names: by its
// identifier where its object has no asm label (on any declaration), or by its label, and an overloadable function's
// by its mangled name where it has no label, never by its identifier. The first scan finds the labels and the
// identifiers' matches, the second the groups' objects' definitions: a function's body, or a variable's definition,
// else its first tentative one.
static bool check_program_alias_names(CheckProgram* program, CheckProgramAlias* aliases, uint32_t count, const InternTable* identifiers, bool scalar,
                                      Arena* scratch)
{
    const CheckInput* input = program->input;
    const Parser* parser = program->parser;
    const uint32_t token_count = input->token_count;
    const uint64_t mark = scratch->length;
    // The entries by their names' hashes, then each entry's group: the first entry of its name in that order. Each sorted
    // array has a temporary of its own, which the sort may leave its result in (c_bindings_sort).
    uint64_t* hashes = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count * 2);
    uint32_t* groups = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count);
    // The groups' identifiers, as keys of the identifier and the group, sorted; their earliest declarations; their
    // objects as keys of the object and the group, sorted; and their definitions and tentative definitions.
    uint64_t* names = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count * 2);
    uint32_t* earliest = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count);
    uint64_t* objects = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count * 2);
    uint32_t* definitions = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count);
    uint32_t* tentatives = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count);
    // The declarations with an asm label, as keys of their object and their token, sorted by object: at most a token's
    // each, of which only the labels' pages are touched.
    uint64_t* labels = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)token_count + 1);
    uint64_t* label_sorting = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)token_count + 1);
    bool valid = hashes != 0 && groups != 0 && names != 0 && earliest != 0 && objects != 0 && definitions != 0 && tentatives != 0 && labels != 0 &&
                 label_sorting != 0;
    uint32_t group_count = 0;
    for (uint32_t k = 0; valid && k < count; k += 1)
    {
        hashes[k] = (uint64_t)(uint32_t)link_hash((const uint8_t*)aliases[k].target.pointer, aliases[k].target.length) << 32 | k;
    }
    const uint64_t* by_hash = valid && count > 1 ? c_bindings_sort(hashes, hashes + count, count, !BUSTER_SCALAR_PATH(scalar), UINT32_MAX) : hashes;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const uint32_t k = (uint32_t)by_hash[i];
        // An earlier entry of the same hash with the same name gives its group (a run of one hash is a name or two).
        uint32_t group = UINT32_MAX;
        for (uint32_t j = i; group == UINT32_MAX && j != 0 && by_hash[j - 1] >> 32 == by_hash[i] >> 32; j -= 1)
        {
            const uint32_t other = (uint32_t)by_hash[j - 1];
            group = check_program_alias_same(aliases[other].target, aliases[k].target) ? groups[other] : UINT32_MAX;
        }
        if (group == UINT32_MAX)
        {
            group = group_count++;
            const String8 target = aliases[k].target;
            // Without the unit's identifiers (a caller's that gave none), only a label can be the name.
            const IdentId identifier = identifiers != NULL && target.length != 0 && target.length < UINT32_MAX ?
                                       intern_find(identifiers, (const uint8_t*)target.pointer, (uint32_t)target.length) : 0;
            names[group] = (uint64_t)identifier << 32 | group;
            earliest[group] = BUSTER_CHECK_NONE;
            definitions[group] = BUSTER_CHECK_NONE;
            tentatives[group] = BUSTER_CHECK_NONE;
        }
        groups[k] = group;
    }
    const uint64_t* by_name = valid && group_count > 1 ? c_bindings_sort(names, names + count, group_count, !BUSTER_SCALAR_PATH(scalar), UINT32_MAX) : names;
    // The first scan: the labels, then their objects in order.
    uint32_t label_count = 0;
    for (uint32_t first = 0; valid && first < token_count; first += 64)
    {
        const uint64_t lanes = BUSTER_SCALAR_PATH(scalar) ? check_input_scan_all(first, token_count) : check_input_scan_nonzero(input->static_objects, first, token_count);
        for (uint64_t selected = lanes; selected != 0; selected &= selected - 1)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctzll(selected);
            if (check_program_alias_named(program, aliases, count, token) && check_program_alias_label(input, token).length != 0)
            {
                labels[label_count++] = (uint64_t)input->objects[token] << 32 | token;
            }
        }
    }
    const uint64_t* by_object = valid && label_count > 1 ? c_bindings_sort(labels, label_sorting, label_count, !BUSTER_SCALAR_PATH(scalar), UINT32_MAX) : labels;
    // A label names its object: its group is the one of its name, by the hash and then the bytes.
    for (uint32_t l = 0; valid && l < label_count; l += 1)
    {
        const uint32_t token = (uint32_t)by_object[l];
        const String8 label = check_program_alias_label(input, token);
        const uint32_t hash = (uint32_t)link_hash((const uint8_t*)label.pointer, label.length);
        for (uint32_t i = check_program_alias_lower(by_hash, count, hash); i < count && (uint32_t)(by_hash[i] >> 32) == hash; i += 1)
        {
            const uint32_t k = (uint32_t)by_hash[i];
            const uint32_t group = groups[k];
            if (check_program_alias_same(aliases[k].target, label) && token < earliest[group])
            {
                earliest[group] = token;
            }
        }
    }
    // An overloadable function's name is the one the emission gives its symbol (check_program_emit): its mangled one
    // (check_mangle_name) where none of its declarations has a label, and never its identifier, which the scan below
    // passes over. Each such object of the unit's overload candidates once (`overloaded`: 1 for an overloadable
    // function's object, 2 once its name is matched), an object with a label left to the labels.
    const CheckGraph* graph = &input->graph;
    uint8_t* overloaded = valid && graph->overload_candidate_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, program->object_count) : 0;
    valid = valid && (graph->overload_candidate_count == 0 || overloaded != 0);
    if (overloaded != 0)
    {
        memset(overloaded, 0, program->object_count);
    }
    for (uint32_t i = 0; overloaded != 0 && i < graph->overload_candidate_count; i += 1)
    {
        const CheckOverloadCandidate* candidate = graph->overload_candidates + i;
        if ((candidate->flags & BUSTER_CHECK_OVERLOADABLE) != 0 && candidate->object != 0 && candidate->object < program->object_count)
        {
            overloaded[candidate->object] = 1;
        }
    }
    CheckMangleSource mangling = {
        .input = input,
        .parser = parser,
        .identifiers = identifiers,
        .types = program->types,
        .output = scratch,
        .scalar = BUSTER_SCALAR_PATH(scalar),
    };
    Scratch mangle = { 0 };
    for (uint32_t i = 0; valid && overloaded != 0 && identifiers != NULL && i < graph->overload_candidate_count; i += 1)
    {
        const CheckOverloadCandidate* candidate = graph->overload_candidates + i;
        const uint32_t object = candidate->object;
        const uint32_t token = candidate->declaration;
        const bool unnamed = object != 0 && object < program->object_count && overloaded[object] == 1 && token < token_count &&
                             check_program_alias_named(program, aliases, count, token);
        const uint32_t labelled = unnamed ? check_program_alias_lower(by_object, label_count, object) : label_count;
        if (unnamed && (labelled == label_count || (uint32_t)(by_object[labelled] >> 32) != object))
        {
            overloaded[object] = 2;
            mangle = mangle.arena == NULL ? scratch_begin(scratch) : mangle;
            mangling.internal = input->linkage[token] == 1;
            String8 spelling = { 0 };
            const CheckMangleStatus status = check_mangle_name(&mangling, candidate->type, parser->tokens.names[token], mangle.arena, &spelling);
            // A signature the mangling refuses names nothing here, and the emission refuses it if it emits the function.
            valid = status != CHECK_MANGLE_RESOURCE;
            const uint32_t hash = status == CHECK_MANGLE_OK ? (uint32_t)link_hash((const uint8_t*)spelling.pointer, spelling.length) : 0;
            for (uint32_t h = status == CHECK_MANGLE_OK ? check_program_alias_lower(by_hash, count, hash) : count; h < count && (uint32_t)(by_hash[h] >> 32) == hash;
                 h += 1)
            {
                const uint32_t k = (uint32_t)by_hash[h];
                const uint32_t group = groups[k];
                if (check_program_alias_same(aliases[k].target, spelling) && token < earliest[group])
                {
                    earliest[group] = token;
                }
            }
        }
    }
    if (mangle.arena != NULL)
    {
        scratch_end(mangle);
    }
    // An identifier names its object where no declaration of it has a label, but an overloadable function's.
    for (uint32_t first = 0; valid && first < token_count; first += 64)
    {
        const uint64_t lanes = BUSTER_SCALAR_PATH(scalar) ? check_input_scan_all(first, token_count) : check_input_scan_nonzero(input->static_objects, first, token_count);
        for (uint64_t selected = lanes; selected != 0; selected &= selected - 1)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctzll(selected);
            const bool named = check_program_alias_named(program, aliases, count, token) && (overloaded == 0 || overloaded[input->objects[token]] == 0);
            const uint32_t identifier = named ? parser->tokens.names[token] : 0;
            const uint32_t at = identifier != 0 ? check_program_alias_lower(by_name, group_count, identifier) : group_count;
            const uint32_t object = input->objects[token];
            const uint32_t labelled = at < group_count && (uint32_t)(by_name[at] >> 32) == identifier ? check_program_alias_lower(by_object, label_count, object) : label_count;
            if (at < group_count && (uint32_t)(by_name[at] >> 32) == identifier && (labelled == label_count || (uint32_t)(by_object[labelled] >> 32) != object))
            {
                const uint32_t group = (uint32_t)by_name[at];
                earliest[group] = token < earliest[group] ? token : earliest[group];
            }
        }
    }
    // The groups' objects, sorted, and the second scan: their definitions.
    for (uint32_t g = 0; valid && g < group_count; g += 1)
    {
        objects[g] = (uint64_t)(earliest[g] != BUSTER_CHECK_NONE ? input->objects[earliest[g]] : 0) << 32 | g;
    }
    const uint64_t* by_target = valid && group_count > 1 ? c_bindings_sort(objects, objects + count, group_count, !BUSTER_SCALAR_PATH(scalar), UINT32_MAX) : objects;
    for (uint32_t first = 0; valid && first < token_count; first += 64)
    {
        const uint64_t lanes = BUSTER_SCALAR_PATH(scalar) ? check_input_scan_all(first, token_count) : check_input_scan_nonzero(input->static_objects, first, token_count);
        for (uint64_t selected = lanes; selected != 0; selected &= selected - 1)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctzll(selected);
            const uint32_t object = input->declarations[token] == token && input->static_objects[token] ? input->objects[token] : 0;
            const bool function = input->declaration_kinds[token] == C_BINDING_FUNCTION;
            const bool defines = input->defined[token] || (!function && input->tentative[token]);
            for (uint32_t i = object != 0 ? check_program_alias_lower(by_target, group_count, object) : group_count;
                 i < group_count && (uint32_t)(by_target[i] >> 32) == object; i += 1)
            {
                const uint32_t group = (uint32_t)by_target[i];
                definitions[group] = input->defined[token] && definitions[group] == BUSTER_CHECK_NONE ? token : definitions[group];
                tentatives[group] = !function && input->tentative[token] && tentatives[group] == BUSTER_CHECK_NONE ? token : tentatives[group];
            }
            // An alias's or ifunc's own object that the unit also defines: a variable's tentative definition after the
            // alias, which clang takes as the alias's (Sema refuses the others).
            const uint32_t entry = defines && object != 0 ? check_program_alias_of(aliases, count, object) : 0;
            if (entry != 0 && aliases[entry - 1].kind != CHECK_PROGRAM_WEAKREF)
            {
                aliases[entry - 1].error = CHECK_PROGRAM_ALIAS_DEFINED;
            }
        }
    }
    // A weakref whose name names no object of the unit takes the symbol of its name's first such weakref (its group's
    // leader, its entry plus one), which names itself after it: one undefined symbol of the name, as clang's module has
    // one declaration of it.
    uint32_t* leaders = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : 0;
    valid = valid && leaders != 0;
    for (uint32_t g = 0; valid && g < group_count; g += 1)
    {
        leaders[g] = 0;
    }
    for (uint32_t k = 0; valid && k < count; k += 1)
    {
        const uint32_t group = groups[k];
        const uint32_t object = earliest[group] != BUSTER_CHECK_NONE ? input->objects[earliest[group]] : 0;
        const bool unnamed = aliases[k].kind == CHECK_PROGRAM_WEAKREF && object == 0;
        aliases[k].target_object = unnamed && leaders[group] != 0 ? aliases[leaders[group] - 1].object : object;
        aliases[k].definition = definitions[group] != BUSTER_CHECK_NONE ? definitions[group] : tentatives[group];
        leaders[group] = unnamed && leaders[group] == 0 ? k + 1 : leaders[group];
    }
    arena_pop_to(scratch, mark);
    return valid;
}

// Each alias's and ifunc's aliasee, base, type and error, from the targets' objects and definitions: the chains of
// aliases followed by pointer jumping (`jump`, an entry each: the entry whose target ends its chain, or one in a cycle),
// double-buffered, until no pointer moves, at most 33 rounds for 2^32 entries. The ifuncs come first, so that an alias
// of one has its resolver for its base.
static bool check_program_alias_chains(CheckProgram* program, CheckProgramAlias* aliases, uint32_t count, Arena* scratch)
{
    const CheckInput* input = program->input;
    const uint64_t mark = scratch->length;
    uint32_t* next = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count);
    uint32_t* jump = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count);
    uint32_t* jumped = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count);
    const bool valid = next != 0 && jump != 0 && jumped != 0;
    for (uint32_t k = 0; valid && k < count; k += 1)
    {
        const uint32_t entry = aliases[k].kind != CHECK_PROGRAM_WEAKREF && aliases[k].target_object != 0 ?
                               check_program_alias_of(aliases, count, aliases[k].target_object) : 0;
        next[k] = entry != 0 && aliases[entry - 1].kind == CHECK_PROGRAM_ALIAS ? entry - 1 : BUSTER_CHECK_NONE;
        jump[k] = next[k] != BUSTER_CHECK_NONE ? next[k] : k;
    }
    bool moved = valid;
    for (uint32_t round = 0; moved && round < 33; round += 1)
    {
        moved = false;
        for (uint32_t k = 0; k < count; k += 1)
        {
            jumped[k] = jump[jump[k]];
            moved |= jumped[k] != jump[k];
        }
        memcpy(jump, jumped, (uint64_t)count * sizeof(uint32_t));
    }
    for (uint32_t pass = 0; valid && pass < 2; pass += 1)
    {
        const uint8_t kind = pass == 0 ? CHECK_PROGRAM_IFUNC : CHECK_PROGRAM_ALIAS;
        for (uint32_t k = 0; k < count; k += 1)
        {
            CheckProgramAlias* alias = aliases + k;
            const uint32_t end = jump[k];
            const bool cycle = next[end] != BUSTER_CHECK_NONE;
            const uint32_t aliasee = cycle ? 0 : aliases[end].target_object;
            const uint32_t definition = cycle ? BUSTER_CHECK_NONE : aliases[end].definition;
            const uint32_t entry = aliasee != 0 ? check_program_alias_of(aliases, count, aliasee) : 0;
            const bool ifunc = entry != 0 && aliases[entry - 1].kind == CHECK_PROGRAM_IFUNC;
            const bool function = input->declaration_kinds[alias->token] == C_BINDING_FUNCTION;
            const bool function_aliasee = ifunc || (definition != BUSTER_CHECK_NONE && input->declaration_kinds[definition] == C_BINDING_FUNCTION);
            const TypeId resolver = kind == CHECK_PROGRAM_IFUNC && function_aliasee && !ifunc ?
                                    type_return(program->types, type_strip_aligned(program->types, input->declared_types[definition])) : 0;
            const uint8_t returned = resolver != 0 ? check_action_kind(program->types, resolver) : BUSTER_TYPE_KIND_NONE;
            if (alias->kind == kind)
            {
                alias->aliasee = aliasee;
                alias->definition = definition;
                alias->aliasee_name = aliases[end].target;
                alias->error = alias->error != CHECK_PROGRAM_ALIAS_OK ? alias->error :
                               cycle || aliasee == alias->object ? CHECK_PROGRAM_ALIAS_CYCLE :
                               aliasee == 0 || (definition == BUSTER_CHECK_NONE && !ifunc) ? CHECK_PROGRAM_ALIAS_UNDEFINED :
                               kind == CHECK_PROGRAM_IFUNC && (ifunc || !function_aliasee) ? CHECK_PROGRAM_ALIAS_NOT_FUNCTION :
                               kind == CHECK_PROGRAM_IFUNC && returned != BUSTER_TYPE_KIND_POINTER && returned != BUSTER_TYPE_KIND_NULLPTR ? CHECK_PROGRAM_ALIAS_RESOLVER :
                               kind == CHECK_PROGRAM_ALIAS && function && !function_aliasee ? CHECK_PROGRAM_ALIAS_TO_VARIABLE :
                               kind == CHECK_PROGRAM_ALIAS && !function && function_aliasee ? CHECK_PROGRAM_ALIAS_TO_FUNCTION :
                               kind == CHECK_PROGRAM_ALIAS && !function && input->thread_local[alias->token] != input->thread_local[definition] ?
                                   CHECK_PROGRAM_ALIAS_THREAD_LOCAL :
                                   CHECK_PROGRAM_ALIAS_OK;
                // An aliased ifunc's base is its resolver, with its error: its errors are its own, which a valid alias of
                // it doesn't repeat (clang's aliasee is the ifunc, which is defined).
                const CheckProgramAlias* aliased = ifunc ? aliases + entry - 1 : NULL;
                const bool based = alias->error == CHECK_PROGRAM_ALIAS_OK && (aliased == NULL || aliased->error == CHECK_PROGRAM_ALIAS_OK);
                alias->base = !based ? 0 : aliased != NULL ? aliased->base : aliasee;
                alias->type = kind == CHECK_PROGRAM_IFUNC || ifunc ? BUSTER_LINK_SYMBOL_IFUNC :
                              function ? BUSTER_LINK_SYMBOL_FUNCTION :
                              definition != BUSTER_CHECK_NONE && input->thread_local[definition] ? BUSTER_LINK_SYMBOL_TLS : BUSTER_LINK_SYMBOL_OBJECT;
            }
        }
    }
    arena_pop_to(scratch, mark);
    return valid;
}

// The unit's entries (CheckProgram.aliases) from `events`' keys of the file-scope declarations with an alias, ifunc or
// weakref attribute: one per object, from its first such declaration, the events' keys sorted by object (stably, so each
// object's are in token order), into `output`; their names resolved and their chains followed; and each alias's and
// ifunc's object `needed`, as clang emits every one of them. A weakref without a target (which clang refuses: "must also
// have an alias attribute") has the error UNDEFINED. Returns false where there was no room.
static bool check_program_aliases(CheckProgram* program, CheckProgramTargetEvents* events, uint8_t* needed, bool scalar, Arena* output, Arena* scratch)
{
    const CheckInput* input = program->input;
    const uint64_t mark = scratch->length;
    const uint32_t event_count = events->alias_count;
    const InternTable* identifiers = program->identifiers != NULL ? program->identifiers :
                                     program->parser->preprocessor != NULL ? program->parser->preprocessor->table : NULL;
    uint64_t* sorting = events->room && event_count > 1 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, event_count) : 0;
    bool valid = events->room && (event_count <= 1 || sorting != 0);
    const uint64_t* keys = valid && event_count > 1 ? c_bindings_sort(events->alias_keys, sorting, event_count, !BUSTER_SCALAR_PATH(scalar), UINT32_MAX) : events->alias_keys;
    uint32_t count = 0;
    for (uint32_t e = 0; valid && e < event_count; e += 1)
    {
        count += e == 0 || keys[e] >> 32 != keys[e - 1] >> 32;
    }
    CheckProgramAlias* aliases = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramAlias, count) : 0;
    uint8_t* states = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, count) : 0;
    valid = valid && (count == 0 || (aliases != 0 && states != 0));
    uint32_t entries = 0;
    for (uint32_t e = 0; valid && e < event_count; e += 1)
    {
        if (e == 0 || keys[e] >> 32 != keys[e - 1] >> 32)
        {
            const uint32_t token = (uint32_t)keys[e];
            const CheckInputAttributes* attributes = input->attributes[token];
            const CheckInputAttribute* weakref = NULL;
            const CheckInputAttribute* ifunc = NULL;
            const CheckInputAttribute* alias = NULL;
            for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
            {
                const CheckInputAttribute* attribute = attributes->records + i;
                weakref = weakref == NULL && !attribute->ignored && attribute->kind == CHECK_ATTRIBUTE_WEAKREF ? attribute : weakref;
                ifunc = ifunc == NULL && !attribute->ignored && attribute->kind == CHECK_ATTRIBUTE_IFUNC ? attribute : ifunc;
                alias = alias == NULL && !attribute->ignored && attribute->kind == CHECK_ATTRIBUTE_ALIAS ? attribute : alias;
            }
            const CheckInputAttribute* named = weakref != NULL ? (weakref->argument_count != 0 || alias == NULL ? weakref : alias) : ifunc != NULL ? ifunc : alias;
            const uint8_t kind = weakref != NULL ? CHECK_PROGRAM_WEAKREF : ifunc != NULL ? CHECK_PROGRAM_IFUNC : CHECK_PROGRAM_ALIAS;
            aliases[entries] = (CheckProgramAlias){
                .object = (uint32_t)(keys[e] >> 32),
                .token = token,
                .attribute = weakref != NULL ? weakref->name_token : named != NULL ? named->name_token : token,
                .target = named != NULL && (named->kind != CHECK_ATTRIBUTE_WEAKREF || named->argument_count != 0) ? named->text : (String8){ 0 },
                .definition = BUSTER_CHECK_NONE,
                .kind = kind,
            };
            states[entries] = 0;
            entries += 1;
        }
    }
    valid = valid && (count == 0 || check_program_alias_names(program, aliases, count, identifiers, BUSTER_SCALAR_PATH(scalar), scratch));
    valid = valid && (count == 0 || check_program_alias_chains(program, aliases, count, scratch));
    for (uint32_t k = 0; valid && k < count; k += 1)
    {
        CheckProgramAlias* alias = aliases + k;
        alias->error = alias->kind == CHECK_PROGRAM_WEAKREF && alias->target.length == 0 ? CHECK_PROGRAM_ALIAS_UNDEFINED : alias->error;
        needed[alias->object] |= alias->kind != CHECK_PROGRAM_WEAKREF;
    }
    if (valid)
    {
        program->aliases = aliases;
        program->alias_states = states;
        program->alias_count = count;
    }
    arena_pop_to(scratch, mark);
    return valid;
}

// Numbers the `needed` objects' symbols (check_program_needed_number), into `program`'s columns from `output`, after its
// weakrefs' references are moved to their targets: a referenced weakref with a target object takes the target's symbol
// rather than one of its own, and makes the target needed, its undefined symbol weak in `states` (zero before) where
// nothing else needs it (clang's GetWeakRefReference, whose extern_weak declaration an ordinary reference makes
// external), which the first loop decides for every weakref before the second marks any target. Returns false where
// there was no room.
static bool check_program_alias_number(CheckProgram* program, uint8_t* needed, uint8_t* states, bool scalar, Arena* output)
{
    for (uint32_t k = 0; k < program->alias_count; k += 1)
    {
        const CheckProgramAlias* alias = program->aliases + k;
        const bool moved = alias->kind == CHECK_PROGRAM_WEAKREF && alias->target_object != 0 && needed[alias->object];
        states[k] |= moved && !needed[alias->target_object] ? BUSTER_CHECK_PROGRAM_ALIAS_WEAK : 0;
    }
    for (uint32_t k = 0; k < program->alias_count; k += 1)
    {
        const CheckProgramAlias* alias = program->aliases + k;
        if (alias->kind == CHECK_PROGRAM_WEAKREF && alias->target_object != 0 && needed[alias->object])
        {
            needed[alias->target_object] = 1;
            needed[alias->object] = 0;
        }
    }
    program->symbol_count = check_program_needed_count(needed, program->object_count, BUSTER_SCALAR_PATH(scalar));
    program->symbol_objects = program->symbol_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, program->symbol_count) : 0;
    const bool valid = program->symbol_count == 0 || program->symbol_objects != 0;
    if (valid && program->symbol_count != 0)
    {
        check_program_needed_number(needed, program->object_count, program->object_symbols, program->symbol_objects, BUSTER_SCALAR_PATH(scalar));
    }
    for (uint32_t k = 0; valid && k < program->alias_count; k += 1)
    {
        const CheckProgramAlias* alias = program->aliases + k;
        if (alias->kind == CHECK_PROGRAM_WEAKREF && alias->target_object != 0)
        {
            program->object_symbols[alias->object] = program->object_symbols[alias->target_object];
        }
    }
    return valid;
}

#endif

// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The real consumer of CheckProgram: lower, validate, compile, size branches, encode, and publish code atoms and
// their symbolic fixups in the Link shared with static data. check_program assigns source object symbols;
// lowering appends helper and explicit target-runtime symbols before any atom/fixup publication.
// data_capacity reserves the data consumer's atoms/fixups; that consumer defines its already-created symbols.
// The caller performs final link_check after both publications. Output tables/names and byte arenas are distinct
// from scratch; all three roll back on failure. Backend template initialization precedes concurrent workers.

// What an executable's link binds against (the direct path, #157): the C runtime, which bind_begin joins between the
// program's symbols and its atoms, its arrays from `arena` and its atoms' bytes copied into the emission's byte arena.
// check_program_emit makes room for it in the link and calls bind_begin; its caller adds the static data and calls
// bind_end. An object's emission has none.
typedef struct CheckProgramBinding CheckProgramBinding;
struct CheckProgramBinding
{
    const Runtime* runtime;
    Bind* bind;
    BindPath path;
    Arena* arena;
    // The static data the caller publishes after the code (check_data_prepare's objects): binding takes their symbols,
    // as the bodies' and the helpers', for the program's definitions before their atoms join.
    const CheckDataObject* data_objects;
    uint32_t data_count;
    // When bind_begin fails about a name (Bind.name), its bytes, since a failed emission rolls its link back.
    char failed_name[256];
};

typedef struct CheckProgramCode CheckProgramCode;
struct CheckProgramCode
{
    Link link;
    CheckProgramLowered lowered;
    AtomId* function_atoms;
    CheckProgramStatus status;
    Tier0Error backend_error;
    uint32_t token;
    uint32_t backend_node;
    // CheckProgramLowered.reason, or the attribute or construct the emission refused.
    const char* reason;
    uint64_t records;
    uint64_t deferred;
    uint64_t code_bytes;
};

// The local symbols whose names the compiler makes are named as clang names them, so that tools and #129's comparison
// with clang's objects (c_lowering_symbols.c) read them alike: a block-scope static is "function.name", its function's
// symbol name (its asm label, if it has one) and its own (an asm label of the static's own replaces both), and a
// file-scope compound literal is ".compoundliteral". A made name that a symbol has already (one the source gives, an
// identifier or an asm label, or one made before) gets ".N", N from one counter for the unit, as LLVM makes a name
// unique in its module. `taken` is a table of mask + 1 NameIds, 0 for an empty slot, with room for every symbol's name
// and at least as many empty slots.
typedef struct CheckProgramNames CheckProgramNames;
struct CheckProgramNames
{
    NameId* taken;
    uint32_t mask;
    uint32_t unique;
};

// Whether a name of `names` is the `length` bytes at `bytes`, of hash `hash`: its slot in `slot` if so, and the empty
// slot it would take if not.
static bool check_program_emit_taken(const Link* link, const CheckProgramNames* names, const uint8_t* bytes, uint32_t length, uint64_t hash, uint32_t* slot)
{
    uint32_t probe = (uint32_t)hash & names->mask;
    bool taken = false;
    while (names->taken[probe] != 0 && !taken)
    {
        const NameId other = names->taken[probe];
        taken = link->names.hash[other] == hash && link->names.length[other] == length && memcmp(link->names.bytes[other], bytes, length) == 0;
        probe = taken ? probe : (probe + 1) & names->mask;
    }
    *slot = probe;
    return taken;
}

// Adds `spelling`, the name the source gives a symbol, to the link's names and to the names a made one must not be: its
// NameId, or 0 when the link has no room.
static NameId check_program_emit_given_name(Link* link, CheckProgramNames* names, const uint8_t* spelling, uint32_t length)
{
    const uint64_t hash = link_hash(spelling, length);
    uint32_t slot = 0;
    const bool taken = check_program_emit_taken(link, names, spelling, length, hash, &slot);
    const NameId result = link_add_name_hashed(link, spelling, length, hash);
    if (!taken && result != 0)
    {
        names->taken[slot] = result;
    }
    return result;
}

// Adds `base`, or the first of its ".N" spellings no name of `names` has, to the link's names: its NameId, or 0 when the
// link or scratch has no room.
static NameId check_program_emit_made_name(Link* link, CheckProgramNames* names, const uint8_t* base, uint32_t base_length, Arena* scratch)
{
    // ".N" takes at most 11 bytes past the base, and snprintf a 12th for its NUL.
    uint8_t* candidate = base_length <= UINT32_MAX - 12 ? arena_push(scratch, (uint64_t)base_length + 12, 1) : 0;
    NameId result = 0;
    if (candidate != 0)
    {
        memcpy(candidate, base, base_length);
        uint32_t length = base_length;
        uint64_t hash = link_hash(candidate, length);
        uint32_t slot = 0;
        while (check_program_emit_taken(link, names, candidate, length, hash, &slot))
        {
            names->unique += 1;
            length = base_length + (uint32_t)snprintf((char*)candidate + base_length, 12, ".%u", names->unique);
            hash = link_hash(candidate, length);
        }
        result = link_add_name_hashed(link, candidate, length, hash);
        names->taken[slot] = result;
    }
    return result;
}

// With `verify`, every function's records must pass x86_validate before they are sized and encoded (the tests', and
// #145's c-testsuite harness's, check of Tier 0's output), or the emission fails with CHECK_PROGRAM_BACKEND.
static bool check_program_emit(CheckProgramCode* result, const CheckProgram* program, const Parser* parser,
                               const InternTable* identifiers, LinkCapacity data_capacity, bool scalar, Arena* output, Arena* bytes, Arena* scratch,
                               CheckProgramBinding* binding, bool verify)
{
    *result = (CheckProgramCode){ .token = BUSTER_CHECK_NONE };
    const uint64_t mark = output->length;
    const uint64_t byte_mark = bytes->length;
    const uint64_t temporary = scratch->length;
    bool valid = output != bytes && output != scratch && bytes != scratch && program->status == CHECK_PROGRAM_OK;
    result->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_INVALID;
    if (valid && !tier0_initialized)
    {
        valid = x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize();
        result->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_BACKEND;
    }
    if (valid)
    {
        valid = check_program_lower(&result->lowered, program, scalar, output, scratch);
        if (!valid)
        {
            result->status = result->lowered.status;
            result->token = result->lowered.token;
            result->backend_node = result->lowered.backend_node;
            result->reason = result->lowered.reason;
        }
    }
    for (uint32_t i = 0; valid && i < program->input->graph.overload_candidate_count; i += 1)
    {
        const CheckOverloadCandidate* candidate = program->input->graph.overload_candidates + i;
        if ((candidate->flags & BUSTER_CHECK_OVERLOADABLE) != 0 && candidate->object != 0 && candidate->object < program->object_count &&
            program->object_symbols[candidate->object] != 0)
        {
            // A typed overload selection does not supply the required ABI symbol mangling.
            result->status = CHECK_PROGRAM_UNSUPPORTED;
            result->reason = "overloadable functions (their mangled names)";
            result->token = candidate->declaration;
            valid = false;
        }
    }
    const uint32_t count = program->body_count;
    Tier0Code* codes = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, Tier0Code, count) : 0;
    uint8_t* alignments = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->symbol_count + 1) : 0;
    String8* symbol_names = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, String8, (uint64_t)program->symbol_count + 1) : 0;
    uint8_t* retained_objects = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, program->object_count) : 0;
    result->function_atoms = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, AtomId, count) : 0;
    valid = valid && alignments != 0 && symbol_names != 0 && retained_objects != 0 && (count == 0 || (codes != 0 && result->function_atoms != 0));
    if (valid)
    {
        memset(alignments, 0, (uint64_t)program->symbol_count + 1);
        memset(symbol_names, 0, ((uint64_t)program->symbol_count + 1) * sizeof(String8));
        memset(retained_objects, 0, program->object_count);
        for (uint32_t token = 0; token < program->input->token_count; token += 1)
        {
            const uint32_t object = program->input->objects[token];
            if (program->input->declarations[token] == token && object != 0 && object < program->object_count)
            {
                bool gnu_inline = false;
                retained_objects[object] |= check_program_attribute_roots(program->input->attributes[token],
                    program->input->declaration_kinds[token] == C_BINDING_FUNCTION, &gnu_inline) != 0;
            }
        }
        for (uint32_t token = 0; valid && token < program->input->token_count; token += 1)
        {
            const CheckInputAttributes* attributes = program->input->attributes[token];
            const uint32_t object = program->input->objects[token];
            if (program->input->declarations[token] == token && attributes != 0 && object != 0 && object < program->object_count)
            {
                const SymbolId symbol = program->object_symbols[object];
                alignments[symbol] = attributes->alignment_log2 > alignments[symbol] ? attributes->alignment_log2 : alignments[symbol];
                for (uint32_t i = 0; valid && i < attributes->count; i += 1)
                {
                    const CheckInputAttribute* attribute = attributes->records + i;
                    if (symbol != 0 && !attribute->ignored && attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL && !program->input->registers[token])
                    {
                        const String8 previous = symbol_names[symbol];
                        valid = previous.length == 0 || (previous.length == attribute->text.length &&
                                memcmp(previous.pointer, attribute->text.pointer, previous.length) == 0);
                        if (valid)
                        {
                            symbol_names[symbol] = attribute->text;
                        }
                        else
                        {
                            result->status = CHECK_PROGRAM_INVALID;
                            result->token = attribute->name_token;
                        }
                    }
                    const CheckInputAttributeKind kind = attribute->kind;
                    const bool alias = kind == CHECK_ATTRIBUTE_ALIAS || kind == CHECK_ATTRIBUTE_IFUNC;
                    const bool visible_alias = alias && (symbol != 0 || program->input->linkage[token] == 2 || retained_objects[object]);
                    const bool unpublished = kind == CHECK_ATTRIBUTE_CONSTRUCTOR || kind == CHECK_ATTRIBUTE_DESTRUCTOR ||
                                             kind == CHECK_ATTRIBUTE_WEAK || kind == CHECK_ATTRIBUTE_WEAKREF || kind == CHECK_ATTRIBUTE_RETAIN ||
                                             kind == CHECK_ATTRIBUTE_SECTION || kind == CHECK_ATTRIBUTE_COMMON ||
                                             (kind == CHECK_ATTRIBUTE_VISIBILITY && !check_input_attribute_equal(attribute->text, "default"));
                    if (!attribute->ignored && (visible_alias || (symbol != 0 && unpublished)))
                    {
                        // The typed source contract retains these obligations. Until symbol aliases and startup
                        // lists, section properties and weak/visibility semantics are published, refusing code
                        // emission is required. A dropped definition has no object-format obligation of its own.
                        result->status = CHECK_PROGRAM_UNSUPPORTED;
                        result->reason = "this attribute (alias, ifunc, constructor, destructor, weak, retain, section, common or a visibility)";
                        result->token = attribute->name_token;
                        valid = false;
                    }
                }
            }
        }
    }
    // How Tier 0 reaches each symbol (Tier0Function.symbol_access), one column for every body: a symbol this unit doesn't
    // define may be a shared library's, which the direct path, without copy relocations, reaches only through its GOT
    // slot (an executable's writer relaxes the load where the symbol is in the image), and a thread-local is the image's
    // by local-exec, or another's by initial-exec. The unit defines its bodies' functions, the objects it defines or
    // tentatively defines (as check_data_prepare selects them), its literals and whatever has no external linkage. The
    // helpers are the unit's, and the C runtime's errno function isn't.
    const uint32_t access_count = program->symbol_count + result->lowered.helpers.count + result->lowered.runtime_symbol_count + 1;
    uint8_t* access = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, access_count) : 0;
    uint8_t* defined = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->object_count + 1) : 0;
    valid = valid && access != 0 && defined != 0;
    if (valid)
    {
        memset(access, BUSTER_TIER0_ACCESS_DIRECT, access_count);
        memset(defined, 0, (uint64_t)program->object_count + 1);
        for (uint32_t b = 0; b < count; b += 1)
        {
            defined[program->bodies[b].object] = 1;
        }
        for (uint32_t token = 0; token < program->input->token_count; token += 1)
        {
            const uint32_t object = program->input->objects[token];
            if (program->input->declarations[token] == token && object < program->object_count && program->input->declaration_kinds[token] == C_BINDING_OBJECT &&
                (program->input->defined[token] || program->input->tentative[token]))
            {
                defined[object] = 1;
            }
        }
        for (uint32_t s = 0; s < program->symbol_count; s += 1)
        {
            const uint32_t object = program->symbol_objects[s];
            const uint32_t declaration = program->object_declarations[object];
            const bool literal = declaration == BUSTER_CHECK_NONE || program->input->compound_objects[declaration];
            const bool own = literal || defined[object] || program->input->linkage[declaration] != 2;
            const bool thread_local = !literal && program->input->thread_local[declaration];
            access[s + 1] = thread_local ? (own ? BUSTER_TIER0_ACCESS_LOCAL_EXEC : BUSTER_TIER0_ACCESS_INITIAL_EXEC) : own ? BUSTER_TIER0_ACCESS_DIRECT : BUSTER_TIER0_ACCESS_GOT;
        }
        if (result->lowered.runtime_symbol_count != 0)
        {
            access[result->lowered.errno_symbol] = BUSTER_TIER0_ACCESS_GOT;
        }
        for (uint32_t b = 0; b < count; b += 1)
        {
            result->lowered.functions[b].symbol_access = access;
            result->lowered.functions[b].symbol_count = access_count;
        }
    }
    uint64_t references = 0;
    for (uint32_t b = 0; valid && b < count; b += 1)
    {
        const Tier0Function* function = &result->lowered.functions[b];
        codes[b] = scalar ? tier0_compile_scalar(function, scratch) : tier0_compile(function, scratch);
        valid = codes[b].error == TIER0_OK;
        if (valid)
        {
            references += codes[b].reference_count;
            result->records += codes[b].count;
            result->deferred += codes[b].deferred;
        }
        else
        {
            // Tier 0's own refusals are features it doesn't have yet: an entry without a template (the conversions
            // between unsigned 64-bit integers and floating values), or values past its depth tables.
            const bool convert = codes[b].node < function->count && function->kinds[codes[b].node] == BUSTER_TYPED_CONVERT;
            result->status = codes[b].error == TIER0_ERROR_MEMORY ? CHECK_PROGRAM_RESOURCE :
                             codes[b].error == TIER0_ERROR_UNSUPPORTED || codes[b].error == TIER0_ERROR_DEPTH ? CHECK_PROGRAM_UNSUPPORTED : CHECK_PROGRAM_BACKEND;
            result->reason = codes[b].error == TIER0_ERROR_DEPTH ? "expressions with more values live at once than Tier 0 has registers for" :
                             codes[b].error != TIER0_ERROR_UNSUPPORTED ? NULL :
                             convert ? "conversions between unsigned 64-bit integers and floating types" : "this operation in Tier 0";
            result->backend_error = codes[b].error;
            result->backend_node = codes[b].node;
            result->token = program->bodies[b].declaration;
        }
    }
    const CheckProgramHelpers* helpers = &result->lowered.helpers;
    for (uint32_t h = 0; valid && h < helpers->count; h += 1)
    {
        references += helpers->items[h].code.reference_count;
        result->records += helpers->items[h].code.count;
        result->deferred += helpers->items[h].code.deferred;
    }
    // An executable's link has room for the runtime's records too.
    uint64_t name_bytes = 0;
    uint64_t atom_bytes = 0;
    const LinkCapacity runtime = binding != NULL && valid ? bind_capacity(binding->runtime, &name_bytes, &atom_bytes) : (LinkCapacity){ 0 };
    const uint64_t atom_count = (uint64_t)data_capacity.atoms + count + helpers->count + runtime.atoms;
    const uint64_t fixup_count = (uint64_t)data_capacity.fixups + references + runtime.fixups;
    const uint64_t wide_count = (uint64_t)data_capacity.wide_addends + references + runtime.wide_addends;
    const uint64_t symbol_count = (uint64_t)program->symbol_count + helpers->count + result->lowered.runtime_symbol_count + runtime.symbols;
    const uint64_t name_count = (uint64_t)program->symbol_count + helpers->count + result->lowered.runtime_symbol_count + runtime.names;
    valid = valid && atom_count < UINT32_MAX && fixup_count < UINT32_MAX && wide_count < UINT32_MAX && symbol_count < UINT32_MAX &&
            name_count < UINT32_MAX && (uint64_t)data_capacity.priorities + runtime.priorities < UINT32_MAX;
    const LinkCapacity capacity = {
        .names = valid ? (uint32_t)name_count : 0,
        .atoms = valid ? (uint32_t)atom_count : 0,
        .symbols = valid ? (uint32_t)symbol_count : 0,
        .fixups = valid ? (uint32_t)fixup_count : 0,
        .wide_addends = valid ? (uint32_t)wide_count : 0,
        .priorities = valid ? data_capacity.priorities + runtime.priorities : 0,
    };
    valid = valid && link_initialize(&result->link, output, output, capacity);
    // A block-scope static's declaration is in a body (declaration_bodies) and has no linkage; a block-scope extern or
    // function declaration has linkage, and its symbol is the file-scope entity's.
    const CheckInput* input = program->input;
    uint32_t slots = 2;
    while (slots <= (uint64_t)program->symbol_count * 2 && slots < (UINT32_C(1) << 31))
    {
        slots *= 2;
    }
    CheckProgramNames names = { .mask = slots - 1 };
    names.taken = valid && program->symbol_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, NameId, slots) : 0;
    NameId* given = valid && program->symbol_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, NameId, program->symbol_count) : 0;
    valid = valid && (program->symbol_count == 0 || (names.taken != 0 && given != 0));
    if (names.taken != 0)
    {
        memset(names.taken, 0, (uint64_t)slots * sizeof(NameId));
    }
    // The names the source gives come first, so that no made name is one of them.
    for (uint32_t s = 0; valid && s < program->symbol_count; s += 1)
    {
        const uint32_t declaration = program->object_declarations[program->symbol_objects[s]];
        const String8 renamed = symbol_names[s + 1];
        const bool made = declaration != BUSTER_CHECK_NONE &&
                          (input->compound_objects[declaration] ||
                           (renamed.length == 0 && input->declaration_bodies[declaration] != BUSTER_CHECK_NONE && input->linkage[declaration] == 0));
        given[s] = 0;
        if (declaration != BUSTER_CHECK_NONE && !made)
        {
            const uint32_t identifier = parser->tokens.names[declaration];
            given[s] = renamed.length != 0 ? check_program_emit_given_name(&result->link, &names, (const uint8_t*)renamed.pointer, (uint32_t)renamed.length) :
                                             check_program_emit_given_name(&result->link, &names, intern_bytes(identifiers, identifier),
                                                                           intern_length(identifiers, identifier));
            valid = given[s] != 0;
        }
    }
    for (uint32_t s = 0; valid && s < program->symbol_count; s += 1)
    {
        const uint32_t object = program->symbol_objects[s];
        const uint32_t declaration = program->object_declarations[object];
        NameId name = 0;
        uint8_t flags = BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT;
        if (declaration != BUSTER_CHECK_NONE)
        {
            const uint32_t identifier = parser->tokens.names[declaration];
            const String8 renamed = symbol_names[s + 1];
            const uint32_t opening = input->declaration_bodies[declaration];
            if (input->compound_objects[declaration])
            {
                name = check_program_emit_made_name(&result->link, &names, (const uint8_t*)".compoundliteral", sizeof(".compoundliteral") - 1, scratch);
            }
            else if (renamed.length == 0 && opening != BUSTER_CHECK_NONE && input->linkage[declaration] == 0)
            {
                // The function's name: its symbol's asm label, or its identifier (--lower-all selects the statics of
                // an inline-only function, which has no symbol, and then no label).
                const uint32_t function = opening < input->token_count ? input->body_declarations[opening] : BUSTER_CHECK_NONE;
                const uint32_t function_object = function != BUSTER_CHECK_NONE ? input->objects[function] : 0;
                const SymbolId function_symbol = function_object != 0 && function_object < program->object_count ? program->object_symbols[function_object] : 0;
                const String8 label = function_symbol != 0 ? symbol_names[function_symbol] : (String8){ 0 };
                const uint32_t function_identifier = function != BUSTER_CHECK_NONE ? parser->tokens.names[function] : 0;
                const uint8_t* context = label.length != 0 ? (const uint8_t*)label.pointer : intern_bytes(identifiers, function_identifier);
                const uint32_t context_length = label.length != 0 ? (uint32_t)label.length : intern_length(identifiers, function_identifier);
                const uint32_t own_length = intern_length(identifiers, identifier);
                const uint64_t base_length = (uint64_t)context_length + 1 + own_length;
                uint8_t* base = function != BUSTER_CHECK_NONE && base_length < UINT32_MAX - 12 ? arena_push(scratch, base_length, 1) : 0;
                if (base != 0)
                {
                    memcpy(base, context, context_length);
                    base[context_length] = '.';
                    memcpy(base + context_length + 1, intern_bytes(identifiers, identifier), own_length);
                    name = check_program_emit_made_name(&result->link, &names, base, (uint32_t)base_length, scratch);
                }
            }
            else
            {
                name = given[s];
            }
            const uint8_t kind = program->input->declaration_kinds[declaration] == C_BINDING_FUNCTION ? BUSTER_LINK_SYMBOL_FUNCTION :
                                 program->input->thread_local[declaration] ? BUSTER_LINK_SYMBOL_TLS : BUSTER_LINK_SYMBOL_OBJECT;
            flags = kind | (program->input->linkage[declaration] == 2 ? BUSTER_LINK_SYMBOL_GLOBAL : BUSTER_LINK_SYMBOL_LOCAL);
        }
        else if (object != 0 && object - 1 < parser->tokens.count && parser->tokens.kinds[object - 1] == PARSE_KIND_STRING)
        {
            char spelling[48];
            const int length = snprintf(spelling, sizeof(spelling), ".Lbuster_string_%u", object);
            name = link_add_name(&result->link, (const uint8_t*)spelling, (uint32_t)length);
        }
        else
        {
            result->status = CHECK_PROGRAM_INVALID;
            result->token = object != 0 ? object - 1 : 0;
        }
        valid = name != 0 && link_add_symbol(&result->link, name, 0, 0, 0, flags, BUSTER_LINK_SOURCE_NONE) == s + 1;
    }
    for (uint32_t h = 0; valid && h < helpers->count; h += 1)
    {
        const CheckProgramHelper* helper = helpers->items + h;
        char spelling[64];
        const int length = snprintf(spelling, sizeof(spelling), ".Lbuster_builtin_%u_%u", helper->id, helper->bytes);
        const NameId name = link_add_name(&result->link, (const uint8_t*)spelling, (uint32_t)length);
        valid = name != 0 && link_add_symbol(&result->link, name, 0, 0, 0, BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_FUNCTION,
                                            BUSTER_LINK_SOURCE_NONE) == helper->symbol;
    }
    if (valid && result->lowered.runtime_symbol_count != 0)
    {
        const NameId name = link_add_name(&result->link, (const uint8_t*)"__errno_location", sizeof("__errno_location") - 1);
        valid = name != 0 && result->lowered.runtime_symbol_count == 1 &&
                link_add_symbol(&result->link, name, 0, 0, 0, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION,
                                BUSTER_LINK_SOURCE_NONE) == result->lowered.errno_symbol;
    }
    // Every symbol of the program's exists: an executable's runtime joins now, before the program's atoms, with the
    // symbols this emission and the static data will define marked as the program's definitions (bind.c).
    for (uint32_t b = 0; valid && binding != NULL && b < count + helpers->count + binding->data_count; b += 1)
    {
        const uint32_t object = b < count ? program->bodies[b].object : b >= count + helpers->count ? binding->data_objects[b - count - helpers->count].object : 0;
        const SymbolId symbol = b >= count && b < count + helpers->count ? helpers->items[b - count].symbol :
                                object < program->object_count ? program->object_symbols[object] : 0;
        if (symbol != 0 && symbol < result->link.symbols.count)
        {
            result->link.symbols.source[symbol] = BUSTER_LINK_SOURCE_PROGRAM;
        }
    }
    if (valid && binding != NULL && !bind_begin(binding->bind, &result->link, binding->runtime, binding->path, bytes, 1, binding->arena))
    {
        const NameId name = binding->bind->name;
        if (name != 0 && name < result->link.names.count)
        {
            snprintf(binding->failed_name, sizeof(binding->failed_name), "%.*s", (int)result->link.names.length[name],
                     (const char*)result->link.names.bytes[name]);
        }
        result->status = binding->bind->error == BIND_ERROR_FULL ? CHECK_PROGRAM_RESOURCE : CHECK_PROGRAM_BACKEND;
        result->reason = "binding against the C runtime failed";
        valid = false;
    }
    for (uint32_t b = 0; valid && b < count + helpers->count; b += 1)
    {
        const bool source_body = b < count;
        const Tier0Code* code = source_body ? &codes[b] : &helpers->items[b - count].code;
        const SymbolId symbol = source_body ? program->object_symbols[program->bodies[b].object] : helpers->items[b - count].symbol;
        const uint8_t alignment = source_body && alignments[symbol] > 5 ? alignments[symbol] - 1 : 4;
        const uint64_t work_mark = scratch->length;
        uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)code->count + 1);
        uint32_t* fixup_ends = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, BUSTER_X86_ENCODER_FIXUP_CAPACITY(code->reference_count));
        const uint64_t byte_capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
        const uint64_t before = bytes->length;
        uint8_t* encoded = arena_push(bytes, byte_capacity, 16);
        valid = offsets != 0 && fixup_ends != 0 && encoded != 0;
        if (valid && verify && x86_validate(code->records, code->count) != code->count)
        {
            result->status = CHECK_PROGRAM_BACKEND;
            result->reason = "internal compiler error: a record of Tier 0's fails x86_validate";
            result->token = source_body ? program->bodies[b].declaration : BUSTER_CHECK_NONE;
            valid = false;
        }
        if (valid)
        {
            const BranchSizingCheck shape = branch_sizing_validate(code->records, code->count, code->branches, code->targets, code->branch_count);
            const BranchSizing sizing = shape.error == BRANCH_SIZING_OK ? size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets) : (BranchSizing){ .error = shape.error };
            const int64_t length = sizing.error != BRANCH_SIZING_OK ? -1 :
                                   scalar ? x86_encode_scalar(code->records, code->count, encoded, byte_capacity, 0, fixup_ends) :
                                            x86_encode(code->records, code->count, encoded, byte_capacity, 0, fixup_ends);
            valid = length >= 0;
            if (valid)
            {
                const uint64_t end = (uint64_t)(encoded - bytes->base) + (uint64_t)length;
                arena_pop_to(bytes, end > before ? end : before);
                const AtomId atom = link_add_atom(&result->link, BUSTER_LINK_CLASS_TEXT, alignment, (uint64_t)length, encoded, 0);
                valid = atom != 0 && symbol != 0 && link_define_symbol(&result->link, symbol, atom, 0, (uint64_t)length) &&
                        link_add_code_fixups(&result->link, atom, code->records, code->count, fixup_ends, code->reference_count, code->references, code->reference_count);
                if (valid)
                {
                    result->link.symbols.source[symbol] = BUSTER_LINK_SOURCE_PROGRAM;
                }
                if (source_body)
                {
                    result->function_atoms[b] = atom;
                }
                result->code_bytes += (uint64_t)length;
            }
            else
            {
                result->status = CHECK_PROGRAM_BACKEND;
                result->token = source_body ? program->bodies[b].declaration : BUSTER_CHECK_NONE;
            }
        }
        arena_pop_to(scratch, work_mark);
    }
    if (!valid)
    {
        const CheckProgramStatus status = result->status == CHECK_PROGRAM_OK ? CHECK_PROGRAM_RESOURCE : result->status;
        const uint32_t token = result->token;
        const Tier0Error backend = result->backend_error;
        const uint32_t backend_node = result->backend_node;
        const char* reason = result->reason;
        arena_pop_to(output, mark);
        arena_pop_to(bytes, byte_mark);
        *result = (CheckProgramCode){ .status = status, .token = token, .backend_error = backend, .backend_node = backend_node, .reason = reason };
    }
    arena_pop_to(scratch, temporary);
    return valid;
}

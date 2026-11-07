// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Checked C's retained typed streams linked by name, without an IR execution path or native-code fallback. Included
// after the source runner's data-only Link adapter and native bookkeeping names. All views live through execution.

static bool interpreter_source_defined(const IrSourceFile* files, uint32_t count, uint32_t input, SymbolId symbol)
{
    const Link* source = files[input].link;
    bool found = false;
    for (uint32_t u = 0; !found && u < count; u += 1)
    {
        const Link* link = files[u].link;
        for (SymbolId s = 1; !found && s < link->symbols.count; s += 1)
        {
            found = (link->symbols.flags[s] & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL &&
                    (link->symbols.atom[s] != 0 || link->symbols.source[s] == BUSTER_LINK_SOURCE_ABSOLUTE) &&
                    interpreter_link_same_name(source, symbol, link, s);
        }
    }
    return found;
}

static InterpreterResult interpreter_source_run(const IrSourceFile* files, uint32_t count, const InterpreterOptions* options, Arena* arena)
{
    InterpreterResult result = { .status = INTERPRETER_RESOURCE };
    uint64_t total = 0;
    uint64_t bodies = 0;
    bool ready = files != 0 && count != 0;
    for (uint32_t u = 0; ready && u < count; u += 1)
    {
        ready = files[u].status == IR_SOURCE_PREPARED && files[u].link != 0;
        total += ready ? files[u].link->symbols.count : 0;
        bodies += files[u].count;
    }
    ready = ready && total < UINT32_MAX - 3 && bodies <= total;
    const uint32_t capacity = ready ? (uint32_t)total : 0;
    uint32_t* offsets = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count) : 0;
    Link* data = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, Link, count) : 0;
    const Link** links = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, const Link*, count) : 0;
    Tier0Function* canonical = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, Tier0Function, bodies) : 0;
    const SymbolId** canonical_labels = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, const SymbolId*, bodies) : 0;
    uint32_t* canonical_offsets = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, bodies) : 0;
    Tier0Function* functions = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, Tier0Function, capacity) : 0;
    SymbolId* symbols = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, capacity) : 0;
    const SymbolId** labels = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, const SymbolId*, capacity) : 0;
    uint64_t* addresses = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, capacity) : 0;
    uint32_t* symbol_offsets = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity) : 0;
    InterpreterLinkExternal* externals = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, InterpreterLinkExternal, capacity) : 0;
    InterpreterNativeBinding* libraries = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, InterpreterNativeBinding, capacity) : 0;
    InterpreterNativeBinding* natives = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, InterpreterNativeBinding, capacity) : 0;
    InterpreterBinding* bindings = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, InterpreterBinding, (uint64_t)capacity + 3) : 0;
    ready = ready && offsets != 0 && data != 0 && links != 0 && canonical != 0 && canonical_labels != 0 && canonical_offsets != 0 && functions != 0 &&
            symbols != 0 && labels != 0 && addresses != 0 && symbol_offsets != 0 && externals != 0 && libraries != 0 && natives != 0 && bindings != 0;
    uint32_t bias = 0;
    uint32_t body = 0;
    uint32_t external_count = 0;
    uint32_t library_count = 0;
    void* gcc_s = 0;
    for (uint32_t u = 0; ready && u < count; u += 1)
    {
        const IrSourceFile* file = &files[u];
        const Link* link = file->link;
        offsets[u] = bias;
        const uint32_t first_body = body;
        ready = ssa_source_data_link(link, arena, &data[u]);
        links[u] = &data[u];
        for (uint32_t f = 0; ready && f < file->count; f += 1)
        {
            const IrSourceFunction* source = &file->functions[f];
            canonical[body] = *source->function;
            canonical_offsets[body] = bias;
            const uint32_t taken = source->function->label_count;
            uint32_t* entries = taken != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, taken) : 0;
            SymbolId* names = taken != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, taken) : 0;
            ready = taken == 0 || (entries != 0 && names != 0 && source->labels != 0);
            for (uint32_t k = 0; ready && k < taken; k += 1)
            {
                entries[k] = source->function->label_entries[k];
                names[k] = bias + source->labels[k];
                ready = external_count < capacity;
                if (ready)
                {
                    externals[external_count++] = (InterpreterLinkExternal){ .input = u, .symbol = source->labels[k], .address = (uintptr_t)&entries[k] };
                }
            }
            canonical[body].label_entries = entries;
            canonical_labels[body] = names;
            body += 1;
        }
        for (SymbolId s = 1; ready && s < link->symbols.count; s += 1)
        {
            const uint8_t flags = link->symbols.flags[s];
            const AtomId atom = link->symbols.atom[s];
            const bool function = (flags & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_FUNCTION;
            uint64_t address = 0;
            for (uint32_t f = 0; function && atom != 0 && f < file->count; f += 1)
            {
                const SymbolId own = file->functions[f].symbol;
                if (link->symbols.atom[own] == atom && link->symbols.value[own] == link->symbols.value[s])
                {
                    address = (uintptr_t)&canonical[first_body + f];
                    break;
                }
            }
            const NameId name = link->symbols.name[s];
            const bool undefined = atom == 0 && link->symbols.source[s] != BUSTER_LINK_SOURCE_ABSOLUTE &&
                                   (flags & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL && name != 0;
            bool earlier = false;
            for (uint32_t e = 0; undefined && !earlier && e < external_count; e += 1)
            {
                const InterpreterLinkExternal candidate = externals[e];
                earlier = interpreter_link_same_name(link, s, files[candidate.input].link, candidate.symbol);
            }
            char spelling[256];
            if (undefined && !earlier && !interpreter_source_defined(files, count, u, s) && link->names.length[name] < sizeof(spelling))
            {
                memcpy(spelling, link->names.bytes[name], link->names.length[name]);
                spelling[link->names.length[name]] = 0;
                void* native = dlsym(RTLD_DEFAULT, spelling);
                native = native != 0 ? native : test_libm(spelling);
                // A static executable must use its own host table; loading libgcc_s would bring a second libc.
                if (native == 0 && !BUSTER_HOST_STATIC_EXECUTABLE)
                {
                    gcc_s = gcc_s != 0 ? gcc_s : dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_LOCAL);
                    native = gcc_s != 0 ? dlsym(gcc_s, spelling) : 0;
                }
                address = (uintptr_t)native;
                if (address != 0 && function)
                {
                    libraries[library_count++] = (InterpreterNativeBinding){ .address = native, .flags = ssa_source_native_flags(spelling) };
                }
            }
            if (address != 0)
            {
                ready = external_count < capacity;
                if (ready)
                {
                    externals[external_count++] = (InterpreterLinkExternal){
                        .input = u,
                        .symbol = s,
                        .address = address,
                        .size = !function && file->sizes != 0 ? file->sizes[s] : 0,
                        .writable = !function,
                    };
                }
            }
        }
        bias += link->symbols.count;
    }
    InterpreterLink image = { 0 };
    const InterpreterLinkResult materialized = ready ? interpreter_link_materialize(&image, links, count, externals, external_count, arena) : (InterpreterLinkResult){ .status = INTERPRETER_LINK_RESOURCE };
    ready = ready && materialized.status == INTERPRETER_LINK_OK;
    if (!ready && materialized.status != INTERPRETER_LINK_RESOURCE)
    {
        fprintf(stderr, "typed source: data linking failed (status %u, unit %u, symbol %u, atom %u, fixup %u)\n", materialized.status, materialized.input,
                materialized.symbol, materialized.atom, materialized.fixup);
        result.status = INTERPRETER_SYMBOL;
    }
    uint32_t function_count = 0;
    uint32_t native_count = 0;
    uint32_t binding_count = 0;
    SymbolId entry = 0;
    uint32_t parameters = 0;
    for (uint32_t u = 0; ready && u < count; u += 1)
    {
        const Link* link = files[u].link;
        const InterpreterLinkUnit* unit = &image.units[u];
        for (uint32_t b = 0; b < unit->binding_count; b += 1)
        {
            bindings[binding_count] = unit->bindings[b];
            bindings[binding_count++].symbol += offsets[u];
        }
        for (SymbolId s = 1; ready && s < link->symbols.count; s += 1)
        {
            if (unit->resolved[s] != 0 && unit->addresses[s] == 0 && unit->data[s] == 0 &&
                (link->symbols.flags[s] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK)
            {
                // An unresolved weak address is a valid null value, with no readable allocation or callable target.
                bindings[binding_count++] = (InterpreterBinding){ .symbol = offsets[u] + s };
            }
            const bool function = (link->symbols.flags[s] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_FUNCTION;
            uint32_t selected = UINT32_MAX;
            for (uint32_t f = 0; function && f < body; f += 1)
            {
                selected = unit->addresses[s] == (uintptr_t)&canonical[f] ? f : selected;
            }
            if (selected != UINT32_MAX)
            {
                const uint32_t f = function_count++;
                functions[f] = canonical[selected];
                symbols[f] = offsets[u] + s;
                labels[f] = canonical_labels[selected];
                addresses[f] = (uintptr_t)&canonical[selected];
                symbol_offsets[f] = canonical_offsets[selected];
                const NameId name = link->symbols.name[s];
                if (name != 0 && link->names.length[name] == 4 && memcmp(link->names.bytes[name], "main", 4) == 0 &&
                    (link->symbols.flags[s] & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL)
                {
                    entry = symbols[f];
                    parameters = functions[f].parameter_count;
                }
            }
            else if (function && unit->addresses[s] != 0)
            {
                bool native = false;
                for (uint32_t n = 0; !native && n < library_count; n += 1)
                {
                    if (unit->addresses[s] == (uintptr_t)libraries[n].address)
                    {
                        natives[native_count] = libraries[n];
                        natives[native_count++].symbol = offsets[u] + s;
                        native = true;
                    }
                }
                ready = native;
            }
        }
    }
    char** argv = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, char*, 2) : 0;
    char** envp = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, char*, 1) : 0;
    char* name = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, char, 8) : 0;
    ready = ready && argv != 0 && envp != 0 && name != 0;
    if (ready && entry != 0 && parameters <= 3)
    {
        memcpy(name, "program", 8);
        argv[0] = name;
        argv[1] = 0;
        envp[0] = 0;
        bindings[binding_count++] = (InterpreterBinding){ .symbol = capacity, .bytes = (uint8_t*)argv, .size = 2 * sizeof(char*), .writable = true };
        bindings[binding_count++] = (InterpreterBinding){ .symbol = capacity + 1, .bytes = (uint8_t*)envp, .size = sizeof(char*), .writable = true };
        bindings[binding_count++] = (InterpreterBinding){ .symbol = capacity + 2, .bytes = (uint8_t*)name, .size = 8, .writable = true };
        const InterpreterProgram program = {
            .functions = functions,
            .function_symbols = symbols,
            .function_count = function_count,
            .bindings = bindings,
            .binding_count = binding_count,
            .natives = natives,
            .native_count = native_count,
            .labels = labels,
            .function_addresses = addresses,
            .symbol_offsets = symbol_offsets,
        };
        const InterpreterValue arguments[3] = { { .low = 1 }, { .low = (uintptr_t)argv }, { .low = (uintptr_t)envp } };
        interpreter_typed_run(&program, entry, arguments, parameters, arena, options, &result);
        if (result.status != INTERPRETER_OK)
        {
            const Tier0Function* failed = result.function < function_count ? &functions[result.function] : 0;
            const bool instruction = failed != 0 && result.entry < failed->count;
            fprintf(stderr, "typed source: interpreter status %u at function %u, entry %u (kind %u, class %u), after %" PRIu64 " steps\n", result.status,
                    result.function, result.entry, instruction ? failed->kinds[result.entry] : 0, instruction ? failed->classes[result.entry] : 0, result.steps);
        }
    }
    else if (ready)
    {
        result.status = INTERPRETER_SYMBOL;
    }
    return result;
}

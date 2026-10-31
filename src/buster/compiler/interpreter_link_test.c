// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original hand-built Link fixtures test data materialization independently of the C producer and object loader.

typedef struct InterpreterLinkTest InterpreterLinkTest;
struct InterpreterLinkTest
{
    uint32_t passed;
    uint32_t failed;
};

typedef struct InterpreterLinkTestInput InterpreterLinkTestInput;
struct InterpreterLinkTestInput
{
    Arena tables;
    Arena names;
    Link link;
};

static void interpreter_link_test_expect(InterpreterLinkTest* tally, bool condition, const char* name)
{
    if (condition)
    {
        tally->passed += 1;
    }
    else
    {
        tally->failed += 1;
        fprintf(stderr, "interpreter-link: %s\n", name);
    }
}

static bool interpreter_link_test_input(InterpreterLinkTestInput* input)
{
    input->tables = arena_reserve(64ull << 10);
    input->names = arena_reserve(4ull << 10);
    const LinkCapacity capacity = {
        .names = 16,
        .atoms = 16,
        .symbols = 32,
        .fixups = 32,
        .wide_addends = 8,
        .priorities = 4,
    };
    const bool result = input->tables.base != 0 && input->names.base != 0 && link_initialize(&input->link, &input->tables, &input->names, capacity);
    return result;
}

static void interpreter_link_test_release(InterpreterLinkTestInput* input)
{
    arena_release(&input->tables);
    arena_release(&input->names);
}

static NameId interpreter_link_test_name(Link* link, const char* text)
{
    const NameId result = link_add_name(link, (const uint8_t*)text, (uint32_t)strlen(text));
    return result;
}

static uint64_t interpreter_link_test_read(const uint8_t* bytes, uint32_t count)
{
    uint64_t result = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        result |= (uint64_t)bytes[i] << (i * 8);
    }
    return result;
}

static void interpreter_link_test_data(InterpreterLinkTest* tally)
{
    InterpreterLinkTestInput input = { 0 };
    Arena output_arena = arena_reserve(64ull << 10);
    const bool ready = interpreter_link_test_input(&input) && output_arena.base != 0;
    interpreter_link_test_expect(tally, ready, "fixture arenas");
    if (ready)
    {
        // The output arena has been used before: BSS must not rely on mmap's initial zeros.
        assert(arena_commit(&output_arena, output_arena.capacity));
        memset(output_arena.base, 0xa5, output_arena.capacity);
        uint8_t string_bytes[] = "hello";
        uint8_t data_bytes[64] = { 0 };
        uint8_t code_bytes[] = { 0xc3 };
        Link* link = &input.link;
        const AtomId strings = link_add_atom(link, BUSTER_LINK_CLASS_STRINGS, 0, sizeof(string_bytes), string_bytes, 1);
        const AtomId data = link_add_atom(link, BUSTER_LINK_CLASS_DATA, 3, sizeof(data_bytes), data_bytes, 1);
        const AtomId bss = link_add_atom(link, BUSTER_LINK_CLASS_BSS, 6, 64, 0, 1);
        const AtomId code = link_add_atom(link, BUSTER_LINK_CLASS_TEXT, 0, sizeof(code_bytes), code_bytes, 1);
        const SymbolId text = link_add_symbol(link, 0, strings, 0, sizeof(string_bytes), BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT, BUSTER_LINK_SOURCE_PROGRAM);
        const SymbolId alias = link_add_symbol(link, 0, strings, 2, 3, BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT, BUSTER_LINK_SOURCE_PROGRAM);
        const SymbolId zero = link_add_symbol(link, interpreter_link_test_name(link, "zero"), bss, 0, 64, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT, BUSTER_LINK_SOURCE_PROGRAM);
        const SymbolId pointers = link_add_symbol(link, interpreter_link_test_name(link, "pointers"), data, 0, sizeof(data_bytes), BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT, BUSTER_LINK_SOURCE_PROGRAM);
        const SymbolId absolute = link_add_symbol(link, interpreter_link_test_name(link, "absolute"), 0, 17, 0, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_NOTYPE, BUSTER_LINK_SOURCE_ABSOLUTE);
        const SymbolId negative = link_add_symbol(link, interpreter_link_test_name(link, "negative"), 0, UINT64_MAX, 0, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_NOTYPE, BUSTER_LINK_SOURCE_ABSOLUTE);
        const NameId function_name = interpreter_link_test_name(link, "function");
        const SymbolId function = link_add_symbol(link, function_name, code, 0, sizeof(code_bytes), BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_PROGRAM);
        const SymbolId function_reference = link_add_symbol(link, function_name, 0, 0, 0, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_NOTYPE, BUSTER_LINK_SOURCE_NONE);
        link_add_fixup(link, data, 0, BUSTER_LINK_FIXUP_ABS64, text, 1);
        link_add_fixup(link, data, 8, BUSTER_LINK_FIXUP_ABS64, zero, 8);
        link_add_fixup(link, data, 16, BUSTER_LINK_FIXUP_PC64, text, 0);
        link_add_fixup(link, data, 24, BUSTER_LINK_FIXUP_SIZE64, text, 2);
        link_add_fixup(link, data, 32, BUSTER_LINK_FIXUP_ABS32, absolute, 3);
        link_add_fixup(link, data, 36, BUSTER_LINK_FIXUP_ABS32S, negative, 0);
        link_add_fixup(link, data, 40, BUSTER_LINK_FIXUP_ABS64, function_reference, 0);
        link_add_fixup(link, data, 48, BUSTER_LINK_FIXUP_ABS64, absolute, 1ll << 33);
        link_add_fixup(link, data, 56, BUSTER_LINK_FIXUP_PC32, text, 0);
        link_add_fixup(link, data, 60, BUSTER_LINK_FIXUP_SIZE32, alias, -1);
        link->atoms.address[data] = 0xabcdef;
        link->symbols.reference[text] = 13;
        const InterpreterLinkExternal external = {
            .input = 0,
            .symbol = function,
            .address = 0x12345000,
            .size = sizeof(code_bytes),
        };
        const Link* inputs[] = {
            link,
        };
        InterpreterLink output = { 0 };
        const InterpreterLinkResult materialized = interpreter_link_materialize(&output, inputs, 1, &external, 1, &output_arena);
        interpreter_link_test_expect(tally, link->error == LINK_ERROR_NONE && pointers != 0 && materialized.status == INTERPRETER_LINK_OK, "materialize strings, aliases, globals, code token and BSS");
        if (materialized.status == INTERPRETER_LINK_OK)
        {
            const InterpreterLinkUnit* unit = &output.units[0];
            const uint8_t* bytes = unit->atom_bytes[data];
            interpreter_link_test_expect(tally, output.unit_count == 1 && output.memory_count == 3 && unit->binding_count == 4 && unit->atom_bytes[code] == 0 && unit->data[function_reference] == 0, "data-only memory regions exclude callable tokens and absolute constants");
            interpreter_link_test_expect(tally, memcmp(unit->atom_bytes[strings], string_bytes, sizeof(string_bytes)) == 0 && unit->addresses[alias] == unit->addresses[text] + 2, "literal bytes and symbol aliases");
            interpreter_link_test_expect(tally, unit->writable[text] == 0 && unit->writable[zero] == 1 && unit->writable[pointers] == 1, "atom permissions reach symbol bindings");
            bool cleared = true;
            for (uint32_t i = 0; i < 64; i += 1)
            {
                cleared &= unit->atom_bytes[bss][i] == 0;
            }
            interpreter_link_test_expect(tally, cleared && ((uintptr_t)unit->atom_bytes[bss] & 63) == 0, "reused BSS is cleared and aligned");
            interpreter_link_test_expect(tally, interpreter_link_test_read(bytes, 8) == unit->addresses[text] + 1 && interpreter_link_test_read(bytes + 8, 8) == unit->addresses[zero] + 8, "ABS64 data addresses and addends");
            const int64_t relative = (int64_t)(unit->addresses[text] - (uintptr_t)(bytes + 16));
            interpreter_link_test_expect(tally, interpreter_link_test_read(bytes + 16, 8) == (uint64_t)relative && interpreter_link_test_read(bytes + 24, 8) == sizeof(string_bytes) + 2, "PC64 and SIZE64 arithmetic");
            interpreter_link_test_expect(tally, interpreter_link_test_read(bytes + 32, 4) == 20 && interpreter_link_test_read(bytes + 40, 8) == external.address, "absolute symbol and callable-token relocations");
            interpreter_link_test_expect(tally, interpreter_link_test_read(bytes + 36, 4) == UINT32_MAX && interpreter_link_test_read(bytes + 48, 8) == 17 + (1ull << 33), "signed absolute and wide addend relocations");
            const int32_t relative32 = (int32_t)(unit->addresses[text] - (uintptr_t)(bytes + 56));
            interpreter_link_test_expect(tally, interpreter_link_test_read(bytes + 56, 4) == (uint32_t)relative32 && interpreter_link_test_read(bytes + 60, 4) == 2, "narrow relative and size fixups");
            InterpreterRuntime runtime = {
                .memories = output.memory,
                .memory_count = output.memory_count,
                .memory_capacity = output.memory_count,
                .check_memory = true,
                .check_undefined = true,
            };
            interpreter_link_test_expect(tally, interpreter_memory_access(&runtime, unit->addresses[text], 1, false) == INTERPRETER_OK && interpreter_memory_access(&runtime, unit->addresses[text], 1, true) == INTERPRETER_MEMORY, "readonly materialized memory rejects writes");
            interpreter_link_test_expect(tally, interpreter_memory_access(&runtime, unit->addresses[zero] + 63, 2, false) == INTERPRETER_MEMORY, "atom boundary remains bounded");
        }
        uint8_t unchanged[sizeof(data_bytes)] = { 0 };
        interpreter_link_test_expect(tally, memcmp(data_bytes, unchanged, sizeof(data_bytes)) == 0 && link->atoms.address[data] == 0xabcdef && link->symbols.reference[text] == 13 && link->error == LINK_ERROR_NONE, "materialization preserves all input columns and bytes");
    }
    interpreter_link_test_release(&input);
    arena_release(&output_arena);
}

static void interpreter_link_test_names(InterpreterLinkTest* tally)
{
    InterpreterLinkTestInput first = { 0 };
    InterpreterLinkTestInput second = { 0 };
    Arena arena = arena_reserve(64ull << 10);
    const bool ready = interpreter_link_test_input(&first) && interpreter_link_test_input(&second) && arena.base != 0;
    interpreter_link_test_expect(tally, ready, "multi-input fixture arenas");
    if (ready)
    {
        uint8_t first_bytes[24] = { 7 };
        uint8_t second_bytes[8] = { 9 };
        const AtomId first_atom = link_add_atom(&first.link, BUSTER_LINK_CLASS_DATA, 3, sizeof(first_bytes), first_bytes, 1);
        const AtomId second_atom = link_add_atom(&second.link, BUSTER_LINK_CLASS_DATA, 3, sizeof(second_bytes), second_bytes, 1);
        const SymbolId weak = link_add_symbol(&first.link, interpreter_link_test_name(&first.link, "shared"), first_atom, 0, 8, BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_OBJECT, BUSTER_LINK_SOURCE_PROGRAM);
        const SymbolId first_local = link_add_symbol(&first.link, interpreter_link_test_name(&first.link, "local"), first_atom, 0, 8, BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT, BUSTER_LINK_SOURCE_PROGRAM);
        const SymbolId absent = link_add_symbol(&first.link, interpreter_link_test_name(&first.link, "absent"), 0, 0, 0, BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_NOTYPE, BUSTER_LINK_SOURCE_NONE);
        // Different intern ordering deliberately gives the same spellings unrelated ids.
        const SymbolId second_local = link_add_symbol(&second.link, interpreter_link_test_name(&second.link, "local"), second_atom, 0, 8, BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT, BUSTER_LINK_SOURCE_PROGRAM);
        const SymbolId strong = link_add_symbol(&second.link, interpreter_link_test_name(&second.link, "shared"), second_atom, 0, 8, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT, BUSTER_LINK_SOURCE_PROGRAM);
        link_add_fixup(&first.link, first_atom, 8, BUSTER_LINK_FIXUP_ABS64, weak, 0);
        link_add_fixup(&first.link, first_atom, 16, BUSTER_LINK_FIXUP_ABS64, absent, 0);
        const Link* inputs[] = {
            &first.link,
            &second.link,
        };
        InterpreterLink image = { 0 };
        InterpreterLinkResult result = interpreter_link_materialize(&image, inputs, 2, 0, 0, &arena);
        interpreter_link_test_expect(tally, result.status == INTERPRETER_LINK_OK, "cross-input symbol resolution");
        if (result.status == INTERPRETER_LINK_OK)
        {
            interpreter_link_test_expect(tally, image.units[0].addresses[weak] == image.units[1].addresses[strong] && *(uint8_t*)image.units[0].addresses[weak] == 9, "strong definition overrides weak by spelling");
            interpreter_link_test_expect(tally, image.units[0].addresses[first_local] != image.units[1].addresses[second_local], "same-spelling locals remain isolated");
            interpreter_link_test_expect(tally, interpreter_link_test_read(image.units[0].atom_bytes[first_atom] + 8, 8) == image.units[1].addresses[strong] && interpreter_link_test_read(image.units[0].atom_bytes[first_atom] + 16, 8) == 0, "canonical addresses and undefined weak zero reach fixups");
        }
        const InterpreterLink previous = image;
        const uint64_t before = arena.length;
        first.link.symbols.flags[weak] = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT;
        result = interpreter_link_materialize(&image, inputs, 2, 0, 0, &arena);
        interpreter_link_test_expect(tally, result.status == INTERPRETER_LINK_DUPLICATE && result.input == 1 && result.symbol == strong && arena.length == before && memcmp(&image, &previous, sizeof(image)) == 0, "duplicate strong definitions roll back output and arena");
        first.link.symbols.flags[weak] = BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_OBJECT;
        second.link.symbols.flags[strong] = BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_OBJECT;
        result = interpreter_link_materialize(&image, inputs, 2, 0, 0, &arena);
        interpreter_link_test_expect(tally, result.status == INTERPRETER_LINK_OK && image.units[0].addresses[weak] == image.units[1].addresses[strong] && *(uint8_t*)image.units[1].addresses[strong] == 7, "first weak definition wins deterministically");
    }
    interpreter_link_test_release(&first);
    interpreter_link_test_release(&second);
    arena_release(&arena);
}

static void interpreter_link_test_failures(InterpreterLinkTest* tally)
{
    InterpreterLinkTestInput input = { 0 };
    Arena arena = arena_reserve(64ull << 10);
    const bool ready = interpreter_link_test_input(&input) && arena.base != 0;
    interpreter_link_test_expect(tally, ready, "failure fixture arenas");
    if (ready)
    {
        uint8_t bytes[16] = { 0 };
        Link* link = &input.link;
        const AtomId atom = link_add_atom(link, BUSTER_LINK_CLASS_DATA, 3, sizeof(bytes), bytes, 1);
        const SymbolId target = link_add_symbol(link, interpreter_link_test_name(link, "target"), 0, 0, 0, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT, BUSTER_LINK_SOURCE_NONE);
        const FixupId fixup = link_add_fixup(link, atom, 0, BUSTER_LINK_FIXUP_ABS64, target, 0);
        const Link* inputs[] = {
            link,
        };
        InterpreterLink image = {
            .unit_count = 123,
            .memory_count = 456,
        };
        const InterpreterLink original = image;
        InterpreterLinkResult result = interpreter_link_materialize(&image, inputs, 1, 0, 0, &arena);
        interpreter_link_test_expect(tally, result.status == INTERPRETER_LINK_UNDEFINED && result.symbol == target && arena.length == 0 && memcmp(&image, &original, sizeof(image)) == 0, "unresolved global is diagnosed transactionally");
        uint8_t external_bytes[8] = { 41 };
        const InterpreterLinkExternal external = {
            .input = 0,
            .symbol = target,
            .address = (uintptr_t)external_bytes,
            .size = sizeof(external_bytes),
            .writable = true,
        };
        result = interpreter_link_materialize(&image, inputs, 1, &external, 1, &arena);
        interpreter_link_test_expect(tally, result.status == INTERPRETER_LINK_OK && image.memory_count == 2 && interpreter_link_test_read(image.units[0].atom_bytes[atom], 8) == (uintptr_t)external_bytes, "explicit native data binding participates in relocation and memory");
        arena_pop_to(&arena, 0);
        image = original;
        link->symbols.source[target] = BUSTER_LINK_SOURCE_ABSOLUTE;
        link->symbols.value[target] = UINT64_MAX;
        link->fixups.kind[fixup] = BUSTER_LINK_FIXUP_ABS32;
        result = interpreter_link_materialize(&image, inputs, 1, 0, 0, &arena);
        interpreter_link_test_expect(tally, result.status == INTERPRETER_LINK_RANGE && result.atom == atom && result.fixup == fixup && arena.length == 0 && memcmp(&image, &original, sizeof(image)) == 0, "overflow rolls back after private byte allocation");
        link->fixups.kind[fixup] = BUSTER_LINK_FIXUP_CALL;
        result = interpreter_link_materialize(&image, inputs, 1, 0, 0, &arena);
        interpreter_link_test_expect(tally, result.status == INTERPRETER_LINK_UNSUPPORTED && result.fixup == fixup && arena.length == 0, "code relocation kind is unsupported in data");
        link->fixups.kind[fixup] = BUSTER_LINK_FIXUP_ABS64;
        link->atoms.class[atom] = BUSTER_LINK_CLASS_TEXT;
        result = interpreter_link_materialize(&image, inputs, 1, 0, 0, &arena);
        interpreter_link_test_expect(tally, result.status == INTERPRETER_LINK_UNSUPPORTED && result.atom == atom && result.fixup == fixup, "native code fixups are rejected precisely");
        link->atoms.class[atom] = BUSTER_LINK_CLASS_TDATA;
        result = interpreter_link_materialize(&image, inputs, 1, 0, 0, &arena);
        interpreter_link_test_expect(tally, result.status == INTERPRETER_LINK_UNSUPPORTED && result.atom == atom, "TLS atoms require a separate runtime model");
        link->atoms.class[atom] = BUSTER_LINK_CLASS_DATA;
        link->fixups.target[fixup] = link->symbols.count;
        result = interpreter_link_materialize(&image, inputs, 1, 0, 0, &arena);
        interpreter_link_test_expect(tally, result.status == INTERPRETER_LINK_INVALID && arena.length == 0, "malformed target id fails before indexing");
        link->fixups.target[fixup] = target;
        const uint32_t original_wide_count = link->fixups.wide_count;
        link->fixups.wide_count = link->fixups.wide_capacity + 1;
        result = interpreter_link_materialize(&image, inputs, 1, 0, 0, &arena);
        interpreter_link_test_expect(tally, result.status == INTERPRETER_LINK_INVALID && arena.length == 0, "malformed wide count fails before addend indexing");
        link->fixups.wide_count = original_wide_count;
        link->fixups.offset[fixup] = UINT32_MAX;
        result = interpreter_link_materialize(&image, inputs, 1, 0, 0, &arena);
        interpreter_link_test_expect(tally, result.status == INTERPRETER_LINK_INVALID && arena.length == 0, "maximum raw offset cannot escape atom bounds");
        link->fixups.offset[fixup] = 0;
        link->symbols.value[target] = 7;
        Arena tiny = {
            .base = arena.base,
            .length = 8,
            .capacity = 32,
        };
        result = interpreter_link_materialize(&image, inputs, 1, 0, 0, &tiny);
        interpreter_link_test_expect(tally, result.status == INTERPRETER_LINK_RESOURCE && tiny.length == 8 && memcmp(&image, &original, sizeof(image)) == 0, "allocation exhaustion preserves prior arena prefix");
        result = interpreter_link_materialize(&image, 0, 0, 0, 0, &arena);
        interpreter_link_test_expect(tally, result.status == INTERPRETER_LINK_OK && image.unit_count == 0 && image.memory_count == 0 && arena.length == 0, "empty batch needs no input arrays or allocations");
    }
    interpreter_link_test_release(&input);
    arena_release(&arena);
}

static int run_interpreter_link_tests(int argc, char** argv)
{
    (void)argv;
    int result = 1;
    if (argc != 0)
    {
        fprintf(stderr, "usage: compiler test-interpreter-link\n");
    }
    else
    {
        InterpreterLinkTest tally = { 0 };
        interpreter_link_test_data(&tally);
        interpreter_link_test_names(&tally);
        interpreter_link_test_failures(&tally);
        printf("interpreter-link: %u passed, %u failed\n", tally.passed, tally.failed);
        result = tally.failed != 0;
    }
    return result;
}

static void interpreter_link_test_stress(InterpreterLinkTest* tally, uint32_t count)
{
    InterpreterLinkTestInput units[128] = { 0 };
    const Link* links[128] = { 0 };
    InterpreterLinkExternal externals[128] = { 0 };
    uint64_t data[128][2] = { 0 };
    Arena arena = arena_reserve(4ull << 20);
    bool ready = arena.base != 0;
    for (uint32_t u = 0; ready && u < count; u += 1)
    {
        ready = interpreter_link_test_input(&units[u]);
        if (ready)
        {
            Link* link = &units[u].link;
            links[u] = link;
            char own[32], next[32], fn[32];
            snprintf(own, sizeof(own), "data_%u", u);
            snprintf(next, sizeof(next), "data_%u", (u + 1) % count);
            snprintf(fn, sizeof(fn), "fn_%u", u);
            data[u][0] = 100 + u;
            const AtomId atom = link_add_atom(link, BUSTER_LINK_CLASS_DATA, 3, sizeof(data[u]), (const uint8_t*)data[u], 1);
            link_add_symbol(link, interpreter_link_test_name(link, own), atom, 0, 8, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT, BUSTER_LINK_SOURCE_PROGRAM);
            const SymbolId ref = link_add_symbol(link, interpreter_link_test_name(link, next), 0, 0, 0, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT, BUSTER_LINK_SOURCE_NONE);
            const SymbolId function = link_add_symbol(link, interpreter_link_test_name(link, fn), 0, 0, 0, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_NONE);
            link_add_symbol(link, interpreter_link_test_name(link, "local"), atom, 0, 8, BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT, BUSTER_LINK_SOURCE_PROGRAM);
            link_add_fixup(link, atom, 8, BUSTER_LINK_FIXUP_ABS64, ref, 0);
            externals[count - 1 - u] = (InterpreterLinkExternal){ .input = u, .symbol = function, .address = 0x10000u + u };
            ready = link->error == LINK_ERROR_NONE;
        }
    }
    InterpreterLink image = { 0 };
    if (ready)
    {
        ready = interpreter_link_materialize(&image, links, count, externals, count, &arena).status == INTERPRETER_LINK_OK;
    }
    for (uint32_t u = 0; ready && u < count; u += 1)
    {
        const InterpreterLinkUnit* unit = &image.units[u];
        const uint32_t next = (u + 1) % count;
        ready = unit->addresses[2] == image.units[next].addresses[1] && unit->addresses[3] == 0x10000u + u &&
                unit->addresses[4] == unit->addresses[1] && unit->sizes[2] == 8 && unit->binding_count == 3 &&
                interpreter_link_test_read(unit->atom_bytes[1] + 8, 8) == unit->addresses[2] &&
                interpreter_link_test_read((const uint8_t*)unit->addresses[2], 8) == 100 + next;
    }
    interpreter_link_test_expect(tally, ready, "cross-unit map, callable token, per-unit local and relocated data ring");
    for (uint32_t u = 0; u < count; u += 1)
    {
        interpreter_link_test_release(&units[u]);
    }
    arena_release(&arena);
}

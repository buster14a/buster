static void probe_address(const InterpreterLink* image, const Link* const* links, const InterpreterLinkExternal* externals, uint32_t external_count, uintptr_t address)
{
    if (address != 0)
    {
        for (uint32_t e = 0; e < external_count; e += 1)
        {
            if (address == externals[e].address)
            {
                fprintf(stderr, "external/%u/%u", externals[e].input, externals[e].symbol);
                return;
            }
        }
        for (uint32_t u = 0; u < image->unit_count; u += 1)
        {
            for (AtomId a = 1; a < links[u]->atoms.count; a += 1)
            {
                const uintptr_t start = (uintptr_t)image->units[u].atom_bytes[a];
                if (start != 0 && address >= start && address - start <= links[u]->atoms.size[a])
                {
                    fprintf(stderr, "atom/%u/%u/%llu", u, a, (unsigned long long)(address - start));
                    return;
                }
            }
        }
    }
    fprintf(stderr, "value/%llu", (unsigned long long)address);
}

static void probe_map(const InterpreterLink* image, const Link* const* links, const InterpreterLinkExternal* externals, uint32_t external_count)
{
    for (uint32_t u = 0; u < image->unit_count; u += 1)
    {
        const InterpreterLinkUnit* unit = &image->units[u];
        const Link* link = links[u];
        for (SymbolId s = 1; s < unit->symbol_count; s += 1)
        {
            fprintf(stderr, "MAP %u %u size=%llu flags=%u/%u/%u addr=", u, s, (unsigned long long)unit->sizes[s], unit->resolved[s], unit->writable[s], unit->data[s]);
            probe_address(image, links, externals, external_count, unit->addresses[s]);
            fputc('\n', stderr);
        }
        for (AtomId a = 1; a < link->atoms.count; a += 1)
        {
            for (uint32_t i = 0; i < link->atoms.fixup_count[a]; i += 1)
            {
                const FixupId fixup = link->atoms.fixup_begin[a] + i;
                const uint8_t* field = unit->atom_bytes[a] + link->fixups.offset[fixup];
                const SymbolId target = link->fixups.target[fixup];
                const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
                const uint8_t width = link_fixup_kinds[kind].width;
                uint64_t raw = 0;
                memcpy(&raw, field, width);
                // Recover the relocated target from the bytes actually written, modulo the field width.
                uint64_t recovered = raw - (uint64_t)link_fixup_addend(link, fixup);
                if (link_fixup_kinds[kind].pc_relative)
                {
                    recovered += (uintptr_t)field;
                }
                const bool size = kind == BUSTER_LINK_FIXUP_SIZE32 || kind == BUSTER_LINK_FIXUP_SIZE64;
                const uint64_t expected = size ? unit->sizes[target] : unit->addresses[target];
                const uint64_t mask = width == 8 ? UINT64_MAX : UINT32_MAX;
                if (((recovered ^ expected) & mask) != 0)
                {
                    abort();
                }
                fprintf(stderr, "FIX %u %u %u offset=%u kind=%u target=%u addend=%lld width=%u\n", u, a, fixup, link->fixups.offset[fixup], kind, target, (long long)link_fixup_addend(link, fixup), width);
            }
        }
    }
}

// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Test-only permutation of opaque NameIds. Atom, symbol and fixup arrays remain in canonical input order: their
// indices are the link representation's placed ranges, not intern IDs. Remap every name reference with its spelling.

typedef struct LinkOrderName LinkOrderName;
struct LinkOrderName
{
    const uint8_t* bytes;
    uint64_t hash;
    uint32_t length;
};

static bool link_test_permute_names(Link* link, uint64_t seed, Arena* arena)
{
    const uint64_t before = arena->length;
    const uint32_t count = link->names.count;
    LinkOrderName* old = BUSTER_ARENA_PUSH_ARRAY(arena, LinkOrderName, count);
    uint32_t* map = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count);
    bool result = old != 0 && map != 0;
    bool changed = false;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        old[i] = (LinkOrderName)
        {
            .bytes = link->names.bytes[i],
            .hash = link->names.hash[i],
            .length = link->names.length[i],
        };
        const uint64_t rotation = count > 3 ? seed % (count - 1) : 0;
        map[i] = i == 0 ? 0 : 1 + (uint32_t)((count - 1ull - i + rotation) % (count - 1));
        changed |= map[i] != i;
    }
    result = result && (count <= 2 || changed);
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        link->names.bytes[map[i]] = old[i].bytes;
        link->names.hash[map[i]] = old[i].hash;
        link->names.length[map[i]] = old[i].length;
    }
    for (uint32_t i = 0; result && i < link->symbols.count; i += 1)
    {
        link->symbols.name[i] = map[link->symbols.name[i]];
    }
    arena_pop_to(arena, before);
    return result;
}

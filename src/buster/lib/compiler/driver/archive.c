// Indexed static archive extraction, included by driver.c. The name table
// survives consecutive archives; provider lists and the ordered worklist live
// only for one archive. compiler_driver_archive_extract appends the same member
// sequence as forward fixed-point scans, including weak and duplicate symbols.
#include <buster/lib/compiler/driver/archive_internal.h>
#include <buster/lib/hash.h>
#include <buster/lib/string.h>

enum
{
    COMPILER_DRIVER_ARCHIVE_PRESENT = 1,
    COMPILER_DRIVER_ARCHIVE_DEFINED = 2,
    COMPILER_DRIVER_ARCHIVE_STRONG = 4,
    COMPILER_DRIVER_ARCHIVE_WEAK = 8,
    COMPILER_DRIVER_ARCHIVE_INITIAL_CAPACITY = 32,
    COMPILER_DRIVER_ARCHIVE_SMALL_OBJECTS = 8,
    COMPILER_DRIVER_ARCHIVE_SMALL_SYMBOLS = 32,
};

BUSTER_GLOBAL_LOCAL bool compiler_driver_archive_member_needed(ObjectFile* member, ObjectFile* selected, u32 selected_count)
{
    bool result = false;
    // ELF weak references may bind to an already selected definition, but
    // do not request archive extraction themselves. Keep other formats'
    // existing selection policy separate from that ELF binding rule.
    bool weak_extracts = object_format_for_target(member->target) != OBJECT_FORMAT_ELF64;
    for (u32 member_symbol_index = 0; member_symbol_index < member->symbol_count && !result; member_symbol_index += 1)
    {
        ObjectSymbol* member_symbol = &member->symbols[member_symbol_index];
        if (!member_symbol->global || member_symbol->section == OBJECT_SECTION_UNDEFINED)
        {
            continue;
        }
        bool unresolved = false;
        bool defined = false;
        for (u32 object_index = 0; object_index < selected_count; object_index += 1)
        {
            ObjectFile* object = &selected[object_index];
            for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
            {
                ObjectSymbol* symbol = &object->symbols[symbol_index];
                if (!symbol->global || !string_equal(symbol->name, member_symbol->name))
                {
                    continue;
                }
                if (symbol->section == OBJECT_SECTION_UNDEFINED)
                {
                    unresolved = unresolved || !symbol->weak || weak_extracts;
                }
                else
                {
                    defined = true;
                }
            }
        }
        result = unresolved && !defined;
    }
    return result;
}

typedef struct CompilerDriverArchiveProvider CompilerDriverArchiveProvider;
struct CompilerDriverArchiveProvider
{
    u64 next;
    u32 member;
    bool weak_extracts;
};

typedef struct CompilerDriverArchiveMember CompilerDriverArchiveMember;
struct CompilerDriverArchiveMember
{
    u64 needed;
    bool queued;
    bool selected;
};

typedef struct CompilerDriverArchiveWork CompilerDriverArchiveWork;
struct CompilerDriverArchiveWork
{
    CompilerDriverArchiveProvider* providers;
    CompilerDriverArchiveMember* members;
    u64* heap;
    u32 count;
    u64 cursor;
};

BUSTER_GLOBAL_LOCAL CompilerDriverArchiveSymbol* compiler_driver_archive_slot(CompilerDriverArchiveState* state, String8 name)
{
    u64 slot = buster_hash_64((u8*)name.pointer, name.length) & (state->capacity - 1);
    while (state->symbols[slot].flags && !string_equal(state->symbols[slot].name, name))
    {
        slot = (slot + 1) & (state->capacity - 1);
    }
    return &state->symbols[slot];
}

BUSTER_GLOBAL_LOCAL CompilerDriverArchiveSymbol* compiler_driver_archive_symbol(CompilerDriverArchiveState* state, String8 name)
{
    CompilerDriverArchiveSymbol* result = state->capacity ? compiler_driver_archive_slot(state, name) : 0;
    if (!result || (!result->flags && state->count >= state->capacity / 2))
    {
        u64 old_capacity = state->capacity;
        CompilerDriverArchiveSymbol* old_symbols = state->symbols;
        state->capacity = old_capacity ? old_capacity * 2 : COMPILER_DRIVER_ARCHIVE_INITIAL_CAPACITY;
        state->symbols = arena_allocate_zeroed(state->arena, CompilerDriverArchiveSymbol, state->capacity);
        for (u64 index = 0; index < old_capacity; index += 1)
        {
            if (old_symbols[index].flags)
            {
                *compiler_driver_archive_slot(state, old_symbols[index].name) = old_symbols[index];
            }
        }
        result = compiler_driver_archive_slot(state, name);
    }
    if (!result->flags)
    {
        result->name = name;
        result->flags = COMPILER_DRIVER_ARCHIVE_PRESENT;
        state->count += 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_archive_unresolved(u32 flags, bool weak_extracts)
{
    return !(flags & COMPILER_DRIVER_ARCHIVE_DEFINED) &&
           ((flags & COMPILER_DRIVER_ARCHIVE_STRONG) || (weak_extracts && (flags & COMPILER_DRIVER_ARCHIVE_WEAK)));
}

BUSTER_GLOBAL_LOCAL void compiler_driver_archive_enqueue(CompilerDriverArchiveWork* work, u32 member)
{
    CompilerDriverArchiveMember* entry = &work->members[member];
    if (entry->needed && !entry->queued && !entry->selected)
    {
        // A newly needed member behind the scan cursor belongs to the next
        // pass. A plain FIFO or minimum-member queue changes duplicate binding.
        u64 key = member;
        if (work->cursor != UINT64_MAX)
        {
            key |= work->cursor & ~(u64)UINT32_MAX;
            if (member <= (u32)work->cursor) key += (u64)1 << 32;
        }
        u32 slot = work->count++;
        while (slot && work->heap[(slot - 1) / 2] > key)
        {
            work->heap[slot] = work->heap[(slot - 1) / 2];
            slot = (slot - 1) / 2;
        }
        work->heap[slot] = key;
        entry->queued = true;
    }
}

BUSTER_GLOBAL_LOCAL u32 compiler_driver_archive_pop(CompilerDriverArchiveWork* work)
{
    u64 key = work->heap[0];
    u64 last = work->heap[--work->count];
    u64 slot = 0;
    while (slot * 2 + 1 < work->count)
    {
        u64 child = slot * 2 + 1;
        if (child + 1 < work->count && work->heap[child + 1] < work->heap[child]) child += 1;
        if (last <= work->heap[child]) break;
        work->heap[slot] = work->heap[child];
        slot = child;
    }
    if (work->count) work->heap[slot] = last;
    work->cursor = key;
    u32 member = (u32)key;
    work->members[member].queued = false;
    return member;
}

BUSTER_GLOBAL_LOCAL void compiler_driver_archive_add_object(CompilerDriverArchiveState* state, CompilerDriverArchiveWork* work, ObjectFile* object)
{
    for (u32 index = 0; index < object->symbol_count; index += 1)
    {
        ObjectSymbol* symbol = &object->symbols[index];
        if (!symbol->global) continue;
        CompilerDriverArchiveSymbol* entry = compiler_driver_archive_symbol(state, symbol->name);
        u32 before = entry->flags;
        entry->flags |= symbol->section != OBJECT_SECTION_UNDEFINED ? COMPILER_DRIVER_ARCHIVE_DEFINED :
                        symbol->weak ? COMPILER_DRIVER_ARCHIVE_WEAK : COMPILER_DRIVER_ARCHIVE_STRONG;
        if (work && before != entry->flags)
        {
            // Each state bit is monotonic. Every provider edge is visited at
            // most three times, regardless of chain depth or symbol repeats.
            for (u64 provider = entry->providers; provider; provider = work->providers[provider].next)
            {
                CompilerDriverArchiveProvider* edge = &work->providers[provider];
                bool was_needed = compiler_driver_archive_unresolved(before, edge->weak_extracts);
                bool needed = compiler_driver_archive_unresolved(entry->flags, edge->weak_extracts);
                if (was_needed != needed)
                {
                    CompilerDriverArchiveMember* member = &work->members[edge->member];
                    if (needed) member->needed += 1;
                    else member->needed -= 1;
                    compiler_driver_archive_enqueue(work, edge->member);
                }
            }
        }
    }
}

// An archive without undefined global symbols cannot create more extraction
// requests. Probe selected state in one forward pass without indexing or
// retaining names from irrelevant members.
BUSTER_GLOBAL_LOCAL void compiler_driver_archive_extract_leaves(CompilerDriverArchiveState* state, ObjectArchive* archive,
                                                               ObjectFile* objects, u32* object_count)
{
    for (u32 member = 0; member < archive->object_count; member += 1)
    {
        ObjectFile* object = &archive->objects[member];
        bool weak_extracts = object_format_for_target(object->target) != OBJECT_FORMAT_ELF64;
        bool needed = false;
        for (u32 index = 0; index < object->symbol_count && !needed && state->capacity; index += 1)
        {
            ObjectSymbol* symbol = &object->symbols[index];
            if (symbol->global && symbol->section != OBJECT_SECTION_UNDEFINED)
            {
                needed = compiler_driver_archive_unresolved(compiler_driver_archive_slot(state, symbol->name)->flags, weak_extracts);
            }
        }
        if (needed)
        {
            objects[(*object_count)++] = *object;
            compiler_driver_archive_add_object(state, 0, object);
        }
    }
}

// Avoid an arena/table startup floor for tiny ordinary archives. The full
// selected+archive population is bounded, including all possible later pulls:
// at most eight productive passes over eight members and 32 symbols total. Large
// selected inputs always use the index, even for a one-member archive.
BUSTER_GLOBAL_LOCAL bool compiler_driver_archive_small(ObjectArchive* archive, ObjectFile* objects, u32 object_count)
{
    bool result = archive->object_count <= COMPILER_DRIVER_ARCHIVE_SMALL_OBJECTS && object_count <= COMPILER_DRIVER_ARCHIVE_SMALL_OBJECTS;
    if (result)
    {
        u64 symbols = 0;
        for (u32 index = 0; index < object_count; index += 1) symbols += objects[index].symbol_count;
        for (u32 index = 0; index < archive->object_count; index += 1) symbols += archive->objects[index].symbol_count;
        result = symbols <= COMPILER_DRIVER_ARCHIVE_SMALL_SYMBOLS;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void compiler_driver_archive_extract_small(ObjectArchive* archive, ObjectFile* objects, u32* object_count)
{
    bool selected[COMPILER_DRIVER_ARCHIVE_SMALL_OBJECTS] = {0};
    bool added;
    do
    {
        added = false;
        for (u32 member = 0; member < archive->object_count; member += 1)
        {
            if (!selected[member] && compiler_driver_archive_member_needed(&archive->objects[member], objects, *object_count))
            {
                selected[member] = true;
                objects[(*object_count)++] = archive->objects[member];
                added = true;
            }
        }
    } while (added);
}

void compiler_driver_archive_extract(Arena* arena, CompilerDriverArchiveState* state, ObjectArchive* archive,
                                    ObjectFile* objects, u32* object_count)
{
    if (archive->object_count && *object_count && !state->arena && compiler_driver_archive_small(archive, objects, *object_count))
    {
        compiler_driver_archive_extract_small(archive, objects, object_count);
    }
    else if (archive->object_count && *object_count)
    {
        if (!state->arena) state->arena = arena_create((ArenaCreation){.flags = {.no_pool = true}});
        while (state->processed_objects < *object_count)
        {
            compiler_driver_archive_add_object(state, 0, &objects[state->processed_objects++]);
        }
        TemporalArena scratch = scratch_begin(&arena, 1);
        u64 definition_count = 0;
        bool has_references = false;
        for (u32 member = 0; member < archive->object_count; member += 1)
        {
            ObjectFile* object = &archive->objects[member];
            for (u32 index = 0; index < object->symbol_count; index += 1)
            {
                ObjectSymbol* symbol = &object->symbols[index];
                definition_count += symbol->global && symbol->section != OBJECT_SECTION_UNDEFINED;
                has_references |= symbol->global && symbol->section == OBJECT_SECTION_UNDEFINED;
            }
        }
        if (!has_references)
        {
            compiler_driver_archive_extract_leaves(state, archive, objects, object_count);
        }
        else
        {
            CompilerDriverArchiveWork work = {
                .providers = arena_allocate(scratch.arena, CompilerDriverArchiveProvider, definition_count + 1),
                .members = arena_allocate_zeroed(scratch.arena, CompilerDriverArchiveMember, archive->object_count),
                .heap = arena_allocate(scratch.arena, u64, archive->object_count),
                .cursor = UINT64_MAX,
            };
            u64 provider_count = 0;
            for (u32 member = 0; member < archive->object_count; member += 1)
            {
                ObjectFile* object = &archive->objects[member];
                bool weak_extracts = object_format_for_target(object->target) != OBJECT_FORMAT_ELF64;
                for (u32 index = 0; index < object->symbol_count; index += 1)
                {
                    ObjectSymbol* symbol = &object->symbols[index];
                    if (!symbol->global || symbol->section == OBJECT_SECTION_UNDEFINED) continue;
                    CompilerDriverArchiveSymbol* entry = compiler_driver_archive_symbol(state, symbol->name);
                    u64 provider = ++provider_count;
                    work.providers[provider] = (CompilerDriverArchiveProvider){entry->providers, member, weak_extracts};
                    entry->providers = provider;
                    work.members[member].needed += compiler_driver_archive_unresolved(entry->flags, weak_extracts);
                }
                compiler_driver_archive_enqueue(&work, member);
            }
            while (work.count)
            {
                u32 member = compiler_driver_archive_pop(&work);
                if (work.members[member].needed)
                {
                    work.members[member].selected = true;
                    objects[(*object_count)++] = archive->objects[member];
                    compiler_driver_archive_add_object(state, &work, &archive->objects[member]);
                }
            }
            // Clear only this archive's provider heads: scanning the whole retained
            // name table here would penalize a command with many small archives.
            for (u32 member = 0; member < archive->object_count; member += 1)
            {
                ObjectFile* object = &archive->objects[member];
                for (u32 index = 0; index < object->symbol_count; index += 1)
                {
                    ObjectSymbol* symbol = &object->symbols[index];
                    if (symbol->global && symbol->section != OBJECT_SECTION_UNDEFINED)
                    {
                        compiler_driver_archive_slot(state, symbol->name)->providers = 0;
                    }
                }
            }
        }
        state->processed_objects = *object_count;
        scratch_end(scratch);
    }
}

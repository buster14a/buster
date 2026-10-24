// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Whole foreign-input pipeline with optional section GC. Include after foreign_test.c. The fixture and timing
// pattern adapt this project's original foreign_unwind_dedup_benchmark.c; no pooling/string benchmark is required.
// Original ET_REL files are generated once, outside timing. Every sample reserves fresh link/name arenas, resolves
// those files, converts, optionally collects, writes/closes, then releases every per-link arena and mapping BEFORE
// stopping the total clock. Commands, generated inputs and thread-local scratch caches persist. This is warm-file
// fresh-link timing, not cold I/O, whole compilation, or a real-corpus result. One lane; pin externally under the lock.
//
// Each leaf object has one named function and its own CIE/FDE. The entry calls alternating leaves in the mixed-dead
// case and every leaf in the all-live control. An untimed replay checks original ELF identities, stable live/dead
// maps, unwind offsets and serialized FDE addresses/ranges. Every measured output executes and matches complete
// reference bytes outside timing; all-live references are shared across GC on/off as well as exact/vector paths.
//
// Layout: fixture/conditioning; independent maps and emitted-frame oracle; fresh sample; rotated cases; CLI.
// Retained-arena bytes are lengths before release, excluding mapped files, generated inputs and transient scratch,
// not peak RSS or reserved capacity. Phase medians need not sum to the total median. Sample spread is within one
// process; acceptance still needs independently started, controlled repetitions. TSC ticks are reference ticks.
// Printed atom/symbol/fixup counts exclude their null ID; the internal table counts used by the oracle include it.

#define BUSTER_FOREIGN_GC_BENCHMARK_MAX_LEAVES 16384
#define BUSTER_FOREIGN_GC_BENCHMARK_CIE_BYTES 32
#define BUSTER_FOREIGN_GC_BENCHMARK_FRAME_BYTES 56
#define BUSTER_FOREIGN_GC_BENCHMARK_PHASES 6

typedef struct ForeignGcBenchmarkVariant ForeignGcBenchmarkVariant;
struct ForeignGcBenchmarkVariant
{
    const char* name;
    ForeignPath path;
    bool collect;
};

static const ForeignGcBenchmarkVariant foreign_gc_benchmark_variants[] = {
    {
        .name = "no-gc-exact",
        .path = FOREIGN_PATH_EXACT,
    },
    {
        .name = "gc-exact",
        .path = FOREIGN_PATH_EXACT,
        .collect = true,
    },
    {
        .name = "no-gc-vector",
        .path = FOREIGN_PATH_VECTOR,
    },
    {
        .name = "gc-vector",
        .path = FOREIGN_PATH_VECTOR,
        .collect = true,
    },
};

typedef struct ForeignGcBenchmarkInput ForeignGcBenchmarkInput;
struct ForeignGcBenchmarkInput
{
    ForeignCommand command;
    uint64_t expected;
    uint64_t input_bytes;
    uint64_t logical_bytes;
    uint32_t count;
    uint32_t live;
    uint32_t entry_length;
    bool all_live;
};

typedef struct ForeignGcBenchmarkSample ForeignGcBenchmarkSample;
struct ForeignGcBenchmarkSample
{
    uint64_t times[BUSTER_FOREIGN_GC_BENCHMARK_PHASES];
    uint64_t ticks;
    uint64_t retained;
    uint64_t output_bytes;
    uint32_t atoms;
    uint32_t symbols;
    uint32_t fixups;
    uint32_t resolve_error;
    ForeignLinkResult built;
    ForeignLinkResult collected;
    ExecutableResult written;
    bool valid;
};

static volatile uint64_t foreign_gc_benchmark_scalar_sink;

static __attribute__((noinline)) void foreign_gc_benchmark_after_scalar(uint32_t milliseconds)
{
    if (milliseconds != 0)
    {
        const uint64_t start = foreign_now();
        const uint64_t duration = (uint64_t)milliseconds * 1000000;
        uint64_t value = 0x9e3779b97f4a7c15ull;
        do
        {
            #pragma clang loop vectorize(disable) interleave(disable)
            for (uint32_t i = 0; i < 4096; i += 1)
            {
                value ^= value >> 12;
                value ^= value << 25;
                value ^= value >> 27;
                value *= 0x2545f4914f6cdd1dull;
            }
        } while (foreign_now() - start < duration);
        foreign_gc_benchmark_scalar_sink = value;
    }
}

static uint64_t foreign_gc_benchmark_ticks(void)
{
    _mm_lfence();
    const uint64_t result = __rdtsc();
    _mm_lfence();
    return result;
}

// zR, pcrel sdata4, CFA = rsp + 8 and return address at CFA - 8 throughout a six-byte leaf. Padding is DW_CFA_nop.
static void foreign_gc_benchmark_frame(uint8_t* bytes)
{
    static const uint8_t state[] = { 1, 'z', 'R', 0, 1, 0x78, 16, 1, 0x1b, 0x0c, 7, 8, 0x90, 1 };
    const uint32_t cie_length = BUSTER_FOREIGN_GC_BENCHMARK_CIE_BYTES - 4;
    const uint32_t fde_length = BUSTER_FOREIGN_GC_BENCHMARK_FRAME_BYTES - BUSTER_FOREIGN_GC_BENCHMARK_CIE_BYTES - 4;
    const uint32_t cie_pointer = BUSTER_FOREIGN_GC_BENCHMARK_CIE_BYTES + 4;
    const uint32_t range = 6;
    memset(bytes, 0, BUSTER_FOREIGN_GC_BENCHMARK_FRAME_BYTES);
    memcpy(bytes, &cie_length, sizeof(cie_length));
    memcpy(bytes + 8, state, sizeof(state));
    memcpy(bytes + 32, &fde_length, sizeof(fde_length));
    memcpy(bytes + 36, &cie_pointer, sizeof(cie_pointer));
    memcpy(bytes + 44, &range, sizeof(range));
}

static bool foreign_gc_benchmark_generate(const char* directory, uint32_t count, bool all_live, Arena* arena, ForeignGcBenchmarkInput* output)
{
    Arena source = arena_reserve(1ull << 32);
    Arena names = arena_reserve(1ull << 30);
    const uint32_t live = all_live ? count : (count + 1) / 2;
    const LinkCapacity capacity = {
        .names = count + 1,
        .atoms = 2 * count + 1,
        .symbols = count + 1,
        .fixups = count + live,
    };
    Link link = { 0 };
    bool valid = source.base != 0 && names.base != 0 && link_initialize(&link, &source, &names, capacity);
    static const uint8_t footer[] = {
        0x53,                                   // push %rbx
        0xb8, 1, 0, 0, 0,                       // mov $1,%eax
        0xbf, 1, 0, 0, 0,                       // mov $1,%edi
        0x48, 0x89, 0xe6,                       // mov %rsp,%rsi
        0xba, 8, 0, 0, 0,                       // mov $8,%edx
        0x0f, 0x05,                             // write the independent sum
        0x31, 0xff,                             // xor %edi,%edi
        0xb8, 60, 0, 0, 0,                      // mov $60,%eax
        0x0f, 0x05,                             // exit(0)
    };
    const uint32_t entry_length = 2 + 8 * live + sizeof(footer);
    uint8_t* entry_bytes = valid ? BUSTER_ARENA_PUSH_ARRAY(&source, uint8_t, entry_length) : 0;
    SymbolId* functions = valid ? BUSTER_ARENA_PUSH_ARRAY(&source, SymbolId, count) : 0;
    AtomId entry = 0;
    if (entry_bytes != 0 && functions != 0)
    {
        memset(entry_bytes, 0, entry_length);
        entry_bytes[0] = 0x31;
        entry_bytes[1] = 0xdb;
        memcpy(entry_bytes + 2 + 8 * live, footer, sizeof(footer));
        entry = link_add_atom(&link, BUSTER_LINK_CLASS_TEXT, 4, entry_length, entry_bytes, 0);
        const NameId name = link_add_name(&link, (const uint8_t*)"_start", 6);
        valid = entry != 0 && name != 0 && link_add_symbol(&link, name, entry, 0, entry_length,
                                                        BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_PROGRAM) != 0;
    }
    else
    {
        valid = false;
    }
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        uint8_t* code = BUSTER_ARENA_PUSH_ARRAY(&source, uint8_t, 6);
        uint8_t* frame = BUSTER_ARENA_PUSH_ARRAY(&source, uint8_t, BUSTER_FOREIGN_GC_BENCHMARK_FRAME_BYTES);
        valid = code != 0 && frame != 0;
        if (valid)
        {
            const uint32_t value = i + 1;
            code[0] = 0xb8;
            memcpy(code + 1, &value, sizeof(value));
            code[5] = 0xc3;
            foreign_gc_benchmark_frame(frame);
            const AtomId text = link_add_atom(&link, BUSTER_LINK_CLASS_TEXT, 4, 6, code, i + 1);
            const AtomId unwind = link_add_atom(&link, BUSTER_LINK_CLASS_EH_FRAME, 3, BUSTER_FOREIGN_GC_BENCHMARK_FRAME_BYTES, frame, i + 1);
            char name[40];
            const int length = snprintf(name, sizeof(name), "gc_leaf_%u", i);
            const NameId named = link_add_name(&link, (const uint8_t*)name, (uint32_t)length);
            functions[i] = link_add_symbol(&link, named, text, 0, 6, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_PROGRAM);
            valid = text != 0 && unwind != 0 && named != 0 && functions[i] != 0 &&
                    link_add_fixup(&link, unwind, 40, BUSTER_LINK_FIXUP_PC32, functions[i], 0) != 0;
        }
    }
    // Keep each atom's fixups contiguous. Mixed-dead calls even-indexed leaves; the independent sum is live^2.
    uint32_t called = 0;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        if (all_live || i % 2 == 0)
        {
            const uint32_t offset = 2 + 8 * called;
            entry_bytes[offset] = 0xe8;
            entry_bytes[offset + 5] = 0x48;
            entry_bytes[offset + 6] = 0x01;
            entry_bytes[offset + 7] = 0xc3;
            valid = link_add_fixup(&link, entry, offset + 1, BUSTER_LINK_FIXUP_CALL, functions[i], -4) != 0;
            called += 1;
        }
    }
    valid = valid && called == live && link_check(&link) == LINK_ERROR_NONE;
    const uint32_t units = count + 1;
    ForeignTestPath* paths = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignTestPath, units);
    char** arguments = BUSTER_ARENA_PUSH_ARRAY(arena, char*, units);
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "gc_%s_%u", all_live ? "all" : "mixed", count);
    valid = valid && paths != 0 && arguments != 0 && foreign_test_write_program(&link, units, directory, prefix, paths, arena);
    uint64_t input_bytes = 0;
    for (uint32_t unit = 0; valid && unit < units; unit += 1)
    {
        arguments[unit] = paths[unit];
        struct stat status;
        valid = stat(paths[unit], &status) == 0 && status.st_size >= 0;
        input_bytes += valid ? (uint64_t)status.st_size : 0;
    }
    ForeignCommand command = { 0 };
    valid = valid && foreign_test_command(&command, arguments, units, arena);
    if (valid)
    {
        *output = (ForeignGcBenchmarkInput){
            .command = command,
            .expected = all_live ? (uint64_t)count * (count + 1) / 2 : (uint64_t)live * live,
            .input_bytes = input_bytes,
            .logical_bytes = entry_length + (uint64_t)count * (6 + BUSTER_FOREIGN_GC_BENCHMARK_FRAME_BYTES),
            .count = count,
            .live = live,
            .entry_length = entry_length,
            .all_live = all_live,
        };
    }
    arena_release(&names);
    arena_release(&source);
    return valid;
}

// Validate original ELF ownership before consulting the GC maps. The immediate and named definition independently
// identify each leaf; object IDs and canonical-symbol order are not assumed to equal fixture indices.
static bool foreign_gc_benchmark_identity(const ForeignObject* object, uint32_t count, uint32_t* index, uint32_t* text, uint32_t* definition)
{
    bool valid = true;
    uint32_t found = 0;
    *index = UINT32_MAX;
    *text = 0;
    *definition = 0;
    for (uint32_t s = 1; valid && s < object->symbols.count; s += 1)
    {
        const Elf64_Sym symbol = import_symbol(&object->symbols, s);
        if (ELF64_ST_TYPE(symbol.st_info) == STT_FUNC && symbol.st_shndx != SHN_UNDEF)
        {
            uint32_t section = 0;
            String8 name = { 0 };
            valid = import_symbol_section(&object->symbols, s, &symbol, &section) && section != 0 && section < object->elf.section_count &&
                    import_string(object->symbols.names, symbol.st_name, &name) && symbol.st_value == 0;
            if (valid)
            {
                const Elf64_Shdr header = import_section_header(&object->elf, section);
                const String8 bytes = import_section_bytes(&object->elf, &header);
                if (name.length == 6 && memcmp(name.pointer, "_start", 6) == 0)
                {
                    valid = bytes.length == symbol.st_size;
                }
                else
                {
                    uint32_t value = 0;
                    valid = symbol.st_size == 6 && bytes.length == 6 && (uint8_t)bytes.pointer[0] == 0xb8 && (uint8_t)bytes.pointer[5] == 0xc3;
                    if (valid)
                    {
                        memcpy(&value, bytes.pointer + 1, sizeof(value));
                        char expected[40];
                        const int length = snprintf(expected, sizeof(expected), "gc_leaf_%u", value - 1);
                        valid = value > 0 && value <= count && name.length == (uint64_t)length && memcmp(name.pointer, expected, name.length) == 0;
                        *index = value - 1;
                    }
                }
                *text = section;
                *definition = s;
                found += 1;
            }
        }
    }
    return valid && found == 1;
}

static bool foreign_gc_benchmark_validate(const ForeignGcBenchmarkInput* input, const Foreign* foreign, const ForeignLink* before,
                                         const ForeignLink* after, bool collect, const char* path, const ExecutableResult* written, Arena* arena)
{
    const uint64_t saved = arena->length;
    const uint32_t retained = collect ? input->live : input->count;
    const String8 bytes = foreign_test_read(arena, path);
    const ExecutableTestImage image = executable_test_image(bytes);
    const Elf64_Shdr* text_section = executable_test_section(&image, ".text");
    const Elf64_Shdr* frames = executable_test_section(&image, ".eh_frame");
    const Elf64_Shdr* header = executable_test_section(&image, ".eh_frame_hdr");
    bool valid = text_section != 0 && frames != 0 && header != 0 && frames->sh_size == (uint64_t)retained * BUSTER_FOREIGN_GC_BENCHMARK_FRAME_BYTES &&
                 header->sh_size == 12 + 8 * (uint64_t)retained && written->fdes == retained &&
                 executable_test_unwind(&after->link, bytes, BUSTER_EXECUTABLE_BASE, 0, written) == 0;
    uint64_t* cies = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)retained * 2 + 1);
    ExecutableTestFde* fdes = BUSTER_ARENA_PUSH_ARRAY(arena, ExecutableTestFde, retained);
    uint8_t* seen = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, input->count);
    uint8_t* covered = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, before->link.atoms.count);
    AtomId* atoms = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, before->link.atoms.count);
    SymbolId* symbols = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, before->link.symbols.count);
    valid = valid && cies != 0 && fdes != 0 && seen != 0 && covered != 0 && atoms != 0 && symbols != 0;
    const uint64_t fde_count = valid ? executable_test_walk_frames((const uint8_t*)bytes.pointer + frames->sh_offset, frames->sh_size,
                                                                  frames->sh_addr, cies, fdes, retained) : UINT64_MAX;
    valid = valid && fde_count == retained;
    if (valid)
    {
        memset(seen, 0, input->count);
        memset(covered, 0, before->link.atoms.count);
        memset(atoms, 0, (uint64_t)before->link.atoms.count * sizeof(*atoms));
        memset(symbols, 0, (uint64_t)before->link.symbols.count * sizeof(*symbols));
    }
    uint32_t entries = 0;
    uint32_t leaves = 0;
    uint32_t frame_sections = 0;
    for (uint32_t id = 1; valid && id < foreign->object_count; id += 1)
    {
        const ForeignObject* object = &foreign->objects[id];
        const ImportObject* original = &before->objects[id];
        const ImportObject* result = &after->objects[id];
        uint32_t index = 0;
        uint32_t text = 0;
        uint32_t definition = 0;
        valid = object->library == 0 && foreign_gc_benchmark_identity(object, input->count, &index, &text, &definition) &&
                original->section_count == result->section_count && original->symbol_count == result->symbol_count &&
                text < original->section_count && definition < original->symbol_count;
        const bool entry = index == UINT32_MAX;
        const bool kept = entry || !collect || input->all_live || index % 2 == 0;
        const AtomId code = valid ? result->section_atoms[text] : 0;
        const SymbolId symbol = valid ? result->symbols[definition] : 0;
        valid = valid && (kept ? code != 0 && code < after->link.atoms.count && symbol != 0 && symbol < after->link.symbols.count &&
                               after->link.symbols.atom[symbol] == code && after->link.symbols.value[symbol] == 0 &&
                               after->link.symbols.size[symbol] == (entry ? input->entry_length : 6) && after->link.atoms.class[code] == BUSTER_LINK_CLASS_TEXT &&
                               after->link.atoms.size[code] == (entry ? input->entry_length : 6) : code == 0 && symbol == 0);
        if (valid && entry)
        {
            entries += 1;
        }
        else if (valid)
        {
            valid = index < input->count && seen[index] == 0;
            if (valid)
            {
                seen[index] = 1;
                leaves += 1;
            }
        }
        if (valid && !entry && kept)
        {
            const uint32_t value = index + 1;
            uint8_t expected[] = { 0xb8, 0, 0, 0, 0, 0xc3 };
            memcpy(expected + 1, &value, sizeof(value));
            const uint64_t pc = BUSTER_EXECUTABLE_BASE + after->link.atoms.address[code];
            valid = pc >= text_section->sh_addr && pc - text_section->sh_addr <= text_section->sh_size &&
                    sizeof(expected) <= text_section->sh_size - (pc - text_section->sh_addr) &&
                    memcmp(after->link.atoms.bytes[code], expected, sizeof(expected)) == 0 &&
                    memcmp(bytes.pointer + text_section->sh_offset + (pc - text_section->sh_addr), expected, sizeof(expected)) == 0;
        }
        uint32_t object_frames = 0;
        uint32_t object_texts = 0;
        for (uint32_t section = 1; valid && section < original->section_count; section += 1)
        {
            const AtomId old = original->section_atoms[section];
            const AtomId atom = result->section_atoms[section];
            valid = old < before->link.atoms.count && atom < after->link.atoms.count;
            if (valid && old != 0)
            {
                const uint8_t class = before->link.atoms.class[old];
                valid = covered[old] == 0 && (class == BUSTER_LINK_CLASS_TEXT || class == BUSTER_LINK_CLASS_EH_FRAME) && (atom != 0) == kept;
                covered[old] = 1;
                atoms[old] = kept ? UINT32_MAX : 0;
                object_texts += class == BUSTER_LINK_CLASS_TEXT ? 1 : 0;
                if (valid && class == BUSTER_LINK_CLASS_EH_FRAME)
                {
                    object_frames += 1;
                    frame_sections += 1;
                    Elf64_Rela relocation = { 0 };
                    uint32_t relocations = 0;
                    for (uint32_t r = 0; valid && r < object->relocation_count; r += 1)
                    {
                        const Elf64_Shdr table = import_section_header(&object->elf, object->relocations[r]);
                        if (table.sh_info == section)
                        {
                            const String8 data = import_section_bytes(&object->elf, &table);
                            valid = data.length == sizeof(relocation);
                            if (valid)
                            {
                                memcpy(&relocation, data.pointer, sizeof(relocation));
                            }
                            relocations += 1;
                        }
                    }
                    const uint64_t target = ELF64_R_SYM(relocation.r_info);
                    const Elf64_Sym owner = target < object->symbols.count ? import_symbol(&object->symbols, (uint32_t)target) : (Elf64_Sym){ 0 };
                    uint32_t owner_section = 0;
                    valid = valid && !entry && relocations == 1 && relocation.r_offset == 40 && relocation.r_addend == 0 &&
                            ELF64_R_TYPE(relocation.r_info) == R_X86_64_PC32 && target < object->symbols.count && owner.st_value == 0 &&
                            import_symbol_section(&object->symbols, (uint32_t)target, &owner, &owner_section) && owner_section == text &&
                            before->link.atoms.size[old] == BUSTER_FOREIGN_GC_BENCHMARK_FRAME_BYTES;
                    const ForeignLinkUnwind* plan = foreign_link_unwind(after, id, section);
                    if (valid && collect && kept)
                    {
                        valid = plan != 0 && plan->plan.old_length == BUSTER_FOREIGN_GC_BENCHMARK_FRAME_BYTES &&
                                plan->plan.bytes.length == BUSTER_FOREIGN_GC_BENCHMARK_FRAME_BYTES && plan->plan.record_count == 2 && plan->plan.fde_count == 1 &&
                                plan->plan.records[0].kind == FOREIGN_UNWIND_CIE && plan->plan.records[0].old_offset == 0 && plan->plan.records[0].new_offset == 0 &&
                                plan->plan.records[1].kind == FOREIGN_UNWIND_FDE && plan->plan.records[1].old_offset == 32 && plan->plan.records[1].new_offset == 32 &&
                                plan->plan.records[1].section == text && plan->plan.records[1].atom == code;
                        for (uint64_t offset = 0; valid && offset <= BUSTER_FOREIGN_GC_BENCHMARK_FRAME_BYTES; offset += 1)
                        {
                            uint64_t mapped = UINT64_MAX;
                            valid = foreign_unwind_offset(&plan->plan, offset, &mapped) && mapped == offset;
                        }
                        uint64_t outside = 0;
                        valid = valid && !foreign_unwind_offset(&plan->plan, BUSTER_FOREIGN_GC_BENCHMARK_FRAME_BYTES + 1, &outside);
                    }
                    else if (valid)
                    {
                        valid = plan == 0;
                    }
                    if (valid && kept)
                    {
                        const uint64_t pc = BUSTER_EXECUTABLE_BASE + after->link.atoms.address[code];
                        const uint64_t frame = BUSTER_EXECUTABLE_BASE + after->link.atoms.address[atom];
                        const uint64_t found = executable_test_fde_at(fdes, fde_count, frame + 32);
                        const uint64_t table = executable_test_lookup((const uint8_t*)bytes.pointer + header->sh_offset + 12, retained, header->sh_addr, (int64_t)pc);
                        int32_t relative = 0;
                        if (table != UINT64_MAX)
                        {
                            memcpy(&relative, bytes.pointer + header->sh_offset + 12 + 8 * table + 4, sizeof(relative));
                        }
                        valid = found != UINT64_MAX && fdes[found].initial == (int64_t)pc && fdes[found].range == 6 && table != UINT64_MAX &&
                                (int64_t)header->sh_addr + relative == (int64_t)(frame + 32) && frame >= frames->sh_addr &&
                                frame - frames->sh_addr <= frames->sh_size && BUSTER_FOREIGN_GC_BENCHMARK_FRAME_BYTES <= frames->sh_size - (frame - frames->sh_addr);
                        if (valid)
                        {
                            uint8_t expected[BUSTER_FOREIGN_GC_BENCHMARK_FRAME_BYTES];
                            foreign_gc_benchmark_frame(expected);
                            const uint8_t* actual = (const uint8_t*)bytes.pointer + frames->sh_offset + (frame - frames->sh_addr);
                            // Only the relocated pc_begin at byte 40 differs; each unchanged CIE remains adjacent.
                            valid = memcmp(actual, expected, 40) == 0 && memcmp(actual + 44, expected + 44, sizeof(expected) - 44) == 0;
                        }
                    }
                }
            }
            else if (valid)
            {
                valid = atom == 0;
            }
        }
        valid = valid && object_texts == 1 && object_frames == (entry ? 0 : 1);
    }
    valid = valid && entries == 1 && leaves == input->count && frame_sections == input->count;
    uint32_t atom_count = 1;
    uint32_t symbol_count = 1;
    for (AtomId atom = 1; valid && atom < before->link.atoms.count; atom += 1)
    {
        valid = covered[atom] == 1;
        if (atoms[atom] != 0)
        {
            atoms[atom] = collect ? atom_count : atom;
            atom_count += 1;
        }
    }
    for (SymbolId symbol = 1; valid && symbol < before->link.symbols.count; symbol += 1)
    {
        const AtomId atom = before->link.symbols.atom[symbol];
        valid = atom < before->link.atoms.count;
        if (valid && (!collect || (atom != 0 && atoms[atom] != 0)))
        {
            symbols[symbol] = collect ? symbol_count : symbol;
            symbol_count += 1;
        }
    }
    valid = valid && after->link.atoms.count == atom_count && after->link.symbols.count == symbol_count &&
            after->link.fixups.count == 1 + input->live + retained;
    for (uint32_t name = 0; valid && name < foreign->names.count; name += 1)
    {
        valid = before->names[name] < before->link.symbols.count && after->names[name] == symbols[before->names[name]];
    }
    for (uint32_t id = 0; valid && id < foreign->object_count; id += 1)
    {
        const ImportObject* original = &before->objects[id];
        const ImportObject* result = &after->objects[id];
        valid = original->section_count == result->section_count && original->symbol_count == result->symbol_count;
        for (uint32_t section = 0; valid && section < original->section_count; section += 1)
        {
            valid = original->section_atoms[section] < before->link.atoms.count && result->section_atoms[section] == atoms[original->section_atoms[section]];
        }
        for (uint32_t symbol = 0; valid && symbol < original->symbol_count; symbol += 1)
        {
            valid = original->symbols[symbol] < before->link.symbols.count && result->symbols[symbol] == symbols[original->symbols[symbol]];
        }
    }
    arena_pop_to(arena, saved);
    return valid;
}

static ForeignGcBenchmarkSample foreign_gc_benchmark_sample(const ForeignGcBenchmarkInput* input, const ForeignGcBenchmarkVariant* variant,
                                                            const char* path, uint32_t after_scalar, Arena* validation)
{
    ForeignGcBenchmarkSample sample = { 0 };
    const bool removed = unlink(path) == 0 || errno == ENOENT;
    const String8 destination = {
        .pointer = (char*)path,
        .length = strlen(path),
    };
    Foreign foreign = { 0 };
    ForeignLink converted = { 0 };
    ForeignLink collected = { 0 };
    foreign_gc_benchmark_after_scalar(after_scalar);
    const uint64_t start_ticks = foreign_gc_benchmark_ticks();
    const uint64_t start = foreign_now();
    Arena arena = arena_reserve(1ull << 32);
    Arena names = arena_reserve(1ull << 30);
    foreign_run(&foreign, &input->command, variant->path, 1, 0);
    const uint64_t resolved = foreign_now();
    sample.built = variant->collect ? foreign_link_build_for_gc(&converted, &foreign, &arena, &names, RUNTIME_KIND_STATIC, (String8){ 0 }) :
                                     foreign_link_build(&converted, &foreign, &arena, &names);
    const uint64_t converted_at = foreign_now();
    ForeignLink* result = &converted;
    if (sample.built.error == FOREIGN_LINK_ERROR_NONE && variant->collect)
    {
        const ForeignGcRoots roots = {
            .entry = foreign_link_test_symbol(&foreign, &converted, "_start"),
        };
        sample.collected = foreign_gc_link(&collected, &foreign, &converted, &roots, variant->path, &arena, &names);
        result = &collected;
    }
    const uint64_t collected_at = foreign_now();
    const bool prepared = removed && arena.base != 0 && names.base != 0 && foreign.failure.error == FOREIGN_ERROR_NONE &&
                          sample.built.error == FOREIGN_LINK_ERROR_NONE && sample.collected.error == FOREIGN_LINK_ERROR_NONE;
    if (prepared)
    {
        const SymbolId entry = foreign_link_test_symbol(&foreign, result, "_start");
        sample.valid = entry != 0 && (variant->path == FOREIGN_PATH_EXACT ? executable_test_write_exact(&result->link, entry, destination, 1, &sample.written) :
                                                                         executable_test_write(&result->link, entry, destination, 1, &sample.written, 0, 0));
        sample.atoms = result->link.atoms.count;
        sample.symbols = result->link.symbols.count;
        sample.fixups = result->link.fixups.count;
    }
    const uint64_t written_at = foreign_now();
    sample.resolve_error = (uint32_t)foreign.failure.error;
    sample.retained = arena.length + names.length + foreign.arena.length;
    for (uint32_t lane = 0; foreign.lane_states != 0 && lane < foreign.lanes; lane += 1)
    {
        sample.retained += foreign.lane_states[lane].arena.length;
    }
    sample.output_bytes = sample.written.file_size;
    // Only the separate replay supplies validation. Its clocks are discarded; reported samples never enter here.
    if (validation != 0 && sample.valid)
    {
        sample.valid = foreign_gc_benchmark_validate(input, &foreign, &converted, result, variant->collect, path, &sample.written, validation);
    }
    arena_release(&names);
    foreign_release(&foreign);
    arena_release(&arena);
    const uint64_t finished = foreign_now();
    const uint64_t finished_ticks = foreign_gc_benchmark_ticks();
    sample.times[0] = resolved - start;
    sample.times[1] = converted_at - resolved;
    sample.times[2] = collected_at - converted_at;
    sample.times[3] = written_at - collected_at;
    sample.times[4] = finished - written_at;
    sample.times[5] = finished - start;
    sample.ticks = finished_ticks - start_ticks;
    return sample;
}

static bool foreign_gc_benchmark_case(const char* directory, uint32_t count, bool all_live, uint32_t runs, uint32_t after_scalar,
                                     const uint32_t* order, uint32_t variants, bool interleaved, Arena* arena)
{
    const uint64_t saved = arena->length;
    ForeignTestTally tally = {
        .check = "foreign GC pipeline validation",
    };
    ForeignGcBenchmarkInput input = { 0 };
    bool valid = foreign_gc_benchmark_generate(directory, count, all_live, arena, &input);
    foreign_test_expect(&tally, valid, "generate frozen original leaf and CIE/FDE objects", 0);
    uint64_t* times = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)4 * BUSTER_FOREIGN_GC_BENCHMARK_PHASES * runs);
    uint64_t* ticks = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)4 * runs);
    valid = valid && times != 0 && ticks != 0;
    String8 reference[2] = { 0 };
    ForeignGcBenchmarkSample last[4] = { 0 };
    const uint32_t samples = variants * (runs + 2);
    for (uint32_t step = 0; valid && step < samples; step += 1)
    {
        const bool replay = step < variants;
        const uint32_t at = replay ? step : step - variants;
        const uint32_t round = replay ? 0 : interleaved ? at / variants : at % (runs + 1);
        const uint32_t position = replay ? at : interleaved ? (at % variants + round % variants) % variants : at / (runs + 1);
        const uint32_t v = order[position];
        const ForeignGcBenchmarkVariant* variant = &foreign_gc_benchmark_variants[v];
        ForeignTestPath path;
        snprintf(path, sizeof(path), "%s/gc_output_%u_%u_%u", directory, count, all_live ? 1 : 0, v);
        const ForeignGcBenchmarkSample sample = foreign_gc_benchmark_sample(&input, variant, path, after_scalar, replay ? arena : 0);
        const uint32_t retained = variant->collect ? input.live : input.count;
        valid = sample.valid && sample.written.fdes == retained && sample.atoms == 2 + 2 * retained && sample.fixups == 1 + input.live + retained;
        char detail[256];
        snprintf(detail, sizeof(detail), "%s count %u %s %s %u: resolve %u convert %u import %u GC %u write %u", all_live ? "all-live" : "mixed-dead",
                 count, variant->name, replay ? "validation" : "round", round, sample.resolve_error, (unsigned)sample.built.error,
                 (unsigned)sample.built.import.error, (unsigned)sample.collected.error, (unsigned)sample.written.error);
        foreign_test_expect(&tally, valid, "whole pipeline and independent live/FDE counts", detail);
        const uint64_t validation = arena->length;
        const uint32_t state = !all_live && variant->collect ? 1 : 0;
        if (valid)
        {
            const bool ran = executable_test_runs(path, (const uint8_t*)&input.expected, sizeof(input.expected));
            const String8 bytes = foreign_test_read(arena, path);
            const bool same = bytes.pointer != 0 && (reference[state].pointer == 0 ||
                              (bytes.length == reference[state].length && memcmp(bytes.pointer, reference[state].pointer, bytes.length) == 0));
            foreign_test_expect(&tally, ran, "live leaf calls produce the independent sum", detail);
            foreign_test_expect(&tally, same, all_live ? "all-live complete bytes agree across paths and GC states" :
                                "complete bytes agree across paths and repeats within GC state", detail);
            valid = ran && same;
            if (valid && !replay && round != 0)
            {
                for (uint32_t phase = 0; phase < BUSTER_FOREIGN_GC_BENCHMARK_PHASES; phase += 1)
                {
                    times[((uint64_t)v * BUSTER_FOREIGN_GC_BENCHMARK_PHASES + phase) * runs + round - 1] = sample.times[phase];
                }
                ticks[(uint64_t)v * runs + round - 1] = sample.ticks;
                last[v] = sample;
            }
        }
        arena_pop_to(arena, validation);
        if (valid && reference[state].pointer == 0)
        {
            reference[state] = foreign_test_read(arena, path);
            valid = reference[state].pointer != 0;
            foreign_test_expect(&tally, valid, "retain byte oracle outside timing", detail);
        }
    }
    // No partial timing rows: every requested variant must finish all measured rounds and validation first.
    for (uint32_t position = 0; valid && position < variants; position += 1)
    {
        const uint32_t v = order[position];
        const ForeignGcBenchmarkVariant* variant = &foreign_gc_benchmark_variants[v];
        double medians[BUSTER_FOREIGN_GC_BENCHMARK_PHASES];
        for (uint32_t phase = 0; phase < BUSTER_ARRAY_LENGTH(medians); phase += 1)
        {
            medians[phase] = foreign_link_test_median(times + ((uint64_t)v * BUSTER_ARRAY_LENGTH(medians) + phase) * runs, runs);
        }
        uint64_t* totals = times + ((uint64_t)v * BUSTER_ARRAY_LENGTH(medians) + 5) * runs;
        uint64_t* elapsed_ticks = ticks + (uint64_t)v * runs;
        qsort(elapsed_ticks, runs, sizeof(*elapsed_ticks), foreign_link_test_compare_time);
        const ForeignGcBenchmarkSample* sample = &last[v];
        const uint32_t retained = variant->collect ? input.live : input.count;
        const uint64_t logical = input.entry_length + (uint64_t)retained * (6 + BUSTER_FOREIGN_GC_BENCHMARK_FRAME_BYTES);
        const double seconds = (double)totals[runs / 2] * 0.000000001;
        printf("foreign-gc %s count %u live %u objects %u %s runs %u schedule %s: resolve %.1f us convert %.1f us GC %.1f us write %.1f us cleanup %.1f us total %.1f us wall-min/median/max %llu/%llu/%llu ns TSCticks %llu after-scalar %u ms ET_REL-input %llu bytes output %llu bytes logical-input %llu bytes logical-output %llu bytes EH-input %llu bytes EH-output %llu bytes FDEs %u atoms %u symbols %u fixups %u retained-arena %llu bytes input-Bps %.0f output-Bps %.0f\n",
               all_live ? "all-live" : "mixed-dead", count, input.live, count + 1, variant->name, runs, interleaved ? "rotating-interleaved" : "blocked",
               medians[0], medians[1], medians[2], medians[3], medians[4], medians[5], (unsigned long long)totals[0],
               (unsigned long long)totals[runs / 2], (unsigned long long)totals[runs - 1], (unsigned long long)elapsed_ticks[runs / 2], after_scalar,
               (unsigned long long)input.input_bytes, (unsigned long long)sample->output_bytes, (unsigned long long)input.logical_bytes, (unsigned long long)logical,
               (unsigned long long)((uint64_t)count * BUSTER_FOREIGN_GC_BENCHMARK_FRAME_BYTES),
               (unsigned long long)((uint64_t)retained * BUSTER_FOREIGN_GC_BENCHMARK_FRAME_BYTES), retained,
               sample->atoms - 1, sample->symbols - 1, sample->fixups - 1, (unsigned long long)sample->retained,
               seconds != 0 ? input.input_bytes / seconds : 0, seconds != 0 ? sample->output_bytes / seconds : 0);
    }
    foreign_test_expect(&tally, valid, "all requested variants completed", 0);
    const bool passed = foreign_test_report(&tally);
    arena_pop_to(arena, saved);
    return passed && valid;
}

static bool foreign_gc_benchmark_counts(const char* text, uint32_t* counts, uint32_t* count)
{
    const char* at = text;
    uint32_t added = 0;
    bool valid = *at != 0;
    while (valid && *at != 0)
    {
        char* end = 0;
        const uint64_t value = strtoull(at, &end, 10);
        valid = *at >= '0' && *at <= '9' && value > 0 && value <= BUSTER_FOREIGN_GC_BENCHMARK_MAX_LEAVES && added < 16 && (*end == 0 || *end == ',');
        if (valid)
        {
            counts[added] = (uint32_t)value;
            added += 1;
            at = *end == ',' ? end + 1 : end;
            valid = *end != ',' || *at != 0;
        }
    }
    *count = valid ? added : 0;
    return valid;
}

static bool foreign_gc_benchmark_order(const char* text, uint32_t* order, uint32_t* count)
{
    const char* at = text;
    uint32_t added = 0;
    uint32_t mask = 0;
    bool valid = *at != 0;
    while (valid && *at != 0)
    {
        const char* comma = strchr(at, ',');
        const uint64_t length = comma != 0 ? (uint64_t)(comma - at) : strlen(at);
        uint32_t found = BUSTER_ARRAY_LENGTH(foreign_gc_benchmark_variants);
        for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(foreign_gc_benchmark_variants); v += 1)
        {
            const char* name = foreign_gc_benchmark_variants[v].name;
            if (strlen(name) == length && memcmp(at, name, length) == 0)
            {
                found = v;
            }
        }
        valid = found < BUSTER_ARRAY_LENGTH(foreign_gc_benchmark_variants) && (mask & (1u << found)) == 0;
        if (valid)
        {
            order[added] = found;
            added += 1;
            mask |= 1u << found;
            at = comma != 0 ? comma + 1 : at + length;
            valid = comma == 0 || *at != 0;
        }
    }
    *count = valid ? added : 0;
    return valid;
}

static __attribute__((unused)) int run_foreign_gc_benchmark(int argc, char** argv)
{
    uint32_t counts[16] = { 8, 1024 };
    uint32_t count = 2;
    uint32_t order[4] = { 0, 1, 2, 3 };
    uint32_t variants = 4;
    uint32_t runs = 21;
    uint32_t after_scalar = 0;
    uint32_t cases = 3;
    bool interleaved = true;
    bool keep = false;
    bool valid = true;
    const char* base = getenv("TMPDIR") != 0 ? getenv("TMPDIR") : "/tmp";
    for (int i = 0; valid && i < argc; i += 1)
    {
        const bool value = i + 1 < argc;
        if (strcmp(argv[i], "--counts") == 0 && value)
        {
            valid = foreign_gc_benchmark_counts(argv[i + 1], counts, &count);
            i += 1;
        }
        else if (strcmp(argv[i], "--runs") == 0 && value)
        {
            char* end = 0;
            const uint64_t number = strtoull(argv[i + 1], &end, 10);
            valid = argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9' && *end == 0 && number > 0 && number <= 1024;
            runs = valid ? (uint32_t)number : runs;
            i += 1;
        }
        else if (strcmp(argv[i], "--order") == 0 && value)
        {
            valid = foreign_gc_benchmark_order(argv[i + 1], order, &variants);
            i += 1;
        }
        else if (strcmp(argv[i], "--schedule") == 0 && value)
        {
            valid = strcmp(argv[i + 1], "interleaved") == 0 || strcmp(argv[i + 1], "blocked") == 0;
            interleaved = strcmp(argv[i + 1], "interleaved") == 0;
            i += 1;
        }
        else if (strcmp(argv[i], "--after-scalar") == 0 && value)
        {
            char* end = 0;
            const uint64_t number = strtoull(argv[i + 1], &end, 10);
            valid = argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9' && *end == 0 && number <= 300;
            after_scalar = valid ? (uint32_t)number : after_scalar;
            i += 1;
        }
        else if (strcmp(argv[i], "--only") == 0 && value)
        {
            cases = strcmp(argv[i + 1], "mixed") == 0 ? 1 : strcmp(argv[i + 1], "all") == 0 ? 2 : 0;
            valid = cases != 0;
            i += 1;
        }
        else if (strcmp(argv[i], "--directory") == 0 && value)
        {
            base = argv[i + 1];
            i += 1;
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else
        {
            valid = false;
        }
    }
    ForeignTestPath directory;
    const int length = snprintf(directory, sizeof(directory), "%s/buster-foreign-gc-XXXXXX", base);
    valid = valid && length > 0 && (uint64_t)length < sizeof(directory);
    Arena arena = arena_reserve(BUSTER_FOREIGN_TEST_ARENA_BYTES);
    bool passed = false;
    if (!valid)
    {
        printf("usage: benchmark-foreign-gc [--counts LIST <= 16384] [--runs N] [--order no-gc-exact,gc-exact,no-gc-vector,gc-vector] [--schedule interleaved|blocked] [--only mixed|all] [--after-scalar MS <= 300] [--directory PATH] [--keep]\n");
    }
    else if (arena.base == 0 || mkdtemp(directory) == 0)
    {
        printf("benchmark-foreign-gc: no room for the arena or output directory\n");
    }
    else
    {
        printf("foreign-gc scope: fresh per-link arenas/maps through writer close and release; frozen-file warm cache; fixture/command generation, output unlink, conditioning and validation excluded; retained arena excludes maps/transient scratch and is not peak RSS; logical bytes are entry/leaf/CFI payload, rates use ET_REL input and complete output bytes; one lane, pin externally\n");
        printf("foreign-gc order:");
        for (uint32_t v = 0; v < variants; v += 1)
        {
            printf(" %s", foreign_gc_benchmark_variants[order[v]].name);
        }
        printf("\n");
        passed = true;
        for (uint32_t p = 0; passed && p < count; p += 1)
        {
            for (uint32_t c = 0; passed && c < 2; c += 1)
            {
                if ((cases & (1u << c)) != 0)
                {
                    passed = foreign_gc_benchmark_case(directory, counts[p], c != 0, runs, after_scalar, order, variants, interleaved, &arena);
                }
            }
        }
        if (!keep && passed)
        {
            char* remove[] = {
                (char*)"rm",
                (char*)"-rf",
                directory,
                0,
            };
            object_test_run(remove, "/dev/null", "/dev/null");
        }
        else
        {
            printf("benchmark-foreign-gc: kept %s\n", directory);
        }
    }
    arena_release(&arena);
    return passed ? 0 : 1;
}

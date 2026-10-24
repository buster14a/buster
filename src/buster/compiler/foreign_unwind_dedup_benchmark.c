// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Whole foreign-input pipeline with optional terminal CIE pooling. Include after foreign_test.c. Original frozen
// ET_REL files are generated once, outside timing. Each measured sample reserves its own link/name arenas, maps
// and resolves every input, converts, optionally pools, writes/closes the executable, and releases every per-link
// arena and input mapping BEFORE stopping the clock. Commands, fixtures and thread-local scratch caches persist;
// retained-arena bytes exclude mapped files, generated inputs and transient scratch and are not peak RSS.
//
// One leaf per object gives one valid CIE/FDE. Duplicate CIEs are identical; unique CIEs differ only in their code
// alignment, unused by these instruction-free FDEs. Both describe the same unchanged leaf stack. The entry calls
// every leaf and prints an independently computed sum. An untimed replay checks each original-section map and FDE
// address/range. Every timed output must run and match its pooling state's full reference bytes outside timing.
//
// Variants rotate within each interleaved round after one warm-up each, or run in configurable blocked order.
// --after-scalar uses bounded scalar conditioning outside timing. TSC ticks are elapsed
// reference ticks, not core cycles. Pin externally to CPU 2 under the approved host lock. This command does not
// change the opt-in default, and timings become evidence only after repeated controlled approved-host runs.

#define BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_MAX_UNITS 16384
#define BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_CIE_BYTES 32
#define BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_FDE_BYTES 24
#define BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_FRAME_BYTES 56
#define BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_PHASES 6

typedef struct ForeignUnwindDedupBenchmarkVariant ForeignUnwindDedupBenchmarkVariant;
struct ForeignUnwindDedupBenchmarkVariant
{
    const char* name;
    ForeignPath path;
    bool dedup;
};

static const ForeignUnwindDedupBenchmarkVariant foreign_unwind_dedup_benchmark_variants[] = {
    {
        .name = "no-dedup-exact",
        .path = FOREIGN_PATH_EXACT,
    },
    {
        .name = "dedup-exact",
        .path = FOREIGN_PATH_EXACT,
        .dedup = true,
    },
    {
        .name = "no-dedup-vector",
        .path = FOREIGN_PATH_VECTOR,
    },
    {
        .name = "dedup-vector",
        .path = FOREIGN_PATH_VECTOR,
        .dedup = true,
    },
};

typedef struct ForeignUnwindDedupBenchmarkInput ForeignUnwindDedupBenchmarkInput;
struct ForeignUnwindDedupBenchmarkInput
{
    ForeignCommand command;
    uint64_t expected;
    uint64_t input_bytes;
    uint32_t count;
    bool duplicate;
};

typedef struct ForeignUnwindDedupBenchmarkSample ForeignUnwindDedupBenchmarkSample;
struct ForeignUnwindDedupBenchmarkSample
{
    uint64_t times[BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_PHASES];
    uint64_t ticks;
    uint64_t retained;
    uint64_t output_bytes;
    uint64_t removed_bytes;
    uint32_t removed_cies;
    uint32_t removed_atoms;
    uint32_t resolve_error;
    ForeignLinkResult built;
    ForeignLinkResult pooled;
    ExecutableResult written;
    bool fallback;
    bool valid;
};

static volatile uint64_t foreign_unwind_dedup_benchmark_scalar_sink;

// This original conditioning chain matches the standalone string benchmark. Inspect its generated code on the
// approved host before interpreting conditioned measurements as a light-work control.
static __attribute__((noinline)) void foreign_unwind_dedup_benchmark_after_scalar(uint32_t milliseconds)
{
    if (milliseconds != 0)
    {
        const uint64_t start = foreign_now();
        const uint64_t duration = (uint64_t)milliseconds * 1000000;
        uint64_t checksum = 0x9e3779b97f4a7c15ull;
        uint64_t iterations = 0;
        do
        {
            #pragma clang loop vectorize(disable) interleave(disable)
            for (uint32_t i = 0; i < 4096; i += 1)
            {
                checksum ^= checksum >> 12;
                checksum ^= checksum << 25;
                checksum ^= checksum >> 27;
                checksum *= 0x2545f4914f6cdd1dull;
            }
            iterations += 4096;
        } while (foreign_now() - start < duration);
        foreign_unwind_dedup_benchmark_scalar_sink = checksum ^ iterations;
    }
}

static uint64_t foreign_unwind_dedup_benchmark_ticks(void)
{
    _mm_lfence();
    const uint64_t result = __rdtsc();
    _mm_lfence();
    return result;
}

// The only code-alignment-dependent CFI operation would advance a PC. Neither record has one: all PCs in a leaf
// have CFA = rsp + 8 and the return address at CFA - 8, irrespective of this positive alignment value.
static void foreign_unwind_dedup_benchmark_frame(uint8_t* bytes, uint32_t alignment)
{
    memset(bytes, 0, BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_FRAME_BYTES);
    const uint32_t cie_length = BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_CIE_BYTES - 4;
    memcpy(bytes, &cie_length, sizeof(cie_length));
    bytes[8] = 1;
    bytes[9] = 'z';
    bytes[10] = 'R';
    uint32_t at = 12;
    do
    {
        const uint8_t next = (uint8_t)(alignment & 0x7f);
        alignment >>= 7;
        bytes[at++] = next | (alignment != 0 ? 0x80 : 0);
    } while (alignment != 0);
    static const uint8_t state[] = { 0x78, 16, 1, 0x1b, 0x0c, 7, 8, 0x90, 1 };
    memcpy(bytes + at, state, sizeof(state));
    const uint32_t fde_length = BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_FDE_BYTES - 4;
    const uint32_t cie_pointer = BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_CIE_BYTES + 4;
    const uint32_t range = 6;
    memcpy(bytes + 32, &fde_length, sizeof(fde_length));
    memcpy(bytes + 36, &cie_pointer, sizeof(cie_pointer));
    memcpy(bytes + 44, &range, sizeof(range));
}

static bool foreign_unwind_dedup_benchmark_generate(const char* directory, uint32_t count, bool duplicate, Arena* arena, ForeignUnwindDedupBenchmarkInput* output)
{
    Arena source = arena_reserve(1ull << 32);
    Arena names = arena_reserve(1ull << 30);
    const LinkCapacity capacity = { .names = count + 1, .atoms = 2 * count + 1, .symbols = count + 1, .fixups = 2 * count };
    Link link = { 0 };
    bool valid = source.base != 0 && names.base != 0 && link_initialize(&link, &source, &names, capacity);
    static const uint8_t footer[] = {
        0x53,                                   // push %rbx
        0xb8, 1, 0, 0, 0,                       // mov $1,%eax
        0xbf, 1, 0, 0, 0,                       // mov $1,%edi
        0x48, 0x89, 0xe6,                       // mov %rsp,%rsi
        0xba, 8, 0, 0, 0,                       // mov $8,%edx
        0x0f, 0x05,                             // write the sum
        0x31, 0xff,                             // xor %edi,%edi
        0xb8, 60, 0, 0, 0,                      // mov $60,%eax
        0x0f, 0x05,                             // exit(0)
    };
    const uint32_t entry_length = 2 + 8 * count + sizeof(footer);
    uint8_t* entry_bytes = valid ? BUSTER_ARENA_PUSH_ARRAY(&source, uint8_t, entry_length) : 0;
    SymbolId* functions = valid ? BUSTER_ARENA_PUSH_ARRAY(&source, SymbolId, count) : 0;
    AtomId entry = 0;
    if (entry_bytes != 0 && functions != 0)
    {
        memset(entry_bytes, 0, entry_length);
        entry_bytes[0] = 0x31;
        entry_bytes[1] = 0xdb;
        memcpy(entry_bytes + 2 + 8 * count, footer, sizeof(footer));
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
        uint8_t* frame = BUSTER_ARENA_PUSH_ARRAY(&source, uint8_t, BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_FRAME_BYTES);
        valid = code != 0 && frame != 0;
        if (valid)
        {
            const uint32_t value = i + 1;
            code[0] = 0xb8;
            memcpy(code + 1, &value, sizeof(value));
            code[5] = 0xc3;
            foreign_unwind_dedup_benchmark_frame(frame, duplicate ? 1 : value);
            const AtomId text = link_add_atom(&link, BUSTER_LINK_CLASS_TEXT, 4, 6, code, i + 1);
            const AtomId unwind = link_add_atom(&link, BUSTER_LINK_CLASS_EH_FRAME, 3, BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_FRAME_BYTES, frame, i + 1);
            char name[40];
            const int length = snprintf(name, sizeof(name), "unwind_leaf_%u", i);
            const NameId named = link_add_name(&link, (const uint8_t*)name, (uint32_t)length);
            const SymbolId symbol = link_add_symbol(&link, named, text, 0, 6, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_PROGRAM);
            functions[i] = symbol;
            const uint32_t offset = 2 + 8 * i;
            entry_bytes[offset] = 0xe8;
            entry_bytes[offset + 5] = 0x48;
            entry_bytes[offset + 6] = 0x01;
            entry_bytes[offset + 7] = 0xc3;
            valid = text != 0 && unwind != 0 && named != 0 && symbol != 0 &&
                    link_add_fixup(&link, unwind, 40, BUSTER_LINK_FIXUP_PC32, symbol, 0) != 0;
        }
    }
    // Each atom's fixups must occupy one contiguous range in the Link table.
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        valid = link_add_fixup(&link, entry, 3 + 8 * i, BUSTER_LINK_FIXUP_CALL, functions[i], -4) != 0;
    }
    valid = valid && link_check(&link) == LINK_ERROR_NONE;
    const uint32_t units = count + 1;
    ForeignTestPath* paths = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignTestPath, units);
    char** arguments = BUSTER_ARENA_PUSH_ARRAY(arena, char*, units);
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "unwind_%s_%u", duplicate ? "duplicate" : "unique", count);
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
        *output = (ForeignUnwindDedupBenchmarkInput){
            .command = command,
            .expected = (uint64_t)count * (count + 1) / 2,
            .input_bytes = input_bytes,
            .count = count,
            .duplicate = duplicate,
        };
    }
    arena_release(&names);
    arena_release(&source);
    return valid;
}

static bool foreign_unwind_dedup_benchmark_maps(const ForeignUnwindDedupBenchmarkInput* input, const Foreign* foreign, const ForeignLink* before,
                                               const ForeignUnwindDedupLink* after, const ExecutableTestFde* fdes, uint64_t fde_count, Arena* arena)
{
    const uint64_t saved = arena->length;
    const uint32_t removed = input->duplicate ? input->count - 1 : 0;
    bool valid = after->section_count == input->count && after->plan.removed_cies == removed && after->plan.removed_atoms == removed &&
                 after->plan.removed_bytes == (uint64_t)removed * BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_CIE_BYTES;
    uint64_t* starts = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)foreign->object_count + 1);
    uint64_t total = 0;
    for (uint32_t id = 0; starts != 0 && id < foreign->object_count; id += 1)
    {
        starts[id] = total;
        total += before->objects[id].section_count;
    }
    uint8_t* seen = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, total);
    valid = valid && starts != 0 && seen != 0;
    if (seen != 0)
    {
        memset(seen, 0, total);
    }
    for (uint32_t s = 0; valid && s < after->section_count; s += 1)
    {
        const ForeignUnwindDedupSection* section = &after->sections[s];
        const bool source = section->object != 0 && section->object < foreign->object_count && section->section < before->objects[section->object].section_count;
        const AtomId original = source ? before->objects[section->object].section_atoms[section->section] : 0;
        uint64_t cie = UINT64_MAX;
        uint64_t fde = UINT64_MAX;
        uint64_t end = UINT64_MAX;
        uint64_t outside = UINT64_MAX;
        valid = source && seen[starts[section->object] + section->section] == 0 &&
                original != 0 && original < before->link.atoms.count && before->link.atoms.class[original] == BUSTER_LINK_CLASS_EH_FRAME &&
                before->link.atoms.size[original] == BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_FRAME_BYTES &&
                section->old_length == BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_FRAME_BYTES && section->record_count == 2 &&
                section->atom != 0 && section->atom < after->converted.link.atoms.count &&
                section->atom == after->converted.objects[section->object].section_atoms[section->section] &&
                foreign_unwind_dedup_section_offset(section, 0, &cie) && foreign_unwind_dedup_section_offset(section, 32, &fde) &&
                foreign_unwind_dedup_section_offset(section, 56, &end) && !foreign_unwind_dedup_section_offset(section, 57, &outside) &&
                cie == 0 && fde >= 32 && end == fde + 24 && end == section->endpoint && end <= after->converted.link.atoms.size[section->atom] &&
                section->records[0].kind == FOREIGN_UNWIND_CIE && section->records[0].new_offset == cie &&
                section->records[1].kind == FOREIGN_UNWIND_FDE && section->records[1].new_offset == fde;
        if (valid)
        {
            seen[starts[section->object] + section->section] = 1;
            // Derive the owner from the original ELF relocation, independently of every sidecar record/map.
            const ForeignObject* object = &foreign->objects[section->object];
            uint32_t matches = 0;
            Elf64_Rela relocation = { 0 };
            for (uint32_t r = 0; r < object->relocation_count; r += 1)
            {
                const Elf64_Shdr header = import_section_header(&object->elf, object->relocations[r]);
                if (header.sh_info == section->section)
                {
                    const String8 entries = import_section_bytes(&object->elf, &header);
                    valid = valid && entries.length == sizeof(relocation);
                    if (entries.length == sizeof(relocation))
                    {
                        memcpy(&relocation, entries.pointer, sizeof(relocation));
                    }
                    matches += 1;
                }
            }
            const uint64_t symbol = ELF64_R_SYM(relocation.r_info);
            valid = valid && matches == 1 && relocation.r_offset == 40 && ELF64_R_TYPE(relocation.r_info) == R_X86_64_PC32 && relocation.r_addend == 0 &&
                    symbol < object->symbols.count;
            const Elf64_Sym definition = valid ? import_symbol(&object->symbols, (uint32_t)symbol) : (Elf64_Sym){ 0 };
            uint32_t original_section = 0;
            valid = valid && import_symbol_section(&object->symbols, (uint32_t)symbol, &definition, &original_section) &&
                    original_section < before->objects[section->object].section_count;
            const AtomId code = valid ? after->converted.objects[section->object].section_atoms[original_section] : 0;
            valid = valid && code != 0 && code < after->converted.link.atoms.count &&
                    section->records[1].atom == code && section->records[1].section == original_section &&
                    after->converted.link.atoms.class[code] == BUSTER_LINK_CLASS_TEXT &&
                    memcmp(after->converted.link.atoms.bytes[section->atom] + cie, before->link.atoms.bytes[original], 32) == 0;
            if (valid)
            {
                const uint64_t address = BUSTER_EXECUTABLE_BASE + after->converted.link.atoms.address[section->atom] + fde;
                const uint64_t found = executable_test_fde_at(fdes, fde_count, address);
                const uint64_t pc = BUSTER_EXECUTABLE_BASE + after->converted.link.atoms.address[code] + definition.st_value;
                valid = found != UINT64_MAX && fdes[found].initial == (int64_t)pc && fdes[found].range == 6;
            }
        }
    }
    uint32_t covered = 0;
    for (uint32_t id = 1; valid && id < foreign->object_count; id += 1)
    {
        for (uint32_t section = 0; valid && section < before->objects[id].section_count; section += 1)
        {
            const AtomId atom = before->objects[id].section_atoms[section];
            if (atom != 0 && before->link.atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME)
            {
                valid = seen[starts[id] + section] == 1;
                covered += 1;
            }
        }
    }
    valid = valid && covered == input->count;
    arena_pop_to(arena, saved);
    return valid;
}

// This replay runs outside every measured sample, while the converted Link is alive. It uses the independent
// executable-test record walker and lookup rather than producer record offsets to locate each named leaf's FDE.
static bool foreign_unwind_dedup_benchmark_validate(const ForeignUnwindDedupBenchmarkInput* input, const Foreign* foreign, const ForeignLink* before,
                                                   const ForeignUnwindDedupLink* pooled, const ForeignLink* converted, const char* path,
                                                   const ExecutableResult* written, uint64_t removed, Arena* arena)
{
    const uint64_t saved = arena->length;
    const String8 bytes = foreign_test_read(arena, path);
    const ExecutableTestImage image = executable_test_image(bytes);
    const Elf64_Shdr* frames = executable_test_section(&image, ".eh_frame");
    const Elf64_Shdr* header = executable_test_section(&image, ".eh_frame_hdr");
    const uint64_t expected = (uint64_t)input->count * BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_FRAME_BYTES - removed;
    bool valid = frames != 0 && header != 0 && frames->sh_size == expected && header->sh_size == 12 + 8 * (uint64_t)input->count &&
                 written->fdes == input->count && executable_test_unwind(&converted->link, bytes, BUSTER_EXECUTABLE_BASE, 0, written) == 0;
    uint64_t* cies = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)input->count * 2 + 1);
    ExecutableTestFde* fdes = BUSTER_ARENA_PUSH_ARRAY(arena, ExecutableTestFde, input->count);
    const uint64_t count = valid && cies != 0 && fdes != 0 ? executable_test_walk_frames((const uint8_t*)bytes.pointer + frames->sh_offset, frames->sh_size,
                                                                                     frames->sh_addr, cies, fdes, input->count) : UINT64_MAX;
    valid = valid && count == input->count;
    for (uint32_t i = 0; valid && i < input->count; i += 1)
    {
        char name[40];
        snprintf(name, sizeof(name), "unwind_leaf_%u", i);
        const SymbolId symbol = foreign_link_test_symbol(foreign, converted, name);
        const AtomId atom = symbol != 0 ? converted->link.symbols.atom[symbol] : 0;
        const uint64_t pc = atom != 0 ? BUSTER_EXECUTABLE_BASE + converted->link.atoms.address[atom] + converted->link.symbols.value[symbol] : 0;
        const uint64_t found = executable_test_lookup((const uint8_t*)bytes.pointer + header->sh_offset + 12, input->count, header->sh_addr, (int64_t)pc);
        int32_t relative = 0;
        if (found != UINT64_MAX)
        {
            memcpy(&relative, bytes.pointer + header->sh_offset + 12 + 8 * found + 4, sizeof(relative));
        }
        const uint64_t fde = found != UINT64_MAX ? executable_test_fde_at(fdes, count, (uint64_t)((int64_t)header->sh_addr + relative)) : UINT64_MAX;
        valid = symbol != 0 && atom != 0 && fde != UINT64_MAX && fdes[fde].initial == (int64_t)pc && fdes[fde].range == 6;
        if (valid)
        {
            const uint64_t offset = fdes[fde].address - frames->sh_addr;
            uint32_t backlink = 0;
            valid = offset <= frames->sh_size && 24 <= frames->sh_size - offset;
            if (valid)
            {
                const uint8_t* record = (const uint8_t*)bytes.pointer + frames->sh_offset + offset;
                memcpy(&backlink, record + 4, sizeof(backlink));
                const uint64_t cie = offset + 4 >= backlink ? offset + 4 - backlink : UINT64_MAX;
                uint8_t expected_frame[BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_FRAME_BYTES];
                foreign_unwind_dedup_benchmark_frame(expected_frame, input->duplicate ? 1 : i + 1);
                // Only the backward CIE pointer and pc_begin may differ from the original fixed-size FDE.
                valid = cie <= frames->sh_size && 32 <= frames->sh_size - cie && memcmp(record, expected_frame + 32, 4) == 0 &&
                        memcmp(record + 12, expected_frame + 44, 12) == 0 &&
                        memcmp(bytes.pointer + frames->sh_offset + cie, expected_frame, 32) == 0;
            }
        }
    }
    if (valid && pooled != 0)
    {
        valid = foreign_unwind_dedup_benchmark_maps(input, foreign, before, pooled, fdes, count, arena);
    }
    // The independent map oracle must reject duplicate source coverage and a later pool member redirected to the
    // first member's otherwise valid FDE. Mutate only copied sidecars; the emitted bytes and producer stay intact.
    if (valid && pooled != 0 && input->count > 1)
    {
        ForeignUnwindDedupSection* sections = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignUnwindDedupSection, pooled->section_count);
        ForeignUnwindDedupLink corrupted = *pooled;
        valid = sections != 0;
        if (valid)
        {
            memcpy(sections, pooled->sections, (uint64_t)pooled->section_count * sizeof(*sections));
            corrupted.sections = sections;
            sections[1] = sections[0];
            valid = !foreign_unwind_dedup_benchmark_maps(input, foreign, before, &corrupted, fdes, count, arena);
            memcpy(sections, pooled->sections, (uint64_t)pooled->section_count * sizeof(*sections));
            bool changed = false;
            for (uint32_t s = 0; valid && !changed && s < pooled->section_count; s += 1)
            {
                if (sections[s].records[1].new_offset > 32)
                {
                    ForeignUnwindRecord records[2];
                    memcpy(records, sections[s].records, sizeof(records));
                    records[1].new_offset = 32;
                    sections[s].records = records;
                    sections[s].endpoint = 56;
                    valid = !foreign_unwind_dedup_benchmark_maps(input, foreign, before, &corrupted, fdes, count, arena);
                    changed = true;
                }
            }
            valid = valid && changed == input->duplicate;
        }
    }
    arena_pop_to(arena, saved);
    return valid;
}

static ForeignUnwindDedupBenchmarkSample foreign_unwind_dedup_benchmark_sample(const ForeignUnwindDedupBenchmarkInput* input,
                                                                             const ForeignUnwindDedupBenchmarkVariant* variant, const char* path,
                                                                             uint32_t after_scalar, Arena* validation)
{
    ForeignUnwindDedupBenchmarkSample sample = { 0 };
    const bool removed = unlink(path) == 0 || errno == ENOENT;
    const String8 destination = { .pointer = (char*)path, .length = strlen(path) };
    Foreign foreign = { 0 };
    ForeignLink converted = { 0 };
    ForeignUnwindDedupLink pooled = { 0 };
    foreign_unwind_dedup_benchmark_after_scalar(after_scalar);
    const uint64_t start_ticks = foreign_unwind_dedup_benchmark_ticks();
    const uint64_t start = foreign_now();
    Arena arena = arena_reserve(1ull << 32);
    Arena names = arena_reserve(1ull << 30);
    foreign_run(&foreign, &input->command, variant->path, 1, 0);
    const uint64_t resolved = foreign_now();
    sample.built = foreign_link_build(&converted, &foreign, &arena, &names);
    const uint64_t converted_at = foreign_now();
    ForeignLink* result = &converted;
    if (sample.built.error == FOREIGN_LINK_ERROR_NONE && variant->dedup)
    {
        const ForeignGcRoots roots = { .entry = foreign_link_test_symbol(&foreign, &converted, "_start") };
        sample.pooled = foreign_unwind_dedup_link(&pooled, &foreign, &converted, &roots, variant->path, &arena, &names);
        sample.fallback = sample.pooled.error == FOREIGN_LINK_ERROR_UNSUPPORTED;
        result = sample.pooled.error == FOREIGN_LINK_ERROR_NONE ? &pooled.converted : result;
    }
    const uint64_t pooled_at = foreign_now();
    const bool prepared = removed && arena.base != 0 && names.base != 0 && foreign.failure.error == FOREIGN_ERROR_NONE &&
                          sample.built.error == FOREIGN_LINK_ERROR_NONE && (sample.pooled.error == FOREIGN_LINK_ERROR_NONE || sample.fallback);
    if (prepared)
    {
        const SymbolId entry = foreign_link_test_symbol(&foreign, result, "_start");
        sample.valid = variant->path == FOREIGN_PATH_EXACT ? executable_test_write_exact(&result->link, entry, destination, 1, &sample.written) :
                                                           executable_test_write(&result->link, entry, destination, 1, &sample.written, 0, 0);
    }
    const uint64_t written_at = foreign_now();
    sample.resolve_error = (uint32_t)foreign.failure.error;
    sample.retained = arena.length + names.length + foreign.arena.length;
    for (uint32_t lane = 0; foreign.lane_states != 0 && lane < foreign.lanes; lane += 1)
    {
        sample.retained += foreign.lane_states[lane].arena.length;
    }
    sample.output_bytes = sample.written.file_size;
    sample.removed_bytes = pooled.plan.removed_bytes;
    sample.removed_cies = pooled.plan.removed_cies;
    sample.removed_atoms = pooled.plan.removed_atoms;
    // Validation deliberately occurs only on a separate untimed replay, never inside a reported sample.
    if (validation != 0 && sample.valid)
    {
        sample.valid = !sample.fallback && foreign_unwind_dedup_benchmark_validate(input, &foreign, &converted, variant->dedup ? &pooled : 0,
                                                                                 result, path, &sample.written, sample.removed_bytes, validation);
    }
    arena_release(&names);
    foreign_release(&foreign);
    arena_release(&arena);
    const uint64_t finished = foreign_now();
    const uint64_t finished_ticks = foreign_unwind_dedup_benchmark_ticks();
    sample.times[0] = resolved - start;
    sample.times[1] = converted_at - resolved;
    sample.times[2] = pooled_at - converted_at;
    sample.times[3] = written_at - pooled_at;
    sample.times[4] = finished - written_at;
    sample.times[5] = finished - start;
    sample.ticks = finished_ticks - start_ticks;
    return sample;
}

static bool foreign_unwind_dedup_benchmark_case(const char* directory, uint32_t count, bool duplicate, uint32_t runs, uint32_t after_scalar,
                                               const uint32_t* order, uint32_t variants, bool interleaved, Arena* arena)
{
    const uint64_t saved = arena->length;
    ForeignTestTally tally = { .check = "foreign unwind dedup pipeline validation" };
    ForeignUnwindDedupBenchmarkInput input = { 0 };
    bool valid = foreign_unwind_dedup_benchmark_generate(directory, count, duplicate, arena, &input);
    foreign_test_expect(&tally, valid, "generate frozen original CIE/FDE object files", 0);
    uint64_t* times = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)4 * BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_PHASES * runs);
    uint64_t* ticks = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)4 * runs);
    valid = valid && times != 0 && ticks != 0;
    String8 reference[2] = { 0 };
    ForeignUnwindDedupBenchmarkSample last[4] = { 0 };
    // Separate replay for every requested variant validates maps before any measurements. Warm-up follows it.
    const uint32_t samples = variants * (runs + 2);
    for (uint32_t step = 0; valid && step < samples; step += 1)
    {
        const bool replay = step < variants;
        const uint32_t at = replay ? step : step - variants;
        const uint32_t round = replay ? 0 : interleaved ? at / variants : at % (runs + 1);
        const uint32_t position = replay ? at : interleaved ? (at % variants + round % variants) % variants : at / (runs + 1);
        const uint32_t v = order[position];
        const ForeignUnwindDedupBenchmarkVariant* variant = &foreign_unwind_dedup_benchmark_variants[v];
        ForeignTestPath path;
        snprintf(path, sizeof(path), "%s/unwind_output_%u_%u_%u", directory, count, duplicate ? 1 : 0, v);
        const ForeignUnwindDedupBenchmarkSample sample = foreign_unwind_dedup_benchmark_sample(&input, variant, path, after_scalar, replay ? arena : 0);
        const uint32_t expected_removed = variant->dedup && duplicate ? count - 1 : 0;
        valid = sample.valid && !sample.fallback && sample.removed_cies == expected_removed && sample.removed_atoms == expected_removed &&
                sample.removed_bytes == (uint64_t)expected_removed * BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_CIE_BYTES;
        char detail[256];
        snprintf(detail, sizeof(detail), "%s count %u %s %s %u: resolve %u convert %u import %u dedup %u write %u fallback %u", duplicate ? "duplicate" : "unique",
                 count, variant->name, replay ? "validation" : "round", round, sample.resolve_error, (unsigned)sample.built.error,
                 (unsigned)sample.built.import.error, (unsigned)sample.pooled.error, (unsigned)sample.written.error, sample.fallback ? 1 : 0);
        foreign_test_expect(&tally, valid, "whole pipeline and expected pooling disposition", detail);
        const uint64_t validation = arena->length;
        if (valid)
        {
            const bool ran = executable_test_runs(path, (const uint8_t*)&input.expected, sizeof(input.expected));
            const String8 bytes = foreign_test_read(arena, path);
            const uint32_t state = duplicate && variant->dedup ? 1 : 0;
            const bool same = bytes.pointer != 0 && (reference[state].pointer == 0 ||
                              (bytes.length == reference[state].length && memcmp(bytes.pointer, reference[state].pointer, bytes.length) == 0));
            foreign_test_expect(&tally, ran, "all leaf calls yield the independent sum", detail);
            foreign_test_expect(&tally, same, duplicate ? "complete bytes agree across paths and repeats within pooling state" :
                                "zero-savings output bytes agree across paths and pooling states", detail);
            valid = ran && same;
            if (valid && !replay && round != 0)
            {
                for (uint32_t phase = 0; phase < BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_PHASES; phase += 1)
                {
                    times[((uint64_t)v * BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_PHASES + phase) * runs + round - 1] = sample.times[phase];
                }
                ticks[(uint64_t)v * runs + round - 1] = sample.ticks;
                last[v] = sample;
            }
        }
        arena_pop_to(arena, validation);
        const uint32_t state = duplicate && variant->dedup ? 1 : 0;
        if (valid && reference[state].pointer == 0)
        {
            reference[state] = foreign_test_read(arena, path);
            valid = reference[state].pointer != 0;
            foreign_test_expect(&tally, valid, "retain output oracle outside timing", detail);
        }
    }
    // Emit no timing rows for an incomplete case: every row covers every requested measured round.
    for (uint32_t position = 0; valid && position < variants; position += 1)
    {
        const uint32_t v = order[position];
        const ForeignUnwindDedupBenchmarkVariant* variant = &foreign_unwind_dedup_benchmark_variants[v];
        double medians[BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_PHASES];
        for (uint32_t phase = 0; phase < BUSTER_ARRAY_LENGTH(medians); phase += 1)
        {
            medians[phase] = foreign_link_test_median(times + ((uint64_t)v * BUSTER_ARRAY_LENGTH(medians) + phase) * runs, runs);
        }
        uint64_t* totals = times + ((uint64_t)v * BUSTER_ARRAY_LENGTH(medians) + 5) * runs;
        uint64_t* elapsed_ticks = ticks + (uint64_t)v * runs;
        qsort(elapsed_ticks, runs, sizeof(*elapsed_ticks), foreign_link_test_compare_time);
        const ForeignUnwindDedupBenchmarkSample* sample = &last[v];
        printf("foreign-unwind-dedup %s count %u objects %u %s runs %u schedule %s: resolve %.1f us convert %.1f us pool %.1f us write %.1f us cleanup %.1f us total %.1f us wall-min/median/max %llu/%llu/%llu ns TSCticks %llu after-scalar %u ms input %llu bytes output %llu bytes EH-input %llu bytes EH-output %llu bytes removed-CIEs %u removed-atoms %u removed-bytes %llu retained-arena %llu bytes fallback %u\n",
               duplicate ? "duplicate" : "unique", count, count + 1, variant->name, runs, interleaved ? "rotating-interleaved" : "blocked",
               medians[0], medians[1], medians[2], medians[3], medians[4], medians[5], (unsigned long long)totals[0],
               (unsigned long long)totals[runs / 2], (unsigned long long)totals[runs - 1], (unsigned long long)elapsed_ticks[runs / 2], after_scalar,
               (unsigned long long)input.input_bytes, (unsigned long long)sample->output_bytes,
               (unsigned long long)((uint64_t)count * BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_FRAME_BYTES),
               (unsigned long long)((uint64_t)count * BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_FRAME_BYTES - sample->removed_bytes),
               sample->removed_cies, sample->removed_atoms, (unsigned long long)sample->removed_bytes, (unsigned long long)sample->retained, sample->fallback ? 1 : 0);
    }
    foreign_test_expect(&tally, valid, "all requested variants completed", 0);
    const bool passed = foreign_test_report(&tally);
    arena_pop_to(arena, saved);
    return passed && valid;
}

static bool foreign_unwind_dedup_benchmark_counts(const char* text, uint32_t* counts, uint32_t* count)
{
    const char* at = text;
    uint32_t added = 0;
    bool valid = *at != 0;
    while (valid && *at != 0)
    {
        char* end = 0;
        const uint64_t value = strtoull(at, &end, 10);
        valid = *at >= '0' && *at <= '9' && value > 0 && value <= BUSTER_FOREIGN_UNWIND_DEDUP_BENCHMARK_MAX_UNITS && added < 16 && (*end == 0 || *end == ',');
        if (valid)
        {
            counts[added++] = (uint32_t)value;
            at = *end == ',' ? end + 1 : end;
            valid = *end != ',' || *at != 0;
        }
    }
    *count = valid ? added : 0;
    return valid;
}

static bool foreign_unwind_dedup_benchmark_order(const char* text, uint32_t* order, uint32_t* count)
{
    const char* at = text;
    uint32_t added = 0;
    uint32_t mask = 0;
    bool valid = *at != 0;
    while (valid && *at != 0)
    {
        const char* comma = strchr(at, ',');
        const uint64_t length = comma != 0 ? (uint64_t)(comma - at) : strlen(at);
        uint32_t found = BUSTER_ARRAY_LENGTH(foreign_unwind_dedup_benchmark_variants);
        for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(foreign_unwind_dedup_benchmark_variants); v += 1)
        {
            const char* name = foreign_unwind_dedup_benchmark_variants[v].name;
            if (strlen(name) == length && memcmp(at, name, length) == 0)
            {
                found = v;
            }
        }
        valid = found < BUSTER_ARRAY_LENGTH(foreign_unwind_dedup_benchmark_variants) && (mask & (1u << found)) == 0;
        if (valid)
        {
            order[added++] = found;
            mask |= 1u << found;
            at = comma != 0 ? comma + 1 : at + length;
            valid = comma == 0 || *at != 0;
        }
    }
    *count = valid ? added : 0;
    return valid;
}

static int run_foreign_unwind_dedup_benchmark(int argc, char** argv)
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
            valid = foreign_unwind_dedup_benchmark_counts(argv[i + 1], counts, &count);
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
            valid = foreign_unwind_dedup_benchmark_order(argv[i + 1], order, &variants);
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
            cases = strcmp(argv[i + 1], "duplicate") == 0 ? 1 : strcmp(argv[i + 1], "unique") == 0 ? 2 : 0;
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
    const int length = snprintf(directory, sizeof(directory), "%s/buster-foreign-unwind-dedup-XXXXXX", base);
    valid = valid && length > 0 && (uint64_t)length < sizeof(directory);
    Arena arena = arena_reserve(BUSTER_FOREIGN_TEST_ARENA_BYTES);
    bool passed = false;
    if (!valid)
    {
        printf("usage: benchmark-foreign-unwind-dedup [--counts LIST <= 16384] [--runs N] [--order no-dedup-exact,dedup-exact,no-dedup-vector,dedup-vector] [--schedule interleaved|blocked] [--only duplicate|unique] [--after-scalar MS <= 300] [--directory PATH] [--keep]\n");
    }
    else if (arena.base == 0 || mkdtemp(directory) == 0)
    {
        printf("benchmark-foreign-unwind-dedup: no room for the arena or output directory\n");
    }
    else
    {
        printf("foreign-unwind-dedup scope: fresh per-link arenas/maps through writer close and release; frozen-file warm cache; generated inputs, command, output unlink, scalar conditioning and validation excluded; retained bytes exclude maps/transient scratch and are not peak RSS; one lane; pin externally; pooling remains opt-in\n");
        printf("foreign-unwind-dedup order:");
        for (uint32_t v = 0; v < variants; v += 1)
        {
            printf(" %s", foreign_unwind_dedup_benchmark_variants[order[v]].name);
        }
        printf("\n");
        passed = true;
        for (uint32_t p = 0; passed && p < count; p += 1)
        {
            for (uint32_t c = 0; passed && c < 2; c += 1)
            {
                if ((cases & (1u << c)) != 0)
                {
                    passed = foreign_unwind_dedup_benchmark_case(directory, counts[p], c == 0, runs, after_scalar, order, variants, interleaved, &arena);
                }
            }
        }
        if (!keep && passed)
        {
            char* remove[] = { (char*)"rm", (char*)"-rf", directory, 0 };
            object_test_run(remove, "/dev/null", "/dev/null");
        }
        else
        {
            printf("benchmark-foreign-unwind-dedup: kept %s\n", directory);
        }
    }
    arena_release(&arena);
    return passed ? 0 : 1;
}

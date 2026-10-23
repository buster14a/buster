// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Black-box COMDAT -> optional GC -> CIE pooling -> literal pooling composition. Include after
// foreign_driver_test.c. Three original objects have two compatible CIEs, a losing COMDAT function, one dead
// function and two identical live literals. ABS64 pointer relocations keep literal pooling supported. Every
// option state executes; an independent ELF walk checks both transformations, then complete bytes must agree
// across exact/vector and 1/2/3/8 lanes. A pass that silently falls back or does nothing cannot satisfy the oracle.

static const ForeignDriverTestSource foreign_driver_pipeline_sources[] = {
    {
        .name = "fpipeline_first",
        .text =
            ".section .text.fpipeline_shared,\"axG\",@progbits,fpipeline_shared,comdat\n.weak fpipeline_shared\n.type fpipeline_shared,@function\n"
            "fpipeline_shared:\n.cfi_startproc\nmov $23,%eax\nret\n.cfi_endproc\n.size fpipeline_shared,.-fpipeline_shared\n"
            ".section .text.fpipeline_a,\"ax\",@progbits\n.globl fpipeline_a\n.type fpipeline_a,@function\n"
            "fpipeline_a:\n.cfi_startproc\nmov $11,%eax\nret\n.cfi_endproc\n.size fpipeline_a,.-fpipeline_a\n"
            ".section .rodata.fpipeline,\"aMS\",@progbits,1\n.globl fpipeline_string_a\n.type fpipeline_string_a,@object\n"
            "fpipeline_string_a:\n.asciz \"pipeline duplicate literal\"\n.size fpipeline_string_a,.-fpipeline_string_a\n"
            ".section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .name = "fpipeline_second",
        .text =
            ".section .text.fpipeline_shared,\"axG\",@progbits,fpipeline_shared,comdat\n.weak fpipeline_shared\n.type fpipeline_shared,@function\n"
            "fpipeline_shared:\n.cfi_startproc\nmov $99,%eax\nret\n.cfi_endproc\n.size fpipeline_shared,.-fpipeline_shared\n"
            ".section .text.fpipeline_dead,\"ax\",@progbits\n.globl fpipeline_dead\n.type fpipeline_dead,@function\n"
            "fpipeline_dead:\n.cfi_startproc\nmov $101,%eax\nret\n.cfi_endproc\n.size fpipeline_dead,.-fpipeline_dead\n"
            ".section .text.fpipeline_b,\"ax\",@progbits\n.globl fpipeline_b\n.type fpipeline_b,@function\n"
            "fpipeline_b:\n.cfi_startproc\nmov $8,%eax\nret\n.cfi_endproc\n.size fpipeline_b,.-fpipeline_b\n"
            ".section .rodata.fpipeline,\"aMS\",@progbits,1\n.globl fpipeline_string_b\n.type fpipeline_string_b,@object\n"
            "fpipeline_string_b:\n.asciz \"pipeline duplicate literal\"\n.size fpipeline_string_b,.-fpipeline_string_b\n"
            ".section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .name = "fpipeline_entry",
        .text =
            ".text\n.globl _start\n.type _start,@function\n_start:\ncall fpipeline_shared\nmov %eax,%ebx\n"
            "call fpipeline_a\nadd %eax,%ebx\ncall fpipeline_b\nadd %eax,%ebx\ncmp $42,%ebx\njne .Lfail\n"
            "lea fpipeline_pointers(%rip),%r8\nmov (%r8),%rsi\nmov 8(%r8),%rdi\nxor %ecx,%ecx\nxor %edx,%edx\n"
            ".Lbyte:\nmovzbl (%rsi,%rcx),%eax\ncmpb (%rdi,%rcx),%al\njne .Lfail\nadd %eax,%edx\ninc %ecx\n"
            "test %al,%al\njnz .Lbyte\ncmp $27,%ecx\njne .Lfail\ncmp $2622,%edx\njne .Lfail\n"
            "mov $42,%edi\njmp .Lexit\n.Lfail:\nmov $101,%edi\n.Lexit:\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
            ".data\n.balign 8\nfpipeline_pointers:\n.quad fpipeline_string_a,fpipeline_string_b\n"
            ".section .note.GNU-stack,\"\",@progbits\n",
    },
};

typedef struct ForeignDriverPipelineImage ForeignDriverPipelineImage;
struct ForeignDriverPipelineImage
{
    uint64_t eh_bytes;
    uint64_t cie_bytes;
    uint64_t rodata_bytes;
    uint64_t text_bytes;
    bool valid;
};

static bool foreign_driver_pipeline_compile(ForeignTestTally* tally, const char* directory, ForeignTestPath* objects)
{
    bool ready = true;
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(foreign_driver_pipeline_sources); i += 1)
    {
        const ForeignDriverTestSource* fixture = &foreign_driver_pipeline_sources[i];
        ForeignTestPath source;
        ForeignTestPath output;
        ForeignTestPath errors;
        snprintf(source, sizeof(source), "%s/%s.s", directory, fixture->name);
        snprintf(objects[i], sizeof(ForeignTestPath), "%s/%s.o", directory, fixture->name);
        snprintf(output, sizeof(output), "%s/%s.compile.out", directory, fixture->name);
        snprintf(errors, sizeof(errors), "%s/%s.compile.err", directory, fixture->name);
        char* arguments[] = {
            (char*)"cc",
            (char*)"-c",
            source,
            (char*)"-o",
            objects[i],
            0,
        };
        ready = foreign_test_write(source, fixture->text, strlen(fixture->text)) && object_test_run(arguments, output, errors) == 0;
        foreign_test_expect(tally, ready, "assemble original combined-pass fixture", fixture->name);
    }
    return ready;
}

// Copy the five required section headers only after validating the table and every name. The generated ELF is
// still untrusted test output: bad offsets or unterminated names must fail the assertion, not escape its bounds.
static bool foreign_driver_pipeline_sections(String8 bytes, Elf64_Shdr* sections, Elf64_Phdr* unwind)
{
    static const char* names[] = { ".eh_frame", ".eh_frame_hdr", ".rodata", ".text", ".data" };
    Elf64_Ehdr elf = { 0 };
    bool valid = bytes.pointer != 0 && bytes.length >= sizeof(elf);
    if (valid)
    {
        memcpy(&elf, bytes.pointer, sizeof(elf));
        valid = memcmp(elf.e_ident, ELFMAG, SELFMAG) == 0 && elf.e_ident[EI_CLASS] == ELFCLASS64 && elf.e_ident[EI_DATA] == ELFDATA2LSB &&
                elf.e_type == ET_EXEC && elf.e_machine == EM_X86_64 && elf.e_shentsize == sizeof(Elf64_Shdr) && elf.e_shnum != 0 &&
                elf.e_shstrndx < elf.e_shnum && elf.e_shoff <= bytes.length && elf.e_shnum <= (bytes.length - elf.e_shoff) / sizeof(Elf64_Shdr) &&
                elf.e_phentsize == sizeof(Elf64_Phdr) && elf.e_phoff <= bytes.length && elf.e_phnum <= (bytes.length - elf.e_phoff) / sizeof(Elf64_Phdr);
    }
    Elf64_Shdr strings = { 0 };
    if (valid)
    {
        memcpy(&strings, bytes.pointer + elf.e_shoff + (uint64_t)elf.e_shstrndx * sizeof(strings), sizeof(strings));
        valid = strings.sh_type == SHT_STRTAB && strings.sh_offset <= bytes.length && strings.sh_size <= bytes.length - strings.sh_offset;
    }
    uint32_t found = 0;
    for (uint32_t i = 0; valid && i < elf.e_shnum; i += 1)
    {
        Elf64_Shdr section;
        memcpy(&section, bytes.pointer + elf.e_shoff + (uint64_t)i * sizeof(section), sizeof(section));
        valid = section.sh_name < strings.sh_size;
        const char* name = valid ? bytes.pointer + strings.sh_offset + section.sh_name : 0;
        valid = valid && memchr(name, 0, strings.sh_size - section.sh_name) != 0;
        for (uint32_t n = 0; valid && n < BUSTER_ARRAY_LENGTH(names); n += 1)
        {
            if (strcmp(name, names[n]) == 0)
            {
                valid = (found & (1u << n)) == 0 && section.sh_type == SHT_PROGBITS && (section.sh_flags & SHF_ALLOC) != 0 && section.sh_size != 0 &&
                        section.sh_offset <= bytes.length && section.sh_size <= bytes.length - section.sh_offset &&
                        section.sh_addr <= INT64_MAX - INT32_MAX && section.sh_size <= INT64_MAX - INT32_MAX - section.sh_addr;
                sections[n] = section;
                found |= 1u << n;
            }
        }
    }
    uint32_t segments = 0;
    for (uint32_t i = 0; valid && i < elf.e_phnum; i += 1)
    {
        Elf64_Phdr segment;
        memcpy(&segment, bytes.pointer + elf.e_phoff + (uint64_t)i * sizeof(segment), sizeof(segment));
        if (segment.p_type == PT_GNU_EH_FRAME)
        {
            *unwind = segment;
            segments += 1;
        }
    }
    return valid && found == (1u << BUSTER_ARRAY_LENGTH(names)) - 1 && segments == 1;
}

static ForeignDriverPipelineImage foreign_driver_pipeline_image(ForeignTestTally* tally, String8 bytes, bool gc, bool dedup, bool strings, const char* name)
{
    static const char literal[] = "pipeline duplicate literal";
    Elf64_Shdr sections[5] = { 0 };
    Elf64_Phdr unwind = { 0 };
    const bool bounded = foreign_driver_pipeline_sections(bytes, sections, &unwind);
    const Elf64_Shdr* frames = &sections[0];
    const Elf64_Shdr* header = &sections[1];
    const Elf64_Shdr* rodata = &sections[2];
    const Elf64_Shdr* text = &sections[3];
    const Elf64_Shdr* data = &sections[4];
    foreign_test_expect(tally, bounded, "combined output has bounded EH, literal and code sections", name);
    ForeignDriverPipelineImage result = { 0 };
    if (bounded)
    {
        result.eh_bytes = frames->sh_size;
        result.rodata_bytes = rodata->sh_size;
        result.text_bytes = text->sh_size;
        uint64_t cies[8] = { 0 };
        ExecutableTestFde fdes[8] = { 0 };
        const uint64_t fde_count = executable_test_walk_frames((const uint8_t*)bytes.pointer + frames->sh_offset, frames->sh_size, frames->sh_addr, cies, fdes, BUSTER_ARRAY_LENGTH(fdes));
        uint32_t cie_count = 0;
        uint64_t offset = 0;
        bool records = fde_count == (gc ? 3u : 4u);
        while (records && offset < frames->sh_size)
        {
            uint32_t length = 0;
            records = frames->sh_size - offset >= 8;
            if (records)
            {
                uint32_t id = 0;
                memcpy(&length, bytes.pointer + frames->sh_offset + offset, sizeof(length));
                memcpy(&id, bytes.pointer + frames->sh_offset + offset + 4, sizeof(id));
                records = length >= 4 && length <= frames->sh_size - offset - 4;
                if (records && id == 0)
                {
                    cie_count += 1;
                    result.cie_bytes += 4ull + length;
                }
            }
            offset += 4ull + length;
        }
        records = records && offset == frames->sh_size && cie_count == (dedup ? 1u : 2u);
        foreign_test_expect(tally, records, "COMDAT/GC keep the expected FDEs and CIE pooling removes one CIE", name);

        // The four original functions are exactly mov imm32,%eax; ret. Distinct immediates identify each owner
        // without asking the converter for its maps; the losing COMDAT's 99 is never an allowed owner.
        static const uint32_t expected[] = { 23, 11, 8, 101 };
        uint32_t owners = 0;
        bool functions = records && text->sh_size >= 6;
        for (uint64_t i = 0; functions && i < fde_count; i += 1)
        {
            functions = fdes[i].initial >= 0 && (uint64_t)fdes[i].initial >= text->sh_addr && fdes[i].range == 6 &&
                        (uint64_t)fdes[i].initial - text->sh_addr <= text->sh_size - 6;
            if (functions)
            {
                const uint8_t* code = (const uint8_t*)bytes.pointer + text->sh_offset + (uint64_t)fdes[i].initial - text->sh_addr;
                uint32_t immediate = 0;
                memcpy(&immediate, code + 1, sizeof(immediate));
                uint32_t matched = 0;
                for (uint32_t e = 0; e < BUSTER_ARRAY_LENGTH(expected); e += 1)
                {
                    matched |= immediate == expected[e] ? 1u << e : 0;
                }
                functions = code[0] == 0xb8 && code[5] == 0xc3 && matched != 0 && (owners & matched) == 0;
                owners |= matched;
            }
        }
        functions = functions && owners == (gc ? 7u : 15u);
        foreign_test_expect(tally, functions, "each retained original function has exactly one frame with its exact address and range", name);

        const uint8_t* index = (const uint8_t*)bytes.pointer + header->sh_offset;
        bool indexed = records && header->sh_size >= 12 && index[0] == 1 && index[1] == 0x1b && index[2] == 0x03 && index[3] == 0x3b &&
                       unwind.p_offset == header->sh_offset && unwind.p_vaddr == header->sh_addr && unwind.p_filesz == header->sh_size &&
                       unwind.p_memsz == header->sh_size && unwind.p_flags == PF_R && unwind.p_align == 4;
        if (indexed)
        {
            int32_t frame_pointer = 0;
            uint32_t count = 0;
            memcpy(&frame_pointer, index + 4, sizeof(frame_pointer));
            memcpy(&count, index + 8, sizeof(count));
            indexed = (int64_t)header->sh_addr + 4 + frame_pointer == (int64_t)frames->sh_addr && count == fde_count && header->sh_size == 12 + 8ull * count;
        }
        uint32_t seen = 0;
        int32_t last = INT32_MIN;
        for (uint64_t i = 0; indexed && i < fde_count; i += 1)
        {
            int32_t entry[2];
            memcpy(entry, index + 12 + 8 * i, sizeof(entry));
            const uint64_t fde = executable_test_fde_at(fdes, fde_count, (uint64_t)((int64_t)header->sh_addr + entry[1]));
            indexed = entry[0] > last && fde < fde_count && fdes[fde].initial == (int64_t)header->sh_addr + entry[0] && (seen & (1u << fde)) == 0;
            if (indexed)
            {
                seen |= 1u << fde;
                last = entry[0];
                for (uint32_t end = 0; indexed && end < 2; end += 1)
                {
                    indexed = executable_test_lookup(index + 12, fde_count, header->sh_addr, fdes[fde].initial + (end != 0 ? 5 : 0)) == i;
                }
            }
        }
        foreign_test_expect(tally, indexed, "EH header indexes each exact FDE once and finds both ends of every function", name);

        uint64_t pointers[2] = { 0 };
        bool addresses = data->sh_size == sizeof(pointers) && rodata->sh_size >= sizeof(literal);
        if (addresses)
        {
            memcpy(pointers, bytes.pointer + data->sh_offset, sizeof(pointers));
        }
        for (uint32_t i = 0; addresses && i < BUSTER_ARRAY_LENGTH(pointers); i += 1)
        {
            addresses = pointers[i] >= rodata->sh_addr && pointers[i] - rodata->sh_addr <= rodata->sh_size - sizeof(literal);
            if (addresses)
            {
                addresses = memcmp(bytes.pointer + rodata->sh_offset + pointers[i] - rodata->sh_addr, literal, sizeof(literal)) == 0;
            }
        }
        addresses = addresses && (strings ? pointers[0] == pointers[1] : pointers[0] != pointers[1]) && rodata->sh_size == sizeof(literal) * (strings ? 1u : 2u);
        foreign_test_expect(tally, addresses, "ABS64 references preserve exact literal bytes and fold only when enabled", name);
        result.valid = records && functions && indexed && addresses;
    }
    return result;
}

static bool foreign_driver_pipeline_test(const char* directory, Arena* arena)
{
    const uint64_t saved = arena->length;
    ForeignTestTally tally = { .check = "foreign command combined pooling pipeline" };
    ForeignTestPath objects[BUSTER_ARRAY_LENGTH(foreign_driver_pipeline_sources)];
    const bool ready = foreign_driver_pipeline_compile(&tally, directory, objects);
    ForeignDriverPipelineImage controls[2][4] = { 0 };
    for (uint32_t gc = 0; ready && gc < 2; gc += 1)
    {
        for (uint32_t state = 0; state < 4; state += 1)
        {
            const bool dedup = (state & 1) != 0;
            const bool strings = (state & 2) != 0;
            String8 reference = { 0 };
            for (uint32_t path = 0; path < 2; path += 1)
            {
                for (uint32_t lane = 0; lane < BUSTER_ARRAY_LENGTH(foreign_test_lanes); lane += 1)
                {
                    char name[80];
                    char lanes[16];
                    snprintf(name, sizeof(name), "pipeline_gc%u_state%u_path%u_lanes%u", gc, state, path, foreign_test_lanes[lane]);
                    snprintf(lanes, sizeof(lanes), "%u", foreign_test_lanes[lane]);
                    char* arguments[16];
                    uint32_t count = 0;
                    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(objects); i += 1)
                    {
                        arguments[count++] = objects[i];
                    }
                    arguments[count++] = (char*)(gc != 0 ? "--gc-sections" : "--no-gc-sections");
                    arguments[count++] = (char*)(dedup ? "--dedup-unwind" : "--no-dedup-unwind");
                    arguments[count++] = (char*)(strings ? "--merge-strings" : "--no-merge-strings");
                    arguments[count++] = (char*)"--lanes";
                    arguments[count++] = lanes;
                    if (path != 0)
                    {
                        arguments[count++] = (char*)"--scalar";
                    }
                    const String8 bytes = foreign_driver_test_invoke(&tally, directory, name, "-o", arguments, count, true, 42, arena);
                    const ForeignDriverPipelineImage inspected = foreign_driver_pipeline_image(&tally, bytes, gc != 0, dedup, strings, name);
                    if (path == 0 && lane == 0)
                    {
                        reference = bytes;
                        controls[gc][state] = inspected;
                    }
                    else
                    {
                        foreign_test_expect(&tally, foreign_driver_test_same(reference, bytes), "complete combined-state output agrees across paths and lane counts", name);
                    }
                }
            }
        }
        const ForeignDriverPipelineImage* state = controls[gc];
        const bool valid = state[0].valid && state[1].valid && state[2].valid && state[3].valid;
        const bool eh = valid && state[0].eh_bytes > state[1].eh_bytes && state[0].eh_bytes == state[2].eh_bytes &&
                        state[1].eh_bytes == state[3].eh_bytes && state[0].cie_bytes > state[1].cie_bytes &&
                        state[0].eh_bytes - state[1].eh_bytes == state[0].cie_bytes - state[1].cie_bytes;
        const bool literals = valid && state[0].rodata_bytes == state[1].rodata_bytes && state[2].rodata_bytes == state[3].rodata_bytes &&
                              state[0].rodata_bytes == 2 * state[3].rodata_bytes;
        foreign_test_expect(&tally, eh && literals, "combined pipeline independently preserves both single-pass savings", gc != 0 ? "GC enabled" : "GC disabled");
    }
    for (uint32_t state = 0; ready && state < 4; state += 1)
    {
        foreign_test_expect(&tally, controls[0][state].valid && controls[1][state].valid && controls[1][state].text_bytes < controls[0][state].text_bytes &&
                                  controls[1][state].eh_bytes < controls[0][state].eh_bytes,
                            "GC removes the dead function and its frame in every pooling state", 0);
    }
    arena_pop_to(arena, saved);
    return foreign_test_report(&tally);
}

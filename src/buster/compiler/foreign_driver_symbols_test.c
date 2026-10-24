// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Named-symbol selection's guarded count/fill contract and black-box command output. Include from the driver test
// after its file/process helpers. ELF parsing below checks every extent/name before use. Original fixtures exercise
// metadata after final maps, stripping without losing dynamic imports, and the writer's zero-sized TLS refusal.

typedef struct ForeignDriverSymbolsImage ForeignDriverSymbolsImage;
struct ForeignDriverSymbolsImage
{
    String8 bytes;
    Elf64_Ehdr elf;
    Elf64_Shdr section_names;
    bool valid;
};

static ForeignDriverSymbolsImage foreign_driver_symbols_image(String8 bytes)
{
    ForeignDriverSymbolsImage image = { .bytes = bytes };
    image.valid = bytes.pointer != 0 && bytes.length >= sizeof(image.elf);
    if (image.valid)
    {
        memcpy(&image.elf, bytes.pointer, sizeof(image.elf));
        const Elf64_Ehdr* elf = &image.elf;
        image.valid = memcmp(elf->e_ident, ELFMAG, SELFMAG) == 0 && elf->e_ident[EI_CLASS] == ELFCLASS64 && elf->e_ident[EI_DATA] == ELFDATA2LSB &&
                      (elf->e_type == ET_EXEC || elf->e_type == ET_DYN) && elf->e_machine == EM_X86_64 && elf->e_shentsize == sizeof(Elf64_Shdr) &&
                      elf->e_shnum != 0 && elf->e_shstrndx < elf->e_shnum && elf->e_shoff <= bytes.length &&
                      elf->e_shnum <= (bytes.length - elf->e_shoff) / sizeof(Elf64_Shdr) && elf->e_phentsize == sizeof(Elf64_Phdr) &&
                      elf->e_phoff <= bytes.length && elf->e_phnum <= (bytes.length - elf->e_phoff) / sizeof(Elf64_Phdr);
        if (image.valid)
        {
            memcpy(&image.section_names, bytes.pointer + elf->e_shoff + (uint64_t)elf->e_shstrndx * sizeof(Elf64_Shdr), sizeof(Elf64_Shdr));
            image.valid = image.section_names.sh_type == SHT_STRTAB && image.section_names.sh_offset <= bytes.length &&
                          image.section_names.sh_size <= bytes.length - image.section_names.sh_offset;
        }
    }
    for (uint32_t i = 0; image.valid && i < image.elf.e_shnum; i += 1)
    {
        Elf64_Shdr section;
        memcpy(&section, bytes.pointer + image.elf.e_shoff + (uint64_t)i * sizeof(section), sizeof(section));
        image.valid = section.sh_name < image.section_names.sh_size &&
                      memchr(bytes.pointer + image.section_names.sh_offset + section.sh_name, 0, image.section_names.sh_size - section.sh_name) != 0 &&
                      (section.sh_type == SHT_NOBITS || (section.sh_offset <= bytes.length && section.sh_size <= bytes.length - section.sh_offset));
    }
    return image;
}

static uint32_t foreign_driver_symbols_section(const ForeignDriverSymbolsImage* image, const char* name, Elf64_Shdr* output)
{
    uint32_t found = 0;
    for (uint32_t i = 1; image->valid && i < image->elf.e_shnum; i += 1)
    {
        Elf64_Shdr section;
        memcpy(&section, image->bytes.pointer + image->elf.e_shoff + (uint64_t)i * sizeof(section), sizeof(section));
        if (strcmp(image->bytes.pointer + image->section_names.sh_offset + section.sh_name, name) == 0)
        {
            found = found == 0 ? i : UINT32_MAX;
            *output = section;
        }
    }
    return found;
}

typedef struct ForeignDriverSymbolsTable ForeignDriverSymbolsTable;
struct ForeignDriverSymbolsTable
{
    ForeignDriverSymbolsImage image;
    Elf64_Shdr table;
    Elf64_Shdr strings;
    uint32_t count;
    bool valid;
};

static ForeignDriverSymbolsTable foreign_driver_symbols_table(String8 bytes)
{
    ForeignDriverSymbolsTable result = { .image = foreign_driver_symbols_image(bytes) };
    const uint32_t table = foreign_driver_symbols_section(&result.image, ".symtab", &result.table);
    const uint32_t strings = foreign_driver_symbols_section(&result.image, ".strtab", &result.strings);
    result.valid = table != 0 && table != UINT32_MAX && strings != 0 && strings != UINT32_MAX && result.table.sh_type == SHT_SYMTAB &&
                   result.table.sh_link == strings && result.table.sh_entsize == sizeof(Elf64_Sym) && result.table.sh_size % sizeof(Elf64_Sym) == 0 &&
                   result.table.sh_size / sizeof(Elf64_Sym) <= UINT32_MAX && result.table.sh_flags == 0 && result.table.sh_addr == 0 &&
                   result.strings.sh_type == SHT_STRTAB && result.strings.sh_flags == 0 && result.strings.sh_addr == 0 && result.strings.sh_size != 0;
    if (result.valid)
    {
        result.count = (uint32_t)(result.table.sh_size / sizeof(Elf64_Sym));
        result.valid = result.count != 0 && result.table.sh_info != 0 && result.table.sh_info <= result.count && bytes.pointer[result.strings.sh_offset] == 0;
    }
    for (uint32_t i = 0; result.valid && i < result.count; i += 1)
    {
        Elf64_Sym symbol;
        memcpy(&symbol, bytes.pointer + result.table.sh_offset + (uint64_t)i * sizeof(symbol), sizeof(symbol));
        if (i == 0)
        {
            const Elf64_Sym zero = { 0 };
            result.valid = memcmp(&symbol, &zero, sizeof(symbol)) == 0;
        }
        else
        {
            result.valid = symbol.st_name != 0 && symbol.st_name < result.strings.sh_size && bytes.pointer[result.strings.sh_offset + symbol.st_name] != 0 &&
                           memchr(bytes.pointer + result.strings.sh_offset + symbol.st_name, 0, result.strings.sh_size - symbol.st_name) != 0 &&
                           ELF64_ST_TYPE(symbol.st_info) != STT_FILE && ELF64_ST_TYPE(symbol.st_info) != STT_SECTION &&
                           ((i < result.table.sh_info) == (ELF64_ST_BIND(symbol.st_info) == STB_LOCAL)) &&
                           (symbol.st_shndx == SHN_UNDEF || symbol.st_shndx == SHN_ABS || symbol.st_shndx < result.image.elf.e_shnum);
        }
    }
    return result;
}

static uint32_t foreign_driver_symbols_find(const ForeignDriverSymbolsTable* table, const char* name, Elf64_Sym* output)
{
    uint32_t found = 0;
    for (uint32_t i = 1; table->valid && i < table->count; i += 1)
    {
        Elf64_Sym symbol;
        memcpy(&symbol, table->image.bytes.pointer + table->table.sh_offset + (uint64_t)i * sizeof(symbol), sizeof(symbol));
        if (strcmp(table->image.bytes.pointer + table->strings.sh_offset + symbol.st_name, name) == 0)
        {
            found += 1;
            *output = symbol;
        }
    }
    return found;
}

// The selection's two paths on name and flags columns that end at a PROT_NONE page, for every length to 65 and every
// start, over names that each end at a PROT_NONE page of their own: 16 of them, of 1 to 16 bytes, so that each start in an
// aligned eight-byte block occurs twice, spelled as temporaries (.L...) or near misses ("x", ".", ".l", "L.", ".M"), and
// symbols local or global by pattern. A read past a column or a name faults.
static void foreign_driver_symbols_guarded(ForeignTestTally* tally)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* memory = mmap(0, 6 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* strings = mmap(0, 32 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool mapped = memory != MAP_FAILED && strings != MAP_FAILED && mprotect(memory + page, page, PROT_NONE) == 0 && mprotect(memory + 3 * page, page, PROT_NONE) == 0 &&
                  mprotect(memory + 5 * page, page, PROT_NONE) == 0;
    // Name k (1 to 16) ends at the guard after its page: its bytes are a temporary's for odd k, a near miss's otherwise.
    const uint8_t* bytes[17] = { 0 };
    uint32_t lengths[17] = { 0 };
    static const char* const misses[] = { "x", ".", ".l", "L.", ".M" };
    for (uint32_t k = 1; mapped && k <= 16; k += 1)
    {
        uint8_t* end = strings + (2 * (uint64_t)k - 1) * page;
        mapped = mprotect(end, page, PROT_NONE) == 0;
        uint8_t* name = end - k;
        for (uint32_t i = 0; i < k; i += 1)
        {
            name[i] = (uint8_t)('a' + i % 26);
        }
        const char* spelling = k % 2 == 1 && k >= 2 ? ".L" : misses[k % BUSTER_ARRAY_LENGTH(misses)];
        memcpy(name, spelling, strlen(spelling) < k ? strlen(spelling) : k);
        bytes[k] = name;
        lengths[k] = k;
    }
    foreign_test_expect(tally, mapped, "map guarded final names, flags, name bytes and exactly sized ID output", 0);
    for (uint32_t length = 0; mapped && length <= 65; length += 1)
    {
        NameId* names = (NameId*)(memory + page) - length;
        uint8_t* flags = memory + 3 * page - length;
        for (uint32_t pattern = 0; pattern < 4; pattern += 1)
        {
            for (uint32_t i = 0; i < length; i += 1)
            {
                names[i] = pattern == 0 ? 0 : pattern == 1 || i % 3 != 0 ? 1 + (i + pattern) % 16 : 0;
                flags[i] = (uint8_t)(pattern == 3 && i % 2 == 0 ? BUSTER_LINK_SYMBOL_GLOBAL : BUSTER_LINK_SYMBOL_LOCAL);
            }
            const Link link = {
                .names = { .bytes = bytes, .length = lengths, .count = 17 },
                .symbols = { .name = names, .flags = flags, .count = length },
            };
            for (uint32_t begin = 0; begin <= length; begin += 1)
            {
                const Range64 range = { .begin = begin, .end = length };
                SymbolId expected[65];
                uint32_t count = 0;
                for (uint32_t i = begin; i < length; i += 1)
                {
                    const bool temporary = flags[i] == BUSTER_LINK_SYMBOL_LOCAL && lengths[names[i]] >= 2 && bytes[names[i]][0] == '.' && bytes[names[i]][1] == 'L';
                    if (names[i] != 0 && !temporary)
                    {
                        expected[count++] = i;
                    }
                }
                SymbolId* ids = (SymbolId*)(memory + 5 * page) - count;
                bool same = foreign_driver_symbols_exact(&link, range, 0) == count && foreign_driver_symbols_vector(&link, range, 0) == count;
                for (uint32_t path = 0; same && path < 2; path += 1)
                {
                    memset(ids, 0xcc, count * sizeof(SymbolId));
                    const uint32_t filled = path == 0 ? foreign_driver_symbols_exact(&link, range, ids) : foreign_driver_symbols_vector(&link, range, ids);
                    same = filled == count && memcmp(ids, expected, count * sizeof(SymbolId)) == 0;
                }
                foreign_test_expect(tally, same, "guarded exact/vector counts and compact ordered IDs without temporaries", 0);
            }
        }
    }
    if (memory != MAP_FAILED)
    {
        munmap(memory, 6 * page);
    }
    if (strings != MAP_FAILED)
    {
        munmap(strings, 32 * page);
    }
}

static void foreign_driver_symbols_select_lane(void* argument)
{
    foreign_driver_symbols(argument);
}

static void foreign_driver_symbols_capacity(ForeignTestTally* tally)
{
    NameId names[] = { 0, 1, 0, 2 };
    uint8_t flags[] = { 0, BUSTER_LINK_SYMBOL_GLOBAL, 0, BUSTER_LINK_SYMBOL_GLOBAL };
    // These selection-only inputs provide the valid producer's complete name and flags columns, including symbol zero;
    // the named symbols are global, so no name's bytes are read.
    for (uint32_t empty = 0; empty < 2; empty += 1)
    {
        Link link = { .symbols = { .name = names, .flags = flags, .count = empty != 0 ? 1 : BUSTER_ARRAY_LENGTH(names) } };
        for (uint32_t path = 0; path < 2; path += 1)
        {
            for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(foreign_test_lanes); l += 1)
            {
                Arena arena = { 0 };
                ForeignDriverWrite write = { .link = &link, .arena = &arena, .options = { .exact = path != 0 }, .symbols_ready = true };
                const bool ran = lane_run(foreign_test_lanes[l], foreign_driver_symbols_select_lane, &write, 0);
                const bool valid = empty != 0 ? write.symbols_ready && write.result.error == EXECUTABLE_ERROR_NONE && write.symbols.count == 0 && write.symbols.ids == 0 :
                                               !write.symbols_ready && write.result.error == EXECUTABLE_ERROR_MEMORY;
                foreign_test_expect(tally, ran && valid && arena.length == 0,
                                    "empty selection allocates nothing; allocation failure converges before writer on every lane", 0);
            }
        }
    }
}

static bool foreign_driver_symbols_compile(ForeignTestTally* tally, const char* directory, const char* name, const char* text, ForeignTestPath object)
{
    ForeignTestPath source;
    ForeignTestPath output;
    ForeignTestPath errors;
    snprintf(source, sizeof(source), "%s/%s.S", directory, name);
    snprintf(object, BUSTER_FOREIGN_TEST_PATH_BYTES, "%s/%s.o", directory, name);
    snprintf(output, sizeof(output), "%s/%s.compile.out", directory, name);
    snprintf(errors, sizeof(errors), "%s/%s.compile.err", directory, name);
    char* arguments[] = { (char*)"cc", (char*)"-c", source, (char*)"-o", object, 0 };
    const bool ready = foreign_test_write(source, text, strlen(text)) && object_test_run(arguments, output, errors) == 0;
    foreign_test_expect(tally, ready, "assemble original regular-symbol fixture", name);
    return ready;
}

static void foreign_driver_symbols_metadata(ForeignTestTally* tally, String8 bytes, const char* name)
{
    const ForeignDriverSymbolsTable table = foreign_driver_symbols_table(bytes);
    foreign_test_expect(tally, table.valid && table.count == 12 && table.table.sh_info == 3, "only named semantic symbols, null row and locals first", name);
    static const char* names[] = { "_start", "symbols_local", "symbols_global", "symbols_weak", "symbols_tls", "symbols_tbss", "symbols_abs", "symbols_ifunc", "symbols_undefined", "symbols_refs", "_end" };
    static const uint64_t sizes[] = { 12, 8, 4, 6, 4, 8, 0, 8, 0, 16, 0 };
    static const uint8_t info[] = { ELF64_ST_INFO(STB_GLOBAL, STT_FUNC), STT_OBJECT, ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT), ELF64_ST_INFO(STB_WEAK, STT_FUNC),
                                    ELF64_ST_INFO(STB_GLOBAL, STT_TLS), ELF64_ST_INFO(STB_WEAK, STT_TLS), ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE),
                                    ELF64_ST_INFO(STB_GLOBAL, STT_GNU_IFUNC), ELF64_ST_INFO(STB_WEAK, STT_FUNC), STT_OBJECT, ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE) };
    Elf64_Shdr tdata = { 0 };
    Elf64_Shdr data = { 0 };
    Elf64_Shdr text = { 0 };
    const uint32_t tls_index = foreign_driver_symbols_section(&table.image, ".tdata", &tdata);
    const uint32_t data_index = foreign_driver_symbols_section(&table.image, ".data", &data);
    const uint32_t text_index = foreign_driver_symbols_section(&table.image, ".text", &text);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        Elf64_Sym symbol = { 0 };
        bool valid = foreign_driver_symbols_find(&table, names[i], &symbol) == 1 && symbol.st_size == sizes[i] && symbol.st_info == info[i] &&
                     symbol.st_other == (i == 1 ? STV_HIDDEN : i == 2 ? STV_PROTECTED : STV_DEFAULT);
        if (valid && i == 6)
        {
            valid = symbol.st_shndx == SHN_ABS && symbol.st_value == 42;
        }
        else if (valid && i == 8)
        {
            valid = symbol.st_shndx == SHN_UNDEF && symbol.st_value == 0;
        }
        else if (valid)
        {
            valid = symbol.st_shndx != SHN_UNDEF && symbol.st_shndx < table.image.elf.e_shnum;
            Elf64_Shdr section = { 0 };
            if (valid)
            {
                memcpy(&section, bytes.pointer + table.image.elf.e_shoff + (uint64_t)symbol.st_shndx * sizeof(section), sizeof(section));
                const bool tls = i == 4 || i == 5;
                const uint64_t base = tls ? section.sh_addr - tdata.sh_addr : section.sh_addr;
                valid = (section.sh_flags & SHF_ALLOC) != 0 && ((section.sh_flags & SHF_TLS) != 0) == tls &&
                        (!tls || (tls_index != 0 && tls_index != UINT32_MAX && section.sh_addr >= tdata.sh_addr)) &&
                        (i == 10 || (symbol.st_value >= base && symbol.st_value - base <= section.sh_size && symbol.st_size <= section.sh_size - (symbol.st_value - base)));
                if (valid && i == 0)
                {
                    valid = symbol.st_value == table.image.elf.e_entry && symbol.st_shndx == text_index && symbol.st_value == text.sh_addr;
                }
                else if (valid && (i == 1 || i == 2 || i == 9))
                {
                    const uint64_t offset = i == 1 ? 0 : i == 2 ? 8 : 16;
                    valid = symbol.st_shndx == data_index && symbol.st_value == data.sh_addr + offset && section.sh_type == SHT_PROGBITS;
                    if (valid && i != 9)
                    {
                        uint64_t value = 0;
                        memcpy(&value, bytes.pointer + section.sh_offset + offset, (size_t)sizes[i]);
                        valid = value == (i == 1 ? 0x1122334455667788ull : 0x12345678ull);
                    }
                }
                else if (valid && i == 3)
                {
                    valid = symbol.st_shndx == text_index && symbol.st_value == text.sh_addr + 12;
                }
                else if (valid && i == 7)
                {
                    static const uint8_t begin[] = { 0x48, 0x8d, 0x05 };
                    valid = symbol.st_shndx == text_index && symbol.st_value == text.sh_addr + 18 && section.sh_type == SHT_PROGBITS &&
                            memcmp(bytes.pointer + section.sh_offset + symbol.st_value - base, begin, sizeof(begin)) == 0 &&
                            bytes.pointer[section.sh_offset + symbol.st_value - base + 7] == (char)0xc3;
                }
                else if (valid && (i == 4 || i == 5))
                {
                    valid = symbol.st_value == base && section.sh_size == sizes[i];
                }
                else if (valid && i == 10)
                {
                    uint64_t end = 0;
                    for (uint32_t s = 0; s < table.image.elf.e_phnum; s += 1)
                    {
                        Elf64_Phdr allocated;
                        memcpy(&allocated, bytes.pointer + table.image.elf.e_phoff + (uint64_t)s * sizeof(allocated), sizeof(allocated));
                        if (allocated.p_type == PT_LOAD)
                        {
                            const uint64_t last = allocated.p_vaddr + allocated.p_memsz;
                            end = last > end ? last : end;
                        }
                    }
                    valid = symbol.st_value == end;
                }
            }
        }
        foreign_test_expect(tally, valid, "named metadata retains size/type/binding/visibility and final definition address", names[i]);
    }
}

static bool foreign_driver_symbols_absent(String8 bytes)
{
    const ForeignDriverSymbolsImage image = foreign_driver_symbols_image(bytes);
    Elf64_Shdr section;
    return image.valid && foreign_driver_symbols_section(&image, ".symtab", &section) == 0 && foreign_driver_symbols_section(&image, ".strtab", &section) == 0;
}

static void foreign_driver_symbols_test(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files, Arena* arena, String8 interpreter)
{
    foreign_driver_symbols_guarded(tally);
    foreign_driver_symbols_capacity(tally);
    const char* source =
        ".file \"original-symbol-fixture.S\"\n.text\n.globl _start\n.type _start,@function\n_start:\nmov $42,%edi\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
        ".weak symbols_weak\n.type symbols_weak,@function\nsymbols_weak:\nmov $7,%eax\nret\n.size symbols_weak,.-symbols_weak\n"
        ".globl symbols_ifunc\n.type symbols_ifunc,@gnu_indirect_function\nsymbols_ifunc:\nlea symbols_weak(%rip),%rax\nret\n.size symbols_ifunc,.-symbols_ifunc\n"
        ".globl symbols_abs\n.set symbols_abs,42\n.weak symbols_undefined\n.type symbols_undefined,@function\n"
        ".data\n.hidden symbols_local\n.type symbols_local,@object\nsymbols_local:\n.quad 0x1122334455667788\n.size symbols_local,8\n"
        ".globl symbols_global\n.protected symbols_global\n.type symbols_global,@object\nsymbols_global:\n.long 0x12345678\n.size symbols_global,4\n"
        ".balign 8\n.type symbols_refs,@object\nsymbols_refs:\n.quad symbols_undefined,_end\n.size symbols_refs,16\n"
        ".section .tdata,\"awT\",@progbits\n.globl symbols_tls\n.type symbols_tls,@tls_object\nsymbols_tls:\n.long 3\n.size symbols_tls,4\n"
        ".section .tbss,\"awT\",@nobits\n.weak symbols_tbss\n.type symbols_tbss,@tls_object\nsymbols_tbss:\n.zero 8\n.size symbols_tbss,8\n"
        ".section .note.GNU-stack,\"\",@progbits\n";
    ForeignTestPath object;
    const bool ready = foreign_driver_symbols_compile(tally, directory, "fdriver_symbols", source, object);
    String8 references[2][2] = { 0 };
    for (uint32_t dynamic = 0; ready && dynamic < 2; dynamic += 1)
    {
        for (uint32_t stripped = 0; stripped < 2; stripped += 1)
        {
            for (uint32_t path = 0; path < 2; path += 1)
            {
                for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(foreign_test_lanes); l += 1)
                {
                    char name[80];
                    char lanes[16];
                    snprintf(name, sizeof(name), "symbols_dynamic%u_strip%u_path%u_lanes%u", dynamic, stripped, path, foreign_test_lanes[l]);
                    snprintf(lanes, sizeof(lanes), "%u", foreign_test_lanes[l]);
                    char* arguments[16];
                    uint32_t count = 0;
                    arguments[count++] = dynamic != 0 ? files->objects[FOREIGN_DRIVER_TEST_CALL] : object;
                    arguments[count++] = (char*)"--lanes";
                    arguments[count++] = lanes;
                    if (dynamic != 0)
                    {
                        arguments[count++] = (char*)"-pie";
                        arguments[count++] = (char*)"--dynamic-linker";
                        arguments[count++] = interpreter.pointer;
                        arguments[count++] = files->needed;
                    }
                    if (stripped != 0)
                    {
                        arguments[count++] = (char*)(l % 2 == 0 ? "-s" : "--strip-all");
                    }
                    if (path != 0)
                    {
                        arguments[count++] = (char*)"--scalar";
                    }
                    const String8 bytes = foreign_driver_test_invoke(tally, directory, name, "-o", arguments, count, true, 42, arena);
                    if (stripped != 0)
                    {
                        foreign_test_expect(tally, foreign_driver_symbols_absent(bytes), "both strip spellings omit regular tables", name);
                    }
                    else if (dynamic == 0)
                    {
                        foreign_driver_symbols_metadata(tally, bytes, name);
                    }
                    else
                    {
                        const ForeignDriverSymbolsTable table = foreign_driver_symbols_table(bytes);
                        Elf64_Sym imported = { 0 };
                        Elf64_Sym entry = { 0 };
                        foreign_test_expect(tally, foreign_driver_symbols_find(&table, "driver_pick", &imported) == 1 && imported.st_shndx == SHN_UNDEF &&
                                                  imported.st_value == 0 && imported.st_size == 0 && imported.st_info == ELF64_ST_INFO(STB_GLOBAL, STT_FUNC) &&
                                                  foreign_driver_symbols_find(&table, "_start", &entry) == 1 && entry.st_size == 14 && entry.st_value == table.image.elf.e_entry &&
                                                  entry.st_info == ELF64_ST_INFO(STB_GLOBAL, STT_FUNC) && entry.st_shndx != SHN_UNDEF,
                                            "regular table keeps an import's type and binding, undefined and without a size, as lld writes it", name);
                    }
                    if (path == 0 && l == 0)
                    {
                        references[dynamic][stripped] = bytes;
                    }
                    else
                    {
                        foreign_test_expect(tally, foreign_driver_test_same(references[dynamic][stripped], bytes), "selected/stripped bytes agree across exact/vector and every lane count", name);
                    }
                }
            }
        }
    }
    const ForeignDriverSymbolsImage kept = foreign_driver_symbols_image(references[1][0]);
    const ForeignDriverSymbolsImage stripped = foreign_driver_symbols_image(references[1][1]);
    const char* dynamic_sections[] = { ".dynsym", ".dynstr", ".dynamic", ".rela.dyn" };
    const uint32_t dynamic_types[] = { SHT_DYNSYM, SHT_STRTAB, SHT_DYNAMIC, SHT_RELA };
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(dynamic_sections); i += 1)
    {
        Elf64_Shdr a = { 0 };
        Elf64_Shdr b = { 0 };
        const uint32_t ai = foreign_driver_symbols_section(&kept, dynamic_sections[i], &a);
        const uint32_t bi = foreign_driver_symbols_section(&stripped, dynamic_sections[i], &b);
        foreign_test_expect(tally, ai != 0 && ai != UINT32_MAX && bi != 0 && bi != UINT32_MAX && a.sh_type == dynamic_types[i] && a.sh_type == b.sh_type && a.sh_addr == b.sh_addr &&
                                  a.sh_size == b.sh_size && memcmp(kept.bytes.pointer + a.sh_offset, stripped.bytes.pointer + b.sh_offset, a.sh_size) == 0,
                            "stripping preserves dynamic table bytes and addresses", dynamic_sections[i]);
    }
    ForeignTestPath empty_tls;
    const char* zero = ".section .tdata,\"awT\",@progbits\n.globl symbols_empty_tls\n.type symbols_empty_tls,@tls_object\nsymbols_empty_tls:\n.size symbols_empty_tls,0\n.section .note.GNU-stack,\"\",@progbits\n";
    if (foreign_driver_symbols_compile(tally, directory, "fdriver_empty_tls", zero, empty_tls))
    {
        char* arguments[] = { files->objects[FOREIGN_DRIVER_TEST_START], empty_tls, (char*)"-s" };
        foreign_driver_test_invoke(tally, directory, "symbols_empty_tls_refused", "-o", arguments, 2, false, 0, arena);
        foreign_driver_test_invoke(tally, directory, "symbols_empty_tls_stripped", "-o", arguments, 3, true, 42, arena);
    }
}

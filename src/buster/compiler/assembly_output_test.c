// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-assembly-output (#1362's stage 1): the assembly sink (assembly_output.c) on hand-built units, whose instruction
// records come from the assembler's matcher (asm_assemble_code on Intel text, independent of the sink's printer),
// sized by branch sizing and encoded by the encoder into the atoms the link holds, as the emission does:
//   - a unit of every piece the sink prints (calls to a local and an undefined function, RIP-relative data with
//     addends, a GOT load, absolute immediates and displacements, a rel32 branch, branches to the code's end, a symbol
//     in a function's middle, data with every data fixup it spells, a string pool with quotes and escapes, runs of
//     zeros, zero-fill data, weak, hidden and local symbols, a name GNU as reads only quoted), and the text of a small
//     unit compared whole with its expected text;
//   - the refusals, each by its reason (thread-local storage, section groups, a common symbol, code without records,
//     two symbols of one name, a name of the sink's prefix, a relocation in a record GNU as encodes otherwise: an MPX
//     bnd call), and the views that contradict themselves (records that
//     aren't their atom's bytes, a symbol inside an instruction, a branch without a target);
//   - the guarded tails: an output arena too small for the text, at every length short of it, and a scratch arena too
//     small for the tables, each leaving both arenas as they were;
//   - the data rows' vector kernel (assembly_byte_row) against libc's formatting of random bytes;
// and with --cross-check, GNU as assembles each printed unit, and its object must be the object writer's object of the
// unit (object_test_same_as, the assembler tests' comparator) but for what an assembler resolves itself
// (assembly_test_resolve: a PC-relative reference to a local symbol of the field's own section).

#define BUSTER_ASSEMBLY_TEST_SYMBOLS 64
#define BUSTER_ASSEMBLY_TEST_ATOMS 32

typedef struct AssemblyTest AssemblyTest;
struct AssemblyTest
{
    X86MatchIndex* index;
    Arena arena;
    Arena output;
    Arena scratch;
    const char* directory;
    uint64_t checks;
    uint64_t failures;
    bool cross_check;
    bool keep;
};

static void assembly_test_check(AssemblyTest* test, bool passed, const char* name, const char* detail)
{
    test->checks += 1;
    if (!passed)
    {
        if (test->failures < 32)
        {
            fprintf(stderr, "assembly output: %s%s%s\n", name, detail != 0 ? ": " : "", detail != 0 ? detail : "");
        }
        test->failures += 1;
    }
}

// A unit under construction: its link, each TEXT atom's code (by AtomId), and its named symbols, which code refers to
// by name.
typedef struct AssemblyFixture AssemblyFixture;
struct AssemblyFixture
{
    Link link;
    Arena names;
    AssemblyCode code[BUSTER_ASSEMBLY_TEST_ATOMS];
    const char* symbol_names[BUSTER_ASSEMBLY_TEST_SYMBOLS];
    SymbolId symbols[BUSTER_ASSEMBLY_TEST_SYMBOLS];
    uint32_t symbol_count;
    bool valid;
};

static void assembly_fixture_begin(AssemblyFixture* fixture, Arena* arena)
{
    *fixture = (AssemblyFixture){
        .names = arena_reserve(1u << 20),
        .valid = true,
    };
    const LinkCapacity capacity = {
        .names = 256,
        .atoms = BUSTER_ASSEMBLY_TEST_ATOMS - 1,
        .symbols = 256,
        .fixups = 256,
        .wide_addends = 16,
        .priorities = 4,
    };
    fixture->valid = link_initialize(&fixture->link, arena, &fixture->names, capacity);
}

static void assembly_fixture_end(AssemblyFixture* fixture)
{
    arena_release(&fixture->names);
}

// The symbol named `name`: the fixture's, or a new one with `flags`, defined nowhere yet (an undefined global unless
// assembly_fixture_define defines it).
static SymbolId assembly_fixture_symbol(AssemblyFixture* fixture, const char* name, uint8_t flags)
{
    SymbolId result = 0;
    for (uint32_t i = 0; result == 0 && i < fixture->symbol_count; i += 1)
    {
        result = strcmp(fixture->symbol_names[i], name) == 0 ? fixture->symbols[i] : 0;
    }
    if (result == 0 && fixture->symbol_count < BUSTER_ASSEMBLY_TEST_SYMBOLS)
    {
        const NameId id = name[0] != 0 ? link_add_name(&fixture->link, (const uint8_t*)name, (uint32_t)strlen(name)) : 0;
        result = link_add_symbol(&fixture->link, id, 0, 0, 0, flags, BUSTER_LINK_SOURCE_NONE);
        fixture->symbol_names[fixture->symbol_count] = name;
        fixture->symbols[fixture->symbol_count] = result;
        fixture->symbol_count += 1;
    }
    fixture->valid = fixture->valid && result != 0;
    return result;
}

// Defines `name` (with `flags`) in `atom` at `value`, of `size`.
static SymbolId assembly_fixture_define(AssemblyFixture* fixture, const char* name, uint8_t flags, AtomId atom, uint64_t value, uint64_t size)
{
    const SymbolId symbol = assembly_fixture_symbol(fixture, name, flags);
    if (symbol != 0)
    {
        fixture->link.symbols.flags[symbol] = flags;
        fixture->link.symbols.source[symbol] = BUSTER_LINK_SOURCE_PROGRAM;
        fixture->valid = fixture->valid && link_define_symbol(&fixture->link, symbol, atom, value, size);
    }
    return symbol;
}

// A function from Intel text, as the emission makes one: the matcher's records, its branches sized, encoded into a TEXT
// atom of 2^alignment, with a fixup per reference (each to the symbol of its name) and its named labels but `.L` ones
// defined where they are; the function's symbol `name` (with `flags`) at its start. Returns the atom.
static AtomId assembly_fixture_function(AssemblyTest* test, AssemblyFixture* fixture, const char* name, uint8_t flags, uint8_t alignment, const char* source)
{
    const String8 text = { .pointer = (char*)source, .length = strlen(source) };
    const AsmCode code = asm_assemble_code(text, test->index, (AsmOptions){ .vectorized = true, .intel = true }, &test->arena, &test->scratch);
    uint32_t* offsets = code.error == 0 ? BUSTER_ARENA_PUSH_ARRAY(&test->arena, uint32_t, (uint64_t)code.count + 1) : 0;
    uint32_t* fixup_ends = offsets != 0 ? BUSTER_ARENA_PUSH_ARRAY(&test->arena, uint32_t, BUSTER_X86_ENCODER_FIXUP_CAPACITY(code.reference_count)) : 0;
    uint8_t* bytes = fixup_ends != 0 ? BUSTER_ARENA_PUSH_ARRAY(&test->arena, uint8_t, BUSTER_X86_ENCODER_CAPACITY(code.count)) : 0;
    LinkReference* references = bytes != 0 ? BUSTER_ARENA_PUSH_ARRAY(&test->arena, LinkReference, (uint64_t)code.reference_count + 1) : 0;
    bool valid = references != 0;
    if (code.error != 0)
    {
        assembly_test_check(test, false, "a fixture's source assembles", code.error);
    }
    const BranchSizing sizing = valid ? size_branches(code.records, code.count, code.branches, code.targets, code.branch_count, offsets) : (BranchSizing){ 0 };
    valid = valid && sizing.error == BRANCH_SIZING_OK;
    const int64_t length = valid ? x86_encode_scalar(code.records, code.count, bytes, BUSTER_X86_ENCODER_CAPACITY(code.count), offsets, fixup_ends) : -1;
    valid = length > 0;
    uint32_t marked = 0;
    for (uint32_t r = 0; valid && r < code.reference_count; r += 1)
    {
        const AsmCodeReference* reference = &code.references[r];
        char symbol_name[64];
        snprintf(symbol_name, sizeof(symbol_name), "%.*s", (int)reference->name.length, reference->name.pointer);
        char* kept = arena_push(&test->arena, strlen(symbol_name) + 1, 1);
        valid = kept != 0;
        if (valid)
        {
            memcpy(kept, symbol_name, strlen(symbol_name) + 1);
            references[r] = (LinkReference){
                .instruction = reference->record,
                .field = reference->field,
                .kind = reference->kind,
                .target = assembly_fixture_symbol(fixture, kept, BUSTER_LINK_SYMBOL_GLOBAL),
                .addend = reference->addend,
            };
            marked += r == 0 || reference->record != code.references[r - 1].record;
        }
    }
    const AtomId atom = valid ? link_add_atom(&fixture->link, BUSTER_LINK_CLASS_TEXT, alignment, (uint64_t)length, bytes, 0) : 0;
    valid = atom != 0 && atom < BUSTER_ASSEMBLY_TEST_ATOMS && link_add_code_fixups(&fixture->link, atom, code.records, code.count, fixup_ends, marked, references, code.reference_count);
    if (valid)
    {
        fixture->code[atom] = (AssemblyCode){
            .records = code.records,
            .branches = code.branches,
            .targets = code.targets,
            .count = code.count,
            .branch_count = code.branch_count,
        };
        assembly_fixture_define(fixture, name, flags, atom, 0, (uint64_t)length);
        offsets[code.count] = (uint32_t)length;
        for (uint32_t l = 0; l < code.label_count; l += 1)
        {
            const AsmCodeLabel* label = &code.labels[l];
            if (!(label->name.length >= 2 && label->name.pointer[0] == '.' && label->name.pointer[1] == 'L'))
            {
                char* kept = arena_push(&test->arena, label->name.length + 1, 1);
                if (kept != 0)
                {
                    memcpy(kept, label->name.pointer, label->name.length);
                    kept[label->name.length] = 0;
                    assembly_fixture_define(fixture, kept, BUSTER_LINK_SYMBOL_GLOBAL, atom, offsets[label->record], 0);
                }
            }
        }
    }
    fixture->valid = fixture->valid && valid;
    return atom;
}

// A data atom of `class`, 2^alignment, `size` bytes of `bytes` (a copy; none for zero-fill).
static AtomId assembly_fixture_data(AssemblyTest* test, AssemblyFixture* fixture, uint8_t class, uint8_t alignment, const void* bytes, uint64_t size)
{
    uint8_t* copy = bytes != 0 ? arena_push(&test->arena, size, 16) : 0;
    if (copy != 0)
    {
        memcpy(copy, bytes, size);
    }
    const AtomId atom = link_add_atom(&fixture->link, class, alignment, size, copy, 0);
    fixture->valid = fixture->valid && atom != 0 && (bytes == 0 || copy != 0);
    return atom;
}

// A data fixup of `atom` at `offset`, of `kind`, to `name` + addend.
static void assembly_fixture_fixup(AssemblyFixture* fixture, AtomId atom, uint32_t offset, uint8_t kind, const char* name, int64_t addend)
{
    const SymbolId target = assembly_fixture_symbol(fixture, name, BUSTER_LINK_SYMBOL_GLOBAL);
    fixture->valid = fixture->valid && link_add_fixup(&fixture->link, atom, offset, kind, target, addend) != 0;
}

static AssemblyView assembly_fixture_view(const AssemblyFixture* fixture)
{
    const AssemblyView view = {
        .link = &fixture->link,
        .code = fixture->code,
        .code_count = BUSTER_ASSEMBLY_TEST_ATOMS,
        .source_name = BUSTER_S8("fixture.c"),
        .unit = 0,
    };
    return view;
}

// The unit of every piece the sink prints (the header's list).
static void assembly_fixture_program(AssemblyTest* test, AssemblyFixture* fixture)
{
    const uint8_t global_function = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION;
    const uint8_t local_function = BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_FUNCTION;
    const uint8_t global_object = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT;
    const uint8_t local_object = BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT;
    assembly_fixture_function(test, fixture, "main", global_function, 4,
                              "push rbp\nmov rbp, rsp\nsub rsp, 16\nmov eax, dword ptr [rip + counter]\nadd eax, dword ptr [rip + table + 8]\n"
                              "lea rdi, [rip + message]\ncall helper\ncall external\nmov rcx, qword ptr [rip + external_data@GOTPCREL]\n"
                              "mov dword ptr [rip + counter + 4], 1000\nmov ecx, offset table\nmov rdx, offset table + 4\nmovabs rsi, offset buffer + 16\n"
                              "mov eax, dword ptr [table + rcx*4]\nmovsd xmm0, qword ptr [rip + table]\naddsd xmm0, xmm1\nmovaps xmm2, xmm0\n"
                              "imul eax, ecx, 100\nshl eax, 1\nsar ecx, 3\nmovzx eax, al\nmovsxd rax, ecx\ncdqe\ncqo\n"
                              "test eax, eax\nje .Ldone\nmov eax, 1\njmp .Lend\n.Ldone:\nxor eax, eax\nmiddle:\nadd ecx, edx\n.Lend:\nleave\nret\n");
    assembly_fixture_function(test, fixture, "helper", local_function, 4, "mov eax, 42\ncmp eax, 7\njne .Lout\ninc eax\n.Lout:\nret\n");
    // A branch over more than 127 bytes: a rel32 jmp, which GNU as relaxes to the same only when told ({disp32}).
    assembly_fixture_function(test, fixture, "far", global_function, 4,
                              "jmp .Lover\nmov rax, qword ptr [rax + 0x1000]\nmov rax, qword ptr [rax + 0x1000]\nmov rax, qword ptr [rax + 0x1000]\n"
                              "mov rax, qword ptr [rax + 0x1000]\nmov rax, qword ptr [rax + 0x1000]\nmov rax, qword ptr [rax + 0x1000]\n"
                              "mov rax, qword ptr [rax + 0x1000]\nmov rax, qword ptr [rax + 0x1000]\nmov rax, qword ptr [rax + 0x1000]\n"
                              "mov rax, qword ptr [rax + 0x1000]\nmov rax, qword ptr [rax + 0x1000]\nmov rax, qword ptr [rax + 0x1000]\n"
                              "mov rax, qword ptr [rax + 0x1000]\nmov rax, qword ptr [rax + 0x1000]\nmov rax, qword ptr [rax + 0x1000]\n"
                              "mov rax, qword ptr [rax + 0x1000]\nmov rax, qword ptr [rax + 0x1000]\nmov rax, qword ptr [rax + 0x1000]\n"
                              "mov rax, qword ptr [rax + 0x1000]\nmov rax, qword ptr [rax + 0x1000]\n.Lover:\njmp .Lover\n");
    const uint32_t counter_bytes[2] = { 7, 0 };
    const AtomId data = assembly_fixture_data(test, fixture, BUSTER_LINK_CLASS_DATA, 3, counter_bytes, sizeof(counter_bytes));
    assembly_fixture_define(fixture, "counter", global_object, data, 0, sizeof(counter_bytes));
    uint8_t pointers[40] = { 0 };
    pointers[32] = 0x5a;
    const AtomId pointer_atom = assembly_fixture_data(test, fixture, BUSTER_LINK_CLASS_DATA, 3, pointers, sizeof(pointers));
    assembly_fixture_define(fixture, "pointers", local_object, pointer_atom, 0, sizeof(pointers));
    assembly_fixture_fixup(fixture, pointer_atom, 0, BUSTER_LINK_FIXUP_ABS64, "main", 0);
    assembly_fixture_fixup(fixture, pointer_atom, 8, BUSTER_LINK_FIXUP_ABS64, "helper", 1);
    assembly_fixture_fixup(fixture, pointer_atom, 16, BUSTER_LINK_FIXUP_PC32, "helper", 4);
    assembly_fixture_fixup(fixture, pointer_atom, 20, BUSTER_LINK_FIXUP_ABS32, "counter", 0);
    assembly_fixture_fixup(fixture, pointer_atom, 24, BUSTER_LINK_FIXUP_ABS64, "weak_undefined", -8);
    uint8_t table[64] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 };
    table[63] = 0xff;
    const AtomId table_atom = assembly_fixture_data(test, fixture, BUSTER_LINK_CLASS_RODATA, 4, table, sizeof(table));
    assembly_fixture_define(fixture, "table", local_object, table_atom, 0, sizeof(table));
    assembly_fixture_define(fixture, "table_tail", local_object, table_atom, 40, 24);
    static const char message[] = "hello \"world\"\\\n\t\x01\xff";
    const AtomId string_atom = assembly_fixture_data(test, fixture, BUSTER_LINK_CLASS_STRINGS, 0, message, sizeof(message));
    assembly_fixture_define(fixture, "message", BUSTER_LINK_SYMBOL_LOCAL, string_atom, 0, sizeof(message));
    const AtomId buffer_atom = assembly_fixture_data(test, fixture, BUSTER_LINK_CLASS_BSS, 5, 0, 96);
    assembly_fixture_define(fixture, "buffer", local_object, buffer_atom, 0, 96);
    const uint64_t answer = 42;
    const AtomId relro = assembly_fixture_data(test, fixture, BUSTER_LINK_CLASS_DATA_RELRO, 3, &answer, sizeof(answer));
    assembly_fixture_define(fixture, "hidden answer", global_object | BUSTER_LINK_SYMBOL_HIDDEN, relro, 0, sizeof(answer));
    const uint8_t weak_bytes[4] = { 1, 2, 3, 4 };
    const AtomId weak_atom = assembly_fixture_data(test, fixture, BUSTER_LINK_CLASS_DATA, 2, weak_bytes, sizeof(weak_bytes));
    assembly_fixture_define(fixture, "weak_definition", BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_OBJECT, weak_atom, 0, sizeof(weak_bytes));
    const SymbolId weak_undefined = assembly_fixture_symbol(fixture, "weak_undefined", BUSTER_LINK_SYMBOL_WEAK);
    fixture->link.symbols.flags[weak_undefined] = BUSTER_LINK_SYMBOL_WEAK;
    // The unwind tables' kind of reference: 8 bytes of PC64.
    uint8_t frame[16] = { 0 };
    const AtomId frame_atom = assembly_fixture_data(test, fixture, BUSTER_LINK_CLASS_EH_FRAME, 3, frame, sizeof(frame));
    assembly_fixture_fixup(fixture, frame_atom, 8, BUSTER_LINK_FIXUP_PC64, "far", 0);
}

// Our object of the unit, as -c builds it (object_build, on `arena`), or none.
static Object assembly_test_object(AssemblyFixture* fixture, Arena* arena)
{
    ObjectUnits units = { 0 };
    ObjectSymbols symbols = { 0 };
    Object result = { .error = OBJECT_ERROR_FULL };
    const bool prepared = link_check(&fixture->link) == LINK_ERROR_NONE && object_prepare(&units, &fixture->link, 1, arena) && object_map_symbols(&symbols, &fixture->link, arena);
    if (prepared)
    {
        result = object_build(&fixture->link, &units, &symbols, 0, (ObjectOptions){ 0 }, BUSTER_S8("fixture.c"), arena);
    }
    return result;
}

// The comparison's normalizations (the sink's form contract, docs/commands/driver.md's "-S"). First, an assembler resolves
// a PC-relative reference (R_X86_64_PC32 or R_X86_64_PLT32) to a local symbol in its field's own section itself, where
// the object writer leaves a relocation. In `object` (our object's mutable copy, open in `view`), each such relocation is
// applied to its field (S + A - P) and taken out of its section, which keeps the others in order; a relocation section
// left empty, which GNU as doesn't write, gets a type the comparison skips (SHT_SYMTAB_SHNDX's). Returns how many it
// resolved.
static uint64_t assembly_test_resolve(ObjectView* view, uint8_t* object)
{
    uint64_t resolved = 0;
    for (uint64_t i = 1; i < view->section_count; i += 1)
    {
        Elf64_Shdr* section = (Elf64_Shdr*)&view->sections[i];
        if (section->sh_type == SHT_RELA && section->sh_info < view->section_count)
        {
            const uint64_t target = section->sh_info;
            const Elf64_Shdr* applied = &view->sections[target];
            Elf64_Rela* relocations = (Elf64_Rela*)(object + section->sh_offset);
            const uint64_t count = section->sh_size / sizeof(Elf64_Rela);
            uint64_t kept = 0;
            for (uint64_t r = 0; r < count; r += 1)
            {
                const Elf64_Rela relocation = relocations[r];
                const uint32_t type = (uint32_t)ELF64_R_TYPE(relocation.r_info);
                const Elf64_Sym* symbol = &view->symbols[ELF64_R_SYM(relocation.r_info)];
                const bool local = ELF64_ST_BIND(symbol->st_info) == STB_LOCAL && object_view_symbol_section(view, ELF64_R_SYM(relocation.r_info)) == target;
                const bool resolvable = local && (type == R_X86_64_PC32 || type == R_X86_64_PLT32) && (applied->sh_flags & SHF_MERGE) == 0 &&
                                        relocation.r_offset + 4 <= applied->sh_size;
                if (resolvable)
                {
                    const int32_t value = (int32_t)((int64_t)symbol->st_value + relocation.r_addend - (int64_t)relocation.r_offset);
                    memcpy(object + applied->sh_offset + relocation.r_offset, &value, sizeof(value));
                    resolved += 1;
                }
                else
                {
                    relocations[kept] = relocation;
                    kept += 1;
                }
            }
            section->sh_size = kept * sizeof(Elf64_Rela);
            section->sh_type = kept == 0 ? SHT_SYMTAB_SHNDX : section->sh_type;
        }
    }
    return resolved;
}

// Second, GNU as lists _GLOBAL_OFFSET_TABLE_, undefined, in an object with a GOT reference, which no relocation names and
// the object writer's -c object doesn't list: in `object` (GNU as's, open in `view`) it becomes a symbol the comparison
// skips (a file symbol). Returns whether there was one.
static bool assembly_test_unlist_got(ObjectView* view, uint8_t* object)
{
    bool found = false;
    for (uint64_t i = 1; i < view->symbol_count; i += 1)
    {
        Elf64_Sym* symbol = (Elf64_Sym*)(object + view->sections[view->symbol_table_index].sh_offset) + i;
        const char* name = object_view_string(view->symbol_names, view->symbol_names_size, symbol->st_name);
        if (symbol->st_shndx == SHN_UNDEF && name != 0 && strcmp(name, "_GLOBAL_OFFSET_TABLE_") == 0)
        {
            symbol->st_info = ELF64_ST_INFO(STB_LOCAL, STT_FILE);
            found = true;
        }
    }
    return found;
}

// GNU as's object of `text` (written to the test's directory as `name`.s) against `object`, ours: the same but for
// the normalizations above. False, with the first difference, if they differ.
static bool assembly_test_against_as(AssemblyTest* test, const char* name, const uint8_t* text, uint64_t length, const Object* object, char* error, uint64_t error_size)
{
    char source_path[PATH_MAX];
    char object_path[PATH_MAX];
    char output_path[PATH_MAX];
    char errors_path[PATH_MAX];
    snprintf(source_path, sizeof(source_path), "%s/%s.s", test->directory, name);
    snprintf(object_path, sizeof(object_path), "%s/%s.as.o", test->directory, name);
    snprintf(output_path, sizeof(output_path), "%s/%s.as.out", test->directory, name);
    snprintf(errors_path, sizeof(errors_path), "%s/%s.as.err", test->directory, name);
    unlink(object_path);
    bool passed = object_write_file(source_path, text, length) == OBJECT_ERROR_NONE && oracle_epoch_file(source_path);
    char* arguments[] = { "as", "--64", "-o", object_path, source_path, 0 };
    passed = passed && asm_test_tool(arguments, object_path, output_path, errors_path) == 0;
    if (!passed)
    {
        snprintf(error, error_size, "GNU as didn't assemble %s (see %s)", source_path, errors_path);
    }
    const uint64_t mark = test->scratch.length;
    uint8_t* ours = passed ? arena_push(&test->scratch, object->size, 64) : 0;
    const String8 theirs_bytes = passed ? object_test_read_file(&test->scratch, object_path) : (String8){ 0 };
    ObjectView ours_view;
    ObjectView theirs_view;
    if (ours != 0)
    {
        memcpy(ours, object->bytes, object->size);
    }
    passed = passed && ours != 0 && theirs_bytes.pointer != 0 && object_view_open(&ours_view, ours, object->size, false, error, error_size) &&
             asm_test_gnu_object(&theirs_view, theirs_bytes, &test->scratch, error, error_size);
    if (passed)
    {
        assembly_test_resolve(&ours_view, ours);
        assembly_test_unlist_got(&theirs_view, (uint8_t*)theirs_bytes.pointer);
        passed = object_test_same_as(&ours_view, &theirs_view, error, error_size);
    }
    arena_pop_to(&test->scratch, mark);
    if (passed && !test->keep)
    {
        unlink(source_path);
        unlink(object_path);
        unlink(output_path);
        unlink(errors_path);
    }
    return passed;
}

// Whether the text contains `needle`.
static bool assembly_test_contains(const AssemblyResult* result, const char* needle)
{
    const uint64_t length = strlen(needle);
    bool found = false;
    for (uint64_t i = 0; !found && result->length >= length && i <= result->length - length; i += 1)
    {
        found = memcmp(result->bytes + i, needle, length) == 0;
    }
    return found;
}

static void assembly_test_program(AssemblyTest* test)
{
    const uint64_t mark = test->arena.length;
    AssemblyFixture fixture;
    assembly_fixture_begin(&fixture, &test->arena);
    assembly_fixture_program(test, &fixture);
    assembly_test_check(test, fixture.valid && link_check(&fixture.link) == LINK_ERROR_NONE, "the program fixture's link", 0);
    const AssemblyView view = assembly_fixture_view(&fixture);
    const uint64_t output_mark = test->output.length;
    const AssemblyResult result = fixture.valid ? assembly_print(&view, &test->output, &test->scratch) : (AssemblyResult){ .error = ASSEMBLY_INVALID };
    assembly_test_check(test, result.error == ASSEMBLY_OK, "the program prints", result.reason);
    // The pieces, as the contracts spell them.
    static const char* const pieces[] = {
        "\t.file \"fixture.c\"\n",
        "\t.globl main\n\t.type main, @function\nmain:\n",
        "\tcall helper@PLT\n",
        "\tcall external@PLT\n",
        "counter(%rip), %eax\n",
        "table+8(%rip), %eax\n",
        "message(%rip), %rdi\n",
        "external_data@GOTPCREL(%rip), %rcx\n",
        "\tmovl $1000, counter+4(%rip)\n",
        "\tmov $table, %ecx\n",
        "\tmov $table+4, %rdx\n",
        "\tmovabs $buffer+16, %rsi\n",
        "table(,%rcx,4), %eax\n",
        "{disp32} jmp " BUSTER_ASSEMBLY_PREFIX,
        "\tcltq\n\tcqto\n",
        "middle:\n",
        "\t.quad main\n\t.quad helper+1\n\t.long helper+4-.\n\t.long counter\n\t.quad weak_undefined-8\n",
        "\t.quad far-.\n",
        "\t.string \"hello \\\"world\\\"\\\\\\012\\011\\001\\377\"\n",
        "\t.zero 23\n\t.byte 0xff\n",
        "\t.zero 96\n",
        "\t.globl \"hidden answer\"\n\t.hidden \"hidden answer\"\n",
        "\t.weak weak_definition\n",
        "\t.weak weak_undefined\n",
        "\t.size main, ",
        "\t.section .rodata.str1.1,\"aMS\",@progbits,1\n",
        "\t.section .eh_frame,\"a\",@unwind\n",
        "\t.section .bss,\"aw\",@nobits\n",
        "\t.p2align 4, 0xcc\n",
        "\t.section .note.GNU-stack,\"\",@progbits\n",
    };
    for (uint32_t i = 0; result.error == ASSEMBLY_OK && i < BUSTER_ARRAY_LENGTH(pieces); i += 1)
    {
        assembly_test_check(test, assembly_test_contains(&result, pieces[i]), "the program's text has a piece", pieces[i]);
    }
    if (result.error == ASSEMBLY_OK && test->keep)
    {
        printf("%.*s", (int)result.length, (const char*)result.bytes);
    }
    // The tails: an output arena with room for less than the text, at lengths across it, and a scratch arena too small,
    // each failing with both arenas as they were.
    for (uint64_t room = 0; result.error == ASSEMBLY_OK && room < result.length; room += 1 + room / 3)
    {
        Arena small = arena_reserve(1u << 20);
        small.capacity = room;
        const uint64_t scratch_length = test->scratch.length;
        const AssemblyResult short_of_room = assembly_print(&view, &small, &test->scratch);
        char detail[128];
        snprintf(detail, sizeof(detail), "room %llu: error %u, length %llu", (unsigned long long)room, (uint32_t)short_of_room.error, (unsigned long long)small.length);
        assembly_test_check(test, short_of_room.error == ASSEMBLY_FULL && small.length == 0 && test->scratch.length == scratch_length, "an output arena short of the text fails whole", detail);
        arena_release(&small);
    }
    // Rooms below the tables' fail whole; the first that holds them prints the same text.
    bool printed = false;
    for (uint64_t room = 0; result.error == ASSEMBLY_OK && !printed && room < (1u << 20); room += 64)
    {
        Arena small = arena_reserve(1u << 20);
        small.capacity = room;
        const uint64_t output_length = test->output.length;
        const AssemblyResult short_of_room = assembly_print(&view, &test->output, &small);
        printed = short_of_room.error == ASSEMBLY_OK;
        char detail[128];
        snprintf(detail, sizeof(detail), "room %llu: error %u, length %llu", (unsigned long long)room, (uint32_t)short_of_room.error, (unsigned long long)small.length);
        const bool whole = printed ? short_of_room.length == result.length && memcmp(short_of_room.bytes, result.bytes, result.length) == 0 :
                                     short_of_room.error == ASSEMBLY_FULL && test->output.length == output_length;
        assembly_test_check(test, whole && small.length == 0, "a scratch arena short of the tables fails whole, and one that holds them prints the same", detail);
        arena_pop_to(&test->output, output_length);
        arena_release(&small);
    }
    assembly_test_check(test, result.error != ASSEMBLY_OK || printed, "a scratch arena of a megabyte holds the tables", 0);
    if (result.error == ASSEMBLY_OK && test->cross_check)
    {
        const uint64_t object_mark = test->scratch.length;
        const Object object = assembly_test_object(&fixture, &test->scratch);
        char error[256] = { 0 };
        const bool same = object.error == OBJECT_ERROR_NONE && assembly_test_against_as(test, "program", result.bytes, result.length, &object, error, sizeof(error));
        assembly_test_check(test, same, "GNU as's object of the program is the object writer's", error);
        arena_pop_to(&test->scratch, object_mark);
    }
    arena_pop_to(&test->output, output_mark);
    assembly_fixture_end(&fixture);
    arena_pop_to(&test->arena, mark);
}

// A small unit's whole text: a local function and a global datum.
static void assembly_test_small(AssemblyTest* test)
{
    const uint64_t mark = test->arena.length;
    AssemblyFixture fixture;
    assembly_fixture_begin(&fixture, &test->arena);
    assembly_fixture_function(test, &fixture, "f", BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_FUNCTION, 4, "xor eax, eax\nret\n");
    const uint8_t byte = 9;
    const AtomId data = assembly_fixture_data(test, &fixture, BUSTER_LINK_CLASS_DATA, 0, &byte, 1);
    assembly_fixture_define(&fixture, "d", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT, data, 0, 1);
    const AssemblyView view = assembly_fixture_view(&fixture);
    const uint64_t output_mark = test->output.length;
    const AssemblyResult result = fixture.valid ? assembly_print(&view, &test->output, &test->scratch) : (AssemblyResult){ .error = ASSEMBLY_INVALID };
    static const char expected[] = "\t.file \"fixture.c\"\n"
                                   "\t.section .text,\"ax\",@progbits\n"
                                   "\t.p2align 4, 0xcc\n"
                                   "\t.type f, @function\n"
                                   "f:\n"
                                   "\txor %eax, %eax\n"
                                   "\tret\n"
                                   "\t.size f, 3\n"
                                   "\t.section .data,\"aw\",@progbits\n"
                                   "\t.globl d\n"
                                   "\t.type d, @object\n"
                                   "d:\n"
                                   "\t.byte 0x09\n"
                                   "\t.size d, 1\n"
                                   "\t.section .note.GNU-stack,\"\",@progbits\n";
    const bool same = result.error == ASSEMBLY_OK && result.length == sizeof(expected) - 1 && memcmp(result.bytes, expected, result.length) == 0;
    assembly_test_check(test, same, "a small unit's whole text", result.error == ASSEMBLY_OK ? (const char*)result.bytes : result.reason);
    arena_pop_to(&test->output, output_mark);
    assembly_fixture_end(&fixture);
    arena_pop_to(&test->arena, mark);
}

// A unit the sink refuses or finds contradicting itself: a function and a datum, changed by `change` (below), which must
// fail with `error` and the reason `reason`, leaving the output arena as it was.
enum
{
    ASSEMBLY_TEST_TLS,
    ASSEMBLY_TEST_GROUP,
    ASSEMBLY_TEST_COMMON,
    ASSEMBLY_TEST_UNCODED,
    ASSEMBLY_TEST_DUPLICATE,
    ASSEMBLY_TEST_PREFIX,
    ASSEMBLY_TEST_RECORDS,
    ASSEMBLY_TEST_INSIDE,
    ASSEMBLY_TEST_TARGETLESS,
    ASSEMBLY_TEST_TLS_FIXUP,
    ASSEMBLY_TEST_BND_CALL,
    ASSEMBLY_TEST_CHANGES,
};

static void assembly_test_refusal(AssemblyTest* test, uint32_t change, AssemblyError error, const char* reason)
{
    const uint64_t mark = test->arena.length;
    AssemblyFixture fixture;
    assembly_fixture_begin(&fixture, &test->arena);
    const AtomId function = assembly_fixture_function(test, &fixture, "f", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, 4, "mov eax, 1\nadd eax, 2\nret\n");
    const uint32_t word = 5;
    const AtomId data = assembly_fixture_data(test, &fixture, change == ASSEMBLY_TEST_TLS ? BUSTER_LINK_CLASS_TDATA : BUSTER_LINK_CLASS_DATA, 2, &word, sizeof(word));
    const uint8_t type = change == ASSEMBLY_TEST_COMMON ? BUSTER_LINK_SYMBOL_COMMON : change == ASSEMBLY_TEST_TLS ? BUSTER_LINK_SYMBOL_TLS : BUSTER_LINK_SYMBOL_OBJECT;
    assembly_fixture_define(&fixture, "v", BUSTER_LINK_SYMBOL_GLOBAL | type, data, 0, sizeof(word));
    uint32_t group_atoms[BUSTER_ASSEMBLY_TEST_ATOMS] = { 0 };
    const LinkGroup groups[2] = { { 0 }, { .unit = 0, .flags = BUSTER_LINK_GROUP_COMDAT } };
    if (change == ASSEMBLY_TEST_GROUP)
    {
        group_atoms[data] = 1;
        fixture.link.groups = (LinkGroups){ .data = groups, .atom_groups = group_atoms, .count = 2, .atom_count = fixture.link.atoms.count };
    }
    if (change == ASSEMBLY_TEST_UNCODED)
    {
        fixture.code[function] = (AssemblyCode){ 0 };
    }
    if (change == ASSEMBLY_TEST_DUPLICATE)
    {
        const NameId name = link_add_name(&fixture.link, (const uint8_t*)"f", 1);
        const SymbolId other = link_add_symbol(&fixture.link, name, 0, 0, 0, BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT, BUSTER_LINK_SOURCE_PROGRAM);
        fixture.valid = fixture.valid && other != 0 && link_define_symbol(&fixture.link, other, data, 2, 0);
    }
    if (change == ASSEMBLY_TEST_PREFIX)
    {
        assembly_fixture_define(&fixture, BUSTER_ASSEMBLY_PREFIX "s1", BUSTER_LINK_SYMBOL_LOCAL, data, 0, 0);
    }
    X86Instruction changed[3];
    if (change == ASSEMBLY_TEST_RECORDS)
    {
        memcpy(changed, fixture.code[function].records, sizeof(changed));
        changed[1].immediate = 3;
        fixture.code[function].records = changed;
    }
    if (change == ASSEMBLY_TEST_INSIDE)
    {
        assembly_fixture_define(&fixture, "inside", BUSTER_LINK_SYMBOL_GLOBAL, function, 1, 0);
    }
    if (change == ASSEMBLY_TEST_TARGETLESS)
    {
        const AtomId branching = assembly_fixture_function(test, &fixture, "g", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, 4, "jmp .L1\n.L1:\nret\n");
        fixture.code[branching].branch_count = 0;
    }
    if (change == ASSEMBLY_TEST_BND_CALL)
    {
        assembly_fixture_function(test, &fixture, "g", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, 4, "bnd call external\nret\n");
    }
    if (change == ASSEMBLY_TEST_TLS_FIXUP)
    {
        const uint64_t zero = 0;
        const AtomId pointer = assembly_fixture_data(test, &fixture, BUSTER_LINK_CLASS_DATA, 3, &zero, sizeof(zero));
        const SymbolId tls = assembly_fixture_symbol(&fixture, "tls", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_TLS);
        fixture.valid = fixture.valid && link_add_fixup(&fixture.link, pointer, 0, BUSTER_LINK_FIXUP_DTPOFF64, tls, 0) != 0;
    }
    const AssemblyView view = assembly_fixture_view(&fixture);
    const uint64_t output_length = test->output.length;
    const uint64_t scratch_length = test->scratch.length;
    const AssemblyResult result = fixture.valid ? assembly_print(&view, &test->output, &test->scratch) : (AssemblyResult){ .error = ASSEMBLY_OK };
    char detail[256];
    snprintf(detail, sizeof(detail), "change %u: error %u (%s), expected %u (%s)", change, (uint32_t)result.error, result.reason != 0 ? result.reason : "none",
             (uint32_t)error, reason);
    const bool refused = fixture.valid && result.error == error && result.reason != 0 && strstr(result.reason, reason) != 0 && test->output.length == output_length &&
                         test->scratch.length == scratch_length;
    assembly_test_check(test, refused, "a refusal or an invalid view, by its reason", detail);
    assembly_fixture_end(&fixture);
    arena_pop_to(&test->arena, mark);
}

// The data rows' kernel against libc's formatting of the same bytes, on random rows and the extremes.
static void assembly_test_rows(AssemblyTest* test)
{
    uint64_t state = 0x9e3779b97f4a7c15ull;
    bool same = true;
    for (uint32_t row = 0; same && row < 4096; row += 1)
    {
        uint8_t input[BUSTER_ASSEMBLY_ROW_BYTES];
        for (uint32_t i = 0; i < BUSTER_ASSEMBLY_ROW_BYTES; i += 1)
        {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            input[i] = row == 0 ? 0 : row == 1 ? 0xff : row == 2 ? (uint8_t)(i * 17) : (uint8_t)(state >> 56);
        }
        uint8_t output[BUSTER_ASSEMBLY_ROW_TEXT + 1];
        assembly_byte_row(output, input);
        char expected[BUSTER_ASSEMBLY_ROW_TEXT + 1];
        int length = snprintf(expected, sizeof(expected), "\t.byte ");
        for (uint32_t i = 0; i < BUSTER_ASSEMBLY_ROW_BYTES; i += 1)
        {
            length += snprintf(expected + length, sizeof(expected) - (size_t)length, "0x%02x%c", input[i], i + 1 == BUSTER_ASSEMBLY_ROW_BYTES ? '\n' : ',');
        }
        same = length == BUSTER_ASSEMBLY_ROW_TEXT && memcmp(output, expected, BUSTER_ASSEMBLY_ROW_TEXT) == 0;
    }
    assembly_test_check(test, same, "the data rows' kernel formats as libc does", 0);
}

// The form sweep's line of a record: the text's line, 1-based, after its atom's alignment, as GNU as's messages number
// them; lines[k] for the k-th atom.
static void assembly_test_lines(const AssemblyResult* result, uint32_t* lines, uint32_t count)
{
    static const char alignment[] = "\t.p2align 4, 0xcc\n";
    const uint64_t length = sizeof(alignment) - 1;
    uint32_t line = 1;
    uint32_t atom = 0;
    for (uint64_t i = 0; i < result->length && atom < count; i += 1)
    {
        if (result->length - i >= length && memcmp(result->bytes + i, alignment, length) == 0 && (i == 0 || result->bytes[i - 1] == '\n'))
        {
            lines[atom] = line + 1;
            atom += 1;
        }
        line += result->bytes[i] == '\n';
    }
}

// Prints the record's line of the text (its `line`) to stderr, after `what`.
static void assembly_test_report_line(const AssemblyResult* result, uint32_t line, const char* what)
{
    uint32_t at_line = 1;
    uint64_t begin = 0;
    for (uint64_t i = 0; i < result->length && at_line < line; i += 1)
    {
        at_line += result->bytes[i] == '\n';
        begin = i + 1;
    }
    uint64_t end = begin;
    while (end < result->length && result->bytes[end] != '\n')
    {
        end += 1;
    }
    fprintf(stderr, "assembly output: forms: %s: %.*s\n", what, (int)(end - begin), (const char*)result->bytes + begin);
}

// --cross-check's form sweep (the instruction contract): every form the matcher admits for a target of every feature,
// but a branch's (whose target is a label: the program fixture's), `variants` random records each
// (x86_random_instruction, which doesn't avoid the encodings GNU as would replace: the sink escapes those), each record an
// atom of its own at 16 bytes' alignment, printed and assembled by GNU as. Each slot's bytes must be the encoder's. A
// form GNU as doesn't know (this binutils' "no such instruction") is counted as skipped; any other message of GNU as's
// fails its form.
static void assembly_test_forms(AssemblyTest* test, uint32_t variants)
{
    const uint64_t mark = test->arena.length;
    const X86Target everything = { .features = (UINT64_C(1) << X86_FEATURE_COUNT) - 1, .architecture = X86_ARCH_OTHER };
    x86_match_initialize(test->index, &everything);
    const X86EncoderMode mode = x86_test_mode;
    x86_test_mode = X86_ENCODER_MODE_64;
    const uint32_t admitted = test->index->begin[BUSTER_X86_MATCH_BUCKETS];
    const uint64_t capacity = (uint64_t)admitted * variants;
    X86Instruction* records = BUSTER_ARENA_PUSH_ARRAY(&test->arena, X86Instruction, capacity);
    uint8_t* keep = BUSTER_ARENA_PUSH_ARRAY(&test->arena, uint8_t, capacity);
    uint32_t* lines = BUSTER_ARENA_PUSH_ARRAY(&test->arena, uint32_t, capacity);
    uint64_t state = 1362;
    uint32_t count = 0;
    uint32_t forms = 0;
    for (uint32_t at = 0; records != 0 && keep != 0 && lines != 0 && at < admitted; at += 1)
    {
        const uint32_t row = test->index->forms[at];
        if (x86_form_descriptions[row].syntax != X86_SYNTAX_REL)
        {
            forms += 1;
            for (uint32_t v = 0; v < variants; v += 1)
            {
                records[count] = x86_random_instruction(&state, (uint16_t)row, false);
                keep[count] = 1;
                count += 1;
            }
        }
    }
    x86_test_mode = mode;
    uint32_t skipped = 0;
    uint32_t failed = 0;
    uint32_t escaped = 0;
    uint32_t compared = 0;
    // Two passes: the first finds the records this GNU as doesn't read; the second compares the rest.
    for (uint32_t pass = 0; pass < 2 && count != 0; pass += 1)
    {
        const uint64_t pass_mark = test->arena.length;
        AssemblyFixture fixture = { .names = arena_reserve(1u << 20) };
        const LinkCapacity link_capacity = {
            .names = 4,
            .atoms = count,
            .symbols = 4,
            .fixups = 4,
            .wide_addends = 4,
            .priorities = 4,
        };
        AssemblyCode* code = BUSTER_ARENA_PUSH_ARRAY(&test->arena, AssemblyCode, (uint64_t)count + 1);
        uint32_t* atoms_records = BUSTER_ARENA_PUSH_ARRAY(&test->arena, uint32_t, (uint64_t)count + 1);
        bool valid = code != 0 && atoms_records != 0 && link_initialize(&fixture.link, &test->arena, &fixture.names, link_capacity);
        uint32_t atoms = 0;
        for (uint32_t i = 0; valid && i < count; i += 1)
        {
            if (keep[i] != 0)
            {
                uint8_t* bytes = arena_push(&test->arena, BUSTER_X86_ENCODER_CAPACITY(1), 16);
                const int64_t length = bytes != 0 ? x86_encode_scalar(&records[i], 1, bytes, BUSTER_X86_ENCODER_CAPACITY(1), 0, 0) : -1;
                const AtomId atom = length > 0 ? link_add_atom(&fixture.link, BUSTER_LINK_CLASS_TEXT, 4, (uint64_t)length, bytes, 0) : 0;
                valid = atom != 0;
                if (valid)
                {
                    code[atom] = (AssemblyCode){ .records = &records[i], .count = 1 };
                    atoms_records[atoms] = i;
                    atoms += 1;
                    escaped += pass == 1 && assembly_escaped(&records[i], x86_form_description(records[i].form), false);
                }
            }
        }
        const AssemblyView view = {
            .link = &fixture.link,
            .code = code,
            .code_count = count + 1,
            .source_name = BUSTER_S8("forms.c"),
        };
        const uint64_t output_mark = test->output.length;
        const AssemblyResult result = valid ? assembly_print(&view, &test->output, &test->scratch) : (AssemblyResult){ .error = ASSEMBLY_FULL };
        assembly_test_check(test, result.error == ASSEMBLY_OK, "forms: every record prints", result.reason);
        uint32_t* atom_lines = result.error == ASSEMBLY_OK ? BUSTER_ARENA_PUSH_ARRAY(&test->arena, uint32_t, (uint64_t)atoms + 1) : 0;
        if (atom_lines != 0)
        {
            memset(atom_lines, 0, ((uint64_t)atoms + 1) * sizeof(*atom_lines));
            assembly_test_lines(&result, atom_lines, atoms);
            char source_path[PATH_MAX];
            char object_path[PATH_MAX];
            char output_path[PATH_MAX];
            char errors_path[PATH_MAX];
            snprintf(source_path, sizeof(source_path), "%s/forms.s", test->directory);
            snprintf(object_path, sizeof(object_path), "%s/forms.o", test->directory);
            snprintf(output_path, sizeof(output_path), "%s/forms.out", test->directory);
            snprintf(errors_path, sizeof(errors_path), "%s/forms.err", test->directory);
            unlink(object_path);
            const bool written = object_write_file(source_path, result.bytes, result.length) == OBJECT_ERROR_NONE && oracle_epoch_file(source_path);
            char* tool[] = { "as", "--64", "-o", object_path, source_path, 0 };
            const bool assembled = written && asm_test_tool(tool, object_path, output_path, errors_path) == 0;
            if (pass == 0 && !assembled)
            {
                // GNU as's messages, "forms.s:LINE: Error: ...": a line it doesn't know the mnemonic of is skipped, any
                // other fails its record.
                FILE* messages = fopen(errors_path, "r");
                char message[1024];
                while (messages != 0 && fgets(message, sizeof(message), messages) != 0)
                {
                    const char* colon = strstr(message, ".s:");
                    const uint32_t line = colon != 0 ? (uint32_t)strtoul(colon + 3, 0, 10) : 0;
                    const bool error = strstr(message, "Error:") != 0;
                    const bool unknown = strstr(message, "no such instruction") != 0 || strstr(message, "unsupported instruction") != 0 ||
                                         strstr(message, "is not supported on this processor") != 0 || strstr(message, "unknown pseudo-prefix") != 0;
                    for (uint32_t a = 0; error && a < atoms; a += 1)
                    {
                        const uint32_t record = atoms_records[a];
                        if (atom_lines[a] == line && keep[record] != 0)
                        {
                            keep[record] = 0;
                            skipped += unknown;
                            failed += !unknown;
                            if (!unknown)
                            {
                                assembly_test_report_line(&result, line, message);
                            }
                        }
                    }
                }
                if (messages != 0)
                {
                    fclose(messages);
                }
            }
            assembly_test_check(test, pass == 0 || assembled, "forms: GNU as assembles every form it knows", errors_path);
            const uint64_t object_mark = test->scratch.length;
            const String8 theirs = pass == 1 && assembled ? object_test_read_file(&test->scratch, object_path) : (String8){ 0 };
            ObjectView view_theirs;
            char error[256] = { 0 };
            const bool opened = theirs.pointer != 0 && object_view_open(&view_theirs, (const uint8_t*)theirs.pointer, theirs.length, true, error, sizeof(error));
            const uint64_t text_index = opened ? object_test_section_named(&view_theirs, ".text", false) : 0;
            const Elf64_Shdr* text = text_index != 0 ? &view_theirs.sections[text_index] : 0;
            assembly_test_check(test, pass == 0 || text != 0, "forms: GNU as's object has the text", error);
            for (uint32_t a = 0; text != 0 && a < atoms; a += 1)
            {
                const AtomId atom = a + 1;
                const uint64_t size = fixture.link.atoms.size[atom];
                const bool inside = (uint64_t)a * 16 + size <= text->sh_size;
                const bool same = inside && memcmp(view_theirs.file + text->sh_offset + (uint64_t)a * 16, fixture.link.atoms.bytes[atom], size) == 0;
                compared += 1;
                if (!same)
                {
                    failed += 1;
                    assembly_test_report_line(&result, atom_lines[a], "GNU as encodes it otherwise");
                }
            }
            arena_pop_to(&test->scratch, object_mark);
            if (!test->keep)
            {
                unlink(source_path);
                unlink(object_path);
                unlink(output_path);
                unlink(errors_path);
            }
        }
        arena_pop_to(&test->output, output_mark);
        arena_release(&fixture.names);
        arena_pop_to(&test->arena, pass_mark);
    }
    printf("assembly output: %u forms, %u records, %u compared with GNU as (%u printed as their bytes), %u skipped (forms this GNU as doesn't know), %u failed\n",
           forms, count, compared, escaped, skipped, failed);
    assembly_test_check(test, failed == 0 && compared != 0, "forms: GNU as encodes every printed record as the encoder does", 0);
    arena_pop_to(&test->arena, mark);
}

static int run_assembly_output_tests(int count, char** arguments)
{
    AssemblyTest test = {
        .arena = arena_reserve(1ull << 30),
        .output = arena_reserve(1ull << 30),
        .scratch = arena_reserve(1ull << 30),
    };
    bool valid = oracle_epoch_begin();
    for (int i = 0; i < count; i += 1)
    {
        if (strcmp(arguments[i], "--cross-check") == 0)
        {
            test.cross_check = true;
        }
        else if (strcmp(arguments[i], "--keep") == 0)
        {
            test.keep = true;
        }
        else
        {
            fprintf(stderr, "usage: compiler test-assembly-output [--cross-check] [--keep]\n");
            valid = false;
        }
    }
    char directory[] = "/tmp/buster-assembly-output-XXXXXX";
    valid = valid && x86_encoder_initialize() && x86_semantics_initialize() && mkdtemp(directory) != 0;
    test.directory = directory;
    test.index = valid ? BUSTER_ARENA_PUSH_ARRAY(&test.arena, X86MatchIndex, 1) : 0;
    valid = valid && test.index != 0;
    if (valid)
    {
        X86Target target;
        valid = x86_target_parse("znver5", strlen("znver5"), &target);
        x86_match_initialize(test.index, &target);
    }
    if (valid)
    {
        assembly_test_rows(&test);
        assembly_test_small(&test);
        assembly_test_program(&test);
        assembly_test_refusal(&test, ASSEMBLY_TEST_TLS, ASSEMBLY_UNSUPPORTED, "thread-local storage");
        assembly_test_refusal(&test, ASSEMBLY_TEST_GROUP, ASSEMBLY_UNSUPPORTED, "section groups");
        assembly_test_refusal(&test, ASSEMBLY_TEST_COMMON, ASSEMBLY_UNSUPPORTED, "common symbols");
        assembly_test_refusal(&test, ASSEMBLY_TEST_UNCODED, ASSEMBLY_UNSUPPORTED, "code without instruction records");
        assembly_test_refusal(&test, ASSEMBLY_TEST_DUPLICATE, ASSEMBLY_UNSUPPORTED, "two symbols of the same name");
        assembly_test_refusal(&test, ASSEMBLY_TEST_PREFIX, ASSEMBLY_UNSUPPORTED, BUSTER_ASSEMBLY_PREFIX);
        assembly_test_refusal(&test, ASSEMBLY_TEST_RECORDS, ASSEMBLY_INVALID, "don't encode to their function's bytes");
        assembly_test_refusal(&test, ASSEMBLY_TEST_INSIDE, ASSEMBLY_INVALID, "a symbol inside an instruction");
        assembly_test_refusal(&test, ASSEMBLY_TEST_TARGETLESS, ASSEMBLY_INVALID, "a relative branch without a target");
        assembly_test_refusal(&test, ASSEMBLY_TEST_TLS_FIXUP, ASSEMBLY_UNSUPPORTED, "thread-local storage");
        assembly_test_refusal(&test, ASSEMBLY_TEST_BND_CALL, ASSEMBLY_UNSUPPORTED, "GNU as encodes otherwise");
        if (test.cross_check)
        {
            assembly_test_forms(&test, 2);
        }
    }
    if (valid && !test.keep)
    {
        rmdir(directory);
    }
    printf("assembly output: %llu checks, %llu failures%s\n", (unsigned long long)test.checks, (unsigned long long)test.failures,
           test.cross_check ? " (with GNU as)" : "");
    arena_release(&test.arena);
    arena_release(&test.output);
    arena_release(&test.scratch);
    return valid && test.failures == 0 ? 0 : 1;
}

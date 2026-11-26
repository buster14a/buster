// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-assembly-output (#1362): the assembly sink (assembly_output.c) on hand-built units, whose instruction
// records come from the assembler's matcher (asm_assemble_code on Intel text, independent of the sink's printer),
// sized by branch sizing and encoded by the encoder into the atoms the link holds, as the emission does:
//   - a unit of every piece the sink prints (calls to a local and an undefined function, RIP-relative data with
//     addends, a GOT load, absolute immediates and displacements, a rel32 branch, branches to the code's end, a symbol
//     in a function's middle, data with every data fixup it spells, a string pool with quotes and escapes, runs of
//     zeros, zero-fill data, a common definition, weak, hidden and local symbols, a name GNU as reads only quoted),
//     and the text of a small unit compared whole with its expected text;
//   - TLS, groups, GNU unique objects, mixed source assembly and semantic unwind comparisons;
//   - the refusals, each by its reason (invalid groups, unsupported common bindings and aliases,
//     code without records, two symbols of one name, a name of the sink's prefix, a relocation GNU as encodes otherwise: an MPX
//     bnd call), and the views that contradict themselves (records that
//     aren't their atom's bytes, a symbol inside an instruction, a branch without a target);
//   - the guarded tails: an output arena too small for the text, at every length short of it, and a scratch arena too
//     small for the tables, each leaving both arenas as they were;
//   - the data rows' vector kernel (assembly_byte_row) against libc's formatting of random bytes;
// and with --cross-check, GNU as assembles each printed unit, and its object must be the object writer's object of the
// unit (object_test_same_as, the assembler tests' comparator) but for what an assembler resolves itself
// (assembly_test_resolve: a PC-relative reference to a local symbol of the field's own section) and equivalent CFI
// encodings checked by the independent frame-rule interpreter. Runtime fixtures round-trip and execute both tiers.

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
    // --option's arguments, which every -S and -c command of --c-testsuite and --compare takes after its own.
    char* options[16];
    uint32_t option_count;
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
    const AsmCode code = asm_assemble_code(text, test->index, (AsmOptions){ .intel = true }, &test->arena, &test->scratch);
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
    const int64_t length = valid ? x86_encode(code.records, code.count, bytes, BUSTER_X86_ENCODER_CAPACITY(code.count), offsets, fixup_ends) : -1;
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
                              "{disp32} mov eax, dword ptr [rsp + 4]\nimul eax, ecx, 100\nshl eax, 1\nsar ecx, 3\nmovzx eax, al\nmovsxd rax, ecx\ncdqe\ncqo\n"
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
    uint8_t pointers[48] = { 0 };
    pointers[32] = 0x5a;
    const AtomId pointer_atom = assembly_fixture_data(test, fixture, BUSTER_LINK_CLASS_DATA, 3, pointers, sizeof(pointers));
    assembly_fixture_define(fixture, "pointers", local_object, pointer_atom, 0, sizeof(pointers));
    assembly_fixture_fixup(fixture, pointer_atom, 0, BUSTER_LINK_FIXUP_ABS64, "main", 0);
    assembly_fixture_fixup(fixture, pointer_atom, 8, BUSTER_LINK_FIXUP_ABS64, "helper", 1);
    assembly_fixture_fixup(fixture, pointer_atom, 16, BUSTER_LINK_FIXUP_PC32, "helper", 4);
    assembly_fixture_fixup(fixture, pointer_atom, 20, BUSTER_LINK_FIXUP_ABS32, "counter", 0);
    assembly_fixture_fixup(fixture, pointer_atom, 24, BUSTER_LINK_FIXUP_ABS64, "weak_undefined", -8);
    assembly_fixture_fixup(fixture, pointer_atom, 40, BUSTER_LINK_FIXUP_ABS64, "common buffer", 3);
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
    const AtomId common_atom = assembly_fixture_data(test, fixture, BUSTER_LINK_CLASS_BSS, 6, 0, 19);
    assembly_fixture_define(fixture, "common buffer", BUSTER_LINK_SYMBOL_COMMON | BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_HIDDEN, common_atom, 0, 19);
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

#include "assembly_output_cfi_test.c"

// Our object (`ours`, `ours_size` bytes) against the reassembled object (`theirs`, which this changes): the same but for the
// normalizations above. False, with the first difference, if they differ.
static bool assembly_test_same_objects(AssemblyTest* test, const uint8_t* ours_bytes, uint64_t ours_size, String8 theirs, char* error, uint64_t error_size)
{
    const uint64_t mark = test->scratch.length;
    uint8_t* ours = arena_push(&test->scratch, ours_size, 64);
    ObjectView ours_view;
    ObjectView theirs_view;
    if (ours != 0)
    {
        memcpy(ours, ours_bytes, ours_size);
    }
    bool passed = ours != 0 && theirs.pointer != 0 && asm_test_ordered_object(&ours_view, (String8){ .pointer = (char*)ours, .length = ours_size }, false, &test->scratch, error, error_size) &&
                  asm_test_gnu_object(&theirs_view, theirs, &test->scratch, error, error_size);
    if (passed)
    {
        assembly_test_resolve(&ours_view, ours);
        // Buster can retain a relocation for a local call that GNU as resolves in place.
        assembly_test_resolve(&theirs_view, (uint8_t*)theirs.pointer);
        assembly_test_unlist_got(&theirs_view, (uint8_t*)theirs.pointer);
        passed = assembly_cfi_normalize(&ours_view, &theirs_view, &test->scratch, error, error_size) &&
                 object_test_same_as(&ours_view, &theirs_view, error, error_size);
    }
    arena_pop_to(&test->scratch, mark);
    return passed;
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
    const String8 theirs = passed ? object_test_read_file(&test->scratch, object_path) : (String8){ 0 };
    passed = passed && assembly_test_same_objects(test, object->bytes, object->size, theirs, error, error_size);
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
        "\t{disp32} mov 4(%rsp), %eax\n",
        "\tcltq\n\tcqto\n",
        "middle:\n",
        "\t.quad main\n\t.quad helper+1\n\t.long helper+4-.\n\t.long counter\n\t.quad weak_undefined-8\n",
        "\t.quad far-.\n",
        "\t.string \"hello \\\"world\\\"\\\\\\012\\011\\001\\377\"\n",
        "\t.zero 23\n\t.byte 0xff\n",
        "\t.zero 96\n",
        "\t.hidden \"common buffer\"\n\t.comm \"common buffer\", 19, 64\n",
        "\t.quad \"common buffer\"+3\n",
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
    if (result.error == ASSEMBLY_OK)
    {
        const uint64_t object_mark = test->scratch.length;
        const Object object = assembly_test_object(&fixture, &test->scratch);
        const String8 source = { .pointer = (char*)result.bytes, .length = result.length };
        AsmResult assembled = asm_assemble(source, test->index, (AsmOptions){ 0 }, &test->scratch);
        const Object rebuilt = assembled.error == 0 ? asm_object(&assembled, BUSTER_S8("fixture.c"), &test->scratch) : (Object){0};
        char error[256] = { 0 };
        const String8 bytes = { .pointer = (char*)rebuilt.bytes, .length = rebuilt.size };
        const bool same = object.error == OBJECT_ERROR_NONE && assembled.error == 0 && rebuilt.error == OBJECT_ERROR_NONE &&
                          assembly_test_same_objects(test, object.bytes, object.size, bytes, error, sizeof(error));
        assembly_test_check(test, same, "the assembler reads the assembly writer's program back into its object", assembled.error != 0 ? assembled.error : error);
        if (test->cross_check)
        {
            const bool gnu = object.error == OBJECT_ERROR_NONE && assembly_test_against_as(test, "program", result.bytes, result.length, &object, error, sizeof(error));
            assembly_test_check(test, gnu, "GNU as's object of the program is the object writer's", error);
        }
        arena_pop_to(&test->scratch, object_mark);
    }
    arena_pop_to(&test->output, output_mark);
    assembly_fixture_end(&fixture);
    arena_pop_to(&test->arena, mark);
}

static void assembly_test_mixed(AssemblyTest* test)
{
    const uint64_t mark = test->arena.length;
    const uint64_t output_mark = test->output.length;
    const uint64_t scratch_mark = test->scratch.length;
    const String8 source = object_test_read_file(&test->arena, "tests/assembly-output/mixed.s");
    AsmResult original = source.pointer != NULL ? asm_assemble(source, test->index, (AsmOptions){ .integrated = true, .keep_records = true }, &test->arena) :
                                                (AsmResult){ .error = "missing mixed assembly fixture" };
    AssemblyCode* code = original.error == NULL ? BUSTER_ARENA_PUSH_ARRAY(&test->arena, AssemblyCode, original.link.atoms.count) : NULL;
    bool valid = code != NULL && original.record_span_count == original.link.atoms.count;
    if (valid)
    {
        memset(code, 0, (uint64_t)original.link.atoms.count * sizeof(AssemblyCode));
        for (AtomId a = 1; a < original.link.atoms.count; a += 1)
        {
            const AsmRecordSpan span = original.record_spans[a];
            code[a].records = span.records;
            code[a].offsets = span.offsets;
            code[a].count = span.count;
        }
    }
    assembly_test_check(test, valid, "retain matched records and byte offsets for mixed assembly", original.error);
    const AssemblyView view = {
        .link = &original.link,
        .code = code,
        .sections = original.object_options.atom_sections,
        .temporary = original.object_options.temporary_symbols,
        .source_name = BUSTER_S8("mixed.s"),
        .code_count = original.link.atoms.count,
        .section_count = original.object_options.atom_sections != NULL ? original.link.atoms.count : 0,
        .temporary_count = original.object_options.temporary_symbols != NULL ? original.link.symbols.count : 0,
    };
    const AssemblyResult printed = valid ? assembly_print(&view, &test->output, &test->scratch) : (AssemblyResult){ .error = ASSEMBLY_INVALID };
    assembly_test_check(test, printed.error == ASSEMBLY_OK, "print mixed records, local branches, RIP references and data relocations", printed.reason);
    if (printed.error == ASSEMBLY_OK)
    {
        const Object expected = asm_object(&original, BUSTER_S8("mixed.s"), &test->scratch);
        const String8 text = { .pointer = (char*)printed.bytes, .length = printed.length };
        AsmResult rebuilt = asm_assemble(text, test->index, (AsmOptions){ 0 }, &test->scratch);
        const Object actual = rebuilt.error == NULL ? asm_object(&rebuilt, BUSTER_S8("mixed.s"), &test->scratch) : (Object){ 0 };
        char error[512] = { 0 };
        const String8 actual_bytes = { .pointer = (char*)actual.bytes, .length = actual.size };
        const bool same = expected.error == OBJECT_ERROR_NONE && rebuilt.error == NULL && actual.error == OBJECT_ERROR_NONE &&
                          assembly_test_same_objects(test, expected.bytes, expected.size, actual_bytes, error, sizeof(error));
        assembly_test_check(test, same, "Buster reassembles mixed records and fragments without changing object semantics", rebuilt.error != NULL ? rebuilt.error : error);
        if (test->cross_check)
        {
            const bool gnu = expected.error == OBJECT_ERROR_NONE && assembly_test_against_as(test, "mixed", printed.bytes, printed.length, &expected, error, sizeof(error));
            assembly_test_check(test, gnu, "GNU as reassembles mixed records and fragments without changing object semantics", error);
        }
        const AtomId atom = original.entry_atom;
        if (atom != 0 && code[atom].count != 0)
        {
            uint32_t* offsets = original.record_spans[atom].offsets;
            offsets[0] = (uint32_t)original.link.atoms.size[atom];
            const uint64_t before = test->output.length;
            const AssemblyResult invalid = assembly_print(&view, &test->output, &test->scratch);
            assembly_test_check(test, invalid.error == ASSEMBLY_INVALID && test->output.length == before,
                                "reject a mixed instruction extending beyond its atom without publishing output", invalid.reason);
        }
    }
    arena_pop_to(&test->scratch, scratch_mark);
    arena_pop_to(&test->output, output_mark);
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

// A hand-built symbolic unit, including optional section descriptors, against both independent assembly readers.
// Its original link and descriptors are the object writer's control; the text must preserve each ELF relationship.
static void assembly_test_section_fixture(AssemblyTest* test, AssemblyFixture* fixture, const ObjectAtomSection* sections, const char* name, const char* const* pieces, uint32_t piece_count)
{
    AssemblyView view = assembly_fixture_view(fixture);
    view.sections = sections;
    view.section_count = sections != NULL ? fixture->link.atoms.count : 0;
    const uint64_t output_mark = test->output.length;
    const uint64_t scratch_mark = test->scratch.length;
    const bool valid = fixture->valid && link_check(&fixture->link) == LINK_ERROR_NONE;
    assembly_test_check(test, valid, "the section fixture's link", name);
    const AssemblyResult result = valid ? assembly_print(&view, &test->output, &test->scratch) : (AssemblyResult){ .error = ASSEMBLY_INVALID };
    assembly_test_check(test, result.error == ASSEMBLY_OK, "the section fixture prints", result.reason != NULL ? result.reason : name);
    for (uint32_t i = 0; result.error == ASSEMBLY_OK && i < piece_count; i += 1)
    {
        assembly_test_check(test, assembly_test_contains(&result, pieces[i]), "the section fixture's text has a piece", pieces[i]);
    }
    if (result.error == ASSEMBLY_OK)
    {
        ObjectUnits units = { 0 };
        ObjectSymbols symbols = { 0 };
        const bool prepared = object_prepare(&units, &fixture->link, 1, &test->scratch) && object_map_symbols(&symbols, &fixture->link, &test->scratch);
        const ObjectOptions options = {
            .atom_sections = sections,
            .atom_section_count = sections != NULL ? fixture->link.atoms.count : 0,
        };
        const Object object = prepared ? object_build(&fixture->link, &units, &symbols, 0, options, BUSTER_S8("fixture.c"), &test->scratch) :
                                        (Object){ .error = OBJECT_ERROR_FULL };
        const String8 source = {
            .pointer = (char*)result.bytes,
            .length = result.length,
        };
        AsmResult assembled = asm_assemble(source, test->index, (AsmOptions){ 0 }, &test->scratch);
        const Object rebuilt = assembled.error == NULL ? asm_object(&assembled, BUSTER_S8("fixture.c"), &test->scratch) :
                                                       (Object){ .error = OBJECT_ERROR_FULL };
        char error[256] = { 0 };
        const String8 bytes = {
            .pointer = (char*)rebuilt.bytes,
            .length = rebuilt.size,
        };
        const bool same = object.error == OBJECT_ERROR_NONE && assembled.error == NULL && rebuilt.error == OBJECT_ERROR_NONE &&
                          assembly_test_same_objects(test, object.bytes, object.size, bytes, error, sizeof(error));
        assembly_test_check(test, same, "Buster preserves the section fixture's object", assembled.error != NULL ? assembled.error : error);
        if (test->cross_check)
        {
            const bool gnu = object.error == OBJECT_ERROR_NONE && assembly_test_against_as(test, name, result.bytes, result.length, &object, error, sizeof(error));
            assembly_test_check(test, gnu, "GNU as preserves the section fixture's object", error);
        }
    }
    arena_pop_to(&test->scratch, scratch_mark);
    arena_pop_to(&test->output, output_mark);
}

// A PC-relative relocation's field addend may fit int64 even when its instruction-relative spelling doesn't.
static void assembly_test_wide_code_addend(AssemblyTest* test)
{
    const uint64_t mark = test->arena.length;
    AssemblyFixture fixture;
    assembly_fixture_begin(&fixture, &test->arena);
    const X86Instruction records[] = {
        { .form = X86_FORM_LEA_R64_M, .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_RIP },
        { .form = X86_FORM_RET },
    };
    uint8_t bytes[BUSTER_X86_ENCODER_CAPACITY(BUSTER_ARRAY_LENGTH(records))];
    uint32_t offsets[BUSTER_ARRAY_LENGTH(records) + 1];
    const int64_t length = x86_encode(records, BUSTER_ARRAY_LENGTH(records), bytes, sizeof(bytes), offsets, NULL);
    const AtomId atom = length > 0 ? link_add_atom(&fixture.link, BUSTER_LINK_CLASS_TEXT, 0, (uint64_t)length, bytes, 0) : 0;
    fixture.valid = fixture.valid && atom != 0 && atom < BUSTER_ASSEMBLY_TEST_ATOMS;
    if (fixture.valid)
    {
        fixture.code[atom] = (AssemblyCode){ .records = records, .count = BUSTER_ARRAY_LENGTH(records) };
        assembly_fixture_define(&fixture, "wide_addend", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, atom, 0, (uint64_t)length);
        assembly_fixture_fixup(&fixture, atom, 3, BUSTER_LINK_FIXUP_PC32, "wide_target", INT64_MAX);
        static const char* const pieces[] = { "\t.long wide_target+9223372036854775807-.\n" };
        assembly_test_section_fixture(test, &fixture, NULL, "code-addend", pieces, BUSTER_ARRAY_LENGTH(pieces));
        fixture.code[atom].offsets = offsets;
        assembly_test_section_fixture(test, &fixture, NULL, "mixed-addend", pieces, BUSTER_ARRAY_LENGTH(pieces));
    }
    assembly_test_check(test, fixture.valid, "construct wide instruction addend fixture", NULL);
    assembly_fixture_end(&fixture);
    arena_pop_to(&test->arena, mark);
}

static void assembly_test_groups(AssemblyTest* test)
{
    const uint64_t mark = test->arena.length;
    AssemblyFixture fixture;
    assembly_fixture_begin(&fixture, &test->arena);
    ObjectAtomSection sections[BUSTER_ASSEMBLY_TEST_ATOMS] = { 0 };
    uint32_t memberships[BUSTER_ASSEMBLY_TEST_ATOMS] = { 0 };
    LinkGroup groups[6] = { 0 };
    const AtomId function = assembly_fixture_function(test, &fixture, "pick", BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_FUNCTION, 0, "mov eax, 17\nret\n");
    const SymbolId pick = assembly_fixture_symbol(&fixture, "pick", BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_FUNCTION);
    groups[1] = (LinkGroup){
        .name = fixture.link.symbols.name[pick],
        .signature = pick,
        .flags = BUSTER_LINK_GROUP_COMDAT,
    };
    groups[2] = (LinkGroup){
        .name = link_add_name(&fixture.link, (const uint8_t*)"quoted group", sizeof("quoted group") - 1),
        .flags = BUSTER_LINK_GROUP_COMDAT,
    };
    groups[3].name = link_add_name(&fixture.link, (const uint8_t*)"plain_signature", sizeof("plain_signature") - 1);
    const SymbolId weak = assembly_fixture_symbol(&fixture, "weak group", BUSTER_LINK_SYMBOL_WEAK);
    groups[4] = (LinkGroup){
        .name = fixture.link.symbols.name[weak],
        .signature = weak,
        .flags = BUSTER_LINK_GROUP_COMDAT,
    };
    memberships[function] = 1;
    const ObjectAtomSection shared = {
        .name = BUSTER_S8(".shared"),
        .type = SHT_PROGBITS,
        .flags = SHF_ALLOC | SHF_WRITE,
    };
    const uint64_t zero = 0;
    for (uint32_t i = 0; i < 4; i += 1)
    {
        const AtomId atom = assembly_fixture_data(test, &fixture, BUSTER_LINK_CLASS_DATA, i == 3 ? 4 : 3, &zero, sizeof(zero));
        sections[atom] = shared;
        memberships[atom] = i == 0 || i == 3 ? 1 : i == 1 ? 2 : 0;
        assembly_fixture_fixup(&fixture, atom, 0, BUSTER_LINK_FIXUP_ABS64, "pick", i);
    }
    // Equal descriptors rejoin their original section even after an identically named different identity.
    for (uint32_t i = 0; i < 3; i += 1)
    {
        const uint8_t byte = (uint8_t)(20 + i);
        const AtomId atom = assembly_fixture_data(test, &fixture, BUSTER_LINK_CLASS_DATA, 0, &byte, 1);
        sections[atom] = (ObjectAtomSection){
            .name = BUSTER_S8(".identity"),
            .type = SHT_PROGBITS,
            .flags = SHF_ALLOC | SHF_WRITE | SHF_GNU_RETAIN,
            .identity = i == 1 ? 2 : 1,
        };
        memberships[atom] = 1;
    }
    // Linked-order sections may belong to a group too. Distinct descriptors sharing the same linked atom stay apart.
    for (uint32_t i = 0; i < 2; i += 1)
    {
        const AtomId atom = assembly_fixture_data(test, &fixture, BUSTER_LINK_CLASS_DATA, 3, &zero, sizeof(zero));
        sections[atom] = (ObjectAtomSection){
            .name = BUSTER_S8(".entries"),
            .type = SHT_PROGBITS,
            .flags = SHF_ALLOC | SHF_WRITE | SHF_LINK_ORDER,
            .link = function,
            .identity = i + 1,
        };
        memberships[atom] = 1;
        assembly_fixture_fixup(&fixture, atom, 0, BUSTER_LINK_FIXUP_ABS64, "pick", i);
    }
    static const char string[] = "grouped";
    const AtomId strings = assembly_fixture_data(test, &fixture, BUSTER_LINK_CLASS_STRINGS, 0, string, sizeof(string));
    const AtomId bss = assembly_fixture_data(test, &fixture, BUSTER_LINK_CLASS_BSS, 2, NULL, 19);
    memberships[strings] = 2;
    memberships[bss] = 2;
    const uint8_t byte = 42;
    const AtomId plain = assembly_fixture_data(test, &fixture, BUSTER_LINK_CLASS_RODATA, 0, &byte, 1);
    sections[plain] = (ObjectAtomSection){
        .name = BUSTER_S8(".plain"),
        .type = SHT_PROGBITS,
        .flags = SHF_ALLOC,
        .identity = 7,
    };
    memberships[plain] = 3;
    const AtomId weak_member = assembly_fixture_data(test, &fixture, BUSTER_LINK_CLASS_RODATA, 0, &byte, 1);
    memberships[weak_member] = 4;
    const AtomId signature = assembly_fixture_data(test, &fixture, BUSTER_LINK_CLASS_RODATA, 0, &byte, 1);
    sections[signature] = (ObjectAtomSection){
        .name = BUSTER_S8(".signature"),
        .type = SHT_PROGBITS,
        .flags = SHF_ALLOC,
    };
    groups[5] = (LinkGroup){
        .name = link_add_name(&fixture.link, (const uint8_t*)".signature", sizeof(".signature") - 1),
        .signature_atom = signature,
        .flags = BUSTER_LINK_GROUP_COMDAT,
    };
    const AtomId member = assembly_fixture_data(test, &fixture, BUSTER_LINK_CLASS_RODATA, 0, &byte, 1);
    memberships[member] = 5;
    fixture.link.groups = (LinkGroups){
        .data = groups,
        .atom_groups = memberships,
        .count = BUSTER_ARRAY_LENGTH(groups),
        .atom_count = fixture.link.atoms.count,
    };
    static const char* const pieces[] = {
        "\t.section .shared,\"awG\",@progbits,pick,comdat\n",
        "\t.section .shared,\"awG\",@progbits,\"quoted group\",comdat\n",
        "\t.section .shared,\"aw\",@progbits\n",
        "\t.section .identity,\"awRG\",@progbits,pick,comdat,unique,",
        "\t.section .entries,\"awoG\",@progbits,pick,pick,comdat,unique,",
        "\t.section .rodata.str1.1,\"aMSG\",@progbits,1,\"quoted group\",comdat\n",
        "\t.section .plain,\"aG\",@progbits,plain_signature,,unique,",
        "\t.size \"weak group\", \"weak group\"-\"weak group\"\n",
        "\t.section .rodata,\"aG\",@progbits,.signature,comdat\n",
    };
    assembly_test_section_fixture(test, &fixture, sections, "groups", pieces, BUSTER_ARRAY_LENGTH(pieces));
    assembly_fixture_end(&fixture);
    arena_pop_to(&test->arena, mark);
}

static void assembly_test_unique(AssemblyTest* test)
{
    const uint64_t mark = test->arena.length;
    AssemblyFixture fixture;
    assembly_fixture_begin(&fixture, &test->arena);
    const uint32_t value = 42;
    const uint8_t classes[] = { BUSTER_LINK_CLASS_DATA, BUSTER_LINK_CLASS_BSS, BUSTER_LINK_CLASS_TDATA, BUSTER_LINK_CLASS_TBSS };
    static const char* const names[] = { "unique object", "unique_zero", "unique_tls", "unique_tls_zero" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(classes); i += 1)
    {
        const bool tls = link_class_is_tls(classes[i]);
        const bool zero = object_class_is_zero_fill(classes[i]);
        const AtomId atom = assembly_fixture_data(test, &fixture, classes[i], 2, zero ? NULL : &value, sizeof(value));
        const uint8_t flags = BUSTER_LINK_SYMBOL_UNIQUE | (tls ? BUSTER_LINK_SYMBOL_TLS : BUSTER_LINK_SYMBOL_OBJECT) |
                              (i == 0 ? BUSTER_LINK_SYMBOL_HIDDEN : BUSTER_LINK_SYMBOL_DEFAULT);
        assembly_fixture_define(&fixture, names[i], flags, atom, 0, sizeof(value));
    }
    const uint64_t zero = 0;
    const AtomId reference = assembly_fixture_data(test, &fixture, BUSTER_LINK_CLASS_DATA, 3, &zero, sizeof(zero));
    assembly_fixture_fixup(&fixture, reference, 0, BUSTER_LINK_FIXUP_ABS64, names[0], 0);
    static const char* const pieces[] = {
        "\t.hidden \"unique object\"\n\t.type \"unique object\", @gnu_unique_object\n",
        "\t.type unique_zero, @gnu_unique_object\n",
        "\t.type unique_tls, @gnu_unique_object\n",
        "\t.type unique_tls_zero, @gnu_unique_object\n",
    };
    assembly_test_section_fixture(test, &fixture, NULL, "unique", pieces, BUSTER_ARRAY_LENGTH(pieces));
    assembly_fixture_end(&fixture);
    arena_pop_to(&test->arena, mark);
}

// A unit the sink refuses or finds contradicting itself: a function and a datum, changed by `change` (below), which must
// fail with `error` and the reason `reason`, leaving the output arena as it was.
// Each represented data relocation retains its ELF kind and addend; TLS instructions also exercise the symbolic
// instruction-field modifiers independently of C lowering and the compiler's generated accessor sequences.
static void assembly_test_tls_fixups(AssemblyTest* test)
{
    const uint64_t mark = test->arena.length;
    AssemblyFixture fixture;
    assembly_fixture_begin(&fixture, &test->arena);
    const uint32_t word = 42;
    const AtomId initialized = assembly_fixture_data(test, &fixture, BUSTER_LINK_CLASS_TDATA, 2, &word, sizeof(word));
    const AtomId zeroed = assembly_fixture_data(test, &fixture, BUSTER_LINK_CLASS_TBSS, 5, NULL, 64);
    const uint8_t tls_flags = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_TLS;
    assembly_fixture_define(&fixture, "tls_initialized", tls_flags, initialized, 0, sizeof(word));
    assembly_fixture_define(&fixture, "tls_zero", BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_TLS, zeroed, 0, 64);
    const SymbolId external = assembly_fixture_symbol(&fixture, "tls_external", tls_flags);
    const SymbolId ordinary = assembly_fixture_symbol(&fixture, "ordinary_external", BUSTER_LINK_SYMBOL_GLOBAL);
    assembly_fixture_function(test, &fixture, "tls_access", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, 4,
                              "mov eax, dword ptr fs:[tls_initialized@TPOFF]\n"
                              "lea rax, [rax + tls_initialized@TPOFF]\n"
                              "mov rcx, qword ptr [rip + tls_external@GOTTPOFF]\n"
                              "lea rdi, [rip + tls_external@TLSGD]\n"
                              "lea rdi, [rip + tls_initialized@TLSLD]\n"
                              "lea rax, [rax + tls_initialized@DTPOFF]\nret\n");
    uint8_t zeros[(BUSTER_LINK_FIXUP_KINDS - 1) * 8] = { 0 };
    const AtomId data = assembly_fixture_data(test, &fixture, BUSTER_LINK_CLASS_DATA, 3, zeros, sizeof(zeros));
    assembly_fixture_define(&fixture, "all_fixups", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT, data, 0, sizeof(zeros));
    for (uint8_t kind = 1; fixture.valid && kind < BUSTER_LINK_FIXUP_KINDS; kind += 1)
    {
        const bool tls = kind == BUSTER_LINK_FIXUP_TPOFF32 || kind == BUSTER_LINK_FIXUP_GOTTPOFF || kind >= BUSTER_LINK_FIXUP_TLSGD;
        const int64_t addend = kind == BUSTER_LINK_FIXUP_ABS64 ? INT64_MIN + 1 : (kind & 1) != 0 ? -13 : 17;
        fixture.valid = link_add_fixup(&fixture.link, data, (kind - 1) * 8, kind, tls ? external : ordinary, addend) != 0;
    }
    static const char* const pieces[] = {
        "\t.section .tdata,\"awT\",@progbits\n",
        "\t.section .tbss,\"awT\",@nobits\n",
        "\t.type tls_external, @tls_object\n",
        "tls_initialized@TPOFF",
        "tls_external@GOTTPOFF(%rip)",
        "tls_external@TLSGD(%rip)",
        "tls_initialized@TLSLD(%rip)",
        "tls_initialized@DTPOFF(%rax)",
        "\t.reloc ., R_X86_64_32S, ordinary_external-13\n\t.long 0\n",
        "\t.quad tls_external@DTPOFF+17\n",
    };
    assembly_test_section_fixture(test, &fixture, NULL, "tls-fixups", pieces, BUSTER_ARRAY_LENGTH(pieces));
    static const char* const invalid[] = {
        ".data\n.reloc ., R_X86_64_32S, external\n.long 1\n",
        ".data\n.reloc ., R_X86_64_64, external\n.long 0\n",
        ".bss\n.reloc ., R_X86_64_64, external\n.zero 8\n",
        ".data\n.reloc ., R_X86_64_32S, external\n.long other\n",
        ".data\n.reloc ., R_X86_64_UNKNOWN, external\n.long 0\n",
        ".data\n.reloc .+4, R_X86_64_32S, external\n.quad 0\n",
        ".data\n.reloc ., R_X86_64_32S, external+9223372036854775808\n.long 0\n",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        const uint64_t scratch_mark = test->scratch.length;
        const AsmResult rejected = asm_assemble((String8){ .pointer = (char*)invalid[i], .length = strlen(invalid[i]) }, test->index, (AsmOptions){ 0 }, &test->scratch);
        assembly_test_check(test, rejected.error != NULL, "invalid explicit relocation fields are diagnosed", invalid[i]);
        arena_pop_to(&test->scratch, scratch_mark);
    }
    assembly_fixture_end(&fixture);
    arena_pop_to(&test->arena, mark);
}

enum
{
    ASSEMBLY_TEST_GROUP,
    ASSEMBLY_TEST_WEAK_COMMON,
    ASSEMBLY_TEST_ALIASED_COMMON,
    ASSEMBLY_TEST_UNCODED,
    ASSEMBLY_TEST_DUPLICATE,
    ASSEMBLY_TEST_PREFIX,
    ASSEMBLY_TEST_RECORDS,
    ASSEMBLY_TEST_INSIDE,
    ASSEMBLY_TEST_TARGETLESS,
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
    const AtomId data = assembly_fixture_data(test, &fixture, BUSTER_LINK_CLASS_DATA, 2, &word, sizeof(word));
    assembly_fixture_define(&fixture, "v", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT, data, 0, sizeof(word));
    if (change == ASSEMBLY_TEST_WEAK_COMMON || change == ASSEMBLY_TEST_ALIASED_COMMON)
    {
        const AtomId common_atom = assembly_fixture_data(test, &fixture, BUSTER_LINK_CLASS_BSS, 2, 0, sizeof(word));
        const uint8_t binding = change == ASSEMBLY_TEST_WEAK_COMMON ? BUSTER_LINK_SYMBOL_WEAK : BUSTER_LINK_SYMBOL_GLOBAL;
        assembly_fixture_define(&fixture, "common", BUSTER_LINK_SYMBOL_COMMON | binding, common_atom, 0, sizeof(word));
        if (change == ASSEMBLY_TEST_ALIASED_COMMON)
        {
            assembly_fixture_define(&fixture, "alias", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL, common_atom, 0, sizeof(word));
        }
    }
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
                const int64_t length = bytes != 0 ? x86_encode(&records[i], 1, bytes, BUSTER_X86_ENCODER_CAPACITY(1), 0, 0) : -1;
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

// A compile's arguments: `compiler`, --tier1 with `tier1`, the test's --option arguments, `mode` (-S or -c), -o `output`
// and `source`, null-terminated, into `arguments` (room for 24).
static void assembly_test_compile_arguments(const AssemblyTest* test, char** arguments, const char* compiler, bool tier1, const char* mode, const char* output,
                                            const char* source)
{
    uint32_t count = 0;
    arguments[count++] = (char*)compiler;
    if (tier1)
    {
        arguments[count++] = "--tier1";
    }
    for (uint32_t i = 0; i < test->option_count; i += 1)
    {
        arguments[count++] = test->options[i];
    }
    arguments[count++] = (char*)mode;
    arguments[count++] = "-o";
    arguments[count++] = (char*)output;
    arguments[count++] = (char*)source;
    arguments[count] = 0;
}

// Runs a program (`arguments`, its path first) in `directory`, its standard output and errors into the files at
// `output_path` and `error_path`, for up to BUSTER_OBJECT_TEST_RUN_SECONDS, as object_test_run does: its exit status, or
// -1 if it couldn't start, was killed by a signal or ran out of time. A program that writes files (c-testsuite's
// 00187.c writes fred.txt) writes them there.
static int assembly_test_run_in(char* const* arguments, const char* directory, const char* output_path, const char* error_path)
{
    int result = -1;
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) == 0)
    {
        pid_t child = 0;
        const bool ready = posix_spawn_file_actions_addopen(&actions, 1, output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0 &&
                           posix_spawn_file_actions_addopen(&actions, 2, error_path, O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0 &&
                           posix_spawn_file_actions_addchdir_np(&actions, directory) == 0;
        if (ready && posix_spawn(&child, arguments[0], &actions, 0, arguments, environ) == 0)
        {
            int status = 0;
            pid_t waited = 0;
            const struct timespec pause = {
                .tv_sec = 0,
                .tv_nsec = 1000000,
            };
            for (uint64_t milliseconds = 0; waited == 0 && milliseconds < 1000ull * BUSTER_OBJECT_TEST_RUN_SECONDS; milliseconds += 1)
            {
                waited = waitpid(child, &status, WNOHANG);
                if (waited == 0)
                {
                    nanosleep(&pause, 0);
                }
            }
            if (waited == 0)
            {
                kill(child, SIGKILL);
                waitpid(child, &status, 0);
            }
            else if (waited == child && WIFEXITED(status))
            {
                result = WEXITSTATUS(status);
            }
        }
        posix_spawn_file_actions_destroy(&actions);
    }
    return result;
}

// Whether a file's bytes are `expected`'s (a file too), both read whole.
static bool assembly_test_same_file(AssemblyTest* test, const char* path, const char* expected_path)
{
    const uint64_t mark = test->scratch.length;
    const String8 actual = object_test_read_file(&test->scratch, path);
    const String8 expected = object_test_read_file(&test->scratch, expected_path);
    const bool same = actual.pointer != 0 && expected.pointer != 0 && actual.length == expected.length && memcmp(actual.pointer, expected.pointer, actual.length) == 0;
    arena_pop_to(&test->scratch, mark);
    return same;
}

// -S of one c-testsuite program (#1362's stage 2 acceptance), by this executable (`compiler`) with `tier1` or not: its
// text must reassemble into -c's object, with the form contract's normalizations (assembly_test_same_objects).
// The program linked from the reassembled object must print the expected output and exit 0.
// Returns whether it passed, with the first failure in `error`.
static bool assembly_test_c_program(AssemblyTest* test, const char* compiler, const char* source, const char* expected, const char* name, bool tier1,
                                    bool own_assembler, char* error, uint64_t error_size)
{
    char text_path[PATH_MAX];
    char object_path[PATH_MAX];
    char reassembled_path[PATH_MAX];
    char program_path[PATH_MAX];
    char output_path[PATH_MAX];
    char errors_path[PATH_MAX];
    snprintf(text_path, sizeof(text_path), "%s/%s.s", test->directory, name);
    snprintf(object_path, sizeof(object_path), "%s/%s.o", test->directory, name);
    snprintf(reassembled_path, sizeof(reassembled_path), "%s/%s.as.o", test->directory, name);
    snprintf(program_path, sizeof(program_path), "%s/%s.exe", test->directory, name);
    snprintf(output_path, sizeof(output_path), "%s/%s.out", test->directory, name);
    snprintf(errors_path, sizeof(errors_path), "%s/%s.err", test->directory, name);
    char* assembly[24];
    char* object[24];
    assembly_test_compile_arguments(test, assembly, compiler, tier1, "-S", text_path, source);
    assembly_test_compile_arguments(test, object, compiler, tier1, "-c", object_path, source);
    char* assembler[] = { "as", "--64", "-o", reassembled_path, text_path, 0 };
    char* buster[] = { (char*)compiler, "assemble", "-o", reassembled_path, text_path, 0 };
    // (Some of the programs call libm's functions, which the direct path's C link brings and an object's link names.)
    char* link[] = { (char*)compiler, "-o", program_path, reassembled_path, "-lm", 0 };
    char* program[] = { program_path, 0 };
    unlink(reassembled_path);
    snprintf(error, error_size, "-S failed (see %s)", errors_path);
    bool passed = object_test_run(assembly, output_path, errors_path) == 0;
    if (passed)
    {
        snprintf(error, error_size, "-c failed (see %s)", errors_path);
        passed = object_test_run(object, output_path, errors_path) == 0;
    }
    if (passed)
    {
        const char* tool = own_assembler ? "Buster's assembler" : "GNU as";
        snprintf(error, error_size, "%s didn't assemble %s (see %s)", tool, text_path, errors_path);
        passed = own_assembler ? object_test_run(buster, output_path, errors_path) == 0 :
                                 oracle_epoch_file(text_path) && asm_test_tool(assembler, reassembled_path, output_path, errors_path) == 0;
    }
    if (passed)
    {
        const uint64_t mark = test->scratch.length;
        const String8 ours = object_test_read_file(&test->scratch, object_path);
        const String8 theirs = object_test_read_file(&test->scratch, reassembled_path);
        passed = ours.pointer != 0 && assembly_test_same_objects(test, (const uint8_t*)ours.pointer, ours.length, theirs, error, error_size);
        arena_pop_to(&test->scratch, mark);
    }
    if (passed)
    {
        passed = object_test_run(link, output_path, errors_path) == 0;
        snprintf(error, error_size, "the reassembled object didn't link (see %s)", errors_path);
    }
    if (passed)
    {
        passed = assembly_test_run_in(program, test->directory, output_path, errors_path) == 0 && assembly_test_same_file(test, output_path, expected);
        snprintf(error, error_size, "the reassembled program didn't print %s and exit 0 (see %s)", expected, output_path);
    }
    if (passed && !test->keep)
    {
        unlink(text_path);
        unlink(object_path);
        unlink(reassembled_path);
        unlink(program_path);
        unlink(output_path);
        unlink(errors_path);
    }
    return passed;
}

// Compare sh_link and relocations as well as the NOP bytes and the function symbols in their sleds.
static void assembly_test_patchable(AssemblyTest* test, const char* source, const char* fixture, bool cross_check)
{
    char compiler[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", compiler, sizeof(compiler) - 1);
    compiler[length > 0 ? length : 0] = 0;
    char expected[PATH_MAX];
    snprintf(expected, sizeof(expected), "%s/patchable-expected.txt", test->directory);
    const bool ready = length > 0 && access(source, R_OK) == 0 &&
                       object_write_file(expected, (const uint8_t*)"", 0) == OBJECT_ERROR_NONE;
    assembly_test_check(test, ready, "prepare patchable function entry fixture", source);
    for (uint32_t tier = 0; ready && tier < 2; tier += 1)
    {
        char name[64];
        char error[512] = { 0 };
        snprintf(name, sizeof(name), "%s-tier%u-buster", fixture, tier);
        const bool buster = assembly_test_c_program(test, compiler, source, expected, name, tier != 0, true, error, sizeof(error));
        assembly_test_check(test, buster, "patchable entry tables, code and symbols round-trip through Buster", error);
        if (cross_check)
        {
            snprintf(name, sizeof(name), "%s-tier%u-gnu", fixture, tier);
            const bool gnu = assembly_test_c_program(test, compiler, source, expected, name, tier != 0, false, error, sizeof(error));
            assembly_test_check(test, gnu, "patchable entry tables, code and symbols round-trip through GNU as", error);
        }
    }
    if (!test->keep)
    {
        unlink(expected);
    }
}

// --compare SOURCE: -S of a C source by this executable (`compiler`), with `tier1` or not, assembled by GNU as into -c's
// object, but for the form contract's normalizations; no link. For sources larger than the c-testsuite's programs.
static bool assembly_test_compare(AssemblyTest* test, const char* compiler, const char* source, const char* name, bool tier1, char* error, uint64_t error_size)
{
    char text_path[PATH_MAX];
    char object_path[PATH_MAX];
    char reassembled_path[PATH_MAX];
    char output_path[PATH_MAX];
    char errors_path[PATH_MAX];
    snprintf(text_path, sizeof(text_path), "%s/%s.s", test->directory, name);
    snprintf(object_path, sizeof(object_path), "%s/%s.o", test->directory, name);
    snprintf(reassembled_path, sizeof(reassembled_path), "%s/%s.as.o", test->directory, name);
    snprintf(output_path, sizeof(output_path), "%s/%s.out", test->directory, name);
    snprintf(errors_path, sizeof(errors_path), "%s/%s.err", test->directory, name);
    char* assembly[24];
    char* object[24];
    assembly_test_compile_arguments(test, assembly, compiler, tier1, "-S", text_path, source);
    assembly_test_compile_arguments(test, object, compiler, tier1, "-c", object_path, source);
    char* assembler[] = { "as", "--64", "-o", reassembled_path, text_path, 0 };
    unlink(reassembled_path);
    bool passed = object_test_run(assembly, output_path, errors_path) == 0;
    snprintf(error, error_size, "-S failed (see %s)", errors_path);
    passed = passed && object_test_run(object, output_path, errors_path) == 0;
    snprintf(error, error_size, passed ? "GNU as didn't assemble %s (see %s)" : "-c failed (see %s)", passed ? text_path : errors_path, errors_path);
    passed = passed && oracle_epoch_file(text_path) && asm_test_tool(assembler, reassembled_path, output_path, errors_path) == 0;
    if (passed)
    {
        const uint64_t mark = test->scratch.length;
        const String8 ours = object_test_read_file(&test->scratch, object_path);
        const String8 theirs = object_test_read_file(&test->scratch, reassembled_path);
        passed = ours.pointer != 0 && assembly_test_same_objects(test, (const uint8_t*)ours.pointer, ours.length, theirs, error, error_size);
        arena_pop_to(&test->scratch, mark);
    }
    if (passed && !test->keep)
    {
        unlink(text_path);
        unlink(object_path);
        unlink(reassembled_path);
        unlink(output_path);
        unlink(errors_path);
    }
    return passed;
}

// Inline templates exercise placed records, record helpers, naked bodies, directive fragments and file-scope code.
// The executable checks their semantics independently of the object comparator at both tiers and with DWARF enabled.
static void assembly_test_inline(AssemblyTest* test, const char* compiler)
{
    const char* source = "tests/assembly-output/inline.c";
    char expected[PATH_MAX];
    snprintf(expected, sizeof(expected), "%s/inline-expected.txt", test->directory);
    const bool ready = access(source, R_OK) == 0 && object_write_file(expected, (const uint8_t*)"", 0) == OBJECT_ERROR_NONE;
    assembly_test_check(test, ready, "prepare inline assembly round-trip fixture", source);
    for (uint32_t debug = 0; ready && debug < 2; debug += 1)
    {
        AssemblyTest options = *test;
        options.options[options.option_count++] = debug ? "-g" : "-g0";
        for (uint32_t tier = 0; tier < 2; tier += 1)
        {
            for (uint32_t gnu = 0; gnu < (test->cross_check ? 2u : 1u); gnu += 1)
            {
                char name[64];
                char error[512] = { 0 };
                snprintf(name, sizeof(name), "inline-%u-%u-%u", debug, tier, gnu);
                const bool same = assembly_test_c_program(&options, compiler, source, expected, name, tier != 0, gnu == 0, error, sizeof(error));
                assembly_test_check(test, same, gnu ? "GNU as preserves inline assembly code, data, symbols and frames" :
                                                    "Buster preserves inline assembly code, data, symbols and frames", error);
            }
        }
    }
    if (!test->keep)
    {
        unlink(expected);
    }
}

// TLS accesses must retain all four models' semantics, and CFI must support an actual unwinder. Exercise both
// reassemblers, both compiler tiers, debug information and PIC on independent executable fixtures.
static void assembly_test_runtime_features(AssemblyTest* test, const char* compiler)
{
    static const char* const fixtures[] = { "tls", "multiversion", "cfi", "groups" };
    char expected[PATH_MAX];
    snprintf(expected, sizeof(expected), "%s/runtime-expected.txt", test->directory);
    const bool ready = test->option_count + 2 <= BUSTER_ARRAY_LENGTH(test->options) && object_write_file(expected, (const uint8_t*)"", 0) == OBJECT_ERROR_NONE;
    assembly_test_check(test, ready, "prepare assembly runtime fixtures", NULL);
    for (uint32_t f = 0; ready && f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        char source[PATH_MAX];
        snprintf(source, sizeof(source), "tests/assembly-output/%s.c", fixtures[f]);
        for (uint32_t mode = 0; mode < 4; mode += 1)
        {
            AssemblyTest options = *test;
            options.options[options.option_count++] = (mode & 1) != 0 ? "-g" : "-g0";
            options.options[options.option_count++] = (mode & 2) != 0 ? "-fPIC" : "-fno-PIC";
            for (uint32_t tier = 0; tier < 2; tier += 1)
            {
                for (uint32_t reader = 0; reader < (test->cross_check ? 2u : 1u); reader += 1)
                {
                    char name[96];
                    char error[512] = { 0 };
                    snprintf(name, sizeof(name), "%s-mode%u-tier%u-%s", fixtures[f], mode, tier, reader == 0 ? "buster" : "gnu");
                    const bool passed = assembly_test_c_program(&options, compiler, source, expected, name, tier != 0, reader == 0, error, sizeof(error));
                    assembly_test_check(test, passed, name, error);
                }
            }
        }
    }
    if (!test->keep)
    {
        unlink(expected);
    }
}

// Kernel-style extraction consumes the substituted source spelling. The raw variant intentionally
// cannot assemble. The .ascii variant can, including the DWARF for the ordinary code beside it.
static void assembly_test_markers(AssemblyTest* test, const char* compiler)
{
    static const char fixture[] =
        "struct layout { char tag; long value; };\n"
        "#ifdef RAW\n#define MARK(x) \"\\n->\" x\n#else\n#define MARK(x) \"\\n.ascii \\\"->\" x \"\\\"\"\n#endif\n"
        "void offsets(void) {\n"
        "  __asm__ volatile(MARK(\"VALUE %0\") : : \"i\"(__builtin_offsetof(struct layout, value)));\n"
        "  if (0) __asm__ volatile(MARK(\"DEAD\") : :);\n"
        "  __asm__ volatile(MARK(\"SIZE %c[size]\") : : [size] \"i\"(sizeof(struct layout)));\n}\n"
        "static int state = 1;\n"
        "__attribute__((noinline,section(\".text.work\"))) int work(int input) { int result = input + state; state = result; return result; }\n"
        "int main(void) { return work(4) != 5; }\n";
    char source[PATH_MAX];
    char output[PATH_MAX];
    char errors[PATH_MAX];
    char object[PATH_MAX];
    char control[PATH_MAX];
    snprintf(source, sizeof(source), "%s/offsets.c", test->directory);
    snprintf(output, sizeof(output), "%s/offsets.s", test->directory);
    snprintf(errors, sizeof(errors), "%s/offsets.err", test->directory);
    snprintf(object, sizeof(object), "%s/offsets.o", test->directory);
    snprintf(control, sizeof(control), "%s/offsets-direct.o", test->directory);
    const bool ready = object_write_file(source, (const uint8_t*)fixture, sizeof(fixture) - 1) == OBJECT_ERROR_NONE;
    assembly_test_check(test, ready, "write inline assembly marker fixture", NULL);
    for (uint32_t tier = 0; ready && tier < 2; tier += 1)
    {
        for (uint32_t raw = 0; raw < 2; raw += 1)
        {
            char* args[] = { (char*)compiler, "-g", raw ? "-DRAW" : "-URAW", "-S", "-o", "-", source, tier ? "--tier1" : NULL, NULL };
            const bool compiled = object_test_run(args, output, errors) == 0;
            const uint64_t mark = test->scratch.length;
            const String8 text = compiled ? object_test_read_file(&test->scratch, output) : (String8){ 0 };
            const char* value = raw ? "\n->VALUE $8\n" : "\n.ascii \"->VALUE $8\"\n";
            const char* size = raw ? "\n->SIZE 16\n" : "\n.ascii \"->SIZE 16\"\n";
            const char* first = text.pointer != NULL ? memmem(text.pointer, text.length, value, strlen(value)) : NULL;
            const char* second = text.pointer != NULL ? memmem(text.pointer, text.length, size, strlen(size)) : NULL;
            const bool extracted = first != NULL && second != NULL && first < second && memmem(text.pointer, text.length, "->DEAD", 6) == NULL;
            assembly_test_check(test, compiled && extracted, "extract checked offsets in source order at both tiers", errors);
            arena_pop_to(&test->scratch, mark);
            if (raw)
            {
                char* direct[] = { (char*)compiler, "-c", "-DRAW", source, "-o", object, tier ? "--tier1" : NULL, NULL };
                assembly_test_check(test, object_test_run(direct, "/dev/null", errors) == 1, "direct object emission still diagnoses raw marker text", errors);
            }
            else
            {
                char* reassemble[] = { (char*)compiler, "-c", output, "-o", object, NULL };
                char* direct[] = { (char*)compiler, "-g", "-c", source, "-o", control, tier ? "--tier1" : NULL, NULL };
                const bool built = compiled && object_test_run(reassemble, "/dev/null", errors) == 0 && object_test_run(direct, "/dev/null", errors) == 0;
                const String8 expected = built ? object_test_read_file(&test->scratch, control) : (String8){ 0 };
                const String8 actual = built ? object_test_read_file(&test->scratch, object) : (String8){ 0 };
                char difference[512] = { 0 };
                const bool same = expected.pointer != NULL && assembly_test_same_objects(test, (const uint8_t*)expected.pointer, expected.length, actual, difference, sizeof(difference));
                assembly_test_check(test, same, "Buster preserves offset marker bytes, DWARF and relocations", built ? difference : errors);
                arena_pop_to(&test->scratch, mark);
            }
        }
        // Exercise section offsets, global addresses, function addresses and value location lists.
        AssemblyTest options = *test;
        options.options[0] = "-g";
        options.option_count = 1;
        char error[512] = { 0 };
        const bool same = assembly_test_compare(&options, compiler, source, tier ? "offsets-debug-1" : "offsets-debug-0", tier != 0, error, sizeof(error));
        assembly_test_check(test, same, "GNU as preserves offset marker bytes, DWARF and relocations", error);
    }
    if (!test->keep)
    {
        unlink(source);
        unlink(output);
        unlink(errors);
        unlink(object);
        unlink(control);
    }
}

// --c-testsuite DIRECTORY --subset FILE: assembly_test_c_program for each program the subset lists (its lines of digits
// alone, as test-c-testsuite reads it), at Tier 0 and with --tier1.
static void assembly_test_c_testsuite(AssemblyTest* test, const char* directory, const char* subset)
{
    char compiler[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", compiler, sizeof(compiler) - 1);
    compiler[length > 0 ? length : 0] = 0;
    FILE* list = fopen(subset, "r");
    assembly_test_check(test, length > 0 && list != 0, "--c-testsuite's executable and subset list", subset);
    char line[256];
    uint32_t programs = 0;
    uint32_t passed = 0;
    while (list != 0 && fgets(line, sizeof(line), list) != 0)
    {
        uint64_t digits = 0;
        while (line[digits] >= '0' && line[digits] <= '9')
        {
            digits += 1;
        }
        if (digits != 0 && (line[digits] == '\n' || line[digits] == 0))
        {
            line[digits] = 0;
            char source[PATH_MAX];
            char expected[PATH_MAX];
            snprintf(source, sizeof(source), "%s/tests/single-exec/%s.c", directory, line);
            snprintf(expected, sizeof(expected), "%s.expected", source);
            for (uint32_t tier = 0; tier < 2; tier += 1)
            {
                char name[64];
                snprintf(name, sizeof(name), "%s-tier%u", line, tier);
                char error[512] = { 0 };
                const bool ok = assembly_test_c_program(test, compiler, source, expected, name, tier == 1, false, error, sizeof(error));
                char detail[640];
                snprintf(detail, sizeof(detail), "%s.c at Tier %u: %s", line, tier, error);
                assembly_test_check(test, ok, "c-testsuite", detail);
                programs += 1;
                passed += ok;
            }
        }
    }
    if (list != 0)
    {
        fclose(list);
    }
    printf("assembly output: c-testsuite: %u of %u -S compiles (Tier 0 and --tier1) reassembled by GNU as into -c's object and ran\n", passed, programs);
}

static int run_assembly_output_tests(int count, char** arguments)
{
    AssemblyTest test = {
        .arena = arena_reserve(1ull << 30),
        .output = arena_reserve(1ull << 30),
        .scratch = arena_reserve(1ull << 30),
    };
    bool valid = oracle_epoch_begin();
    const char* c_testsuite = 0;
    const char* subset = 0;
    const char* compared[64];
    uint32_t compared_count = 0;
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
        else if (strcmp(arguments[i], "--c-testsuite") == 0 && i + 1 < count)
        {
            c_testsuite = arguments[++i];
        }
        else if (strcmp(arguments[i], "--subset") == 0 && i + 1 < count)
        {
            subset = arguments[++i];
        }
        else if (strcmp(arguments[i], "--compare") == 0 && i + 1 < count && compared_count < BUSTER_ARRAY_LENGTH(compared))
        {
            compared[compared_count++] = arguments[++i];
        }
        else if (strcmp(arguments[i], "--option") == 0 && i + 1 < count && test.option_count < BUSTER_ARRAY_LENGTH(test.options))
        {
            test.options[test.option_count++] = arguments[++i];
        }
        else
        {
            valid = false;
        }
    }
    if (!valid || (c_testsuite != 0) != (subset != 0))
    {
        fprintf(stderr, "usage: compiler test-assembly-output [--cross-check] [--keep] [--c-testsuite DIRECTORY --subset FILE] [--compare SOURCE]... [--option ARGUMENT]...\n");
        valid = false;
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
        assembly_test_cfi_rules(&test);
        assembly_test_cfi_boundaries(&test);
        assembly_test_small(&test);
        assembly_test_program(&test);
        assembly_test_groups(&test);
        assembly_test_unique(&test);
        assembly_test_mixed(&test);
        assembly_test_wide_code_addend(&test);
        assembly_test_tls_fixups(&test);
        assembly_test_refusal(&test, ASSEMBLY_TEST_GROUP, ASSEMBLY_INVALID, "invalid section groups");
        assembly_test_refusal(&test, ASSEMBLY_TEST_WEAK_COMMON, ASSEMBLY_UNSUPPORTED, "a non-global common definition");
        assembly_test_refusal(&test, ASSEMBLY_TEST_ALIASED_COMMON, ASSEMBLY_UNSUPPORTED, "sharing an atom");
        assembly_test_refusal(&test, ASSEMBLY_TEST_UNCODED, ASSEMBLY_UNSUPPORTED, "without instruction records");
        assembly_test_refusal(&test, ASSEMBLY_TEST_DUPLICATE, ASSEMBLY_UNSUPPORTED, "two symbols of the same name");
        assembly_test_refusal(&test, ASSEMBLY_TEST_PREFIX, ASSEMBLY_UNSUPPORTED, BUSTER_ASSEMBLY_PREFIX);
        assembly_test_refusal(&test, ASSEMBLY_TEST_RECORDS, ASSEMBLY_INVALID, "don't encode to their function's bytes");
        assembly_test_refusal(&test, ASSEMBLY_TEST_INSIDE, ASSEMBLY_INVALID, "a symbol inside an instruction");
        assembly_test_refusal(&test, ASSEMBLY_TEST_TARGETLESS, ASSEMBLY_INVALID, "a relative branch without a target");
        assembly_test_refusal(&test, ASSEMBLY_TEST_BND_CALL, ASSEMBLY_UNSUPPORTED, "GNU as encodes otherwise");
        assembly_test_patchable(&test, "tests/assembly-output/patchable.c", "patchable", test.cross_check);
        // GNU as refuses a section named after an ordinary symbol. Compare this spelling with direct object emission.
        assembly_test_patchable(&test, "tests/assembly-output/patchable-collisions.c", "patchable-collisions", false);
        if (test.cross_check)
        {
            assembly_test_forms(&test, 2);
        }
        if (c_testsuite != 0)
        {
            assembly_test_c_testsuite(&test, c_testsuite, subset);
        }
        char compiler[PATH_MAX];
        const ssize_t length = readlink("/proc/self/exe", compiler, sizeof(compiler) - 1);
        compiler[length > 0 ? length : 0] = 0;
        if (length > 0 && test.option_count < BUSTER_ARRAY_LENGTH(test.options))
        {
            assembly_test_inline(&test, compiler);
            assembly_test_runtime_features(&test, compiler);
        }
        if (test.cross_check)
        {
            assembly_test_check(&test, length > 0, "locate the compiler for offset marker tests", NULL);
            if (length > 0)
            {
                assembly_test_markers(&test, compiler);
            }
        }
        for (uint32_t i = 0; i < compared_count; i += 1)
        {
            for (uint32_t tier = 0; tier < 2; tier += 1)
            {
                char name[64];
                snprintf(name, sizeof(name), "compare-%u-tier%u", i, tier);
                char error[512] = { 0 };
                const bool same = length > 0 && assembly_test_compare(&test, compiler, compared[i], name, tier == 1, error, sizeof(error));
                char detail[PATH_MAX + 640];
                snprintf(detail, sizeof(detail), "%s at Tier %u: %s", compared[i], tier, error);
                assembly_test_check(&test, same, "--compare", detail);
                printf("assembly output: %s at Tier %u: %s\n", compared[i], tier, same ? "-S reassembled by GNU as into -c's object" : "failed");
            }
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

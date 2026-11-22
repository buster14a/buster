// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// #1503: dynamic_test.c includes this after its small programs and lane helpers. Position-dependent dynamic executables
// (-no-pie: ExecutableDynamic.position_dependent), written against the position-dependent executable's runtime
// (RUNTIME_KIND_NO_PIE: crt1.o, crti.o, crtbegin.o, crtend.o and crtn.o with the dynamic PIE's libraries):
// - the PIE's small programs (dynamic_test_small), none refused: each written by every lane count into the same bytes,
//   and by the exact twins with 1 and 8 lanes into them too; the file an ET_EXEC whose PT_LOADs are at
//   BUSTER_EXECUTABLE_BASE from their file offsets, with the interpreter, .dynamic and no DF_1_PIE, RELR or RELATIVE
//   relocation; the fields a PIE refuses hold their values (an ABS32 and a .rodata ABS64 of main's address, a PC32 to a
//   weak reference to nothing, address 0's distance, and PC64s to a hidden one and to an absolute symbol); and each
//   runs, with status 0, or 1 for the three absolute cases whose main reads buster_data for 42, which they don't write;
// - an import's .rodata ABS64 (environ's), a text relocation, refused with its fixup by every lane count and the exact
//   twins, without an output; and its .data ABS64, an R_X86_64_64 against it, which runs;
// - a weak reference to nothing that an ABS32 takes and that a function calls: both at address 0, without a stub or a
//   GOT slot, as GNU ld resolves them, and it runs.

typedef enum DynamicTestNoPieCase
{
    DYNAMIC_TEST_NO_PIE_IMPORT_TEXT,
    DYNAMIC_TEST_NO_PIE_IMPORT_POINTER,
    DYNAMIC_TEST_NO_PIE_WEAK_ZERO,
    DYNAMIC_TEST_NO_PIE_CASES,
} DynamicTestNoPieCase;

static const char* const dynamic_test_no_pie_names[DYNAMIC_TEST_NO_PIE_CASES] = {
    [DYNAMIC_TEST_NO_PIE_IMPORT_TEXT] = "an import's .rodata pointer",
    [DYNAMIC_TEST_NO_PIE_IMPORT_POINTER] = "an import's .data pointer",
    [DYNAMIC_TEST_NO_PIE_WEAK_ZERO] = "a weak reference to nothing by ABS32 and a call",
};

typedef struct DynamicTestNoPieProgram DynamicTestNoPieProgram;
struct DynamicTestNoPieProgram
{
    BindTestLink test;
    // The fixup that must be refused, or 0 for a program that runs; the data's atom, its fixup, and the weak symbol,
    // buster_never and environ.
    FixupId refused;
    AtomId data;
    FixupId fixup;
    SymbolId weak;
    SymbolId never;
    SymbolId imported;
    bool bound;
};

// main returns 0; buster_never calls buster_maybe in the weak case; buster_data is 8 bytes in .rodata or .data with the
// case's fixup: an ABS64 to environ, or an ABS32 to buster_maybe.
static void dynamic_test_no_pie_program(DynamicTestNoPieProgram* program, const Runtime* runtime, DynamicTestNoPieCase which)
{
    BindTestLink* test = &program->test;
    *program = (DynamicTestNoPieProgram){ 0 };
    bool built = bind_test_link_begin(test, runtime, bind_test_room(runtime));
    const uint8_t program_source = BUSTER_LINK_SOURCE_PROGRAM;
    const SymbolId main = built ? bind_test_symbol(test, "main", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, program_source) : 0;
    const SymbolId data = built ? bind_test_symbol(test, "buster_data", BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT, program_source) : 0;
    const SymbolId never = built ? bind_test_symbol(test, "buster_never", BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_FUNCTION, program_source) : 0;
    const SymbolId weak = built ? bind_test_symbol(test, "buster_maybe", BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_NOTYPE, BUSTER_LINK_SOURCE_NONE) : 0;
    const SymbolId imported = built ? bind_test_symbol(test, "environ", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_NOTYPE, BUSTER_LINK_SOURCE_NONE) : 0;
    built = main != 0 && data != 0 && never != 0 && weak != 0 && imported != 0 &&
            bind_begin(&test->bind, &test->link, runtime, BIND_PATH_VECTOR, &test->bytes, 1, &test->arena);
    const X86Instruction main_code[] = {
        { .immediate = 0, .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R32_IMM32 },
        { .form = X86_FORM_RET },
    };
    built = built && bind_test_code(test, main_code, BUSTER_ARRAY_LENGTH(main_code), 0, 0, main);
    const X86Instruction never_code[] = {
        { .form = X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP },
        { .form = X86_FORM_RET },
    };
    const LinkReference never_references[] = {
        { .instruction = 0, .field = X86_FIXUP_DISPLACEMENT, .kind = BUSTER_LINK_FIXUP_CALL, .target = weak, .addend = 0 },
    };
    const bool calls = which == DYNAMIC_TEST_NO_PIE_WEAK_ZERO;
    built = built && bind_test_code(test, never_code, BUSTER_ARRAY_LENGTH(never_code), never_references, calls ? 1 : 0, never);
    uint8_t* bytes = built ? arena_push(&test->bytes, 8, 8) : 0;
    const uint8_t class = which == DYNAMIC_TEST_NO_PIE_IMPORT_TEXT ? BUSTER_LINK_CLASS_RODATA : BUSTER_LINK_CLASS_DATA;
    const AtomId atom = bytes != 0 ? link_add_atom(&test->link, class, 3, 8, bytes, 0) : 0;
    if (bytes != 0)
    {
        memset(bytes, 0, 8);
    }
    built = atom != 0 && link_define_symbol(&test->link, data, atom, 0, 8);
    const uint8_t kind = which == DYNAMIC_TEST_NO_PIE_WEAK_ZERO ? BUSTER_LINK_FIXUP_ABS32 : BUSTER_LINK_FIXUP_ABS64;
    const FixupId fixup = built ? link_add_fixup(&test->link, atom, 0, kind, which == DYNAMIC_TEST_NO_PIE_WEAK_ZERO ? weak : imported, 0) : 0;
    program->refused = which == DYNAMIC_TEST_NO_PIE_IMPORT_TEXT ? fixup : 0;
    program->data = atom;
    program->fixup = fixup;
    program->weak = weak;
    program->never = never;
    program->imported = imported;
    program->bound = built && fixup != 0 && bind_end(&test->bind, &test->link) && link_check(&test->link) == LINK_ERROR_NONE;
}

// The file read as dynamic_test_image reads a PIE's, but an ET_EXEC.
static DynamicTestImage dynamic_test_no_pie_image(String8 file)
{
    const ExecutableTestImage image = executable_test_image_of(file, ET_EXEC);
    return (DynamicTestImage){
        .file = file,
        .header = image.header,
        .program_headers = image.program_headers,
        .section_headers = image.section_headers,
        .names = image.names,
        .valid = image.valid,
    };
}

// A position-dependent executable's file: an ET_EXEC from BUSTER_EXECUTABLE_BASE with the interpreter and .dynamic, and
// no DF_1_PIE, DT_RELR, DT_RELACOUNT or RELATIVE relocation; *relocations its R_X86_64_64 relocations at `place`
// against a symbol, or 0 with none.
static void dynamic_test_no_pie_structure(DynamicTestTally* tally, String8 file, uint64_t place, uint32_t* pointers, uint64_t index)
{
    const DynamicTestImage image = dynamic_test_no_pie_image(file);
    const Elf64_Phdr* interpreter = image.valid ? dynamic_test_segment(&image, PT_INTERP) : 0;
    const Elf64_Phdr* dynamic = image.valid ? dynamic_test_segment(&image, PT_DYNAMIC) : 0;
    bool placed = image.valid;
    for (uint32_t p = 0; image.valid && p < image.header->e_phnum; p += 1)
    {
        const Elf64_Phdr* segment = &image.program_headers[p];
        placed = placed && (segment->p_type != PT_LOAD || (segment->p_vaddr >= BUSTER_EXECUTABLE_BASE && segment->p_vaddr - BUSTER_EXECUTABLE_BASE == segment->p_offset));
    }
    dynamic_test_expect(tally, image.valid && image.header->e_type == ET_EXEC && image.header->e_entry >= BUSTER_EXECUTABLE_BASE && interpreter != 0 && dynamic != 0 && placed,
                        "an ET_EXEC at the fixed base with the interpreter and .dynamic", index);
    uint64_t flags = 0;
    uint64_t unused = 0;
    const bool pie = image.valid && dynamic_test_tag(&image, DT_FLAGS_1, &flags) && (flags & DF_1_PIE) != 0;
    const bool relr = image.valid && (dynamic_test_tag(&image, DT_RELR, &unused) || dynamic_test_tag(&image, DT_RELACOUNT, &unused));
    const Elf64_Shdr* rela = image.valid ? dynamic_test_section(&image, ".rela.dyn") : 0;
    const uint8_t* bytes = rela != 0 ? dynamic_test_bytes(&image, rela) : 0;
    uint64_t relative = 0;
    *pointers = 0;
    for (uint64_t at = 0; bytes != 0 && at + sizeof(Elf64_Rela) <= rela->sh_size; at += sizeof(Elf64_Rela))
    {
        Elf64_Rela relocation;
        memcpy(&relocation, bytes + at, sizeof(relocation));
        relative += ELF64_R_TYPE(relocation.r_info) == R_X86_64_RELATIVE ? 1 : 0;
        *pointers += ELF64_R_TYPE(relocation.r_info) == R_X86_64_64 && ELF64_R_SYM(relocation.r_info) != 0 && relocation.r_offset == place ? 1 : 0;
    }
    dynamic_test_expect(tally, image.valid && !pie && !relr && relative == 0, "no DF_1_PIE, RELR or RELATIVE relocation", index);
}

// The link written by every lane count into the same bytes (dynamic_test_lanes), then by the exact twins with 1 and 8
// lanes into them too: whether all were, the file in `file`.
static bool dynamic_test_no_pie_write(DynamicTestTally* tally, BindTestLink* test, const Runtime* runtime, String8 path, const char* name, uint64_t id_order, String8* file,
                                      Arena* arena)
{
    ExecutableResult result = { 0 };
    bool written = dynamic_test_lanes(tally, &test->link, &test->bind, runtime, path, name, &result, file, id_order, arena);
    for (uint32_t lanes = 1; written && lanes <= 8; lanes += 7)
    {
        DynamicTestWrite write = {
            .exact = true,
        };
        ExecutableResult exact = { 0 };
        const bool reset = executable_test_reset(&test->link, path.pointer);
        const bool succeeded = reset && dynamic_test_write_with(&write, &test->link, &test->bind, runtime, path, lanes, &exact);
        const String8 bytes = succeeded ? executable_test_read(arena, path.pointer) : (String8){ 0 };
        const bool same = succeeded && bytes.length == file->length && memcmp(bytes.pointer, file->pointer, bytes.length) == 0;
        dynamic_test_expect(tally, same, "the exact twins write the same file", lanes);
        written = written && same;
    }
    return written;
}

// The field of `width` bytes at the start of an atom in the file, sign-extended for 4 bytes when `signed_field`.
static int64_t dynamic_test_no_pie_field(String8 file, const Link* link, AtomId atom, uint32_t width, bool signed_field)
{
    const uint64_t offset = link->atoms.address[atom];
    int64_t result = INT64_MIN;
    if (offset <= file.length && width <= file.length - offset && width == 8)
    {
        memcpy(&result, file.pointer + offset, 8);
    }
    else if (offset <= file.length && width <= file.length - offset)
    {
        int32_t value = 0;
        memcpy(&value, file.pointer + offset, 4);
        result = signed_field ? (int64_t)value : (int64_t)(uint32_t)value;
    }
    return result;
}

static bool dynamic_test_no_pie(const ExecutableTestDirectory* directory)
{
    DynamicTestTally tally = {
        .check = "position-dependent executables",
    };
    Runtime runtime;
    const RuntimeError imported = runtime_import_kind(&runtime, RUNTIME_KIND_NO_PIE);
    dynamic_test_expect(&tally, imported == RUNTIME_ERROR_NONE, "the position-dependent executable's runtime imports", (uint64_t)imported);
    char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    const String8 path = executable_test_path(directory, "no-pie", path_bytes);
    for (uint32_t which = 0; imported == RUNTIME_ERROR_NONE && which < DYNAMIC_TEST_SMALLS; which += 1)
    {
        DynamicTestSmallProgram program;
        dynamic_test_small(&program, &runtime, (DynamicTestSmall)which);
        BindTestLink* test = &program.test;
        dynamic_test_expect(&tally, program.bound, dynamic_test_small_names[which], which);
        Scratch scratch = scratch_begin(0);
        String8 file = { 0 };
        const bool written = program.bound && dynamic_test_no_pie_write(&tally, test, &runtime, path, dynamic_test_small_names[which], directory->id_order, &file, scratch.arena);
        uint32_t pointers = 0;
        if (written)
        {
            dynamic_test_no_pie_structure(&tally, file, 0, &pointers, which);
        }
        // The fields a PIE refuses, as the link has their targets: main's address, and address 0's or 42's distance from
        // the field.
        const Link* link = &test->link;
        const AtomId main_atom = written ? link->symbols.atom[dynamic_global(&runtime, &test->bind, "main")] : 0;
        const int64_t main_address = (int64_t)BUSTER_EXECUTABLE_BASE + (int64_t)(main_atom != 0 ? link->atoms.address[main_atom] : 0);
        const int64_t place = (int64_t)BUSTER_EXECUTABLE_BASE + (int64_t)(written ? link->atoms.address[program.data] : 0);
        if (written && which == DYNAMIC_TEST_SMALL_TEXT_POINTER)
        {
            dynamic_test_expect(&tally, dynamic_test_no_pie_field(file, link, program.data, 8, false) == main_address, "a .rodata pointer holds main's address", which);
        }
        else if (written && which == DYNAMIC_TEST_SMALL_ABSOLUTE32)
        {
            dynamic_test_expect(&tally, dynamic_test_no_pie_field(file, link, program.data, 4, false) == main_address, "an ABS32 holds main's address", which);
        }
        else if (written && which == DYNAMIC_TEST_SMALL_PC32_DYNAMIC)
        {
            dynamic_test_expect(&tally, dynamic_test_no_pie_field(file, link, program.data, 4, true) == -place, "a PC32 reaches a weak reference to nothing at 0", which);
        }
        else if (written && which == DYNAMIC_TEST_SMALL_HIDDEN_PC64)
        {
            dynamic_test_expect(&tally, dynamic_test_no_pie_field(file, link, program.data, 8, false) == -place, "a PC64 reaches a hidden one at 0", which);
        }
        else if (written && which == DYNAMIC_TEST_SMALL_ABSOLUTE_PC64)
        {
            dynamic_test_expect(&tally, dynamic_test_no_pie_field(file, link, program.data, 8, false) == BUSTER_DYNAMIC_TEST_ABSOLUTE_VALUE - place,
                                "a PC64 reaches an absolute symbol at its value", which);
        }
        const bool unwritten_42 = which == DYNAMIC_TEST_SMALL_ABSOLUTE_LEA || which == DYNAMIC_TEST_SMALL_ABSOLUTE_CALL || which == DYNAMIC_TEST_SMALL_ABSOLUTE_PC64;
        uint8_t output[BUSTER_EXECUTABLE_TEST_OUTPUT_BYTES];
        uint64_t length = 0;
        int status = -1;
        const bool ran = written && executable_test_run(path.pointer, output, &length, &status) && WIFEXITED(status) && WEXITSTATUS(status) == (unwritten_42 ? 1 : 0) &&
                         length == 0;
        dynamic_test_expect(&tally, ran, "runs with its status", which);
        printf("  -no-pie %s: %s\n", dynamic_test_small_names[which], ran ? "ran" : "wrong");
        unlink(path.pointer);
        scratch_end(scratch);
        bind_test_link_end(test);
    }
    for (uint32_t which = 0; imported == RUNTIME_ERROR_NONE && which < DYNAMIC_TEST_NO_PIE_CASES; which += 1)
    {
        DynamicTestNoPieProgram program;
        dynamic_test_no_pie_program(&program, &runtime, (DynamicTestNoPieCase)which);
        BindTestLink* test = &program.test;
        dynamic_test_expect(&tally, program.bound, dynamic_test_no_pie_names[which], which);
        bool outcome = program.bound;
        // By every lane count, then by the exact twins with 1 and 8 lanes.
        for (uint32_t l = 0; program.bound && program.refused != 0 && l < BUSTER_ARRAY_LENGTH(dynamic_test_lane_counts) + 2; l += 1)
        {
            const bool exact = l >= BUSTER_ARRAY_LENGTH(dynamic_test_lane_counts);
            const uint32_t lanes = exact ? (l == BUSTER_ARRAY_LENGTH(dynamic_test_lane_counts) ? 1 : 8) : dynamic_test_lane_counts[l];
            DynamicTestWrite write = {
                .exact = exact,
            };
            ExecutableResult result;
            unlink(path.pointer);
            const bool written = dynamic_test_write_with(&write, &test->link, &test->bind, &runtime, path, lanes, &result);
            const bool refused = !written && result.error == EXECUTABLE_ERROR_RELOCATION && result.cause == EXECUTABLE_CAUSE_RELOCATION_TEXT &&
                                 result.fixup == program.refused && access(path.pointer, F_OK) != 0;
            dynamic_test_expect(&tally, refused, dynamic_test_no_pie_names[which], (exact ? 100 : 0) + lanes);
            outcome = outcome && refused;
        }
        Scratch scratch = scratch_begin(0);
        String8 file = { 0 };
        const bool written = program.bound && program.refused == 0 &&
                             dynamic_test_no_pie_write(&tally, test, &runtime, path, dynamic_test_no_pie_names[which], directory->id_order, &file, scratch.arena);
        const Link* link = &test->link;
        const uint64_t place = BUSTER_EXECUTABLE_BASE + (written ? link->atoms.address[program.data] : 0);
        uint32_t pointers = 0;
        if (written)
        {
            dynamic_test_no_pie_structure(&tally, file, place, &pointers, which);
        }
        if (written && which == DYNAMIC_TEST_NO_PIE_IMPORT_POINTER)
        {
            dynamic_test_expect(&tally, pointers == 1 && link->symbols.source[program.imported] == BUSTER_LINK_SOURCE_RUNTIME_SHARED, "an R_X86_64_64 against the import",
                                which);
        }
        else if (written && which == DYNAMIC_TEST_NO_PIE_WEAK_ZERO)
        {
            // The call's rel32, buster_never's first fixup, from the end of its 5 bytes.
            const AtomId never = link->symbols.atom[program.never];
            const uint64_t call = never != 0 ? link->atoms.address[never] : 0;
            int32_t displacement = 0;
            const bool inside = never != 0 && call + 5 <= file.length;
            if (inside)
            {
                memcpy(&displacement, file.pointer + call + 1, sizeof(displacement));
            }
            const bool zero = dynamic_test_no_pie_field(file, link, program.data, 4, false) == 0 && inside &&
                              (int64_t)displacement == -(int64_t)(BUSTER_EXECUTABLE_BASE + call + 5) && link->symbols.got_slot[program.weak] == 0 &&
                              link->symbols.reference[program.weak] == (uint32_t)(0 - BUSTER_EXECUTABLE_BASE);
            dynamic_test_expect(&tally, zero, "the ABS32 and the call reach address 0, without a stub or a slot", which);
        }
        if (program.refused == 0)
        {
            uint8_t output[BUSTER_EXECUTABLE_TEST_OUTPUT_BYTES];
            uint64_t length = 0;
            int status = -1;
            outcome = written && executable_test_run(path.pointer, output, &length, &status) && WIFEXITED(status) && WEXITSTATUS(status) == 0 && length == 0;
            dynamic_test_expect(&tally, outcome, "runs with status 0", which);
        }
        printf("  -no-pie %s: %s\n", dynamic_test_no_pie_names[which], !outcome ? "wrong" : program.refused != 0 ? "refused" : "ran");
        unlink(path.pointer);
        scratch_end(scratch);
        bind_test_link_end(test);
    }
    if (imported == RUNTIME_ERROR_NONE)
    {
        runtime_release(&runtime);
    }
    return dynamic_test_report(&tally);
}

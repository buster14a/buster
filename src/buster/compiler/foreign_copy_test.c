// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Copy relocations and libraries' own references in foreign PIEs (foreign_link_copies, and the resolver's regular
// references). Include from foreign_test.c after foreign_dynamic_test.c, whose image helpers it takes. The fixtures are
// original: a library of data with aliases, a 64-byte alignment, read-only data and a version, which assembly reads by
// PC32 as GCC's -fPIE code reads a library's data, and which the library itself reads back through its GOT; an object of
// eleven names, written by PC32 and read through the program's GOT by its ten aliases, each of which the library reads
// back through its own; a C program
// that reads libc's stdio streams and environ through the C runtime; a program that needs the loader, which libc.so's
// script names as needed, by a reference of its own; and refusals of a function and of an object of no size reached
// that way. Each resolution, on both paths and 1/2/3/8 lanes, writes a PIE that the loader runs, byte for byte alike;
// the test reads its dynamic symbols, versions and COPY relocations. With --cross-check, lld links the same inputs:
// its copied names (each one's version and size) and the names its COPY relocations copy must be ours.

typedef struct ForeignCopyTestSource ForeignCopyTestSource;
struct ForeignCopyTestSource
{
    const char* file;
    const char* text;
    // Compiled with -fPIC (the library, and a program that must reach its data through the GOT) or as cc compiles a
    // PIE's code by default.
    bool pic;
    const char* library;
    const char* version;
};

static const ForeignCopyTestSource foreign_copy_test_sources[] = {
    {
        .file = "fc_data.c",
        .text = "int fc_value = 7;\n"
                "extern int fc_alias __attribute__((alias(\"fc_value\")));\n"
                "extern int fc_weak __attribute__((weak, alias(\"fc_value\")));\n"
                "__attribute__((aligned(64))) long fc_wide[3] = { 1, 2, 3 };\n"
                "const int fc_constant = 11;\n"
                "char fc_text[5] = \"copy\";\n"
                "__asm__(\".section .data.fc_empty,\\\"aw\\\"\\n.byte 1\\n.globl fc_empty\\n.type fc_empty,@object\\nfc_empty:\\n.size fc_empty,0\\n.byte 2\\n.text\\n\");\n"
                "int fc_function(void) { return fc_value; }\n"
                "int fc_read(void) { return fc_value + fc_weak; }\n",
        .pic = true,
        .library = "libfc-data.so",
        .version = "FC_1 { global: fc_*; local: *; };\n",
    },
    {
        .file = "fc_main.s",
        .text = ".text\n.globl _start\n.type _start,@function\n_start:\n"
                "mov fc_value(%rip),%eax\ncmp $7,%eax\njne .Lfail\n"
                "mov fc_alias(%rip),%eax\ncmp $7,%eax\njne .Lfail\n"
                "lea fc_value(%rip),%rcx\nlea fc_alias(%rip),%rdx\ncmp %rcx,%rdx\njne .Lfail\n"
                "movl $9,fc_value(%rip)\ncall fc_read\ncmp $18,%eax\njne .Lfail\n"
                "lea fc_wide(%rip),%rax\ntest $63,%al\njnz .Lfail\nmov 16(%rax),%rax\ncmp $3,%rax\njne .Lfail\n"
                "mov fc_constant(%rip),%eax\ncmp $11,%eax\njne .Lfail\n"
                "movzbl fc_text+3(%rip),%eax\ncmp $121,%eax\njne .Lfail\n"
                "mov $42,%edi\njmp .Lexit\n.Lfail:\nmov $101,%edi\n.Lexit:\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .file = "fc_libc.c",
        .text = "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n"
                "extern char** environ;\n"
                "int main(void) {\n"
                "    if (setenv(\"FC_COPY\", \"1\", 1) != 0) return 101;\n"
                "    int found = 0;\n"
                "    for (char** e = environ; *e != 0; e += 1) found += strcmp(*e, \"FC_COPY=1\") == 0;\n"
                "    fprintf(stdout, \"out %d\\n\", found);\n"
                "    fflush(stdout);\n"
                "    fputs(\"err\\n\", stderr);\n"
                "    return found == 1 ? 42 : 102;\n"
                "}\n",
    },
    {
        .file = "fc_loader.c",
        .text = "extern void* __libc_stack_end;\nint main(void) { return __libc_stack_end != 0 ? 42 : 101; }\n",
        .pic = true,
    },
    {
        .file = "fc_function.s",
        .text = ".text\n.globl _start\n.type _start,@function\n_start:\nlea fc_function(%rip),%rax\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .file = "fc_empty.s",
        .text = ".text\n.globl _start\n.type _start,@function\n_start:\nmov fc_empty(%rip),%eax\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        // One object of eleven names, which the library reads back through its GOT, name by name.
        .file = "fc_many_library.s",
        .text = ".data\n.globl fc_many\n.type fc_many,@object\n.size fc_many,4\nfc_many:\n.long 1\n"
                ".globl fc_many_1\n.type fc_many_1,@object\n.size fc_many_1,4\n.set fc_many_1,fc_many\n"
                ".globl fc_many_2\n.type fc_many_2,@object\n.size fc_many_2,4\n.set fc_many_2,fc_many\n"
                ".globl fc_many_3\n.type fc_many_3,@object\n.size fc_many_3,4\n.set fc_many_3,fc_many\n"
                ".globl fc_many_4\n.type fc_many_4,@object\n.size fc_many_4,4\n.set fc_many_4,fc_many\n"
                ".globl fc_many_5\n.type fc_many_5,@object\n.size fc_many_5,4\n.set fc_many_5,fc_many\n"
                ".globl fc_many_6\n.type fc_many_6,@object\n.size fc_many_6,4\n.set fc_many_6,fc_many\n"
                ".globl fc_many_7\n.type fc_many_7,@object\n.size fc_many_7,4\n.set fc_many_7,fc_many\n"
                ".globl fc_many_8\n.type fc_many_8,@object\n.size fc_many_8,4\n.set fc_many_8,fc_many\n"
                ".globl fc_many_9\n.type fc_many_9,@object\n.size fc_many_9,4\n.set fc_many_9,fc_many\n"
                ".globl fc_many_10\n.type fc_many_10,@object\n.size fc_many_10,4\n.set fc_many_10,fc_many\n"
                ".text\n.globl fc_many_read\n.type fc_many_read,@function\nfc_many_read:\nxor %eax,%eax\n"
                "mov fc_many_1@GOTPCREL(%rip),%rcx\nadd (%rcx),%eax\n"
                "mov fc_many_2@GOTPCREL(%rip),%rcx\nadd (%rcx),%eax\n"
                "mov fc_many_3@GOTPCREL(%rip),%rcx\nadd (%rcx),%eax\n"
                "mov fc_many_4@GOTPCREL(%rip),%rcx\nadd (%rcx),%eax\n"
                "mov fc_many_5@GOTPCREL(%rip),%rcx\nadd (%rcx),%eax\n"
                "mov fc_many_6@GOTPCREL(%rip),%rcx\nadd (%rcx),%eax\n"
                "mov fc_many_7@GOTPCREL(%rip),%rcx\nadd (%rcx),%eax\n"
                "mov fc_many_8@GOTPCREL(%rip),%rcx\nadd (%rcx),%eax\n"
                "mov fc_many_9@GOTPCREL(%rip),%rcx\nadd (%rcx),%eax\n"
                "mov fc_many_10@GOTPCREL(%rip),%rcx\nadd (%rcx),%eax\n"
                "ret\n.size fc_many_read,.-fc_many_read\n.section .note.GNU-stack,\"\",@progbits\n",
        .pic = true,
        .library = "libfc-many.so",
        .version = "FC_MANY_1 { global: fc_many*; local: *; };\n",
    },
    {
        // The object written by PC32, so copied, and each of its ten aliases read through the program's GOT: every name
        // is the copy's, and the library's reads of them see the write.
        .file = "fc_many_main.s",
        .text = ".text\n.globl _start\n.type _start,@function\n_start:\nmovl $5,fc_many(%rip)\n"
                "mov fc_many_1@GOTPCREL(%rip),%rcx\ncmpl $5,(%rcx)\njne .Lfail\n"
                "mov fc_many_2@GOTPCREL(%rip),%rcx\ncmpl $5,(%rcx)\njne .Lfail\n"
                "mov fc_many_3@GOTPCREL(%rip),%rcx\ncmpl $5,(%rcx)\njne .Lfail\n"
                "mov fc_many_4@GOTPCREL(%rip),%rcx\ncmpl $5,(%rcx)\njne .Lfail\n"
                "mov fc_many_5@GOTPCREL(%rip),%rcx\ncmpl $5,(%rcx)\njne .Lfail\n"
                "mov fc_many_6@GOTPCREL(%rip),%rcx\ncmpl $5,(%rcx)\njne .Lfail\n"
                "mov fc_many_7@GOTPCREL(%rip),%rcx\ncmpl $5,(%rcx)\njne .Lfail\n"
                "mov fc_many_8@GOTPCREL(%rip),%rcx\ncmpl $5,(%rcx)\njne .Lfail\n"
                "mov fc_many_9@GOTPCREL(%rip),%rcx\ncmpl $5,(%rcx)\njne .Lfail\n"
                "mov fc_many_10@GOTPCREL(%rip),%rcx\ncmpl $5,(%rcx)\njne .Lfail\n"
                "call fc_many_read\ncmp $50,%eax\njne .Lfail\n"
                "mov $42,%edi\njmp .Lexit\n.Lfail:\nmov $101,%edi\n.Lexit:\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
};

// The cases: the fixture objects each links (after the C runtime's for those with main), and what it must do.
typedef enum ForeignCopyTestCase
{
    FOREIGN_COPY_TEST_DATA,
    FOREIGN_COPY_TEST_LIBC,
    FOREIGN_COPY_TEST_LOADER,
    FOREIGN_COPY_TEST_FUNCTION,
    FOREIGN_COPY_TEST_EMPTY,
    FOREIGN_COPY_TEST_MANY,
    FOREIGN_COPY_TEST_CASES,
} ForeignCopyTestCase;

static const char* const foreign_copy_test_objects[FOREIGN_COPY_TEST_CASES] = {
    [FOREIGN_COPY_TEST_DATA] = "fc_main.o",
    [FOREIGN_COPY_TEST_LIBC] = "fc_libc.o",
    [FOREIGN_COPY_TEST_LOADER] = "fc_loader.o",
    [FOREIGN_COPY_TEST_FUNCTION] = "fc_function.o",
    [FOREIGN_COPY_TEST_EMPTY] = "fc_empty.o",
    [FOREIGN_COPY_TEST_MANY] = "fc_many_main.o",
};

static bool foreign_copy_test_compile(const char* directory, ForeignTestTally* tally)
{
    bool result = true;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(foreign_copy_test_sources); i += 1)
    {
        const ForeignCopyTestSource* fixture = &foreign_copy_test_sources[i];
        const int stem = (int)(strrchr(fixture->file, '.') - fixture->file);
        ForeignTestPath source;
        ForeignTestPath object;
        ForeignTestPath output;
        ForeignTestPath errors;
        snprintf(source, sizeof(source), "%s/%s", directory, fixture->file);
        snprintf(object, sizeof(object), "%s/%.*s.o", directory, stem, fixture->file);
        snprintf(output, sizeof(output), "%s/%.*s_compile.out", directory, stem, fixture->file);
        snprintf(errors, sizeof(errors), "%s/%.*s_compile.err", directory, stem, fixture->file);
        char* compile[] = {
            (char*)"cc",
            (char*)"-c",
            (char*)"-O2",
            fixture->pic ? (char*)"-fPIC" : (char*)"-fPIE",
            source,
            (char*)"-o",
            object,
            0,
        };
        bool made = foreign_test_write(source, fixture->text, strlen(fixture->text)) && object_test_run(compile, output, errors) == 0;
        if (made && fixture->library != 0)
        {
            ForeignTestPath library;
            ForeignTestPath script;
            char soname[128];
            char version_option[BUSTER_FOREIGN_TEST_PATH_BYTES + 32];
            snprintf(library, sizeof(library), "%s/%s", directory, fixture->library);
            snprintf(soname, sizeof(soname), "-Wl,-soname,%s", fixture->library);
            snprintf(script, sizeof(script), "%s/%.*s.map", directory, stem, fixture->file);
            snprintf(version_option, sizeof(version_option), "-Wl,--version-script=%s", script);
            snprintf(output, sizeof(output), "%s/%.*s_link.out", directory, stem, fixture->file);
            snprintf(errors, sizeof(errors), "%s/%.*s_link.err", directory, stem, fixture->file);
            char* link[] = {
                (char*)"cc",
                (char*)"-shared",
                (char*)"-nostdlib",
                soname,
                object,
                (char*)"-o",
                library,
                version_option,
                0,
            };
            made = foreign_test_write(script, fixture->version, strlen(fixture->version)) && object_test_run(link, output, errors) == 0;
        }
        foreign_test_expect(tally, made, "compile original copy-relocation fixture", fixture->file);
        result = result && made;
    }
    return result;
}

// A defined dynamic symbol's version name (from .gnu.version and .gnu.version_r), or "" for none or the executable's
// own.
static const char* foreign_copy_test_version(const DynamicTestImage* image, uint32_t index)
{
    const Elf64_Shdr* versions = dynamic_test_section(image, ".gnu.version");
    const Elf64_Shdr* needs = dynamic_test_section(image, ".gnu.version_r");
    const Elf64_Shdr* strings = dynamic_test_section(image, ".dynstr");
    const Elf64_Half* indices = (const Elf64_Half*)dynamic_test_bytes(image, versions);
    const uint8_t* need = dynamic_test_bytes(image, needs);
    const char* names = (const char*)dynamic_test_bytes(image, strings);
    const Elf64_Half wanted = indices != 0 && index < versions->sh_size / sizeof(Elf64_Half) ? indices[index] & 0x7fff : 0;
    const char* result = "";
    uint64_t offset = 0;
    for (uint32_t entry = 0; wanted > VER_NDX_GLOBAL && need != 0 && names != 0 && entry < needs->sh_info && offset + sizeof(Elf64_Verneed) <= needs->sh_size; entry += 1)
    {
        Elf64_Verneed file;
        memcpy(&file, need + offset, sizeof(file));
        uint64_t aux = offset + file.vn_aux;
        for (uint32_t a = 0; a < file.vn_cnt && aux + sizeof(Elf64_Vernaux) <= needs->sh_size; a += 1)
        {
            Elf64_Vernaux version;
            memcpy(&version, need + aux, sizeof(version));
            result = version.vna_other == wanted && version.vna_name < strings->sh_size ? names + version.vna_name : result;
            aux += version.vna_next;
        }
        offset += file.vn_next;
    }
    return result;
}

// The copies of an image as text, "name@version:size;" for each defined dynamic symbol, sorted, so that two linkers'
// layouts compare; and how many COPY relocations there are, each of which must be at a defined symbol's address and
// name one of the symbols there (UINT32_MAX if one doesn't). Which alias a COPY names is the linker's choice: lld names
// the first referenced one, GNU ld another.
static uint32_t foreign_copy_test_copies(const DynamicTestImage* image, char* text, uint64_t capacity)
{
    const Elf64_Shdr* table = dynamic_test_section(image, ".dynsym");
    const Elf64_Shdr* strings = dynamic_test_section(image, ".dynstr");
    const Elf64_Shdr* relocations = dynamic_test_section(image, ".rela.dyn");
    const Elf64_Sym* symbols = (const Elf64_Sym*)dynamic_test_bytes(image, table);
    const char* names = (const char*)dynamic_test_bytes(image, strings);
    const Elf64_Rela* entries = (const Elf64_Rela*)dynamic_test_bytes(image, relocations);
    char lines[64][160];
    uint32_t line_count = 0;
    uint32_t copies = 0;
    for (uint64_t i = 1; symbols != 0 && names != 0 && i < table->sh_size / sizeof(Elf64_Sym); i += 1)
    {
        if (symbols[i].st_shndx != SHN_UNDEF && symbols[i].st_name < strings->sh_size && line_count < BUSTER_ARRAY_LENGTH(lines))
        {
            snprintf(lines[line_count], sizeof(lines[line_count]), "%s@%s:%" PRIu64 ";", names + symbols[i].st_name, foreign_copy_test_version(image, (uint32_t)i),
                     (uint64_t)symbols[i].st_size);
            line_count += 1;
        }
    }
    for (uint64_t i = 0; entries != 0 && symbols != 0 && names != 0 && i < relocations->sh_size / sizeof(Elf64_Rela); i += 1)
    {
        const uint64_t symbol = ELF64_R_SYM(entries[i].r_info);
        if (ELF64_R_TYPE(entries[i].r_info) == R_X86_64_COPY)
        {
            const bool placed = symbol < table->sh_size / sizeof(Elf64_Sym) && symbols[symbol].st_shndx != SHN_UNDEF && symbols[symbol].st_value == entries[i].r_offset;
            copies = placed && copies != UINT32_MAX ? copies + 1 : UINT32_MAX;
        }
    }
    // Sorted, by insertion: a few lines.
    for (uint32_t i = 1; i < line_count; i += 1)
    {
        for (uint32_t j = i; j > 0 && strcmp(lines[j - 1], lines[j]) > 0; j -= 1)
        {
            char swap[160];
            memcpy(swap, lines[j], sizeof(swap));
            memcpy(lines[j], lines[j - 1], sizeof(swap));
            memcpy(lines[j - 1], swap, sizeof(swap));
        }
    }
    uint64_t length = 0;
    text[0] = 0;
    for (uint32_t i = 0; i < line_count && length + strlen(lines[i]) + 1 < capacity; i += 1)
    {
        memcpy(text + length, lines[i], strlen(lines[i]) + 1);
        length += strlen(lines[i]);
    }
    return copies;
}

static bool foreign_copy_test_needs(const DynamicTestImage* image, const char* library)
{
    const char* needed[16] = { 0 };
    const uint32_t count = dynamic_test_needed(image, needed, BUSTER_ARRAY_LENGTH(needed));
    bool result = false;
    for (uint32_t i = 0; i < count && i < BUSTER_ARRAY_LENGTH(needed); i += 1)
    {
        result = result || strcmp(needed[i], library) == 0;
    }
    return result;
}

// What each case's PIE must hold, read from its file.
static void foreign_copy_test_structure(ForeignTestTally* tally, String8 file, ForeignCopyTestCase test, const char* detail)
{
    const DynamicTestImage image = dynamic_test_image(file);
    char copies[8192];
    const uint32_t copy_count = foreign_copy_test_copies(&image, copies, sizeof(copies));
    Elf64_Sym symbol = { 0 };
    if (test == FOREIGN_COPY_TEST_DATA)
    {
        Elf64_Sym value = { 0 };
        Elf64_Sym alias = { 0 };
        Elf64_Sym weak = { 0 };
        Elf64_Sym wide = { 0 };
        const bool defined = foreign_dynamic_test_symbol(&image, "fc_value", &value) != 0 && foreign_dynamic_test_symbol(&image, "fc_alias", &alias) != 0 &&
                             foreign_dynamic_test_symbol(&image, "fc_weak", &weak) != 0 && foreign_dynamic_test_symbol(&image, "fc_wide", &wide) != 0;
        foreign_test_expect(tally, defined && value.st_shndx != SHN_UNDEF && value.st_value == alias.st_value && value.st_value == weak.st_value && value.st_size == 4 &&
                            ELF64_ST_BIND(weak.st_info) == STB_WEAK && ELF64_ST_TYPE(value.st_info) == STT_OBJECT,
                            "a copy and its aliases are one exported object", detail);
        foreign_test_expect(tally, defined && wide.st_value % 64 == 0 && wide.st_size == 24, "a copy keeps its definition's 64-byte alignment", detail);
        foreign_test_expect(tally, strstr(copies, "fc_value@FC_1:4;") != 0 && strstr(copies, "fc_alias@FC_1:4;") != 0 && strstr(copies, "fc_weak@FC_1:4;") != 0 &&
                            strstr(copies, "fc_constant@FC_1:4;") != 0 && strstr(copies, "fc_text@FC_1:5;") != 0,
                            "copies are exported at the library's version", detail);
        foreign_test_expect(tally, copy_count == 4, "one COPY relocation per copied object", detail);
        foreign_test_expect(tally, foreign_dynamic_test_symbol(&image, "fc_function", &symbol) == 0 && foreign_dynamic_test_symbol(&image, "fc_empty", &symbol) == 0,
                            "nothing else of the library's is defined", detail);
    }
    else if (test == FOREIGN_COPY_TEST_LIBC)
    {
        foreign_test_expect(tally, foreign_copy_test_needs(&image, "libc.so.6") && !foreign_copy_test_needs(&image, "ld-linux-x86-64.so.2"),
                            "libc's own references to the loader don't make it needed", detail);
        foreign_test_expect(tally, foreign_dynamic_test_symbol(&image, "_rtld_global", &symbol) == 0 && foreign_dynamic_test_symbol(&image, "_dl_argv", &symbol) == 0,
                            "names only libraries refer to aren't imported", detail);
    }
    else if (test == FOREIGN_COPY_TEST_LOADER)
    {
        foreign_test_expect(tally, foreign_copy_test_needs(&image, "ld-linux-x86-64.so.2") && foreign_dynamic_test_symbol(&image, "__libc_stack_end", &symbol) != 0,
                            "the program's own reference makes the as-needed loader needed", detail);
    }
    else if (test == FOREIGN_COPY_TEST_MANY)
    {
        // Every one of the eleven names exported at the copy, at the library's version: more than a copy's first
        // names, and each a record of the copies' exports.
        Elf64_Sym first = { 0 };
        bool all = foreign_dynamic_test_symbol(&image, "fc_many", &first) != 0 && first.st_shndx != SHN_UNDEF && strstr(copies, "fc_many@FC_MANY_1:4;") != 0;
        for (uint32_t i = 1; i <= 10; i += 1)
        {
            char name[32];
            char line[64];
            snprintf(name, sizeof(name), "fc_many_%u", i);
            snprintf(line, sizeof(line), "%s@FC_MANY_1:4;", name);
            all = all && foreign_dynamic_test_symbol(&image, name, &symbol) != 0 && symbol.st_value == first.st_value && strstr(copies, line) != 0;
        }
        foreign_test_expect(tally, all, "every name of a copied object is exported at the copy, its aliases past ten among them", detail);
        foreign_test_expect(tally, copy_count == 1, "one COPY relocation for the object of eleven names", detail);
    }
}

static bool foreign_copy_test_run(const char* directory, char* executable, ForeignCopyTestCase test, Arena* arena)
{
    char environment[BUSTER_FOREIGN_TEST_PATH_BYTES + 32];
    char output[BUSTER_FOREIGN_TEST_PATH_BYTES + 16];
    char errors[BUSTER_FOREIGN_TEST_PATH_BYTES + 16];
    snprintf(environment, sizeof(environment), "LD_LIBRARY_PATH=%s", directory);
    snprintf(output, sizeof(output), "%s_run.out", executable);
    snprintf(errors, sizeof(errors), "%s_run.err", executable);
    char* arguments[] = {
        (char*)"env",
        environment,
        executable,
        0,
    };
    const bool ran = object_test_run(arguments, output, errors) == 42;
    const String8 printed = foreign_test_read(arena, output);
    const String8 written = foreign_test_read(arena, errors);
    const char* expected = test == FOREIGN_COPY_TEST_LIBC ? "out 1\n" : "";
    const char* expected_errors = test == FOREIGN_COPY_TEST_LIBC ? "err\n" : "";
    return ran && printed.pointer != 0 && printed.length == strlen(expected) && memcmp(printed.pointer, expected, printed.length) == 0 && written.pointer != 0 &&
           written.length == strlen(expected_errors) && memcmp(written.pointer, expected_errors, written.length) == 0;
}

// lld's link of the same line: it must run alike, and its copies must be ours.
static void foreign_copy_test_oracle(ForeignTestTally* tally, const char* directory, const ForeignTestLine* line, ForeignCopyTestCase test, String8 ours, Arena* arena)
{
    const uint64_t saved = arena->length;
    ForeignTestPath executable;
    ForeignTestPath output;
    ForeignTestPath errors;
    snprintf(executable, sizeof(executable), "%s/fc_reference_%u", directory, (unsigned)test);
    snprintf(output, sizeof(output), "%s/fc_reference_%u_link.out", directory, (unsigned)test);
    snprintf(errors, sizeof(errors), "%s/fc_reference_%u_link.err", directory, (unsigned)test);
    char** arguments = BUSTER_ARENA_PUSH_ARRAY(arena, char*, (uint64_t)line->their_count + 4);
    bool ready = arguments != 0;
    if (ready)
    {
        arguments[0] = (char*)"ld.lld";
        arguments[1] = (char*)"-o";
        arguments[2] = executable;
        memcpy(arguments + 3, line->theirs, (uint64_t)line->their_count * sizeof(char*));
        arguments[line->their_count + 3] = 0;
        ready = object_test_run(arguments, output, errors) == 0;
    }
    foreign_test_expect(tally, ready && foreign_copy_test_run(directory, executable, test, arena), "lld links and runs the copy fixture", 0);
    const String8 theirs = ready ? foreign_test_read(arena, executable) : (String8){ 0 };
    const DynamicTestImage our_image = dynamic_test_image(ours);
    const DynamicTestImage their_image = dynamic_test_image(theirs);
    char our_copies[8192];
    char their_copies[8192];
    const uint32_t our_count = foreign_copy_test_copies(&our_image, our_copies, sizeof(our_copies));
    const uint32_t their_count = foreign_copy_test_copies(&their_image, their_copies, sizeof(their_copies));
    const bool same = their_image.valid && strcmp(our_copies, their_copies) == 0 && our_count == their_count && our_count != UINT32_MAX;
    foreign_test_expect(tally, same, "copies, their versions and sizes are lld's", 0);
    if (!same)
    {
        printf("  ours:  %s (%u COPY)\n  lld's: %s (%u COPY)\n", our_copies, our_count, their_copies, their_count);
    }
    arena_pop_to(arena, saved);
}

static void foreign_copy_test_case(ForeignTestTally* tally, const char* directory, Arena* arena, bool cross_check, ForeignTestSystem* system, String8 interpreter,
                                   ForeignCopyTestCase test)
{
    const uint64_t saved = arena->length;
    ForeignTestLine* line = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignTestLine, 1);
    const bool c_runtime = test == FOREIGN_COPY_TEST_LIBC || test == FOREIGN_COPY_TEST_LOADER;
    bool ready = line != 0 && (!c_runtime || system->found);
    foreign_test_expect(tally, ready, "copy fixture has command room and required C runtime", 0);
    if (ready)
    {
        *line = (ForeignTestLine){ 0 };
        if (c_runtime)
        {
            foreign_test_pie_begin(line, system);
        }
        else
        {
            foreign_test_add(line, (char*)"-pie", false, true);
            foreign_test_add(line, (char*)"-e", false, true);
            foreign_test_add(line, (char*)"_start", false, true);
            foreign_test_add(line, (char*)"-dynamic-linker", false, true);
            foreign_test_add(line, interpreter.pointer, false, true);
        }
        foreign_test_add(line, foreign_test_path(directory, foreign_copy_test_objects[test], arena), true, true);
        if (!c_runtime)
        {
            foreign_test_add(line, foreign_test_path(directory, test == FOREIGN_COPY_TEST_MANY ? "libfc-many.so" : "libfc-data.so", arena), true, true);
        }
        else
        {
            foreign_test_pie_end(line, system);
        }
        for (uint32_t i = 0; i < line->our_count; i += 1)
        {
            ready = ready && line->ours[i] != 0;
        }
        ForeignCommand command = { 0 };
        ready = ready && foreign_test_command(&command, line->ours, line->our_count, arena);
        foreign_test_expect(tally, ready, "prepare copy fixture command", 0);
        const bool refused = test == FOREIGN_COPY_TEST_FUNCTION || test == FOREIGN_COPY_TEST_EMPTY;
        String8 reference = { 0 };
        for (uint32_t path = 0; ready && path < 2; path += 1)
        {
            for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(foreign_test_lanes); l += 1)
            {
                const uint64_t iteration = arena->length;
                Foreign foreign;
                foreign_run(&foreign, &command, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, foreign_test_lanes[l], 0);
                Arena names = arena_reserve(1ull << 28);
                ForeignLink converted = { 0 };
                const ForeignLinkResult built = foreign_link_build_dynamic(&converted, &foreign, arena, &names, interpreter);
                char detail[192];
                snprintf(detail, sizeof(detail), "case %u %s %u lanes: resolve %u convert %u name %u", (unsigned)test, path == 0 ? "exact" : "vector",
                         foreign_test_lanes[l], (unsigned)foreign.failure.error, (unsigned)built.error, built.name);
                if (refused)
                {
                    const char* name = test == FOREIGN_COPY_TEST_FUNCTION ? "fc_function" : "fc_empty";
                    const uint32_t wanted = foreign.failure.error == FOREIGN_ERROR_NONE ? foreign_test_name(&foreign, name) : 0;
                    foreign_test_expect(tally, wanted != 0 && built.error == FOREIGN_LINK_ERROR_UNSUPPORTED && built.name == wanted,
                                        "a reference that a copy can't serve is refused, naming its symbol", detail);
                }
                else
                {
                    const bool linked = foreign.failure.error == FOREIGN_ERROR_NONE && built.error == FOREIGN_LINK_ERROR_NONE;
                    foreign_test_expect(tally, linked, "convert copy fixture", detail);
                    ForeignTestPath executable;
                    snprintf(executable, sizeof(executable), "%s/fc_output_%u_%u_%u", directory, (unsigned)test, path, foreign_test_lanes[l]);
                    ExecutableTestWrite write = {
                        .link = &converted.link,
                        .options = {
                            .path = {
                                .pointer = executable,
                                .length = strlen(executable),
                            },
                            .entry = linked ? foreign_link_test_symbol(&foreign, &converted, "_start") : 0,
                            .dynamic = &converted.dynamic,
                            .exact = path == 0,
                        },
                    };
                    const bool published = linked && lane_run(foreign_test_lanes[l], executable_test_lane_main, &write, 0) && write.succeeded;
                    foreign_test_expect(tally, published, "write copy fixture PIE", detail);
                    if (linked && !published)
                    {
                        executable_test_print_failure(detail, &write.result);
                    }
                    const String8 bytes = published ? foreign_test_read(arena, executable) : (String8){ 0 };
                    foreign_test_expect(tally, published && foreign_copy_test_run(directory, executable, test, arena), "loader runs copy fixture", detail);
                    if (published)
                    {
                        foreign_copy_test_structure(tally, bytes, test, detail);
                    }
                    if (path == 0 && l == 0)
                    {
                        reference = bytes;
                        if (cross_check && published)
                        {
                            foreign_copy_test_oracle(tally, directory, line, test, bytes, arena);
                        }
                    }
                    else
                    {
                        const bool same = reference.pointer != 0 && bytes.pointer != 0 && reference.length == bytes.length && memcmp(reference.pointer, bytes.pointer, bytes.length) == 0;
                        foreign_test_expect(tally, same, "copy fixture output is identical across paths and lanes", detail);
                    }
                }
                arena_release(&names);
                foreign_release(&foreign);
                if (path != 0 || l != 0)
                {
                    arena_pop_to(arena, iteration);
                }
            }
        }
    }
    arena_pop_to(arena, saved);
}

static bool foreign_copy_test(const char* directory, Arena* arena, bool cross_check, ForeignTestSystem* system)
{
    ForeignTestTally tally = {
        .check = "foreign copy relocations",
    };
    Runtime runtime;
    const bool found = runtime_import(&runtime) == RUNTIME_ERROR_NONE && runtime.loader_path.length != 0;
    foreign_test_expect(&tally, found, "find system dynamic loader", 0);
    if (found && foreign_copy_test_compile(directory, &tally))
    {
        for (uint32_t test = 0; test < FOREIGN_COPY_TEST_CASES; test += 1)
        {
            foreign_copy_test_case(&tally, directory, arena, cross_check, system, runtime.loader_path, (ForeignCopyTestCase)test);
        }
    }
    runtime_release(&runtime);
    return foreign_test_report(&tally);
}

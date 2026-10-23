// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Raw link-foreign command tests. foreign_test.c includes this after the dynamic test helpers. Every link starts a
// fresh /proc/self/exe; only the guarded selection-kernel checks call driver internals. Original assembly supplies
// its own entry and syscall exit, except the main-only fixture that checks explicitly supplied system CRT inputs.
// The output starts as a sentinel, so both successful replacement and failure preservation are observable. Archive
// winners, scoped input options, GC, string pooling and dynamic dependencies are checked in the resulting programs.

#define BUSTER_FOREIGN_DRIVER_TEST_ARGUMENTS 64

typedef enum ForeignDriverTestFixture
{
    FOREIGN_DRIVER_TEST_START,
    FOREIGN_DRIVER_TEST_ENTRY,
    FOREIGN_DRIVER_TEST_DEAD,
    FOREIGN_DRIVER_TEST_CALL,
    FOREIGN_DRIVER_TEST_FIRST,
    FOREIGN_DRIVER_TEST_SECOND,
    FOREIGN_DRIVER_TEST_POISON,
    FOREIGN_DRIVER_TEST_UNUSED,
    FOREIGN_DRIVER_TEST_STRINGS,
    FOREIGN_DRIVER_TEST_STRINGS_PC,
    FOREIGN_DRIVER_TEST_MAIN,
    FOREIGN_DRIVER_TEST_OPTION_NAMES,
    FOREIGN_DRIVER_TEST_ABSOLUTE,
    FOREIGN_DRIVER_TEST_FIXTURES,
} ForeignDriverTestFixture;

typedef struct ForeignDriverTestSource ForeignDriverTestSource;
struct ForeignDriverTestSource
{
    const char* name;
    const char* text;
};

static const ForeignDriverTestSource foreign_driver_test_sources[FOREIGN_DRIVER_TEST_FIXTURES] = {
    [FOREIGN_DRIVER_TEST_START] = {
        .name = "fdriver_start",
        .text = ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                ".cfi_startproc\nmov $42,%edi\nmov $60,%eax\nsyscall\n.cfi_endproc\n.size _start,.-_start\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_ENTRY] = {
        .name = "fdriver_entry",
        .text = ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                "mov $19,%edi\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".section .text.alternate,\"ax\",@progbits\n.globl driver_alternate\n.type driver_alternate,@function\ndriver_alternate:\n"
                "mov $42,%edi\nmov $60,%eax\nsyscall\n.size driver_alternate,.-driver_alternate\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_DEAD] = {
        .name = "fdriver_dead",
        .text = ".section .text.dead,\"ax\",@progbits\n.globl driver_dead\n.type driver_dead,@function\ndriver_dead:\n"
                ".cfi_startproc\ncall driver_missing\nret\n.cfi_endproc\n.size driver_dead,.-driver_dead\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_CALL] = {
        .name = "fdriver_call",
        .text = ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                ".cfi_startproc\ncall driver_pick@PLT\nmov %eax,%edi\nmov $60,%eax\nsyscall\n.cfi_endproc\n.size _start,.-_start\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_FIRST] = {
        .name = "fdriver_first",
        .text = ".text\n.globl driver_pick\n.type driver_pick,@function\ndriver_pick:\nmov $42,%eax\nret\n.size driver_pick,.-driver_pick\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_SECOND] = {
        .name = "fdriver_second",
        .text = ".text\n.globl driver_pick\n.type driver_pick,@function\ndriver_pick:\nmov $19,%eax\nret\n.size driver_pick,.-driver_pick\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_POISON] = {
        .name = "fdriver_poison",
        .text = ".text\n.globl driver_poison\n.type driver_poison,@function\ndriver_poison:\ncall driver_missing\nret\n.size driver_poison,.-driver_poison\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_UNUSED] = {
        .name = "fdriver_unused",
        .text = ".text\n.globl driver_unused\n.type driver_unused,@function\ndriver_unused:\nmov $7,%eax\nret\n.size driver_unused,.-driver_unused\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_STRINGS] = {
        .name = "fdriver_strings",
        .text = ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                "mov $1,%edi\nmov driver_string_a@GOTPCREL(%rip),%rax\nmov driver_string_b@GOTPCREL(%rip),%rcx\n"
                "lea driver_string_pointers(%rip),%r8\ncmp (%r8),%rax\njne 1f\ncmp 8(%r8),%rcx\njne 1f\n"
                "cmpb $100,(%rax)\njne 1f\ncmpb $100,(%rcx)\njne 1f\nmov $42,%edi\n1: mov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".section .rodata.driver,\"aMS\",@progbits,1\n.globl driver_string_a\n.type driver_string_a,@object\ndriver_string_a:\n"
                ".asciz \"driver duplicate string\"\n.size driver_string_a,.-driver_string_a\n"
                ".globl driver_string_b\n.type driver_string_b,@object\ndriver_string_b:\n"
                ".asciz \"driver duplicate string\"\n.size driver_string_b,.-driver_string_b\n"
                ".data\n.balign 8\ndriver_string_pointers:\n.quad driver_string_a,driver_string_b\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_STRINGS_PC] = {
        .name = "fdriver_strings_pc",
        .text = ".section .text.start,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
                "mov $1,%edi\nlea driver_string_a(%rip),%rax\nlea driver_string_b(%rip),%rcx\n"
                "lea driver_string_pointers(%rip),%r8\ncmp (%r8),%rax\njne 1f\ncmp 8(%r8),%rcx\njne 1f\n"
                "cmpb $100,(%rax)\njne 1f\ncmpb $100,(%rcx)\njne 1f\nmov $42,%edi\n1: mov $60,%eax\nsyscall\n.size _start,.-_start\n"
                ".section .rodata.driver,\"aMS\",@progbits,1\n.globl driver_string_a\n.type driver_string_a,@object\ndriver_string_a:\n"
                ".asciz \"driver duplicate string\"\n.size driver_string_a,.-driver_string_a\n"
                ".globl driver_string_b\n.type driver_string_b,@object\ndriver_string_b:\n"
                ".asciz \"driver duplicate string\"\n.size driver_string_b,.-driver_string_b\n"
                ".data\n.balign 8\ndriver_string_pointers:\n.quad driver_string_a,driver_string_b\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_MAIN] = {
        .name = "fdriver_main",
        .text = ".text\n.globl main\n.type main,@function\nmain:\nmov $42,%eax\nret\n.size main,.-main\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_OPTION_NAMES] = {
        .name = "fdriver_option_names",
        .text = ".section .text.option_a,\"ax\",@progbits\n.globl \"--gc-sections\"\n.type \"--gc-sections\",@function\n\"--gc-sections\":\nmov $0x13ab47e1,%eax\nret\n"
                ".section .text.option_b,\"ax\",@progbits\n.globl \"--scalar\"\n.type \"--scalar\",@function\n\"--scalar\":\nmov $0x35cd68f2,%eax\nret\n"
                ".section .text.option_c,\"ax\",@progbits\n.globl \"-s\"\n.type \"-s\",@function\n\"-s\":\nmov $11,%eax\nret\n"
                ".section .text.option_d,\"ax\",@progbits\n.globl \"--strip-all\"\n.type \"--strip-all\",@function\n\"--strip-all\":\nmov $13,%eax\nret\n"
                ".section .note.GNU-stack,\"\",@progbits\n",
    },
    [FOREIGN_DRIVER_TEST_ABSOLUTE] = {
        .name = "fdriver_absolute",
        .text = ".globl driver_pick\n.set driver_pick,0x7fff000000000000\n.section .note.GNU-stack,\"\",@progbits\n",
    },
};

typedef struct ForeignDriverTestFiles ForeignDriverTestFiles;
struct ForeignDriverTestFiles
{
    ForeignTestPath objects[FOREIGN_DRIVER_TEST_FIXTURES];
    char* first;
    char* second;
    char* poison;
    char* entry;
    char* alternate;
    char* options;
    char* choice;
    ForeignTestPath needed;
    ForeignTestPath unused;
    ForeignTestPath choice_library;
};

static bool foreign_driver_test_compile(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files, Arena* arena)
{
    bool ready = true;
    for (uint32_t f = 0; f < FOREIGN_DRIVER_TEST_FIXTURES; f += 1)
    {
        const ForeignDriverTestSource* fixture = &foreign_driver_test_sources[f];
        ForeignTestPath source;
        ForeignTestPath output;
        ForeignTestPath errors;
        snprintf(source, sizeof(source), "%s/%s.S", directory, fixture->name);
        snprintf(files->objects[f], sizeof(files->objects[f]), "%s/%s.o", directory, fixture->name);
        snprintf(output, sizeof(output), "%s/%s_compile.out", directory, fixture->name);
        snprintf(errors, sizeof(errors), "%s/%s_compile.err", directory, fixture->name);
        char* arguments[] = {
            (char*)"cc",
            (char*)"-c",
            (char*)"-fPIC",
            source,
            (char*)"-o",
            files->objects[f],
            0,
        };
        const bool made = foreign_test_write(source, fixture->text, strlen(fixture->text)) && object_test_run(arguments, output, errors) == 0;
        foreign_test_expect(tally, made, "assemble original driver fixture", fixture->name);
        ready = ready && made;
    }
    if (ready)
    {
        const char* first[] = {
            "fdriver_first.o",
        };
        const char* second[] = {
            "fdriver_second.o",
        };
        const char* poison[] = {
            "fdriver_poison.o",
        };
        const char* entry[] = {
            "fdriver_start.o",
        };
        const char* alternate[] = {
            "fdriver_entry.o",
        };
        const char* options[] = {
            "fdriver_option_names.o",
        };
        files->first = foreign_test_archive(directory, "libfdriver_pick.a", first, BUSTER_ARRAY_LENGTH(first), false, arena);
        files->second = foreign_test_archive(directory, "libfdriver_second.a", second, BUSTER_ARRAY_LENGTH(second), false, arena);
        files->poison = foreign_test_archive(directory, "libfdriver_poison.a", poison, BUSTER_ARRAY_LENGTH(poison), false, arena);
        files->entry = foreign_test_archive(directory, "libfdriver_entry.a", entry, BUSTER_ARRAY_LENGTH(entry), false, arena);
        files->alternate = foreign_test_archive(directory, "libfdriver_alternate.a", alternate, BUSTER_ARRAY_LENGTH(alternate), false, arena);
        files->options = foreign_test_archive(directory, "libfdriver_options.a", options, BUSTER_ARRAY_LENGTH(options), false, arena);
        files->choice = foreign_test_archive(directory, "libfdriver_choice.a", second, BUSTER_ARRAY_LENGTH(second), false, arena);
        ready = files->first != 0 && files->second != 0 && files->poison != 0 && files->entry != 0 && files->alternate != 0 && files->options != 0 && files->choice != 0;
        foreign_test_expect(tally, ready, "write original driver archives", 0);
    }
    for (uint32_t d = 0; ready && d < 3; d += 1)
    {
        ForeignTestPath output;
        ForeignTestPath errors;
        char* library = d == 0 ? files->needed : d == 1 ? files->unused : files->choice_library;
        const char* basename = d == 0 ? "pick" : d == 1 ? "unused" : "choice";
        char soname[64];
        snprintf(library, BUSTER_FOREIGN_TEST_PATH_BYTES, "%s/libfdriver_%s.so", directory, basename);
        snprintf(soname, sizeof(soname), "-Wl,-soname,libfdriver_%s.so", basename);
        snprintf(output, sizeof(output), "%s/fdriver_dso_%u.out", directory, d);
        snprintf(errors, sizeof(errors), "%s/fdriver_dso_%u.err", directory, d);
        char* arguments[] = {
            (char*)"cc",
            (char*)"-shared",
            (char*)"-nostdlib",
            soname,
            files->objects[d == 1 ? FOREIGN_DRIVER_TEST_UNUSED : FOREIGN_DRIVER_TEST_FIRST],
            (char*)"-o",
            library,
            0,
        };
        ready = object_test_run(arguments, output, errors) == 0;
        foreign_test_expect(tally, ready, "link original driver DSO", library);
    }
    return ready;
}

static bool foreign_driver_test_same(String8 left, String8 right)
{
    return left.pointer != 0 && right.pointer != 0 && left.length == right.length && memcmp(left.pointer, right.pointer, left.length) == 0;
}

// Execute the output path directly: env would parse a path containing '=' as another assignment. Borrow every
// inherited entry except LD_LIBRARY_PATH, replacing all its occurrences without changing the process environment.
static int foreign_driver_test_run(const char* directory, const char* executable, const char* output, const char* errors, Arena* arena)
{
    const uint64_t saved = arena->length;
    char library_path[BUSTER_FOREIGN_TEST_PATH_BYTES + 32];
    const int length = snprintf(library_path, sizeof(library_path), "LD_LIBRARY_PATH=%s", directory);
    uint64_t count = 0;
    for (char** entry = environ; entry != 0 && *entry != 0; entry += 1)
    {
        count += 1;
    }
    char** environment = length >= 0 && (uint64_t)length < sizeof(library_path) ? BUSTER_ARENA_PUSH_ARRAY(arena, char*, count + 2) : 0;
    int result = -1;
    if (environment != 0)
    {
        uint64_t used = 0;
        for (uint64_t i = 0; i < count; i += 1)
        {
            if (strncmp(environ[i], "LD_LIBRARY_PATH=", sizeof("LD_LIBRARY_PATH=") - 1) != 0)
            {
                environment[used] = environ[i];
                used += 1;
            }
        }
        environment[used] = library_path;
        environment[used + 1] = 0;
        char* arguments[] = { (char*)executable, 0 };
        result = object_test_run_environment(arguments, output, errors, environment);
    }
    arena_pop_to(arena, saved);
    return result;
}

// A failed invocation must exit normally, issue a diagnostic, and preserve the old output byte for byte. A signal,
// timeout or spawn failure is reported as -1 by object_test_run and must never pass as an expected refusal.
// output_option uses the literal placeholders -oFILE and --output=FILE for the two joined spellings.
static String8 foreign_driver_test_invoke(ForeignTestTally* tally, const char* directory, const char* name, const char* output_option, char* const* inputs, uint32_t count, bool success, int exit_status, Arena* arena)
{
    static const char sentinel[] = "existing driver output\000must survive failed link\377";
    ForeignTestPath executable;
    ForeignTestPath output;
    ForeignTestPath errors;
    char output_argument[BUSTER_FOREIGN_TEST_PATH_BYTES + 16];
    snprintf(executable, sizeof(executable), "%s/fdriver_%s", directory, name);
    snprintf(output, sizeof(output), "%s/fdriver_%s.out", directory, name);
    snprintf(errors, sizeof(errors), "%s/fdriver_%s.err", directory, name);
    char* arguments[BUSTER_FOREIGN_DRIVER_TEST_ARGUMENTS];
    const bool prepared = count + 5 <= BUSTER_ARRAY_LENGTH(arguments) && foreign_test_write(executable, sentinel, sizeof(sentinel));
    foreign_test_expect(tally, prepared, "prepare driver output sentinel", name);
    String8 result = { 0 };
    if (prepared)
    {
        arguments[0] = (char*)"/proc/self/exe";
        arguments[1] = (char*)"link-foreign";
        uint32_t first = 2;
        if (strcmp(output_option, "-oFILE") == 0 || strcmp(output_option, "--output=FILE") == 0)
        {
            snprintf(output_argument, sizeof(output_argument), "%s%s", output_option[1] == 'o' ? "-o" : "--output=", executable);
            arguments[first++] = output_argument;
        }
        else
        {
            arguments[first++] = (char*)output_option;
            arguments[first++] = executable;
        }
        memcpy(arguments + first, inputs, count * sizeof(char*));
        arguments[count + first] = 0;
        const int status = object_test_run(arguments, output, errors);
        foreign_test_expect(tally, success ? status == 0 : status > 0, success ? "driver links fixture" : "driver refuses fixture normally", name);
        const String8 bytes = foreign_test_read(arena, executable);
        if (success && status == 0)
        {
            const int ran = foreign_driver_test_run(directory, executable, output, errors, arena);
            foreign_test_expect(tally, ran == exit_status, "driver executable returns expected status", name);
            const String8 printed = foreign_test_read(arena, output);
            foreign_test_expect(tally, printed.pointer != 0 && printed.length == 0, "driver executable has no unexpected output", name);
            result = bytes;
        }
        else if (!success)
        {
            const String8 expected = {
                .pointer = (char*)sentinel,
                .length = sizeof(sentinel),
            };
            const String8 diagnostic = foreign_test_read(arena, errors);
            foreign_test_expect(tally, foreign_driver_test_same(bytes, expected), "failed driver leaves existing output unchanged", name);
            foreign_test_expect(tally, diagnostic.pointer != 0 && diagnostic.length != 0, "failed driver explains refusal", name);
        }
    }
    return result;
}

static void foreign_driver_test_structure(ForeignTestTally* tally, String8 bytes, uint16_t type, String8 interpreter, const char* name)
{
    Elf64_Ehdr header = { 0 };
    const bool header_inside = bytes.pointer != 0 && bytes.length >= sizeof(header);
    if (header_inside)
    {
        memcpy(&header, bytes.pointer, sizeof(header));
    }
    const bool valid = header_inside && memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_ident[EI_CLASS] == ELFCLASS64 && header.e_type == type &&
                       header.e_machine == EM_X86_64 && header.e_entry != 0 && header.e_phoff <= bytes.length &&
                       (uint64_t)header.e_phnum * sizeof(Elf64_Phdr) <= bytes.length - header.e_phoff;
    foreign_test_expect(tally, valid, "driver output has expected ELF type and entry", name);
    uint32_t interpreters = 0;
    for (uint32_t p = 0; valid && p < header.e_phnum; p += 1)
    {
        Elf64_Phdr segment;
        memcpy(&segment, bytes.pointer + header.e_phoff + p * sizeof(segment), sizeof(segment));
        if (segment.p_type == PT_INTERP)
        {
            interpreters += 1;
            const bool inside = segment.p_offset <= bytes.length && segment.p_filesz <= bytes.length - segment.p_offset;
            const bool same = inside && interpreter.pointer != 0 && segment.p_filesz == interpreter.length + 1 &&
                              memcmp(bytes.pointer + segment.p_offset, interpreter.pointer, interpreter.length) == 0 && bytes.pointer[segment.p_offset + interpreter.length] == 0;
            foreign_test_expect(tally, same, "driver preserves explicit interpreter", name);
        }
    }
    foreign_test_expect(tally, interpreters == (interpreter.length != 0 ? 1u : 0u), "driver emits expected interpreter count", name);
}

#include "buster/compiler/foreign_driver_symbols_test.c"

static void foreign_driver_test_paths(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files, Arena* arena, String8 interpreter)
{
    uint64_t baseline_text_size = UINT64_MAX;
    for (uint32_t fixture = 0; fixture < 3; fixture += 1)
    {
        String8 reference = { 0 };
        for (uint32_t path = 0; path < 2; path += 1)
        {
            for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(foreign_test_lanes); l += 1)
            {
                char name[64];
                char lanes[16];
                char* arguments[16];
                uint32_t count = 0;
                snprintf(name, sizeof(name), "paths_%u_%u_%u", fixture, path, foreign_test_lanes[l]);
                snprintf(lanes, sizeof(lanes), "%u", foreign_test_lanes[l]);
                arguments[count++] = files->objects[fixture == 2 ? FOREIGN_DRIVER_TEST_CALL : FOREIGN_DRIVER_TEST_START];
                arguments[count++] = (char*)"--lanes";
                arguments[count++] = lanes;
                if (path == 0)
                {
                    arguments[count++] = (char*)"--scalar";
                }
                if (fixture == 1)
                {
                    arguments[count++] = files->objects[FOREIGN_DRIVER_TEST_DEAD];
                    arguments[count++] = (char*)"--gc-sections";
                }
                else if (fixture == 2)
                {
                    arguments[count++] = (char*)"-pie";
                    arguments[count++] = (char*)(l % 2 == 0 ? "-dynamic-linker" : "--dynamic-linker");
                    arguments[count++] = interpreter.pointer;
                    arguments[count++] = files->needed;
                }
                const String8 bytes = foreign_driver_test_invoke(tally, directory, name, l % 2 == 0 ? "-o" : "--output", arguments, count, true, 42, arena);
                foreign_driver_test_structure(tally, bytes, fixture == 2 ? ET_DYN : ET_EXEC, fixture == 2 ? interpreter : (String8){ 0 }, name);
                if (path == 0 && l == 0)
                {
                    reference = bytes;
                }
                else
                {
                    foreign_test_expect(tally, foreign_driver_test_same(reference, bytes), "driver output is identical across paths and lane counts", name);
                }
                if (fixture == 1)
                {
                    const ExecutableTestImage image = executable_test_image(bytes);
                    const Elf64_Shdr* text = executable_test_section(&image, ".text");
                    foreign_test_expect(tally, text != 0 && text->sh_size == baseline_text_size, "GC output has only the baseline entry's code", name);
                    const ForeignDriverSymbolsTable table = foreign_driver_symbols_table(bytes);
                    Elf64_Sym symbol;
                    foreign_test_expect(tally, table.valid && foreign_driver_symbols_find(&table, "driver_dead", &symbol) == 0 &&
                                              foreign_driver_symbols_find(&table, "driver_missing", &symbol) == 0,
                                        "GC dead definitions and references are omitted, never fake undefined rows", name);
                }
                else if (fixture == 0 && path == 0 && l == 0)
                {
                    const ExecutableTestImage image = executable_test_image(bytes);
                    const Elf64_Shdr* text = executable_test_section(&image, ".text");
                    baseline_text_size = text != 0 ? text->sh_size : UINT64_MAX;
                }
            }
        }
    }
}

typedef struct ForeignDriverTestCase ForeignDriverTestCase;
struct ForeignDriverTestCase
{
    const char* name;
    char* arguments[16];
    bool success;
    int status;
};

static void foreign_driver_test_cases(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files, Arena* arena)
{
    ForeignDriverTestCase cases[] = {
        {
            .name = "default_static",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "short_entry",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_ENTRY],
                (char*)"-e",
                (char*)"driver_alternate",
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "long_entry",
            .arguments = {
                (char*)"--entry",
                (char*)"driver_alternate",
                files->objects[FOREIGN_DRIVER_TEST_ENTRY],
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "archive_entry",
            .arguments = {
                files->entry,
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "joined_entry_and_lanes",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_ENTRY],
                (char*)"--entry=driver_alternate",
                (char*)"--lanes=2",
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "archive_alternate_entry",
            .arguments = {
                files->alternate,
                (char*)"--entry",
                (char*)"driver_alternate",
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "archive_first_wins",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_CALL],
                files->first,
                (char*)"--scalar",
                files->second,
                (char*)"--lanes",
                (char*)"2",
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "archive_second_wins",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_CALL],
                files->second,
                (char*)"--lanes",
                (char*)"3",
                files->first,
            },
            .success = true,
            .status = 19,
        },
        {
            .name = "whole_scope",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_CALL],
                (char*)"--whole-archive",
                files->first,
                (char*)"--scalar",
                (char*)"--no-whole-archive",
                files->poison,
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "whole_poison",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--whole-archive",
                (char*)"--lanes",
                (char*)"2",
                files->poison,
                (char*)"--no-whole-archive",
            },
        },
        {
            .name = "library_search",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_CALL],
                (char*)"-L",
                (char*)directory,
                (char*)"-Bstatic",
                (char*)"--lanes",
                (char*)"3",
                (char*)"-l",
                (char*)"fdriver_pick",
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "option_named_symbols",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--gc-sections",
                (char*)"-u",
                (char*)"--gc-sections",
                (char*)"--undefined",
                (char*)"--scalar",
                files->options,
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "strip_option_named_symbols",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--gc-sections",
                (char*)"-u",
                (char*)"-s",
                (char*)"--undefined",
                (char*)"--strip-all",
                files->options,
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "strip_reject_value",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--strip-all=false",
            },
        },
        {
            .name = "strip_reject_joined",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"-sfoo",
            },
        },
        {
            .name = "stripped_undefined",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"-s",
                files->objects[FOREIGN_DRIVER_TEST_DEAD],
            },
        },
        {
            .name = "default_static_search",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_CALL],
                (char*)"-L",
                (char*)directory,
                (char*)"-lfdriver_choice",
            },
            .success = true,
            .status = 19,
        },
        {
            .name = "explicit_static_after_pie",
            .arguments = {
                (char*)"-pie",
                files->objects[FOREIGN_DRIVER_TEST_CALL],
                (char*)"-L",
                (char*)directory,
                (char*)"-static",
                (char*)"-lfdriver_choice",
            },
            .success = true,
            .status = 19,
        },
        {
            .name = "static_explicit_dso",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_CALL],
                files->needed,
            },
        },
        {
            .name = "static_dynamic_search_override",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_CALL],
                (char*)"-L",
                (char*)directory,
                (char*)"-Bdynamic",
                (char*)"-lfdriver_choice",
            },
        },
        {
            .name = "eager_undefined",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                files->objects[FOREIGN_DRIVER_TEST_DEAD],
            },
        },
        {
            .name = "gc_disabled_last",
            .arguments = {
                (char*)"--gc-sections",
                files->objects[FOREIGN_DRIVER_TEST_START],
                files->objects[FOREIGN_DRIVER_TEST_DEAD],
                (char*)"--no-gc-sections",
            },
        },
        {
            .name = "gc_enabled_last",
            .arguments = {
                (char*)"--no-gc-sections",
                files->objects[FOREIGN_DRIVER_TEST_START],
                files->objects[FOREIGN_DRIVER_TEST_DEAD],
                (char*)"--gc-sections",
            },
            .success = true,
            .status = 42,
        },
        {
            .name = "gc_live_undefined",
            .arguments = {
                (char*)"--gc-sections",
                files->objects[FOREIGN_DRIVER_TEST_START],
                files->objects[FOREIGN_DRIVER_TEST_DEAD],
                (char*)"-e",
                (char*)"driver_dead",
            },
        },
        {
            .name = "gc_forced_undefined",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                files->objects[FOREIGN_DRIVER_TEST_DEAD],
                (char*)"--gc-sections",
                (char*)"-u",
                (char*)"driver_missing",
            },
        },
        {
            .name = "gc_forced_dead_function",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                files->objects[FOREIGN_DRIVER_TEST_DEAD],
                (char*)"--gc-sections",
                (char*)"--undefined=driver_dead",
            },
        },
        {
            .name = "no_implicit_crt",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_MAIN],
            },
        },
        {
            .name = "missing_entry",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--entry",
                (char*)"driver_absent",
            },
        },
        {
            .name = "call_out_of_range",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_CALL],
                files->objects[FOREIGN_DRIVER_TEST_ABSOLUTE],
            },
        },
        {
            .name = "no_inputs",
        },
        {
            .name = "unknown_option",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--not-a-link-foreign-option",
            },
        },
        {
            .name = "missing_entry_value",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--entry",
            },
        },
        {
            .name = "missing_output_value",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"-o",
            },
        },
        {
            .name = "missing_loader_value",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"-pie",
                (char*)"--dynamic-linker",
            },
        },
        {
            .name = "missing_lanes_value",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--lanes",
            },
        },
        {
            .name = "zero_lanes",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--lanes",
                (char*)"0",
            },
        },
        {
            .name = "negative_lanes",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--lanes",
                (char*)"-1",
            },
        },
        {
            .name = "invalid_lanes",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--lanes",
                (char*)"2x",
            },
        },
        {
            .name = "overflow_lanes",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--lanes",
                (char*)"4294967296",
            },
        },
        {
            .name = "empty_joined_output",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--output=",
            },
        },
        {
            .name = "empty_joined_entry",
            .arguments = {
                files->objects[FOREIGN_DRIVER_TEST_START],
                (char*)"--entry=",
            },
        },
    };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        ForeignDriverTestCase* test = &cases[c];
        uint32_t count = 0;
        while (count < BUSTER_ARRAY_LENGTH(test->arguments) && test->arguments[count] != 0)
        {
            count += 1;
        }
        const String8 bytes = foreign_driver_test_invoke(tally, directory, test->name, c % 2 == 0 ? "-o" : "--output", test->arguments, count, test->success, test->status, arena);
        if (c == 0)
        {
            foreign_driver_test_structure(tally, bytes, ET_EXEC, (String8){ 0 }, test->name);
        }
        else if (strcmp(test->name, "option_named_symbols") == 0)
        {
            static const uint8_t first[] = { 0xb8, 0xe1, 0x47, 0xab, 0x13, 0xc3 };
            static const uint8_t second[] = { 0xb8, 0xf2, 0x68, 0xcd, 0x35, 0xc3 };
            const ExecutableTestImage image = executable_test_image(bytes);
            const Elf64_Shdr* section = executable_test_section(&image, ".text");
            bool found_first = false;
            bool found_second = false;
            const bool inside = section != 0 && section->sh_offset <= bytes.length && section->sh_size <= bytes.length - section->sh_offset;
            for (uint64_t offset = 0; inside && offset + sizeof(first) <= section->sh_size; offset += 1)
            {
                const char* code = bytes.pointer + section->sh_offset + offset;
                found_first = found_first || memcmp(code, first, sizeof(first)) == 0;
                found_second = found_second || memcmp(code, second, sizeof(second)) == 0;
            }
            foreign_test_expect(tally, found_first && found_second, "both option-looking forced roots retain their distinct code through GC", test->name);
        }
        else if (strcmp(test->name, "strip_option_named_symbols") == 0)
        {
            const ForeignDriverSymbolsTable table = foreign_driver_symbols_table(bytes);
            Elf64_Sym short_name = { 0 };
            Elf64_Sym long_name = { 0 };
            foreign_test_expect(tally, foreign_driver_symbols_find(&table, "-s", &short_name) == 1 && foreign_driver_symbols_find(&table, "--strip-all", &long_name) == 1 &&
                                      short_name.st_shndx != SHN_UNDEF && long_name.st_shndx != SHN_UNDEF,
                                "strip-looking undefined values remain named GC roots without stripping", test->name);
        }
    }
    ForeignTestPath missing;
    snprintf(missing, sizeof(missing), "%s/fdriver_missing.o", directory);
    char* arguments[] = {
        files->objects[FOREIGN_DRIVER_TEST_START],
        missing,
    };
    foreign_driver_test_invoke(tally, directory, "missing_file", "-o", arguments, BUSTER_ARRAY_LENGTH(arguments), false, 0, arena);
    char* joined[] = {
        files->objects[FOREIGN_DRIVER_TEST_START],
    };
    foreign_driver_test_invoke(tally, directory, "attached=fdriver_equals_regression", "-oFILE", joined, BUSTER_ARRAY_LENGTH(joined), true, 42, arena);
    foreign_driver_test_invoke(tally, directory, "joined_output", "--output=FILE", joined, BUSTER_ARRAY_LENGTH(joined), true, 42, arena);
}

// The original fixtures put two relocated addresses in their only DATA atom and read them at runtime. This oracle
// inspects those addresses independently of the regular symbol table, so it cannot pass from metadata alone.
static bool foreign_driver_test_string_values(String8 bytes, uint64_t* first, uint64_t* second)
{
    const ExecutableTestImage image = executable_test_image(bytes);
    const Elf64_Shdr* data = executable_test_section(&image, ".data");
    const bool valid = data != 0 && data->sh_size == 2 * sizeof(uint64_t) && data->sh_offset <= bytes.length && data->sh_size <= bytes.length - data->sh_offset;
    if (valid)
    {
        memcpy(first, bytes.pointer + data->sh_offset, sizeof(*first));
        memcpy(second, bytes.pointer + data->sh_offset + sizeof(*first), sizeof(*second));
    }
    return valid;
}

static void foreign_driver_test_strings(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files, Arena* arena)
{
    String8 gc_reference = { 0 };
    for (uint32_t mode = 0; mode < 6; mode += 1)
    {
        char name[64];
        char* arguments[4];
        uint32_t count = 0;
        snprintf(name, sizeof(name), "strings_%u", mode);
        arguments[count++] = files->objects[FOREIGN_DRIVER_TEST_STRINGS];
        if (mode >= 4)
        {
            arguments[count++] = (char*)"--gc-sections";
            arguments[count++] = (char*)"--merge-strings";
            if (mode == 5)
            {
                arguments[count++] = (char*)"--scalar";
            }
        }
        else if (mode != 0)
        {
            arguments[count++] = (char*)(mode == 3 ? "--no-merge-strings" : "--merge-strings");
        }
        if (mode == 2 || mode == 3)
        {
            arguments[count++] = (char*)(mode == 2 ? "--no-merge-strings" : "--merge-strings");
        }
        const String8 bytes = foreign_driver_test_invoke(tally, directory, name, "-o", arguments, count, true, 42, arena);
        uint64_t first = 0;
        uint64_t second = 0;
        const bool named = foreign_driver_test_string_values(bytes, &first, &second);
        foreign_test_expect(tally, named, "driver emits both relocated string addresses", name);
        foreign_test_expect(tally, named && ((first == second) == (mode == 1 || mode >= 3)), "last string option controls pooling, initially disabled", name);
        if (mode == 4)
        {
            gc_reference = bytes;
        }
        else if (mode == 5)
        {
            foreign_test_expect(tally, foreign_driver_test_same(gc_reference, bytes), "GC then string pooling has identical scalar and vector output", name);
        }
    }
    // A named PC-relative relocation does not preserve enough field-bias information for string pooling. The
    // successful fallback must keep the complete original layout, including both literal addresses and fixups.
    char* baseline_arguments[] = {
        files->objects[FOREIGN_DRIVER_TEST_STRINGS_PC],
        (char*)"--no-merge-strings",
    };
    const String8 baseline = foreign_driver_test_invoke(tally, directory, "strings_pc_baseline", "-o", baseline_arguments, BUSTER_ARRAY_LENGTH(baseline_arguments), true, 42, arena);
    uint64_t first = 0;
    uint64_t second = 0;
    const bool distinct = foreign_driver_test_string_values(baseline, &first, &second) && first != second;
    foreign_test_expect(tally, distinct, "named PC-relative fallback baseline keeps distinct literal addresses", 0);
    for (uint32_t path = 0; path < 2; path += 1)
    {
        char* arguments[] = {
            files->objects[FOREIGN_DRIVER_TEST_STRINGS_PC],
            (char*)"--merge-strings",
            (char*)"--scalar",
        };
        const char* name = path == 0 ? "strings_pc_fallback_vector" : "strings_pc_fallback_exact";
        const uint32_t count = BUSTER_ARRAY_LENGTH(arguments) - (path == 0 ? 1u : 0u);
        const String8 bytes = foreign_driver_test_invoke(tally, directory, name, "-o", arguments, count, true, 42, arena);
        foreign_test_expect(tally, foreign_driver_test_same(baseline, bytes), "named PC-relative string merge fallback preserves complete output", name);
    }
}

static void foreign_driver_test_dynamic(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files, Arena* arena, ForeignTestSystem* system, String8 interpreter)
{
    char loader_option[BUSTER_FOREIGN_TEST_PATH_BYTES + 32];
    snprintf(loader_option, sizeof(loader_option), "--dynamic-linker=%.*s", (int)interpreter.length, interpreter.pointer);
    for (uint32_t explicit_loader = 0; explicit_loader < 2; explicit_loader += 1)
    {
        char* arguments[] = {
            (char*)"-pie",
            files->objects[FOREIGN_DRIVER_TEST_CALL],
            (char*)"-L",
            (char*)directory,
            (char*)"-lfdriver_choice",
            loader_option,
        };
        const char* name = explicit_loader == 0 ? "default_pie_search_and_loader" : "default_pie_search_joined_loader";
        const uint32_t count = BUSTER_ARRAY_LENGTH(arguments) - (explicit_loader == 0 ? 1u : 0u);
        const String8 bytes = foreign_driver_test_invoke(tally, directory, name, "-o", arguments, count, true, 42, arena);
        foreign_driver_test_structure(tally, bytes, ET_DYN, interpreter, name);
        const DynamicTestImage image = dynamic_test_image(bytes);
        const char* needed[2] = { 0 };
        const uint32_t found = dynamic_test_needed(&image, needed, BUSTER_ARRAY_LENGTH(needed));
        foreign_test_expect(tally, image.valid && found == 1 && needed[0] != 0 && strcmp(needed[0], "libfdriver_choice.so") == 0,
                            "PIE defaults to shared library with different result from same-name archive", name);
    }
    for (uint32_t mode = 0; mode < 3; mode += 1)
    {
        char name[64];
        char* arguments[24];
        uint32_t count = 0;
        snprintf(name, sizeof(name), "needed_%u", mode);
        arguments[count++] = (char*)"-pie";
        arguments[count++] = (char*)"--dynamic-linker";
        arguments[count++] = interpreter.pointer;
        arguments[count++] = files->objects[FOREIGN_DRIVER_TEST_CALL];
        arguments[count++] = (char*)"-L";
        arguments[count++] = (char*)directory;
        arguments[count++] = (char*)(mode == 2 ? "-Bstatic" : "-Bdynamic");
        arguments[count++] = (char*)"--lanes";
        arguments[count++] = (char*)"3";
        arguments[count++] = (char*)"-lfdriver_pick";
        arguments[count++] = (char*)(mode == 0 ? "--as-needed" : "--no-as-needed");
        arguments[count++] = (char*)"--no-gc-sections";
        arguments[count++] = (char*)"-Bdynamic";
        arguments[count++] = (char*)"-l";
        arguments[count++] = (char*)"fdriver_unused";
        const String8 bytes = foreign_driver_test_invoke(tally, directory, name, "--output", arguments, count, true, 42, arena);
        foreign_driver_test_structure(tally, bytes, ET_DYN, interpreter, name);
        const DynamicTestImage image = dynamic_test_image(bytes);
        const char* needed[4] = { 0 };
        const uint32_t expected = mode == 1 ? 2 : 1;
        const uint32_t found = dynamic_test_needed(&image, needed, BUSTER_ARRAY_LENGTH(needed));
        const bool first = found != 0 && needed[0] != 0 && strcmp(needed[0], mode == 2 ? "libfdriver_unused.so" : "libfdriver_pick.so") == 0;
        const bool second = mode != 1 || (found > 1 && needed[1] != 0 && strcmp(needed[1], "libfdriver_unused.so") == 0);
        foreign_test_expect(tally, image.valid && found == expected && first && second, "driver preserves library search mode and as-needed scope", name);
    }
    foreign_test_expect(tally, system->found, "find explicit system CRT inputs", 0);
    if (system->found)
    {
        char* arguments[BUSTER_FOREIGN_DRIVER_TEST_ARGUMENTS];
        uint32_t count = 0;
        arguments[count++] = (char*)"-pie";
        arguments[count++] = (char*)"-dynamic-linker";
        arguments[count++] = interpreter.pointer;
        for (uint32_t i = 0; i < RUNTIME_OBJECT_END; i += 1)
        {
            arguments[count++] = system->objects[i];
        }
        arguments[count++] = files->objects[FOREIGN_DRIVER_TEST_MAIN];
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(system->directories); i += 1)
        {
            arguments[count++] = system->directories[i];
        }
        arguments[count++] = (char*)"-lgcc";
        arguments[count++] = (char*)"--as-needed";
        arguments[count++] = (char*)"-lgcc_s";
        arguments[count++] = (char*)"--no-as-needed";
        arguments[count++] = (char*)"-lc";
        arguments[count++] = (char*)"-lgcc";
        arguments[count++] = (char*)"--as-needed";
        arguments[count++] = (char*)"-lgcc_s";
        arguments[count++] = (char*)"--no-as-needed";
        for (uint32_t i = RUNTIME_OBJECT_END; i < RUNTIME_OBJECTS; i += 1)
        {
            arguments[count++] = system->objects[i];
        }
        const String8 bytes = foreign_driver_test_invoke(tally, directory, "explicit_crt", "-o", arguments, count, true, 42, arena);
        foreign_driver_test_structure(tally, bytes, ET_DYN, interpreter, "explicit_crt");
    }
}

static void foreign_driver_test_oracles(ForeignTestTally* tally, const char* directory, ForeignDriverTestFiles* files, Arena* arena, String8 interpreter)
{
    static const char* const linkers[] = {
        "ld.bfd",
        "ld.lld",
    };
    for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(linkers); l += 1)
    {
        for (uint32_t fixture = 0; fixture < 3; fixture += 1)
        {
            ForeignTestPath executable;
            ForeignTestPath output;
            ForeignTestPath errors;
            snprintf(executable, sizeof(executable), "%s/fdriver_oracle_%u_%u", directory, l, fixture);
            snprintf(output, sizeof(output), "%s/fdriver_oracle_%u_%u.out", directory, l, fixture);
            snprintf(errors, sizeof(errors), "%s/fdriver_oracle_%u_%u.err", directory, l, fixture);
            char* arguments[16];
            uint32_t count = 0;
            arguments[count++] = (char*)linkers[l];
            arguments[count++] = (char*)"-o";
            arguments[count++] = executable;
            arguments[count++] = (char*)"-e";
            arguments[count++] = (char*)"_start";
            arguments[count++] = files->objects[fixture == 2 ? FOREIGN_DRIVER_TEST_CALL : FOREIGN_DRIVER_TEST_START];
            if (fixture == 2)
            {
                arguments[count++] = (char*)"-pie";
                arguments[count++] = (char*)"--dynamic-linker";
                arguments[count++] = interpreter.pointer;
                arguments[count++] = files->needed;
            }
            else
            {
                arguments[count++] = (char*)"-static";
                if (fixture == 1)
                {
                    arguments[count++] = (char*)"--gc-sections";
                    arguments[count++] = files->objects[FOREIGN_DRIVER_TEST_DEAD];
                }
            }
            arguments[count] = 0;
            const bool linked = object_test_run(arguments, output, errors) == 0;
            foreign_test_expect(tally, linked, "reference linker accepts original driver fixture", linkers[l]);
            if (linked)
            {
                foreign_test_expect(tally, foreign_driver_test_run(directory, executable, output, errors, arena) == 42, "reference driver fixture exits 42", linkers[l]);
                const String8 bytes = foreign_test_read(arena, executable);
                foreign_driver_test_structure(tally, bytes, fixture == 2 ? ET_DYN : ET_EXEC, fixture == 2 ? interpreter : (String8){ 0 }, linkers[l]);
            }
        }
    }
}

static bool foreign_driver_test(const char* directory, Arena* arena, bool cross_check, ForeignTestSystem* system)
{
    const uint64_t saved = arena->length;
    ForeignTestTally tally = {
        .check = "foreign command driver",
    };
    ForeignDriverTestFiles files = { 0 };
    Runtime runtime;
    const bool found = runtime_import(&runtime) == RUNTIME_ERROR_NONE && runtime.loader_path.length != 0;
    foreign_test_expect(&tally, found, "find system loader for raw driver tests", 0);
    if (found && foreign_driver_test_compile(&tally, directory, &files, arena))
    {
        foreign_driver_test_paths(&tally, directory, &files, arena, runtime.loader_path);
        foreign_driver_symbols_test(&tally, directory, &files, arena, runtime.loader_path);
        foreign_driver_test_cases(&tally, directory, &files, arena);
        foreign_driver_test_strings(&tally, directory, &files, arena);
        foreign_driver_test_dynamic(&tally, directory, &files, arena, system, runtime.loader_path);
        if (cross_check)
        {
            foreign_driver_test_oracles(&tally, directory, &files, arena, runtime.loader_path);
        }
    }
    runtime_release(&runtime);
    arena_pop_to(arena, saved);
    return foreign_test_report(&tally);
}

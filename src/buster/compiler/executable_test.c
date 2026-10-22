// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The direct path's executable tests, for the test-executable command:
//
//   build/compiler test-executable [--seed N] [--functions N] [--directory PATH] [--keep]
//
// main.c includes this file after executable.c and calls run_executable_tests for the command. Executables are written
// into a directory of their own under --directory (TMPDIR, or /tmp, by default), which the tests remove unless --keep
// says to leave it and the last executables there. The checks, in this order:
//   - Programs: link_program_generate with the sets of options test-link uses (link_test_program_options), and one of
//     50,000 workers in 38 chunks, several for each of 8 lanes to write. Each is written by executable_write with
//     groups of 1, 2, 3 and 8 lanes (lane_run), each time from zeroed fields over the last file scribbled over
//     (executable_test_reset), so that every write has all of it to do: the files must be byte-identical, and run, each
//     must exit with status 0 having written LinkProgram.output; and the exact twins must write the same file with 1
//     and 8 lanes. A copy with one fixup 8 bytes off must write something else. The sets that call libc, step 5's, must
//     fail with an import named, without creating the output.
//   - Fields: in each program's file, every fixup's field against a scalar reference of its kind's arithmetic
//     (executable_test_expected), from the atoms' addresses and the symbols' definitions, with the TLS segment and the
//     linker's sections read back from the file's headers; the GOT, a slot for each GOT kind's target, numbered from 1
//     in symbol order and holding its value; and the headers themselves: the segments in lld's order, 4 KB-congruent,
//     with contiguous file offsets; and the unwind tables, .eh_frame_hdr read back as an unwinder reads it against the
//     test's own walk of .eh_frame (executable_test_unwind), which test-dynamic shares.
//   - Chunks: every chunk boundary of every program, and of programs built to put 4-byte fields (a call's rel32) and
//     8-byte fields (an ABS64 at an odd offset) across each place around a nominal boundary
//     (executable_test_straddle): no boundary splits a field, the lanes write the same file (each from zeroed fields
//     over a scribbled file, as above) and the program runs.
//   - Files: in place (a smaller program after a larger one, then a file whose mode was changed), created, and written
//     beside the output and renamed over it when a process runs it, when it has another link, when it is a symbolic
//     link, when it is someone else's (as root), past a file a killed link left, and with a name of NAME_MAX bytes;
//     written in order into a FIFO, opened once for writing (inotify counts the closes after writing), for a reader
//     that opens it and one that holds the read end of a FIFO that may be written but not read; a directory and a path
//     under a regular file refused; a link that fails before opening the output leaves it alone, one whose fixup
//     doesn't fit removes it, one that fails beside it leaves it and nothing beside it, and one into a FIFO leaves the
//     FIFO.
//   - Limits: a weak thread-local symbol defined nowhere, TLS blocks and atoms past 2 GB, refusals reported alike with
//     every lane count (among them the first of two symbols defined nowhere in different lanes' ranges), and a lane
//     short of scratch memory at every amount from none up (executable_test_limits).
//   - Build IDs: the lanes' (the files are identical), another program's, and the tree's root recomputed from the file.
//   - Unwind tables: the scanner against its twin and what the test knows of .eh_frame atoms built here, the sort
//     against a stable sort, and links whose tables must come out sorted, or are refused (see "Unwind tables").
//   - Blocks: random links for the vector passes over blocks of 16 atoms and symbols, from the seed and from seed 116,
//     written by every lane count and by the exact twins into the same file, which runs (executable_test_blocks).
//   - The patch kernel: the fast path against the checked one (executable_test_kernel).
// The exit status is 1 if the arguments are invalid or a check fails.
//
// Layout, in this order: the tally, running and reading files, writing with lanes, the fields and headers, the unwind
// tables read back, the build IDs, the programs, hand-made links, the chunks, the files, the limits, the unwind tables,
// the blocks, the patch kernel, and run_executable_tests.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <elf.h>

// OS-dependent includes
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define BUSTER_EXECUTABLE_TEST_REPORTED 3
#define BUSTER_EXECUTABLE_TEST_RUN_SECONDS 20
#define BUSTER_EXECUTABLE_TEST_OUTPUT_BYTES 256
#define BUSTER_EXECUTABLE_TEST_DEFAULT_FUNCTIONS 2000
#define BUSTER_EXECUTABLE_TEST_PATH_BYTES 512
#define BUSTER_EXECUTABLE_TEST_MAX_BOUNDARIES 4096
// Workers enough for 38 chunks, several for each of 8 lanes.
#define BUSTER_EXECUTABLE_TEST_MANY_CHUNKS_FUNCTIONS 50000

static const uint32_t executable_test_lane_counts[] = { 1, 2, 3, 8 };

typedef struct ExecutableTestTally ExecutableTestTally;
struct ExecutableTestTally
{
    const char* check;
    uint64_t checked;
    uint64_t failures;
};

static void executable_test_expect(ExecutableTestTally* tally, bool condition, const char* what, uint64_t index)
{
    tally->checked += 1;
    if (!condition)
    {
        if (tally->failures < BUSTER_EXECUTABLE_TEST_REPORTED)
        {
            printf("  %s: %s (%llu)\n", tally->check, what, (unsigned long long)index);
        }
        tally->failures += 1;
    }
}

static bool executable_test_report(const ExecutableTestTally* tally)
{
    printf("%s: %llu checks, %llu failed\n", tally->check, (unsigned long long)tally->checked, (unsigned long long)tally->failures);
    return tally->failures == 0;
}

// splitmix64.
static uint64_t executable_test_random(uint64_t* state)
{
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t x = *state;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// ---------------------------------------------------------------------------------------------------------------------
// Running and reading files

// The test directory, and paths in it.
typedef struct ExecutableTestDirectory ExecutableTestDirectory;
struct ExecutableTestDirectory
{
    uint64_t id_order;
    char path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    bool keep;
};

static String8 executable_test_path(const ExecutableTestDirectory* directory, const char* name, char* buffer)
{
    const int length = snprintf(buffer, BUSTER_EXECUTABLE_TEST_PATH_BYTES, "%s/%s", directory->path, name);
    String8 result = {
        .pointer = buffer,
        .length = length > 0 ? (uint64_t)length : 0,
    };
    return result;
}

// Runs the executable `arguments` name first with them (a null-terminated list), in `environment`: what it writes to
// standard output, and how it ended.
static bool executable_test_run_arguments(char** arguments, char** environment, uint8_t* output, uint64_t* output_length, int* status)
{
    int ends[2];
    bool result = pipe(ends) == 0;
    *output_length = 0;
    *status = -1;
    if (result)
    {
        fflush(stdout);
        fflush(stderr);
        const pid_t child = fork();
        if (child == 0)
        {
            dup2(ends[1], 1);
            close(ends[0]);
            close(ends[1]);
            alarm(BUSTER_EXECUTABLE_TEST_RUN_SECONDS);
            execve(arguments[0], arguments, environment);
            _exit(127);
        }
        close(ends[1]);
        result = child > 0;
        ssize_t got = 1;
        while (result && got > 0 && *output_length < BUSTER_EXECUTABLE_TEST_OUTPUT_BYTES)
        {
            got = read(ends[0], output + *output_length, BUSTER_EXECUTABLE_TEST_OUTPUT_BYTES - *output_length);
            *output_length += got > 0 ? (uint64_t)got : 0;
        }
        close(ends[0]);
        result = result && waitpid(child, status, 0) == child;
    }
    return result;
}

// Runs the executable at `path` without arguments, in `environment`: what it writes to standard output, and how it
// ended.
static bool executable_test_run_with(const char* path, char** environment, uint8_t* output, uint64_t* output_length, int* status)
{
    char* arguments[] = { (char*)path, 0 };
    return executable_test_run_arguments(arguments, environment, output, output_length, status);
}

// Runs the executable at `path` without arguments, in an environment of its own: what it writes to standard output, and
// how it ended.
static bool executable_test_run(const char* path, uint8_t* output, uint64_t* output_length, int* status)
{
    char* environment[] = { "BUSTER=1", 0 };
    return executable_test_run_with(path, environment, output, output_length, status);
}

// Whether the executable at `path` exits with status 0 having written exactly `expected`.
static bool executable_test_runs(const char* path, const uint8_t* expected, uint64_t expected_length)
{
    uint8_t output[BUSTER_EXECUTABLE_TEST_OUTPUT_BYTES];
    uint64_t length = 0;
    int status = -1;
    const bool ran = executable_test_run(path, output, &length, &status);
    return ran && WIFEXITED(status) && WEXITSTATUS(status) == 0 && length == expected_length && memcmp(output, expected, length) == 0;
}

// The file at `path`, read whole into `arena`: its bytes, or a null pointer.
static String8 executable_test_read(Arena* arena, const char* path)
{
    String8 result = { 0 };
    const int file = open(path, O_RDONLY | O_CLOEXEC);
    struct stat status;
    if (file >= 0 && fstat(file, &status) == 0)
    {
        char* bytes = arena_push(arena, (uint64_t)status.st_size + 1, 64);
        uint64_t length = 0;
        ssize_t got = 1;
        while (bytes != 0 && got > 0 && length < (uint64_t)status.st_size)
        {
            got = pread(file, bytes + length, (uint64_t)status.st_size - length, (off_t)length);
            length += got > 0 ? (uint64_t)got : 0;
        }
        if (bytes != 0 && length == (uint64_t)status.st_size)
        {
            result.pointer = bytes;
            result.length = length;
        }
    }
    if (file >= 0)
    {
        close(file);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Writing with lanes

typedef struct ExecutableTestWrite ExecutableTestWrite;
struct ExecutableTestWrite
{
    Link* link;
    ExecutableOptions options;
    ExecutableResult result;
    // With `starve`, lane `starved_lane` leaves executable_write only `room` bytes of its scratch arena.
    bool starve;
    uint32_t starved_lane;
    uint64_t room;
    bool succeeded;
};

static void executable_test_lane_main(void* argument)
{
    ExecutableTestWrite* write = argument;
    Scratch scratch = scratch_begin(0);
    const uint64_t free_bytes = scratch.arena->capacity - scratch.arena->length;
    if (write->starve && lane_index() == write->starved_lane && free_bytes > write->room)
    {
        arena_push(scratch.arena, free_bytes - write->room, 1);
    }
    const bool succeeded = executable_write(write->link, &write->options, &write->result);
    scratch_end(scratch);
    if (lane_index() == 0)
    {
        write->succeeded = succeeded;
    }
}

// Writes the link to `path` with a group of `lanes` lanes, lane `starved_lane` having only `room` bytes of scratch
// memory: whether executable_write succeeded, and its result.
static bool executable_test_write_starved(Link* link, SymbolId entry, String8 path, uint32_t lanes, uint32_t starved_lane, uint64_t room, ExecutableResult* result)
{
    ExecutableTestWrite write = {
        .link = link,
        .options = {
            .path = path,
            .entry = entry,
        },
        .starve = true,
        .starved_lane = starved_lane,
        .room = room,
        .succeeded = false,
    };
    const bool ran = lane_run(lanes, executable_test_lane_main, &write, 0);
    *result = write.result;
    return ran && write.succeeded;
}

// Writes the link to `path` with a group of `lanes` lanes: whether executable_write succeeded, and its result.
static bool executable_test_write(Link* link, SymbolId entry, String8 path, uint32_t lanes, ExecutableResult* result, uint64_t* boundaries, uint64_t boundary_capacity)
{
    ExecutableTestWrite write = {
        .link = link,
        .options = {
            .path = path,
            .entry = entry,
            .boundaries = boundaries,
            .boundary_capacity = boundary_capacity,
        },
        .succeeded = false,
    };
    const bool ran = lane_run(lanes, executable_test_lane_main, &write, 0);
    *result = write.result;
    return ran && write.succeeded;
}

// Writes the link to `path` with a group of `lanes` lanes by the passes' exact twins (ExecutableOptions.exact): whether
// executable_write succeeded, and its result.
static bool executable_test_write_exact(Link* link, SymbolId entry, String8 path, uint32_t lanes, ExecutableResult* result)
{
    ExecutableTestWrite write = {
        .link = link,
        .options = {
            .path = path,
            .entry = entry,
            .exact = true,
        },
        .succeeded = false,
    };
    const bool ran = lane_run(lanes, executable_test_lane_main, &write, 0);
    *result = write.result;
    return ran && write.succeeded;
}

// Undoes what a write left, so that the next write has all of it to do again: every fixup's field in the atoms' bytes
// back to zeros, as the generator leaves them, and the output, if it exists, overwritten with 0x5a over its whole
// length, so that a write in place over it must write every byte. Returns whether the output could be overwritten.
static bool executable_test_reset(Link* link, const char* path)
{
    for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
    {
        uint8_t* bytes = link->atoms.bytes[atom];
        const FixupId begin = link->atoms.fixup_begin[atom];
        for (FixupId fixup = begin; bytes != 0 && fixup < begin + link->atoms.fixup_count[atom]; fixup += 1)
        {
            const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
            memset(bytes + link->fixups.offset[fixup], 0, link_fixup_kinds[kind].width);
        }
    }
    const int file = open(path, O_RDWR | O_CLOEXEC);
    struct stat status;
    bool result = file < 0 ? errno == ENOENT : fstat(file, &status) == 0;
    uint8_t pattern[BUSTER_EXECUTABLE_PAGE_BYTES];
    memset(pattern, 0x5a, sizeof(pattern));
    for (uint64_t at = 0; result && file >= 0 && at < (uint64_t)status.st_size; at += sizeof(pattern))
    {
        const uint64_t size = (uint64_t)status.st_size - at < sizeof(pattern) ? (uint64_t)status.st_size - at : sizeof(pattern);
        result = pwrite(file, pattern, size, (off_t)at) == (ssize_t)size;
    }
    if (file >= 0)
    {
        close(file);
    }
    return result;
}

// The atoms' bytes as the link's maker left them, all in one copy on `arena`, or null if it has no room. A write
// patches them in place, and its relaxations rewrite instructions (a TLS access's sequence, a load's or a call's
// through the GOT), which a TLS access's next write would no longer find, and which an object written from the link
// must not have: executable_test_restore puts them back.
static uint8_t* executable_test_save(const Link* link, Arena* arena)
{
    uint64_t total = 0;
    for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
    {
        total += link->atoms.bytes[atom] != 0 ? link->atoms.size[atom] : 0;
    }
    uint8_t* result = arena_push(arena, total + 1, 64);
    uint64_t at = 0;
    for (AtomId atom = 1; result != 0 && atom < link->atoms.count; atom += 1)
    {
        const uint64_t size = link->atoms.bytes[atom] != 0 ? link->atoms.size[atom] : 0;
        memcpy(result + at, link->atoms.bytes[atom], size);
        at += size;
    }
    return result;
}

static void executable_test_restore(Link* link, const uint8_t* saved)
{
    uint64_t at = 0;
    for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
    {
        const uint64_t size = link->atoms.bytes[atom] != 0 ? link->atoms.size[atom] : 0;
        memcpy(link->atoms.bytes[atom], saved + at, size);
        at += size;
    }
}

static const char* const executable_test_error_names[] = {
    [EXECUTABLE_ERROR_NONE] = "none",
    [EXECUTABLE_ERROR_MEMORY] = "memory",
    [EXECUTABLE_ERROR_LAYOUT] = "layout",
    [EXECUTABLE_ERROR_UNDEFINED] = "undefined",
    [EXECUTABLE_ERROR_UNSUPPORTED] = "unsupported",
    [EXECUTABLE_ERROR_ENTRY] = "entry",
    [EXECUTABLE_ERROR_RELOCATION] = "relocation",
    [EXECUTABLE_ERROR_FIXUP] = "fixup",
    [EXECUTABLE_ERROR_FILE] = "file",
    [EXECUTABLE_ERROR_UNWIND] = "unwind",
};
_Static_assert(BUSTER_ARRAY_LENGTH(executable_test_error_names) == EXECUTABLE_ERROR_UNWIND + 1, "every error has a name");

static void executable_test_print_failure(const char* what, const ExecutableResult* result)
{
    printf("  %s: %s error, atom %u, symbol %u, fixup %u, %s: %s\n", what, executable_test_error_names[result->error], result->atom, result->symbol, result->fixup,
           result->call != 0 ? result->call : "-", result->system_error != 0 ? strerror(result->system_error) : "-");
}

// ---------------------------------------------------------------------------------------------------------------------
// Fields and headers
//
// What the file says about its own layout, read back from its headers: the program headers' segments and the section
// headers' sections, by name.

typedef struct ExecutableTestImage ExecutableTestImage;
struct ExecutableTestImage
{
    const Elf64_Ehdr* header;
    const Elf64_Phdr* program_headers;
    const Elf64_Shdr* section_headers;
    const char* names;
    bool valid;
};

// The file read as an executable of ELF type `type` (ET_EXEC, or ET_DYN for a PIE): valid if its headers are within it.
static ExecutableTestImage executable_test_image_of(String8 file, uint16_t type)
{
    ExecutableTestImage result = { 0 };
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)file.pointer;
    result.valid = file.length >= sizeof(Elf64_Ehdr) && memcmp(header->e_ident, ELFMAG, SELFMAG) == 0 && header->e_ident[EI_CLASS] == ELFCLASS64 &&
                   header->e_type == type && header->e_machine == EM_X86_64 && header->e_phoff + header->e_phnum * sizeof(Elf64_Phdr) <= file.length &&
                   header->e_shoff + header->e_shnum * sizeof(Elf64_Shdr) <= file.length && header->e_shstrndx < header->e_shnum;
    if (result.valid)
    {
        result.header = header;
        result.program_headers = (const Elf64_Phdr*)(file.pointer + header->e_phoff);
        result.section_headers = (const Elf64_Shdr*)(file.pointer + header->e_shoff);
        const Elf64_Shdr* names = &result.section_headers[header->e_shstrndx];
        result.valid = names->sh_offset + names->sh_size <= file.length;
        result.names = file.pointer + names->sh_offset;
    }
    return result;
}

static ExecutableTestImage executable_test_image(String8 file)
{
    return executable_test_image_of(file, ET_EXEC);
}

// A section by name, or null.
static const Elf64_Shdr* executable_test_section(const ExecutableTestImage* image, const char* name)
{
    const Elf64_Shdr* result = 0;
    for (uint32_t i = 1; image->valid && i < image->header->e_shnum; i += 1)
    {
        result = strcmp(image->names + image->section_headers[i].sh_name, name) == 0 ? &image->section_headers[i] : result;
    }
    return result;
}

// A program header by type, or null.
static const Elf64_Phdr* executable_test_segment(const ExecutableTestImage* image, uint32_t type)
{
    const Elf64_Phdr* result = 0;
    for (uint32_t i = 0; image->valid && i < image->header->e_phnum; i += 1)
    {
        result = result == 0 && image->program_headers[i].p_type == type ? &image->program_headers[i] : result;
    }
    return result;
}

// What fills an executable's GOT, ours or another linker's, read from its sections: its slots, .got's entries and the
// entries of .got.plt that IRELATIVE relocations fill (lld's IFUNCs' slots: its header and its PLT's slots aren't the
// GOT's), and of .got's entries, those that relocations fill, RELATIVE ones (.rela.dyn's and .relr.dyn's), IRELATIVE
// ones and a dynamic symbol's, and those none fills, which hold the link's values. False `valid` for a file that doesn't
// read as an executable of `type`.
typedef struct ExecutableTestGot ExecutableTestGot;
struct ExecutableTestGot
{
    uint64_t slots;
    uint64_t relative;
    uint64_t irelative;
    uint64_t symbolic;
    uint64_t plain;
    bool valid;
};

static ExecutableTestGot executable_test_got(String8 file, uint16_t type)
{
    const ExecutableTestImage image = executable_test_image_of(file, type);
    const Elf64_Shdr* got = executable_test_section(&image, ".got");
    const Elf64_Shdr* got_plt = executable_test_section(&image, ".got.plt");
    ExecutableTestGot result = {
        .valid = image.valid,
    };
    uint64_t relocated = 0;
    uint64_t igot = 0;
    for (uint32_t i = 1; image.valid && i < image.header->e_shnum; i += 1)
    {
        const Elf64_Shdr* section = &image.section_headers[i];
        const bool rela = section->sh_type == SHT_RELA && section->sh_offset + section->sh_size <= file.length;
        const bool relr = section->sh_type == SHT_RELR && section->sh_offset + section->sh_size <= file.length;
        const uint64_t count = rela ? section->sh_size / sizeof(Elf64_Rela) : relr ? section->sh_size / 8 : 0;
        uint64_t where = 0;
        for (uint64_t r = 0; r < count; r += 1)
        {
            Elf64_Rela relocation = { 0 };
            uint64_t word = 0;
            if (rela)
            {
                memcpy(&relocation, file.pointer + section->sh_offset + r * sizeof(Elf64_Rela), sizeof(relocation));
            }
            else
            {
                memcpy(&word, file.pointer + section->sh_offset + 8 * r, sizeof(word));
            }
            // A RELR word: an even one an address, an odd one a bitmap of the 63 words after the last address's.
            const uint32_t type_of = rela ? (uint32_t)ELF64_R_TYPE(relocation.r_info) : R_X86_64_RELATIVE;
            const uint64_t addresses = rela || (word & 1) == 0 ? 1 : 63;
            for (uint64_t a = 0; a < addresses; a += 1)
            {
                const bool present = rela || (word & 1) == 0 || (word >> (a + 1) & 1) != 0;
                const uint64_t address = rela ? relocation.r_offset : (word & 1) == 0 ? word : where + 8 * a;
                const bool in_got = present && got != 0 && address >= got->sh_addr && address < got->sh_addr + got->sh_size;
                const bool in_got_plt = present && got_plt != 0 && address >= got_plt->sh_addr && address < got_plt->sh_addr + got_plt->sh_size;
                result.relative += in_got && type_of == R_X86_64_RELATIVE ? 1 : 0;
                result.irelative += in_got && type_of == R_X86_64_IRELATIVE ? 1 : 0;
                result.symbolic += in_got && type_of != R_X86_64_RELATIVE && type_of != R_X86_64_IRELATIVE ? 1 : 0;
                relocated += in_got ? 1 : 0;
                igot += in_got_plt && type_of == R_X86_64_IRELATIVE ? 1 : 0;
            }
            where = relr && (word & 1) == 0 ? word + 8 : relr ? where + 8 * 63 : where;
        }
    }
    const uint64_t entries = got != 0 ? got->sh_size / 8 : 0;
    result.slots = entries + igot;
    result.plain = entries - relocated;
    return result;
}

// How many of a link's symbols have GOT slots that hold their offsets from the thread pointer (initial-exec accesses'),
// which lld relaxes to local-exec in an executable where the writer doesn't (yet).
static uint64_t executable_test_tls_slots(const Link* link)
{
    uint64_t result = 0;
    for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
    {
        const AtomId atom = link->symbols.atom[symbol];
        const bool tls = atom != 0 ? link_class_is_tls(link->atoms.class[atom]) : (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_TLS;
        result += link->symbols.got_slot[symbol] != 0 && tls ? 1 : 0;
    }
    return result;
}

// The file offset of an address, from the PT_LOAD that has it in its file bytes: UINT64_MAX if none does.
static uint64_t executable_test_file_offset(const ExecutableTestImage* image, uint64_t address)
{
    uint64_t result = UINT64_MAX;
    for (uint32_t i = 0; image->valid && i < image->header->e_phnum; i += 1)
    {
        const Elf64_Phdr* segment = &image->program_headers[i];
        if (segment->p_type == PT_LOAD && address >= segment->p_vaddr && address < segment->p_vaddr + segment->p_filesz)
        {
            result = segment->p_offset + (address - segment->p_vaddr);
        }
    }
    return result;
}

// The headers: PT_PHDR first and loaded; PT_LOADs in lld's order (R, RX, RW, RW), their offsets contiguous and
// 4 KB-congruent with their addresses, each from a page after the last's end; PT_TLS within the RELRO segment;
// PT_GNU_RELRO the RELRO segment to its page's end; PT_GNU_STACK not executable; and the build ID's note.
static void executable_test_headers(ExecutableTestTally* tally, const ExecutableTestImage* image, uint64_t index)
{
    executable_test_expect(tally, image->valid, "the ELF header", index);
    if (image->valid)
    {
        static const uint32_t flags[] = { PF_R, PF_R | PF_X, PF_R | PF_W, PF_R | PF_W };
        const Elf64_Phdr* phdr = executable_test_segment(image, PT_PHDR);
        executable_test_expect(tally, phdr == &image->program_headers[0] && phdr->p_offset == image->header->e_phoff, "PT_PHDR first", index);
        uint32_t loads = 0;
        uint64_t previous_end = 0;
        uint64_t previous_file_end = 0;
        const Elf64_Phdr* relro = 0;
        for (uint32_t i = 0; i < image->header->e_phnum; i += 1)
        {
            const Elf64_Phdr* segment = &image->program_headers[i];
            if (segment->p_type == PT_LOAD)
            {
                // Contiguous but for the first section's alignment: no page of padding.
                executable_test_expect(tally, loads == 0 || (segment->p_offset >= previous_file_end && segment->p_offset - previous_file_end < BUSTER_EXECUTABLE_PAGE_BYTES),
                                       "contiguous file offsets", index);
                executable_test_expect(tally, segment->p_vaddr % BUSTER_EXECUTABLE_PAGE_BYTES == segment->p_offset % BUSTER_EXECUTABLE_PAGE_BYTES &&
                                                  segment->p_align == BUSTER_EXECUTABLE_PAGE_BYTES,
                                       "4 KB-congruent segments", index);
                executable_test_expect(tally, loads == 0 || segment->p_vaddr >= executable_align(previous_end, BUSTER_EXECUTABLE_PAGE_BYTES), "segments on later pages",
                                       index);
                executable_test_expect(tally, loads == 0 ? segment->p_offset == 0 && segment->p_vaddr == BUSTER_EXECUTABLE_BASE : true, "the R segment from the start", index);
                const bool known = loads < BUSTER_ARRAY_LENGTH(flags);
                executable_test_expect(tally, known && segment->p_flags == flags[loads], "lld's order of segments", index);
                relro = known && loads == 2 ? segment : relro;
                previous_end = segment->p_vaddr + segment->p_memsz;
                previous_file_end = segment->p_offset + segment->p_filesz;
                loads += 1;
            }
        }
        const Elf64_Phdr* gnu_relro = executable_test_segment(image, PT_GNU_RELRO);
        executable_test_expect(tally, relro != 0 && gnu_relro != 0 && gnu_relro->p_vaddr == relro->p_vaddr && gnu_relro->p_memsz == relro->p_memsz &&
                                          (relro->p_vaddr + relro->p_memsz) % BUSTER_EXECUTABLE_PAGE_BYTES == 0,
                               "PT_GNU_RELRO to its page's end", index);
        const Elf64_Phdr* tls = executable_test_segment(image, PT_TLS);
        executable_test_expect(tally, tls == 0 || (relro != 0 && tls->p_vaddr >= relro->p_vaddr && tls->p_vaddr % tls->p_align == 0), "PT_TLS in RELRO", index);
        const Elf64_Phdr* stack = executable_test_segment(image, PT_GNU_STACK);
        executable_test_expect(tally, stack != 0 && (stack->p_flags & PF_X) == 0, "a stack that isn't executable", index);
        const Elf64_Phdr* note = executable_test_segment(image, PT_NOTE);
        const Elf64_Shdr* build_id = executable_test_section(image, ".note.gnu.build-id");
        executable_test_expect(tally, note != 0 && build_id != 0 && note->p_offset == build_id->sh_offset && note->p_filesz == BUSTER_EXECUTABLE_NOTE_BYTES,
                               "the build ID's note", index);
    }
}

// What executable_test_expected knows of a field.
typedef enum ExecutableTestField
{
    EXECUTABLE_TEST_FIELD_FITS,
    EXECUTABLE_TEST_FIELD_DOESNT_FIT,
    // Its target is the start or end of an empty init or fini array, which has no section header to say where it is:
    // executable_test_fields checks that its start and end are the same instead.
    EXECUTABLE_TEST_FIELD_UNKNOWN,
} ExecutableTestField;

// A symbol's value as the program sees it, from its atom's address and the file's headers: its address, or a TLS
// symbol's offset from the thread pointer (in the TLS segment, less align_up(p_memsz + f, p_align) - f, f being
// p_vaddr modulo p_align, as glibc places the block and GNU ld and lld compute TPOFF), or 0 for a weak symbol defined
// nowhere, or for one of the linker's the start or end of its array, from the array's
// section header, in an image at `base` (BUSTER_EXECUTABLE_BASE, or a PIE's 0). Returns false for an array that is
// empty, which has no section header to say where it is.
static bool executable_test_symbol_value(const Link* link, const ExecutableTestImage* image, SymbolId symbol, uint64_t* value, uint64_t base)
{
    const AtomId atom = link->symbols.atom[symbol];
    bool result = true;
    if (atom != 0 && link_class_is_tls(link->atoms.class[atom]))
    {
        const Elf64_Phdr* tls = executable_test_segment(image, PT_TLS);
        result = tls != 0;
        const uint64_t remainder = result ? tls->p_vaddr % tls->p_align : 0;
        const uint64_t block = result ? executable_align(tls->p_memsz + remainder, tls->p_align) - remainder : 0;
        *value = result ? base + link->atoms.address[atom] + link->symbols.value[symbol] - tls->p_vaddr - block : 0;
    }
    else if (atom != 0)
    {
        *value = base + link->atoms.address[atom] + link->symbols.value[symbol];
    }
    else if (link->symbols.source[symbol] == BUSTER_LINK_SOURCE_LINKER)
    {
        const bool init = executable_named(link, symbol, "__init_array_start") || executable_named(link, symbol, "__init_array_end");
        const bool end = executable_named(link, symbol, "__init_array_end") || executable_named(link, symbol, "__fini_array_end");
        const Elf64_Shdr* section = executable_test_section(image, init ? ".init_array" : ".fini_array");
        *value = section != 0 ? section->sh_addr + (end ? section->sh_size : 0) : 0;
        result = section != 0;
    }
    else
    {
        *value = 0;
    }
    return result;
}

// Whether a symbol's value is an address in the image, by the test's own rule: one defined in an atom that isn't
// thread-local, or one of the linker's names. A thread-local symbol's offset, an absolute symbol's value, an import and
// a weak symbol defined nowhere (0) aren't.
static bool executable_test_in_image(const Link* link, SymbolId symbol)
{
    const AtomId atom = link->symbols.atom[symbol];
    return (atom != 0 && !link_class_is_tls(link->atoms.class[atom])) || link->symbols.source[symbol] == BUSTER_LINK_SOURCE_LINKER;
}

// Whether a symbol is an IFUNC with a resolver, the code at it.
static bool executable_test_is_ifunc(const Link* link, SymbolId symbol)
{
    return link->symbols.atom[symbol] != 0 && (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_IFUNC;
}

// Whether a fixup reaches its target directly, as GNU ld and lld relax it in an executable of any kind, by the test's
// own rule: a CALL_GOT or LOAD_GOT with the addend -4 to a symbol of the image's (executable_test_in_image) that isn't
// an IFUNC.
static bool executable_test_relaxes(const Link* link, FixupId fixup)
{
    const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
    const SymbolId target = link->fixups.target[fixup];
    const bool loads = kind == BUSTER_LINK_FIXUP_CALL_GOT || kind == BUSTER_LINK_FIXUP_LOAD_GOT;
    return loads && link_fixup_addend(link, fixup) == -4 && executable_test_in_image(link, target) && !executable_test_is_ifunc(link, target);
}

// The 6 bytes that a relaxed load or call (executable_test_relaxes), fixup `fixup` of `holder`, leaves from 2 bytes
// before its field, for its target at address `symbol` and its field at `place`: for a mov, lea's 8d and its ModRM;
// for a call, addr32 call's 67 e8; for a jmp, e9 and its rel32 a byte earlier, measured from its end, and a nop; and the
// distance to the target. The instruction comes from the link's bytes, as their maker left them (call or jmp
// [rip + slot], ff 15 or ff 25, and REX.W mov, 8b) or as a write left them (67 e8, e9 and 8d, which a write takes for
// theirs). False if they are neither, or the distance doesn't fit.
static bool executable_test_relaxed(const Link* link, AtomId holder, FixupId fixup, uint64_t symbol, uint64_t place, uint8_t* expected)
{
    const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
    const uint64_t offset = link->fixups.offset[fixup];
    const uint8_t* bytes = link->atoms.bytes[holder];
    const uint8_t opcode = offset >= 2 ? bytes[offset - 2] : 0;
    const uint8_t modrm = offset >= 2 ? bytes[offset - 1] : 0;
    const bool load = kind == BUSTER_LINK_FIXUP_LOAD_GOT;
    const bool jump = !load && ((opcode == 0xff && modrm == 0x25) || opcode == 0xe9);
    const bool call = !load && ((opcode == 0xff && modrm == 0x15) || (opcode == 0x67 && modrm == 0xe8));
    const int64_t distance = (int64_t)symbol - (int64_t)(place + 4) + (jump ? 1 : 0);
    const int32_t field = (int32_t)distance;
    expected[0] = load ? 0x8d : jump ? 0xe9 : 0x67;
    expected[1] = load ? modrm : 0xe8;
    memcpy(expected + (jump ? 1 : 2), &field, sizeof(field));
    expected[5] = jump ? 0x90 : expected[5];
    return offset >= 2 && distance == field && (load ? opcode == 0x8b || opcode == 0x8d : jump || call);
}

// Whether a load or call through the GOT that doesn't relax keeps its instruction in `bytes`, its field at `offset`: a
// CALL_GOT its call or jmp [rip + slot] (ff 15 or ff 25), a LOAD_GOT its mov (8b). Every other kind keeps its own.
static bool executable_test_unrelaxed(uint8_t kind, const uint8_t* bytes, uint64_t offset)
{
    const bool call = kind == BUSTER_LINK_FIXUP_CALL_GOT;
    const bool load = kind == BUSTER_LINK_FIXUP_LOAD_GOT;
    return (!call && !load) || (offset >= 2 && (call ? bytes[offset - 2] == 0xff && (bytes[offset - 1] == 0x15 || bytes[offset - 1] == 0x25) : bytes[offset - 2] == 0x8b));
}

// The value a fixup's field must hold, from the reference arithmetic of its kind (link_test_load's), with its target's
// value from executable_test_symbol_value. `holder` is the atom whose fixup it is, and width gets the field's width. A
// value that can't fit is a failure: a generated program never has one.
static ExecutableTestField executable_test_expected(const Link* link, const ExecutableTestImage* image, AtomId holder, FixupId fixup, uint64_t* value, uint32_t* width)
{
    const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
    const SymbolId target = link->fixups.target[fixup];
    const Elf64_Shdr* got = executable_test_section(image, ".got");
    uint64_t symbol = 0;
    const bool known = executable_test_symbol_value(link, image, target, &symbol, BUSTER_EXECUTABLE_BASE);
    const uint64_t place = BUSTER_EXECUTABLE_BASE + link->atoms.address[holder] + link->fixups.offset[fixup];
    const uint64_t slot = got != 0 ? got->sh_addr + 8 * (uint64_t)link->symbols.got_slot[target] : 0;
    const uint64_t addend = (uint64_t)link_fixup_addend(link, fixup);
    const uint64_t size = link->symbols.size[target];
    uint32_t range = 0;
    switch (kind)
    {
        case BUSTER_LINK_FIXUP_CALL:
        case BUSTER_LINK_FIXUP_PC32:
            *value = symbol + addend - place;
            break;
        case BUSTER_LINK_FIXUP_CALL_GOT:
        case BUSTER_LINK_FIXUP_LOAD_GOT:
        case BUSTER_LINK_FIXUP_GOTPCREL:
        case BUSTER_LINK_FIXUP_GOTTPOFF:
            *value = slot + addend - place;
            break;
        case BUSTER_LINK_FIXUP_TPOFF32:
        case BUSTER_LINK_FIXUP_ABS32S:
            *value = symbol + addend;
            break;
        case BUSTER_LINK_FIXUP_ABS32:
            *value = symbol + addend;
            range = 1;
            break;
        case BUSTER_LINK_FIXUP_SIZE32:
            *value = size + addend;
            range = 1;
            break;
        case BUSTER_LINK_FIXUP_ABS64:
            *value = symbol + addend;
            range = 2;
            break;
        case BUSTER_LINK_FIXUP_PC64:
            *value = symbol + addend - place;
            range = 2;
            break;
        case BUSTER_LINK_FIXUP_SIZE64:
            *value = size + addend;
            range = 2;
            break;
        default:
            range = 3;
            break;
    }
    *width = range == 2 ? 8 : 4;
    const bool fits = range == 2 || (range == 1 && *value <= UINT32_MAX) || (range == 0 && (int64_t)*value == (int32_t)*value);
    return !known ? EXECUTABLE_TEST_FIELD_UNKNOWN : fits ? EXECUTABLE_TEST_FIELD_FITS : EXECUTABLE_TEST_FIELD_DOESNT_FIT;
}

// Every fixup's field in the file holds the reference's value, and a relaxed load's or call's instruction is rewritten
// (executable_test_relaxed) where another keeps its own; the GOT has a slot for each target of a GOT kind that a fixup
// reads unrelaxed, numbered from 1 in symbol order and holding its value; and an empty init or fini array starts where
// it ends. Returns how many loads and calls relaxed.
static uint64_t executable_test_fields(ExecutableTestTally* tally, const Link* link, String8 file, uint64_t index)
{
    const ExecutableTestImage image = executable_test_image(file);
    executable_test_headers(tally, &image, index);
    uint64_t mismatched = 0;
    uint64_t relaxed_count = 0;
    FixupId first = 0;
    for (AtomId atom = 1; image.valid && atom < link->atoms.count; atom += 1)
    {
        const FixupId begin = link->atoms.fixup_begin[atom];
        const uint64_t atom_offset = executable_test_file_offset(&image, BUSTER_EXECUTABLE_BASE + link->atoms.address[atom]);
        for (FixupId fixup = begin; fixup < begin + link->atoms.fixup_count[atom]; fixup += 1)
        {
            uint64_t expected = 0;
            uint32_t width = 0;
            const ExecutableTestField field = executable_test_expected(link, &image, atom, fixup, &expected, &width);
            const uint64_t offset = atom_offset + link->fixups.offset[fixup];
            uint64_t actual = 0;
            const bool readable = atom_offset != UINT64_MAX && offset + width <= file.length;
            if (readable)
            {
                memcpy(&actual, file.pointer + offset, width);
            }
            const uint64_t mask = width == 8 ? UINT64_MAX : UINT32_MAX;
            const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
            bool same = field == EXECUTABLE_TEST_FIELD_UNKNOWN ||
                        (field == EXECUTABLE_TEST_FIELD_FITS && readable && (actual & mask) == (expected & mask) &&
                         executable_test_unrelaxed(kind, (const uint8_t*)file.pointer + atom_offset, link->fixups.offset[fixup]));
            if (executable_test_relaxes(link, fixup))
            {
                uint8_t relaxed[6];
                uint64_t symbol = 0;
                const bool known = executable_test_symbol_value(link, &image, link->fixups.target[fixup], &symbol, BUSTER_EXECUTABLE_BASE);
                const uint64_t place = BUSTER_EXECUTABLE_BASE + link->atoms.address[atom] + link->fixups.offset[fixup];
                same = known && readable && executable_test_relaxed(link, atom, fixup, symbol, place, relaxed) && memcmp(file.pointer + offset - 2, relaxed, sizeof(relaxed)) == 0;
                relaxed_count += 1;
            }
            first = !same && mismatched == 0 ? fixup : first;
            mismatched += same ? 0 : 1;
        }
    }
    executable_test_expect(tally, mismatched == 0, "every field holds its kind's value, a relaxed load or call's instruction rewritten", first);
    // The GOT: a slot for each target of a GOT kind and no other symbol, numbered from 1 in symbol order, each holding
    // its symbol's value, after slot 0, which holds 0.
    Scratch scratch = scratch_begin(0);
    uint8_t* targets = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, link->symbols.count);
    const Elf64_Shdr* got = executable_test_section(&image, ".got");
    bool slots = targets != 0;
    if (slots)
    {
        memset(targets, 0, link->symbols.count);
        for (FixupId fixup = 1; fixup < link->fixups.count; fixup += 1)
        {
            const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
            const bool reads_slot = kind == BUSTER_LINK_FIXUP_CALL_GOT || kind == BUSTER_LINK_FIXUP_LOAD_GOT || kind == BUSTER_LINK_FIXUP_GOTPCREL || kind == BUSTER_LINK_FIXUP_GOTTPOFF;
            targets[link->fixups.target[fixup]] |= reads_slot && !executable_test_relaxes(link, fixup) ? 1 : 0;
        }
    }
    uint32_t next = 1;
    SymbolId wrong = 0;
    for (SymbolId symbol = 1; slots && image.valid && symbol < link->symbols.count; symbol += 1)
    {
        const uint32_t slot = link->symbols.got_slot[symbol];
        uint64_t expected = 0;
        uint64_t actual = 0;
        const bool known = executable_test_symbol_value(link, &image, symbol, &expected, BUSTER_EXECUTABLE_BASE);
        const uint64_t offset = got != 0 ? got->sh_offset + 8 * (uint64_t)slot : 0;
        const bool inside = got != 0 && 8 * ((uint64_t)slot + 1) <= got->sh_size && offset + 8 <= file.length;
        if (slot != 0 && inside)
        {
            memcpy(&actual, file.pointer + offset, sizeof(actual));
        }
        const bool right = (slot != 0) == (targets[symbol] != 0) && (slot == 0 || (slot == next && inside && (!known || actual == expected)));
        wrong = !right && wrong == 0 ? symbol : wrong;
        next += slot != 0 ? 1 : 0;
    }
    uint64_t zero = UINT64_MAX;
    if (got != 0 && got->sh_offset + 8 <= file.length)
    {
        memcpy(&zero, file.pointer + got->sh_offset, sizeof(zero));
    }
    slots = slots && wrong == 0 && (got == 0 ? next == 1 : got->sh_size == 8 * (uint64_t)next && zero == 0);
    executable_test_expect(tally, slots, "a GOT slot for each target of an unrelaxed GOT kind, in symbol order, holding its value", wrong);
    scratch_end(scratch);
    // An empty init or fini array starts where it ends.
    for (uint32_t list = 0; list < 2; list += 1)
    {
        uint64_t start = UINT64_MAX;
        uint64_t end = UINT64_MAX;
        for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
        {
            start = executable_named(link, symbol, list == 0 ? "__init_array_start" : "__fini_array_start") ? link->symbols.reference[symbol] : start;
            end = executable_named(link, symbol, list == 0 ? "__init_array_end" : "__fini_array_end") ? link->symbols.reference[symbol] : end;
        }
        const bool present = executable_test_section(&image, list == 0 ? ".init_array" : ".fini_array") != 0;
        executable_test_expect(tally, present || start == end, "an empty array's start and end", list);
    }
    return relaxed_count;
}

// ---------------------------------------------------------------------------------------------------------------------
// Unwind tables, read back
//
// A file's .eh_frame_hdr read as an unwinder reads it, against the test's own walk of .eh_frame (executable_test_unwind),
// which test-dynamic's checks of its PIEs share.

// An FDE that the test's walk of .eh_frame finds: its address, and the initial location and range that its pc_begin and
// pc_range give.
typedef struct ExecutableTestFde ExecutableTestFde;
struct ExecutableTestFde
{
    uint64_t address;
    int64_t initial;
    uint64_t range;
};

// Reads the LEB128 at *at, before `end`, past it, as unsigned. False if it runs past `end`.
static bool executable_test_leb128(const uint8_t* bytes, uint64_t end, uint64_t* at, uint64_t* value)
{
    uint64_t result = 0;
    uint32_t shift = 0;
    bool more = true;
    while (more && *at < end)
    {
        result |= shift < 64 ? (uint64_t)(bytes[*at] & 0x7f) << shift : 0;
        more = (bytes[*at] & 0x80) != 0;
        shift += 7;
        *at += 1;
    }
    *value = result;
    return !more;
}

// The FDE pointers' encoding of the CIE at `cie` among .eh_frame's `size` bytes, from its augmentation's 'R', or 0xff
// if it isn't a CIE the test reads: version 1 or 3, and an augmentation of 'z' then 'R', 'P', 'L' and 'S'.
static uint8_t executable_test_cie_encoding(const uint8_t* bytes, uint64_t size, uint64_t cie)
{
    uint32_t length = 0;
    uint32_t id = UINT32_MAX;
    bool valid = cie + 8 <= size;
    if (valid)
    {
        memcpy(&length, bytes + cie, sizeof(length));
        memcpy(&id, bytes + cie + 4, sizeof(id));
    }
    const uint64_t end = cie + 4 + (uint64_t)length;
    valid = valid && id == 0 && length >= 5 && end <= size;
    uint64_t at = cie + 9;
    const uint8_t version = valid ? bytes[cie + 8] : 0;
    const uint64_t augmentation = at;
    while (valid && at < end && bytes[at] != 0)
    {
        at += 1;
    }
    valid = valid && (version == 1 || version == 3) && at < end && bytes[augmentation] == 'z';
    at += 1;
    // The code and data alignments (a signed LEB128 has an unsigned one's bytes), then the return address's register.
    uint64_t value = 0;
    valid = valid && executable_test_leb128(bytes, end, &at, &value) && executable_test_leb128(bytes, end, &at, &value);
    if (valid && version == 1)
    {
        valid = at < end;
        at += 1;
    }
    else if (valid)
    {
        valid = executable_test_leb128(bytes, end, &at, &value);
    }
    uint64_t data_length = 0;
    valid = valid && executable_test_leb128(bytes, end, &at, &data_length) && data_length <= end - at;
    const uint64_t data_end = valid ? at + data_length : 0;
    uint8_t result = 0xff;
    for (uint64_t letter = augmentation + 1; valid && bytes[letter] != 0; letter += 1)
    {
        const uint8_t which = bytes[letter];
        valid = which == 'S' || at < data_end;
        const uint8_t encoding = valid && which != 'S' ? bytes[at] : 0;
        at += which == 'S' ? 0 : 1;
        if (valid && which == 'R')
        {
            result = encoding;
        }
        else if (valid && which == 'P')
        {
            // The personality's pointer, by its encoding's format: absptr, udata2, udata4, udata8, sdata2, sdata4,
            // sdata8, or a LEB128.
            const uint8_t format = encoding & 0x0f;
            const uint64_t pointer = format == 0 || format == 4 || format == 12 ? 8 : format == 2 || format == 10 ? 2 : format == 3 || format == 11 ? 4 : 0;
            valid = pointer != 0 || format == 1 || format == 9;
            if (valid && pointer == 0)
            {
                valid = executable_test_leb128(bytes, data_end, &at, &value);
            }
            at += pointer;
        }
        else
        {
            valid = valid && (which == 'L' || which == 'S');
        }
    }
    return valid && at <= data_end ? result : 0xff;
}

// Walks .eh_frame's records, `size` bytes at `address`, one after another: each FDE into `fdes` (room for `capacity`),
// which must point back to a CIE met before it with pc-relative sdata4 pointers. A zero length is a terminator, which
// must be the last record, as it is where an unwinder that walks the records stops. Returns how many FDEs, or
// UINT64_MAX if a record isn't one the test reads.
static uint64_t executable_test_walk_frames(const uint8_t* bytes, uint64_t size, uint64_t address, uint64_t* cies, ExecutableTestFde* fdes, uint64_t capacity)
{
    uint64_t cie_count = 0;
    uint64_t count = 0;
    uint64_t offset = 0;
    bool valid = true;
    while (valid && offset < size)
    {
        uint32_t length = 0;
        uint32_t id = 0;
        valid = size - offset >= 4;
        if (valid)
        {
            memcpy(&length, bytes + offset, sizeof(length));
        }
        const bool terminator = valid && length == 0;
        valid = valid && (terminator ? size - offset == 4 : length >= 4 && length != UINT32_MAX && (uint64_t)length <= size - offset - 4);
        if (valid && !terminator)
        {
            memcpy(&id, bytes + offset + 4, sizeof(id));
        }
        if (valid && !terminator && id == 0)
        {
            valid = executable_test_cie_encoding(bytes, size, offset) == 0x1b && cie_count < capacity;
            cies[valid ? cie_count : 0] = offset;
            cie_count += valid ? 1 : 0;
        }
        else if (valid && !terminator)
        {
            // Its CIE, by a binary search of the ones met, which the walk met in offset order.
            const uint64_t pointed = offset + 4 >= id ? offset + 4 - id : UINT64_MAX;
            uint64_t low = 0;
            uint64_t high = cie_count;
            while (low < high)
            {
                const uint64_t middle = low + (high - low) / 2;
                low = cies[middle] < pointed ? middle + 1 : low;
                high = cies[middle] < pointed ? high : middle;
            }
            valid = low < cie_count && cies[low] == pointed && length >= 12 && count < capacity;
            int32_t begin = 0;
            uint32_t range = 0;
            if (valid)
            {
                memcpy(&begin, bytes + offset + 8, sizeof(begin));
                memcpy(&range, bytes + offset + 12, sizeof(range));
                fdes[count] = (ExecutableTestFde){
                    .address = address + offset,
                    .initial = (int64_t)(address + offset + 8) + begin,
                    .range = range,
                };
                count += 1;
            }
        }
        offset += terminator ? 4 : 4 + (uint64_t)length;
    }
    return valid ? count : UINT64_MAX;
}

// The table's entry for `pc`, as libgcc's unwinder looks it up: a binary search for the last initial location at or
// before it. Returns its index, or UINT64_MAX if every one is after it.
static uint64_t executable_test_lookup(const uint8_t* table, uint64_t count, uint64_t header, int64_t pc)
{
    uint64_t low = 0;
    uint64_t high = count;
    while (low < high)
    {
        const uint64_t middle = low + (high - low) / 2;
        int32_t initial = 0;
        memcpy(&initial, table + 8 * middle, sizeof(initial));
        const bool at_or_before = (int64_t)header + initial <= pc;
        low = at_or_before ? middle + 1 : low;
        high = at_or_before ? high : middle;
    }
    return low == 0 ? UINT64_MAX : low - 1;
}

// The walked FDE at `address`, by a binary search of `fdes` (in address order), or UINT64_MAX.
static uint64_t executable_test_fde_at(const ExecutableTestFde* fdes, uint64_t count, uint64_t address)
{
    uint64_t low = 0;
    uint64_t high = count;
    while (low < high)
    {
        const uint64_t middle = low + (high - low) / 2;
        low = fdes[middle].address < address ? middle + 1 : low;
        high = fdes[middle].address < address ? high : middle;
    }
    return low < count && fdes[low].address == address ? low : UINT64_MAX;
}

// The first rule that `file`'s unwind tables break, or 0 if they keep them all. The file is an executable of `link`
// whose image starts at `base` (BUSTER_EXECUTABLE_BASE, or 0 for a PIE); the program's own atoms are its units' below
// `units`; `result` is its write's, or null.
//   - With EH_FRAME atoms of bytes in the link, .eh_frame_hdr and .eh_frame, and PT_GNU_EH_FRAME exactly over the
//     header; without, neither the header nor its segment.
//   - The header's version and encodings, its pointer to .eh_frame, its count of the FDEs that the test's walk of
//     .eh_frame finds, and its size.
//   - Its table sorted by initial location, each entry an FDE's address and that FDE's initial location, each FDE
//     once.
//   - A lookup of each FDE's first and last byte, by libgcc's binary search, finds it; and one of each function of the
//     program's (each TEXT atom with bytes of its units that have .eh_frame atoms) finds an FDE that starts there, whose
//     range is the size of the function that the atom starts with (not its section's symbol, which an imported object's
//     atom starts with too).
//   - The result's count, and whether the initial locations go up in .eh_frame's order, as fdes_in_order says.
static const char* executable_test_unwind(const Link* link, String8 file, uint64_t base, uint32_t units, const ExecutableResult* result)
{
    const Elf64_Ehdr* elf = (const Elf64_Ehdr*)file.pointer;
    bool valid = file.length >= sizeof(Elf64_Ehdr) && elf->e_shoff + (uint64_t)elf->e_shnum * sizeof(Elf64_Shdr) <= file.length &&
                 elf->e_phoff + (uint64_t)elf->e_phnum * sizeof(Elf64_Phdr) <= file.length && elf->e_shstrndx < elf->e_shnum;
    const Elf64_Shdr* sections = valid ? (const Elf64_Shdr*)(file.pointer + elf->e_shoff) : 0;
    const Elf64_Phdr* segments = valid ? (const Elf64_Phdr*)(file.pointer + elf->e_phoff) : 0;
    valid = valid && sections[elf->e_shstrndx].sh_offset + sections[elf->e_shstrndx].sh_size <= file.length;
    const Elf64_Shdr* header = 0;
    const Elf64_Shdr* frames = 0;
    const Elf64_Phdr* segment = 0;
    for (uint32_t i = 1; valid && i < elf->e_shnum; i += 1)
    {
        const char* name = file.pointer + sections[elf->e_shstrndx].sh_offset + sections[i].sh_name;
        const bool inside = sections[i].sh_name < sections[elf->e_shstrndx].sh_size && sections[i].sh_offset + sections[i].sh_size <= file.length;
        header = inside && strcmp(name, ".eh_frame_hdr") == 0 ? &sections[i] : header;
        frames = inside && strcmp(name, ".eh_frame") == 0 ? &sections[i] : frames;
    }
    for (uint32_t i = 0; valid && i < elf->e_phnum; i += 1)
    {
        segment = segment == 0 && segments[i].p_type == PT_GNU_EH_FRAME ? &segments[i] : segment;
    }
    bool unwind = false;
    for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
    {
        unwind = unwind || (link->atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME && link->atoms.size[atom] != 0);
    }
    const char* failed = 0;
    if (!valid)
    {
        failed = "the ELF headers";
    }
    else if (!unwind)
    {
        failed = header != 0 || segment != 0 ? "no index without unwind tables" : 0;
    }
    else if (header == 0 || frames == 0 || segment == 0)
    {
        failed = ".eh_frame_hdr, .eh_frame and PT_GNU_EH_FRAME";
    }
    else if (segment->p_offset != header->sh_offset || segment->p_vaddr != header->sh_addr || segment->p_filesz != header->sh_size ||
             segment->p_memsz != header->sh_size || segment->p_flags != PF_R || segment->p_align != 4)
    {
        failed = "PT_GNU_EH_FRAME over .eh_frame_hdr";
    }
    const uint8_t* index = unwind && failed == 0 ? (const uint8_t*)file.pointer + header->sh_offset : 0;
    uint32_t count = 0;
    if (index != 0)
    {
        int32_t pointer = 0;
        const bool fields = header->sh_size >= 12 && index[0] == 1 && index[1] == 0x1b && index[2] == 0x03 && index[3] == 0x3b;
        if (fields)
        {
            memcpy(&pointer, index + 4, sizeof(pointer));
            memcpy(&count, index + 8, sizeof(count));
        }
        failed = !fields                                                            ? "the header's version and encodings"
                 : (int64_t)header->sh_addr + 4 + pointer != (int64_t)frames->sh_addr ? "the header's pointer to .eh_frame"
                 : header->sh_size != 12 + 8 * (uint64_t)count                      ? "the header's size"
                                                                                    : 0;
    }
    Scratch scratch = scratch_begin(0);
    const uint64_t capacity = index != 0 ? frames->sh_size / 8 + 1 : 0;
    uint64_t* cies = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, capacity);
    ExecutableTestFde* fdes = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, ExecutableTestFde, capacity);
    uint8_t* seen = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, capacity);
    const bool room = cies != 0 && fdes != 0 && seen != 0;
    const uint64_t walked = failed == 0 && index != 0 && room ? executable_test_walk_frames((const uint8_t*)file.pointer + frames->sh_offset, frames->sh_size, frames->sh_addr, cies, fdes, capacity)
                                                               : UINT64_MAX;
    if (failed == 0 && index != 0)
    {
        failed = !room ? "room for the walk" : walked == UINT64_MAX ? "the records of .eh_frame" : walked != count ? "the header's count of .eh_frame's FDEs" : 0;
    }
    const uint8_t* table = index != 0 ? index + 12 : 0;
    if (failed == 0 && index != 0)
    {
        memset(seen, 0, capacity);
        bool sorted = true;
        bool matched = true;
        bool once = true;
        int32_t last = INT32_MIN;
        for (uint64_t i = 0; i < count; i += 1)
        {
            int32_t entry[2];
            memcpy(entry, table + 8 * i, sizeof(entry));
            sorted = sorted && entry[0] >= last;
            last = entry[0];
            const uint64_t fde = executable_test_fde_at(fdes, walked, (uint64_t)((int64_t)header->sh_addr + entry[1]));
            matched = matched && fde != UINT64_MAX && fdes[fde].initial == (int64_t)header->sh_addr + entry[0];
            once = once && (fde == UINT64_MAX || seen[fde] == 0);
            seen[fde != UINT64_MAX ? fde : 0] = 1;
        }
        failed = !sorted ? "the table sorted by initial location" : !matched ? "each entry an FDE and its initial location" : !once ? "each FDE once" : 0;
    }
    if (failed == 0 && index != 0)
    {
        bool found = true;
        for (uint64_t f = 0; f < walked; f += 1)
        {
            for (uint64_t end = 0; end < 2 && fdes[f].range != 0; end += 1)
            {
                const int64_t pc = fdes[f].initial + (end == 0 ? 0 : (int64_t)fdes[f].range - 1);
                const uint64_t entry = executable_test_lookup(table, count, header->sh_addr, pc);
                int32_t fde = 0;
                if (entry != UINT64_MAX)
                {
                    memcpy(&fde, table + 8 * entry + 4, sizeof(fde));
                }
                found = found && entry != UINT64_MAX && (int64_t)header->sh_addr + fde == (int64_t)fdes[f].address;
            }
        }
        // Each atom's first function's size, from the functions that start one, and the program's units that have
        // .eh_frame atoms, a byte each.
        uint64_t* sizes = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, link->atoms.count);
        for (AtomId atom = 0; sizes != 0 && atom < link->atoms.count; atom += 1)
        {
            sizes[atom] = UINT64_MAX;
        }
        for (SymbolId symbol = link->symbols.count - 1; sizes != 0 && symbol >= 1; symbol -= 1)
        {
            const AtomId atom = link->symbols.atom[symbol];
            const bool function = (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_FUNCTION;
            if (atom != 0 && function && link->symbols.value[symbol] == 0)
            {
                sizes[atom] = link->symbols.size[symbol];
            }
        }
        uint32_t unit_count = 0;
        for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
        {
            const uint32_t unit = link->atoms.unit[atom];
            unit_count = unit < units && unit + 1 > unit_count ? unit + 1 : unit_count;
        }
        uint8_t* framed = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, unit_count);
        bool functions = framed != 0 && sizes != 0;
        for (uint32_t unit = 0; framed != 0 && unit < unit_count; unit += 1)
        {
            framed[unit] = 0;
        }
        for (AtomId atom = 1; framed != 0 && atom < link->atoms.count; atom += 1)
        {
            const uint32_t unit = link->atoms.unit[atom];
            if (link->atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME && unit < unit_count)
            {
                framed[unit] = 1;
            }
        }
        for (AtomId atom = 1; functions && atom < link->atoms.count; atom += 1)
        {
            const uint32_t unit = link->atoms.unit[atom];
            if (link->atoms.class[atom] == BUSTER_LINK_CLASS_TEXT && link->atoms.size[atom] != 0 && unit < unit_count && framed[unit] != 0)
            {
                const int64_t pc = (int64_t)(base + link->atoms.address[atom]);
                const uint64_t entry = executable_test_lookup(table, count, header->sh_addr, pc);
                int32_t fde = 0;
                if (entry != UINT64_MAX)
                {
                    memcpy(&fde, table + 8 * entry + 4, sizeof(fde));
                }
                const uint64_t walked_fde = entry != UINT64_MAX ? executable_test_fde_at(fdes, walked, (uint64_t)((int64_t)header->sh_addr + fde)) : UINT64_MAX;
                functions = functions && walked_fde != UINT64_MAX && fdes[walked_fde].initial == pc && fdes[walked_fde].range == sizes[atom];
            }
        }
        failed = !found ? "a lookup of each FDE's first and last byte finds it" : !functions ? "a lookup of each function finds its FDE, of its size" : 0;
    }
    if (failed == 0 && result != 0)
    {
        bool in_order = true;
        for (uint64_t f = 1; index != 0 && f < walked; f += 1)
        {
            in_order = in_order && fdes[f].initial >= fdes[f - 1].initial;
        }
        failed = result->fdes != (index != 0 ? count : 0) || result->fdes_in_order != in_order ? "the result's count of FDEs and their order" : 0;
    }
    scratch_end(scratch);
    return failed;
}

// ---------------------------------------------------------------------------------------------------------------------
// Build IDs

// Whether the file's build ID is the hash of its chunks' hashes, each chunk hashed with the note's hash zeroed.
static bool executable_test_build_id(Arena* arena, String8 file, const uint64_t* boundaries, uint64_t chunks)
{
    const ExecutableTestImage image = executable_test_image(file);
    const Elf64_Shdr* note = executable_test_section(&image, ".note.gnu.build-id");
    uint8_t* copy = arena_push(arena, file.length, 64);
    uint8_t* hashes = arena_push(arena, BUSTER_EXECUTABLE_BUILD_ID_BYTES * chunks, 64);
    bool result = note != 0 && copy != 0 && hashes != 0 && chunks + 1 <= BUSTER_EXECUTABLE_TEST_MAX_BOUNDARIES && boundaries[chunks] == file.length;
    uint8_t root[BUSTER_EXECUTABLE_BUILD_ID_BYTES];
    if (result)
    {
        const uint64_t id = note->sh_offset + sizeof(Elf64_Nhdr) + 4;
        memcpy(copy, file.pointer, file.length);
        memset(copy + id, 0, BUSTER_EXECUTABLE_BUILD_ID_BYTES);
        for (uint64_t chunk = 0; chunk < chunks; chunk += 1)
        {
            ExecutableHash hash;
            executable_hash_begin(&hash);
            executable_hash_bytes(&hash, copy + boundaries[chunk], boundaries[chunk + 1] - boundaries[chunk]);
            executable_hash_end(&hash, hashes + BUSTER_EXECUTABLE_BUILD_ID_BYTES * chunk);
        }
        ExecutableHash hash;
        executable_hash_begin(&hash);
        executable_hash_bytes(&hash, hashes, BUSTER_EXECUTABLE_BUILD_ID_BYTES * chunks);
        executable_hash_end(&hash, root);
        result = memcmp(file.pointer + id, root, BUSTER_EXECUTABLE_BUILD_ID_BYTES) == 0;
    }
    return result;
}

// The chunk hash of every length from 0 to BUSTER_EXECUTABLE_TEST_HASH_LENGTHS bytes, cut into pieces at random places,
// against the same bytes whole; and runs of zeros of every length up to a page, whose hashes must all differ from the
// next length's.
#define BUSTER_EXECUTABLE_TEST_HASH_LENGTHS 2100
#define BUSTER_EXECUTABLE_TEST_HASH_CUTS 20

static bool executable_test_hash(uint64_t seed)
{
    ExecutableTestTally tally = {
        .check = "chunk hash",
    };
    Scratch scratch = scratch_begin(0);
    uint8_t* bytes = arena_push(scratch.arena, BUSTER_EXECUTABLE_PAGE_BYTES, 64);
    uint64_t state = seed * 0x9E3779B97F4A7C15ull + 7;
    for (uint64_t i = 0; bytes != 0 && i < BUSTER_EXECUTABLE_PAGE_BYTES; i += 1)
    {
        bytes[i] = (uint8_t)executable_test_random(&state);
    }
    for (uint64_t length = 0; bytes != 0 && length <= BUSTER_EXECUTABLE_TEST_HASH_LENGTHS; length += 1)
    {
        uint8_t whole[BUSTER_EXECUTABLE_BUILD_ID_BYTES];
        ExecutableHash hash;
        executable_hash_begin(&hash);
        executable_hash_bytes(&hash, bytes, length);
        executable_hash_end(&hash, whole);
        for (uint32_t cut = 0; cut < BUSTER_EXECUTABLE_TEST_HASH_CUTS; cut += 1)
        {
            executable_hash_begin(&hash);
            uint64_t at = 0;
            while (at < length)
            {
                const uint64_t random = executable_test_random(&state);
                const uint64_t piece = random % 4 == 0 ? (random >> 8) % 700 : (random >> 8) % 70;
                const uint64_t taken = piece < length - at ? piece : length - at;
                executable_hash_bytes(&hash, bytes + at, taken);
                at += taken;
            }
            uint8_t pieces[BUSTER_EXECUTABLE_BUILD_ID_BYTES];
            executable_hash_end(&hash, pieces);
            executable_test_expect(&tally, memcmp(whole, pieces, sizeof(whole)) == 0, "the same hash in pieces", length);
        }
    }
    uint8_t previous[BUSTER_EXECUTABLE_BUILD_ID_BYTES] = { 0 };
    for (uint64_t length = 0; bytes != 0 && length <= BUSTER_EXECUTABLE_PAGE_BYTES; length += 1)
    {
        uint8_t current[BUSTER_EXECUTABLE_BUILD_ID_BYTES];
        ExecutableHash hash;
        executable_hash_begin(&hash);
        executable_hash_bytes(&hash, executable_zeros, length);
        executable_hash_end(&hash, current);
        executable_test_expect(&tally, length == 0 || memcmp(previous, current, sizeof(current)) != 0, "zeros of another length", length);
        memcpy(previous, current, sizeof(current));
    }
    scratch_end(scratch);
    return executable_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Programs

// Whether no boundary splits a fixup's field: each field lies within one chunk.
static bool executable_test_boundaries(const Link* link, String8 file, const uint64_t* boundaries, uint64_t chunks)
{
    const ExecutableTestImage image = executable_test_image(file);
    bool result = image.valid && chunks + 1 <= BUSTER_EXECUTABLE_TEST_MAX_BOUNDARIES && boundaries[0] == 0 && boundaries[chunks] == file.length;
    for (uint64_t chunk = 0; result && chunk < chunks; chunk += 1)
    {
        result = boundaries[chunk] <= boundaries[chunk + 1];
    }
    for (AtomId atom = 1; result && atom < link->atoms.count; atom += 1)
    {
        const FixupId begin = link->atoms.fixup_begin[atom];
        const uint64_t atom_offset = executable_test_file_offset(&image, BUSTER_EXECUTABLE_BASE + link->atoms.address[atom]);
        for (FixupId fixup = begin; result && fixup < begin + link->atoms.fixup_count[atom]; fixup += 1)
        {
            const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
            // A load's or call's through the GOT from its instruction's opcode, which its relaxation may rewrite.
            const bool relaxing = kind == BUSTER_LINK_FIXUP_CALL_GOT || kind == BUSTER_LINK_FIXUP_LOAD_GOT;
            const uint64_t field = atom_offset + link->fixups.offset[fixup] - (relaxing ? 2 : 0);
            const uint64_t field_end = atom_offset + link->fixups.offset[fixup] + link_fixup_kinds[kind].width;
            // The boundaries are sorted: the last one at or before the field must be at or past its end... or before it.
            uint64_t low = 0;
            uint64_t high = chunks + 1;
            while (low < high)
            {
                const uint64_t middle = low + (high - low) / 2;
                if (boundaries[middle] > field)
                {
                    high = middle;
                }
                else
                {
                    low = middle + 1;
                }
            }
            // boundaries[low] is the first past the field's start, which must not be inside the field.
            result = low > chunks || boundaries[low] >= field_end;
        }
    }
    return result;
}

static bool executable_test_programs(const ExecutableTestDirectory* directory, uint64_t seed, uint64_t id_order, uint32_t functions)
{
    ExecutableTestTally tally = {
        .check = "programs",
    };
    // test-link's sets, then one with chunks enough for every lane to write several.
    LinkProgramOptions options[BUSTER_LINK_TEST_PROGRAMS + 1];
    const char* names[BUSTER_LINK_TEST_PROGRAMS + 1];
    link_test_program_options(options, seed, functions);
    memcpy(names, link_test_program_names, sizeof(link_test_program_names));
    options[BUSTER_LINK_TEST_PROGRAMS] = link_program_default_options();
    options[BUSTER_LINK_TEST_PROGRAMS].seed = seed + BUSTER_LINK_TEST_PROGRAMS;
    options[BUSTER_LINK_TEST_PROGRAMS].units = 32;
    options[BUSTER_LINK_TEST_PROGRAMS].functions = BUSTER_EXECUTABLE_TEST_MANY_CHUNKS_FUNCTIONS;
    options[BUSTER_LINK_TEST_PROGRAMS].globals = 1000;
    names[BUSTER_LINK_TEST_PROGRAMS] = "many chunks";
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(options); i += 1)
    {
        LinkProgram program;
        const bool generated = link_program_generate(&program, options[i]);
        executable_test_expect(&tally, generated, names[i], i);
        Scratch scratch = scratch_begin(0);
        uint64_t* boundaries = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, BUSTER_EXECUTABLE_TEST_MAX_BOUNDARIES);
        char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        char name[64];
        snprintf(name, sizeof(name), "program-%u", i);
        const String8 path = executable_test_path(directory, name, path_bytes);
        String8 first = { 0 };
        ExecutableResult first_result = { 0 };
        // A program that calls libc is step 5's: its imports are refused, before the output is opened.
        const bool libc = options[i].libc;
        for (uint32_t l = 0; generated && libc && l < BUSTER_ARRAY_LENGTH(executable_test_lane_counts); l += 1)
        {
            const uint32_t lanes = executable_test_lane_counts[l];
            ExecutableResult result;
            const bool written = executable_test_write(&program.link, program.entry, path, lanes, &result, 0, 0);
            const SymbolId symbol = result.symbol < program.link.symbols.count ? result.symbol : 0;
            const bool refused = !written && result.error == EXECUTABLE_ERROR_UNSUPPORTED && symbol != 0 &&
                                 program.link.symbols.source[symbol] == BUSTER_LINK_SOURCE_RUNTIME_SHARED && access(path.pointer, F_OK) != 0;
            executable_test_expect(&tally, refused, "an import from libc is refused", i * 10 + lanes);
            if (l == 0)
            {
                const NameId name = program.link.symbols.name[symbol];
                printf("  %s: %s, %s\n", names[i], refused ? "refused" : "not refused", name != 0 ? (const char*)program.link.names.bytes[name] : "-");
            }
        }
        for (uint32_t l = 0; generated && !libc && boundaries != 0 && l < BUSTER_ARRAY_LENGTH(executable_test_lane_counts); l += 1)
        {
            const uint32_t lanes = executable_test_lane_counts[l];
            if (l != 0)
            {
                executable_test_expect(&tally, link_test_permute_names(&program.link, id_order + l, scratch.arena), "permuted NameIds", i * 10 + lanes);
            }
            ExecutableResult result;
            executable_test_expect(&tally, executable_test_reset(&program.link, path.pointer), "the last file overwritten", i * 10 + lanes);
            const bool written = executable_test_write(&program.link, program.entry, path, lanes, &result, boundaries, BUSTER_EXECUTABLE_TEST_MAX_BOUNDARIES);
            if (!written)
            {
                executable_test_print_failure(names[i], &result);
            }
            executable_test_expect(&tally, written, "executable_write", i * 10 + lanes);
            const String8 file = written ? executable_test_read(scratch.arena, path.pointer) : (String8){ 0 };
            const bool ran = written && executable_test_runs(path.pointer, program.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES);
            executable_test_expect(&tally, ran, "the program runs and prints its checksum", i * 10 + lanes);
            if (l == 0)
            {
                first = file;
                first_result = result;
                const uint64_t relaxed = executable_test_fields(&tally, &program.link, file, i);
                executable_test_expect(&tally, written && executable_test_build_id(scratch.arena, file, boundaries, result.chunks), "the build ID is the tree of the file's chunks", i);
                executable_test_expect(&tally, written && executable_test_boundaries(&program.link, file, boundaries, result.chunks), "no boundary splits a field", i);
                const char* unwind = written ? executable_test_unwind(&program.link, file, BUSTER_EXECUTABLE_BASE, UINT32_MAX, &result) : "executable_write";
                executable_test_expect(&tally, unwind == 0 && result.fdes != 0 && result.fdes_in_order, unwind != 0 ? unwind : "FDEs in order", i);
                printf("  %s: %u atoms, %u fixups, %llu loads and calls relaxed, %llu GOT slots, %llu FDEs, %llu bytes in %llu chunks: %s\n", names[i],
                       program.link.atoms.count - 1, program.link.fixups.count - 1, (unsigned long long)relaxed, (unsigned long long)result.got_slots,
                       (unsigned long long)result.fdes, (unsigned long long)result.file_size, (unsigned long long)result.chunks, ran ? "ran" : "failed");
            }
            else
            {
                executable_test_expect(&tally, file.pointer != 0 && first.pointer != 0 && file.length == first.length && memcmp(file.pointer, first.pointer, file.length) == 0,
                                       "the same file with every lane count", i * 10 + lanes);
                executable_test_expect(&tally, memcmp(result.build_id, first_result.build_id, BUSTER_EXECUTABLE_BUILD_ID_BYTES) == 0, "the same build ID", i * 10 + lanes);
            }
        }
        // The exact twins write the same file, with 1 and 8 lanes.
        static const uint32_t exact_lanes[] = { 1, 8 };
        for (uint32_t l = 0; generated && !libc && first.pointer != 0 && l < BUSTER_ARRAY_LENGTH(exact_lanes); l += 1)
        {
            ExecutableResult result;
            const bool written = executable_test_reset(&program.link, path.pointer) && executable_test_write_exact(&program.link, program.entry, path, exact_lanes[l], &result);
            const String8 file = written ? executable_test_read(scratch.arena, path.pointer) : (String8){ 0 };
            executable_test_expect(&tally, file.pointer != 0 && file.length == first.length && memcmp(file.pointer, first.pointer, file.length) == 0 &&
                                               result.fdes == first_result.fdes && result.fdes_in_order == first_result.fdes_in_order,
                                   "the exact twins write the same file", i * 10 + exact_lanes[l]);
        }
        // buster_globals' first pointer, 8 bytes off: the program must print something else.
        if (generated && i == 0)
        {
            FixupId broken = 0;
            for (SymbolId symbol = 1; symbol < program.link.symbols.count; symbol += 1)
            {
                broken = executable_named(&program.link, symbol, "buster_globals") ? program.link.atoms.fixup_begin[program.link.symbols.atom[symbol]] : broken;
            }
            program.link.fixups.addend[broken] += 8;
            ExecutableResult result;
            const bool written = executable_test_write(&program.link, program.entry, path, 1, &result, 0, 0);
            executable_test_expect(&tally, broken != 0 && written && !executable_test_runs(path.pointer, program.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES),
                                   "a broken fixup changes the output", broken);
            executable_test_expect(&tally, written && memcmp(result.build_id, first_result.build_id, BUSTER_EXECUTABLE_BUILD_ID_BYTES) != 0, "another build ID", broken);
        }
        if (!directory->keep)
        {
            unlink(path.pointer);
        }
        scratch_end(scratch);
        link_program_release(&program);
    }
    return executable_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Hand-made links
//
// Links of a few atoms built here, for the checks that need fields at chosen places, or programs that do one thing:
// wait for a signal, or fail.

#define BUSTER_EXECUTABLE_TEST_MAX_CODE 16

typedef struct ExecutableTestLink ExecutableTestLink;
struct ExecutableTestLink
{
    Link link;
    Arena tables;
    Arena names;
    Arena bytes;
};

static bool executable_test_link_begin_with(ExecutableTestLink* test, uint64_t bytes, LinkCapacity capacity)
{
    test->tables = arena_reserve(link_capacity_bytes(capacity));
    test->names = arena_reserve(BUSTER_EXECUTABLE_PAGE_BYTES);
    test->bytes = arena_reserve(bytes);
    return link_initialize(&test->link, &test->tables, &test->names, capacity) && test->names.base != 0 && test->bytes.base != 0;
}

static bool executable_test_link_begin(ExecutableTestLink* test, uint64_t bytes)
{
    const LinkCapacity capacity = {
        .names = 16,
        .atoms = 16,
        .symbols = 16,
        .fixups = 64,
        .wide_addends = 4,
    };
    return executable_test_link_begin_with(test, bytes, capacity);
}

static void executable_test_link_end(ExecutableTestLink* test)
{
    arena_release(&test->tables);
    arena_release(&test->names);
    arena_release(&test->bytes);
}

// A symbol of the program named `name`, to be defined later.
static SymbolId executable_test_symbol(Link* link, const char* name, uint8_t flags)
{
    const NameId id = link_add_name(link, (const uint8_t*)name, (uint32_t)strlen(name));
    return id != 0 ? link_add_symbol(link, id, 0, 0, 0, flags, BUSTER_LINK_SOURCE_PROGRAM) : 0;
}

// A run of instructions for executable_test_code: where it starts in the atom, and its instructions, of which those
// with a reference are marked.
typedef struct ExecutableTestCode ExecutableTestCode;
struct ExecutableTestCode
{
    uint64_t offset;
    X86Instruction instructions[BUSTER_EXECUTABLE_TEST_MAX_CODE];
    LinkReference references[BUSTER_EXECUTABLE_TEST_MAX_CODE];
    uint32_t instruction_count;
    uint32_t reference_count;
};

static void executable_test_add(ExecutableTestCode* code, X86Instruction instruction)
{
    code->instructions[code->instruction_count] = instruction;
    code->instruction_count += 1;
}

static void executable_test_refer(ExecutableTestCode* code, X86Instruction instruction, uint8_t kind, SymbolId target, int64_t addend)
{
    instruction.form |= BUSTER_X86_FORM_FIXUP;
    code->references[code->reference_count] = (LinkReference){
        .instruction = code->instruction_count,
        .field = X86_FIXUP_DISPLACEMENT,
        .kind = kind,
        .target = target,
        .addend = addend,
    };
    code->reference_count += 1;
    executable_test_add(code, instruction);
}

// A TEXT atom of `size` bytes (a multiple of 16), int3 but for the runs of code, which don't overlap and are in order,
// defining `symbol` at its start: its id, or 0.
static AtomId executable_test_code(ExecutableTestLink* test, uint64_t size, ExecutableTestCode* runs, uint32_t run_count, SymbolId symbol)
{
    Link* link = &test->link;
    uint8_t* bytes = arena_push(&test->bytes, size, 16);
    AtomId atom = bytes != 0 ? link_add_atom(link, BUSTER_LINK_CLASS_TEXT, BUSTER_EXECUTABLE_FUNCTION_ALIGNMENT_LOG2, size, bytes, 0) : 0;
    X86Instruction instructions[4 * BUSTER_EXECUTABLE_TEST_MAX_CODE];
    LinkReference references[4 * BUSTER_EXECUTABLE_TEST_MAX_CODE];
    uint32_t ends[4 * BUSTER_EXECUTABLE_TEST_MAX_CODE];
    uint32_t instruction_count = 0;
    uint32_t reference_count = 0;
    if (atom != 0)
    {
        memset(bytes, BUSTER_EXECUTABLE_INT3, size);
    }
    for (uint32_t run = 0; atom != 0 && run < run_count; run += 1)
    {
        ExecutableTestCode* code = &runs[run];
        uint8_t encoded[BUSTER_X86_ENCODER_CAPACITY(BUSTER_EXECUTABLE_TEST_MAX_CODE)];
        uint32_t run_ends[BUSTER_X86_ENCODER_FIXUP_CAPACITY(BUSTER_EXECUTABLE_TEST_MAX_CODE)];
        const int64_t length = x86_encode(code->instructions, code->instruction_count, encoded, sizeof(encoded), 0, run_ends);
        atom = length >= 0 && code->offset + (uint64_t)length <= size ? atom : 0;
        if (atom != 0)
        {
            memcpy(bytes + code->offset, encoded, (uint64_t)length);
            for (uint32_t i = 0; i < code->reference_count; i += 1)
            {
                references[reference_count] = code->references[i];
                references[reference_count].instruction += instruction_count;
                ends[reference_count] = run_ends[i] + (uint32_t)code->offset;
                reference_count += 1;
            }
            memcpy(instructions + instruction_count, code->instructions, code->instruction_count * sizeof(X86Instruction));
            instruction_count += code->instruction_count;
        }
    }
    const bool added = atom != 0 && link_add_code_fixups(link, atom, instructions, instruction_count, ends, reference_count, references, reference_count) &&
                       (symbol == 0 || link_define_symbol(link, symbol, atom, 0, size));
    return added ? atom : 0;
}

// A data atom of `class` with `size` bytes, a copy of `bytes` or zero.
static AtomId executable_test_data(ExecutableTestLink* test, uint8_t class, uint8_t alignment_log2, uint64_t size, const void* bytes, SymbolId symbol)
{
    uint8_t* copy = arena_push(&test->bytes, size, 1ull << alignment_log2);
    if (copy != 0)
    {
        if (bytes != 0)
        {
            memcpy(copy, bytes, size);
        }
        else
        {
            memset(copy, 0, size);
        }
    }
    const AtomId atom = copy != 0 ? link_add_atom(&test->link, class, alignment_log2, size, copy, 0) : 0;
    return atom != 0 && (symbol == 0 || link_define_symbol(&test->link, symbol, atom, 0, size)) ? atom : 0;
}

static X86Instruction executable_test_move(uint8_t reg, int32_t immediate)
{
    X86Instruction result = {
        .immediate = immediate,
        .rm = reg,
        .form = X86_FORM_MOV_R32_IMM32,
    };
    return result;
}

static X86Instruction executable_test_bare(uint32_t form)
{
    X86Instruction result = {
        .form = (uint16_t)form,
    };
    return result;
}

// exit_group(status).
static void executable_test_exit(ExecutableTestCode* code, int32_t status)
{
    executable_test_add(code, executable_test_move(X86_REGISTER_RAX, 231));
    executable_test_add(code, executable_test_move(X86_REGISTER_RDI, status));
    executable_test_add(code, executable_test_bare(X86_FORM_SYSCALL));
}

// ---------------------------------------------------------------------------------------------------------------------
// Chunks
//
// Programs whose fields cross the nominal boundaries: a large function whose call's rel32 starts `call_before` bytes
// before the first one (a chunk's bytes into the file), and a large table aligned to a byte whose ABS64 starts
// `pointer_before` bytes before the second, which the call's target writes. With 1 to 3 bytes before, the rel32
// crosses the boundary, and with 1 to 7 the pointer does: the boundary must move to the field's end. With 0 the field
// starts there, and with its width before it ends there: the boundary stays. Then the call is through the GOT, call
// [rip + slot], which relaxes to addr32 call (67 e8) and so rewrites the 2 bytes before its field: with 1 to 5 of its 6
// bytes before the boundary, the boundary must move to the field's end, with 0 or 6 it stays.

#define BUSTER_EXECUTABLE_TEST_LARGE_BYTES (BUSTER_EXECUTABLE_CHUNK_BYTES + 8192)

static const char executable_test_message[] = "across the boundary\n";

typedef struct ExecutableTestStraddle ExecutableTestStraddle;
struct ExecutableTestStraddle
{
    ExecutableTestLink test;
    SymbolId start;
    AtomId code;
    AtomId table;
};

// Builds the program with the call at `call` in the function, through the GOT if `relaxing`, and the pointer at
// `pointer` in the table.
static bool executable_test_straddle(ExecutableTestStraddle* straddle, uint64_t call, uint64_t pointer, bool relaxing)
{
    ExecutableTestLink* test = &straddle->test;
    bool result = executable_test_link_begin(test, 4 * BUSTER_EXECUTABLE_TEST_LARGE_BYTES);
    Link* link = &test->link;
    const uint8_t global = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION;
    straddle->start = result ? executable_test_symbol(link, "_start", global) : 0;
    const SymbolId writer = result ? executable_test_symbol(link, "writer", global) : 0;
    const SymbolId table = result ? executable_test_symbol(link, "table", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT) : 0;
    const SymbolId message = result ? executable_test_symbol(link, "message", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT) : 0;
    result = result && straddle->start != 0 && writer != 0 && table != 0 && message != 0;

    // _start: rsi = the table's pointer, then a jump to the call, which the padding puts where the test wants it.
    ExecutableTestCode runs[2] = { 0 };
    const X86Instruction load = {
        .reg = X86_REGISTER_RSI,
        .mode = BUSTER_X86_MEMORY_RIP,
        .form = X86_FORM_MOV_R64_RM64,
    };
    executable_test_refer(&runs[0], load, BUSTER_LINK_FIXUP_PC32, table, (int64_t)pointer);
    X86Instruction jump = executable_test_bare(X86_FORM_JMP_REL32);
    // mov r64, [rip + disp32] is 7 bytes and the jump 5: it ends at 12.
    jump.displacement = (int32_t)call - 12;
    executable_test_add(&runs[0], jump);
    runs[1].offset = call;
    const X86Instruction through_slot = {
        .mode = BUSTER_X86_MEMORY_RIP,
        .form = X86_FORM_CALL_RM64,
    };
    executable_test_refer(&runs[1], relaxing ? through_slot : executable_test_bare(X86_FORM_CALL_REL32), relaxing ? BUSTER_LINK_FIXUP_CALL_GOT : BUSTER_LINK_FIXUP_CALL, writer,
                          0);
    executable_test_exit(&runs[1], 0);
    result = result && call >= 16 && executable_test_code(test, BUSTER_EXECUTABLE_TEST_LARGE_BYTES, runs, 2, straddle->start) != 0;
    straddle->code = result ? link->atoms.count - 1 : 0;

    // writer: write(1, rsi, the message's length), and return.
    ExecutableTestCode write = { 0 };
    executable_test_add(&write, executable_test_move(X86_REGISTER_RAX, 1));
    executable_test_add(&write, executable_test_move(X86_REGISTER_RDI, 1));
    executable_test_add(&write, executable_test_move(X86_REGISTER_RDX, (int32_t)sizeof(executable_test_message) - 1));
    executable_test_add(&write, executable_test_bare(X86_FORM_SYSCALL));
    executable_test_add(&write, executable_test_bare(X86_FORM_RET));
    result = result && executable_test_code(test, 32, &write, 1, writer) != 0;
    result = result && executable_test_data(test, BUSTER_LINK_CLASS_RODATA, 0, sizeof(executable_test_message) - 1, executable_test_message, message) != 0;
    straddle->table = result ? executable_test_data(test, BUSTER_LINK_CLASS_DATA_RELRO, 0, BUSTER_EXECUTABLE_TEST_LARGE_BYTES, 0, table) : 0;
    result = result && straddle->table != 0 && link_add_fixup(link, straddle->table, (uint32_t)pointer, BUSTER_LINK_FIXUP_ABS64, message, 0) != 0;
    result = result && link_check(link) == LINK_ERROR_NONE;
    return result;
}

static bool executable_test_chunks(const ExecutableTestDirectory* directory)
{
    ExecutableTestTally tally = {
        .check = "chunks",
    };
    Scratch scratch = scratch_begin(0);
    uint64_t* boundaries = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, BUSTER_EXECUTABLE_TEST_MAX_BOUNDARIES);
    char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    const String8 path = executable_test_path(directory, "straddle", path_bytes);
    // Where the function and the table go: their places don't depend on where the call and the pointer are in them.
    ExecutableTestStraddle probe;
    ExecutableResult result;
    bool placed = boundaries != 0 && executable_test_straddle(&probe, 16, 0, false) && executable_test_write(&probe.test.link, probe.start, path, 1, &result, 0, 0);
    const String8 probe_file = placed ? executable_test_read(scratch.arena, path.pointer) : (String8){ 0 };
    const ExecutableTestImage probe_image = executable_test_image(probe_file);
    const uint64_t code_offset = placed ? executable_test_file_offset(&probe_image, BUSTER_EXECUTABLE_BASE + probe.test.link.atoms.address[probe.code]) : 0;
    const uint64_t table_offset = placed ? executable_test_file_offset(&probe_image, BUSTER_EXECUTABLE_BASE + probe.test.link.atoms.address[probe.table]) : 0;
    executable_test_link_end(&probe.test);
    const uint64_t first = BUSTER_EXECUTABLE_CHUNK_BYTES;
    const uint64_t second = 2 * BUSTER_EXECUTABLE_CHUNK_BYTES;
    placed = placed && code_offset < first && code_offset + BUSTER_EXECUTABLE_TEST_LARGE_BYTES > first + 16 && table_offset < second &&
             table_offset + BUSTER_EXECUTABLE_TEST_LARGE_BYTES > second + 16 && table_offset > first;
    executable_test_expect(&tally, placed, "the large atoms cross the nominal boundaries", 0);
    // Each place of the rel32 from 0 to 4 bytes before the first boundary, and of the pointer from 0 to 8 before the
    // second; then of the call through the GOT's 6 bytes from 0 to 6 before the first.
    for (uint32_t before = 0; placed && before <= 8 + 7; before += 1)
    {
        const bool relaxing = before > 8;
        const uint32_t call_before = relaxing ? before - 9 : before < 4 ? before : before % 5;
        const uint32_t pointer_before = relaxing ? 0 : before;
        // The call's rel32 is a byte after its opcode; the call through the GOT's field 2 bytes after its opcode, and
        // `call_before` counts its instruction's bytes before the boundary.
        const uint64_t call = first - call_before - (relaxing ? 0 : 1) - code_offset;
        const uint64_t pointer = second - pointer_before - table_offset;
        ExecutableTestStraddle straddle;
        const bool built = executable_test_straddle(&straddle, call, pointer, relaxing);
        executable_test_expect(&tally, built, "the program is built", before);
        String8 first_file = { 0 };
        for (uint32_t l = 0; built && l < BUSTER_ARRAY_LENGTH(executable_test_lane_counts); l += 1)
        {
            executable_test_expect(&tally, executable_test_reset(&straddle.test.link, path.pointer), "the last file overwritten", before * 10 + l);
            const bool written = executable_test_write(&straddle.test.link, straddle.start, path, executable_test_lane_counts[l], &result, boundaries,
                                                       BUSTER_EXECUTABLE_TEST_MAX_BOUNDARIES);
            const String8 file = written ? executable_test_read(scratch.arena, path.pointer) : (String8){ 0 };
            const bool ran = written && executable_test_runs(path.pointer, (const uint8_t*)executable_test_message, sizeof(executable_test_message) - 1);
            executable_test_expect(&tally, ran, "the program runs through both fields", before * 10 + l);
            executable_test_expect(&tally, written && executable_test_boundaries(&straddle.test.link, file, boundaries, result.chunks), "no boundary splits a field",
                                   before * 10 + l);
            // The cut: at the field's end when the field (or the instruction a relaxation rewrites) crosses the nominal
            // boundary, there otherwise.
            const uint32_t width = relaxing ? 6 : 4;
            const uint64_t first_cut = call_before > 0 && call_before < width ? first - call_before + width : first;
            const uint64_t second_cut = pointer_before > 0 && pointer_before < 8 ? second - pointer_before + 8 : second;
            executable_test_expect(&tally, written && result.chunks >= 3 && boundaries[1] == first_cut && boundaries[2] == second_cut, "the boundaries move past the fields",
                                   before * 10 + l);
            if (l == 0)
            {
                first_file = file;
            }
            executable_test_expect(&tally, file.pointer != 0 && first_file.length == file.length && memcmp(file.pointer, first_file.pointer, file.length) == 0,
                                   "the same file with every lane count", before * 10 + l);
        }
        executable_test_link_end(&straddle.test);
    }
    if (!directory->keep)
    {
        unlink(path.pointer);
    }
    scratch_end(scratch);
    return executable_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Files

// A program that waits for a signal, pause() over and over: something for a process to run while its file is relinked.
static bool executable_test_pause(ExecutableTestLink* test, SymbolId* start)
{
    bool result = executable_test_link_begin(test, BUSTER_EXECUTABLE_PAGE_BYTES);
    *start = result ? executable_test_symbol(&test->link, "_start", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION) : 0;
    ExecutableTestCode code = { 0 };
    executable_test_add(&code, executable_test_move(X86_REGISTER_RAX, 34));
    executable_test_add(&code, executable_test_bare(X86_FORM_SYSCALL));
    X86Instruction again = executable_test_bare(X86_FORM_JMP_REL32);
    // mov eax, imm32 is 5 bytes and syscall 2: back 12 bytes from the jump's end.
    again.displacement = -12;
    executable_test_add(&code, again);
    return result && *start != 0 && executable_test_code(test, 16, &code, 1, *start) != 0 && link_check(&test->link) == LINK_ERROR_NONE;
}

// A program that exits 0, with one more fixup: one of `kind` (ABS32 or PC32) in a table after the code, to _start plus
// `addend`, and with `undefined` a call to "nowhere", defined nowhere.
static bool executable_test_failing(ExecutableTestLink* test, SymbolId* start, uint8_t kind, int64_t addend, bool undefined)
{
    bool result = executable_test_link_begin(test, BUSTER_EXECUTABLE_PAGE_BYTES);
    Link* link = &test->link;
    *start = result ? executable_test_symbol(link, "_start", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION) : 0;
    ExecutableTestCode code = { 0 };
    if (undefined)
    {
        const NameId name = result ? link_add_name(link, (const uint8_t*)"nowhere", 7) : 0;
        const SymbolId nowhere = name != 0 ? link_add_symbol(link, name, 0, 0, 0, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_NONE) : 0;
        result = result && nowhere != 0;
        executable_test_refer(&code, executable_test_bare(X86_FORM_CALL_REL32), BUSTER_LINK_FIXUP_CALL, nowhere, 0);
    }
    executable_test_exit(&code, 0);
    result = result && *start != 0 && executable_test_code(test, 32, &code, 1, *start) != 0;
    const AtomId table = result ? executable_test_data(test, BUSTER_LINK_CLASS_DATA, 2, 4, 0, 0) : 0;
    result = result && table != 0 && link_add_fixup(link, table, 0, kind, *start, addend) != 0;
    return result && link_check(link) == LINK_ERROR_NONE;
}

static bool executable_test_same_file(Arena* arena, const char* path, String8 bytes)
{
    const String8 file = executable_test_read(arena, path);
    return file.pointer != 0 && bytes.pointer != 0 && file.length == bytes.length && memcmp(file.pointer, bytes.pointer, bytes.length) == 0;
}

// Waits up to 5 s until a process runs the file at `path`: until opening it for writing fails with ETXTBSY.
static bool executable_test_running(const char* path)
{
    bool result = false;
    for (uint32_t attempt = 0; !result && attempt < 5000; attempt += 1)
    {
        const int file = open(path, O_RDWR | O_CLOEXEC);
        result = file < 0 && errno == ETXTBSY;
        if (file >= 0)
        {
            close(file);
        }
        if (!result)
        {
            usleep(1000);
        }
    }
    return result;
}

// Whether `directory` holds a file whose name starts with `prefix`: what a link that failed shouldn't leave.
static bool executable_test_leftover(const char* directory, const char* prefix)
{
    DIR* listing = opendir(directory);
    bool result = false;
    for (struct dirent* entry = listing != 0 ? readdir(listing) : 0; entry != 0 && !result; entry = readdir(listing))
    {
        result = strncmp(entry->d_name, prefix, strlen(prefix)) == 0;
    }
    if (listing != 0)
    {
        closedir(listing);
    }
    return result;
}

// Starts a process that reads a FIFO to its first end of file into a new file at `copy`: from `input`, a read end held
// from before, once a writer has written or closed the FIFO (a read with no writer ends at once), or if `input` is -1,
// from the FIFO at `fifo`, which it opens after the copy, so that it reads as soon as a writer opens the FIFO. Returns
// its id, or -1.
static pid_t executable_test_read_fifo(const char* fifo, int input, const char* copy)
{
    fflush(stdout);
    const pid_t child = fork();
    if (child == 0)
    {
        alarm(BUSTER_EXECUTABLE_TEST_RUN_SECONDS);
        const int output = open(copy, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        struct pollfd written = {
            .fd = input,
            .events = POLLIN,
        };
        const bool ready = input >= 0 && fcntl(input, F_SETFL, 0) == 0 && poll(&written, 1, -1) == 1;
        const int reading = input >= 0 ? (ready ? input : -1) : open(fifo, O_RDONLY | O_CLOEXEC);
        bool copied = reading >= 0 && output >= 0;
        uint8_t buffer[65536];
        for (ssize_t length = copied ? read(reading, buffer, sizeof(buffer)) : 0; copied && length > 0; length = read(reading, buffer, sizeof(buffer)))
        {
            copied = write(output, buffer, (size_t)length) == length;
        }
        _exit(copied ? 0 : 1);
    }
    return child;
}

// Whether the process `child` exited with status 0.
static bool executable_test_succeeded(pid_t child)
{
    int status = -1;
    return child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// How many times the file that inotify descriptor `watch` watches, for IN_OPEN and IN_CLOSE_WRITE, was closed after
// being opened for writing. inotify merges an event into the one before it when they are the same, but an open comes
// between any two such closes.
static uint32_t executable_test_closes_written(int watch)
{
    uint32_t result = 0;
    uint8_t buffer[4096];
    for (ssize_t length = read(watch, buffer, sizeof(buffer)); length > 0; length = read(watch, buffer, sizeof(buffer)))
    {
        for (ssize_t offset = 0; offset < length;)
        {
            struct inotify_event event;
            memcpy(&event, buffer + offset, sizeof(event));
            result += (event.mask & IN_CLOSE_WRITE) != 0;
            offset += (ssize_t)(sizeof(event) + event.len);
        }
    }
    return result;
}

// A shared library's first publication is staged beside an absent destination. A failed link or close must remove
// only that temporary, even if another writer created the destination after executable_open saw nothing there.
static void executable_test_first_publication(ExecutableTestTally* tally, const ExecutableTestDirectory* directory, Arena* arena)
{
    char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    const String8 path = executable_test_path(directory, "first-publication", path_bytes);
    static char marker[] = "another writer's completed output\n";
    static const char partial[] = "the failed library's partial output\n";
    const String8 expected = {
        .pointer = marker,
        .length = sizeof(marker) - 1,
    };
    for (uint32_t close_failure = 0; close_failure < 2; close_failure += 1)
    {
        unlink(path.pointer);
        ExecutableFile file = { 0 };
        ExecutableResult result = { 0 };
        const bool opened = executable_open(&file, path, true, &result);
        struct stat status;
        const bool staged = opened && file.write == EXECUTABLE_WRITE_CREATED && file.temporary && lstat(path.pointer, &status) != 0 && errno == ENOENT &&
                            stat(file.written_path, &status) == 0 && S_ISREG(status.st_mode);
        executable_test_expect(tally, staged, "the first publication is not visible before close", close_failure);
        const int concurrent = staged ? open(path.pointer, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644) : -1;
        struct stat concurrent_status;
        const bool created = concurrent >= 0 && write(concurrent, marker, sizeof(marker) - 1) == sizeof(marker) - 1 && fstat(concurrent, &concurrent_status) == 0;
        if (concurrent >= 0)
        {
            close(concurrent);
        }
        const bool filled = opened && write(file.descriptor, partial, sizeof(partial) - 1) == sizeof(partial) - 1;
        // With keep false, the link failed before close. Otherwise close the real descriptor first so that close
        // itself fails with EBADF: neither path may rename the partial output or unlink the concurrent destination.
        const bool invalidated = close_failure == 0 || (opened && close(file.descriptor) == 0);
        const bool kept = opened && executable_close(&file, sizeof(partial) - 1, close_failure != 0, &result);
        const bool failed = close_failure == 0 ? result.error == EXECUTABLE_ERROR_NONE
                                              : result.error == EXECUTABLE_ERROR_FILE && strcmp(result.call, "close") == 0 && result.system_error == EBADF;
        const bool retained = created && stat(path.pointer, &status) == 0 && status.st_dev == concurrent_status.st_dev && status.st_ino == concurrent_status.st_ino &&
                              executable_test_same_file(arena, path.pointer, expected);
        executable_test_expect(tally, opened && filled && invalidated && !kept && failed && retained, "a failed first publication leaves the concurrent output", close_failure);
        executable_test_expect(tally, opened && access(file.written_path, F_OK) != 0 && !executable_test_leftover(directory->path, BUSTER_EXECUTABLE_TEMPORARY_PREFIX),
                               "a failed first publication removes its temporary", close_failure);
        if (opened)
        {
            unlink(file.written_path);
        }
    }
    if (!directory->keep)
    {
        unlink(path.pointer);
    }
}

static bool executable_test_files(const ExecutableTestDirectory* directory, uint64_t seed)
{
    ExecutableTestTally tally = {
        .check = "files",
    };
    Scratch scratch = scratch_begin(0);
    executable_test_first_publication(&tally, directory, scratch.arena);
    const mode_t mask = umask(0);
    umask(mask);
    LinkProgramOptions large_options = link_program_default_options();
    large_options.seed = seed;
    LinkProgramOptions small_options = large_options;
    small_options.functions = 1;
    small_options.globals = 2;
    small_options.thread_locals = 2;
    LinkProgram large;
    LinkProgram small;
    const bool generated = link_program_generate(&large, large_options) && link_program_generate(&small, small_options);
    executable_test_expect(&tally, generated, "the programs", 0);
    char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char other_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    const String8 path = executable_test_path(directory, "file", path_bytes);
    const String8 other = executable_test_path(directory, "other", other_bytes);
    ExecutableResult result;
    struct stat status;
    if (generated)
    {
        // Created, then in place and smaller, then in place with its mode back.
        unlink(path.pointer);
        bool written = executable_test_write(&large.link, large.entry, path, 1, &result, 0, 0);
        executable_test_expect(&tally, written && result.write == EXECUTABLE_WRITE_CREATED, "a new file", 0);
        executable_test_expect(&tally, written && executable_test_runs(path.pointer, large.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES), "the new file runs", 0);
        written = executable_test_write(&small.link, small.entry, path, 1, &result, 0, 0);
        executable_test_expect(&tally, written && result.write == EXECUTABLE_WRITE_IN_PLACE, "in place", 0);
        executable_test_expect(&tally, written && stat(path.pointer, &status) == 0 && (uint64_t)status.st_size == result.file_size, "truncated to its size", 0);
        executable_test_expect(&tally, written && executable_test_runs(path.pointer, small.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES), "the smaller file runs", 0);
        chmod(path.pointer, 0600);
        written = executable_test_write(&large.link, large.entry, path, 1, &result, 0, 0);
        executable_test_expect(&tally, written && result.write == EXECUTABLE_WRITE_IN_PLACE && stat(path.pointer, &status) == 0 &&
                                           (status.st_mode & 07777) == (0777 & ~mask),
                               "in place, with its mode back", 0);
        executable_test_expect(&tally, written && executable_test_runs(path.pointer, large.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES), "the larger file runs", 0);

        // Another link to it: written beside it and renamed over it, and the other link keeps what it had.
        const String8 before = executable_test_read(scratch.arena, path.pointer);
        unlink(other.pointer);
        link(path.pointer, other.pointer);
        written = executable_test_write(&small.link, small.entry, path, 1, &result, 0, 0);
        executable_test_expect(&tally, written && result.write == EXECUTABLE_WRITE_RENAMED, "renamed, with another link", 0);
        executable_test_expect(&tally, written && executable_test_same_file(scratch.arena, other.pointer, before), "the other link keeps its bytes", 0);
        executable_test_expect(&tally, written && stat(path.pointer, &status) == 0 && status.st_nlink == 1 &&
                                           executable_test_runs(path.pointer, small.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES),
                               "the renamed file runs", 0);

        // A symbolic link: replaced by the file, and what it pointed at keeps its bytes.
        unlink(path.pointer);
        symlink(other.pointer, path.pointer);
        written = executable_test_write(&large.link, large.entry, path, 1, &result, 0, 0);
        executable_test_expect(&tally, written && result.write == EXECUTABLE_WRITE_RENAMED && lstat(path.pointer, &status) == 0 && S_ISREG(status.st_mode),
                               "a symbolic link replaced", 0);
        executable_test_expect(&tally, written && executable_test_same_file(scratch.arena, other.pointer, before), "the link's target keeps its bytes", 0);
        executable_test_expect(&tally, written && executable_test_runs(path.pointer, large.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES), "the file runs", 0);

        // A running program: renamed over, and the process runs on.
        ExecutableTestLink pause;
        SymbolId pause_start = 0;
        const bool paused = executable_test_pause(&pause, &pause_start) && executable_test_write(&pause.link, pause_start, path, 1, &result, 0, 0);
        executable_test_expect(&tally, paused, "the pausing program", 0);
        fflush(stdout);
        const pid_t child = paused ? fork() : -1;
        if (child == 0)
        {
            char* arguments[] = { path_bytes, 0 };
            char* environment[] = { 0 };
            alarm(BUSTER_EXECUTABLE_TEST_RUN_SECONDS);
            execve(path_bytes, arguments, environment);
            _exit(127);
        }
        const bool running = child > 0 && executable_test_running(path.pointer);
        executable_test_expect(&tally, running, "the program runs", 0);
        written = running && executable_test_write(&large.link, large.entry, path, 2, &result, 0, 0);
        executable_test_expect(&tally, written && result.write == EXECUTABLE_WRITE_RENAMED, "renamed over a running program", 0);
        executable_test_expect(&tally, written && executable_test_runs(path.pointer, large.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES), "the new file runs", 0);
        executable_test_expect(&tally, running && kill(child, 0) == 0, "the old one runs on", 0);
        if (child > 0)
        {
            kill(child, SIGKILL);
            waitpid(child, 0, 0);
        }
        executable_test_link_end(&pause);

        // A link that fails before opening the output leaves it alone; one whose fixup doesn't fit removes it.
        const String8 kept = executable_test_read(scratch.arena, path.pointer);
        ExecutableTestLink failing;
        SymbolId failing_start = 0;
        bool built = executable_test_failing(&failing, &failing_start, BUSTER_LINK_FIXUP_ABS32, 0, true);
        written = built && executable_test_write(&failing.link, failing_start, path, 1, &result, 0, 0);
        executable_test_expect(&tally, built && !written && result.error == EXECUTABLE_ERROR_UNDEFINED && executable_named(&failing.link, result.symbol, "nowhere"),
                               "an undefined symbol, named", 0);
        executable_test_expect(&tally, built && executable_test_same_file(scratch.arena, path.pointer, kept), "the output left alone", 0);
        executable_test_link_end(&failing);
        const int64_t below_zero = -(int64_t)BUSTER_EXECUTABLE_BASE - 0x10000;
        built = executable_test_failing(&failing, &failing_start, BUSTER_LINK_FIXUP_ABS32, below_zero, false);
        written = built && executable_test_write(&failing.link, failing_start, path, 3, &result, 0, 0);
        executable_test_expect(&tally, built && !written && result.error == EXECUTABLE_ERROR_FIXUP && result.fixup != 0, "an ABS32 below 0", 0);
        executable_test_expect(&tally, built && access(path.pointer, F_OK) != 0, "the output removed", 0);
        executable_test_link_end(&failing);
        // The same fixup, fitting: the program runs.
        built = executable_test_failing(&failing, &failing_start, BUSTER_LINK_FIXUP_ABS32, 0, false);
        written = built && executable_test_write(&failing.link, failing_start, path, 1, &result, 0, 0);
        executable_test_expect(&tally, written && result.write == EXECUTABLE_WRITE_CREATED && executable_test_runs(path.pointer, (const uint8_t*)"", 0), "the fitting one", 0);
        executable_test_link_end(&failing);
        // A PC32 from the table back to _start with INT32_MIN for its addend: S - P is negative, so S + A - P is below
        // int32's range, which only the checked path sees, since the addend's magnitude rules the fast path out.
        built = executable_test_failing(&failing, &failing_start, BUSTER_LINK_FIXUP_PC32, INT32_MIN, false);
        written = built && executable_test_write(&failing.link, failing_start, path, 2, &result, 0, 0);
        executable_test_expect(&tally, built && !written && result.error == EXECUTABLE_ERROR_FIXUP && result.fixup != 0 && access(path.pointer, F_OK) != 0,
                               "a PC32 outside int32, and the output removed", 0);
        executable_test_link_end(&failing);

        // A link that fails after opening a file it writes beside the output leaves the output, its other link and no
        // file beside them; and a file a killed link left under the name without random digits doesn't block the next
        // one.
        written = executable_test_write(&large.link, large.entry, path, 1, &result, 0, 0);
        const String8 before_failure = executable_test_read(scratch.arena, path.pointer);
        unlink(other.pointer);
        link(path.pointer, other.pointer);
        built = executable_test_failing(&failing, &failing_start, BUSTER_LINK_FIXUP_ABS32, below_zero, false);
        written = built && executable_test_write(&failing.link, failing_start, path, 2, &result, 0, 0);
        executable_test_expect(&tally, built && !written && result.error == EXECUTABLE_ERROR_FIXUP && executable_test_same_file(scratch.arena, path.pointer, before_failure) &&
                                           executable_test_same_file(scratch.arena, other.pointer, before_failure) && !executable_test_leftover(directory->path, BUSTER_EXECUTABLE_TEMPORARY_PREFIX),
                               "a failure beside the output leaves it, and nothing beside it", 0);
        executable_test_link_end(&failing);
        char stale[BUSTER_EXECUTABLE_TEST_PATH_BYTES + 32];
        snprintf(stale, sizeof(stale), "%s/" BUSTER_EXECUTABLE_TEMPORARY_PREFIX "%d", directory->path, (int)getpid());
        const int stale_file = open(stale, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
        if (stale_file >= 0)
        {
            close(stale_file);
        }
        written = executable_test_write(&small.link, small.entry, path, 1, &result, 0, 0);
        executable_test_expect(&tally, stale_file >= 0 && written && result.write == EXECUTABLE_WRITE_RENAMED &&
                                           executable_test_runs(path.pointer, small.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES),
                               "a file a killed link left beside the output", 0);
        unlink(stale);
        unlink(other.pointer);

        // A name of NAME_MAX bytes with another link: written beside it under a name of its own, renamed over it, and
        // the other link keeps its bytes. Only where the directory's path leaves room for such a name.
        char longest_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        const int prefix = snprintf(longest_bytes, sizeof(longest_bytes), "%s/", directory->path);
        if (prefix > 0 && (uint64_t)prefix + NAME_MAX < sizeof(longest_bytes))
        {
            memset(longest_bytes + prefix, 'n', NAME_MAX);
            longest_bytes[prefix + NAME_MAX] = 0;
            const String8 longest = {
                .pointer = longest_bytes,
                .length = (uint64_t)prefix + NAME_MAX,
            };
            const bool linked = executable_test_write(&small.link, small.entry, longest, 1, &result, 0, 0) && link(longest_bytes, other.pointer) == 0;
            const String8 kept_bytes = linked ? executable_test_read(scratch.arena, other.pointer) : (String8){ 0 };
            written = linked && executable_test_write(&large.link, large.entry, longest, 2, &result, 0, 0);
            executable_test_expect(&tally, written && result.write == EXECUTABLE_WRITE_RENAMED && executable_test_same_file(scratch.arena, other.pointer, kept_bytes) &&
                                               executable_test_runs(longest_bytes, large.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES),
                                   "a name of NAME_MAX bytes with another link", 0);
            unlink(longest_bytes);
            unlink(other.pointer);
        }
        else
        {
            printf("  %s leaves no room for a name of NAME_MAX bytes: not checked\n", directory->path);
        }

        // Someone else's file, when this process may give it away (as root): written beside it and renamed over it,
        // never changed in place, so the new file is this user's.
        if (chown(path.pointer, 65534, 65534) == 0)
        {
            written = executable_test_write(&large.link, large.entry, path, 1, &result, 0, 0);
            executable_test_expect(&tally, written && result.write == EXECUTABLE_WRITE_RENAMED && stat(path.pointer, &status) == 0 && status.st_uid == geteuid() &&
                                               executable_test_runs(path.pointer, large.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES),
                                   "someone else's file renamed over", 0);
        }

        // A FIFO: opened once, for writing, and written in order, as a regular file would hold it, for a reader that
        // opens it and reads to the first end of file; then a failed link writes nothing, and the FIFO stays.
        char fifo_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        char copy_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        const String8 fifo = executable_test_path(directory, "fifo", fifo_bytes);
        const String8 copy = executable_test_path(directory, "copy", copy_bytes);
        unlink(path.pointer);
        written = executable_test_write(&large.link, large.entry, path, 1, &result, 0, 0);
        const String8 regular = executable_test_read(scratch.arena, path.pointer);
        unlink(fifo.pointer);
        const bool made = mkfifo(fifo.pointer, 0644) == 0;
        for (uint32_t lanes = 1; made && lanes <= 3; lanes += 2)
        {
            const int watch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
            const bool watched = watch >= 0 && inotify_add_watch(watch, fifo.pointer, IN_OPEN | IN_CLOSE_WRITE) >= 0;
            const pid_t reader = executable_test_read_fifo(fifo.pointer, -1, copy.pointer);
            written = reader > 0 && executable_test_write(&large.link, large.entry, fifo, lanes, &result, 0, 0);
            const bool read_all = executable_test_succeeded(reader);
            executable_test_expect(&tally, written && result.write == EXECUTABLE_WRITE_SEQUENTIAL && read_all && executable_test_same_file(scratch.arena, copy.pointer, regular),
                                   "a FIFO, written in order", lanes);
            executable_test_expect(&tally, read_all && watched && executable_test_closes_written(watch) == 1, "a FIFO opened once, for writing", lanes);
            if (watch >= 0)
            {
                close(watch);
            }
        }
        built = made && executable_test_failing(&failing, &failing_start, BUSTER_LINK_FIXUP_ABS32, below_zero, false);
        const pid_t reader = built ? executable_test_read_fifo(fifo.pointer, -1, copy.pointer) : -1;
        written = reader > 0 && executable_test_write(&failing.link, failing_start, fifo, 2, &result, 0, 0);
        const bool read_nothing = executable_test_succeeded(reader) && stat(copy.pointer, &status) == 0 && status.st_size == 0;
        executable_test_expect(&tally, built && !written && result.error == EXECUTABLE_ERROR_FIXUP && read_nothing && lstat(fifo.pointer, &status) == 0 && S_ISFIFO(status.st_mode),
                               "a failed link into a FIFO, which stays", 0);
        if (built)
        {
            executable_test_link_end(&failing);
        }
        // A FIFO that may be written but not read (root may read it anyway), whose read end a reader opened before:
        // written in order, and it stays.
        unlink(fifo.pointer);
        const int held = mkfifo(fifo.pointer, 0600) == 0 ? open(fifo.pointer, O_RDONLY | O_NONBLOCK | O_CLOEXEC) : -1;
        const pid_t holder = held >= 0 && chmod(fifo.pointer, 0222) == 0 ? executable_test_read_fifo(0, held, copy.pointer) : -1;
        if (held >= 0)
        {
            close(held);
        }
        written = holder > 0 && executable_test_write(&large.link, large.entry, fifo, 2, &result, 0, 0);
        const bool held_all = executable_test_succeeded(holder);
        executable_test_expect(&tally, written && result.write == EXECUTABLE_WRITE_SEQUENTIAL && held_all && executable_test_same_file(scratch.arena, copy.pointer, regular) &&
                                           lstat(fifo.pointer, &status) == 0 && S_ISFIFO(status.st_mode),
                               "a FIFO that may be written but not read", 0);
        unlink(fifo.pointer);
        unlink(copy.pointer);

        // A directory, and a path under a regular file: refused, naming the call, and the directory stays.
        char under_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES + 8];
        const String8 under = {
            .pointer = under_bytes,
            .length = (uint64_t)snprintf(under_bytes, sizeof(under_bytes), "%s/under", path.pointer),
        };
        written = executable_test_write(&small.link, small.entry, under, 1, &result, 0, 0);
        executable_test_expect(&tally, !written && result.error == EXECUTABLE_ERROR_FILE && strcmp(result.call, "lstat") == 0 && result.system_error == ENOTDIR,
                               "a path under a regular file", 0);
        unlink(path.pointer);
        const bool directory_made = mkdir(path.pointer, 0755) == 0;
        written = directory_made && executable_test_write(&small.link, small.entry, path, 2, &result, 0, 0);
        executable_test_expect(&tally, directory_made && !written && result.error == EXECUTABLE_ERROR_FILE && strcmp(result.call, "open") == 0 && result.system_error == EISDIR &&
                                           lstat(path.pointer, &status) == 0 && S_ISDIR(status.st_mode) && !executable_test_leftover(directory->path, BUSTER_EXECUTABLE_TEMPORARY_PREFIX),
                               "a directory", 0);
        rmdir(path.pointer);
    }
    if (!directory->keep)
    {
        unlink(path.pointer);
        unlink(other.pointer);
    }
    if (generated)
    {
        link_program_release(&large);
        link_program_release(&small);
    }
    scratch_end(scratch);
    return executable_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Limits
//
// What the representation allows and the writer must refuse or resolve consistently: a weak thread-local symbol
// defined nowhere, TLS blocks and atoms past what 32-bit offsets reach, and lanes short of scratch memory.

// The scratch room a starved lane gets, from 0 up by this step until the link succeeds, and the most it gets.
#define BUSTER_EXECUTABLE_TEST_ROOM_STEP 64
#define BUSTER_EXECUTABLE_TEST_MAX_ROOM (1ull << 20)

// A program that loads the GOT slot of a weak thread-local symbol defined nowhere (GOTTPOFF) and exits 0, with a table
// holding the same symbol's TPOFF32: both must be 0.
static bool executable_test_absent_tls(ExecutableTestLink* test, SymbolId* start)
{
    bool result = executable_test_link_begin(test, BUSTER_EXECUTABLE_PAGE_BYTES);
    Link* link = &test->link;
    *start = result ? executable_test_symbol(link, "_start", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION) : 0;
    const NameId name = result ? link_add_name(link, (const uint8_t*)"absent_tls", 10) : 0;
    const SymbolId absent = name != 0 ? link_add_symbol(link, name, 0, 0, 0, BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_TLS, BUSTER_LINK_SOURCE_NONE) : 0;
    result = result && *start != 0 && absent != 0;
    ExecutableTestCode code = { 0 };
    const X86Instruction load = {
        .reg = X86_REGISTER_RAX,
        .mode = BUSTER_X86_MEMORY_RIP,
        .form = X86_FORM_MOV_R64_RM64,
    };
    executable_test_refer(&code, load, BUSTER_LINK_FIXUP_GOTTPOFF, absent, 0);
    executable_test_exit(&code, 0);
    result = result && executable_test_code(test, 32, &code, 1, *start) != 0;
    const AtomId table = result ? executable_test_data(test, BUSTER_LINK_CLASS_DATA, 2, 4, 0, 0) : 0;
    result = result && table != 0 && link_add_fixup(link, table, 0, BUSTER_LINK_FIXUP_TPOFF32, absent, 0) != 0;
    return result && link_check(link) == LINK_ERROR_NONE;
}

// A program that exits 0, with zero-fill atoms of `class` and the given sizes, aligned to 8, and a table with a TPOFF32
// (for TBSS) or an ABS64 (otherwise) to the first one's start. *first gets that atom.
static bool executable_test_zero_fill(ExecutableTestLink* test, SymbolId* start, uint8_t class, const uint64_t* sizes, uint32_t count, AtomId* first)
{
    bool result = executable_test_link_begin(test, BUSTER_EXECUTABLE_PAGE_BYTES);
    Link* link = &test->link;
    const bool tls = class == BUSTER_LINK_CLASS_TBSS;
    *start = result ? executable_test_symbol(link, "_start", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION) : 0;
    const SymbolId variable = result ? executable_test_symbol(link, "variable", BUSTER_LINK_SYMBOL_GLOBAL | (tls ? BUSTER_LINK_SYMBOL_TLS : BUSTER_LINK_SYMBOL_OBJECT)) : 0;
    result = result && *start != 0 && variable != 0;
    ExecutableTestCode code = { 0 };
    executable_test_exit(&code, 0);
    result = result && executable_test_code(test, 32, &code, 1, *start) != 0;
    *first = 0;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        const AtomId atom = link_add_atom(link, class, 3, sizes[i], 0, 0);
        *first = i == 0 ? atom : *first;
        result = atom != 0;
    }
    result = result && link_define_symbol(link, variable, *first, 0, 8);
    const AtomId table = result ? executable_test_data(test, BUSTER_LINK_CLASS_DATA, 3, 8, 0, 0) : 0;
    result = result && table != 0 && link_add_fixup(link, table, 0, tls ? BUSTER_LINK_FIXUP_TPOFF32 : BUSTER_LINK_FIXUP_ABS64, variable, 0) != 0;
    return result && link_check(link) == LINK_ERROR_NONE;
}

// A program that exits 0, with a table of `atom_class` at `alignment_log2` of `size` bytes (zeros), and `failures` ABS32
// fields that fall below 0, each in a table of its own, the first one's aligned to 4 and the others' to 8, so that
// the layout puts the first after the others. `*table` gets the first table, and `*data` a DATA symbol.
static bool executable_test_refusal(ExecutableTestLink* test, SymbolId* start, SymbolId* data, uint8_t atom_class, uint8_t alignment_log2, uint64_t size,
                                    uint32_t failures, AtomId* table)
{
    bool result = executable_test_link_begin(test, 4 * BUSTER_EXECUTABLE_PAGE_BYTES);
    Link* link = &test->link;
    *start = result ? executable_test_symbol(link, "_start", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION) : 0;
    *data = result ? executable_test_symbol(link, "data", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT) : 0;
    result = result && *start != 0 && *data != 0;
    ExecutableTestCode code = { 0 };
    executable_test_exit(&code, 0);
    result = result && executable_test_code(test, 32, &code, 1, *start) != 0;
    *table = result ? executable_test_data(test, atom_class, alignment_log2, size, 0, *data) : 0;
    result = result && *table != 0;
    for (uint32_t i = 0; result && i < failures; i += 1)
    {
        const AtomId failing = executable_test_data(test, BUSTER_LINK_CLASS_DATA, i == 0 ? 2 : 3, 8, 0, 0);
        result = failing != 0 && link_add_fixup(link, failing, 0, BUSTER_LINK_FIXUP_ABS32, *start, -(int64_t)BUSTER_EXECUTABLE_BASE - 0x10000) != 0;
    }
    return result && link_check(link) == LINK_ERROR_NONE;
}

// The symbols after _start of executable_test_undefined's program: with _start, 16, two for each of 8 lanes.
#define BUSTER_EXECUTABLE_TEST_UNDEFINED_SYMBOLS 15

// A program that exits 0, with symbols 2 to 16 after _start, defined in a table but for the first and the last, which
// are defined nowhere: lane 0's range has the first and the last lane's the last, with 2, 3 and 8 lanes. *first gets
// the first.
static bool executable_test_undefined(ExecutableTestLink* test, SymbolId* start, SymbolId* first)
{
    bool result = executable_test_link_begin(test, BUSTER_EXECUTABLE_PAGE_BYTES);
    Link* link = &test->link;
    *start = result ? executable_test_symbol(link, "_start", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION) : 0;
    result = result && *start != 0;
    ExecutableTestCode code = { 0 };
    executable_test_exit(&code, 0);
    result = result && executable_test_code(test, 32, &code, 1, *start) != 0;
    const AtomId table = result ? executable_test_data(test, BUSTER_LINK_CLASS_DATA, 3, 8, 0, 0) : 0;
    result = result && table != 0;
    *first = 0;
    for (uint32_t i = 0; result && i < BUSTER_EXECUTABLE_TEST_UNDEFINED_SYMBOLS; i += 1)
    {
        char name[16];
        const int length = snprintf(name, sizeof(name), "symbol_%u", i);
        const NameId name_id = link_add_name(link, (const uint8_t*)name, (uint32_t)length);
        const bool defined = i != 0 && i != BUSTER_EXECUTABLE_TEST_UNDEFINED_SYMBOLS - 1;
        const SymbolId symbol = name_id != 0 ? link_add_symbol(link, name_id, defined ? table : 0, 0, defined ? 8 : 0, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT,
                                                               defined ? BUSTER_LINK_SOURCE_PROGRAM : BUSTER_LINK_SOURCE_NONE)
                                             : 0;
        *first = i == 0 ? symbol : *first;
        result = symbol != 0;
    }
    return result && link_check(link) == LINK_ERROR_NONE;
}

// Writes a link that must fail with every lane count, alike: `error` and what it is about, without creating `path`.
static void executable_test_refused(ExecutableTestTally* tally, Link* link, SymbolId entry, String8 path, ExecutableError error, AtomId atom, SymbolId symbol, FixupId fixup,
                                    const char* what)
{
    bool refused = true;
    for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(executable_test_lane_counts); l += 1)
    {
        unlink(path.pointer);
        ExecutableResult result;
        const bool written = executable_test_write(link, entry, path, executable_test_lane_counts[l], &result, 0, 0);
        refused = refused && !written && result.error == error && result.atom == atom && result.symbol == symbol && result.fixup == fixup && access(path.pointer, F_OK) != 0;
    }
    executable_test_expect(tally, refused, what, error);
}

static bool executable_test_limits(const ExecutableTestDirectory* directory, uint64_t seed)
{
    ExecutableTestTally tally = {
        .check = "limits",
    };
    Scratch scratch = scratch_begin(0);
    char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    const String8 path = executable_test_path(directory, "limits", path_bytes);
    ExecutableResult result;

    // A weak thread-local symbol defined nowhere: its TPOFF32 and its GOT slot are both 0, as GNU ld makes them.
    ExecutableTestLink absent;
    SymbolId absent_start = 0;
    bool built = executable_test_absent_tls(&absent, &absent_start);
    unlink(path.pointer);
    bool written = built && executable_test_write(&absent.link, absent_start, path, 1, &result, 0, 0);
    executable_test_expect(&tally, written, "a weak thread-local symbol defined nowhere", 0);
    const String8 file = written ? executable_test_read(scratch.arena, path.pointer) : (String8){ 0 };
    if (written)
    {
        executable_test_fields(&tally, &absent.link, file, 0);
    }
    executable_test_expect(&tally, written && executable_test_runs(path.pointer, (const uint8_t*)"", 0), "its program runs", 0);
    executable_test_link_end(&absent);

    // TLS blocks: exactly 2 GB fits, and more doesn't, from atoms of at most 2 GB each; nor does one atom over 2 GB, of
    // .tbss or of .bss, nor one whose size its alignment would wrap. A refused link leaves no output.
    static const struct
    {
        const char* name;
        uint8_t class;
        uint64_t sizes[2];
        uint32_t count;
        bool fits;
        bool names_atom;
    } cases[] = {
        { "a TLS block of 2 GB", BUSTER_LINK_CLASS_TBSS, { (1ull << 31) - 16, 16 }, 2, true, false },
        { "a TLS block past 2 GB", BUSTER_LINK_CLASS_TBSS, { (1ull << 31) - 8, 16 }, 2, false, false },
        { "a .tbss atom of 3 GB", BUSTER_LINK_CLASS_TBSS, { 3ull << 30, 0 }, 1, false, true },
        { "a .bss atom past 2 GB", BUSTER_LINK_CLASS_BSS, { (1ull << 31) + 8, 0 }, 1, false, true },
        { "a .bss atom whose padding wraps", BUSTER_LINK_CLASS_BSS, { UINT64_MAX - 3, 0 }, 1, false, true },
    };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        ExecutableTestLink zero_fill;
        SymbolId zero_fill_start = 0;
        AtomId first = 0;
        built = executable_test_zero_fill(&zero_fill, &zero_fill_start, cases[c].class, cases[c].sizes, cases[c].count, &first);
        unlink(path.pointer);
        written = built && executable_test_write(&zero_fill.link, zero_fill_start, path, 2, &result, 0, 0);
        const bool expected = cases[c].fits ? written && executable_test_runs(path.pointer, (const uint8_t*)"", 0)
                                            : built && !written && result.error == EXECUTABLE_ERROR_LAYOUT && (!cases[c].names_atom || result.atom == first) &&
                                                  access(path.pointer, F_OK) != 0;
        executable_test_expect(&tally, expected, cases[c].name, c);
        executable_test_link_end(&zero_fill);
    }

    // Refusals reported alike with any number of lanes: the entry point checked before the symbols (a DATA symbol, with
    // a symbol defined nowhere beside it), the first of two symbols defined nowhere in different lanes' ranges, the
    // smallest failing fixup though another comes first in the file, an atom aligned past a page, an init array's atom
    // of 12 bytes, and an atom of each of the C runtime's classes, .init's and .fini's, which step 4's executables don't
    // have.
    ExecutableTestLink refusal;
    SymbolId refusal_start = 0;
    SymbolId data = 0;
    AtomId table = 0;
    built = executable_test_refusal(&refusal, &refusal_start, &data, BUSTER_LINK_CLASS_DATA, 3, 8, 0, &table);
    const NameId nowhere_name = built ? link_add_name(&refusal.link, (const uint8_t*)"nowhere", 7) : 0;
    built = nowhere_name != 0 && link_add_symbol(&refusal.link, nowhere_name, 0, 0, 0, BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_NONE) != 0;
    executable_test_expect(&tally, built, "the entry's program", 0);
    if (built)
    {
        executable_test_refused(&tally, &refusal.link, data, path, EXECUTABLE_ERROR_ENTRY, 0, data, 0, "an entry point outside the code, before a symbol defined nowhere");
    }
    executable_test_link_end(&refusal);
    SymbolId first_undefined = 0;
    built = executable_test_undefined(&refusal, &refusal_start, &first_undefined);
    executable_test_expect(&tally, built, "the undefined symbols' program", 0);
    if (built)
    {
        executable_test_refused(&tally, &refusal.link, refusal_start, path, EXECUTABLE_ERROR_UNDEFINED, 0, first_undefined, 0, "the first of two symbols defined nowhere");
    }
    executable_test_link_end(&refusal);
    built = executable_test_refusal(&refusal, &refusal_start, &data, BUSTER_LINK_CLASS_DATA, 3, 8, 2, &table);
    executable_test_expect(&tally, built, "the failing fixups' program", 0);
    if (built)
    {
        // The two tables' fixups are the last two, and the first is the smaller.
        const FixupId smallest = refusal.link.fixups.count - 2;
        executable_test_refused(&tally, &refusal.link, refusal_start, path, EXECUTABLE_ERROR_FIXUP, 0, 0, smallest, "the smallest failing fixup, though later in the file");
    }
    executable_test_link_end(&refusal);
    built = executable_test_refusal(&refusal, &refusal_start, &data, BUSTER_LINK_CLASS_DATA, BUSTER_EXECUTABLE_PAGE_LOG2 + 1, 8, 0, &table);
    if (built)
    {
        executable_test_refused(&tally, &refusal.link, refusal_start, path, EXECUTABLE_ERROR_LAYOUT, table, 0, 0, "an atom aligned past a page");
    }
    executable_test_link_end(&refusal);
    built = executable_test_refusal(&refusal, &refusal_start, &data, BUSTER_LINK_CLASS_INIT_ARRAY, 3, 12, 0, &table);
    if (built)
    {
        executable_test_refused(&tally, &refusal.link, refusal_start, path, EXECUTABLE_ERROR_LAYOUT, table, 0, 0, "an init array's atom of 12 bytes");
    }
    executable_test_link_end(&refusal);
    static const uint8_t runtime_classes[] = {
        BUSTER_LINK_CLASS_INIT,
        BUSTER_LINK_CLASS_FINI,
    };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(runtime_classes); c += 1)
    {
        built = executable_test_refusal(&refusal, &refusal_start, &data, runtime_classes[c], 2, 8, 0, &table);
        if (built)
        {
            executable_test_refused(&tally, &refusal.link, refusal_start, path, EXECUTABLE_ERROR_LAYOUT, table, 0, 0, "an atom of a C runtime's class");
        }
        executable_test_link_end(&refusal);
    }

    // A lane short of scratch memory, from no room up: every link fails with EXECUTABLE_ERROR_MEMORY and leaves no
    // output, until the lane has room enough and the program runs. Lane 0 of one lane, and the last of 2, 3 and 8.
    LinkProgramOptions options[BUSTER_LINK_TEST_PROGRAMS];
    link_test_program_options(options, seed, 1);
    LinkProgram program;
    const bool generated = link_program_generate(&program, options[1]);
    executable_test_expect(&tally, generated, "the smallest program", 0);
    for (uint32_t l = 0; generated && l < BUSTER_ARRAY_LENGTH(executable_test_lane_counts); l += 1)
    {
        const uint32_t lanes = executable_test_lane_counts[l];
        bool succeeded = false;
        uint64_t wrong = UINT64_MAX;
        for (uint64_t room = 0; !succeeded && room <= BUSTER_EXECUTABLE_TEST_MAX_ROOM; room += BUSTER_EXECUTABLE_TEST_ROOM_STEP)
        {
            unlink(path.pointer);
            succeeded = executable_test_write_starved(&program.link, program.entry, path, lanes, lanes - 1, room, &result);
            const bool clean = succeeded || (result.error == EXECUTABLE_ERROR_MEMORY && access(path.pointer, F_OK) != 0);
            wrong = !clean && wrong == UINT64_MAX ? room : wrong;
        }
        executable_test_expect(&tally, wrong == UINT64_MAX, "a lane short of memory fails the link with no output", wrong);
        executable_test_expect(&tally, succeeded && executable_test_runs(path.pointer, program.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES), "then succeeds with room enough",
                               lanes);
    }
    if (generated)
    {
        link_program_release(&program);
    }
    unlink(path.pointer);
    scratch_end(scratch);
    return executable_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Unwind tables
//
// The scanner and the sort against their twins and references, and links whose unwind tables must be sorted, or are
// refused:
//   - The scanner: .eh_frame atoms built here (ExecutableTestFrames) from every kind of CIE the header indexes and
//     some it doesn't, with and without a terminator, then changed in each way a record can be wrong, or right but not
//     as the vector path reads: the exact walk must count the FDEs that the test knows they have, or refuse the atom,
//     the vector path must count the same or leave the atom to the walk (and take every atom of one CIE and its FDEs),
//     and with table entries, both must give a reference's, or refuse an entry outside int32.
//   - The sort: tables sorted, sorted but for one pair, reversed, random and of equal locations, of every count to 200
//     and some larger: the sorted check must say whether they are sorted, and the sort (always with `exact`) give a
//     stable sort's order.
//   - Links: functions aligned apart so that their FDEs' order isn't their addresses', in .eh_frame atoms of each CIE
//     the header indexes, written by every lane count and by the exact twins into the same file, whose table must be
//     sorted and the program run; an FDE's initial location at INT32_MAX from the header and at one more (refused), and
//     one from a wide addend; and atoms the header can't index, the first of two named, by every lane count, and before
//     a symbol defined nowhere, which only the symbols' phase after the layout refuses: by the count, before it.

// The CIEs the scanner's atoms start from: the ones the header indexes, GCC's ("zR", in versions 1 and 3), with a
// personality, an LSDA and a signal frame, and with a personality's pointer absolute or a LEB128; then ones it doesn't
// index: without 'z', with absolute FDE pointers, with a letter it doesn't know, of version 2, with augmentation data
// past the record, without 'R', and with two, absolute then pc-relative (unwinders don't agree on which one counts).
typedef enum ExecutableTestCie
{
    EXECUTABLE_TEST_CIE_ZR,
    EXECUTABLE_TEST_CIE_ZR_VERSION_3,
    EXECUTABLE_TEST_CIE_ZPLR,
    EXECUTABLE_TEST_CIE_ZRS,
    EXECUTABLE_TEST_CIE_ZPR_ABSOLUTE,
    EXECUTABLE_TEST_CIE_ZPR_LEB128,
    EXECUTABLE_TEST_CIES_INDEXED,
    EXECUTABLE_TEST_CIE_NO_Z = EXECUTABLE_TEST_CIES_INDEXED,
    EXECUTABLE_TEST_CIE_R_ABSOLUTE,
    EXECUTABLE_TEST_CIE_UNKNOWN_LETTER,
    EXECUTABLE_TEST_CIE_VERSION_2,
    EXECUTABLE_TEST_CIE_DATA_PAST_END,
    EXECUTABLE_TEST_CIE_Z_ONLY,
    EXECUTABLE_TEST_CIE_R_TWICE,
    EXECUTABLE_TEST_CIES,
} ExecutableTestCie;

#define BUSTER_EXECUTABLE_TEST_FRAMES_BYTES 8192
#define BUSTER_EXECUTABLE_TEST_FRAMES_FIXUPS 256
#define BUSTER_EXECUTABLE_TEST_FRAMES_SYMBOLS 8

// An .eh_frame atom being built: its bytes and fixups, its FDEs in order (each one's offset and pc_begin's fixup), the
// last CIE's offset and whether its FDEs have an LSDA pointer.
typedef struct ExecutableTestFrames ExecutableTestFrames;
struct ExecutableTestFrames
{
    uint8_t bytes[BUSTER_EXECUTABLE_TEST_FRAMES_BYTES];
    uint32_t size;
    uint32_t offsets[BUSTER_EXECUTABLE_TEST_FRAMES_FIXUPS];
    uint8_t kinds[BUSTER_EXECUTABLE_TEST_FRAMES_FIXUPS];
    SymbolId targets[BUSTER_EXECUTABLE_TEST_FRAMES_FIXUPS];
    int64_t addends[BUSTER_EXECUTABLE_TEST_FRAMES_FIXUPS];
    uint32_t fixup_count;
    uint32_t fdes[BUSTER_EXECUTABLE_TEST_FRAMES_FIXUPS];
    uint32_t fde_fixups[BUSTER_EXECUTABLE_TEST_FRAMES_FIXUPS];
    uint32_t fde_count;
    uint32_t cie;
    bool lsda;
};

static void executable_test_frames_put(ExecutableTestFrames* frames, const void* bytes, uint32_t count)
{
    memcpy(frames->bytes + frames->size, bytes, count);
    frames->size += count;
}

static void executable_test_frames_byte(ExecutableTestFrames* frames, uint8_t byte)
{
    executable_test_frames_put(frames, &byte, 1);
}

static void executable_test_frames_fixup(ExecutableTestFrames* frames, uint8_t kind, SymbolId target, int64_t addend)
{
    const uint32_t width = link_fixup_kinds[kind].width;
    frames->offsets[frames->fixup_count] = frames->size;
    frames->kinds[frames->fixup_count] = kind;
    frames->targets[frames->fixup_count] = target;
    frames->addends[frames->fixup_count] = addend;
    frames->fixup_count += 1;
    memset(frames->bytes + frames->size, 0, width);
    frames->size += width;
}

// Ends the record that starts at `start`: DW_CFA_nops to a multiple of 4 bytes, and its length.
static void executable_test_frames_end(ExecutableTestFrames* frames, uint32_t start)
{
    while ((frames->size - start) % 4 != 0)
    {
        executable_test_frames_byte(frames, 0);
    }
    const uint32_t length = frames->size - start - 4;
    memcpy(frames->bytes + start, &length, sizeof(length));
}

// Appends a CIE of `kind`, whose personality, if it has one, is `personality`.
static void executable_test_frames_cie(ExecutableTestFrames* frames, ExecutableTestCie kind, SymbolId personality)
{
    static const char* const augmentations[EXECUTABLE_TEST_CIES] = {
        [EXECUTABLE_TEST_CIE_ZR] = "zR",
        [EXECUTABLE_TEST_CIE_ZR_VERSION_3] = "zR",
        [EXECUTABLE_TEST_CIE_ZPLR] = "zPLR",
        [EXECUTABLE_TEST_CIE_ZRS] = "zRS",
        [EXECUTABLE_TEST_CIE_ZPR_ABSOLUTE] = "zPR",
        [EXECUTABLE_TEST_CIE_ZPR_LEB128] = "zPR",
        [EXECUTABLE_TEST_CIE_NO_Z] = "",
        [EXECUTABLE_TEST_CIE_R_ABSOLUTE] = "zR",
        [EXECUTABLE_TEST_CIE_UNKNOWN_LETTER] = "zRX",
        [EXECUTABLE_TEST_CIE_VERSION_2] = "zR",
        [EXECUTABLE_TEST_CIE_DATA_PAST_END] = "zR",
        [EXECUTABLE_TEST_CIE_Z_ONLY] = "z",
        [EXECUTABLE_TEST_CIE_R_TWICE] = "zRR",
    };
    const uint32_t start = frames->size;
    const uint32_t zero = 0;
    executable_test_frames_put(frames, &zero, 4);
    executable_test_frames_put(frames, &zero, 4);
    const bool version_3 = kind == EXECUTABLE_TEST_CIE_ZR_VERSION_3;
    executable_test_frames_byte(frames, version_3 ? 3 : kind == EXECUTABLE_TEST_CIE_VERSION_2 ? 2 : 1);
    executable_test_frames_put(frames, augmentations[kind], (uint32_t)strlen(augmentations[kind]) + 1);
    // Code alignment 1, data alignment -8, and the return address's register, 16: in version 3 a LEB128, two bytes.
    executable_test_frames_byte(frames, 0x01);
    executable_test_frames_byte(frames, 0x78);
    if (version_3)
    {
        executable_test_frames_byte(frames, 0x90);
        executable_test_frames_byte(frames, 0x00);
    }
    else
    {
        executable_test_frames_byte(frames, 0x10);
    }
    // The augmentation data's length, then its bytes.
    const uint8_t fde_encoding = kind == EXECUTABLE_TEST_CIE_R_ABSOLUTE ? 0x00 : 0x1b;
    if (kind == EXECUTABLE_TEST_CIE_ZPLR)
    {
        executable_test_frames_byte(frames, 7);
        executable_test_frames_byte(frames, 0x9b);
        executable_test_frames_fixup(frames, BUSTER_LINK_FIXUP_PC32, personality, 0);
        executable_test_frames_byte(frames, 0x1b);
        executable_test_frames_byte(frames, 0x1b);
    }
    else if (kind == EXECUTABLE_TEST_CIE_ZPR_ABSOLUTE)
    {
        executable_test_frames_byte(frames, 10);
        executable_test_frames_byte(frames, 0x00);
        executable_test_frames_fixup(frames, BUSTER_LINK_FIXUP_ABS64, personality, 0);
        executable_test_frames_byte(frames, 0x1b);
    }
    else if (kind == EXECUTABLE_TEST_CIE_ZPR_LEB128)
    {
        executable_test_frames_byte(frames, 4);
        executable_test_frames_byte(frames, 0x01);
        executable_test_frames_byte(frames, 0x80);
        executable_test_frames_byte(frames, 0x01);
        executable_test_frames_byte(frames, 0x1b);
    }
    else if (kind == EXECUTABLE_TEST_CIE_Z_ONLY)
    {
        executable_test_frames_byte(frames, 0);
    }
    else if (kind == EXECUTABLE_TEST_CIE_DATA_PAST_END)
    {
        executable_test_frames_byte(frames, 0x7f);
        executable_test_frames_byte(frames, 0x1b);
    }
    else if (kind == EXECUTABLE_TEST_CIE_R_TWICE)
    {
        executable_test_frames_byte(frames, 2);
        executable_test_frames_byte(frames, 0x00);
        executable_test_frames_byte(frames, 0x1b);
    }
    else if (kind != EXECUTABLE_TEST_CIE_NO_Z)
    {
        executable_test_frames_byte(frames, 1);
        executable_test_frames_byte(frames, fde_encoding);
    }
    // DW_CFA_def_cfa rsp + 8, and DW_CFA_offset rip at CFA - 8.
    static const uint8_t instructions[] = { 0x0c, 0x07, 0x08, 0x90, 0x01 };
    executable_test_frames_put(frames, instructions, sizeof(instructions));
    executable_test_frames_end(frames, start);
    frames->cie = start;
    frames->lsda = kind == EXECUTABLE_TEST_CIE_ZPLR;
}

// Appends an FDE for `target` + `addend` of `range` bytes, pointing back to the last CIE, with `instructions` bytes of
// call frame instructions (DW_CFA_advance_locs), and an LSDA pointer to `lsda` if the CIE says so.
static void executable_test_frames_fde(ExecutableTestFrames* frames, SymbolId target, int64_t addend, uint32_t range, uint32_t instructions, SymbolId lsda)
{
    const uint32_t start = frames->size;
    const uint32_t pointer = start + 4 - frames->cie;
    const uint32_t zero = 0;
    executable_test_frames_put(frames, &zero, 4);
    executable_test_frames_put(frames, &pointer, 4);
    frames->fdes[frames->fde_count] = start;
    frames->fde_fixups[frames->fde_count] = frames->fixup_count;
    frames->fde_count += 1;
    executable_test_frames_fixup(frames, BUSTER_LINK_FIXUP_PC32, target, addend);
    executable_test_frames_put(frames, &range, 4);
    executable_test_frames_byte(frames, frames->lsda ? 4 : 0);
    if (frames->lsda)
    {
        executable_test_frames_fixup(frames, BUSTER_LINK_FIXUP_PC32, lsda, 0);
    }
    for (uint32_t i = 0; i < instructions; i += 1)
    {
        executable_test_frames_byte(frames, (uint8_t)(0x40 | (1 + i % 63)));
    }
    executable_test_frames_end(frames, start);
}

// Adds the atom to the link, with its fixups: its id, or 0.
static AtomId executable_test_frames_atom(Link* link, Arena* arena, const ExecutableTestFrames* frames)
{
    uint8_t* bytes = arena_push(arena, frames->size, 8);
    if (bytes != 0)
    {
        memcpy(bytes, frames->bytes, frames->size);
    }
    AtomId atom = bytes != 0 ? link_add_atom(link, BUSTER_LINK_CLASS_EH_FRAME, 3, frames->size, bytes, 0) : 0;
    for (uint32_t i = 0; atom != 0 && i < frames->fixup_count; i += 1)
    {
        atom = link_add_fixup(link, atom, frames->offsets[i], frames->kinds[i], frames->targets[i], frames->addends[i]) != 0 ? atom : 0;
    }
    return atom;
}

// The ways the scanner's checks change an atom of one CIE and its FDEs (FDE `at` of them), and what the walk must then
// find: an FDE too short, cut after its pc_begin with the next record there, or too long, pointing into its CIE or past
// itself, without its pc_begin's fixup or with an ABS32 there, a record too short after the last FDE, and one CIE more
// than BUSTER_EXECUTABLE_EH_FRAME_CIES (all refused); a terminator at the end (the same FDEs); a terminator before the
// FDE (the FDEs before it); a second CIE that the FDE and the ones after it point to, as many CIEs as the walk keeps, a
// fixup among the FDE's instructions, and a wide addend (the same FDEs, for the walk alone); and initial locations at
// int32's limits from the header, and one past each (refused with entries).
typedef enum ExecutableTestChange
{
    EXECUTABLE_TEST_CHANGE_NONE,
    EXECUTABLE_TEST_CHANGE_SHORT_FDE,
    EXECUTABLE_TEST_CHANGE_TRUNCATED,
    EXECUTABLE_TEST_CHANGE_LONG_FDE,
    EXECUTABLE_TEST_CHANGE_INTO_CIE,
    EXECUTABLE_TEST_CHANGE_AHEAD,
    EXECUTABLE_TEST_CHANGE_NO_FIXUP,
    EXECUTABLE_TEST_CHANGE_ABS32,
    EXECUTABLE_TEST_CHANGE_TRAILING,
    EXECUTABLE_TEST_CHANGE_CIES_PAST_LIMIT,
    EXECUTABLE_TEST_CHANGE_TERMINATOR,
    EXECUTABLE_TEST_CHANGE_MIDDLE_TERMINATOR,
    EXECUTABLE_TEST_CHANGE_SECOND_CIE,
    EXECUTABLE_TEST_CHANGE_CIES_AT_LIMIT,
    EXECUTABLE_TEST_CHANGE_EXTRA_FIXUP,
    EXECUTABLE_TEST_CHANGE_WIDE,
    EXECUTABLE_TEST_CHANGE_ENTRY_MAX,
    EXECUTABLE_TEST_CHANGE_ENTRY_MIN,
    EXECUTABLE_TEST_CHANGE_ENTRY_ABOVE,
    EXECUTABLE_TEST_CHANGE_ENTRY_BELOW,
    EXECUTABLE_TEST_CHANGES,
} ExecutableTestChange;

// The header and the atom's addresses the scanner's checks give, and the symbols' references.
#define BUSTER_EXECUTABLE_TEST_FRAMES_HEADER 0x1000ull
#define BUSTER_EXECUTABLE_TEST_FRAMES_ADDRESS 0x2000ull

// Builds an atom of a CIE of `cie` and `fdes` FDEs, changed by `change` at FDE `at`: into `frames`, with how many FDEs
// the walk must find (UINT32_MAX for none), whether the vector path must take it and whether the entries must be
// refused. FDE j is for symbol 1 + j % BUSTER_EXECUTABLE_TEST_FRAMES_SYMBOLS, whose reference is `references`'s.
static void executable_test_frames_build(ExecutableTestFrames* frames, ExecutableTestCie cie, uint32_t fdes, ExecutableTestChange change, uint32_t at, bool terminator,
                                         const uint32_t* references, uint64_t* state, uint32_t* walked, bool* vector, bool* refused)
{
    memset(frames, 0, sizeof(*frames));
    const SymbolId personality = BUSTER_EXECUTABLE_TEST_FRAMES_SYMBOLS;
    const uint32_t extra_cies = change == EXECUTABLE_TEST_CHANGE_CIES_PAST_LIMIT ? BUSTER_EXECUTABLE_EH_FRAME_CIES
                                : change == EXECUTABLE_TEST_CHANGE_CIES_AT_LIMIT ? BUSTER_EXECUTABLE_EH_FRAME_CIES - 1
                                                                                 : 0;
    for (uint32_t c = 0; c < extra_cies; c += 1)
    {
        executable_test_frames_cie(frames, EXECUTABLE_TEST_CIE_ZR, personality);
    }
    executable_test_frames_cie(frames, cie, personality);
    uint32_t before_terminator = fdes;
    for (uint32_t j = 0; j < fdes; j += 1)
    {
        if (j == at && change == EXECUTABLE_TEST_CHANGE_MIDDLE_TERMINATOR)
        {
            const uint32_t zero = 0;
            executable_test_frames_put(frames, &zero, 4);
            before_terminator = j;
        }
        if (j == at && change == EXECUTABLE_TEST_CHANGE_SECOND_CIE)
        {
            executable_test_frames_cie(frames, EXECUTABLE_TEST_CIE_ZR, personality);
        }
        // The changed FDE's target: symbol 1, whose reference is below the header, for an initial location below
        // int32's, and the last, far above it, for one above or a wide addend that fits.
        const bool low = change == EXECUTABLE_TEST_CHANGE_ENTRY_MIN || change == EXECUTABLE_TEST_CHANGE_ENTRY_BELOW;
        const SymbolId target = j != at ? 1 + j % BUSTER_EXECUTABLE_TEST_FRAMES_SYMBOLS : low ? 1 : BUSTER_EXECUTABLE_TEST_FRAMES_SYMBOLS;
        const int64_t reference = references[target];
        const int64_t header = (int64_t)BUSTER_EXECUTABLE_TEST_FRAMES_HEADER;
        int64_t addend = (int64_t)(executable_test_random(state) % 8192) - 4096;
        addend = j != at                                         ? addend
                 : change == EXECUTABLE_TEST_CHANGE_WIDE        ? (int64_t)INT32_MIN - 1
                 : change == EXECUTABLE_TEST_CHANGE_ENTRY_MAX   ? INT32_MAX - reference + header
                 : change == EXECUTABLE_TEST_CHANGE_ENTRY_MIN   ? INT32_MIN - reference + header
                 : change == EXECUTABLE_TEST_CHANGE_ENTRY_ABOVE ? (int64_t)INT32_MAX + 1 - reference + header
                 : change == EXECUTABLE_TEST_CHANGE_ENTRY_BELOW ? (int64_t)INT32_MIN - 1 - reference + header
                                                                : addend;
        const uint32_t fde = frames->size;
        const uint32_t range = 16 + (uint32_t)(executable_test_random(state) % 4096);
        const uint32_t instructions = (uint32_t)(executable_test_random(state) % 24);
        if (j == at && change == EXECUTABLE_TEST_CHANGE_TRUNCATED)
        {
            // Its length, CIE pointer and pc_begin alone: 12 bytes, of length 8.
            const uint32_t pointer = fde + 4 - frames->cie;
            const uint32_t eight = 8;
            executable_test_frames_put(frames, &eight, 4);
            executable_test_frames_put(frames, &pointer, 4);
            frames->fdes[frames->fde_count] = fde;
            frames->fde_fixups[frames->fde_count] = frames->fixup_count;
            frames->fde_count += 1;
            executable_test_frames_fixup(frames, BUSTER_LINK_FIXUP_PC32, target, addend);
        }
        else
        {
            executable_test_frames_fde(frames, target, addend, range, instructions, personality);
        }
        uint32_t length = 0;
        memcpy(&length, frames->bytes + fde, sizeof(length));
        uint32_t pointer = 0;
        memcpy(&pointer, frames->bytes + fde + 4, sizeof(pointer));
        if (j == at && change == EXECUTABLE_TEST_CHANGE_SHORT_FDE)
        {
            length = 8;
        }
        else if (j == at && change == EXECUTABLE_TEST_CHANGE_TRUNCATED)
        {
            length = 8;
        }
        else if (j == at && change == EXECUTABLE_TEST_CHANGE_LONG_FDE)
        {
            length = BUSTER_EXECUTABLE_TEST_FRAMES_BYTES;
        }
        else if (j == at && change == EXECUTABLE_TEST_CHANGE_INTO_CIE)
        {
            pointer -= 4;
        }
        else if (j == at && change == EXECUTABLE_TEST_CHANGE_AHEAD)
        {
            pointer = fde + 8;
        }
        else if (j == at && change == EXECUTABLE_TEST_CHANGE_NO_FIXUP)
        {
            // pc_begin's fixup, the FDE's first.
            const uint32_t fixup = frames->fde_fixups[frames->fde_count - 1];
            memmove(&frames->offsets[fixup], &frames->offsets[fixup + 1], (frames->fixup_count - fixup - 1) * sizeof(frames->offsets[0]));
            memmove(&frames->kinds[fixup], &frames->kinds[fixup + 1], (frames->fixup_count - fixup - 1) * sizeof(frames->kinds[0]));
            memmove(&frames->targets[fixup], &frames->targets[fixup + 1], (frames->fixup_count - fixup - 1) * sizeof(frames->targets[0]));
            memmove(&frames->addends[fixup], &frames->addends[fixup + 1], (frames->fixup_count - fixup - 1) * sizeof(frames->addends[0]));
            frames->fixup_count -= 1;
        }
        else if (j == at && change == EXECUTABLE_TEST_CHANGE_ABS32)
        {
            frames->kinds[frames->fde_fixups[frames->fde_count - 1]] = BUSTER_LINK_FIXUP_ABS32;
        }
        else if (j == at && change == EXECUTABLE_TEST_CHANGE_EXTRA_FIXUP)
        {
            // An ABS32 over the FDE's range, after pc_begin's fixup.
            const uint32_t fixup = frames->fde_fixups[frames->fde_count - 1] + 1;
            memmove(&frames->offsets[fixup + 1], &frames->offsets[fixup], (frames->fixup_count - fixup) * sizeof(frames->offsets[0]));
            memmove(&frames->kinds[fixup + 1], &frames->kinds[fixup], (frames->fixup_count - fixup) * sizeof(frames->kinds[0]));
            memmove(&frames->targets[fixup + 1], &frames->targets[fixup], (frames->fixup_count - fixup) * sizeof(frames->targets[0]));
            memmove(&frames->addends[fixup + 1], &frames->addends[fixup], (frames->fixup_count - fixup) * sizeof(frames->addends[0]));
            frames->offsets[fixup] = fde + 12;
            frames->kinds[fixup] = BUSTER_LINK_FIXUP_ABS32;
            frames->targets[fixup] = target;
            frames->addends[fixup] = 0;
            frames->fixup_count += 1;
        }
        memcpy(frames->bytes + fde, &length, sizeof(length));
        memcpy(frames->bytes + fde + 4, &pointer, sizeof(pointer));
    }
    if (change == EXECUTABLE_TEST_CHANGE_TRAILING)
    {
        const uint32_t four = 4;
        executable_test_frames_put(frames, &four, 4);
    }
    if (terminator || change == EXECUTABLE_TEST_CHANGE_TERMINATOR)
    {
        const uint32_t zero = 0;
        executable_test_frames_put(frames, &zero, 4);
    }
    const bool indexed = cie < EXECUTABLE_TEST_CIES_INDEXED;
    const bool changed = fdes != 0 && change != EXECUTABLE_TEST_CHANGE_NONE;
    // What breaks the records, and what the vector path doesn't read.
    const bool broken = changed && change <= EXECUTABLE_TEST_CHANGE_CIES_PAST_LIMIT;
    const bool every_time = change == EXECUTABLE_TEST_CHANGE_TRAILING || change == EXECUTABLE_TEST_CHANGE_CIES_PAST_LIMIT;
    const bool other_layout = (changed && change >= EXECUTABLE_TEST_CHANGE_MIDDLE_TERMINATOR && change <= EXECUTABLE_TEST_CHANGE_WIDE) || change == EXECUTABLE_TEST_CHANGE_CIES_AT_LIMIT;
    const bool simple = cie == EXECUTABLE_TEST_CIE_ZR || cie == EXECUTABLE_TEST_CIE_ZR_VERSION_3 || cie == EXECUTABLE_TEST_CIE_ZRS || cie == EXECUTABLE_TEST_CIE_ZPR_LEB128;
    *walked = !indexed || broken || every_time ? UINT32_MAX : before_terminator;
    *vector = *walked != UINT32_MAX && simple && !other_layout;
    *refused = changed && (change == EXECUTABLE_TEST_CHANGE_ENTRY_ABOVE || change == EXECUTABLE_TEST_CHANGE_ENTRY_BELOW);
}

static bool executable_test_frames(uint64_t seed)
{
    ExecutableTestTally tally = {
        .check = "unwind scanner",
    };
    Scratch scratch = scratch_begin(0);
    ExecutableTestFrames* frames = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, ExecutableTestFrames, 1);
    int32_t* walk_entries = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, int32_t, 2 * BUSTER_EXECUTABLE_TEST_FRAMES_FIXUPS);
    int32_t* vector_entries = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, int32_t, 2 * BUSTER_EXECUTABLE_TEST_FRAMES_FIXUPS);
    int32_t* expected = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, int32_t, 2 * BUSTER_EXECUTABLE_TEST_FRAMES_FIXUPS);
    uint64_t state = seed * 0x9E3779B97F4A7C15ull + 11;
    uint32_t references[BUSTER_EXECUTABLE_TEST_FRAMES_SYMBOLS + 1] = { 0 };
    // Atom by atom, each in a link of its own whose arenas a round reuses.
    const LinkCapacity capacity = {
        .names = 16,
        .atoms = 4,
        .symbols = BUSTER_EXECUTABLE_TEST_FRAMES_SYMBOLS + 4,
        .fixups = BUSTER_EXECUTABLE_TEST_FRAMES_FIXUPS + 4,
        .wide_addends = 8,
    };
    Arena tables = arena_reserve(link_capacity_bytes(capacity));
    Arena names = arena_reserve(BUSTER_EXECUTABLE_PAGE_BYTES);
    Arena bytes = arena_reserve(2 * BUSTER_EXECUTABLE_TEST_FRAMES_BYTES);
    bool ready = frames != 0 && walk_entries != 0 && vector_entries != 0 && expected != 0 && tables.base != 0 && names.base != 0 && bytes.base != 0;
    executable_test_expect(&tally, ready, "room for the atoms", 0);
    uint64_t vector_taken = 0;
    uint64_t walk_taken = 0;
    uint64_t round = 0;
    for (uint32_t cie = 0; ready && cie < EXECUTABLE_TEST_CIES; cie += 1)
    {
        for (uint32_t change = 0; change < EXECUTABLE_TEST_CHANGES; change += 1)
        {
            for (uint32_t trial = 0; trial < 12; trial += 1)
            {
                // From none to 40 FDEs, across the vector path's groups of 16, the change at a random one.
                const uint32_t fdes = trial < 3 ? trial : (uint32_t)(executable_test_random(&state) % 41);
                const uint32_t at = fdes != 0 ? (uint32_t)(executable_test_random(&state) % fdes) : 0;
                const bool terminator = (executable_test_random(&state) & 1) != 0;
                // Symbol 1's reference below the header, the last's at least 2^28, and the others' anywhere below 2^30.
                for (uint32_t s = 1; s <= BUSTER_EXECUTABLE_TEST_FRAMES_SYMBOLS; s += 1)
                {
                    const uint64_t random = executable_test_random(&state);
                    references[s] = s == 1 ? (uint32_t)(random % BUSTER_EXECUTABLE_TEST_FRAMES_HEADER)
                                    : s == BUSTER_EXECUTABLE_TEST_FRAMES_SYMBOLS ? (uint32_t)((1u << 28) + random % (1u << 29))
                                                                                 : (uint32_t)(random % (1u << 30));
                }
                uint32_t walked = 0;
                bool vector = false;
                bool refused = false;
                executable_test_frames_build(frames, (ExecutableTestCie)cie, fdes, (ExecutableTestChange)change, at, terminator, references, &state, &walked, &vector, &refused);
                tables.length = 0;
                names.length = 0;
                bytes.length = 0;
                Link link;
                bool built = link_initialize(&link, &tables, &names, capacity);
                for (uint32_t s = 1; built && s <= BUSTER_EXECUTABLE_TEST_FRAMES_SYMBOLS; s += 1)
                {
                    char name[16];
                    snprintf(name, sizeof(name), "function_%u", s);
                    built = executable_test_symbol(&link, name, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION) == s;
                    link.symbols.reference[s] = references[s];
                }
                const AtomId atom = built ? executable_test_frames_atom(&link, &bytes, frames) : 0;
                executable_test_expect(&tally, atom != 0, "the atom", round);
                if (atom != 0)
                {
                    const uint64_t header = BUSTER_EXECUTABLE_TEST_FRAMES_HEADER;
                    const uint64_t address = BUSTER_EXECUTABLE_TEST_FRAMES_ADDRESS;
                    const uint32_t walk = executable_eh_frame_walk(&link, atom, 0, 0, 0);
                    const uint32_t fast = executable_eh_frame_fdes(&link, atom);
                    executable_test_expect(&tally, walk == walked, "the walk's count", round);
                    executable_test_expect(&tally, vector ? fast == walked : fast == UINT32_MAX, vector ? "the vector path's count" : "the vector path leaves it to the walk",
                                           round);
                    bool walked_vector = false;
                    bool walked_exact = false;
                    const bool either = executable_eh_frame(&link, atom, false, &walked_vector) == walked && executable_eh_frame(&link, atom, true, &walked_exact) == walked;
                    executable_test_expect(&tally, either && walked_vector == !vector && walked_exact, "either way's count, and which read it", round);
                    // With entries: the reference's, from the FDEs before any terminator, or refused.
                    for (uint32_t j = 0; walked != UINT32_MAX && j < walked; j += 1)
                    {
                        const uint32_t fixup = frames->fde_fixups[j];
                        expected[2 * j] = (int32_t)((int64_t)references[frames->targets[fixup]] + frames->addends[fixup] - (int64_t)header);
                        expected[2 * j + 1] = (int32_t)(address + frames->fdes[j] - header);
                    }
                    memset(walk_entries, 0x5a, 2 * BUSTER_EXECUTABLE_TEST_FRAMES_FIXUPS * sizeof(int32_t));
                    memset(vector_entries, 0x5a, 2 * BUSTER_EXECUTABLE_TEST_FRAMES_FIXUPS * sizeof(int32_t));
                    const uint32_t walk_with = executable_eh_frame_walk(&link, atom, walk_entries, header, address);
                    const uint32_t entries = walked != UINT32_MAX && !refused ? walked : UINT32_MAX;
                    executable_test_expect(&tally, walk_with == entries && (entries == UINT32_MAX || memcmp(walk_entries, expected, 8 * (uint64_t)entries) == 0),
                                           refused ? "the walk refuses an entry outside int32" : "the walk's entries", round);
                    // The vector path's entries, from the fixups of an atom it read.
                    const bool fits = vector && executable_eh_frame_entries(&link, atom, vector_entries, header, address);
                    executable_test_expect(&tally, !vector || (fits == !refused && (refused || memcmp(vector_entries, expected, 8 * (uint64_t)walked) == 0)),
                                           refused ? "the vector path refuses an entry outside int32" : "the vector path's entries", round);
                    vector_taken += vector ? 1 : 0;
                    walk_taken += !vector && walked != UINT32_MAX ? 1 : 0;
                }
                round += 1;
            }
        }
    }
    printf("  %llu atoms: %llu the vector path takes, %llu only the walk, %llu refused\n", (unsigned long long)round, (unsigned long long)vector_taken,
           (unsigned long long)walk_taken, (unsigned long long)(round - vector_taken - walk_taken));
    arena_release(&tables);
    arena_release(&names);
    arena_release(&bytes);
    scratch_end(scratch);
    return executable_test_report(&tally);
}

// The sort's tables: sorted with equal neighbors, sorted but for one pair, reversed, random, all equal, and sorted from
// INT32_MIN to INT32_MAX; and the counts besides 0 to 200.
#define BUSTER_EXECUTABLE_TEST_SORT_PATTERNS 6
#define BUSTER_EXECUTABLE_TEST_SORT_MOST 5000

static const uint32_t executable_test_sort_counts[] = { 255, 256, 257, 1000, 4999, BUSTER_EXECUTABLE_TEST_SORT_MOST };

static bool executable_test_sort(uint64_t seed)
{
    ExecutableTestTally tally = {
        .check = "unwind sort",
    };
    Scratch scratch = scratch_begin(0);
    uint8_t* bytes = arena_push(scratch.arena, BUSTER_EXECUTABLE_EH_FRAME_HDR_BYTES + 8 * BUSTER_EXECUTABLE_TEST_SORT_MOST, 8);
    uint64_t* input = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, BUSTER_EXECUTABLE_TEST_SORT_MOST);
    uint64_t* reference = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, BUSTER_EXECUTABLE_TEST_SORT_MOST);
    const bool ready = bytes != 0 && input != 0 && reference != 0;
    executable_test_expect(&tally, ready, "room for the tables", 0);
    uint64_t state = seed * 0x9E3779B97F4A7C15ull + 13;
    const uint32_t extra = BUSTER_ARRAY_LENGTH(executable_test_sort_counts);
    for (uint32_t c = 0; ready && c < 201 + extra; c += 1)
    {
        const uint64_t count = c < 201 ? c : executable_test_sort_counts[c - 201];
        for (uint32_t pattern = 0; pattern < BUSTER_EXECUTABLE_TEST_SORT_PATTERNS; pattern += 1)
        {
            // Each entry as the table holds it, its initial location in the low 32 bits and its FDE's (its index, so
            // that a sort that isn't stable shows) in the high.
            int64_t initial = (int64_t)(executable_test_random(&state) % 1000) - 500;
            const uint64_t swapped = count >= 2 ? executable_test_random(&state) % (count - 1) : 0;
            for (uint64_t e = 0; e < count; e += 1)
            {
                const uint64_t random = executable_test_random(&state);
                int64_t value = initial;
                if (pattern == 0)
                {
                    initial += (int64_t)(random % 4);
                    value = initial;
                }
                else if (pattern == 1)
                {
                    initial += 1 + (int64_t)(random % 4);
                    value = initial;
                }
                else if (pattern == 2)
                {
                    value = 1000000 - (int64_t)e * 3;
                }
                else if (pattern == 3)
                {
                    value = (int32_t)(uint32_t)random;
                }
                else if (pattern == 5)
                {
                    value = count > 1 ? INT32_MIN + (int64_t)(((uint64_t)UINT32_MAX * e) / (count - 1)) : 0;
                }
                input[e] = (uint32_t)(int32_t)value | (e << 32);
            }
            if (pattern == 1 && count >= 2)
            {
                const uint64_t held = input[swapped];
                input[swapped] = (input[swapped + 1] & 0xffffffffull) | (held & ~0xffffffffull);
                input[swapped + 1] = (held & 0xffffffffull) | (input[swapped + 1] & ~0xffffffffull);
            }
            // The reference, by a stable insertion sort.
            bool sorted = true;
            for (uint64_t e = 0; e < count; e += 1)
            {
                sorted = sorted && (e == 0 || (int32_t)input[e - 1] <= (int32_t)input[e]);
                uint64_t place = e;
                while (place > 0 && (int32_t)reference[place - 1] > (int32_t)input[e])
                {
                    reference[place] = reference[place - 1];
                    place -= 1;
                }
                reference[place] = input[e];
            }
            for (uint32_t exact = 0; exact < 2; exact += 1)
            {
                memcpy(bytes + BUSTER_EXECUTABLE_EH_FRAME_HDR_BYTES, input, 8 * count);
                bool in_order = !sorted;
                const uint64_t mark = scratch.arena->length;
                const bool done = executable_eh_frame_sort(bytes, count, exact != 0, scratch.arena, &in_order);
                arena_pop_to(scratch.arena, mark);
                const uint64_t index = (count * BUSTER_EXECUTABLE_TEST_SORT_PATTERNS + pattern) * 2 + exact;
                executable_test_expect(&tally, done && in_order == sorted, "whether the table is sorted", index);
                executable_test_expect(&tally, done && memcmp(bytes + BUSTER_EXECUTABLE_EH_FRAME_HDR_BYTES, reference, 8 * count) == 0, "a stable sort's order", index);
            }
        }
    }
    scratch_end(scratch);
    return executable_test_report(&tally);
}

// The unwind links' program: _start, which exits 0, then `functions` functions of 16 bytes, aligned to 16, 64, 32,
// 16, 128 and so on, so that the layout's buckets put them out of their order, and .eh_frame atoms of the CIEs the
// header indexes in turn, each after the functions it has FDEs for (_start's first), the last ending with a
// terminator, so that every lane's range has some. The personality and the LSDA pointers are to `data`.
#define BUSTER_EXECUTABLE_TEST_UNWIND_FUNCTIONS 40
#define BUSTER_EXECUTABLE_TEST_UNWIND_ATOMS 12

static bool executable_test_unwind_program(ExecutableTestLink* test, SymbolId* start, ExecutableTestFrames* frames)
{
    const LinkCapacity capacity = {
        .names = BUSTER_EXECUTABLE_TEST_UNWIND_FUNCTIONS + 8,
        .atoms = BUSTER_EXECUTABLE_TEST_UNWIND_FUNCTIONS + BUSTER_EXECUTABLE_TEST_UNWIND_ATOMS + 8,
        .symbols = BUSTER_EXECUTABLE_TEST_UNWIND_FUNCTIONS + 8,
        .fixups = 4 * BUSTER_EXECUTABLE_TEST_UNWIND_FUNCTIONS + 64,
        .wide_addends = 4,
    };
    bool result = executable_test_link_begin_with(test, 16 * BUSTER_EXECUTABLE_PAGE_BYTES, capacity);
    Link* link = &test->link;
    *start = result ? executable_test_symbol(link, "_start", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION) : 0;
    const SymbolId data = result ? executable_test_symbol(link, "data", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT) : 0;
    result = result && *start != 0 && data != 0 && executable_test_data(test, BUSTER_LINK_CLASS_DATA, 3, 8, 0, data) != 0;
    ExecutableTestCode code = { 0 };
    executable_test_exit(&code, 0);
    result = result && executable_test_code(test, 32, &code, 1, *start) != 0;
    static const uint8_t alignments[] = { 4, 6, 5, 4, 7 };
    uint8_t body[16];
    memset(body, BUSTER_EXECUTABLE_INT3, sizeof(body));
    const uint32_t per_atom = (BUSTER_EXECUTABLE_TEST_UNWIND_FUNCTIONS + BUSTER_EXECUTABLE_TEST_UNWIND_ATOMS) / BUSTER_EXECUTABLE_TEST_UNWIND_ATOMS;
    for (uint32_t a = 0; result && a < BUSTER_EXECUTABLE_TEST_UNWIND_ATOMS; a += 1)
    {
        memset(frames, 0, sizeof(*frames));
        executable_test_frames_cie(frames, (ExecutableTestCie)(a % EXECUTABLE_TEST_CIES_INDEXED), data);
        // Function f's FDE, _start's for f = 0.
        for (uint32_t f = a * per_atom; result && f < (a + 1) * per_atom && f <= BUSTER_EXECUTABLE_TEST_UNWIND_FUNCTIONS; f += 1)
        {
            char name[32];
            snprintf(name, sizeof(name), "function_%u", f);
            const SymbolId function = f == 0 ? *start : executable_test_symbol(link, name, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION);
            result = function != 0 &&
                     (f == 0 || executable_test_data(test, BUSTER_LINK_CLASS_TEXT, alignments[f % BUSTER_ARRAY_LENGTH(alignments)], sizeof(body), body, function) != 0);
            executable_test_frames_fde(frames, function, 0, f == 0 ? 32 : 16, f % 7, data);
        }
        if (a + 1 == BUSTER_EXECUTABLE_TEST_UNWIND_ATOMS)
        {
            const uint32_t zero = 0;
            executable_test_frames_put(frames, &zero, 4);
        }
        result = result && executable_test_frames_atom(link, &test->bytes, frames) != 0;
    }
    return result && link_check(link) == LINK_ERROR_NONE;
}

// A program of _start, which exits 0, and a function after it, with an .eh_frame atom of GCC's CIE and FDEs for both,
// the function's to it plus `addend`; or with `broken`, two such atoms whose function's FDE has no pc_begin's fixup,
// 12 atoms of data between them, so that they lie in different lanes' ranges. *frame_atom gets the (first) .eh_frame
// atom.
static bool executable_test_unwind_small(ExecutableTestLink* test, SymbolId* start, ExecutableTestFrames* frames, int64_t addend, bool broken, AtomId* frame_atom)
{
    const LinkCapacity capacity = {
        .names = 8,
        .atoms = 24,
        .symbols = 8,
        .fixups = 16,
        .wide_addends = 4,
    };
    bool result = executable_test_link_begin_with(test, 4 * BUSTER_EXECUTABLE_PAGE_BYTES, capacity);
    Link* link = &test->link;
    *start = result ? executable_test_symbol(link, "_start", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION) : 0;
    const SymbolId function = result ? executable_test_symbol(link, "function", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION) : 0;
    result = result && *start != 0 && function != 0;
    ExecutableTestCode code = { 0 };
    executable_test_exit(&code, 0);
    uint8_t body[16];
    memset(body, BUSTER_EXECUTABLE_INT3, sizeof(body));
    result = result && executable_test_code(test, 32, &code, 1, *start) != 0 && executable_test_data(test, BUSTER_LINK_CLASS_TEXT, 4, sizeof(body), body, function) != 0;
    *frame_atom = 0;
    for (uint32_t copy = 0; result && copy < (broken ? 2 : 1); copy += 1)
    {
        for (uint32_t filler = 0; result && copy == 1 && filler < 12; filler += 1)
        {
            result = executable_test_data(test, BUSTER_LINK_CLASS_DATA, 3, 8, 0, 0) != 0;
        }
        memset(frames, 0, sizeof(*frames));
        executable_test_frames_cie(frames, EXECUTABLE_TEST_CIE_ZR, 0);
        executable_test_frames_fde(frames, *start, 0, 32, 3, 0);
        executable_test_frames_fde(frames, function, addend, 16, 2, 0);
        if (broken)
        {
            frames->fixup_count -= 1;
        }
        const AtomId atom = result ? executable_test_frames_atom(link, &test->bytes, frames) : 0;
        *frame_atom = copy == 0 ? atom : *frame_atom;
        result = atom != 0;
    }
    return result && link_check(link) == LINK_ERROR_NONE;
}

// Writes `link` with every lane count and by the exact twins with 1 and 8, each from zeroed fields over the last file
// scribbled over: whether every write succeeded into the same file, which runs, and whose unwind tables keep
// executable_test_unwind's rules (with the functions of units below `units`). *result gets the first write's.
static bool executable_test_unwind_writes(ExecutableTestTally* tally, Link* link, SymbolId entry, String8 path, uint32_t units, const char* what, ExecutableResult* result)
{
    Scratch scratch = scratch_begin(0);
    String8 first = { 0 };
    bool same = true;
    for (uint32_t w = 0; w < BUSTER_ARRAY_LENGTH(executable_test_lane_counts) + 2; w += 1)
    {
        const bool exact = w >= BUSTER_ARRAY_LENGTH(executable_test_lane_counts);
        const uint32_t lanes = exact ? (w == BUSTER_ARRAY_LENGTH(executable_test_lane_counts) ? 1 : 8) : executable_test_lane_counts[w];
        ExecutableResult written;
        const bool reset = executable_test_reset(link, path.pointer);
        const bool succeeded = reset && (exact ? executable_test_write_exact(link, entry, path, lanes, &written) : executable_test_write(link, entry, path, lanes, &written, 0, 0));
        if (!succeeded)
        {
            executable_test_print_failure(what, &written);
        }
        const String8 file = succeeded ? executable_test_read(scratch.arena, path.pointer) : (String8){ 0 };
        if (w == 0)
        {
            first = file;
            *result = written;
        }
        same = same && file.pointer != 0 && first.pointer != 0 && file.length == first.length && memcmp(file.pointer, first.pointer, file.length) == 0 &&
               written.fdes == result->fdes && written.fdes_in_order == result->fdes_in_order;
    }
    executable_test_expect(tally, same, what, 0);
    const char* failed = first.pointer != 0 ? executable_test_unwind(link, first, BUSTER_EXECUTABLE_BASE, units, result) : "the first write";
    executable_test_expect(tally, failed == 0, failed != 0 ? failed : what, 0);
    const bool ran = first.pointer != 0 && executable_test_runs(path.pointer, (const uint8_t*)"", 0);
    executable_test_expect(tally, ran, "the program runs", 0);
    scratch_end(scratch);
    return same && failed == 0 && ran;
}

static bool executable_test_unwind_links(const ExecutableTestDirectory* directory)
{
    ExecutableTestTally tally = {
        .check = "unwind links",
    };
    Scratch scratch = scratch_begin(0);
    char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    const String8 path = executable_test_path(directory, "unwind", path_bytes);
    ExecutableTestFrames* frames = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, ExecutableTestFrames, 1);
    executable_test_expect(&tally, frames != 0, "room for the atoms", 0);
    ExecutableResult result = { 0 };

    // Functions out of their FDEs' order, in atoms of every CIE the header indexes, some of which only the walk reads:
    // the table must come out sorted, and fdes_in_order false.
    ExecutableTestLink test;
    SymbolId start = 0;
    bool built = frames != 0 && executable_test_unwind_program(&test, &start, frames);
    executable_test_expect(&tally, built, "the functions' program", 0);
    if (built)
    {
        executable_test_unwind_writes(&tally, &test.link, start, path, UINT32_MAX, "functions out of their FDEs' order", &result);
        executable_test_expect(&tally, result.fdes == BUSTER_EXECUTABLE_TEST_UNWIND_FUNCTIONS + 1 && !result.fdes_in_order, "every FDE, out of order", result.fdes);
        printf("  functions out of their FDEs' order: %llu FDEs in %u atoms, %s\n", (unsigned long long)result.fdes, BUSTER_EXECUTABLE_TEST_UNWIND_ATOMS,
               result.fdes_in_order ? "in order" : "sorted");
    }
    if (frames != 0)
    {
        executable_test_link_end(&test);
    }

    // An FDE's initial location at INT32_MAX from the header links, and one more is refused, naming the atom; and one
    // from a wide addend links. The first write, with the function's own address, says where the header and the
    // function are, which the addend doesn't change.
    AtomId frame_atom = 0;
    built = frames != 0 && executable_test_unwind_small(&test, &start, frames, 0, false, &frame_atom);
    const bool learned = built && executable_test_unwind_writes(&tally, &test.link, start, path, UINT32_MAX, "an FDE for each function", &result);
    String8 file = learned ? executable_test_read(scratch.arena, path.pointer) : (String8){ 0 };
    const ExecutableTestImage image = executable_test_image(file);
    const Elf64_Shdr* header = executable_test_section(&image, ".eh_frame_hdr");
    int64_t function = 0;
    for (SymbolId symbol = 1; learned && symbol < test.link.symbols.count; symbol += 1)
    {
        function = executable_named(&test.link, symbol, "function") ? (int64_t)(BUSTER_EXECUTABLE_BASE + test.link.atoms.address[test.link.symbols.atom[symbol]]) : function;
    }
    executable_test_expect(&tally, header != 0 && function != 0, "the header and the function's address", 0);
    if (frames != 0)
    {
        executable_test_link_end(&test);
    }
    if (header != 0 && function != 0)
    {
        const int64_t at_limit = (int64_t)INT32_MAX + (int64_t)header->sh_addr - function;
        built = executable_test_unwind_small(&test, &start, frames, at_limit, false, &frame_atom);
        executable_test_expect(&tally, built && executable_test_unwind_writes(&tally, &test.link, start, path, 0, "an initial location at INT32_MAX", &result), "at INT32_MAX", 0);
        executable_test_link_end(&test);
        built = executable_test_unwind_small(&test, &start, frames, at_limit + 1, false, &frame_atom);
        if (built)
        {
            executable_test_refused(&tally, &test.link, start, path, EXECUTABLE_ERROR_UNWIND, frame_atom, 0, 0, "an initial location past INT32_MAX");
        }
        executable_test_link_end(&test);
        built = executable_test_unwind_small(&test, &start, frames, (int64_t)INT32_MIN - 1, false, &frame_atom);
        executable_test_expect(&tally, built && test.link.fixups.wide_count > 1, "a wide addend", 0);
        executable_test_expect(&tally, built && executable_test_unwind_writes(&tally, &test.link, start, path, 0, "an initial location from a wide addend", &result),
                               "from a wide addend", 0);
        executable_test_link_end(&test);
    }

    // Records the header can't index: an FDE without its pc_begin's fixup, in the first of two such atoms, which lie in
    // different lanes' ranges.
    built = frames != 0 && executable_test_unwind_small(&test, &start, frames, 0, true, &frame_atom);
    executable_test_expect(&tally, built, "the broken atoms' program", 0);
    if (built)
    {
        executable_test_refused(&tally, &test.link, start, path, EXECUTABLE_ERROR_UNWIND, frame_atom, 0, 0, "the first of two atoms the header can't index");
    }
    if (frames != 0)
    {
        executable_test_link_end(&test);
    }
    // And with a symbol defined nowhere, which the symbols' phase would refuse after the layout: the count refuses the
    // atom before it.
    built = frames != 0 && executable_test_unwind_small(&test, &start, frames, 0, true, &frame_atom);
    const NameId nowhere = built ? link_add_name(&test.link, (const uint8_t*)"nowhere", 7) : 0;
    built = nowhere != 0 && link_add_symbol(&test.link, nowhere, 0, 0, 0, BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_NONE) != 0 && link_check(&test.link) == LINK_ERROR_NONE;
    executable_test_expect(&tally, built, "the broken atoms' program with a symbol defined nowhere", 0);
    if (built)
    {
        executable_test_refused(&tally, &test.link, start, path, EXECUTABLE_ERROR_UNWIND, frame_atom, 0, 0, "an atom the header can't index, before a symbol defined nowhere");
    }
    if (frames != 0)
    {
        executable_test_link_end(&test);
    }
    unlink(path.pointer);
    scratch_end(scratch);
    return executable_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The patch kernel
//
// executable_patch_range, which does CALL and PC32 in one loop and redoes the other kinds after it, against
// executable_patch_other on every fixup (the checked path), on random fixups of every kind: every count from 0 to 199,
// from each of four starts in a group of 64, with the kind column ending at a PROT_NONE page so that the masked loads of
// the last group are checked.

#define BUSTER_EXECUTABLE_TEST_KERNEL_FIXUPS 512
#define BUSTER_EXECUTABLE_TEST_KERNEL_SYMBOLS 64

// ---------------------------------------------------------------------------------------------------------------------
// Blocks
//
// Links for the vector passes over blocks of 16 (executable_atom_block's atoms in executable_count and
// executable_place, and executable_resolve's symbols), against their exact twins: atoms of every class a static
// executable takes, in runs of one class and alignment of random lengths, so that blocks change bucket at every lane,
// of sizes from 0, with zero-fill ones, entry arrays' 8 bytes at a time, alignments from 0 to 6 and a page's; symbols
// in half of them at random places, thread-local, functions or objects, local or global, and weak ones defined nowhere;
// GOT slots for some (loads through the GOT in _start, which runs them), and a table with a pointer to (or a
// thread-local's offset of) every one. Every other link's atom table is just as large as its atoms, so that its last
// atoms are past the class gather's limit. Each link is written by every lane count and by the exact twins into the
// same file, which runs.

#define BUSTER_EXECUTABLE_TEST_BLOCK_LINKS 24
#define BUSTER_EXECUTABLE_TEST_BLOCK_ATOMS 200
// The largest atom's size, and _start's.
#define BUSTER_EXECUTABLE_TEST_BLOCK_SIZE 200
#define BUSTER_EXECUTABLE_TEST_BLOCK_CODE_BYTES 512
// The loads through the GOT in _start: 3 runs of up to 15 each.
#define BUSTER_EXECUTABLE_TEST_BLOCK_LOADS 45
// A seed whose links once outgrew their bytes' arena, which reserved 64 pages for up to 200 page-aligned atoms: the
// check runs it besides the one it is given.
#define BUSTER_EXECUTABLE_TEST_BLOCK_REGRESSION_SEED 116

static const uint8_t executable_test_block_classes[] = {
    BUSTER_LINK_CLASS_RODATA, BUSTER_LINK_CLASS_STRINGS, BUSTER_LINK_CLASS_TEXT,       BUSTER_LINK_CLASS_TDATA, BUSTER_LINK_CLASS_TBSS,
    BUSTER_LINK_CLASS_INIT_ARRAY, BUSTER_LINK_CLASS_FINI_ARRAY, BUSTER_LINK_CLASS_DATA_RELRO, BUSTER_LINK_CLASS_DATA,  BUSTER_LINK_CLASS_BSS,
};

// Builds one link from `state`, its atom table only as large as its atoms if `tight`. *start gets _start.
static bool executable_test_block_link(ExecutableTestLink* test, uint64_t* state, bool tight, SymbolId* start)
{
    const uint32_t atom_count = BUSTER_EXECUTABLE_TEST_BLOCK_ATOMS;
    const LinkCapacity capacity = {
        .names = 2 * atom_count + 8,
        .atoms = atom_count + 2 + (tight ? 0 : 64),
        .symbols = 2 * atom_count + 8,
        .fixups = 2 * atom_count + BUSTER_EXECUTABLE_TEST_BLOCK_LOADS + 8,
        .wide_addends = 4,
    };
    // The bytes at most: every atom of the largest size after the most padding a page's alignment takes, then _start's
    // code (aligned to 16) and the table (8 bytes per symbol, aligned to 8).
    const uint64_t byte_capacity = (uint64_t)atom_count * (BUSTER_EXECUTABLE_TEST_BLOCK_SIZE + BUSTER_EXECUTABLE_PAGE_BYTES - 1) + BUSTER_EXECUTABLE_TEST_BLOCK_CODE_BYTES + 15 +
                                   8 * (uint64_t)capacity.symbols + 7;
    bool result = executable_test_link_begin_with(test, byte_capacity, capacity);
    Link* link = &test->link;
    *start = result ? executable_test_symbol(link, "_start", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION) : 0;
    result = result && *start != 0;
    uint8_t run_class = BUSTER_LINK_CLASS_TEXT;
    uint8_t run_alignment = 0;
    uint32_t run_left = 0;
    for (uint32_t i = 0; result && i < atom_count; i += 1)
    {
        if (run_left == 0)
        {
            const uint64_t pick = executable_test_random(state);
            run_class = executable_test_block_classes[pick % BUSTER_ARRAY_LENGTH(executable_test_block_classes)];
            const bool array = run_class == BUSTER_LINK_CLASS_INIT_ARRAY || run_class == BUSTER_LINK_CLASS_FINI_ARRAY;
            run_alignment = array ? 3 : (pick >> 8) % 16 == 0 ? BUSTER_EXECUTABLE_PAGE_LOG2 : (uint8_t)((pick >> 12) % 7);
            run_left = 1 + (uint32_t)((pick >> 16) % 40);
        }
        run_left -= 1;
        const uint64_t pick = executable_test_random(state);
        const bool zero_fill = link_class_is_zero_fill(run_class);
        const bool array = run_class == BUSTER_LINK_CLASS_INIT_ARRAY || run_class == BUSTER_LINK_CLASS_FINI_ARRAY;
        const uint64_t size = array ? 8 * (pick % 4) : pick % 5 == 0 ? 0 : 1 + (pick >> 4) % BUSTER_EXECUTABLE_TEST_BLOCK_SIZE;
        uint8_t* bytes = !zero_fill && size != 0 ? arena_push(&test->bytes, size, 1ull << run_alignment) : 0;
        for (uint64_t b = 0; bytes != 0 && b < size; b += 1)
        {
            bytes[b] = (uint8_t)(pick >> (8 * (b % 8)));
        }
        const AtomId atom = zero_fill || size == 0 || bytes != 0 ? link_add_atom(link, run_class, run_alignment, size, bytes, 0) : 0;
        result = atom != 0;
        // A symbol in half of them, at a random place, of up to the rest of the atom.
        if (result && (pick >> 32) % 2 == 0)
        {
            char name[32];
            const int length = snprintf(name, sizeof(name), "a%u", i);
            const uint64_t value = size != 0 ? (pick >> 33) % (size + 1) : 0;
            const uint8_t type = link_class_is_tls(run_class) ? BUSTER_LINK_SYMBOL_TLS : run_class == BUSTER_LINK_CLASS_TEXT ? BUSTER_LINK_SYMBOL_FUNCTION : BUSTER_LINK_SYMBOL_OBJECT;
            const uint8_t binding = (pick >> 40) % 2 == 0 ? BUSTER_LINK_SYMBOL_GLOBAL : BUSTER_LINK_SYMBOL_LOCAL;
            const NameId id = link_add_name(link, (const uint8_t*)name, (uint32_t)length);
            result = id != 0 && link_add_symbol(link, id, atom, value, 0, type | binding, BUSTER_LINK_SOURCE_PROGRAM) != 0;
        }
        // And now and then a weak one defined nowhere, thread-local or not.
        if (result && (pick >> 48) % 8 == 0)
        {
            char name[32];
            const int length = snprintf(name, sizeof(name), "w%u", i);
            const uint8_t type = (pick >> 52) % 2 == 0 ? BUSTER_LINK_SYMBOL_TLS : BUSTER_LINK_SYMBOL_OBJECT;
            const NameId id = link_add_name(link, (const uint8_t*)name, (uint32_t)length);
            result = id != 0 && link_add_symbol(link, id, 0, 0, 0, type | BUSTER_LINK_SYMBOL_WEAK, BUSTER_LINK_SOURCE_NONE) != 0;
        }
    }
    // _start: loads through the GOT of some of the symbols (GOTTPOFF for a thread-local), then exit_group(0), each run
    // where the one before ends (a load is 7 bytes: REX.W, the opcode, ModRM and a 32-bit displacement).
    ExecutableTestCode runs[4] = { 0 };
    const SymbolId symbol_count = link->symbols.count;
    uint32_t loads = 0;
    for (SymbolId symbol = 2; result && symbol < symbol_count && loads < BUSTER_EXECUTABLE_TEST_BLOCK_LOADS; symbol += 1)
    {
        if (executable_test_random(state) % 4 == 0)
        {
            const X86Instruction load = {
                .reg = X86_REGISTER_RAX,
                .mode = BUSTER_X86_MEMORY_RIP,
                .form = X86_FORM_MOV_R64_RM64,
            };
            const bool tls = (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_TLS;
            executable_test_refer(&runs[loads / 15], load, tls ? BUSTER_LINK_FIXUP_GOTTPOFF : BUSTER_LINK_FIXUP_LOAD_GOT, symbol, 0);
            loads += 1;
        }
    }
    for (uint32_t run = 1; run < 4; run += 1)
    {
        runs[run].offset = runs[run - 1].offset + 7 * (uint64_t)runs[run - 1].instruction_count;
    }
    executable_test_exit(&runs[3], 0);
    result = result && executable_test_code(test, BUSTER_EXECUTABLE_TEST_BLOCK_CODE_BYTES, runs, 4, *start) != 0;
    // The table: each symbol's address, or a thread-local's offset from the thread pointer, 8 bytes each.
    const uint64_t table_size = 8 * (uint64_t)symbol_count;
    const AtomId table = result ? executable_test_data(test, BUSTER_LINK_CLASS_DATA, 3, table_size, 0, 0) : 0;
    result = result && table != 0;
    for (SymbolId symbol = 2; result && symbol < symbol_count; symbol += 1)
    {
        const bool tls = (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_TLS;
        result = link_add_fixup(link, table, 8 * symbol, tls ? BUSTER_LINK_FIXUP_TPOFF32 : BUSTER_LINK_FIXUP_ABS64, symbol, 0) != 0;
    }
    return result && link_check(link) == LINK_ERROR_NONE;
}

static bool executable_test_blocks(const ExecutableTestDirectory* directory, uint64_t seed)
{
    ExecutableTestTally tally = {
        .check = "blocks",
    };
    char buffer[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    const String8 path = executable_test_path(directory, "blocks", buffer);
    const uint64_t seeds[2] = { seed, BUSTER_EXECUTABLE_TEST_BLOCK_REGRESSION_SEED };
    for (uint32_t s = 0; s < (seed == BUSTER_EXECUTABLE_TEST_BLOCK_REGRESSION_SEED ? 1 : 2); s += 1)
    {
        uint64_t state = seeds[s] * 0x9E3779B97F4A7C15ull + 7;
        for (uint32_t i = 0; i < BUSTER_EXECUTABLE_TEST_BLOCK_LINKS; i += 1)
        {
            ExecutableTestLink test;
            SymbolId start = 0;
            const bool built = executable_test_block_link(&test, &state, i % 2 == 1, &start);
            executable_test_expect(&tally, built, "the link", (uint64_t)s * BUSTER_EXECUTABLE_TEST_BLOCK_LINKS + i);
            ExecutableResult result;
            if (built)
            {
                executable_test_unwind_writes(&tally, &test.link, start, path, 0, "the vector passes and the exact twins write the same file", &result);
            }
            executable_test_link_end(&test);
        }
    }
    if (!directory->keep)
    {
        unlink(path.pointer);
    }
    return executable_test_report(&tally);
}

static bool executable_test_kernel(uint64_t seed)
{
    ExecutableTestTally tally = {
        .check = "patch kernel",
    };
    const uint64_t page = BUSTER_EXECUTABLE_PAGE_BYTES;
    // The kind column's page, then a guard page.
    uint8_t* guarded = mmap(0, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool mapped = guarded != MAP_FAILED && mprotect(guarded + page, page, PROT_NONE) == 0;
    executable_test_expect(&tally, mapped, "the guard page", 0);
    Scratch scratch = scratch_begin(0);
    const uint64_t bytes_size = 16 * BUSTER_EXECUTABLE_TEST_KERNEL_FIXUPS;
    uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, BUSTER_EXECUTABLE_TEST_KERNEL_FIXUPS);
    SymbolId* targets = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, SymbolId, BUSTER_EXECUTABLE_TEST_KERNEL_FIXUPS);
    int32_t* addends = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, int32_t, BUSTER_EXECUTABLE_TEST_KERNEL_FIXUPS);
    uint8_t* kinds = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, BUSTER_EXECUTABLE_TEST_KERNEL_FIXUPS);
    uint32_t* reference = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, BUSTER_EXECUTABLE_TEST_KERNEL_SYMBOLS);
    uint32_t* got_slots = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, BUSTER_EXECUTABLE_TEST_KERNEL_SYMBOLS);
    uint64_t* sizes = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, BUSTER_EXECUTABLE_TEST_KERNEL_SYMBOLS);
    uint8_t* fast = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, bytes_size);
    uint8_t* checked = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, bytes_size);
    const int64_t wide_addends[4] = { 0, 1ll << 33, -(1ll << 40), 5ll << 32 };
    bool ready = mapped && offsets != 0 && targets != 0 && addends != 0 && kinds != 0 && reference != 0 && got_slots != 0 && sizes != 0 && fast != 0 && checked != 0;
    uint64_t state = seed * 0x9E3779B97F4A7C15ull + 1;
    for (uint32_t trial = 0; ready && trial < 4; trial += 1)
    {
        // Fields one per 16 bytes at a random place there; small references and addends, so that every value fits but
        // the ABS32 and ABS32S ones some trials push below 0.
        for (uint32_t symbol = 0; symbol < BUSTER_EXECUTABLE_TEST_KERNEL_SYMBOLS; symbol += 1)
        {
            reference[symbol] = (uint32_t)(executable_test_random(&state) % (1u << 24));
            got_slots[symbol] = (uint32_t)(executable_test_random(&state) % 1000);
            sizes[symbol] = executable_test_random(&state) % 4096;
        }
        for (uint32_t i = 0; i < BUSTER_EXECUTABLE_TEST_KERNEL_FIXUPS; i += 1)
        {
            const uint64_t pick = executable_test_random(&state);
            // The kinds up to SIZE64: an executable doesn't take the general- and local-dynamic ones yet.
            uint8_t kind = (uint8_t)(1 + pick % BUSTER_LINK_FIXUP_SIZE64);
            kind = pick % 3 == 0 ? kind : (pick & 8) != 0 ? BUSTER_LINK_FIXUP_CALL : BUSTER_LINK_FIXUP_PC32;
            const bool wide = (pick >> 8) % 16 == 0;
            offsets[i] = 16 * i + (uint32_t)((pick >> 16) % 8);
            targets[i] = (SymbolId)((pick >> 24) % BUSTER_EXECUTABLE_TEST_KERNEL_SYMBOLS);
            addends[i] = wide ? (int32_t)(1 + (pick >> 32) % 3) : (int32_t)((pick >> 32) % 4096) - 2048 - (trial == 3 ? (int32_t)BUSTER_EXECUTABLE_BASE * 2 : 0);
            kinds[i] = kind | (wide ? BUSTER_LINK_FIXUP_WIDE : 0);
        }
        for (uint32_t start = 0; start < 64; start += 21)
        {
            for (uint32_t count = 0; count < 200 && start + count <= BUSTER_EXECUTABLE_TEST_KERNEL_FIXUPS; count += 1)
            {
                // The kinds of fixups [start, start + count) end at the guard page.
                uint8_t* guarded_kinds = guarded + page - (start + count);
                memcpy(guarded_kinds + start, kinds + start, count);
                const ExecutablePatch patch = {
                    .offsets = offsets,
                    .targets = targets,
                    .addends = addends,
                    .kinds = guarded_kinds,
                    .wide_addends = wide_addends,
                    .reference = reference,
                    .got_slots = got_slots,
                    .sizes = sizes,
                    .base = BUSTER_EXECUTABLE_BASE,
                    .got = 1u << 20,
                    .checked = false,
                };
                ExecutablePatch reference_patch = patch;
                reference_patch.checked = true;
                memset(fast, 0, bytes_size);
                memset(checked, 0, bytes_size);
                const FixupId fast_failed = executable_patch_range(&patch, fast, 0x1000, start, start + count);
                const FixupId checked_failed = executable_patch_range(&reference_patch, checked, 0x1000, start, start + count);
                executable_test_expect(&tally, memcmp(fast, checked, bytes_size) == 0 && fast_failed == checked_failed, "the same fields as the checked path",
                                       trial * 100000 + start * 1000 + count);
            }
        }
    }
    scratch_end(scratch);
    if (guarded != MAP_FAILED)
    {
        munmap(guarded, 2 * page);
    }
    return executable_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

static bool executable_test_parse_u64(const char* text, uint64_t* value)
{
    char* end = 0;
    const unsigned long long parsed = strtoull(text, &end, 0);
    const bool result = end != text && *end == 0;
    *value = result ? parsed : *value;
    return result;
}

static int run_executable_tests(int argc, char** argv)
{
    uint64_t seed = 1;
    uint64_t id_order = 153;
    uint64_t functions = BUSTER_EXECUTABLE_TEST_DEFAULT_FUNCTIONS;
    const char* parent = getenv("TMPDIR") != 0 ? getenv("TMPDIR") : "/tmp";
    bool keep = false;
    bool valid = true;
    for (int i = 0; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            valid = valid && executable_test_parse_u64(argv[i + 1], &seed);
            i += 1;
        }
        else if (strcmp(argv[i], "--id-order") == 0 && i + 1 < argc)
        {
            valid = valid && executable_test_parse_u64(argv[i + 1], &id_order);
            i += 1;
        }
        else if (strcmp(argv[i], "--functions") == 0 && i + 1 < argc)
        {
            valid = valid && executable_test_parse_u64(argv[i + 1], &functions) && functions >= 1 && functions <= BUSTER_LINK_PROGRAM_MAX_COUNT;
            i += 1;
        }
        else if (strcmp(argv[i], "--directory") == 0 && i + 1 < argc)
        {
            parent = argv[i + 1];
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
    ExecutableTestDirectory directory = {
        .keep = keep,
    };
    const int length = snprintf(directory.path, sizeof(directory.path), "%s/buster-executable-XXXXXX", parent);
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-executable [--seed N] [--id-order N] [--functions N] [--directory PATH] [--keep]\n");
    }
    else if (length <= 0 || (uint64_t)length + 64 >= sizeof(directory.path) || mkdtemp(directory.path) == 0)
    {
        fprintf(stderr, "test-executable: can't make a directory in %s\n", parent);
    }
    else if (!x86_encoder_initialized && !x86_encoder_initialize())
    {
        fprintf(stderr, "the encoder's templates can't be built\n");
    }
    else
    {
        bool passed = executable_test_hash(seed);
        passed = executable_test_programs(&directory, seed, id_order, (uint32_t)functions) && passed;
        passed = executable_test_chunks(&directory) && passed;
        passed = executable_test_files(&directory, seed) && passed;
        passed = executable_test_limits(&directory, seed) && passed;
        passed = executable_test_frames(seed) && passed;
        passed = executable_test_sort(seed) && passed;
        passed = executable_test_unwind_links(&directory) && passed;
        passed = executable_test_blocks(&directory, seed) && passed;
        passed = executable_test_kernel(seed) && passed;
        result = passed ? 0 : 1;
        if (keep)
        {
            printf("kept %s\n", directory.path);
        }
        else
        {
            rmdir(directory.path);
        }
    }
    return result;
}

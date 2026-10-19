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
//     must exit with status 0 having written LinkProgram.output. A copy with one fixup 8 bytes off must write
//     something else. The sets that call libc, step 5's, must fail with an import named, without creating the output.
//   - Fields: in each program's file, every fixup's field against a scalar reference of its kind's arithmetic
//     (executable_test_expected), from the atoms' addresses and the symbols' definitions, with the TLS segment and the
//     linker's sections read back from the file's headers; the GOT, a slot for each GOT kind's target, numbered from 1
//     in symbol order and holding its value; and the headers themselves: the segments in lld's order, 4 KB-congruent,
//     with contiguous file offsets.
//   - Chunks: every chunk boundary of every program, and of programs built to put 4-byte fields (a call's rel32) and
//     8-byte fields (an ABS64 at an odd offset) across each place around a nominal boundary
//     (executable_test_straddle): no boundary splits a field, the lanes write the same file (each from zeroed fields
//     over a scribbled file, as above) and the program runs.
//   - Files: in place (a smaller program after a larger one, then a file whose mode was changed), created, and written
//     beside the output and renamed over it when a process runs it, when it has another link, and when it is a symbolic
//     link; a link that fails before opening the output leaves it alone, and one whose fixup doesn't fit removes it.
//   - Build IDs: the lanes' (the files are identical), another program's, and the tree's root recomputed from the file.
// The exit status is 1 if the arguments are invalid or a check fails.
//
// Layout, in this order: the tally, running and reading files, writing with lanes, the programs, the fields and
// headers, the chunks, the files, and run_executable_tests.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <elf.h>

// OS-dependent includes
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
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

// Runs the executable at `path` without arguments: what it writes to standard output, and how it ended.
static bool executable_test_run(const char* path, uint8_t* output, uint64_t* output_length, int* status)
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
            char* arguments[] = { (char*)path, 0 };
            char* environment[] = { "BUSTER=1", 0 };
            execve(path, arguments, environment);
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
    bool succeeded;
};

static void executable_test_lane_main(void* argument)
{
    ExecutableTestWrite* write = argument;
    const bool succeeded = executable_write(write->link, &write->options, &write->result);
    if (lane_index() == 0)
    {
        write->succeeded = succeeded;
    }
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

static const char* const executable_test_error_names[] = {
    "none",
    "memory",
    "layout",
    "undefined",
    "unsupported",
    "entry",
    "fixup",
    "file",
};

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

static ExecutableTestImage executable_test_image(String8 file)
{
    ExecutableTestImage result = { 0 };
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)file.pointer;
    result.valid = file.length >= sizeof(Elf64_Ehdr) && memcmp(header->e_ident, ELFMAG, SELFMAG) == 0 && header->e_ident[EI_CLASS] == ELFCLASS64 &&
                   header->e_type == ET_EXEC && header->e_machine == EM_X86_64 && header->e_phoff + header->e_phnum * sizeof(Elf64_Phdr) <= file.length &&
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
// section header. Returns false for an array that is empty, which has no section header to say where it is.
static bool executable_test_symbol_value(const Link* link, const ExecutableTestImage* image, SymbolId symbol, uint64_t* value)
{
    const AtomId atom = link->symbols.atom[symbol];
    bool result = true;
    if (atom != 0 && link_class_is_tls(link->atoms.class[atom]))
    {
        const Elf64_Phdr* tls = executable_test_segment(image, PT_TLS);
        result = tls != 0;
        const uint64_t remainder = result ? tls->p_vaddr % tls->p_align : 0;
        const uint64_t block = result ? executable_align(tls->p_memsz + remainder, tls->p_align) - remainder : 0;
        *value = result ? BUSTER_EXECUTABLE_BASE + link->atoms.address[atom] + link->symbols.value[symbol] - tls->p_vaddr - block : 0;
    }
    else if (atom != 0)
    {
        *value = BUSTER_EXECUTABLE_BASE + link->atoms.address[atom] + link->symbols.value[symbol];
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

// The value a fixup's field must hold, from the reference arithmetic of its kind (link_test_load's), with its target's
// value from executable_test_symbol_value. `holder` is the atom whose fixup it is, and width gets the field's width. A
// value that can't fit is a failure: a generated program never has one.
static ExecutableTestField executable_test_expected(const Link* link, const ExecutableTestImage* image, AtomId holder, FixupId fixup, uint64_t* value, uint32_t* width)
{
    const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
    const SymbolId target = link->fixups.target[fixup];
    const Elf64_Shdr* got = executable_test_section(image, ".got");
    uint64_t symbol = 0;
    const bool known = executable_test_symbol_value(link, image, target, &symbol);
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

// Every fixup's field in the file holds the reference's value; the GOT has a slot for each GOT kind's target, numbered
// from 1 in symbol order and holding its value; and an empty init or fini array starts where it ends.
static void executable_test_fields(ExecutableTestTally* tally, const Link* link, String8 file, uint64_t index)
{
    const ExecutableTestImage image = executable_test_image(file);
    executable_test_headers(tally, &image, index);
    uint64_t mismatched = 0;
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
            const bool same = field == EXECUTABLE_TEST_FIELD_UNKNOWN || (field == EXECUTABLE_TEST_FIELD_FITS && readable && (actual & mask) == (expected & mask));
            first = !same && mismatched == 0 ? fixup : first;
            mismatched += same ? 0 : 1;
        }
    }
    executable_test_expect(tally, mismatched == 0, "every field holds its kind's value", first);
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
            targets[link->fixups.target[fixup]] |= reads_slot ? 1 : 0;
        }
    }
    uint32_t next = 1;
    SymbolId wrong = 0;
    for (SymbolId symbol = 1; slots && image.valid && symbol < link->symbols.count; symbol += 1)
    {
        const uint32_t slot = link->symbols.got_slot[symbol];
        uint64_t expected = 0;
        uint64_t actual = 0;
        const bool known = executable_test_symbol_value(link, &image, symbol, &expected);
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
    executable_test_expect(tally, slots, "a GOT slot for each GOT kind's target, in symbol order, holding its value", wrong);
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
            const uint64_t field = atom_offset + link->fixups.offset[fixup];
            const uint64_t field_end = field + link_fixup_kinds[kind].width;
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

static bool executable_test_programs(const ExecutableTestDirectory* directory, uint64_t seed, uint32_t functions)
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
                executable_test_fields(&tally, &program.link, file, i);
                executable_test_expect(&tally, written && executable_test_build_id(scratch.arena, file, boundaries, result.chunks), "the build ID is the tree of the file's chunks", i);
                executable_test_expect(&tally, written && executable_test_boundaries(&program.link, file, boundaries, result.chunks), "no boundary splits a field", i);
                printf("  %s: %u atoms, %u fixups, %llu GOT slots, %llu bytes in %llu chunks: %s\n", names[i], program.link.atoms.count - 1, program.link.fixups.count - 1,
                       (unsigned long long)result.got_slots, (unsigned long long)result.file_size, (unsigned long long)result.chunks, ran ? "ran" : "failed");
            }
            else
            {
                executable_test_expect(&tally, file.pointer != 0 && first.pointer != 0 && file.length == first.length && memcmp(file.pointer, first.pointer, file.length) == 0,
                                       "the same file with every lane count", i * 10 + lanes);
                executable_test_expect(&tally, memcmp(result.build_id, first_result.build_id, BUSTER_EXECUTABLE_BUILD_ID_BYTES) == 0, "the same build ID", i * 10 + lanes);
            }
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

static bool executable_test_link_begin(ExecutableTestLink* test, uint64_t bytes)
{
    const LinkCapacity capacity = {
        .names = 16,
        .atoms = 16,
        .symbols = 16,
        .fixups = 64,
        .wide_addends = 4,
    };
    test->tables = arena_reserve(link_capacity_bytes(capacity));
    test->names = arena_reserve(BUSTER_EXECUTABLE_PAGE_BYTES);
    test->bytes = arena_reserve(bytes);
    return link_initialize(&test->link, &test->tables, &test->names, capacity) && test->names.base != 0 && test->bytes.base != 0;
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
// starts there, and with its width before it ends there: the boundary stays.

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

// Builds the program with the call at `call` in the function and the pointer at `pointer` in the table.
static bool executable_test_straddle(ExecutableTestStraddle* straddle, uint64_t call, uint64_t pointer)
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
    executable_test_refer(&runs[1], executable_test_bare(X86_FORM_CALL_REL32), BUSTER_LINK_FIXUP_CALL, writer, 0);
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
    bool placed = boundaries != 0 && executable_test_straddle(&probe, 16, 0) && executable_test_write(&probe.test.link, probe.start, path, 1, &result, 0, 0);
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
    // second.
    for (uint32_t before = 0; placed && before <= 8; before += 1)
    {
        const uint32_t call_before = before < 4 ? before : before % 5;
        const uint32_t pointer_before = before;
        // The call's rel32 is a byte after its opcode.
        const uint64_t call = first - call_before - 1 - code_offset;
        const uint64_t pointer = second - pointer_before - table_offset;
        ExecutableTestStraddle straddle;
        const bool built = executable_test_straddle(&straddle, call, pointer);
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
            // The cut: at the field's end when the field crosses the nominal boundary, there otherwise.
            const uint64_t first_cut = call_before > 0 && call_before < 4 ? first - call_before + 4 : first;
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

static bool executable_test_files(const ExecutableTestDirectory* directory, uint64_t seed)
{
    ExecutableTestTally tally = {
        .check = "files",
    };
    Scratch scratch = scratch_begin(0);
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
// The patch kernel
//
// executable_patch_range, which does CALL and PC32 in one loop and redoes the other kinds after it, against
// executable_patch_other on every fixup (the checked path), on random fixups of every kind: every count from 0 to 199,
// from each of four starts in a group of 64, with the kind column ending at a PROT_NONE page so that the masked loads of
// the last group are checked.

#define BUSTER_EXECUTABLE_TEST_KERNEL_FIXUPS 512
#define BUSTER_EXECUTABLE_TEST_KERNEL_SYMBOLS 64

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
            uint8_t kind = (uint8_t)(1 + pick % (BUSTER_LINK_FIXUP_KINDS - 1));
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
        fprintf(stderr, "usage: test-executable [--seed N] [--functions N] [--directory PATH] [--keep]\n");
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
        passed = executable_test_programs(&directory, seed, (uint32_t)functions) && passed;
        passed = executable_test_chunks(&directory) && passed;
        passed = executable_test_files(&directory, seed) && passed;
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

// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Standalone translation unit for the fused AVX-512 direct-to-ELF64 C17 compiler (#2850).
// Owns c_direct_lex.c, c_direct_x86.c, c_direct_types.c, and c_direct_cc.c privately so
// edits rebuild only this unit and internal symbols remain strictly TU-local.

#define _GNU_SOURCE

#include "buster/compiler/c_direct.h"
#include "buster/compiler/c_direct_cc.c"

#include <dirent.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <pthread.h>
#include <time.h>

// A unit is small unless its main file is this long (#3592). A small unit's arenas take base pages and its identifier
// tables start small. The arenas' 55 tables each start in a region of their own, and under transparent huge pages a
// unit that writes the first page of each has the kernel clear 2 MB for most of them: on the 9700X a one-function unit
// took 0.85 ms, 4.08 M of its 4.67 M cycles the kernel's, and 0.31 ms with base pages. For sqlite3.c's 9.5 MB huge
// pages are worth 2% (17.68 against 17.98 ms): the two meet at a few megabytes of source, most of which a main file
// below this has to get from its headers.
#define BUSTER_FDIRECT_LARGE_UNIT_BYTES (UINT64_C(1) << 20)

static bool c_direct_unit_is_small(const char* input_path)
{
    struct stat st;
    return stat(input_path, &st) != 0 || (uint64_t)st.st_size < BUSTER_FDIRECT_LARGE_UNIT_BYTES;
}

static bool c_direct_default_output_path(const char* input_path, char* buffer, size_t capacity)
{
    bool ok = false;
    if (input_path != NULL && input_path[0] != '\0')
    {
        const char* base = strrchr(input_path, '/');
        base = base != NULL ? base + 1 : input_path;
        const char* dot = strrchr(base, '.');
        const size_t stem_len = dot != NULL && dot > base ? (size_t)(dot - base) : strlen(base);
        if (stem_len > 0 && stem_len + 3u <= capacity)
        {
            memcpy(buffer, base, stem_len);
            buffer[stem_len] = '.';
            buffer[stem_len + 1u] = 'o';
            buffer[stem_len + 2u] = '\0';
            ok = true;
        }
    }
    return ok;
}

static bool c_direct_run_cmdline_macros(FastPP* pp, FastArena* scratch, const char* const* macros, uint32_t macro_count)
{
    bool ok = true;
    if (macro_count > 0)
    {
        uint64_t total_len = 0;
        for (uint32_t i = 0; i < macro_count; i += 1)
        {
            const char* m = macros[i];
            if (m != NULL && m[0] != '\0')
            {
                total_len += strlen(m) + 16u;
            }
        }
        char* buf = (char*)fast_arena_alloc(scratch, total_len + 16u, 16);
        if (buf == NULL)
        {
            ok = false;
        }
        else
        {
            uint64_t pos = 0;
            for (uint32_t i = 0; i < macro_count; i += 1)
            {
                const char* m = macros[i];
                if (m == NULL || m[0] == '\0') { continue; }
                const char kind = m[0];
                const char* body = m + 1;
                if (kind == 'D')
                {
                    const char* eq = strchr(body, '=');
                    memcpy(buf + pos, "#define ", 8);
                    pos += 8;
                    if (eq != NULL)
                    {
                        const size_t nlen = (size_t)(eq - body);
                        const size_t vlen = strlen(eq + 1);
                        memcpy(buf + pos, body, nlen);
                        pos += nlen;
                        buf[pos++] = ' ';
                        memcpy(buf + pos, eq + 1, vlen);
                        pos += vlen;
                    }
                    else
                    {
                        const size_t nlen = strlen(body);
                        memcpy(buf + pos, body, nlen);
                        pos += nlen;
                        buf[pos++] = ' ';
                        buf[pos++] = '1';
                    }
                    buf[pos++] = '\n';
                }
                else if (kind == 'U')
                {
                    const size_t nlen = strlen(body);
                    memcpy(buf + pos, "#undef ", 7);
                    pos += 7;
                    memcpy(buf + pos, body, nlen);
                    pos += nlen;
                    buf[pos++] = '\n';
                }
            }
            buf[pos] = '\0';
            if (pos > 0)
            {
                ok = fast_pp_run_buffer(pp, "<command line>", buf, pos);
            }
        }
    }
    return ok;
}

int c_direct_try_compile(const CDirectRequest* request)
{
    int status = 1; // 1 = clean bailout (caller runs full pipeline)
    if (request == NULL || request->input_path == NULL)
    {
        return 1;
    }

    char default_out[1024];
    const char* out_path = request->output_path;
    if (out_path == NULL)
    {
        if (!c_direct_default_output_path(request->input_path, default_out, sizeof(default_out)))
        {
            return 1;
        }
        out_path = default_out;
    }

    const bool small = c_direct_unit_is_small(request->input_path);
    FastArena arena = { 0 };
    FastArena scratch = { 0 };
    if (!fast_arena_init(&arena, UINT64_C(32) * 1024u * 1024u, request->avoid_huge_pages || small) ||
        !fast_arena_init(&scratch, UINT64_C(6) * 1024u * 1024u, request->avoid_huge_pages || small))
    {
        fast_arena_destroy(&scratch);
        fast_arena_destroy(&arena);
        return 1;
    }

    FastPP* pp = (FastPP*)fast_arena_alloc(&scratch, sizeof(FastPP), 64);
    FastObj* obj = (FastObj*)fast_arena_alloc(&scratch, sizeof(FastObj), 64);
    FastCCState* cc = (FastCCState*)fast_arena_alloc(&scratch, sizeof(FastCCState), 64);

    bool ok = pp != NULL && obj != NULL && cc != NULL && fast_pp_init(pp, &arena, &scratch, small) &&
              (request->dependencies == NULL || fast_pp_collect_dependencies(pp));
    if (ok)
    {
        pp->unsigned_char = request->unsigned_char;
        fast_pp_set_language(pp, request->c23, request->strict_iso, request->no_gnu_keywords, request->trigraphs, request->raw_strings);
        pp->feature_query = request->feature_query;
        pp->feature_context = request->feature_context;
        // __BASE_FILE__ is the main file's in a forced include too.
        pp->base_file = request->input_path;
        for (uint32_t i = 0; i < request->directory_count; i += 1)
        {
            fast_pp_add_dir(pp, request->directories[i], request->directory_system != NULL && request->directory_system[i] != 0);
        }
        pp->angled_first = request->angled_first <= pp->dir_count ? request->angled_first : pp->dir_count;
    }

    if (request->mark_stage != NULL)
    {
        request->mark_stage(request->clock_context, request->stage_setup);
    }

    // 1. Predefines (their __STDC macros are builtins, #3577; a macro's text stays readable to the end)
    if (ok)
    {
        pp->in_predefines = true;
        if (request->predefines_path != NULL)
        {
            // A system header's, as the full pipeline reads it (pp_run_prelude_macros).
            ok = fast_pp_run_file(pp, request->predefines_path, false, true);
        }
        else if (request->predefines_text != NULL && request->predefines_length > 0)
        {
            ok = fast_pp_run_buffer(pp, "<predefines>", request->predefines_text, request->predefines_length);
            char* eval_buf = ok ? (char*)fast_arena_alloc(&arena, 64u, 16) : NULL;
            if (eval_buf != NULL)
            {
                const int elen = snprintf(eval_buf, 64u, "#define __FLT_EVAL_METHOD__ %u\n", request->flt_eval_method);
                if (elen > 0)
                {
                    ok = fast_pp_run_buffer(pp, "<predefines>", eval_buf, (uint64_t)elen);
                }
            }
            else
            {
                ok = false;
            }
        }
        pp->in_predefines = false;
    }

    // 2. Command-line -D / -U macros
    if (ok)
    {
        ok = c_direct_run_cmdline_macros(pp, &scratch, request->macros, request->macro_count);
    }

    // 3. Forced -include files
    for (uint32_t i = 0; ok && i < request->include_count; i += 1)
    {
        const char* inc = request->includes[i];
        if (inc != NULL && inc[0] != '\0')
        {
            char resolved[1024];
            uint32_t dir_idx = 0;
            bool is_sys = false;
            if (access(inc, R_OK) == 0)
            {
                ok = fast_pp_run_file(pp, inc, false, false);
            }
            else if (fast_pp_resolve_include(pp, inc, (uint32_t)strlen(inc), false, false, resolved, sizeof(resolved), &dir_idx, &is_sys))
            {
                ok = fast_pp_run_file(pp, resolved, false, is_sys);
            }
            else
            {
                ok = false;
            }
        }
    }

    // 4. Main translation unit lex + preprocess
    if (ok)
    {
        ok = fast_pp_run_file(pp, request->input_path, true, false);
    }

    if (ok && request->out_token_count != NULL)
    {
        *request->out_token_count = pp->out_count;
    }

    if (ok && request->mark_stage != NULL)
    {
        request->mark_stage(request->clock_context, request->stage_preprocess);
    }

    // 5. Fused parse + typecheck + direct x86-64 machine-code generation
    if (ok)
    {
        ok = fast_obj_init(obj, &arena, request->pic, request->unwind_tables) &&
             fast_cc_init_state(cc, &arena, &scratch, pp, obj, request->unsigned_char, request->quiet) &&
             fast_cc_compile_unit(cc);
    }

    if (ok && request->mark_stage != NULL)
    {
        request->mark_stage(request->clock_context, request->stage_codegen);
    }

    // -MD and its kin (#3592): the files the unit read, of which the driver makes its rule now, so that a unit whose rule
    // it can't make leaves before an object is written. The main file is read but not listed (pp_dependencies).
    if (ok && request->dependencies != NULL)
    {
        if (pp->dependency_main != 0)
        {
            pp->dependency_records[pp->dependency_main - 1u].used = false;
        }
        ok = request->dependencies(request->dependency_context, pp->dependency_records, pp->dependency_count);
    }

    // 6. Direct single-writev ELF64 .o emission
    if (ok)
    {
        const bool wrote = fast_obj_write_elf(obj, out_path);
        if (request->mark_stage != NULL)
        {
            request->mark_stage(request->clock_context, request->stage_object_write);
        }
        if (wrote)
        {
            status = 0;
        }
        else
        {
            fprintf(stderr, "error: could not write object file %s\n", out_path);
            status = -1;
        }
    }

    // The rule that refused, for a caller that asks (#3577): the first bail's reason, the compiler's or else the
    // preprocessor's; a step that failed without one (a file that can't be read) is the setup's or the input's.
    if (status == 1 && request->out_bail_reason != NULL)
    {
        const char* reason = "setup or input";
        if (cc != NULL && cc->bailout && cc->bailout_reason != NULL)
        {
            reason = cc->bailout_reason;
        }
        else if (pp != NULL && pp->bailout && pp->bailout_reason != NULL)
        {
            reason = pp->bailout_reason;
        }
        *request->out_bail_reason = reason;
    }
    // Always release on bailout (caller will run full pipeline) or when caller requests cleanup
    if (status != 0 || request->release)
    {
        if (pp != NULL)
        {
            fast_pp_release_files(pp);
        }
        fast_arena_destroy(&scratch);
        fast_arena_destroy(&arena);
    }
    return status;
}

// ---------------------------------------------------------------------------------------------------------------------
// Self-tests for `build/compiler test-direct`

typedef struct CDirectExpectedSymbol CDirectExpectedSymbol;
struct CDirectExpectedSymbol
{
    const char* name;
    const void* bytes; // data to compare, or NULL for functions in increasing text-offset order
    uint32_t size;     // with no bytes, UINT32_MAX for a symbol the object must not have
};

static bool c_direct_test_symbols(const uint8_t* file, size_t size, const Elf64_Shdr* tables, uint32_t sections,
                                  const Elf64_Shdr* symtab, const CDirectExpectedSymbol* expected)
{
    bool ok = symtab->sh_link < sections;
    const Elf64_Shdr* names = ok ? tables + symtab->sh_link : NULL;
    ok = ok && names->sh_offset <= size && names->sh_size <= size - names->sh_offset;
    const Elf64_Sym* syms = (const Elf64_Sym*)(file + symtab->sh_offset);
    uint64_t next_text = 0;
    for (const CDirectExpectedSymbol* want = expected; ok && want->name != NULL; want += 1)
    {
        const size_t name_size = strlen(want->name) + 1u;
        const bool absent = want->bytes == NULL && want->size == UINT32_MAX;
        bool found = absent;
        for (size_t i = 0; i < symtab->sh_size / sizeof(*syms); i += 1)
        {
            const Elf64_Sym sym = syms[i];
            if (sym.st_name > names->sh_size || name_size > names->sh_size - sym.st_name ||
                memcmp(file + names->sh_offset + sym.st_name, want->name, name_size) != 0) { continue; }
            if (absent || sym.st_shndx == SHN_UNDEF || sym.st_shndx >= sections) { found = false; break; }
            const Elf64_Shdr* sec = tables + sym.st_shndx;
            if (sec->sh_offset > size || sec->sh_size > size - sec->sh_offset ||
                sym.st_value > sec->sh_size || sym.st_size > sec->sh_size - sym.st_value) { break; }
            if (want->bytes != NULL)
            {
                found = sym.st_size == want->size && memcmp(file + sec->sh_offset + sym.st_value, want->bytes, want->size) == 0;
            }
            else
            {
                found = ELF64_ST_TYPE(sym.st_info) == STT_FUNC && sym.st_size > 0 && sym.st_value >= next_text;
                next_text = sym.st_value + sym.st_size;
            }
            break;
        }
        ok = found;
    }
    return ok;
}

// A language mode for the tests (#3592): CDirectRequest's fields of the same names.
typedef struct CDirectTestMode CDirectTestMode;
struct CDirectTestMode
{
    bool c23;
    bool strict_iso;
    bool no_gnu_keywords;
    bool trigraphs;
    uint8_t raw_strings;
};

static bool c_direct_test_compile_source_with(const char* source, const char* obj_path, int expected_status, const char* const* directories,
                                              uint32_t directory_count, bool keep, uint32_t* out_text_len, uint32_t* out_sym_count,
                                              uint64_t* out_header_end, const CDirectExpectedSymbol* expected, const void* expected_rodata, size_t expected_bytes,
                                              CDirectTestMode mode);

static bool c_direct_test_compile_mode(const char* source, const char* obj_path, int expected_status, uint32_t* out_text_len, uint32_t* out_sym_count,
                                       CDirectTestMode mode)
{
    return c_direct_test_compile_source_with(source, obj_path, expected_status, NULL, 0, false, out_text_len, out_sym_count, NULL, NULL, NULL, 0, mode);
}

static bool c_direct_test_compile_source(const char* source, const char* obj_path, int expected_status,
                                         uint32_t* out_text_len, uint32_t* out_sym_count)
{
    return c_direct_test_compile_source_with(source, obj_path, expected_status, NULL, 0, false, out_text_len, out_sym_count, NULL, NULL, NULL, 0, (CDirectTestMode){ 0 });
}

// A valid source's object, left at `obj_path` over what is there, with angled includes searched in `directories`;
// `*out_header_end` is where the object's section headers end, which is where the file must.
static bool c_direct_test_compile_source_keep(const char* source, const char* obj_path, const char* const* directories, uint32_t directory_count,
                                              uint32_t* out_text_len, uint32_t* out_sym_count, uint64_t* out_header_end)
{
    return c_direct_test_compile_source_with(source, obj_path, 0, directories, directory_count, true, out_text_len, out_sym_count, out_header_end, NULL, NULL, 0, (CDirectTestMode){ 0 });
}

static bool c_direct_test_compile_source_with(const char* source, const char* obj_path, int expected_status, const char* const* directories,
                                              uint32_t directory_count, bool keep, uint32_t* out_text_len, uint32_t* out_sym_count,
                                              uint64_t* out_header_end, const CDirectExpectedSymbol* expected, const void* expected_rodata, size_t expected_bytes,
                                              CDirectTestMode mode)
{
    char src_path[256];
    snprintf(src_path, sizeof(src_path), "/tmp/buster_direct_test_%d.c", (int)getpid());
    FILE* f = fopen(src_path, "wb");
    if (f == NULL) { return false; }
    const size_t slen = strlen(source);
    const bool wrote = fwrite(source, 1, slen, f) == slen;
    fclose(f);
    if (!wrote) { unlink(src_path); return false; }

    if (!keep)
    {
        unlink(obj_path);
    }
    static const uint8_t system_directories[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
    static const char minimal_predefs[] =
        "#define __x86_64__ 1\n"
        "#define __LP64__ 1\n"
        "#define __STDC__ 1\n"
        "#define __STDC_VERSION__ 201710L\n"
        "#define __SIZE_TYPE__ unsigned long\n"
        "#define __PTRDIFF_TYPE__ long\n"
        "#define __INT64_TYPE__ long\n"
        "#define __UINT64_TYPE__ unsigned long\n";
    const CDirectRequest req = {
        .input_path = src_path,
        .output_path = obj_path,
        .predefines_text = minimal_predefs,
        .predefines_length = (uint32_t)(sizeof(minimal_predefs) - 1u),
        .flt_eval_method = 0,
        .directories = directories,
        .directory_system = system_directories,
        .directory_count = directory_count <= 8u ? directory_count : 0u,
        .pic = true,
        .unwind_tables = true,
        .unsigned_char = false,
        .c23 = mode.c23,
        .strict_iso = mode.strict_iso,
        .no_gnu_keywords = mode.no_gnu_keywords,
        .trigraphs = mode.trigraphs,
        .raw_strings = mode.raw_strings,
        .quiet = true,
        .release = true,
    };
    const int st = c_direct_try_compile(&req);
    unlink(src_path);
    if (st != expected_status)
    {
        unlink(obj_path);
        return false;
    }
    if (expected_status == 1)
    {
        // Bailout MUST NOT create output file!
        const bool exists = access(obj_path, F_OK) == 0;
        if (exists) { unlink(obj_path); }
        return !exists;
    }

    // Validate ELF64 header + symtab invariants on emitted .o
    FILE* ef = fopen(obj_path, "rb");
    if (ef == NULL) { return false; }
    fseek(ef, 0, SEEK_END);
    const long fsz = ftell(ef);
    fseek(ef, 0, SEEK_SET);
    if (fsz < (long)sizeof(Elf64_Ehdr)) { fclose(ef); unlink(obj_path); return false; }
    uint8_t* buf = (uint8_t*)malloc((size_t)fsz);
    if (buf == NULL || fread(buf, 1, (size_t)fsz, ef) != (size_t)fsz) { free(buf); fclose(ef); unlink(obj_path); return false; }
    fclose(ef);
    if (!keep)
    {
        unlink(obj_path);
    }

    const Elf64_Ehdr* eh = (const Elf64_Ehdr*)buf;
    bool valid = eh->e_ident[0] == 0x7fu && eh->e_ident[1] == 'E' && eh->e_ident[2] == 'L' && eh->e_ident[3] == 'F' &&
                 eh->e_ident[4] == ELFCLASS64 && eh->e_type == ET_REL && eh->e_machine == EM_X86_64 &&
                 eh->e_shoff + (uint64_t)eh->e_shnum * sizeof(Elf64_Shdr) <= (uint64_t)fsz;
    uint32_t text_len = 0;
    uint32_t sym_count = 0;
    bool matched = expected == NULL;
    if (valid)
    {
        const Elf64_Shdr* sh = (const Elf64_Shdr*)(buf + eh->e_shoff);
        if (expected_rodata != NULL)
        {
            valid = FSEC_RODATA < eh->e_shnum && sh[FSEC_RODATA].sh_size == expected_bytes &&
                    sh[FSEC_RODATA].sh_offset <= (uint64_t)fsz &&
                    expected_bytes <= (uint64_t)fsz - sh[FSEC_RODATA].sh_offset &&
                    memcmp(buf + sh[FSEC_RODATA].sh_offset, expected_rodata, expected_bytes) == 0;
        }
        for (uint16_t i = 0; i < eh->e_shnum; i += 1)
        {
            if (sh[i].sh_type == SHT_PROGBITS && (sh[i].sh_flags & SHF_EXECINSTR) != 0)
            {
                text_len = (uint32_t)sh[i].sh_size;
            }
            if (sh[i].sh_type == SHT_SYMTAB)
            {
                if (sh[i].sh_offset > (uint64_t)fsz || sh[i].sh_size > (uint64_t)fsz - sh[i].sh_offset)
                {
                    valid = false;
                    break;
                }
                if (expected != NULL) { matched = c_direct_test_symbols(buf, (size_t)fsz, sh, eh->e_shnum, sh + i, expected); }
                sym_count = (uint32_t)(sh[i].sh_size / sizeof(Elf64_Sym));
                const Elf64_Sym* syms = (const Elf64_Sym*)(buf + sh[i].sh_offset);
                // ELF invariant: all STB_LOCAL symbols MUST appear before sh_info, non-locals at >= sh_info!
                for (uint32_t s = 0; s < sym_count; s += 1)
                {
                    const uint8_t bind = ELF64_ST_BIND(syms[s].st_info);
                    if (s < sh[i].sh_info && bind != STB_LOCAL) { valid = false; }
                    if (s >= sh[i].sh_info && bind == STB_LOCAL) { valid = false; }
                }
            }
        }
    }
    if (out_header_end != NULL) { *out_header_end = valid ? eh->e_shoff + (uint64_t)eh->e_shnum * sizeof(Elf64_Shdr) : 0u; }
    free(buf);
    if (out_text_len != NULL) { *out_text_len = text_len; }
    if (out_sym_count != NULL) { *out_sym_count = sym_count; }
    return valid && matched && (expected_rodata == NULL || sym_count > 0);
}


// The lexer's tests (#2850). A text's tokens must not depend on where its bytes fall in the lexer's 64-byte blocks, nor
// on what separates them: every case's text is lexed at every offset of a block and between spaces, newlines and
// comments, and must give the kinds written here (a number's kind is its value's and is not pinned) and the same data
// each time. An inactive group in the middle is the skipper's part: it must leave no token at any offset either.
#define BUSTER_DIRECT_TEST_NUMBER 0xffu

typedef struct CDirectLexCase CDirectLexCase;
struct CDirectLexCase
{
    const char* text;
    uint8_t count;
    uint8_t kinds[8];
};

static const CDirectLexCase c_direct_lex_cases[] = {
    { "a", 1, { FTOK_IDENT } },
    { "foo_bar1", 1, { FTOK_IDENT } },
    { "an_identifier_of_more_than_sixty_four_bytes_which_no_block_holds_whole_", 1, { FTOK_IDENT } },
    { "an_identifier_of_more_than_two_blocks_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 1, { FTOK_IDENT } },
    { "an_identifier_of_more_than_two_hundred_and_fifty_five_bytes_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 1, { FTOK_IDENT } },
    { "int", 1, { FTOK_KW_INT } },
    { "return", 1, { FTOK_KW_RETURN } },
    // glibc's assert under NDEBUG, `(__ASSERT_VOID_CAST (0))`, in the window's text: an empty statement when it stands alone, and
    // `((void)0)` elsewhere. (A file's last 80 bytes are fast_lex_one's, which expands it in full: these cases
    // stand in the first half for that.)
    { "{ CHECK(a > b); }", 3, { FTOK_LBRACE, FTOK_SEMI, FTOK_RBRACE } },
    { "RETAINED(a);", 7, { FTOK_LPAREN, FTOK_IDENT, FTOK_LPAREN, BUSTER_DIRECT_TEST_NUMBER, FTOK_RPAREN, FTOK_RPAREN, FTOK_SEMI } },
    { ", CHECK(a);", 8, { FTOK_COMMA, FTOK_LPAREN, FTOK_LPAREN, FTOK_KW_VOID, FTOK_RPAREN, BUSTER_DIRECT_TEST_NUMBER, FTOK_RPAREN, FTOK_SEMI } },
    { "CHECK(a)", 6, { FTOK_LPAREN, FTOK_LPAREN, FTOK_KW_VOID, FTOK_RPAREN, BUSTER_DIRECT_TEST_NUMBER, FTOK_RPAREN } },
    { "0", 1, { BUSTER_DIRECT_TEST_NUMBER } },
    { "42", 1, { BUSTER_DIRECT_TEST_NUMBER } },
    { "0x1F", 1, { BUSTER_DIRECT_TEST_NUMBER } },
    { "10UL", 1, { BUSTER_DIRECT_TEST_NUMBER } },
    { "123456789012", 1, { BUSTER_DIRECT_TEST_NUMBER } },
    { "1.5", 1, { BUSTER_DIRECT_TEST_NUMBER } },
    { ".5", 1, { BUSTER_DIRECT_TEST_NUMBER } },
    { "1.", 1, { BUSTER_DIRECT_TEST_NUMBER } },
    { "1e5", 1, { BUSTER_DIRECT_TEST_NUMBER } },
    { "1e-6", 1, { BUSTER_DIRECT_TEST_NUMBER } },
    { "1E+5", 1, { BUSTER_DIRECT_TEST_NUMBER } },
    { "0x1p-3", 1, { BUSTER_DIRECT_TEST_NUMBER } },
    { "2.5e+3f", 1, { BUSTER_DIRECT_TEST_NUMBER } },
    { "1-2", 3, { BUSTER_DIRECT_TEST_NUMBER, FTOK_MINUS, BUSTER_DIRECT_TEST_NUMBER } },
    { "e+1", 3, { FTOK_IDENT, FTOK_PLUS, BUSTER_DIRECT_TEST_NUMBER } },
    { "x.y", 3, { FTOK_IDENT, FTOK_DOT, FTOK_IDENT } },
    { "f(1.5,.25)", 6, { FTOK_IDENT, FTOK_LPAREN, BUSTER_DIRECT_TEST_NUMBER, FTOK_COMMA, BUSTER_DIRECT_TEST_NUMBER, FTOK_RPAREN } },
    { "...", 1, { FTOK_ELLIPSIS } },
    { ".", 1, { FTOK_DOT } },
    { "(...)", 3, { FTOK_LPAREN, FTOK_ELLIPSIS, FTOK_RPAREN } },
    { "<<=", 1, { FTOK_SHL_ASSIGN } },
    { ">>=", 1, { FTOK_SHR_ASSIGN } },
    { "<<", 1, { FTOK_LSHIFT } },
    { ">>", 1, { FTOK_RSHIFT } },
    { "<=", 1, { FTOK_LE } },
    { ">=", 1, { FTOK_GE } },
    { "==", 1, { FTOK_EQ } },
    { "!=", 1, { FTOK_NE } },
    { "->", 1, { FTOK_ARROW } },
    { "++", 1, { FTOK_INC } },
    { "--", 1, { FTOK_DEC } },
    { "+=", 1, { FTOK_ADD_ASSIGN } },
    { "-=", 1, { FTOK_SUB_ASSIGN } },
    { "*=", 1, { FTOK_MUL_ASSIGN } },
    { "/=", 1, { FTOK_DIV_ASSIGN } },
    { "%=", 1, { FTOK_MOD_ASSIGN } },
    { "&=", 1, { FTOK_AND_ASSIGN } },
    { "|=", 1, { FTOK_OR_ASSIGN } },
    { "^=", 1, { FTOK_XOR_ASSIGN } },
    { "&&", 1, { FTOK_LAND } },
    { "||", 1, { FTOK_LOR } },
    { "+ - * / % & |", 7, { FTOK_PLUS, FTOK_MINUS, FTOK_STAR, FTOK_SLASH, FTOK_PERCENT, FTOK_AMP, FTOK_PIPE } },
    { "^ ~ ! < > = ?", 7, { FTOK_CARET, FTOK_TILDE, FTOK_BANG, FTOK_LT, FTOK_GT, FTOK_ASSIGN, FTOK_QUESTION } },
    { ": ; , ( ) [ ]", 7, { FTOK_COLON, FTOK_SEMI, FTOK_COMMA, FTOK_LPAREN, FTOK_RPAREN, FTOK_LBRACKET, FTOK_RBRACKET } },
    { "{}", 2, { FTOK_LBRACE, FTOK_RBRACE } },
    { "i++==j", 4, { FTOK_IDENT, FTOK_INC, FTOK_EQ, FTOK_IDENT } },
    { "a+++b", 4, { FTOK_IDENT, FTOK_INC, FTOK_PLUS, FTOK_IDENT } },
    { "a---b", 4, { FTOK_IDENT, FTOK_DEC, FTOK_MINUS, FTOK_IDENT } },
    { "p->q", 3, { FTOK_IDENT, FTOK_ARROW, FTOK_IDENT } },
    { "x<<=1", 3, { FTOK_IDENT, FTOK_SHL_ASSIGN, BUSTER_DIRECT_TEST_NUMBER } },
    { "y>>=2", 3, { FTOK_IDENT, FTOK_SHR_ASSIGN, BUSTER_DIRECT_TEST_NUMBER } },
    { "a<<b", 3, { FTOK_IDENT, FTOK_LSHIFT, FTOK_IDENT } },
    { "a<<==b", 4, { FTOK_IDENT, FTOK_SHL_ASSIGN, FTOK_ASSIGN, FTOK_IDENT } },
    { "a->>b", 4, { FTOK_IDENT, FTOK_ARROW, FTOK_GT, FTOK_IDENT } },
    { "a>>>=b", 4, { FTOK_IDENT, FTOK_RSHIFT, FTOK_GE, FTOK_IDENT } },
    { "a&&&b", 4, { FTOK_IDENT, FTOK_LAND, FTOK_AMP, FTOK_IDENT } },
    { "a|=b||c", 5, { FTOK_IDENT, FTOK_OR_ASSIGN, FTOK_IDENT, FTOK_LOR, FTOK_IDENT } },
    { "*p++=c", 5, { FTOK_STAR, FTOK_IDENT, FTOK_INC, FTOK_ASSIGN, FTOK_IDENT } },
    { "a/b", 3, { FTOK_IDENT, FTOK_SLASH, FTOK_IDENT } },
    { "a/=b", 3, { FTOK_IDENT, FTOK_DIV_ASSIGN, FTOK_IDENT } },
    { "a/*c*/b", 2, { FTOK_IDENT, FTOK_IDENT } },
    { "a/**/+/***/b", 3, { FTOK_IDENT, FTOK_PLUS, FTOK_IDENT } },
    { "a/*/ b */c", 2, { FTOK_IDENT, FTOK_IDENT } },
    { "x/ *y/**/", 4, { FTOK_IDENT, FTOK_SLASH, FTOK_STAR, FTOK_IDENT } },
    { "a*/**/b", 3, { FTOK_IDENT, FTOK_STAR, FTOK_IDENT } },
    { "\"str\"", 1, { FTOK_STR_LIT } },
    { "\"a\\\"b/*\"", 1, { FTOK_STR_LIT } },
    { "\"\"", 1, { FTOK_STR_LIT } },
    { "'c'", 1, { FTOK_CHAR_LIT } },
    { "'\\n'", 1, { FTOK_CHAR_LIT } },
    { "'\\''", 1, { FTOK_CHAR_LIT } },
    { "'\"'", 1, { FTOK_CHAR_LIT } },
    { "'\\x41'", 1, { FTOK_CHAR_LIT } },
    { "123\"s\"'c'", 3, { BUSTER_DIRECT_TEST_NUMBER, FTOK_STR_LIT, FTOK_CHAR_LIT } },
    { "s[1]='a';", 7, { FTOK_IDENT, FTOK_LBRACKET, BUSTER_DIRECT_TEST_NUMBER, FTOK_RBRACKET, FTOK_ASSIGN, FTOK_CHAR_LIT, FTOK_SEMI } },
    { "u8\"x\"", 1, { FTOK_STR_LIT } },
    { "L'x'", 1, { FTOK_CHAR_LIT } },
    { "\"caf\xc3\xa9\"", 1, { FTOK_STR_LIT } },
    { "a /* caf\xc3\xa9 */ b", 2, { FTOK_IDENT, FTOK_IDENT } },
    { "x = 1 \\\n + 2;", 6, { FTOK_IDENT, FTOK_ASSIGN, BUSTER_DIRECT_TEST_NUMBER, FTOK_PLUS, BUSTER_DIRECT_TEST_NUMBER, FTOK_SEMI } },
    { "a@b", 3, { FTOK_IDENT, FTOK_PP_OTHER, FTOK_IDENT } },
    { "a<\xbd" "b", 4, { FTOK_IDENT, FTOK_LT, FTOK_PP_OTHER, FTOK_IDENT } },
    { "a+\xab-\xad", 5, { FTOK_IDENT, FTOK_PLUS, FTOK_PP_OTHER, FTOK_MINUS, FTOK_PP_OTHER } },
    { "1.e2", 1, { BUSTER_DIRECT_TEST_NUMBER } },
    { "a.b.c", 5, { FTOK_IDENT, FTOK_DOT, FTOK_IDENT, FTOK_DOT, FTOK_IDENT } },
    { "a<<<=b", 4, { FTOK_IDENT, FTOK_LSHIFT, FTOK_LE, FTOK_IDENT } },
    { "x+++++y", 5, { FTOK_IDENT, FTOK_INC, FTOK_INC, FTOK_PLUS, FTOK_IDENT } },
    { "TWO", 1, { BUSTER_DIRECT_TEST_NUMBER } },
    { "ID(q)", 1, { FTOK_IDENT } },
    { "EMPTY", 0, { 0 } },
    { "F(1,2)", 3, { BUSTER_DIRECT_TEST_NUMBER, FTOK_PLUS, BUSTER_DIRECT_TEST_NUMBER } },
    { "F ( a , ( b , c ) )", 7, { FTOK_IDENT, FTOK_PLUS, FTOK_LPAREN, FTOK_IDENT, FTOK_COMMA, FTOK_IDENT, FTOK_RPAREN } },
    { "STR(x y)", 1, { FTOK_STR_LIT } },
    { "CAT(a,b)", 1, { FTOK_IDENT } },
    { "CAT(<,<=)", 1, { FTOK_SHL_ASSIGN } },
    { "ID(ID(TWO))", 1, { BUSTER_DIRECT_TEST_NUMBER } },
    { "last", 1, { FTOK_IDENT } },
};

static bool c_direct_test_kind_is_number(uint8_t kind)
{
    return kind == FTOK_INT_LIT || kind == FTOK_FLOAT_LIT || kind == FTOK_INT_U32 || kind == FTOK_INT_I64 || kind == FTOK_INT_U64 ||
           kind == FTOK_FLOAT32 || kind == FTOK_FLOAT80;
}

// The preprocessor's tokens of `text`, at most `capacity` of them.
static bool c_direct_test_tokens_mode(const char* text, uint64_t length, uint8_t* kinds, uint32_t* data, uint32_t capacity, uint32_t* out_count, bool small,
                                      CDirectTestMode mode)
{
    FastArena arena = { 0 };
    FastArena scratch = { 0 };
    bool ok = fast_arena_init(&arena, UINT64_C(32) * 1024u * 1024u, true) && fast_arena_init(&scratch, UINT64_C(6) * 1024u * 1024u, true);
    if (ok)
    {
        FastPP* pp = (FastPP*)fast_arena_alloc(&scratch, sizeof(FastPP), 64);
        ok = pp != NULL && fast_pp_init(pp, &arena, &scratch, small);
        if (ok)
        {
            fast_pp_set_language(pp, mode.c23, mode.strict_iso, mode.no_gnu_keywords, mode.trigraphs, mode.raw_strings);
        }
        ok = ok && fast_pp_run_buffer(pp, "<test>", text, length) && pp->out_count <= capacity;
        if (ok)
        {
            memcpy(kinds, pp->out_kind, pp->out_count);
            memcpy(data, pp->out_data, (size_t)pp->out_count * sizeof(uint32_t));
            *out_count = pp->out_count;
        }
    }
    fast_arena_destroy(&scratch);
    fast_arena_destroy(&arena);
    return ok;
}

// One layout of the cases' texts: `separator` and `shift` spaces before each, so that each text meets every offset of
// the block that starts after the last comment or literal before it, and an inactive group half way, each of its lines
// after a line of `shift` spaces for the same reason.
static uint32_t c_direct_test_layout(char* buffer, uint32_t shift, const char* separator)
{
    static const char header[] =
        "#define TWO 2\n#define ID(x) x\n#define EMPTY\n#define F(a, b) a+b\n#define STR(x) #x\n#define CAT(a, b) a ## b\n"
        "#define VOID_CAST (void)\n#define CHECK(x) (VOID_CAST (0))\n"
        "#define CHANGING (void)\n#define RETAINED(x) (CHANGING (0))\n#undef CHANGING\n#define CHANGING record_call\n";
    static const char* const inactive[] = {
        "#if 0\n",
        "\"/*\" is no comment: #endif here ends no group\n",
        "#if 1\n",
        "/* a comment is a blank */ #endif\n",
        "/* a comment\n#endif inside it\n*/ 'it's\n",
        "x = '\"'; y = \"//\"; /* \" */\n",
        "a line of text and then the two characters that start a comment /",
        "* #endif\n*/\n",
        "#  if 1\n",
        "#else\n",
        "#  endif // #endif\n",
        "    #if 1\n",
        "\t #endif\n",
        "y = \"a literal, then the two characters that start a comment /",
        "* inside it\";\n",
        "#if 1\n",
        "/* c */\n",
        "#endif\n",
        "#define Q \"an open literal \\\n#endif in a literal that goes on\"\n",
        "text, a quote \" and a backslash at the line's end \\",
        "\n#endif in a literal that goes on\"\n",
        "// a line comment \\\n#endif in a comment that goes on\n",
        "#error don't\n",
        "#endif\n",
    };
    const uint32_t count = (uint32_t)(sizeof(c_direct_lex_cases) / sizeof(c_direct_lex_cases[0]));
    const uint32_t separator_length = (uint32_t)strlen(separator);
    uint32_t length = (uint32_t)(sizeof(header) - 1u);
    memcpy(buffer, header, length);
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t text_length = (uint32_t)strlen(c_direct_lex_cases[i].text);
        memcpy(buffer + length, separator, separator_length);
        length += separator_length;
        memset(buffer + length, ' ', shift);
        length += shift;
        memcpy(buffer + length, c_direct_lex_cases[i].text, text_length);
        length += text_length;
        if (i == count / 2u)
        {
            buffer[length++] = '\n';
            for (uint32_t j = 0; j < sizeof(inactive) / sizeof(inactive[0]); j += 1)
            {
                const uint32_t line_length = (uint32_t)strlen(inactive[j]);
                // A line of spaces before a line, but not inside one that is given in two parts.
                if (j == 0 || inactive[j - 1u][strlen(inactive[j - 1u]) - 1u] == '\n')
                {
                    memset(buffer + length, ' ', shift);
                    length += shift;
                    buffer[length++] = '\n';
                }
                memcpy(buffer + length, inactive[j], line_length);
                length += line_length;
            }
        }
    }
    buffer[length++] = '\n';
    return length;
}

// The class table against the tables it restates.
static bool c_direct_test_tokens_sized(const char* text, uint64_t length, uint8_t* kinds, uint32_t* data, uint32_t capacity, uint32_t* out_count, bool small)
{
    return c_direct_test_tokens_mode(text, length, kinds, data, capacity, out_count, small, (CDirectTestMode){ 0 });
}

static bool c_direct_test_tokens(const char* text, uint64_t length, uint8_t* kinds, uint32_t* data, uint32_t capacity, uint32_t* out_count)
{
    return c_direct_test_tokens_sized(text, length, kinds, data, capacity, out_count, false);
}

// Whether every used slot of the cache is where its key's hash puts it, names an interned name of its key's length
// and kind, and the cache has `bits`.
static bool c_direct_test_cache_consistent(const FastIdentTable* t, uint32_t bits)
{
    bool ok = t->cache_bits == bits && t->cache_shift == 64u - bits;
    for (uint32_t i = 0; ok && i < (1u << bits); i += 1)
    {
        const FastIdentCacheSlot* slot = t->cache + i;
        if (slot->id != 0)
        {
            const FastIdentEntry* entry = t->entries + slot->id;
            ok = slot->id <= t->count && entry->length == slot->len && slot->tok_kind == (entry->kw_kind != 0 ? entry->kw_kind : FTOK_IDENT) &&
                 (BUSTER_FIDENT_CACHE_HASH(slot->w0, slot->len > 8u ? BUSTER_FIDENT_CACHE_MIX(slot->w1, slot->len) : 0u) >> t->cache_shift) == i;
        }
    }
    return ok;
}

// A name of each of the lookup's three lengths by `kind`: up to eight bytes, nine to sixteen, and more.
static uint32_t c_direct_test_growth_name(char* name, uint32_t kind, uint32_t i)
{
    static const char* const formats[3] = { "n%u", "mid_name_%u", "a_name_of_more_than_16_bytes_%u" };
    return (uint32_t)snprintf(name, 64, formats[kind], i);
}

// A small unit's identifier tables (#3592): the cache grows at 8,192 and at 32,768 names and keeps every slot it had,
// the hash table grows from its small start, a lookup finds a name where the growth put it, and a unit's tokens are the
// same from small tables and from full ones.
static bool c_direct_test_ident_growth(void)
{
    uint32_t kind_growths[3] = { 0 };
    enum { names = 36000, capacity = 2 * names + 64, text_size = 2 * names * 40 };
    bool ok = true;
    // Three phases: each of the three lengths makes the cache grow in one of them.
    for (uint32_t phase = 0; ok && phase < 3u; phase += 1)
    {
        FastArena arena = { 0 };
        ok = fast_arena_init(&arena, UINT64_C(32) * 1024u * 1024u, true);
        FastIdentTable table;
        ok = ok && fast_ident_init(&table, &arena, true);
        ok = ok && table.group_count == BUSTER_FIDENT_SMALL_GROUPS && c_direct_test_cache_consistent(&table, BUSTER_FIDENT_SMALL_CACHE_BITS);
        if (ok)
        {
            const uint32_t seeded = table.count;
            uint32_t* ids = (uint32_t*)fast_arena_alloc(&arena, names * sizeof(uint32_t), 64);
            FastIdentCacheSlot* before = (FastIdentCacheSlot*)fast_arena_alloc(&arena, ((uint64_t)1 << BUSTER_FIDENT_CACHE_BITS) * sizeof(FastIdentCacheSlot), 64);
            uint32_t growths = 0;
            ok = ids != NULL && before != NULL && seeded < 1024u && seeded + names < BUSTER_FIDENT_MAX_IDS;
            for (uint32_t i = 0; ok && i < names; i += 1)
            {
                char name[80] = { 0 };
                const uint32_t name_kind = (i + phase) % 3u;
                const uint32_t length = c_direct_test_growth_name(name, name_kind, i);
                uint8_t kind = 0;
                const uint32_t before_bits = table.cache_bits;
                const bool grows = table.count + 1u == table.cache_grow_at && before_bits <= BUSTER_FIDENT_CACHE_BITS;
                uint32_t held = 0;
                if (grows)
                {
                    memcpy(before, table.cache, ((size_t)1 << before_bits) * sizeof(FastIdentCacheSlot));
                }
                ids[i] = fast_ident_lookup(&table, (const uint8_t*)name, length, &kind);
                ok = ids[i] == seeded + i + 1u && kind == FTOK_IDENT;
                if (grows)
                {
                    // The cache grows as the names reach four times its slots, by two bits, and keeps every slot it had:
                    // each old key is at its hash's place in the new cache, but for one that the name that made it grow
                    // may have replaced, and that name is in the new cache too.
                    ok = ok && table.count == (BUSTER_FIDENT_CACHE_GROW_LOAD << before_bits) && table.cache != before &&
                         table.cache_bits == (before_bits + BUSTER_FIDENT_CACHE_GROW_BITS < BUSTER_FIDENT_CACHE_BITS ? before_bits + BUSTER_FIDENT_CACHE_GROW_BITS : BUSTER_FIDENT_CACHE_BITS);
                    uint32_t replaced = 0;
                    uint32_t moved = 0;
                    for (uint32_t slot = 0; ok && slot < (1u << before_bits); slot += 1)
                    {
                        if (before[slot].id != 0)
                        {
                            const uint64_t mix = before[slot].len > 8u ? BUSTER_FIDENT_CACHE_MIX(before[slot].w1, before[slot].len) : 0u;
                            const FastIdentCacheSlot* now = table.cache + (BUSTER_FIDENT_CACHE_HASH(before[slot].w0, mix) >> table.cache_shift);
                            const bool same = now->id == before[slot].id && now->w0 == before[slot].w0 && now->w1 == before[slot].w1 && now->len == before[slot].len && now->tok_kind == before[slot].tok_kind;
                            held += 1u;
                            moved += same;
                            replaced += !same && now->id == ids[i];
                            ok = same || now->id == ids[i];
                        }
                    }
                    uint32_t kept = 0;
                    for (uint32_t slot = 0; ok && slot < (1u << table.cache_bits); slot += 1)
                    {
                        kept += table.cache[slot].id != 0;
                    }
                    ok = ok && moved > (1u << before_bits) / 4u && replaced <= 1u && kept == held + 1u - replaced && c_direct_test_cache_consistent(&table, table.cache_bits);
                    growths += 1u;
                    kind_growths[name_kind] += 1u;
                }
                else
                {
                    ok = ok && table.cache_bits == before_bits;
                }
                ok = ok && table.cache_grow_at == (table.cache_bits < BUSTER_FIDENT_CACHE_BITS ? BUSTER_FIDENT_CACHE_GROW_LOAD << table.cache_bits : UINT32_MAX);
            }
            ok = ok && growths == 2u && c_direct_test_cache_consistent(&table, BUSTER_FIDENT_CACHE_BITS) && table.group_count > BUSTER_FIDENT_SMALL_GROUPS;
            // Every name again: the same id, from the cache or from the table.
            for (uint32_t i = 0; ok && i < names; i += 1)
            {
                char name[80] = { 0 };
                const uint32_t length = c_direct_test_growth_name(name, (i + phase) % 3u, i);
                uint8_t kind = 0;
                const uint32_t id = fast_ident_lookup(&table, (const uint8_t*)name, length, &kind);
                ok = id == ids[i] && kind == FTOK_IDENT;
            }
            ok = ok && table.count == seeded + names && c_direct_test_cache_consistent(&table, BUSTER_FIDENT_CACHE_BITS);
            // And the lookup finds them where the growth put them: with the table's control bytes cleared, a name its
            // slot holds is found in the cache, or would be interned anew under another id. The last name to reach a
            // slot holds it, so the names found fill most of the 16,384 slots (36,000 names: about 14,500).
            memset(table.ctrl, 0, (size_t)table.group_count * 64u);
            uint32_t cached = 0;
            for (uint32_t i = 0; ok && i < names; i += 1)
            {
                char name[80] = { 0 };
                const uint32_t length = c_direct_test_growth_name(name, (i + phase) % 3u, i);
                uint64_t first = 0;
                uint64_t last = 0;
                memcpy(&first, name, sizeof(first));
                first = length <= 8u ? _bzhi_u64(first, length * 8u) : first;
                if (length > 8u)
                {
                    memcpy(&last, name + length - 8u, sizeof(last));
                }
                const uint64_t mix = length > 8u ? BUSTER_FIDENT_CACHE_MIX(last, length) : 0u;
                if (table.cache[BUSTER_FIDENT_CACHE_HASH(first, mix) >> table.cache_shift].id == ids[i])
                {
                    uint8_t kind = 0;
                    cached += 1u;
                    ok = fast_ident_lookup(&table, (const uint8_t*)name, length, &kind) == ids[i];
                }
            }
            ok = ok && cached > (1u << BUSTER_FIDENT_CACHE_BITS) / 2u;
        }
        fast_arena_destroy(&arena);
    }
    // Of the six growths, a name of each length made two.
    ok = ok && kind_growths[0] == 2u && kind_growths[1] == 2u && kind_growths[2] == 2u;

    // Without room for a larger cache the small one stays, and is not asked to grow again.
    FastArena tight = { 0 };
    ok = ok && fast_arena_init(&tight, UINT64_C(4) * 1024u * 1024u, true);
    if (ok)
    {
        FastIdentTable stays;
        ok = fast_ident_init(&stays, &tight, true);
        tight.used = tight.reserved;
        const FastIdentCacheSlot* const cache = stays.cache;
        stays.count = stays.cache_grow_at;
        ok = ok && !fast_ident_cache_grow(&stays) && stays.cache == cache && stays.cache_grow_at == UINT32_MAX && stays.cache_bits == BUSTER_FIDENT_SMALL_CACHE_BITS;
    }
    fast_arena_destroy(&tight);

    // A unit of 72,000 identifier tokens gives the same kinds and ids from both sizes.
    char* text = (char*)malloc(text_size);
    uint8_t* kinds = (uint8_t*)malloc(2 * capacity);
    uint32_t* data = (uint32_t*)malloc(2 * capacity * sizeof(uint32_t));
    ok = ok && text != NULL && kinds != NULL && data != NULL;
    if (ok)
    {
        uint64_t length = 0;
        for (uint32_t pass = 0; pass < 2u; pass += 1)
        {
            for (uint32_t i = 0; i < names; i += 1)
            {
                length += (uint64_t)snprintf(text + length, 40, i % 3u == 2u ? "a_name_of_more_than_16_bytes_%u\n" : "n%u ", i);
            }
        }
        uint32_t small_count = 0;
        uint32_t full_count = 0;
        ok = c_direct_test_tokens_sized(text, length, kinds, data, capacity, &small_count, true) &&
             c_direct_test_tokens_sized(text, length, kinds + capacity, data + capacity, capacity, &full_count, false);
        ok = ok && small_count == 2u * names && full_count == small_count && memcmp(kinds, kinds + capacity, small_count) == 0 &&
             memcmp(data, data + capacity, (size_t)small_count * sizeof(uint32_t)) == 0;
        for (uint32_t i = 0; ok && i < names; i += 1)
        {
            ok = kinds[i] == FTOK_IDENT && data[i] == data[i + names];
        }
    }
    free(data);
    free(kinds);
    free(text);
    return ok;
}

// The kilobytes of this process's mappings that are advised against huge pages (`nh` in /proc/self/smaps), and its
// resident pages that are no file's (/proc/self/statm: a child's code pages become resident as it runs them).
static uint64_t c_direct_test_base_page_kilobytes(void)
{
    uint64_t total = 0;
    FILE* maps = fopen("/proc/self/smaps", "r");
    if (maps != NULL)
    {
        char line[1024];
        uint64_t size = 0;
        while (fgets(line, sizeof(line), maps) != NULL)
        {
            if (strncmp(line, "Size:", 5) == 0)
            {
                size = strtoull(line + 5, NULL, 10);
            }
            else if (strncmp(line, "VmFlags:", 8) == 0 && strstr(line, " nh") != NULL)
            {
                total += size;
            }
        }
        fclose(maps);
    }
    return total;
}

static uint64_t c_direct_test_resident_pages(void)
{
    uint64_t resident = 0;
    FILE* statm = fopen("/proc/self/statm", "r");
    if (statm != NULL)
    {
        unsigned long long size = 0;
        unsigned long long pages = 0;
        unsigned long long shared = 0;
        if (fscanf(statm, "%llu %llu %llu", &size, &pages, &shared) == 3 && pages >= shared)
        {
            resident = pages - shared;
        }
        fclose(statm);
    }
    return resident;
}

// One unit compiled in a child process that keeps its arenas (`release` false): `padding` bytes of comment, then 600
// declarations. Returns whether the compile's arenas were advised base pages as `small` says, and for a small unit
// whether the compile added under 150 resident pages (measured: 72 with the small tables, 194 with the full ones,
// whose 384 KB cache and 160 KB hash table 600 names touch all over).
static bool c_direct_test_unit_pages(uint64_t padding, bool small)
{
    char src_path[256];
    char obj_path[256];
    snprintf(src_path, sizeof(src_path), "/tmp/buster_direct_pages_%d.c", (int)getpid());
    snprintf(obj_path, sizeof(obj_path), "/tmp/buster_direct_pages_%d.o", (int)getpid());
    bool ok = false;
    FILE* source = fopen(src_path, "wb");
    if (source != NULL)
    {
        ok = true;
        if (padding != 0)
        {
            ok = fputs("/*", source) >= 0;
            for (uint64_t i = 0; ok && i < padding; i += 64)
            {
                ok = fputs("                                                               \n", source) >= 0;
            }
            ok = ok && fputs("*/\n", source) >= 0;
        }
        for (uint32_t i = 0; ok && i < 600u; i += 1)
        {
            ok = fprintf(source, "extern int declared_name_%u;\n", i) > 0;
        }
        ok = ok && fputs("int add(int a, int b) { return a + b + declared_name_7; }\n", source) >= 0;
        ok = fclose(source) == 0 && ok;
    }
    struct stat st;
    ok = ok && stat(src_path, &st) == 0 && ((uint64_t)st.st_size < BUSTER_FDIRECT_LARGE_UNIT_BYTES) == small && c_direct_unit_is_small(src_path) == small;
    if (ok)
    {
        fflush(NULL);
        const pid_t child = fork();
        if (child == 0)
        {
            static const char predefines[] = "#define __x86_64__ 1\n#define __LP64__ 1\n#define __STDC__ 1\n#define __STDC_VERSION__ 201710L\n";
            const CDirectRequest request = {
                .input_path = src_path,
                .output_path = obj_path,
                .predefines_text = predefines,
                .predefines_length = (uint32_t)(sizeof(predefines) - 1u),
                .pic = true,
                .unwind_tables = true,
                .quiet = true,
                .release = false,
            };
            const bool advice = access("/sys/kernel/mm/transparent_hugepage/enabled", F_OK) == 0;
            const uint64_t advised_before = c_direct_test_base_page_kilobytes();
            const uint64_t resident_before = c_direct_test_resident_pages();
            const int status = c_direct_try_compile(&request);
            const uint64_t resident = c_direct_test_resident_pages() - resident_before;
            const uint64_t advised = c_direct_test_base_page_kilobytes() - advised_before;
            const uint64_t arenas = (UINT64_C(32) + UINT64_C(6)) * 1024u;
            bool child_ok = status == 0;
            child_ok = child_ok && (!advice || advised == (small ? arenas : 0u));
            child_ok = child_ok && (!small || resident < 150u);
            if (!child_ok)
            {
                fprintf(stderr, "test-direct: a %s unit: status %d, %llu resident pages added, %llu KB advised base pages\n", small ? "small" : "large", status,
                        (unsigned long long)resident, (unsigned long long)advised);
            }
            fflush(NULL);
            _exit(child_ok ? 0 : 1);
        }
        int status = 0;
        ok = child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    unlink(src_path);
    unlink(obj_path);
    return ok;
}

// A small unit's start (#3592): a unit below BUSTER_FDIRECT_LARGE_UNIT_BYTES takes base pages and small tables, one at
// that size takes the pages the host gives and the full tables.
static bool c_direct_test_small_unit(void)
{
    // A sanitizer's runtime maps and advises memory of its own while the unit compiles.
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
    const bool counted = false;
#else
    const bool counted = true;
#endif
    return !counted || (c_direct_test_unit_pages(0, true) && c_direct_test_unit_pages(BUSTER_FDIRECT_LARGE_UNIT_BYTES, false));
}

// ---------------------------------------------------------------------------------------------------------------------
// The language modes (#3592): -std=c99, c11, c17 and c23, -trigraphs, -fno-asm and -w

// The trigraph scan at every offset of its blocks: each of the nine trigraphs, and "??" before a byte that makes none, at
// offsets 0 to 130 of texts that end right after it, a byte later and 70 bytes later, so that its three bytes meet both
// loads' block ends and the last block's masks; none past a text's end, however close, at the end of a page whose next
// page is unmapped, where a load not masked to the text faults.
static bool c_direct_test_trigraph_scan(void)
{
    static const char thirds[] = "=(/)'<!>-";
    static const char others[] = "?a0 \n\"\\*.#`~";
    const uint32_t third_count = (uint32_t)(sizeof(thirds) - 1u);
    const uint32_t other_count = (uint32_t)(sizeof(others) - 1u);
    uint8_t buffer[256];
    bool ok = true;
    for (uint32_t at = 0; ok && at <= 130u; at += 1)
    {
        for (uint32_t tail = 0; ok && tail < 3u; tail += 1)
        {
            const uint32_t extra = tail == 0 ? 0u : tail == 1 ? 1u : 70u;
            for (uint32_t c = 0; ok && c < third_count + other_count; c += 1)
            {
                const bool trigraph = c < third_count;
                memset(buffer, 'x', sizeof(buffer));
                buffer[at] = '?';
                buffer[at + 1u] = '?';
                buffer[at + 2u] = (uint8_t)(trigraph ? thirds[c] : others[c - third_count]);
                ok = fast_has_trigraph(buffer, at + 3u + extra) == trigraph && !fast_has_trigraph(buffer, at + 2u) && !fast_has_trigraph(buffer, at + 1u);
            }
        }
    }
    // "???" before "=" holds a trigraph a byte in, and "?" "?=" split by any byte none, nor a lone '?' two bytes
    // before one of the nine.
    ok = ok && fast_has_trigraph((const uint8_t*)"x??" "?=", 5) && !fast_has_trigraph((const uint8_t*)"?x?=?", 5) && !fast_has_trigraph((const uint8_t*)"", 0) &&
         !fast_has_trigraph((const uint8_t*)"?x=", 3) && !fast_has_trigraph((const uint8_t*)"x?x(x", 5);
    const long page = sysconf(_SC_PAGESIZE);
    uint8_t* pages = page > 0 ? (uint8_t*)mmap(NULL, (size_t)page * 2u, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : (uint8_t*)MAP_FAILED;
    ok = ok && pages != (uint8_t*)MAP_FAILED && mprotect(pages + page, (size_t)page, PROT_NONE) == 0;
    for (uint32_t length = 1; ok && length <= 130u; length += 1)
    {
        uint8_t* text = pages + page - length;
        memset(text, '?', length);
        ok = !fast_has_trigraph(text, length);
        text[length - 1u] = '=';
        ok = ok && fast_has_trigraph(text, length) == (length >= 3u);
    }
    if (pages != (uint8_t*)MAP_FAILED)
    {
        munmap(pages, (size_t)page * 2u);
    }
    return ok;
}

// A text's tokens in a mode against `expected`'s kinds (identifiers' ids aside): `, ## __VA_ARGS__` as clang has it.
static bool c_direct_test_mode_expands(const char* text, const char* expected, CDirectTestMode mode)
{
    enum { capacity = 256 };
    uint8_t kinds[capacity];
    uint32_t data[capacity];
    uint8_t want[capacity];
    uint32_t want_data[capacity];
    uint32_t count = 0;
    uint32_t want_count = 0;
    bool ok = c_direct_test_tokens_mode(text, strlen(text), kinds, data, capacity, &count, false, mode) &&
              c_direct_test_tokens_mode(expected, strlen(expected), want, want_data, capacity, &want_count, false, mode);
    return ok && count == want_count && memcmp(kinds, want, count) == 0;
}

// asm and typeof by mode: keywords with GNU's, identifiers without them but typeof in C23; their __ spellings always.
static bool c_direct_test_mode_keywords(CDirectTestMode mode, const uint8_t* expected)
{
    static const char text[] = "asm typeof __asm__ __typeof__ __asm __typeof";
    uint8_t kinds[16];
    uint32_t data[16];
    uint32_t count = 0;
    const bool ok = c_direct_test_tokens_mode(text, sizeof(text) - 1u, kinds, data, 16u, &count, false, mode);
    return ok && count == 6u && memcmp(kinds, expected, 6) == 0;
}

static bool c_direct_test_language_tokens(void)
{
    static const char macros[] =
        "#define F(x, ...) f(x, ## __VA_ARGS__)\n"
        "#define G(...) g(0, ## __VA_ARGS__)\n"
        "#define H(x, ...) h(1, x ## __VA_ARGS__)\n"
        "#define N(x, rest...) n(x, ## rest)\n"
        "F(1) F(1,) F(1, 2) F(1, 2, 3) G() G( ) G(2) H() H(3) H(,4) N(1) N(1,) N(1, 2)\n";
    // clang 23's -E: a left-out argument drops the comma, an empty one doesn't, one with tokens follows it unpasted, and
    // without GNU's extensions G() keeps it.
    static const char gnu[] = "f(1) f(1,) f(1, 2) f(1, 2, 3) g(0) g(0) g(0,2) h(1) h(1, 3) h(1, 4) n(1) n(1,) n(1, 2)";
    static const char strict[] = "f(1) f(1,) f(1, 2) f(1, 2, 3) g(0,) g(0,) g(0,2) h(1) h(1, 3) h(1, 4) n(1) n(1,) n(1, 2)";
    const CDirectTestMode gnu_mode = { 0 };
    const CDirectTestMode c17 = { .strict_iso = true, .no_gnu_keywords = true, .trigraphs = true };
    const CDirectTestMode c23 = { .c23 = true, .strict_iso = true, .no_gnu_keywords = true };
    const CDirectTestMode gnu_no_asm = { .no_gnu_keywords = true };
    bool ok = c_direct_test_mode_expands(macros, gnu, gnu_mode) && c_direct_test_mode_expands(macros, strict, c17) &&
              c_direct_test_mode_expands(macros, strict, c23) && c_direct_test_mode_expands(macros, gnu, gnu_no_asm);
    const uint8_t all[6] = { FTOK_KW_ASM, FTOK_KW_TYPEOF, FTOK_KW_ASM, FTOK_KW_TYPEOF, FTOK_KW_ASM, FTOK_KW_TYPEOF };
    const uint8_t none[6] = { FTOK_IDENT, FTOK_IDENT, FTOK_KW_ASM, FTOK_KW_TYPEOF, FTOK_KW_ASM, FTOK_KW_TYPEOF };
    const uint8_t typeof_only[6] = { FTOK_IDENT, FTOK_KW_TYPEOF, FTOK_KW_ASM, FTOK_KW_TYPEOF, FTOK_KW_ASM, FTOK_KW_TYPEOF };
    ok = ok && c_direct_test_mode_keywords(gnu_mode, all) && c_direct_test_mode_keywords(c17, none) && c_direct_test_mode_keywords(gnu_no_asm, none) &&
         c_direct_test_mode_keywords(c23, typeof_only) && c_direct_test_mode_keywords((CDirectTestMode){ .c23 = true }, all);
    return ok;
}

// The modes' compiles: a trigraph anywhere, in the unit or a header, leaves the direct path where trigraphs are replaced;
// asm and typeof name objects without GNU's keywords; F(1,) keeps its comma, so `f(1,)` leaves as the error it is; and a
// main that doesn't return int leaves without GNU's extensions, at file scope or in a block, but not a typedef's name.
static bool c_direct_test_language_compiles(const char* obj_path)
{
    const CDirectTestMode gnu = { 0 };
    const CDirectTestMode c17 = { .strict_iso = true, .no_gnu_keywords = true, .trigraphs = true };
    const CDirectTestMode c23 = { .c23 = true, .strict_iso = true, .no_gnu_keywords = true };
    static const char in_string[] = "int g(void) { return (int)sizeof(\"?" "?=\"); }\n";
    static const char in_comment[] = "/* a comment ?" "?/\n */\nint g(void) { return 1; }\n";
    static const char not_one[] = "int g(void) { return (int)sizeof(\"??a ?? =\"); }\n";
    bool ok = c_direct_test_compile_mode(in_string, obj_path, 1, NULL, NULL, c17) && c_direct_test_compile_mode(in_string, obj_path, 0, NULL, NULL, gnu) &&
              c_direct_test_compile_mode(in_string, obj_path, 0, NULL, NULL, c23) && c_direct_test_compile_mode(in_comment, obj_path, 1, NULL, NULL, c17) &&
              c_direct_test_compile_mode(not_one, obj_path, 0, NULL, NULL, c17);
    char header[256];
    char source[512];
    snprintf(header, sizeof(header), "/tmp/buster_direct_trigraph_%d.h", (int)getpid());
    snprintf(source, sizeof(source), "#include \"%s\"\nint g(void) { return G; }\n", header);
    FILE* file = fopen(header, "wb");
    ok = ok && file != NULL && fputs("/* ?" "?! */\n#define G 2\n", file) >= 0;
    ok = file != NULL && fclose(file) == 0 && ok;
    ok = ok && c_direct_test_compile_mode(source, obj_path, 1, NULL, NULL, c17) && c_direct_test_compile_mode(source, obj_path, 0, NULL, NULL, gnu);
    unlink(header);
    static const char keywords[] = "int typeof = 3;\nint asm(int x) { return x + typeof; }\nint g(void) { return asm(4); }\n";
    ok = ok && c_direct_test_compile_mode(keywords, obj_path, 0, NULL, NULL, c17) && c_direct_test_compile_mode(keywords, obj_path, 1, NULL, NULL, gnu) &&
         c_direct_test_compile_mode(keywords, obj_path, 1, NULL, NULL, c23);
    // `__VA_ARGS__ ## __VA_ARGS__` of a comma is two commas, as in clang, and they declare nothing; nor do the other
    // declarators without a name.
    static const char commas[] = "#define G(...) __VA_ARGS__ ## __VA_ARGS__\nG(,)\nint g(void) { return 0; }\n";
    static const char* const nameless[] = {
        "int , x;\n", "int x, ;\n", "int (*)(void);\n", "int *;\n", "int g(void) { return 0; } ,\nint h(void) { return 1; }\n",
        "int g(void) { int , x; return 0; }\n", "int g(void) { int x, ; return 0; }\n", "int g(void) { int *; return 0; }\n",
    };
    ok = ok && c_direct_test_compile_mode(commas, obj_path, 1, NULL, NULL, gnu);
    for (uint32_t i = 0; ok && i < (uint32_t)(sizeof(nameless) / sizeof(nameless[0])); i += 1)
    {
        ok = c_direct_test_compile_mode(nameless[i], obj_path, 1, NULL, NULL, gnu);
    }
    static const char named[] = "int a, b, *c, (*d)(void), e[2];\nint g(void) { int x = 1, y, *z = &x; y = *z; return a + b + y + e[0]; }\n";
    ok = ok && c_direct_test_compile_mode(named, obj_path, 0, NULL, NULL, gnu);
    static const char empty_argument[] = "#define F(x, ...) f(x, ## __VA_ARGS__)\nint f(int a, ...) { return a; }\nint g(void) { return F(1,); }\n";
    static const char left_out[] = "#define F(x, ...) f(x, ## __VA_ARGS__)\nint f(int a, ...) { return a; }\nint g(void) { return F(1); }\n";
    ok = ok && c_direct_test_compile_mode(empty_argument, obj_path, 1, NULL, NULL, gnu) && c_direct_test_compile_mode(left_out, obj_path, 0, NULL, NULL, gnu) &&
         c_direct_test_compile_mode(left_out, obj_path, 0, NULL, NULL, c17);
    static const char void_main[] = "void main(void) { }\n";
    static const char block_main[] = "int f(void) { void main(void); return 0; }\n";
    static const char long_main[] = "long main(void) { return 0; }\n";
    static const char int_main[] = "int main(void) { return 0; }\n";
    static const char typedef_main[] = "typedef void main(void);\nint g(void) { return 0; }\n";
    ok = ok && c_direct_test_compile_mode(void_main, obj_path, 1, NULL, NULL, c17) && c_direct_test_compile_mode(void_main, obj_path, 1, NULL, NULL, c23) &&
         c_direct_test_compile_mode(void_main, obj_path, 0, NULL, NULL, gnu) && c_direct_test_compile_mode(block_main, obj_path, 1, NULL, NULL, c17) &&
         c_direct_test_compile_mode(long_main, obj_path, 1, NULL, NULL, c17) && c_direct_test_compile_mode(int_main, obj_path, 0, NULL, NULL, c17) &&
         c_direct_test_compile_mode(typedef_main, obj_path, 0, NULL, NULL, c17);
    return ok;
}

// The driver's admission (#3592): `-c` of a unit by this executable in a child, without SOURCE_DATE_EPOCH and
// BUSTER_NO_DIRECT, which keep any unit off the direct path; 1 where the direct path wrote the object, 0 where the full
// pipeline did, -1 where neither did.
static int c_direct_test_driver_path(const char* source_path, const char* object_path, const char* const* options, uint32_t option_count)
{
    char executable[4096];
    const ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1u);
    int result = -1;
    if (length > 0)
    {
        executable[length] = '\0';
        const char* argv[16];
        uint32_t argc = 0;
        argv[argc++] = executable;
        argv[argc++] = "-c";
        for (uint32_t i = 0; i < option_count && argc < 12u; i += 1)
        {
            argv[argc++] = options[i];
        }
        argv[argc++] = source_path;
        argv[argc++] = "-o";
        argv[argc++] = object_path;
        argv[argc] = NULL;
        unlink(object_path);
        fflush(NULL);
        const pid_t child = fork();
        if (child == 0)
        {
            unsetenv("SOURCE_DATE_EPOCH");
            unsetenv("BUSTER_NO_DIRECT");
            const int null = open("/dev/null", O_WRONLY);
            if (null >= 0)
            {
                dup2(null, 1);
                dup2(null, 2);
            }
            execv(executable, (char* const*)argv);
            _exit(127);
        }
        int status = 0;
        const bool exited = child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
        FILE* object = exited ? fopen(object_path, "rb") : NULL;
        if (object != NULL)
        {
            static char bytes[1u << 16];
            const size_t read = fread(bytes, 1, sizeof(bytes), object);
            fclose(object);
            result = memmem(bytes, read, "buster direct", 13) != NULL ? 1 : 0;
        }
        unlink(object_path);
    }
    return result;
}

static bool c_direct_test_admission(void)
{
    char plain[256];
    char trigraph[256];
    char keyword[256];
    char comma[256];
    char main_void[256];
    char raw[256];
    char object[256];
    snprintf(plain, sizeof(plain), "/tmp/buster_direct_admit_%d.c", (int)getpid());
    snprintf(trigraph, sizeof(trigraph), "/tmp/buster_direct_admit_tri_%d.c", (int)getpid());
    snprintf(keyword, sizeof(keyword), "/tmp/buster_direct_admit_asm_%d.c", (int)getpid());
    snprintf(comma, sizeof(comma), "/tmp/buster_direct_admit_comma_%d.c", (int)getpid());
    snprintf(main_void, sizeof(main_void), "/tmp/buster_direct_admit_main_%d.c", (int)getpid());
    snprintf(raw, sizeof(raw), "/tmp/buster_direct_admit_raw_%d.c", (int)getpid());
    snprintf(object, sizeof(object), "/tmp/buster_direct_admit_%d.o", (int)getpid());
    static const char plain_text[] = "int add(int a, int b) { return a + b; }\n";
    static const char trigraph_text[] = "/* ?" "?= */\nint add(int a, int b) { return a + b; }\n";
    static const char keyword_text[] = "int asm = 1;\nint get(void) { return asm; }\n";
    static const char comma_text[] = "#define G(...) f(0, ## __VA_ARGS__)\nint f(int a, ...) { return a; }\nint get(void) { return G(); }\n";
    static const char main_text[] = "void main(void) { }\n";
    static const char raw_text[] = "#if 0\nR\"(\n#endif\nint y;\n#if 0\n)\"\n#endif\nint x;\n";
    const char* const paths[6] = { plain, trigraph, keyword, comma, main_void, raw };
    const char* const texts[6] = { plain_text, trigraph_text, keyword_text, comma_text, main_text, raw_text };
    bool ok = true;
    for (uint32_t i = 0; ok && i < 6u; i += 1)
    {
        FILE* file = fopen(paths[i], "wb");
        ok = file != NULL && fputs(texts[i], file) >= 0;
        ok = file != NULL && fclose(file) == 0 && ok;
    }
    static const char* const c17[] = { "-std=c17" };
    static const char* const c99[] = { "-std=c99" };
    static const char* const c11_w[] = { "-std=c11", "-w" };
    static const char* const w[] = { "-w" };
    static const char* const w_twice[] = { "-w", "-w" };
    static const char* const w_wall[] = { "-w", "-Wall" };
    static const char* const gnu17[] = { "-std=gnu17" };
    static const char* const gnu17_trigraphs[] = { "-std=gnu17", "-trigraphs" };
    static const char* const gnu17_no_asm[] = { "-std=gnu17", "-fno-asm" };
    ok = ok && c_direct_test_driver_path(plain, object, c17, 1) == 1 && c_direct_test_driver_path(plain, object, c99, 1) == 1 &&
         c_direct_test_driver_path(plain, object, c11_w, 2) == 1 && c_direct_test_driver_path(plain, object, w, 1) == 1 &&
         c_direct_test_driver_path(plain, object, w_twice, 2) == 1 && c_direct_test_driver_path(plain, object, w_wall, 2) == 0;
    ok = ok && c_direct_test_driver_path(trigraph, object, c17, 1) == 0 && c_direct_test_driver_path(trigraph, object, gnu17, 1) == 1 &&
         c_direct_test_driver_path(trigraph, object, gnu17_trigraphs, 2) == 0;
    ok = ok && c_direct_test_driver_path(keyword, object, c17, 1) == 1 && c_direct_test_driver_path(keyword, object, gnu17_no_asm, 2) == 1 &&
         c_direct_test_driver_path(keyword, object, gnu17, 1) == -1;
    // G() is g(0) in GNU's modes and the error g(0,) in -std=c17's, as is a main that returns void.
    ok = ok && c_direct_test_driver_path(comma, object, gnu17, 1) == 1 && c_direct_test_driver_path(comma, object, c17, 1) == -1 &&
         c_direct_test_driver_path(main_void, object, gnu17, 1) == 1 && c_direct_test_driver_path(main_void, object, c17, 1) == -1;
    // A raw string literal in a skipped group (#3600): GNU's modes have them, so the full pipeline compiles the unit;
    // -std=c17's don't, so the direct path does.
    ok = ok && c_direct_test_driver_path(raw, object, gnu17, 1) == 0 && c_direct_test_driver_path(raw, object, c17, 1) == 1;
    for (uint32_t i = 0; i < 6u; i += 1)
    {
        unlink(paths[i]);
    }
    return ok;
}

static bool c_direct_test_classes(void)
{
    bool ok = true;
    for (uint32_t c = 0; c < 128u; c += 1)
    {
        const bool space = c == ' ' || (c >= 9u && c <= 13u);
        const bool run = (fast_char_class[c] & 1u) != 0;
        const bool decided = c == '/' || c == '#' || c == '"' || c == '\'' || c == '\\' || (fast_ascii_to_kind[c] == 0 && !space);
        const uint32_t expected = (space ? BUSTER_FCLASS_SPACE : 0u) | (c == '\n' ? BUSTER_FCLASS_NEWLINE : 0u) |
                                  (decided ? BUSTER_FCLASS_SPECIAL : 0u) | (run ? BUSTER_FCLASS_RUN : 0u) |
                                  (c == '.' ? BUSTER_FCLASS_DOT : 0u);
        ok = ok && fast_lex_class[c] == expected;
        // The class of the byte after another: a punctuator's second character, a run's, what makes a '.' a number's.
        const uint32_t expected_next = fast_pair_second[c] | (run ? BUSTER_FCLASS_RUN : 0u) |
                                       (c == '.' || (c >= '0' && c <= '9') ? BUSTER_FCLASS_DOT : 0u);
        ok = ok && fast_lex_next_class[c] == expected_next && fast_pair_second[c] < 8u;
        // A run's character has an identifier's or a number's kind, and every other character with a kind is a punctuator.
        ok = ok && run == (fast_ascii_to_kind[c] == FTOK_IDENT || fast_ascii_to_kind[c] == FTOK_INT_LIT);
        ok = ok && (fast_ascii_to_kind[c] <= FTOK_STR_LIT || fast_punct_len[fast_ascii_to_kind[c]] == 1u);
    }
    return ok;
}

static bool c_direct_test_lexer(void)
{
    enum { capacity = 1024, buffer_size = 65536 };
    static const char* const separators[] = { " ", "\n", "/**/", " \t\n ", "/* a comment of some length, with a ' and a \" */ ", "\n\n// a line comment\n" };
    char* const buffer = (char*)malloc(buffer_size);
    uint8_t* const kinds = (uint8_t*)malloc(capacity);
    uint32_t* const data = (uint32_t*)malloc(capacity * sizeof(uint32_t));
    uint32_t* const first_data = (uint32_t*)malloc(capacity * sizeof(uint32_t));
    const uint32_t cases = (uint32_t)(sizeof(c_direct_lex_cases) / sizeof(c_direct_lex_cases[0]));
    bool ok = buffer != NULL && kinds != NULL && data != NULL && first_data != NULL && c_direct_test_classes();
    bool have_first = false;
    for (uint32_t s = 0; ok && s < sizeof(separators) / sizeof(separators[0]); s += 1)
    {
        for (uint32_t shift = 0; ok && shift < 131u; shift += 1)
        {
            uint32_t count = 0;
            const uint32_t length = c_direct_test_layout(buffer, shift, separators[s]);
            ok = length < buffer_size && c_direct_test_tokens(buffer, length, kinds, data, capacity, &count);
            if (!ok)
            {
                fprintf(stderr, "test-direct: lexer: no tokens, separator %u, shift %u\n", s, shift);
            }
            uint32_t at = 0;
            for (uint32_t i = 0; ok && i < cases; i += 1)
            {
                for (uint32_t k = 0; ok && k < c_direct_lex_cases[i].count; k += 1)
                {
                    const uint8_t expected = c_direct_lex_cases[i].kinds[k];
                    ok = at < count && (expected == BUSTER_DIRECT_TEST_NUMBER ? c_direct_test_kind_is_number(kinds[at]) : kinds[at] == expected);
                    if (!ok)
                    {
                        fprintf(stderr, "test-direct: lexer: case \"%s\" token %u, separator %u, shift %u\n", c_direct_lex_cases[i].text, k, s, shift);
                    }
                    at += 1u;
                }
            }
            if (ok && at != count)
            {
                ok = false;
                fprintf(stderr, "test-direct: lexer: %u tokens for %u, separator %u, shift %u\n", count, at, s, shift);
            }
            if (ok && !have_first)
            {
                memcpy(first_data, data, (size_t)count * sizeof(uint32_t));
                have_first = true;
            }
            else if (ok && memcmp(first_data, data, (size_t)count * sizeof(uint32_t)) != 0)
            {
                ok = false;
                fprintf(stderr, "test-direct: lexer: the tokens' data differ, separator %u, shift %u\n", s, shift);
            }
        }
    }
    // What the direct path leaves to the full compiler must be left at every offset too: an identifier a backslash and
    // a newline splice, a digit separator, and (#3577) a char16_t constant, whose type isn't int, and a number the full
    // pipeline refuses (`1..2`: an invalid suffix).
    for (uint32_t shift = 0; ok && shift < 131u; shift += 1)
    {
        static const char* const refused[] = { "ab\\\ncd", "1'000", "u'x'", "1..2" };
        for (uint32_t r = 0; ok && r < sizeof(refused) / sizeof(refused[0]); r += 1)
        {
            uint32_t count = 0;
            const uint32_t text_length = (uint32_t)strlen(refused[r]);
            memset(buffer, ' ', shift);
            memcpy(buffer + shift, refused[r], text_length);
            memset(buffer + shift + text_length, ' ', 100);
            buffer[shift + text_length + 100u] = '\n';
            ok = !c_direct_test_tokens(buffer, shift + text_length + 101u, kinds, data, capacity, &count);
            if (!ok)
            {
                fprintf(stderr, "test-direct: lexer: refused text %u was taken, shift %u\n", r, shift);
            }
        }
    }
    if (ok)
    {
        // A window's tokens start within 64 KB of its first byte: a comment that ends near that distance, and a stretch
        // of blanks, a literal and a comment longer than it between tokens, which go on in the next window.
        enum { long_size = 4 * 70000 + 400 };
        char* const text = (char*)malloc(long_size);
        uint32_t count = 0;
        uint32_t length = 0;
        ok = text != NULL;
        if (ok)
        {
            memcpy(text, "a /*", 4);
            length = 4u;
            // The comment ends just short of 64 KB, so that the tokens after it are past it.
            memset(text + length, 'x', 65530);
            length += 65530u;
            memcpy(text + length, "*/ b + c", 8);
            length += 8u;
            memset(text + length, ' ', 70000);
            length += 70000u;
            memcpy(text + length, "d \"", 3);
            length += 3u;
            memset(text + length, 'z', 70000);
            length += 70000u;
            memcpy(text + length, "\" e /*", 6);
            length += 6u;
            memset(text + length, '\n', 70000);
            length += 70000u;
            memcpy(text + length, "*/ b", 4);
            length += 4u;
            memset(text + length, ' ', 100);
            length += 100u;
            text[length++] = '\n';
            ok = c_direct_test_tokens(text, length, kinds, data, capacity, &count) && count == 8u && kinds[0] == FTOK_IDENT &&
                 kinds[1] == FTOK_IDENT && kinds[2] == FTOK_PLUS && kinds[3] == FTOK_IDENT && kinds[4] == FTOK_IDENT &&
                 kinds[5] == FTOK_STR_LIT && kinds[6] == FTOK_IDENT && kinds[7] == FTOK_IDENT && data[7] == data[1] && data[1] != data[0];
        }
        if (!ok)
        {
            fprintf(stderr, "test-direct: lexer: tokens more than 64 KB apart\n");
        }
        free(text);
    }
    if (ok)
    {
        // Two identifiers that differ in their last byte alone, past the 255 a window's token holds a length for.
        uint32_t count = 0;
        uint32_t length = 0;
        for (uint32_t i = 0; i < 3u; i += 1)
        {
            memset(buffer + length, 'x', 300);
            buffer[length + 300u] = i == 1u ? 'b' : 'a';
            buffer[length + 301u] = ' ';
            length += 302u;
        }
        memset(buffer + length, ' ', 100);
        buffer[length + 100u] = '\n';
        length += 101u;
        ok = c_direct_test_tokens(buffer, length, kinds, data, capacity, &count) && count == 3u && kinds[0] == FTOK_IDENT &&
             data[0] == data[2] && data[0] != data[1];
        if (!ok)
        {
            fprintf(stderr, "test-direct: lexer: identifiers of 301 bytes\n");
        }
    }
    free(first_data);
    free(data);
    free(kinds);
    free(buffer);
    return ok;
}

// The macro calls' and the directives' tests (#3584). The directives' lines, the calls' arguments from the window and
// from an expansion's frames are read in bulk, and a token at a time where the window can't give them whole; what
// comes out must not depend on which. A case is a text and its output rendered (c_direct_test_render), each laid out at
// every offset of a block with each separator at its '@'s (and the defines' at theirs): a '$' in the rendering is a
// space where the uses' separator is text and nothing where it is empty, a '%' the same for the defines'. Then a call
// at each place across a window's end, and texts whose __LINE__ fixes where a reader leaves its line.
typedef struct CDirectMacroCase CDirectMacroCase;
struct CDirectMacroCase
{
    const char* text;
    const char* rendered;
};

// Strings longer than the window's length byte holds (fast_pp_window_convert refuses them).
#define BUSTER_DIRECT_TEST_LONG_X "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" \
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" \
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
#define BUSTER_DIRECT_TEST_LONG_Y "yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy" \
    "yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy" \
    "yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy"

static const char* const c_direct_macro_defines[] = {
    "#define F(a,@b)@a@+@b",
    "#define G(x)@F(x,@1)",
    "#define ID(x)@x",
    "#define S(x)@#x",
    "#define XS(x)@S(x)",
    "#define C(a,@b)@a@##@b",
    "#define BODY @a@+@b",
    "#define E0()@z",
    "#define E1(a)@[a]",
    "#define E2(a,@b)@[a@|@b]",
    "#define V(...)@f(__VA_ARGS__)",
    "#define W(a,@...)@g(a,@##@__VA_ARGS__)",
    "#define NV(a,@rest...)@h(a,@rest)",
    "#define P3(a,@b,@c)@c @b @a",
    "#define K(int)@int@*@2",
    "#define OPEN @F(",
    "#define FN @F",
    "#define TWICE(x)@x @x",
    "#define LONGS @\"" BUSTER_DIRECT_TEST_LONG_Y "\"",
    "#define NUMS @1.5@+@2e+3@+@0x1p-3",
    "#define CHARS @'a'@'\\n'",
    "#define MIX @x->y@<<=@z@...@\"s\"",
    "#define LONGS2 @\"" BUSTER_DIRECT_TEST_LONG_Y "\"@+@1",
    "#define WRAP(x)@[x]",
    "#define CAT3(a,@b)@z a##b",
    "#define NESTED @F((1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1),@2)",
};

static const CDirectMacroCase c_direct_macro_cases[] = {
    { "F(1,@2)", "1 + 2" },
    { "F(@a@,@b@)", "a + b" },
    { "F(F(1,2),@3)", "1 + 2 + 3" },
    { "F((1,@2),@(3))", "( 1 , 2 ) + ( 3 )" },
    { "G(2)", "2 + 1" },
    { "G(G(5))", "5 + 1 + 1" },
    { "ID(ID(ID(7)))", "7" },
    { "S(@a@+@b@)", "\"a$+$b\"" },
    { "S(@\"q\"@)", "\"\"q\"\"" },
    { "S(@F(1,@2)@)", "\"F(1,$2)\"" },
    { "XS(BODY)", "\"a%+%b\"" },
    { "XS(F(x,@y))", "\"x%+%y\"" },
    { "C(x,@1)", "x1" },
    { "C(,@y)", "y" },
    { "C(x,@)", "x" },
    { "C(<,@<=)", "<<=" },
    { "E0()", "z" },
    { "E0(@)", "z" },
    { "E1()", "[ ]" },
    { "E2(,)", "[ | ]" },
    { "E2(,@y)", "[ | y ]" },
    { "V()", "f ( )" },
    { "V(1)", "f ( 1 )" },
    { "V(1,@2,@3)", "f ( 1 , 2 , 3 )" },
    { "W(1)", "g ( 1 )" },
    { "NV(1,@2,@3)", "h ( 1 , 2 , 3 )" },
    { "P3(1,@2,@3)", "3 2 1" },
    { "K(5)", "5 * 2" },
    { "OPEN @1,@2)", "1 + 2" },
    { "FN@(3,@4)", "3 + 4" },
    { "TWICE(F(1,@2))", "1 + 2 1 + 2" },
    { "F(1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1,@2)", "1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 2" },
    { "F(1.5,@2e+3)", "1.5 + 2000" },
    { "F(0x1p-3,@'a')", "0.125 + 97" },
    { "ID(\"" BUSTER_DIRECT_TEST_LONG_X "\")", "\"" BUSTER_DIRECT_TEST_LONG_X "\"" },
    { "LONGS", "\"" BUSTER_DIRECT_TEST_LONG_Y "\"" },
    { "NUMS", "1.5 + 2000 + 0.125" },
    { "CHARS", "97 10" },
    { "MIX", "x -> y <<= z ... \"s\"" },
    { "F(@L'x',@2)", "120 + 2" },
    { "F(a // c\n,@b)", "a + b" },
    // '#' and '##' take their operand as written: it is not prescanned (no __COUNTER__ is spent there, no bad call
    // bails out). An argument's first token takes its parameter's space, and a pasted token its left operand's.
    { "S(__COUNTER__)@C(__COUNTER__ x,)@__COUNTER__", "\"__COUNTER__\" 0 x 1" },
    { "S(@ID(a,@b)@)", "\"ID(a,$b)\"" },
    { "XS(WRAP(@a))", "\"[a]\"" },
    { "XS(CAT3(@x,1))", "\"z x1\"" },
    { "S(@\"" BUSTER_DIRECT_TEST_LONG_X "\"@+@1@)", "\"\"" BUSTER_DIRECT_TEST_LONG_X "\"$+$1\"" },
    { "XS(LONGS2)", "\"\"" BUSTER_DIRECT_TEST_LONG_Y "\"%+%1\"" },
    { "F((1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1),@2)", "( 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 ) + 2" },
    { "NESTED", "( 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 ) + 2" },
};

// The output of a preprocessor run, a token a word between single spaces: a name as it is spelled, an integer by its
// value, a float with %g, a character constant by its code, a string's bytes between quotes, a punctuator as it is
// spelled. False where the rendering doesn't fit.
static bool c_direct_test_render(const FastPP* pp, char* out, uint32_t capacity)
{
    uint32_t n = 0;
    bool ok = true;
    for (uint32_t i = 0; ok && i < pp->out_count; i += 1)
    {
        const uint8_t kind = pp->out_kind[i];
        const uint32_t data = pp->out_data[i];
        char word[1024];
        uint32_t length = 0;
        if (kind == FTOK_INT_LIT || kind == FTOK_INT_U32 || kind == FTOK_CHAR_LIT)
        {
            length = (uint32_t)snprintf(word, sizeof(word), "%u", data);
        }
        else if (kind == FTOK_INT_I64 || kind == FTOK_INT_U64)
        {
            length = (uint32_t)snprintf(word, sizeof(word), "%llu", (unsigned long long)pp->num_pool[data]);
        }
        else if (kind == FTOK_FLOAT_LIT || kind == FTOK_FLOAT80)
        {
            double value = 0.0;
            memcpy(&value, pp->num_pool + data, sizeof(value));
            length = (uint32_t)snprintf(word, sizeof(word), "%g", value);
        }
        else if (kind == FTOK_STR_LIT)
        {
            const FastLitSlice slice = pp->lit_slices[data];
            ok = slice.length + 2u < sizeof(word);
            if (ok)
            {
                word[0] = '"';
                memcpy(word + 1, pp->lit_bytes + slice.offset, slice.length);
                word[slice.length + 1u] = '"';
                length = slice.length + 2u;
            }
        }
        else
        {
            const FastTok tok = fast_tok_make(kind, 0, 0, data);
            length = fast_tok_to_text(pp, tok, word, sizeof(word));
        }
        ok = ok && length != 0 && n + length + 2u < capacity;
        if (ok)
        {
            if (n != 0)
            {
                out[n++] = ' ';
            }
            memcpy(out + n, word, length);
            n += length;
        }
    }
    if (n < capacity)
    {
        out[n] = '\0';
    }
    return ok && n < capacity;
}

// The rendered output of preprocessing `text`; false where the preprocessor bails out.
static bool c_direct_test_rendered(const char* text, uint64_t length, char* out, uint32_t capacity)
{
    FastArena arena = { 0 };
    FastArena scratch = { 0 };
    bool ok = fast_arena_init(&arena, UINT64_C(32) * 1024u * 1024u, true) && fast_arena_init(&scratch, UINT64_C(6) * 1024u * 1024u, true);
    if (ok)
    {
        FastPP* pp = (FastPP*)fast_arena_alloc(&scratch, sizeof(FastPP), 64);
        ok = pp != NULL && fast_pp_init(pp, &arena, &scratch, false) && fast_pp_run_buffer(pp, "<test>", text, length) &&
             c_direct_test_render(pp, out, capacity);
        if (!ok && pp != NULL && pp->bailout)
        {
            fprintf(stderr, "test-direct: macros: the preprocessor bailed out: %s\n", pp->bailout_reason);
        }
    }
    fast_arena_destroy(&scratch);
    fast_arena_destroy(&arena);
    return ok;
}

// `text` with each '@' replaced by `separator`, appended at buffer[*io_length].
static void c_direct_test_put(char* buffer, uint32_t* io_length, const char* text, const char* separator)
{
    uint32_t n = *io_length;
    for (const char* c = text; *c != '\0'; c += 1)
    {
        if (*c == '@')
        {
            const uint32_t separator_length = (uint32_t)strlen(separator);
            memcpy(buffer + n, separator, separator_length);
            n += separator_length;
        }
        else
        {
            buffer[n++] = *c;
        }
    }
    *io_length = n;
}

static bool c_direct_test_macros(void)
{
    enum { buffer_size = 262144, rendered_size = 65536 };
    // The uses' separators and the defines' (which must not end a directive's line).
    static const char* const separators[][2] = {
        { "", "" }, { " ", " " }, { "\n", "/**/" }, { "/**/", "/* a comment\n over two lines */" }, { " \t ", "\t" },
        { "\n\n// a line comment\n", " /* c */ " },
    };
    const uint32_t defines = (uint32_t)(sizeof(c_direct_macro_defines) / sizeof(c_direct_macro_defines[0]));
    const uint32_t cases = (uint32_t)(sizeof(c_direct_macro_cases) / sizeof(c_direct_macro_cases[0]));
    char* const buffer = (char*)malloc(buffer_size);
    char* const expected = (char*)malloc(rendered_size);
    char* const rendered = (char*)malloc(rendered_size);
    bool ok = buffer != NULL && expected != NULL && rendered != NULL;
    for (uint32_t s = 0; ok && s < sizeof(separators) / sizeof(separators[0]); s += 1)
    {
        for (uint32_t shift = 0; ok && shift < 131u; shift += 1)
        {
            uint32_t length = 0;
            for (uint32_t d = 0; d < defines; d += 1)
            {
                memset(buffer + length, ' ', shift);
                length += shift;
                c_direct_test_put(buffer, &length, c_direct_macro_defines[d], separators[s][1]);
                buffer[length++] = '\n';
            }
            uint32_t e = 0;
            for (uint32_t i = 0; i < cases; i += 1)
            {
                memset(buffer + length, ' ', shift);
                length += shift;
                c_direct_test_put(buffer, &length, c_direct_macro_cases[i].text, separators[s][0]);
                buffer[length++] = '\n';
                for (const char* c = c_direct_macro_cases[i].rendered; *c != '\0'; c += 1)
                {
                    const bool use_space = *c == '$' && separators[s][0][0] != '\0';
                    const bool define_space = *c == '%' && separators[s][1][0] != '\0';
                    if (use_space || define_space || (*c != '$' && *c != '%'))
                    {
                        expected[e++] = use_space || define_space ? ' ' : *c;
                    }
                }
                expected[e] = ' ';
                e += c_direct_macro_cases[i].rendered[0] != '\0' ? 1u : 0u;
            }
            e -= e != 0 ? 1u : 0u;
            expected[e] = '\0';
            // The file's last 80 bytes are fast_lex_one's: the cases stand before them.
            memset(buffer + length, ' ', 100);
            length += 100u;
            buffer[length++] = '\n';
            ok = c_direct_test_rendered(buffer, length, rendered, rendered_size) && strcmp(rendered, expected) == 0;
            if (!ok)
            {
                fprintf(stderr, "test-direct: macros: separators %u, shift %u:\n  got      %s\n  expected %s\n", s, shift, rendered, expected);
            }
        }
    }
    // A call at each place across a window's end: the window holds 2,048 tokens, cut at a block's.
    for (uint32_t before = 1950u; ok && before < 2080u; before += 1)
    {
        uint32_t length = 0;
        c_direct_test_put(buffer, &length, "#define F(a, b) a + b\n#define S(x) #x\n", "");
        for (uint32_t i = 0; i < before; i += 1)
        {
            buffer[length++] = 'x';
            buffer[length++] = ' ';
        }
        c_direct_test_put(buffer, &length, "F(1, F(2, 3)) S( a  +  b ) F(\"s\", 'c')", "");
        memset(buffer + length, ' ', 100);
        length += 100u;
        buffer[length++] = '\n';
        ok = c_direct_test_rendered(buffer, length, rendered, rendered_size) &&
             strcmp(rendered + 2u * before, "1 + 2 + 3 \"a + b\" \"s\" + 99") == 0;
        if (!ok)
        {
            fprintf(stderr, "test-direct: macros: a call after %u tokens: %s\n", before, rendered + 2u * before);
        }
    }
    // Where a reader leaves the file's line: a call's arguments over lines (the line of its ')'), a define's line with
    // a comment over two lines and a splice, an #if's.
    static const CDirectMacroCase lines[] = {
        { "#define L(x) x __LINE__\nL(\n1\n)\n__LINE__\n", "1 4 5" },
        { "#define D1 1 /* a comment\nover two lines */ + 2\nD1 __LINE__\n", "1 + 2 3" },
        { "#define D2 a \\\n+ b\nD2 __LINE__\n", "a + b 3" },
        { "#if 1 /* c */ + \\\n2 == 3\nyes __LINE__\n#endif\n__LINE__\n", "yes 3 5" },
        { "#define F(a, b) a b\nF(__LINE__,\n__LINE__) __LINE__\n", "3 3 3" },
    };
    for (uint32_t i = 0; ok && i < sizeof(lines) / sizeof(lines[0]); i += 1)
    {
        uint32_t length = 0;
        c_direct_test_put(buffer, &length, lines[i].text, "");
        memset(buffer + length, ' ', 100);
        length += 100u;
        buffer[length++] = '\n';
        ok = c_direct_test_rendered(buffer, length, rendered, rendered_size) && strcmp(rendered, lines[i].rendered) == 0;
        if (!ok)
        {
            fprintf(stderr, "test-direct: macros: lines case %u: %s\n", i, rendered);
        }
    }
    free(rendered);
    free(expected);
    free(buffer);
    return ok;
}

// An object written over a longer file that was there is the object and nothing after it (#2850: the file is not
// truncated when it is opened).
static bool c_direct_test_overwrite(const char* obj_path)
{
    static const char source[] = "int answer(void) { return 42; }\n";
    uint32_t text_length = 0;
    uint32_t symbol_count = 0;
    bool ok = c_direct_test_compile_source_keep(source, obj_path, NULL, 0, &text_length, &symbol_count, NULL);
    struct stat fresh = { 0 };
    ok = ok && stat(obj_path, &fresh) == 0 && fresh.st_size > 0;
    if (ok)
    {
        // Twice the object's size of bytes that are no ELF file, then the same compile over them.
        FILE* const junk = fopen(obj_path, "wb");
        ok = junk != NULL;
        for (off_t i = 0; ok && i < fresh.st_size * 2; i += 1)
        {
            ok = fputc(0xa5, junk) != EOF;
        }
        ok = (junk == NULL || fclose(junk) == 0) && ok;
        uint64_t header_end = 0;
        ok = ok && c_direct_test_compile_source_keep(source, obj_path, NULL, 0, &text_length, &symbol_count, &header_end);
        struct stat over = { 0 };
        ok = ok && stat(obj_path, &over) == 0 && over.st_size == fresh.st_size && (uint64_t)over.st_size == header_end;
    }
    unlink(obj_path);
    return ok;
}

// A header that the search's second directory holds, included twice: the first directory is asked once and
// remembered as holding none (#2850), and both includes find the header. Then a header that the first directory does
// hold, whose path falls in the remembered one's slot of the table: it is another path, and is found.
static bool c_direct_test_include_search(const char* obj_path)
{
    char first[256];
    char second[256];
    char header[320];
    char missing[320];
    char other[320];
    char source[512];
    snprintf(first, sizeof(first), "/tmp/buster_direct_test_%d_a", (int)getpid());
    snprintf(second, sizeof(second), "/tmp/buster_direct_test_%d_b", (int)getpid());
    snprintf(header, sizeof(header), "%s/counted.h", second);
    const uint32_t missing_length = (uint32_t)snprintf(missing, sizeof(missing), "%s/counted.h", first);
    uint32_t other_number = 0;
    bool collides = false;
    while (!collides && other_number < 100000u)
    {
        other_number += 1u;
        const uint32_t other_length = (uint32_t)snprintf(other, sizeof(other), "%s/other%u.h", first, other_number);
        collides = ((fast_guard_hash(other, other_length) ^ fast_guard_hash(missing, missing_length)) & (BUSTER_FPP_MAX_MISSING - 1u)) == 0;
    }
    bool ok = collides && mkdir(first, 0700) == 0 && mkdir(second, 0700) == 0;
    if (ok)
    {
        FILE* const file = fopen(header, "wb");
        ok = file != NULL && fputs("COUNTED\n", file) != EOF;
        ok = (file == NULL || fclose(file) == 0) && ok;
        FILE* const other_file = fopen(other, "wb");
        ok = other_file != NULL && fputs("COUNTED\n", other_file) != EOF && ok;
        ok = (other_file == NULL || fclose(other_file) == 0) && ok;
    }
    if (ok)
    {
        // Each include adds one to the initializer: three of them make 3, which the object's .data holds.
        snprintf(source, sizeof(source),
                 "#define COUNTED +1\n"
                 "int counted = 0\n"
                 "#include <counted.h>\n"
                 "#include <counted.h>\n"
                 "#include <other%u.h>\n"
                 ";\n", other_number);
        const char* directories[2] = { first, second };
        uint32_t text_length = 0;
        uint32_t symbol_count = 0;
        ok = c_direct_test_compile_source_keep(source, obj_path, directories, 2, &text_length, &symbol_count, NULL);
        if (ok)
        {
            ok = false;
            FILE* const object = fopen(obj_path, "rb");
            if (object != NULL)
            {
                Elf64_Ehdr eh;
                if (fread(&eh, 1, sizeof(eh), object) == sizeof(eh))
                {
                    for (uint16_t i = 0; i < eh.e_shnum && !ok; i += 1)
                    {
                        Elf64_Shdr sh;
                        uint32_t value = 0;
                        if (fseek(object, (long)(eh.e_shoff + (uint64_t)i * sizeof(sh)), SEEK_SET) == 0 && fread(&sh, 1, sizeof(sh), object) == sizeof(sh) &&
                            sh.sh_type == SHT_PROGBITS && (sh.sh_flags & (SHF_WRITE | SHF_EXECINSTR)) == SHF_WRITE && sh.sh_size == 4u &&
                            fseek(object, (long)sh.sh_offset, SEEK_SET) == 0 && fread(&value, 1, 4, object) == 4u)
                        {
                            ok = value == 3u;
                        }
                    }
                }
                fclose(object);
            }
        }
    }
    unlink(obj_path);
    unlink(header);
    unlink(other);
    rmdir(second);
    rmdir(first);
    return ok;
}

// #3577: no initializer here overrides another or changes its value (the full pipeline's warnings). Each inline function
// is checked where it is defined, `unused` and `here` too, and only the ones a compiled function references are emitted,
// at the unit's end, in the order they become reachable.
static bool c_direct_test_tables_and_inline_bodies(const char* obj_path)
{
    static const char source[] =
        "unsigned char bytes[] = {0,1,22,33,44,55,66,77,88,99,100,111,122,133,144,155,"
        "0xff,030,8+2,[23]=7};\n"
        "unsigned short rows[2][17] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,"
        "17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,1};\n"
        "unsigned int decimals[] = {0,1,22,333,4444,55555,666666,7777777,88888888,"
        "9,10,11,12,13,14,15,123456789,(unsigned)2.5,3+4};\n"
        "unsigned long wide[] = {0xffffffffU,1,2,3,4,5,6,7,8,9,10,11,12,13,14,2147483648U,-2};\n"
        "static inline int a(int), b(int), c(int), d(int);\n"
        "static inline int a(int n){return n ? d(n-1) : 1;}\n"
        "static inline int b(int n){return n ? a(n-1)+c(n-1) : 2;}\n"
        "static inline int c(int n){return a(n); }\n"
        "static inline int d(int n){return b(n); }\n"
        "static inline int unused(void){return 3;}\n"
        "static inline int here(void){return 4;}\n"
        "int (*hook)(int)=b; int entry(int n){return hook(n); }\n";
    static const uint8_t bytes[24] = {0,1,22,33,44,55,66,77,88,99,100,111,122,133,144,155,255,24,10,[23]=7};
    static const uint16_t rows[34] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,1};
    static const uint32_t decimals[] = {0,1,22,333,4444,55555,666666,7777777,88888888,9,10,11,12,13,14,15,123456789,2,7};
    static const uint64_t wide[] = {UINT32_MAX,1,2,3,4,5,6,7,8,9,10,11,12,13,14,UINT32_C(2147483648),UINT64_MAX-1u};
    static const CDirectExpectedSymbol expected[] = {
        { "bytes", bytes, sizeof(bytes) },
        { "rows", rows, sizeof(rows) },
        { "decimals", decimals, sizeof(decimals) },
        { "wide", wide, sizeof(wide) },
        { "entry", NULL, 0 },
        { "b", NULL, 0 },
        { "c", NULL, 0 },
        { "a", NULL, 0 },
        { "d", NULL, 0 },
        { "unused", NULL, UINT32_MAX },
        { "here", NULL, UINT32_MAX },
        { NULL, NULL, 0 },
    };
    return c_direct_test_compile_source_with(source, obj_path, 0, NULL, 0, false, NULL, NULL, NULL, expected, NULL, 0, (CDirectTestMode){ 0 });
}

// ---------------------------------------------------------------------------------------------------------------------
// The symbols of a declaration's attributes and of inline definitions, as the full pipeline gives them (#3595)

typedef struct CDirectTestSymbol CDirectTestSymbol;
struct CDirectTestSymbol
{
    uint32_t count;
    uint8_t bind;
    uint8_t type;
    uint8_t visibility;
    uint16_t section;
    uint64_t value;
    uint64_t size;
};

// The symbols named `name` in the object at `path`: how many, and the first one's binding, type, visibility, section
// index, value and size.
static CDirectTestSymbol c_direct_test_symbol(const char* path, const char* name)
{
    CDirectTestSymbol result = { 0 };
    FILE* file = fopen(path, "rb");
    uint8_t* bytes = NULL;
    long size = 0;
    if (file != NULL && fseek(file, 0, SEEK_END) == 0 && (size = ftell(file)) > (long)sizeof(Elf64_Ehdr) && fseek(file, 0, SEEK_SET) == 0)
    {
        bytes = (uint8_t*)malloc((size_t)size);
        if (bytes != NULL && fread(bytes, 1, (size_t)size, file) != (size_t)size)
        {
            free(bytes);
            bytes = NULL;
        }
    }
    if (file != NULL)
    {
        fclose(file);
    }
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)bytes;
    const bool sections = bytes != NULL && header->e_shoff + (uint64_t)header->e_shnum * sizeof(Elf64_Shdr) <= (uint64_t)size;
    for (uint16_t i = 0; sections && i < header->e_shnum; i += 1)
    {
        const Elf64_Shdr* table = (const Elf64_Shdr*)(bytes + header->e_shoff);
        if (table[i].sh_type == SHT_SYMTAB && table[i].sh_link < header->e_shnum && table[i].sh_offset + table[i].sh_size <= (uint64_t)size)
        {
            const Elf64_Shdr* strings = table + table[i].sh_link;
            const Elf64_Sym* symbols = (const Elf64_Sym*)(bytes + table[i].sh_offset);
            for (uint64_t k = 1; k < table[i].sh_size / sizeof(Elf64_Sym); k += 1)
            {
                const uint64_t at = strings->sh_offset + symbols[k].st_name;
                if (at + strlen(name) + 1u <= (uint64_t)size && strcmp((const char*)bytes + at, name) == 0)
                {
                    if (result.count == 0)
                    {
                        result.bind = ELF64_ST_BIND(symbols[k].st_info);
                        result.type = ELF64_ST_TYPE(symbols[k].st_info);
                        result.visibility = ELF64_ST_VISIBILITY(symbols[k].st_other);
                        result.section = symbols[k].st_shndx;
                        result.value = symbols[k].st_value;
                        result.size = symbols[k].st_size;
                    }
                    result.count += 1u;
                }
            }
        }
    }
    free(bytes);
    return result;
}

typedef struct CDirectDeclarationCase CDirectDeclarationCase;
struct CDirectDeclarationCase
{
    const char* source;
    const char* name;     // the symbol the case is about
    const char* absent;   // a name the object must not have, or NULL
    uint8_t bind;
    uint8_t visibility;
    bool defined;
};

// Each case's symbol as the full pipeline gives it: one symbol of the name, its binding and visibility, defined or not.
// The inline definitions follow C99's rules and gnu_inline's (an extern inline one with gnu_inline is never emitted, a
// C99 one only where a declaration without inline or with extern makes it external, whether or not it is called); an
// asm label is its literals joined (glibc's __REDIRECT is "" and the name); internal visibility is hidden, and a later
// declaration's visibility and weak join the symbol's; a block's declaration of a function names the file's.
static bool c_direct_test_declarations(const char* obj_path)
{
    static const CDirectDeclarationCase cases[] = {
        { "inline int f(void){return 7;}\nint g(void){return f();}\n", "f", NULL, STB_GLOBAL, STV_DEFAULT, false },
        { "inline int f(void){return 7;}\nint f(void);\nint g(void){return 1;}\n", "f", NULL, STB_GLOBAL, STV_DEFAULT, true },
        { "inline int f(void);\nint g(void){return f();}\ninline int f(void){return 7;}\nextern int f(void);\n", "f", NULL, STB_GLOBAL, STV_DEFAULT, true },
        { "inline int f(void);\ninline int f(void){return 7;}\nint (*p)(void) = f;\n", "f", NULL, STB_GLOBAL, STV_DEFAULT, false },
        { "extern inline int f(void){return 7;}\n", "f", NULL, STB_GLOBAL, STV_DEFAULT, true },
        { "extern inline __attribute__((gnu_inline)) int f(void){return 7;}\nint g(void){return f();}\n", "f", NULL, STB_GLOBAL, STV_DEFAULT, false },
        { "extern __inline __attribute__ ((__gnu_inline__)) int f(void){return 7;}\nint g(void){return 1;}\n", "g", "f", STB_GLOBAL, STV_DEFAULT, true },
        { "int f(void) __attribute__((gnu_inline));\nextern inline int f(void){return 7;}\nint g(void){return f();}\n", "f", NULL, STB_GLOBAL, STV_DEFAULT, false },
        { "inline __attribute__((gnu_inline)) int f(void){return 7;}\nint g(void){return 1;}\n", "f", NULL, STB_GLOBAL, STV_DEFAULT, true },
        { "extern inline __attribute__((gnu_inline)) int f(void){return 7;}\ninline int f(void);\nint g(void){return f();}\n", "f", NULL, STB_GLOBAL, STV_DEFAULT, true },
        { "extern inline __attribute__((gnu_inline)) int f(void){return 7;}\nint f(void){return 8;}\nint g(void){return f();}\n", "f", NULL, STB_GLOBAL, STV_DEFAULT, true },
        { "extern inline __attribute__((gnu_inline)) int f(void){return 7;}\nint f(void){return 8;}\ninline int f(void);\n", "f", NULL, STB_GLOBAL, STV_DEFAULT, true },
        { "static int f(void);\ninline int f(void){return 7;}\nint g(void){return f();}\n", "f", NULL, STB_LOCAL, STV_DEFAULT, true },
        { "static int f(void);\ninline int f(void){return 7;}\nint g(void){return 1;}\n", "g", "f", STB_GLOBAL, STV_DEFAULT, true },
        { "inline int f(void);\nint g(void){return f();}\ninline int f(void){return 7;}\n", "f", NULL, STB_GLOBAL, STV_DEFAULT, false },
        { "int scan(void) __asm__(\"\" \"__isoc99_scan\");\nint g(void){return scan();}\n", "__isoc99_scan", "scan", STB_GLOBAL, STV_DEFAULT, false },
        { "int h(void) __asm__(\"a\" \"b\" \"c\");\nint h(void){return 0;}\n", "abc", "a", STB_GLOBAL, STV_DEFAULT, true },
        { "int scan(void);\nint scan(void) __asm__(\"__isoc99_scan\");\nint g(void){return scan();}\n", "__isoc99_scan", "scan", STB_GLOBAL, STV_DEFAULT, false },
        { "int g(void) __attribute__((visibility(\"internal\")));\nint g(void){return 0;}\n", "g", NULL, STB_GLOBAL, STV_HIDDEN, true },
        { "__attribute__((visibility(\"hidden\"))) int g(void) __attribute__((visibility(\"default\")));\nint g(void){return 0;}\n", "g", NULL, STB_GLOBAL, STV_DEFAULT, true },
        { "int g(void) __attribute__((visibility(\"default\"), visibility(\"hidden\")));\nint g(void){return 0;}\n", "g", NULL, STB_GLOBAL, STV_HIDDEN, true },
        { "int g(void);\nint g(void) __attribute__((visibility(\"hidden\")));\nint g(void){return 0;}\n", "g", NULL, STB_GLOBAL, STV_HIDDEN, true },
        { "int v;\nextern int v __attribute__((visibility(\"protected\")));\nint r(void){return v;}\n", "v", NULL, STB_GLOBAL, STV_PROTECTED, true },
        { "int g(void);\nint g(void) __attribute__((weak));\nint g(void){return 0;}\n", "g", NULL, STB_WEAK, STV_DEFAULT, true },
        { "int g(void);\nint g(void) __attribute__((weak));\nint f(void){return g();}\n", "g", NULL, STB_WEAK, STV_DEFAULT, false },
        { "static int f(void){return 1;}\nint g(void){ int f(void); return f(); }\n", "f", NULL, STB_LOCAL, STV_DEFAULT, true },
        { "int f(void){return 1;}\nint g(void){ int f(void); return f(); }\n", "f", NULL, STB_GLOBAL, STV_DEFAULT, true },
        { "int x = 2;\nint g(void){ extern int x; return x; }\n", "x", NULL, STB_GLOBAL, STV_DEFAULT, true },
    };
    bool ok = true;
    for (uint32_t i = 0; ok && i < (uint32_t)(sizeof(cases) / sizeof(cases[0])); i += 1)
    {
        const CDirectDeclarationCase* c = cases + i;
        ok = c_direct_test_compile_source_keep(c->source, obj_path, NULL, 0, NULL, NULL, NULL);
        const CDirectTestSymbol sym = c_direct_test_symbol(obj_path, c->name);
        ok = ok && sym.count == 1u && sym.bind == c->bind && sym.visibility == c->visibility && (sym.section != SHN_UNDEF) == c->defined &&
             (c->absent == NULL || c_direct_test_symbol(obj_path, c->absent).count == 0);
        unlink(obj_path);
        if (!ok)
        {
            fprintf(stderr, "test-direct: a declaration's symbol is not the full pipeline's: %s", c->source);
        }
    }
    // What the full pipeline refuses or warns of, and what the direct path leaves to it.
    static const char* const leaves[] = {
        "int g(void) __asm__();\nint g(void){return 0;}\n",
        "int g(void) __asm__(\"\");\nint g(void){return 0;}\n",
        "int g(void) __asm__(\"\" \"\");\nint g(void){return 0;}\n",
        "int g(void) __asm__(u8\"gg\");\nint g(void){return 0;}\n",
        "int g(void) __asm__(\"g\\x67\");\nint g(void){return 0;}\n",
        "int g(void) __attribute__((visibility(\"bogus\")));\nint g(void){return 0;}\n",
        "int g(void) __attribute__((visibility(\"hidden\\n\")));\nint g(void){return 0;}\n",
        "int g(void) __attribute__((visibility(\"\\x68idden\")));\nint g(void){return 0;}\n",
        "int g(void) __attribute__((visibility(\"\\150idden\")));\nint g(void){return 0;}\n",
        "int g(void) __attribute__((visibility(u8\"hidden\")));\nint g(void){return 0;}\n",
        "int g(void) __attribute__((visibility(\"hid\" \"den\")));\nint g(void){return 0;}\n",
        "int g(void) __attribute__((visibility(\"hidden\" \"x\")));\nint g(void){return 0;}\n",
        "int g(void) __attribute__((visibility(\"hidden\", 1)));\nint g(void){return 0;}\n",
        "int f(void){ L: __attribute__((visibility(\"hidden\"))); return 0; }\n",
        "typedef int T __attribute__((visibility(\"hidden\")));\n",
        "int f(void){ typedef int T __attribute__((visibility(\"hidden\"))); return 0; }\n",
        "int g(void) __attribute__((visibility(\"hidden\")));\nint g(void) __attribute__((visibility(\"default\")));\n",
        "int g(void) __asm__(\"a\");\nint g(void) __asm__(\"b\");\n",
        "int g(void);\nint f(void){return g();}\nint g(void) __attribute__((visibility(\"hidden\")));\n",
        "int g(void);\nint f(void){return g();}\nint g(void) __attribute__((weak));\n",
        "int g(void);\nint f(void){return g();}\nint g(void) __asm__(\"h\");\n",
        "inline int f(void){return 7;}\nint f(void){return 8;}\n",
        "inline int f(void){return 7;}\nint f(void) __attribute__((gnu_inline));\n",
        "extern inline __attribute__((gnu_inline)) int f(void){return 7;}\ninline int f(void){return 8;}\n",
        "int g(void){ int f(void); return f(); }\n",
        "int f(void);\nint g(void){ int f = 1; { int f(void); return f(); } }\n",
        "int f(void){return 1;}\nint g(void){ int f = 2; { int f(void); } return f; }\n",
        "int f(void);\nint g(void){ int f(void) __asm__(\"h\"); return f(); }\n",
        "static int x = 1;\nint g(void){ extern int x; return x; }\n",
    };
    for (uint32_t i = 0; ok && i < (uint32_t)(sizeof(leaves) / sizeof(leaves[0])); i += 1)
    {
        ok = c_direct_test_compile_source(leaves[i], obj_path, 1, NULL, NULL);
        if (!ok)
        {
            fprintf(stderr, "test-direct: a declaration the direct path took: %s", leaves[i]);
        }
    }
    return ok;
}

// ---------------------------------------------------------------------------------------------------------------------
// GNU's alias attribute and typeof(name) (#3592)

// Whether `alias` is defined where `target` is, with `bind` and `type`.
static bool c_direct_test_alias_of(const char* path, const char* alias, const char* target, uint8_t bind, uint8_t type)
{
    const CDirectTestSymbol a = c_direct_test_symbol(path, alias);
    const CDirectTestSymbol t = c_direct_test_symbol(path, target);
    return a.count == 1u && t.count == 1u && a.bind == bind && a.type == type && t.type == type && a.section != SHN_UNDEF &&
           a.section == t.section && a.value == t.value && a.size == t.size && t.size != 0;
}

// weak_alias(old, new) as musl writes it, of functions and objects: an alias of a function a header declared, of an
// initialized and a tentative object, of a static function and of a static inline one only the alias reaches, one
// declared before its target, and typeof of a typedef name. Each alias is defined at its target's place; an alias in
// any other place, or of a target the unit doesn't define, leaves the direct path.
static bool c_direct_test_aliases(const char* obj_path)
{
    static const char functions[] =
        "char* crypt_like(const char* k);\n"
        "char* __crypt_like(const char* k) { return (char*)k + 1; }\n"
        "extern __typeof(__crypt_like) crypt_like __attribute__((__weak__, __alias__(\"__crypt_like\")));\n"
        "char* use(void) { return crypt_like(\"xy\"); }\n"
        "static int helper(void) { return 3; }\n"
        "extern __typeof(helper) exported __attribute__((alias(\"helper\"), visibility(\"hidden\")));\n"
        "static inline int tiny(void) { return 7; }\n"
        "extern __typeof(tiny) tiny_alias __attribute__((alias(\"tiny\")));\n"
        "int later(void);\n"
        "extern __typeof(later) early __attribute__((alias(\"later\")));\n"
        "int later(void) { return 9; }\n"
        "int called(void);\n"
        "int use_called(void) { return called(); }\n"
        "int body(void) { return 5; }\n"
        "extern __typeof(body) called __attribute__((weak, alias(\"body\")));\n";
    static const char objects[] =
        "char** __env_like = 0;\n"
        "extern char** env_like;\n"
        "extern __typeof(__env_like) env_like __attribute__((weak, alias(\"__env_like\")));\n"
        "int counter;\n"
        "extern __typeof(counter) count2 __attribute__((alias(\"counter\")));\n"
        "const long table[3] = { 1, 2, 3 };\n"
        "__typeof(table) table2 __attribute__((alias(\"table\")));\n"
        "char*** where(void) { return &env_like; }\n"
        "typedef long Wide;\n"
        "__typeof(Wide) wide = 3;\n";
    uint32_t text = 0;
    uint32_t symbols = 0;
    bool ok = c_direct_test_compile_source_keep(functions, obj_path, NULL, 0, &text, &symbols, NULL);
    ok = ok && c_direct_test_alias_of(obj_path, "crypt_like", "__crypt_like", STB_WEAK, STT_FUNC) &&
         c_direct_test_alias_of(obj_path, "exported", "helper", STB_GLOBAL, STT_FUNC) && c_direct_test_symbol(obj_path, "exported").visibility == STV_HIDDEN &&
         c_direct_test_symbol(obj_path, "helper").bind == STB_LOCAL && c_direct_test_alias_of(obj_path, "tiny_alias", "tiny", STB_GLOBAL, STT_FUNC) &&
         c_direct_test_alias_of(obj_path, "early", "later", STB_GLOBAL, STT_FUNC) &&
         c_direct_test_alias_of(obj_path, "called", "body", STB_WEAK, STT_FUNC);
    unlink(obj_path);
    ok = ok && c_direct_test_compile_source_keep(objects, obj_path, NULL, 0, &text, &symbols, NULL);
    ok = ok && c_direct_test_alias_of(obj_path, "env_like", "__env_like", STB_WEAK, STT_OBJECT) &&
         c_direct_test_alias_of(obj_path, "count2", "counter", STB_GLOBAL, STT_OBJECT) &&
         c_direct_test_alias_of(obj_path, "table2", "table", STB_GLOBAL, STT_OBJECT) && c_direct_test_symbol(obj_path, "wide").size == 8u;
    unlink(obj_path);
    static const char* const leaves[] = {
        "int ext(void);\nextern __typeof(ext) a __attribute__((alias(\"ext\")));\n",
        "extern int a __attribute__((alias(\"nothing\")));\n",
        "int g(void) { return 0; }\nint __attribute__((alias(\"g\"))) f(void) { return 1; }\n",
        "int g(void) { return 0; }\nint f(void) { extern int a __attribute__((alias(\"g\"))); return 0; }\n",
        "int g(void) { return 0; }\nextern __typeof(g) a __attribute__((alias(g)));\n",
        "int g(void) { return 0; }\nextern __typeof(g) a __attribute__((alias(\"g\" \"\")));\n",
        "int g(void) { return 0; }\nextern __typeof(g) a __attribute__((alias(\"a b\")));\n",
        "int g(void) { return 0; }\nextern __typeof(g) a __attribute__((alias(\"\")));\n",
        "int g(void) { return 0; }\nextern __typeof(g) a __attribute__((alias(u8\"g\")));\n",
        "int g(void) { return 0; }\nextern __typeof(g) a __attribute__((alias(\"\\x67\")));\n",
        "int g(void) { return 0; }\nextern __typeof(g) a __asm__(\"b\") __attribute__((alias(\"g\")));\n",
        "int g(void) { return 0; }\nstatic int f(void);\nextern __typeof(g) f __attribute__((alias(\"g\")));\n",
        "inline int f(void) { return 7; }\nint g(void) { return 0; }\nextern __typeof(g) f __attribute__((alias(\"g\")));\n",
        "int g(void) { return 0; }\nint h(void) __attribute__((alias(\"g\")));\nint h(void) { return 1; }\n",
        "int g(void) { return 0; }\nint h(void) __attribute__((alias(\"g\")));\nint h(void) __attribute__((alias(\"g\")));\n",
        "int g(void) { return 0; }\nint h(void) __attribute__((alias(\"g\")));\nint h(void) __attribute__((used));\n",
        "int x = 1;\nextern __typeof(x) y __attribute__((alias(\"x\")));\nint y = 2;\n",
        "int x = 1;\nextern __typeof(x) y __attribute__((alias(\"x\")));\nint y;\n",
        "int g(void) { return 0; }\nstruct S { int x __attribute__((alias(\"g\"))); };\n",
        "int g(void) { return 0; }\nvoid f(int x __attribute__((alias(\"g\"))));\n",
        "int g = 1;\nint f(void) { static int s __attribute__((alias(\"g\"))); return s; }\n",
        "int g(void) { return 0; }\nint f(void) { int x __attribute__((alias(\"g\"))) = 1; return x; }\n",
        "int g(void) { return 0; }\ntypedef int T __attribute__((alias(\"g\")));\n",
        "int g(void) { return 0; }\nstatic __typeof(g) a __attribute__((alias(\"g\")));\n",
        "int g(void) { return 0; }\nextern __typeof(g) a __attribute__((alias(\"g\")));\nextern __typeof(g) b __attribute__((alias(\"a\")));\n",
        "int x = 1;\nextern int f(void) __attribute__((alias(\"x\")));\n",
        "int x = 1;\nextern __typeof(x) y __attribute__((alias(\"x\"))) = 2;\n",
        "int x = 1;\nint y;\nextern __typeof(x) y __attribute__((alias(\"x\")));\n",
        "int g(void) { return 0; }\nint f(void);\nint f(void) { return 1; }\nextern __typeof(g) f __attribute__((alias(\"g\")));\n",
        "int g(void) __asm__(\"g_label\");\nint g(void) { return 0; }\nextern __typeof(g) a __attribute__((alias(\"g\")));\n",
        "int x;\nunsigned __typeof(x) y;\n",
        "__typeof(1 + 2) z;\n",
        "__typeof(undeclared) z;\n",
    };
    for (uint32_t i = 0; ok && i < (uint32_t)(sizeof(leaves) / sizeof(leaves[0])); i += 1)
    {
        ok = c_direct_test_compile_source(leaves[i], obj_path, 1, NULL, NULL);
        if (!ok)
        {
            fprintf(stderr, "test-direct: an alias case the direct path took: %s", leaves[i]);
        }
    }
    return ok;
}

// ---------------------------------------------------------------------------------------------------------------------
// A declaration's specifiers that the direct path doesn't take (#3599)

// A declaration without a type specifier is implicit int, which C99 refused, or C23's auto, whose type is its
// initializer's; two storage classes, or one where none may stand, are refused or warned of; a for's static, extern or
// typedef declaration declares no automatic object. Each leaves the direct path. What C17 and C23 take stays on it.
static bool c_direct_test_specifiers(const char* obj_path)
{
    static const char* const leaves[] = {
        "int f(void) { auto x = 1.5; return (int)x; }\n",
        "static auto z = 2.5;\n",
        "int f(void) { const x = 3; return x; }\n",
        "static y = 4;\n",
        "int f(a, b);\n",
        "int g(void) { register static int z = 1; return z; }\n",
        "static extern int u;\n",
        "extern static int u;\n",
        "typedef static int T;\n",
        "int h(void) { auto static int s = 1; return s; }\n",
        "static static int x;\n",
        "register int w;\n",
        "auto int v;\n",
        "void k(static int p) { }\n",
        "void k(auto int p) { }\n",
        "void k(inline int p) { }\n",
        "void k(_Noreturn int p) { }\n",
        "void k(typedef int p) { }\n",
        "void k(void (*cb)(static int)) { }\n",
        "struct S { static int m; };\n",
        "struct S { register int m; };\n",
        "int f(void) { return sizeof(static int); }\n",
        "int f(void) { for (static int i = 0; i < 2; i++) {} return 0; }\n",
        "int f(void) { for (extern int i; ;) {} return 0; }\n",
        "int f(void) { for (typedef int T; ;) {} return 0; }\n",
        "int f(void) { inline int x = 0; return x; }\n",
        "inline int g;\n",
        "_Noreturn int h;\n",
        "typedef inline int T;\n",
        "typedef inline void F(void);\n",
        "int f(void) { typedef _Noreturn void G(void); return 0; }\n",
    };
    static const char kept[] =
        "int e;\n"
        "void k(register int p) { (void)p; }\n"
        "int f1(void) { int n = 0; for (register int i = 0; i < 2; i++) { n += i; } for (auto int j = 0; j < 2; j++) { n += j; } return n; }\n"
        "int f2(void) { auto int x = 1; register long y = 2; static int z; extern int e; typedef int T; T t = 0; return x + (int)y + z + t + e; }\n"
        "static inline int g(void) { return 1; }\n"
        "_Noreturn void die(void);\n"
        "typedef unsigned T2;\n"
        "signed s;\n"
        "unsigned u;\n"
        "short sh;\n"
        "long long ll;\n"
        "int f3(void) { signed a = 1; unsigned b = 2; const long c = 3; return a + (int)b + (int)c + g(); }\n"
        "struct S2 { unsigned : 3; int m; };\n"
        "int f4(void) { return (int)sizeof(const int); }\n"
        "void cb_user(void (*cb)(register int));\n"
        "__extension__ typedef unsigned long long U64;\n";
    bool ok = true;
    for (uint32_t i = 0; ok && i < (uint32_t)(sizeof(leaves) / sizeof(leaves[0])); i += 1)
    {
        ok = c_direct_test_compile_source(leaves[i], obj_path, 1, NULL, NULL);
        if (!ok)
        {
            fprintf(stderr, "test-direct: a declaration's specifiers the direct path took: %s", leaves[i]);
        }
    }
    const CDirectTestMode c23 = { .c23 = true };
    ok = ok && c_direct_test_compile_mode("int f(void) { auto x = 1.5; return (int)(x * 2); }\n", obj_path, 1, NULL, NULL, c23) &&
         c_direct_test_compile_mode("static auto z = 2.5;\n", obj_path, 1, NULL, NULL, c23) &&
         c_direct_test_compile_source(kept, obj_path, 0, NULL, NULL) && c_direct_test_compile_mode(kept, obj_path, 0, NULL, NULL, c23);
    return ok;
}

// ---------------------------------------------------------------------------------------------------------------------
// Raw string literals (#3600) and the literals of a directive's line the direct path doesn't lex (#3603)

typedef struct CDirectRawCase CDirectRawCase;
struct CDirectRawCase
{
    const char* source;
    uint8_t raw_strings;   // CDirectTestMode.raw_strings
    int expected;          // 1: leaves the direct path; 0: its object defines `defined`
    const char* defined;   // the symbols the object defines, separated by spaces
};

// Each case's status and symbols, and then a raw string literal's prefix, an "R" that ends an ordinary string and a
// raw string after `#define R` or `#define LR`, at each offset of a block of the skipped group's scan and of the lexer's
// windows. A window starts where 80 bytes are left, so the sources end in a comment of more.
static bool c_direct_test_raw_strings(const char* obj_path)
{
    static const CDirectRawCase cases[] = {
        { "#if 0\nR\"(\n#endif\nint y;\n#if 0\n)\"\n#endif\nint x;\n", 1, 1, NULL },
        { "#if 0\nR\"(\n#endif\nint y;\n#if 0\n)\"\n#endif\nint x;\n", 0, 0, "x y" },
        { "const char* s = R\"(x)\";\n", 1, 1, NULL },
        { "#define R\nconst char* s = R\"(x)\";\n", 0, 0, "s" },
        { "#define R\nconst char* s = R\"(x)\";\n", 1, 1, NULL },
        { "const char* s = LR\"(x)\";\n", 1, 1, NULL },
        { "const char* s = u8R\"(x)\";\n", 2, 1, NULL },
        { "#define S R\"(x)\"\nint y;\n", 1, 1, NULL },
        { "#if 0\nx = c ? \"R\" : \"W\";\n#endif\nint x;\n", 1, 0, "x" },
        { "const char* p = \"R\"; const char* q = \"BAR\";\n", 1, 0, "p q" },
        { "#if 0\nconst char* a = \"x\"; const char* b = \"R\";\n#endif\nint y;\n", 1, 0, "y" },
        { "#if 0\nprintf(\"%s\", \"ERROR\"); f(\"R\",(int)x);\n#endif\nint z;\n", 1, 0, "z" },
        { "#if 0\nchar c = 'R'; const char* d = \"\\\"R\\\"\";\n#endif\nint z;\n", 1, 0, "z" },
        { "#if 0\na = \"x\" R\"(y)\";\n#endif\nint z;\n", 1, 1, NULL },
        { "#if 0\na = xR\"(y)\";\n#endif\nint z;\n", 1, 0, "z" },
        { "#if 0\na = 1R\"(y)\";\n#endif\nint z;\n", 1, 0, "z" },
        { "#if 0\nLR\"(\n#endif\nint y;\n#if 0\n)\"\n#endif\nint x;\n", 1, 1, NULL },
        { "#if 0\nu8R\"(\n#endif\nint y;\n#if 0\n)\"\n#endif\nint x;\n", 1, 0, "x y" },
        { "#if 0\na = uR\"(y)\";\n#endif\nint z;\n", 1, 0, "z" },
        { "#if 0\na = uR\"(y)\";\n#endif\nint z;\n", 2, 1, NULL },
        { "#if 0\na = u8R\"(y)\";\n#endif\nint z;\n", 2, 1, NULL },
        { "#if 0\n/* R\"( */ int a;\n#endif\nint z;\n", 1, 0, "z" },
        { "#if 0\n// R\"(\n#endif\nint z;\n", 1, 0, "z" },
        { "#pragma message(\"/* look\")\nint x = 1;\n", 1, 1, NULL },
        { "#pragma foo \"/*\"\nint y = 2;\n/* */ int z = 3;\n", 1, 0, "y z" },
        { "#pragma foo '/*'\nint y = 2;\n", 0, 0, "y" },
        // (#3577: a #line's file name, which __FILE__ would take, leaves the direct path.)
        { "#line 5 \"a/*b.c\"\nint y = 2;\nint z = 3;\n", 0, 1, NULL },
        { "#pragma foo don't\nint y = 2;\n", 0, 1, NULL },
        { "#if 1\nint y = 2;\n#endif // it's\nint z = 3;\n", 0, 0, "y z" },
        { "#pragma foo R\"(x)\"\nint y = 2;\n", 1, 1, NULL },
        { "#pragma foo R\"(x)\"\nint y = 2;\n", 0, 0, "y" },
        { "#if 0\n#else // \"/*\"\nint y = 2;\n#endif\nint z = 3;\n", 0, 0, "y z" },
        { "#if 0\na = \xc3\xa9R\"(y)\";\n#endif\nint z;\n", 1, 0, "z" },
        { "#if 0\na = R'x'; /* c */\n#endif\nint z;\n", 1, 0, "z" },
        { "#define LR\nconst char* s = LR\"(x)\";\n", 1, 1, NULL },
        { "#define uR\nconst char* s = uR\"(x)\";\n", 1, 0, "s" },
        { "#define u8R\nconst char* s = u8R\"(x)\";\n", 1, 0, "s" },
        { "#define u8R\nconst char* s = u8R\"(x)\";\n", 2, 1, NULL },
        { "int R = 1;\nint LR = 2;\nint f(void) { return R + LR; }\n", 2, 0, "R LR f" },
        { "#pragma foo \"\\\"/*\"\nint y = 2;\n", 1, 0, "y" },
    };
    bool ok = true;
    for (uint32_t i = 0; ok && i < (uint32_t)(sizeof(cases) / sizeof(cases[0])); i += 1)
    {
        const CDirectTestMode mode = { .raw_strings = cases[i].raw_strings };
        ok = c_direct_test_compile_source_with(cases[i].source, obj_path, cases[i].expected, NULL, 0, true, NULL, NULL, NULL, NULL, NULL, 0, mode);
        char names[64];
        snprintf(names, sizeof(names), "%s", cases[i].defined != NULL ? cases[i].defined : "");
        char* save = NULL;
        for (char* name = strtok_r(names, " ", &save); ok && name != NULL; name = strtok_r(NULL, " ", &save))
        {
            const CDirectTestSymbol sym = c_direct_test_symbol(obj_path, name);
            ok = sym.count == 1u && sym.section != SHN_UNDEF;
        }
        unlink(obj_path);
        if (!ok)
        {
            fprintf(stderr, "test-direct: raw strings' case %u, raw %u\n", i, (unsigned)cases[i].raw_strings);
        }
    }
    // A literal's line splice on a directive's line is a line: __LINE__ after it counts it, as it counts the lines of
    // directives that end at their newline.
    static const int32_t expected_line[] = { 3 };
    ok = ok && c_direct_test_compile_source_with("#pragma foo \"a\\\nb\"\nconst int line = __LINE__;\n", obj_path, 0, NULL, 0, false, NULL, NULL, NULL,
                                                 NULL, expected_line, sizeof(expected_line), (CDirectTestMode){ 0 });
    static const int32_t expected_directive_line[] = { 4 };
    ok = ok && c_direct_test_compile_source_with("#define X 1\n#ifdef X\n#endif\nconst int line = __LINE__;\n", obj_path, 0, NULL, 0, false, NULL, NULL,
                                                 NULL, NULL, expected_directive_line, sizeof(expected_directive_line), (CDirectTestMode){ 0 });
    const CDirectTestMode raw = { .raw_strings = 1u };
    static const char tail[] = "/* ........................................................................................ */\n";
    for (uint32_t k = 0; ok && k < 140u; k += 1)
    {
        char source[512];
        snprintf(source, sizeof(source), "#if 0\n%*sR\"(\n#endif\nint y;\n#if 0\n)\"\n#endif\nint x;\n%s", (int)k, "", tail);
        ok = c_direct_test_compile_mode(source, obj_path, 1, NULL, NULL, raw);
        snprintf(source, sizeof(source), "#if 0\n%*sx = \"R\";\n#endif\nint z;\n%s", (int)k, "", tail);
        ok = ok && c_direct_test_compile_mode(source, obj_path, 0, NULL, NULL, raw);
        snprintf(source, sizeof(source), "#define R\n%*sconst char* s = R\"(x)\";\n%s", (int)k, "", tail);
        ok = ok && c_direct_test_compile_mode(source, obj_path, 1, NULL, NULL, raw);
        snprintf(source, sizeof(source), "#define LR\n%*sconst char* s = LR\"(x)\";\n%s", (int)k, "", tail);
        ok = ok && c_direct_test_compile_mode(source, obj_path, 1, NULL, NULL, raw);
        if (!ok)
        {
            fprintf(stderr, "test-direct: raw strings at offset %u\n", k);
        }
    }
    return ok;
}

// ---------------------------------------------------------------------------------------------------------------------
// GNU's inline assembly of musl's two shapes (#3592)

typedef long (*CDirectTestAsm0)(void);
typedef long (*CDirectTestAsm1)(long);
typedef long (*CDirectTestAsm2)(long, long);
typedef long (*CDirectTestAsmPointer)(void*);

#if !__has_feature(memory_sanitizer)
// The object's code of `name`, at its place in a copy of its section that may run, or NULL when the object has no
// such function or relocates its section. `*mapping` and `*mapped` are the copy, for munmap. The copy starts 64 bytes
// into its pages: UndefinedBehaviorSanitizer's check of a call through a pointer reads the 8 bytes before the function.
static const void* c_direct_test_code(const char* path, const char* name, void** mapping, size_t* mapped)
{
    const void* result = NULL;
    const CDirectTestSymbol sym = c_direct_test_symbol(path, name);
    FILE* file = sym.count == 1u && sym.type == STT_FUNC && sym.section != SHN_UNDEF ? fopen(path, "rb") : NULL;
    uint8_t* bytes = NULL;
    long size = 0;
    if (file != NULL && fseek(file, 0, SEEK_END) == 0 && (size = ftell(file)) > (long)sizeof(Elf64_Ehdr) && fseek(file, 0, SEEK_SET) == 0)
    {
        bytes = (uint8_t*)malloc((size_t)size);
        if (bytes != NULL && fread(bytes, 1, (size_t)size, file) != (size_t)size)
        {
            free(bytes);
            bytes = NULL;
        }
    }
    if (file != NULL)
    {
        fclose(file);
    }
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)bytes;
    const bool headers = bytes != NULL && header->e_shoff + (uint64_t)header->e_shnum * sizeof(Elf64_Shdr) <= (uint64_t)size && sym.section < header->e_shnum;
    const Elf64_Shdr* table = headers ? (const Elf64_Shdr*)(bytes + header->e_shoff) : NULL;
    bool relocated = false;
    for (uint16_t i = 0; headers && i < header->e_shnum; i += 1)
    {
        relocated = relocated || ((table[i].sh_type == SHT_RELA || table[i].sh_type == SHT_REL) && table[i].sh_info == sym.section && table[i].sh_size != 0);
    }
    const Elf64_Shdr* section = headers && !relocated ? table + sym.section : NULL;
    const bool fits = section != NULL && section->sh_type == SHT_PROGBITS && section->sh_offset + section->sh_size <= (uint64_t)size &&
                      sym.value + sym.size <= section->sh_size;
    const size_t length = fits ? (size_t)((64u + section->sh_size + 4095u) & ~(uint64_t)4095u) : 0u;
    uint8_t* code = length != 0 ? (uint8_t*)mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : (uint8_t*)MAP_FAILED;
    if (code != (uint8_t*)MAP_FAILED)
    {
        memcpy(code + 64, bytes + section->sh_offset, (size_t)section->sh_size);
        if (mprotect(code, length, PROT_READ | PROT_EXEC) == 0)
        {
            result = code + 64 + sym.value;
            *mapping = code;
            *mapped = length;
        }
        else
        {
            munmap(code, length);
        }
    }
    free(bytes);
    return result;
}
#endif

typedef long (*CDirectTestPointers3)(void*, void*, void*);

// Parenthesized pointer parameters (#3577), run: `int (*p)` a pointer and `int (*q)[3]` a pointer to three ints, where
// main's direct path made them an int and an int * (sizeof(p) 4, sizeof(*q) 4, q + 1 four bytes on, and q[1][2] its
// error); and a pointer to a two-dimensional array.
static bool c_direct_test_pointer_parameters(const char* obj_path)
{
    static const char source[] =
        "long t_sizes(int (*p), int (*q)[3], char (*r)[2][5]) { return (long)(sizeof(p) * 10000 + sizeof(*q) * 100 + sizeof(*r)); }\n"
        "long t_step(int (*q)[3]) { return (long)((char*)(q + 1) - (char*)q); }\n"
        "long t_read(int (*q)[3]) { return q[1][2]; }\n";
    bool ok = c_direct_test_compile_source_keep(source, obj_path, NULL, 0, NULL, NULL, NULL);
#if !__has_feature(memory_sanitizer)
    void* mappings[3] = { NULL, NULL, NULL };
    size_t mapped[3] = { 0, 0, 0 };
    const CDirectTestPointers3 sizes = (CDirectTestPointers3)(uintptr_t)(ok ? c_direct_test_code(obj_path, "t_sizes", mappings + 0, mapped + 0) : NULL);
    const CDirectTestAsmPointer step = (CDirectTestAsmPointer)(uintptr_t)(ok ? c_direct_test_code(obj_path, "t_step", mappings + 1, mapped + 1) : NULL);
    const CDirectTestAsmPointer read = (CDirectTestAsmPointer)(uintptr_t)(ok ? c_direct_test_code(obj_path, "t_read", mappings + 2, mapped + 2) : NULL);
    int rows[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    ok = ok && sizes != NULL && step != NULL && read != NULL;
    ok = ok && sizes(NULL, NULL, NULL) == 81210L && step(rows) == 12L && read(rows) == 6L;
    for (uint32_t i = 0; i < 3u; i += 1)
    {
        if (mappings[i] != NULL)
        {
            munmap(mappings[i], mapped[i]);
        }
    }
#endif
    unlink(obj_path);
    return ok;
}

// musl's shapes, run: syscall with no argument (getpid, whose number is an int), with two (munmap of a page and
// clock_gettime, given a pointer), and with six of which three are local register variables (mmap of the page), and
// the thread pointer against pthread_self's. Every other shape, operand,
// constraint, clobber or label leaves the direct path. The functions call nothing, so that their section runs where it
// is copied. MemorySanitizer can't see a result an uninstrumented function makes, so it only compiles them.
static bool c_direct_test_asm(const char* obj_path)
{
    static const char source[] =
        "long t_getpid(void)\n"
        "{\n"
        "    unsigned long ret;\n"
        "    __asm__ __volatile__ (\"syscall\" : \"=a\"(ret) : \"a\"(39) : \"rcx\", \"r11\", \"memory\");\n"
        "    return ret;\n"
        "}\n"
        "long t_mmap(long length)\n"
        "{\n"
        "    unsigned long ret;\n"
        "    register long r10 __asm__(\"r10\") = 0x22;\n"
        "    register long r8 __asm__(\"r8\") = -1;\n"
        "    register long r9 __asm__(\"r9\") = 0;\n"
        "    __asm__ __volatile__ (\"syscall\" : \"=a\"(ret) : \"a\"(9L), \"D\"(0L), \"S\"(length), \"d\"(3L), \"r\"(r10), \"r\"(r8), \"r\"(r9) : \"rcx\", \"r11\", \"memory\");\n"
        "    return ret;\n"
        "}\n"
        "long t_munmap(long address, long length)\n"
        "{\n"
        "    unsigned long ret;\n"
        "    __asm__ __volatile__ (\"syscall\" : \"=a\"(ret) : \"a\"(11L), \"D\"(address), \"S\"(length) : \"rcx\", \"r11\", \"memory\");\n"
        "    return ret;\n"
        "}\n"
        "long t_clock(void* ts)\n"
        "{\n"
        "    unsigned long ret;\n"
        "    __asm__ __volatile__ (\"syscall\" : \"=a\"(ret) : \"a\"(228L), \"D\"(1), \"S\"(ts) : \"rcx\", \"r11\", \"memory\", \"cc\");\n"
        "    return ret;\n"
        "}\n"
        "long t_tp(void)\n"
        "{\n"
        "    unsigned long tp;\n"
        "    __asm__ (\"mov %%fs:0,%0\" : \"=r\" (tp));\n"
        "    return tp;\n"
        "}\n";
    bool ok = c_direct_test_compile_source_keep(source, obj_path, NULL, 0, NULL, NULL, NULL);
#if !__has_feature(memory_sanitizer)
    void* mappings[5] = { NULL, NULL, NULL, NULL, NULL };
    size_t mapped[5] = { 0, 0, 0, 0, 0 };
    const CDirectTestAsm0 getpid_direct = (CDirectTestAsm0)(uintptr_t)(ok ? c_direct_test_code(obj_path, "t_getpid", mappings + 0, mapped + 0) : NULL);
    const CDirectTestAsm1 mmap_direct = (CDirectTestAsm1)(uintptr_t)(ok ? c_direct_test_code(obj_path, "t_mmap", mappings + 1, mapped + 1) : NULL);
    const CDirectTestAsm2 munmap_direct = (CDirectTestAsm2)(uintptr_t)(ok ? c_direct_test_code(obj_path, "t_munmap", mappings + 2, mapped + 2) : NULL);
    const CDirectTestAsm0 tp_direct = (CDirectTestAsm0)(uintptr_t)(ok ? c_direct_test_code(obj_path, "t_tp", mappings + 3, mapped + 3) : NULL);
    const CDirectTestAsmPointer clock_direct = (CDirectTestAsmPointer)(uintptr_t)(ok ? c_direct_test_code(obj_path, "t_clock", mappings + 4, mapped + 4) : NULL);
    ok = ok && getpid_direct != NULL && mmap_direct != NULL && munmap_direct != NULL && tp_direct != NULL && clock_direct != NULL;
    ok = ok && getpid_direct() == (long)getpid() && tp_direct() == (long)(uintptr_t)pthread_self();
    struct timespec now = { .tv_sec = -1, .tv_nsec = -1 };
    ok = ok && clock_direct(&now) == 0 && now.tv_sec >= 0 && now.tv_nsec >= 0 && now.tv_nsec < 1000000000L;
    const long page = ok ? mmap_direct(4096) : -1;
    ok = ok && (unsigned long)page < (unsigned long)-4095L;
    if (ok)
    {
        *(volatile char*)(uintptr_t)page = 1;
        ok = munmap_direct(page, 4096) == 0;
    }
    for (uint32_t i = 0; i < 5u; i += 1)
    {
        if (mappings[i] != NULL)
        {
            munmap(mappings[i], mapped[i]);
        }
    }
#endif
    unlink(obj_path);
    static const char* const leaves[] = {
        "void f(void) { __asm__(\"nop\"); }\n",
        "void f(void) { __asm__ volatile (\"sys\" \"call\" : : : \"memory\"); }\n",
        "long f(void) { unsigned long r; __asm__ volatile (\"syscall\" : \"=r\"(r) : \"a\"(39L) : \"rcx\", \"r11\"); return r; }\n",
        "long f(void) { unsigned long r; __asm__ volatile (\"syscall\" : \"=a\"(r) : \"D\"(1L) : \"rcx\", \"r11\"); return r; }\n",
        "long f(void) { unsigned long r; __asm__ volatile (\"syscall\" : \"=a\"(r) : \"a\"(39L), \"b\"(1L) : \"rcx\", \"r11\"); return r; }\n",
        "long f(void) { unsigned long r; __asm__ volatile (\"syscall\" : \"=a\"(r) : \"a\"(39L), \"D\"(1L), \"D\"(2L) : \"rcx\", \"r11\"); return r; }\n",
        "long f(void) { unsigned long r; __asm__ volatile (\"syscall\" : \"=a\"(r) : \"a\"(39L) : \"rbx\"); return r; }\n",
        "long f(long x) { unsigned long r; __asm__ volatile (\"syscall\" : \"=a\"(r) : \"a\"(39L), \"r\"(x)); return r; }\n",
        "long f(void) { unsigned long r; register long b __asm__(\"rbx\") = 1; __asm__ volatile (\"syscall\" : \"=a\"(r) : \"a\"(39L), \"r\"(b)); return r; }\n",
        "long f(void) { unsigned long r; register int i __asm__(\"r10\") = 1; __asm__ volatile (\"syscall\" : \"=a\"(r) : \"a\"(39L), \"r\"(i)); return r; }\n",
        "long f(void) { unsigned long r; long m; __asm__ volatile (\"syscall\" : \"=a\"(r), \"=m\"(m) : \"a\"(39L)); return r; }\n",
        "long f(void) { unsigned long r; __asm__ volatile (\"syscall\" : [out] \"=a\"(r) : \"a\"(39L)); return r; }\n",
        "long f(void) { unsigned long r; __asm__ volatile (\"syscall\" : \"=a\"(r) : \"a\"(1.5)); return r; }\n",
        "long f(void) { unsigned long t; __asm__ (\"mov %%fs:0,%0\" : \"=r\"(t) : \"a\"(1L)); return t; }\n",
        "long f(void) { int t; __asm__ (\"mov %%fs:0,%0\" : \"=r\"(t)); return t; }\n",
        "long f(void) { static long s __asm__(\"r10\"); return s; }\n",
        "long f(void) { long l __asm__(\"r10\") = 1; return l; }\n",
        "long f(void) { for (register long i __asm__(\"r10\") = 0; i < 2; i++) {} return 0; }\n",
        "void f(void) { __asm__ goto (\"jmp %l0\" : : : : done); done: return; }\n",
        "long g(long);\nlong f(void) { unsigned long r; __asm__ volatile (\"syscall\" : \"=a\"(r) : \"a\"(39L), \"D\"(g)(1)); return r; }\n",
        "long f(void) { const unsigned long r = 0; __asm__ volatile (\"syscall\" : \"=a\"(r) : \"a\"(39L)); return r; }\n",
        "long f(void) { unsigned long r; register long c __asm__(\"rcx\") = 1; __asm__ volatile (\"syscall\" : \"=a\"(r) : \"a\"(39L), \"r\"(c) : \"rcx\"); return r; }\n",
        "long f(void) { unsigned long r; register long c __asm__(\"r11\") = 1; __asm__ volatile (\"syscall\" : \"=a\"(r) : \"a\"(39L), \"r\"(c) : \"memory\", \"r11\"); return r; }\n",
        "long f(void)\n"
        "{\n"
        "    unsigned long r;\n"
        "    register long a __asm__(\"r10\") = 0;\n"
        "    register long b __asm__(\"r8\") = 0;\n"
        "    register long c __asm__(\"r9\") = 0;\n"
        "    register long d __asm__(\"r11\") = 0;\n"
        "    __asm__ volatile (\"syscall\" : \"=a\"(r) : \"a\"(39L), \"D\"(0L), \"S\"(0L), \"d\"(0L), \"r\"(a), \"r\"(b), \"r\"(c), \"r\"(d));\n"
        "    return r;\n"
        "}\n",
        "long f(void) { unsigned long r; __asm__ volatile (u8\"syscall\" : \"=a\"(r) : \"a\"(39L)); return r; }\n",
        "unsigned long g;\nlong f(void) { __asm__ volatile (\"syscall\" : \"=a\"(g) : \"a\"(39L)); return g; }\n",
        "long f(void) { double d; __asm__ (\"mov %%fs:0,%0\" : \"=r\"(d)); return (long)d; }\n",
        "long f(void) { unsigned long t; __asm__ (\"mov %%fs:0,%0\" : \"=m\"(t)); return t; }\n",
        "long f(void) { unsigned long t = 0; __asm__ (\"mov %%fs:0,%0\" : \"+r\"(t)); return t; }\n",
        "extern long g __asm__(\"r10\");\nlong f(void) { unsigned long r; __asm__ volatile (\"syscall\" : \"=a\"(r) : \"a\"(39L), \"r\"(g)); return r; }\n",
        "long f(void) { unsigned long r; __asm__ volatile (\"syscall\" : \"=a\"(r) : \"a\" - 39L)); return r; }\n",
        "long f(void) { unsigned long r; __asm__ volatile (\"syscall\" : \"=a\"(r) : \"a\"(39L ; ); return r; }\n",
        "long f(void) { unsigned long r; __asm__ volatile (\"syscall\" : \"=a\"(r : : \"a\"(39L)); return r; }\n",
        "long f(void) { unsigned long r; __asm__ volatile (\"syscall\" : \"=a\"(r) : \"a\"((__int128)39)); return r; }\n",
        "long f(void) { unsigned long r; __asm__ volatile (\"syscall\" , \"=a\"(r) : \"a\"(39L)); return r; }\n",
        "long f(void) { unsigned long r; __asm__ volatile (\"syscall\" : \"=a\"(r) : \"a\"(39L)) r; return r; }\n",
        "long f(void) { unsigned long r; __asm__ volatile volatile (\"syscall\" : \"=a\"(r) : \"a\"(39L)); return r; }\n",
        "long f(void) { typedef long T __asm__(\"r10\"); return 0; }\n",
        "long f(void) { extern long e __asm__(\"r10\"); return e; }\n",
        "long f(void) { long g(void) __asm__(\"r10\"); return g(); }\n",
        "long f(void) { register double d __asm__(\"r10\") = 1; return (long)d; }\n",
        "long f(void) { unsigned long r; register long x __asm__(\"r1\") = 0; __asm__ volatile (\"syscall\" : \"=a\"(r) : \"a\"(39L), \"r\"(x)); return r; }\n",
        "long f(void) { unsigned long r; __asm__ volatile (\"sys\" : \"=a\"(r) : \"a\"(39L)); return r; }\n",
        "long f(void) { register int i __asm__(\"r10\") = 1; return i; }\n",
        "long f(void) { register long x __asm__(\"r1\") = 0; return x; }\n",
        "long f(void) { unsigned long r; __asm__ volatile (\"syscall\" : \"=a\"(r) : \"a\"(39L) ]; return r; }\n",
    };
    for (uint32_t i = 0; ok && i < (uint32_t)(sizeof(leaves) / sizeof(leaves[0])); i += 1)
    {
        ok = c_direct_test_compile_source(leaves[i], obj_path, 1, NULL, NULL);
        if (!ok)
        {
            fprintf(stderr, "test-direct: an asm case the direct path took: %s", leaves[i]);
        }
    }
    return ok;
}

// musl's atomics (arch/x86_64/atomic_arch.h, #3592's step 6c), compiled by the direct path and run in this process (not
// under MSan, as the asm test): each shape on values a 32-bit operation and a 64-bit one would tell apart, and the shapes
// that still leave.
typedef int (*CDirectTestCas)(volatile int*, int, int);
typedef void* (*CDirectTestCasPointer)(volatile void*, void*, void*);
typedef int (*CDirectTestUpdate)(volatile int*, int);
typedef void (*CDirectTestUpdateVoid)(volatile int*, int);
typedef void (*CDirectTestUpdate64)(volatile unsigned long*, unsigned long);
typedef void (*CDirectTestStep)(volatile int*);
typedef int (*CDirectTestBits)(unsigned long);
typedef long (*CDirectTestLong)(long);
typedef void (*CDirectTestVoid)(void);
typedef void (*CDirectTestPointerVoid)(void*);

static bool c_direct_test_atomics(const char* obj_path)
{
    static const char source[] =
        "int t_cas(volatile int *p, int t, int s)\n"
        "{\n"
        "    __asm__ __volatile__ (\"lock ; cmpxchg %3, %1\" : \"=a\"(t), \"=m\"(*p) : \"a\"(t), \"r\"(s) : \"memory\");\n"
        "    return t;\n"
        "}\n"
        "void *t_cas_p(volatile void *p, void *t, void *s)\n"
        "{\n"
        "    __asm__( \"lock ; cmpxchg %3, %1\" : \"=a\"(t), \"=m\"(*(void *volatile *)p) : \"a\"(t), \"r\"(s) : \"memory\" );\n"
        "    return t;\n"
        "}\n"
        "int t_swap(volatile int *p, int v)\n"
        "{\n"
        "    __asm__ __volatile__(\"xchg %0, %1\" : \"=r\"(v), \"=m\"(*p) : \"0\"(v) : \"memory\");\n"
        "    return v;\n"
        "}\n"
        "int t_fetch_add(volatile int *p, int v)\n"
        "{\n"
        "    __asm__ __volatile__(\"lock ; xadd %0, %1\" : \"=r\"(v), \"=m\"(*p) : \"0\"(v) : \"memory\");\n"
        "    return v;\n"
        "}\n"
        "void t_and(volatile int *p, int v) { __asm__ __volatile__(\"lock ; and %1, %0\" : \"=m\"(*p) : \"r\"(v) : \"memory\"); }\n"
        "void t_or(volatile int *p, int v) { __asm__ __volatile__(\"lock ; or %1, %0\" : \"=m\"(*p) : \"r\"(v) : \"memory\"); }\n"
        "void t_and_64(volatile unsigned long *p, unsigned long v) { __asm__ __volatile(\"lock ; and %1, %0\" : \"=m\"(*p) : \"r\"(v) : \"memory\"); }\n"
        "void t_or_64(volatile unsigned long *p, unsigned long v) { __asm__ __volatile__(\"lock ; or %1, %0\" : \"=m\"(*p) : \"r\"(v) : \"memory\"); }\n"
        "void t_inc(volatile int *p) { __asm__ __volatile__(\"lock ; incl %0\" : \"=m\"(*p) : \"m\"(*p) : \"memory\"); }\n"
        "void t_dec(volatile int *p) { __asm__ __volatile__(\"lock ; decl %0\" : \"=m\"(*p) : \"m\"(*p) : \"memory\"); }\n"
        "void t_store(volatile int *p, int x) { __asm__ __volatile__(\"mov %1, %0 ; lock ; orl $0,(%%rsp)\" : \"=m\"(*p) : \"r\"(x) : \"memory\"); }\n"
        "void t_barrier(void) { __asm__ __volatile__( \"\" : : : \"memory\" ); }\n"
        "void t_spin(void) { __asm__ __volatile__( \"pause\" : : : \"memory\" ); }\n"
        "void t_crash(void) { __asm__ __volatile__( \"hlt\" : : : \"memory\" ); }\n"
        "int t_ctz_64(unsigned long x) { __asm__( \"bsf %1,%0\" : \"=r\"(x) : \"r\"(x) ); return x; }\n"
        "int t_clz_64(unsigned long x) { __asm__( \"bsr %1,%0 ; xor $63,%0\" : \"=r\"(x) : \"r\"(x) ); return x; }\n"
        "void t_keep(void *d) { __asm__ __volatile__ (\"\" : : \"r\"(d) : \"memory\"); }\n"
        "long t_through(long s) { __asm__ ( \"\" : \"+r\"(s) : : \"memory\" ); return s; }\n"
        "int t_cas_twice(volatile int *p, int t, int s)\n"
        "{\n"
        "    __asm__ __volatile__ (\"lock ; cmpxchg %3, %1\" : \"=a\"(t), \"=m\"(*p) : \"a\"(t), \"r\"(s) : \"memory\");\n"
        "    __asm__ __volatile__ (\"lock ; cmpxchg %3, %1\" : \"=a\"(t), \"=m\"(*p) : \"a\"(t), \"r\"(s) : \"memory\");\n"
        "    return t;\n"
        "}\n";
    bool ok = c_direct_test_compile_source_keep(source, obj_path, NULL, 0, NULL, NULL, NULL);
    if (!ok)
    {
        fprintf(stderr, "test-direct: musl's atomics left the direct path\n");
    }
#if !__has_feature(memory_sanitizer)
    static const char* const names[] = {
        "t_cas", "t_cas_p", "t_swap", "t_fetch_add", "t_and", "t_or", "t_and_64", "t_or_64", "t_inc", "t_dec",
        "t_store", "t_barrier", "t_spin", "t_ctz_64", "t_clz_64", "t_keep", "t_through", "t_crash", "t_cas_twice",
    };
    enum { count = sizeof(names) / sizeof(names[0]) };
    void* mappings[count] = { 0 };
    size_t mapped[count] = { 0 };
    const void* code[count] = { 0 };
    for (uint32_t i = 0; ok && i < (uint32_t)count; i += 1)
    {
        code[i] = c_direct_test_code(obj_path, names[i], mappings + i, mapped + i);
        ok = code[i] != NULL;
    }
    if (ok)
    {
        int x = 5;
        ok = ((CDirectTestCas)(uintptr_t)code[0])(&x, 5, 7) == 5 && x == 7 && ((CDirectTestCas)(uintptr_t)code[0])(&x, 5, 9) == 7 && x == 7;
        int a = 0;
        int b = 0;
        int c = 0;
        void* volatile q = &a;
        ok = ok && ((CDirectTestCasPointer)(uintptr_t)code[1])(&q, &a, &b) == &a && q == &b;
        ok = ok && ((CDirectTestCasPointer)(uintptr_t)code[1])(&q, &a, &c) == &b && q == &b;
        int y = 3;
        ok = ok && ((CDirectTestUpdate)(uintptr_t)code[2])(&y, 8) == 3 && y == 8;
        ok = ok && ((CDirectTestUpdate)(uintptr_t)code[3])(&y, 5) == 8 && y == 13;
        ((CDirectTestUpdateVoid)(uintptr_t)code[4])(&y, 6);
        ok = ok && y == 4;
        ((CDirectTestUpdateVoid)(uintptr_t)code[5])(&y, 3);
        ok = ok && y == 7;
        // The 64-bit forms reach the high half; the 32-bit ones leave the word after them alone.
        volatile unsigned long z = UINT64_C(0xf0f0f0f0f0f0f0f0);
        ((CDirectTestUpdate64)(uintptr_t)code[6])(&z, UINT64_C(0xff00ff00ff00ff00));
        ok = ok && z == UINT64_C(0xf000f000f000f000);
        ((CDirectTestUpdate64)(uintptr_t)code[7])(&z, UINT64_C(0x0100000000000001));
        ok = ok && z == UINT64_C(0xf100f000f000f001);
        volatile int pair[2] = { 7, 100 };
        ((CDirectTestStep)(uintptr_t)code[8])(pair);
        ok = ok && pair[0] == 8 && pair[1] == 100;
        ((CDirectTestStep)(uintptr_t)code[9])(pair);
        ((CDirectTestStep)(uintptr_t)code[9])(pair);
        ok = ok && pair[0] == 6 && pair[1] == 100;
        ((CDirectTestUpdateVoid)(uintptr_t)code[10])(pair, -42);
        ok = ok && pair[0] == -42 && pair[1] == 100;
        ((CDirectTestVoid)(uintptr_t)code[11])();
        ((CDirectTestVoid)(uintptr_t)code[12])();
        // bsf and bsr on values with bits at both ends, which tell the two apart and a 32-bit operation from a 64-bit one.
        ok = ok && ((CDirectTestBits)(uintptr_t)code[13])(UINT64_C(0x0100000000000080)) == 7 && ((CDirectTestBits)(uintptr_t)code[13])(UINT64_C(0x0300000000000000)) == 56;
        ok = ok && ((CDirectTestBits)(uintptr_t)code[14])(1u) == 63 && ((CDirectTestBits)(uintptr_t)code[14])(UINT64_C(0x00000100000000ff)) == 23;
        // A register output stored at its own size: the pointer beside it is read again.
        int w = 5;
        ok = ok && ((CDirectTestCas)(uintptr_t)code[18])(&w, 5, 7) == 7 && w == 7;
        ((CDirectTestPointerVoid)(uintptr_t)code[15])(&y);
        ok = ok && y == 7 && ((CDirectTestLong)(uintptr_t)code[16])(-123456789012L) == -123456789012L;
        // The locks, which a single thread can't tell, by the instructions' bytes; and hlt, which faults outside the kernel
        // and isn't run.
        static const struct
        {
            uint8_t function;
            uint8_t length;
            uint8_t bytes[6];
        } instructions[] = {
            { 0, 4, { 0xf0, 0x0f, 0xb1, 0x0a } },
            { 1, 5, { 0xf0, 0x48, 0x0f, 0xb1, 0x0a } },
            { 2, 2, { 0x87, 0x02 } },
            { 3, 4, { 0xf0, 0x0f, 0xc1, 0x02 } },
            { 4, 3, { 0xf0, 0x21, 0x02 } },
            { 5, 3, { 0xf0, 0x09, 0x02 } },
            { 6, 4, { 0xf0, 0x48, 0x21, 0x02 } },
            { 7, 4, { 0xf0, 0x48, 0x09, 0x02 } },
            { 8, 3, { 0xf0, 0xff, 0x02 } },
            { 9, 3, { 0xf0, 0xff, 0x0a } },
            { 10, 6, { 0x89, 0x02, 0xf0, 0x83, 0x0c, 0x24 } },
            { 12, 2, { 0xf3, 0x90 } },
            { 17, 1, { 0xf4 } },
        };
        for (uint32_t i = 0; ok && i < (uint32_t)(sizeof(instructions) / sizeof(instructions[0])); i += 1)
        {
            const uint8_t* const bytes = (const uint8_t*)code[instructions[i].function];
            bool found = false;
            for (uint32_t at = 0; !found && at + instructions[i].length <= 128u; at += 1)
            {
                found = memcmp(bytes + at, instructions[i].bytes, instructions[i].length) == 0;
            }
            ok = found;
        }
        if (!ok)
        {
            fprintf(stderr, "test-direct: musl's atomics ran wrong\n");
        }
    }
    for (uint32_t i = 0; i < (uint32_t)count; i += 1)
    {
        if (mappings[i] != NULL)
        {
            munmap(mappings[i], mapped[i]);
        }
    }
#endif
    unlink(obj_path);
    // What leaves: another constraint or operand, a memory operand that isn't `*p` of a pointer local, sizes that differ,
    // an operand too many or too few, a clobber besides memory, and anything after the operands.
    static const char* const leaves[] = {
        "int f(volatile int *p, int t, int s) { __asm__ (\"lock ; cmpxchg %3, %1\" : \"=r\"(t), \"=m\"(*p) : \"a\"(t), \"r\"(s) : \"memory\"); return t; }\n",
        "int f(volatile int *p, int t, int s) { __asm__ (\"lock ; cmpxchg %3, %1\" : \"=a\"(t), \"=m\"(*p) : \"r\"(t), \"r\"(s) : \"memory\"); return t; }\n",
        "int f(volatile int *p, int t, int s) { __asm__ (\"lock ; cmpxchg %3, %1\" : \"=a\"(t), \"=m\"(*p) : \"a\"(t), \"r\"(s)); return t; }\n",
        "int f(volatile int *p, int t, int s) { __asm__ (\"lock ; cmpxchg %3, %1\" : \"=a\"(t), \"=m\"(p[1]) : \"a\"(t), \"r\"(s) : \"memory\"); return t; }\n",
        "int f(volatile int *p, long t, long s) { __asm__ (\"lock ; cmpxchg %3, %1\" : \"=a\"(t), \"=m\"(*p) : \"a\"(t), \"r\"(s) : \"memory\"); return t; }\n",
        "volatile int *g;\nint f(int t, int s) { __asm__ (\"lock ; cmpxchg %3, %1\" : \"=a\"(t), \"=m\"(*g) : \"a\"(t), \"r\"(s) : \"memory\"); return t; }\n",
        "int f(volatile int *p, int t, int s) { __asm__ (\"lock ; cmpxchg %3, %1\" : \"=a\"(t), \"=m\"(*p) : \"a\"(t), \"r\"(s) : \"memory\", \"cc\"); return t; }\n",
        "int f(volatile int *p, int t, int s) { __asm__ (\"lock ; cmpxchg %3, %1\" : \"=a\"(t), \"=m\"(*p) : \"a\"(t), \"r\"(s), \"r\"(s) : \"memory\"); return t; }\n",
        "int f(volatile int *p, int v) { __asm__ (\"xchg %0, %1\" : \"=r\"(v), \"=m\"(*p) : \"r\"(v) : \"memory\"); return v; }\n",
        "int f(volatile short *p, short v) { __asm__ (\"xchg %0, %1\" : \"=r\"(v), \"=m\"(*p) : \"0\"(v) : \"memory\"); return v; }\n",
        "int f(volatile int *p, int v) { __asm__ (\"lock ; xadd %0, %1\" : \"=m\"(*p), \"=r\"(v) : \"0\"(v) : \"memory\"); return v; }\n",
        "void f(volatile int *p, long v) { __asm__ (\"lock ; and %1, %0\" : \"=m\"(*p) : \"r\"(v) : \"memory\"); }\n",
        "void f(volatile int *p, volatile int *q) { __asm__ (\"lock ; incl %0\" : \"=m\"(*p) : \"m\"(*q) : \"memory\"); }\n",
        "void f(volatile int *p, int x) { __asm__ (\"mov %1, %0 ; lock ; orl $0,(%%rsp)\" : \"=m\"(*p) : \"a\"(x) : \"memory\"); }\n",
        "void f(int x) { __asm__ (\"\" : \"=r\"(x)); }\n",
        "void f(void) { __asm__ (\"pause\" : : \"r\"(1)); }\n",
        "int f(unsigned long x) { __asm__ (\"bsf %1,%0\" : \"=r\"(x) : \"r\"(x) : \"memory\"); return x; }\n",
        "int f(unsigned long x) { int r; __asm__ (\"bsf %1,%0\" : \"=r\"(r) : \"r\"(x)); return r; }\n",
        "int f(double x) { __asm__ (\"bsf %1,%0\" : \"=r\"(x) : \"r\"(x)); return x; }\n",
        "void f(volatile int *p) { __asm__ (\"lock ; incl %0\" : \"=m\"(*p) : \"m\"(*p) : \"memory\") p; }\n",
        "void f(volatile int *p) { __asm__ (\"lock ; incl %0\" : \"=m\"(*(p)) : \"m\"(*p) : \"memory\"); }\n",
        "void f(void) { __asm__ (\"lock ; cmpxchg8b %0\" : : : \"memory\"); }\n",
        "void f(volatile int *p) { __asm__ (\"lock ; incl %0\" : \"=m\"(p) : \"m\"(p) : \"memory\"); }\n",
        "void f(volatile int *p) { __asm__ (\"lock ; incl %0\" : \"=m\"(*(long)p) : \"m\"(*(long)p) : \"memory\"); }\n",
        "int f(volatile int *p, int s) { const int t = 5; __asm__ (\"lock ; cmpxchg %3, %1\" : \"=a\"(t), \"=m\"(*p) : \"a\"(5), \"r\"(s) : \"memory\"); return t; }\n",
        "long f(volatile long *p, double t, long s) { __asm__ (\"lock ; cmpxchg %3, %1\" : \"=a\"(t), \"=m\"(*p) : \"a\"(s), \"r\"(s) : \"memory\"); return s; }\n",
        "void f(volatile long *p) { __asm__ (\"lock ; and %1, %0\" : \"=m\"(*p) : \"r\"(1.5) : \"memory\"); }\n",
        "int f(volatile int *p, int t, long s) { __asm__ (\"lock ; cmpxchg %3, %1\" : \"=a\"(t), \"=m\"(*p) : \"a\"(t), \"r\"(s) : \"memory\"); return t; }\n",
    };
    for (uint32_t i = 0; ok && i < (uint32_t)(sizeof(leaves) / sizeof(leaves[0])); i += 1)
    {
        ok = c_direct_test_compile_source(leaves[i], obj_path, 1, NULL, NULL);
        if (!ok)
        {
            fprintf(stderr, "test-direct: an atomic asm case the direct path took: %s", leaves[i]);
        }
    }
    return ok;
}

// HUGE_VAL (#3592): glibc's math.h writes it __builtin_huge_val(), an infinity as __builtin_inf() is, in code and in a
// static initializer; Lua's math and string libraries use it. The constants in .rodata: +inf as a double, the table's
// +inf and -inf, and +inf as a float.
static bool c_direct_test_huge_val(const char* obj_path)
{
    static const char source[] =
        "double t_huge(void) { return __builtin_huge_val(); }\n"
        "static const double table[2] = { __builtin_huge_val(), -__builtin_huge_val() };\n"
        "double t_table(int i) { return table[i]; }\n"
        "float t_hugef(void) { return __builtin_huge_valf(); }\n";
    static const uint8_t rodata[28] = {
        0, 0, 0, 0, 0, 0, 0xf0, 0x7f, 0, 0, 0, 0, 0, 0, 0xf0, 0x7f, 0, 0, 0, 0, 0, 0, 0xf0, 0xff, 0, 0, 0x80, 0x7f,
    };
    const bool ok = c_direct_test_compile_source_with(source, obj_path, 0, NULL, 0, false, NULL, NULL, NULL, NULL, rodata, sizeof(rodata),
                                                      (CDirectTestMode){ 0 });
    if (!ok)
    {
        fprintf(stderr, "test-direct: HUGE_VAL left the direct path or isn't an infinity\n");
    }
    return ok;
}

typedef int (*CDirectTestNone)(void);

// The object file at `path` whole, malloc'd, its size into *size; NULL if it can't be read.
static uint8_t* c_direct_test_file(const char* path, size_t* size)
{
    FILE* file = fopen(path, "rb");
    uint8_t* bytes = NULL;
    long length = 0;
    if (file != NULL && fseek(file, 0, SEEK_END) == 0 && (length = ftell(file)) > (long)sizeof(Elf64_Ehdr) && fseek(file, 0, SEEK_SET) == 0)
    {
        bytes = (uint8_t*)malloc((size_t)length);
        if (bytes != NULL && fread(bytes, 1, (size_t)length, file) != (size_t)length)
        {
            free(bytes);
            bytes = NULL;
        }
    }
    if (file != NULL)
    {
        fclose(file);
    }
    *size = bytes != NULL ? (size_t)length : 0u;
    return bytes;
}

// The bytes of the section named `name` in an object's bytes, their count into *size; NULL without one.
static const uint8_t* c_direct_test_section(const uint8_t* bytes, size_t size, const char* name, size_t* section_size)
{
    const uint8_t* result = NULL;
    *section_size = 0;
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)bytes;
    const bool sections = bytes != NULL && header->e_shoff + (uint64_t)header->e_shnum * sizeof(Elf64_Shdr) <= (uint64_t)size && header->e_shstrndx < header->e_shnum;
    const Elf64_Shdr* table = sections ? (const Elf64_Shdr*)(bytes + header->e_shoff) : NULL;
    for (uint16_t i = 0; sections && i < header->e_shnum && result == NULL; i += 1)
    {
        const uint64_t at = table[header->e_shstrndx].sh_offset + table[i].sh_name;
        if (at + strlen(name) + 1u <= (uint64_t)size && strcmp((const char*)bytes + at, name) == 0 && table[i].sh_offset + table[i].sh_size <= (uint64_t)size)
        {
            result = bytes + table[i].sh_offset;
            *section_size = (size_t)table[i].sh_size;
        }
    }
    return result;
}

// __PRETTY_FUNCTION__ is the function's signature in C as clang prints it (#3618, #3592). The 32 strings are clang
// 23's for the definitions in `source`, which the full pipeline prints alike; the lengths run in this process. A
// definition the printer doesn't take, or that clang and the full pipeline print differently (a parenthesized name, `()`
// after a declaration, a parameter's redundant parentheses), leaves. __func__ and __FUNCTION__ are the name.
static bool c_direct_test_pretty_function(const char* obj_path)
{
    static const char source[] =
        "typedef struct Q Qrf;\n"
        "typedef unsigned long long u64;\n"
        "typedef int (*cb_t)(void*, int);\n"
        "typedef int fn_t(int);\n"
        "typedef __builtin_va_list va_list;\n"
        "struct S { int a; };\n"
        "union U { int a; };\n"
        "enum E { E0 };\n"
        "const char *names[33];\n"
        "void p01(int a) { names[1] = __PRETTY_FUNCTION__; }\n"
        "void p02(Qrf *p, const char *z, int a[10], unsigned long n) { names[2] = __PRETTY_FUNCTION__; }\n"
        "int p03(void) { names[3] = __PRETTY_FUNCTION__; return 0; }\n"
        "int p04() { names[4] = __PRETTY_FUNCTION__; return 0; }\n"
        "void p05(int n, ...) { names[5] = __PRETTY_FUNCTION__; }\n"
        "int (*p06(int x))(int) { names[6] = __PRETTY_FUNCTION__; return 0; }\n"
        "void p07(const volatile int *p, int *restrict q, int *const r, char **argv, void *v, const void *cv) { names[7] = __PRETTY_FUNCTION__; }\n"
        "const int p08(void) { names[8] = __PRETTY_FUNCTION__; return 0; }\n"
        "const char *const p09(void) { names[9] = __PRETTY_FUNCTION__; return 0; }\n"
        "void (*p10(int sig, void (*func)(int)))(int) { names[10] = __PRETTY_FUNCTION__; return 0; }\n"
        "int (*p11(void))[3] { names[11] = __PRETTY_FUNCTION__; return 0; }\n"
        "void p12(fn_t g, int x[const 3], int y[static restrict 2], char z[][2][3]) { names[12] = __PRETTY_FUNCTION__; }\n"
        "static int p13(int x __attribute__((unused)), unsigned char c, long int l, int long m, unsigned u, signed s, short int si,\n"
        "               long long int lli, unsigned long long ull) { names[13] = __PRETTY_FUNCTION__; return 0; }\n"
        // (#3577: named, as the full pipeline wants a definition's parameters before C23; the names aren't printed.)
        "void p14(void (*f)(void), int (*g)(), char (*h)[]) { names[14] = __PRETTY_FUNCTION__; }\n"
        "void p15(va_list ap) { names[15] = __PRETTY_FUNCTION__; }\n"
        "void p16(__builtin_va_list ap) { names[16] = __PRETTY_FUNCTION__; }\n"
        "void p17(va_list *ap) { names[17] = __PRETTY_FUNCTION__; }\n"
        "_Bool p18(_Bool b) { names[18] = __PRETTY_FUNCTION__; return b; }\n"
        "void p19(signed char a, char b, unsigned char c, short d, unsigned short e) { names[19] = __PRETTY_FUNCTION__; }\n"
        "void p20(enum E e, union U *u, struct S s) { names[20] = __PRETTY_FUNCTION__; }\n"
        "void p21(const Qrf *p, Qrf const *q, Qrf *const r) { names[21] = __PRETTY_FUNCTION__; }\n"
        "char *(*p22(int a))[4] { names[22] = __PRETTY_FUNCTION__; return 0; }\n"
        "void p23(int (*fp)(int (*)(int), char *)) { names[23] = __PRETTY_FUNCTION__; }\n"
        "void p24(int *(*arr)[5]) { names[24] = __PRETTY_FUNCTION__; }\n"
        "void p25(char x[], const char y[], char *z[]) { names[25] = __PRETTY_FUNCTION__; }\n"
        "void p26(int a[0x10], int b[2u]) { names[26] = __PRETTY_FUNCTION__; }\n"
        "u64 *p27(cb_t cb) { names[27] = __PRETTY_FUNCTION__; return 0; }\n"
        "void p28(int (* const p)(void)) { names[28] = __PRETTY_FUNCTION__; }\n"
        "void p29(__int128 a, unsigned __int128 b) { names[29] = __PRETTY_FUNCTION__; }\n"
        "static __inline__ float p30(float a, double b) { names[30] = __PRETTY_FUNCTION__; return a; }\n"
        "int use(void) { return p13(0, 0, 0, 0, 0, 0, 0, 0, 0) + (int)p30(0, 0); }\n"
        "void p31(int (*fp)(int, ...), int n, ...) { names[31] = __PRETTY_FUNCTION__; }\n"
        "void p32(const char *const *volatile pp, int *restrict *q) { names[32] = __PRETTY_FUNCTION__; }\n";
    static const char* const expected[] = {
        "void p01(int)",
        "void p02(Qrf *, const char *, int *, unsigned long)",
        "int p03(void)",
        "int p04()",
        "void p05(int, ...)",
        "int (*p06(int))(int)",
        "void p07(const volatile int *, int *restrict, int *const, char **, void *, const void *)",
        "const int p08(void)",
        "const char *const p09(void)",
        "void (*p10(int, void (*)(int)))(int)",
        "int (*p11(void))[3]",
        "void p12(fn_t *, int *const, int *restrict, char (*)[2][3])",
        "int p13(int, unsigned char, long, long, unsigned int, int, short, long long, unsigned long long)",
        "void p14(void (*)(void), int (*)(), char (*)[])",
        "void p15(struct __va_list_tag *)",
        "void p16(struct __va_list_tag *)",
        "void p17(va_list *)",
        "_Bool p18(_Bool)",
        "void p19(signed char, char, unsigned char, short, unsigned short)",
        "void p20(enum E, union U *, struct S)",
        "void p21(const Qrf *, const Qrf *, Qrf *const)",
        "char *(*p22(int))[4]",
        "void p23(int (*)(int (*)(int), char *))",
        "void p24(int *(*)[5])",
        "void p25(char *, const char *, char **)",
        "void p26(int *, int *)",
        "u64 *p27(cb_t)",
        "void p28(int (*const)(void))",
        "void p29(__int128, unsigned __int128)",
        "float p30(float, double)",
        "void p31(int (*)(int, ...), int, ...)",
        "void p32(const char *const *volatile, int *restrict *)",
    };
    bool printed = c_direct_test_compile_source_keep(source, obj_path, NULL, 0, NULL, NULL, NULL);
    size_t size = 0;
    uint8_t* bytes = printed ? c_direct_test_file(obj_path, &size) : NULL;
    size_t strings_size = 0;
    const uint8_t* strings = c_direct_test_section(bytes, size, ".rodata.str1.1", &strings_size);
    // Each string, NUL-terminated, and nothing else.
    size_t total = 0;
    for (uint32_t i = 0; printed && i < (uint32_t)(sizeof(expected) / sizeof(expected[0])); i += 1)
    {
        const size_t length = strlen(expected[i]) + 1u;
        bool found = false;
        for (size_t at = 0; strings != NULL && !found && at + length <= strings_size; at += strlen((const char*)strings + at) + 1u)
        {
            found = memcmp(strings + at, expected[i], length) == 0;
        }
        printed = found;
        total += length;
        if (!found)
        {
            fprintf(stderr, "test-direct: __PRETTY_FUNCTION__ isn't \"%s\"\n", expected[i]);
        }
    }
    printed = printed && total == strings_size;
    free(bytes);
    unlink(obj_path);

    // The lengths, run: sizeof takes the string's.
    static const char lengths[] =
        "typedef struct Q Qrf;\n"
        "int t_short(void) { return (int)sizeof(__PRETTY_FUNCTION__); }\n"
        "int t_typed(Qrf *p, int a[3]) { return (int)sizeof(__PRETTY_FUNCTION__); }\n"
        "#define WHERE __PRETTY_FUNCTION__\n"
        "int t_macro(int x) { return (int)sizeof(WHERE) + x * 0; }\n"
        "int t_dead(int x) { return 1 ? (int)sizeof(__func__) : (int)sizeof(__PRETTY_FUNCTION__); }\n"
        "int t_func(void) { return (int)sizeof(__func__); }\n"
        "int t_function(void) { return (int)sizeof(__FUNCTION__); }\n"
        "typedef int T;\n"
        "int t_shadowed(T x) { int T = 3; return (int)sizeof(__PRETTY_FUNCTION__) + T * 0 + x * 0; }\n"
        "int t_empty() { return (int)sizeof(__PRETTY_FUNCTION__); }\n"
        "int t_bool(_Bool b) { return (int)sizeof(__PRETTY_FUNCTION__) + b * 0; }\n";
    // "int t_short(void)", "int t_typed(Qrf *, int *)", "int t_macro(int)", the names, "int t_shadowed(T)" (the
    // typedef, which a local shadows in the body), and in C17 and C23 "int t_empty()" and "int t_empty(void)", "int
    // t_bool(_Bool)" and "int t_bool(bool)".
    bool measured = true;
    for (uint32_t c23 = 0; measured && c23 < 2u; c23 += 1)
    {
        measured = c_direct_test_compile_source_with(lengths, obj_path, 0, NULL, 0, true, NULL, NULL, NULL, NULL, NULL, 0, (CDirectTestMode){ .c23 = c23 != 0 });
#if !__has_feature(memory_sanitizer)
        static const int sizes[2][9] = { { 18, 26, 17, 7, 7, 11, 18, 14, 18 }, { 18, 26, 17, 7, 7, 11, 18, 18, 17 } };
        static const char* const functions[] = { "t_short", "t_typed", "t_macro", "t_dead", "t_func", "t_function", "t_shadowed", "t_empty", "t_bool" };
        enum { count = sizeof(functions) / sizeof(functions[0]) };
        void* mappings[count] = { 0 };
        size_t mapped[count] = { 0 };
        for (uint32_t i = 0; measured && i < (uint32_t)count; i += 1)
        {
            const void* code = c_direct_test_code(obj_path, functions[i], mappings + i, mapped + i);
            measured = code != NULL && ((CDirectTestNone)(uintptr_t)code)() == sizes[c23][i];
            if (!measured)
            {
                fprintf(stderr, "test-direct: sizeof(__PRETTY_FUNCTION__) in %s isn't %d (C%s)\n", functions[i], sizes[c23][i], c23 != 0 ? "23" : "17");
            }
        }
        for (uint32_t i = 0; i < (uint32_t)count; i += 1)
        {
            if (mappings[i] != NULL)
            {
                munmap(mappings[i], mapped[i]);
            }
        }
#endif
        unlink(obj_path);
    }

    static const char* const leaves[] = {
        "int (f)(int a) { return (int)sizeof(__PRETTY_FUNCTION__); }\n",
        "int f(void);\nint f() { return (int)sizeof(__PRETTY_FUNCTION__); }\n",
        "typedef int arr4[4];\nint f(arr4 a) { return (int)sizeof(__PRETTY_FUNCTION__); }\n",
        "int f(int a[2 + 1]) { return (int)sizeof(__PRETTY_FUNCTION__); }\n",
        "int f(struct { int a; } *p) { return (int)sizeof(__PRETTY_FUNCTION__); }\n",
        "int f(int *p __attribute__((nonnull))) { return (int)sizeof(__PRETTY_FUNCTION__); }\n",
        "int __attribute__((mode(DI))) f(void) { return (int)sizeof(__PRETTY_FUNCTION__); }\n",
        "int f(__typeof__(1) a) { return (int)sizeof(__PRETTY_FUNCTION__); }\n",
        "int f(int g(int)) { return (int)sizeof(__PRETTY_FUNCTION__); }\n",
        "int f(int (*q)) { return (int)sizeof(__PRETTY_FUNCTION__); }\n",
        "unsigned long n;\nint ((*f(void)))[3] { n = sizeof(__PRETTY_FUNCTION__); return 0; }\n",
    };
    bool left = true;
    for (uint32_t i = 0; left && i < (uint32_t)(sizeof(leaves) / sizeof(leaves[0])); i += 1)
    {
        left = c_direct_test_compile_source(leaves[i], obj_path, 1, NULL, NULL);
        if (!left)
        {
            fprintf(stderr, "test-direct: __PRETTY_FUNCTION__ took the direct path: %s", leaves[i]);
        }
    }
    if (!printed || !measured)
    {
        fprintf(stderr, "test-direct: __PRETTY_FUNCTION__ isn't clang's (%s)\n", printed ? "its length" : "its string");
    }
    return printed && measured && left;
}

// glibc's assert in GNU's modes is a statement expression (#3592), which the direct path takes in its one shape by
// rewriting it as the conditional expression it computes: a unit with the shape, in a statement and in an expression
// (SQLite's ALWAYS) and in a dead branch, compiles to the object of the same unit with that expression written out. A
// statement expression of any other shape leaves.
static bool c_direct_test_assert(const char* obj_path)
{
    static const char shaped[] =
        "extern void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function);\n"
        "#define assert(expr) ((void) sizeof ((expr) ? 1 : 0), __extension__ ({ if (expr) ; else __assert_fail (#expr, __FILE__, __LINE__, __extension__ __PRETTY_FUNCTION__); }))\n"
        "#define ALWAYS(X) ((X) ? 1 : (assert(0), 0))\n"
        "int t_stmt(int x) { assert(x > 0 && (x & 1) == 0); return x * 3; }\n"
        "int t_expr(int *p, int x) { return ALWAYS(p != 0) ? p[x] : -x; }\n"
        "int t_dead(int x) { if (0) { assert(x); } else { x += 1; } return x; }\n"
        "int t_nested(int x) { assert((x ? x : 1) > 0); assert(x != 3); return x; }\n";
    static const char written[] =
        "extern void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function);\n"
        "#define assert(expr) ((void) sizeof ((expr) ? 1 : 0), __extension__ ((expr) ? (void) 0 : __assert_fail (#expr, __FILE__, __LINE__, __extension__ __PRETTY_FUNCTION__)))\n"
        "#define ALWAYS(X) ((X) ? 1 : (assert(0), 0))\n"
        "int t_stmt(int x) { assert(x > 0 && (x & 1) == 0); return x * 3; }\n"
        "int t_expr(int *p, int x) { return ALWAYS(p != 0) ? p[x] : -x; }\n"
        "int t_dead(int x) { if (0) { assert(x); } else { x += 1; } return x; }\n"
        "int t_nested(int x) { assert((x ? x : 1) > 0); assert(x != 3); return x; }\n";
    char written_path[300];
    snprintf(written_path, sizeof(written_path), "%s.written", obj_path);
    bool ok = c_direct_test_compile_source_keep(shaped, obj_path, NULL, 0, NULL, NULL, NULL) &&
              c_direct_test_compile_source_keep(written, written_path, NULL, 0, NULL, NULL, NULL);
    size_t shaped_size = 0;
    size_t written_size = 0;
    uint8_t* shaped_bytes = ok ? c_direct_test_file(obj_path, &shaped_size) : NULL;
    uint8_t* written_bytes = ok ? c_direct_test_file(written_path, &written_size) : NULL;
    ok = shaped_bytes != NULL && written_bytes != NULL && shaped_size == written_size && memcmp(shaped_bytes, written_bytes, shaped_size) == 0;
    // And it names __assert_fail and the signatures: the code has its call.
    size_t strings_size = 0;
    const uint8_t* strings = c_direct_test_section(shaped_bytes, shaped_size, ".rodata.str1.1", &strings_size);
    ok = ok && strings != NULL && memmem(strings, strings_size, "int t_expr(int *, int)", 23) != NULL && c_direct_test_symbol(obj_path, "__assert_fail").count == 1u;
    free(shaped_bytes);
    free(written_bytes);
    unlink(obj_path);
    unlink(written_path);
    if (!ok)
    {
        fprintf(stderr, "test-direct: glibc's assert compiles to another object than its conditional expression\n");
    }
    // After a comma, as glibc's has it, where the rewrite would happen: another callee, another statement after the
    // call, another then branch.
    static const char* const leaves[] = {
        "int f(int x) { return ((void)0, __extension__ ({ int y = x; y + 1; })); }\n",
        "extern void fail(void);\nint f(int x) { return ((void)0, __extension__ ({ if (x) ; else fail(); }), x); }\n",
        "extern void __assert_fail(void);\nint f(int x) { return ((void)0, __extension__ ({ if (x) ; else __assert_fail(); x; }), x); }\n",
        "extern void __assert_fail(void);\nint f(int x) { return ((void)0, __extension__ ({ if (x) x += 1; else __assert_fail(); }), x); }\n",
        "int f(int x) { return ({ x + 1; }); }\n",
    };
    bool left = true;
    for (uint32_t i = 0; left && i < (uint32_t)(sizeof(leaves) / sizeof(leaves[0])); i += 1)
    {
        left = c_direct_test_compile_source(leaves[i], obj_path, 1, NULL, NULL);
        if (!left)
        {
            fprintf(stderr, "test-direct: a statement expression took the direct path: %s", leaves[i]);
        }
    }
    return ok && left;
}

typedef double (*CDirectTestRound)(double);
typedef float (*CDirectTestRoundf)(float);
typedef double (*CDirectTestMixed)(double, int);
typedef long (*CDirectTestLive)(long, double);

// __builtin_ceil, __builtin_floor and their float forms (#3592) are computed inline as the full pipeline computes them:
// each special input's result, run, is this process's own __builtin_ceil's or __builtin_floor's bits (clang's ceil and
// floor, which match the full pipeline's helper), MXCSR's DAZ makes a denormal zero, and no exception flag is raised.
static bool c_direct_test_round(const char* obj_path)
{
    static const char source[] =
        "double t_ceil(double x) { return __builtin_ceil(x); }\n"
        "double t_floor(double x) { return __builtin_floor(x); }\n"
        "float t_ceilf(float x) { return __builtin_ceilf(x); }\n"
        "float t_floorf(float x) { return __builtin_floorf(x); }\n"
        "double t_mixed(double x, int n) { return __builtin_ceil(n) + __builtin_floor(x + x); }\n"
        "long t_live(long a, double x) { return a * 3 + (long)__builtin_floor(x) + a * 5; }\n";
    bool ok = c_direct_test_compile_source_keep(source, obj_path, NULL, 0, NULL, NULL, NULL);
#if !__has_feature(memory_sanitizer)
    static const char* const names[] = { "t_ceil", "t_floor", "t_ceilf", "t_floorf", "t_mixed", "t_live" };
    enum { count = sizeof(names) / sizeof(names[0]) };
    void* mappings[count] = { 0 };
    size_t mapped[count] = { 0 };
    const void* code[count] = { 0 };
    for (uint32_t i = 0; ok && i < (uint32_t)count; i += 1)
    {
        code[i] = c_direct_test_code(obj_path, names[i], mappings + i, mapped + i);
        ok = code[i] != NULL;
    }
    static const uint64_t inputs[] = {
        UINT64_C(0x7ff8000000000001), UINT64_C(0x7ff0000000000001), UINT64_C(0xfff4000000000000), UINT64_C(0x7ff0000000000000),
        UINT64_C(0xfff0000000000000), UINT64_C(0), UINT64_C(0x8000000000000000), UINT64_C(1), UINT64_C(0x8000000000000001),
        UINT64_C(0x000fffffffffffff), UINT64_C(0x3fe0000000000000), UINT64_C(0xbfe0000000000000), UINT64_C(0x3ff8000000000000),
        UINT64_C(0xbff8000000000000), UINT64_C(0x4330000000000000), UINT64_C(0x432fffffffffffff), UINT64_C(0xc32fffffffffffff),
        UINT64_C(0x3ff0000000000000), UINT64_C(0x400c000000000000), UINT64_C(0xc00c000000000000), UINT64_C(0x4415af1d78b58c40),
        UINT64_C(0x3fefffffffffffff), UINT64_C(0xbfefffffffffffff), UINT64_C(0x3f50624dd2f1a9fc), UINT64_C(0xc0c3880000000001),
    };
    const unsigned int csr = _mm_getcsr();
    for (uint32_t daz = 0; ok && daz < 2u; daz += 1)
    {
        for (uint32_t i = 0; ok && i < (uint32_t)(sizeof(inputs) / sizeof(inputs[0])); i += 1)
        {
            double bits = 0;
            memcpy(&bits, &inputs[i], 8);
            const volatile double x = bits;
            const volatile float xf = (float)bits;
            // This process's own results, in its default mode and stored before MXCSR changes: clang moves a
            // computation it takes as pure across _mm_setcsr. With DAZ, a denormal is a zero of its sign.
            const bool denormal = (inputs[i] & UINT64_C(0x7ff0000000000000)) == 0 && (inputs[i] & UINT64_C(0x000fffffffffffff)) != 0;
            const uint64_t zero = inputs[i] & UINT64_C(0x8000000000000000);
            double zero_value = 0;
            memcpy(&zero_value, &zero, 8);
            const volatile double ceil_theirs = daz != 0 && denormal ? zero_value : __builtin_ceil(x);
            const volatile double floor_theirs = daz != 0 && denormal ? zero_value : __builtin_floor(x);
            const volatile float ceilf_theirs = __builtin_ceilf(xf);
            const volatile float floorf_theirs = __builtin_floorf(xf);
            _mm_setcsr((csr & ~0x3fu & ~0x40u) | (daz != 0 ? 0x40u : 0u));
            const double ceil_ours = ((CDirectTestRound)(uintptr_t)code[0])(x);
            const double floor_ours = ((CDirectTestRound)(uintptr_t)code[1])(x);
            const float ceilf_ours = ((CDirectTestRoundf)(uintptr_t)code[2])(xf);
            const float floorf_ours = ((CDirectTestRoundf)(uintptr_t)code[3])(xf);
            const unsigned int raised = _mm_getcsr() & 0x3fu;
            _mm_setcsr(csr);
            // The flags the inputs' signaling NaNs raise (invalid, once each way) are clang's too; none other is.
            const bool signaling = (inputs[i] & UINT64_C(0x7ff8000000000000)) == UINT64_C(0x7ff0000000000000) && (inputs[i] & UINT64_C(0x000fffffffffffff)) != 0;
            const double ceil_expected = ceil_theirs;
            const double floor_expected = floor_theirs;
            const float ceilf_expected = ceilf_theirs;
            const float floorf_expected = floorf_theirs;
            ok = memcmp(&ceil_ours, &ceil_expected, 8) == 0 && memcmp(&floor_ours, &floor_expected, 8) == 0 && memcmp(&ceilf_ours, &ceilf_expected, 4) == 0 &&
                 memcmp(&floorf_ours, &floorf_expected, 4) == 0 && (signaling || raised == 0);
            if (!ok)
            {
                fprintf(stderr, "test-direct: ceil or floor of %016llx (DAZ %u) differs (flags %#x)\n", (unsigned long long)inputs[i], daz, raised);
            }
        }
    }
    // An int argument converted, an expression's, and a value live in a register across the sequence.
    ok = ok && ((CDirectTestMixed)(uintptr_t)code[4])(-2.75, -2) == -8.0 && ((CDirectTestMixed)(uintptr_t)code[4])(7.5, 7) == 22.0 &&
         ((CDirectTestLive)(uintptr_t)code[5])(7, 2.5) == 58;
    for (uint32_t i = 0; i < (uint32_t)count; i += 1)
    {
        if (mappings[i] != NULL)
        {
            munmap(mappings[i], mapped[i]);
        }
    }
#endif
    unlink(obj_path);
    static const char* const leaves[] = {
        "double f(void) { return __builtin_ceil(1.0, 2.0); }\n",
        "double f(int *p) { return __builtin_floor(p); }\n",
        "struct s { int a; };\ndouble f(struct s v) { return __builtin_ceil(v); }\n",
    };
    bool left = true;
    for (uint32_t i = 0; left && i < (uint32_t)(sizeof(leaves) / sizeof(leaves[0])); i += 1)
    {
        left = c_direct_test_compile_source(leaves[i], obj_path, 1, NULL, NULL);
        if (!left)
        {
            fprintf(stderr, "test-direct: a ceil or floor that clang refuses took the direct path: %s", leaves[i]);
        }
    }
    if (!ok)
    {
        fprintf(stderr, "test-direct: ceil and floor aren't the full pipeline's\n");
    }
    return ok && left;
}

typedef int (*CDirectTestChain)(int);

// A function's statements nest up to BUSTER_FSTMT_FRAMES frames (#3592): an `else if` chain takes one a branch, and
// SQLite's shell.c's do_meta_command has 72. A chain of 200 compiles and runs; one of 300 leaves.
static bool c_direct_test_statement_depth(const char* obj_path)
{
    bool ok = true;
    for (uint32_t round = 0; ok && round < 2u; round += 1)
    {
        const uint32_t branches = round == 0 ? 200u : 300u;
        char* source = (char*)malloc(64u * branches + 128u);
        ok = source != NULL;
        size_t length = 0;
        if (ok)
        {
            length += (size_t)sprintf(source + length, "int t_chain(int x)\n{\n    if (x == 0) { return 1000; }\n");
            for (uint32_t i = 1; i < branches; i += 1)
            {
                length += (size_t)sprintf(source + length, "    else if (x == %u) { return %u; }\n", i, 1000u + i * 3u);
            }
            sprintf(source + length, "    else { return -1; }\n}\n");
            ok = round == 0 ? c_direct_test_compile_source_keep(source, obj_path, NULL, 0, NULL, NULL, NULL) : c_direct_test_compile_source(source, obj_path, 1, NULL, NULL);
        }
#if !__has_feature(memory_sanitizer)
        if (ok && round == 0)
        {
            void* mapping = NULL;
            size_t mapped = 0;
            const void* code = c_direct_test_code(obj_path, "t_chain", &mapping, &mapped);
            ok = code != NULL && ((CDirectTestChain)(uintptr_t)code)(0) == 1000 && ((CDirectTestChain)(uintptr_t)code)(150) == 1450 &&
                 ((CDirectTestChain)(uintptr_t)code)(199) == 1597 && ((CDirectTestChain)(uintptr_t)code)(200) == -1;
            if (mapping != NULL)
            {
                munmap(mapping, mapped);
            }
        }
#endif
        unlink(obj_path);
        free(source);
        if (!ok)
        {
            fprintf(stderr, "test-direct: an else-if chain of %u branches %s\n", branches, round == 0 ? "didn't compile and run" : "took the direct path");
        }
    }
    return ok;
}

typedef int (*CDirectTestThree)(int, int, int);

// A sizeof operand has no code, and no value of it holds a register (#3616): releasing a call's argument or a comma's
// left side there freed rax, under the live `a * b`, and the next value went into it.
static bool c_direct_test_unevaluated_registers(const char* obj_path)
{
    static const char source[] =
        "int t_pick(int a, int b) { return a + b; }\n"
        "int t_call(int a, int b, int c) { return (a * b) + (int)sizeof(t_pick(a + 1, 0)) * c; }\n"
        "int t_comma(int a, int b, int c) { return (a * b) + (int)sizeof((a + 1, c)) * c; }\n"
        "int t_after(int a, int b, int c) { return (a * b) + c * (int)sizeof(t_pick(a - 1, b + 1)); }\n";
    bool ok = c_direct_test_compile_source_keep(source, obj_path, NULL, 0, NULL, NULL, NULL);
#if !__has_feature(memory_sanitizer)
    static const char* const names[] = { "t_call", "t_comma", "t_after" };
    enum { count = sizeof(names) / sizeof(names[0]) };
    void* mappings[count] = { 0 };
    size_t mapped[count] = { 0 };
    for (uint32_t i = 0; ok && i < (uint32_t)count; i += 1)
    {
        const void* code = c_direct_test_code(obj_path, names[i], mappings + i, mapped + i);
        ok = code != NULL && ((CDirectTestThree)(uintptr_t)code)(3, 2, 5) == 26;
    }
    for (uint32_t i = 0; i < (uint32_t)count; i += 1)
    {
        if (mappings[i] != NULL)
        {
            munmap(mappings[i], mapped[i]);
        }
    }
#endif
    unlink(obj_path);
    if (!ok)
    {
        fprintf(stderr, "test-direct: a sizeof operand freed a live register\n");
    }
    return ok;
}

typedef int (*CDirectTestPick)(int, int);
typedef double (*CDirectTestMix)(int);
typedef long (*CDirectTestWide)(int);
typedef int (*CDirectTestSide)(int*);
typedef int (*CDirectTestOne)(int);

// Dead arms (#3592): a `?:` or an `if` whose condition is a constant checks its dead side as an operand of sizeof is,
// with no code and no symbol, as the full pipeline drops it. musl's isnan(), signbit() and FORCE_EVAL() of a float or a
// double have a long double side, a call that only code would refuse; and its a_ctz_l() names a_ctz_32 on a 64-bit
// target. The live side keeps its value, in the common type, as an rvalue. A dead branch with a declaration, a jump, a
// loop or a label has code as before; and a dead side's call is still checked, as a sizeof operand's is.
static bool c_direct_test_dead_arms(const char* obj_path)
{
    static const char musl[] =
        "int __fpclassifyl(long double);\n"
        "int __signbitl(long double);\n"
        "static __inline unsigned __FLOAT_BITS(float __f) { union {float __f; unsigned __i;} __u; __u.__f = __f; return __u.__i; }\n"
        "static __inline unsigned long long __DOUBLE_BITS(double __f) { union {double __f; unsigned long long __i;} __u; __u.__f = __f; return __u.__i; }\n"
        "#define isnan(x) ( sizeof(x) == sizeof(float) ? (__FLOAT_BITS(x) & 0x7fffffff) > 0x7f800000 : \\\n"
        "    sizeof(x) == sizeof(double) ? (__DOUBLE_BITS(x) & -1ULL>>1) > 0x7ffULL<<52 : __fpclassifyl(x) == 0)\n"
        "#define signbit(x) ( sizeof(x) == sizeof(float) ? (int)(__FLOAT_BITS(x)>>31) : \\\n"
        "    sizeof(x) == sizeof(double) ? (int)(__DOUBLE_BITS(x)>>63) : __signbitl(x) )\n"
        "static inline void fp_force_evalf(float x) { volatile float y; y = x; }\n"
        "static inline void fp_force_eval(double x) { volatile double y; y = x; }\n"
        // (#3577: the direct path checks an inline body where it is defined, and a long double one leaves it: musl's
        // fp_force_evall() is declared here, its call as dead as with its definition.)
        "void fp_force_evall(long double x);\n"
        "#define FORCE_EVAL(x) do { if (sizeof(x) == sizeof(float)) { fp_force_evalf(x); } \\\n"
        "    else if (sizeof(x) == sizeof(double)) { fp_force_eval(x); } else { fp_force_evall(x); } } while(0)\n"
        "static inline int a_ctz_32(unsigned x) { return (int)x; }\n"
        "static inline int a_ctz_64(unsigned long x) { return (int)x; }\n"
        "static inline int a_ctz_l(unsigned long x) { return (sizeof(long) < 8) ? a_ctz_32(x) : a_ctz_64(x); }\n"
        "int t_isnan(double x) { return isnan(x); }\n"
        "int t_signbitf(float x) { return signbit(x); }\n"
        "double t_force(double x) { FORCE_EVAL(x); return x; }\n"
        "int t_ctz(unsigned long x) { return a_ctz_l(x); }\n"
        "int never_and(int);\n"
        "int never_or(int);\n"
        "int t_logic(int x) { return (0 && never_and(x)) + (1 || never_or(x)); }\n";
    bool ok = c_direct_test_compile_source_keep(musl, obj_path, NULL, 0, NULL, NULL, NULL);
    static const char* const present[] = { "__FLOAT_BITS", "__DOUBLE_BITS", "fp_force_eval", "a_ctz_64", "a_ctz_l" };
    static const char* const absent[] = { "__fpclassifyl", "__signbitl", "fp_force_evall", "fp_force_evalf", "a_ctz_32", "never_and", "never_or" };
    for (uint32_t i = 0; ok && i < (uint32_t)(sizeof(present) / sizeof(present[0])); i += 1)
    {
        ok = c_direct_test_symbol(obj_path, present[i]).count == 1u;
    }
    for (uint32_t i = 0; ok && i < (uint32_t)(sizeof(absent) / sizeof(absent[0])); i += 1)
    {
        ok = c_direct_test_symbol(obj_path, absent[i]).count == 0u;
    }
    if (!ok)
    {
        fprintf(stderr, "test-direct: musl's dead arms left the direct path or kept a symbol\n");
    }

    // The values, run: no function calls another, so that their section runs where it is copied.
    static const char values[] =
        "struct S { int a, b; };\n"
        "int t_pick(int a, int b) { return 1 ? a : b; }\n"
        "int t_pick0(int a, int b) { return 0 ? a : b; }\n"
        "double t_mix(int a) { return 1 ? a : 2.5; }\n"
        "double t_mix0(int a) { return 0 ? 2.5 : a; }\n"
        "long t_wide(int a) { return sizeof(a) == 4 ? a : 1L; }\n"
        "int t_side(int *p) { return 0 ? (*p = 7) : 3; }\n"
        "int t_side1(int *p) { return 1 ? 4 : (*p = 7); }\n"
        "int t_nest(int a) { return sizeof(a) == 8 ? 1 : sizeof(a) == 4 ? a + 1 : 3; }\n"
        "int t_cmp(int a, int b) { return 1 ? a < b : 0; }\n"
        "int t_rec(int a, int b) { struct S s = { a, b }; struct S t = { b, a }; return (0 ? s : t).b * 10 + (1 ? s : t).b; }\n"
        "int t_if(int a) { if (sizeof(a) == 4) { a += 1; } else { a += 2; } return a; }\n"
        "int t_if0(int a) { if (sizeof(a) == 8) { a += 1; } else { a += 2; } return a; }\n"
        "int t_chain(int a) { if (sizeof(a) == 2) { a = 1; } else if (sizeof(a) == 4) { a = 2; } else { a = 3; } return a; }\n"
        "int t_dangle(int a) { if (0) { if (a) a = 5; else a = 6; } return a; }\n"
        "int t_inner(int a) { if (1) { a += 10; } else { if (a) { a = 2; } else { a = 3; } } return a; }\n"
        "int t_void(int *p) { 1 ? (void)(*p = 1) : (void)(*p = 2); 0 ? (void)(*p += 5) : (void)(*p += 7); return *p; }\n"
        "int t_logic(int *p) { return (0 && (*p = 5)) * 10 + (1 || (*p = 6)) + (0 && *p || 1) * 100; }\n"
        "int t_after(int a) { if (1) { a += 1; } else { a = 7; } a = a * 3; if (0) { a = 9; } else { a += 2; } return a * 5 + 1; }\n";
    ok = ok && c_direct_test_compile_source_keep(values, obj_path, NULL, 0, NULL, NULL, NULL);
    if (!ok)
    {
        fprintf(stderr, "test-direct: constant conditions' values left the direct path\n");
    }
#if !__has_feature(memory_sanitizer)
    static const char* const names[] = {
        "t_pick", "t_pick0", "t_mix", "t_mix0", "t_wide", "t_side", "t_side1", "t_nest", "t_cmp", "t_rec",
        "t_if", "t_if0", "t_chain", "t_dangle", "t_inner", "t_void", "t_logic", "t_after",
    };
    enum { count = sizeof(names) / sizeof(names[0]) };
    void* mappings[count] = { 0 };
    size_t mapped[count] = { 0 };
    const void* code[count] = { 0 };
    for (uint32_t i = 0; ok && i < (uint32_t)count; i += 1)
    {
        code[i] = c_direct_test_code(obj_path, names[i], mappings + i, mapped + i);
        ok = code[i] != NULL;
    }
    if (ok)
    {
        int v = 1;
        ok = ((CDirectTestPick)(uintptr_t)code[0])(3, 4) == 3 && ((CDirectTestPick)(uintptr_t)code[1])(3, 4) == 4 &&
             ((CDirectTestMix)(uintptr_t)code[2])(-3) == -3.0 && ((CDirectTestMix)(uintptr_t)code[3])(5) == 5.0 &&
             ((CDirectTestWide)(uintptr_t)code[4])(-2) == -2L && ((CDirectTestSide)(uintptr_t)code[5])(&v) == 3 && v == 1 &&
             ((CDirectTestSide)(uintptr_t)code[6])(&v) == 4 && v == 1 && ((CDirectTestOne)(uintptr_t)code[7])(5) == 6 &&
             ((CDirectTestPick)(uintptr_t)code[8])(1, 2) == 1 && ((CDirectTestPick)(uintptr_t)code[8])(2, 1) == 0 &&
             ((CDirectTestPick)(uintptr_t)code[9])(3, 4) == 34 && ((CDirectTestOne)(uintptr_t)code[10])(1) == 2 &&
             ((CDirectTestOne)(uintptr_t)code[11])(1) == 3 && ((CDirectTestOne)(uintptr_t)code[12])(0) == 2 &&
             ((CDirectTestOne)(uintptr_t)code[13])(1) == 1 && ((CDirectTestOne)(uintptr_t)code[14])(1) == 11 &&
             ((CDirectTestSide)(uintptr_t)code[15])(&v) == 8 && ((CDirectTestSide)(uintptr_t)code[16])(&v) == 101 && v == 8 &&
             ((CDirectTestOne)(uintptr_t)code[17])(1) == 41;
        if (!ok)
        {
            fprintf(stderr, "test-direct: constant conditions ran wrong\n");
        }
    }
    for (uint32_t i = 0; i < (uint32_t)count; i += 1)
    {
        if (mappings[i] != NULL)
        {
            munmap(mappings[i], mapped[i]);
        }
    }
#endif
    unlink(obj_path);

    // A dead branch with code the dead mode doesn't take compiles as before, its call kept; a simple one keeps none.
    static const struct
    {
        const char* source;
        uint32_t calls;
    } branches[] = {
        { "void never(int);\nint f(int a) { if (0) never(a); return a; }\n", 0u },
        { "void never(int);\nint f(int a) { if (sizeof(a) == 4) { a += 1; } else { never(a); } return a; }\n", 0u },
        { "void never(int);\nint f(int a) { if (0) { int z = a; never(z); } return a; }\n", 1u },
        { "void never(int);\nint f(int a) { if (0) { never(a); return 1; } return a; }\n", 1u },
        { "void never(int);\nint f(int a) { if (0) { while (a) never(a); } return a; }\n", 1u },
        { "void never(int);\nint f(int a) { if (0) { l: never(a); } return a; }\n", 1u },
        { "void never(int);\nint f(int a) { if (0) { switch (a) { case 1: never(a); } } return a; }\n", 1u },
        { "void never(int);\nint f(int a) { if (1) { a += 1; } else { do never(a); while (a); } return a; }\n", 1u },
        { "void never(int);\nint f(int a) { if (a) { never(a); } return a; }\n", 1u },
        { "void never(int);\nint f(int a) { if (0) { if (a) never(a); else return 1; } return a; }\n", 1u },
        { "void never(int);\nint f(int a) { if (0) { if (a) { never(a); } } else { a += 1; } return a; }\n", 0u },
        { "void never(int);\nint f(int a) { if (1) { a += 1; } else { a += 2; } never(a); return a; }\n", 1u },
        { "void never(int);\nint f(int a) { if (0) { a += 1; } never(a); return a; }\n", 1u },
    };
    for (uint32_t i = 0; ok && i < (uint32_t)(sizeof(branches) / sizeof(branches[0])); i += 1)
    {
        ok = c_direct_test_compile_source_keep(branches[i].source, obj_path, NULL, 0, NULL, NULL, NULL) &&
             c_direct_test_symbol(obj_path, "never").count == branches[i].calls;
        unlink(obj_path);
        if (!ok)
        {
            fprintf(stderr, "test-direct: a dead branch's code is wrong: %s", branches[i].source);
        }
    }

    // A dead side has no code: each pair's text is as long with a dead side that would have code as without it.
    static const char* const same[][2] = {
        { "int g(int);\nint f(int a) { if (0) { if (a) { a = g(a); } else { a = 2; } } return a; }\n",
          "int g(int);\nint f(int a) { if (0) { } return a; }\n" },
        { "int g(int);\nint f(int a) { if (1) { a += 1; } else { a = g(a); } return a; }\n",
          "int g(int);\nint f(int a) { if (1) { a += 1; } else { } return a; }\n" },
        { "int g(int);\nint f(int a) { return 1 ? a : g(a) + a * 3; }\n",
          "int g(int);\nint f(int a) { return 1 ? a : 0; }\n" },
        { "int g(int);\nint f(int a) { return 0 ? g(a) + a * 3 : a; }\n",
          "int g(int);\nint f(int a) { return 0 ? 0 : a; }\n" },
        { "int g(int);\nint f(int a) { return (0 && g(a)) + (1 || g(a + 1)); }\n",
          "int g(int);\nint f(int a) { return (0 && 0) + (1 || 0); }\n" },
        { "int g(int);\nint f(int a, int b, int c) { return a * b + (1 ? c : g(c)); }\n",
          "int g(int);\nint f(int a, int b, int c) { return a * b + (0 ? g(c) : c); }\n" },
    };
    for (uint32_t i = 0; ok && i < (uint32_t)(sizeof(same) / sizeof(same[0])); i += 1)
    {
        uint32_t dead_text = 0;
        uint32_t empty_text = 0;
        ok = c_direct_test_compile_source(same[i][0], obj_path, 0, &dead_text, NULL) &&
             c_direct_test_compile_source(same[i][1], obj_path, 0, &empty_text, NULL) && dead_text == empty_text;
        if (!ok)
        {
            fprintf(stderr, "test-direct: a dead side has code (%u bytes against %u): %s", dead_text, empty_text, same[i][0]);
        }
    }

    // What leaves: a dead side's call with the wrong arguments, a constant condition's value as an lvalue or a null
    // pointer constant, and an undeclared name in a dead side.
    static const char* const leaves[] = {
        "int g(int);\nint f(void) { return 0 ? g(1, 2) : 0; }\n",
        "int g(int);\nint f(void) { if (0) g(1, 2); return 0; }\n",
        "int g(int *);\nint f(double *p) { return 0 ? g(p) : 0; }\n",
        "int g(int *);\nint f(double *p) { if (0) { g(p); } return 0; }\n",
        "int f(int a, int b) { (1 ? a : b) = 5; return a; }\n",
        "int *f(int x) { return 1 ? 0 : x; }\n",
        "int f(void) { return 0 ? undeclared(1) : 0; }\n",
        "int f(void) { if (0) { undeclared(1); } return 0; }\n",
        "struct S { int a; };\nint f(struct S s) { return 0 && s; }\n",
        "struct S { int a; };\nvoid f(struct S s, struct S t) { (1 ? s : t).a = 5; }\n",
        "struct S { int a; };\nvoid f(struct S s, struct S t) { (0 ? s : t).a = 5; }\n",
        "int *f(int x) { return 0 && x; }\n",
        "int *f(int x) { return 1 || x; }\n",
        "int g(int);\nint f(void) { return 1 || g(1, 2); }\n",
    };
    for (uint32_t i = 0; ok && i < (uint32_t)(sizeof(leaves) / sizeof(leaves[0])); i += 1)
    {
        ok = c_direct_test_compile_source(leaves[i], obj_path, 1, NULL, NULL);
        if (!ok)
        {
            fprintf(stderr, "test-direct: a dead side the direct path took: %s", leaves[i]);
        }
    }
    return ok;
}

// What the dependency test expects of the records (#3592): the paths after the test's directory, in order, and the flags.
typedef struct CDirectTestDependency CDirectTestDependency;
struct CDirectTestDependency
{
    const char* path;
    bool used;
    bool system;
};

typedef struct CDirectTestDependencies CDirectTestDependencies;
struct CDirectTestDependencies
{
    const char* directory;
    const CDirectTestDependency* expected;
    uint32_t expected_count;
    bool accept;
    bool called;
    bool matched;
};

// CDirectRequest.dependencies for the test: whether the records are the expected ones (any, without a list), and the
// test's answer.
static bool c_direct_test_dependency_records(void* context, const CDirectDependency* files, uint32_t count)
{
    CDirectTestDependencies* const test = (CDirectTestDependencies*)context;
    const size_t prefix = strlen(test->directory);
    bool matched = test->expected == NULL || count == test->expected_count;
    for (uint32_t i = 0; matched && test->expected != NULL && i < count; i += 1)
    {
        const CDirectTestDependency want = test->expected[i];
        const bool absolute = want.path[0] == '/';
        const size_t length = strlen(want.path);
        matched = files[i].used == want.used && files[i].system == want.system && files[i].length == strlen(files[i].path) &&
                  (absolute ? files[i].length == length && memcmp(files[i].path, want.path, length) == 0
                            : files[i].length == prefix + 1u + length && memcmp(files[i].path, test->directory, prefix) == 0 && files[i].path[prefix] == '/' &&
                                  memcmp(files[i].path + prefix + 1u, want.path, length) == 0);
        if (!matched)
        {
            fprintf(stderr, "test-direct: dependency %u is %.*s (used %d, system %d), not %s (used %d, system %d)\n", i, (int)files[i].length, files[i].path,
                    files[i].used, files[i].system, want.path, want.used, want.system);
        }
    }
    if (test->expected != NULL && count != test->expected_count)
    {
        fprintf(stderr, "test-direct: %u dependency records, not %u\n", count, test->expected_count);
    }
    test->called = true;
    test->matched = matched;
    return test->accept;
}

// -MD's records (#3592) of a unit that reads its files every way the direct path reads them, against the full pipeline's
// pp_dependency_observe: in the order of their first reading, one record a file (two spellings of one file make one,
// under the first: sys/t.h, a system header's in s.h and the main file's by another spelling, is then a user file); a
// file skipped by its guard or #pragma once, and one a __has_include finds, read too; a system
// header's when every reading was one, where a reading (an include, a skip, a __has_include) is as much a system
// header's as its includer is, and
// #pragma GCC system_header makes the rest of a header one, and the header in its later inclusions, but is ignored in the
// main file; the predefines file a system
// header, an -include file one when the search found it in a system directory; the main file not used. Then a rule the
// driver can't make leaves before the object is written, and so does a __has_include that finds a directory.
static bool c_direct_test_dependencies(const char* obj_path)
{
    char directory[128];
    snprintf(directory, sizeof(directory), "/tmp/buster_direct_deps_%d", (int)getpid());
    static const char* const subdirectories[] = { "", "/inc", "/sys", "/inc/sub" };
    static const struct
    {
        const char* name;
        const char* text;
    } files[] = {
        { "predefs.h", "#define __x86_64__ 1\n#define __LP64__ 1\n#define __STDC__ 1\n#define __STDC_VERSION__ 201710L\n" },
        { "forced.h", "int forced;\n" },
        { "sys/sysforced.h", "int sysforced;\n" },
        { "inc/g.h", "#ifndef G_H\n#define G_H\nint g;\n#endif\n" },
        { "inc/o.h", "#pragma once\nint o;\n" },
        { "inc/p.h", "int p;\n" },
        { "sys/s.h", "#include \"t.h\"\n#include <u.h>\n#include <u.h>\n#include <q.h>\n#if __has_include(<p2.h>)\n#endif\n" },
        { "inc/q.h", "#ifndef Q_H\n#define Q_H\n#pragma GCC system_header\nint q;\n#endif\n" },
        { "inc/p2.h", "int p2;\n" },
        { "sys/t.h", "int t;\n" },
        { "inc/u.h", "#ifndef U_H\n#define U_H\nint u;\n#endif\n" },
        { "inc/h.h", "#ifdef SECOND\n#include <k2.h>\n#endif\n#include <k.h>\n#pragma GCC system_header\n#include <w.h>\n" },
        { "inc/k.h", "int k;\n" },
        { "inc/k2.h", "int k2;\n" },
        { "inc/w.h", "int w;\n" },
        { "inc/m.h", "int m;\n" },
        { "main.c",
          "#include <g.h>\n"
          "#include <g.h>\n"
          "#include <o.h>\n"
          "#include <o.h>\n"
          "#if __has_include(<p.h>) && __has_include(<o.h>) && !__has_include(<absent.h>)\n"
          "int probed;\n"
          "#endif\n"
          "#include <s.h>\n"
          "#include \"sys/../sys/t.h\"\n"
          "#include <u.h>\n"
          "#include <q.h>\n"
          "#include \"inc/../inc/g.h\"\n"
          "#include <h.h>\n"
          "#define SECOND\n"
          "#include <h.h>\n"
          "#include <m.h>\n"
          "int x;\n" },
        { "dir.c", "#if __has_include(<sub>)\n#endif\nint x;\n" },
    };
    static const CDirectTestDependency expected[] = {
        { "predefs.h", true, true },
        { "forced.h", true, false },
        { "sys/sysforced.h", true, true },
        { "main.c", false, false },
        { "inc/g.h", true, false },
        { "inc/o.h", true, false },
        { "inc/p.h", true, false },
        { "sys/s.h", true, true },
        { "sys/t.h", true, false },
        { "inc/u.h", true, false },
        { "inc/q.h", true, true },
        { "inc/p2.h", true, true },
        { "inc/h.h", true, false },
        { "inc/k.h", true, false },
        { "inc/w.h", true, true },
        { "inc/k2.h", true, true },
        { "inc/m.h", true, false },
    };
    char path[256];
    bool ok = true;
    for (uint32_t i = 0; i < sizeof(subdirectories) / sizeof(subdirectories[0]); i += 1)
    {
        snprintf(path, sizeof(path), "%s%s", directory, subdirectories[i]);
        ok = mkdir(path, 0700) == 0 && ok;
    }
    for (uint32_t i = 0; ok && i < sizeof(files) / sizeof(files[0]); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, files[i].name);
        FILE* const file = fopen(path, "wb");
        ok = file != NULL && fputs(files[i].text, file) != EOF;
        ok = (file == NULL || fclose(file) == 0) && ok;
    }
    char user[160];
    char system[160];
    char predefines[160];
    char forced[160];
    char main_path[160];
    char dir_path[160];
    snprintf(user, sizeof(user), "%s/inc", directory);
    snprintf(system, sizeof(system), "%s/sys", directory);
    snprintf(predefines, sizeof(predefines), "%s/predefs.h", directory);
    snprintf(forced, sizeof(forced), "%s/forced.h", directory);
    snprintf(main_path, sizeof(main_path), "%s/main.c", directory);
    snprintf(dir_path, sizeof(dir_path), "%s/dir.c", directory);
    const char* const directories[2] = { user, system };
    static const uint8_t levels[2] = { 0, 1 };
    const char* const includes[2] = { forced, "sysforced.h" };
    CDirectTestDependencies test = {
        .directory = directory,
        .expected = expected,
        .expected_count = (uint32_t)(sizeof(expected) / sizeof(expected[0])),
        .accept = true,
    };
    CDirectRequest request = {
        .input_path = main_path,
        .output_path = obj_path,
        .predefines_path = predefines,
        .directories = directories,
        .directory_system = levels,
        .directory_count = 2,
        .includes = includes,
        .include_count = 2,
        .pic = true,
        .unwind_tables = true,
        .quiet = true,
        .release = true,
        .dependencies = c_direct_test_dependency_records,
        .dependency_context = &test,
    };
    unlink(obj_path);
    ok = ok && c_direct_try_compile(&request) == 0 && test.called && test.matched && access(obj_path, F_OK) == 0;
    // A rule the driver can't make: the unit leaves, and no object is written.
    unlink(obj_path);
    test = (CDirectTestDependencies){
        .directory = directory,
        .expected = expected,
        .expected_count = (uint32_t)(sizeof(expected) / sizeof(expected[0])),
    };
    ok = ok && c_direct_try_compile(&request) == 1 && test.called && test.matched && access(obj_path, F_OK) != 0;
    // What leaves when the unit collects records and compiles when it doesn't: a __has_include that finds a directory,
    // which the full pipeline's search goes on past; a __has_include of a macro (#3608); and a line marker's or #line's
    // file name, which would change the system header level.
    static const char* const leaving[] = {
        "#if __has_include(<sub>)\n#endif\nint x;\n",
        "#define H <g.h>\n#if __has_include(H)\n#endif\nint x;\n",
        "# 7 \"other.c\" 3\nint x;\n",
        "#line 7 \"other.c\"\nint x;\n",
    };
    // Without the records, the first compiles here; the others leave all the same (#3577): this path doesn't expand a
    // __has_include operand's macro, whose answer may be the full pipeline's other, nor follow a file name __FILE__ takes.
    static const int without_records[] = { 0, 1, 1, 1 };
    request.input_path = dir_path;
    request.include_count = 0;
    for (uint32_t i = 0; ok && i < sizeof(leaving) / sizeof(leaving[0]); i += 1)
    {
        FILE* const file = fopen(dir_path, "wb");
        ok = file != NULL && fputs(leaving[i], file) != EOF;
        ok = (file == NULL || fclose(file) == 0) && ok;
        test = (CDirectTestDependencies){ .directory = directory, .accept = true };
        request.dependencies = c_direct_test_dependency_records;
        ok = ok && c_direct_try_compile(&request) == 1 && !test.called && access(obj_path, F_OK) != 0;
        request.dependencies = NULL;
        ok = ok && c_direct_try_compile(&request) == without_records[i];
        unlink(obj_path);
        if (!ok)
        {
            fprintf(stderr, "test-direct: dependency case %u\n", i);
        }
    }
    // A line marker's number alone, and #line's, change nothing a record sees. __has_include and __has_include_next are
    // defined, as clang's builtin macros are, to defined, #ifdef, #ifndef, #elifdef and #elifndef: each case reads p.h
    // where they are and k.h where they aren't.
    static const char* const staying[] = {
        "# 7\nint x;\n#include <p.h>\n",
        "#line 7\nint x;\n#include <p.h>\n",
        "#ifdef __has_include\n#include <p.h>\n#else\n#include <k.h>\n#endif\n",
        "#ifndef __has_include_next\n#include <k.h>\n#else\n#include <p.h>\n#endif\n",
        "#if defined(__has_include) && defined __has_include_next\n#include <p.h>\n#else\n#include <k.h>\n#endif\n",
        "#if 0\n#elifdef __has_include\n#include <p.h>\n#else\n#include <k.h>\n#endif\n",
        "#if 0\n#elifndef __has_include_next\n#include <k.h>\n#else\n#include <p.h>\n#endif\n",
    };
    for (uint32_t i = 0; ok && i < sizeof(staying) / sizeof(staying[0]); i += 1)
    {
        FILE* const file = fopen(dir_path, "wb");
        ok = file != NULL && fputs(staying[i], file) != EOF;
        ok = (file == NULL || fclose(file) == 0) && ok;
        static const CDirectTestDependency alone[] = {
            { "predefs.h", true, true },
            { "dir.c", false, false },
            { "inc/p.h", true, false },
        };
        test = (CDirectTestDependencies){ .directory = directory, .expected = alone, .expected_count = 3, .accept = true };
        request.dependencies = c_direct_test_dependency_records;
        // #elifdef and #elifndef are C23's: before it, the full pipeline's warning (#3577) leaves the direct path.
        request.c23 = i >= 5u;
        ok = ok && c_direct_try_compile(&request) == 0 && test.called && test.matched;
        request.c23 = false;
        unlink(obj_path);
        if (!ok)
        {
            fprintf(stderr, "test-direct: dependency case %u that stays\n", i);
        }
    }
    // The tables' bound: the predefines file, the main file and 4,094 headers fill them, and one more header leaves.
    char* const many = (char*)malloc((size_t)BUSTER_FPP_MAX_DEPENDENCIES * 24u);
    uint32_t written = 0;
    for (uint32_t i = 0; ok && many != NULL && i + 1u < BUSTER_FPP_MAX_DEPENDENCIES; i += 1)
    {
        snprintf(path, sizeof(path), "%s/inc/sub/%u.h", directory, i);
        FILE* const file = fopen(path, "wb");
        ok = file != NULL && fclose(file) == 0;
        written += ok ? 1u : 0u;
    }
    uint32_t length = 0;
    for (uint32_t i = 0; ok && many != NULL && i + 2u < BUSTER_FPP_MAX_DEPENDENCIES; i += 1)
    {
        length += (uint32_t)snprintf(many + length, 24u, "#include <sub/%u.h>\n", i);
    }
    for (uint32_t extra = 0; ok && many != NULL && extra < 2u; extra += 1)
    {
        FILE* const file = fopen(dir_path, "wb");
        ok = file != NULL && fwrite(many, 1, length, file) == length;
        if (ok && extra != 0)
        {
            ok = fprintf(file, "#include <sub/%u.h>\n", BUSTER_FPP_MAX_DEPENDENCIES - 2u) > 0;
        }
        ok = (file == NULL || fclose(file) == 0) && ok;
        test = (CDirectTestDependencies){ .directory = directory, .accept = true };
        request.dependencies = c_direct_test_dependency_records;
        const int status = ok ? c_direct_try_compile(&request) : -2;
        ok = ok && status == (extra == 0 ? 0 : 1) && test.called == (extra == 0);
        unlink(obj_path);
        if (!ok)
        {
            fprintf(stderr, "test-direct: %u files read, status %d\n", BUSTER_FPP_MAX_DEPENDENCIES + extra, status);
        }
    }
    ok = ok && many != NULL;
    free(many);
    for (uint32_t i = 0; i < written; i += 1)
    {
        snprintf(path, sizeof(path), "%s/inc/sub/%u.h", directory, i);
        unlink(path);
    }
    for (uint32_t i = 0; i < sizeof(files) / sizeof(files[0]); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, files[i].name);
        unlink(path);
    }
    for (uint32_t i = sizeof(subdirectories) / sizeof(subdirectories[0]); i > 0; i -= 1)
    {
        snprintf(path, sizeof(path), "%s%s", directory, subdirectories[i - 1u]);
        rmdir(path);
    }
    return ok;
}

// ---------------------------------------------------------------------------------------------------------------------
// The differential (#3577): `test-direct --differential [--jobs N] [--direct-floor N] DIRECTORY...`. Every C file under
// the directories is compiled with -c by this executable twice, each time a child of its own: by default, where the
// direct path compiles what it can and leaves the rest to the full pipeline, and with --no-direct, the full pipeline
// alone. The two must exit alike and print the same bytes, standard output and errors together. The direct path
// compiled a file itself where its run exited 0 without a line in the log BUSTER_TEST_DIRECT_BAILS names; fewer such
// files than --direct-floor fail the run, as a direct path that took none would leave nothing compared.

#define BUSTER_C_DIRECT_DIFFERENTIAL_CAPTURE_BYTES (UINT64_C(64) << 20)
#define BUSTER_C_DIRECT_DIFFERENTIAL_REPORTS 20u

// Appends ITEM, which the list then owns, to a list of strings; frees it and returns false where memory runs out.
static bool c_direct_differential_push(char*** items, uint32_t* count, uint32_t* capacity, char* item)
{
    bool ok = item != NULL;
    if (ok && *count == *capacity)
    {
        const uint32_t grown = *capacity == 0 ? 256u : *capacity * 2u;
        char** bigger = realloc(*items, (size_t)grown * sizeof(char*));
        ok = bigger != NULL;
        if (ok)
        {
            *items = bigger;
            *capacity = grown;
        }
    }
    if (ok)
    {
        (*items)[*count] = item;
        *count += 1u;
    }
    else
    {
        free(item);
    }
    return ok;
}

static int c_direct_differential_order(const void* left, const void* right)
{
    return strcmp(*(const char* const*)left, *(const char* const*)right);
}

// Appends the regular files named *.c under ROOT, at every depth, to FILES: a stack of directories walks the tree (the
// call graph allows no recursion), and a link is followed to neither a file nor a directory. False where a directory
// can't be read or memory runs out.
static bool c_direct_differential_collect(const char* root, char*** files, uint32_t* file_count, uint32_t* file_capacity)
{
    char** stack = NULL;
    uint32_t depth = 0;
    uint32_t stack_capacity = 0;
    bool ok = c_direct_differential_push(&stack, &depth, &stack_capacity, strdup(root));
    while (ok && depth > 0)
    {
        depth -= 1u;
        char* directory = stack[depth];
        DIR* handle = opendir(directory);
        ok = handle != NULL;
        if (!ok)
        {
            fprintf(stderr, "test-direct: %s: %s\n", directory, strerror(errno));
        }
        for (struct dirent* entry = ok ? readdir(handle) : NULL; ok && entry != NULL; entry = readdir(handle))
        {
            const char* name = entry->d_name;
            const size_t name_length = strlen(name);
            const size_t directory_length = strlen(directory);
            if (name[0] != '.')
            {
                char* path = malloc(directory_length + name_length + 2u);
                ok = path != NULL;
                struct stat stats = { 0 };
                if (ok)
                {
                    memcpy(path, directory, directory_length);
                    path[directory_length] = '/';
                    memcpy(path + directory_length + 1u, name, name_length + 1u);
                    ok = lstat(path, &stats) == 0;
                }
                if (ok && S_ISDIR(stats.st_mode))
                {
                    ok = c_direct_differential_push(&stack, &depth, &stack_capacity, path);
                }
                else if (ok && S_ISREG(stats.st_mode) && name_length > 2u && strcmp(name + name_length - 2u, ".c") == 0)
                {
                    ok = c_direct_differential_push(files, file_count, file_capacity, path);
                }
                else
                {
                    free(path);
                }
            }
        }
        if (handle != NULL)
        {
            closedir(handle);
        }
        free(directory);
    }
    while (depth > 0)
    {
        depth -= 1u;
        free(stack[depth]);
    }
    free(stack);
    return ok;
}

// The work directory's three files of run RUN (file RUN / 2's, by default where RUN is even, with --no-direct where it
// is odd): its standard output and errors, its object, its bail log.
static void c_direct_differential_names(const char* work, uint32_t run, char* capture, char* object, char* bails, size_t capacity)
{
    snprintf(capture, capacity, "%s/%u.txt", work, run);
    snprintf(object, capacity, "%s/%u.o", work, run);
    snprintf(bails, capacity, "%s/%u.bails", work, run);
}

// Starts SELF on FILE with -c (with --no-direct where FULL), its standard input /dev/null, its standard output and
// errors into CAPTURE, its object into OBJECT, without core files and killed past two minutes of CPU time. The
// environment is the comparison's own: no BUSTER_NO_DIRECT and no SOURCE_DATE_EPOCH, either of which turns the direct
// path off, and BUSTER_TEST_DIRECT_BAILS naming BAILS for the default run alone. Its pid, or -1.
static pid_t c_direct_differential_start(const char* self, const char* file, bool full, const char* capture, const char* object, const char* bails)
{
    const pid_t child = fork();
    if (child == 0)
    {
        const int input = open("/dev/null", O_RDONLY | O_CLOEXEC);
        const int output = open(capture, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        const struct rlimit cpu = { .rlim_cur = 120, .rlim_max = 125 };
        const struct rlimit core = { 0 };
        const bool named = full ? unsetenv("BUSTER_TEST_DIRECT_BAILS") == 0 : setenv("BUSTER_TEST_DIRECT_BAILS", bails, 1) == 0;
        const bool ready = named && unsetenv("BUSTER_NO_DIRECT") == 0 && unsetenv("SOURCE_DATE_EPOCH") == 0 && input >= 0 && output >= 0 &&
                           setrlimit(RLIMIT_CPU, &cpu) == 0 && setrlimit(RLIMIT_CORE, &core) == 0 &&
                           dup2(input, 0) == 0 && dup2(output, 1) == 1 && dup2(output, 2) == 2;
        if (ready)
        {
            char* direct_arguments[] = { (char*)self, (char*)"-c", (char*)file, (char*)"-o", (char*)object, NULL };
            char* full_arguments[] = { (char*)self, (char*)"--no-direct", (char*)"-c", (char*)file, (char*)"-o", (char*)object, NULL };
            execv(self, full ? full_arguments : direct_arguments);
        }
        _exit(127);
    }
    return child;
}

// The file at PATH in memory the caller frees (*length its bytes), an empty one where there is none; null where it
// can't be read whole.
static char* c_direct_differential_read(const char* path, uint64_t* length)
{
    *length = 0;
    char* bytes = NULL;
    const int descriptor = open(path, O_RDONLY | O_CLOEXEC);
    struct stat stats = { 0 };
    if (descriptor < 0)
    {
        bytes = errno == ENOENT ? calloc(1, 1) : NULL;
    }
    else if (fstat(descriptor, &stats) == 0 && S_ISREG(stats.st_mode) && (uint64_t)stats.st_size <= BUSTER_C_DIRECT_DIFFERENTIAL_CAPTURE_BYTES)
    {
        bytes = calloc((uint64_t)stats.st_size + 1u, 1);
        bool reading = bytes != NULL;
        while (reading && *length < (uint64_t)stats.st_size)
        {
            const ssize_t got = read(descriptor, bytes + *length, (uint64_t)stats.st_size - *length);
            reading = got > 0 || (got < 0 && errno == EINTR);
            *length += got > 0 ? (uint64_t)got : 0u;
        }
        if (bytes != NULL && *length != (uint64_t)stats.st_size)
        {
            free(bytes);
            bytes = NULL;
        }
    }
    if (descriptor >= 0)
    {
        close(descriptor);
    }
    return bytes;
}

static void c_direct_differential_print_status(const char* who, int status)
{
    if (WIFEXITED(status))
    {
        fprintf(stderr, "%s exited %d", who, WEXITSTATUS(status));
    }
    else if (WIFSIGNALED(status))
    {
        fprintf(stderr, "%s was killed by signal %d", who, WTERMSIG(status));
    }
    else
    {
        fprintf(stderr, "%s ended with wait status %d", who, status);
    }
}

// Compares file FILE's two runs (statuses STATUSES[0], by default, and STATUSES[1], with --no-direct) and removes their
// files: whether they exited alike and printed the same bytes. *DIRECT: whether the direct path compiled the file itself.
// The first BUSTER_C_DIRECT_DIFFERENTIAL_REPORTS differences, counted in *REPORTED, print each run's first line that
// differs.
static bool c_direct_differential_compare(const char* work, uint32_t file, const char* path, const int* statuses, bool* direct, uint32_t* reported)
{
    char capture[2][512];
    char object[2][512];
    char bails[2][512];
    uint64_t length[2] = { 0, 0 };
    uint64_t bail_length = 0;
    c_direct_differential_names(work, 2u * file, capture[0], object[0], bails[0], sizeof(capture[0]));
    c_direct_differential_names(work, 2u * file + 1u, capture[1], object[1], bails[1], sizeof(capture[1]));
    char* direct_output = c_direct_differential_read(capture[0], &length[0]);
    char* full_output = c_direct_differential_read(capture[1], &length[1]);
    char* bail_log = c_direct_differential_read(bails[0], &bail_length);
    const bool read = direct_output != NULL && full_output != NULL && bail_log != NULL;
    const bool same = read && statuses[0] == statuses[1] && length[0] == length[1] && memcmp(direct_output, full_output, length[0]) == 0;
    *direct = read && WIFEXITED(statuses[0]) && WEXITSTATUS(statuses[0]) == 0 && bail_length == 0;
    if (!same && *reported < BUSTER_C_DIRECT_DIFFERENTIAL_REPORTS)
    {
        fprintf(stderr, "test-direct: DIFF %s: ", path);
        c_direct_differential_print_status("by default it", statuses[0]);
        c_direct_differential_print_status(", with --no-direct it", statuses[1]);
        fprintf(stderr, "%s\n", read ? "" : "; a run's output couldn't be read");
        if (read)
        {
            // Each run's line where the outputs part.
            uint64_t common = 0;
            while (common < length[0] && common < length[1] && direct_output[common] == full_output[common])
            {
                common += 1u;
            }
            while (common > 0 && direct_output[common - 1u] != '\n')
            {
                common -= 1u;
            }
            const char* const outputs[2] = { direct_output, full_output };
            const char* const labels[2] = { "    default", "--no-direct" };
            for (uint32_t side = 0; side < 2u; side += 1u)
            {
                uint64_t end = common;
                while (end < length[side] && end - common < 200u && outputs[side][end] != '\n')
                {
                    end += 1u;
                }
                fprintf(stderr, "    %s| %.*s%s\n", labels[side], (int)(end - common), outputs[side] + common, end < length[side] ? "" : " <end of output>");
            }
        }
        *reported += 1u;
    }
    free(direct_output);
    free(full_output);
    free(bail_log);
    for (uint32_t side = 0; side < 2u; side += 1u)
    {
        unlink(capture[side]);
        unlink(object[side]);
        unlink(bails[side]);
    }
    return same;
}

static int c_direct_differential(int argc, char** argv)
{
    uint32_t jobs = 0;
    uint32_t direct_floor = 0;
    uint32_t roots = 0;
    char** files = NULL;
    uint32_t file_count = 0;
    uint32_t file_capacity = 0;
    bool ok = true;
    for (int i = 0; ok && i < argc; i += 1)
    {
        const bool valued = i + 1 < argc;
        if (valued && strcmp(argv[i], "--jobs") == 0)
        {
            jobs = (uint32_t)strtoul(argv[i + 1], NULL, 10);
            i += 1;
        }
        else if (valued && strcmp(argv[i], "--direct-floor") == 0)
        {
            direct_floor = (uint32_t)strtoul(argv[i + 1], NULL, 10);
            i += 1;
        }
        else if (argv[i][0] == '-')
        {
            fprintf(stderr, "test-direct: --differential takes --jobs N, --direct-floor N and directories, not %s\n", argv[i]);
            ok = false;
        }
        else
        {
            ok = c_direct_differential_collect(argv[i], &files, &file_count, &file_capacity);
            roots += 1u;
        }
    }
    if (ok && (roots == 0 || file_count == 0))
    {
        fprintf(stderr, "test-direct: --differential found no C file to compare\n");
        ok = false;
    }
    if (jobs == 0)
    {
        const char* cores = getenv("CORES");
        const long online = sysconf(_SC_NPROCESSORS_ONLN);
        jobs = cores != NULL && cores[0] != 0 ? (uint32_t)strtoul(cores, NULL, 10) : (uint32_t)(online > 0 ? online : 1);
        jobs = jobs == 0 ? 1u : jobs;
    }
    char self[4096];
    const ssize_t self_length = readlink("/proc/self/exe", self, sizeof(self) - 1u);
    ok = ok && self_length > 0 && (size_t)self_length < sizeof(self) - 1u;
    self[self_length > 0 && (size_t)self_length < sizeof(self) ? self_length : 0] = 0;
    const char* temporary = getenv("TMPDIR");
    char work[512];
    snprintf(work, sizeof(work), "%s/buster-direct-differential.XXXXXX", temporary != NULL && temporary[0] != 0 ? temporary : "/tmp");
    const bool made = ok && mkdtemp(work) != NULL;
    ok = made;
    if (file_count > 1u)
    {
        qsort(files, file_count, sizeof(char*), c_direct_differential_order);
    }

    // Each file's two runs as children, JOBS at a time; run R is file R / 2's.
    const uint32_t run_count = 2u * file_count;
    pid_t* children = calloc(jobs, sizeof(pid_t));
    uint32_t* child_run = calloc(jobs, sizeof(uint32_t));
    int* statuses = calloc(run_count + 1u, sizeof(int));
    uint8_t* ended = calloc(file_count + 1u, 1);
    ok = ok && children != NULL && child_run != NULL && statuses != NULL && ended != NULL;
    uint32_t next = 0;
    uint32_t running = 0;
    uint32_t differ = 0;
    uint32_t direct = 0;
    uint32_t reported = 0;
    while ((ok && next < run_count) || running > 0)
    {
        for (uint32_t slot = 0; ok && slot < jobs && next < run_count; slot += 1u)
        {
            if (children[slot] == 0)
            {
                char capture[512];
                char object[512];
                char bails[512];
                c_direct_differential_names(work, next, capture, object, bails, sizeof(capture));
                const pid_t child = c_direct_differential_start(self, files[next / 2u], (next & 1u) != 0, capture, object, bails);
                ok = child > 0;
                children[slot] = child > 0 ? child : 0;
                child_run[slot] = next;
                running += (uint32_t)(child > 0);
                next += 1u;
            }
        }
        int status = 0;
        const pid_t child = running > 0 ? waitpid(-1, &status, 0) : 0;
        for (uint32_t slot = 0; child > 0 && slot < jobs; slot += 1u)
        {
            if (children[slot] == child)
            {
                const uint32_t run = child_run[slot];
                const uint32_t file = run / 2u;
                children[slot] = 0;
                running -= 1u;
                statuses[run] = status;
                ended[file] += 1u;
                if (ended[file] == 2u)
                {
                    bool compiled = false;
                    differ += (uint32_t)!c_direct_differential_compare(work, file, files[file], statuses + 2u * file, &compiled, &reported);
                    direct += (uint32_t)compiled;
                }
            }
        }
        if (child < 0 && errno != EINTR)
        {
            fprintf(stderr, "test-direct: waitpid: %s\n", strerror(errno));
            ok = false;
            running = 0;
        }
    }
    if (made)
    {
        rmdir(work);
    }
    const bool floor_met = direct >= direct_floor;
    if (ok && differ > 0)
    {
        printf("test-direct: %u of %u C files differ by default from --no-direct (FAILED)\n", differ, file_count);
    }
    else if (ok)
    {
        printf("test-direct: %u C files: by default each exits and prints as with --no-direct; the direct path compiled %u of them itself\n",
               file_count, direct);
    }
    if (ok && !floor_met)
    {
        printf("test-direct: the direct path compiled %u of the files, fewer than --direct-floor %u (FAILED)\n", direct, direct_floor);
    }
    for (uint32_t i = 0; i < file_count; i += 1u)
    {
        free(files[i]);
    }
    free(files);
    free(children);
    free(child_run);
    free(statuses);
    free(ended);
    return ok && differ == 0 && floor_met ? 0 : 1;
}

static int c_direct_unit_tests(void)
{
    char obj_path[256];
    snprintf(obj_path, sizeof(obj_path), "/tmp/buster_direct_test_%d.o", (int)getpid());
    uint32_t passed = 0;
    uint32_t failed = 0;

    static const char* const valid_cases[] = {
        // 1. Nested macro prescan, stringify, token paste, variadic comma elision, unused-param skipping
        "#define INC(x) ((x) + 1)\n"
        "#define ADD(a, b) ((a) + (b))\n"
        "#define PASTE(a, b) a ## b\n"
        "#define STR(x) #x\n"
        "#define IGN(x) ((void)(0))\n"
        "int PASTE(fn_, 1)(int x) { IGN(x > 0 && x < 99); const char* s = STR(hello); return ADD(INC(x), s[0]); }\n",

        // 2. Structs, anonymous unions, bitfields, enum bitfields, designated initializers
        "enum Color { RED = 0, GREEN = 1, BLUE = 2 };\n"
        "struct S { union { int a; unsigned b; }; enum Color c : 3; signed d : 5; int arr[3]; };\n"
        "static struct S gs = { .c = BLUE, .d = -3, .arr = { [2] = 7 } };\n"
        "int read_s(struct S* p) { p->a = 42; return (int)p->c + p->d + gs.arr[2]; }\n",

        // 3. 128-bit integer arithmetic + dense switch jump table + float/double NaN & u64 conversions
        "unsigned long mul128_hi(unsigned long a, unsigned long b) {\n"
        "    __uint128_t p = (__uint128_t)a * (__uint128_t)b;\n"
        "    return (unsigned long)(p >> 64);\n"
        "}\n"
        "int dense_sw(int x) {\n"
        "    switch (x) {\n"
        "        case 0: return 10; case 1: return 20; case 2: return 30;\n"
        "        case 3: return 40; case 4: return 50; case 5: return 60;\n"
        "        default: return -1;\n"
        "    }\n"
        "}\n"
        "int fp_test(double a, double b, unsigned long u) {\n"
        "    double d = (double)u + a * b;\n"
        "    unsigned long r = (unsigned long)d;\n"
        "    return (a == b) + (a < b) + (int)(r & 7u);\n"
        "}\n",

        // 4. Variadic functions, >16B struct pass/return, nested function-pointer declarators, builtins
        "struct Big { long x[4]; };\n"
        "struct Big make_big(struct Big in, int (*cb)(int (*)(void*), void*)) {\n"
        "    __sync_synchronize();\n"
        "    struct Big out = in;\n"
        "    if (cb) { out.x[0] += cb(0, 0); }\n"
        "    return out;\n"
        "}\n"
        "int sum_va(int n, ...) {\n"
        "    __builtin_va_list ap;\n"
        "    __builtin_va_start(ap, n);\n"
        "    int s = 0;\n"
        "    for (int i = 0; i < n; i += 1) { s += __builtin_va_arg(ap, int); }\n"
        "    __builtin_va_end(ap);\n"
        "    return s + __builtin_popcount((unsigned)n);\n"
        "}\n",

        // 5. EFLAGS preservation when comparing/combining/storing lazy comparison results (regression for `0 == (a >= 2)`)
        "struct FlagsBf { unsigned f : 1; unsigned g : 3; };\n"
        "int eflags_order(int x, int y, struct FlagsBf* bf) {\n"
        "    int r = (0 == (x >= 2)) + ((x < y) == (y >= 3)) + (1 != (x <= 0));\n"
        "    bf->f = (x >= 2);\n"
        "    r += (x == y);\n"
        "    return r + (int)bf->f;\n"
        "}\n",

        // 6. -Wunsequenced quiet (#3577): a modification that is the expression's last operator; each modified name once
        // among the tokens; and, compiled again for the log, `&&` sequencing a reuse, a string literal's array, and more
        // than four modifications.
        "int sequenced_next(int v);\n"
        "int sequenced(int x, int* p, int c, int a, int b, int d, int e) {\n"
        "    x = x + 1;\n"
        "    x += x;\n"
        "    *p++ = c;\n"
        "    a = b++;\n"
        "    if ((c = sequenced_next(c)) == 0 && c == sequenced_next(x)) { return 1; }\n"
        "    if ((d = (int)sizeof(\"a string literal\")) == 17 && d) { return d; }\n"
        "    a = b = c = d = e = x;\n"
        "    return a + b + *p;\n"
        "}\n",

        // 7. -Wdangling-else quiet (#3577): the inner if-else in braces, an outer else, as a do's, a for's, a while's or a
        // switch's body or with a label, and in an else arm.
        "int dangling(int a, int b, int n) {\n"
        "    if (a) { if (b) n += 1; else n -= 1; }\n"
        "    if (a) if (b) n += 2; else n -= 2; else n = 0;\n"
        "    if (a) do if (b) n += 3; else n -= 3; while (n < 0);\n"
        "    if (a) for (int i = 0; i < 2; i++) if (b) n += i; else n -= i;\n"
        "    if (a) while (n > 9) if (b) n -= 2; else n -= 1;\n"
        "    if (a) switch (n) case 1: if (b) n++; else n--;\n"
        "    if (a) next: if (b) n += 6; else n -= 6;\n"
        "    if (a) n += 4; else if (b) n += 5; else n -= 5;\n"
        "    return n;\n"
        "}\n",
        // 8. Comparisons -Wtautological-compare and -Wstring-compare leave alone (#3577): two places alike but not one path
        // (another pointer's member, a union's other member, a sum), a float with itself, a string literal inside sizeof,
        // a pointer sum against another pointer.
        "struct S { int a; };\nunion U { int i; int j; };\n"
        "int compared(struct S* p, struct S* q, union U u, int x, double d, const char* cp, int* ip, int* end, unsigned n) {\n"
        "    int r = (p->a == q->a) + (u.i == u.j) + (x == x + 1) + (x + 1 == x) + (d == d);\n"
        "    r += (int)sizeof(x == x) + (int)sizeof(\"a\" == cp) + (ip + n < end) + (p->a < q->a);\n"
        "    return r + (cp != 0);\n"
        "}\n",
        // 9. Switches the full pipeline's -Wswitch-bool leaves alone (#3577): a member, a mask, `|` with an int, an
        // assignment of a call, a conditional of ints, arithmetic over a comparison, a comparison in a call's argument, a
        // cast of one to int.
        "struct T { int op; unsigned char *blob; };\nint g(int);\n"
        "int switched(struct T* t, int n, int c, _Bool b, int i) {\n"
        "    switch (t->op) { case 1: n += 1; }\n"
        "    switch (t->blob[i] & 0x0f) { case 2: n += 2; }\n"
        "    switch (b | n) { case 3: n += 3; }\n"
        "    switch (c = g(n)) { case 4: n += 4; }\n"
        "    switch (c ? n : 0) { case 5: n += 5; }\n"
        "    switch (128 * (c == '$') + n) { case 6: n += 6; }\n"
        "    switch (g(n == 1)) { case 7: n += 7; }\n"
        "    switch ((int)(n == 1)) { case 8: n += 8; }\n"
        "    return n;\n"
        "}\n",
        // 10. A pointer to void under `&` and in sizeof, which the full pipeline's -Wvoid-ptr-dereference leaves alone.
        "void* addressed(void* p) { return &*p; }\nunsigned long measured(void* p) { return sizeof(*p); }\n",
        // 11. Conditionals of pointers the full pipeline's -Wpointer-type-mismatch leaves alone: one to void, one more
        // qualified, a null pointer constant.
        "const int* chosen(int c, int* p, const int* q, void* v) { return c ? p : (c > 1 ? q : (c > 2 ? (const int*)v : (void*)0)); }\n",
        // 12. Packed records the full pipeline's -Waddress-of-packed-member leaves alone: a char member's address, an array
        // member's decay and its element's address, an aligned packed record's aligned member, a read of a misaligned one.
        "struct __attribute__((packed)) P { char c; int i; int arr[2]; };\n"
        "struct __attribute__((packed, aligned(4))) A { int i; int j; };\n"
        "int packed(struct P* p, struct A* a) { char* c = &p->c; int* e = p->arr; int* f = &p->arr[1]; int* j = &a->j; return *c + *e + *f + *j + p->i; }\n",
        // 13. Packed records' layouts as clang's (#3577), which main's direct path laid out otherwise: an aligned(4)
        // before the braces, and a record defined inside a packed one, which isn't packed itself (a static assertion
        // that fails leaves the direct path, and fails this case).
        "struct __attribute__((packed, aligned(4))) Q { char c; int j; };\n"
        "struct R { char c; int j; } __attribute__((packed, aligned(4)));\n"
        "struct __attribute__((packed)) P { char c; struct In { char a; int x; } sub; struct { short s; } anon; };\n"
        "struct X { char c; struct In i; };\n"
        "struct __attribute__((packed)) A { char c; struct { int x; }; };\n"
        "_Static_assert(sizeof(struct Q) == 8 && _Alignof(struct Q) == 4 && sizeof(struct R) == 8 && _Alignof(struct R) == 4, \"aligned\");\n"
        "_Static_assert(sizeof(struct In) == 8 && _Alignof(struct In) == 4 && sizeof(struct X) == 12 && sizeof(struct P) == 11, \"nested\");\n"
        "_Static_assert(sizeof(struct A) == 5 && __builtin_offsetof(struct A, x) == 1, \"anonymous\");\n"
        "int laid(struct P* p, struct X* x) { return p->sub.x + p->anon.s + x->i.x; }\n",
        // 14. Constant indices the full pipeline's -Warray-bounds leaves alone: the end's address, `5[a]`, a member's
        // array (a trailing one or not), a string's terminator, a dead branch's and sizeof's.
        "struct S { int arr[2]; int n; int d[1]; };\nint a[3];\n"
        "int bounded(struct S* s) { int* e = &a[3]; return *e + 2[a] + s->arr[5] + s->d[4] + \"abc\"[3] + (0 ? a[7] : 1) + (int)sizeof(a[9]); }\n",
        // 15. Returns the full pipeline's -Wreturn-stack-address leaves alone: an address a call, `*`, a subscript, a
        // comparison or an assignment takes; a static's, a pointer member's array, a parameter array, a pointer's member.
        "struct T { int b[2]; int n; };\nint* g(int*);\nint* p;\n"
        "int* r1(void) { int x = 0; return g(&x); }\nint r2(void) { int x = 1; return *&x; }\n"
        "int r3(int* q) { int a[2] = { 0, 1 }; return q[&a[1] - a]; }\nint r4(int* q) { int x = 0; return q == &x; }\n"
        "int* r5(void) { int x = 0; return p = &x; }\nint* r6(void) { static int s[2]; return s; }\n"
        "int* r7(struct T* t) { return t->b; }\nint* r8(int a[2]) { return a; }\nint* r9(struct T* t) { return &t->n; }\n",
        // 16. Library calls the full pipeline leaves alone: memcpy's and memset's object sizes, another pointer's, `&p`'s
        // pointer, a multiple, a pointer to void; abs of a short and an int, labs of a long long, fabs of a float.
        "void* memcpy(void*, const void*, unsigned long);\nvoid* memset(void*, int, unsigned long);\n"
        "int abs(int);\nlong labs(long);\ndouble fabs(double);\nstruct S { int a, b; };\n"
        "long sized(int* p, int* q, char* r, void* v, void* w, int n, short h, long long ll, float fl) {\n"
        "    struct S s; int a[4]; memset(&s, 0, sizeof(s)); memset(a, 0, sizeof(a)); memcpy(p, q, sizeof(*p));\n"
        "    memcpy(p, q, sizeof(r)); memcpy(&p, &q, sizeof(p)); memcpy(p, q, n * sizeof(p)); memcpy(v, w, sizeof(v));\n"
        "    return abs(h) + abs(n) + labs(ll) + (long)fabs(fl) + s.a + a[0];\n"
        "}\n",
    };

    for (uint32_t i = 0; i < sizeof(valid_cases) / sizeof(valid_cases[0]); i += 1)
    {
        uint32_t tlen = 0;
        uint32_t scnt = 0;
        if (c_direct_test_compile_source(valid_cases[i], obj_path, 0, &tlen, &scnt) && tlen > 0 && scnt > 0)
        {
            passed += 1u;
        }
        else
        {
            fprintf(stderr, "test-direct: FAIL valid case %u\n", i);
            failed += 1u;
        }
    }

    // Soundness bailout cases: MUST return 1 and write NOTHING!
    static const char* const bail_cases[] = {
        "#pragma push_macro(\"X\")\nint f(void) { return 0; }\n",
        "#error forced error\n",
        "int f(void) { return undeclared_xyz + 1; }\n",
        "int f(int a, int b) { switch (a) { case 7: switch (b) { case 7: return 1; } case 7: return 2; } return 0; }",
        "static inline int leaf(void) { return undeclared_xyz; }\n"
        "static inline int bridge(void) { return leaf(); }\n"
        "int root(void) { return bridge(); }\n",
        "const void* wide_str(void) { return L\"hello\"; }\n",
        // #3577: what the full pipeline diagnoses or compiles otherwise. An initializer over another, and a constant
        // whose value a conversion changes.
        "const unsigned short table[] = { [2] = 77, 0, [0] = 300, 101, 102 };\n",
        "const unsigned short table[] = { 12345678 };\n",
        // An inline body nothing uses is checked; a body is checked in its own scope, before a later declaration.
        "static inline int unused(void) { return undeclared_xyz; }\nint f(void) { return 0; }\n",
        "static inline int early(void) { return later; }\nint later;\nint g(void) { return early(); }\n",
        // Compound literals: an element past the type, one over another, one an aggregate member takes by brace elision,
        // and a designator of more than one member.
        "int f(void) { return (int){ 1, 2 }; }\n",
        "struct P { int y, z; };\nint f(void) { return (struct P){ .z = 1, .z = 2 }.z; }\n",
        "struct S { int x, y; };\nstruct P { struct S s; int z; };\nint f(int a) { return (struct P){ a, a, a }.z; }\n",
        "struct S { int x, y; };\nstruct P { struct S s; int z; };\nint f(void) { return (struct P){ .s.x = 1 }.z; }\n",
        // An object of incomplete type, and a for clause's const object left uninitialized.
        "int f(void) { struct T t; return 0; }\n",
        "int f(int n) { for (const int k; n; n--) { } return n; }\n",
        // -Wunsequenced: a modification and a use the log finds unsequenced, compiled again for it; and an expression
        // compiled again that declares a type, which its first compile declared already.
        "int f(int i, int* a) { a[i] = i++; return i; }\n",
        "int f(int x) { return x++ + x; }\n",
        "int f(int x) { if ((x = 1) && x == (int)sizeof(struct S { int a; })) { return 1; } return 0; }\n",
        // A pragma the full pipeline acts on or reports: in the main file, once and GCC system_header are its warnings.
        "#pragma once\nint f(void) { return 0; }\n",
        "#pragma GCC system_header\nint f(void) { return 0; }\n",
        "#pragma unroll\nint f(void) { return 0; }\n",
        // __VA_OPT__ in a macro that isn't variadic: the full pipeline's warning, object-like or function-like.
        "#define X __VA_OPT__(1)\nint f(void) { return 0; }\n",
        "#define F(a) __VA_OPT__(a)\nint f(void) { return 0; }\n",
        // -Wdangling-else: an else an inner if takes, in an outer if without one (directly, or through a loop's body).
        "int f(int a, int b) { if (a) if (b) return 1; else return 2; return 0; }\n",
        "int f(int a, int b, int c) { if (a) if (b) if (c) return 1; else return 2; return 0; }\n",
        // -Wtautological-compare: one path on both sides (in parentheses too), two arrays, a pointer sum and its pointer;
        // and -Wstring-compare, through parentheses and casts; in a dead branch as well.
        "int f(int x) { return x == x; }\n",
        "int f(int x) { return (x) != x; }\n",
        "struct S { int a; struct S* next; };\nint f(struct S* p) { return p->next->a < p->next->a; }\n",
        "int g[4];\nint f(int i) { return g[i] >= g[i]; }\n",
        "int a[4], b[4];\nint f(void) { return a == b; }\n",
        "int f(int* p, unsigned n) { return p + n < p; }\n",
        "int f(const char* p) { return \"a\" == p; }\n",
        "int f(const char* p) { return p < ((const char*)\"z\"); }\n",
        "int f(int x) { if (0) { return x == x; } return 0; }\n",
        // -Wundefined-internal of a static function a dead branch alone calls (#3592's without code): a use all the same.
        "static int sf(void);\nint f(void) { return 0 ? sf() : 1; }\n",
        "static int sf(void);\nint f(void) { if (0) { (void)sf(); } return 1; }\n",
        // A floating constant an integer type can't hold, whose conversion is undefined, in the fold as in the program.
        "int f(void) { return (int)1e20; }\n",
        "unsigned f(void) { return (unsigned)-1.5; }\n",
        // -Wextern-initializer: an extern object's initializer at file scope, the first declarator's or a later one's.
        "extern int x = 1;\nint f(void) { return x; }\n",
        "extern int x, y = 2;\nint f(void) { return x + y; }\n",
        // An enumerator past INT64_MAX, written or implicit: the full pipeline's unsigned 64-bit enum (main's direct path
        // took 0xFFFFFFFFFFFFFFFFull for an int -1), or its -Wenum-too-large beside a negative value.
        "enum E { X = 0xFFFFFFFFFFFFFFFFull };\nint f(void) { return X > 0; }\n",
        "enum E { X = 0x8000000000000000ull, Y = -1 };\nint f(void) { return 0; }\n",
        "enum E { X = 0x7FFFFFFFFFFFFFFF, Y };\nint f(void) { return 0; }\n",
        // -Wswitch-bool: a _Bool, a comparison in parentheses, `^` of two _Bools, a conditional of comparisons, an
        // assignment's right operand, unary `+` of a _Bool, a dead switch's.
        "int f(_Bool b) { switch (b) { case 0: return 1; } return 0; }\n",
        "int f(int x) { switch ((x == 1)) { case 1: return 1; } return 0; }\n",
        "int f(_Bool a, _Bool b) { switch (a ^ b) { case 1: return 1; } return 0; }\n",
        "int f(int c, int x, int y) { switch (c ? x == 1 : y < 2) { case 1: return 1; } return 0; }\n",
        "int f(int x, int y) { switch (y = !x) { case 1: return 1; } return 0; }\n",
        "int f(_Bool b) { switch (+b) { case 1: return 1; } return 0; }\n",
        "int f(int x) { if (0) { switch (x < 1) { case 1: return 1; } } return 0; }\n",
        // -Wvoid-ptr-dereference: `*` of a pointer to void, a qualified one, in a dead branch; and a subscript of one, on
        // which the full pipeline stops.
        "void f(void* p) { (void)*p; }\n",
        "void f(const volatile void* p) { (void)*p; }\n",
        "void f(void* p) { if (0) { (void)*p; } }\n",
        "void f(void* p) { (void)p[0]; }\n",
        // -Wpointer-type-mismatch: a conditional of pointers to incompatible types, char's and unsigned char's, two
        // functions', in a constant conditional.
        "void* f(int c, int* p, long* q) { return c ? p : q; }\n",
        "void* f(int c, char* p, unsigned char* q) { return c ? p : q; }\n",
        "int g(int);\nlong h(long);\nvoid f(int c) { (void)(c ? g : h); }\n",
        "void* f(int* p, long* q) { return 1 ? p : q; }\n",
        // -Waddress-of-packed-member: the address of a packed record's int, of a member of its member, of a local's,
        // inside sizeof, of a member packed itself, of an int at an aligned packed record's odd offset.
        "struct __attribute__((packed)) P { char c; int i; };\nint* f(struct P* p) { return &p->i; }\n",
        "struct In { int x; };\nstruct __attribute__((packed)) P { char c; struct In sub; };\nint* f(struct P* p) { return &p->sub.x; }\n",
        "struct __attribute__((packed)) P { char c; int i; };\nint f(void) { struct P s = { 0, 1 }; int* q = &s.i; return *q; }\n",
        "struct __attribute__((packed)) P { char c; int i; };\nunsigned long f(struct P* p) { return sizeof(&p->i); }\n",
        "struct Q { char c; int i __attribute__((packed)); };\nint* f(struct Q* q) { return &q->i; }\n",
        "struct __attribute__((packed, aligned(4))) Q { char c; int j; };\nint* f(struct Q* q) { return &q->j; }\n",
        // -Warray-bounds: a constant index past a named array's end (its end too, but under `&`), before its start, past
        // a row's or a string literal's, in parentheses.
        "int a[3];\nint f(void) { return a[3]; }\n",
        "int a[3];\nint* f(void) { return &a[4]; }\n",
        "int f(void) { int a[3] = { 0 }; return a[-1]; }\n",
        "int b[2][3];\nint f(void) { return b[1][5]; }\n",
        "int b[2][3];\nint* f(void) { return b[2]; }\n",
        "int f(void) { return \"abc\"[5]; }\n",
        "int a[3];\nint f(void) { return (a)[5]; }\n",
        // -Wreturn-stack-address: a local's, a parameter's and a member's address, through a cast, arithmetic, sizeof and
        // a conditional's arm (a dead one too), an element at a variable index, a local array, a row, a compound literal.
        "int* f(void) { int x = 0; return &x; }\n",
        "long f(int y) { return -(long)&y & 4095; }\n",
        "struct S { int a; };\nint* f(struct S s) { return &s.a; }\n",
        "unsigned long f(void) { int x = 0; return sizeof(&x) + (unsigned long)x; }\n",
        "int* f(void) { int x = 0; return 0 ? &x : 0; }\n",
        "int* f(int i) { int a[4] = { 0 }; return &a[i]; }\n",
        "char* f(void) { char s[] = \"abc\"; return s; }\n",
        "int* f(int i) { int a[2][3] = { { 0 } }; return a[i]; }\n",
        "int* f(void) { return (int[2]){ 1, 2 }; }\n",
        // -Wsizeof-pointer-memaccess: memcpy's size the pointer its destination or source is, memset's, a member's, in a
        // dead branch; -Wabsolute-value: abs of a long, an unsigned, a _Bool, a double, fabs of an int.
        "void* memcpy(void*, const void*, unsigned long);\nvoid f(int* p, int* q) { memcpy(p, q, sizeof(p)); }\n",
        "void* memcpy(void*, const void*, unsigned long);\nvoid f(int* p, int* q) { memcpy(p, q, (sizeof q)); }\n",
        "void* memset(void*, int, unsigned long);\nvoid f(int* p) { memset(p, 0, sizeof(p)); }\n",
        "struct S { char* buf; };\nvoid* memcpy(void*, const void*, unsigned long);\nvoid f(struct S* s, const char* c) { memcpy(s->buf, c, sizeof(s->buf)); }\n",
        "void* memcpy(void*, const void*, unsigned long);\nvoid f(int* p, int* q) { if (0) { memcpy(p, q, sizeof(p)); } }\n",
        "int abs(int);\nlong f(long x) { return abs(x); }\n",
        "int abs(int);\nint f(unsigned x) { return abs(x); }\n",
        "int abs(int);\nint f(_Bool b) { return abs(b); }\n",
        "int abs(int);\nint f(double d) { return abs(d); }\n",
        "double fabs(double);\ndouble f(int x) { return fabs(x); }\n",
        // -Wempty-body: an if's empty statement on its condition's line, and a switch's.
        "int f(int x) { if (x); return x; }\n",
        "int f(int x) { switch (x); return x; }\n",
    };
    for (uint32_t i = 0; i < sizeof(bail_cases) / sizeof(bail_cases[0]); i += 1)
    {
        if (c_direct_test_compile_source(bail_cases[i], obj_path, 1, NULL, NULL))
        {
            passed += 1u;
        }
        else
        {
            fprintf(stderr, "test-direct: FAIL bailout case %u\n", i);
            failed += 1u;
        }
    }

    // The block after a designator extends an incomplete array; the general evaluator handles the expression after it
    // and the final designator. (#3577: a second block over the first, `[0] = 300, 101, ...`, is the full pipeline's
    // -Winitializer-overrides, and 12345678 for 24910 its -Wconstant-conversion: bailout cases above.)
    static const char table_source[] =
        "const unsigned short table[] = { [2] = 77, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,"
        " 9+7, [22] = 24910 };";
    static const uint16_t expected_table[] = {
        0, 0, 77, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 0, 0, 24910
    };
    if (c_direct_test_compile_source_with(table_source, obj_path, 0, NULL, 0, false, NULL, NULL, NULL, NULL, expected_table, sizeof(expected_table), (CDirectTestMode){ 0 }))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL integer initializer bytes\n");
        failed += 1u;
    }

    if (c_direct_test_tables_and_inline_bodies(obj_path))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL initialized data or deferred function order\n");
        failed += 1u;
    }

    if (c_direct_test_declarations(obj_path))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL declarations' symbols: inline definitions, labels, visibility, weak\n");
        failed += 1u;
    }

    if (c_direct_test_lexer())
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL lexer\n");
        failed += 1u;
    }

    if (c_direct_test_macros())
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL macro calls and directives\n");
        failed += 1u;
    }

    if (c_direct_test_small_unit())
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL a small unit's start\n");
        failed += 1u;
    }

    if (c_direct_test_trigraph_scan())
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL the trigraph scan\n");
        failed += 1u;
    }

    if (c_direct_test_language_tokens())
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL the language modes' tokens\n");
        failed += 1u;
    }

    if (c_direct_test_language_compiles(obj_path))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL the language modes' compiles\n");
        failed += 1u;
    }

    if (c_direct_test_admission())
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL the driver's admission of the language modes and -w\n");
        failed += 1u;
    }

    if (c_direct_test_aliases(obj_path))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL aliases and typeof\n");
        failed += 1u;
    }

    if (c_direct_test_specifiers(obj_path))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL a declaration's specifiers\n");
        failed += 1u;
    }

    if (c_direct_test_raw_strings(obj_path))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL raw string literals and a directive's literals\n");
        failed += 1u;
    }

    if (c_direct_test_pointer_parameters(obj_path))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL parenthesized pointer parameters\n");
        failed += 1u;
    }

    if (c_direct_test_asm(obj_path))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL inline assembly\n");
        failed += 1u;
    }

    if (c_direct_test_atomics(obj_path))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL musl's atomics' inline assembly\n");
        failed += 1u;
    }

    if (c_direct_test_huge_val(obj_path))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL HUGE_VAL\n");
        failed += 1u;
    }

    if (c_direct_test_pretty_function(obj_path))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL __PRETTY_FUNCTION__\n");
        failed += 1u;
    }

    if (c_direct_test_assert(obj_path))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL glibc's assert\n");
        failed += 1u;
    }

    if (c_direct_test_round(obj_path))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL ceil and floor\n");
        failed += 1u;
    }

    if (c_direct_test_statement_depth(obj_path))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL statement nesting\n");
        failed += 1u;
    }

    if (c_direct_test_unevaluated_registers(obj_path))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL a sizeof operand's registers\n");
        failed += 1u;
    }

    if (c_direct_test_dead_arms(obj_path))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL dead arms of constant conditions\n");
        failed += 1u;
    }

    if (c_direct_test_ident_growth())
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL identifier tables' growth\n");
        failed += 1u;
    }

    if (c_direct_test_overwrite(obj_path))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL an object over a longer file\n");
        failed += 1u;
    }

    if (c_direct_test_include_search(obj_path))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL include search past a directory without the header\n");
        failed += 1u;
    }

    if (c_direct_test_dependencies(obj_path))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL -MD's records of the files a unit reads\n");
        failed += 1u;
    }

    printf("test-direct: %u passed, %u failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

int run_c_direct_tests(int argc, char** argv)
{
    int result = 0;
    if (argc >= 1 && strcmp(argv[0], "--differential") == 0)
    {
        result = c_direct_differential(argc - 1, argv + 1);
    }
    else
    {
        result = c_direct_unit_tests();
    }
    return result;
}

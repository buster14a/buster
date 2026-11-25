// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Standalone translation unit for the fused AVX-512 direct-to-ELF64 C17 compiler (#2850).
// Owns c_direct_lex.c, c_direct_x86.c, c_direct_types.c, and c_direct_cc.c privately so
// edits rebuild only this unit and internal symbols remain strictly TU-local.

#define _GNU_SOURCE

#include "buster/compiler/c_direct.h"
#include "buster/compiler/c_direct_cc.c"

#include <sys/wait.h>

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

    bool ok = pp != NULL && obj != NULL && cc != NULL && fast_pp_init(pp, &arena, &scratch, small);
    if (ok)
    {
        pp->unsigned_char = request->unsigned_char;
        fast_pp_set_language(pp, request->c23, request->strict_iso, request->no_gnu_keywords, request->trigraphs);
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

    // 1. Predefines
    if (ok)
    {
        if (request->predefines_path != NULL)
        {
            ok = fast_pp_run_file(pp, request->predefines_path);
        }
        else if (request->predefines_text != NULL && request->predefines_length > 0)
        {
            ok = fast_pp_run_buffer(pp, "<predefines>", request->predefines_text, request->predefines_length);
            if (ok)
            {
                char eval_buf[64];
                const int elen = snprintf(eval_buf, sizeof(eval_buf), "#define __FLT_EVAL_METHOD__ %u\n", request->flt_eval_method);
                if (elen > 0)
                {
                    ok = fast_pp_run_buffer(pp, "<predefines>", eval_buf, (uint64_t)elen);
                }
            }
        }
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
                ok = fast_pp_run_file(pp, inc);
            }
            else if (fast_pp_resolve_include(pp, inc, (uint32_t)strlen(inc), false, false, resolved, sizeof(resolved), &dir_idx, &is_sys))
            {
                ok = fast_pp_run_file(pp, resolved);
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
        ok = fast_pp_run_file(pp, request->input_path);
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

    // Always release on bailout (caller will run full pipeline) or when caller requests cleanup
    if (status != 0 || request->release)
    {
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
    uint32_t size;
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
        bool found = false;
        for (size_t i = 0; i < symtab->sh_size / sizeof(*syms); i += 1)
        {
            const Elf64_Sym sym = syms[i];
            if (sym.st_name > names->sh_size || name_size > names->sh_size - sym.st_name ||
                memcmp(file + names->sh_offset + sym.st_name, want->name, name_size) != 0) { continue; }
            if (sym.st_shndx == SHN_UNDEF || sym.st_shndx >= sections) { break; }
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
    { "u'x'", 1, { FTOK_CHAR_LIT } },
    { "\"caf\xc3\xa9\"", 1, { FTOK_STR_LIT } },
    { "a /* caf\xc3\xa9 */ b", 2, { FTOK_IDENT, FTOK_IDENT } },
    { "x = 1 \\\n + 2;", 6, { FTOK_IDENT, FTOK_ASSIGN, BUSTER_DIRECT_TEST_NUMBER, FTOK_PLUS, BUSTER_DIRECT_TEST_NUMBER, FTOK_SEMI } },
    { "a@b", 3, { FTOK_IDENT, FTOK_PP_OTHER, FTOK_IDENT } },
    { "a<\xbd" "b", 4, { FTOK_IDENT, FTOK_LT, FTOK_PP_OTHER, FTOK_IDENT } },
    { "a+\xab-\xad", 5, { FTOK_IDENT, FTOK_PLUS, FTOK_PP_OTHER, FTOK_MINUS, FTOK_PP_OTHER } },
    { "1..2", 1, { BUSTER_DIRECT_TEST_NUMBER } },
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
            fast_pp_set_language(pp, mode.c23, mode.strict_iso, mode.no_gnu_keywords, mode.trigraphs);
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

// Whether every used slot of the cache is where its key's hash puts it, names an interned name of its key's length,
// kind and macro bit, and the cache has `bits`.
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
                 slot->macro == (uint8_t)fast_ident_is_macro(t, slot->id) &&
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
                uint8_t macro = 0;
                const uint32_t before_bits = table.cache_bits;
                const bool grows = table.count + 1u == table.cache_grow_at && before_bits <= BUSTER_FIDENT_CACHE_BITS;
                uint32_t held = 0;
                if (grows)
                {
                    memcpy(before, table.cache, ((size_t)1 << before_bits) * sizeof(FastIdentCacheSlot));
                }
                ids[i] = fast_ident_lookup(&table, (const uint8_t*)name, length, &kind, &macro);
                ok = ids[i] == seeded + i + 1u && kind == FTOK_IDENT && macro == 0;
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
                uint8_t macro = 0;
                const uint32_t id = fast_ident_lookup(&table, (const uint8_t*)name, length, &kind, &macro);
                ok = id == ids[i] && kind == FTOK_IDENT && macro == 0;
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
                    uint8_t macro = 0;
                    cached += 1u;
                    ok = fast_ident_lookup(&table, (const uint8_t*)name, length, &kind, &macro) == ids[i];
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

// The cache slots' macro flags (buster#3583): a slot's flag follows fast_ident_set_macro_bit for the name the slot holds
// and not for another name with the same slot, a name whose bit was set before its first lookup has the flag from the
// bit, the cache stays consistent through all of it and through its growth, and a name that a #define makes a macro,
// an #undef a name and another #define a macro again is read so where a window emits it.
static bool c_direct_test_ident_macro_flags(void)
{
    FastArena arena = { 0 };
    bool ok = fast_arena_init(&arena, UINT64_C(32) * 1024u * 1024u, true);
    FastIdentTable table;
    ok = ok && fast_ident_init(&table, &arena, true);
    enum { longest = 40 };
    uint32_t ids[longest + 1] = { 0 };
    for (uint32_t length = 1; ok && length <= longest; length += 1)
    {
        char name[longest + 8] = { 0 };
        for (uint32_t j = 0; j < length; j += 1)
        {
            name[j] = (char)('a' + (j * 7u + length) % 26u);
        }
        uint8_t kind = 0;
        uint8_t macro = 1;
        ids[length] = fast_ident_lookup(&table, (const uint8_t*)name, length, &kind, &macro);
        ok = ids[length] != 0 && kind == FTOK_IDENT && macro == 0;
        // Its bit both ways, read from its slot.
        fast_ident_set_macro_bit(&table, ids[length], true);
        ok = ok && fast_ident_lookup(&table, (const uint8_t*)name, length, &kind, &macro) == ids[length] && macro == 1u &&
             c_direct_test_cache_consistent(&table, table.cache_bits);
        fast_ident_set_macro_bit(&table, ids[length], false);
        ok = ok && fast_ident_lookup(&table, (const uint8_t*)name, length, &kind, &macro) == ids[length] && macro == 0;
    }
    // A keyword's slot says it is a macro too (`#define inline __inline`).
    const uint32_t keyword = ok ? fast_ident_intern_cstr(&table, "inline") : 0;
    if (keyword != 0)
    {
        uint8_t kind = 0;
        uint8_t macro = 1;
        ok = fast_ident_lookup(&table, (const uint8_t*)"inline\0\0", 6u, &kind, &macro) == keyword && kind == FTOK_KW_INLINE && macro == 0;
        fast_ident_set_macro_bit(&table, keyword, true);
        ok = ok && fast_ident_lookup(&table, (const uint8_t*)"inline\0\0", 6u, &kind, &macro) == keyword && kind == FTOK_KW_INLINE && macro == 1u;
    }
    else
    {
        ok = false;
    }
    // A name interned without a lookup and made a macro: its first lookup fills its slot with the flag.
    const uint32_t fresh = ok ? fast_ident_intern_cstr(&table, "a_fresh_macro") : 0;
    if (fresh != 0)
    {
        uint8_t kind = 0;
        uint8_t macro = 0;
        fast_ident_set_macro_bit(&table, fresh, true);
        ok = fast_ident_lookup(&table, (const uint8_t*)"a_fresh_macro", 13u, &kind, &macro) == fresh && macro == 1u;
    }
    else
    {
        ok = false;
    }
    // Two names with the same slot: the second's lookup takes the slot, and the first's bit leaves the second's flag.
    char pair_names[2][16] = { { 0 } };
    uint32_t pair_lengths[2] = { 0 };
    uint32_t pair[2] = { 0 };
    uint32_t seen_slots[512] = { 0 };
    for (uint32_t i = 0; ok && pair[1] == 0 && i < 512u; i += 1)
    {
        char name[16] = { 0 };
        const uint32_t length = (uint32_t)snprintf(name, sizeof(name), "c%u", i);
        uint64_t first = 0;
        memcpy(&first, name, sizeof(first));
        seen_slots[i] = (uint32_t)(BUSTER_FIDENT_CACHE_HASH(_bzhi_u64(first, length * 8u), UINT64_C(0)) >> table.cache_shift);
        for (uint32_t j = 0; j < i && pair[1] == 0; j += 1)
        {
            if (seen_slots[j] == seen_slots[i])
            {
                uint8_t kind = 0;
                uint8_t macro = 0;
                pair_lengths[0] = (uint32_t)snprintf(pair_names[0], sizeof(pair_names[0]), "c%u", j);
                pair_lengths[1] = length;
                memcpy(pair_names[1], name, sizeof(name));
                pair[0] = fast_ident_lookup(&table, (const uint8_t*)pair_names[0], pair_lengths[0], &kind, &macro);
                pair[1] = fast_ident_lookup(&table, (const uint8_t*)pair_names[1], pair_lengths[1], &kind, &macro);
            }
        }
    }
    if (ok && pair[0] != 0 && pair[1] != 0 && pair[0] != pair[1])
    {
        uint8_t kind = 0;
        uint8_t macro = 0;
        fast_ident_set_macro_bit(&table, pair[0], true);
        ok = fast_ident_lookup(&table, (const uint8_t*)pair_names[1], pair_lengths[1], &kind, &macro) == pair[1] && macro == 0 &&
             fast_ident_lookup(&table, (const uint8_t*)pair_names[0], pair_lengths[0], &kind, &macro) == pair[0] && macro == 1u &&
             c_direct_test_cache_consistent(&table, table.cache_bits);
    }
    else
    {
        ok = false;
    }
    // The flags through the cache's growth: every tenth name of 9,000 a macro.
    for (uint32_t i = 0; ok && i < 9000u; i += 1)
    {
        char name[24] = { 0 };
        const uint32_t length = (uint32_t)snprintf(name, sizeof(name), "grown_%u", i);
        uint8_t kind = 0;
        uint8_t macro = 0;
        const uint32_t id = fast_ident_lookup(&table, (const uint8_t*)name, length, &kind, &macro);
        ok = id != 0 && macro == 0;
        if (ok && i % 10u == 0)
        {
            fast_ident_set_macro_bit(&table, id, true);
        }
    }
    ok = ok && table.cache_bits > BUSTER_FIDENT_SMALL_CACHE_BITS && c_direct_test_cache_consistent(&table, table.cache_bits);
    for (uint32_t i = 0; ok && i < 9000u; i += 1)
    {
        char name[24] = { 0 };
        const uint32_t length = (uint32_t)snprintf(name, sizeof(name), "grown_%u", i);
        uint8_t kind = 0;
        uint8_t macro = 0;
        ok = fast_ident_lookup(&table, (const uint8_t*)name, length, &kind, &macro) != 0 && macro == (i % 10u == 0 ? 1u : 0u);
    }
    fast_arena_destroy(&arena);

    // Through the preprocessor: a name that is a macro, then not, then a macro again, read where a window emits it.
    static const char text[] =
        "int a_name_that_changes;\n#define a_name_that_changes 1\nx = a_name_that_changes;\n#undef a_name_that_changes\n"
        "y = a_name_that_changes;\n#define a_name_that_changes 2\nz = a_name_that_changes;\n";
    uint8_t kinds[64] = { 0 };
    uint32_t data[64] = { 0 };
    uint32_t count = 0;
    ok = ok && c_direct_test_tokens(text, sizeof(text) - 1u, kinds, data, 64u, &count) && count == 15u &&
         kinds[1] == FTOK_IDENT && c_direct_test_kind_is_number(kinds[5]) && data[5] == 1u && kinds[9] == FTOK_IDENT && data[9] == data[1] &&
         c_direct_test_kind_is_number(kinds[13]) && data[13] == 2u;
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
    char object[256];
    snprintf(plain, sizeof(plain), "/tmp/buster_direct_admit_%d.c", (int)getpid());
    snprintf(trigraph, sizeof(trigraph), "/tmp/buster_direct_admit_tri_%d.c", (int)getpid());
    snprintf(keyword, sizeof(keyword), "/tmp/buster_direct_admit_asm_%d.c", (int)getpid());
    snprintf(comma, sizeof(comma), "/tmp/buster_direct_admit_comma_%d.c", (int)getpid());
    snprintf(main_void, sizeof(main_void), "/tmp/buster_direct_admit_main_%d.c", (int)getpid());
    snprintf(object, sizeof(object), "/tmp/buster_direct_admit_%d.o", (int)getpid());
    static const char plain_text[] = "int add(int a, int b) { return a + b; }\n";
    static const char trigraph_text[] = "/* ?" "?= */\nint add(int a, int b) { return a + b; }\n";
    static const char keyword_text[] = "int asm = 1;\nint get(void) { return asm; }\n";
    static const char comma_text[] = "#define G(...) f(0, ## __VA_ARGS__)\nint f(int a, ...) { return a; }\nint get(void) { return G(); }\n";
    static const char main_text[] = "void main(void) { }\n";
    const char* const paths[5] = { plain, trigraph, keyword, comma, main_void };
    const char* const texts[5] = { plain_text, trigraph_text, keyword_text, comma_text, main_text };
    bool ok = true;
    for (uint32_t i = 0; ok && i < 5u; i += 1)
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
    for (uint32_t i = 0; i < 5u; i += 1)
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
    // a newline splice, and a digit separator.
    for (uint32_t shift = 0; ok && shift < 131u; shift += 1)
    {
        static const char* const refused[] = { "ab\\\ncd", "1'000" };
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

static bool c_direct_test_tables_and_inline_bodies(const char* obj_path)
{
    static const char source[] =
        "unsigned char bytes[] = {0,1,22,33,44,55,66,77,88,99,100,111,122,133,144,155,"
        "0x1ff,030,8+2,[23]=7,[2]=99};\n"
        "unsigned short rows[2][17] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,"
        "17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,65537};\n"
        "unsigned int decimals[] = {0,1,22,333,4444,55555,666666,7777777,88888888,"
        "9,10,11,12,13,14,15,123456789,2.5,3+4};\n"
        "unsigned long wide[] = {0xffffffffU,1,2,3,4,5,6,7,8,9,10,11,12,13,14,2147483648U,-2};\n"
        "static inline int a(int), b(int), c(int), d(int);\n"
        "static inline int a(int n){return n ? d(n-1) : 1;}\n"
        "static inline int b(int n){return n ? a(n-1)+c(n-1) : 2;}\n"
        "static inline int c(int n){return a(n); }\n"
        "static inline int d(int n){return b(n); }\n"
        "static inline int unused(void){return undeclared;}\n"
        "int (*hook)(int)=b; int entry(int n){return hook(n); }\n";
    static const uint8_t bytes[24] = {0,1,99,33,44,55,66,77,88,99,100,111,122,133,144,155,255,24,10,[23]=7};
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

int run_c_direct_tests(int argc, char** argv)
{
    (void)argc;
    (void)argv;
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

    // The first block extends an incomplete array, the second overwrites it after a designator.
    // The general evaluator handles the intervening expression and the final designator.
    static const char table_source[] =
        "const unsigned short table[] = { [2] = 77, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,"
        " 9+7, [0] = 300, 101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,"
        " [20] = 12345678 };";
    static const uint16_t expected_table[] = {
        300, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 14, 15, 16, 24910
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

    if (c_direct_test_ident_growth())
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL identifier tables' growth\n");
        failed += 1u;
    }

    if (c_direct_test_ident_macro_flags())
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL identifier cache's macro flags\n");
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

    printf("test-direct: %u passed, %u failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

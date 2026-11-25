// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Standalone translation unit for the fused AVX-512 direct-to-ELF64 C17 compiler (#2850).
// Owns c_direct_lex.c, c_direct_x86.c, c_direct_types.c, and c_direct_cc.c privately so
// edits rebuild only this unit and internal symbols remain strictly TU-local.

#define _GNU_SOURCE

#include "buster/compiler/c_direct.h"
#include "buster/compiler/c_direct_cc.c"

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

    FastArena arena = { 0 };
    FastArena scratch = { 0 };
    if (!fast_arena_init(&arena, UINT64_C(32) * 1024u * 1024u, request->avoid_huge_pages) ||
        !fast_arena_init(&scratch, UINT64_C(6) * 1024u * 1024u, request->avoid_huge_pages))
    {
        fast_arena_destroy(&scratch);
        fast_arena_destroy(&arena);
        return 1;
    }

    FastPP* pp = (FastPP*)fast_arena_alloc(&scratch, sizeof(FastPP), 64);
    FastObj* obj = (FastObj*)fast_arena_alloc(&scratch, sizeof(FastObj), 64);
    FastCCState* cc = (FastCCState*)fast_arena_alloc(&scratch, sizeof(FastCCState), 64);

    bool ok = pp != NULL && obj != NULL && cc != NULL && fast_pp_init(pp, &arena, &scratch);
    if (ok)
    {
        pp->unsigned_char = request->unsigned_char;
        pp->c23 = request->c23;
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

static bool c_direct_test_compile_source(const char* source, const char* obj_path, int expected_status,
                                         const void* expected_rodata, size_t expected_bytes)
{
    char src_path[256];
    snprintf(src_path, sizeof(src_path), "/tmp/buster_direct_test_%d.c", (int)getpid());
    FILE* f = fopen(src_path, "wb");
    if (f == NULL) { return false; }
    const size_t slen = strlen(source);
    const bool wrote = fwrite(source, 1, slen, f) == slen;
    fclose(f);
    if (!wrote) { unlink(src_path); return false; }

    unlink(obj_path);
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
        .pic = true,
        .unwind_tables = true,
        .unsigned_char = false,
        .c23 = false,
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
    unlink(obj_path);

    const Elf64_Ehdr* eh = (const Elf64_Ehdr*)buf;
    bool valid = eh->e_ident[0] == 0x7fu && eh->e_ident[1] == 'E' && eh->e_ident[2] == 'L' && eh->e_ident[3] == 'F' &&
                 eh->e_ident[4] == ELFCLASS64 && eh->e_type == ET_REL && eh->e_machine == EM_X86_64 &&
                 eh->e_shoff + (uint64_t)eh->e_shnum * sizeof(Elf64_Shdr) <= (uint64_t)fsz;
    uint32_t text_len = 0;
    uint32_t sym_count = 0;
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
    free(buf);
    return valid && sym_count > 0 && (text_len > 0 || expected_rodata != NULL);
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
    uint8_t kinds[7];
};

static const CDirectLexCase c_direct_lex_cases[] = {
    { "a", 1, { FTOK_IDENT } },
    { "foo_bar1", 1, { FTOK_IDENT } },
    { "an_identifier_of_more_than_sixty_four_bytes_which_no_block_holds_whole_", 1, { FTOK_IDENT } },
    { "an_identifier_of_more_than_two_blocks_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 1, { FTOK_IDENT } },
    { "an_identifier_of_more_than_two_hundred_and_fifty_five_bytes_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 1, { FTOK_IDENT } },
    { "int", 1, { FTOK_KW_INT } },
    { "return", 1, { FTOK_KW_RETURN } },
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
static bool c_direct_test_tokens(const char* text, uint64_t length, uint8_t* kinds, uint32_t* data, uint32_t capacity, uint32_t* out_count)
{
    FastArena arena = { 0 };
    FastArena scratch = { 0 };
    bool ok = fast_arena_init(&arena, UINT64_C(32) * 1024u * 1024u, true) && fast_arena_init(&scratch, UINT64_C(6) * 1024u * 1024u, true);
    if (ok)
    {
        FastPP* pp = (FastPP*)fast_arena_alloc(&scratch, sizeof(FastPP), 64);
        ok = pp != NULL && fast_pp_init(pp, &arena, &scratch) && fast_pp_run_buffer(pp, "<test>", text, length) && pp->out_count <= capacity;
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
        "#define TWO 2\n#define ID(x) x\n#define EMPTY\n#define F(a, b) a+b\n#define STR(x) #x\n#define CAT(a, b) a ## b\n";
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
                                  (c == '.' ? BUSTER_FCLASS_DOT : 0u) | (c == '.' || (c >= '0' && c <= '9') ? BUSTER_FCLASS_DOT_NEXT : 0u);
        ok = ok && fast_lex_class[c] == expected;
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
        if (c_direct_test_compile_source(valid_cases[i], obj_path, 0, NULL, 0))
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
        if (c_direct_test_compile_source(bail_cases[i], obj_path, 1, NULL, 0))
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
    if (c_direct_test_compile_source(table_source, obj_path, 0, expected_table, sizeof(expected_table)))
    {
        passed += 1u;
    }
    else
    {
        fprintf(stderr, "test-direct: FAIL integer initializer bytes\n");
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

    printf("test-direct: %u passed, %u failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

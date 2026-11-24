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
                                         uint32_t* out_text_len, uint32_t* out_sym_count)
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
    if (out_text_len != NULL) { *out_text_len = text_len; }
    if (out_sym_count != NULL) { *out_sym_count = sym_count; }
    return valid;
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

    printf("test-direct: %u passed, %u failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

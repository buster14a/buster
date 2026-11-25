// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Fused AVX-512 direct-to-ELF64 C17 single-TU compiler (#2850).
// Compiles clean single-TU -c object builds in a single arena-backed pass without building ASTs,
// IR graphs, or intermediate link objects. Bails out silently (writing nothing) on any diagnostic,
// warning-sensitive construct, or unsupported extension so the driver falls back to the full pipeline.

#ifndef BUSTER_C_DIRECT_H
#define BUSTER_C_DIRECT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct CDirectRequest CDirectRequest;
struct CDirectRequest
{
    const char* input_path;
    const char* output_path;         // NULL -> default <basename>.o in current directory
    const char* predefines_text;     // built-in predefines buffer (when predefines_path == NULL)
    uint32_t predefines_length;
    uint32_t flt_eval_method;
    const char* predefines_path;     // optional --predefines FILE path
    const char* const* directories;
    const uint8_t* directory_system;
    uint32_t directory_count;
    uint32_t angled_first;
    const char* const* macros;       // "Dname", "Dname=val", or "Uname"
    uint32_t macro_count;
    const char* const* includes;     // -include files
    uint32_t include_count;
    bool pic;
    bool unwind_tables;
    bool unsigned_char;
    bool c23;
    bool strict_iso;                 // -std=cNN without GNU's extensions: `, ## __VA_ARGS__` and main as clang has them there
    bool no_gnu_keywords;            // asm, and typeof before C23, are identifiers (-std=cNN, -fno-gnu-keywords, -fno-asm)
    bool trigraphs;                  // trigraphs are replaced (-std=c99/c11/c17, -trigraphs): a unit that has one leaves
    uint8_t raw_strings;             // raw string literals (#3600): 1 their R and LR forms (GNU's modes from C99, as the full
                                     // pipeline's pp_raw_strings), 2 also u8R, uR and UR (from C11); a unit with one leaves
    bool quiet;                      // -w (suppress warnings)
    bool release;                    // release arenas before return (false for terminal CLI exit)
    bool avoid_huge_pages;           // busy host (#672/#1561): advise MADV_NOHUGEPAGE to avoid 8-core THP lock contention
    void (*mark_stage)(void* context, uint32_t stage_id);
    void* clock_context;
    uint32_t stage_setup;
    uint32_t stage_preprocess;
    uint32_t stage_codegen;
    uint32_t stage_object_write;
    uint32_t* out_token_count;       // optional: receives preprocessed token count for --time-stages
};

// Returns 0 on success (object written), 1 on clean bailout (nothing written -> caller runs full pipeline),
// or -1 on fatal output-file I/O error (reported on stderr).
int c_direct_try_compile(const CDirectRequest* request);

// Self-tests for the fused direct-to-ELF compiler (`build/compiler test-direct`).
int run_c_direct_tests(int argc, char** argv);

#endif

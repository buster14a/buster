// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU assembly frontend. asm_assemble consumes a bounded source slice and caller-owned arena, returning the shared
// Link representation. No C parser, subprocess, allocation per record, or modification of the final encoder record.
// Layout map: asm_lex; symbols and expressions; operands; directives/statements; matching; fragment layout; emission;
// asm_assemble_code, a source's records for code that another function's records hold (C's inline assembly, #147).
// The public boundary rejects unsupported syntax and validates records before trusted encoding. Layout only promotes
// branches: each can grow once, alignment is recomputed every round, and a final fixed-layout check covers rel8-only
// instructions. This terminates after at most branch_count + 1 rounds even when alignment absorbs earlier growth.

// Source offsets and token counts are 32-bit, with four lookahead records. The symbol table's power-of-two slot
// count is also 32-bit and at least twice the parser's token capacity.
#define BUSTER_ASM_MAX_SOURCE (UINT32_MAX - 4u)
#define BUSTER_ASM_MAX_TOKENS ((1u << 30) - 4u)
#define BUSTER_ASM_MAX_SECTION (256u << 20)
#define BUSTER_ASM_EXPR_DEPTH 128
#define BUSTER_ASM_WORD 256
#define BUSTER_ASM_STRING 257
#define BUSTER_ASM_MAX_SECTIONS 65536

typedef struct AsmToken AsmToken;
struct AsmToken
{
    uint32_t start;
    uint32_t end;
    uint32_t kind;
    uint32_t reserved; // Zero after lexing; the vector parser caches bounded register classification here.
};

// GNU's relocation modifiers (`symbol@PLT`, `symbol@GOTPCREL`, and the TLS models' `symbol@TPOFF`, `@GOTTPOFF`,
// `@TLSGD`, `@TLSLD` and `@DTPOFF`, #1115). They attach to one symbol term and stay with it through constant addends;
// of data directives, `.long` takes a TLS offset's (asm_data_value).
typedef enum AsmRelocation
{
    ASM_RELOCATION_NONE,
    ASM_RELOCATION_PLT,
    ASM_RELOCATION_GOTPCREL,
    ASM_RELOCATION_GOTPCRELX,
    ASM_RELOCATION_TPOFF,
    ASM_RELOCATION_GOTTPOFF,
    ASM_RELOCATION_TLSGD,
    ASM_RELOCATION_TLSLD,
    ASM_RELOCATION_DTPOFF,
} AsmRelocation;

typedef struct AsmExpr AsmExpr;
struct AsmExpr
{
    __int128 value;
    uint32_t plus;
    uint32_t minus;
    // Intel addresses keep register terms through parentheses. These fields occupy the record's former padding.
    // Architectural GPR number plus one; 17 is RIP; a vector index 32 + 32 * (class - XMM) + number; a 32-bit GPR 128 plus
    // its number plus one, and 145 EIP (#2339).
    uint8_t registers[2];
    uint8_t scales[2];
    uint8_t register_count;
    uint8_t indexed; // Multiplication explicitly chooses an index, including a scale of one.
    uint16_t relocation;
};

enum
{
    ASM_EXPR_SHL = 300,
    ASM_EXPR_SHR,
    ASM_EXPR_EQ,
    ASM_EXPR_NE,
    ASM_EXPR_LE,
    ASM_EXPR_GE,
    ASM_EXPR_AND,
    ASM_EXPR_OR,
};

typedef struct AsmSymbol AsmSymbol;
struct AsmSymbol
{
    String8 name;
    AsmExpr size;
    __int128 constant;
    uint32_t fragment;
    uint32_t location;
    SymbolId link;
    uint32_t section_index;
    uint32_t group;
    uint32_t group_flags;
    // A mention outside a group declaration, and an expression use (including one cancelled by arithmetic).
    bool ordinary;
    bool referenced;
    // An explicit binding, visibility, type or size directive named this symbol.
    bool section_attributes;
    uint8_t flags;
    bool defined;
    bool absolute;
    bool explicit_binding;
    bool has_size;
    bool internal;
    // GNU as gives every section symbol its name: `.long .debug_abbrev` is that section's start, not an undefined
    // external. A later label of the same name replaces this.
    bool section_start;
    // A TLS model's relocation modifier names it (#1115): GNU as makes it STT_TLS, an undefined one too.
    bool tls;
    // `.set name, symbol + constant` (#1115): an alias, whose references become alias_target's (asm_resolve_aliases), or
    // where that symbol is defined, a symbol at its place plus the constant: `offset` from its fragment's.
    bool alias;
    AsmExpr alias_target;
    int64_t offset;
    // An alias whose target wasn't defined at its `.set`, which takes the target's type and size once it is (GNU as's
    // copy_symbol_attributes; asm_alias_inherit).
    bool inherits;
    // An alias of it names it (#1115): GNU as lists an undefined weak symbol only when something uses it, a relocation
    // or an alias, and every other undefined symbol the source names (asm_object_options).
    bool used;
    // A size that reaches past its section's end, which GNU as writes and the Link's symbol, within its atom, can't
    // hold: the object's st_size (ObjectOptions.symbol_sizes), or 0.
    uint64_t beyond;
    // `.comm` and `.lcomm` (#1115): ASM_COMMON_GLOBAL for a common symbol (SHN_COMMON, of common_size bytes aligned to
    // common_alignment: a COMMON symbol of its own atom in the Link, asm_emit), ASM_COMMON_BSS for one .bss holds
    // (.lcomm, or a local's .comm), placed after .bss's other contents in directive order (common_next, from
    // AsmParser.common_first), as GNU as places them.
    uint8_t common;
    uint32_t common_alignment;
    uint32_t common_next;
    uint64_t common_size;
    // A global common made local and declared again: the bytes before its place in .bss (asm_common_directive).
    uint64_t common_bias;
    // GCC's `.loc ... view .LVU3` names it (#1115): the line row whose view number it is, plus one, which data reads
    // (asm_resolve; assembler_lines.c).
    uint32_t view;
};

#define ASM_COMMON_GLOBAL 1
#define ASM_COMMON_BSS 2

typedef enum AsmFragmentKind
{
    ASM_LABEL,
    ASM_INSTRUCTION,
    ASM_VALUE,
    ASM_BYTES,
    ASM_FILL,
    ASM_ALIGN,
    ASM_ULEB,
    ASM_SLEB,
} AsmFragmentKind;

// How an ASM_ALIGN fragment pads: with its fill byte, or in code (no fill, or 0x90) with GNU as's NOPs, which start
// with a one-byte nop when the section's last statement was data.
typedef enum AsmPadding
{
    ASM_PADDING_FILL,
    ASM_PADDING_NOPS,
    ASM_PADDING_NOPS_AFTER_DATA,
} AsmPadding;

typedef struct AsmFragment AsmFragment;
struct AsmFragment
{
    AsmExpr expression;
    uint8_t* bytes;
    uint32_t offset;
    uint32_t size;
    uint32_t location;
    uint32_t instruction;
    uint32_t alignment;
    uint32_t maximum;
    uint8_t kind;
    uint32_t section;
    uint8_t fill;
    uint8_t padding;
};

typedef struct AsmInstruction AsmInstruction;
struct AsmInstruction
{
    X86MatchRequest request;
    AsmExpr expressions[4];
    X86Instruction record;
    // The instruction's mnemonic as the source writes it (its own token, after any prefix), which a diagnostic names.
    String8 written;
    uint32_t location;
    uint32_t fragment;
    // Whether an AT&T suffix gave request.width without the parser checking each operand against it: the matcher does,
    // by each form's suffix width (x86_match_suffix_width), and a failure tries the operands without it to say so.
    bool suffixed;
    // REP BSF/BSR is matched as a baseline scan, then encoded with its F3-prefixed form.
    bool rep_scan;
    bool branch;
    bool local_branch;
    // A memory operand of 32-bit base and index registers (or EIP) in 64-bit mode (#2339): the record encodes the
    // address with their 64-bit numbers, and the bytes take a 67 (asm_emit_address32), one byte more than the match.
    bool address32;
};

typedef struct AsmOptions AsmOptions;
struct AsmOptions
{
    bool vectorized;
    bool intel;
    // A C unit's assembly (inline templates and file-scope asm), which follows clang's integrated assembler where it
    // differs from GNU as: `.comm` without an alignment and `.lcomm` align to 1 (#2304). `.s` files keep GNU as's rules,
    // which test-assembler's cross-check compares.
    bool integrated;
};

typedef struct AsmAbsolute AsmAbsolute;
struct AsmAbsolute
{
    String8 name;
    int64_t value;
};

// Per Link symbol, the directives the source gave it: a binding directive (`.globl`, `.local`, `.weak`) and a `.size`.
// The C checker applies them to a program symbol a template's name maps to, as clang's assembled module does. And an
// `.lcomm` or a local's `.comm` (ASM_COMMON_BSS), whose local binding clang's assembler reports when C's definition of
// the name is global (#1848).
#define BUSTER_ASM_DIRECTIVE_BINDING 0x01
#define BUSTER_ASM_DIRECTIVE_SIZE 0x02
#define BUSTER_ASM_DIRECTIVE_LOCAL_COMMON 0x04

typedef struct AsmResult AsmResult;
struct AsmResult
{
    Link link;
    String8 source_name;
    ObjectOptions object_options;
    const char* error;
    // The instruction an error names, as the source writes it (a slice of the source), or empty (#1134).
    String8 error_name;
    uint32_t location;
    uint32_t rounds;
    uint32_t instructions;
    // The source's absolute (`.set`) symbols with names, as GNU as and clang keep them locally: another template of the
    // same C unit doesn't see them (the checker refuses such a reference by name), and -c's object has none.
    AsmAbsolute* absolutes;
    uint32_t absolute_count;
    // Indexed by SymbolId: BUSTER_ASM_DIRECTIVE_BINDING, _SIZE and _LOCAL_COMMON for the symbols whose directives gave them.
    uint8_t* directives;
    // The atom the source's first fragment is in: the code other function's records hold starts there (asm_assemble_code
    // callers that assemble a whole unit, C's inline assembly helpers).
    AtomId entry_atom;
    // Whether the default text section has a fragment that emits bytes or a value (anything but a label or alignment):
    // a template of alignment and section directives alone has no code to run.
    bool entry_code;
    // Whether the source's first control flow, before any branch, is a return: a C helper whose frame would still be
    // reserved, which the checker refuses as it does the record path's.
    bool ret_first;
    // The source's common symbols (.comm of a global, #1115): in the Link each is #147's COMMON symbol of its own atom,
    // which the object writes as SHN_COMMON's. The C checker refuses a template's.
    uint32_t common_count;
    uint64_t lex_ns;
    uint64_t parse_ns;
    uint64_t match_ns;
    uint64_t layout_ns;
    uint64_t emit_ns;
    // .eh_frame's CFI and .debug_line's line table (#1115), which are written after layout.
    uint64_t cfi_ns;
};

typedef struct AsmSection AsmSection;
struct AsmSection
{
    ObjectAtomSection object;
    uint32_t group;
    uint32_t name_symbol;
    bool name_ambiguous;
    uint32_t size;
    AtomId atom;
    uint8_t class;
    uint8_t alignment;
    bool data;
    bool declared;
    // Its last line row, plus one (assembler_lines.c).
    uint32_t line_last;
};

typedef struct AsmParser AsmParser;
typedef struct AsmCfiEvent AsmCfiEvent;
struct AsmCfiEvent
{
    uint32_t fragment;
    uint32_t opcode;
    int64_t first;
    int64_t second;
    uint8_t* bytes;
    uint32_t size;
};

typedef struct AsmCfiProcedure AsmCfiProcedure;
struct AsmCfiProcedure
{
    uint32_t symbol;
    uint32_t end;
    uint32_t first;
    uint32_t count;
    uint32_t return_column;
    uint32_t initial_count;
    uint32_t relocation;
    // Its .debug_frame FDE's (#1115): the offset of its CIE pointer, the 64-bit address after it, and its CIE's.
    uint32_t debug_relocation;
    uint32_t debug_cie;
    bool simple;
    bool signal;
};

typedef struct AsmCfiState AsmCfiState;
struct AsmCfiState
{
    int64_t offset;
    uint32_t reg;
};

// GNU as's line table (#1115, assembler_lines.c): a row of `.loc`'s, at its label fragment's place in a code section.
typedef struct AsmLineRow AsmLineRow;
struct AsmLineRow
{
    uint32_t fragment;
    // The previous and the next row of its section, plus one.
    uint32_t previous;
    uint32_t next;
    // Its view number (asm_line_views), and what `.loc`'s `view` says of it (ASM_VIEW_*).
    uint32_t view;
    uint32_t file;
    uint32_t line;
    uint32_t column;
    uint32_t isa;
    uint32_t discriminator;
    uint8_t flags;
    uint8_t view_kind;
};

typedef struct AsmLineFile AsmLineFile;
struct AsmLineFile
{
    String8 name;
    uint32_t directory;
    uint8_t md5[16];
    bool assigned;
};

// A relocation of the generated .debug_line, which emission adds: a set address's ABS64, a line string's ABS32.
typedef struct AsmLineFixup AsmLineFixup;
struct AsmLineFixup
{
    uint32_t offset;
    uint32_t symbol;
    uint8_t kind;
};

// The location the latest `.loc` gives (GNU as's `current`), and whether its row waits for an instruction.
typedef struct AsmLineState AsmLineState;
struct AsmLineState
{
    uint32_t file;
    uint32_t line;
    uint32_t column;
    uint32_t isa;
    uint32_t discriminator;
    uint32_t view_symbol;
    uint8_t flags;
    uint8_t view_kind;
    bool pending;
};

struct AsmParser
{
    String8 source;
    Arena* arena;
    const X86MatchIndex* index;
    AsmOptions options;
    AsmResult result;
    AsmToken* tokens;
    AsmSymbol* symbols;
    AsmFragment* fragments;
    AsmInstruction* instructions;
    uint32_t* symbol_slots;
    uint32_t symbol_mask;
    uint32_t token_count;
    uint32_t at;
    uint32_t capacity;
    uint32_t instruction_capacity;
    uint32_t symbol_count;
    uint32_t fragment_count;
    uint32_t* fragment_order;
    uint32_t* fragment_begin;
    uint32_t instruction_count;
    uint32_t numeric[10];
    AsmSection* sections;
    uint32_t* section_slots;
    uint32_t section_mask;
    uint32_t* section_name_slots;
    uint32_t section_name_mask;
    uint32_t section_count;
    uint32_t section_capacity;
    uint32_t group_count;
    uint32_t section;
    // .pushsection/.popsection's stack, and the section .previous returns to.
    uint32_t* section_stack;
    uint32_t section_depth;
    uint32_t previous_section;
    AsmCfiEvent* cfi;
    AsmCfiProcedure* procedures;
    AsmCfiState* cfi_stack;
    AsmCfiState cfa;
    uint32_t cfi_count;
    uint32_t procedure_count;
    uint32_t cfi_depth;
    bool cfi_active;
    // .cfi_sections' choice (#1115): .eh_frame unless it names others, and .debug_frame; the .debug_frame section, the
    // label at its start, which its FDEs' CIE pointers are relocated against, and the fragment its bytes fill.
    bool cfi_no_eh_frame;
    bool cfi_debug_frame;
    uint32_t cfi_debug_section;
    uint32_t cfi_debug_label;
    uint32_t cfi_debug_fragment;
    bool prefix;
    // asm_assemble_code's source: code another function's records hold, which takes no directive but the syntax's.
    bool code;
    // The source used a directive the record path can't hold (asm_assemble_code), so that its caller assembles it whole.
    bool directive;
    // A relocation modifier other than @PLT appeared, and _GLOBAL_OFFSET_TABLE_ is declared, as GNU as does (#1115).
    bool got;
    uint32_t got_symbol;
    // The .bss allocations of .lcomm and local .comm symbols, in directive order (AsmSymbol.common_next), and the
    // number of common symbols.
    uint32_t common_first;
    uint32_t common_last;
    uint32_t common_count;
    // The source's symbol aliases (AsmSymbol.alias).
    uint32_t alias_count;
    // The symbols whose sizes reach past their sections' ends (AsmSymbol.beyond).
    uint32_t beyond_count;
    // GNU as's line table (#1115, assembler_lines.c): the rows, the file and directory tables (a directory of no
    // pointer is unset), the latest `.loc`'s state, and whether GNU as's DWARF level is 5 (after `.file 0` or `.loc 0`),
    // not its default 3.
    AsmLineRow* line_rows;
    AsmLineFile* line_files;
    String8* line_directories;
    AsmLineState line;
    uint32_t line_row_count;
    uint32_t line_file_count;
    uint32_t line_directory_count;
    bool line_dwarf5;
    // The table's .debug_line section and the fragment its bytes fill after layout (asm_line_emit), or 0 for none;
    // each directory's and file's string label in .debug_line_str (DWARF 5), and the table's relocations.
    uint32_t line_section;
    uint32_t line_fragment;
    uint32_t* line_strings;
    AsmLineFixup* line_fixups;
    uint32_t line_fixup_count;
};

static uint64_t asm_clock(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000000000 + time.tv_nsec;
}

static void asm_fail(AsmParser* parser, uint32_t location, const char* message)
{
    if (parser->result.error == 0)
    {
        parser->result.error = message;
        parser->result.location = location;
    }
}

// asm_fail, naming the instruction (AsmResult.error_name).
static void asm_fail_named(AsmParser* parser, uint32_t location, const char* message, String8 name)
{
    if (parser->result.error == 0)
    {
        parser->result.error_name = name;
    }
    asm_fail(parser, location, message);
}

static bool asm_word_byte(uint8_t byte)
{
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '_' || byte == '.';
}

static uint32_t asm_word_length(const uint8_t* source, uint32_t length, bool vectorized)
{
    (void)vectorized;

    uint32_t count = 0;
    if (BUSTER_VECTOR_PATH(vectorized))
    {
        while (count < length)
        {
            const uint32_t left = length - count;
            const uint32_t n = left < 64 ? left : 64;
            const uint64_t valid = n == 64 ? UINT64_MAX : (1ull << n) - 1;
            const __m512i bytes = _mm512_maskz_loadu_epi8(valid, source + count);
            const __m512i lower = _mm512_or_si512(bytes, _mm512_set1_epi8(32));
            uint64_t word = _mm512_cmple_epu8_mask(_mm512_sub_epi8(lower, _mm512_set1_epi8('a')), _mm512_set1_epi8(25));
            word |= _mm512_cmple_epu8_mask(_mm512_sub_epi8(bytes, _mm512_set1_epi8('0')), _mm512_set1_epi8(9));
            word |= _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('_')) | _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('.'));
            const uint64_t stop = valid & ~word;
            count += stop != 0 ? __builtin_ctzll(stop) : n;
            if (stop != 0)
            {
                break;
            }
        }
    }
    else
    {
        while (count < length && asm_word_byte(source[count]))
        {
            count += 1;
        }
    }
    return count;
}

static void asm_lex_scalar(AsmParser* parser)
{
    const uint8_t* bytes = (const uint8_t*)parser->source.pointer;
    const uint32_t length = parser->source.length;
    uint32_t at = 0;
    while (at < length && parser->result.error == 0)
    {
        const uint32_t start = at;
        const uint8_t byte = bytes[at++];
        uint16_t kind = byte;
        if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\v' || byte == '\f')
        {
            continue;
        }
        if (byte == '#' || (byte == '/' && at < length && bytes[at] == '/'))
        {
            while (at < length && bytes[at] != '\n')
            {
                at += 1;
            }
            continue;
        }
        if (byte == '/' && at < length && bytes[at] == '*')
        {
            at += 1;
            while (at + 1 < length && !(bytes[at] == '*' && bytes[at + 1] == '/'))
            {
                // A block comment is whitespace; newlines still terminate assembly statements.
                if (bytes[at] == '\n')
                {
                    parser->tokens[parser->token_count++] = (AsmToken){.start = at, .end = at + 1, .kind = '\n'};
                }
                at += 1;
            }
            if (at + 1 >= length)
            {
                asm_fail(parser, start, "unterminated block comment");
            }
            at += 2;
            continue;
        }
        if (asm_word_byte(byte))
        {
            at = start + asm_word_length(bytes + start, length - start, BUSTER_VECTOR_PATH(parser->options.vectorized));
            kind = BUSTER_ASM_WORD;
        }
        else if (byte == '"')
        {
            bool closed = false;
            while (at < length && !closed && bytes[at] != '\n')
            {
                if (bytes[at] == '\\' && at + 1 < length)
                {
                    at += 2;
                }
                else
                {
                    closed = bytes[at++] == '"';
                }
            }
            if (!closed)
            {
                asm_fail(parser, start, "unterminated string");
            }
            kind = BUSTER_ASM_STRING;
        }
        else if (byte == ';')
        {
            kind = '\n';
        }
        else if (byte == 0 || byte >= 128)
        {
            asm_fail(parser, start, "unsupported source byte");
        }
        parser->tokens[parser->token_count++] = (AsmToken){.start = start, .end = at, .kind = kind};
    }
    parser->tokens[parser->token_count] = (AsmToken){.start = length, .end = length, .kind = 0};
}

// Transpose compacted columns into the parser's fixed 16-byte records, four records per store. The lexer computes
// token boundaries over 64 characters, never walks each ordinary token or character. The parse view remains records
// because expression/diagnostic recovery needs all three columns of one token together.
static void asm_lex_store(AsmToken* output, const uint32_t* starts, const uint32_t* ends, const uint32_t* kinds, uint32_t count, uint32_t base)
{
    const __m512i offsets = _mm512_set1_epi32(base);
    for (uint32_t at = 0; at < count; at += 16)
    {
        const uint32_t left = count - at;
        const uint32_t n = left < 16 ? left : 16;
        const __mmask16 valid = (1u << n) - 1;
        const __m512i begin = _mm512_add_epi32(_mm512_maskz_loadu_epi32(valid, starts + at), offsets);
        const __m512i end = _mm512_add_epi32(_mm512_maskz_loadu_epi32(valid, ends + at), offsets);
        const __m512i kind = _mm512_maskz_loadu_epi32(valid, kinds + at);
        for (uint32_t group = 0; group < n; group += 4)
        {
            const __m512i places = _mm512_add_epi32(_mm512_setr_epi32(0, 16, 0, 0, 1, 17, 0, 0, 2, 18, 0, 0, 3, 19, 0, 0), _mm512_set1_epi32(group));
            const __m512i kind_places = _mm512_add_epi32(_mm512_setr_epi32(0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3), _mm512_set1_epi32(group));
            __m512i records = _mm512_maskz_permutex2var_epi32(0x3333, begin, places, end);
            records = _mm512_mask_mov_epi32(records, 0x4444, _mm512_permutexvar_epi32(kind_places, kind));
            const uint32_t records_left = n - group;
            const __mmask16 store = records_left >= 4 ? 0xffff : (1u << (records_left * 4)) - 1;
            _mm512_mask_storeu_epi32(output + at + group, store, records);
        }
    }
}

static void asm_lex_vector(AsmParser* parser)
{
#if !BUSTER_HOST_VECTOR
    asm_lex_scalar(parser);
#else
    const uint8_t* source = (const uint8_t*)parser->source.pointer;
    const uint32_t length = parser->source.length;
    const __m512i iota = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    uint32_t at = 0;
    while (at < length && parser->result.error == 0)
    {
        const uint32_t left = length - at;
        uint32_t n = left < 64 ? left : 64;
        uint64_t valid = n == 64 ? UINT64_MAX : (1ull << n) - 1;
        const __m512i bytes = _mm512_maskz_loadu_epi8(valid, source + at);
        const __m512i lower = _mm512_or_si512(bytes, _mm512_set1_epi8(32));
        uint64_t word = _mm512_cmple_epu8_mask(_mm512_sub_epi8(lower, _mm512_set1_epi8('a')), _mm512_set1_epi8(25));
        word |= _mm512_cmple_epu8_mask(_mm512_sub_epi8(bytes, _mm512_set1_epi8('0')), _mm512_set1_epi8(9));
        word |= _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('_')) | _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('.'));
        uint64_t special = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('"')) | _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('#'));
        special |= _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('/')) | _mm512_cmpeq_epi8_mask(bytes, _mm512_setzero_si512());
        special |= _mm512_movepi8_mask(bytes);
        special &= valid;
        if (special != 0)
        {
            n = __builtin_ctzll(special);
            valid = n == 0 ? 0 : (1ull << n) - 1;
        }
        // Defer a word crossing the load boundary; the next load starts at that word. A word longer than a vector
        // is one rare token, scanned with full-width comparisons by asm_word_length.
        if (n == 64 && (word >> 63) != 0 && at + n < length && asm_word_byte(source[at + n]))
        {
            const uint64_t nonword = ~word;
            n = nonword != 0 ? 64 - __builtin_clzll(nonword) : 0;
            valid = n == 0 ? 0 : (1ull << n) - 1;
        }
        if (n != 0)
        {
            word &= valid;
            const uint64_t space = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8(' ')) |
                                   _mm512_cmple_epu8_mask(_mm512_sub_epi8(bytes, _mm512_set1_epi8('\t')), _mm512_set1_epi8(4));
            const uint64_t newline = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\n')) | _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8(';'));
            const uint64_t punctuation = valid & ~word & (~space | newline);
            const uint64_t starts_mask = (word & ~(word << 1)) | punctuation;
            const uint64_t ends_mask = (word & ~(word >> 1)) | punctuation;
            uint32_t starts[64];
            uint32_t ends[64];
            uint32_t kinds[64];
            uint32_t start_count = 0;
            uint32_t end_count = 0;
            uint8_t byte_values[64] __attribute__((aligned(64)));
            _mm512_store_si512(byte_values, bytes);
            for (uint32_t group = 0; group < 4; group += 1)
            {
                const __mmask16 start = starts_mask >> (16 * group);
                const __mmask16 end = ends_mask >> (16 * group);
                const __m512i positions = _mm512_add_epi32(iota, _mm512_set1_epi32(16 * group));
                __m512i kind = _mm512_cvtepu8_epi32(_mm_load_si128((const __m128i*)(byte_values + group * 16)));
                kind = _mm512_mask_mov_epi32(kind, word >> (16 * group), _mm512_set1_epi32(BUSTER_ASM_WORD));
                kind = _mm512_mask_mov_epi32(kind, newline >> (16 * group), _mm512_set1_epi32('\n'));
                const __m512i compact_starts = _mm512_maskz_compress_epi32(start, positions);
                const __m512i compact_ends = _mm512_maskz_compress_epi32(end, _mm512_add_epi32(positions, _mm512_set1_epi32(1)));
                const __m512i compact_kinds = _mm512_maskz_compress_epi32(start, kind);
                const uint32_t a = __builtin_popcount(start);
                const uint32_t b = __builtin_popcount(end);
                _mm512_mask_storeu_epi32(starts + start_count, (1u << a) - 1, compact_starts);
                _mm512_mask_storeu_epi32(kinds + start_count, (1u << a) - 1, compact_kinds);
                _mm512_mask_storeu_epi32(ends + end_count, (1u << b) - 1, compact_ends);
                start_count += a;
                end_count += b;
            }
            asm_lex_store(parser->tokens + parser->token_count, starts, ends, kinds, start_count, at);
            parser->token_count += start_count;
            at += n;
        }
        else if (asm_word_byte(source[at]))
        {
            const uint32_t end = at + asm_word_length(source + at, length - at, true);
            parser->tokens[parser->token_count++] = (AsmToken){.start = at, .end = end, .kind = BUSTER_ASM_WORD};
            at = end;
        }
        else
        {
            uint32_t end = at + 1;
            if (source[at] == '"')
            {
                while (end < length && source[end] != '"' && source[end] != '\n')
                {
                    end += source[end] == '\\' && end + 1 < length ? 2 : 1;
                }
                end += end < length && source[end] == '"';
            }
            else if (source[at] == '#' || (source[at] == '/' && end < length && source[end] == '/'))
            {
                while (end < length && source[end] != '\n')
                {
                    end += 1;
                }
            }
            else if (source[at] == '/' && end < length && source[end] == '*')
            {
                end += 1;
                while (end + 1 < length && !(source[end] == '*' && source[end + 1] == '/'))
                {
                    end += 1;
                }
                end = end + 1 < length ? end + 2 : length;
            }
            AsmParser rare = *parser;
            rare.source = (String8){.pointer = parser->source.pointer + at, .length = end - at};
            rare.tokens += parser->token_count;
            rare.token_count = 0;
            rare.options.vectorized = false;
            asm_lex_scalar(&rare);
            for (uint32_t i = 0; i < rare.token_count; i += 1)
            {
                rare.tokens[i].start += at;
                rare.tokens[i].end += at;
            }
            if (rare.result.error != 0)
            {
                asm_fail(parser, at + rare.result.location, rare.result.error);
            }
            parser->token_count += rare.token_count;
            at = end;
        }
    }
    parser->tokens[parser->token_count] = (AsmToken){.start = length, .end = length, .kind = 0};
#endif
}

static void asm_lex(AsmParser* parser)
{
    parser->instruction_capacity = 1;
#if !BUSTER_HOST_VECTOR
    asm_lex_scalar(parser);
    for (uint32_t at = 0; at < parser->token_count; at += 1)
    {
        parser->instruction_capacity += parser->tokens[at].kind == '\n';
    }
#else
    if (BUSTER_VECTOR_PATH(parser->options.vectorized))
    {
        asm_lex_vector(parser);
        // At most one instruction per statement. Count separators in the kind column without gathers, and reserve
        // instruction records for statements instead of all operand tokens. Only the third word of each token counts.
        for (uint32_t at = 0; at < parser->token_count; at += 4)
        {
            const uint32_t left = parser->token_count - at;
            const uint32_t count = left < 4 ? left : 4;
            const __mmask16 valid = (1u << (count * 4)) - 1;
            const __m512i records = _mm512_maskz_loadu_epi32(valid, parser->tokens + at);
            parser->instruction_capacity += __builtin_popcount(_mm512_mask_cmpeq_epi32_mask(valid & 0x4444, records, _mm512_set1_epi32('\n')));
        }
    }
    else
    {
        asm_lex_scalar(parser);
        for (uint32_t at = 0; at < parser->token_count; at += 1)
        {
            parser->instruction_capacity += parser->tokens[at].kind == '\n';
        }
    }
#endif
}

static String8 asm_token_text(const AsmParser* parser, uint32_t token)
{
    const AsmToken* value = &parser->tokens[token];
    return (String8){.pointer = parser->source.pointer + value->start, .length = value->end - value->start};
}

static bool asm_is(const AsmParser* parser, uint32_t token, const char* name)
{
    return x86_match_name(asm_token_text(parser, token), name);
}

static bool asm_is_case(const AsmParser* parser, uint32_t token, const char* name)
{
    const String8 text = asm_token_text(parser, token);
    bool equal = text.length == strlen(name);
    for (uint32_t i = 0; equal && i < text.length; i += 1)
    {
        const uint8_t byte = text.pointer[i];
        const uint8_t lower = byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte;
        equal = lower == (uint8_t)name[i];
    }
    return equal;
}

static bool asm_name_token(const AsmParser* parser, uint32_t token)
{
    return parser->tokens[token].kind == BUSTER_ASM_WORD || parser->tokens[token].kind == BUSTER_ASM_STRING;
}

static String8 asm_name_text(AsmParser* parser, uint32_t token)
{
    String8 name = asm_token_text(parser, token);
    if (parser->tokens[token].kind == BUSTER_ASM_STRING)
    {
        name.pointer += 1;
        name.length -= 2;
        // GCC's ordinary quoted identifiers need no copy. Decode escapes only when the spelling contains one.
        if (memchr(name.pointer, '\\', name.length) != 0)
        {
            char* bytes = arena_push(parser->arena, name.length, 1);
            if (bytes == 0)
            {
                asm_fail(parser, parser->tokens[token].start, "arena exhausted decoding a symbol");
            }
            else
            {
                uint32_t count = 0;
                for (uint32_t i = 0; i < name.length; i += 1)
                {
                    if (name.pointer[i] == '\\' && i + 1 < name.length)
                    {
                        i += 1;
                        if (name.pointer[i] != '\\' && name.pointer[i] != '"')
                        {
                            asm_fail(parser, parser->tokens[token].start, "quoted names support only quote and backslash escapes");
                        }
                    }
                    bytes[count++] = name.pointer[i];
                }
                name = (String8){.pointer = bytes, .length = count};
            }
        }
        if (memchr(name.pointer, 0, name.length) != 0)
        {
            asm_fail(parser, parser->tokens[token].start, "a name cannot contain NUL");
        }
    }
    return name;
}

static bool asm_take(AsmParser* parser, uint16_t kind)
{
    const bool result = parser->tokens[parser->at].kind == kind;
    parser->at += result;
    return result;
}

static void asm_expect(AsmParser* parser, uint16_t kind, const char* message)
{
    if (!asm_take(parser, kind))
    {
        asm_fail(parser, parser->tokens[parser->at].start, message);
    }
}

static uint32_t asm_fragment(AsmParser* parser, uint8_t kind, uint32_t location)
{
    uint32_t result = 0;
    if (parser->section == BUSTER_LINK_CLASS_NONE)
    {
        asm_fail(parser, location, ".note.GNU-stack must be empty");
    }
    else if (parser->fragment_count >= parser->capacity)
    {
        asm_fail(parser, location, "fragment capacity exhausted");
    }
    else
    {
        result = parser->fragment_count++;
        parser->fragments[result] = (AsmFragment){.kind = kind, .section = parser->section, .location = location};
    }
    return result;
}

static uint32_t asm_find_section(const AsmParser* parser, String8 name, uint32_t group);
static uint32_t asm_find_latest_section(const AsmParser* parser, String8 name);
static void asm_line_instruction(AsmParser* parser, uint32_t location);

static uint32_t asm_symbol_scoped(AsmParser* parser, String8 name, uint32_t location, bool internal)
{
    if (name.length == 0 || (name.pointer[0] >= '0' && name.pointer[0] <= '9') || (name.length == 1 && name.pointer[0] == '.'))
    {
        asm_fail(parser, location, "invalid symbol name");
    }
    uint32_t slot = (link_hash((const uint8_t*)name.pointer, name.length) ^ (internal ? 0x9e3779b9u : 0)) & parser->symbol_mask;
    uint32_t result = parser->symbol_slots[slot];
    while (result != 0 && (parser->symbols[result].internal != internal || parser->symbols[result].name.length != name.length || memcmp(parser->symbols[result].name.pointer, name.pointer, name.length) != 0))
    {
        slot = (slot + 1) & parser->symbol_mask;
        result = parser->symbol_slots[slot];
    }
    if (result == 0)
    {
        if (parser->symbol_count >= parser->capacity)
        {
            asm_fail(parser, location, "symbol capacity exhausted");
        }
        else
        {
            result = parser->symbol_count++;
            parser->symbols[result] = (AsmSymbol){.name = name, .location = location, .internal = internal};
            if (!internal && name.length != 0 && name.pointer[0] == '.')
            {
                const uint32_t section = asm_find_section(parser, name, 0);
                parser->symbols[result].section_start = section != 0;
                parser->symbols[result].section_index = section;
            }
            parser->symbol_slots[slot] = result;
        }
    }
    return result;
}

// Bind earlier unresolved section-name expressions when the first section of that name is created.
static void asm_bind_forward_section(AsmParser* parser, String8 name, uint32_t section)
{
    uint32_t slot = link_hash((const uint8_t*)name.pointer, name.length) & parser->symbol_mask;
    uint32_t id = parser->symbol_slots[slot];
    while (id != 0 && (parser->symbols[id].internal || parser->symbols[id].name.length != name.length ||
                       memcmp(parser->symbols[id].name.pointer, name.pointer, name.length) != 0))
    {
        slot = (slot + 1) & parser->symbol_mask;
        id = parser->symbol_slots[slot];
    }
    if (id != 0)
    {
        AsmSymbol* symbol = &parser->symbols[id];
        if ((symbol->defined && !symbol->absolute && !symbol->alias) || symbol->common != 0)
        {
            asm_fail(parser, symbol->location, "section name already defined as a symbol");
        }
        else if (symbol->section_attributes)
        {
            asm_fail(parser, symbol->location, "section symbol attributes are not supported");
        }
        else if (symbol->referenced && !symbol->defined && !symbol->alias && !symbol->section_start)
        {
            symbol->section_start = true;
            symbol->section_index = section;
            symbol->flags = BUSTER_LINK_SYMBOL_LOCAL;
        }
    }
}

static uint32_t asm_symbol(AsmParser* parser, String8 name, uint32_t location)
{
    const uint32_t result = asm_symbol_scoped(parser, name, location, false);
    parser->symbols[result].ordinary = true;
    return result;
}

// Section symbols have ELF attributes of their own, which the shared writer cannot currently preserve. Remember
// directives before a section's declaration, too, so a later declaration cannot silently change their meaning.
static void asm_section_attribute(AsmParser* parser, AsmSymbol* symbol, String8 name, uint32_t location)
{
    symbol->section_attributes = true;
    const uint32_t section = parser->code ? 0 : parser->section_name_slots != 0 ? asm_find_latest_section(parser, name) : asm_find_section(parser, name, 0);
    if (section != 0)
    {
        asm_fail(parser, location, "section symbol attributes are not supported");
    }
}

static uint32_t asm_numeric(AsmParser* parser, uint32_t digit, bool forward, bool definition, uint32_t location)
{
    if (definition)
    {
        parser->numeric[digit] += 1;
    }
    const uint32_t serial = parser->numeric[digit] + forward;
    char name[48];
    // The internal namespace is distinct even from a quoted source name with exactly this spelling.
    const int length = snprintf(name, sizeof(name), ":numeric.%u.%u", digit, serial);
    char* copy = arena_push(parser->arena, length, 1);
    uint32_t result = 0;
    if (copy == 0 || serial == 0)
    {
        asm_fail(parser, location, copy == 0 ? "arena exhausted" : "numeric backward label has no definition");
    }
    else
    {
        memcpy(copy, name, length);
        result = asm_symbol_scoped(parser, (String8){.pointer = copy, .length = length}, location, true);
    }
    return result;
}

static uint32_t asm_dot(AsmParser* parser, uint32_t location)
{
    uint32_t result = 0;
    if (parser->symbol_count >= parser->capacity)
    {
        asm_fail(parser, location, "symbol capacity exhausted");
    }
    else
    {
        result = parser->symbol_count++;
        parser->symbols[result] = (AsmSymbol){.fragment = asm_fragment(parser, ASM_LABEL, location), .location = location, .defined = true, .internal = true};
    }
    return result;
}

static bool asm_number(String8 text, __int128* value)
{
    uint32_t base = 10;
    uint32_t at = 0;
    if (text.length > 1 && text.pointer[0] == '0')
    {
        base = 8;
        if (text.pointer[1] == 'x' || text.pointer[1] == 'X' || text.pointer[1] == 'b' || text.pointer[1] == 'B')
        {
            base = text.pointer[1] == 'b' || text.pointer[1] == 'B' ? 2 : 16;
            at = 2;
        }
    }
    bool valid = at < text.length;
    __int128 result = 0;
    for (; valid && at < text.length; at += 1)
    {
        const uint8_t byte = text.pointer[at];
        const uint32_t digit = byte >= '0' && byte <= '9' ? byte - '0' : (byte | 32) >= 'a' && (byte | 32) <= 'f' ? (byte | 32) - 'a' + 10
                                                                                                                  : 255;
        valid = digit < base && result <= (((__int128)UINT64_MAX - digit) / base);
        if (valid)
        {
            result = result * base + digit;
        }
    }
    *value = result;
    return valid;
}

static bool asm_expr_range(__int128 value)
{
    return value >= INT64_MIN && value <= (__int128)UINT64_MAX;
}

static AsmExpr asm_combine(AsmParser* parser, AsmExpr left, AsmExpr right, uint16_t op, uint32_t location)
{
    AsmExpr result = {0};
    if ((left.relocation != ASM_RELOCATION_NONE && right.relocation != ASM_RELOCATION_NONE) || (op == '-' && right.relocation != ASM_RELOCATION_NONE))
    {
        asm_fail(parser, location, "invalid relocation modifier combination");
    }
    result.relocation = left.relocation | right.relocation;
    const bool symbolic = left.plus || left.minus || right.plus || right.minus;
    if (op == '+' || op == '-')
    {
        if ((op == '-' && right.register_count != 0) || left.register_count + right.register_count > 2)
        {
            asm_fail(parser, location, "invalid address register expression");
        }
        else
        {
            for (uint32_t i = 0; i < left.register_count; i += 1)
            {
                result.registers[result.register_count] = left.registers[i];
                result.indexed |= ((left.indexed >> i) & 1u) << result.register_count;
                result.scales[result.register_count++] = left.scales[i];
            }
            for (uint32_t i = 0; i < right.register_count; i += 1)
            {
                result.registers[result.register_count] = right.registers[i];
                result.indexed |= ((right.indexed >> i) & 1u) << result.register_count;
                result.scales[result.register_count++] = right.scales[i];
            }
        }
        result.value = left.value + (op == '+' ? right.value : -right.value);
        const uint32_t plus[2] = {left.plus, op == '+' ? right.plus : right.minus};
        const uint32_t minus[2] = {left.minus, op == '+' ? right.minus : right.plus};
        uint32_t p[2] = {plus[0], plus[1]};
        uint32_t m[2] = {minus[0], minus[1]};
        for (uint32_t i = 0; i < 2; i += 1)
        {
            for (uint32_t j = 0; j < 2; j += 1)
            {
                if (p[i] != 0 && p[i] == m[j])
                {
                    p[i] = 0;
                    m[j] = 0;
                }
            }
        }
        if ((p[0] && p[1]) || (m[0] && m[1]))
        {
            asm_fail(parser, location, "expression needs more than a symbol difference");
        }
        result.plus = p[0] ? p[0] : p[1];
        result.minus = m[0] ? m[0] : m[1];
        if (result.relocation != ASM_RELOCATION_NONE && result.plus == 0 && result.minus == 0)
        {
            asm_fail(parser, location, "relocation modifier cancels its symbol");
        }
    }
    else if (symbolic)
    {
        asm_fail(parser, location, "operator requires absolute operands");
    }
    else if (left.register_count != 0 || right.register_count != 0)
    {
        const AsmExpr registers = left.register_count != 0 ? left : right;
        const AsmExpr factor = left.register_count != 0 ? right : left;
        if (op != '*' || factor.register_count != 0 || factor.value < 1 || factor.value > 8)
        {
            asm_fail(parser, location, "invalid address register expression");
        }
        else
        {
            result = registers;
            result.value *= factor.value;
            result.indexed = (1u << result.register_count) - 1;
            for (uint32_t i = 0; i < result.register_count; i += 1)
            {
                const uint32_t scale = result.scales[i] * (uint32_t)factor.value;
                if (scale != 1 && scale != 2 && scale != 4 && scale != 8)
                {
                    asm_fail(parser, location, "address scale must be 1, 2, 4 or 8");
                }
                result.scales[i] = scale;
            }
        }
    }
    else
    {
        switch (op)
        {
        case '*':
        {
            if (__builtin_mul_overflow(left.value, right.value, &result.value))
            {
                asm_fail(parser, location, "expression overflow");
            }
        }
        break;
        case '/':
        case '%':
        {
            if (right.value == 0)
            {
                asm_fail(parser, location, "division by zero");
            }
            else
            {
                result.value = op == '/' ? left.value / right.value : left.value % right.value;
            }
        }
        break;
        case '&':
            result.value = (int64_t)((uint64_t)left.value & (uint64_t)right.value);
            break;
        case '|':
            result.value = (int64_t)((uint64_t)left.value | (uint64_t)right.value);
            break;
        case '^':
            result.value = (int64_t)((uint64_t)left.value ^ (uint64_t)right.value);
            break;
        case '!':
            result.value = (int64_t)((uint64_t)left.value | ~(uint64_t)right.value);
            break;
        case ASM_EXPR_SHL:
            result.value = right.value >= 0 && right.value < 64 ? (int64_t)((uint64_t)left.value << (uint32_t)right.value) : 0;
            break;
        case ASM_EXPR_SHR:
            result.value = right.value >= 0 && right.value < 64 ? (int64_t)((uint64_t)left.value >> (uint32_t)right.value) : 0;
            break;
        case ASM_EXPR_EQ:
            result.value = -((uint64_t)left.value == (uint64_t)right.value);
            break;
        case ASM_EXPR_NE:
            result.value = -((uint64_t)left.value != (uint64_t)right.value);
            break;
        case '<':
            result.value = -((int64_t)left.value < (int64_t)right.value);
            break;
        case '>':
            result.value = -((int64_t)left.value > (int64_t)right.value);
            break;
        case ASM_EXPR_LE:
            result.value = -((int64_t)left.value <= (int64_t)right.value);
            break;
        case ASM_EXPR_GE:
            result.value = -((int64_t)left.value >= (int64_t)right.value);
            break;
        case ASM_EXPR_AND:
            result.value = left.value != 0 && right.value != 0;
            break;
        case ASM_EXPR_OR:
            result.value = left.value != 0 || right.value != 0;
            break;
        default:
            asm_fail(parser, location, "unsupported expression operator");
            break;
        }
    }
    if (!asm_expr_range(result.value))
    {
        asm_fail(parser, location, "expression overflow");
    }
    return result;
}

static uint32_t asm_precedence(uint16_t op)
{
    // GAS shifts share multiplication's precedence; its bitwise operators bind more tightly than addition.
    return op == '*' || op == '/' || op == '%' || op == ASM_EXPR_SHL || op == ASM_EXPR_SHR ? 6 :
        op == '&' || op == '|' || op == '^' || op == '!' ? 5 :
        op == '+' || op == '-' ? 4 :
        op == '<' || op == '>' || op == ASM_EXPR_EQ || op == ASM_EXPR_NE || op == ASM_EXPR_LE || op == ASM_EXPR_GE ? 3 :
        op == ASM_EXPR_AND ? 2 : op == ASM_EXPR_OR ? 1 : 0;
}

static uint16_t asm_expression_operator(const AsmParser* parser, uint32_t end, uint32_t* width)
{
    const AsmToken* token = &parser->tokens[parser->at];
    uint16_t op = token->kind;
    *width = 1;
    if (parser->at + 1 < end && token->end == token[1].start)
    {
        const uint32_t pair = op | token[1].kind << 8;
        op = pair == ('<' | '<' << 8) ? ASM_EXPR_SHL : pair == ('>' | '>' << 8) ? ASM_EXPR_SHR :
            pair == ('=' | '=' << 8) ? ASM_EXPR_EQ : pair == ('!' | '=' << 8) || pair == ('<' | '>' << 8) ? ASM_EXPR_NE :
            pair == ('<' | '=' << 8) ? ASM_EXPR_LE : pair == ('>' | '=' << 8) ? ASM_EXPR_GE :
            pair == ('&' | '&' << 8) ? ASM_EXPR_AND : pair == ('|' | '|' << 8) ? ASM_EXPR_OR : op;
        *width = op >= ASM_EXPR_SHL && op <= ASM_EXPR_OR ? 2 : 1;
    }
    return op;
}

static AsmExpr asm_unary(AsmParser* parser, AsmExpr value, uint16_t op, uint32_t location)
{
    if (op == '-' && value.relocation != ASM_RELOCATION_NONE)
    {
        asm_fail(parser, location, "invalid relocation modifier with unary minus");
    }
    else if (op == '-')
    {
        value = asm_combine(parser, (AsmExpr){0}, value, '-', location);
    }
    else if (op == '~' || op == '!')
    {
        if (value.plus || value.minus || value.register_count)
        {
            asm_fail(parser, location, "not requires an absolute operand");
        }
        value.value = op == '~' ? (int64_t)~(uint64_t)value.value : value.value == 0;
    }
    return value;
}

static bool asm_token_register(const AsmParser* parser, uint32_t at, X86Operand* operand);

// GNU's relocation modifier after a symbol. Only the modifiers compiler-generated code writes are admitted, in either
// case as register names and mnemonics are: clang writes @TPOFF and GCC @tpoff.
static uint16_t asm_relocation(const AsmParser* parser, uint32_t token)
{
    static const char* const names[] = {"plt", "gotpcrel", "gotpcrelx", "tpoff", "gottpoff", "tlsgd", "tlsld", "dtpoff"};
    uint16_t result = ASM_RELOCATION_NONE;
    for (uint32_t i = 0; result == ASM_RELOCATION_NONE && i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        result = asm_is_case(parser, token, names[i]) ? (uint16_t)(ASM_RELOCATION_PLT + i) : ASM_RELOCATION_NONE;
    }
    return result;
}

static AsmExpr asm_expression_mode(AsmParser* parser, uint32_t end, bool address)
{
    AsmExpr values[BUSTER_ASM_EXPR_DEPTH];
    uint16_t operators[BUSTER_ASM_EXPR_DEPTH];
    uint32_t value_count = 0;
    uint32_t operator_count = 0;
    bool need_value = true;
    const uint32_t location = parser->tokens[parser->at].start;
    while (parser->at < end && parser->result.error == 0)
    {
        const AsmToken token = parser->tokens[parser->at];
        if (value_count + 1 >= BUSTER_ASM_EXPR_DEPTH || operator_count + 1 >= BUSTER_ASM_EXPR_DEPTH)
        {
            asm_fail(parser, token.start, "expression nesting limit exceeded");
            break;
        }
        if (need_value)
        {
            if (token.kind == '+' || token.kind == '-' || token.kind == '~' || token.kind == '!')
            {
                operators[operator_count++] = token.kind | 0x8000;
                parser->at += 1;
            }
            else if (token.kind == '(')
            {
                operators[operator_count++] = '(';
                parser->at += 1;
            }
            else if (asm_name_token(parser, parser->at) || (address && parser->prefix && token.kind == '%' && parser->at + 1 < end && parser->tokens[parser->at + 1].kind == BUSTER_ASM_WORD))
            {
                const bool prefixed = token.kind == '%';
                parser->at += prefixed;
                const uint32_t name_at = parser->at++;
                const String8 text = asm_name_text(parser, name_at);
                AsmExpr value = {0};
                if (token.kind == BUSTER_ASM_WORD && text.length == 2 && text.pointer[0] >= '0' && text.pointer[0] <= '9' && (text.pointer[1] == 'b' || text.pointer[1] == 'f'))
                {
                    value.plus = asm_numeric(parser, text.pointer[0] - '0', text.pointer[1] == 'f', false, token.start);
                }
                else if (token.kind == BUSTER_ASM_WORD && text.pointer[0] >= '0' && text.pointer[0] <= '9')
                {
                    if (!asm_number(text, &value.value))
                    {
                        asm_fail(parser, token.start, "invalid or overflowing integer");
                    }
                }
                else
                {
                    // Without register prefixes, a register's name is never a symbol: GNU as refuses it in an expression
                    // (`.quad rsi`, `dword ptr eax`, `[rax + -rsi]`) rather than reading it as an external. Register
                    // names are at most five bytes, which keeps the check off the longer symbol names.
                    X86Operand reg;
                    const bool is_register = parser->options.intel && prefixed == parser->prefix && token.kind != BUSTER_ASM_STRING && text.length <= 5 && asm_token_register(parser, name_at, &reg);
                    if (is_register)
                    {
                        // A gather's or scatter's vector index too (#1115, `[rdx+4*ymm7]`).
                        const bool vector = reg.class >= BUSTER_X86_CLASS_XMM && reg.class <= BUSTER_X86_CLASS_ZMM;
                        if (!address || (reg.class != BUSTER_X86_CLASS_GPR64 && reg.class != BUSTER_X86_CLASS_GPR32 && reg.class != 255 && !vector))
                        {
                            asm_fail(parser, token.start, "invalid use of register");
                        }
                        const uint8_t narrow = reg.class == BUSTER_X86_CLASS_GPR32 || (reg.class == 255 && reg.width == 4) ? 128 : 0;
                        value.register_count = 1;
                        value.registers[0] = (uint8_t)(narrow + (reg.class == 255 ? 17 : vector ? (uint8_t)(32 + 32 * (reg.class - BUSTER_X86_CLASS_XMM) + reg.reg) : reg.reg + 1));
                        value.scales[0] = 1;
                    }
                    else
                    {
                        if (prefixed)
                        {
                            asm_fail(parser, token.start, "expected address register");
                        }
                        const uint32_t section = parser->code ? 0 : parser->section_name_slots != 0 ? asm_find_latest_section(parser, text) : asm_find_section(parser, text, 0);
                        uint32_t symbol = 0;
                        if (section != 0)
                        {
                            // GNU as versions disagree on which same-name section a bare section name denotes.
                            // Do not silently pick a different relocation target than the assembler in use.
                            if (parser->sections[section].name_ambiguous)
                            {
                                asm_fail(parser, token.start, "ambiguous section name in expression");
                            }
                            symbol = parser->sections[section].name_symbol;
                            if (symbol == 0 && parser->symbol_count >= parser->capacity)
                            {
                                asm_fail(parser, token.start, "symbol capacity exhausted");
                            }
                            else if (symbol == 0)
                            {
                                symbol = parser->symbol_count++;
                                parser->sections[section].name_symbol = symbol;
                                parser->symbols[symbol] = (AsmSymbol){.name = text, .location = token.start, .section_index = section,
                                                                      .section_start = true, .internal = true};
                            }
                        }
                        else
                        {
                            symbol = text.length == 1 && text.pointer[0] == '.' ? asm_dot(parser, token.start) : asm_symbol(parser, text, token.start);
                        }
                        parser->symbols[symbol].referenced = true;
                        if (parser->at < end && parser->tokens[parser->at].kind == '@')
                        {
                            if (parser->at + 1 >= end || parser->tokens[parser->at + 1].kind != BUSTER_ASM_WORD)
                            {
                                asm_fail(parser, parser->tokens[parser->at].start, "expected a relocation modifier name");
                            }
                            else
                            {
                                value.relocation = asm_relocation(parser, parser->at + 1);
                                if (value.relocation == ASM_RELOCATION_NONE)
                                {
                                    asm_fail(parser, parser->tokens[parser->at + 1].start, "unsupported relocation modifier");
                                }
                                // A TLS model's modifier makes its symbol a TLS variable, an undefined one too, and every
                                // modifier but @PLT has GNU as declare _GLOBAL_OFFSET_TABLE_, an undefined global no
                                // relocation refers to.
                                parser->symbols[symbol].tls |= value.relocation >= ASM_RELOCATION_TPOFF;
                                if (value.relocation > ASM_RELOCATION_PLT && !parser->got && !parser->code)
                                {
                                    parser->got = true;
                                    parser->got_symbol = asm_symbol(parser, BUSTER_S8("_GLOBAL_OFFSET_TABLE_"), token.start);
                                }
                                parser->at += 2;
                            }
                        }
                        if (parser->symbols[symbol].absolute)
                        {
                            if (value.relocation != ASM_RELOCATION_NONE)
                            {
                                asm_fail(parser, token.start, "relocation modifier requires a relocatable symbol");
                            }
                            value.value = parser->symbols[symbol].constant;
                        }
                        else
                        {
                            value.plus = symbol;
                        }
                    }
                }
                values[value_count++] = value;
                need_value = false;
            }
            else
            {
                asm_fail(parser, token.start, "expected expression operand");
            }
        }
        else
        {
            const bool close = token.kind == ')';
            uint32_t operator_width;
            const uint16_t binary = asm_expression_operator(parser, end, &operator_width);
            const uint32_t precedence = asm_precedence(binary);
            if (!close && precedence == 0)
            {
                break;
            }
            while (operator_count != 0 && operators[operator_count - 1] != '(' && (close || (operators[operator_count - 1] & 0x8000) || asm_precedence(operators[operator_count - 1]) >= precedence))
            {
                const uint16_t op = operators[--operator_count];
                if (op & 0x8000)
                {
                    values[value_count - 1] = asm_unary(parser, values[value_count - 1], op & 0x7fff, location);
                }
                else if (value_count >= 2)
                {
                    values[value_count - 2] = asm_combine(parser, values[value_count - 2], values[value_count - 1], op, location);
                    value_count -= 1;
                }
            }
            if (close)
            {
                if (operator_count == 0)
                {
                    break;
                }
                operator_count -= 1;
                parser->at += 1;
            }
            else
            {
                operators[operator_count++] = binary;
                parser->at += operator_width;
                need_value = true;
            }
        }
    }
    if (need_value)
    {
        asm_fail(parser, location, "incomplete expression");
    }
    while (operator_count != 0 && parser->result.error == 0)
    {
        const uint16_t op = operators[--operator_count];
        if (op == '(')
        {
            asm_fail(parser, location, "unclosed expression parenthesis");
        }
        else if (op & 0x8000)
        {
            values[value_count - 1] = asm_unary(parser, values[value_count - 1], op & 0x7fff, location);
        }
        else if (value_count >= 2)
        {
            values[value_count - 2] = asm_combine(parser, values[value_count - 2], values[value_count - 1], op, location);
            value_count -= 1;
        }
        else
        {
            asm_fail(parser, location, "incomplete expression");
        }
    }
    return value_count != 0 ? values[0] : (AsmExpr){0};
}

static AsmExpr asm_expression(AsmParser* parser, uint32_t end)
{
    return asm_expression_mode(parser, end, false);
}

static __int128 asm_absolute(AsmParser* parser, uint32_t end)
{
    const uint32_t location = parser->tokens[parser->at].start;
    const AsmExpr expression = asm_expression(parser, end);
    if (expression.plus || expression.minus || expression.relocation != ASM_RELOCATION_NONE)
    {
        asm_fail(parser, location, "absolute expression required here (forward constants are unsupported)");
    }
    return expression.value;
}

#include "assembler_words.c"

// A register's name as GNU as reads it: in any case (RAX is rax), with canonical numbers (r8 and xmm0, but r08, xmm00
// and k00 are symbols). No name is longer than five bytes (xmm31), so a longer spelling is never a register.
static bool asm_register(String8 spelling, X86Operand* operand)
{
    char lower[5];
    for (uint32_t i = 0; i < spelling.length && i < sizeof(lower); i += 1)
    {
        const char byte = spelling.pointer[i];
        lower[i] = byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte;
    }
    const String8 name = {.pointer = lower, .length = spelling.length <= sizeof(lower) ? spelling.length : 0};
    static const char* const names[4][8] =
        {
            {"al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil"},
            {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"},
            {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"},
            {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi"},
    };
    bool found = false;
    for (uint32_t width = 0; width < 4; width += 1)
    {
        for (uint32_t reg = 0; reg < 8; reg += 1)
        {
            if (x86_match_name(name, names[width][reg]))
            {
                *operand = (X86Operand){.kind = X86_OPERAND_REGISTER, .class = width + 1, .width = 1u << width, .reg = reg};
                found = true;
            }
        }
    }
    if (x86_match_name(name, "st"))
    {
        // The x87's stack's top; asm_read_register reads st(i) after it.
        *operand = (X86Operand){.kind = X86_OPERAND_REGISTER, .class = BUSTER_X86_CLASS_ST, .width = 0, .reg = 0};
        found = true;
    }
    if (name.length == 3 && name.pointer[0] == 'm' && name.pointer[1] == 'm' && name.pointer[2] >= '0' && name.pointer[2] <= '7')
    {
        // MMX's mm0 to mm7 (#1846), 8 bytes each.
        *operand = (X86Operand){.kind = X86_OPERAND_REGISTER, .class = BUSTER_X86_CLASS_MMX, .width = 8, .reg = (uint8_t)(name.pointer[2] - '0')};
        found = true;
    }
    if (name.length == 2 && name.pointer[1] == 'h' && name.pointer[0] >= 'a' && name.pointer[0] <= 'd')
    {
        const uint8_t high[] = {X86_REGISTER_AH, X86_REGISTER_BH, X86_REGISTER_CH, X86_REGISTER_DH};
        *operand = (X86Operand){.kind = X86_OPERAND_REGISTER, .class = BUSTER_X86_CLASS_GPR8, .width = 1, .reg = high[name.pointer[0] - 'a']};
        found = true;
    }
    if (!found && name.length >= 2)
    {
        uint32_t start = 0;
        uint32_t class = 0;
        uint32_t width = 0;
        if (name.pointer[0] == 'r' && name.pointer[1] >= '0' && name.pointer[1] <= '9')
        {
            start = 1;
            class = BUSTER_X86_CLASS_GPR64;
            width = 8;
        }
        else if (name.length > 3 && name.pointer[1] == 'm' && name.pointer[2] == 'm')
        {
            class = name.pointer[0] == 'x' ? BUSTER_X86_CLASS_XMM : name.pointer[0] == 'y' ? BUSTER_X86_CLASS_YMM
                                                                : name.pointer[0] == 'z'   ? BUSTER_X86_CLASS_ZMM
                                                                                           : 0;
            start = 3;
            width = class != 0 ? 16u << (class - BUSTER_X86_CLASS_XMM) : 0;
        }
        else if (name.pointer[0] == 'k')
        {
            class = BUSTER_X86_CLASS_OPMASK;
            start = 1;
        }
        if (class != 0)
        {
            uint32_t at = start;
            uint32_t reg = 0;
            while (at < name.length && name.pointer[at] >= '0' && name.pointer[at] <= '9' && reg < 100)
            {
                reg = reg * 10 + name.pointer[at++] - '0';
            }
            if (class == BUSTER_X86_CLASS_GPR64 && at + 1 == name.length)
            {
                const char suffix = name.pointer[at++];
                width = suffix == 'b' ? 1 : suffix == 'w' ? 2
                                        : suffix == 'd'   ? 4
                                                          : 0;
                class = width == 1 ? BUSTER_X86_CLASS_GPR8 : width == 2 ? BUSTER_X86_CLASS_GPR16
                                                                        : BUSTER_X86_CLASS_GPR32;
            }
            found = at > start && at == name.length && (name.pointer[start] != '0' || at == start + 1) && reg < (class == BUSTER_X86_CLASS_OPMASK ? 8u : class <= BUSTER_X86_CLASS_GPR64 ? 16u
                                                                                                                                      : 32u);
            found &= class > BUSTER_X86_CLASS_GPR64 || (reg >= 8 && width != 0);
            if (found)
            {
                *operand = (X86Operand){.kind = X86_OPERAND_REGISTER, .class = class, .width = width, .reg = reg};
            }
        }
    }
    if (x86_match_name(name, "rip") || x86_match_name(name, "eip"))
    {
        *operand = (X86Operand){.kind = X86_OPERAND_REGISTER, .class = 255, .width = name.pointer[0] == 'e' || name.pointer[0] == 'E' ? 4 : 8};
        found = true;
    }
    return found;
}

static bool asm_token_register(const AsmParser* parser, uint32_t at, X86Operand* operand)
{
    const uint32_t cached = parser->tokens[at].reserved;
    bool found;
    if (BUSTER_VECTOR_PATH(parser->options.vectorized) && (cached & BUSTER_ASM_REGISTER_CACHED))
    {
        found = (cached & 255) == X86_OPERAND_REGISTER;
        if (found)
        {
            *operand = (X86Operand){.kind = X86_OPERAND_REGISTER, .reg = cached >> 8 & 255, .class = cached >> 16 & 255, .width = cached >> 24 & 127};
        }
    }
    else
    {
        found = asm_register(asm_token_text(parser, at), operand);
    }
    return found;
}

static bool asm_read_register(AsmParser* parser, uint32_t end, X86Operand* operand)
{
    uint32_t at = parser->at;
    const bool prefix = at < end && parser->tokens[at].kind == '%';
    at += prefix;
    const bool found = prefix == parser->prefix && at < end && parser->tokens[at].kind == BUSTER_ASM_WORD && asm_token_register(parser, at, operand);
    if (found)
    {
        parser->at = at + 1;
    }
    // The x87's st(i), 0 to 7, as GNU as reads it after st, in either syntax (`%st(1)`, `st(1)`).
    if (found && operand->class == BUSTER_X86_CLASS_ST && at + 3 < end && parser->tokens[at + 1].kind == '(' &&
        parser->tokens[at + 2].kind == BUSTER_ASM_WORD && parser->tokens[at + 3].kind == ')')
    {
        const String8 digit = asm_token_text(parser, at + 2);
        if (digit.length == 1 && digit.pointer[0] >= '0' && digit.pointer[0] <= '7')
        {
            operand->reg = (uint8_t)(digit.pointer[0] - '0');
            parser->at = at + 4;
        }
        else
        {
            asm_fail(parser, parser->tokens[at + 2].start, "invalid x87 register (st(0) through st(7))");
        }
    }
    return found;
}

// An address's register: RIP or EIP, a base, an index or a gather's vector index. 32-bit base and index registers (and
// EIP) give a 32-bit address in 64-bit mode (#2339), as GNU as and clang's integrated assembler take them
// (`leal (%eax,%ecx), %eax`, an inline template's int operands): X86Operand.address_size records 4 or 8 by the general
// registers seen, which must agree, and the instruction then takes a 67 (asm_emit_address32).
static void asm_address_register(AsmParser* parser, X86Operand* memory, X86Operand reg, uint32_t scale, bool index, uint32_t location)
{
    const bool vector = reg.class >= BUSTER_X86_CLASS_XMM && reg.class <= BUSTER_X86_CLASS_ZMM;
    const bool general = reg.class == BUSTER_X86_CLASS_GPR64 || reg.class == BUSTER_X86_CLASS_GPR32 || reg.class == 255;
    const uint8_t size = !general ? 0 : reg.class == BUSTER_X86_CLASS_GPR32 ? 4 : (uint8_t)reg.width;
    if (size != 0 && memory->address_size != 0 && memory->address_size != size)
    {
        asm_fail(parser, location, "address registers mix 32- and 64-bit sizes");
    }
    else if (reg.class == 255 && !index && !(memory->mode & (BUSTER_X86_MODE_BASE | BUSTER_X86_MODE_INDEX)))
    {
        memory->mode = BUSTER_X86_MEMORY_RIP;
    }
    else if (vector && (index || parser->options.intel) && !(memory->mode & (BUSTER_X86_MODE_RIP | BUSTER_X86_MODE_INDEX)) && scale <= 3)
    {
        // A gather's or scatter's vector index (VSIB, #1115: clang's `vpgatherqd 32(%rdx,%ymm7), %xmm8 {%k1}`, Intel's
        // `[rdx+4*ymm7]`): in Intel syntax wherever it is written, which is never a base; in AT&T's, the index's place.
        memory->mode |= BUSTER_X86_MODE_INDEX | scale;
        memory->index = reg.reg;
        memory->vsib = reg.class;
    }
    else if ((reg.class != BUSTER_X86_CLASS_GPR64 && reg.class != BUSTER_X86_CLASS_GPR32) || (memory->mode & BUSTER_X86_MODE_RIP) || scale > 3)
    {
        asm_fail(parser, location, "address requires 32- or 64-bit base/index registers and a scale of 1, 2, 4 or 8");
    }
    else if (!index && !(memory->mode & BUSTER_X86_MODE_BASE))
    {
        memory->mode |= BUSTER_X86_MODE_BASE;
        memory->base = reg.reg;
    }
    else if (!(memory->mode & BUSTER_X86_MODE_INDEX))
    {
        memory->mode |= BUSTER_X86_MODE_INDEX | scale;
        memory->index = reg.reg;
    }
    else
    {
        asm_fail(parser, location, "too many address registers");
    }
    memory->address_size = size != 0 ? size : memory->address_size;
}

static uint32_t asm_scale(AsmParser* parser, uint32_t end)
{
    const uint32_t location = parser->tokens[parser->at].start;
    const __int128 value = asm_absolute(parser, end);
    if (value != 1 && value != 2 && value != 4 && value != 8)
    {
        asm_fail(parser, location, "address scale must be 1, 2, 4 or 8");
    }
    return value == 2 ? 1 : value == 4 ? 2
                        : value == 8   ? 3
                                       : 0;
}

static void asm_intel_address(AsmParser* parser, uint32_t end, X86Operand* operand, AsmExpr* expression)
{
    const uint32_t location = parser->tokens[parser->at].start;
    operand->kind = X86_OPERAND_MEMORY;
    operand->mode = BUSTER_X86_MEMORY_ABSOLUTE;
    asm_expect(parser, '[', "expected '['");
    *expression = asm_expression_mode(parser, end, true);
    // Prefer RSP as the unscaled base: unlike the other GPRs, it cannot be an index. Otherwise retain source order.
    if (expression->register_count == 2 && expression->registers[1] == 5 && !(expression->indexed & 2))
    {
        const uint8_t reg = expression->registers[0];
        const uint8_t scale = expression->scales[0];
        expression->registers[0] = expression->registers[1];
        expression->scales[0] = expression->scales[1];
        expression->registers[1] = reg;
        expression->scales[1] = scale;
        expression->indexed = (expression->indexed >> 1) | ((expression->indexed & 1) << 1);
    }
    for (uint32_t i = 0; i < expression->register_count && parser->result.error == 0; i += 1)
    {
        const uint32_t factor = expression->scales[i];
        const uint32_t scale = factor == 2 ? 1 : factor == 4 ? 2 : factor == 8 ? 3 : 0;
        const uint8_t number = expression->registers[i];
        const bool narrow = number >= 128;
        const uint8_t wide = narrow ? (uint8_t)(number - 128) : number;
        const X86Operand reg =
        {
            .kind = X86_OPERAND_REGISTER,
            .class = wide == 17 ? 255 : wide >= 32 ? (uint8_t)(BUSTER_X86_CLASS_XMM + (wide - 32) / 32) : narrow ? BUSTER_X86_CLASS_GPR32 : BUSTER_X86_CLASS_GPR64,
            .width = narrow ? 4 : 8,
            .reg = wide >= 32 ? (uint8_t)((wide - 32) % 32) : (uint8_t)(wide - 1),
        };
        asm_address_register(parser, operand, reg, scale, (expression->indexed >> i) & 1, location);
    }
    expression->register_count = 0;
    expression->indexed = 0;
    memset(expression->registers, 0, sizeof(expression->registers));
    memset(expression->scales, 0, sizeof(expression->scales));
    asm_expect(parser, ']', "unclosed address");
}

static void asm_operand(AsmParser* parser, uint32_t end, bool branch, X86Operand* operand, AsmExpr* expression)
{
    const uint32_t location = parser->tokens[parser->at].start;
    bool immediate = false;
    bool indirect = false;
    if (!parser->options.intel)
    {
        indirect = asm_take(parser, '*');
        immediate = asm_take(parser, '$');
    }
    else if (asm_is_case(parser, parser->at, "offset"))
    {
        parser->at += 1;
        immediate = true;
        if (asm_is_case(parser, parser->at, "flat") && parser->at + 1 < end)
        {
            parser->at += 1;
            asm_expect(parser, ':', "expected ':' after flat");
        }
    }
    if (!immediate && parser->options.intel)
    {
        // tbyte is the x87's extended value's 10 bytes (#1134: `fld tbyte ptr [rax]`), and fword a far pointer's 6
        // (`lfs ebx, fword ptr [rax]`).
        static const char* const names[] = {"byte", "word", "dword", "qword", "xmmword", "ymmword", "zmmword", "tbyte", "fword"};
        static const uint8_t widths[] = {1, 2, 4, 8, 16, 32, 64, 10, 6};
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
        {
            if (asm_is_case(parser, parser->at, names[i]))
            {
                operand->width = widths[i];
                parser->at += 1;
                if (!asm_is_case(parser, parser->at, "ptr"))
                {
                    asm_fail(parser, location, "expected 'ptr' after memory width");
                }
                else
                {
                    parser->at += 1;
                }
                break;
            }
        }
    }
    uint32_t segment_at = parser->at;
    segment_at += segment_at < end && parser->tokens[segment_at].kind == '%';
    // FS's, GS's or SS's override (the encoder's BUSTER_X86_SEGMENT_FS, _GS and _SS).
    const bool colon = !immediate && segment_at + 1 < end && parser->tokens[segment_at + 1].kind == ':';
    if (colon && (asm_is_case(parser, segment_at, "fs") || asm_is_case(parser, segment_at, "gs") || asm_is_case(parser, segment_at, "ss")))
    {
        operand->segment = asm_is_case(parser, segment_at, "fs") ? BUSTER_X86_SEGMENT_FS : asm_is_case(parser, segment_at, "gs") ? BUSTER_X86_SEGMENT_GS : BUSTER_X86_SEGMENT_SS;
        parser->at = segment_at + 2;
    }
    const uint16_t width = operand->width;
    const uint8_t segment = operand->segment;
    if (!immediate && width == 0 && segment == 0 && asm_read_register(parser, end, operand))
    {
        if (operand->class == 255 || (branch && !parser->options.intel && !indirect))
        {
            asm_fail(parser, location, "invalid register operand (AT&T indirect branches require '*')");
        }
    }
    else if (parser->options.intel && !immediate && parser->tokens[parser->at].kind == '[')
    {
        asm_intel_address(parser, end, operand, expression);
    }
    else if (!parser->options.intel && !immediate && (!branch || indirect))
    {
        operand->kind = X86_OPERAND_MEMORY;
        operand->mode = BUSTER_X86_MEMORY_ABSOLUTE;
        // A displacement can contain parenthesized arithmetic before the address's own tuple (#2550).
        // The expression reader stops at a following '(', but a leading register/comma tuple has no displacement.
        const uint32_t first = parser->at + 1;
        X86Operand first_register;
        const bool tuple = parser->tokens[parser->at].kind == '(' && first < end &&
                           (parser->tokens[first].kind == '%' || parser->tokens[first].kind == ',' ||
                            (!parser->prefix && parser->tokens[first].kind == BUSTER_ASM_WORD && asm_token_register(parser, first, &first_register)));
        if (!tuple)
        {
            *expression = asm_expression(parser, end);
        }
        if (asm_take(parser, '('))
        {
            X86Operand reg;
            if (asm_read_register(parser, end, &reg))
            {
                asm_address_register(parser, operand, reg, 0, false, location);
            }
            if (asm_take(parser, ','))
            {
                if (!asm_read_register(parser, end, &reg))
                {
                    asm_fail(parser, location, "expected address index register");
                }
                else
                {
                    const uint32_t scale = asm_take(parser, ',') ? asm_scale(parser, end) : 0;
                    asm_address_register(parser, operand, reg, scale, true, location);
                }
            }
            asm_expect(parser, ')', "unclosed address");
        }
    }
    else
    {
        *expression = asm_expression(parser, end);
        operand->kind = X86_OPERAND_IMMEDIATE;
        // A bare symbol is a branch's destination but any other instruction's memory; a width or a segment always
        // asks for memory (`call qword ptr callback` calls through the pointer at callback).
        if (parser->options.intel && !immediate && (width != 0 || segment != 0 || (!branch && (expression->plus != 0 || expression->minus != 0))))
        {
            operand->kind = X86_OPERAND_MEMORY;
            operand->mode = BUSTER_X86_MEMORY_ABSOLUTE;
        }
        if (parser->options.intel && !immediate && parser->at < end && parser->tokens[parser->at].kind == '[')
        {
            // GCC writes symbol[rip] as well as [rip+symbol]. Keep the prefix's displacement and symbol.
            AsmExpr address = {0};
            asm_intel_address(parser, end, operand, &address);
            *expression = asm_combine(parser, *expression, address, '+', location);
        }
    }
    if (segment != 0 && operand->kind != X86_OPERAND_MEMORY)
    {
        asm_fail(parser, location, "segment override requires memory");
    }
    operand->value = expression->value;
    operand->symbolic = expression->plus != 0 || expression->minus != 0;
}

static void asm_decorations(AsmParser* parser, uint32_t end, X86MatchRequest* request, uint32_t operand)
{
    while (parser->at < end && asm_take(parser, '{') && parser->result.error == 0)
    {
        asm_take(parser, '%');
        const String8 text = asm_token_text(parser, parser->at);
        X86Operand mask;
        if (asm_token_register(parser, parser->at, &mask) && mask.class == BUSTER_X86_CLASS_OPMASK && mask.reg != 0 && (request->decorations & 7) == 0)
        {
            if (operand != (parser->options.intel ? 0u : request->count - 1))
            {
                asm_fail(parser, parser->tokens[parser->at].start, "mask must decorate the destination");
            }
            request->decorations |= mask.reg;
        }
        else if (x86_match_name(text, "z") && !(request->decorations & BUSTER_X86_DECORATION_ZEROING))
        {
            if (operand != (parser->options.intel ? 0u : request->count - 1))
            {
                asm_fail(parser, parser->tokens[parser->at].start, "zeroing must decorate the destination");
            }
            request->decorations |= BUSTER_X86_DECORATION_ZEROING;
        }
        else if (text.length > 3 && memcmp(text.pointer, "1to", 3) == 0 && request->broadcast == 0 && request->operands[operand].kind == X86_OPERAND_MEMORY)
        {
            __int128 count;
            // An element broadcast to a vector of 2 to 32 of them: {1to2} of an xmm's doubles to {1to32} of a zmm's words.
            if (!asm_number((String8){.pointer = text.pointer + 3, .length = text.length - 3}, &count) || (count != 2 && count != 4 && count != 8 && count != 16 && count != 32))
            {
                asm_fail(parser, parser->tokens[parser->at].start, "unsupported broadcast");
            }
            request->broadcast = count;
        }
        else
        {
            asm_fail(parser, parser->tokens[parser->at].start, "unsupported or duplicate instruction decoration");
        }
        parser->at += 1;
        asm_expect(parser, '}', "unclosed decoration");
    }
}

// Replaces a common condition or shift alias with the canonical catalogue name it shares an identity with.
static bool asm_alias(String8* name)
{
    static const char* const aliases[][2] =
        {
            {"jz", "je"},
            {"jnz", "jne"},
            {"jc", "jb"},
            {"jnc", "jae"},
            {"jna", "jbe"},
            {"jnbe", "ja"},
            {"jnae", "jb"},
            {"jnb", "jae"},
            {"jnge", "jl"},
            {"jnl", "jge"},
            {"jng", "jle"},
            {"jnle", "jg"},
            {"jpe", "jp"},
            {"jpo", "jnp"},
            // The same conditions' aliases of setcc and cmovcc (#1134).
            {"setz", "sete"},
            {"setnz", "setne"},
            {"setc", "setb"},
            {"setnc", "setae"},
            {"setna", "setbe"},
            {"setnbe", "seta"},
            {"setnae", "setb"},
            {"setnb", "setae"},
            {"setnge", "setl"},
            {"setnl", "setge"},
            {"setng", "setle"},
            {"setnle", "setg"},
            {"setpe", "setp"},
            {"setpo", "setnp"},
            {"cmovz", "cmove"},
            {"cmovnz", "cmovne"},
            {"cmovc", "cmovb"},
            {"cmovnc", "cmovae"},
            {"cmovna", "cmovbe"},
            {"cmovnbe", "cmova"},
            {"cmovnae", "cmovb"},
            {"cmovnb", "cmovae"},
            {"cmovnge", "cmovl"},
            {"cmovnl", "cmovge"},
            {"cmovng", "cmovle"},
            {"cmovnle", "cmovg"},
            {"cmovpe", "cmovp"},
            {"cmovpo", "cmovnp"},
            {"sal", "shl"},
            // GNU as's other names (#1134): the dword string instructions' AT&T ones that asm_string_mnemonic doesn't
            // read (insl, outsl), iret's and sysret's of the default operand size, and the far returns' (lret).
            {"insl", "insd"},
            {"outsl", "outsd"},
            {"iret", "iretd"},
            {"iretl", "iretd"},
            {"sysret", "sysretd"},
            {"sysretl", "sysretd"},
            {"lret", "retfd"},
            {"lretw", "retfw"},
            {"lretl", "retfd"},
            {"lretq", "retfq"},
            // The x87's 16-bit environment and state forms, which AT&T spells with an s (#1134: fnsaves, as GNU as).
            {"fldenvs", "fldenvw"},
            {"fnstenvs", "fnstenvw"},
            {"fstenvs", "fstenvw"},
            {"frstors", "frstorw"},
            {"fnsaves", "fnsavew"},
            {"fsaves", "fsavew"},
            // LLVM's AT&T names of the comparisons that pop (#1115: clang's long double code writes fucompi).
            {"fucompi", "fucomip"},
            {"fcompi", "fcomip"},
            {"cbtw", "cbw"},
            {"cwtl", "cwde"},
            {"cltq", "cdqe"},
            {"cwtd", "cwd"},
            {"cltd", "cdq"},
            {"cqto", "cqo"},
    };
    bool found = false;
    // Every instruction comes here: compare whole names only with the rows of its first letter.
    const char first = name->length != 0 ? name->pointer[0] : 0;
    for (uint32_t i = 0; !found && i < BUSTER_ARRAY_LENGTH(aliases); i += 1)
    {
        found = aliases[i][0][0] == first && x86_match_name(*name, aliases[i][0]);
        if (found)
        {
            *name = (String8){.pointer = (char*)aliases[i][1], .length = strlen(aliases[i][1])};
        }
    }
    return found;
}

// An AT&T mnemonic whose suffix gives its operands' widths: the catalogue's mnemonic, and the source's and the
// destination's widths.
typedef struct AsmAttExtension AsmAttExtension;
struct AsmAttExtension
{
    const char* name;
    const char* canonical;
    uint8_t source;
    uint8_t destination;
};

static bool asm_att_extension(String8* name, uint32_t* width, uint32_t* source_width)
{
    static const AsmAttExtension moves[] =
    {
        {"movzbw", "movzx", 1, 2},
        {"movzbl", "movzx", 1, 4},
        {"movzbq", "movzx", 1, 8},
        {"movzwl", "movzx", 2, 4},
        {"movzwq", "movzx", 2, 8},
        {"movsbw", "movsx", 1, 2},
        {"movsbl", "movsx", 1, 4},
        {"movsbq", "movsx", 1, 8},
        {"movswl", "movsx", 2, 4},
        {"movswq", "movsx", 2, 8},
        {"movslq", "movsxd", 4, 8},
    };
    // Only a move's extension starts with m: every AT&T instruction comes here, and one scan of all the rows cost each of
    // them about 55 ns on the 9700X once the x87's integers had theirs (#147), which asm_att_suffix now reads by rule.
    const char letter = name->length != 0 ? name->pointer[0] : 0;
    const AsmAttExtension* rows = moves;
    const uint32_t count = letter == 'm' ? BUSTER_ARRAY_LENGTH(moves) : 0;
    bool found = false;
    for (uint32_t i = 0; !found && i < count; i += 1)
    {
        found = x86_match_name(*name, rows[i].name);
        if (found)
        {
            *name = (String8){.pointer = (char*)rows[i].canonical, .length = strlen(rows[i].canonical)};
            *width = rows[i].destination;
            *source_width = rows[i].source;
        }
    }
    return found;
}

// The x87's instructions on its stack registers as GNU as reads them (#147), on a request in Intel's operand order: the
// operands it leaves out (fxch, fcom, fcomp, fucom and fucomp of st(1); AT&T's bare fadd as faddp, and the other five
// arithmetic mnemonics as their popping forms; fadd st(i) as fadd st(0), st(i); faddp alone as
// faddp st(1), st(0), and faddp st(i) as faddp st(i), st(0); fcomi st(i) as fcomi st(0), st(i)), and AT&T's subtraction
// and division with st(i) the destination, which GNU as and clang assemble as Intel's reversed ones (`fsub %st, %st(1)`
// is Intel's fsubr st(1), st(0), and `fsubp` Intel's fsubrp): the System V assembler's convention, which GNU as keeps.
static void asm_x87_operands(const AsmParser* parser, X86MatchRequest* request)
{
    static const char* const one[] = { "fxch", "fcom", "fcomp", "fucom", "fucomp" };
    static const char* const arithmetic[] = { "fadd", "fmul", "fsub", "fsubr", "fdiv", "fdivr" };
    static const char* const popping[] = { "faddp", "fmulp", "fsubp", "fsubrp", "fdivp", "fdivrp" };
    static const char* const compares[] = { "fcomi", "fcomip", "fucomi", "fucomip" };
    // Each of fsub, fsubr, fdiv and fdivr's (and their popping forms') reversed mnemonic.
    static const char* const reversed[][2] = {
        { "fsub", "fsubr" }, { "fsubr", "fsub" }, { "fdiv", "fdivr" }, { "fdivr", "fdiv" },
        { "fsubp", "fsubrp" }, { "fsubrp", "fsubp" }, { "fdivp", "fdivrp" }, { "fdivrp", "fdivp" },
    };
    const X86Operand top = { .kind = X86_OPERAND_REGISTER, .class = BUSTER_X86_CLASS_ST, .reg = 0 };
    const X86Operand next = { .kind = X86_OPERAND_REGISTER, .class = BUSTER_X86_CLASS_ST, .reg = 1 };
    const bool stack = request->count == 0 || (request->count <= 2 && request->operands[0].kind == X86_OPERAND_REGISTER && request->operands[0].class == BUSTER_X86_CLASS_ST &&
                                               (request->count == 1 || (request->operands[1].kind == X86_OPERAND_REGISTER && request->operands[1].class == BUSTER_X86_CLASS_ST)));
    bool popped = false;
    // A bare arithmetic mnemonic in AT&T syntax is its popping form's (GNU as warns that it translates `fadd` to `faddp`,
    // and Intel syntax refuses it, where clang's assembler takes it).
    for (uint32_t i = 0; stack && !parser->options.intel && request->count == 0 && i < BUSTER_ARRAY_LENGTH(arithmetic); i += 1)
    {
        if (x86_match_name(request->mnemonic, arithmetic[i]))
        {
            request->mnemonic = (String8){ .pointer = (char*)popping[i], .length = strlen(popping[i]) };
            break;
        }
    }
    for (uint32_t i = 0; stack && i < BUSTER_ARRAY_LENGTH(one); i += 1)
    {
        if (x86_match_name(request->mnemonic, one[i]) && request->count == 0)
        {
            request->operands[request->count++] = next;
        }
    }
    for (uint32_t i = 0; stack && i < BUSTER_ARRAY_LENGTH(arithmetic); i += 1)
    {
        if (x86_match_name(request->mnemonic, arithmetic[i]) && request->count == 1)
        {
            request->operands[1] = request->operands[0];
            request->operands[0] = top;
            request->count = 2;
        }
    }
    for (uint32_t i = 0; stack && i < BUSTER_ARRAY_LENGTH(popping); i += 1)
    {
        popped |= x86_match_name(request->mnemonic, popping[i]);
        if (x86_match_name(request->mnemonic, popping[i]) && request->count < 2)
        {
            request->operands[0] = request->count == 0 ? next : request->operands[0];
            request->operands[1] = top;
            request->count = 2;
        }
    }
    for (uint32_t i = 0; stack && i < BUSTER_ARRAY_LENGTH(compares); i += 1)
    {
        if (x86_match_name(request->mnemonic, compares[i]) && request->count < 2)
        {
            request->operands[1] = request->count == 0 ? next : request->operands[0];
            request->operands[0] = top;
            request->count = 2;
        }
    }
    // faddp and fmulp of st(0) and st(i), which are commutative, take st(i) as the destination, as GNU as translates them
    // with a warning (#1115: `faddp %st(1), %st` is `faddp %st, %st(1)`, and Intel's `faddp st, st(1)` its own). GNU as
    // refuses the other pops so, and so does the matcher, which has no such form.
    const bool commutative = x86_match_name(request->mnemonic, "faddp") || x86_match_name(request->mnemonic, "fmulp");
    if (stack && commutative && request->count == 2 && request->operands[0].reg == 0)
    {
        const X86Operand source = request->operands[1];
        request->operands[1] = request->operands[0];
        request->operands[0] = source;
    }
    // AT&T's st(i) destination: a popping form always, another where st(i) isn't st(0) (fsub %st, %st is Intel's fsub
    // st(0), st(0), the form of st(0)'s destination, as GNU as takes it).
    const bool destination = stack && request->count == 2 && request->operands[1].reg == 0 && (popped || request->operands[0].reg != 0);
    for (uint32_t i = 0; destination && !parser->options.intel && i < BUSTER_ARRAY_LENGTH(reversed); i += 1)
    {
        if (x86_match_name(request->mnemonic, reversed[i][0]))
        {
            request->mnemonic = (String8){ .pointer = (char*)reversed[i][1], .length = strlen(reversed[i][1]) };
            break;
        }
    }
}

// An AT&T mnemonic's size suffix, for a name the index doesn't have whole (#1134), taken off `name`: the width it
// gives, and whether it is the x87's memory width, which the parser then checks each operand against. The x87's: s a
// float's or a short's, l a double's or a long's, t an extended value's and ll or q a long long's (`fistpll`), where its
// integer instructions are those whose names start with fi, of a base the index has. A vector's memory width: x, y or
// z for 16, 32 or 64 bytes (`vcvtpd2dqx`), of a base the index has. Otherwise an integer's: b, w, l or q, whose width
// the matcher compares with each form's (x86_match_suffix_width): `cvtsi2sdl` sizes its source, `cvtsd2siq` its
// destination. 0 for a name without a suffix.
static uint32_t asm_att_suffix(const X86MatchIndex* index, String8* name, bool* x87)
{
    static const char* const floats[] = { "ll", "q", "s", "l", "t" };
    uint32_t width = 0;
    *x87 = false;
    for (uint32_t i = 0; width == 0 && name->length > 2 && name->pointer[0] == 'f' && i < BUSTER_ARRAY_LENGTH(floats); i += 1)
    {
        const uint64_t length = strlen(floats[i]);
        const String8 base = { .pointer = name->pointer, .length = name->length - length };
        const bool suffixed = name->length > length && memcmp(name->pointer + base.length, floats[i], length) == 0;
        const bool integer = base.length > 2 && base.pointer[1] == 'i';
        const bool known = suffixed && index->names[x86_match_bucket(index, base)].pointer != 0;
        const uint32_t widths[][2] = { { 0, 8 }, { 0, 8 }, { 4, 2 }, { 8, 4 }, { 10, 0 } };
        width = known ? widths[i][integer] : 0;
        if (width != 0)
        {
            *name = base;
            *x87 = true;
        }
    }
    const char last = name->length > 1 ? name->pointer[name->length - 1] : 0;
    const String8 vector = { .pointer = name->pointer, .length = name->length - 1 };
    if (width == 0 && (last == 'x' || last == 'y' || last == 'z') && index->names[x86_match_bucket(index, vector)].pointer != 0)
    {
        width = last == 'x' ? 16 : last == 'y' ? 32 : 64;
        *name = vector;
    }
    if (width == 0)
    {
        width = last == 'b' ? 1 : last == 'w' ? 2 : last == 'l' ? 4 : last == 'q' ? 8 : 0;
        name->length -= width != 0;
    }
    return width;
}

// A locked instruction as the catalogue spells it (`lock cmpxchg`, one mnemonic, whose forms take memory): the prefix
// before the instruction on its line (`lock cmpxchg %ecx, (%rdx)`, two tokens), or as a statement of its own before it
// (`lock ; cmpxchg %ecx, (%rdx)`, as musl's atomics write it), which asm_parse puts on the instruction's line, as GNU as
// applies it to the next instruction. `name` is the mnemonic's token; a prefix takes the next token, the instruction,
// which the parser then skips. Any other mnemonic stays as it is.
static void asm_lock_mnemonic(AsmParser* parser, String8* name, uint32_t end)
{
    if (x86_match_name(*name, "lock") && parser->at < end && parser->tokens[parser->at].kind == BUSTER_ASM_WORD)
    {
        const String8 instruction = asm_token_text(parser, parser->at);
        char* bytes = arena_push(parser->arena, instruction.length + 5, 1);
        if (bytes == 0)
        {
            asm_fail(parser, parser->tokens[parser->at - 1].start, "arena exhausted");
        }
        else
        {
            memcpy(bytes, "lock ", 5);
            memcpy(bytes + 5, instruction.pointer, instruction.length);
            *name = (String8){ .pointer = bytes, .length = instruction.length + 5 };
            parser->at += 1;
        }
    }
}

// Whether a (possibly prefixed) mnemonic is a branch. GNU as writes notrack and bnd before the branch, two or three
// tokens which the catalogue keeps as one mnemonic.
static bool asm_branch_mnemonic(String8 name)
{
    return (name.length != 0 && name.pointer[0] == 'j') || x86_match_name(name, "call") || (name.length >= 4 && memcmp(name.pointer, "loop", 4) == 0) ||
           (name.length >= 4 && memcmp(name.pointer + name.length - 4, " jmp", 4) == 0) ||
           (name.length >= 5 && memcmp(name.pointer + name.length - 5, " call", 5) == 0);
}

// GNU's `notrack jmp *%rax`, `bnd call *%rax` and the two together, which the encoder's catalogue names with spaces.
// Assemble them into one mnemonic when a branch follows; otherwise leave the statement to the ordinary diagnostics.
static void asm_prefixed_mnemonic(AsmParser* parser, String8* name, uint32_t end)
{
    const bool notrack = x86_match_name(*name, "notrack");
    const bool bnd = x86_match_name(*name, "bnd");
    char text[64];
    uint32_t length = notrack || bnd ? name->length : 0;
    if (length != 0 && length < sizeof(text))
    {
        memcpy(text, name->pointer, length);
        uint32_t at = parser->at;
        bool branch = false;
        while (!branch && at < end && parser->tokens[at].kind == BUSTER_ASM_WORD)
        {
            const String8 word = asm_token_text(parser, at);
            const bool prefix = x86_match_name(word, "notrack") || x86_match_name(word, "bnd");
            const bool jump = (word.length == 3 || word.length == 4) && memcmp(word.pointer, "jmp", 3) == 0 && (word.length == 3 || word.pointer[3] == 'q');
            const bool call = (word.length == 4 || word.length == 5) && memcmp(word.pointer, "call", 4) == 0 && (word.length == 4 || word.pointer[4] == 'q');
            branch = jump || call;
            if ((!prefix && !branch) || length + 1 + word.length >= sizeof(text))
            {
                break;
            }
            text[length++] = ' ';
            memcpy(text + length, word.pointer, word.length);
            length += word.length;
            at += 1;
        }
        if (branch)
        {
            char* spelling = arena_push(parser->arena, length, 1);
            if (spelling == 0)
            {
                asm_fail(parser, parser->tokens[at - 1].start, "arena exhausted");
            }
            else
            {
                memcpy(spelling, text, length);
                *name = (String8){.pointer = spelling, .length = length};
                parser->at = at;
            }
        }
    }
}

// A string instruction as the catalogue spells it, with its prefix (`rep movsb`, two tokens, is one mnemonic): repz and
// repnz are repe and repne, rep before a compare or a scan is repe, and AT&T's dword suffix l is d. `name` is the
// mnemonic's token; a prefix takes the next token, the instruction, which the parser then skips. Any other mnemonic
// stays as it is, and only a prefix's first letter or an AT&T dword's suffix makes one worth comparing.
static bool asm_string_mnemonic(AsmParser* parser, String8* name, uint32_t end)
{
    static const char* const prefixes[][2] = { { "rep", "rep" }, { "repe", "repe" }, { "repz", "repe" }, { "repne", "repne" }, { "repnz", "repne" } };
    static const char* const bases[] = { "movs", "stos", "lods", "cmps", "scas" };
    const bool candidate = (name->length >= 3 && name->pointer[0] == 'r') || (!parser->options.intel && name->length == 5 && name->pointer[4] == 'l');
    const char* prefix = NULL;
    for (uint32_t i = 0; candidate && prefix == NULL && i < BUSTER_ARRAY_LENGTH(prefixes); i += 1)
    {
        prefix = x86_match_name(*name, prefixes[i][0]) ? prefixes[i][1] : NULL;
    }
    const bool prefixed = prefix != NULL && parser->at < end && parser->tokens[parser->at].kind == BUSTER_ASM_WORD;
    const String8 instruction = prefixed ? asm_token_text(parser, parser->at) : *name;
    const bool rep_nop = prefixed && strcmp(prefix, "rep") == 0 && x86_match_name(instruction, "nop");
    if (rep_nop)
    {
        // The spin hint is the same encoding as PAUSE. Its operandless form alone is admitted here.
        *name = BUSTER_S8("pause");
        parser->at += 1;
    }
    // `rep ret` and `repz ret` (and their q suffixed spelling): AMD's return-stack-buffer hint, F3 C3, and its repne
    // spelling, F2 C3. `retl` is not one GNU as takes.
    const bool rep_ret = prefixed && (strcmp(prefix, "rep") == 0 || strcmp(prefix, "repe") == 0 || strcmp(prefix, "repne") == 0) &&
                         (x86_match_name(instruction, "ret") || x86_match_name(instruction, "retq"));
    if (rep_ret)
    {
        *name = strcmp(prefix, "repne") == 0 ? BUSTER_S8("repne ret") : BUSTER_S8("rep ret");
        parser->at += 1;
    }
    // Match REP BSF/BSR against the baseline scans. Their F3-prefixed encodings also spell TZCNT/LZCNT, but explicit
    // scan syntax is valid without those features. An AT&T size suffix stays on the name.
    const char suffix = instruction.length == 4 && !parser->options.intel ? instruction.pointer[3] : 0;
    const bool scan = prefixed && (strcmp(prefix, "rep") == 0 || strcmp(prefix, "repe") == 0) && (instruction.length == 3 || suffix == 'w' || suffix == 'l' || suffix == 'q') &&
                      (memcmp(instruction.pointer, "bsr", 3) == 0 || memcmp(instruction.pointer, "bsf", 3) == 0);
    if (scan)
    {
        *name = instruction;
        parser->at += 1;
    }
    uint32_t base = BUSTER_ARRAY_LENGTH(bases);
    for (uint32_t i = 0; candidate && instruction.length == 5 && i < BUSTER_ARRAY_LENGTH(bases); i += 1)
    {
        base = memcmp(instruction.pointer, bases[i], 4) == 0 ? i : base;
    }
    const char written = base < BUSTER_ARRAY_LENGTH(bases) ? instruction.pointer[4] : 0;
    const char size = !parser->options.intel && written == 'l' ? 'd' : written;
    if (!rep_nop && !scan && (size == 'b' || size == 'w' || size == 'd' || size == 'q') && (prefixed || size != written))
    {
        const char* spelled = prefixed && base >= 3 && strcmp(prefix, "rep") == 0 ? "repe" : prefixed ? prefix : "";
        const uint64_t prefix_length = strlen(spelled);
        const uint64_t length = prefix_length + (prefixed ? 1 : 0) + 5;
        char* bytes = arena_push(parser->arena, length, 1);
        if (bytes == 0)
        {
            asm_fail(parser, parser->tokens[parser->at - 1].start, "arena exhausted");
        }
        else
        {
            memcpy(bytes, spelled, prefix_length);
            bytes[prefix_length] = ' ';
            memcpy(bytes + length - 5, bases[base], 4);
            bytes[length - 1] = size;
            *name = (String8){ .pointer = bytes, .length = length };
            parser->at += prefixed ? 1 : 0;
        }
    }
    return scan;
}

// A string instruction's explicit operand (#1115): memory of RSI or RDI (or ESI or EDI) alone, with a segment or not;
// the accumulator; or DX, or AT&T's `(%dx)`, for a port. Its width is a register's, or Intel's `ptr`'s.
typedef struct AsmStringOperand AsmStringOperand;
struct AsmStringOperand
{
    uint8_t kind;
    uint8_t segment;
    uint8_t width;
    bool destination;
    bool narrow;
};

#define ASM_STRING_NONE 0
#define ASM_STRING_MEMORY 1
#define ASM_STRING_ACCUMULATOR 2
#define ASM_STRING_PORT 3

static AsmStringOperand asm_string_operand(const AsmParser* parser, uint32_t at, uint32_t end)
{
    static const char* const segments[] = { "es", "cs", "ss", "ds", "fs", "gs" };
    static const uint8_t overrides[] = { 0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65 };
    static const char* const accumulators[] = { "al", "ax", "eax", "rax" };
    static const char* const sizes[] = { "byte", "word", "dword", "qword" };
    const bool intel = parser->options.intel;
    AsmStringOperand result = { 0 };
    for (uint32_t i = 0; intel && at + 1 < end && asm_is_case(parser, at + 1, "ptr") && i < BUSTER_ARRAY_LENGTH(sizes); i += 1)
    {
        result.width = asm_is_case(parser, at, sizes[i]) ? (uint8_t)(1u << i) : result.width;
    }
    at += result.width != 0 ? 2 : 0;
    const uint32_t named = at + (!intel && at < end && parser->tokens[at].kind == '%' ? 1 : 0);
    for (uint32_t i = 0; named + 1 < end && parser->tokens[named + 1].kind == ':' && (intel || named != at) && i < BUSTER_ARRAY_LENGTH(segments); i += 1)
    {
        result.segment = asm_is_case(parser, named, segments[i]) ? overrides[i] : result.segment;
    }
    at = result.segment != 0 ? named + 2 : at;
    const uint32_t reg = at + (intel ? 0 : 1);
    if (result.segment == 0 && reg + 1 == end && (intel || parser->tokens[at].kind == '%'))
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(accumulators); i += 1)
        {
            if (asm_is_case(parser, reg, accumulators[i]))
            {
                result.kind = ASM_STRING_ACCUMULATOR;
                result.width = (uint8_t)(1u << i);
            }
        }
        result.kind = asm_is_case(parser, reg, "dx") ? ASM_STRING_PORT : result.kind;
    }
    else if (at < end && parser->tokens[at].kind == (intel ? '[' : '('))
    {
        const uint32_t base = at + (intel ? 1 : 2);
        const bool shaped = base + 2 == end && parser->tokens[end - 1].kind == (intel ? ']' : ')') && (intel || parser->tokens[at + 1].kind == '%');
        const bool source = shaped && (asm_is_case(parser, base, "rsi") || asm_is_case(parser, base, "esi"));
        const bool destination = shaped && (asm_is_case(parser, base, "rdi") || asm_is_case(parser, base, "edi"));
        result.kind = source || destination ? ASM_STRING_MEMORY : !intel && shaped && result.segment == 0 && asm_is_case(parser, base, "dx") ? ASM_STRING_PORT : ASM_STRING_NONE;
        result.destination = destination;
        result.narrow = (source || destination) && asm_token_text(parser, base).pointer[0] != 'r' && asm_token_text(parser, base).pointer[0] != 'R';
    }
    return result;
}

// A string instruction's explicit operands, as GNU as takes them (#1115: clang's `rep;movsq (%rsi), %es:(%rdi)` and
// `rep stosb es:[rdi], al`): RSI's memory, of any segment, as the source; RDI's, of ES alone, as the destination; the
// accumulator and DX's port, each where the instruction has it (AT&T's order, Intel's the reverse). They name its
// size where the mnemonic doesn't (`stos %eax, %es:(%rdi)` is stosl) and must agree with it. GNU as then writes the
// source's segment but DS's, and 0x67 for ESI and EDI, before the instruction's own prefixes: `prefixes` takes those
// bytes. Operands of another shape (SSE's movsd and cmpsd) are left to the instruction's own forms.
static uint32_t asm_string_operands(AsmParser* parser, String8* name, uint32_t end, uint8_t* prefixes)
{
    static const char* const bases[] = { "movs", "cmps", "stos", "lods", "scas", "ins", "outs" };
    // What each takes, in AT&T's order: S the source's memory, D the destination's, A the accumulator, P a port.
    static const char* const shapes[] = { "SD", "DS", "AD", "SA", "DA", "PD", "SP" };
    uint64_t space = 0;
    for (uint64_t i = 0; i < name->length; i += 1)
    {
        space = name->pointer[i] == ' ' ? i + 1 : space;
    }
    const String8 bare = { .pointer = name->pointer + space, .length = name->length - space };
    uint32_t base = BUSTER_ARRAY_LENGTH(bases);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(bases); i += 1)
    {
        const uint64_t length = strlen(bases[i]);
        base = base == BUSTER_ARRAY_LENGTH(bases) && (bare.length == length || bare.length == length + 1) && memcmp(bare.pointer, bases[i], length) == 0 ? i : base;
    }
    const uint64_t base_length = base < BUSTER_ARRAY_LENGTH(bases) ? strlen(bases[base]) : 0;
    const char written = base_length != 0 && bare.length == base_length + 1 ? bare.pointer[base_length] : 0;
    const char suffix = written == 'l' && !parser->options.intel ? 'd' : written;
    uint32_t comma = end;
    uint32_t commas = 0;
    uint32_t depth = 0;
    for (uint32_t i = parser->at; base_length != 0 && i < end; i += 1)
    {
        const uint16_t kind = parser->tokens[i].kind;
        depth += kind == '(' || kind == '[';
        depth -= (kind == ')' || kind == ']') && depth != 0;
        comma = kind == ',' && depth == 0 && commas++ == 0 ? i : comma;
    }
    uint32_t count = 0;
    const bool sized = written == 0 || suffix == 'b' || suffix == 'w' || suffix == 'd' || (suffix == 'q' && base < 5);
    if (base_length != 0 && sized && commas == 1 && parser->at < comma && comma + 1 < end)
    {
        AsmStringOperand operands[2] = { asm_string_operand(parser, parser->at, comma), asm_string_operand(parser, comma + 1, end) };
        if (parser->options.intel)
        {
            const AsmStringOperand first = operands[0];
            operands[0] = operands[1];
            operands[1] = first;
        }
        bool shaped = true;
        uint8_t width = suffix == 'b' ? 1 : suffix == 'w' ? 2 : suffix == 'd' ? 4 : suffix == 'q' ? 8 : 0;
        bool agree = true;
        bool narrow = false;
        bool wide = false;
        uint8_t segment = 0;
        bool destination_segment = true;
        for (uint32_t i = 0; i < 2; i += 1)
        {
            const char shape = shapes[base][i];
            const AsmStringOperand* operand = &operands[i];
            shaped &= shape == 'S' ? operand->kind == ASM_STRING_MEMORY && !operand->destination : shape == 'D' ? operand->kind == ASM_STRING_MEMORY && operand->destination :
                      shape == 'A' ? operand->kind == ASM_STRING_ACCUMULATOR : operand->kind == ASM_STRING_PORT;
            agree &= operand->width == 0 || width == 0 || operand->width == width;
            width = width == 0 ? operand->width : width;
            narrow |= operand->kind == ASM_STRING_MEMORY && operand->narrow;
            wide |= operand->kind == ASM_STRING_MEMORY && !operand->narrow;
            segment = shape == 'S' && operand->segment != 0x3e ? operand->segment : segment;
            destination_segment &= shape != 'D' || operand->segment == 0 || operand->segment == 0x26;
        }
        if (shaped)
        {
            const char size = width == 1 ? 'b' : width == 2 ? 'w' : width == 4 ? 'd' : width == 8 && base < 5 ? 'q' : 0;
            char* spelling = arena_push(parser->arena, space + base_length + 1, 1);
            if (!destination_segment)
            {
                asm_fail(parser, parser->tokens[parser->at].start, "a string instruction's destination must use the ES segment");
            }
            else if (!agree || size == 0)
            {
                asm_fail(parser, parser->tokens[parser->at].start, size == 0 ? "a string instruction's operands give no size, nor does its mnemonic"
                                                                             : "a string instruction's operands and mnemonic disagree on its size");
            }
            else if (narrow && wide)
            {
                asm_fail(parser, parser->tokens[parser->at].start, "a string instruction's operands mix address sizes");
            }
            else if (spelling == 0)
            {
                asm_fail(parser, parser->tokens[parser->at].start, "arena exhausted");
            }
            else
            {
                memcpy(spelling, name->pointer, space);
                memcpy(spelling + space, bases[base], base_length);
                spelling[space + base_length] = size;
                *name = (String8){ .pointer = spelling, .length = space + base_length + 1 };
                prefixes[count] = segment;
                count += segment != 0;
                prefixes[count] = 0x67;
                count += narrow;
                parser->at = end;
            }
        }
    }
    return count;
}

// GNU as's compare-predicate names (#1115): cmpPss, cmpPsd, cmpPps and cmpPpd for SSE's eight predicates, vcmpP... for
// AVX's 32 (and the aliases of the first eight), and AVX-512's vpcmpP{b,w,d,q} and vpcmpPu{b,w,d,q} for its eight, each
// the instruction without the predicate and the predicate's number as its immediate: clang writes `cmpltsd %xmm2,
// %xmm1` for a comparison's mask (cmpsd $1). vpcmpeq{b,w,d,q} are instructions of their own (PCMPEQ), not vpcmp's
// predicate 0. Returns the predicate, or -1 for any other name, which stays as it is.
static int32_t asm_compare_predicate(String8* name)
{
    static const char* const vector[] = {
        "eq", "lt", "le", "unord", "neq", "nlt", "nle", "ord",
        "eq_uq", "nge", "ngt", "false", "neq_oq", "ge", "gt", "true",
        "eq_os", "lt_oq", "le_oq", "unord_s", "neq_us", "nlt_uq", "nle_uq", "ord_s",
        "eq_us", "nge_uq", "ngt_uq", "false_os", "neq_os", "ge_oq", "gt_oq", "true_us",
        "eq_oq", "lt_os", "le_os", "unord_q", "neq_uq", "nlt_us", "nle_us", "ord_q",
    };
    static const char* const integer[] = { "eq", "lt", "le", "false", "neq", "nlt", "nle", "true" };
    static const char* const floats[] = { "ss", "sd", "ps", "pd" };
    static const char* const sse_names[] = { "cmpss", "cmpsd", "cmpps", "cmppd" };
    static const char* const avx_names[] = { "vcmpss", "vcmpsd", "vcmpps", "vcmppd" };
    static const char* const elements[] = { "ub", "uw", "ud", "uq", "b", "w", "d", "q" };
    static const char* const integer_names[] = { "vpcmpub", "vpcmpuw", "vpcmpud", "vpcmpuq", "vpcmpb", "vpcmpw", "vpcmpd", "vpcmpq" };
    int32_t result = -1;
    const char* canonical = 0;
    const bool avx = name->length > 6 && memcmp(name->pointer, "vcmp", 4) == 0;
    const bool sse = name->length > 5 && memcmp(name->pointer, "cmp", 3) == 0;
    const bool masks = name->length > 6 && memcmp(name->pointer, "vpcmp", 5) == 0;
    for (uint32_t f = 0; (avx || sse) && canonical == 0 && f < BUSTER_ARRAY_LENGTH(floats); f += 1)
    {
        const uint64_t start = avx ? 4 : 3;
        const uint64_t middle = name->length - start - 2;
        if (memcmp(name->pointer + name->length - 2, floats[f], 2) == 0)
        {
            for (uint32_t p = 0; canonical == 0 && p < (avx ? BUSTER_ARRAY_LENGTH(vector) : 8u); p += 1)
            {
                if (strlen(vector[p]) == middle && memcmp(name->pointer + start, vector[p], middle) == 0)
                {
                    result = (int32_t)(p < 32 ? p : p - 32);
                    canonical = avx ? avx_names[f] : sse_names[f];
                }
            }
        }
    }
    for (uint32_t e = 0; masks && canonical == 0 && e < BUSTER_ARRAY_LENGTH(elements); e += 1)
    {
        const uint64_t suffix = strlen(elements[e]);
        const uint64_t middle = name->length - 5 - suffix;
        if (name->length > 5 + suffix && memcmp(name->pointer + name->length - suffix, elements[e], suffix) == 0)
        {
            for (uint32_t p = 0; canonical == 0 && p < BUSTER_ARRAY_LENGTH(integer); p += 1)
            {
                if (strlen(integer[p]) == middle && memcmp(name->pointer + 5, integer[p], middle) == 0 && (p != 0 || suffix == 2))
                {
                    result = (int32_t)p;
                    canonical = integer_names[e];
                }
            }
        }
    }
    if (canonical != 0)
    {
        *name = (String8){ .pointer = (char*)canonical, .length = strlen(canonical) };
    }
    return result;
}

// Whether an AT&T instruction's operands (its tokens to `end`) name an XMM or an MMX register: then movq is SSE2's or
// MMX's (#1846), as GNU as reads it, not mov's 64-bit form.
static bool asm_vector_operands(const AsmParser* parser, uint32_t end)
{
    bool found = false;
    for (uint32_t i = parser->at; !found && i + 1 < end; i += 1)
    {
        X86Operand operand;
        found = parser->tokens[i].kind == '%' && parser->tokens[i + 1].kind == BUSTER_ASM_WORD && asm_token_register(parser, i + 1, &operand) &&
                (operand.class == BUSTER_X86_CLASS_XMM || operand.class == BUSTER_X86_CLASS_MMX);
    }
    return found;
}

static void asm_instruction(AsmParser* parser, uint32_t end)
{
    const uint32_t location = parser->tokens[parser->at].start;
    if (parser->instruction_count >= parser->instruction_capacity || object_class_is_zero_fill(parser->sections[parser->section].class))
    {
        asm_fail(parser, location, "instruction capacity exhausted or instruction in .bss");
    }
    else
    {
        AsmInstruction* instruction = &parser->instructions[parser->instruction_count++];
        *instruction = (AsmInstruction){.location = location};
        instruction->request.mnemonic = asm_token_text(parser, parser->at++);
        String8* name = &instruction->request.mnemonic;
        asm_lock_mnemonic(parser, name, end);
        asm_prefixed_mnemonic(parser, name, end);
        instruction->rep_scan = asm_string_mnemonic(parser, name, end);
        instruction->written = asm_token_text(parser, parser->at - 1);
        uint8_t string_prefixes[2];
        const uint32_t string_prefix_count = asm_string_operands(parser, name, end, string_prefixes);
        const int32_t predicate = asm_compare_predicate(name);
        uint32_t width = 0;
        uint32_t source_width = 0;
        // A complete alias keeps its last letter: jnb, jnl and sal end in suffix letters.
        bool known = asm_alias(name) || (!parser->options.intel && asm_att_extension(name, &width, &source_width));
        // Whether the parser checks each operand's width against the suffix's: a move's extension (movzbl, both widths),
        // the x87's memory width and LEA's destination. Any other suffix the matcher checks against each form.
        bool checked = width != 0;
        if (!parser->options.intel && !known && name->length > 1)
        {
            // Strip suffixes only when the full mnemonic has no catalogue name, and movq's (mov's 64-bit form) unless an
            // XMM operand makes it SSE2's. A name whose forms the target refuses is still that instruction's.
            const uint32_t bucket = x86_match_bucket(parser->index, *name);
            known = parser->index->names[bucket].pointer != 0 && (!x86_match_name(*name, "movq") || asm_vector_operands(parser, end));
            if (!known)
            {
                bool x87 = false;
                width = asm_att_suffix(parser->index, name, &x87);
                source_width = x87 ? width : 0;
                checked = x87;
                instruction->suffixed = width != 0 && !x87;
                asm_alias(name);
            }
        }
        // LEA's memory operand is an address, not a load: its suffix sizes the destination register alone.
        const bool address = x86_match_name(*name, "lea");
        checked |= address && width != 0;
        instruction->suffixed &= !address;
        // Extension forms describe their source memory width; the destination is fixed by its register class.
        instruction->request.width = address ? 0 : source_width != 0 ? source_width : width;
        instruction->branch = asm_branch_mnemonic(*name);
        uint32_t begins[4];
        uint32_t ends[4];
        uint32_t first = parser->at;
        uint32_t depth = 0;
        for (uint32_t i = first; i <= end && parser->result.error == 0; i += 1)
        {
            const uint16_t kind = parser->tokens[i].kind;
            if (i == end || (kind == ',' && depth == 0))
            {
                if (i > first && instruction->request.count < 4)
                {
                    const uint32_t operand = instruction->request.count++;
                    begins[operand] = first;
                    ends[operand] = i;
                }
                else if (i != first || i != end || first != parser->at)
                {
                    asm_fail(parser, location, "empty operand or too many operands");
                }
                first = i + 1;
            }
            depth += kind == '[' || kind == '(' || kind == '{';
            if (kind == ']' || kind == ')' || kind == '}')
            {
                if (depth == 0)
                {
                    asm_fail(parser, parser->tokens[i].start, "unmatched operand delimiter");
                }
                else
                {
                    depth -= 1;
                }
            }
        }
        if (depth != 0)
        {
            asm_fail(parser, location, "unclosed operand delimiter");
        }
        for (uint32_t i = 0; parser->result.error == 0 && i < instruction->request.count; i += 1)
        {
            parser->at = begins[i];
            asm_operand(parser, ends[i], instruction->branch, &instruction->request.operands[i], &instruction->expressions[i]);
            asm_decorations(parser, ends[i], &instruction->request, i);
            X86Operand* operand = &instruction->request.operands[i];
            const bool shift = x86_match_name(*name, "shl") || x86_match_name(*name, "shr") || x86_match_name(*name, "sar") || x86_match_name(*name, "rol") || x86_match_name(*name, "ror") || x86_match_name(*name, "shld") || x86_match_name(*name, "shrd");
            const bool count_register = !parser->options.intel && i == 0 && shift && operand->kind == X86_OPERAND_REGISTER && operand->class == BUSTER_X86_CLASS_GPR8 && operand->reg == 1;
            if (checked && width != 0 && operand->kind != X86_OPERAND_IMMEDIATE && !count_register && !(address && operand->kind == X86_OPERAND_MEMORY))
            {
                const uint32_t expected_width = i == 0 && source_width != 0 ? source_width : width;
                if (operand->width != 0 && operand->width != expected_width)
                {
                    asm_fail(parser, location, "mnemonic suffix and register width disagree");
                }
                operand->width = expected_width;
            }
            if (parser->at != ends[i])
            {
                asm_fail(parser, parser->tokens[parser->at].start, "unsupported operand syntax");
            }
        }
        // A shift or rotate of one operand shifts it by 1, as GNU as reads `shrl %ecx` (clang's x >> 1): the count written
        // out, which takes D1's form.
        static const char* const shifts[] = { "shl", "shr", "sar", "rol", "ror", "rcl", "rcr" };
        bool shift = false;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(shifts); i += 1)
        {
            shift |= x86_match_name(*name, shifts[i]);
        }
        // A compare-predicate name's predicate is the immediate (asm_compare_predicate): first in AT&T, last in Intel.
        if (parser->result.error == 0 && predicate >= 0)
        {
            const uint32_t count = instruction->request.count;
            if (count >= BUSTER_X86_MATCH_OPERANDS)
            {
                asm_fail(parser, location, "empty operand or too many operands");
            }
            else
            {
                const uint32_t at = parser->options.intel ? count : 0;
                for (uint32_t i = count; i > at; i -= 1)
                {
                    instruction->request.operands[i] = instruction->request.operands[i - 1];
                    instruction->expressions[i] = instruction->expressions[i - 1];
                }
                instruction->request.operands[at] = (X86Operand){ .kind = X86_OPERAND_IMMEDIATE, .value = predicate };
                instruction->expressions[at] = (AsmExpr){ .value = predicate };
                instruction->request.count = count + 1;
            }
        }
        if (parser->result.error == 0 && shift && instruction->request.count == 1 && instruction->request.operands[0].kind != X86_OPERAND_IMMEDIATE)
        {
            const uint32_t count = parser->options.intel ? 1 : 0;
            instruction->request.operands[1] = instruction->request.operands[0];
            instruction->expressions[1] = instruction->expressions[0];
            instruction->request.operands[count] = (X86Operand){ .kind = X86_OPERAND_IMMEDIATE, .value = 1 };
            instruction->expressions[count] = (AsmExpr){ .value = 1 };
            instruction->request.count = 2;
        }
        if (!parser->options.intel)
        {
            for (uint32_t i = 0; i < instruction->request.count / 2; i += 1)
            {
                const uint32_t j = instruction->request.count - 1 - i;
                const X86Operand operand = instruction->request.operands[i];
                const AsmExpr expression = instruction->expressions[i];
                instruction->request.operands[i] = instruction->request.operands[j];
                instruction->expressions[i] = instruction->expressions[j];
                instruction->request.operands[j] = operand;
                instruction->expressions[j] = expression;
            }
        }
        asm_x87_operands(parser, &instruction->request);
        // An exchange of two general registers is commutative: with the accumulator first (Intel's order), its operands
        // swap, so that the accumulator's one-byte form (90+r) can take it, as GNU as encodes `xchg rax, rcx` (#1134).
        X86Operand* exchanged = instruction->request.operands;
        if (x86_match_name(*name, "xchg") && instruction->request.count == 2 && exchanged[0].kind == X86_OPERAND_REGISTER && exchanged[1].kind == X86_OPERAND_REGISTER &&
            exchanged[0].class >= BUSTER_X86_CLASS_GPR16 && exchanged[0].class <= BUSTER_X86_CLASS_GPR64 && exchanged[1].class == exchanged[0].class && exchanged[0].reg == 0 &&
            exchanged[1].reg != 0)
        {
            const X86Operand operand = exchanged[0];
            const AsmExpr expression = instruction->expressions[0];
            exchanged[0] = exchanged[1];
            instruction->expressions[0] = instruction->expressions[1];
            exchanged[1] = operand;
            instruction->expressions[1] = expression;
        }
        // GNU as gives an instruction the row a `.loc` left waiting (#1115), before its prefixes.
        asm_line_instruction(parser, location);
        for (uint32_t i = 0; i < instruction->request.count; i += 1)
        {
            instruction->address32 |= instruction->request.operands[i].kind == X86_OPERAND_MEMORY && instruction->request.operands[i].address_size == 4;
        }
        if (instruction->address32 && parser->code && parser->result.error == 0)
        {
            // Another function's records have no room for the 67: its caller assembles the template whole.
            parser->directive = true;
            asm_fail(parser, location, "inline assembly's 32-bit addresses are its unit's");
        }
        else if (string_prefix_count != 0 && parser->code)
        {
            // Code that another function's records hold has no bytes but its records': its caller assembles it whole.
            parser->directive = true;
            asm_fail(parser, location, "inline assembly's string instruction operands that need a prefix are its unit's");
        }
        else if (string_prefix_count != 0 && parser->result.error == 0)
        {
            const uint32_t prefix = asm_fragment(parser, ASM_BYTES, location);
            uint8_t* bytes = arena_push(parser->arena, string_prefix_count, 1);
            if (bytes == 0)
            {
                asm_fail(parser, location, "arena exhausted");
            }
            else if (parser->result.error == 0)
            {
                memcpy(bytes, string_prefixes, string_prefix_count);
                parser->fragments[prefix].bytes = bytes;
                parser->fragments[prefix].size = string_prefix_count;
            }
        }
        instruction->fragment = asm_fragment(parser, ASM_INSTRUCTION, location);
        parser->fragments[instruction->fragment].instruction = parser->instruction_count - 1;
        parser->sections[parser->section].data = false;
        parser->at = end;
    }
}

// GNU as's data16 and rex64 written as instructions of their own (#1115), before an instruction on their line or alone:
// their bytes, 66 and 48, as GNU as writes them (it warns of a stand-alone data16). The general-dynamic TLS sequence
// is so written, clang's as `data16` / `leaq x@TLSGD(%rip), %rdi` / `data16` / `data16` / `rex64` / `callq
// __tls_get_addr@PLT` and GCC's as `data16 leaq ...`, `.value 0x6666`, `rex64` and `call`: 16 bytes a linker relaxes
// whole. A prefix is code, as an instruction is, for the padding after it. Code that another function's records hold
// (asm_assemble_code) has no bytes but its records', so its caller assembles a template with one whole.
static void asm_prefix_statements(AsmParser* parser, uint32_t end)
{
    while (parser->at < end && parser->result.error == 0 && (asm_is(parser, parser->at, "data16") || asm_is(parser, parser->at, "rex64")))
    {
        const uint32_t location = parser->tokens[parser->at].start;
        if (parser->code)
        {
            parser->directive = true;
            asm_fail(parser, location, "inline assembly's data16 and rex64 statements are its unit's");
        }
        else if (object_class_is_zero_fill(parser->sections[parser->section].class))
        {
            asm_fail(parser, location, "instruction capacity exhausted or instruction in .bss");
        }
        else
        {
            // GNU as reads a data16 or rex64 statement as an instruction, which takes the row a `.loc` left waiting.
            asm_line_instruction(parser, location);
            const uint32_t fragment = asm_fragment(parser, ASM_BYTES, location);
            uint8_t* byte = arena_push(parser->arena, 1, 1);
            if (byte == 0)
            {
                asm_fail(parser, location, "arena exhausted");
            }
            else if (parser->result.error == 0)
            {
                *byte = asm_is(parser, parser->at, "data16") ? 0x66 : 0x48;
                parser->fragments[fragment].bytes = byte;
                parser->fragments[fragment].size = 1;
                parser->sections[parser->section].data = false;
            }
        }
        parser->at += 1;
    }
}

static void asm_string(AsmParser* parser, uint32_t end, bool terminated)
{
    do
    {
        const AsmToken token = parser->tokens[parser->at];
        if (parser->at >= end || token.kind != BUSTER_ASM_STRING)
        {
            asm_fail(parser, token.start, "expected string literal");
            break;
        }
        const uint32_t fragment = asm_fragment(parser, ASM_BYTES, token.start);
        AsmFragment* output = &parser->fragments[fragment];
        output->bytes = arena_push(parser->arena, token.end - token.start, 1);
        if (output->bytes == 0)
        {
            asm_fail(parser, token.start, "arena exhausted");
            break;
        }
        for (uint32_t at = token.start + 1; at + 1 < token.end && parser->result.error == 0; at += 1)
        {
            uint32_t byte = (uint8_t)parser->source.pointer[at];
            if (byte == '\\')
            {
                byte = (uint8_t)parser->source.pointer[++at];
                if (byte == 'n')
                {
                    byte = '\n';
                }
                else if (byte == 'r')
                {
                    byte = '\r';
                }
                else if (byte == 't')
                {
                    byte = '\t';
                }
                else if (byte == 'b')
                {
                    byte = '\b';
                }
                else if (byte == 'f')
                {
                    byte = '\f';
                }
                else if (byte == '\\' || byte == '"')
                {
                }
                else if (byte >= '0' && byte <= '7')
                {
                    byte -= '0';
                    for (uint32_t n = 1; n < 3 && at + 2 < token.end && parser->source.pointer[at + 1] >= '0' && parser->source.pointer[at + 1] <= '7'; n += 1)
                    {
                        byte = byte * 8 + parser->source.pointer[++at] - '0';
                    }
                }
                else
                {
                    asm_fail(parser, at, "unsupported string escape (use octal for arbitrary bytes)");
                }
                if (byte > 255)
                {
                    asm_fail(parser, at, "string escape overflows a byte");
                }
            }
            output->bytes[output->size++] = byte;
        }
        if (terminated)
        {
            output->bytes[output->size++] = 0;
        }
        parser->at += 1;
    } while (parser->result.error == 0 && asm_take(parser, ','));
}

// `.base64 "string"[, "string"...]` (GNU as 2.43's), each string's bytes in base64, which GCC 15 writes for its string
// literals when the assembler takes it: groups of four of A-Z, a-z, 0-9, + and /, the last group ending in one or two
// = for one or two bytes fewer (#1115).
static void asm_base64(AsmParser* parser, uint32_t end)
{
    do
    {
        const AsmToken token = parser->tokens[parser->at];
        if (parser->at >= end || token.kind != BUSTER_ASM_STRING)
        {
            asm_fail(parser, token.start, "expected string literal");
            break;
        }
        const uint32_t length = token.end - token.start - 2;
        const uint8_t* text = (const uint8_t*)parser->source.pointer + token.start + 1;
        const uint32_t fragment = asm_fragment(parser, ASM_BYTES, token.start);
        AsmFragment* output = &parser->fragments[fragment];
        output->bytes = arena_push(parser->arena, length / 4 * 3 + 1, 1);
        if (output->bytes == 0)
        {
            asm_fail(parser, token.start, "arena exhausted");
            break;
        }
        bool valid = length % 4 == 0;
        for (uint32_t group = 0; valid && group < length; group += 4)
        {
            uint32_t bits = 0;
            uint32_t padding = 0;
            for (uint32_t i = 0; valid && i < 4; i += 1)
            {
                const uint8_t byte = text[group + i];
                const uint32_t value = byte >= 'A' && byte <= 'Z' ? byte - 'A' : byte >= 'a' && byte <= 'z' ? byte - 'a' + 26 : byte >= '0' && byte <= '9' ? byte - '0' + 52 :
                                       byte == '+' ? 62 : byte == '/' ? 63 : 64;
                // Padding ends the last group: its last one or two characters.
                valid = value < 64 ? padding == 0 : byte == '=' && group + 4 == length && i >= 2;
                padding += byte == '=';
                bits = bits << 6 | (value < 64 ? value : 0);
            }
            for (uint32_t i = 0; valid && i < 3 - padding; i += 1)
            {
                output->bytes[output->size++] = (uint8_t)(bits >> (16 - 8 * i));
            }
        }
        if (!valid)
        {
            asm_fail(parser, token.start, "invalid base64 string");
        }
        parser->at += 1;
    } while (parser->result.error == 0 && asm_take(parser, ','));
}

static uint32_t asm_note_section(const AsmParser* parser, uint32_t end)
{
    const uint32_t at = parser->at;
    // '-' is an expression token. A section spelling joins it only when the three tokens are adjacent.
    const bool unquoted = at + 2 < end && asm_is(parser, at, ".note.GNU") && parser->tokens[at + 1].kind == '-' && asm_is(parser, at + 2, "stack") && parser->tokens[at].end == parser->tokens[at + 1].start && parser->tokens[at + 1].end == parser->tokens[at + 2].start;
    return unquoted ? 3 : at < end && asm_is(parser, at, "\".note.GNU-stack\"") ? 1 : 0;
}

// `.comm name, size[, alignment]` and `.lcomm name, size` (#1115), as GNU as reads them for ELF. A common symbol (C's
// tentative definitions under -fcommon) is an object of SHN_COMMON, which the linker places: a global OBJECT whose
// alignment is the third operand, or without one the size's power of two up to 16 (in the Link, #147's COMMON symbol
// of its own atom). A local's .comm and every .lcomm are instead an object of .bss, placed after its other contents
// (GNU as's subsection 1) in directive order, aligned to the third operand or, for .lcomm, to the size's power of two
// up to 8 (asm_place_commons). A global's repeated .comm keeps the first size, as GNU as does (it warns of another),
// and takes the last alignment: its third operand, or without one (or 0) the kept size's power of two; one made local
// and declared again joins .bss's, as GNU as places it (below). A local's repeated .comm or .lcomm, or a name defined
// otherwise, is a duplicate definition, as in GNU as, and a weak name is refused. A C unit's assembly
// (AsmOptions.integrated) takes clang's integrated assembler's alignment instead: 1 for a `.comm` without a third
// operand and for every `.lcomm` (#2304).
static void asm_common_directive(AsmParser* parser, uint32_t token, uint32_t end)
{
    const uint32_t location = parser->tokens[token].start;
    const bool lcomm = asm_is(parser, token, ".lcomm");
    if (parser->at >= end || !asm_name_token(parser, parser->at))
    {
        asm_fail(parser, location, "common directive requires a symbol name");
    }
    else
    {
        const String8 name = asm_name_text(parser, parser->at++);
        const uint32_t id = asm_symbol(parser, name, location);
        const uint32_t section = parser->code ? 0 : parser->section_name_slots != 0 ? asm_find_latest_section(parser, name) : asm_find_section(parser, name, 0);
        if (section != 0)
        {
            asm_fail(parser, location, "section name already defined as a symbol");
        }
        asm_expect(parser, ',', "expected ',' after symbol");
        const __int128 size = asm_absolute(parser, end);
        __int128 alignment = 0;
        const bool aligned = !lcomm && asm_take(parser, ',');
        if (aligned)
        {
            alignment = asm_absolute(parser, end);
        }
        AsmSymbol* symbol = &parser->symbols[id];
        const uint8_t binding = symbol->flags & BUSTER_LINK_SYMBOL_BINDING;
        const bool local = lcomm || (symbol->explicit_binding && binding == BUSTER_LINK_SYMBOL_LOCAL);
        if (size < 0 || size > BUSTER_ASM_MAX_SECTION || alignment < 0 || alignment > ((__int128)1 << 28) || (alignment & (alignment - 1)) != 0)
        {
            asm_fail(parser, location, "common symbol size must be at most 256 MiB, its alignment a power of two");
        }
        else if (binding == BUSTER_LINK_SYMBOL_WEAK)
        {
            asm_fail(parser, location, "a common symbol can't be weak");
        }
        else if (parser->result.error == 0)
        {
            const uint8_t kind = local ? ASM_COMMON_BSS : ASM_COMMON_GLOBAL;
            const bool repeated = symbol->common == ASM_COMMON_GLOBAL && kind == ASM_COMMON_GLOBAL;
            // A global common made local and declared again (`.comm x,4,4`, `.local x`, then `.comm x` or `.lcomm x`):
            // GNU as keeps its first size, as for a repeated .comm, and places it in .bss with the new directive's
            // alignment, but at its common size past that place, as the symbol's value is still that size when
            // bss_alloc gives it its frag; the .bss it takes is twice the size (#1115).
            const bool relocal = symbol->common == ASM_COMMON_GLOBAL && kind == ASM_COMMON_BSS;
            const uint32_t bytes = (repeated || relocal) && symbol->common_size != 0 ? (uint32_t)symbol->common_size : (uint32_t)size;
            uint32_t power = 1;
            while (power < bytes && power < 16)
            {
                power <<= 1;
            }
            const bool integrated = parser->options.integrated;
            const uint32_t sized = bytes >= 8 ? 8 : bytes >= 4 ? 4 : bytes >= 2 ? 2 : 1;
            const uint32_t placed = lcomm && !integrated ? sized : alignment != 0 ? (uint32_t)alignment : 1;
            const uint32_t chosen = local ? placed : alignment != 0 ? (uint32_t)alignment : integrated ? 1 : power;
            if ((symbol->common != 0 && !repeated && !relocal) || symbol->defined || symbol->absolute)
            {
                asm_fail(parser, location, "duplicate symbol definition");
            }
            else if (repeated)
            {
                symbol->common_alignment = chosen;
            }
            else
            {
                symbol->common_bias = relocal ? symbol->common_size : 0;
                parser->common_count -= relocal;
                symbol->common = kind;
                symbol->common_size = bytes;
                symbol->common_alignment = chosen;
                symbol->flags = (uint8_t)((symbol->flags & ~BUSTER_LINK_SYMBOL_TYPE) | BUSTER_LINK_SYMBOL_OBJECT);
                symbol->size = (AsmExpr){ .value = bytes };
                symbol->has_size = true;
                parser->common_count += kind == ASM_COMMON_GLOBAL;
                if (kind == ASM_COMMON_BSS)
                {
                    if (parser->common_last != 0)
                    {
                        parser->symbols[parser->common_last].common_next = id;
                    }
                    else
                    {
                        parser->common_first = id;
                    }
                    parser->common_last = id;
                }
            }
        }
    }
}

// The .bss objects of .lcomm and the locals' .comm, after the source's own .bss contents (asm_common_directive).
static void asm_place_commons(AsmParser* parser)
{
    const uint32_t current = parser->section;
    parser->section = BUSTER_LINK_CLASS_BSS;
    for (uint32_t id = parser->common_first; id != 0 && parser->result.error == 0; id = parser->symbols[id].common_next)
    {
        AsmSymbol* symbol = &parser->symbols[id];
        if (symbol->defined)
        {
            asm_fail(parser, symbol->location, "duplicate symbol definition");
        }
        const uint32_t align = asm_fragment(parser, ASM_ALIGN, symbol->location);
        const uint32_t bias = symbol->common_bias != 0 ? asm_fragment(parser, ASM_FILL, symbol->location) : 0;
        const uint32_t label = asm_fragment(parser, ASM_LABEL, symbol->location);
        const uint32_t fill = asm_fragment(parser, ASM_FILL, symbol->location);
        if (parser->result.error == 0)
        {
            parser->fragments[align].alignment = symbol->common_alignment;
            parser->fragments[align].maximum = BUSTER_ASM_MAX_SECTION;
            parser->fragments[align].padding = ASM_PADDING_FILL;
            if (bias != 0)
            {
                parser->fragments[bias].size = (uint32_t)symbol->common_bias;
                parser->fragments[bias].padding = ASM_PADDING_FILL;
            }
            parser->fragments[fill].size = (uint32_t)symbol->common_size;
            parser->fragments[fill].padding = ASM_PADDING_FILL;
            symbol->defined = true;
            symbol->section_start = false;
            symbol->fragment = label;
            const uint32_t log = (uint32_t)__builtin_ctz(symbol->common_alignment);
            parser->sections[BUSTER_LINK_CLASS_BSS].alignment = log > parser->sections[BUSTER_LINK_CLASS_BSS].alignment ? (uint8_t)log : parser->sections[BUSTER_LINK_CLASS_BSS].alignment;
        }
    }
    parser->section = current;
}

static void asm_alias_expression(const AsmParser* parser, AsmExpr* expression)
{
    if (expression->plus != 0 && parser->symbols[expression->plus].alias)
    {
        const AsmExpr target = parser->symbols[expression->plus].alias_target;
        expression->plus = target.plus;
        expression->value += target.value;
    }
    if (expression->minus != 0 && parser->symbols[expression->minus].alias)
    {
        const AsmExpr target = parser->symbols[expression->minus].alias_target;
        expression->minus = target.plus;
        expression->value -= target.value;
    }
}

// GNU as's copy_symbol_attributes for `.set alias, target`: the alias takes the target's type and size, where it has
// none of its own, when the target is defined (its type and size then: a later `.type` or `.size` of the target isn't
// copied), or once a target defined later is (all of them). GCC writes `.weak alias` and `.set alias, original` for a
// weak alias of a function, which GNU as makes FUNC with the function's size; an `.lcomm` object's alias is OBJECT. A
// later `.type` or `.size` of the alias replaces them. A global common isn't a target: GNU as refuses one.
static void asm_alias_inherit(AsmSymbol* symbol, const AsmSymbol* target)
{
    if ((symbol->flags & BUSTER_LINK_SYMBOL_TYPE) == 0)
    {
        symbol->flags = (uint8_t)(symbol->flags | (target->flags & BUSTER_LINK_SYMBOL_TYPE));
    }
    if (!symbol->has_size && target->has_size)
    {
        symbol->size = target->size;
        symbol->has_size = true;
    }
    symbol->inherits = false;
}

// The aliases, once the source has defined every symbol (#1115), an alias of an alias by its target's target. The alias
// of a label the source defines (`.set .LC4, .LC11+4`, GCC's constant pool entry within another) is a symbol at the
// label's place plus the constant, as GNU as makes it. Each reference to an alias of another symbol (an undefined one)
// becomes one to that symbol plus the constant, in its instructions' operands, data values and sizes, and the alias is
// in no table. A target that is a constant (one defined later) stays a forward constant, which is refused.
static void asm_resolve_aliases(AsmParser* parser)
{
    for (uint32_t i = 1; parser->alias_count != 0 && i < parser->symbol_count && parser->result.error == 0; i += 1)
    {
        AsmSymbol* symbol = &parser->symbols[i];
        for (uint32_t step = 0; symbol->alias && parser->symbols[symbol->alias_target.plus].alias && step <= parser->alias_count; step += 1)
        {
            const AsmExpr next = parser->symbols[symbol->alias_target.plus].alias_target;
            symbol->alias_target.plus = next.plus;
            symbol->alias_target.value += next.value;
        }
        AsmSymbol* target = &parser->symbols[symbol->alias_target.plus];
        // A TLS modifier on the alias is one on its target, which an undefined one's references become.
        target->tls |= symbol->alias && symbol->tls && !target->defined;
        if (symbol->alias && target->alias)
        {
            asm_fail(parser, symbol->location, "circular symbol alias");
        }
        else if (symbol->alias && target->absolute)
        {
            asm_fail(parser, symbol->location, "absolute expression required here (forward constants are unsupported)");
        }
        else if (symbol->alias && target->common == ASM_COMMON_GLOBAL)
        {
            // GNU as refuses it too, whichever comes first (#1115): a common symbol has no place yet to be one of.
            asm_fail(parser, symbol->location, "a symbol can't be equated to a common symbol");
        }
        else if (symbol->alias && target->defined && !target->section_start && target->offset + symbol->alias_target.value >= INT32_MIN &&
                 target->offset + symbol->alias_target.value <= INT32_MAX)
        {
            // A defined label's alias is a symbol at its place plus the constant, as GNU as makes `.LC4` of `.set .LC4,
            // .LC11+4` (references to it stay its own: a merged section's relocation needs the symbol).
            symbol->fragment = target->fragment;
            symbol->offset = target->offset + (int64_t)symbol->alias_target.value;
            symbol->alias = false;
            parser->alias_count -= 1;
            if (symbol->inherits)
            {
                asm_alias_inherit(symbol, target);
            }
        }
        else if (symbol->alias)
        {
            // An alias of an undefined symbol, exported or not, is in no table, as GNU as leaves it out (its binding,
            // type and size with it): references to it are the target's, and the target is listed, a weak one too,
            // which an alias uses (asm_object_options).
            target->used = true;
        }
    }
    for (uint32_t i = 0; parser->alias_count != 0 && i < parser->instruction_count; i += 1)
    {
        for (uint32_t j = 0; j < BUSTER_ARRAY_LENGTH(parser->instructions[i].expressions); j += 1)
        {
            asm_alias_expression(parser, &parser->instructions[i].expressions[j]);
        }
    }
    for (uint32_t i = 0; parser->alias_count != 0 && i < parser->fragment_count; i += 1)
    {
        asm_alias_expression(parser, &parser->fragments[i].expression);
    }
    for (uint32_t i = 1; parser->alias_count != 0 && i < parser->symbol_count; i += 1)
    {
        asm_alias_expression(parser, &parser->symbols[i].size);
    }
}

#include "assembler_sections.c"
#include "assembler_cfi.c"
#include "assembler_lines.c"

static void asm_directive(AsmParser* parser, uint32_t end)
{
    const uint32_t token = parser->at++;
    const uint32_t location = parser->tokens[token].start;
    if (parser->code && !asm_is(parser, token, ".intel_syntax") && !asm_is(parser, token, ".att_syntax"))
    {
        // Code that another function's records hold (asm_assemble_code) has no sections, data or symbols of its own: the
        // caller assembles the unit whole (asm_assemble) when this says a template has such a directive.
        parser->directive = true;
        asm_fail(parser, location, "inline assembly takes no directive but .intel_syntax and .att_syntax");
    }
    else if (asm_is(parser, token, ".intel_syntax") || asm_is(parser, token, ".att_syntax"))
    {
        parser->options.intel = asm_is(parser, token, ".intel_syntax");
        parser->prefix = true;
        if (parser->at < end)
        {
            if (!asm_is(parser, parser->at, "prefix") && !asm_is(parser, parser->at, "noprefix"))
            {
                asm_fail(parser, location, "syntax directive expects prefix or noprefix");
            }
            parser->prefix = !asm_is(parser, parser->at++, "noprefix");
        }
    }
    else if (asm_section_prefix(asm_token_text(parser, token), ".cfi_"))
    {
        asm_cfi_directive(parser, token, end);
    }
    else if (asm_is(parser, token, ".loc"))
    {
        // A line row of GNU as's line table, and GCC's view numbers (#1115, assembler_lines.c).
        asm_line_loc(parser, location, end);
    }
    else if (asm_is(parser, token, ".prefalign"))
    {
        // LLVM's preferred-alignment hint doesn't change the code this object writer records. GNU as 2.42 doesn't
        // accept .prefalign either; taking the hint as no alignment still assembles compiler output.
        parser->at = end;
    }
    else if (asm_is(parser, token, ".file") || asm_is(parser, token, ".ident"))
    {
        const bool numbered = asm_is(parser, token, ".file") && parser->at < end && parser->tokens[parser->at].kind == BUSTER_ASM_WORD &&
                              asm_token_text(parser, parser->at).pointer[0] >= '0' && asm_token_text(parser, parser->at).pointer[0] <= '9';
        if (numbered)
        {
            // `.file N ["directory"] "name" [md5 0x...]`: the line table's file (#1115, assembler_lines.c).
            asm_line_file(parser, location, end);
        }
        else if (parser->at >= end || parser->tokens[parser->at].kind != BUSTER_ASM_STRING)
        {
            asm_fail(parser, location, "expected a quoted metadata string");
        }
        else
        {
            const String8 text = asm_name_text(parser, parser->at++);
            if (asm_is(parser, token, ".file"))
            {
                parser->result.source_name = text;
            }
        }
    }
    else if (asm_is(parser, token, ".addrsig") || asm_is(parser, token, ".addrsig_sym"))
    {
        // Omitting LLVM's optional address-significance table conservatively marks every address significant.
        if (asm_is(parser, token, ".addrsig_sym"))
        {
            if (parser->at >= end || !asm_name_token(parser, parser->at))
            {
                asm_fail(parser, location, "expected an address-significant symbol name");
            }
            else
            {
                parser->at += 1;
            }
        }
    }
    else if ((asm_is(parser, token, ".section") || asm_is(parser, token, ".pushsection")) && asm_note_section(parser, end))
    {
        // The shared writer already supplies the empty non-executable-stack marker.
        if (asm_is(parser, token, ".pushsection"))
        {
            if (parser->section_stack == 0 || parser->section_depth >= parser->section_capacity)
            {
                asm_fail(parser, location, "section stack exhausted");
            }
            else
            {
                parser->section_stack[parser->section_depth++] = parser->section;
            }
        }
        parser->section = BUSTER_LINK_CLASS_NONE;
        parser->at += asm_note_section(parser, end);
        if (asm_take(parser, ','))
        {
            if (!asm_is(parser, parser->at++, "\"\""))
            {
                asm_fail(parser, location, "only a non-executable GNU-stack note is supported");
            }
            if (asm_take(parser, ','))
            {
                asm_expect(parser, '@', "expected section type");
                if (!asm_is(parser, parser->at++, "progbits"))
                {
                    asm_fail(parser, location, "GNU-stack note requires @progbits");
                }
            }
        }
    }
    else if (asm_is(parser, token, ".pushsection"))
    {
        if (parser->section_stack == 0 || parser->section_depth >= parser->section_capacity)
        {
            asm_fail(parser, location, "section stack exhausted");
        }
        else
        {
            parser->section_stack[parser->section_depth++] = parser->section;
            asm_section_directive(parser, token, end);
        }
    }
    else if (asm_is(parser, token, ".popsection"))
    {
        if (parser->section_depth == 0)
        {
            asm_fail(parser, location, "section stack is empty");
        }
        else
        {
            parser->previous_section = parser->section;
            parser->section = parser->section_stack[--parser->section_depth];
        }
    }
    else if (asm_is(parser, token, ".previous"))
    {
        const uint32_t current = parser->section;
        parser->section = parser->previous_section;
        parser->previous_section = current;
    }
    else if (asm_is(parser, token, ".text") || asm_is(parser, token, ".data") || asm_is(parser, token, ".bss") || asm_is(parser, token, ".section"))
    {
        asm_section_directive(parser, token, end);
    }
    else if (asm_is(parser, token, ".globl") || asm_is(parser, token, ".global") || asm_is(parser, token, ".local") || asm_is(parser, token, ".weak"))
    {
        const uint8_t binding = asm_is(parser, token, ".local") ? BUSTER_LINK_SYMBOL_LOCAL : asm_is(parser, token, ".weak") ? BUSTER_LINK_SYMBOL_WEAK
                                                                                                                            : BUSTER_LINK_SYMBOL_GLOBAL;
        do
        {
            if (parser->at >= end || !asm_name_token(parser, parser->at))
            {
                asm_fail(parser, location, "binding directive requires a symbol name");
                break;
            }
            const String8 name = asm_name_text(parser, parser->at++);
            AsmSymbol* symbol = &parser->symbols[asm_symbol(parser, name, location)];
            asm_section_attribute(parser, symbol, name, location);
            symbol->flags = (symbol->flags & ~BUSTER_LINK_SYMBOL_BINDING) | binding;
            symbol->explicit_binding = true;
        } while (asm_take(parser, ','));
    }
    else if (asm_is(parser, token, ".hidden") || asm_is(parser, token, ".protected") || asm_is(parser, token, ".internal"))
    {
        const uint8_t visibility = asm_is(parser, token, ".hidden") ? BUSTER_LINK_SYMBOL_HIDDEN
                                  : asm_is(parser, token, ".internal") ? BUSTER_LINK_SYMBOL_INTERNAL
                                                                       : BUSTER_LINK_SYMBOL_PROTECTED;
        do
        {
            if (parser->at >= end || !asm_name_token(parser, parser->at))
            {
                asm_fail(parser, location, "visibility directive requires a symbol name");
                break;
            }
            const String8 name = asm_name_text(parser, parser->at++);
            AsmSymbol* symbol = &parser->symbols[asm_symbol(parser, name, location)];
            asm_section_attribute(parser, symbol, name, location);
            symbol->flags = (symbol->flags & ~BUSTER_LINK_SYMBOL_VISIBILITY) | visibility;
        } while (asm_take(parser, ','));
    }
    else if (asm_is(parser, token, ".type") || asm_is(parser, token, ".size") || asm_is(parser, token, ".equ") || asm_is(parser, token, ".equiv") || asm_is(parser, token, ".set"))
    {
        if (parser->at >= end || !asm_name_token(parser, parser->at))
        {
            asm_fail(parser, location, "directive requires a symbol name");
        }
        else
        {
            const String8 name = asm_name_text(parser, parser->at++);
            AsmSymbol* symbol = &parser->symbols[asm_symbol(parser, name, location)];
            if (asm_is(parser, token, ".type") || asm_is(parser, token, ".size"))
            {
                asm_section_attribute(parser, symbol, name, location);
            }
            const uint32_t section = parser->code ? 0 : parser->section_name_slots != 0 ? asm_find_latest_section(parser, name) : asm_find_section(parser, name, 0);
            if (section != 0 && (asm_is(parser, token, ".equ") || asm_is(parser, token, ".equiv") || asm_is(parser, token, ".set")))
            {
                asm_fail(parser, location, "section name already defined as a symbol");
            }
            asm_expect(parser, ',', "expected ',' after symbol");
            if (asm_is(parser, token, ".type"))
            {
                if (!asm_take(parser, '@') && !asm_take(parser, '%'))
                {
                    asm_fail(parser, location, "expected '@' or '%' before symbol type");
                }
                uint8_t type = BUSTER_LINK_SYMBOL_NOTYPE;
                if (asm_is(parser, parser->at, "function"))
                {
                    type = BUSTER_LINK_SYMBOL_FUNCTION;
                }
                else if (asm_is(parser, parser->at, "object"))
                {
                    type = BUSTER_LINK_SYMBOL_OBJECT;
                }
                else if (asm_is(parser, parser->at, "tls_object"))
                {
                    type = BUSTER_LINK_SYMBOL_TLS;
                }
                else if (!asm_is(parser, parser->at, "notype"))
                {
                    asm_fail(parser, location, "unsupported symbol type");
                }
                symbol->flags = (symbol->flags & ~BUSTER_LINK_SYMBOL_TYPE) | type;
                parser->at += 1;
            }
            else if (asm_is(parser, token, ".size"))
            {
                symbol->size = asm_expression(parser, end);
                symbol->has_size = true;
            }
            else
            {
                const uint32_t value_location = parser->tokens[parser->at].start;
                const AsmExpr value = asm_expression(parser, end);
                const bool absolute = value.plus == 0 && value.minus == 0 && value.relocation == ASM_RELOCATION_NONE;
                if (absolute && symbol->defined && (!symbol->absolute || asm_is(parser, token, ".equiv")))
                {
                    asm_fail(parser, location, "duplicate symbol definition");
                }
                else if (absolute)
                {
                    symbol->constant = value.value;
                    symbol->absolute = true;
                    symbol->defined = true;
                    symbol->section_start = false;
                }
                else if (value.plus == 0 || value.minus != 0 || value.relocation != ASM_RELOCATION_NONE)
                {
                    asm_fail(parser, value_location, "absolute expression required here (forward constants are unsupported)");
                }
                else if (symbol->defined)
                {
                    asm_fail(parser, location, "duplicate symbol definition");
                }
                else
                {
                    // An alias of a symbol plus a constant, which may come later (asm_resolve_aliases): GCC's constant
                    // pools write one entry within another so (#1115: `.set .LC4, .LC11+4`).
                    const AsmSymbol* target = &parser->symbols[value.plus];
                    const bool placed = target->defined || target->common == ASM_COMMON_BSS;
                    symbol->alias = true;
                    symbol->alias_target = value;
                    symbol->defined = true;
                    symbol->section_start = false;
                    symbol->inherits = !placed;
                    parser->alias_count += 1;
                    if (placed)
                    {
                        asm_alias_inherit(symbol, target);
                    }
                }
            }
        }
    }
    else if (asm_is(parser, token, ".byte") || asm_is(parser, token, ".word") || asm_is(parser, token, ".short") || asm_is(parser, token, ".value") || asm_is(parser, token, ".long") ||
             asm_is(parser, token, ".int") || asm_is(parser, token, ".quad") || asm_is(parser, token, ".uleb128") || asm_is(parser, token, ".sleb128"))
    {
        const bool leb = asm_is(parser, token, ".uleb128") || asm_is(parser, token, ".sleb128");
        const uint32_t width = asm_is(parser, token, ".byte") ? 1 : asm_is(parser, token, ".quad") ? 8 : asm_is(parser, token, ".long") || asm_is(parser, token, ".int") ? 4 : 2;
        do
        {
            // `.` is the address of this datum, so it is bound before the datum takes its place.
            const AsmExpr expression = asm_expression(parser, end);
            const uint32_t fragment = asm_fragment(parser, leb ? (asm_is(parser, token, ".uleb128") ? ASM_ULEB : ASM_SLEB) : ASM_VALUE, location);
            if (fragment != 0 || parser->result.error == 0)
            {
                parser->fragments[fragment].expression = expression;
                parser->fragments[fragment].size = leb ? 1 : width;
            }
        } while (parser->result.error == 0 && asm_take(parser, ','));
        parser->sections[parser->section].data = true;
    }
    else if (asm_is(parser, token, ".ascii") || asm_is(parser, token, ".asciz") || asm_is(parser, token, ".string"))
    {
        asm_string(parser, end, !asm_is(parser, token, ".ascii"));
        parser->sections[parser->section].data = true;
    }
    else if (asm_is(parser, token, ".base64"))
    {
        asm_base64(parser, end);
        parser->sections[parser->section].data = true;
    }
    else if (asm_is(parser, token, ".zero") || asm_is(parser, token, ".skip") || asm_is(parser, token, ".space") || asm_is(parser, token, ".nops") ||
             asm_is(parser, token, ".align") || asm_is(parser, token, ".balign") || asm_is(parser, token, ".p2align"))
    {
        const bool nops = asm_is(parser, token, ".nops");
        // GNU as's .zero is its .space, of an optional fill too: clang writes `.zero 16,63` for a vector of 63s (#1115).
        const bool skip = asm_is(parser, token, ".skip") || asm_is(parser, token, ".space") || asm_is(parser, token, ".zero");
        const bool align = !skip && !nops;
        __int128 value = asm_absolute(parser, end);
        if (asm_is(parser, token, ".p2align"))
        {
            if (value < 0 || value > 28)
            {
                asm_fail(parser, location, "alignment exponent must be in [0, 28]");
                value = 0;
            }
            value = (__int128)1 << (uint32_t)value;
        }
        if (value < 0 || value > BUSTER_ASM_MAX_SECTION || (align && (value == 0 || ((uint64_t)value & ((uint64_t)value - 1)))))
        {
            asm_fail(parser, location, "invalid size or alignment (alignment must be a power of two)");
        }
        const uint32_t fragment = asm_fragment(parser, align ? ASM_ALIGN : ASM_FILL, location);
        AsmFragment* output = &parser->fragments[fragment];
        output->size = align ? 0 : (uint32_t)value;
        output->alignment = align ? (uint32_t)value : 0;
        output->maximum = BUSTER_ASM_MAX_SECTION;
        output->fill = 0;
        output->padding = nops ? ASM_PADDING_NOPS :
                          skip || !align || (parser->sections[parser->section].object.flags & SHF_EXECINSTR) == 0 ? ASM_PADDING_FILL :
                          parser->sections[parser->section].data ? ASM_PADDING_NOPS_AFTER_DATA :
                                                                   ASM_PADDING_NOPS;
        if ((align || skip || nops) && asm_take(parser, ','))
        {
            if (nops)
            {
                // .nops's optional second argument bounds the length of each NOP it writes, which this writer's table
                // doesn't need; it takes the value as GNU as does, without changing the bytes' padding rule.
                if (parser->tokens[parser->at].kind != ',')
                {
                    const __int128 control = asm_absolute(parser, end);
                    if (control < 0 || control > 255)
                    {
                        asm_fail(parser, location, "NOP control must fit in a byte");
                    }
                }
            }
            else if (parser->tokens[parser->at].kind != ',')
            {
                const __int128 fill = asm_absolute(parser, end);
                if (fill < -128 || fill > 255)
                {
                    asm_fail(parser, location, "fill must fit in a byte");
                }
                output->fill = (uint8_t)fill;
                // As in GNU as, an alignment's code is padded with NOPs for a fill of 0x90 too, and with any other fill
                // byte as given; .skip and .space write the fill byte itself.
                output->padding = align && output->fill != 0x90 ? ASM_PADDING_FILL : output->padding;
            }
            if (align && asm_take(parser, ','))
            {
                const __int128 maximum = asm_absolute(parser, end);
                if (maximum < 0 || maximum > BUSTER_ASM_MAX_SECTION)
                {
                    asm_fail(parser, location, "invalid alignment maximum");
                }
                // A maximum of 0 is no maximum, as in GNU as.
                output->maximum = maximum == 0 ? BUSTER_ASM_MAX_SECTION : (uint32_t)maximum;
            }
        }
        parser->sections[parser->section].data |= !align && !nops;
        if (align && value > 0)
        {
            const uint32_t log = __builtin_ctzll((uint64_t)value);
            if (log > parser->sections[parser->section].alignment)
            {
                parser->sections[parser->section].alignment = log;
            }
        }
    }
    else if (asm_is(parser, token, ".comm") || asm_is(parser, token, ".lcomm"))
    {
        asm_common_directive(parser, token, end);
    }
    else
    {
        asm_fail(parser, location, "unsupported assembler directive");
    }
    if (parser->at != end)
    {
        asm_fail(parser, parser->tokens[parser->at <= parser->token_count ? parser->at : parser->token_count].start, "unexpected directive argument");
    }
}

// Eight bounded spellings in lanes. Register descriptions come from the token cache. Decimal conversion reduces
// digit pairs and quartets, then combines eight digits without an eight-step multiply dependency. Uncommon
// spellings (long names, hexadecimal, expressions, addresses/decorations) go to the exact grammar.
static void asm_simple_words_vector(const uint64_t* keys, const uint64_t* lengths, uint32_t count, uint64_t* descriptors, uint64_t* values)
{
    for (uint32_t first = 0; first < count; first += 8)
    {
        const uint32_t left = count - first;
        const __mmask8 valid = left >= 8 ? 255 : (1u << left) - 1;
        const __m512i words = _mm512_maskz_loadu_epi64(valid, keys + first);
        const __m512i sizes = _mm512_maskz_loadu_epi64(valid, lengths + first);
        __m512i descriptions = _mm512_maskz_loadu_epi64(valid, descriptors + first);
        // RIP is valid in an address, but never as a standalone register operand. Keep its exact diagnostic and
        // source-order error handling in asm_operand instead of accepting the cached spelling into this batch.
        const __m512i class_mask = _mm512_set1_epi64(255u << 16);
        const __m512i classes = _mm512_and_si512(descriptions, class_mask);
        descriptions = _mm512_maskz_mov_epi64(_mm512_cmpneq_epi64_mask(classes, class_mask), descriptions);
        __mmask8 decimal = valid & _mm512_cmpgt_epi64_mask(sizes, _mm512_setzero_si512());
        const __m512i shift = _mm512_slli_epi64(_mm512_sub_epi64(_mm512_set1_epi64(8), sizes), 3);
        // Move the spelling to the high end of its qword; shifting supplies zero-valued leading digits and
        // discards every byte past the bounded spelling. Byte subtraction never borrows between digits.
        const __m512i digits = _mm512_sllv_epi64(_mm512_sub_epi8(words, _mm512_set1_epi8('0')), shift);
        decimal &= _mm512_cmpeq_epi64_mask(digits, _mm512_min_epu8(digits, _mm512_set1_epi8(9)));
        const __m512i pairs = _mm512_maddubs_epi16(digits, _mm512_set1_epi16(0x010a));
        const __m512i quartets = _mm512_madd_epi16(pairs, _mm512_set1_epi32(0x00010064));
        const __m512i numbers = _mm512_add_epi64(_mm512_mul_epu32(quartets, _mm512_set1_epi64(10000)), _mm512_srli_epi64(quartets, 32));
        // Leading-zero integers use GAS's octal rule and take the grammar instead of this decimal kernel.
        decimal &= ~(_mm512_cmpgt_epi64_mask(sizes, _mm512_set1_epi64(1)) & _mm512_cmpeq_epi64_mask(_mm512_and_epi64(words, _mm512_set1_epi64(255)), _mm512_set1_epi64('0')));
        descriptions = _mm512_mask_mov_epi64(descriptions, decimal, _mm512_set1_epi64(X86_OPERAND_IMMEDIATE));
        _mm512_mask_storeu_epi64(descriptors + first, valid, descriptions);
        _mm512_mask_storeu_epi64(values + first, valid, numbers);
    }
}

// Common Intel statements are classified in batches rather than calling the scalar operand grammar for every
// register and integer. The first uncommon row is retried by asm_instruction; no source is consumed speculatively.
// State-changing directives and labels delimit batches. Both paths produce exactly the same request/fragment stream.
static bool asm_parse_vector_batch(AsmParser* parser)
{
    uint32_t row_begin[16];
    uint32_t row_end[16];
    uint32_t operand_begin[17] = {0};
    uint64_t keys[64] = {0};
    uint64_t lengths[64] = {0};
    uint64_t descriptors[64];
    uint64_t values[64];
    bool negative[64] = {0};
    uint32_t rows = 0;
    uint32_t words = 0;
    uint32_t at = parser->at;
    bool available = parser->options.intel && !parser->prefix;
    while (available && rows < 16 && at < parser->token_count)
    {
        const uint32_t first = at;
        if (parser->tokens[at].kind != BUSTER_ASM_WORD || asm_token_text(parser, at).pointer[0] == '.')
        {
            break;
        }
        row_begin[rows] = at++;
        const uint32_t saved_words = words;
        uint32_t operands = 0;
        while (available && at < parser->token_count && parser->tokens[at].kind != '\n')
        {
            if (operands != 0)
            {
                available = parser->tokens[at++].kind == ',';
            }
            if (available && at < parser->token_count && operands < 4)
            {
                const bool sign = parser->tokens[at].kind == '-';
                at += sign;
                const String8 text = asm_token_text(parser, at);
                available = parser->tokens[at].kind == BUSTER_ASM_WORD && text.length <= 8;
                if (available)
                {
                    memcpy(&keys[words], text.pointer, text.length);
                    lengths[words] = text.length;
                    descriptors[words] = parser->tokens[at].reserved & ~BUSTER_ASM_REGISTER_CACHED;
                    negative[words++] = sign;
                    at += 1;
                    operands += 1;
                }
            }
            else
            {
                available = false;
            }
        }
        if (available)
        {
            row_end[rows] = at;
            operand_begin[++rows] = words;
            at += at < parser->token_count;
        }
        else
        {
            words = saved_words;
            at = first;
        }
    }
    if (rows != 0)
    {
        asm_simple_words_vector(keys, lengths, words, descriptors, values);
    }
    uint32_t done = 0;
    for (uint32_t row = 0; row < rows; row += 1)
    {
        bool supported = true;
        for (uint32_t word = operand_begin[row]; word < operand_begin[row + 1]; word += 1)
        {
            supported &= descriptors[word] != 0 && (!negative[word] || (descriptors[word] & 255) == X86_OPERAND_IMMEDIATE);
        }
        const String8 mnemonic = asm_token_text(parser, row_begin[row]);
        // Canonical names only: aliases, branch symbols, width suffixes and other grammar keep their exact path, and so
        // do the exchange, whose operands asm_instruction may swap, and the shifts and rotates, one operand of which
        // shifts by 1 there (#1134).
        const uint32_t bucket = x86_match_bucket(parser->index, mnemonic);
        const bool named = parser->index->names[bucket].pointer != 0;
        static const char* const exact[] = { "sal", "xchg", "shl", "shr", "sar", "rol", "ror", "rcl", "rcr" };
        bool grammar = false;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(exact); i += 1)
        {
            grammar |= x86_match_name(mnemonic, exact[i]);
        }
        supported &= named && !grammar;
        if (!supported || parser->instruction_count >= parser->instruction_capacity || object_class_is_zero_fill(parser->sections[parser->section].class))
        {
            break;
        }
        const uint32_t location = parser->tokens[row_begin[row]].start;
        AsmInstruction* instruction = &parser->instructions[parser->instruction_count++];
        *instruction = (AsmInstruction){.location = location};
        instruction->request.mnemonic = mnemonic;
        instruction->written = mnemonic;
        instruction->request.count = operand_begin[row + 1] - operand_begin[row];
        for (uint32_t word = operand_begin[row]; word < operand_begin[row + 1]; word += 1)
        {
            const uint32_t operand = word - operand_begin[row];
            const uint64_t descriptor = descriptors[word];
            const bool immediate = (descriptor & 255) == X86_OPERAND_IMMEDIATE;
            const __int128 value = immediate ? negative[word] ? -(__int128)values[word] : values[word] : 0;
            instruction->request.operands[operand] = (X86Operand){
                .value = value,
                .width = (descriptor >> 24) & 255,
                .kind = descriptor & 255,
                .class = (descriptor >> 16) & 255,
                .reg = (descriptor >> 8) & 255,
            };
            instruction->expressions[operand].value = value;
        }
        // The x87's operands GNU as leaves out (fxch alone, fadd st), as asm_instruction gives them.
        asm_x87_operands(parser, &instruction->request);
        // GNU as gives an instruction the row a `.loc` left waiting (#1115).
        asm_line_instruction(parser, location);
        instruction->fragment = asm_fragment(parser, ASM_INSTRUCTION, location);
        parser->fragments[instruction->fragment].instruction = parser->instruction_count - 1;
        parser->sections[parser->section].data = false;
        parser->at = row_end[row];
        parser->at += parser->at < parser->token_count;
        done += 1;
    }
    return done != 0;
}

static void asm_parse(AsmParser* parser)
{
    while (parser->at < parser->token_count && parser->result.error == 0)
    {
        if (BUSTER_VECTOR_PATH(parser->options.vectorized) && parser->options.intel && !parser->prefix && asm_parse_vector_batch(parser))
        {
            continue;
        }
        if (asm_take(parser, '\n'))
        {
            continue;
        }
        while (parser->at + 1 < parser->token_count && asm_name_token(parser, parser->at) && parser->tokens[parser->at + 1].kind == ':')
        {
            const uint32_t location = parser->tokens[parser->at].start;
            const String8 name = asm_name_text(parser, parser->at);
            uint32_t id;
            if (parser->tokens[parser->at].kind == BUSTER_ASM_WORD && name.length == 1 && name.pointer[0] >= '0' && name.pointer[0] <= '9')
            {
                id = asm_numeric(parser, name.pointer[0] - '0', false, true, location);
            }
            else
            {
                id = asm_symbol(parser, name, location);
                const uint32_t section = parser->code ? 0 : parser->section_name_slots != 0 ? asm_find_latest_section(parser, name) : asm_find_section(parser, name, 0);
                if (section != 0)
                {
                    asm_fail(parser, location, "section name already defined as a symbol");
                }
                if ((name.pointer[0] >= '0' && name.pointer[0] <= '9') || (name.length == 1 && name.pointer[0] == '.'))
                {
                    asm_fail(parser, location, "invalid label (numeric labels are 0 through 9)");
                }
            }
            if (parser->symbols[id].defined)
            {
                asm_fail(parser, location, "duplicate label definition");
            }
            parser->symbols[id].defined = true;
            parser->symbols[id].section_start = false;
            parser->symbols[id].fragment = asm_fragment(parser, ASM_LABEL, location);
            parser->at += 2;
        }
        uint32_t end = parser->at;
        while (end < parser->token_count && parser->tokens[end].kind != '\n')
        {
            end += 1;
        }
        // GNU as applies a prefix written as a statement of its own to the instruction after it, past a semicolon or
        // lines: `lock ; cmpxchg %ecx, (%rdx)` (musl's atomics), `rep; bsr` (lzcnt, as dr_flac spells it for
        // assemblers that don't know it) and the spin hint `rep; nop`. That statement is then the instruction with its
        // prefix, as written on one line (asm_lock_mnemonic, asm_string_mnemonic): the prefix's token takes the place of
        // the last separator before the instruction. Its bytes are the line's, which before a 16-bit instruction put the
        // 66 first, where GNU as writes the prefix it emits alone first: the same instruction (docs/assembler.md). Only
        // an instruction takes it, with no label between, which would mark the prefix's place apart from the
        // instruction's.
        static const char* const prefixes[] = { "lock", "rep", "repe", "repz", "repne", "repnz" };
        bool prefix = false;
        for (uint32_t i = 0; end == parser->at + 1 && i < BUSTER_ARRAY_LENGTH(prefixes); i += 1)
        {
            prefix |= asm_is(parser, parser->at, prefixes[i]);
        }
        if (prefix && parser->result.error == 0)
        {
            uint32_t next = end;
            while (next < parser->token_count && parser->tokens[next].kind == '\n')
            {
                next += 1;
            }
            const bool instruction = next < parser->token_count && parser->tokens[next].kind == BUSTER_ASM_WORD && asm_token_text(parser, next).pointer[0] != '.' &&
                                     parser->tokens[next + 1].kind != ':';
            if (!instruction)
            {
                asm_fail(parser, parser->tokens[parser->at].start, "a prefix needs an instruction after it, with no label between");
            }
            else
            {
                parser->tokens[next - 1] = parser->tokens[parser->at];
                parser->at = next - 1;
                end = next;
                while (end < parser->token_count && parser->tokens[end].kind != '\n')
                {
                    end += 1;
                }
            }
        }
        if (parser->at < end && parser->result.error == 0)
        {
            if (parser->tokens[parser->at].kind != BUSTER_ASM_WORD)
            {
                asm_fail(parser, parser->tokens[parser->at].start, "expected mnemonic, directive or label");
            }
            else if (asm_token_text(parser, parser->at).pointer[0] == '.')
            {
                asm_directive(parser, end);
            }
            else
            {
                asm_prefix_statements(parser, end);
                if (parser->at < end && parser->result.error == 0)
                {
                    asm_instruction(parser, end);
                }
            }
        }
        parser->at = end;
    }
    asm_place_commons(parser);
    asm_resolve_aliases(parser);
    asm_line_finish(parser);
    asm_cfi_finish(parser);
    if (parser->cfi_active)
    {
        asm_fail(parser, parser->source.length, "missing .cfi_endproc");
    }
}

// A defined symbol's place in its section: its fragment's offset, and an alias's constant (AsmSymbol.offset).
static __int128 asm_symbol_position(const AsmParser* parser, const AsmSymbol* symbol)
{
    return (__int128)parser->fragments[symbol->fragment].offset + symbol->offset;
}

static AsmExpr asm_resolve(AsmParser* parser, AsmExpr expression, uint32_t location)
{
    // A view symbol's value is its row's view number (#1115), which layout gives it (asm_line_views).
    if (expression.plus != 0 && parser->symbols[expression.plus].view != 0 && expression.relocation == ASM_RELOCATION_NONE)
    {
        expression.value += parser->line_rows[parser->symbols[expression.plus].view - 1].view;
        expression.plus = 0;
    }
    if (expression.relocation != ASM_RELOCATION_NONE && (expression.plus == 0 || expression.minus != 0))
    {
        asm_fail(parser, location, "relocation modifier requires one symbol");
    }
    if ((expression.plus != 0 && parser->symbols[expression.plus].absolute) || (expression.minus != 0 && parser->symbols[expression.minus].absolute))
    {
        asm_fail(parser, location, "constant used before its definition is unsupported");
    }
    if (expression.minus != 0)
    {
        const AsmSymbol* positive = &parser->symbols[expression.plus];
        const AsmSymbol* negative = &parser->symbols[expression.minus];
        if (expression.plus == 0 || !positive->defined || !negative->defined || positive->absolute || negative->absolute || parser->fragments[positive->fragment].section != parser->fragments[negative->fragment].section)
        {
            asm_fail(parser, location, "symbol difference requires two defined labels in the same section");
        }
        else
        {
            expression.value += asm_symbol_position(parser, positive) - asm_symbol_position(parser, negative);
            expression.plus = 0;
            expression.minus = 0;
        }
    }
    if (!asm_expr_range(expression.value))
    {
        asm_fail(parser, location, "expression overflow after layout");
    }
    return expression;
}

static bool asm_local_target(const AsmParser* parser, AsmExpr expression, uint32_t section, bool global_jump)
{
    const AsmSymbol* symbol = &parser->symbols[expression.plus];
    const uint8_t binding = symbol->flags & BUSTER_LINK_SYMBOL_BINDING;
    const bool fixed = binding == BUSTER_LINK_SYMBOL_LOCAL || (global_jump && binding == BUSTER_LINK_SYMBOL_GLOBAL);
    return expression.relocation == ASM_RELOCATION_NONE && expression.plus != 0 && expression.minus == 0 && symbol->defined && !symbol->absolute && fixed &&
           parser->fragments[symbol->fragment].section == section;
}

// Why no form matched an instruction's request (#1134), for its diagnostic: its name has no form the syntax admits, or
// only forms no target allows (the target table doesn't classify them), and is no instruction this assembler has; its
// forms need ISA features the target lacks; or none takes these operands (on this target, where it refuses some), or
// they would without the AT&T suffix's width.
static const char* asm_unmatched(const AsmParser* parser, const AsmInstruction* instruction)
{
    const X86MatchIndex* index = parser->index;
    const uint32_t bucket = x86_match_bucket(index, instruction->request.mnemonic);
    const bool named = index->names[bucket].pointer != 0;
    const bool forms = index->begin[bucket] != index->begin[bucket + 1];
    const bool target = (index->excluded[bucket] & BUSTER_X86_MATCH_EXCLUDED_TARGET) != 0;
    X86MatchRequest unsized = instruction->request;
    unsized.width = 0;
    const bool suffix = forms && instruction->suffixed && x86_match(index, &unsized, BUSTER_VECTOR_PATH(parser->options.vectorized)).found;
    return !named || (!forms && !target) ? "instruction not supported"
           : !forms                       ? "instruction requires ISA features the target lacks (see -march)"
           : suffix                       ? "mnemonic suffix and register width disagree"
           : target                       ? "no form of the instruction takes these operands on this target"
                                          : "no form of the instruction takes these operands";
}

// Whether a branch form is a rel8 that GNU as relaxes to the rel32 form of its mnemonic, the next row: jmp and jcc,
// not jrcxz or loop.
static bool asm_branch_grows(uint32_t row)
{
    const X86FormDescription* form = &x86_form_descriptions[row];
    return form->relative_bytes == 1 && row + 1 < X86_FORM_COUNT && form[1].relative_bytes == 4 && (form->mnemonic == form[1].mnemonic || strcmp(form->mnemonic, form[1].mnemonic) == 0);
}

static uint32_t asm_rep_scan_delta(const AsmInstruction* instruction)
{
    const bool reverse = x86_match_name(instruction->request.mnemonic, "bsr");
    return reverse ? X86_FORM_LZCNT_R16_RM16 - X86_FORM_BSR_R16_RM16 : X86_FORM_TZCNT_R16_RM16 - X86_FORM_BSF_R16_RM16;
}

static void asm_match_instructions(AsmParser* parser)
{
    for (uint32_t i = 0; i < parser->instruction_count && parser->result.error == 0; i += 1)
    {
        AsmInstruction* instruction = &parser->instructions[i];
        AsmFragment* fragment = &parser->fragments[instruction->fragment];
        uint32_t width = instruction->request.width;
        for (uint32_t j = 0; j < instruction->request.count; j += 1)
        {
            const X86Operand* operand = &instruction->request.operands[j];
            if (operand->kind == X86_OPERAND_REGISTER || operand->kind == X86_OPERAND_MEMORY)
            {
                width = operand->width > width ? operand->width : width;
            }
        }
        for (uint32_t j = 0; j < instruction->request.count; j += 1)
        {
            X86Operand* operand = &instruction->request.operands[j];
            if (operand->kind == X86_OPERAND_IMMEDIATE && !operand->symbolic && width != 0 && width <= 8)
            {
                // GNU integer operands are bit patterns of their destination width. Canonicalize only values in
                // that width's unsigned range; oversized constants still fail the matcher's exact range check.
                const __int128 modulus = (__int128)1 << (width * 8);
                if (operand->value >= modulus / 2 && operand->value < modulus)
                {
                    operand->value -= modulus;
                }
            }
            if (instruction->expressions[j].plus != 0 && parser->symbols[instruction->expressions[j].plus].absolute)
            {
                asm_fail(parser, instruction->location, "constant used before its definition is unsupported");
            }
            if (instruction->expressions[j].minus != 0)
            {
                asm_fail(parser, instruction->location, "instruction operands require an absolute value or symbol plus addend; use data directives for label differences");
            }
        }
        X86Match match = x86_match(parser->index, &instruction->request, BUSTER_VECTOR_PATH(parser->options.vectorized));
        if (!match.found && !match.ambiguous && x86_match_name(instruction->request.mnemonic, "movsx"))
        {
            // GNU as reads movsx of a doubleword as movsxd (#1134: gcc -O2 -masm=intel writes `movsx r8, r8d`).
            X86MatchRequest doubleword = instruction->request;
            doubleword.mnemonic = BUSTER_S8("movsxd");
            match = x86_match(parser->index, &doubleword, BUSTER_VECTOR_PATH(parser->options.vectorized));
        }
        if (!match.found)
        {
            asm_fail_named(parser, instruction->location, match.ambiguous ? "ambiguous memory width; specify a size" : asm_unmatched(parser, instruction), instruction->written);
        }
        else
        {
            instruction->record = match.instruction;
            if (instruction->rep_scan)
            {
                // The scan and count forms have matching widths, operands and segment variants; only F3 is added.
                instruction->record.form += asm_rep_scan_delta(instruction);
            }
            fragment->size = match.length + instruction->rep_scan + instruction->address32;
            const X86FormDescription* form = x86_form_description(instruction->record.form);
            instruction->branch = form->relative_bytes != 0;
            if (instruction->branch && instruction->expressions[0].plus == 0)
            {
                // GAS treats a literal as an absolute destination, not a displacement from this instruction.
                // The Link contract has no absolute-symbol target; require a label instead of changing its meaning.
                asm_fail(parser, instruction->location, "direct branch requires a label or symbol target; absolute destinations are unsupported");
            }
            // GNU as resolves same-section global jumps without -mshared; global calls remain interposable.
            instruction->local_branch = instruction->branch && asm_local_target(parser, instruction->expressions[0], fragment->section, strcmp(form->mnemonic, "call") != 0);
            if (instruction->branch && !instruction->local_branch && form->relative_bytes == 1)
            {
                const uint32_t row = instruction->record.form;
                if (!asm_branch_grows(row))
                {
                    asm_fail(parser, instruction->location, "rel8-only branch requires a defined local target in its section");
                }
                else
                {
                    instruction->record.form += 1;
                    fragment->size += branch_sizing_growth(row);
                }
            }
        }
    }
}

// The compiler's existing branch sizer already has the right columnar AVX-512/scalar algorithms when assembly has
// only records and labels in one section. Adapt labels to record boundaries after proving that contract. Arbitrary
// data and alignment take the fragment adapter below; no instruction index is ever used as a byte position there.
static bool asm_layout_records(AsmParser* parser)
{
    bool eligible = parser->fragment_count != 0;
    const uint32_t section = parser->fragments[0].section;
    for (uint32_t i = 0; eligible && i < parser->fragment_count; i += 1)
    {
        const AsmFragment* fragment = &parser->fragments[i];
        eligible = fragment->section == section && (fragment->kind == ASM_LABEL || fragment->kind == ASM_INSTRUCTION);
    }
    for (uint32_t i = 0; eligible && i < parser->instruction_count; i += 1)
    {
        const AsmInstruction* instruction = &parser->instructions[i];
        eligible = !instruction->address32;
        if (eligible && instruction->local_branch)
        {
            const uint32_t row = instruction->record.form;
            eligible = instruction->expressions[0].value == 0 && x86_form_descriptions[row].relative_bytes == 1 && row + 1 < X86_FORM_COUNT && x86_form_descriptions[row + 1].relative_bytes == 4;
        }
    }
    if (eligible)
    {
        const uint64_t saved = parser->arena->length;
        uint32_t* positions = BUSTER_ARENA_PUSH_ARRAY(parser->arena, uint32_t, parser->fragment_count + 1);
        uint32_t* branches = BUSTER_ARENA_PUSH_ARRAY(parser->arena, uint32_t, parser->instruction_count + 1);
        uint32_t* targets = BUSTER_ARENA_PUSH_ARRAY(parser->arena, uint32_t, parser->instruction_count + 1);
        uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(parser->arena, uint32_t, parser->instruction_count + 1);
        X86Instruction* records = BUSTER_ARENA_PUSH_ARRAY(parser->arena, X86Instruction, parser->instruction_count + 1);
        if (positions == 0 || branches == 0 || targets == 0 || offsets == 0 || records == 0)
        {
            asm_fail(parser, 0, "arena exhausted adapting instruction layout");
        }
        else
        {
            uint32_t at = 0;
            for (uint32_t i = 0; i < parser->fragment_count; i += 1)
            {
                positions[i] = at;
                at += parser->fragments[i].kind == ASM_INSTRUCTION;
            }
            uint32_t count = 0;
            for (uint32_t i = 0; i < parser->instruction_count; i += 1)
            {
                const AsmInstruction* instruction = &parser->instructions[i];
                records[i] = instruction->record;
                if (instruction->local_branch)
                {
                    branches[count] = i;
                    targets[count++] = positions[parser->symbols[instruction->expressions[0].plus].fragment];
                }
            }
            const BranchSizingCheck check = branch_sizing_validate(records, parser->instruction_count, branches, targets, count);
            if (check.error != BRANCH_SIZING_OK)
            {
                asm_fail(parser, 0, "instruction layout adapter rejected its input");
            }
            else
            {
                const BranchSizing layout = BUSTER_VECTOR_PATH(parser->options.vectorized) ? size_branches(records, parser->instruction_count, branches, targets, count, offsets) : size_branches_scalar(records, parser->instruction_count, branches, targets, count, offsets);
                if (layout.error != BRANCH_SIZING_OK || layout.length > BUSTER_ASM_MAX_SECTION)
                {
                    asm_fail(parser, 0, "instruction layout failed or exceeds section limit");
                }
                else
                {
                    offsets[parser->instruction_count] = layout.length;
                    parser->sections[section].size = layout.length;
                    parser->result.rounds = layout.rounds;
                    for (uint32_t i = 0; i < parser->instruction_count; i += 1)
                    {
                        parser->instructions[i].record = records[i];
                        parser->fragments[parser->instructions[i].fragment].size = offsets[i + 1] - offsets[i];
                    }
                    for (uint32_t i = 0; i < parser->fragment_count; i += 1)
                    {
                        parser->fragments[i].offset = offsets[positions[i]];
                    }
                }
            }
        }
        parser->arena->length = saved;
    }
    return eligible;
}

static __m512i asm_prefix16(__m512i values)
{
    values = _mm512_add_epi32(values, _mm512_maskz_permutexvar_epi32(0xfffe, _mm512_setr_epi32(0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14), values));
    values = _mm512_add_epi32(values, _mm512_maskz_permutexvar_epi32(0xfffc, _mm512_setr_epi32(0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13), values));
    values = _mm512_add_epi32(values, _mm512_maskz_permutexvar_epi32(0xfff0, _mm512_setr_epi32(0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11), values));
    values = _mm512_add_epi32(values, _mm512_maskz_permutexvar_epi32(0xff00, _mm512_setr_epi32(0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7), values));
    return values;
}

// DWARF's variable-length integers, as GNU as's .uleb128 and .sleb128 write them. Compiler debug sections use them for
// constant offsets, label differences and label offsets, which asm_leb_value reduces to one value.
static uint32_t asm_leb_size(__int128 value, bool is_signed)
{
    uint32_t size = 1;
    if (is_signed)
    {
        int64_t number = (int64_t)value;
        bool more = true;
        while (more)
        {
            const uint8_t byte = number & 0x7f;
            number >>= 7;
            const bool sign = (byte & 0x40) != 0;
            more = !((number == 0 && !sign) || (number == -1 && sign));
            size += more;
        }
    }
    else
    {
        uint64_t number = (uint64_t)value;
        while (number >= 0x80)
        {
            number >>= 7;
            size += 1;
        }
    }
    return size;
}

static uint32_t asm_leb_write(uint8_t* bytes, __int128 value, bool is_signed)
{
    uint32_t size = 0;
    if (is_signed)
    {
        int64_t number = (int64_t)value;
        bool more = true;
        while (more)
        {
            uint8_t byte = number & 0x7f;
            number >>= 7;
            const bool sign = (byte & 0x40) != 0;
            more = !((number == 0 && !sign) || (number == -1 && sign));
            bytes[size++] = byte | (more ? 0x80 : 0);
        }
    }
    else
    {
        uint64_t number = (uint64_t)value;
        do
        {
            uint8_t byte = number & 0x7f;
            number >>= 7;
            bytes[size++] = byte | (number != 0 ? 0x80 : 0);
        } while (number != 0);
    }
    return size;
}

// The value a LEB128 directive encodes: an absolute expression, or a label difference, which asm_resolve has already
// reduced, or a label's offset in its section. Debug sections use the last form; the label's section is relocated as a
// whole, as GNU as encodes it.
static bool asm_leb_value(const AsmParser* parser, AsmExpr expression, __int128* value)
{
    bool valid = expression.minus == 0 && expression.relocation == ASM_RELOCATION_NONE;
    *value = expression.value;
    if (valid && expression.plus != 0)
    {
        const AsmSymbol* symbol = &parser->symbols[expression.plus];
        if (symbol->absolute)
        {
            valid = false;
        }
        else if (!symbol->section_start)
        {
            valid = symbol->defined;
            if (valid)
            {
                *value += asm_symbol_position(parser, symbol);
            }
        }
    }
    return valid;
}

// A .uleb128 or .sleb128's size, from the value it encodes where layout now has its labels.
static uint32_t asm_leb_fragment_size(AsmParser* parser, const AsmFragment* fragment)
{
    const bool is_signed = fragment->kind == ASM_SLEB;
    const AsmExpr expression = asm_resolve(parser, fragment->expression, fragment->location);
    __int128 value = 0;
    uint32_t size = 0;
    if (!asm_leb_value(parser, expression, &value) || (!is_signed && value < 0) || value < INT64_MIN || (is_signed ? value > INT64_MAX : value > UINT64_MAX))
    {
        asm_fail(parser, fragment->location, "LEB128 value must be an absolute value, a label difference or a label offset");
    }
    else
    {
        size = asm_leb_size(value, is_signed);
    }
    return size;
}

// Layout is GNU as's relax_segment (#1115), so that each branch takes the size GNU as gives it: compilers' output is
// compared byte for byte with GNU as's objects, and a different size moves the code and CFI after it. A pass visits a
// section in order, and a fragment that grows moves the rest at once (the pass's stretch), so a branch after it
// measures the distances this pass makes. A target the pass hasn't reached yet is where the last pass put it, moved by
// the stretch when the stretch is negative or no alignment lies between them; otherwise the alignment may absorb the
// stretch, so a branch whose target then appears behind it waits for the next pass. Alignments pad to where they now
// start, and LEB128s take the size of their value. Passes repeat until one grows nothing. A branch never shrinks, but
// with alignment a distance can shrink as code before it grows (max-skip padding), so the order decides which branches
// grow, as it does in GNU as. A previous round-based layout measured every branch against the last round's offsets
// and grew one GNU as kept short (GCC 16's -O2 floating program, where a backward jmp crossed `.p2align 4,,10`), and on
// GCC 13 -O2 output of 1,500 branchy functions it grew branches worth 80 bytes that GNU as keeps short.
//
// The order is the result, so a pass is sequential, and the passes are as many as GNU as's: 179 for those functions,
// where growth reaches each later function a pass later. What a pass reads is small instead. Only alignments, LEB128s
// and short branches that may grow (its events) change size, so the fragments between two events move together, and
// a pass reads each event's 32 bytes alone, as GNU as's frags hold the bytes between two of its variable parts. A
// label is its anchor (the event before it, or the section's start) plus a distance that no pass changes. Fragment
// offsets are written once, after the passes, by a prefix sum. Skipping the events whose distances a pass doesn't
// change (marking those over each change of the stretch, and deferring forward branches across alignments until the
// stretch at them allows growth) visited 15 times fewer events on those functions but took as long on a cloud host,
// so it isn't done; the 9700X's benchmark-assembler measures this layout.
#define ASM_RELAX_PLAIN 0
#define ASM_RELAX_ALIGN 1
#define ASM_RELAX_LEB 2
#define ASM_RELAX_BRANCH 3

typedef struct AsmRelaxEvent AsmRelaxEvent;
struct AsmRelaxEvent
{
    // A branch: its target's distance past its anchor's end, with the addend. An alignment: its alignment.
    int64_t local;
    uint32_t offset;
    uint32_t size;
    // The bytes between the previous event's end (or the section's start) and this event.
    uint32_t gap;
    uint32_t fragment;
    // A branch: its target's anchor, an event's index plus one, or 0 for the section's start. An alignment: its most
    // padding.
    uint32_t anchor;
    // The alignments before the event in its section, above its role's two bits; a branch that grew is plain.
    uint32_t region;
};

typedef struct AsmRelax AsmRelax;
struct AsmRelax
{
    // The short branches that may grow, a bit by fragment id, which asm_layout marks from the instruction records.
    uint64_t* branches;
    AsmRelaxEvent* events;
    // Each section's first event, and its bytes after its last.
    uint32_t* event_begin;
    uint32_t* tails;
    uint32_t event_count;
    uint64_t limit;
    bool lebs;
};

static void asm_relax_add(AsmRelax* relax, const AsmFragment* fragment, uint32_t id, uint32_t region, uint32_t* end)
{
    const bool align = (region & 3) == ASM_RELAX_ALIGN;
    relax->events[relax->event_count++] = (AsmRelaxEvent){
        .local = align ? fragment->alignment : 0,
        .offset = fragment->offset,
        .size = fragment->size,
        .gap = fragment->offset - *end,
        .fragment = id,
        .anchor = align ? fragment->maximum : 0,
        .region = region,
    };
    *end = fragment->offset + fragment->size;
}

static uint32_t asm_relax_role(const AsmRelax* relax, const AsmFragment* fragment, uint32_t id)
{
    const bool leb = fragment->kind == ASM_ULEB || fragment->kind == ASM_SLEB;
    const bool branch = (relax->branches[id / 64] >> (id % 64) & 1) != 0;
    return leb ? ASM_RELAX_LEB : branch ? ASM_RELAX_BRANCH : ASM_RELAX_PLAIN;
}

// GNU as's first addresses, every branch short and every LEB128 one byte, recording the section's events. Between
// alignments, the vector path computes sixteen offsets by a prefix network; the exact path is a plain sum. Scalar
// column loads/stores avoid Zen 5's costly gather/scatter.
static void asm_relax_first(AsmParser* parser, AsmRelax* relax, uint32_t section)
{
    const uint32_t* order = parser->fragment_order;
    const uint32_t end = parser->fragment_begin[section + 1];
    uint32_t at = parser->fragment_begin[section];
    uint32_t offset = 0;
    uint32_t region = 0;
    uint32_t event_end = 0;
    relax->event_begin[section] = relax->event_count;
    while (at < end && parser->result.error == 0)
    {
        const uint32_t id = order[at];
        AsmFragment* fragment = &parser->fragments[id];
        if (fragment->kind == ASM_ALIGN)
        {
            const uint32_t padding = -offset & (fragment->alignment - 1);
            fragment->size = padding <= fragment->maximum ? padding : 0;
            fragment->offset = offset;
            asm_relax_add(relax, fragment, id, region << 2 | ASM_RELAX_ALIGN, &event_end);
            region += 1;
            offset += fragment->size;
            at += 1;
        }
        else if (BUSTER_VECTOR_PATH(parser->options.vectorized))
        {
            uint32_t sizes[16] = {0};
            uint32_t offsets[16];
            uint32_t count = 0;
            uint64_t total = offset;
            while (count < 16 && at + count < end && parser->fragments[order[at + count]].kind != ASM_ALIGN)
            {
                sizes[count] = parser->fragments[order[at + count]].size;
                total += sizes[count++];
            }
            const __m512i values = _mm512_loadu_si512(sizes);
            const __m512i prefix = asm_prefix16(values);
            _mm512_storeu_si512(offsets, _mm512_add_epi32(_mm512_sub_epi32(prefix, values), _mm512_set1_epi32(offset)));
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t member = order[at + i];
                AsmFragment* placed = &parser->fragments[member];
                placed->offset = offsets[i];
                const uint32_t role = asm_relax_role(relax, placed, member);
                if (role != ASM_RELAX_PLAIN)
                {
                    asm_relax_add(relax, placed, member, region << 2 | role, &event_end);
                }
            }
            if (total > BUSTER_ASM_MAX_SECTION)
            {
                // Name the fragment that crosses the limit, as the exact path does, not the group's first.
                uint64_t crossed = offset;
                uint32_t i = 0;
                while ((crossed += sizes[i]) <= BUSTER_ASM_MAX_SECTION)
                {
                    i += 1;
                }
                asm_fail(parser, parser->fragments[order[at + i]].location, "section exceeds the 256 MiB layout limit");
            }
            offset = (uint32_t)total;
            at += count;
        }
        else
        {
            fragment->offset = offset;
            const uint32_t role = asm_relax_role(relax, fragment, id);
            if (role != ASM_RELAX_PLAIN)
            {
                asm_relax_add(relax, fragment, id, region << 2 | role, &event_end);
            }
            offset += fragment->size;
            at += 1;
        }
        if (offset > BUSTER_ASM_MAX_SECTION)
        {
            asm_fail(parser, fragment->location, "section exceeds the 256 MiB layout limit");
        }
    }
    relax->tails[section] = offset - event_end;
    parser->sections[section].size = offset;
}

// Each short branch's target as its anchor and distance, from the first addresses: the events before the target
// label are those of lower fragment id in its section (asm_partition_fragments is a stable partition, so a section's
// fragments are in id order). Targets are near, so the count gallops from the branch to a bracket, then a binary
// search finds it.
static void asm_relax_targets(AsmParser* parser, AsmRelax* relax)
{
    for (uint32_t section = 1; section < parser->section_count; section += 1)
    {
        const uint32_t first = relax->event_begin[section];
        const uint32_t last = relax->event_begin[section + 1];
        for (uint32_t e = first; e < last; e += 1)
        {
            AsmRelaxEvent* event = &relax->events[e];
            if ((event->region & 3) == ASM_RELAX_BRANCH)
            {
                const AsmInstruction* instruction = &parser->instructions[parser->fragments[event->fragment].instruction];
                const AsmSymbol* symbol = &parser->symbols[instruction->expressions[0].plus];
                const bool ahead = event->fragment < symbol->fragment;
                uint32_t low = ahead ? e + 1 : first;
                uint32_t high = ahead ? last : e;
                uint32_t step = 1;
                bool bracketed = false;
                while (!bracketed)
                {
                    const uint32_t probe = ahead ? (high - low > step ? low + step : high) : (high - low > step ? high - step : low);
                    const bool before = probe < last && relax->events[probe].fragment < symbol->fragment;
                    bracketed = ahead ? (probe == high || !before) : (probe == low || before);
                    low = ahead ? (bracketed ? low : probe + 1) : (bracketed && before ? probe + 1 : low);
                    high = ahead ? (bracketed && probe < high ? probe : high) : (bracketed ? high : probe);
                    step *= 2;
                }
                while (low < high)
                {
                    const uint32_t middle = low + (high - low) / 2;
                    const bool before = relax->events[middle].fragment < symbol->fragment;
                    low = before ? middle + 1 : low;
                    high = before ? high : middle;
                }
                const AsmRelaxEvent* anchor = low == first ? 0 : &relax->events[low - 1];
                const __int128 base = anchor == 0 ? 0 : (__int128)anchor->offset + anchor->size;
                const __int128 local = asm_symbol_position(parser, symbol) + instruction->expressions[0].value - base;
                // A distance past a signed 64-bit one is out of range either way, and the final check names it.
                event->local = local > INT64_MAX ? INT64_MAX : local < INT64_MIN ? INT64_MIN : (int64_t)local;
                event->anchor = low == first ? 0 : low;
            }
        }
    }
}

// relax_frag's choice for the short branch event `e`, now at `offset`, `stretch` bytes from where the last pass put
// it. The event still has the last pass's offset, which a target just after it is anchored to. A branch that grows
// takes its rel32 form.
static void asm_relax_branch(AsmParser* parser, AsmRelax* relax, uint32_t e, uint32_t offset, int64_t stretch)
{
    AsmRelaxEvent* event = &relax->events[e];
    const AsmRelaxEvent* anchor = event->anchor == 0 ? 0 : &relax->events[event->anchor - 1];
    __int128 target = (anchor == 0 ? 0 : (__int128)anchor->offset + anchor->size) + event->local;
    bool hold = false;
    // An anchor this pass hasn't reached, or this branch itself, holds a target the pass hasn't reached.
    if (stretch != 0 && event->anchor > e)
    {
        const uint32_t region = (anchor->region >> 2) + ((anchor->region & 3) == ASM_RELAX_ALIGN);
        if (stretch < 0 || region == event->region >> 2)
        {
            target += stretch;
        }
        else
        {
            // GNU as's address is the rel8's, the branch's last byte.
            hold = target < (__int128)offset + event->size - 1;
        }
    }
    const __int128 distance = target - offset - event->size;
    if (!hold && (distance < -128 || distance > 127))
    {
        AsmFragment* fragment = &parser->fragments[event->fragment];
        AsmInstruction* instruction = &parser->instructions[fragment->instruction];
        const uint32_t growth = branch_sizing_growth(instruction->record.form);
        instruction->record.form += 1;
        fragment->size += growth;
        event->size += growth;
        event->region &= ~(uint32_t)3;
    }
}

// One pass over a section's events in order; returns whether a size changed.
static bool asm_relax_pass(AsmParser* parser, AsmRelax* relax, uint32_t section)
{
    uint64_t running = 0;
    bool stretched = false;
    for (uint32_t e = relax->event_begin[section]; e < relax->event_begin[section + 1] && parser->result.error == 0; e += 1)
    {
        AsmRelaxEvent* event = &relax->events[e];
        const uint64_t offset = running + event->gap;
        const uint32_t size = event->size;
        const uint32_t role = event->region & 3;
        if (offset + size > BUSTER_ASM_MAX_SECTION)
        {
            asm_fail(parser, parser->fragments[event->fragment].location, "section exceeds the 256 MiB layout limit");
        }
        else if (role == ASM_RELAX_ALIGN)
        {
            const uint32_t padding = -(uint32_t)offset & ((uint32_t)event->local - 1);
            event->size = padding <= event->anchor ? padding : 0;
        }
        else if (role == ASM_RELAX_LEB)
        {
            // Its labels are where the last pass put them.
            AsmFragment* fragment = &parser->fragments[event->fragment];
            fragment->size = asm_leb_fragment_size(parser, fragment);
            event->size = fragment->size;
        }
        else if (role == ASM_RELAX_BRANCH)
        {
            asm_relax_branch(parser, relax, e, (uint32_t)offset, (int64_t)offset - event->offset);
        }
        event->offset = (uint32_t)offset;
        stretched |= event->size != size;
        running = offset + event->size;
    }
    running += relax->tails[section];
    if (running > BUSTER_ASM_MAX_SECTION && parser->result.error == 0)
    {
        asm_fail(parser, 0, "section exceeds the 256 MiB layout limit");
    }
    parser->sections[section].size = (uint32_t)running;
    return stretched;
}

// Every fragment's offset from the passes' sizes: one prefix sum per section, sixteen at a time on the vector path,
// with no alignment to stop at, as the events' sizes are final.
static void asm_relax_place(AsmParser* parser, const AsmRelax* relax, uint32_t section)
{
    for (uint32_t e = relax->event_begin[section]; e < relax->event_begin[section + 1]; e += 1)
    {
        parser->fragments[relax->events[e].fragment].size = relax->events[e].size;
    }
    const uint32_t* order = parser->fragment_order;
    const uint32_t end = parser->fragment_begin[section + 1];
    uint32_t at = parser->fragment_begin[section];
    uint32_t offset = 0;
    while (at < end)
    {
        if (BUSTER_VECTOR_PATH(parser->options.vectorized))
        {
            uint32_t sizes[16] = {0};
            uint32_t offsets[16];
            const uint32_t count = end - at < 16 ? end - at : 16;
            for (uint32_t i = 0; i < count; i += 1)
            {
                sizes[i] = parser->fragments[order[at + i]].size;
            }
            const __m512i values = _mm512_loadu_si512(sizes);
            const __m512i prefix = asm_prefix16(values);
            _mm512_storeu_si512(offsets, _mm512_add_epi32(_mm512_sub_epi32(prefix, values), _mm512_set1_epi32(offset)));
            for (uint32_t i = 0; i < count; i += 1)
            {
                parser->fragments[order[at + i]].offset = offsets[i];
            }
            offset = offsets[count - 1] + sizes[count - 1];
            at += count;
        }
        else
        {
            AsmFragment* fragment = &parser->fragments[order[at]];
            fragment->offset = offset;
            offset += fragment->size;
            at += 1;
        }
    }
}

static void asm_layout(AsmParser* parser)
{
    asm_partition_fragments(parser);
    if (parser->result.error == 0 && !asm_layout_records(parser))
    {
        Scratch scratch = scratch_begin(parser->arena);
        // A pass that changes nothing ends layout, and a branch grows once; a LEB128 grows at most nine times unless an
        // alignment after it makes it bounce, which GNU as breaks by padding and this refuses.
        const uint64_t count = (uint64_t)parser->fragment_count + 1;
        AsmRelax relax = {
            .branches = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, count / 64 + 1),
            .events = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, AsmRelaxEvent, count),
            .event_begin = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, parser->section_count + 1),
            .tails = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, parser->section_count + 1),
            .limit = 2,
        };
        if (relax.branches == 0 || relax.events == 0 || relax.event_begin == 0 || relax.tails == 0)
        {
            asm_fail(parser, 0, "arena exhausted laying out fragments");
        }
        else
        {
            memset(relax.branches, 0, (count / 64 + 1) * sizeof(uint64_t));
            for (uint32_t i = 0; i < parser->instruction_count; i += 1)
            {
                const AsmInstruction* instruction = &parser->instructions[i];
                if (instruction->local_branch && asm_branch_grows(instruction->record.form))
                {
                    relax.branches[instruction->fragment / 64] |= (uint64_t)1 << (instruction->fragment % 64);
                    relax.limit += 1;
                }
            }
            for (uint32_t section = 1; section < parser->section_count; section += 1)
            {
                asm_relax_first(parser, &relax, section);
            }
            relax.event_begin[parser->section_count] = relax.event_count;
            for (uint32_t e = 0; e < relax.event_count; e += 1)
            {
                const bool leb = (relax.events[e].region & 3) == ASM_RELAX_LEB;
                relax.lebs |= leb;
                relax.limit += leb ? 9 : 0;
            }
            asm_relax_targets(parser, &relax);
            // A .uleb128 of a view number (#1115) reads the code's offsets as any LEB128 reads its labels'.
            asm_line_views(parser, false);
            bool stretched = true;
            bool moved = false;
            while (stretched && parser->result.error == 0)
            {
                stretched = false;
                parser->result.rounds += 1;
                for (uint32_t section = 1; section < parser->section_count; section += 1)
                {
                    stretched |= asm_relax_pass(parser, &relax, section);
                }
                moved |= stretched;
                // A LEB128 reads labels' fragment offsets, so a round that moved them places them for the next.
                for (uint32_t section = 1; relax.lebs && stretched && section < parser->section_count; section += 1)
                {
                    asm_relax_place(parser, &relax, section);
                }
                if (relax.lebs && stretched)
                {
                    asm_line_views(parser, false);
                }
                if (stretched && parser->result.rounds >= relax.limit)
                {
                    asm_fail(parser, 0, "layout doesn't converge: a .uleb128 or .sleb128 bounces between sizes across an alignment");
                }
            }
            for (uint32_t section = 1; moved && !relax.lebs && parser->result.error == 0 && section < parser->section_count; section += 1)
            {
                asm_relax_place(parser, &relax, section);
            }
        }
        scratch_end(scratch);
    }
    if (parser->result.error == 0)
    {
        asm_line_views(parser, true);
    }
    for (uint32_t i = 0; i < parser->instruction_count && parser->result.error == 0; i += 1)
    {
        AsmInstruction* instruction = &parser->instructions[i];
        if (instruction->local_branch)
        {
            const AsmFragment* fragment = &parser->fragments[instruction->fragment];
            const AsmExpr expression = instruction->expressions[0];
            const __int128 target = asm_symbol_position(parser, &parser->symbols[expression.plus]);
            const __int128 distance = target + expression.value - fragment->offset - fragment->size;
            const uint32_t width = x86_form_description(instruction->record.form)->relative_bytes;
            if (distance < INT32_MIN || distance > INT32_MAX || (width == 1 && (distance < -128 || distance > 127)))
            {
                asm_fail(parser, instruction->location, "branch displacement is out of range");
            }
            else
            {
                instruction->record.displacement = (int32_t)distance;
            }
        }
        X86Instruction target_record = instruction->record;
        if (instruction->rep_scan)
        {
            // The F3 form is encoded as TZCNT/LZCNT, but the source asked for the baseline bit-scan instruction.
            target_record.form -= asm_rep_scan_delta(instruction);
        }
        if (x86_target_validate(parser->index->allowed, &target_record, 1) != 1 || x86_validate(&instruction->record, 1) != 1)
        {
            asm_fail(parser, instruction->location, "resolved instruction is invalid for the target");
        }
    }
}

// One instruction of a 32-bit address in 64-bit mode (#2339): its 64-bit address's encoding with a 67 after the
// prefixes GNU as and clang's integrated assembler write first, a waiting form's 9B and a segment override
// (`9b 67 d9 38` for `fstcw (%eax)`, `64 67 66 89 00` for `movw %ax, %fs:(%eax)`, `67 f0 83 00 01` for `lock addl $1,
// (%eax)`): in 64-bit mode those bytes are prefixes alone. The registers' numbers and the ModRM, SIB and displacement
// are the 64-bit address's, so the fields a fixup patches keep their distance from the instruction's end. Returns the
// length, or -1 when the record doesn't encode.
static int64_t asm_emit_address32(const AsmParser* parser, const X86Instruction* record, uint8_t* output)
{
    uint8_t bytes[BUSTER_X86_ENCODER_CAPACITY(1)];
    const int64_t length = BUSTER_VECTOR_PATH(parser->options.vectorized) ? x86_encode(record, 1, bytes, sizeof(bytes), 0, 0) : x86_encode_scalar(record, 1, bytes, sizeof(bytes), 0, 0);
    uint32_t at = 0;
    const bool waiting = length > 0 && bytes[0] == 0x9b;
    at += waiting;
    const uint8_t first = length > at ? bytes[at] : 0;
    const bool segment = first == 0x26 || first == 0x2e || first == 0x36 || first == 0x3e || first == 0x64 || first == 0x65;
    at += segment;
    if (length > 0)
    {
        memcpy(output, bytes, at);
        output[at] = 0x67;
        memcpy(output + at + 1, bytes + at, (uint64_t)length - at);
    }
    return length > 0 ? length + 1 : -1;
}

static void asm_data_value(AsmParser* parser, AtomId atom, AsmFragment* fragment)
{
    // GNU as's `.long A - B` where B is in the datum's section and A isn't (#1115: GCC's -O2 PIE jump tables, `.long
    // .L45-.L35` in .rodata with .L45 in .text): A's distance from the datum, plus the datum's from B, which is constant,
    // so a PC-relative relocation of A (PC32, and PC64 for `.quad`). A B elsewhere stays refused, as GNU as refuses it.
    AsmExpr source = fragment->expression;
    bool relative = false;
    if (source.plus != 0 && source.minus != 0 && source.relocation == ASM_RELOCATION_NONE)
    {
        const AsmSymbol* positive = &parser->symbols[source.plus];
        const AsmSymbol* negative = &parser->symbols[source.minus];
        const bool here = negative->defined && !negative->absolute && !negative->section_start && parser->fragments[negative->fragment].section == fragment->section;
        const bool elsewhere = !positive->absolute && (!positive->defined || parser->fragments[positive->fragment].section != fragment->section);
        if (here && elsewhere)
        {
            source.value += (__int128)fragment->offset - asm_symbol_position(parser, negative);
            source.minus = 0;
            relative = true;
        }
    }
    AsmExpr expression = asm_resolve(parser, source, fragment->location);
    // A TLS variable's offset as data (#1115): `.quad x@dtpoff` (DWARF's location of a TLS variable, which GCC and Clang
    // write at -g), `.long x@dtpoff` (32-bit DWARF expressions), and `.quad x@tpoff` and `.long x@tpoff`.
    const bool offset = expression.relocation == ASM_RELOCATION_TPOFF || expression.relocation == ASM_RELOCATION_DTPOFF;
    if (expression.relocation != ASM_RELOCATION_NONE && (!offset || (fragment->size != 4 && fragment->size != 8)))
    {
        asm_fail(parser, fragment->location, "of data, only .long and .quad take a relocation modifier, @TPOFF or @DTPOFF");
    }
    uint8_t* bytes = parser->result.link.atoms.bytes[atom];
    if (expression.plus == 0)
    {
        const __int128 low = -((__int128)1 << (fragment->size * 8 - 1));
        const __int128 high = ((__int128)1 << (fragment->size * 8)) - 1;
        if (expression.value < low || expression.value > high)
        {
            asm_fail(parser, fragment->location, "data value overflows its field");
        }
        else if (bytes != 0)
        {
            const uint64_t value = expression.value;
            memcpy(bytes + fragment->offset, &value, fragment->size);
        }
        else if (expression.value != 0)
        {
            asm_fail(parser, fragment->location, ".bss may contain only zero fill");
        }
    }
    else if (bytes == 0 || (fragment->size != 4 && fragment->size != 8) || expression.value < INT64_MIN || expression.value > INT64_MAX)
    {
        asm_fail(parser, fragment->location, "symbol relocation needs a 32/64-bit data field and a signed 64-bit addend");
    }
    else
    {
        const bool wide = fragment->size == 8;
        const uint8_t kind = expression.relocation == ASM_RELOCATION_TPOFF  ? (wide ? BUSTER_LINK_FIXUP_TPOFF64 : BUSTER_LINK_FIXUP_TPOFF32)
                           : expression.relocation == ASM_RELOCATION_DTPOFF ? (wide ? BUSTER_LINK_FIXUP_DTPOFF64 : BUSTER_LINK_FIXUP_DTPOFF32)
                           : relative                                       ? (fragment->size == 4 ? BUSTER_LINK_FIXUP_PC32 : BUSTER_LINK_FIXUP_PC64)
                           : fragment->size == 4                            ? BUSTER_LINK_FIXUP_ABS32
                                                                            : BUSTER_LINK_FIXUP_ABS64;
        memset(bytes + fragment->offset, 0, fragment->size);
        link_add_fixup(&parser->result.link, atom, fragment->offset, kind, parser->symbols[expression.plus].link, (int64_t)expression.value);
    }
}

// The fixup kind of a TLS model's relocation modifier on an instruction's field (#1115), where GNU as takes it: @TPOFF
// (local-exec) and @DTPOFF (local-dynamic's offset) in a signed 32-bit field, a displacement without RIP or an
// immediate the form sign-extends (`movl %fs:x@tpoff, %eax`, `leaq x@dtpoff(%rax), %rdx`, `addq $x@tpoff, %rax`), as
// TPOFF32 and DTPOFF32; @GOTTPOFF (initial-exec), @TLSGD (general-dynamic) and @TLSLD (local-dynamic) in a
// RIP-relative displacement, as their kinds.
static uint8_t asm_tls_kind(AsmParser* parser, uint32_t location, uint16_t relocation, bool rip, bool signed32, bool wide)
{
    uint8_t kind = BUSTER_LINK_FIXUP_NONE;
    if (relocation == ASM_RELOCATION_TPOFF || relocation == ASM_RELOCATION_DTPOFF)
    {
        // A 64-bit immediate (`movabs $x@tpoff, %rax`) takes the 64-bit kinds, R_X86_64_TPOFF64 and DTPOFF64.
        if (!signed32 && !wide)
        {
            asm_fail(parser, location, "@TPOFF and @DTPOFF take a signed 32-bit displacement or immediate, or a 64-bit immediate");
        }
        kind = relocation == ASM_RELOCATION_TPOFF ? (wide ? BUSTER_LINK_FIXUP_TPOFF64 : BUSTER_LINK_FIXUP_TPOFF32) : (wide ? BUSTER_LINK_FIXUP_DTPOFF64 : BUSTER_LINK_FIXUP_DTPOFF32);
    }
    else
    {
        if (!rip)
        {
            asm_fail(parser, location, "@GOTTPOFF, @TLSGD and @TLSLD require a RIP-relative memory displacement");
        }
        kind = relocation == ASM_RELOCATION_GOTTPOFF ? BUSTER_LINK_FIXUP_GOTTPOFF : relocation == ASM_RELOCATION_TLSGD ? BUSTER_LINK_FIXUP_TLSGD : BUSTER_LINK_FIXUP_TLSLD;
    }
    return kind;
}

static void asm_instruction_fixups(AsmParser* parser, AtomId atom, AsmInstruction* instruction)
{
    const AsmFragment* fragment = &parser->fragments[instruction->fragment];
    const X86FormDescription* form = x86_form_description(instruction->record.form);
    // Displacement precedes immediate in the bytes, independently of the syntax's operand order.
    for (uint32_t field = X86_FIXUP_DISPLACEMENT; field <= X86_FIXUP_IMMEDIATE; field += 1)
    {
        for (uint32_t i = 0; i < instruction->request.count; i += 1)
        {
            const X86Operand* operand = &instruction->request.operands[i];
            const bool displacement = operand->kind == X86_OPERAND_MEMORY || instruction->branch;
            const AsmExpr expression = instruction->expressions[i];
            if (expression.plus == 0 || (field == X86_FIXUP_DISPLACEMENT) != displacement || instruction->local_branch)
            {
                continue;
            }
            const uint32_t distance = x86_fixup_distance(instruction->record.form, field);
            const uint32_t offset = fragment->offset + fragment->size - distance;
            uint8_t* bytes = parser->result.link.atoms.bytes[atom] + offset;
            const bool pc_relative = instruction->branch || (operand->mode & BUSTER_X86_MODE_RIP);
            const uint32_t width = displacement ? 4 : form->immediate_bytes;
            if (pc_relative && expression.relocation == ASM_RELOCATION_NONE && !instruction->branch && asm_local_target(parser, expression, fragment->section, false))
            {
                const __int128 value = asm_symbol_position(parser, &parser->symbols[expression.plus]) + expression.value - fragment->offset - fragment->size;
                if (value < INT32_MIN || value > INT32_MAX)
                {
                    asm_fail(parser, instruction->location, "RIP-relative displacement out of range");
                }
                else
                {
                    const int32_t narrowed = value;
                    memcpy(bytes, &narrowed, 4);
                }
            }
            else
            {
                const __int128 addend = expression.value - (pc_relative ? distance : 0);
                if (addend < INT64_MIN || addend > INT64_MAX || (width != 4 && width != 8))
                {
                    asm_fail(parser, instruction->location, "unrepresentable instruction relocation");
                }
                else
                {
                    // A disp32 and an immediate the form sign-extends (REX.W's, and PUSH's) take 32S, other imm32s 32. A
                    // 32-bit address's disp32 (#2339) is zero-extended, so GNU as gives it 32; clang's integrated
                    // assembler keeps 32S, which a C unit's assembly follows.
                    const bool truncated_lea = strcmp(form->mnemonic, "lea") == 0 && !form->w;
                    const bool unsigned_address = instruction->address32 && !parser->options.integrated;
                    const bool signed_field = (displacement && !truncated_lea && !unsigned_address) || x86_match_sign_extends(form);
                    const bool interposable = (parser->symbols[expression.plus].flags & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL;
                    uint8_t kind = instruction->branch && interposable ? BUSTER_LINK_FIXUP_CALL : pc_relative ? BUSTER_LINK_FIXUP_PC32
                                                                                 : width == 8    ? BUSTER_LINK_FIXUP_ABS64
                                                                                 : signed_field  ? BUSTER_LINK_FIXUP_ABS32S
                                                                                                 : BUSTER_LINK_FIXUP_ABS32;
                    if (expression.relocation == ASM_RELOCATION_PLT)
                    {
                        if (!instruction->branch || !displacement)
                        {
                            asm_fail(parser, instruction->location, "@PLT requires a call or jump displacement");
                        }
                        kind = BUSTER_LINK_FIXUP_CALL;
                    }
                    else if (expression.relocation == ASM_RELOCATION_GOTPCREL || expression.relocation == ASM_RELOCATION_GOTPCRELX)
                    {
                        if (!displacement || (operand->mode & BUSTER_X86_MODE_RIP) == 0)
                        {
                            asm_fail(parser, instruction->location, "@GOTPCREL requires a RIP-relative memory displacement");
                        }
                        const uint32_t row = instruction->record.form & BUSTER_X86_FORM_NUMBER;
                        // GNU as writes the relaxable GOTPCRELX forms for the instructions the psABI lets a linker
                        // rewrite, with the field's own -4 addend, whether the source says GOTPCREL or GOTPCRELX.
                        const bool relaxed = addend == -4;
                        kind = relaxed && (row == X86_FORM_CALL_RM64 || row == X86_FORM_JMP_RM64) ? BUSTER_LINK_FIXUP_CALL_GOT
                             : relaxed && row == X86_FORM_MOV_R64_RM64                               ? BUSTER_LINK_FIXUP_LOAD_GOT
                                                                                                    : BUSTER_LINK_FIXUP_GOTPCREL;
                    }
                    else if (expression.relocation != ASM_RELOCATION_NONE)
                    {
                        kind = asm_tls_kind(parser, instruction->location, expression.relocation, displacement && (operand->mode & BUSTER_X86_MODE_RIP) != 0,
                                            !pc_relative && width == 4 && signed_field, !pc_relative && !displacement && width == 8);
                    }
                    memset(bytes, 0, width);
                    link_add_fixup(&parser->result.link, atom, offset, kind, parser->symbols[expression.plus].link, addend);
                }
            }
        }
    }
}

// GNU as's code padding (binutils 2.47, 64-bit code): after data, a one-byte nop first; past seven NOPs' worth, a jump
// over the rest (rel8 while it reaches); then the remainder's NOP and 11-byte ones. Executed padding before an aligned
// loop head then costs a few instructions instead of one per byte, and objects compare with GNU as's byte for byte.
static void asm_code_padding(uint8_t* bytes, uint32_t count, bool after_data)
{
    static const uint8_t nops[11][11] =
        {
            {0x90},
            {0x66, 0x90},
            {0x0f, 0x1f, 0x00},
            {0x0f, 0x1f, 0x40, 0x00},
            {0x0f, 0x1f, 0x44, 0x00, 0x00},
            {0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00},
            {0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00},
            {0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
            {0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
            {0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
            {0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
    };
    uint32_t at = 0;
    if (after_data && count != 0)
    {
        bytes[at++] = 0x90;
        count -= 1;
    }
    if (count / 11 > 7)
    {
        if (count - 2 <= 127)
        {
            count -= 2;
            bytes[at++] = 0xeb;
            bytes[at++] = (uint8_t)count;
        }
        else
        {
            count -= 5;
            bytes[at++] = 0xe9;
            memcpy(bytes + at, &count, 4);
            at += 4;
        }
    }
    const uint32_t first = count % 11;
    if (first != 0)
    {
        memcpy(bytes + at, nops[first - 1], first);
        at += first;
    }
    for (uint32_t i = first; i < count; i += 11)
    {
        memcpy(bytes + at, nops[10], 11);
        at += 11;
    }
}

static void asm_emit(AsmParser* parser)
{
    Link* link = &parser->result.link;
    const LinkCapacity capacity =
        {
            .names = parser->symbol_count,
            // A section's atom each, and a global common's (#147's representation).
            .atoms = parser->section_count + parser->common_count,
            .symbols = parser->symbol_count,
            .fixups = parser->fragment_count + parser->instruction_count * 2 + parser->line_fixup_count + parser->procedure_count * 2,
            .wide_addends = parser->fragment_count + parser->instruction_count * 2,
    };
    if (!link_initialize(link, parser->arena, parser->arena, capacity))
    {
        asm_fail(parser, 0, "arena exhausted building link tables");
    }
    for (uint32_t f = 0; f < parser->fragment_count && parser->result.error == 0; f += 1)
    {
        const uint32_t section = parser->fragments[f].section;
        AsmSection* descriptor = &parser->sections[section];
        if (descriptor->atom == 0)
        {
            const uint32_t size = descriptor->size;
            // The trusted encoder requires worst-case capacity plus its documented store slack even when the
            // chosen encodings are shorter. Keep that padding outside the logical atom size, including the tail.
            const bool zero = object_class_is_zero_fill(descriptor->class);
            uint8_t* bytes = zero ? 0 : arena_push(parser->arena, (uint64_t)size + BUSTER_X86_ENCODER_CAPACITY(256), 64);
            if (bytes == 0 && !zero)
            {
                asm_fail(parser, parser->fragments[f].location, "arena exhausted allocating section");
            }
            else
            {
                if (bytes != 0)
                {
                    memset(bytes, 0, size);
                }
                descriptor->atom = link_add_atom(link, descriptor->class, descriptor->alignment, size, bytes, 0);
            }
        }
    }
    // The source's entry is its default text section's start: the function's or helper's first byte, before any switch.
    parser->result.entry_atom = parser->sections[BUSTER_LINK_CLASS_TEXT].atom;
    for (uint32_t f = 0; f < parser->fragment_count; f += 1)
    {
        const AsmFragment* fragment = &parser->fragments[f];
        parser->result.entry_code |= fragment->section == BUSTER_LINK_CLASS_TEXT && fragment->kind != ASM_LABEL && fragment->kind != ASM_ALIGN &&
                                      !(fragment->kind == ASM_FILL && fragment->padding == ASM_PADDING_NOPS);
    }
    // Whether the first control flow is a return: a branch first makes a later return a snippet's, as the record path takes
    // a call's for no branch (only jmp and jcc are branch sizing's columns).
    for (uint32_t i = 0; i < parser->instruction_count && !parser->result.ret_first; i += 1)
    {
        const AsmInstruction* instruction = &parser->instructions[i];
        const X86FormDescription* form = x86_form_description(instruction->record.form);
        const uint16_t row = instruction->record.form & BUSTER_X86_FORM_NUMBER;
        const bool ret = row == X86_FORM_RET || row == X86_FORM_RET_IMM16 || row == X86_FORM_RETW || row == X86_FORM_RETW_IMM16 ||
                         row == X86_FORM_RETD || row == X86_FORM_RETD_IMM16 || row == X86_FORM_REP_RET || row == X86_FORM_REPNE_RET;
        if (ret)
        {
            parser->result.ret_first = true;
        }
        else if (instruction->branch && strcmp(form->mnemonic, "call") != 0)
        {
            break;
        }
    }
    // The source's absolute symbols, which its Link doesn't hold: the checker records them for the one template and
    // refuses another template's reference to one.
    uint32_t absolute_count = 0;
    for (uint32_t i = 1; i < parser->symbol_count; i += 1)
    {
        const AsmSymbol* symbol = &parser->symbols[i];
        absolute_count += symbol->absolute && !symbol->explicit_binding && !symbol->has_size && !symbol->internal && symbol->name.length != 0;
    }
    parser->result.absolute_count = absolute_count;
    parser->result.absolutes = absolute_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(parser->arena, AsmAbsolute, absolute_count) : 0;
    if (absolute_count != 0 && parser->result.absolutes == 0)
    {
        asm_fail(parser, 0, "arena exhausted recording absolute symbols");
    }
    uint32_t absolute_at = 0;
    for (uint32_t i = 1; i < parser->symbol_count && parser->result.error == 0; i += 1)
    {
        AsmSymbol* symbol = &parser->symbols[i];
        if (symbol->absolute)
        {
            if (symbol->explicit_binding || symbol->has_size || symbol->group != 0)
            {
                asm_fail(parser, symbol->location, "exported absolute symbols are not supported by the shared object writer");
            }
            else if (!symbol->internal && symbol->name.length != 0)
            {
                if (symbol->constant < INT64_MIN || symbol->constant > UINT64_MAX)
                {
                    asm_fail(parser, symbol->location, "absolute symbol value doesn't fit 64 bits");
                }
                else
                {
                    char* name = arena_push(parser->arena, symbol->name.length, 1);
                    if (name == 0)
                    {
                        asm_fail(parser, 0, "arena exhausted recording absolute symbols");
                    }
                    else
                    {
                        memcpy(name, symbol->name.pointer, symbol->name.length);
                        parser->result.absolutes[absolute_at++] = (AsmAbsolute){ .name = { .pointer = name, .length = symbol->name.length },
                                                                                .value = (int64_t)symbol->constant };
                    }
                }
            }
            continue;
        }
        // A group-only signature is a local NOTYPE symbol of its SHT_GROUP section, not an undefined Link symbol.
        // An explicitly global or referenced signature, and any definition, retain their ordinary symbol instead.
        if (symbol->group != 0 && !symbol->defined && !symbol->alias && symbol->common == 0 &&
            (!symbol->ordinary || ((symbol->flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK && !symbol->used && !symbol->referenced)))
        {
            continue;
        }
        if (symbol->alias || symbol->view != 0)
        {
            // No reference is to it any more (asm_resolve_aliases), or it is a view number (asm_resolve).
            continue;
        }
        if (symbol->section_start && !symbol->defined)
        {
            AsmSection* target = &parser->sections[symbol->section_index];
            if (target->atom == 0)
            {
                target->atom = link_add_atom(link, target->class, target->alignment, 0, 0, 0);
            }
            const NameId name = symbol->name.length != 0 ? link_add_name(link, (const uint8_t*)symbol->name.pointer, symbol->name.length) : 0;
            symbol->link = link_add_symbol(link, name, target->atom, 0, 0, symbol->flags, BUSTER_LINK_SOURCE_PROGRAM);
            continue;
        }
        if (!symbol->defined && symbol->group == 0 && (symbol->internal || (symbol->name.length >= 2 && memcmp(symbol->name.pointer, ".L", 2) == 0)))
        {
            asm_fail(parser, symbol->location, "undefined local symbol");
            continue;
        }
        // An undefined symbol is global unless weak: also one `.local` names, which GNU as lists as a global (#1115).
        if (!symbol->defined && (!symbol->explicit_binding || (symbol->flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL))
        {
            symbol->flags = (uint8_t)((symbol->flags & ~BUSTER_LINK_SYMBOL_BINDING) | BUSTER_LINK_SYMBOL_GLOBAL);
        }
        const AsmFragment* definition = &parser->fragments[symbol->fragment];
        // GNU as makes a symbol of a TLS section a TLS variable (`.type tl, @object` in .tbss, as clang and GCC write
        // it), and an undefined one a TLS modifier names (#1115). A TLS modifier on another section's symbol is no TLS
        // access.
        const bool tls_section = symbol->defined && link_class_is_tls(parser->sections[definition->section].class);
        const uint8_t type = symbol->flags & BUSTER_LINK_SYMBOL_TYPE;
        // A global common (#147's representation, BUSTER_LINK_SYMBOL_COMMON): a symbol of its own zero-filled atom,
        // which the object writer leaves to the linker as SHN_COMMON, its value the atom's alignment (#1115).
        const bool common = symbol->common == ASM_COMMON_GLOBAL && !symbol->defined;
        if (symbol->common == ASM_COMMON_GLOBAL && symbol->defined)
        {
            asm_fail(parser, symbol->location, "duplicate symbol definition");
        }
        if (common && symbol->tls)
        {
            // As GNU as refuses it ("Accessing `x' as thread-local object"): a common symbol is no TLS variable.
            asm_fail(parser, symbol->location, "TLS relocation modifier on a common symbol");
        }
        if (symbol->defined && symbol->tls && !tls_section)
        {
            asm_fail(parser, symbol->location, "TLS relocation modifier on a symbol outside a TLS section");
        }
        if ((tls_section || (!symbol->defined && symbol->tls)) && (type == BUSTER_LINK_SYMBOL_NOTYPE || type == BUSTER_LINK_SYMBOL_OBJECT))
        {
            symbol->flags = (uint8_t)((symbol->flags & ~BUSTER_LINK_SYMBOL_TYPE) | BUSTER_LINK_SYMBOL_TLS);
        }
        AsmExpr size = symbol->has_size ? asm_resolve(parser, symbol->size, symbol->location) : (AsmExpr){0};
        if (size.plus || size.minus || size.value < 0 || size.value > UINT64_MAX)
        {
            asm_fail(parser, symbol->location, "symbol size must resolve to a nonnegative absolute value");
        }
        const NameId name = !symbol->internal && symbol->name.length != 0 ? link_add_name(link, (const uint8_t*)symbol->name.pointer, symbol->name.length) : 0;
        const __int128 position = symbol->defined ? asm_symbol_position(parser, symbol) : 0;
        if (position < 0 || (symbol->defined && position > link->atoms.size[parser->sections[definition->section].atom]))
        {
            asm_fail(parser, symbol->location, "symbol alias outside its target's section");
        }
        // GNU as writes a size that reaches past the section's end (`.set tail, object+2` of a 4-byte object takes its
        // size, #1115). A Link symbol lies within its atom, so its size is what remains there, and the object's st_size
        // is the source's (asm_object_options).
        const uint64_t remaining = symbol->defined && position >= 0 && position <= link->atoms.size[parser->sections[definition->section].atom] ? link->atoms.size[parser->sections[definition->section].atom] - (uint64_t)position : 0;
        const bool beyond = symbol->defined && (uint64_t)size.value > remaining;
        symbol->beyond = beyond ? (uint64_t)size.value : 0;
        parser->beyond_count += beyond;
        if (common)
        {
            const AtomId atom = link_add_atom(link, BUSTER_LINK_CLASS_BSS, (uint8_t)__builtin_ctz(symbol->common_alignment), symbol->common_size, 0, 0);
            symbol->flags = (uint8_t)((symbol->flags & ~BUSTER_LINK_SYMBOL_TYPE) | BUSTER_LINK_SYMBOL_COMMON);
            symbol->link = link_add_symbol(link, name, atom, 0, symbol->common_size, symbol->flags, BUSTER_LINK_SOURCE_PROGRAM);
        }
        else
        {
            symbol->link = link_add_symbol(link, name, symbol->defined ? parser->sections[definition->section].atom : 0, (uint64_t)position, beyond ? remaining : (uint64_t)size.value, symbol->flags, symbol->defined ? BUSTER_LINK_SOURCE_PROGRAM : BUSTER_LINK_SOURCE_NONE);
        }
    }
    parser->result.directives = link->symbols.count != 0 ? BUSTER_ARENA_PUSH_ARRAY(parser->arena, uint8_t, link->symbols.count) : 0;
    if (link->symbols.count != 0 && parser->result.directives == 0)
    {
        asm_fail(parser, 0, "arena exhausted recording symbol directives");
    }
    else if (parser->result.directives != 0)
    {
        memset(parser->result.directives, 0, link->symbols.count);
        for (uint32_t i = 1; i < parser->symbol_count; i += 1)
        {
            const AsmSymbol* symbol = &parser->symbols[i];
            if (symbol->link != 0)
            {
                parser->result.directives[symbol->link] |=
                    (uint8_t)((symbol->explicit_binding ? BUSTER_ASM_DIRECTIVE_BINDING : 0) | (symbol->has_size ? BUSTER_ASM_DIRECTIVE_SIZE : 0) |
                              (symbol->common == ASM_COMMON_BSS ? BUSTER_ASM_DIRECTIVE_LOCAL_COMMON : 0));
            }
        }
    }
    for (uint32_t section = 1; section < parser->section_count && parser->result.error == 0; section += 1)
    {
        const AtomId atom = parser->sections[section].atom;
        if (atom == 0)
        {
            continue;
        }
        const uint32_t end = parser->fragment_begin[section + 1];
        for (uint32_t position = parser->fragment_begin[section]; position < end && parser->result.error == 0; position += 1)
        {
            const uint32_t f = parser->fragment_order[position];
            AsmFragment* fragment = &parser->fragments[f];
            uint8_t* bytes = link->atoms.bytes[atom];
            if (fragment->kind == ASM_VALUE)
            {
                asm_data_value(parser, atom, fragment);
            }
            else if (fragment->kind == ASM_INSTRUCTION)
            {
                X86Instruction records[256];
                uint32_t fragments[256];
                uint32_t count = 0;
                uint32_t size = 0;
                uint32_t next = position;
                const bool address32 = parser->instructions[fragment->instruction].address32;
                while (next < end && count < (address32 ? 1u : 256u))
                {
                    const uint32_t id = parser->fragment_order[next];
                    AsmFragment* item = &parser->fragments[id];
                    if ((item->kind != ASM_INSTRUCTION && item->kind != ASM_LABEL) || (count != 0 && item->kind == ASM_INSTRUCTION && parser->instructions[item->instruction].address32))
                    {
                        break;
                    }
                    if (item->kind == ASM_INSTRUCTION)
                    {
                        fragments[count] = id;
                        records[count++] = parser->instructions[item->instruction].record;
                        size += item->size;
                    }
                    next += 1;
                }
                const uint64_t capacity = link->atoms.size[atom] - fragment->offset + BUSTER_X86_ENCODER_CAPACITY(256);
                const int64_t encoded = address32 ? asm_emit_address32(parser, records, bytes + fragment->offset)
                                      : BUSTER_VECTOR_PATH(parser->options.vectorized) ? x86_encode(records, count, bytes + fragment->offset, capacity, 0, 0)
                                                                                        : x86_encode_scalar(records, count, bytes + fragment->offset, capacity, 0, 0);
                if (encoded != size)
                {
                    asm_fail(parser, fragment->location, "encoder length disagrees with assembly layout");
                }
                else
                {
                    for (uint32_t j = 0; j < count; j += 1)
                    {
                        asm_instruction_fixups(parser, atom, &parser->instructions[parser->fragments[fragments[j]].instruction]);
                    }
                }
                position = next - 1;
            }
            else if (fragment->kind == ASM_BYTES)
            {
                if (bytes == 0)
                {
                    for (uint32_t j = 0; j < fragment->size; j += 1)
                    {
                        if (fragment->bytes[j] != 0)
                        {
                            asm_fail(parser, fragment->location, ".bss may contain only zero fill");
                        }
                    }
                }
                else
                {
                    memcpy(bytes + fragment->offset, fragment->bytes, fragment->size);
                }
            }
            else if (fragment->kind == ASM_ULEB || fragment->kind == ASM_SLEB)
            {
                const bool is_signed = fragment->kind == ASM_SLEB;
                const AsmExpr expression = asm_resolve(parser, fragment->expression, fragment->location);
                __int128 value = 0;
                if (!asm_leb_value(parser, expression, &value))
                {
                    asm_fail(parser, fragment->location, "LEB128 value must be an absolute value, a label difference or a label offset");
                }
                else if (bytes != 0)
                {
                    const uint32_t size = asm_leb_write(bytes + fragment->offset, value, is_signed);
                    if (size != fragment->size)
                    {
                        asm_fail(parser, fragment->location, "LEB128 size disagrees with layout");
                    }
                }
                else if (value != 0)
                {
                    asm_fail(parser, fragment->location, ".bss may contain only zero fill");
                }
            }
            else if (fragment->kind == ASM_FILL || fragment->kind == ASM_ALIGN)
            {
                if (bytes != 0 && fragment->padding != ASM_PADDING_FILL)
                {
                    asm_code_padding(bytes + fragment->offset, fragment->size, fragment->padding == ASM_PADDING_NOPS_AFTER_DATA);
                }
                else if (bytes != 0)
                {
                    memset(bytes + fragment->offset, fragment->fill, fragment->size);
                }
                else if (fragment->fill != 0 && fragment->size != 0)
                {
                    asm_fail(parser, fragment->location, ".bss may contain only zero fill");
                }
            }
        }
    }
    for (uint32_t i = 0; !parser->cfi_no_eh_frame && i < parser->procedure_count && parser->result.error == 0; i += 1)
    {
        const AsmCfiProcedure* procedure = &parser->procedures[i];
        link_add_fixup(link, parser->sections[BUSTER_LINK_CLASS_EH_FRAME].atom, procedure->relocation, BUSTER_LINK_FIXUP_PC32, parser->symbols[procedure->symbol].link, 0);
    }
    // .debug_frame's, each FDE's in GNU as's order: its CIE's offset in the section, then its first address.
    for (uint32_t i = 0; parser->cfi_debug_section != 0 && i < parser->procedure_count && parser->result.error == 0; i += 1)
    {
        const AsmCfiProcedure* procedure = &parser->procedures[i];
        const AtomId atom = parser->sections[parser->cfi_debug_section].atom;
        link_add_fixup(link, atom, procedure->debug_relocation, BUSTER_LINK_FIXUP_ABS32, parser->symbols[parser->cfi_debug_label].link, procedure->debug_cie);
        link_add_fixup(link, atom, procedure->debug_relocation + 4, BUSTER_LINK_FIXUP_ABS64, parser->symbols[procedure->symbol].link, 0);
    }
    // The line table's (asm_line_emit): its set addresses' and line strings' relocations, in their order.
    for (uint32_t i = 0; i < parser->line_fixup_count && parser->result.error == 0; i += 1)
    {
        const AsmLineFixup* fixup = &parser->line_fixups[i];
        link_add_fixup(link, parser->sections[parser->line_section].atom, fixup->offset, fixup->kind, parser->symbols[fixup->symbol].link, 0);
    }
    asm_object_options(parser);
    parser->result.common_count = parser->common_count;
    // A record link_add_* refused leaves the Link's error, not a record link_check would see.
    if (parser->result.error == 0 && (link->error != LINK_ERROR_NONE || link_check(link) != LINK_ERROR_NONE))
    {
        asm_fail(parser, 0, "invalid link representation (symbol size, capacity or relocation)");
    }
}

// The source's tokens, asm_assemble's and asm_assemble_code's first phase: the lexer's records, and four lookahead
// sentinels past them.
static void asm_lex_source(AsmParser* parser)
{
    const String8 source = parser->source;
    Arena* arena = parser->arena;
    if (source.length > BUSTER_ASM_MAX_SOURCE || (source.length != 0 && source.pointer == 0))
    {
        asm_fail(parser, 0, "invalid source or source exceeds the 32-bit offset implementation limit");
    }
    else
    {
        parser->tokens = BUSTER_ARENA_PUSH_ARRAY(arena, AsmToken, source.length + 4);
        if (parser->tokens == 0)
        {
            asm_fail(parser, 0, "arena exhausted lexing source");
        }
        else
        {
            asm_lex(parser);
            // Lexing may emit one token per byte, but retain only the actual records and four lookahead sentinels.
            // Every lexer store initializes the full record. No full-source clearing or unused-token working set.
            memset(parser->tokens + parser->token_count, 0, 4 * sizeof(AsmToken));
            parser->tokens[parser->token_count].start = source.length;
            parser->tokens[parser->token_count].end = source.length;
            arena->length = (uint8_t*)(parser->tokens + parser->token_count + 4) - arena->base;
        }
    }
}

// The second phase: the tables, as large as the tokens may need, and the statements parsed into them.
static void asm_parse_source(AsmParser* parser)
{
    Arena* arena = parser->arena;
    if (parser->token_count > BUSTER_ASM_MAX_TOKENS)
    {
        asm_fail(parser, 0, "assembly token count exceeds the 32-bit symbol-table implementation limit");
    }
    if (parser->result.error == 0)
    {
        parser->capacity = parser->token_count + 4;
        if (BUSTER_VECTOR_PATH(parser->options.vectorized))
        {
            asm_cache_registers(parser);
        }
        uint32_t slots = 16;
        while (slots < parser->capacity * 2)
        {
            slots *= 2;
        }
        parser->symbol_mask = slots - 1;
        parser->symbol_slots = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
        parser->symbols = BUSTER_ARENA_PUSH_ARRAY(arena, AsmSymbol, parser->capacity);
        parser->fragments = BUSTER_ARENA_PUSH_ARRAY(arena, AsmFragment, parser->capacity);
        parser->instructions = BUSTER_ARENA_PUSH_ARRAY(arena, AsmInstruction, parser->instruction_capacity);
        parser->section_capacity = parser->instruction_capacity + BUSTER_LINK_CLASS_COUNT;
        parser->section_capacity = parser->section_capacity < BUSTER_ASM_MAX_SECTIONS ? parser->section_capacity : BUSTER_ASM_MAX_SECTIONS;
        parser->sections = BUSTER_ARENA_PUSH_ARRAY(arena, AsmSection, parser->section_capacity);
        parser->section_stack = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, parser->section_capacity);
        if (parser->symbol_slots == 0 || parser->symbols == 0 || parser->fragments == 0 || parser->instructions == 0 || parser->sections == 0 ||
            parser->section_stack == 0)
        {
            asm_fail(parser, 0, "arena exhausted allocating parser tables");
        }
        else
        {
            memset(parser->symbol_slots, 0, slots * sizeof(uint32_t));
            parser->symbols[0] = (AsmSymbol){0};
            asm_sections_initialize(parser);
            asm_fragment(parser, ASM_LABEL, 0);
            asm_parse(parser);
        }
    }
}

static AsmResult asm_assemble(String8 source, const X86MatchIndex* index, AsmOptions options, Arena* arena)
{
    AsmParser parser =
        {
            .source = source,
            .arena = arena,
            .index = index,
            .options = options,
            .section = BUSTER_LINK_CLASS_TEXT,
            .previous_section = BUSTER_LINK_CLASS_TEXT,
            .prefix = !options.intel,
            .symbol_count = 1,
    };
    const uint64_t saved = arena->length;
    const uint64_t start = asm_clock();
    asm_lex_source(&parser);
    const uint64_t lexed = asm_clock();
    asm_parse_source(&parser);
    const uint64_t parsed = asm_clock();
    if (parser.result.error == 0)
    {
        asm_match_instructions(&parser);
    }
    const uint64_t matched = asm_clock();
    if (parser.result.error == 0)
    {
        asm_layout(&parser);
    }
    const uint64_t laid_out = asm_clock();
    if (parser.result.error == 0)
    {
        asm_cfi_emit(&parser);
    }
    if (parser.result.error == 0)
    {
        asm_line_emit(&parser);
    }
    const uint64_t cfi_emitted = asm_clock();
    if (parser.result.error == 0)
    {
        asm_emit(&parser);
    }
    const uint64_t emitted = asm_clock();
    parser.result.lex_ns = lexed - start;
    parser.result.parse_ns = parsed - lexed;
    parser.result.match_ns = matched - parsed;
    parser.result.layout_ns = laid_out - matched;
    parser.result.emit_ns = emitted - laid_out;
    parser.result.cfi_ns = cfi_emitted - laid_out;
    parser.result.instructions = parser.instruction_count;
    if (parser.result.error != 0)
    {
        arena->length = saved;
        parser.result.link = (Link){0};
        parser.result.object_options = (ObjectOptions){0};
        parser.result.absolutes = 0;
        parser.result.absolute_count = 0;
        parser.result.directives = 0;
    }
    return parser.result;
}

// A symbolic field of asm_assemble_code's records: what the field refers to, the symbol `name` (as the source spells it,
// in output's bytes) plus `addend`, as a LinkReference has it, the name standing for its target.
typedef struct AsmCodeReference AsmCodeReference;
struct AsmCodeReference
{
    String8 name;
    int64_t addend;
    uint32_t record;
    // X86_FIXUP_DISPLACEMENT or X86_FIXUP_IMMEDIATE, and BUSTER_LINK_FIXUP_*.
    uint8_t field;
    uint8_t kind;
};

// A label asm_assemble_code's source defines by name (a numeric label or `.` has none): its name, in output's bytes, and
// its next record (the count for the end), where the caller's symbol of it goes.
typedef struct AsmCodeLabel AsmCodeLabel;
struct AsmCodeLabel
{
    String8 name;
    uint32_t record;
};

// What asm_assemble_code makes of a source: `count` records, an instruction each, in order; every jmp and jcc to a label
// of the source's without an addend a rel8 with displacement 0, in `branches` (their indices, ascending) and `targets`
// (each label's next record, or the count for the end), branch sizing's columns; its symbolic fields (`references`, a
// record's displacement's before its immediate's, in record order), each record with one marked BUSTER_X86_FORM_FIXUP;
// and its named labels (`labels`, in the order the source first names them). Or an `error` at the source's byte
// `location`, with nothing kept.
typedef struct AsmCode AsmCode;
struct AsmCode
{
    X86Instruction* records;
    uint32_t* branches;
    uint32_t* targets;
    AsmCodeReference* references;
    AsmCodeLabel* labels;
    const char* error;
    // The instruction the error names, a slice of the source, or empty (AsmResult.error_name).
    String8 error_name;
    uint32_t count;
    uint32_t branch_count;
    uint32_t reference_count;
    uint32_t label_count;
    uint32_t location;
    // The source used a directive besides the syntax switches, or a numeric label as data or a loop's or jrcxz's branch,
    // which need a whole assembled unit (asm_assemble).
    bool directive;
};

// Assembles a source whose instructions another function's records hold (#147: a C asm statement's template, after its
// operands are substituted), in place among them: instructions and labels, in either syntax and switching with
// .intel_syntax and .att_syntax, which are its only directives. It has no sections, data or symbols of its own, so it
// refuses every other directive. A jmp or jcc to a label of its own, without an addend, is a branch that branch sizing
// sizes. Every other symbolic field refers to a symbol by name, which the caller resolves: one of the source's named
// labels (as data, a call's target, or with an addend), a label another source defines (a `.L` one among them), or any
// other symbol. Its numeric labels and `.` are only its own jmp's and jcc's targets: another use of one, and a loop's or
// a jrcxz's branch to a label, fail with `directive` set, for the caller to assemble the source whole. Matching is
// asm_assemble's, by
// options.vectorized's path; the parser's tables come from `scratch`, which is as it was after, and the result from
// `output`, which a failure leaves as it was too.
static AsmCode asm_assemble_code(String8 source, const X86MatchIndex* index, AsmOptions options, Arena* output, Arena* scratch)
{
    AsmParser parser =
        {
            .source = source,
            .arena = scratch,
            .index = index,
            .options = options,
            .section = BUSTER_LINK_CLASS_TEXT,
            .previous_section = BUSTER_LINK_CLASS_TEXT,
            .prefix = !options.intel,
            .symbol_count = 1,
            .code = true,
    };
    AsmCode result = { 0 };
    const uint64_t saved = output->length;
    const uint64_t temporary = scratch->length;
    asm_lex_source(&parser);
    asm_parse_source(&parser);
    if (parser.result.error == 0)
    {
        asm_match_instructions(&parser);
    }
    const uint32_t count = parser.instruction_count;
    uint32_t label_count = 0;
    for (uint32_t s = 1; parser.result.error == 0 && s < parser.symbol_count; s += 1)
    {
        label_count += parser.symbols[s].defined && !parser.symbols[s].internal;
    }
    // Each label's next record: the instructions before its fragment.
    uint32_t* positions = parser.result.error == 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)parser.fragment_count + 1) : 0;
    result.records = parser.result.error == 0 && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, X86Instruction, count) : 0;
    result.branches = parser.result.error == 0 && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, count) : 0;
    result.targets = parser.result.error == 0 && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, count) : 0;
    result.references = parser.result.error == 0 && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, AsmCodeReference, 2 * (uint64_t)count) : 0;
    result.labels = parser.result.error == 0 && label_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, AsmCodeLabel, label_count) : 0;
    if (parser.result.error == 0 && (positions == 0 || (count != 0 && (result.records == 0 || result.branches == 0 || result.targets == 0 || result.references == 0)) ||
                                     (label_count != 0 && result.labels == 0)))
    {
        asm_fail(&parser, 0, "arena exhausted placing inline assembly's records");
    }
    // The branches of no rel32 form (loop, loope, loopne, jrcxz) to the source's own labels, each record's index and its
    // target's, whose displacements are fixed after the records are (#1839's item 4).
    uint32_t* fixed = parser.result.error == 0 && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, 2 * (uint64_t)count) : 0;
    uint32_t fixed_count = 0;
    if (parser.result.error == 0 && count != 0 && fixed == 0)
    {
        asm_fail(&parser, 0, "arena exhausted placing inline assembly's records");
    }
    uint32_t at = 0;
    for (uint32_t f = 0; parser.result.error == 0 && f < parser.fragment_count; f += 1)
    {
        positions[f] = at;
        at += parser.fragments[f].kind == ASM_INSTRUCTION;
    }
    for (uint32_t s = 1; parser.result.error == 0 && s < parser.symbol_count; s += 1)
    {
        const AsmSymbol* symbol = &parser.symbols[s];
        char* name = symbol->defined && !symbol->internal ? arena_push(output, symbol->name.length, 1) : 0;
        if (symbol->defined && !symbol->internal && name == 0)
        {
            asm_fail(&parser, 0, "arena exhausted placing inline assembly's records");
        }
        else if (name != 0)
        {
            memcpy(name, symbol->name.pointer, symbol->name.length);
            result.labels[result.label_count++] = (AsmCodeLabel){ .name = { .pointer = name, .length = symbol->name.length }, .record = positions[symbol->fragment] };
        }
    }
    for (uint32_t i = 0; parser.result.error == 0 && i < count; i += 1)
    {
        AsmInstruction* instruction = &parser.instructions[i];
        X86Instruction record = instruction->record;
        const X86FormDescription* form = x86_form_description(record.form);
        // A branch to a label of the source's: sized as a jmp or jcc (its rel8 form, which grows to the rel32 one after
        // it) without an addend; else, to a named label, a reference to it from the branch's rel32 form.
        bool referred = !instruction->local_branch;
        if (instruction->local_branch)
        {
            const uint32_t row = record.form;
            const AsmSymbol* target = &parser.symbols[instruction->expressions[0].plus];
            const bool sized = form->relative_bytes == 1 && row + 1 < X86_FORM_COUNT && x86_form_descriptions[row + 1].relative_bytes == 4 &&
                               strcmp(form->mnemonic, x86_form_descriptions[row + 1].mnemonic) == 0;
            if (sized && instruction->expressions[0].value == 0)
            {
                result.branches[result.branch_count] = i;
                result.targets[result.branch_count++] = positions[target->fragment];
            }
            else if (!sized && form->relative_bytes == 1 && target->defined && instruction->expressions[0].value == 0)
            {
                // A loop's or a jrcxz's to a label of the source's: a record whose rel8 displacement is fixed below, where
                // no branch that branch sizing sizes lies between it and its target.
                fixed[2 * fixed_count] = i;
                fixed[2 * fixed_count + 1] = positions[target->fragment];
                fixed_count += 1;
            }
            else if (!target->internal && (sized || form->relative_bytes == 4))
            {
                record.form = (uint16_t)(sized ? row + 1 : row);
                form = x86_form_description(record.form);
                referred = true;
            }
            else
            {
                // loop, loope, loopne and jrcxz have no rel32 form for a reference (an undefined label's), and branch
                // sizing sizes only jmp and jcc: the caller assembles the source whole instead (`directive`, asm_assemble),
                // as a template of directives.
                asm_fail(&parser, instruction->location, form->relative_bytes == 1 && !sized ? "loop, loope, loopne and jrcxz in inline assembly" :
                                                                                                "a branch to inline assembly's own numeric label must be jmp or jcc, without an addend");
                parser.directive = true;
            }
        }
        for (uint32_t field = X86_FIXUP_DISPLACEMENT; referred && field <= X86_FIXUP_IMMEDIATE && parser.result.error == 0; field += 1)
        {
            for (uint32_t j = 0; j < instruction->request.count && parser.result.error == 0; j += 1)
            {
                const X86Operand* operand = &instruction->request.operands[j];
                const bool displacement = operand->kind == X86_OPERAND_MEMORY || instruction->branch;
                const AsmExpr expression = instruction->expressions[j];
                const AsmSymbol* symbol = &parser.symbols[expression.plus];
                if (expression.plus == 0 || (field == X86_FIXUP_DISPLACEMENT) != displacement)
                {
                    continue;
                }
                const bool pc_relative = instruction->branch || (operand->mode & BUSTER_X86_MODE_RIP);
                const uint32_t width = displacement ? 4 : form->immediate_bytes;
                const bool truncated_lea = strcmp(form->mnemonic, "lea") == 0 && !form->w;
                const bool signed_field = (displacement && !truncated_lea) || x86_match_sign_extends(form);
                uint8_t kind = instruction->branch ? BUSTER_LINK_FIXUP_CALL : pc_relative ? BUSTER_LINK_FIXUP_PC32 : width == 8 ? BUSTER_LINK_FIXUP_ABS64
                                                                                  : signed_field ? BUSTER_LINK_FIXUP_ABS32S
                                                                                                 : BUSTER_LINK_FIXUP_ABS32;
                if (expression.relocation == ASM_RELOCATION_PLT)
                {
                    if (!instruction->branch || !displacement)
                    {
                        asm_fail(&parser, instruction->location, "@PLT requires a call or jump displacement");
                    }
                    kind = BUSTER_LINK_FIXUP_CALL;
                }
                else if (expression.relocation == ASM_RELOCATION_GOTPCREL || expression.relocation == ASM_RELOCATION_GOTPCRELX)
                {
                    if (!displacement || (operand->mode & BUSTER_X86_MODE_RIP) == 0)
                    {
                        asm_fail(&parser, instruction->location, "@GOTPCREL requires a RIP-relative memory displacement");
                    }
                    const uint32_t row = record.form & BUSTER_X86_FORM_NUMBER;
                    const bool relaxed = expression.value == 0;
                    kind = relaxed && (row == X86_FORM_CALL_RM64 || row == X86_FORM_JMP_RM64) ? BUSTER_LINK_FIXUP_CALL_GOT
                         : relaxed && row == X86_FORM_MOV_R64_RM64                               ? BUSTER_LINK_FIXUP_LOAD_GOT
                                                                                                : BUSTER_LINK_FIXUP_GOTPCREL;
                }
                else if (expression.relocation != ASM_RELOCATION_NONE)
                {
                    // A C template's references take the code generators' kinds (link_kind_fits), which the 64-bit TLS
                    // offsets aren't.
                    kind = asm_tls_kind(&parser, instruction->location, expression.relocation, displacement && (operand->mode & BUSTER_X86_MODE_RIP) != 0,
                                        !pc_relative && width == 4 && signed_field, false);
                }
                char* name = !symbol->internal && symbol->name.length != 0 ? arena_push(output, symbol->name.length, 1) : 0;
                if (symbol->internal && symbol->defined)
                {
                    // A numeric label or `.` as data, an address or a call's target is no name to refer to: the caller
                    // assembles the source whole instead (`directive`, asm_assemble), whose own fixups take it.
                    asm_fail(&parser, instruction->location, "inline assembly's numeric labels and `.` are only its own jmp's and jcc's targets");
                    parser.directive = true;
                }
                else if (symbol->internal)
                {
                    asm_fail(&parser, symbol->location, "undefined local symbol");
                }
                else if (expression.value < INT64_MIN || expression.value > INT64_MAX || (width != 4 && width != 8))
                {
                    asm_fail(&parser, instruction->location, "unrepresentable instruction relocation");
                }
                else if (name == 0)
                {
                    asm_fail(&parser, instruction->location, "arena exhausted placing inline assembly's records");
                }
                else
                {
                    // A symbol the source doesn't define may be preempted, so a call or jump to it is a CALL (PLT32), as
                    // asm_instruction_fixups makes an undefined symbol's; and so is a branch to a label of the source's
                    // that it doesn't size.
                    memcpy(name, symbol->name.pointer, symbol->name.length);
                    result.references[result.reference_count++] = (AsmCodeReference){
                        .name = { .pointer = name, .length = symbol->name.length },
                        .addend = (int64_t)expression.value,
                        .record = i,
                        .field = (uint8_t)field,
                        .kind = kind,
                    };
                    record.form |= BUSTER_X86_FORM_FIXUP;
                }
            }
        }
        result.records[i] = record;
    }
    // The fixed branches' displacements: from the record after each to its target, by the records' lengths, where no
    // sized branch lies between them (whose growth would move the target) and the distance fits a rel8. Otherwise the
    // caller assembles the source whole, as before.
    uint32_t* offsets = parser.result.error == 0 && fixed_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    X86Instruction* measured = offsets != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, X86Instruction, count) : 0;
    for (uint32_t r = 0; measured != 0 && r < count; r += 1)
    {
        measured[r] = result.records[r];
        measured[r].form &= (uint16_t)~BUSTER_X86_FORM_FIXUP;
    }
    const int64_t length = measured == 0 ? -1 : BUSTER_VECTOR_PATH(options.vectorized) ? x86_measure(measured, count, offsets) : x86_measure_scalar(measured, count, offsets);
    if (parser.result.error == 0 && fixed_count != 0 && length < 0)
    {
        asm_fail(&parser, 0, measured == 0 ? "arena exhausted placing inline assembly's records" : "inline assembly's records don't encode");
    }
    if (offsets != 0 && length >= 0)
    {
        offsets[count] = (uint32_t)length;
    }
    for (uint32_t k = 0; parser.result.error == 0 && k < fixed_count; k += 1)
    {
        const uint32_t r = fixed[2 * k];
        const uint32_t t = fixed[2 * k + 1];
        const uint32_t low = t <= r ? t : r + 1;
        const uint32_t high = t <= r ? r + 1 : t;
        bool between = false;
        for (uint32_t b = 0; b < result.branch_count; b += 1)
        {
            between |= result.branches[b] >= low && result.branches[b] < high;
        }
        const int64_t displacement = (int64_t)offsets[t] - (int64_t)offsets[r + 1];
        if (between || displacement < INT8_MIN || displacement > INT8_MAX)
        {
            asm_fail(&parser, parser.instructions[r].location, "loop, loope, loopne and jrcxz in inline assembly");
            parser.directive = true;
        }
        else
        {
            result.records[r].displacement = (int32_t)displacement;
        }
    }
    if (parser.result.error != 0)
    {
        output->length = saved;
        result = (AsmCode){ .error = parser.result.error, .error_name = parser.result.error_name, .location = parser.result.location, .directive = parser.directive };
    }
    else
    {
        result.count = count;
    }
    scratch->length = temporary;
    return result;
}

#pragma once

// Record shapes of the canonical typed IR: the format-neutral types,
// values, instructions, blocks, functions, symbols, globals, and source
// ranges every frontend lowers into and every backend consumes. Everything
// is integer-ID based and arena-owned; the typed wrappers below exist so a
// type id cannot be handed where a value id belongs. This layer is shared:
// frontend entity ids, parser pointers, and language-specific sentinels
// must not appear here (AGENTS.md). Construction and validation live in
// ir.h/ir.c.

#include <buster/lib/base.h>

typedef u32 IrIdUnderlying;

typedef struct IrTypeId IrTypeId;
struct IrTypeId
{
    IrIdUnderlying value;
};

typedef struct IrSymbolId IrSymbolId;
struct IrSymbolId
{
    IrIdUnderlying value;
};

typedef struct IrLocalId IrLocalId;
struct IrLocalId
{
    IrIdUnderlying value;
};

typedef struct IrSourceId IrSourceId;
struct IrSourceId
{
    IrIdUnderlying value;
};

#define IR_ID_UNDERLYING_INVALID UINT32_MAX
#define IR_TYPE_ID_INVALID ((IrTypeId){.value = IR_ID_UNDERLYING_INVALID})
#define IR_SYMBOL_ID_INVALID ((IrSymbolId){.value = IR_ID_UNDERLYING_INVALID})
#define IR_LOCAL_ID_INVALID ((IrLocalId){.value = IR_ID_UNDERLYING_INVALID})
#define IR_SOURCE_ID_INVALID ((IrSourceId){.value = IR_ID_UNDERLYING_INVALID})

BUSTER_CT_CHECK(sizeof(IrTypeId) == sizeof(IrIdUnderlying));
BUSTER_CT_CHECK(sizeof(IrSymbolId) == sizeof(IrIdUnderlying));
BUSTER_CT_CHECK(sizeof(IrLocalId) == sizeof(IrIdUnderlying));
BUSTER_CT_CHECK(sizeof(IrSourceId) == sizeof(IrIdUnderlying));

// What an IR instruction remembers about where it came from: the source it
// belongs to, and the byte offset and length of its spelling in the byte
// space the producing frontend maps (see IrSourceMap). Line and column are
// deliberately absent — recovering them costs a search per query, and the
// number of ranges a lowering builds is orders of magnitude larger than the
// number a compile ever asks a line of. They resolve through
// `ir_source_position` at the only four places that need them: diagnostic
// formatting, DWARF line-table generation, CodeView line generation, and
// source-navigation requests.
typedef struct IrSourceRange IrSourceRange;
struct IrSourceRange
{
    IrSourceId source;
    u32 offset;
    u32 length;
};

// A range is copied by value through the whole lowering surface and stored
// once per instruction, so its size is part of the design, not an accident.
BUSTER_CT_CHECK(sizeof(IrSourceRange) == 12);

// One range resolved: the source it lands in, the byte offset within that
// source's own text, and the one-based line and column there. `line == 0`
// means the range carried no position — an unresolvable offset, a frontend
// that published no map, or a range that was never filled in.
typedef struct IrSourcePosition IrSourcePosition;
struct IrSourcePosition
{
    u32 source;
    u32 offset;
    u32 line;
    u32 column;
};

// A retained line start: the offset in the source's own text where a line
// begins, and the line/column there. Offset and column advance one per byte
// from a checkpoint until the next one, so a checkpoint is needed only where
// that linearity breaks — a newline, or a byte the frontend deleted while
// translating (a C line splice, a stripped carriage return).
typedef struct IrSourceCheckpoint IrSourceCheckpoint;
struct IrSourceCheckpoint
{
    u32 offset;
    u32 line;
    u32 column;
};

typedef enum IrSourceRegionKind
{
    // Offsets resolve through the checkpoints, shifted by `line_delta`.
    IR_SOURCE_REGION_TEXT,
    // Every offset in the region resolves to `stamp`: a run of bytes with no
    // source text of its own, such as one macro invocation's output.
    IR_SOURCE_REGION_STAMP,
} IrSourceRegionKind;

// One region of the mapped byte space, keyed by its first offset; a region
// covers up to the next region's start (regions are sorted by start, gaps
// belong to the preceding one).
typedef struct IrSourceRegion IrSourceRegion;
struct IrSourceRegion
{
    u32 start;
    u32 source;
    IrSourceCheckpoint* checkpoints;
    // Mapped-space offsets of the checkpoints, relative to `base`.
    u32* checkpoint_offsets;
    // Checkpoint covering the first byte of each page of the region's text,
    // the same bracket the region search gets from IrSourceMap.pages: a line
    // table walks lines, so a query lands on a different checkpoint than the
    // last about half the time and the search that follows is the cost that
    // matters.
    u32* checkpoint_pages;
    u32 checkpoint_page_count;
    u32 checkpoint_count;
    u32 base;
    // What `#line` (or any other renumbering directive) moved this region's
    // lines by, relative to what the checkpoints recorded.
    s64 line_delta;
    IrSourcePosition stamp;
    IrSourceRegionKind kind;
    u32 reserved;
};

// Everything a lookup needs before it knows which region it wants: the
// region's first offset, and the source it belongs to. Split out of the
// region because a 64-byte region row puts consecutive `start` fields on
// different cache lines, and both the region search and the answer to "which
// source is this offset in" — the query every lowered instruction runs — read
// nothing else. Eight keys share a line, so the search strides one line per
// three steps instead of one per step, and a containment check that hits
// touches exactly one.
typedef struct IrSourceRegionKey IrSourceRegionKey;
struct IrSourceRegionKey
{
    u32 start;
    u32 source;
};

// The frontend's byte space, partitioned into regions. The page index holds
// the region covering each 1 KB page's first byte, so a lookup is one load
// and a short bounded search instead of a binary search over every region.
typedef struct IrSourceMap IrSourceMap;
struct IrSourceMap
{
    // `count + 1` entries: the last is a sentinel starting at UINT32_MAX, so
    // a containment check never needs an end-of-array branch.
    IrSourceRegionKey* keys;
    IrSourceRegion* regions;
    u32* pages;
    u32 count;
    u32 page_count;
};

enum
{
    IR_SOURCE_MAP_PAGE_SHIFT = 10,
    // Source lines average tens of bytes, so a page this size brackets the
    // checkpoint search to a handful of candidates.
    IR_SOURCE_CHECKPOINT_PAGE_SHIFT = 7,
};

// Amortizes the two searches a lookup would otherwise run. Consumers query
// in roughly ascending offset order, so the remembered region and checkpoint
// answer most queries with a containment check. Text and stamp regions get a
// slot each: a line's macro-expanded output sits far from the file bytes
// around it, so one slot would thrash on every interleave.
typedef struct IrSourceMapCursor IrSourceMapCursor;
struct IrSourceMapCursor
{
    u32 text_region;
    u32 checkpoint;
    u32 stamp_region;
    // Repeat queries for one offset are common (every instruction of one
    // expression asks about the same token); the memo answers them with one
    // compare. UINT32_MAX = empty.
    u32 memo_offset;
    IrSourcePosition memo_position;
};

#define IR_SOURCE_MAP_CURSOR_EMPTY ((IrSourceMapCursor){.memo_offset = UINT32_MAX})

typedef enum IrTypeKind
{
    IR_TYPE_VOID,
    IR_TYPE_BOOLEAN,
    IR_TYPE_INTEGER,
    IR_TYPE_FLOAT,
    IR_TYPE_VA_LIST,
    IR_TYPE_POINTER,
    IR_TYPE_SLICE,
    IR_TYPE_ARRAY,
    IR_TYPE_VECTOR,
    IR_TYPE_FUNCTION,
    IR_TYPE_RANGE,
    IR_TYPE_STRUCT,
    IR_TYPE_UNION,
    IR_TYPE_ENUM,
    IR_TYPE_COUNT,
} IrTypeKind;

typedef enum IrCallingConvention
{
    IR_CALLING_CONVENTION_C,
    IR_CALLING_CONVENTION_SYSTEMV,
    IR_CALLING_CONVENTION_WIN64,
    IR_CALLING_CONVENTION_COUNT,
} IrCallingConvention;

typedef enum IrAbiClass
{
    IR_ABI_CLASS_NONE,
    IR_ABI_CLASS_INTEGER,
    IR_ABI_CLASS_FLOAT,
    IR_ABI_CLASS_VECTOR,
    IR_ABI_CLASS_POINTER,
    IR_ABI_CLASS_AGGREGATE,
    IR_ABI_CLASS_MEMORY,
    // System V x86-64 uses this pair for an 80-bit x87 value.  The upper
    // eightbyte is padding/storage rather than a second independent value,
    // but keeping the X87_UP class explicit lets aggregate postprocessing
    // distinguish a valid long-double result from an incompatible merge.
    IR_ABI_CLASS_X87,
    IR_ABI_CLASS_X87_UP,
    IR_ABI_CLASS_COUNT,
} IrAbiClass;

typedef enum IrAbiConvention
{
    IR_ABI_CONVENTION_SYSTEMV_X86_64,
    IR_ABI_CONVENTION_WIN64_X86_64,
    IR_ABI_CONVENTION_AAPCS64,
    IR_ABI_CONVENTION_DARWIN_AARCH64,
    IR_ABI_CONVENTION_WINDOWS_AARCH64,
    IR_ABI_CONVENTION_COUNT,
} IrAbiConvention;

typedef enum IrAbiUse
{
    IR_ABI_USE_ARGUMENT,
    IR_ABI_USE_RESULT,
    IR_ABI_USE_VARIADIC_ARGUMENT,
    IR_ABI_USE_COUNT,
} IrAbiUse;

enum
{
    IR_ABI_MAX_PARTS = 4,
};

typedef struct IrAbiPart IrAbiPart;
struct IrAbiPart
{
    IrAbiClass abi_class;
    u32 value_offset;
    u32 size;
};

typedef struct IrAbiValue IrAbiValue;
struct IrAbiValue
{
    IrAbiPart parts[IR_ABI_MAX_PARTS];
    u32 part_count;
    bool indirect;
    bool memory;
    u8 reserved[2];
};

typedef struct IrTypeAbi IrTypeAbi;
struct IrTypeAbi
{
    IrAbiValue values[IR_ABI_CONVENTION_COUNT][IR_ABI_USE_COUNT];
    bool resolved[IR_ABI_CONVENTION_COUNT];
    u8 reserved[3];
};

typedef struct IrTypeLayout IrTypeLayout;
struct IrTypeLayout
{
    u64 size;
    u32 alignment;
    IrAbiClass abi_class;
    bool resolved;
    u8 reserved[3];
};

typedef struct IrField IrField;
struct IrField
{
    String8 name;
    IrSourceRange source;
    IrTypeId type;
    u64 offset;
    u32 bit_offset;
    u32 bit_width;
    bool is_bit_field;
    u8 reserved[7];
};

typedef struct IrEnumMember IrEnumMember;
struct IrEnumMember
{
    String8 name;
    IrSourceRange source;
    u64 value;
};

typedef struct IrType IrType;
struct IrType
{
    String8 name;
    IrField* fields;
    IrEnumMember* enum_members;
    IrTypeId* parameter_types;
    IrTypeId id;
    IrTypeId element_type;
    IrTypeId return_type;
    IrTypeId unqualified_type;
    IrTypeLayout layout;
    IrTypeAbi* abi;
    IrTypeKind kind;
    IrCallingConvention calling_convention;
    u64 element_count;
    u32 field_count;
    u32 enum_member_count;
    u32 parameter_count;
    u32 bit_width;
    bool is_signed;
    bool is_variadic;
    bool is_atomic;
    bool is_nullptr;
    bool is_volatile;
    // A C complex type, modelled as the two-field struct the psABIs classify
    // it as (see c_ir_complex_type). Only the C frontend sets and reads it;
    // to the backends the type is an ordinary aggregate.
    bool is_complex;
    // A function type whose declarator spelled `noreturn`, so a call through
    // it ends control flow. The marker written on a function declaration is
    // read off that declaration instead (c_ir_declaration_is_noreturn); this
    // carries the one shape no declaration can answer for -- the attribute on
    // a function pointer type or typedef, where the call site only ever sees
    // the type. Only the C frontend sets and reads it.
    bool is_noreturn;
};

typedef struct IrTypeTable IrTypeTable;
struct IrTypeTable
{
    IrType* types;
    u32 count;
    u32 capacity;
};

typedef enum IrSymbolKind
{
    IR_SYMBOL_FUNCTION,
    IR_SYMBOL_DATA,
    IR_SYMBOL_TYPE,
    IR_SYMBOL_COUNT,
} IrSymbolKind;

typedef enum IrLinkage
{
    IR_LINKAGE_INTERNAL,
    IR_LINKAGE_EXTERNAL,
    IR_LINKAGE_IMPORT,
    IR_LINKAGE_COUNT,
} IrLinkage;

typedef struct IrSymbol IrSymbol;
struct IrSymbol
{
    String8 name;
    String8 link_name;
    // Optional object-format section requested by the source declaration.
    // Direct object backends consume this without frontend-specific parsing.
    String8 section_name;
    IrSourceRange source;
    IrTypeId type;
    IrSymbolId id;
    IrSymbolKind kind;
    IrLinkage linkage;
    bool is_definition;
    bool is_thread_local;
    // A replaceable definition or a reference that may go unresolved:
    // __attribute__((weak)).  The object layer carries it as
    // ObjectSymbol.weak; see that field for what each format spells it as.
    bool is_weak;
    // Not exported from the final image (ELF STV_HIDDEN). Only a `.hidden`
    // directive in module-level assembly sets it today; the object layer
    // carries it as ObjectSymbol.hidden.
    bool is_hidden;
};

// One symbol that is a second name for another: __attribute__((alias("t"))).
// An alias owns no storage, no function body and no global initializer -- it
// takes the target definition's section, offset and size and contributes only
// its own binding -- so it is not a symbol property but a relation between
// two, and it is kept as a module-level list of the pairs.  A module's
// aliases must name definitions in that same module, which is what
// ir_validate_canonical_module checks.
typedef struct IrSymbolAlias IrSymbolAlias;
struct IrSymbolAlias
{
    IrSymbolId symbol;
    IrSymbolId target;
};

typedef struct IrSymbolTable IrSymbolTable;
struct IrSymbolTable
{
    IrSymbol* symbols;
    u32 count;
    u32 capacity;
};

typedef struct IrSource IrSource;
struct IrSource
{
    String8 path;
    // The bytes an IrSourceRange offset indexes, for frontends that keep
    // their sources whole and map them one-to-one. Empty when the program
    // carries an IrSourceMap instead, which is the case whenever the byte
    // space is not the file (the C frontend's preprocessing space holds
    // every include's translated text and every synthesized spelling).
    String8 text;
    IrSourceId id;
};

typedef struct IrSourceTable IrSourceTable;
struct IrSourceTable
{
    IrSource* sources;
    u32 count;
    u32 capacity;
};

BUSTER_F_DECL IrType* ir_type_from_id(IrTypeTable* table, IrTypeId id);
BUSTER_F_DECL IrSymbol* ir_symbol_from_id(IrSymbolTable* table, IrSymbolId id);
BUSTER_F_DECL IrSource* ir_source_from_id(IrSourceTable* table, IrSourceId id);

// The mapped byte space, resolved. `cursor` may be null; passing one across a
// walk is what keeps a sequence of lookups from re-searching.
BUSTER_F_DECL IrSourcePosition ir_source_map_position(IrSourceMap const* map, u32 offset, IrSourceMapCursor* cursor);
// Only which source the offset lands in, skipping the line/column search.
BUSTER_F_DECL u32 ir_source_map_source(IrSourceMap const* map, u32 offset, IrSourceMapCursor* cursor);
// The same, for a source the frontend handed over whole: counts the line
// breaks that precede `offset` in `text`, advancing `cursor` instead of
// rescanning when queries ascend.
BUSTER_F_DECL IrSourcePosition ir_source_text_position(String8 text, u32 source, u32 offset, IrSourceMapCursor* cursor);

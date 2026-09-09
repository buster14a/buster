# Shared compiler diagnostics

`compiler/diagnostic.h` defines the published record, ownership copy and terminal
renderer. The driver exposes an ordered `CompilerDriverResult.diagnostics` array;
`diagnostic` (first error) and `warning` remain the compatible text outputs. Stable
codes are explicit names such as `c.undeclared-identifier`,
`assembly.unknown-instruction`, `codegen.unsupported-instruction`,
`codegen.machine-fallback` and `link.unresolved-symbol`. Producer enum values are
not the public spelling. Grammar-specific construction remains in the C and
assembler frontends; driver adapters translate their records on failure/warning.

## Provenance inventory and mapping

| Producer | Available evidence | Publication |
| --- | --- | --- |
| C lexer/preprocessor | File identity, mapped byte location, physical checkpoints, `#line` remapping, macro invocation stamps | Logical primary position plus original physical path/position; directive-only includes register a name only when diagnosed |
| C parser/semantic/lowering | `CDiagnostic` point in the retained preprocessing map | Same mapping as preprocessing; no invented token length |
| Raw assembly | Input text, line, column and diagnostic length (the unit layer currently narrows instruction errors to one byte) | Bounded physical byte range in the input, clamped to the same line |
| Preprocessed assembly | Generated printer text and retained C tokens/map | Resolve an error byte back to its token lazily; undo the root input's inserted dollar separator bytes; publish a point in the originating file |
| Native backend | Canonical function/instruction or module-assembly range, refusal enum, optional reason, internal IDs | Canonical location and symbolic backend context; specific supplied reasons remain verbatim |
| Linker | Link error and symbol; merge/native-image results carry no source or archive-member reference range | Stable link code and symbol, with an explicitly unavailable source range |
| Driver/I/O/external tools | Stage error and message, sometimes a path embedded in the message | Stable stage code and unchanged evidence; no fabricated line or range |
| Debug writers | Canonical ranges and `DebugSourceLocation` resolved during output generation | Continue using their existing canonical resolver; diagnostic records do not replace debug tables |

`CompilerDiagnosticLocation.range` retains canonical mapped offsets for C and
backend diagnostics. `position.offset` and `original_position.offset` are byte
offsets in their respective source text; they are **not** interchangeable with
the mapped range offset. Source IDs belong to their translation unit. Paths and
resolved positions remain usable after that unit's arena is released.

The primary path/position follows `#line`. The original path/position retains
physical identity and line numbering. A macro expansion points to its invocation,
not to a made-up contiguous range over its expanded spelling. `range.length == 0`
is a point. `has_range == false` means no supported mapped range; `line == 0`
means no known position. A synthesized assembly separator with no originating
token leaves the position unavailable. Flat optional notes can supply additional
messages and locations; publication does not infer expansion stacks or secondary
locations that the producer did not provide.

`IrSourceRegion.origin_plus_one` uses the existing four-byte metadata slot:
TEXT regions store a physical source ID plus one, STAMP regions store an originating
mapped offset plus one, and zero means unknown. Original resolution follows stamps
iteratively with a region-count bound, so malformed cycles resolve as unavailable.
The ordinary source-position lookup remains unchanged. Token-free successful
headers stay absent from the canonical file table, preserving lazy header handling
and the self-hosting fixed point.

## Ownership, ordering and cost

The driver copies published strings, notes and optional backend context into the
result arena before releasing per-input arenas. Records remain in input order,
then pipeline stage order, then producer order. Warnings precede a later-stage
error. All records are available even though the compatibility text selects the
first error. The shared terminal renderer handles both C and assembler records
and optional notes; it adds no eager formatting on successful parse paths.

A clean compile allocates no record array. API callers can set
`CompilerDriverInvocation.suppress_diagnostic_records` to avoid retaining the
structured copies while preserving error text and warnings. This is an API-only
control, not a new command-line language option. Original provenance metadata is
retained independently because later backend errors can need it. Measurements
must distinguish that source-map cost from the cold publication/copy cost.

Backend records retain target, allocator, function, symbolic opcode/operation,
specific reason and referenced symbol when available. Internal IDs remain numeric;
`UINT32_MAX` denotes an inapplicable ID. Signature and target-exclusion fallback
prints `opcode=not-applicable`. The existing `CodegenStatistics` reason/opcode/stage
censuses remain the single owner of fallback counts; records introduce no second
counter system. Existing feature evidence is preserved in messages; the adapter
does not reconstruct required/effective features from a generic refusal.

`compiler_diagnostic_tests` covers ownership, notes, unknown locations, original
mapping and cycles, macro/include remapping, directive-only headers, multiple-input
ordering, raw/preprocessed assembly and the opt-out. Driver tests also check strict
fallback wording and its untouched output, backend refusal context and link symbols.

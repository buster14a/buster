# Compiler phase lifetimes

This contract complements the [frontend guide](agents/frontend/foundations.md),
the [pipeline contract](compiler-pipeline.md) and the
[allocation census](allocation-census.md). It records which arena owns each
consequential family of compiler state, where that state stops being read, and
the boundaries at which a phase gives back what only it used. Measurements and
the remaining stages live in the tracking issue linked from the pull requests
that introduced this file; reproduce them against the current revision before
treating them as current.

## Why phases hold arenas

A translation unit used to allocate every phase's state in one bump arena, the
unit arena, and release it only when the unit finished. Nothing was reclaimed
across a phase boundary, so a unit's peak resident size was the sum of its
phases. A page-granular trace of the stage-1 unity input (every resident page
protected at each boundary, each later touch recorded) showed most of what
preprocessing and semantic analysis leave in that arena is never read again:
the per-file lexed rows, macro records, include tables, line staging and
worst-case checkpoint reservations of preprocessing, and the per-query
type-table arrays of the semantic layout solve. Those pages were retained
through lowering, code generation, object construction and emission.

## Arenas of one C unit

| Arena | Owner and lifetime | Holds |
|---|---|---|
| Unit arena (the caller's `arena`, or a pooled per-input arena) | driver; the whole unit, then the invocation for the single-input path | sealed preprocessing result, syntax, semantic model, canonical IR, codegen module, object |
| Phase arena (`C_PHASE_ARENA_RESERVED_SIZE`) | each frontend phase (`c_preprocess`, `c_analyze_semantics_core`), created on the calling thread and retired as the phase returns; or a caller's (`CPreprocessOptions.phase_arena`, carried to semantic analysis as `CSourceMapRecovery.phase_arena`), which that caller retires | what a phase reads only while it runs (below) |
| Spelling arena (`CSourceMapRecovery.spelling_arena`) | preprocessor result; the whole unit | every spelling, including the names canonical IR borrows |
| Token and shape arenas | preprocessor result; until lowering has published | the final token stream and its shape sidecar |
| Semantic machine buffer | `c_analyze_semantics_core`; semantics | type-parse frames, expression tasks |
| Lowering arena | `c_lower_to_ir_with_options`; one function body at a time | per-function lowering state |
| Thread scratch | the thread; one call | temporaries under `scratch_begin`/`scratch_end` |
| Codegen attempt scope | `codegen_generate_canonical_module_with_trace`; one attempt | a code-buffer attempt a retry rewinds |
| Machine function scratch | code generation; one function | selected MIR, placement, schedule |

## Boundaries

**Preprocessing seal.** `c_preprocess` allocates everything in the phase arena,
above that arena's entry position. Before returning, `c_preprocess_seal` copies
the graph a later phase can reach from `CPreprocessResult` into the caller's
arena at exact sizes: the recovery record and its source map (keys, regions,
pages, and each lex's checkpoint arrays once, however many regions share them),
the symbol table (moved whole and retargeted to the caller's arena, because
parsing and lowering keep interning), the diagnostics and their messages, the
file table, the pack changes and the metrics rows. Any string the phase arena
owns is copied; a caller's path, a static spelling or a spelling-space pointer
is left alone. The phase arena is then released back to its entry position.
Size checks on every sealed structure fail the build when one changes shape.
The lexer's diagnostic rows and messages leave its temporary diagnostic arena
together for the same reason.

**Semantic layout queries.** `c_parse_type_layout_core` allocates its
query-local state (the arrays sized to the whole type table, bound token copies,
local spelling spaces, evaluation buffers) in the machine's phase arena above a
mark and releases it on return, nested queries first. The layout cache outlives
the query and grows inside one, so it stays in the result arena. Semantic
analysis takes the unit's phase arena (`CSourceMapRecovery.phase_arena`), or
creates and retires a private one when the caller gave none, and releases it
back to its entry position before returning. A machine without a phase arena
(a failed reservation, lowering's, tests') keeps each query in the arena it is
given, which costs memory, never the result. Analysis reports what it released
exactly, in `CParseResult.phase_released_bytes` and `phase_releases`, set once
at its single return so a speculative rollback of the record cannot lose them.

**Frontend release.** Once lowering has published canonical IR,
`compiler_driver_release_frontend` retires the token and shape arenas.
Canonical IR keeps only the spelling space (names) and the sealed source map.
Every exit of `compiler_driver_execute_c_single` after preprocessing reaches
it, so failure paths release too, and it leaves null handles so a stale use
through the driver's result faults at the null.

A phase arena is rewound, not unmapped, within its phase. `arena_retire` ends
an arena on its creating thread: it releases the whole arena, returns
committed pages beyond a retained prefix (`C_PHASE_ARENA_RETAINED_SIZE`) to the
OS and parks the mapping for the next phase or unit on that thread (see the
same-thread rule in the [parallelism guide](agents/parallelism.md)). Each
phase retires its own arena as it returns rather than handing a committed one
to the next: semantic analysis's queries fit in the prefix of the mapping
preprocessing parked, so carrying preprocessing's pages through parsing would
only keep them resident.

## Rules

- No phase result references its phase arena. A new pointer field on a sealed
  structure needs a seal rule in the same change; the size checks enforce it.
- Release with `arena_release_to_position`, never `arena_set_position`, when the
  caller asserts nothing survives: sanitized builds poison the released range.
- Allocation, decommit and destruction unpoison what they hand out or give
  back, so poison never outlives a committed range.
- A phase arena is retired by the thread that created it, at the end of the
  last phase that uses it: a rewound arena that is never retired keeps its
  committed pages.
- Resident sizes vary run to run; they appear in `-fsource-metrics` output,
  never in `-v`.

## Stale-reference defenses

| Defense | Where | Catches |
|---|---|---|
| Size checks on sealed structures | `c_source.c` | a new field crossing the seal without a rule |
| Independent ownership walk | `c_test_preprocess_references_range` | any pointer of the result graph into the released range |
| Release fill (`arena_test_fill_releases`), applied by `arena_release_to_position` and so by `arena_retire` | `c_test_phase_arena_release`, `compiler_driver_test_released_phase_fill`, `arena_tests` | a surviving reader in builds without AddressSanitizer: output, diagnostics and object bytes must not change |
| AddressSanitizer poisoning | `arena_release_to_position` | the first stale access, at its source |
| Poisoning positive control | `arena_tests` child mode `released_read` | a sanitized build whose poisoning silently stopped working |
| Lexer message lifetime | `c_test_lex_diagnostic_message_lifetime` | a diagnostic message left behind in a rewound temporary arena |
| Null handles | `compiler_driver_release_frontend` | a use of the token stream, the shape sidecar or a retired arena through the driver's result |

`CPreprocessDetail.boundary` counts, exactly, what the seal copied (bytes,
arrays and strings, pointer fields) and what the phase released.

## Ledger

`CompilerDriverResult.lifetime` samples each boundary of a unit (preprocess,
parse, semantic, lower, frontend release, prepare, codegen, object, emit) and
`ide cc -fsource-metrics=<path>` writes it as `lifetime.*` keys:

| Key | Meaning |
|---|---|
| `lifetime.<boundary>.unit_bytes` | unit-arena bytes above its entry position |
| `lifetime.<boundary>.preprocess_bytes` | spelling, token and shape arena bytes |
| `lifetime.<boundary>.resident_bytes` | process resident size |
| `lifetime.<boundary>.allocation_calls`, `allocation_bytes`, `commit_bytes` | the phase's arena requests and OS commit growth (`BUSTER_BENCH_ALLOCATIONS` only) |
| `lifetime.peak_live_bytes` | maximum of the two extents' sum over boundaries |
| `lifetime.live_byte_integral`, `lifetime.resident_integral` | those sums over reached boundaries |
| `lifetime.preprocess_*` | seal: released and copied bytes, copied objects, pointer fields |
| `lifetime.semantic_released_bytes`, `lifetime.semantic_releases` | layout-query tables semantic analysis released, and the releases that did it |
| `lifetime.frontend_released_bytes` | token and shape bytes retired after lowering |
| `lifetime.spelling_references` | canonical-IR names still borrowing the spelling space |
| `lifetime.cleanup_count`, `lifetime.units`, `lifetime.failed_units` | releases performed, units measured, units that stopped at a failed boundary |

Byte counts are logical arena extents, exact and including untouched
worst-case reservations; compare resident sizes for memory pressure. Take
timing only from an uninstrumented compiler.

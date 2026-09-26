# Incremental code generation (research prototype)

## Status and scope

This is a research prototype. It is **off by default**, and it must not be
enabled by default or merged without explicit authorization. It answers one
question: can Buster reuse compilation work at function granularity, safely and
deterministically, without a daemon, hidden global state, an external database
or a second permanent IR? The answer comes with evidence.

- Source pin: `main` at `ade6ac4b6ecb21f30b61b656439bac476c145e2f`, tree
  `4c5306221fdb22fccc929b55e333163742de17d0`.
- Code: `src/buster/lib/compiler/incremental/` holds the records, artifact,
  pack and session. A seam in `codegen.c` captures and replays artifacts, the
  driver owns the flags, and `ide.c` prints the records.
- Oracle harness: `tools/incremental_edit_matrix.py`. Hosted workflow:
  `.github/workflows/incremental-codegen-research.yml`. Unit tests:
  `src/buster/tests/compiler/incremental/`.
- Out of scope: #36/#880/#881 retirement, service, admission, deployment and
  generated bindings. None of those files is touched.

**Answer.** Yes, for native code generation. A function's machine-code
contribution to a `CodegenModule` can be reused under a complete, canonical,
content-addressed dependency record. The output stays byte-identical to a clean
compilation on every target, edit scenario, randomized edit sequence and
self-host stage tested. The frontend cannot yet be skipped on the same footing:
C lowering interns program-wide types and symbols as a side effect, and that is
where most compile time goes (see [Where the time goes](#where-the-time-goes)).
The narrower boundary is sound and complete today, and it is the base the
frontend work must build on (see [Migration path](#migration-path)).

Measured by work avoided, the prototype does what it claims: after an edit,
typically more than 99% of the self-host unit's IR rows skip code generation.
Measured by wall time, it is not yet a net win. On the unity unit, building
records and rewriting the pack cost about as much as the code generation they
save (see [Work avoided and cost](#work-avoided-and-cost)).

## Using it

| Flag | Effect |
| --- | --- |
| `-fincremental-cache=DIR` | Enable reuse for native x86-64/AArch64 C objects. There is one pack per input path in `DIR`. An empty `DIR` is an argument error. |
| `-fno-incremental-cache` | Disable it. The last of the two flags wins. The default is disabled. |
| `-fincremental-stats` | Print the `INCREMENTAL`, `INCREMENTAL_LOOKUP`, `INCREMENTAL_CAPTURE`, `INCREMENTAL_WORK`, `INCREMENTAL_RECORD`, `INCREMENTAL_VERIFY`, `INCREMENTAL_AUDIT` and `INCREMENTAL_TIME` records on stdout. |
| `-fincremental-trace` | Print one `INCREMENTAL_FUNCTION name= fingerprint= instructions= code_bytes= lookup= capture= verify_mismatch= audit_violation=` row per function. |
| `-fincremental-verify` | Compile every hit again, emit the fresh result and count artifact mismatches. |

The cache is inactive in these cases:

- the target is not x86-64 or AArch64;
- `-fno-register-allocator` is given (the canonical path);
- `-fverify-codegen` or `-fcodegen-fallback-census` is given;
- bootstrap tracing is on;
- LLVM bitcode is being emitted;
- a module-generation attempt is retried after capacity exhaustion, because
  module assembly in the failed attempt may already have created symbols.

Reuse never changes what a clean build does. Without the flag, no session is
opened, no compiler identity is read and no file is touched.

## Where the time goes

This is one supplementary local measurement: the Clang-built Release `ide`
compiling the self-host unity unit with a phase-timing patch that was thrown
away afterwards. Times are in milliseconds.

| Preprocess | Parse | Analysis + lowering | IR preparation | Code generation | Object | Emit |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 319 | 60 | 2568 (48%) | 439 (8%) | 893 (17%) | 360 | 694 |

The September 12 [throughput priorities](compiler-throughput-priorities-2026-09-12.md)
report the same shape. Code generation is the largest phase whose inputs can be
fully keyed today. Lowering is the largest phase overall, and it is the one
whose reuse needs frontend changes first.

## Candidate reuse boundaries

| Candidate | What reuse would skip | Dependency and determinism obstacles at `ade6ac4` | Verdict |
| --- | --- | --- | --- |
| Preprocessed source regions | Preprocessing (6%) | Tokens depend on the whole macro state at that point, on `__LINE__`/`__COUNTER__`, on `#include` resolution and on `#pragma` side effects. A region key amounts to the whole preceding state. | Rejected: small gain, keys as large as the prefix. |
| Parsed declarations | Parsing (1%) | The AST lives in an arena of pointers. Semantic analysis merges redeclarations, completes tentative definitions and interns types across the unit. | Rejected: little to skip, and nothing stable to reuse. |
| Canonical function IR (after lowering) | Analysis and lowering (48%) | Lowering one body can intern new program-wide types and symbols and advance their ids. Replaying a body would require replaying those side effects, i.e. a second persistent representation of the interning tables. | Deferred: the most valuable boundary. See [Migration path](#migration-path). |
| Validated machine-independent bodies (after preparation) | Preparation (8%) on top of lowering | Worth something only if lowering is also skipped. Serves as the **key** instead (see below). | Used as the key, not as the artifact. |
| Target machine functions (MIR) | Selection, allocation, scheduling | `MachineFunction` is a scratch-arena working representation, not a stable format. Persisting it would be a second permanent IR, and encoding it again saves little. | Rejected by constraint. |
| **Encoded function bodies with symbolic relocations** | **Selection, SSA/edge copies, allocation, scheduling, stack placement, encoding, unwind description, debug-location and line-mark recording** | Every input reaches the function through its prepared IR, program ids, types, symbols and a small module-wide context, and all of these can be keyed. | **Chosen.** |
| Object sections | Object writing (7%) | Sections, symbol indices, string tables and DWARF/CodeView units cover the whole unit. Splitting them per function would change clean-build output. | Rejected: it would change clean output. |

## The chosen boundary

The artifact is exactly what one machine-emitted function appends to the
`CodegenModule`, made independent of where it lands:

- the code bytes;
- code relocations, each with a function-relative offset, kind, addend and
  **symbol slot** (the rank of the target among the function's own referenced
  symbol ids);
- unwind actions, the prolog size and epilog offsets;
- block offsets, which global initializers' label-address relocations need;
- line marks as (function-relative code offset, canonical instruction) pairs.
  On replay they go through `codegen_record_line_hot` with positions taken
  from the **current** source map, so edits that only shift lines stay
  reusable;
- debug-location seeds, with offsets made function-relative and the function
  symbol rebased;
- the function's `CodegenStatistics` deltas, so `-v` output matches a clean
  build.

Frontend, preparation, validation, module assembly (globals, initializers,
top-level assembly), object writing and DWARF/CodeView emission always run on
the current program. Replay puts the bytes into the same buffer at the same
offset the machine path would use, so everything downstream sees an identical
`CodegenModule`.

Two kinds of function are never reused:

- **Inline assembly**: its template names symbols by spelling, and the
  assembler can reach program state that the record does not name.
- **Canonical-path functions** that fall back from the machine path.

A capture is also refused when the function produces something outside the
artifact's vocabulary: a non-code or label-address relocation, a symbol outside
the record, or a fallback or verification counter moving (`capture=unsupported`
or `foreign-symbol` in the trace).

## Dependency model

### Reuse condition

A stored artifact is replayed only when both of these hold:

1. The **context record** of the current compilation is byte-identical to the
   one stored in the pack.
2. The function's **canonical dependency record** is byte-identical to the
   record stored beside the artifact.

The 64-bit xxh64 fingerprint (`buster_hash_64`) only orders and indexes the
entry table. A fingerprint collision cannot cause reuse, because
`incremental_pack_find_entry` compares the full record bytes. The unit tests
cover this with an impostor entry that has the same fingerprint and a
different record.

### Context record

The context record holds every module-wide fact that code generation reads for
a function without going through that function's own ids:

- format version and key schema;
- compiler identity: the size and xxh64 of the running executable image, read
  through `os_executable_path`;
- CPU architecture, model and explicit-feature flag, every CPU feature word,
  OS and OS version;
- the full `TargetDataLayout`: 21 scalar layouts, atomic widths and alignment,
  ABI stack and maximum alignment, endianness, plain-char signedness, and
  128-bit integer support;
- the `CodegenAbi`, the object format and, for each ABI convention, the System
  V unnamed-bit-field policy;
- `debug_info`, effective position independence (x86-64 ELF only; see
  `CodegenModuleOptions`), register allocator, assembly syntax, the local
  promotion switches and `fast_passes`.

`assume_validated` is always true on this path, and `verify_invariants` and
`record_fallbacks` disable the cache. A change to any context fact misses the
whole pack (`miss-context`).

### Function record

The function record has three sections. Each is fingerprinted separately so
that a miss can report which one differs.

1. **Body.** Every field of the prepared `IrFunction` except source provenance
   and names:
   - state, entry, counts, opcode summary and operand totals;
   - every instruction: opcode, operation and memory-order bytes, flags,
     operands, immediates, types, symbols, successors and extras;
   - values, blocks with parameters, incomings and local maps;
   - the published CFG with its instruction remap;
   - local places and `local_uses_memory`;
   - debug locals (id, type, scope depth, whether a parameter);
   - label metadata, inline-assembly operand names and clobbers.
2. **Types.** The complete type closure. It starts from every type the
   function names and every type its referenced symbols name, and follows
   element, return, unqualified, parameter and field types. Each type records
   its kind, calling convention, float format, flags, the full resolved layout
   (size, alignment, ABI class, natural alignment), element count, bit width,
   parameters, fields (type, offset, bit offset, bit width, whether a
   bit-field, access size) and enum values.
3. **Symbols.** For every referenced symbol: its kind, linkage, and whether it
   is a definition, thread-local, weak or hidden, plus its section name and
   type.

**Relabeling.** Program-level type and symbol ids are replaced by their rank
among the function's own referenced ids. Ranking keeps every equality and
ordering relation codegen can observe between ids. An unrelated edit that
shifts absolute id numbers therefore changes nothing. A reference outside the
collected set fails the record (`ineligible-record`) instead of widening the
equivalence silently.

**Excluded, and why that is sound.** Type, field, symbol and local names, and
all source ranges, are left out. Native code generation does not read them:
relocations bind to symbol ids, and the object writer and the debug emitters
that do read names always run on the current program. The oracle backs this
up. With debug information on, the `local_rename` and `line_shift` scenarios
reuse every function and still produce objects byte-identical to clean builds.

### Every listed fact, mapped

| Fact | Where it enters the key |
| --- | --- |
| Source content | Every semantic effect of the source on the function reaches its prepared IR (body section). Positions are excluded and resolved from the current source map on replay. |
| Declaration and type dependencies | Type closure section, with full layouts; referenced symbols' types. |
| Macro or language-option state | Macros act only through the lowered IR (body). Language options that change lowering change the IR. The options code generation reads directly (promotion switches, `fast_passes`) are in the context. |
| Target triple and data layout | Context: architecture, model, features, OS and version, object format, full data layout. |
| ABI | Context: `CodegenAbi` and the per-convention bit-field policy. Types: calling convention and ABI class of every type in the closure. |
| Function attributes | Body (every `IrFunction` field), the function's own symbol (linkage, weak, hidden, section, TLS) and its canonical type (noreturn, variadic, unprototyped, convention). |
| Pragmas | Their effects: `pack` changes field offsets and layouts (types section); other pragmas change IR or attributes. The `pragma_pack` scenario exercises this. |
| Compiler version | Context: the size and xxh64 of the running compiler image. Any rebuild misses. |
| Optimization mode | Context: register allocator (every C `-O` level selects it), `fast_passes`, promotion switches. |
| Debug policy | Context: `debug_info`. |
| Relevant global declarations | Symbols section: kind, linkage, definition, TLS, weak, hidden, section and type of every referenced global and function. Initializer contents are not read by per-function code generation, and module assembly regenerates them on every build (`global_initializer` scenario). |
| Inline or constant dependencies | Code generation does not inline. Constants folded by the frontend or by FAST preparation are already in the body. |
| Canonicalization version | `INCREMENTAL_KEY_SCHEMA` and `INCREMENTAL_FORMAT_VERSION` in every record and pack. Any canonicalization change also changes the compiler identity. |
| Backend configuration | Context: register allocator, assembly syntax, PIC, CPU model and features. Verification, fallback census and bootstrap tracing disable the cache. |

### Measured, not guessed: the access audit

The dependency rules were checked by instrumenting compilation, not assumed.
Building with `-DBUSTER_INCREMENTAL_AUDIT=1` compiles a thread-local
`IrAccessAudit` into these accessors:

- `ir_type_from_id` and `ir_symbol_from_id`;
- the two `type_classes` projections in `machine_select.c`;
- the x86-64 type-class and signature-plan lookups.

These are the ways code generation reaches program-wide types and symbols.
While each machine-path function is generated, the audit records every id
these accessors hand out. `incremental_session_audit_close` then checks that
set against the function's key closure. Any id read outside the closure is a
violation, reported per function (`audit_violation=1`) and in total
(`INCREMENTAL_AUDIT`). In the default build the hooks compile to nothing.

| Workload | Functions | Types read / keyed | Symbols read / keyed | Violations | Overflows |
| --- | ---: | ---: | ---: | ---: | ---: |
| Self-host unity unit (host) | 5285 | 99,923 / 411,394 | 27,237 / 32,509 | 0 | 0 |
| `tests/*.c` × {host, aarch64-unknown-linux, x86_64-pc-windows, aarch64-apple-macos} × {fast, quality, mir-stack}: 4143 compiles (561 fixture/target combinations the compiler refuses are skipped) | 28,633 | 201,523 / 229,855 | 46,568 / 75,189 | 0 | 0 |

On the self-host unit, the keyed type closure is about four times the set
actually read. That is the price of keying whole types, and it is a main source
of over-invalidation below. Fields of `IrSymbol` and `CodegenModuleOptions` are
covered by inspection: the record contains every `IrSymbol` field except
`name`, `link_name`, `source` and `id`, and the context contains every option
field that does not disable the cache. Module globals (`IrGlobal`) are read
only by module assembly, which always runs.

### Invalidation proof

Take a function *f* in compilation *C′* whose record *R′* equals the stored
record *R* from compilation *C*, with equal contexts *X′ = X*. The claim is
that the artifact produced for *f* in *C′* equals the stored artifact *A*.

1. Machine code generation of *f* is a deterministic function of: its prepared
   `IrFunction`; the types and symbols it reaches through program ids; the
   module-wide facts in the context record; and the offset at which it is
   placed.
   - The audit supports this empirically for types and symbols.
   - Inspection supports it for options and module state: the seam receives
     nothing else, and module globals are not read.
   - Determinism is an existing Buster invariant, exercised by the self-host
     fixed point.
2. The record is injective over those inputs, up to the renaming of program
   ids by rank. Every field is written in a fixed order, and varints are the
   canonical minimal LEB128. A null array is written as a 0 header and a
   present array as its count plus one. Value ids are delta-coded within a
   fixed stream order. So equal bytes imply equal field values, and equal
   relabeled references imply the same equality and order structure among the
   referenced ids.
3. Code generation observes ids only through equality, order and the fields
   of the objects they name, and never through their absolute values. A
   renaming that preserves both relations therefore yields the same code
   bytes. The relocations differ only in which symbol id they name, and slots
   map those back through the current record's own ranks. This premise is
   tested directly: every edit that adds types, symbols or functions early in
   the unit shifts the ids of all later ones, and those functions are reused
   with byte-identical objects.
4. Placement enters only through function-relative quantities: code, relocation
   and debug offsets and block offsets. Replay rebases these onto the current
   code offset. References to other functions and to data leave code
   generation as module relocations, which the object writer resolves, so the
   code bytes do not depend on the function's base. The capture refuses any
   relocation it cannot express in function-relative form. The
   `unrelated_addition` scenario and the random add and remove steps shift
   every later function's offset, and the replayed objects still match.
5. Therefore *A* is the artifact *C′* would have produced for *f*. Replaying it
   leaves the `CodegenModule` identical to what the machine path would have
   built. Line rows and debug seeds are derived from the current source map
   and function, exactly as the machine path derives them.

**Evidence that tests the premises.** The proof rests on the premises in
steps 1, 3 and 4. They are tested, not assumed:

- the oracle comparison below;
- `-fincremental-verify`, which regenerates every hit and compares artifacts
  byte for byte, with 0 mismatches in every run;
- the audit.

## Serialization formats

All integers are little-endian. Records and artifacts use unsigned LEB128 for
integers and zigzag LEB128 for signed values. In both, ids are written as
id + 1, with 0 for `IR_ID_UNDERLYING_INVALID`. Arrays have a 0 header when null
and count + 1 otherwise.

- **Context record:** magic `BICX`, format version, key schema, then the facts
  above in fixed order.
- **Function record:** magic `BIIR`, key schema, then the body, types and
  symbols sections. Instructions use a presence mask for the seven narrowed
  operation and memory-order bytes. Value ids are delta-coded. Types and
  symbols appear in rank order.
- **Artifact:** magic `BICA`, then:
  - the code length and bytes;
  - the prolog size and symbol-slot count;
  - unwind actions (offset, value, kind, register);
  - epilog offsets, then block offsets;
  - relocations (offset, slot, signed addend, kind);
  - line marks, delta-coded;
  - debug seeds (local, start, length, location pieces);
  - the statistics vector with its length.
- **Pack (`<DIR>/<xxh64(input path) as 16 hex digits>.bpk`):**

| Bytes | Field |
| --- | --- |
| `[0,8)` | Magic `BUSTINC1` |
| `[8,12)` / `[12,16)` | Format version / key schema |
| `[16,24)` | File size |
| `[24,32)` | xxh64 of `[64, size)` |
| `[32,36)` / `[36,40)` / `[40,44)` | Context length / entry count / manifest count |
| `[44,48)` | Zero |
| `[48,56)` | xxh64 of `[0,48)` |
| `[56,64)` | Zero |

The body holds, in order:

1. the context record;
2. the entry table: 32-byte rows of fingerprint, record offset, record length,
   artifact length and artifact offset, sorted by (fingerprint, record bytes);
3. the manifest: 48-byte rows of name offset, name length, entry index, record
   fingerprint and three section fingerprints, sorted by name;
4. the name, record and artifact bytes.

The manifest only explains misses. It maps a function name to its previous
section fingerprints so that a miss can be classified as body, types or
symbols. It never grants reuse.

Encoding is canonical. Entries and manifest rows are ordered by an iterative
heapsort on content keys, and there are no timestamps, paths or pointers. So
two compilations of the same input under the same compiler produce
byte-identical packs. A warm rebuild with no edits encodes the pack, finds it
identical to the file and skips the write.

## Corruption, interruption and concurrency

Readers treat every byte as hostile. `incremental_pack_decode` rejects the
whole pack as `CORRUPT` in these cases:

- the file is short;
- the magic is wrong;
- the size field does not match the file;
- the header or body fingerprint does not match;
- a reserved word is nonzero;
- a table or byte range falls out of bounds;
- the entry or manifest order is not strictly sorted.

A different format version or schema reads as `CONTEXT_CHANGED` and is never
reinterpreted. A rejected pack yields no reuse (`miss-no-pack` or
`miss-context`). The next successful compilation overwrites it.

Each artifact that is hit is decoded again, with limits checked against the
current function:

- the block count must match;
- line-mark instructions must be in range;
- relocation slots must be below the slot count;
- debug locals must be below the function's local bound;
- counts must be backed by the remaining bytes;
- code must be at most 1 GiB, with at most 64 debug pieces.

An artifact that fails is `miss-malformed` and is recompiled.

A pack is published only after the object has been written successfully. The
write goes to a staging file in the same directory, which is then renamed over
the pack (`os_file_staging_create`, `os_file_replace`), without an fsync. A
concurrent reader sees either the old pack or the new one. A pack torn by a
crash fails its size or fingerprint check. A failed publication does not fail
the compilation. A failed compilation publishes nothing and leaves the previous
pack in place (`diagnostics_error` scenario). Two concurrent compilations of the
same input both publish complete packs, and the last rename wins.

The unit tests (`incremental_tests`, 167 assertions) cover:

- truncating the pack at every length;
- flipping every byte;
- reordered entries and out-of-range offsets, with the fingerprints recomputed;
- version changes;
- a fingerprint impostor;
- truncated and bit-flipped artifacts.

The driver tests on four targets cover a half-truncated file, garbage content
and a single flipped byte in the published pack.

## Diagnostics

Diagnostics come from the frontend and from module-level checks, which always
run, so they are identical by construction. The harness compares exit status,
stderr and every ordinary stdout record (including `-v` statistics, which
replay restores from the stored deltas) against the clean run on every step.
Three diagnostics scenarios run on every target:

- `diagnostics_warning`: a `#warning` is reported identically;
- `diagnostics_error`: a syntax error fails the same way and publishes nothing;
- `diagnostics_recovery`: the pack from before the error is reused after the
  fix.

## Evidence

The oracle for each step is a full clean compilation with the same compiler.
Each step compiles the edited unit three times:

- clean;
- incremental, through the cache the previous step published;
- verify, through a copy of that cache with `-fincremental-verify`.

A step passes only if all of the following hold:

- the three objects are byte-identical, which implies equal hashes and equal
  object structure;
- exit status, stderr and ordinary stdout agree;
- verification finds no mismatches.

For every invalidated function, the harness compares the artifacts from before
and after the step:

- **necessary**: the machine code changed, or there was no artifact;
- **relabel**: the code is identical but slots or metadata moved;
- **over**: the stored artifact was exactly right.

### Edit matrix (host, x86-64 Linux)

19 functions in a freestanding corpus. "Reused IR rows" counts the prepared IR
instructions whose code generation was skipped. "Bytes reparsed" is the whole
lexed unit, because the frontend always runs.

| Scenario (edit) | Invalidated (reason) | Necessary / relabel / over | Reused IR rows | Reused code bytes | Bytes reparsed |
| --- | --- | ---: | ---: | ---: | ---: |
| `diagnostics_warning`: #warning added | all 19 (miss-no-pack) | 19 / 0 / 0 | 0/236 | 0 | 2248 |
| `diagnostics_error`: syntax error: both compiles fail identically | none | 0 / 0 / 0 | 0/0 | 0 | 2247 |
| `diagnostics_recovery`: error fixed: the pre-error pack is reused | none | 0 / 0 / 0 | 236/236 | 2190 | 2248 |
| `function_body`: constant changed inside arith | `arith` (miss-body) | 1 / 0 / 0 | 227/236 | 2099 | 2217 |
| `local_declaration`: new local variable used in loop_sum | `loop_sum` (miss-body) | 1 / 0 / 0 | 216/238 | 1932 | 2242 |
| `local_rename`: local renamed in loop_sum (debug names only) | none | 0 / 0 / 0 | 236/236 | 2190 | 2247 |
| `line_shift`: comment lines inserted before every function | none | 0 / 0 / 0 | 236/236 | 2190 | 2249 |
| `public_type`: field appended to struct vec | `main` (miss-body), `vec_dot` (miss-types), `vec_len2` (miss-types) | 2 / 0 / 1 | 168/238 | 1596 | 2224 |
| `macro`: SCALE redefined | `arith` (miss-body) | 1 / 0 / 0 | 227/236 | 2099 | 2217 |
| `abi_signature`: string_len returns long instead of int | `greet_len` (miss-body), `string_len` (miss-body) | 2 / 0 / 0 | 217/238 | 1958 | 2218 |
| `target`: CPU model changed | all 19 (miss-context) | 0 / 0 / 19 | 0/236 | 0 | 2217 |
| `compiler`: compiler image changed | all 19 (miss-context) | 0 / 0 / 19 | 0/236 | 0 | 2217 |
| `debug`: debug information disabled | all 19 (miss-context) | 0 / 19 / 0 | 0/236 | 0 | 2217 |
| `declaration_order`: vec_dot moved after mix, globals reordered | `main` (miss-body) | 0 / 1 / 0 | 203/236 | 1849 | 2217 |
| `unrelated_addition`: new function with a string literal and a new type added first | `extra_sum` (miss-new) | 1 / 0 / 0 | 236/253 | 2190 | 2372 |
| `global_initializer`: table and names initializers changed | none | 0 / 0 / 0 | 236/236 | 2190 | 2217 |
| `linkage`: leaf_add loses static | `arith` (miss-symbols), `leaf_add` (miss-symbols), `uses_apply` (miss-symbols) | 0 / 0 / 3 | 216/236 | 1993 | 2210 |
| `definition`: shared_total becomes an external declaration | `use_external` (miss-symbols) | 0 / 0 / 1 | 229/236 | 2128 | 2224 |
| `pragma_pack`: struct pair packed | `pair_value` (miss-types) | 1 / 0 / 0 | 226/236 | 2141 | 2257 |
| `inline_assembly`: a function with inline assembly added | `fence_now` (ineligible-inline-assembly) | 0 / 0 / 0 | 236/239 | 2190 | 2295 |
| `label_table_neighbor`: classify edited beside the label-address table | `classify` (miss-body) | 1 / 0 / 0 | 223/236 | 2075 | 2217 |
| `abi_option`: System V unnamed bit-field policy changed | all 19 (miss-context) | 0 / 0 / 19 | 0/236 | 0 | 2217 |

`diagnostics_warning` is the first compile of its sequence, with an empty
cache. Every other scenario starts from the pack its unedited base published.

The same scenarios run on `aarch64-unknown-linux`, `aarch64-apple-macos`,
`aarch64-pc-windows`, `x86_64-pc-windows` and `x86_64-apple-macos`, with the
same invalidation sets. The one exception is the `target` scenario
(`-march=baseline`). An explicit triple already defaults to the baseline CPU
model, so on the cross targets no context fact changes and everything is reused,
with objects identical to the clean builds. There is one more difference, in
`abi_signature` on the LLP64 Windows targets: `long` is as wide as `int`
there, so the caller `greet_len` keeps identical code and counts as a relabel.
Over the 6 targets, 254 matrix steps pass the oracle.

Reading the matrix:

- **Function-body, local-declaration and macro edits** invalidate only the
  edited function.
- **Line shifts and local renames** invalidate nothing. Line tables and debug
  names come from the current program, and the objects still match.
- **A public type change** (a field appended to `struct vec`) invalidates the
  functions whose type closure contains the struct:
  - `main` builds the struct and `vec_dot` takes it by value, so both get new
    code;
  - `vec_len2` only reads `x` and `y` through a pointer, so its code is
    unchanged. It is the over-invalidation cost of keying whole types.
- **An ABI change** (`string_len` now returns `long`) invalidates the callee
  and its caller.
- **Context changes** (CPU model, compiler image, debug mode, ABI option) miss
  every function:
  - The CPU-model and compiler changes happen to leave this corpus's code
    unchanged. They are keyed wholesale because nothing narrower can be proven
    about a different compiler or feature set.
  - `-g0` leaves every function's code unchanged but drops its line marks and
    debug seeds, which makes it a relabel. Debug mode must stay in the key.
- **Declaration reordering** changes the rank order of the symbols `main`
  references. Its code is identical and only its relocation slots move (a
  relabel).
- **Adding an unrelated function** (with a new type and string literal)
  invalidates only the new function. The shifted type and symbol ids of every
  other function are absorbed by relabeling.
- **Global initializer changes** invalidate nothing. Module assembly re-emits
  data on every build.
- **Linkage and definition changes** (dropping `static`, turning a definition
  into `extern`) invalidate every function that references the symbol,
  because its attributes are keyed. The code does not change here: without
  position-independent code a call or address does not depend on linkage. This
  is over-invalidation.
- **`#pragma pack`** invalidates the struct's only user, which gets new code.
- **Inline assembly** is never reused.
- **Editing beside a label-address table** invalidates only the edited
  function. Global label-address relocations are rebuilt from the replayed
  block offsets.

### Randomized edit sequences

`--random SEEDS:STEPS` drives a generated program through random edits. There
are ten mutation kinds: constants, adding and removing functions, swapping
definitions, struct fields, globals, renames, comments, macros and linkage.
Each step is checked against the oracle.

Each of the 6 targets runs the same 6 seeds × 30 edits, which is 1116 steps,
and every step passes the oracle. The counts per target are identical:

| Seed | Reused / lowered IR rows | Necessary | Relabel | Over |
| ---: | ---: | ---: | ---: | ---: |
| 0 | 4768 / 4905 | 9 | 0 | 4 |
| 1 | 2534 / 2700 | 9 | 0 | 11 |
| 2 | 3238 / 3359 | 8 | 0 | 2 |
| 3 | 3995 / 4225 | 5 | 0 | 19 |
| 4 | 3466 / 3733 | 12 | 0 | 13 |
| 5 | 3568 / 3700 | 6 | 0 | 2 |
| **all** | **21569 / 22622 (95.3%)** | **49** | **0** | **51** |

Linkage toggles cause 50 of the 51 over-invalidations: 21 toggles
invalidate 53 functions, and only 3 of those get new code. One constant edit
that left the code unchanged causes the last one. Otherwise, every mutation
invalidates exactly the functions whose code changed:

- macro redefinitions, field additions, function additions and the other
  constant edits;
- swaps, removals, comment insertions, renames and global-initializer changes,
  which invalidate nothing.

### Real code: the self-host unity unit

Edits are applied to a private copy of `src/`, and `ide.c` is compiled as a
single unit.

| Edit | Invalidated (reason) | Necessary / relabel / over | Reused / lowered IR rows | Reused code bytes | Bytes reparsed |
| --- | --- | ---: | ---: | ---: | ---: |
| base (empty cache) | 5285 (miss-no-pack); 2 inline-assembly functions ineligible | 5285 / 0 / 0 | 0 / 1,349,374 | 0 | 32,586,930 |
| `leaf_body`: a statement added to `string_equal` | 16 (miss-body) | 16 / 0 / 0 | 1,344,887 / 1,349,378 | 21,591,166 | 32,586,999 |
| `new_function`: a function appended to `arena.c` | 1 (miss-new) | 1 / 0 / 0 | 1,349,337 / 1,349,384 | 21,649,886 | 32,587,115 |
| `line_shift`: two comment lines at the top of `base.h` | none | 0 / 0 / 0 | 1,349,343 / 1,349,384 | 21,649,948 | 32,587,163 |
| `header_struct`: a field appended to `ByteWriter` | 33 (31 miss-types, 2 miss-body) | 4 / 0 / 29 | 1,342,530 / 1,349,385 | 21,580,026 | 32,587,190 |

The 41 IR rows that are never reused belong to the two inline-assembly
functions, `cpuid` and `xgetbv`.

- **`leaf_body`** adds a statement to `string_equal`. It invalidates
  `string_equal` and the 15 functions defined after it in `string.c`. All 16
  are *necessary*: the assertion macros in `os.h` pass `__LINE__` into the
  code, so every asserting function below the edit gets a new immediate.
- **`line_shift`** in `base.h` shifts no `__LINE__` that reaches code, so all
  5286 eligible functions are reused.
- **`header_struct`** appends a field to `ByteWriter`. It invalidates the
  33 functions whose type closure contains it. Only 4 get new code
  (`byte_writer_make`, `codeview_build_legacy`, `pdb_build`, `pdb_msf_build`),
  because appending a field moves no existing offset. This is the largest
  measured over-invalidation class.

### Self-hosting through the cache

Each stage compiler builds the next one three ways: clean, cold (empty cache)
and warm (cache from the cold run). All three objects must be byte-identical,
and stage 2 must equal stage 3.

| Stage | Compiler | clean = cold = warm | Warm reused | Supplementary wall ms (clean / cold / warm) |
| ---: | --- | --- | ---: | --- |
| 2 | Clang-built Release `ide` builds stage 2 | yes | 5285 of 5285 eligible | 4717 / 5751 / 4610 |
| 3 | Self-built stage 2 builds stage 3 | yes | 5285 of 5285 eligible | 25631 / 27944 / 23557 |

Stage 2 equals stage 3 byte for byte, so the fixed point holds through the
cache. `test_self_host --config Release` also passes on the final tree with
the feature off, which is its default.

### Dependency audit

The dependency audit table in [Measured, not guessed](#measured-not-guessed-the-access-audit)
covers 33,918 audited function compilations: the self-host unit, plus every
`tests/*.c` fixture on four targets under three allocators. None read a type or
symbol outside its key. The hosted workflow repeats the self-host unit and the
fixture sweep on each runner.

## Work avoided and cost

On a hit, the following are skipped for that function:

- machine instruction selection;
- SSA and edge-copy construction;
- register allocation;
- scheduling;
- stack placement;
- encoding;
- unwind and epilog description;
- dense debug-location recording;
- line-mark collection.

Replay copies the code, rebinds relocations through the slots and replays line
rows from the current source map.

Nothing before code generation is skipped. Every step re-preprocesses and
reparses the whole unit, which is the "bytes reparsed" column, and re-lowers,
re-prepares and re-validates it.

The mechanism has these costs:

- identity: hashing the compiler image once per invocation;
- building every function's record;
- reading and validating the pack;
- writing it, or skipping the write when it is unchanged;
- storage.

On the self-host unit:

| Quantity (self-host unity unit, Clang-built compiler) | Value |
| --- | ---: |
| Lowered functions / eligible / not lowered (declarations) | 5287 / 5285 / 840 |
| Canonical dependency records, total | 50.06 MB (body 33.87 MB, types 15.97 MB, symbols 0.18 MB) |
| Keyed types / symbols (sum over functions) | 411,394 / 32,509 |
| Pack size / entries | 77.33 MB / 5052 |
| Work replayed on a warm, unchanged rebuild | 5285 functions, 1,349,343 IR rows, 21.65 MB code, 51,446 relocations, 190,922 debug seeds |
| Compiler identity (hash a 45 MB image) | 16–17 ms |
| Records for every function | 327–371 ms |
| Pack read and validation | 27 ms |
| Replay | 51–53 ms |
| Pack write: unchanged, compare only / rewritten after an edit / first write | 85 ms / 246–390 ms / 495 ms |

The records and a pack rewrite together cost about as much as the code
generation they replace, which took 893 ms in the phase split above. That is
why the timing below is close to neutral after an edit. Cheaper records and
incremental pack updates are migration item 2. They are costs of this
implementation, not of the boundary.

Hosted and local wall times are **supplementary** and are not a performance
claim. No `benchpress` or dedicated-host measurement was made, and no
performance audit is recorded.

Real-code steps, measured once each on a 4-core cloud container. The
Clang-built compiler compiled the self-host unit with `-c`. The randomized
matrix was running concurrently for part of this run.

| Step | Clean ms | Incremental ms |
| --- | ---: | ---: |
| base (cold: no pack) | 4842 | 6031 |
| `leaf_body` | 4916 | 4779 |
| `new_function` | 4812 | 4935 |
| `line_shift` (warm, nothing to rewrite) | 4927 | 4216 |
| `header_struct` | 4584 | 4532 |

In short: an unchanged warm rebuild saves about 13%. An edited rebuild is
within noise of a clean build, because the pack rewrite and record building
absorb the code-generation savings. A cold build costs about 20% extra. The
hosted workflow reports the same wall times on each runner, also as
supplementary data. The result of this work is the counts, not the times.

## Limitations

- **The frontend always runs.** Most compile time is analysis and lowering,
  so end-to-end savings are bounded by the code-generation share (about 17% on
  the unity unit). The boundary proves the mechanism and the key; it does not
  deliver the work-proportional regime on its own.
- **Object writing always runs.** The object, DWARF and CodeView are rebuilt
  from the `CodegenModule` every time.
- **Over-invalidation**, measured above, comes from:
  - whole-type keying, where an appended field invalidates every function
    whose closure contains the struct;
  - whole-symbol keying, where linkage or definition flips invalidate every
    referencing function;
  - compiler identity, where any compiler rebuild invalidates everything.
- **Relabels**: a declaration reorder can change the rank order of a
  function's symbols. The function is then recompiled to the same code with
  new slots.
- **Necessary invalidation from `__LINE__`**: code that embeds line numbers
  (Buster's own assertion macros) changes when lines above it move. The key
  handles this correctly; it only limits reuse in such code.
- **Pack size.** The self-host unit's records total about 50 MB, and its pack
  is about 77 MB. Type closures are the second-largest part, after bodies.
- **Ineligible functions**: those with inline assembly, those on the canonical
  path and those whose capture is unsupported.
- **One pack per input path**, keyed by the path's spelling. Two spellings of
  one file get separate packs. Nothing prunes packs or leftover staging files.
- **Mechanism cost.** Records are built for every lowered function, even when
  there is no pack: about 0.35 s on the unity unit. Any change rewrites the
  whole pack: 0.25–0.5 s for 77 MB. As measured, an edited rebuild of the
  unity unit is therefore about as fast as a clean one, and a cold build is
  about 20% slower.
- **Targets**: x86-64 and AArch64 native code generation only.

## Migration path

Each step keeps the default off and uses the harness in this document as its
acceptance gate.

1. **Precision.** Replace whole-type keys with the projections code generation
   actually reads, which the audit can enumerate: layout, ABI class and the
   offsets of accessed fields. Replace whole-symbol keys with the attribute
   bits each relocation kind depends on. The audit's touched-versus-keyed
   ratio measures progress.
2. **Mechanism cost.** Build records in parallel per function; they are
   independent given frozen program tables. Update the pack incrementally, so
   that an edit writes the changed entries and an index instead of rewriting
   77 MB. The atomic replacement stays on the index. The acceptance signal:
   an edited warm rebuild of the self-host unit costs less than a clean one,
   measured by the trusted method in `docs/agents/benchmarking.md`.
3. **Frontend reuse, the main gain.** Make lowering free of side effects at
   function granularity. Pre-intern every type and symbol a body can mention
   during declaration analysis, so that lowering a body only reads the program
   tables. Then the lowered-and-prepared body is itself the artifact. It is
   keyed by the token range of the body and by the declaration-level record
   this prototype already defines, and analysis and lowering (48%) join the
   skipped phases. That needs no second permanent IR, because the stored form
   is the canonical IR record defined here.
4. **Object sections.** When per-function sections become a supported output
   mode on their own merits, object writing can splice stored sections.
5. **Build integration.** Let `build.c` and `ide cc` pass a per-configuration
   cache directory (still opt-in). Add pack pruning keyed on compiler
   identity.
6. **Enablement.** Only with explicit authorization: run the hosted research
   workflow on every change to code generation, then make the cache the
   default for local development builds only. Release and CI builds stay
   clean.

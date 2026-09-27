# Where Buster's next order of magnitude is — research synthesis, 2026-09-27

## Status and scope

This is a ranked research synthesis, not an adopted plan, an acceptance
measurement or a repository-policy change. It combines three evidence sources:

1. **Diagnostic measurements** on unmodified `main`
   `ade6ac4b6ecb21f30b61b656439bac476c145e2f`, recorded with their exact
   commands, inputs and limits in audit
   [`2026-09-27T155317Z`](performance-audits/2026-09-27T155317Z.md). They come
   from a shared 4-vCPU cloud VM with a Clang-bootstrapped build driver. They
   rank opportunities; they do not accept any.
2. **The open portfolio** on 2026-09-27: 361 open issues and 110 open pull
   requests, read to name existing owners and the gaps no issue covers.
3. **External literature** on fast compilers, caches, linkers and hot
   reload. The container's proxy blocked most non-GitHub hosts, so several
   external numbers were taken from search summaries of the cited pages
   rather than from the primary text. Those are marked *(summary)*; spot-check
   them before quoting them elsewhere.

Every multiplier below that is not a measurement is labelled **model** and
states its assumption. Nothing here approves a budget, changes the priorities
in [`AGENTS.md`](../AGENTS.md), or supersedes the owners it names. No compiler
source was changed to produce it.

## The short answer

The canonical benchmark — the stage-1 unity self-compile, one 3.6 M-token
translation unit — is the one workload shape on which Buster's largest costs
are invisible. Real C projects are many small translation units that share
large headers and are rebuilt after small edits. Measured on that shape:

- **Most of a per-file build is not the file's own code.** Lua 5.4.8 built
  one `ide cc -c` process per file spends 57% of its time in the fixed
  per-process floor, 21% re-processing the same headers, 7% on the file's own
  frontend work and 15% in the backend. Compiling the same 33 files in one
  process is 2.7x faster serially.
- **Header-heavy files erase Buster's advantage.** A five-line `Python.h`
  extension spends ~97% of its time on headers and x86 table preparation and
  takes as long as Clang -O0 (87-94 ms against 88-94 ms).
- **A one-pass compiler is ~5x faster on the same inputs.** TinyCC 0.9.27
  compiles the SQLite amalgamation in 0.17-0.19 s with 18 MB (Buster
  0.89-1.07 s, 257 MB) and the Lua files one process each in 0.30 s (Buster
  1.39-1.55 s). It validates far less, but its generated SQLite runs the
  test workload 1.6x faster than Buster's.
- **The backend emits ~2.6x the machine instructions of Clang -O0**, and
  that code runs ~1.8x slower than Clang -O0. The allocator, encoder, line
  tables, relocations and object writer all process that volume.
- **The in-process parallel path does not scale yet.** `-fcompile-jobs=4` is
  no faster than four ordinary processes, with twice the memory.

The single-TU profile is already flat after ~380 audits: no phase exceeds
~31%, and the self-built compiler is 4x slower than the Clang-built one. The
next order of magnitude is therefore not another 5% inside a phase. It comes
from **not doing work**: not repeating per-process setup, not re-reading
headers, not walking each function body several times, not emitting 2.6x the
instructions, and not recompiling or relinking what did not change.

## Ranked leaps

| # | Leap | Evidence | Plausible size | Existing owner | Gap |
|---|---|---|---|---|---|
| 1 | **Project-scale compilation**: one warm process compiles many TUs, schedules them dynamically across lanes and shares header state | Measured 2.7x from batching alone; floor 57% and headers 21% of per-file Lua; header-heavy TU ~97% overhead | **Model:** ~3-4x over `make -j4` on Lua; ~10x on header-heavy TUs | Floor: [#1295](https://github.com/buster14a/buster/issues/1295), [#1125](https://github.com/buster14a/buster/issues/1125). TU lanes: [#53](https://github.com/buster14a/buster/issues/53), [#424](https://github.com/buster14a/buster/issues/424), [#1265](https://github.com/buster14a/buster/issues/1265) | No issue owns a resident compiler, a batch/`compile_commands.json` mode, or header-state reuse |
| 2 | **Incremental and live**: result cache, then function cache, in-place relink and hot reload | Lua one-file edit: ~45 ms compile + 36-49 ms link today | **Model:** edit-to-executable from ~90 ms to ~10 ms; edit-to-running-code without restart | Function cache: [#1470](https://github.com/buster14a/buster/issues/1470) (blocked) | No owner for a TU result cache, incremental linking, hot reload or `ide run` |
| 3 | **Emit less machine code**: register-resident values, operand folding at selection, per-function FAST decline | 2.6x Clang -O0's instructions; 40% frame-slot traffic ([#1473](https://github.com/buster14a/buster/issues/1473)); one prototype shape disables FAST for all of SQLite | **Model:** code near Clang -O0 speed (TinyCC's single pass is within ~15-20% of it here) and backend stages that shrink with row count | [#49](https://github.com/buster14a/buster/issues/49), [#56](https://github.com/buster14a/buster/issues/56), [#1385](https://github.com/buster14a/buster/issues/1385), [#1386](https://github.com/buster14a/buster/issues/1386), #1473 leads | The whole-module FAST decline is untracked |
| 4 | **Fewer walks, less memory, per function**: one semantic walk per body, streaming function-at-a-time back half | Bodies are walked by binding, validation and lowering, the last two re-deriving types; peak RSS 388 MB after analysis, 1.1 GB after codegen | **Model:** ~2x single-TU if validation folds into lowering and the backend halves | [#1028](https://github.com/buster14a/buster/issues/1028), [#1551](https://github.com/buster14a/buster/issues/1551), [#1544](https://github.com/buster14a/buster/issues/1544), [#54](https://github.com/buster14a/buster/issues/54), [#546](https://github.com/buster14a/buster/issues/546) | No owner for fusing validation into lowering |
| 5 | **Drop-in adoption**: depfiles, shared libraries, PIE, flag tolerance, a build-system corpus, LSP | Unknown flags are hard errors; no `-M*`, no `-shared`; `-fPIE` rejected on x86-64 ELF | Unlocks real build systems and a far larger regression corpus | [#1232](https://github.com/buster14a/buster/issues/1232), [#1418](https://github.com/buster14a/buster/issues/1418), [#1398](https://github.com/buster14a/buster/issues/1398), [#423](https://github.com/buster14a/buster/issues/423) | No owner for `-shared` output, an LSP, or a sole-compiler project ladder |
| 6 | **Velocity multipliers**: a workload-shape benchmark, a built-in reducer, differential fuzzing, dogfooding | 243 of 361 open issues are unlabeled, mostly single-cause correctness bugs filed 2026-09-25..27; three reductions here were manual | Makes every later fix and measurement cheaper | `ide metamorphic`, [#1583](https://github.com/buster14a/buster/issues/1583) | No reducer; the per-file build shape is not a gate |

The order reflects size of measured waste, independence from unfinished
retirement work (#36), and how much existing code each leap reuses. Leaps 1,
2 and 5 are mostly *driver and service* work that composes with any backend;
leaps 3 and 4 change the compiler's core and interact with the #31/#36
migration.

## 1. Project-scale compilation

### What the measurements say

| Lua 5.4.8, 33 TUs, `-g` | Wall |
|---|---:|
| 33 `ide cc -c` processes, serial | 1.39-1.55 s |
| 33 processes, 4 at a time | 0.43 s |
| one `ide cc -c` with 33 inputs (serial, one process) | 0.55-0.59 s |
| one link invocation, `-fcompile-jobs=4` | 0.44-0.50 s |
| unity `onelua.c` (upper bound for header sharing) | 0.34-0.42 s |
| TinyCC, 33 processes, serial | 0.30-0.31 s |

The per-process floor is x86 table preparation: `-fsyntax-only` of an empty
file costs 2.7 ms, `-c` of the same file 21 ms. #1295 records 124 M
instructions per invocation and a prototype that removes 30-37 M of them.

The in-process path already exists but is unfinished: `-c` with several
inputs is serial; `-fcompile-jobs` only batches consecutive inputs of a *link*
invocation, in fixed cohorts of one TU per worker that wait for their slowest
member; and per-worker arenas return to the wrong pool (#1265), so each
cohort faults fresh memory (system time 0.12 s at one job, 0.35-0.40 s at
four).

### Mechanisms, in dependency order

1. **Finish removing the floor.** #1295's demand-driven tables are the owner.
   A stronger variant is to materialize the prepared tables at build time into
   read-only data derived from the generated metadata, so an invocation pays
   nothing and the pages are shared through the page cache. Its cost is the
   self-host compile of the larger initializers; measure both.
2. **A batch mode that build systems can use.** Let `-c` accept
   `-fcompile-jobs=N`; claim TUs with an atomic take-index in longest-first
   order (by input size) instead of fixed cohorts; keep one warm arena set per
   lane (fixing #1265). A new `ide build <compile_commands.json>` command
   (proposed) would let CMake, Meson and Bear drive one process per project.
   Go and Zig avoid per-file processes this way.
3. **A resident compiler.** A proposed `ide serve` would hold prewarmed
   tables, warm arenas and caches, and `ide cc` would forward argv, the
   working directory and the relevant environment over a per-user socket,
   falling back to in-process compilation when no server answers, so make and
   ninja need no change. Warm arenas also avoid the system time (first-touch
   faults and zeroing) that is 30-50% of the self-compile's wall time on this
   VM. zapcc reported 2-5x on full C++ builds and 10-50x on single-file
   rebuilds with this shape *(its README)*; Buck2 and Gradle use the same
   daemon pattern.
4. **Share header state.**
   - *Step 1 — raw tokens per header*, keyed by file identity and content
     hash, plus include-resolution and `stat` caching. Lexing does not depend
     on macro state.
   - *Step 2 — preamble snapshots*: the complete frontend state after a TU's
     leading run of directives (tokens, macro table, include-once state,
     entities, types, scopes), keyed by compiler identity, target and options,
     predefined macros and the content hash of every file read. clangd's
     preamble works this way; GCC's PCH is the manual form. Buster's frontend
     tables are integer-ID arrays in arenas with no pointer graph, which is
     what makes a snapshot copyable. The obstacle is phase structure:
     preprocessing, parsing and analysis each run over the whole token stream,
     so a preamble must become a resumable boundary.
   - *Step 3 — lazy analysis of header declarations* (cache-free): the
     `Python.h` unit declares ~1,633 functions and 55 `static inline`
     definitions to serve five lines. Reachability already skips lowering of
     unreferenced internal functions (`function_needed` in `c_gen.c`), but
     every declaration's type is still analyzed. Deferring that until first
     use in system headers needs a deferred-diagnostics policy; GCC and Clang
     already treat system headers differently.

### Expected size (model)

- Lua: 0.55 s in one process, toward 0.34-0.42 s with header sharing
  (the unity bound), divided across four lanes if TU claiming scales like
  independent processes do (3.5x): **~0.10-0.16 s against 0.43 s for
  `make -j4` today.**
- `Python.h` extension: its own code lowers to 35 canonical instructions. With
  no floor and a warm preamble the remaining work is that code plus the
  object write: **~10x** against 87-94 ms.

### Risks

Cache keys that miss an input (MSVC's `/Gm` was deprecated for missing
header changes); `__COUNTER__`, `__DATE__`, `__TIME__` and `#pragma` state
inside a preamble; diagnostics that must be replayed from a snapshot;
memory growth in a long-lived server; a per-user socket's security;
determinism, which requires snapshots to be byte-for-byte what a cold compile
would build. A periodic cold-compile comparison is the standard guard.

## 2. Incremental and live

Today a one-file Lua edit costs a ~45 ms compile (about 22 ms of it floor)
plus a 36-49 ms link. Buster owns every piece needed to go further: object
writers, the linker and an in-process JIT loader
(`jit_link_object` with explicit host bindings).

1. **TU result cache.** Key an object by compiler identity, options and the
   exact list and hashes of files the preprocessor read (the driver already
   reports `lexed_files`). This is a precise, built-in ccache. Go's action
   cache is the model *(summary)*.
2. **Function cache.** #1470 already reuses encoded functions with
   byte-identical output across 1,116 randomized edits, but saves no time
   because preprocessing, analysis and lowering (~48%) always rerun and its
   keys cost 0.35 s per run. Leap 1 changes that economy: in a resident
   process the keys live in memory and the frontend is mostly preamble.
3. **In-place relinking.** Reserve padding per output section, keep
   unchanged sections and symbols at fixed addresses, and patch only changed
   function bodies and their relocations. Zig's self-hosted ELF linker does
   this for `-fincremental` *(its issue #21165)*; MSVC `/INCREMENTAL` pads and
   uses thunks *(summary)*; Wild's design targets feedback within 10 ms of
   saving *(its design post)*.
4. **Hot reload and `ide run`.** A proposed `ide run file.c` would compile
   to memory, resolve undefined symbols with `dlsym`, and call `main`
   (TinyCC's `-run`). Hot reload would compile a changed unit, link it with
   bindings that point its references at the *running* image's existing
   globals and functions, and patch old entries with a jump to the new code —
   the Live++ and jet-live approach, which Zig also planned through a global
   offset table. No published measurement of sub-100 ms edit-to-running-code
   for C was found; given Leap 1, tens of milliseconds is a **model**.

Risks: layout changes to live structures cannot be hot-swapped; inlined
copies and function pointers held by the program keep old code alive;
incremental images must be either byte-identical to a clean link or clearly
restricted to development builds.

## 3. Emit less machine code

The SQLite measurements put Buster's FAST output at 2.61 MB of `.text`
against 0.97 MB for Clang -O0 and 1.27 MB for TinyCC, and its runtime at
2.12 s against 1.15 s and 1.31 s. `sqlite3Get4byte` shows the causes: 17
instructions under Clang -O0, about 60 under Buster, because constant
indices are materialized and added instead of folded into `[rdi+1]`,
constant shifts go through `cl`, values pass through extra copies and frame
slots, and a leaf function builds a frame with a stack probe. #1473's census
reaches the same picture from the encoder side: frame-slot loads and stores
are ~40% of self-host instructions and aggregate copies 37% of its bytes.

1. **Fix the whole-module FAST decline now.** When any function fails the
   stricter canonical validator, `ir_prepare_canonical_module` skips FAST for
   the entire module. A function returning a function pointer that also has
   a prior prototype (`static int (*pick(int))(int);` followed by its
   definition) produces an IR `RETURN_TYPE` mismatch, which disables FAST for
   all 2,548 SQLite functions. Fixing the mismatch and declining per function
   is worth -10.5% canonical instructions and -7.6% `.text` on SQLite at no
   compile-time cost.
2. **Keep values in registers until pressure spills them.** The dominant
   pattern is values homed in frame slots. TPDE's single pass computes a
   block order, live-interval ends and reference counts, then selects,
   allocates and encodes together, freeing a register when its last use is
   passed; it reports 8-24x the speed of LLVM's -O0 back-end with run time
   within about ±9% *(paper, summary)*. Cranelift's single-pass `fastalloc`
   documents the same constraint that bit it: a fast allocator must degrade
   to spilling, never fail, on awkward constraint shapes *(FASTALLOC.md)*.
3. **Fold at selection.** Immediates into ALU, shift and compare forms;
   base-plus-displacement and scaled-index addressing; compare-and-branch;
   single-use loads into memory operands. #1473 defers exactly these
   (`zero-compare-to-test` needs a compare-with-immediate MIR form;
   `constant-sign-extend-fold` needs canonical folding).

This is a throughput lever as well as a code-quality one: selection,
placement, encoding, line records and object bytes scale with machine rows,
and those stages are ~35-40% of a large compile. The priority rule in
`AGENTS.md` against spending compile time on generated code is not in
tension here, because the proposal removes rows; each change must still show
that it lowers total compile time. TPDE's own Clang measurement is the
caution: an 8-24x back-end gave ~1.5x end to end because the front end
dominated *(slides, summary)*.

## 4. Fewer walks, less memory, per function

There is no expression AST: lowering re-walks token ranges. On SQLite,
`c_parse_validate_lowering_constraints` (19%) and `c_ir_lower_body` (21%)
each walk every body and each derives expression types again. The validation
pass came from PR #860 (2026-09-19), so that `-fsyntax-only` diagnoses what
lowering would reject. #1551 counts eleven loops over body tokens in
validation alone; #1247 and #1229 find five constant evaluators and four
type-compatibility engines.

1. **One semantic walk per body.** Fold the lowering-constraint checks into
   the lowering walk, with emission disabled for `-fsyntax-only`, so that each
   body is typed once. This keeps the canonical-IR contract and adds no IR:
   the checks move to the walk that already derives the types. TinyCC's
   single pass is the extreme form; its speed bounds how much the extra walks
   cost.
2. **Function-at-a-time back half.** Lower, prepare, select, allocate and
   encode one function, then release its IR and MIR. Peak memory for the
   self-compile is 388 MB after analysis and 1.1 GB after code generation;
   TinyCC compiles SQLite in 18 MB. Draft PRs under #1544 cut peak RSS 22.7%
   by releasing phase state at boundaries, and PR #833 priced a
   function-at-a-time order. Streaming also creates the unit of work for #54's per-function
   parallel back end and for a pipeline in which one lane lowers function
   *n + 1* while others finish function *n*. Zig's threaded code generation
   (one analysis thread feeding N code-generation threads and one linker
   thread) cut its behavior-test build 55.8% *(PR #24124)*.
3. **Parallel lowering stays gated by #531.** Its no-go is about the current
   code, in which 21% of bodies publish shared state and account for 61% of
   the body loop's time. Deterministic function-local naming (for example
   string literal symbols keyed by function and ordinal instead of a global
   counter) is the usual cure, but it is a redesign, not a cut.

## 5. Drop-in adoption

`ide cc` rejects unknown options. It has no `-M`, `-MD`, `-MMD` or `-MF`
(and ignores `-Wp,-MMD`, so `make` reuses stale objects — #1232), no
`-shared` output, and rejects `-fPIE` for x86-64 ELF, which CMake passes for
position-independent executables. These stop configure scripts, CMake and
Meson before a single file compiles. Other C compilers grew adoption this way:
Kefir keeps a suite of about 100 real projects (bash, curl, git, OpenSSL,
PostgreSQL, Python, SQLite and others), and slimcc's CI builds projects such
as CPython, curl, git and PostgreSQL with slimcc as the *sole* compiler and
requires their tests to pass *(summary)*. `zig cc` was adopted as a drop-in
cross compiler, for example for all C in Uber's Go monorepo *(summary)*. A
compile-only census on 2026-09-26 already produced ~25 issues from 14
projects.

- Dependency files, shared libraries, PIE and a tolerated-flag list first.
- Then a ladder of projects built through their own build systems
  (curl, git, Redis, PostgreSQL, CPython, FFmpeg), each a harness in the
  existing style, before the Linux kernel (`asm goto`, must-fold builtins,
  linker scripts).
- An LSP server fits naturally inside the resident process of Leap 1: the
  frontend is fast enough to re-analyze a unit per keystroke.

## 6. Velocity multipliers

- **Measure the workload shape users have.** Every audit to date optimizes
  the unity self-compile. A per-file build of a real project (Lua, a
  header-heavy extension, Buster's own split build) as a first-class gate
  under #423 would have exposed the floor and header costs long ago.
- **A built-in reducer.** Delta debugging over the preprocessed token stream
  with a predicate (an error text, or a mismatch against Clang) turns a
  compatibility report into a minimal repro in minutes. All three reductions
  in the audit were manual bisection.
- **Differential fuzzing** against Clang and GCC with a generator in
  `tools/`, alongside `ide metamorphic`.
- **Dogfooding.** Clang needs 5 min 5 s here to build Buster's Release unity
  TU; Buster compiles the same source in 6-7 s, but the result compiles
  SQLite ~4x slower here, and #61 measures 7.36x the instructions. Once
  Leap 3 closes most of that gap, the development loop can use a Buster-built
  Buster.

## What does not look like a leap

| Idea | Evidence |
|---|---|
| Transparent huge pages as the memory fix | Forcing THP on this VM cut faults from 284K to 2.3K but doubled RSS and did not reduce system time; #1585 reports 6-7% on the 9700X. A policy choice, not a leap. |
| Whole-program pruning before codegen | `--gc-sections` removes 12 of 4,447 self-host functions. |
| Parallel lowering as the code stands | #531's census bounds it at ×1.22-1.24. |
| A separate baseline IR or tier | #546 recommends one canonical and one machine IR with a work budget per mode; nothing here argues otherwise. |
| More SIMD in a flat profile | Worth doing where measured, but no remaining phase is large enough for an order of magnitude. |

## First experiments

Each is bounded and falsifiable, and each names its stop rule.

1. **Per-function FAST decline and the prototype fix.** Metric: SQLite
   `IR_FAST functions` and `.text`; stop if any fixture's output changes
   beyond the intended functions.
2. **Batch `-c` with dynamic claiming.** Lua, SQLite plus shell, and a
   split build of Buster at 1/2/4/8 lanes against the same number of
   processes. Accept at ≥1.5x over processes at equal lanes with identical
   objects and RSS within 1.5x of `-fcompile-jobs=1`.
3. **Preamble token cache inside batch mode.** Cache the preprocessed prefix
   and macro table for identical leading directive runs. Metric: header
   share on the Lua and `Python.h` units; stop if byte-identical objects fail
   anywhere in `test_all`.
4. **Declarations analyzed versus referenced.** A census across the
   compatibility corpus bounds lazy header analysis before any design.
5. **Register residency for leaf functions.** Rows, `.text`, compile time
   and SQLite runtime; accept only if compile time does not rise.
6. **`ide run` minimum.** `jit_link_object` plus `dlsym` bindings. Metric:
   time to first output for `hello.c` and for the Lua interpreter against
   `tcc -run`.

## Sources

Measurements: audit [`2026-09-27T155317Z`](performance-audits/2026-09-27T155317Z.md)
and its [evidence](performance-audits/evidence/2026-09-27T155317Z/).

External (*summary* marks a source read through a search summary):

- TPDE: <https://github.com/tpde2/tpde>, <https://arxiv.org/abs/2505.22610>
  *(summary)*, <https://discourse.llvm.org/t/tpde-llvm-10-20x-faster-llvm-o0-back-end/86664>
  *(summary)*.
- Copy-and-patch: <https://arxiv.org/abs/2011.13127> *(summary)*;
  CPython's JIT, <https://github.com/python/peps/blob/main/peps/pep-0744.rst>.
- Cranelift single-pass allocator:
  <https://github.com/bytecodealliance/regalloc2/blob/main/doc/FASTALLOC.md>.
- TinyCC: <https://bellard.org/tcc/> *(summary)*,
  <https://github.com/TinyCC/tinycc/blob/mob/tcc-doc.texi>.
- Zig threaded code generation and incremental linking:
  <https://github.com/ziglang/zig/pull/24124>,
  <https://github.com/ziglang/zig/issues/21165>.
- Go build cache and export data: <https://pkg.go.dev/cmd/go/internal/cache>,
  <https://jayconrod.com/posts/112/export-data--the-secret-of-go-s-fast-builds>
  *(summary)*.
- zapcc: <https://github.com/yrnkrn/zapcc>.
- Wild incremental linking design:
  <https://github.com/davidlattimore/davidlattimore.github.io/blob/main/_posts/2024-11-20-designing-wilds-incremental-linking.md>.
- mold: <https://github.com/rui314/mold>.
- MSVC incremental linking:
  <https://learn.microsoft.com/en-us/cpp/build/reference/incremental-link-incrementally>
  *(summary)*.
- Hot reload: <https://github.com/ddovod/jet-live>, <https://liveplusplus.tech/>
  *(summary)*, Zig's offset-table proposal <https://github.com/ziglang/zig/issues/5260>.
- Minimal rebuild caveat:
  <https://github.com/MicrosoftDocs/cpp-docs/blob/main/docs/build/reference/gm-enable-minimal-rebuild.md>.
- Real-project ladders: <https://github.com/fuhsnn/slimcc> *(summary)*,
  <https://github.com/sourcehut-mirrors/kefir>.

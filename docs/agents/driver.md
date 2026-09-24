# Compiler driver and target options

[Agent instructions](../../AGENTS.md) · Paths and commands below are relative to the repository root.

## Compiler output streams

`ide cc` writes warnings, source diagnostics, and `cc: error:` driver errors to
stderr. With `-E` or `-S` and no `-o`, the generated text goes to stdout;
warnings cannot enter the preprocessed or assembly stream. `#warning` and
`#error` messages retain the spelling between the first and last message
tokens, including punctuation and internal whitespace, without expanding macros.

Opt-in machine-readable records remain on stdout: `CODEGEN_VERIFY`,
`CODEGEN_FALLBACK*`, `CODEGEN`, `IR_*`, `TARGET`, `GPU`, and the `-v` source
statistics. The differential runner reads `CODEGEN_VERIFY` there and compares
captured stderr diagnostics separately; the retirement census reads its
`-v`/fallback records from stdout. `ide metamorphic` uses stderr first when
reporting a failed compiler invocation. A `-v -E` invocation also prints the
requested statistics on stdout; use plain `-E` when piping preprocessed C.

## Opt-in native translation-unit lanes

`-fcompile-jobs=N` accepts a positive 32-bit worker request. Omission (or
zero in the invocation API) means one worker. Only consecutive native C
inputs in a link invocation are batched; preprocessing, syntax-only, `-S`,
`-c`, LLVM/GPU/Wasm/eBPF paths and single-input fast paths retain their
existing execution. Objects, archives and assembly are serial boundaries,
even when `-x c` is present. Worker count is clamped to logical CPUs, input
count and one inside an embedding caller's multi-lane gang.

Each cohort contains at most one full TU per worker. `lane_range` gives
stable input slots, the existing persistent gang is reused, and each worker
uses a private TU arena and diagnostic collector. No worker writes an output
file. The caller deep-copies compact objects and diagnostics in input order,
retains constructor priorities, and uses the existing ordered linker. The
first failing input wins; later completed results and warnings are discarded
and all cohort arenas are released. The one-worker and
`BUSTER_SINGLE_THREADED` builds run the same unit kernel.

`compiler_parallel_prewarm()` prepares both native target families, including
all x86 per-form caches and exact plans, before the first persistent worker.
Both are needed because a later invocation may switch architectures while
workers are parked. Embedding callers must call it before starting their own
gang. The lighter `compiler_prewarm()` remains the serial frontend/common-table
entry. The full cold prewarm is an opt-in cost, not a new serial startup floor.
`CompilerDriverResult.compilation_workers` reports the largest active cohort
(or one); it is not a physical-core or memory measurement.

This is a bounded feature, **not an accepted throughput improvement**. The
default stays one pending paired representative measurements. A large input
can still dominate a cohort; no adaptive grain, dynamic claim queue, cgroup
admission or physical-memory estimate is introduced. Full-TU retention is
bounded by the worker request, but compact objects still accumulate for the
link as before. Function compilation remains serial within each TU, preserving
signature/source-cursor and inline-assembly ordering. Existing SIMD kernels
inside each lane are unchanged.

The Clang-like `ide cc` driver accepts `-march=<model>` and
`-mcpu=<model>` (or their separated forms), ordered target-feature overrides
through `-mattr=+feature,-feature`, and x86 assembly dialect selection through
`-masm=att|intel`. CPU and feature options also accept separated values. CPU names use the canonical
spellings printed by `cpu_model_to_string_os`, such as `baseline`, `native`,
`haswell`, `znver5`, and `apple-m4`; incompatible target/model pairs are
diagnosed. `-v` reports the selected CPU, the sorted effective feature set,
and maximum native vector width. `-target`/`--target` strings are
`arch[-vendor][-os][-environment]`: the vendor and environment components stay
free-form, but a CPU model there is rejected in favor of `-march=`, and so is
anything past the fourth component. Both used to be dropped silently, which
left baseline code generation and no hint that the request was ignored.
Native x86-64 and AArch64 compilation uses the FAST register allocator at
every optimization level, including the default and `-O0`, while
`-fno-register-allocator` selects the canonical stack emitter. Advanced and
diagnostic callers may select `none`, `mir-stack`, `fast`, or `quality` with
`-fregister-allocator=<mode>`; when several allocator-affecting options are
present, the last one wins.
The allocators run on x86-64 under both System V and Win64, and on AArch64
including ordinary Windows/UEFI functions with validated compact MIR frame
and unwind records. Windows and Darwin AArch64 variadic definitions and calls
use MIR with their platform argument placement and pointer lists. Win64 differs
from System V in the file it allocates — RSI and RDI are
callee-saved there, so the allocator has seven callee-saved registers instead
of five and the vector class keeps only the volatile ZMMs — and in how a call
is built: the outgoing arguments and the callee's shadow space are written
into a fixed area at the bottom of the frame instead of pushed, because the
stack pointer must not move inside the body of a function whose unwind data
can only carry a frame-pointer offset up to 240 bytes. Its prologue pushes the
callee-saved registers before establishing the frame pointer for the same
reason. Windows/UEFI variadic definitions and calls use the positional home
area and float-register duplication described in the [machine guide](machine.md).
Win64 indirect aggregate arguments use private caller copies with up to
sixteen-byte alignment, as described in the [machine guide](machine.md).
Win64 128-bit integer signatures pass arguments indirectly and return in XMM0.
Shapes the Win64 subset does not build yet — split wide vector signatures and
aggregate arguments aligned above sixteen bytes — fall back per function,
which `-v`'s `fallback_functions` and `CODEGEN_FALLBACK` lines report.
`CODEGEN_FALLBACK_REASON` additionally identifies the target, allocator and
stable reason name for every fallback. Its disjoint counts sum to
`fallback_functions`: `target-excluded`, `signature`, `opcode`,
`selection-other`, `verification`, `placement`, `encoding`, `output-capacity`,
and `unwind`. `signature` means the target's function ABI gate rejected the
signature; `opcode` retains the first rejected canonical opcode in the legacy
`CODEGEN_FALLBACK` census. `selection-other` is deliberately unclassified,
while `verification` identifies an implementation failure. The allocator,
stage, opcode and reason counters all survive multi-input compilation.

The signature-reason negative control uses the currently direct-only 32-byte
Win64 vector ABI. Narrow vectors and argument count have strict-success regressions and
must not be constrained to keep a telemetry test failing.

For source-assembled conversion forms, an ordinary memory qualifier names
its source width, not the destination mnemonic suffix or register width.
Legacy, VEX and XOP candidates publish that fixed width directly; EVEX
FULL/HALF candidates publish a scalar element plus a source tuple. A broadcast
qualifier names the scalar element. The selector projects each compatible
candidate independently and keeps an explicit qualifier as a constraint. For
example, masked `vcvtps2pd zmm0, m256` and VEX `vcvtps2pd ymm0, m128` both
read 32-bit elements, while masked `vcvtpd2ps ymm0, m512` reads 64-bit
elements. AT&T's unqualified memory spelling uses the same candidate contract.
When the same visible operands admit different unsized source widths, selection
rejects the source as ambiguous; encoding length and candidate order never
choose the number of input lanes.

The default `-fcanonical-fast` shared pipeline and independent
`-fcanonical-fast-{fold,address,dce,parameters}` controls are described in
[the FAST pipeline contract](../canonical-fast-pipeline.md). Timing is separate
(`-ftime-canonical-fast -v`); register allocation selection is unchanged.

`-fno-frontend-ssa` selects the original memory-form C lowering;
`-ffrontend-ssa` restores direct SSA for the bounded supported subset. The last
flag wins. These controls are independent of `-fno-canonical-local-promotion`
and `-fno-target-local-promotion`: disabling shared promotion does not undo
SSA already built by the frontend. For a fully memory-form differential input,
disable frontend SSA as well. Verbose compilation reports `IR_FRONTEND_SSA`
counters beside `IR_LOCAL_PROMOTION`; see the
[frontend ownership contract](frontend/foundations.md#direct-local-ssa-github-34).

`-fsysv-unnamed-bitfields=integer|padding` selects the classification of
nonzero-width unnamed bit-fields on native System V x86-64 targets. `padding`
is the unchanged Buster default; `integer` includes those fields in INTEGER
eightbyte classification for GCC interoperability. Zero-width fields contribute
no class in either mode, and object layout is unchanged. The last selection
wins. Invalid values, other native conventions, nonnative targets and LLVM
bitcode output reject the option. This is one explicit ABI boundary, not a
general emulation of any GCC or Clang version. Compile interoperating units
with the policy their external objects use; the linker cannot infer it.

The configured-host packed-layout tests use an independent register probe
(`tests/host_sysv_unnamed_bitfields.c`) instead of guessing from a version
string. A later float argument forces a known live XMM0 value under either
convention. Both link directions then run all four allocators and both frontend
forms, including later integer/float parameters and an assembly return control
that zeros the unselected return register. A failed or unknown probe fails the
test; it never silently assumes a convention or waives a mixed-link check.
The policy-specific caller also requires `-fverify-codegen` in both link
directions. The original caller remains intact; its separate underaligned
volatile aggregate construction defect is tracked in #398.
`IR_LOCAL_PROMOTION_WORK` reports shared-promotion parameter-cleanup sweeps and
actual visits, separately from removed rows. The [middle-end pass map](../middle-end-pass-map.md)
defines their scope, invalidation rules and separate diagnostic replay protocol.

`-fno-machine-fallback` makes native C coverage strict: after code generation
succeeds, any fallback fails the translation unit before object writing and
reports its first function, source, target, allocator, opcode and reason.
`-fmachine-fallback` restores the normal differential-oracle behavior; the last
of these two flags wins. Strict mode requires a native target and a machine
allocator (`mir-stack`, `fast` or `quality`); NONE, direct non-native emission,
preprocessing and syntax-only checks cannot satisfy the gate. Assembly inputs
and linked prebuilt objects have no canonical C functions to gate.
For example, `build/Release/ide cc -fregister-allocator=mir-stack -fno-machine-fallback -target aarch64-unknown-linux -c tests/basic_c_call_abi.c -o build/mir-coverage.o`.
`compiler_driver_test_machine_fallback` runs the same eleven-fixture arithmetic,
control-flow, call-ABI, aggregate and frame corpus for x86-64 and AArch64 on
Linux, macOS and Windows, under all three machine allocators and both explicit
frontend forms in `test_all`, including CI. Its 396 object-compilation rows
require 396 non-empty strict successes, including the two variadic fixtures on
Windows/Darwin AArch64. Any future explicit refusals require exact fallback-function, reason and opcode counts, preserve an
existing output, and still compile through the direct fallback. They are not
skips; implementing a gap must replace its refusal expectation with strict
success. Every target/allocator/frontend cohort emits a `MIR_COVERAGE` row
with actual strict successes, validated expected rejections and failures.
Object compilation is not target execution. Separate AArch64 vector,
integer-pair and sixteen-byte atomic load/store tests, unsupported signature
controls, Windows/UEFI large-frame tests, and native Windows ARM64
unwind-boundary execution remain registered. The atomic lane is strict across
all AArch64 desktop targets, allocators and frontend forms; its broader
aggregate and i128 censuses both require zero fallback, including exchange,
arithmetic/bitwise updates and compare-exchange. The separate nine-function
atomic-update fixture covers all three AArch64 desktop targets, four allocator
modes and both frontend forms; MIR legs reject fallback, and only the matching
native desktop executes the result. This adds 24 object-compilation cases
outside the eleven-fixture floor above. The direct backend remains its
semantic reference, with failed wide CAS requiring a validated pair read.
This corpus is a coverage floor for #36, not a claim of complete MIR lowering
or permission to retire the canonical oracle.

## C input phase selection

A `.c` input and any path under `-x c` begin as raw C source and run the full
preprocessor. In automatic language mode, `.i` begins as preprocessed C;
`-x cpp-output` selects that same phase for any suffix, including an
extensionless path. `-x c` deliberately overrides a `.i` suffix, while
`-x none` restores suffix inference.

Preprocessed C still runs the normal lexer, identifier interning, source-map
publication, numeric/`#line` marker handling, pragma state and C23 keyword
normalization. It does not replay command-line `-D`/`-U` operations, includes,
conditional or definition directives, diagnostics directives, or macro
expansion in ordinary text. `-E` on preprocessed C serializes that retained
token stream with the normal output spacing; it is not a second preprocessing
pass and need not preserve the input bytes verbatim. This is a starting-phase
distinction, not a new C dialect or backend.
`COMPILER_DRIVER_LANGUAGE_CPP_OUTPUT` exposes the same contract to invocation
API callers; a future per-input `-x` snapshot can carry that language value
without changing the frontend contract.

A `.s` input, or any input under `-x assembler`, is an assembly translation
unit rather than a C one. `assembly_unit_encode` (`assembly_unit.c`) is the
layer above `assembly_encode`: it interprets the directive vocabulary, tracks
one offset per section, resolves labels, and hands each instruction line to
the instruction layer beneath, and the driver turns its sections, symbols and
relocations into an `ObjectFile` like any other. The vocabulary is `.text`,
`.data`, `.bss`, `.rodata` and `.section`; `.globl`/`.global`, `.weak`,
`.hidden`, `.type` and `.size`; `.align`, `.balign` and `.p2align`; `.byte`,
`.short`/`.word`/`.hword`/`.value`, `.long`/`.int`, `.quad`, `.ascii`,
`.asciz`/`.string`, and `.zero`/`.skip`/`.space`; `.intel_syntax noprefix` and
`.att_syntax prefix`; and the `.cfi_*` family, accepted and dropped because it
describes unwinding rather than bytes. Anything else -- a directive the table
does not claim, or an operand form one of these does not cover -- is a
diagnostic naming the directive and its line, the way every other unsupported
construct here is reported rather than silently dropped.

Text alignment without an explicit fill uses x86-64 NOP bytes or complete
little-endian AArch64 NOP instructions. A partial AArch64 instruction boundary
is zero-filled before the NOPs; explicit fills remain repeated bytes on both
targets. Data alignment defaults to zero fill.

Three things that layer owns rather than the instruction layer. Local numeric
labels: `1:` becomes a generated name and `1f`/`1b` resolve to the nearest
following or preceding definition in source order, and those names leave the
symbol table again once every reference to one is folded, the way GNU as drops
its own `.L` locals. A repeat or lock prefix alone on a line joins the
instruction on the next one. And a same-section PC-relative reference to a
label defined in the file is written into the bytes; only a cross-section or
undefined name becomes a relocation. `@PLT` is dropped: a static link resolves
such a call the same way it resolves a plain one. Sections keep their own
names -- `.init` and `.fini` are neither `.text` nor absent -- and a
hand-written section gets alignment 1, because `crti.o` and `crtn.o`
contribute one and two bytes to `.init` and any padding between them would
run as code.

A forward branch to a label always uses the near form: the instruction layer
sizes a statement before the label is known and this assembler does not relax.
`.S` inputs run through C preprocessing with assembly comment-line handling.
The printer preserves line structure and source adjacency when adjacent emitted
spellings still re-lex as the same tokens. A lexical boundary check inserts one
space when keyword respelling or macro replacement would instead fuse
identifiers, preprocessing numbers, literal prefixes, punctuators or comment
openers, and keeps a backslash token from splicing away a generated newline.
The root input splits unquoted dollar prefixes before lexing. Assembly errors
resolve lazily back to originating tokens and physical positions, including
`#line` identities; an inserted separator itself has no source range.

`-D` and `-U` form one ordered macro-operation stream across native C,
preprocessed assembly, and external GPU forwarding. Predefined macros are
installed before that stream is replayed, so later command-line operations win;
function-like `-D` operands use the same parameter and replacement parser as a
source `#define`. API callers that still populate separate definition and
undefinition arrays retain the historical compatibility order (all definitions,
then all undefinitions), but a nonempty ordered stream is authoritative.

C, assembly and backend failures publish the shared
[diagnostic contract](../diagnostics.md). Strict fallback uses symbolic opcode
names and `not-applicable` for signature/target exclusions, while tooling retains
internal IDs in the optional backend context. Source locations name the resolved
file, including remapped or included source, rather than always the top-level input.

`-emit-llvm` emits binary LLVM bitcode directly from canonical typed IR for C
inputs. It writes `<input>.bc` by default, accepts `-o` for a single
input, and rejects native objects, archives, libraries, frameworks, linker
arguments, `-E`, `-S`, and `-fsyntax-only`. The writer has no LLVM dependency;
see `LLVM_BITCODE.md` for its target metadata, API, and supported boundary.

Static archive extraction uses `compiler_driver_archive_extract` in the
private `driver/archive.c` implementation. Its invocation-owned name table
records selected definitions and strong/weak undefined references once per
newly selected object. Each archive occurrence builds symbol-to-member provider
lists and a heap ordered by `(scan pass, member index)`. A dependency discovered
behind the cursor belongs to the next pass, preserving the former forward
fixed-point selection sequence and first-definition behavior. Earlier archives
are revisited only when explicitly present again; `-l` archives retain their
existing driver placement after ordinary inputs. Definition binding strength
does not alter eligibility once a selected definition exists.

Provider state changes are monotonic: an edge is revisited at most three times.
Selection work is expected O(S + A + D log(M + 1)), with S visited selected
symbols, A archive symbols, D archive definition edges and M archive members;
hashing includes symbol-name bytes. Provider lists and heap storage are scratch allocations for one archive.
The name table is created only at the first nonempty archive with selected
inputs, survives subsequent archives, and is destroyed before object linking
or on an earlier driver error. No index storage remains in the returned result.
Before allocating state, archives with at most eight selected objects, eight
members and 32 total symbols use a bounded scan with a stack selection mask.
An archive without undefined global symbols takes one indexed forward pass;
it cannot introduce new extraction requests and does not retain irrelevant
member names. The synthetic runtime-object checks retain their single-member
path.

`compiler_driver_archive_tests` compares exact member sequences, linker errors
and successful ELF/COFF/Mach-O object bytes against an independent scan oracle.
The fixed duplicate-definition fixture also verifies which member supplies the
winning byte. Set `BUSTER_ARCHIVE_BENCH=1` for paired reverse-chain, forward-chain
and irrelevant-member timing rows (with both one and many root references) within the driver tests; setup and destruction
are included and timing never gates correctness. `state_bytes` is the name
arena's used prefix (including superseded growth tables) before destruction,
not physical RSS or the per-archive scratch peak.

An undefined weak ELF reference does not select a static archive member.
It may bind to a member selected for a separate strong dependency, to a
direct object input, or to an already included shared library. Keep archive
selection separate from those later resolution rules (GitHub #226).

ELF executable data placement honors both page and requested object alignment.
Align the final virtual address, not only its file offset: an initialized
global may require alignment larger than a page or the fixed image base.
The object writer already carries that requirement into the section metadata
(GitHub #225).

Every hosted ELF link reads the shared libraries' own dynamic symbol tables.
`compiler_driver_elf_library_exports` looks `libc.so.6` and each requested
library up where the loader would — the `-L` paths, then the sysroot or host
`lib`/`usr/lib` roots, multiarch first — and rejects a file whose ELF machine
disagrees with the target, so a cross link never reads the host's own libc.
`compiler_driver_elf_dynamic_symbols` walks that table once and produces two
things.

The first is the defined global and weak **objects** with their addresses and
sizes, as `NativeDynamicDataSymbol` arrays, which `link.c` uses to reserve
copy-relocation slots: a slot stands for the library's object rather than the
one name the program spelled, so it carries every name the library exports at
that address, takes the library's own size, and is shared by two imported names
for one object. The alias set is what makes `extern char **environ` work. A
definition in the executable takes precedence over the library's for every one
of its names, and glibc stores the environment through `__environ` after
startup; an executable that defined only `environ` left that store in libc's
own storage while the program read a copy taken before startup ran, which is to
say null. This half is still collected only for a link with an undefined data
symbol, since a link with none has nothing to copy, and a library that cannot
be found or parsed leaves the pointer-sized slots the writer reserved before
alias sets existed.

`link_elf_index_initialize` builds one temporary index per ELF link, shared by
writer selection, weak/strong import classification, version binding and the
AArch64 staging writer. Names keep the first data definition and the first
default version in runtime/library/export order. Data objects are grouped by
export-table identity and address; their alias chains retain export order.
The first imported name owns the copy slot. Global-name membership and per-slot
alias deduplication are indexed too. Skipped aliases do not enlarge a slot.
Version pairs retain first-use numbering and per-library emission order.
Aggregate counts and index storage are checked before allocation; all indexes
are rewound on both successful and failed links.

The second is the **symbol version** of every defined entry, functions
included, read from the library's `.gnu.version` and `.gnu.version_d` into
`NativeDynamicVersionedSymbol` arrays. That half is collected on every hosted
ELF link, because versioning applies to functions and there is no cheaper way
to know: reading `libc.so.6` where nothing did before costs about 0,65 M
instructions, a tenth of a percent of the smallest hosted compile. It buys two
things in the x86-64 dynamic writer, and the AArch64 one through it:

- **A reference records the version it bound to.** `.gnu.version` carries one
  index per dynamic symbol, `.gnu.version_r` names per library the versions
  the image needs, and `DT_VERSYM`/`DT_VERNEED`/`DT_VERNEEDNUM` publish both.
  The alias names a copy slot defines are versioned too, because each of them
  is a name the library publishes. Without this the image binds by name to
  whatever the running glibc calls default, which is the versioning
  mechanism's whole purpose: `readelf -W --version-info` on a Buster
  executable now agrees with GNU ld's for the same program, `stat@GLIBC_2.33`
  included. Both sections are omitted when nothing needed a version, and the
  layout then collapses to exactly what it was before, so an unversioned
  library's image is byte-for-byte unchanged.
- **A name with no default version is refused** rather than linked, as
  `LINK_ERROR_SYMBOL_VERSION`. glibc publishes `sys_errlist` four times, once
  per historical layout, and every one of them is a non-default `name@VER`; an
  unversioned reference has nothing to bind to, GNU ld reports it undefined,
  and Buster linked it and let the loader pick. **Do not use `sys_errlist` as a
  Clang-differential fixture** — a harness that reads "Clang refuses, Buster
  accepts" as a Buster success measures nothing (issue #660).

## External ELF debug information

The ELF object reader carries the DWARF 5 `.debug_addr`, `.debug_str_offsets`,
`.debug_line_str`, `.debug_rnglists`, and `.debug_loclists` sections alongside
its existing DWARF 4 sections. Unit headers and payloads remain opaque; the
compiler's own DWARF writer still emits version 4. `link_objects` concatenates
contributions and rebases their symbols and relocations. The ELF image writer
resolves references into debug sections as section offsets at either 32- or
64-bit width, and address-table entries as link-time addresses. Empty new
sections add no executable section headers.

Compressed debug sections require decompression and are explicitly refused
with the section name. A relocation into an unsupported section likewise
names that section instead of leaving the driver with a numeric error alone.
Split DWARF and accelerator-section support are outside this section family.
The registered object tests exercise both ELF architectures; Linux driver
regressions build and link two optimized external CUs with the configured
compiler in DWARF 4, DWARF 5, and DWARF64 modes, inspect relocated offsets,
and run the result. They also check the compressed-section driver diagnostic.

## Native invariant verification

`-fverify-codegen` validates canonical IR even when the frontend certified it,
then checks selected and changed scheduled MIR and placement validity. Invalid
verified states fail compilation before fallback can hide them. It applies to
native x86-64/AArch64 code generation, including the `none` canonical path;
preprocessing, syntax-only and direct non-native output reject the flag.
Successful compilation prints a versioned `CODEGEN_VERIFY` line with module,
selected-function and scheduled-function counts and the effective allocator.
Normal compilation keeps its existing validation certificates and fast paths.
The [native differential runner](../differential-testing.md) consumes this
explicit opt-in evidence and compares executable observations independently.
When selected or scheduled MIR fails verification, the refusal names the
`MachineVerifyError` and its block, machine instruction, and operand. Without a
failing canonical instruction its opcode is `unknown`, not an IR enum default.

With `-v`, aggregate `CODEGEN` and fallback reason/opcode/stage counters are also
printed after codegen errors, including strict fallback rejection. The optional
`-fcodegen-fallback-census` retains and reports every observed fallback's function
ID/name, source coordinates, reason/stage and opcode; normal compilation allocates
no record array. The first strict diagnostic remains unchanged. The
[retirement object census](../native-retirement-census.md) validates and retains
both aggregate and function records, including records before a fatal stop.

## Positional source-language selection

`-x` is positional. The command-line parser snapshots the active language
beside each following input, and `-x none` restores automatic extension
classification only for later inputs. A later or trailing `-x` never
reclassifies an earlier path.

`CompilerDriverInvocation.input_languages` is authoritative when non-null
and must contain exactly `input_count` entries. Embedding callers that
leave the pointer null and `input_language_count` zero retain the legacy
invocation-wide `language` behavior. Any code that slices `input_paths`
for a single translation unit must slice the language array in lockstep.
The GPU handoff follows the same null-means-global compatibility rule.

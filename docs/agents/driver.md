# Compiler driver and target options

[Agent instructions](../../AGENTS.md) · Paths and commands below are relative to the repository root.

## Compiler output streams

`ide cc` writes warnings, source diagnostics, and `cc: error:` driver errors to
stderr. With `-E` or `-S` and no `-o`, or with `-o -`, generated text
goes to stdout;
warnings cannot enter the preprocessed or assembly stream. `#warning` and
`#error` messages retain the spelling between the first and last message
tokens, including punctuation and internal whitespace, without expanding macros.

The invocation API retains that text in `CompilerDriverResult.output`.
`-o -` is refused for binary output and for multiple inputs. An explicit
`-o` with multiple `-E` inputs is refused, as for `-c` and `-S`. Default
LLVM bitcode output uses the source basename in the current directory, matching
object output.

C, assembly and direct canonical emitters retain atomic publication for regular
files. Existing POSIX character devices and FIFOs such as `/dev/null` are
written directly. Symbolic links/reparse points, directories and other special
destinations are explicitly refused. Write failures name the path and the OS
error when supplied; native link failures identify the executable or PDB that
failed. External GPU tools retain their own file-publication behavior.

Opt-in machine-readable records remain on stdout: `CODEGEN_VERIFY`,
`CODEGEN_FALLBACK*`, `CODEGEN`, `IR_*`, `TARGET`, `GPU`, and the `-v` source
statistics. The differential runner reads `CODEGEN_VERIFY` there and compares
captured stderr diagnostics separately; the retirement census reads its
`-v`/fallback records from stdout. `ide metamorphic` uses stderr first when
reporting a failed compiler invocation. A `-v -E` invocation also prints the
requested statistics on stdout; use plain `-E` when piping preprocessed C.

With several inputs, only a native link copies each unit's in-memory object
out of its translation-unit arena into the result arena for `link_objects`.
Each `-c` unit has already written its own `.o`, and `-S`, `-E`,
`-fsyntax-only` and `-emit-llvm` finish before the link, so they retain
nothing per unit and leave `CompilerDriverResult.object` unset.

The registered `compiler_diagnostic_tests` include a desktop flush-failure
cleanup regression. It injects one refusal after the real staging write, checks
source mapping and native handle balance, old-or-absent destination bytes and
a private directory inventory, then reuses the same invocation successfully
against an independent literal preprocessing result. Close and staging deletion
remain native operations. Android and iOS skip this desktop observation path
explicitly; the existing portable write-failure tests still run. This is
controlled failure-path coverage, not a real disk-failure or crash-durability
claim. Run `ide test --module=compiler_diagnostic_tests --verbose=1 --ci=1`.

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
and all cohort arenas are released (with `-fkeep-going`, later inputs still
compile, see [per-input records](#per-input-records-and-continue-on-failure)).
The one-worker and `BUSTER_SINGLE_THREADED` builds run the same unit kernel.

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

## Per-input records and continue-on-failure

A batched invocation (`ide cc -c a.c b.c ...`, one translation unit and one
object per input) can publish everything a one-input-per-process run would
have told a harness. This section is the contract and the schema; `driver.h`
only points here.

- `-fmetrics-out=FILE` (API: `collect_input_metrics`) fills
  `CompilerDriverResult.inputs`, one `CompilerDriverInputResult` per input in
  input order, and `ide cc` writes the records to FILE after everything else it
  prints. An unwritable FILE fails the process like an unwritable `-o`.
- `-fmetrics-functions` (`collect_function_sizes`) adds each compiled
  function's name and code bytes. Without `-fmetrics-out` it is an argument
  error (the API refuses `collect_function_sizes` without
  `collect_input_metrics`).
- `-fkeep-going` (`keep_going`; `-fno-keep-going` restores the default)
  keeps compiling after a failed input: later inputs still compile and `-c`
  still writes their objects; the invocation takes the first failure's error,
  prints one `cc: error:` line per failed input, exits nonzero and never links
  (serial and lane paths alike). `-E` and `-S` over several inputs produce one
  concatenated stream, so after any failure nothing is printed or written for
  any input; the records still report each one. A TU arena that cannot be
  allocated is that input's `failed` record and the batch continues. An
  unreadable or malformed prebuilt object or archive is also a `failed`
  record, but it still stops the invocation, because the link it belongs to
  cannot proceed. `-fkeep-going` alone allocates records with statuses only.
- Metrics refuse the API's `suppress_diagnostic_records`, since the digest
  below covers every structured record. GPU pipelines reject all three options.

Without these options nothing changes: no clock is read, no record or prime is
allocated, objects are byte-identical, and the first failing input still stops
the batch.

**Setup stays outside every input.** With metrics requested, before the first
interval opens the driver runs `compiler_prewarm()` and
`codegen_prewarm_for_target(target)` (the one-time frontend, ABI and x86
metadata tables, about 20 ms that used to land in input 0's `codegen` phase),
and primes first-touch page faults: it commits and touches a pooled TU arena
(`COMPILER_DRIVER_METRICS_TU_PRIME_BYTES`, which the first unit's arena
reuses) and the calling thread's scratch arenas once per thread, plus the
result arena when a single input compiles in it. No warm-up input is needed.
`compiler_driver_test_input_metrics` observes the completed setup calls and
the real serial input boundaries through a private, test-only calling-thread
observer. It detects incomplete setup at an input start, setup during or after
an input, and unbalanced intervals; malformed event streams check those
negative cases. Correctness does not compare real-clock durations of tiny twin
compilations: scheduling, source mapping and object publication can change
their ratio independently of setup order. Phase timings remain diagnostic.
Lane workers' own arenas are not primed or observed by this serial test seam.

**Intervals.** Every per-input offset and the header's `wall_ns` count
monotonic nanoseconds from one origin: `ide cc` takes it right after argument
parsing (`metrics_origin`; an API caller may supply it, otherwise the driver
takes it on entry). `start_ns`/`end_ns` bracket that input's
`compiler_driver_execute_c_single` call: reading, preprocessing, parsing,
semantics, canonical IR, code generation, object construction and writing its
object file. `total_ns` is `end_ns - start_ns`. The eight phases are the
time between consecutive boundaries, each an offset from the same origin, so
they partition `total_ns` exactly on every clock. Driver setup, TU arena
creation, result merging, arena release, metrics serialization and teardown
fall between or outside the intervals, never inside one. With one worker (every
`-c`, `-S`, `-E` and `-fsyntax-only` batch, and a link without
`-fcompile-jobs`) the header says `intervals=serial`: intervals are ordered,
non-overlapping and inside `[0, wall_ns]`. A `-fcompile-jobs` link with more
than one active worker says `intervals=concurrent`: records stay in input
order, but intervals may overlap.

**Phases** (`CompilerDriverPhase`): `read` (map the input), `preprocess`
(including publishing its diagnostics), `parse`, `analysis` (semantic analysis
and canonical-IR lowering are one call), `ir` (`ir_prepare_canonical_module`:
validation, local promotion and FAST), `codegen`, `object` (object-model
construction), and `emit` (serialize and publish for `-c`, print for `-S`, the
single-input link, or a `-E`/LLVM/Wasm/eBPF/SPIR-V artifact). A failing phase keeps
the time up to the failure. Assembly units credit everything before `emit` to
`read`. Phase timings are diagnostic.

**Status.** `ok`; `rejected` for source refused with diagnostics
(`driver.tokenize`, `driver.parse`, `driver.analysis`, and
`driver.invalid-input` from the assembler); `failed` for every other stage
(file I/O, IR validation, code generation, object construction, writing, TU
arena allocation, an unreadable prebuilt input); `not_run` for inputs after a
batch-stopping failure or when the invocation failed before compiling;
`prebuilt` for object and archive link inputs that were read.

**Diagnostics.** Error and warning counts come from the input's own structured
records. `message` is the driver's rendered first error; `diagnostic_code`,
`diagnostic_path` and `diagnostic_line`/`diagnostic_column` are the first error
record's (the code falls back to the driver stage). `diagnostic_records` counts
every record of the input and `diagnostic_digest` is the SHA-256
(`sha256_*` in `<buster/lib/hash.h>`) over them in emission order. Each record
contributes its severity, code, primary path, line, column and full message;
integers are 8-byte little-endian and every string is its 8-byte little-endian
length followed by its bytes. An input without records has the digest of
the empty string.

**Memory.** `arena_peak_bytes` is the memory the input needs: for the TU arena
and each of the running thread's scratch arenas, the peak cursor above that
arena's position when the unit started (the scoped `Arena.high_water` mark,
into which rewinds fold the discarded cursor), rounded up to that arena's commit
granularity (`Arena.granularity`, 64 KiB by default), summed. It counts
committed-granule demand, never reserved address space or allocation requests,
and is independent of pooling and input order up to alignment padding at the
granule boundary. When the unit runs in the result arena (a single input), that
arena is its TU arena. `arena_retained_bytes` is the TU arena's bytes still
allocated when the unit returned. Shared tables and result-arena copies are not
attributed to an input. Peaks are not RSS and must not be summed into a process
peak.

**Output.** `object_file_bytes` is the serialized object `-c` published (zero
otherwise). Section bytes group the object model's sections by a static
`ObjectSectionKind` table before format serialization: `text`, `rodata`, `data`,
`bss` and `tbss` (virtual sizes), `tdata`, `initializer`
(`.init_array`/`.fini_array`), `unwind` (`.eh_frame`, `.pdata`, `.xdata`) and
`debug` (DWARF/CodeView). Function sizes are the defined function symbols'
sizes, the values the object writer records, capped at
`COMPILER_DRIVER_INPUT_FUNCTION_LIMIT` (65,536) per input with the remainder
counted as `function_records_omitted`. Counters are the unit's
`CodegenStatistics`, the fallback-census record count, lexed source bytes and
preprocessed tokens.

**Record text.** The file follows the tagged `NAME version=N key=value`
convention of `CODEGEN_FALLBACK_FUNCTION`: one record per line, space-separated
fields in a fixed order, decimal numbers, strings as lowercase hex (`-` when
empty). Messages and function names are cut at
`COMPILER_DRIVER_METRICS_TEXT_LIMIT` (1,024) bytes, with their full length
(`message_bytes`, `name_bytes`) and a `message_truncated`/`name_truncated` flag;
paths and diagnostic codes are written whole. Version 1:

```text
CC_METRICS version schema=buster-cc-metrics inputs records ok rejected failed not_run prebuilt error exit_status action target allocator compile_jobs compilation_workers intervals keep_going function_sizes wall_ns peak_rss_bytes
CC_METRICS_INPUT version index status error errors warnings measured start_ns end_ns total_ns read_ns preprocess_ns parse_ns analysis_ns ir_ns codegen_ns object_ns emit_ns arena_peak_bytes arena_retained_bytes source_bytes preprocessed_tokens object_file_bytes text_bytes rodata_bytes data_bytes bss_bytes tdata_bytes tbss_bytes initializer_bytes unwind_bytes debug_bytes codegen_functions instructions values code_bytes stack_frame_bytes max_stack_frame_bytes fallback_functions fallback_records function_records function_records_omitted diagnostic_records diagnostic_digest diagnostic_line diagnostic_column path_hex diagnostic_code_hex diagnostic_path_hex message_bytes message_truncated message_hex
CC_METRICS_FUNCTION version input ordinal code_bytes name_bytes name_truncated name_hex
```

The header comes first, then each input followed by its functions. Between
identical runs only the `*_ns` fields, `peak_rss_bytes` and possibly
`arena_peak_bytes` (granule-boundary padding) vary, plus
`compile_jobs`/`compilation_workers`/`intervals` across worker counts.
`peak_rss_bytes` is the process high water (`os_get_peak_resident_memory_size`:
`ru_maxrss` on Linux and Apple, the peak working set on Windows, 0 when
unavailable). Process start-up before argument parsing and teardown after the
file is written are outside `wall_ns`, so a harness keeps its own exec-to-exit
clock. Readers take the fields they know; later versions only append fields.
`compiler_driver_test_input_metrics` and
`compiler_driver_test_input_metrics_lanes` cover the contract.

The Clang-like `ide cc` driver accepts `-march=<model>` and
`-mcpu=<model>` (or their separated forms), ordered target-feature overrides
through `-mattr=+feature,-feature`, and x86 assembly dialect selection through
`-masm=att|intel`. CPU and feature options also accept separated values. CPU names use the canonical
spellings printed by `cpu_model_to_string_os`, such as `baseline`, `native`,
`haswell`, `znver5`, and `apple-m4`; incompatible target/model pairs are
diagnosed. x86-64 CPU selection requires AMD64 long mode: the historical
`i486`, `pentium`, `k6`, `k6-2`, `k6-3`, `geode`, `athlon` and `athlon-xp`
spellings are recognized but refused for x86-64, including through `-mcpu`.
K8, Core 2 and newer represented x86-64 models remain available.
Host detection falls back to the dynamic `native` identity if a virtualized
family/model description names a processor incompatible with the executing
architecture; independently probed host features are preserved. Explicit
`-march`/`-mcpu` requests still receive the incompatibility diagnostic.
`-v` reports the selected CPU, the sorted effective feature set,
and maximum native vector width. `-target`/`--target` strings are
`arch[-vendor][-os][-environment]`: the vendor and environment components stay
free-form, but a CPU model there is rejected in favor of `-march=`, and so is
anything past the fourth component. Both used to be dropped silently, which
left baseline code generation and no hint that the request was ignored.
Native x86-64 and AArch64 compilation uses the FAST register allocator at
every optimization level, including the default and `-O0`, while
`-fno-register-allocator` and `-fregister-allocator=none` retain their accepted
spelling but select MIR_STACK placement. Advanced and diagnostic callers may
select `none`, `mir-stack`, `fast`, or `quality`; every spelling runs canonical
IR -> MIR -> placement -> metadata-backed native emission, and the last
allocator-affecting option wins.
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
Shapes the selected MIR vocabulary cannot build fail code generation with the
function, source, target, allocator, opcode and stable stage reason attached.
The driver does not write an object and does not replace an existing output.
Signature rejection maps to `codegen.unsupported-abi`; opcode and encoding
rejection map to `codegen.unsupported-instruction`; verification maps to
`codegen.invalid-ir`; placement and output capacity map to `codegen.capacity`.

The opcode-reason negative control uses seventeen inline-assembly operands,
above the current MIR limit of sixteen. Wide Win64 vectors, scalar fixed-register
assembly, and argument count retain strict-success coverage; do not constrain
supported inputs to keep a negative test failing.

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

`-fno-machine-fallback` and `-fmachine-fallback` remain accepted so existing
build scripts do not break; neither changes native behavior or can re-enable
the retired direct emitter. Native C generation is always strict, including
the retained `none` spelling. Direct non-native output, preprocessing and
syntax-only checks still reject the native-only strict option. Assembly inputs
and linked prebuilt objects have no canonical C functions to gate.
For example, `build/Release/ide cc -fregister-allocator=mir-stack -fno-machine-fallback -target aarch64-unknown-linux -c tests/basic_c_call_abi.c -o build/mir-coverage.o`.
`compiler_driver_test_machine_fallback` runs the same fourteen-fixture arithmetic,
control-flow, call-ABI, aggregate and frame corpus for x86-64 and AArch64 on
Linux, macOS and Windows, under all three machine allocators and both explicit
frontend forms in `test_all`, including CI. Its 504 object-compilation rows
require 504 non-empty strict successes, including the two variadic fixtures on
Windows/Darwin AArch64. Any future explicit refusal must diagnose its function,
reason and opcode, preserve an existing output and publish no object. It is not
a skip; implementing a gap must replace its refusal expectation with strict
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
modes and both frontend forms; MIR legs reject failures, and only the matching
native desktop executes the result. This adds 24 object-compilation cases
outside the fourteen-fixture floor above. Independent host/compiler observers
remain the semantic reference; the in-tree direct-native path is not a
production oracle.

Quoted symbol spellings in x86 Intel and AT&T instruction operands retain
the same symbol identity as labels and data directives. RIP-relative and
absolute memory operands and direct branch targets accept the quoted spelling
and a numeric addend. A direct branch's `@PLT` modifier remains outside its
quoted name; `@PLT` and numeric-label-looking bytes inside the quote stay name
bytes. Intel also accepts a displacement before the brackets
(`"g"+8[rip]`). Delimiters and comment punctuation within a quoted name remain
name bytes. Malformed or empty quoted symbols fail with an operand diagnostic.
The registered `-g0`/`-g` listing round trip covers a string reference and an
external call under all four allocators; matching Linux x86-64 hosts execute
the linked result.

## Plain-char signedness

`-fsigned-char` and `-funsigned-char` override the target's implementation-
defined plain-`char` signedness; the last option wins. With neither option,
the target ABI default remains in effect. This policy is carried through
`TargetDataLayout`, so the C frontend uses it consistently for plain-`char`
typing and promotions, casts, character constants, `__CHAR_UNSIGNED__`, and
the `CHAR_MIN`/`CHAR_MAX` definitions in `<limits.h>`. Explicit `signed char`
and `unsigned char` keep their specified behavior. The options apply to C
frontend paths for native objects, LLVM bitcode, Wasm64, and eBPF. External GPU
pipelines reject them because their toolchains do not use Buster's C frontend.

## C input phase selection

`--target=spirv-vulkan1.2-compute -c` selects the direct Vulkan 1.2 / SPIR-V 1.5
compute emitter. It accepts one bounded C kernel and no native link inputs,
external GPU tool flags, LLVM emission, explicit native allocator, debug/PIC,
or native verification options. The [compute contract](../spirv-compute.md)
defines the interface, unsigned integer subset, automatic bounds guard and
pending physical-device evidence. Existing external `spirv` routes are separate.
Without `-o`, the direct target publishes `<input-path>.spv`.

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
`.data`, `.bss`, `.rodata` and `.section`; `.globl`/`.global`/`.extern`, `.weak`,
`.hidden`, `.type` and `.size`; `.align`, `.balign` and `.p2align`; `.byte`,
`.short`/`.word`/`.hword`/`.value`, `.long`/`.int`, `.quad`, `.ascii`,
`.asciz`/`.string`, and `.zero`/`.skip`/`.space`; `.intel_syntax noprefix` and
`.att_syntax prefix`; and the `.cfi_*` family, accepted and dropped because it
describes unwinding rather than bytes. Anything else -- a directive the table
does not claim, or an operand form one of these does not cover -- is a
diagnostic naming the directive and its line, the way every other unsupported
construct here is reported rather than silently dropped.

Bare `.section NAME` accepts `.text`, `.data`, `.rodata`, `.bss`
and their dot-delimited suffixes, exact `.init`/`.fini`, and the existing
DWARF names (`.debug_info`, `.debug_abbrev`, `.debug_line`, `.debug_str`,
`.debug_loc`, `.debug_ranges`, `.debug_addr`, `.debug_str_offsets`,
`.debug_line_str`, `.debug_rnglists`, `.debug_loclists`). DWARF sections
retain nonallocated object identities through the driver. Raw-prefix
lookalikes such as `.initdata` cannot acquire executable flags. Other bare
names are diagnosed while the wider explicit flag/type and generic
nonallocated section contract remains tracked in
[#1279](https://github.com/buster14a/buster/issues/1279). These known bare
DWARF names cover the section directives emitted by the current `-S` printer;
quoted instruction operands and the broader round trip are tracked in
[#2519](https://github.com/buster14a/buster/issues/2519).

Statement boundaries follow the target: x86-64 and non-Apple AArch64 use
`;` between statements; Apple AArch64 uses `%%` and treats `;` as a line
comment. `#` starts an x86-64 comment and remains part of AArch64 immediates.
`//` comments are accepted on both architectures. Quoted strings retain these
markers and block-comment text, including escaped quotes. Diagnostics keep
physical lines/columns after a separator; numeric labels resolve by statement
order even when their definitions share one physical line. Scalar AArch64
constant operands accept an optional `#` through the existing constant parser.
`mov wN, constant` and `mov xN, constant` accept an unsigned sixteen-bit
constant through the scalar `movz` form; register aliases keep their existing
operand rules.
Pair-exclusive `ldxp`/`ldaxp` and `stxp`/`stlxp` spellings project matching W/X
data registers, W store status and an X/SP base into the existing typed AArch64
memory semantic encoder. Data/status ZR roles are retained; store status cannot
overlap either data register or a non-SP base. The optional address offset must
be zero. Nonzero/symbolic offsets, mismatched widths, and writeback are source
operand diagnostics. No pair instruction words or generated identities are
duplicated in the source adapter.
Unsupported post-index memory operands are refused with their full spelling,
so their writeback cannot silently disappear during comment handling.

Integer data expressions retain `.` as the current field's section-relative
address, including each separate operand in a comma-separated directive.
`.long symbol - .` and `.quad symbol - .` use ELF PC32/PC64 on x86-64 and
PREL32/PREL64 on AArch64; `.quad .` and `label + constant` retain absolute
address relocations. Quoted names printed by `-S` are accepted. Differences
between defined, non-weak terms in the same section fold after forward labels
are known. Cross-section symbol differences, negative undefined addresses,
multiple positive symbolic terms, and symbolic fields narrower than four
bytes are diagnosed with the directive and source line. Weak definitions
retain relocations because a linker can replace their addresses.

Text alignment without an explicit fill uses x86-64 NOP bytes or complete
little-endian AArch64 NOP instructions. A partial AArch64 instruction boundary
is zero-filled before the NOPs; explicit fills remain repeated bytes on both
targets. Data alignment defaults to zero fill.

Three things that layer owns rather than the instruction layer. Local numeric
labels: `1:` becomes a generated name and `1f`/`1b` resolve to the nearest
following or preceding definition in source order, and those names leave the
symbol table again once every reference to one is folded, the way GNU as drops
its own `.L` locals. A repeat or lock prefix alone on a line joins the
instruction on the next one. A same-section PC-relative reference is written
into the bytes only when its symbol's identity cannot change at link time.
Weak symbols, including hidden weak definitions, retain references for strong
replacement. Default-visible ELF globals also retain references for shared
library interposition; hidden strong and local labels still fold. Cross-section
and undefined references remain relocations. Direct x86 ELF calls and jumps to
default-visible globals use PLT32, and an explicit `@PLT` request is preserved
for a retained direct call/jump. Other `@PLT` operand forms are diagnosed.
Retained displacement families the object model cannot express (such as an
8-bit `loop` to a weak symbol) fail before publishing an object. ELF visibility
rules do not change COFF or Mach-O global fixups. Sections keep their own
names -- `.init` and `.fini` are neither `.text` nor absent -- and a
hand-written section gets alignment 1, because `crti.o` and `crtn.o`
contribute one and two bytes to `.init` and any padding between them would
run as code.

AArch64 units fold same-section, binding-invariant `b`, `bl`, `b.cond`,
`cbz`/`cbnz`, and `tbz`/`tbnz` references using the shared control semantic
fixup, including signed addends and numeric labels. Out-of-range or unaligned
references are diagnosed at their physical source position. Undefined,
cross-section, weak, and default-visible ELF global short branches are refused
because the object model cannot retain their relocation families; `b`/`bl`
retain the existing object relocations. This unit-local capability does not
enable machine inline-asm private-label expansion. The registered driver
fixture assembles pristine `tests/aarch64_atomic_update_pair_oracle.s` through
`.s` inference and `-x assembler`, checks all 108 text bytes against independent
literal words, and compares the same words with Clang cross-assembly when a
configured or PATH Clang is available. An unavailable Clang observer is reported
explicitly; its comparison is not a passed gate.

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

Direct WebAssembly output accepts one C source for `wasm64-unknown-freestanding`
or `wasm32-wasip1` (also spelled `wasm32-wasi`). The latter emits a WASI Preview 1
command module, with an exported `_start` and 32-bit pointers. Its `--sysroot`
header paths and supported imports are in [WASI.md](../../WASI.md). Direct wasm32
output rejects `-emit-llvm`, native link inputs, and `-S`.

Wasm32 also refuses runtime function addresses, including stored/returned
references and aliases; direct calls remain supported. The existing instruction
emitter checks pointer-valued FUNCTION materialization and function-typed
operand escapes outside a direct CALL callee. This prevents a zero function
index from masquerading as null without adding a use-graph pass or allocation.
Refusal produces a Wasm driver error with empty artifact aliases before output
publication; existing destination bytes remain intact. Memory64 behavior and
its nonzero function handles are unchanged.

The direct backend consumes canonical integer bit-count operations at their
semantic bit width, independently of the i32/i64 WebAssembly carrier. Leading
and trailing zeros count within that width; a zero operand produces the width,
and population count ignores carrier extension bits. This is the canonical IR
contract rather than a promise about C builtins on undefined zero inputs.

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

Archive input uses `object_archive_read_link`, a borrowed descriptor reader,
while `object_archive_read` remains the eager public API. The driver retains
archive mappings through extraction and releases them on every invocation exit;
fully admitted objects own their payload and names in the result arena. Descriptor
capacity follows the actual member-header count rather than archive payload bytes.

GNU/COFF first linker-member and GNU64 indexes, plus BSD/Darwin32/64 ranlib
indexes, provide definition metadata without reading object payloads. BSD
extended metadata names are classified after decoding; Mach-O index names lose
exactly the same leading underscore as the full reader. Unindexed ELF, COFF and
Mach-O members read only symbol/name metadata. An unrelated foreign-target or
unsupported-relocation member therefore cannot reject a link. A selected member
runs the ordinary complete object reader before its object or undefined references
enter extraction state; refusals name its archive member and actual/requested
targets. The ordered provider worklist keeps the same member-order, duplicate,
weak-reference and repeated-archive rules. Provider heads are cleared by their
original indexed names before scratch release, even if admission replaces a
descriptor's symbol table.

Selection metadata does not extend the object reader's section or symbol
vocabulary. An index can request a definition in a section the full reader
cannot retain; selecting that member still reaches the existing admission or
unresolved-symbol diagnostic. That unsupported-definition limitation remains
at the full-reader boundary rather than silently publishing a descriptor as a
linked object.

`compiler_driver_archive_test_lazy` exercises all three object formats, 32/64-bit
GNU and BSD indexes, BSD extended names, unindexed input, transitive dependencies,
no-selected-member archives, a required incompatible member, duplicate providers,
weak references and repeated occurrences. A separate valid ELF `R_X86_64_SIZE64`
control verifies that an irrelevant same-target unsupported relocation is deferred
and its selected member still fails.

An undefined weak ELF reference does not select a static archive member.
It may bind to a member selected for a separate strong dependency, to a
direct object input, or to an already included shared library. Keep archive
selection separate from those later resolution rules (GitHub #226).

Linux `-lNAME` static archives are searched in explicit `-L` directories first,
then the same target roots used by ELF export discovery: `lib/<triple>`,
`usr/lib/<triple>`, `lib64`, `usr/lib64`, `lib`, and `usr/lib` under a supplied
sysroot. Without a sysroot, the absolute host roots also include
`/usr/<triple>/lib` after the two multiarch roots. The sysroot replaces these
default host paths; explicit `-L` directories retain their literal meaning.
Each directory prefers a target-compatible `libNAME.so` to `libNAME.a`, so an earlier explicit
archive wins over a later default shared library. `-l:FILE.a` searches the
exact archive name without that shared-library probe and retains its existing
bare-path fallback. A little-endian ELF64 shared candidate naming a different
CPU is skipped before archive selection, allowing an archive in the same or
a later directory to satisfy the request. Unrecognized or malformed shared
files retain the existing export-discovery refusal. Other target search
policies are unchanged.

`compiler_driver_archive_test_default_roots`, invoked by the registered lazy
archive fixture, checks both ELF CPUs and all six literal sysroot roots,
named/exact/direct image parity, distinct provider precedence, explicit `-L`,
shared preference, incompatible shared headers beside and before usable
archives, exact archive bypass and output preservation on refusal.
Its configured native Linux control builds a real archive with host compiler
and archiver, links an independent host control, and runs both Buster's direct
and sysroot-default named links.

For requested Linux shared libraries, a located GNU linker script beginning
with `INPUT`, `GROUP`, `AS_NEEDED`, `OUTPUT_FORMAT`, `OUTPUT_ARCH`, or
`SEARCH_DIR` and an opening parenthesis is refused as
`unsupported GNU linker script <path> requested by -l<request>`. Leading
whitespace and C block comments are accepted for this classification. The
first script in search order stops lookup; a later ELF library cannot hide it.
The driver neither interprets script members nor creates an image from them.
Ordinary missing/malformed-library policy and automatic runtime-provider
lookup retain their existing behavior. `compiler_driver_test_elf_linker_scripts`
covers literal script/non-script boundaries, both ELF CPUs, normal/exact
library spellings, single/multi-input links, first-request error ordering,
later-ELF/script precedence, and preservation of an existing output.
General GNU linker-script interpretation and Apple's missing-library behavior
remain separate #1285 work.

ELF executable data placement honors both page and requested object alignment.
Align the final virtual address, not only its file offset: an initialized
global may require alignment larger than a page or the fixed image base.
The object writer already carries that requirement into the section metadata
(GitHub #225).

Every hosted ELF link reads the shared libraries' own dynamic symbol tables.
`compiler_driver_elf_library_exports` looks `libc.so.6` and each requested
library up where the loader would — the `-L` paths, then the sysroot or host
`lib`/`usr/lib` roots, multiarch first; without a sysroot also the Debian
cross-libc root `/usr/<triple>/lib` — and rejects a file whose ELF machine
disagrees with the target, so a cross link never reads the host's own libc.
A cross link that finds no target libc cannot tell a missing symbol from a
libc import and keeps every strong undefined reference as an import (GitHub
#1729).

Native Linux links also supply the compiler-runtime calls used for binary16
conversion and binary128 arithmetic/conversion (GitHub #1272). After merging
objects and reading explicitly requested shared libraries, a remaining strong
undefined function helper selects `libgcc_s.so.1` from the same `-L`, target
and sysroot roots. Its ELF machine must match the target, and it must export a
default-version callable definition of every required helper. An earlier
explicit data/TLS/unknown definition cannot preempt that call as code.
Hidden/internal definitions and non-default versions provide no helper;
truncated version-symbol metadata is refused. Missing runtime files or helpers
fail the link before replacing the output, including cross links with no
readable libc. Existing object/archive definitions and explicit shared-library
providers take precedence; weak optional references add no runtime dependency.
Data references with helper-like names also add none; untyped external-object
references retain the separate #1242 boundary.
An explicit `-l:libgcc_s.so.1` is reused without a duplicate `DT_NEEDED` entry.
Normal library symbol-version binding records the GCC version the selected
runtime publishes. Ordinary links with no such unresolved helper do not read
or name libgcc_s. This uses an installed target runtime; it installs or embeds
none. Apple, Windows, Android and freestanding runtime provisioning remain
separate #1272 work.

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

A hosted executable also exports each of its own definitions that a requested
library leaves undefined, as GNU ld does: `compiler_driver_elf_dynamic_symbols`
records every library's undefined names as `referenced_symbols`, and the
index marks them, so a library that calls back into the program or reads its
data binds without `-rdynamic`. Only the requested libraries are consulted, not
`libc.so.6`, whose undefined names are loader internals.

## Shared objects and position-independent executables

On x86-64 Linux, `-shared` links a shared object and `-pie` a
position-independent executable (`NativeImageKind`, carried to the linker in
`NativeExecutableLinkOptions.image_kind`). `-shared` outranks `-pie` in either
order and `-no-pie` undoes only `-pie`. Linking either kind compiles the C
inputs of that invocation with the position-independent code model, and
`-fPIE`/`-fpie` select that same model on every target (the last of the four
positive spellings wins; `-fno-pie` cancels only a PIE spelling). On any other
target a link that asks for either image is refused as an unsupported option,
while a compile-only invocation ignores the link option, as GCC does.

`link_native_image_elf64_x86_64_position_independent` writes both kinds as an
ET_DYN at base zero. Its orientation comment is the contract; in short:

- Every absolute address becomes a dynamic relocation: `R_X86_64_RELATIVE` for
  a definition bound in the image, `R_X86_64_64`/`GLOB_DAT` for an import and,
  in a shared object, for an exported definition, because an executable may
  copy-relocate the library's data and the library must then follow the copy.
  Direct calls bind to the library's own definitions (ld's
  `-Bsymbolic-functions` answer). A rel32 to preemptible data, 32-bit absolute
  addresses, and address relocations in code are refused with a hint to
  compile with `-fPIC`. A rel32 to imported data is refused in a shared object;
  a PIE instead reserves a copy slot after `.bss` and emits `R_X86_64_COPY`,
  as ld and lld do for GCC's `-fPIE` code, and every other reference to that
  symbol binds to the slot. The slot planning (`link_elf_copy_plan_build`,
  including the library's alias names such as `environ`/`__environ`) is shared
  with the fixed-address writer.
- A shared object exports every defined default-visibility symbol, leaves
  undefined ones for the loader (`-Wl,--no-undefined`/`-z,defs` restore the
  executable's rule), keeps `.init_array`/`.fini_array` for the loader, takes
  `DT_SONAME` from `-Wl,-soname,NAME`, and records symbol versions like the
  fixed-address writer. `.rodata`, the initializer arrays, `.dynamic` and
  `.got` sit under `PT_GNU_RELRO`; `PT_GNU_STACK` is not executable.
- Thread-local storage in a PIE is relaxed to local-exec as in a fixed-address
  executable. Initial-exec accepts both Buster's `add reg,[rip+x@GOTTPOFF]`
  and GCC/Clang's `mov reg,[rip+x@GOTTPOFF]`: each becomes the same-length
  ADD or sign-extending MOV immediate, with REX.R moved to REX.B. Metadata
  validates the instruction envelope before writing; arbitrary field bytes
  and ignored input REX.X/B bits never change the destination register.
  In a shared object general-dynamic keeps its `__tls_get_addr`
  call with a `DTPMOD64`/`DTPOFF64` pair, initial-exec gets `TPOFF64` and
  `DF_STATIC_TLS`, and local-exec is refused.
- Local-dynamic TLS, which this compiler never emits but GCC and Clang do for a
  file-local `__thread` under `-fPIC -O1` and above (`R_X86_64_TLSLD` then
  `DTPOFF32` per variable, issue 1711), is read from foreign objects. An
  executable -- fixed-address or PIE -- relaxes the `lea`/`call
  __tls_get_addr` pair (direct, or through the GOT under `-fno-plt`) to
  `mov rax, fs:0` behind data16 padding and resolves `DTPOFF32` in code to the
  thread-pointer offset, as ld does. A shared object keeps the call and gives
  the image one `DTPMOD64` pair with a zero offset, and `DTPOFF32` is the
  variable's offset in the module's block. `DTPOFF32`/`DTPOFF64` in DWARF
  sections resolve to that block offset in every image.

`compiler_driver_test_position_independent_images` exercises the whole path:
a Buster library loaded by `dlopen` and linked by Buster (fixed-address and
PIE) and by the host toolchain (PIE and `-no-pie`, whose copy relocations the
library must follow), calls and data in both directions, the lifecycle order
of initializers and handlers, a randomized PIE base, copy relocations in a
Buster PIE for an object that reads library data and `environ` with rel32s
(the shape GCC's `-fPIE` emits), a CPython extension when `python3` and its
headers exist, and the `-fPIC` refusal.
`compiler_driver_test_local_dynamic_tls` links GCC and Clang `-O2 -fPIC`
local-dynamic objects (plain, `-fno-plt`, and `-g`) into each image kind and
runs them, the shared object under both a Buster PIE and the host toolchain.
AArch64 ELF, PE DLLs and Mach-O dylibs have no writer yet.

`compiler_driver_test_initial_exec_tls` serializes independent MOV-form ELF
fixtures using rax and r8, then links and executes them as fixed-address and
PIE images. Available GCC/Clang compilers add default `-O2`, `-O0 -fno-pie`
and `-O2 -fPIC -ftls-model=initial-exec` objects, with host-linked controls and
initialized/zero TLS reads before and after mutation. Malformed MOV sites
fail without replacing output. The driver emits its `-fPIC` hint only when
the ELF planner identifies a refused fixed-address relocation; generic
relocation failures, including malformed TLS sites, do not imply that cause.

## Pass-through options

`-Wl,a,b,c` produces three individual linker arguments, in order. Each
`-Xlinker value` contributes exactly one argument; commas in that value are
not split. Empty comma fields and missing operands fail with a diagnostic.
The driver and native linker share `link_validate_linker_arguments`, and the
linker checks the actual static/dynamic image before publishing output.

The supported subset is:

- Linux and Android dynamic ELF executables: `-E`, `-export-dynamic`, and
  `--export-dynamic` export definitions. A static image refuses these options.
- Linux and Android ELF links: `--no-undefined`, `-no-undefined`, and the
  two arguments `-z defs` require strong references to resolve. Executables
  already enforce this rule; on a shared object it overrides loader resolution.
- x86-64 Linux shared objects: `-soname NAME`, `--soname NAME`, `-h NAME`,
  `-soname=NAME`, and `--soname=NAME` set `DT_SONAME`.

Other linker values and unsupported targets/output modes fail rather than
silently losing link semantics. This includes PE, Mach-O, UEFI, compile-only,
preprocessing, assembly-text and bitcode output. `-rdynamic` uses the same
export validation. Unknown options, entry overrides, wrapping, archive-mode
switches, runtime paths, version scripts, and other `-z` modes are refused.

`-Wp,` and `-Wa,` are unsupported and are never warning options. This includes
preprocessor macro/include operations and dependency requests. Direct `-D`,
`-U`, and `-I` remain available; dependency generation (`-M`, `-MM`, `-MD`,
`-MMD`, `-MF`, `-MT`, `-MP`) is refused in every spelling. A failed request
preserves any existing artifact instead of reporting a successful stale build.

## Object output (`-c`)

`-c` writes the object through `object_write_borrowing`. The ELF64 writer
plans the whole file with checked arithmetic, then stores each byte once,
except that each section payload of at least 4 KiB is named in place and the
file is published from the image's ranges and those payloads in order
(`object_artifact_slices`, `file_publish_slices`), byte-identical to
`object_write`'s image. It refuses an
object whose section count reaches `SHN_LORESERVE`, whose string tables need
offsets past 32 bits, or whose size overflows or exceeds the arena, with the
diagnostic `native elf64 object exceeds the object writer's limits (...)`,
and leaves an existing output file untouched. `-v` prints the writer's exact
work as one `OBJECT_WRITE` record, summed over the objects of a multi-input
`-c`. See [object emission](../object-emission.md).

## ELF TLS companion lookup

The x86-64 executable writers index TLSGD/TLSLD section/offset sites in link
scratch after initializer stripping. Import classification and relocation
planning reuse those exact identities; input relocation order and duplicate
sites do not determine membership. Shared images retain helper calls. Empty
cases allocate no table, scratch exhaustion fails before publication, and the
existing encoding and relocation bounds checks remain mandatory. See the
[link comparison package](../linker-tls-comparison.md) for work counters,
object/archive loader controls, latency boundaries and evidence limitations.

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
verified states fail compilation. It applies to native x86-64/AArch64 code
generation, including the `none` MIR_STACK compatibility spelling;
preprocessing, syntax-only and direct non-native output reject the flag.
Successful compilation prints a versioned `CODEGEN_VERIFY` line with module,
selected-function and scheduled-function counts and the effective allocator.
Normal compilation keeps its existing validation certificates and fast paths.
The [native differential runner](../differential-testing.md) consumes this
explicit opt-in evidence and compares executable observations independently.
When selected or scheduled MIR fails verification, the refusal names the
`MachineVerifyError` and its block, machine instruction, and operand. Without a
failing canonical instruction its opcode is `unknown`, not an IR enum default.

With `-v`, aggregate `CODEGEN` data is printed after codegen errors too. Legacy
fallback counters and `-fcodegen-fallback-census` remain accepted during the
#514 ABI/CLI cleanup, but production native generation keeps them empty.

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

## Response files

`compiler_driver_parse_arguments` expands `@path` arguments before it reads
any option, so `ide cc` and embedding callers share one behavior. An argument
whose first byte is `@` is replaced, in place, by the arguments held in the
file `path` names; a relative path resolves against the current directory.
Expansion applies after `--` as well, as in GCC and Clang. An argument that
contains `@` elsewhere (`a@b.c`, `-Wl,@x`) is not a response file. With no
argument beginning with `@`, the argument slice is used unchanged and neither
bound below applies.

The file's text follows GCC's `expandargv` and Clang's GNU tokenizer:

- space, tab, newline, carriage return, vertical tab and form feed separate
  arguments; there is no comment syntax;
- single and double quotes group bytes, including whitespace, and are
  removed, so `a"b c"d` is one argument and `""` or `''` is an empty one;
- a backslash takes the next byte literally inside or outside either quote,
  so `\"`, `\'`, `\\`, `\ ` and a backslash-newline pair each yield that
  byte. Write Windows paths with `/` or doubled backslashes.

Where those compilers accept malformed text in different ways, this driver
refuses it with a `driver.argument` error naming the file: a quote still open
at the end of the file, a trailing backslash, a NUL byte, and a bare `@`.
Nesting is not supported: an expanded argument that itself begins with `@`,
quoted or escaped, is refused rather than expanded, so one file never
includes another. Name an input that begins with `@` as `./@name`.

`COMPILER_DRIVER_RESPONSE_FILE_BYTE_LIMIT` (4 MiB) bounds the bytes read from
all response files of one invocation together, and
`COMPILER_DRIVER_RESPONSE_FILE_ARGUMENT_LIMIT` (65536) bounds the fully
expanded command line; exceeding either is a `driver.argument` error. The
reader requests one byte past the remaining budget, so a pipe or device is
bounded too. A file that cannot be opened or read (missing, a directory) is a
`driver.file-read` error, `could not read response file <path>`, which
`ide cc` prints after `cc: error:` before exiting nonzero. Expanded arguments
are NUL-terminated copies in the invocation arena.
`compiler_driver_test_response_file_arguments` covers the grammar and bounds;
`compiler_driver_test_response_file_batch` checks that a 400-input `-c` batch
writes the same objects through `@file` as on the command line.

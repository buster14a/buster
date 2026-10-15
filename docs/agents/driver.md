# Compiler driver and target options

[Agent instructions](../../AGENTS.md) · Paths and commands below are relative to the repository root.

## Compiler output streams

For optional one-function native source/MIR/object investigation, see
[the capture and command guide](../native-compiler-investigation.md). Capture
uses `-finvestigation=PATH` with `-finvestigation-function=NAME`; `ide investigate`
consumes the sidecar and exact object. The initial supported route is x86-64
Linux C `-c` with FAST. Missing/transformed mappings stay explicit.

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

Before publishing a named output, the driver compares the followed file
identity of the destination with every positional input and each physical
source or header file the C preprocessor opened, including forced `-include`
and `#include_next` files. Virtual builtin resource headers have no filesystem
identity and are not treated as files read from the working directory; real
files with the same basename remain protected. System headers stay in the
collision closure even when `-MMD` omits them from dependency text. A match
fails with an argument diagnostic before staging or replacing the destination,
preserving both the input and any existing output bytes. This catches direct,
symbolic-link, hard-link and case aliases whenever the filesystem reports the
same identity. Direct Wasm, SPIR-V and eBPF emitters use the same action-aware
path selector for checking and publication, including their default artifact
names. Native executable link outputs and `-MF` dependency outputs are checked
against each unit's physical inputs before publication; multi-input links
retain each C and preprocessed-assembly unit's physical closure through the
final output check. For multi-input `-c` and `-emit-llvm`, every derived `.o`
or `.bc` destination is checked against the full positional input set before
any unit runs, so an earlier output cannot replace a later input. The check
does not resolve implicit `-lNAME` library searches or compiler-generated
secondary evidence outputs.

Publication (`file_publish_checked`, `file_publish_slices_checked` and
`file_copy_checked` in `src/buster/lib/file.c`) writes a staging file beside
the destination, closes it and renames it over the destination. It promises
completion and atomic replacement, not crash durability: nothing calls
`fsync(2)` or `FlushFileBuffers`, so after a power loss or kernel crash a
just-published artifact may be missing, stale or empty, as with Clang and GCC
objects. A process that observes a successful publication, including the
build driver's self-host verification, sees the complete bytes; build systems
recover from a crash by rebuilding from timestamps. Removing the per-artifact
flush (#2621) saved 7-8 ms of wall per object on a btrfs desktop. No caller
needs durability today; one that does should flush explicitly with
`os_file_flush` rather than make every artifact pay for it.

Buster does not overwrite outputs in place as mold and wild do; #3687 records
the measured gain and the downsides. Preallocating the staging file with
`fallocate(2)` is measured but not adopted: it awaits qualified Zen 5 evidence
(#3685).

`-fearly-exit` (API field `early_exit`; off by default, and `-fno-early-exit`
restores the default) is mold and wild's fork trick (#3686). It lives in
`run_c_compiler` and `os_early_exit_*` in `src/buster/lib/os.c`.

- **Arming.** After argument parsing, and before the first `lane_run` starts
  the gang, `ide cc` forks. The parent only waits on a pipe. Arming is
  skipped on Windows, Android and iOS, and when a tracked thread already
  exists (`os_is_only_live_thread` does not see untracked threads; desktop
  `ide cc` starts none before this point). It is also skipped when the
  status pipe cannot be created through `os_process_spawn_pipe_create`
  (close-on-exec, both ends moved above fd 2), or when `fork` fails. The
  compile then simply runs in-process. Because the status pipe never lands
  on fds 0-2, a process started with standard streams closed cannot write
  compiler output into it.
- **Release.** Once every artifact and report is out, the child calls
  `os_early_exit_release`. It points its standard streams at `/dev/null`,
  so a build system's pipes reach EOF, and sends its status. The parent
  `_exit`s with that status, or with the child's own status if the child
  died before reporting.
- **Background work.** Destroying the 32 GiB translation-unit arena, then
  `exit_mmap`, then the release of each output that publication replaced
  then run in the background. While armed, `file_publish_slices_checked`
  holds a read-only reference to a replaced destination across the
  `rename(2)` and hands it to `os_early_exit_defer_close`, so the old
  inode's blocks are freed after the report rather than inside the rename.
  Atomic publication is unchanged.

The costs are why it stays opt-in:

- `time(1)`, `getrusage(RUSAGE_CHILDREN)` and `-fmetrics-out` wrappers that
  measure the process see only the parent (about 5 MB RSS and no CPU time).
- A debugger follows the parent.
- A `make -j` or cgroup budget frees the job slot while the child still
  holds its memory.
- A failure after the release cannot change the exit status, and any later
  child output is discarded.

Do not use it for benchmarks, profiling, debugging or memory budgeting. The
`compiler_driver_test_early_exit` regression checks byte-identical objects
against the default path, a replaced output, `-E` output through the caller's
pipe, and failure propagation. On desktop POSIX, it also runs both modes with
fds 0-1 and 0-2 closed, with and without `-v`, and compares raw exit statuses
and artifacts. It forces the setup-failure fallback through the test-build seam
`BUSTER_TEST_EARLY_EXIT_PIPE_FAILURE=1`. That seam fails the pipe's
configuration step and validates that the failure happened.

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

The registered `compiler_diagnostic_tests` include a desktop close-failure
cleanup regression. It injects one refusal after the real staging write and
close, checks source mapping and native handle balance, old-or-absent
destination bytes and a private directory inventory, then reuses the same invocation successfully
against an independent literal preprocessing result. Close and staging deletion
remain native operations. Android and iOS skip this desktop observation path
explicitly; the existing portable write-failure tests still run. This is
controlled failure-path coverage, not a real disk-failure or crash-durability
claim. Run `ide test --module=compiler_diagnostic_tests --verbose=1 --ci=1`.

## In-memory execution (`ide run`)

Use `ide run <C compiler options and inputs> [-- <program arguments>]`.
Arguments before the first literal `--` are compiler options and inputs;
everything after it is passed to the program, and omitting the separator means
there are no program arguments. The first input path becomes `argv[0]` exactly
as spelled, including `-` for source read from standard input. The program is
called as `int main(int, char **, char **)`, with the current process
environment as `envp`; a returned `int` becomes the `ide` process exit status.
Calling `exit()` instead terminates the `ide` process directly.

The command runs the ordinary C frontend, canonical-IR lowering, native
code-generation and `link_objects` merge, then passes the merged object to
`jit_link_object`. It creates neither an intermediate object file nor an
executable image. Included headers are still read from the filesystem. When
the source input is `-`, `ide` consumes standard input to EOF before calling
the program, so the program sees its standard input at EOF.

Failures exit with status 1, print `run: error:` to stderr, and execute no
program code. Execution requires an x86-64 Linux host and an x86-64 Linux
target; other hosts report
`run: error: ide run supports only x86-64 Linux hosts`, and a foreign target
is named in its refusal. Queries, non-link actions (`-c`, `-S`, `-E`,
`-fsyntax-only`), explicit `-o`, `-shared`, `-pie`, LLVM bitcode, GPU targets,
and linker/library/framework options including `-l` are refused. `ide run`
binds only its documented host imports and loads no libraries.

With `BUSTER_LINK_LIBC` on the supported host, the only imported functions
bound by the command are:

```text
puts, putchar, printf, sprintf, snprintf, vprintf, vsnprintf, getchar,
strlen, strcmp, strncmp, strcpy, strncpy, strcat, strchr, strrchr, strstr,
memcpy, memmove, memset, memcmp, malloc, calloc, realloc, free, atoi, atol,
strtol, strtoul, strtod, abs, labs, qsort, rand, srand, exit, abort, write,
read, __errno_location
```

Without `BUSTER_LINK_LIBC`, the binding table is empty. Data imports such as
`stdout`, `stderr` and `environ` are unsupported, as are TLS references;
nonempty initializer/finalizer arrays are refused. The underlying JIT's Apple
Silicon implementation remains subject to its one-`MAP_JIT`-region
restriction, although `ide run` itself is supported only on x86-64 Linux.
This is in-process execution, not a sandbox: the program has the `ide`
process's privileges and can affect it. After a returned `main`, the JIT
program is released and its arena is destroyed; when the program calls
`exit()`, the operating system reclaims them as the process terminates.
Executed test coverage is x86-64 Linux only; shebang execution is not
implemented.

## Opt-in native translation-unit lanes

`-fcompile-jobs=N` accepts a positive 32-bit worker request. Omission (or
zero in the invocation API) means one worker. Only consecutive native C
inputs in a link invocation are batched; preprocessing, syntax-only, `-S`,
`-c`, LLVM/GPU/Wasm/eBPF paths and single-input fast paths retain their
existing execution. Objects, archives, assembly and each `-l` occurrence are
serial boundaries, even when `-x c` is present. A library between C sources
ends the cohort before later translation units can publish definitions. Worker count is clamped to the logical CPUs the
process may run on (the affinity mask on Linux and Windows, so `taskset`, a
cpuset or a job object narrows it; cgroup CPU quotas are not considered), input
count and one inside an embedding caller's multi-lane gang. The default
`lane_run` width uses the same count.

Each cohort contains at most one full TU per worker. `lane_range` gives
stable input slots, the existing persistent gang is reused, and each worker
uses a private TU arena and diagnostic collector. No worker writes an output
file. The caller deep-copies compact objects and diagnostics in input order,
retains constructor priorities, and uses the existing ordered linker. The
first failing input wins; later completed results and warnings are discarded
and all cohort arenas are released (with `-fkeep-going`, later inputs still
compile, see [per-input records](#per-input-records-and-continue-on-failure)).
The one-worker and `BUSTER_SINGLE_THREADED` builds run the same unit kernel.

Before its first multi-lane cohort, the driver prepares only the
invocation's native family (`compiler_driver_native_family_prepare`). For
x86-64 that is the serial compile's own prewarm plus the forwarding and NOP
recipes (`buster_x86_metadata_prewarm_for_lanes`): the per-form metadata
caches, exact plans, TLS/GOT recipe groups, expansion shapes, dense tables,
variable-memory cells and template rows are filled by the lanes that reach
them, under once-claims (see the [parallelism guide](parallelism.md)), so a
gang prepares the forms it emits instead of all 11,013 (#53). For AArch64 it
is canonical-form validity and the semantic blobs. Prepared families are
tracked in driver state.
Parked workers count as live threads, so a later invocation on the same thread
that needs a family not yet prepared first releases the parked gang with
`lane_release()`, prewarms that family serially, and lets the next `lane_run`
recreate the gang. This applies to a later cohort and to a serial compile of
that family while a cohort's gang is parked. Only a transition to a family not
yet prepared releases the gang; once both are prepared, alternating targets
reuses the parked gang.
A process that never runs a cohort keeps the serial lazy path. The end-to-end
effect awaits the 9700X `scaling-v1` rows.

`compiler_parallel_prewarm()` still prepares both families. Embedding callers
must call it before starting their own gang, or before calling the driver from
more than one thread, because the driver cannot release a gang it does not
own. The lighter `compiler_prewarm()` remains the serial frontend/common-table
entry. The AArch64 prewarm is an opt-in cost, not a new serial startup floor.
The x86-64 target prewarm is the existing one: every x86-64 process that
generates code pays it once, on the first `codegen_prewarm_for_target`. On the
Ryzen 7 9700X it costs 1.74 ms of the compiler's own time and 6.6 M retired
instructions above an AArch64 compile of the same one-function unit
([`2026-10-13T160944Z`](../performance-audits/2026-10-13T160944Z.md), #1125).
`CompilerDriverResult.compilation_workers` reports the largest active cohort
(or one); it is not a physical-core or memory measurement.

**Lane gate.** Entering the multi-lane path costs a fixed preparation of the
cohort's native family per invocation, and each extra worker adds its own
overhead. Before dispatch, `compiler_driver_unit_worker_limit` therefore
estimates the work of every eligible native C input of the invocation (#53):

- each input adds `COMPILER_DRIVER_LANE_INPUT_WORK_BYTES`, a fixed charge per
  translation unit, plus its file size from `os_path_followed_stats`;
- inputs are stat'ed only until the sum covers both the serial minimum and the
  per-worker floor at the requested width, so a large population costs a few
  stats. A missing or unreadable input adds no bytes and still fails in input
  order.

A population below `COMPILER_DRIVER_LANE_MIN_WORK_BYTES` keeps exactly the
`-fcompile-jobs=1` path: no prewarm, no gang and the same output. Otherwise the
worker count is capped at the estimated work divided by
`COMPILER_DRIVER_LANE_WORK_PER_WORKER_BYTES`, never below two; a capped run is
the ordinary `-fcompile-jobs=N` path at that smaller N and links the same
image. `CompilerDriverResult.lane_gate` records the decision:

| Value | Meaning |
|---|---|
| `none` | Lanes cannot apply: one worker after clamping, fewer than two eligible inputs, an embedding gang or a source cache. |
| `lanes` | Lanes were permitted at the requested width; `compilation_workers` still reports the cohorts that actually ran. |
| `serial` | The population was below the minimum; `compilation_workers` is 1. |
| `capped` | Fewer workers than requested ran, so each keeps the per-worker floor; `compilation_workers` reports them. |

The three constants in `driver.h` come from the 9700X `scaling-v1` receipt of
#3943 (check run 115115906751, main `10e44760` with the one-family prewarm and
the lane-safe on-demand x86 fills):

- **Serial minimum 0.** Two equal TUs (11.7 ms serial) already gain 1.20× at
  two workers, so no measured population is protected by staying serial.
- **Per-input charge 513 bytes.** Serial wall time over the `size-n2`…`n64`
  series is about 5.6 ms + 0.067 µs per byte; `tiny`'s 112 one-function TUs
  then imply about 0.035 ms per input.
- **Per-worker floor 32 KiB.** `tiny` (about 76 KB of estimated work) loses at
  7 workers (10.8 KB per worker, 0.93×). It gains most at 2 workers (37.8 KB
  per worker: 1.15× and, in the validation run 115276385855, 1.09×), ahead of
  4 workers (1.11× and 1.04×), so the floor caps it at 2. A 16 KiB floor
  (4 workers) brought `tiny` at 7 workers to 0.99× and at 13 SMT workers to
  1.01× in that run. Every `equal`, `count` and `size` row keeps more than
  48 KB per worker and is unaffected.

The estimate reads file bytes only, so a small file that includes large
headers is underestimated: that costs a possible gain, never a loss against
serial. Any change to the lane entry cost moves all three values, so re-derive
them from a fresh `scaling.request`; the `size`, `tiny` and `count` series are
the inputs. `compiler_driver_test_set_lane_work` (`driver_internal.h`)
overrides them for tests; `compiler_driver_test_unit_lane_gate` covers each
bound and the cap.

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
  allocated is that input's `failed` record with `driver.resource` (rather
  than an invalid-source rejection) and the batch continues. The diagnostic
  states the 32 GiB virtual reservation required by that unit. This preserves
  the fixed arena's large-input capacity; restricted-address-space admission
  and job-count adaptation remain separate work (#1311). An
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
`codegen_prewarm_for_target(target)` (the one-time frontend, ABI and target
tables that used to land in input 0's `codegen` phase), and primes first-touch
page faults. On the 9700X this setup put an x86-64 one-function input's
`start_ns` at 2.4 ms and an AArch64 one's at 0.7 ms
([`2026-10-13T160944Z`](../performance-audits/2026-10-13T160944Z.md)). It runs
for every action, so a measured `-fsyntax-only` or `-E` pays a target prewarm
that the same command without `-fmetrics-out` never runs; compare such runs
only with each other (#4135). The priming commits and touches a pooled TU
arena (`COMPILER_DRIVER_METRICS_TU_PRIME_BYTES`, which the first unit's arena
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
CC_METRICS version schema=buster-cc-metrics inputs records ok rejected failed not_run prebuilt error exit_status action target allocator compile_jobs compilation_workers intervals keep_going function_sizes wall_ns peak_rss_bytes lane_gate
CC_METRICS_INPUT version index status error errors warnings measured start_ns end_ns total_ns read_ns preprocess_ns parse_ns analysis_ns ir_ns codegen_ns object_ns emit_ns arena_peak_bytes arena_retained_bytes source_bytes preprocessed_tokens object_file_bytes text_bytes rodata_bytes data_bytes bss_bytes tdata_bytes tbss_bytes initializer_bytes unwind_bytes debug_bytes codegen_functions instructions values code_bytes stack_frame_bytes max_stack_frame_bytes fallback_functions fallback_records function_records function_records_omitted diagnostic_records diagnostic_digest diagnostic_line diagnostic_column path_hex diagnostic_code_hex diagnostic_path_hex message_bytes message_truncated message_hex
CC_METRICS_FUNCTION version input ordinal code_bytes name_bytes name_truncated name_hex
```

The header comes first, then each input followed by its functions. Between
identical runs only the `*_ns` fields, `peak_rss_bytes` and possibly
`arena_peak_bytes` (granule-boundary padding) vary, plus
`compile_jobs`/`compilation_workers`/`intervals`/`lane_gate` across worker
counts. `lane_gate` (`none`, `lanes`, `serial` or `capped`; see the lane gate
above) was appended after `peak_rss_bytes`.
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
`-masm=att|intel` (x86-64 `-S` listings are Intel syntax only, so `-S -masm=att` of a C input is refused). CPU and feature options also accept separated values. CPU names use the canonical
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
`-march=x86-64` (the existing `baseline` model) and the psABI levels
`-march=x86-64-v2`, `-v3` and `-v4` are accepted on x86-64 targets only (other
targets keep `unsupported CPU model`): each level is `baseline` plus the
cumulative features of `compiler_driver_psabi_features` (v2: cx16, popcnt,
sse3, ssse3, sse4.1, sse4.2; v3: avx, avx2, bmi1, bmi2, f16c, fma, lzcnt,
movbe, xsave; v4: avx512f/bw/cd/dq/vl). The feature predefines the frontend
publishes (`c_target_feature_macros`: `__AVX__`, `__AVX2__`, `__AVX512F__` and
the other AVX-512 subsets it lists) follow the level; SSE3/SSE4.x/BMI macros are
not predefined at any level. `-mattr` overrides still refine the level.
The psABI's LAHF-SAHF has no target feature and is implied by long mode.
`-m64` selects x86-64 execution mode, including from an i386 target, and clears
`code16`. Together with `-m16` and `-m32`, the last spelling wins independently
of `--target` order. Repeated `-m64` preserves an existing x86-64 target's OS
data model and CPU features. All three modes are refused for foreign and GPU
targets; `-mx32` remains unsupported (#3458).

For 32-bit x86, the canonical target architecture is `i386`; `i386`, `i486`,
`i586` and `i686` are accepted architecture spellings, while `-march=i386`
and `-march=i686` select explicit CPU models. The I386 baseline models GCC's
`-m32` default (i686 without MMX/SSE). `-m16` and `-m32` select I386 execution
mode, with the last spelling winning independently of `--target` order;
`-m16` also records the `code16` target environment. These modes support
preprocessing (`-E`, including `-dM`) and frontend-only validation
(`-fsyntax-only`). I386 artifact generation—including assembly, objects,
LLVM output and links—is deliberately refused with an explicit #3654
diagnostic. I386 is supported only for Linux and freestanding targets; an
execution-mode option on an x86 target with another OS is refused using the
pre-mode target triple in its diagnostic. `-m64` switches back to the existing
x86-64 target as described above.

Without `-march`/`-mcpu` the CPU model is `baseline`, matching Clang's and
GCC's portable `x86-64` default. A command with no `-target` compiles for the
host's architecture and operating system (`target_native`,
`compiler_driver_parse_arguments`) but not for its CPU features. It is the
same as naming the host triple: on x86-64, `-target x86_64-linux` alone gives
`cpu=baseline features=sse2`, and AArch64 gets `fp-armv8,neon`, Apple targets
included, where Apple Clang would pick `apple-m1`. So object bytes and the
predefined feature macros (`__AVX2__`, `__AVX512F__`, ...) do not depend on
the build machine. `-march=native`, or `-target native`, selects the host's
detected model and features. Tests exercise native code unless they select a
target: `ide test` and `ide metamorphic` set
`compiler_driver_host_cpu_default`, so their in-process invocations keep the
host CPU, while `compiler_driver_test_run_cc`, the build.c test gates and
`test_self_host` stage compiles pass `-march=native`.
The native-retirement census pins `baseline`, and the work ledger pins
`znver3` (GitHub #1557).
`-mtune=<model>` is accepted with any nonempty value, `native` included, and
ignored: it selects only a scheduling model, and instruction selection here has
no per-CPU tuning, so it never changes the emitted code (GitHub #2851).
`-m<feature>` and `-mno-<feature>` (`-mavx2`, `-msse4.1`, `-mno-avx2`,
`-mpclmul`) are `-mattr=+feature` and `-mattr=-feature` in the same ordered
override list, so the last option naming a feature wins and they refine a
`-march` level wherever they sit. The feature names are the ones `-mattr`
takes, dotted spellings included. On AArch64, `sve` is a feature name and is
not included in CPU-model defaults. On x86-64 they also follow GCC's implied
features: `-mavx2` adds AVX, and `-mno-avx2` / `-mno-avx` also drop every
enabled feature that requires it (AVX-512, FMA, VNNI, ...). The dependency pairs
are `target_x86_feature_requirements` in `target.c`: the rules
`target_cpu_features_are_valid` enforces plus GCC's SSE chain, XSAVE for AVX and
AMX-TILE, and the FMA4/XOP/AVX512VP2INTERSECT edges. Where the validator is
stricter than GCC 13 the table follows the validator (VAES needs AVX2,
VPCLMULQDQ needs AVX, AVX512VBMI2 and AVX512BITALG need AVX512BW), so a closed
set is always accepted. `-mattr` deliberately stays exact: it
applies only the named features and the combination check refuses the rest
(`-mattr=-avx2` on an AVX-512 set is `invalid target feature combination`).
`-mno-sse2` is refused by name because SSE2 is part of the x86-64 baseline here.
Rejected spellings are listed in the table below (GitHub #1418).
`-march=`, `-mcpu=`, `-mtune=`, `-mattr=` and `-masm=` keep their own meanings.
`-v` reports the selected CPU, the sorted effective feature set,
and maximum native vector width. `-target`/`--target` strings are
`arch[-vendor][-os][-environment]`: the vendor and environment components stay
free-form, but a CPU model there is rejected in favor of `-march=`, and so is
anything past the fourth component. Both used to be dropped silently, which
left baseline code generation and no hint that the request was ignored.
`-fno-strict-overflow` is accepted as `-fwrapv` (signed overflow already wraps;
there is no `-fno-wrapv`, so `-fstrict-overflow` stays unsupported). `-w` is
accepted and publishes no warning text or records and outranks every other
warning option.
`-ffreestanding` publishes `__STDC_HOSTED__` as zero and keeps the builtin
`<limits.h>` from forwarding to a hosted system header. Buster does not
implicitly builtinize or fold ordinary library calls such as `memcpy` or
`strlen`; only explicit `__builtin_*` spellings are compiler builtins, so no
ordinary-call suppression is needed. `-fomit-frame-pointer` grants permission
to omit a frame pointer; it does not require one to be omitted. Default native
objects currently contain `.eh_frame`, so `-fno-asynchronous-unwind-tables` is
accepted only for I386, where artifact generation is refused.
`-fcf-protection=none` is accepted because default output contains no
`endbr32`/`endbr64` instructions and no IBT/SHSTK GNU property note.
`--version`, `-dumpversion`, `-dumpmachine` and
`-print-target-triple`/`-print-effective-triple` need no input and exit 0
(`compiler_driver_query_text`):
`-dumpversion` prints `18.0.0`, the `__clang_major__`/`__clang_minor__`/
`__clang_patchlevel__` triple (`__clang_version__` is `18.0.0 (buster)`);
`-dumpmachine` prints the effective target as `arch-os[-environment]` with the
`gnu` environment on Linux and `msvc` on Windows (for example
`x86_64-linux-gnu`, or `i386-linux-gnu` under `-m32`); `--version` prints
`Buster clang version 18.0.0 (buster)`,
`Target: <that triple>` and `Thread model: posix`. `-print-target-triple` and
`-print-effective-triple` print Clang's normalized `arch-vendor-os[-environment]`
form, with the vendor `apple` on macOS/iOS and `unknown` elsewhere (for example
`x86_64-unknown-linux-gnu`, `aarch64-unknown-linux-android`,
`x86_64-unknown-windows-msvc`).
`-trigraphs` and `-ftrigraphs` enable trigraph replacement and `-fno-trigraphs`
disables it. A later `-std=` restores that dialect's default (on in strict C99
to C17, off in GNU modes and C23), so the last of these options wins, as in
Clang. `-nostdlibinc` drops the system include directories but keeps the
compiler resource directory (`stddef.h` and the other freestanding headers);
`-nostdinc` drops both. Explicit `-I`, `-isystem` and `-idirafter`
directories remain available with either switch. `-idirafter DIR` and
`-idirafterDIR` add system-header directories after `-I`, `-isystem`, the
compiler resource headers and the default system roots, preserving the order
of repeated `-idirafter` options. Both quoted and angle includes consult them;
quoted includes still search the including file\'s directory first. Headers
resolved there are excluded by `-MMD`. External GPU pipelines refuse
`-idirafter` explicitly because their system-path forwarding would lose its
late search position. The registered
`compiler_driver_test_idirafter` covers shadowed headers, sysroot defaults,
`-nostdinc`/`-nostdlibinc`, `#include_next`, both spellings and MD/MMD classification. `-working-directory=DIR` and `-working-directory DIR`
resolve relative input paths against `DIR` with host path rules (on POSIX only
a leading `/` is absolute and `\` and `:` are ordinary filename bytes; on
Windows `\x.c` takes the root name of `DIR` (`C:` or UNC `\\server`) and
`C:x.c` its root-relative directory, as in LLVM's `make_absolute`). Output paths and `-I` directories stay relative to the process
directory, and a `DIR` that is not a directory is rejected with
`unable to set working directory: DIR`.
Windows targets implement the MSVC ABI only, so the MinGW spellings
(`*-mingw32`, and a `gnu`/`gnullvm` environment on Windows) are rejected with
`unsupported target environment` instead of being aliased to MSVC (#1492);
MinGW's GCC `ms_struct` layout is not modelled.
The GNU environment refusal applies after a recognized Windows OS component;
`x86_64-gnu-windows-msvc` and `x86_64-gnullvm-windows-msvc` retain their free-form
vendor meaning. An excess component retains precedence over that refusal.
Native x86-64 and AArch64 compilation uses the FAST register allocator at
every optimization level, including the default and `-O0`. Callers may select
`fast` or `quality`; both run canonical IR -> MIR -> placement ->
metadata-backed native emission. Explicit allocator selection is independent
of `-O`; the last allocator option wins.
The stack-only mode is removed: `-fregister-allocator=none`,
`-fregister-allocator=mir-stack` and `-fno-register-allocator` are argument errors.
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

For explicit two-operand AT&T port I/O, `inb`/`inw`/`inl` and
`outb`/`outw`/`outl` apply the suffix to the accumulator's data width.
The port operand retains its separate architectural DX16 or imm8 role;
metadata validates the exact accumulator, port register and immediate range.
Source selection consumes both operands even though the imported XED rows mark
the accumulator and DX as implicit. The hidden `OeAX()` accumulator selects
AX or EAX and has no ModRM field; its width determines the word prefix rather
than DX's fixed 16-bit width. The memory-looking `(%dx)` port spelling that
GNU objdump prints and GNU as accepts names DX itself
(`assembly_x86_att_port_dx_parse`), so only a bare parenthesized `%dx` on
`in`/`out` qualifies; `(%rdx)`, `(%edx)`, displacement, segment and index forms
remain invalid. Unsized Intel `ldmxcsr`/`stmxcsr [mem]` infers MXCSR's only m32
width, as AT&T's unsuffixed spelling already does.
`assembly_test_att_port_suffixes` checks exact bytes against matching Intel
spellings, including immediate boundaries, word prefixes and invalid register,
memory, suffix-width and 64-bit neighbours. It never executes port I/O.

Explicit `-O`/`-O0` skips optional canonical FAST cleanup; positive levels
select the existing bounded `fold,address,dce,parameters` pipeline. Omission
retains the adopted throughput recipe. Explicit whole-pipeline and per-pass
controls override level defaults independently of option order; the last
explicit control of each bit wins. Level/alias policy, fixed pass order and
budgets are in [the FAST pipeline contract](../canonical-fast-pipeline.md#native-optimization-level-policy).
Verbose output reports disabled as well as enabled passes. Timing is separate
(`-ftime-canonical-fast -v`); register allocation selection is independent.
The block-local CSE experiment (#3011) is selected only by
`-fcanonical-fast-cse`; no level or whole-pipeline control enables it, and
`-fcanonical-fast`/`-fno-canonical-fast` clear an earlier selection.
`-fcanonical-fast-cse-strategy=window|table` picks the census's linear or
hash-table arm (default `table`; the last one given wins). See
[opt-in block-local CSE](../canonical-fast-pipeline.md#opt-in-block-local-cse-3011).
`-fcanonical-intern-constants` / `-fno-canonical-intern-constants` turn on and
off construction-time constant interning in C lowering (on by default, last
one wins, independent of `-O` and FAST controls); see
[construction-time constant interning](../canonical-fast-pipeline.md#construction-time-constant-interning-3011).
`-fcanonical-construction-fold` / `-fno-canonical-construction-fold` turn on
and off the experimental construction-time fold of integer-constant
conversions and truth tests in C lowering (off by default, last one wins,
independent of `-O` and FAST controls); `-v` prints `C_CONSTRUCTION_FOLD`. See
[opt-in construction-time folding](../canonical-fast-pipeline.md#opt-in-construction-time-folding-3010).

Canonical inlining is separate from register allocation. Tiny direct-call
inlining is opt-in at every optimization level: `-fcanonical-inline` enables
it and `-fno-canonical-inline` disables it (the last of the two wins), and
`-O*` levels never change it. The matched 9700X tiny-off/on self-host profile
on #3111 measured no compiler-throughput gain from enabling it (stage-1
compile wall +1.66%, generated compiler self-host wall +0.68%), so no level
adopts it by default. The optimizer still enforces `always_inline` requirements when tiny
inlining is disabled; an impossible requirement produces a source-mapped
`ir.inline-required` error. Positive 32-bit per-invocation limits are set with
`-fcanonical-inline-max-callee=N`,
`-fcanonical-inline-function-growth=N`,
`-fcanonical-inline-module-growth=N`,
`-fcanonical-inline-call-sites=N`, and
`-fcanonical-inline-caller-growth-shift=N`. These flags set budgets but do not enable
tiny inlining; zero is rejected on the command line. Zero-valued API row/site limits
select the optional defaults while required expansion uses independent per-caller
work/storage guards (including one relocation of existing caller data); other zero API fields normalize to the named defaults.
Those guards price block local-value snapshots only for blocks that carry one,
and canonical C construction attaches none (#4050).
Explicit budgets cap callee instruction count, added instructions per
caller, added instructions per module, and accepted call sites, respectively;
the shift sets the caller relocation bound described below. The callee limit
defaults to 16 canonical rows (`IR_INLINE_TINY_INSTRUCTIONS`); the FAST
pipeline contract records the #3854 census and why a 13-row default was
reverted.
Without an explicit module limit, the optional module budget is relative: the larger of
4096 rows and 1/32 of the module's lowered canonical instruction rows
(`ir_inline_module_growth_limit`). A caller keeps optional splices only when
their growth is at least 1/32 of its rows (`IR_INLINE_CALLER_GROWTH_SHIFT`);
see the FAST pipeline contract for the measured memory tradeoff.
Verbose output emits one `IR_INLINE` record when tiny inlining is enabled or
inlining candidates were encountered. Its fields are `tiny`, `candidates`,
`inlined`, `always_inlined`, `copied_instructions`, `growth`,
`budget_skips`, `shape_skips`, `linkage_skips`, `recursion_skips`, `visits`,
and `released_bytes` (pages of relocated caller tables handed back to the OS).
It is compile-work telemetry, not generated-code runtime.

`-fno-frontend-ssa` selects the original memory-form C lowering;
`-ffrontend-ssa` restores direct SSA for the bounded supported subset. The last
flag wins. These controls are independent of `-fno-canonical-local-promotion`
and `-fno-target-local-promotion`: disabling shared promotion does not undo
SSA already built by the frontend. For a fully memory-form differential input,
disable frontend SSA as well. Verbose compilation reports `IR_FRONTEND_SSA`
counters beside `IR_LOCAL_PROMOTION`; see the
[frontend ownership contract](frontend/foundations.md#direct-local-ssa-github-34).

`-fc-ast-pilot[=implicit|hybrid|explicit]` (default `implicit` since the
[owner decision of 2026-10-13](https://github.com/buster14a/buster/issues/3102#issuecomment-6146782845);
the bare flag is `implicit`) builds the [implicit postorder syntax
tree](frontend/ast.md#driver-pilot-hook) of each C input after a successful
`c_preprocess`, inside the parse phase, so its time lands in `parse_ns`. The
unit's declaration records then come from the tree
([`c_parse_ast_from_tree`](frontend/ast.md#declaration-split-from-the-tree)),
which hands any unit that `c_parse_ast` would diagnose or read differently
back to `c_parse_ast`. Semantic analysis answers function-body
expression-type queries from the tree where it can (the
[tree expression typer](frontend/ast.md#tree-expression-typer)), its body
binder follows each function body's
[statement plan](frontend/ast.md#statement-plan-from-the-tree), and lowering
answers its root-conditional splits from it
([lowering from the tree](frontend/ast.md#lowering-from-the-tree)); no other
stage reads it. Only with the tree does analysis share the primitive and
pointer type rows those queries mint
([interned rows](frontend/ast.md#interned-rows)). With the flag the object
and every diagnostic are byte-identical to the tree-free path's. A builder that
cannot reserve its storage fails the unit with the parse error class; a unit
whose tree the builder rejects on syntax compiles without a tree, so it reports what
`-fno-c-ast-pilot` reports; `-v` counts it in a `C_AST_REJECTED` row. It does nothing for `-E`, assembly inputs or the other
languages. Any other layout value is an argument error
(`unsupported -fc-ast-pilot layout: <value>`). Verbose compilation prints
`C_AST`, `C_AST_WALK`, `C_AST_TYPES`, `C_AST_SPLIT`, `C_AST_STATEMENTS`,
`C_AST_LOWER` and `C_AST_FACTS` rows, among others, beside `C_TYPE_LAYOUT`.
Except under `-fsyntax-only`, the tree also retains semantic analysis's
per-node facts for lowering
([retained facts](frontend/ast.md#retained-facts)). `-fno-c-ast-pilot` selects
the tree-free path: analysis and lowering answer every question from tokens,
as they did before the switch, and no facts are retained. It is the reference
arm for differentials and bisection, and
[#4058](https://github.com/buster14a/buster/issues/4058) removes it once
lowering reads only the tree. The last of the two flags wins.

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
convention. Both link directions then run both allocators and both frontend
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
both FAST and QUALITY. Direct non-native output, preprocessing and
syntax-only checks still reject the native-only strict option. Assembly inputs
and linked prebuilt objects have no canonical C functions to gate.
For example, `build/Release/ide cc -fregister-allocator=fast -fno-machine-fallback -target aarch64-unknown-linux -c tests/basic_c_call_abi.c -o build/mir-coverage.o`.
`compiler_driver_test_machine_fallback` runs the same fourteen-fixture arithmetic,
control-flow, call-ABI, aggregate and frame corpus for x86-64 and AArch64 on
Linux, macOS and Windows, under both machine allocators and both explicit
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
atomic-update fixture covers all three AArch64 desktop targets, two allocator
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
external call under both allocators; matching Linux x86-64 hosts execute
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

## Short wchar_t and x86-64 code models

`-fshort-wchar` (#4052) sets `Target.short_wchar`, which makes
`target_uses_16_bit_wchar` and `target_uses_unsigned_wchar` true on any
native target, so `wchar_t` is `unsigned short`. The type layer, wide
character and `L"..."` literals, `<stddef.h>` `wchar_t`, `__WCHAR_TYPE__`,
`__SIZEOF_WCHAR_T__`, `__WCHAR_MAX__`, `__WCHAR_WIDTH__` and
`__WCHAR_UNSIGNED__` all follow it. `wint_t` keeps the ABI's own width, as in
GCC. External GPU pipelines reject the option.

`-mcmodel=small` is the x86-64 default and a no-op. The backend emits only the
small model (rip-relative or GOT-relative references; no kernel-model
sign-extended absolute addressing, no large-model `movabs`), so
`-mcmodel=kernel`, `medium` and `large` fail with
`unsupported option: -mcmodel=<model> (the x86-64 backend implements only the
small code model)` rather than aliasing small. Unknown values report
`invalid value for -mcmodel`, and non-x86-64 or GPU targets report
`unsupported option ... for this target`.

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
relocations into an `ObjectFile` like any other. On Mach-O, assembly symbol
names are already final: the driver drops the source's leading `_` because the
Mach-O writer adds the C-level one, and marks a name without one
`ObjectSymbol.final_name` so it is written unchanged. `-S` prints the real
`_`-prefixed names (`compiler_driver_test_macho_assembly_symbol_names`). The vocabulary is `.text`,
`.data`, `.bss`, `.rodata` and `.section`, plus `.pushsection` (same operands as
`.section`), `.popsection` and `.previous`; `.globl`/`.global`/`.extern`, `.weak`,
`.hidden`, `.type` and `.size`; on Mach-O targets `.weak_definition` (N_WEAK_DEF on
a defined global in either order relative to `.globl`; on a defined local it is dropped, as
llvm-mc does; on an undefined name it is refused, since the object model has no weak
undefined Mach-O symbol, and `.weak_reference` is refused for the same reason); `.align`, `.balign` and `.p2align`; `.byte`,
`.short`/`.word`/`.hword`/`.value`, `.long`/`.int`, `.quad`, `.ascii`,
`.asciz`/`.string`, and `.zero`/`.skip`/`.space`; `.intel_syntax noprefix` and
`.att_syntax prefix`; `.local` with `.comm name, size[, alignment]`, and
`.lcomm`, which reserve a private zero-filled object in `.bss`; `.set`/`.equ`
of `symbol` or `symbol±constant` (or a `.`-relative value), resolved once every
label is known, so GCC may write it ahead of the label it names; and, accepted
and dropped because they carry no bytes the linked program uses, the `.cfi_*`
family, `.file`, `.ident`, and Clang's `.addrsig`/`.addrsig_sym`. In an instruction operand `.` is the address of its
own statement, so `b .`, `bl .`, `b.cond .`, `cbz x0, .`,
`ldr x0, .`, `jmp .+5` and the like resolve locally without a relocation or a symbol-table entry. AArch64 `b`/`bl` and the
other PC-relative control forms also take `#imm` (a byte displacement) as well
as a bare `imm`, matching llvm-mc (#2687). The constant is a byte
displacement from the instruction and may be negative (`cbz x0, #-4`); the
range is the form's own (`b.cond`, `cbz`, `cbnz` and `adr` 1 MiB, `tbz` and
`tbnz` 32 KiB, `b`/`bl` 128 MiB; all but `adr` a multiple of 4) and a value
outside it or misaligned is refused (#2706). `adr Xd, label` folds a target
defined in its own section like a branch does. Any other target (another
section, an undefined name, a `.globl` or `.weak` label) stays an
`R_AARCH64_ADR_PREL_LO21` relocation with its symbol and addend on ELF, which
the object reader, in-memory/ELF linkers and `object_aarch64_elf_page_relocate`
resolve as S + A - P; Mach-O and COFF have no such relocation, so there a
target the unit cannot fold is refused.

X86-64 unit assembly relaxes unprefixed symbolic `jmp` and `jcc` to rel8
when the target is binding-invariant and defined in the same section. Layout
starts with eligible branches short and grows out-of-range forms to rel32;
instruction encodings are cached while labels, numeric labels, `.` expressions,
`.set` aliases, data expressions, symbol sizes and alignment are recomputed.
Calls, explicit `@PLT`, external/cross-section targets, weak definitions and
default-visible ELF globals retain rel32. The single-instruction
`assembly_encode` API keeps its existing encoding policy. Short branches in
object `-S` listings therefore reassemble with their original bytes when
labels identify the same targets; this does not preserve an intentionally
nonminimal near encoding or an instruction prefix that the printer omits.

Symbolic page addressing takes the same relocation path (#2933). `adrp Xd, sym[+off]`
names its symbol directly; the low 12 bits are `add Xd, Xn, :lo12:sym[+off]` and an
unsigned-offset `[Xn, :lo12:sym[+off]]` on any LDR/STR/LDRB/LDRH/LDRSB/LDRSH/LDRSW,
FP/SIMD or PRFM form (a leading `#` is accepted). A unit never folds these, even for a
symbol it defines, because final section placement decides the pages; the word's
immediate stays zero and the symbol and signed addend ride the relocation. The
assembler's kinds are object-format neutral and the driver spells them per format:
ELF `R_AARCH64_ADR_PREL_PG_HI21`, `R_AARCH64_ADD_ABS_LO12_NC` and
`R_AARCH64_LDST{8,16,32,64,128}_ABS_LO12_NC` (chosen from the encoded access size,
128 for Q registers); COFF `IMAGE_REL_ARM64_PAGEBASE_REL21`, `PAGEOFFSET_12A` and
`PAGEOFFSET_12L` with the same `:lo12:` source spelling; Mach-O `ARM64_RELOC_PAGE21`
and `PAGEOFF12` (one kind for ADD and every access size) spelled `sym@PAGE` and
`sym@PAGEOFF` (`:lo12:` is refused there, and a bare `adrp sym` is also accepted). A modifier on any other instruction (`sub`, `adds`, `mov`), a
shifted ADD, writeback or post-index addressing, `@PAGE` off
Mach-O, or an instruction the relocation cannot patch (LDUR, LDP) is a structured
diagnostic naming the combination. Out-of-range pages and misaligned scaled offsets
are link-time checks (`object_aarch64_elf_page_relocate` and the PE/Mach-O
equivalents), as with any assembler. On ELF, `adrp Xd, :got:sym` and
`ldr Xt, [Xn, :got_lo12:sym]` are also accepted (#3192), spelling
`R_AARCH64_ADR_GOT_PAGE` and `R_AARCH64_LD64_GOT_LO12_NC` with an exactly zero
addend; `:got:` on any other instruction, `:got_lo12:` on anything but a plain
64-bit `ldr [base, offset]`, and either modifier on Mach-O or COFF are
diagnosed. TLS and codegen-PIC expansion stay with their own owners and
remain refused here. Numeric `:lo12:` expressions on ELF and COFF
are absolute nonnegative immediates without a symbol or relocation. ADD accepts
0 through 4095; load/store offsets must be aligned and fit the encoded 12-bit
scaled field, so their byte offset may exceed 4095. Negative, misaligned and
out-of-range values are diagnosed. Numeric Mach-O `@PAGEOFF` remains
refused. Mach-O unit symbols currently receive the
object writer's C-name underscore prefix on top of the source spelling, as for `bl`.

A global
`.comm` (an ELF common symbol, as `-fcommon` produces) and a `.set` of an
absolute value are refused by name. Widths and alignment follow the target
as in GNU as: on x86-64 `.align N` is N bytes and `.word` is 16 bits; on
AArch64 `.align N` is 2^N bytes like `.p2align`, `.word` is 32 bits, and
`.xword`/`.dword` add 64-bit data. A constant that fits neither the signed nor
the unsigned reading of its directive's width is refused, as llvm-mc does,
rather than truncated. Anything else -- a directive the table
does not claim, or an operand form one of these does not cover -- is a
diagnostic naming the directive and its line, the way every other unsupported
construct here is reported rather than silently dropped.

For x86-64 ELF, `-S` text states what the `-c` object carries so that
reassembling it (here or with GNU as/llvm-mc) gives the same symbol bindings,
initializer order and calls (#1281, partial): a weak definition or undefined
reference prints `.weak`, constructor and destructor priority prints one
`.section .init_array.NNNNN,"aw",@init_array` / `.fini_array.NNNNN` group per
priority (the unsuffixed section last), and an external call prints
`call f@PLT`. A `@init_array`/`@fini_array` section keeps its ELF section type
in this assembler, so priority names reach the linker. A call to a symbol the
unit defines is `R_X86_64_PC32` in `-c` but an assembler always makes it
`R_X86_64_PLT32`.

The same listing keeps the rest of what an assembler cannot infer (#1281):
`.hidden name` after the binding directive of a hidden definition and after
the `.extern`/`.weak` line of a hidden undefined reference, and no label,
`.type` or `.size` for a section symbol (a private zero-value, zero-size
symbol named for its own section: `.text`, `.debug_*`), because GNU as and
llvm-mc already define it and refuse "symbol .text is already defined". A
general-dynamic TLS access keeps its padding as data (`.byte 0x66` before
`lea rdi, [rip + "x"@TLSGD]`, `.byte 0x66, 0x66, 0x48` before
`call "__tls_get_addr"@PLT`), since the linker relaxes the 16-byte sequence by
matching those bytes. `-g` and `-fPIC` listings therefore assemble with GNU as
and Clang to the same section contents, symbol bindings and visibilities, and
relocations as `-c`; the one difference is that a section symbol an assembler
supplies replaces `ctor`-style local references to offset 0. Buster's own
assembler accepts the `-g` listing but still has no `@TLSGD`. The listing is
always Intel syntax: `-S` with `-masm=att` on a C input is refused ("-masm=att
is not supported with -S"), while `-masm=att` with `-c` or with an assembly
input (where it names the dialect the input is read in) is unchanged.
`compiler_driver_test_assembly_x86_64_object_semantics`,
`compiler_driver_test_assembly_x86_64_tls_general_dynamic_padding` and
`object_test_x86_64_elf_listing_metadata` cover this.

ELF `.section .note.GNU-stack,"",@progbits` is an empty nonallocated stack
declaration; `"x"` explicitly requests an executable stack. `@progbits` and
`%progbits` are accepted, repeated declarations preserve any request, and
payload or other flags/types are diagnosed. C and ordinary assembly objects
always emit the nonexecuting declaration. Native linking refuses an explicit
request from assembly, an external ELF object or a selected archive member
with a diagnostic naming that input; it publishes no image. Inputs with no
note remain accepted as nonexecuting. See [object emission](../object-emission.md).

Every section an assembly file declares is its own object section, in
declaration order, on every format: thousands on ELF, up to 65,279 on COFF
(`OBJECT_COFF_MAX_SECTIONS`) and 255 on Mach-O (`OBJECT_MACH_O_MAX_SECTIONS`).
ELF and COFF keep each section's own name; the Mach-O writer spells each one
by its kind (`__TEXT,__text`, `__TEXT,__const`, ...), so several sections may
share a name there while keeping their own contents and relocations. Past a format's limit, `-c` refuses with the
first section that does not fit (`section '.text$f255' is section 256 of 256;
a mach-o64 object holds at most 255 sections`). Only a section whose kind is
an initializer array takes COFF's `.CRT$XCU`/`.CRT$XTX` name.
`compiler_driver_test_assembly_many_object_sections` covers this.

The x86-64 instruction layer accepts the GNU spellings that GCC and Clang
listings and Buster's own `-S` output use, encoding the same bytes as GNU as:
register-immediate `movabs`/`movabsq`; AT&T `retq` and `callq`; `endbr32` and
`endbr64` on every target, since they are hint NOPs without IBT (other CET rows
still require `shstk`); a one-operand shift or rotate (count 1); two-operand
`shld`/`shrd` (count `%cl`); `xchg` with its memory operand in either position;
`rep bsf`/`rep bsr`, GCC's spelling of the TZCNT/LZCNT bytes, on every target;
AT&T `movq` between a general register or memory and an XMM/MMX register; and
a constant before the symbol in a displacement (`8+w(%rip)`). An unsigned
immediate field as wide as its operand takes either interpretation, so
`movb $0xff`, `andb $0xf0`, `xorb $-1` and `mov rax, -2147483649` assemble, and
an all-ones 64-bit literal is the sign-extended -1. Deliberate differences from
GNU as: values outside -2^(w-1)..2^w-1 and negative shift counts are diagnosed
rather than wrapped. A matching instruction shape with an out-of-range immediate
reports `x86 immediate is out of range`; invalid operand types still report an
operand mismatch. A `movabs` value that fits a sign-extended imm32
takes the shorter `mov` row. `ret`/`retq` with an immediate (`c2 imm16`), a
multi-byte `nop` with a register or memory operand (`nopw 0(%rax,%rax,1)`,
`nopl 0x0(%rax)`, `nop %eax`, Intel `nop word ptr [rax + rax]`; always the
`0F 1F /0` encoding) and the `movabs` moffs forms (`movabsq 0x1122334455667788, %rax`
and the store, `a0`..`a3` in every width, Intel `movabs rax, ds:addr`) assemble
to GNU's bytes; a moffs `movabs` forces the moffs row even when the address
would fit a ModRM disp32, and a symbolic address is not accepted. A bare `cs`
or `ds` instruction prefix before an AT&T mnemonic and unsized AT&T `nop mem`
are not accepted (write `%cs:` in the operand and `nopl`).
The short accumulator ALU immediate forms in [#2680](https://github.com/buster14a/buster/issues/2680)
now select GNU's shorter accumulator opcodes when those forms are shortest
(for example, `and al, 1` uses `24 01`). Source `movq %xmm3, %xmm9` chooses
GNU's `F3 0F 7E` form on the equal-length XMM-register tie; machine queries
retain their existing `66 0F D6` form, and memory/GPR/MMX transfers keep their
existing encodings.

The x86-64 metadata completion command is `ide x86_64_completion_census
[--output=<path>]`. Its schema-4 manifest records every generated form row,
the structural result, Intel and AT&T outcomes separately, source reasons,
byte/relocation counts and per-row diagnostics. It also retains escaped
synthesized source text, complete direct and public-assembler byte sequences,
all relocation values and symbols, and the selected x86 metadata form observed
after successful checked emission. Each dialect's source witness is captured
from the ordinary public assembler path; failed source rows retain their
source and diagnostics, with no selected form invented. When run by hosted CI,
the artifact is bound to the tested checkout through the workflow run and
checkout SHA. `source_partition_complete` means every emitted metadata row
has an outcome for each dialect; `source_complete` means every such row is
source-capable in both dialects. The default command exit checks structural,
record, diagnostic, witness and metadata-audit completeness while retaining
known source gaps in the report. `--require-source-complete` adds the strict
per-dialect requirement and needs `--output` so the report is still written
when it fails. The retained witnesses establish what the admitted public
source and encoder paths did for this snapshot; they do not supply an
independent architectural oracle for every form. The broader #2931 issue
remains open for unrelated encoding defects and further proof.

X86 source forms also include baseline `pause`, operand-free `rep nop` (also
`repe nop` and `repz nop`), `prefetcht0`/`prefetcht1`/
`prefetcht2`/`prefetchnta`, `clflush`, and 32/64-bit `movnti` stores. PAUSE is
an unconditional REP-NOP hint; prefetch and cache-flush rows map to the existing
SSE/SSE2 baseline. ADX and PCLMUL metadata aliases use `adx` and `pclmul`.
`rdtscp` is independently enableable with `-mattr=+rdtscp` or `-mrdtscp`, is
included by modern AMD/Intel model profiles, and is detected from extended
CPUID EDX bit 27 for `-march=native`. Its absence is a named feature refusal.

AT&T `cvtsi2sd{l,q}`, `cvtsi2ss{l,q}` and `crc32{b,w,l,q}` suffixes describe
the integer source, independently of the XMM/CRC accumulator width. CRC32 word
sources use the 66 prefix with a 32-bit accumulator; qword sources require a
64-bit accumulator. Register-controlled `bextr` selects BMI1's VEX form; an
immediate control still selects the separately gated TBM XOP form. `-mbmi`
and `-mattr=+bmi` alias `bmi1`. `kmovw` accepts mask/GPR32 and mask/memory16
transfers, and `vpcompressd` accepts a masked memory destination with a dword
element, including AT&T `(%rax){%k2}`. Zeroing a memory destination is refused.

`assembly_test_x86_common_source_forms` checks GNU as 2.47 bytes in both
dialects, public assembly units, baseline feature refusals, Zen 4/Skylake
AVX-512 profiles and malformed neighbours. The driver's
`compiler_driver_test_pause_conversion_round_trip` reassembles FAST/QUALITY
Buster listings with byte-identical text and captured pristine GCC 16.2.1
`-O2 -S` scalar conversions. These bounded witnesses do not claim every
metadata form or arbitrary external listing is source-complete (#2931).
`assembly_test_x86_rep_nop` checks the operand-free REP-NOP aliases and
malformed operands in both dialects; `compiler_driver_test_gcc_pause_listing`
assembles the pristine GCC PAUSE loop with its maximum-skip alignment directives.

On Mach-O, `.section SEGMENT,SECTION[,TYPE[,ATTRIBUTES]]` and
`.pushsection` accept the pairs Buster's listing writer represents:
`__TEXT,__text,regular,pure_instructions`, `__TEXT,__const`,
`__DATA,__data`, `__DATA,__bss,zerofill`, `__DATA,__thread_data`,
`__DATA,__thread_bss`, the `__mod_init_func`/`__mod_term_func` pairs with
`mod_init_funcs`/`mod_term_funcs`, and
`__TEXT,__eh_frame,coalesced,no_toc+strip_static_syms+live_support`.
Known `__DWARF,__debug_*` sections retain their debug identities.
The segment/section pairs normalize to the object's section names, so
`.text` and `__TEXT,__text` reopen the same section. Unknown pairs, types,
attributes and extra operands are structured directive diagnostics.
AArch64 `adrp Xd, sym@TLVPPAGE` and
`ldr Xt, [Xn, sym@TLVPPAGEOFF]` retain the Darwin TLVP page/load relocations,
with zero addends; nonzero signed addends are refused because the platform
linker rejects them. The low half must be a plain 64-bit unsigned-offset
LDR; ADD, stores, other widths and writeback are refused. The Mach-O-only
`@GOTPAGE`/`@GOTPAGEOFF` pair uses the existing GOT-load page/load kinds;
its addend must be zero and its low half must load into a 64-bit integer
register other than XZR. `compiler_driver_test_macho_aarch64_listing_round_trip` compares
serialized text and TLVP/GOT kinds, offsets, addends and symbols with direct
`-c`, for plain functions and defined/undefined TLS in fast/quality modes.

Bare `.section NAME` accepts `.text`, `.data`, `.rodata`, `.bss`, `.init_array`,
`.preinit_array`, `.fini_array`, `.tdata`, `.tbss` and their dot-delimited
suffixes (so `.init_array.00101` keeps its priority; `.preinit_array` takes
none, as `ld` runs only that exact name), exact `.init`/`.fini`, exact
`.tls$` (PE thread-local data, which Windows assembly output prints for both
TLS kinds), and the existing DWARF names (`.debug_info`, `.debug_abbrev`, `.debug_line`,
`.debug_str`, `.debug_loc`, `.debug_ranges`, `.debug_addr`,
`.debug_str_offsets`, `.debug_line_str`, `.debug_rnglists`,
`.debug_loclists`). DWARF sections retain nonallocated object identities
through the driver. Raw-prefix lookalikes such as `.initdata` cannot acquire
flags from the prefix and other bare names are diagnosed.

`.section NAME,"flags",@type` (and `.pushsection`) honours `w`, `x`, `T`
(TLS: `SHF_TLS`, so labels become `STT_TLS`) and the types `@progbits`,
`@nobits`, `@init_array`, `@fini_array` and `@preinit_array` (the last only
for a `.preinit_array` name). `a`, `M`, `S` and `R` are accepted and change
nothing the object stores. Other flag letters (`G`, `e`, `o`, ...), other
types (`@unwind`, ...), array types combined with `x`/`T`/`@nobits`,
`T` with `x`, and reopening a section as or from an array/TLS section with
different flags are diagnosed naming the directive. Still open in
[#1279](https://github.com/buster14a/buster/issues/1279): a section without
`a` (such as `.note.GNU-stack,""`) is still written allocated, and `M`/`S`
merging has no object representation, and `@note` is accepted as an approximation: the section takes the kind its flags select (read-only data for `"a"`) and `SHT_NOTE` is not preserved.
Quoted instruction operands and the broader round trip are tracked in
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
`ldr`-family statements whose address is a label rather than `[...]` select the
control owner's PC-relative LDR (literal) rows.

### AArch64 base instruction vocabulary

AArch64 statements the table-driven owners refuse are retried by the base A64
encoder (`aarch64_base_assembly.c`, entry `a64_base_assemble`), which encodes
constant-operand GNU/LLVM spellings straight to one word. Statements an owner
already accepts never reach it. Its vocabulary is:

- Arithmetic: ADD/ADDS/SUB/SUBS with immediates, shifted registers and extended
  registers, plus CMP/CMN/NEG/NEGS. A negative immediate selects the opposite
  operation, a larger immediate with its low 12 bits clear uses `lsl #12`, and
  an SP operand selects the extended form, all as llvm-mc does. Also
  ADC/ADCS/SBC/SBCS and NGC/NGCS.
- Logical and moves: AND/ORR/EOR/ANDS/BIC/ORN/EON/BICS with bitmask immediates
  or shifted registers, plus TST and MVN. MOV covers register to register,
  to and from SP, immediates (MOVZ, then MOVN, then an ORR bitmask), and
  element/vector moves. Explicit MOVZ/MOVN/MOVK are accepted.
- Bitfield: SBFM/BFM/UBFM; LSL/LSR/ASR/ROR with an immediate or a register;
  SXTB/SXTH/SXTW/UXTB/UXTH; SBFX/UBFX/BFXIL; SBFIZ/UBFIZ/BFI/BFC; EXTR.
- Conditional and other data processing: CSEL/CSINC/CSINV/CSNEG, the
  CSET/CSETM/CINC/CINV/CNEG aliases (AL/NV refused), CCMP/CCMN with a register
  or immediate, UDIV/SDIV/LSLV/LSRV/ASRV/RORV, RBIT/REV16/REV/REV32/REV64/CLZ/CLS.
- Multiply: MADD/MSUB/SMADDL/SMSUBL/UMADDL/UMSUBL/SMULH/UMULH, with the
  MUL/MNEG/SMULL/UMULL/SMNEGL/UMNEGL aliases.
- Single loads and stores: LDR/STR (W/X and B/H/S/D/Q),
  LDRB/STRB/LDRH/STRH/LDRSB/LDRSH/LDRSW and PRFM (named or `#imm` operation).
  Addressing covers scaled unsigned offsets, pre- and post-index, and register
  offsets with LSL/UXTW/SXTW/SXTX. An offset the scaled form cannot hold uses
  the unscaled encoding, as llvm-mc does. The LDUR/STUR family and PRFUM take
  unscaled offsets only.
- Pairs and exclusives: LDP/STP/LDPSW (offset, pre- and post-index) and
  LDNP/STNP. LDXR/LDAXR/STXR/STLXR/LDAR/STLR, each with B/H forms.
- Floating point (`fp-armv8`): FMOV (register, general register including
  `Vn.D[1]`, imm8, and `#0.0` as ZR); FADD/FSUB/FMUL/FDIV/FMAX/FMIN/FMAXNM/FMINNM/FNMUL;
  FABS/FNEG/FSQRT/FRINT{N,P,M,Z,A,X,I}; FCVT; FMADD/FMSUB/FNMADD/FNMSUB;
  FCMP/FCMPE (including `#0.0`); FCCMP/FCCMPE; FCSEL. FCVT{N,A,P,M,Z}{S,U} and
  SCVTF/UCVTF take general registers (with fixed-point `#fbits` where the
  architecture has it) or the AdvSIMD scalar same-size form.
- NEON element moves: UMOV/SMOV/INS/DUP and their MOV aliases.
- AdvSIMD forms compilers emit:
  - MOVI/MVNI and ORR/BIC immediates, including `lsl`/`msl` and the 64-bit
    byte-mask form (`movi d0, #0000000000000000`), and FMOV vector immediates;
  - MVN/NOT and EXT;
  - XTN{2}; SHL, SSHR, USHR, SSRA and USRA (vector, or scalar D);
  - SSHLL/USHLL{2} and their SXTL/UXTL{2} aliases;
  - SADDL/UADDL/SSUBL/USUBL/SMULL/UMULL/SMLAL/UMLAL/SMLSL/UMLSL{2};
  - CMEQ/CMGE/CMGT/CMLE/CMLT against `#0`;
  - single-lane LD1/ST1 (`{ v0.s }[1], [x0]`, optionally post-indexed).
- System: DMB/DSB (named or `#imm` option), ISB, CLREX, BRK/HLT/SVC/HVC/SMC/UDF.
  MRS/MSR accept NZCV, DAIF, FPCR, FPSR, TPIDR_EL0, TPIDRRO_EL0, CTR_EL0,
  DCZID_EL0, CNTFRQ_EL0, CNTPCT_EL0, CNTVCT_EL0, MIDR_EL1, MPIDR_EL1 and
  CurrentEL; MSR refuses the read-only names. Barrier immediates are limited
  to the twelve named options. The generic `s<op0>_<op1>_c<n>_c<m>_<op2>`
  spelling stays with the system-register owner. The system owners' own rows remain gated to the Apple M1
  profile, so on other targets these spellings reach this encoder.
- Feature-gated: half-precision operands need `fullfp16`; LSE
  LD{ADD,CLR,EOR,SET,SMAX,SMIN,UMAX,UMIN}, ST<op>, SWP and CAS (each with
  `A`/`AL`/`L` and `B`/`H` suffixes) need `lse`. Apple M1 has both. Without the
  feature the diagnostic names it.

Writeback whose base register is also a transfer register, and an LDP whose two
destinations are the same register, are operand diagnostics, as in llvm-mc.

Not in this vocabulary, and still refused unless another owner accepts them:

- relocated operands other than the page-address forms documented with the unit
  vocabulary (TLS and further GOT-style modifiers; the control owner handles
  label LDR, and ELF `:got:`/`:got_lo12:` are documented above);
- CASP, LDAPR (RCPC), LDTR/STTR, CRC32 and pointer authentication;
- AdvSIMD forms beyond the list above that the direct SIMD owner does not
  cover. By-element FMLA such as `fmla v0.4s, v1.4s, v2.s[0]` is encoded and
  asserted by `aarch64_base_assembly_tests` (#2932).

`aarch64_base_assembly_tests` checks the encoder against llvm-mc-derived words
for each family, plus refusal and feature-gating controls. The driver round
trip `compiler_driver_test_aarch64_assembly_round_trip` reassembles `-S` output
with frames, calls, arrays, floating point, division and narrowing under every
allocator, and compares its text with direct `-c`. Instructions the printer
still writes as `.word` (#1280) reassemble through the 32-bit AArch64 `.word`.
A corpus-wide differential census against llvm-mc
(26,350 of 26,351 constant lines identical, 0 different, one documented
refusal) was recorded on #2688. Its native, #2467-compliant reimplementation
is tracked in [#2695](https://github.com/buster14a/buster/issues/2695).

Arrangement suffixes and lane indices accept only unsigned decimal
architectural spellings: `v1.-16b`, `v1.0x10b` and `v1.s[0x1]` are operand
diagnostics.

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
targets. Data alignment defaults to zero fill. `.p2align`, `.balign`, and
`.align` accept `alignment[, fill[, maximum_skip]]`, including an omitted
fill (`.p2align 4,,10`). A nonzero maximum suppresses the gap when the required
padding exceeds it; equality permits padding and zero means unlimited. The
section retains its requested alignment even when padding is suppressed.
Maximum-skip expressions must be absolute and in 0..4294967295; malformed or
out-of-range operands are diagnosed. Every branch-relaxation layout replay
recomputes the gap and limit. `.align` keeps its target-specific interpretation:
bytes on x86-64 ELF, a power of two on AArch64.

Three things that layer owns rather than the instruction layer. Local numeric
labels: `1:` becomes a generated name and `1f`/`1b` resolve to the nearest
following or preceding definition in source order, and those names leave the
symbol table again once every reference to one is folded, the way GNU as drops
its own `.L` locals. User-written private names follow the same rule per object
format: on ELF targets a local `.L` label, and on Mach-O a local `L` label, is
dropped from the symbol table unless a relocation still names it (a literal
pool or rodata address reached from another section keeps its symbol, where GNU
as would reference the section symbol plus an addend; the object model cannot
express that, so the symbol stays, typed `NOTYPE`). `.globl`, weak and
undefined names are never dropped. COFF has no verified private prefix, so only
the generated numeric names leave a COFF object and a spelled `.L` label stays.
A plain local label in an executable section is `STT_NOTYPE` on ELF, as GNU as
writes it, so disassemblers do not split a function at it; `.type name,@function`
(or `%function`) gives `STT_FUNC`. An exported (`.globl` or weak) label in an
executable section stays `STT_FUNC` without `.type`: the linker's entry-point
and call checks key on the function kind, and the object reader only infers a
function from an untyped exported label on AArch64. Buster's own `-S` output
spells `.type` for every function symbol, so it is unaffected. A bare section
name used as an expression term (`.long .text - .` in the `.eh_frame` the AArch64
`-S` printer writes) means that section's start, as in GNU as: a name no label,
`.set` or `.globl` defined, equal to a section opened in the unit, becomes a
local `STT_NOTYPE` symbol at offset 0 of it. Same-section differences fold, an
unreferenced one is dropped, and a surviving relocation names this local symbol
(the object model has no section symbol plus addend). A label the file defines
itself, such as the x86-64 printer's `.text:`, is used as written. A repeat or lock prefix alone on a line joins the
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
`cbz`/`cbnz`, `tbz`/`tbnz` and LDR (literal, W/X/SW and S/D/Q destinations)
references using the shared control semantic fixup, including signed addends
and numeric labels. Out-of-range or unaligned references are diagnosed at
their physical source position. Undefined, cross-section, weak, and
default-visible ELF global short branches and literal loads are refused
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
Text-output requests retain macro-expanded line boundaries in an optional
`CPreprocessDetail.output_spacing` sidecar. The raw lexer and replacement
tokens provide leading whitespace before their spelling offsets are replaced;
this preserves `F();`, `F() ;` and `F()/**/;` independently of the expanded
spelling's width. Ordinary compilation does not request or allocate the sidecar.
The lexical separator remains authoritative even when a retained boundary says
adjacent, and untouched source lines keep the source-column recovery path.
Empty replacements and substituted parameters carry pending whitespace to the
next surviving token, including across argument prescan. That shared boundary
is also needed by subsequent stringification; retaining text-output metadata
must never change the string literals compared with ordinary compilation.
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
output rejects `-emit-llvm`, native link inputs, and `-S`. It also rejects
`__attribute__((constructor/destructor))`, naming `wasm32` or `wasm64`; the
direct writer emits no `linking` section to hold `InitFunctions`
(see [Linkage](frontend/linkage.md)).

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
are revisited only when explicitly present again. Parsed native links apply
static `-lfoo`, `-l foo` and `-l:filename` archives at their original CLI
positions among source, object and directly named archive inputs. A found
library has the same extraction eligibility as that archive named directly
at the same position. Repeated occurrences remain separate requests; a later
object can introduce demand for an explicit repeat, but cannot implicitly
revisit an earlier occurrence. Global `-L` roots remain shared search state.
Definition binding strength does not alter eligibility once a selected
definition exists.

The parser stores this order in `CompilerDriverInvocation.link_operations`,
with indices into the existing input and library arrays; per-file `-x`
selections remain attached to the input array. A nonzero stream must cover
all files and library occurrences exactly once, retaining each array's
order, or execution diagnoses an invalid invocation before reading inputs.
The parser reserves one operation record per expanded argument; the native
consumer walks it without another allocation. API-built invocations with a
zero `link_operation_count` retain the explicit legacy order of all inputs
followed by all libraries. Non-link output actions retain their prior order.
Group and whole-archive requests remain unsupported. Shared-symbol
interposition retains its existing behavior.

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
linked object. An unindexed ELF member counts every global with a non-zero section
index, reserved `SHN_ABS`, `SHN_COMMON` and `SHN_XINDEX` included, as a definition,
exactly as a ranlib index does. The member is selected in archive order and the full
reader refuses it with member and symbol attribution; the link does not report an
unattributed unresolved symbol or fall through to a later member. Unindexed Mach-O
and COFF members count their forms of the same definitions -- external `N_ABS`,
`N_INDR` and `IMAGE_SYM_ABSOLUTE` symbols, and commons (an undefined external with a
non-zero value) -- the same way (#1243).

An unindexed archive is accepted, unlike GNU ld's "archive has no index", but
every member's symbol table must be readable. Selection walks members in archive
order, so skipping an unreadable member could let a later member satisfy a symbol
the skipped one defines first and silently change the link. One unreadable
member therefore rejects the whole archive, even when the link would select only
other members: `object_archive_read_link` sets `failed_member` and a diagnostic
naming the member, and the driver reports `could not read archive <path>: member
<name>: unreadable symbol table in an archive without a symbol index`. Running
`ranlib` over the archive defers the member's validation to selection, as for any
indexed archive (#1319).

`compiler_driver_archive_test_lazy` exercises all three object formats, 32/64-bit
GNU and BSD indexes, BSD extended names, unindexed input, transitive dependencies,
no-selected-member archives, a required incompatible member, duplicate providers,
weak references and repeated occurrences.
`compiler_driver_archive_test_unindexed_unreadable_member` covers the unindexed
refusal through the reader and both positional and `-l` driver inputs, with an
indexed control that selects the valid member and leaves the bad one unread. A separate valid ELF `R_X86_64_SIZE64`
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
a later directory to satisfy the request. Files without that recognized
foreign shared header retain the existing export-discovery refusal. The probe
does not validate the complete foreign object. Other target search policies
are unchanged.

`compiler_driver_archive_test_default_roots`, invoked by the registered lazy
archive fixture, checks both ELF CPUs and all six literal sysroot roots,
named/exact/direct image parity, distinct provider precedence, explicit `-L`,
shared preference, incompatible shared headers beside and before usable
archives, exact archive bypass and output preservation on refusal.
Its configured native Linux control builds a real archive with host compiler
and archiver, links an independent host control, and runs both Buster's direct
and sysroot-default named links.

Native x86-64 and AArch64 Linux ELF image links also accept direct `.so`
and versioned `.so.N` file operands (numeric version components), including the full paths returned by
CMake's `find_library`. Classification uses the per-input language snapshot:
`-x c`, `-x cpp-output` and `-x assembler` select source even with a
`.so` suffix; `-x none` restores DSO admission. Automatic `.so.c`,
`.so.i` and `.so.s` inputs keep their source phases.
DSOs retain their position among `-l` occurrences
and form a serial translation-unit boundary. The driver reads exactly the
supplied path; `-L` and default roots cannot substitute for a missing or
incompatible direct input. The ordinary ELF export/version reader supplies
imports, and `DT_NEEDED` uses `DT_SONAME` when present, otherwise the
operand's original spelling. Repeated identities are emitted once.
The `-v` `buster-native-ld` line includes each original file operand.

Direct DSOs are refused for compile-only, preprocessing, syntax-only,
assembly, LLVM/GPU output, foreign/non-ELF targets and the in-memory
retained-object route. Missing, malformed and target-incompatible files
fail the link with the supplied path and preserve existing output.
`compiler_driver_library_path_inputs` checks both ELF targets and allocators,
SONAME/no-SONAME identity, versioned paths, mixed `-l` order, duplicates,
linker scripts and refusals. `compiler_driver_library_path_native` compares
native links and execution against the configured GCC/Clang.

For requested Linux shared libraries, located GNU linker scripts support
`INPUT(...)`, `GROUP(...)` and `AS_NEEDED(...)`, including nested commands,
commas, quoted filenames, whitespace and C block comments. Members name ELF64
shared libraries or further scripts: bare filenames, absolute paths and `-lX`
(including `-l:FILE`). Bare members search beside the script, then the current
directory, then the existing explicit/default ELF roots. Named members use the
existing ELF roots. Absolute members are prefixed with the sysroot when their
script is inside that sysroot; `=` and `$SYSROOT` force that prefix, following
[GNU ld's file-command rules](https://sourceware.org/binutils/docs/ld.html).
Explicit `-L` paths retain their literal meaning.

Expansion uses an explicit worklist in member order, limited to 32 script files
on a dependency chain, 32 nested command levels, 4,096 total script members per
invocation, and 65,536 bytes per script. Cycles reach the same depth/count refusal;
they never recurse on the C stack. DSO identity comes from `DT_SONAME` when
present, with filename spelling retained otherwise; repeated identities create
one dynamic-library entry. Worklist and expanded-library storage is allocated
only when a script is encountered.

This is a shared-library subset: archive/object members are refused, including
glibc's `libc_nonshared.a`, rather than dropped or admitted as shared objects.
`GROUP` preserves member order without archive rescanning. `AS_NEEDED` includes
its members unconditionally, just like `INPUT`; it does not implement GNU ld's
as-needed selection. Other commands, including `OUTPUT_FORMAT`, `OUTPUT_ARCH`
and `SEARCH_DIR`, remain unsupported. Unsupported syntax, archive members and
bounds failures retain `unsupported GNU linker script <path> requested by
-l<request>`; missing DSOs retain the original request's missing-library error.
A later DSO cannot hide the first located script. Automatic runtime lookup and
the existing special treatment of `-lc` are unchanged.

`compiler_driver_test_elf_linker_scripts` covers both ELF CPUs, normal/exact
requests, single/multi-input links, member forms, comments, SONAME identity,
nested scripts at/beyond the cap, cycles, parser byte/member/command-depth bounds,
unsupported grammar, first-request error ordering and preserved output on refusal.
On native Linux with a configured GCC or Clang driver, a host-style
`INPUT(-lncursesw)` fixture compares its `DT_NEEDED` identity with GNU ld.

On macOS and iOS, `-lNAME` searches explicit `-L` roots in order, probing
`libNAME.tbd` before `libNAME.dylib` within each root, then
`<sysroot>/usr/lib` when a sysroot was supplied. A compatible thin or fat
Mach-O dylib contributes its `LC_ID_DYLIB` install name and current and
compatibility versions to `LC_LOAD_DYLIB`; TAPI YAML TBD v1-v4 and JSON TBD v5 stubs provide
the same metadata, and wrong-CPU or wrong-platform candidates are skipped.
Mach-O dylib platforms come from `LC_BUILD_VERSION` and `LC_VERSION_MIN_*`;
TBD v4 `targets:` entries must match both architecture and platform suffix,
while TBD v1-v3 `archs:` entries match the CPU and an optional top-level
`platform:` narrows the platform. Located versions are written verbatim,
including zero; absent TBD current/compatibility versions default to 1.0.0,
while a present malformed version makes the stub unusable. `-lc` and `-lm` keep
their existing implicit behavior. Without a located candidate, only the
allowlisted Apple system libraries retain the legacy `/usr/lib/libNAME.dylib`
fallback; other requests fail with `cannot find -l<request>`. TBD v5 reads only
`main_library`: `target_info[].target` must match both CPU and platform, and
`install_names`, `current_versions` and `compatibility_versions` honor their
optional `targets` lists. Inlined `libraries` do not supply the identity.
The JSON scan validates the complete document with a fixed 64-frame stack;
malformed/truncated input and deeper nesting are unusable. Ordinary metadata
strings borrow mapped bytes; escaped strings/keys are decoded into the caller
arena, including Unicode surrogate pairs. Invalid Unicode metadata and embedded
NUL are refused. Ambiguous duplicate identity fields
and overlapping target-specific metadata are refused. `compiler_driver_test_mach_library_search` covers parser bounds,
architecture selection, search order, versions, fallback, refusal and archive
interaction on every host without running the cross-linked image.

Dependency names passed through `-l NAME` or `-framework NAME` must be
nonempty. An explicitly empty operand is a `driver.argument` error naming the
option before any source is read or output is published; response files and
embedding invocations follow the same rule. A missing operand retains its
separate `missing argument after OPTION` diagnostic. Empty `-I`/`-L` path
policy is independent. `compiler_driver_test_empty_dependency_names` covers
macOS/iOS, single/multiple inputs, an empty request followed by a missing name,
argv/response/API calls, and preservation of existing output.

`-framework NAME` searches every explicit `-F` root in order, then
`<sysroot>/System/Library/Frameworks`, or `/System/Library/Frameworks` when
no sysroot is supplied. Within each root it probes `NAME.framework/NAME.tbd`
before `NAME.framework/NAME`. The same Mach-O/TAPI CPU and platform checks
apply; unusable candidates are skipped. YAML TAPI stubs may describe AArch64
libraries as `arm64e`; this ARM64 CPU subtype is accepted for ordinary AArch64
consumers, while macOS/iOS platform matching remains exact. Unknown architecture
spellings and other CPU families remain incompatible. The located file supplies the install
name and both versions, including zero, rather than the search path. Duplicate
install names produce one load command. A framework without a compatible file
fails with `framework not found: NAME` before output publication, preserving
any existing output. SDK-less links also require a real file: there is no
framework-name allowlist or guessed load path. Supply `-F` or a sysroot for
cross links. `compiler_driver_test_mach_framework_search` covers synthetic
framework trees for macOS and iOS on every host, including both single-source
and multi-input link paths, without executing the foreign image. On native
macOS, the same fixture resolves the active SDK through `xcrun` and records
`MACH_FRAMEWORK_SDK` candidate paths, formats, sizes, statuses, install names
and packed versions for AppKit, Metal, QuartzCore and Foundation. Every one
must have a compatible identity; the Release self-host gate also links and
executes against these real framework identities.

ELF executable data placement honors both page and requested object alignment.
Align the final virtual address, not only its file offset: an initialized
global may require alignment larger than a page or the fixed image base.
The object writer already carries that requirement into the section metadata
(GitHub #225).

Every hosted ELF link reads the shared libraries' own dynamic symbol tables.
Undefined ELF `STT_NOTYPE` references carry explicit `undefined_untyped`
provenance in `ObjectSymbol`; TLS identity is independent. A typed reference
wins over an untyped one in either object order. Both ELF object writers keep
untyped assembly references `STT_NOTYPE`, even when a call relocation proves
their callable use. Before any Linux or Android image writer, the selected
library definition's `st_type` classifies an import: `STT_FUNC` and
`STT_GNU_IFUNC` are functions, and `STT_OBJECT` remains data even without a
storage-table entry. An untyped address reference with no usable definition,
or a definition of unknown type, fails with its symbol and input named;
it cannot become a guessed copy relocation. A call relocation proves use,
not the providing definition's type: missing target exports refuse untyped
calls and address references alike. Recognized `__tls_get_addr` companion
calls retain the existing dynamic-loader ABI contract, including calls the
writer relaxes away.
An absent weak reference still follows the existing zero-resolution rule.

Android reads `libc.so` and explicitly requested libraries through the same
export reader. Its roots use `x86_64-linux-android` or
`aarch64-linux-android` under the supplied sysroot, plus its generic
`lib64`, `usr/lib64`, `lib` and `usr/lib` roots and explicit `-L` paths.
Native Android also searches `/system/lib64`; cross Android links never
search host GNU libc roots. Supply an API-specific NDK library directory
with `-L` when needed. Missing target exports retain the refusal above.

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

Native x86-64 Linux links also supply GCC's CPU-feature runtime (GitHub
#3399). GCC and Clang `target_clones` resolvers and `__builtin_cpu_init`,
`__builtin_cpu_supports` and `__builtin_cpu_is` call `__cpu_indicator_init`
and read `__cpu_model` / `__cpu_features2`. Those are hidden definitions in
the static `libgcc.a` (`cpuinfo.o`, with its own `.init_array` constructor),
which the host compiler driver links implicitly as `-lgcc`. After objects,
explicit archives and the ELF runtime object are selected, a remaining strong
undefined reference to one of these names that no selected object defines
makes `compiler_driver_elf_cpu_runtime` read `libgcc.a`: first from the `-L`
directories, then the newest `usr/lib/gcc`, `usr/lib64/gcc` or Debian
`usr/lib/gcc-cross` `<triple>/<version>/` directory under the sysroot, or the
host without one (`compiler_driver_elf_libgcc_path`). Only the members that
define those names are selected; libgcc.a is not otherwise added to the link,
so the libgcc_s helper policy above is unchanged. Links without such a
reference, including weak ones, do not read or name libgcc.a. A missing
archive, or one that does not define the name, fails the link with the archive
and symbol named, before replacing the output.

The same policy covers AArch64 Linux (GitHub #3508), with a target-keyed name
set and GCC triple list (`aarch64-linux-gnu`, `aarch64-unknown-linux-gnu`,
`aarch64-redhat-linux`, `aarch64-suse-linux`); the x86-64 names and triples
are unchanged. Every AArch64 name below is a hidden definition that only the
static `libgcc.a` provides; `libgcc_s.so.1` exports none of them, so they
belong here rather than in the libgcc_s helper policy:

- GCC 10+ and Clang default to `-moutline-atomics`, so `__atomic_*` builtins
  call `__aarch64_<op><bytes>_<order>` helpers: `cas` (1, 2, 4, 8 and 16
  bytes) and `swp`, `ldadd`, `ldclr`, `ldeor`, `ldset` (1, 2, 4 and 8 bytes),
  each with `relax`, `acq`, `rel`, `acq_rel` or `sync`
  (`compiler_driver_elf_outline_atomic_symbol`). Each helper member references
  `__aarch64_have_lse_atomics`, so `lse-init.o` and its `.init_array` LSE
  probe are selected with it.
- GCC 14+ and compiler-rt FMV resolvers use `__aarch64_cpu_features`,
  `__init_cpu_features_resolver` and `__init_cpu_features` (`cpuinfo.o`, with
  its own constructor). GCC 13 and older `libgcc.a` has no `cpuinfo.o`, so such
  a link fails with the archive and symbol named, as the host link does.
- `__builtin___clear_cache` calls `__aarch64_sync_cache_range`
  (`sync-cache.o`).

The selected members reference `getauxval` or `__getauxval`, which bind to
glibc like any other import. `compiler_driver_elf_aarch64_cpu_runtime_tests`
checks these links against the host link on AArch64 Linux hosts.
`compiler_driver_elf_aarch64_cpu_runtime_selection_tests` covers selection,
refusal, and per-target keying from any Linux host. SME runtime entry points,
Android, and non-Linux targets remain out of scope.

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
inputs of that invocation with the position-independent code model.
The last of `-fPIC`, `-fpic`, `-fPIE` and `-fpie` selects the requested
model; `-fno-pic` clears it, while `-fno-pie` cancels only a PIE spelling.
On x86-64 and AArch64 ELF the positive spellings select the implemented PIC
reference model (see the position-independent code bullets in
[machine.md](machine.md)). On AArch64 ELF the model makes `-fPIC` objects
acceptable to `ld.lld -shared -z text`; Buster's Linux `-shared` and `-pie`
writers still exist only for x86-64. Android AArch64 executable links use the
AArch64 PIC model automatically. Thread-local access under AArch64
ELF PIC emits the general-dynamic descriptor sequence and its four TLSDESC
relocations. Independent platform linkers can build shared objects from these
objects; Buster's fixed-address AArch64 linker relaxes the descriptor to
local-exec, using the same path as foreign TLSDESC objects (see the
AArch64 TLS paragraph below). Cancellation, preprocessing,
syntax-only and assembly/prebuilt-only input routes retain their behavior.
Mach-O and COFF keep their existing target models; Wasm/eBPF compatibility
behavior is unchanged. LLVM-bitcode and direct backend model requests remain
an audit residual of issue #1289.
On x86-64 and AArch64 Android every executable link is a PIE, with or without
`-pie` (`-no-pie` cannot select another image there): the kernel places
bionic's executable at a load bias, so a fixed-address image marked `ET_DYN`
would run its preinit entries, IFUNC runner and every data pointer
unrelocated. The writers name `/system/bin/linker64` and `libc.so`; their
entry stubs do not register a loader finalizer, because bionic's `linker64`
does not provide the glibc x86-64 RDX finalizer contract or an AArch64 X0
finalizer. `-shared` stays refused for Android. `android/runtime_order.sh`
executes an x86-64 image on the CI emulator (see the Android CI notes in
[testing.md](testing.md)).
On any other target a link that asks for either image is refused as an
unsupported option, while a compile-only invocation ignores the link option,
as GCC does.

`-fvisibility=default|hidden|internal` (the last wins) sets the visibility of
definitions that carry neither a `visibility` attribute nor an active
`#pragma GCC visibility`; it never changes a plain `extern` declaration, as in
GCC. `internal` is emitted as hidden. `-fvisibility=protected` and any other
value are argument errors, and a GPU target ignores the option. The bit
reaches `st_other` on ELF only; Mach-O and COFF objects do not record it.

The default fixed-address dynamic executable
(`link_native_executable_elf64_x86_64_dynamic`, which the AArch64 Linux and
Android dynamic writers also build on) is hardened the way GNU ld's default is
(#2720). It has three loads: R (headers, `.interp`, `.dynstr`/`.dynsym`/
`.hash`/version tables, `.rela.plt`/`.rela.dyn`, `.rodata`, `.eh_frame_hdr`,
`.eh_frame`), R+X (the entry stub, `.text`, `.plt`) and RW. The RW load starts
with the `PT_GNU_RELRO` range, `.got.plt` then `.dynamic`, padded to a page
boundary because the loader seals whole pages; `.data`, TLS and `.bss` follow.
`DT_FLAGS` `DF_BIND_NOW` and `DT_FLAGS_1` `DF_1_NOW` request eager binding, so
the sealed range covers the PLT slots too. No page is both writable and
executable. Each load starts on a page boundary, so images grow by up to three
pages of zero padding against the earlier single R+X load. The static
(no-import) writers have no GOT and keep their single R+X load; separating
their `.rodata` is not done yet. `link_test_elf_hardened_layout` checks the
structure on both machines without `readelf`.

`-static` follows the same split on every target: `-c`, `-S`, `-E` and
`-fsyntax-only` ignore it, and a link refuses it as
`unsupported option: -static (...)` because no image writer produces a
static executable; hosted ELF links import `libc.so.6` dynamically. A
configure probe that links with `-static` therefore learns the truth instead of
receiving a dynamic executable (GitHub #2851).

`-nostdlib`, `-nostartfiles` and `-nodefaultlibs` follow the same split: `-c`,
`-S`, `-E` and `-fsyntax-only` accept and ignore them, as GCC does when nothing
is linked, and a link refuses the first one named as
`unsupported option: -nostdlib (...)`. Buster implements none of their link
semantics (a link without the C runtime start-up files or default libraries), so
they are never silently ignored where they would matter (GitHub #1418).

`-include FILE` and `-includeFILE` (GitHub #1418) process each file as
`#include "FILE"` before the first line of every C and assembly translation
unit, in command-line order, after `-D`/`-U` have been applied. The names become
`#include` lines of a synthetic `<command-line>` frame stacked above the
primary file's frame (`forced_includes` in `CPreprocessOptions`), so the
primary file's `__LINE__`, `__FILE__` and diagnostic lines never shift. Lookup
approximates GCC: the working directory first (the synthetic file's directory
is `.`), then the `-I` chain, then builtin headers; the primary file's
directory is not searched. An absolute name is opened directly. A missing file
is the ordinary `included file was not found: FILE` diagnostic located in
`<command-line>`, and the compilation fails. A name with a double quote or line
break is refused, as is a joined spelling that begins with `-`
(`-include-pch` and `-iprefix` stay unsupported options), and `-include` with a
GPU target is refused because the external GPU pipeline does not forward it.

### Deliberately rejected GCC/Clang spellings

Each row is covered by a driver test. A spelling is refused, never ignored
silently, when ignoring it would change what the user asked for. Open requests
are tracked on GitHub #1418.

| Spelling | Result | Reason |
|---|---|---|
| `-nostdlib`, `-nostartfiles`, `-nodefaultlibs` when linking | `unsupported option: -nostdlib (...)` (first one named) | Link semantics (no C runtime start-up files or default libraries) are not implemented. With `-c`, `-S`, `-E` or `-fsyntax-only` they are accepted and ignored, as GCC and Clang do. |
| `-nostdlib++`, `-nostdlibs`, `-nolibc` | `unsupported option` | Not spellings of the above. |
| `-mfoo`, `-mno-foo` (no architecture has the feature) | `unsupported option: -mfoo` | `-m<feature>` takes only the names `-mattr` takes. |
| `-mavx2` for a non-x86 target or a GPU target | `unsupported option: -mavx2` | The feature belongs to another architecture, or the GPU pipeline has no feature overrides. |
| `-mred-zone`, `-mno-red-zone` | `unsupported option` | They name no target feature and have no implementation. |
| `-mAVX2`, `-mavx2=1`, `-m`, `-mno-` | `unsupported option` | Feature names are exact lower case; `=` forms belong to `-march=`, `-mcpu=`, `-mtune=`, `-mattr=`, `-masm=`. |
| `-mno-sse2` | `unsupported option: -mno-sse2 (SSE2 is part of the x86-64 baseline)` | Every x86-64 target here requires SSE2. |
| `-xc++` and other unknown joined `-x<lang>` | `unsupported language: c++` | Same language names as `-x <lang>`; C is the only source frontend. |
| bare `-x` | `missing argument after -x` | As GCC and Clang. |

`link_native_image_elf64_position_independent` writes the supported
position-independent images as an ET_DYN at base zero. Its orientation comment
is the contract; in short:

- An Android AArch64 PIE shares the address-zero ELF layout and uses
  `EM_AARCH64`, `/system/bin/linker64`, and `libc.so`. Its `R_AARCH64_RELATIVE`,
  `GLOB_DAT`, and `JUMP_SLOT` relocations use ABI types 1027, 1025, and 1026.
  The hosted stub omits the X0 loader-finalizer registration; imported calls
  use PLT thunks, direct page/PREL references bind locally or through copy
  slots, and imported function-address references without GOT indirection are
  refused. Linux AArch64 PIE/shared output remains unsupported.
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
- A shared object exports every defined default-visibility symbol (hidden ones,
  from `__attribute__((visibility("hidden")))`, `#pragma GCC visibility` or
  `-fvisibility=hidden`, stay out of `.dynsym`; see "Symbol visibility" in
  [linkage](frontend/linkage.md)), leaves
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
AArch64 ELF shared objects, PE DLLs and Mach-O dylibs have no writer yet.

`compiler_driver_test_initial_exec_tls` serializes independent MOV-form ELF
fixtures using rax and r8, then links and executes them as fixed-address and
PIE images. Available GCC/Clang compilers add default `-O2`, `-O0 -fno-pie`
and `-O2 -fPIC -ftls-model=initial-exec` objects, with host-linked controls and
initialized/zero TLS reads before and after mutation. Malformed MOV sites
fail without replacing output. The driver emits its `-fPIC` hint only when
the ELF planner identifies a refused fixed-address relocation; generic
relocation failures, including malformed TLS sites, do not imply that cause.

AArch64 Linux writes only fixed-address executables (`-shared` and `-pie` are
refused), so its thread-local access is always resolved at link time, and
foreign initial-exec and TLS descriptor objects are relaxed to local-exec as
GNU ld and lld do for static executables (issue 2582). Initial-exec
(`R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21` 541 and
`R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC` 542) `adrp xN` / `ldr xN, [xN]`
becomes `movz xN, #tprel[31:16], lsl #16` / `movk xN, #tprel[15:0]`. The
general-dynamic descriptor sequence (`R_AARCH64_TLSDESC_ADR_PAGE21` 562,
`_LD64_LO12` 563, `_ADD_LO12` 564 and `_CALL` 569) `adrp x0` / `ldr Xt, [x0]`
/ `add x0, x0` / `blr Xt`, which Clang and GCC emit as one adjacent run,
becomes `movz x0` / `movk x0` with the same halves, then two `nop`s. Both use
the variant-I offset local-exec uses (`link_aarch64_elf_tprel_offset`; the
words come from `object_aarch64_elf_tls_ie_relax` and
`object_aarch64_elf_tls_desc_relax`). The reader accepts RELA entries whose
words are exactly those instructions (the immediates are canonicalized to
zero, x0 where the ABI fixes it, a scratch `Xt` of x1..x30) and refuses any
other word by relocation name. `link_aarch64_tls_relax` heap-sorts the sites
by section and offset, then scans them once. A descriptor sequence is consumed
whole: each step must sit four bytes after the previous one, in order, with one
symbol and addend and one scratch register for its LDR and BLR. An initial-exec
pair may be non-adjacent, since Clang 18 at `-O2` schedules loads between the
halves: each section keeps a table of pending ADRPs keyed by destination
register, and an LDR (base register equal to its destination, 64-bit) pairs
with the nearest preceding ADRP of that register in the same section, which
must name the same symbol and addend. Each ADRP is consumed by exactly one
LDR; a second ADRP for a register that is still pending, an LDR without a
partner, two LDRs sharing one ADRP, a register, symbol or addend mismatch, or
an ADRP left unpaired at the end of its section refuses the link. The sort is
O(n log n). The halves may enclose at most `LINK_AARCH64_TLS_IE_GAP_MAX` (64)
words, and every one of them must be proven harmless for `xN` or the link is
refused: it must match a row of `link_aarch64_inert_forms` (load/store
register in its unsigned-immediate, unscaled, pre/post-index and
register-offset forms, GPR and FP/SIMD; load/store pair; ADD/SUB immediate,
shifted and extended register; logical shifted register and immediate, the
latter with an allocated bitmask; MOVN/MOVZ/MOVK; ADR/ADRP; SBFM/BFM/UBFM;
UDIV/SDIV and variable shifts; MADD/MSUB and the long and high multiplies;
CSEL/CSINC/CSINV/CSNEG; `mrs Xt, TPIDR_EL0`; NOP), each narrowed to its
allocated size/opc/option combinations, and none of the general-register
fields that row names (Rt/Rd, Rn, Rt2/Ra, Rm; FP/SIMD data registers are not
general registers, a writeback base is) may equal N. Field value 31 is SP or
XZR and never N, since the reader limits N to x0..x30. Branches of every
kind, exception, barrier and other system instructions, any other MRS or MSR,
and any unlisted or unallocated word refuse the link
(`link_aarch64_tls_ie_gap_inert`). The pairing scan is therefore O(n + total
gap). All of it runs against a defined thread-local symbol with a 32-bit
offset and fails the link before the image is written, as `link.relocation` naming the symbol and the relocation that
could not start or complete its sequence. The enforced contract is that,
along the straight-line words from the ADRP to its LDR, nothing observes the
GOT page the ADRP produced or replaces it before the LDR consumes it, so the
`movz`/`movk` pair computes the offset the original pair loaded. A branch
from elsewhere into those words is outside what the linker can see; it is
safe whenever the incoming path, as the original LDR requires, carries the
page from a GOTTPREL ADRP of the same symbol and addend, because every such
ADRP becomes the same `movz Xn, #hi` and every matching LDR the same
`movk Xn, #lo`. The register-overlap forms the architecture calls CONSTRAINED
UNPREDICTABLE (for example a writeback base equal to its transfer register)
are accepted only when none of their registers is N; their permitted
behaviors change only the named registers or raise UNDEFINED at that word.
TLS descriptors stay strictly adjacent. The other descriptor forms (560/561
and the 565-568 `OFF_G1`/`OFF_G0_NC`/`LDR`/`ADD` sequence), the dynamic
`R_AARCH64_TLSDESC` 1031, and TLS owned by a loader or shared library are
still refused by name. The tests (the "initial-exec TLS (#2582)" and "TLS
descriptors (#2582)" blocks of `object_tests`,
`link_test_aarch64_tls_initial_exec_relaxation` and
`link_test_aarch64_tls_descriptor_relaxation`) check encodings, including
adversarial reads, writes, base uses, branches, unrecognized words and
oversized gaps between non-adjacent halves.
`compiler_driver_test_aarch64_elf_tls_initial_exec` runs only on Linux
AArch64 hosts: it compiles `fixtures/aarch64_elf_tls_initial_exec.c` with the
host compiler at `-O2 -ftls-model=initial-exec`, requires a non-adjacent
541/542 pair in the object, links it with Buster, checks the relaxed
`movz`/`movk` words and compares the output with a host-linked control.
`compiler_driver_test_aarch64_elf_tlsdesc` runs on Linux AArch64 hosts: the
configured host compiler builds `fixtures/aarch64_elf_tlsdesc.c` with `-fPIC
-ftls-model=global-dynamic` at `-O0` and `-O2`, Buster links it with a
Buster-compiled main defining the other thread-local, and the test checks
every descriptor appears relaxed and the image prints the same values as a
host-linked control.

## Native linker identity

`ide cc -Wl,--version` and `ide cc -Xlinker --version` print
`Buster native linker` followed by the effective target triple and exit
successfully without reading source or standard input, compiling, spawning an
external tool, or writing an artifact. `-march=x86-64 -O0 -g` may accompany the query. The independent
`-m64` driver capability is tracked by
[#3458](https://github.com/buster14a/buster/issues/3458). The typed invocation API exposes
`COMPILER_DRIVER_QUERY_LINKER_VERSION`; execution returns the identity in
`CompilerDriverResult.output`.

The query requires a native x86-64/AArch64 link action and a supported native
OS. The raw linker request must contain exactly `--version`; other linker
arguments, compiler queries, compile-only actions, bitcode and GPU pipelines
are refused. A SONAME operand named `--version` retains its ordinary meaning.
The identity makes no GNU ld/LLD compatibility or invented version claim.
Buster currently links in process and has no external-linker selector.

For a native Linux x86-64 or AArch64 ELF link, raw linker `-v` from
`-Wl,-v` or `-Xlinker -v` requests the native linker's identity while allowing
the link to continue. It is consumed only in option position; operands to
`-soname`, `--soname`, `-h`, `-z` or `--version-script` are preserved. Other targets and output
modes continue to reject it. With driver `-v`, the driver also prints a
` buster-native-ld` link-report line containing the output, explicit and
target/sysroot library search roots in lookup order, command-line object,
archive and shared-library inputs, and requested libraries. This is a report
label for Buster's in-process link, not an executable name or linker choice;
it does not claim GNU ld or LLD identity. CMake's
`CMakeDetermineCompilerABI` / `CMakeParseImplicitLinkInfo` consume this
link-line shape to discover Linux multiarch implicit library directories.

Meson versions without a Buster linker detection/class implementation still
reject this truthful identity. Native query support alone does not qualify
Meson/QEMU configuration: upstream recognition and tests against unmodified
Meson are required under [#3475](https://github.com/buster14a/buster/issues/3475).

### Standalone CMake ABI discovery reduction

`tools/cmake_abi_discovery/CMakeLists.txt` is a standalone reduction, not a CI
test. Compare its `discovery.txt` output with Clang and Buster in separate
build directories. For Clang:

```sh
cmake -S tools/cmake_abi_discovery -B out-clang -DCMAKE_C_COMPILER=clang
cmake --build out-clang
cat out-clang/discovery.txt
```

With CMake 3.29 or newer, point CMake directly at `ide` and make `cc` the
compiler argument:

```sh
cmake -S tools/cmake_abi_discovery -B out-buster \
  -DCMAKE_C_COMPILER=/path/to/ide -DCMAKE_C_COMPILER_ARG1=cc
cmake --build out-buster
cat out-buster/discovery.txt
```

For older CMake, use a wrapper whose contents are `exec /path/to/ide cc "$@"`,
then configure with `-DCMAKE_C_COMPILER=/path/to/wrapper`. Compare pointer
size, library architecture, implicit directories, linker identity, and the
paths found for z and X11.

## Pass-through options

`-Wl,a,b,c` produces three individual linker arguments, in order. Each
`-Xlinker value` contributes exactly one argument; commas in that value are
not split. Empty comma fields and missing operands fail with a diagnostic.
The driver and native linker share `link_validate_linker_arguments`, and the
linker checks the actual static/dynamic image before publishing output.

When no link happens (`-c`, `-S`, `-E`, `-fsyntax-only`), `-Wl,...` and
`-Xlinker` arguments are accepted and ignored, as GCC does and Clang does with
an unused-argument warning (#4041); build-system probes such as
`-Wl,--enable-new-dtags -c` therefore succeed. The same arguments are still
validated on a link, and a leading `--version` stays a linker query that a
non-link action refuses.

Accepted and ignored options (no effect on output):

- `-fcolor-diagnostics`, `-fno-color-diagnostics`: the driver never colors
  diagnostics, so both spellings are presentation-only no-ops (#4041).
- `-fcomment-block-commands=<names>`: a Clang doc-comment option that only
  feeds `-Wdocumentation`, which is not implemented; an empty list is refused
  (#4041).

The supported subset is:

- Linux and Android dynamic ELF executables: `-E`, `-export-dynamic`, and
  `--export-dynamic` export definitions. A static image refuses these options.
- Linux and Android ELF links: `--no-undefined`, `-no-undefined`, and the
  two arguments `-z defs` require strong references to resolve. Executables
  already enforce this rule; on a shared object it overrides loader resolution.
- x86-64 Linux driver links: `--as-needed` and `--no-as-needed` select
  subsequent `-l` requests and automatic-language direct `.so` / `.so.N`
  operands in input order (the default is no-as-needed). Explicit `-x` source
  inputs retain their selected language. A DSO
  is retained when it satisfies a strong unresolved reference from preceding
  objects, or from a previously retained DSO when the candidate is absent
  from every retained DSO's DT_NEEDED list (matched by SONAME identity).
  DSO-only dependencies already named there remain indirect; a regular-object
  demand still keeps them. Later objects and weak references do not keep it. The driver consumes these switches before the native image API,
  which receives the selected library list and still refuses raw policy flags
  without an ordered driver request. GNU linker-script `AS_NEEDED` remains
  unsupported.
- x86-64 Linux ELF links: `-O1` permits the native writer's existing layout and
  hash policy; it does not request generated-code optimization. `--sort-common`
  is accepted because there are no admitted common symbols to sort: the object
  reader continues to refuse `SHN_COMMON`, and assembly `.comm` definitions
  are already ordinary zero-filled definitions.
- x86-64 Linux executables, PIEs and shared objects: `-z relro`, `-z now` and
  `-z noexecstack` request RELRO, eager binding and a non-executable stack.
  The fixed-address dynamic writer already seals its GOT and binds eagerly.
  `-z now` extends a PIE/shared object's sealed range to include its PLT GOT
  and emits both NOW flags. These requests select a dynamic executable even
  without source imports, so their metadata is present on trivial programs.
- x86-64 Linux ELF links: `-z pack-relative-relocs` converts eligible PIE/shared
  `R_X86_64_RELATIVE` relocations to `DT_RELR` with direct addresses and bitmaps,
  storing their implicit addends in the target words. Unaligned and zero-fill
  locations retain RELA, as do symbolic, TLS, COPY and IRELATIVE relocations.
  glibc's `GLIBC_ABI_DT_RELR` version dependency is recorded and must be present
  in the selected libc's version definitions; an incompatible libc is refused.
  Fixed-address executables have no load-bias-relative relocations to pack.
  The existing layout reservation stays as padding; this promises RELR
  semantics, not a smaller artifact or a compiler-throughput improvement.
- x86-64 Linux executables, PIEs and shared objects: `-rpath DIR`,
  `--rpath DIR`, `-rpath=DIR` and `--rpath=DIR` write a runtime search path.
  Repeated paths are joined with `:` in command-line order. The default and
  `--enable-new-dtags` write `DT_RUNPATH`; `--disable-new-dtags` writes
  `DT_RPATH`, and the last dtag switch wins. Loader tokens such as `$ORIGIN`
  are preserved literally (quote them in the shell). Each image's loader
  expands `$ORIGIN` relative to that image. This is a runtime path, separate
  from `-L` library discovery at link time; `-rpath-link` remains refused.
  Missing or empty operands and unsupported targets fail before publication.
- x86-64 Linux shared objects: `-soname NAME`, `--soname NAME`, `-h NAME`,
  `-soname=NAME`, and `--soname=NAME` set `DT_SONAME`.

- x86-64 Linux executables, PIEs and shared objects: one
  `--version-script=PATH` or `--version-script PATH` applies a GNU version
  script. The file must be regular, at most 1 MiB, and contain one anonymous
  or named node: `[NAME] { [global: SYMBOL; ...] [local: SYMBOL; ...] };`.
  Clause order is flexible; each clause may occur once. Exact unquoted names
  use ASCII letters, digits, underscores, dots and dollar signs. The local
  clause also accepts one `*;` catch-all; exact global names override it.
  Whitespace, `#` line comments and `/* ... */` comments are accepted.
  At most 8,192 distinct exact names are accepted. Conflicting exact
  global/local rules, multiple scripts/nodes, inheritance, quoted names,
  language blocks, other patterns and malformed/trailing input fail before
  publication. Missing exact names are permitted, as with GNU ld.
  Local definitions become local in `.symtab`, leave `.dynsym`, and bind
  inside shared objects. Input-hidden/local symbols cannot be promoted and
  undefined imports are not localized. A named node assigns default version
  index 2 to its exported exact globals, emits base/node `.gnu.version_d`
  records and `DT_VERDEF/DT_VERDEFNUM`, and shifts needed import versions
  above index 2. Unmatched exports remain unversioned. The base record names
  the SONAME or output basename. Executable export selection still follows
  `--export-dynamic` and library references; the script limits that selection.
  Anonymous nodes change binding/export visibility without defining a version.
  This covers pristine SDL3's single-node map and executable CMake probe;
  broader GNU grammar and other targets remain unsupported.

Other linker values and unsupported targets/output modes fail rather than
silently losing link semantics. This includes PE, Mach-O, UEFI, compile-only,
preprocessing, assembly-text and bitcode output. `-rdynamic` uses the same
export validation. Unknown options, entry overrides, wrapping, archive-mode
switches, runtime paths, and other `-z` modes are refused.

`-Wp,` and `-Wa,` are never warning options. Only the exact
`-Wp,-MD,PATH` and `-Wp,-MMD,PATH` dependency spellings described below are
supported; other pass-through requests, including macro/include operations,
are refused. The one accepted `-Wa,` value is `--noexecstack` (alone or as a
comma-separated list of it): every Linux and Android ELF object already ends
in an empty non-allocated `.note.GNU-stack` marking a non-executable stack
unless an assembly input requests `"x"`, so the option changes nothing, for
`-c`, `-S` and `-E` alike. Any other `-Wa,` value, including `--execstack`,
an empty item and `-Wa,` alone, is refused with `unsupported pass-through
option`.

## C90 dialects, `-P` and `-Qunused-arguments`

`-std=c89`, `-std=c90`, `-std=iso9899:1990` and `-ansi` select a strict ISO C90
preprocessing dialect (`C_PREPROCESS_DIALECT_C89`), separate from the existing
`-std=gnu89`/`-std=gnu90`. Like the other strict dialects it defines
`__STRICT_ANSI__`, leaves out the GNU `linux`/`unix` macros and the GNU
keywords (`typeof`, `asm`), and replaces trigraphs by default. Like `gnu89` it
leaves `__STDC_VERSION__` undefined and defines `__GNUC_GNU_INLINE__`, with
GNU inline semantics for `inline`. `-ansi` is exactly `-std=c90`; the last of
`-ansi` and `-std=` wins. `-std=iso9899:199409` and `-std=c88` stay refused.

The dialect is a preprocessing and keyword switch only. The frontend has no
C90 language-rule switches, so `//` comments, mixed declarations and
statements, and compound literals and other C99 constructs that GCC and Clang
diagnose or reject under `-std=c90 -pedantic` are accepted. What the dialect
supplies is the predefined-macro and header-selection behavior real C90
projects (SDL's tests, parson) depend on, not a conformance check.

`-P` (suppress linemarkers) and `-Qunused-arguments` (suppress Clang's
unused-argument warning) are accepted for every action. The `-E` serializer
never writes linemarkers, so `-P` already holds, and no unused-argument
warning exists to suppress. Direct `-D`, `-U`, and `-I` remain available.
Standalone `-M`, `-MM`, and `-MG` remain unsupported.

## Macro filename prefix maps

`-fmacro-prefix-map=OLD=NEW` records literal, case-sensitive prefix
substitutions for C and preprocessed assembly. Empty OLD or NEW is allowed;
the last `=` separates them, and the last matching option wins once, following
GCC. Matching is a byte prefix, without a directory-boundary requirement,
canonicalization or repeated application to the replacement.

The preprocessor applies the mapping when expanding `__FILE__` and
`__BASE_FILE__`; `__FILE_NAME__` takes its basename first. Logical `#line`
names can match too. GCC-style directive operands retain their original
filenames, so mapping cannot redirect `#include __FILE__` or mutate a
`#line __FILE__` operand. Diagnostics, source maps, include identity/lookup,
dependency prerequisites, output paths and debug information retain their
original names. GPU forwarding is explicitly refused. The independent
`-ffile-prefix-map`, `-fdebug-prefix-map` and `-fcanon-prefix-map` options
remain unsupported.

GCC 15.2 [prefix-map implementation](https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/file-prefix-map.cc)
and [file builtins](https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/libcpp/macro.cc)
define the ordering, separator and directive policy.
Clang 21.1 [language option parsing](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.0/clang/lib/Frontend/CompilerInvocation.cpp)
and [mapping](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.0/clang/lib/Basic/LangOptions.cpp)
agree for the single directory-to-empty Kbuild mapping, but select overlapping
prefixes in reverse lexical order, retain the first duplicate prefix and split
the first `=`. These differences
are explicit oracle cases, not an assertion of universal GCC/Clang equivalence.
The registered `compiler_driver_test_macro_prefix_map` and
`compiler_driver_test_macro_prefix_map_files` cover the contract.
Set `BUSTER_PREFIX_MAP_ORACLES=1` on hosted Linux to additionally require
independent installed GCC and Clang preprocessing of the matrix and includes.

## Dependency files

`-MD` and `-MMD` write a Make-compatible depfile for C `-c` output. The default
depfile path comes from the object path by removing only the basename's last
extension and appending `.d`; `-MF` overrides it. `-MT` adds a raw target,
`-MQ` adds an escaped target, and the default target is the escaped object path.
Multiple target options retain command-line order, including repeats and empty
fragments. Parsing retains raw/quoted fragments and joins them once with checked
sizing; target storage and copying grow linearly with argument and output bytes.
`compiler_driver_test_dependency_target_scaling` bounds actual parser arena growth
at 128 and 2,048 targets, directly and through response files. `-MP` adds phony rules for
header prerequisites. `-MF`, `-MT`, `-MQ`, and `-MP` require `-MD` or `-MMD`;
explicit `-MF`/target options cannot be used with multiple inputs.

Prerequisites come from resolved preprocessor include identities, in
first-inclusion order; the main source comes first unless compiling stdin.
Repeated identities appear once, builtin headers and `<command-line>` do not
appear, and forced includes follow the main source. `-MMD` excludes headers
found in system include paths and all headers included from a system header.
Makefile-special spaces, tabs, dollar signs, and `#` characters are escaped.
Paths containing a newline or carriage return cannot be represented and fail
with a file-write diagnostic.
The depfile is atomically published only after successful compilation and
object publication, so a failed compile preserves existing object and depfile
contents.

The bounded Linux Kbuild/BusyBox spellings `-Wp,-MMD,PATH` and
`-Wp,-MD,PATH` reuse that collector and writer for exactly one native
x86-64/AArch64 C input. They support `-c` and `-S -o FILE`; assembly must
name its output file, so stdout output is refused. The path must be nonempty,
contain no comma, newline, carriage return or NUL, and cannot be `-` or
alias the source or artifact. Repeated pass-through dependency requests,
additional comma-separated operations, direct `-MD/-MMD/-MF/-MT/-MQ/-MP`
modifiers, retained in-memory objects, libraries and unsupported output modes
are refused before compilation. Direct dependency options retain their C
`-c` contract; this does not enable direct `-MD -S`.

The default escaped target is the actual object or assembly output path,
matching Clang's handling of these pass-through spellings. GCC uses the
source basename with an `.o` suffix instead; this target-name difference is
explicit. Linux Kbuild's `fixdep` supplies the real build target separately.
A depfile is published only after its named artifact succeeds. A failed
source or artifact write preserves the prior depfile; failure to publish the
depfile reports failure even if the artifact has already been published.

## Warning options

The warnings the driver publishes today are the preprocessor's (`#warning`,
extra tokens after a directive such as `#endif junk`), the GNU obsolete
`member: value` designator in a strict ISO dialect, and semantic analysis's
signedness-only pointer conversions (`-Wpointer-sign`, #4051). Each belongs to a group
(`CompilerDriverWarningGroup`, mapped from the diagnostic kind by
`compiler_driver_warning_group_of_kind`), and `compiler_driver_publish_c_diagnostics`
applies one `CompilerDriverWarningPolicy` to them for every input, serial or
batched, including those whose source was replayed from the source cache.

| Group | Spellings | Warning |
|---|---|---|
| `cpp` | `-Wcpp` (GCC), `-W#warnings` (Clang) | `#warning` |
| `extra-tokens` | `-Wextra-tokens` (Clang), `-Wendif-labels` (GCC, which names only `#else`/`#endif` there) | tokens after a directive |
| `gnu-designator` | `-Wgnu-designator` (Clang; GCC files it under `-Wpedantic`) | `member:` designator |
| `pointer-sign` | `-Wpointer-sign` (GCC and Clang) | implicit pointer conversion whose pointees differ only in integer signedness |

Options apply left to right, as in GCC and Clang, and the last one that names
a group or the global flag wins:

- `-w` suppresses every warning, so nothing is promoted either, whatever the
  order.
- `-Wno-<group>` drops that group; `-W<group>` and `-Werror=<group>` enable it
  again.
- A parent name acts on each of its members: `gnu` covers `gnu-designator`
  and `everything` (Clang) covers every group, so `-Wno-everything`,
  `-Wno-gnu` and `-Werror=gnu` behave as if each member were named, in the
  same left-to-right order. As in Clang, `everything` is only a `-W`/`-Wno-`
  name: `-Werror=everything` and `-Wno-error=everything` name nothing and are
  ignored, and `-Wno-everything` is sticky, so a later `-Weverything`
  re-enables nothing while a named group (`-Wcpp`, `-Werror=cpp`) still does.
- `-Werror` makes every enabled warning an error, including one with no group;
  `-Wno-error` undoes it.
- `-Werror=<group>` promotes one group and `-Wno-error=<group>` exempts one.
  An explicit per-group choice outranks the global `-Werror`/`-Wno-error` in
  either order (`-Wno-error=cpp -Werror` leaves `#warning` a warning).

A promoted warning is published with error severity and the option that
promoted it (`... [-Werror=cpp]`, `[-Werror=extra-tokens]`,
`[-Werror=gnu-designator]` or `[-Werror=pointer-sign]`; a warning without a
group gets `[-Werror]`). The unit fails with the tokenizer error (the analysis
error for `pointer-sign`), so `-c`, `-S`, `-E`, `-fsyntax-only`, a link
and a batch of several inputs all return failure and write no output; the
failed-unit rules above decide which later results are discarded. Its record
in `CompilerDriverResult.diagnostics` and the per-input `error_count` are those
of an error, and `tokenizer_warning_count` no longer counts it. The count
still includes a warning that `-w` or `-Wno-<group>` dropped, as it did for `-w`
before this policy existed; only the promoted ones are subtracted.

Every other `-W...` spelling is still accepted and ignored: `-Wall`, `-Wextra`,
and names that are neither a group nor a parent of one (`-Werror=unused-variable`,
`-Werror=` and `-Wno-` neither enable nor promote anything). Diagnosing unknown or unimplemented names is a
separate decision (#1574). Semantic analysis has one warning channel,
`CParseResult.warnings` (carried to `CIRLowerResult.warnings`), which only
`-Wpointer-sign` uses: the driver publishes it ahead of the unit's errors for
`-fsyntax-only` and for every lowering action, counts it in
`CompilerDriverResult.analysis_warning_count` (less promoted ones, as for
`tokenizer_warning_count`), and fails the unit with
`COMPILER_DRIVER_ERROR_ANALYSIS` when `-Werror` promotes one. Other parser and
lowering findings remain errors, so signed-overflow in constant expressions is
not yet a warning that `-Werror` can promote.

`-Wpointer-sign` is on by default, as in Clang (GCC enables it only with
`-Wall` or `-Wpedantic`); `-Wall` is accepted and changes nothing. The driver
has no `-pedantic-errors` mode, so only `-Werror` and `-Werror=pointer-sign`
make it an error. The frontend contract (which conversions belong to the class)
is in [Calls](frontend/calls.md#signedness-only-pointer-conversions--wpointer-sign).

Under a bare `-Werror` the GNU obsolete designator fails the build, as it does
in Clang (where `-Wgnu-designator` is on by default); GCC accepts the same
source silently.

## Source debug information

`ide cc` omits source debug information by default. Pass `-g` to emit it or
`-g0` to disable it explicitly; when both occur, the last option wins.
Other debug levels and formats such as `-g1` remain unsupported. This keeps
ordinary compilation focused on time to an artifact: it avoids constructing
source debug models and their larger object payloads unless requested.
`-g` selects DWARF 4 for native ELF/Mach-O targets and CodeView for Windows
objects. Unwind information remains independent of source debug information.

Direct `wasm32-wasip1`, `wasm64-unknown-freestanding`, and `bpfel-unknown-linux`
output does not emit source debug information. An enabled `-g` request fails
with a named `driver.argument` diagnostic before reading source or publishing
an artifact. The typed invocation API rejects `debug_info=true` too, regardless
of `debug_info_explicit`. Default output, `-g0`, and `-g -g0` remain supported;
`-g0 -g` is refused. Preprocessing and syntax-only actions accept `-g` because
they do not emit a debug artifact. `compiler_driver_test_nonnative_debug_options`
covers these refusals, option ordering, unchanged existing output, and identical
default/disabled-debug bytes.

A linked Windows image's PDB (`pdb_build`) gives its TPI stream a hash stream:
one `hashTypeRecord` bucket per record modulo `0x3ffff` (`pdb_type_record_hash`)
and a sparse type-index offset entry per 8 KiB of records. DbgHelp (cdb,
WinDbg, `symchk`, `dbh`) faults with `0xc0000005` loading a PDB whose TPI
stream names no hash stream (#3411). The regression is `pdb_test_tpi_hashes`.
The globals and publics streams index each module's top-level procedures
(`S_PROCREF`/`S_LPROCREF` naming the module-stream record) and data (copies of
`S_GDATA32`/`S_LDATA32`); global ones also get an `S_PUB32` and an entry in the
publics address map, sorted by section and offset (`pdb_gsi_emit`). With these
empty, DbgHelp loads the PDB only as "public symbols & lines". The regression is
`pdb_test_global_indexes`. Bucket ordering retains the format hash and name
comparison, with stable insertion sort capped at 16 entries and stable bottom-up
merge sort for larger buckets (#3737). One scratch array sized to the largest
bucket is reused, bounding comparisons to O(N log N) even for repeated-hash
names. `pdb_test_gsi_sorting` checks comparison bounds and stable ties;
`pdb_test_gsi_colliding_indexes` checks both emitted indexes. The existing
mixed ASCII/non-ASCII comparator can be non-transitive (#3752); for such inputs the
large-bucket merge order is deterministic but may differ from insertion order.
No comparator or on-disk format change is made.

CodeView names a frame-resident location with `S_DEFRANGE_REGISTER_REL`
against RBP (x86-64) or X29 (AArch64), the base codegen normalizes FRAME offsets
to (`codeview_emit_frame_relative`). `S_DEFRANGE_FRAMEPOINTER_REL` is not used:
it needs an `S_FRAMEPROC` record this producer does not emit, and cdb and RAD
drop it without one. A frame-resident piece of a split variable uses the same
record with the `spilledUdtMember` flag and its 12-bit offset in the parent; a
piece past that offset stays unavailable. Consecutive line entries for one
source line become one C13 row, so a `file:line` breakpoint resolves to a
single range. Regressions: `codeview_test_long_location_range`,
`codeview_test_line_ranges`, `codeview_test_pointer_sizes` and the CodeView
consumer in `codegen_test_machine_debug_locations`.

With `-g`, named scalar locals that need more than one defining instruction to
describe stay in distinct frame slots, so their location lists follow stores
through reassignment and control-flow joins. A frame location starts after its
first entry-block store. A local first stored on a later control-flow path
gets must-initialized ranges from the published CFG
(`machine_debug_initialized_build`): the whole of each reachable block that
every incoming path has written, else the rest of a block after its own first
write. A write on only some paths therefore never reaches the join, and
uninitialized frame bytes are never exposed. These per-block ranges are
organized in machine layout order and emitted as one debug value for each
layout-adjacent run; a partial-start block starts a new run. Machine blocks
are published in selection layout order, with each block's line marks forming
a contiguous row group, so adjacent block spans meet at a row boundary.
Replaying the run therefore preserves the same seeds as separate block
replay followed by `codegen_canonical_location_append`. Past
`MACHINE_DEBUG_INITIALIZED_CELL_LIMIT` block-local cells, or once the
per-block range budget `MACHINE_DEBUG_INITIALIZED_RANGE_LIMIT` is exceeded,
the remaining locals stay unavailable. The CodeView symbol reserve counts
every location range (`CODEVIEW_DEFRANGE_RECORD_BYTES`), and a range longer
than the 16-bit `S_DEFRANGE_*` length is emitted as consecutive records of at
most `CODEVIEW_DEFRANGE_MAX_LENGTH` bytes rather than clamped. Regressions:
`codegen_test_debug_local_seed_coverage`,
`machine_test_debug_value_initialized_blocks`,
`machine_test_debug_value_initialized_budget`,
`codeview_test_many_location_ranges` and
`codeview_test_long_location_range`. A
local stays SSA only when its sole write is its entry initializer and that
initializer is an instruction result no other local already names. This costs
code in debug builds: the target keeps the remaining memory locals in frame
slots and FAST/QUALITY frame layout does not coalesce these frame objects over
disjoint lifetimes. `-fno-pinned-debug-locals` is an explicit opt-out;
`-fpinned-debug-locals` requests the default behavior explicitly. The regression
is `compiler_driver_test_debug_scalar_local_locations`.

This default also applies when compiler-driver arguments are parsed for an
embedding caller. The typed invocation API uses its `debug_info` field
explicitly; a zero-initialized field disables debug output. Release/Debug
configuration of the Buster compiler executable does not change these source
compilation options. The self-host recipes pass `-g` explicitly.

The registered driver regression checks the default, both individual switches
and both orders across x86-64/AArch64 ELF, COFF and Mach-O objects. It reads the
serialized artifacts, requires debug payloads only in the enabled modes, and
checks that code/data bytes are unchanged and the default matches `-g0` exactly.

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

COFF object reads merge same-kind contributions into initialized file-backed
storage. Alignment gaps and tails introduced by empty aligned sections contain
zero bytes even when reader arenas are reused; BSS remains virtual-only.

### Large static initializers

Static constant-initializer contexts (`c_ir_constant_initializer_bytes`) are
sized from the by-value nesting depth of the initialized type
(`c_ir_initializer_nesting_depth`), not from the token count, so the length of
a flat table does not limit them. Driver regressions
(`compiler_driver_test_large_static_initializers`) compile with `-c` a
1,000,000-element `unsigned char` array, a 250,000-entry `const char *` table
and a 200,000-entry struct array. Semantic analysis no longer sizes its
initializer work by the tokens of the whole braced list: the typed-constant
task stack in `c_parse_typed_constant` grows on demand, and the array-count
inference frames and continuations are also bounded by the type count.
`c_test_parse_large_static_initializers` analyses a 230,000-entry
`struct { int a; const char *s; short v[3]; }` table (#3254) and a
4,000,000-element byte table, both of which aborted on the 256 MiB thread
scratch before. Measured with `ide cc -c`: 8,000,000 bytes take 3.2 s and
471 MB; 1,000,000 entries of that struct take 15 s and 2.7 GB, so time, not a
reservation, is the next limit. The mobile builds of the driver fixture use
smaller counts to keep their deadlines. A reservation that cannot be carved
is a positioned `initializer working storage exceeds the scratch reservation`
or `initializer nesting exceeds its capacity` diagnostic and a failed result.

Automatic aggregate initializers lower through the nested compound-literal
machine, which zero-fills each brace-enclosed element before storing its
members. Its scratch is per element and released with it: the zero-value task
stack grows with the widest path through the type (`c_ir_emit_zero_value`),
the cursor stack follows the type's nesting depth, and the unit-lifetime query
stack gets its own mapping when it would leave lowering less than half of the
thread scratch. `c_test_ir_local_table_zero_scratch` lowers a 20,000-entry
local table that previously exhausted the scratch (#2527). A task stack that
cannot grow is the diagnostic
`aggregate zero-initializer exceeds the lowering scratch reservation`.

### Large functions in the backend

Machine selection, placement, scheduling and encoding of one function share
the thread scratch, which is 256 MiB by default. `codegen_machine_scratch_bound` plans the
function's need from its IR instruction, value and block counts before
selection. The plan has a linear term several times the measured peak (about
290 bytes per machine row under FAST, 410 under QUALITY) and a term for the
FAST allocator's block-by-register liveness planes. When the bound exceeds
what is left of the thread scratch, `codegen_machine_scratch_begin` installs
private reservations of that size as the thread's scratch arenas for this one
function, so nested pass scratch grows with it too, and restores the originals
afterwards. Ordinary functions, including every function of the self-host,
never take this path. A bound past 256 GiB, or a reservation the system
refuses, fails the function with `kind=capacity reason=machine-scratch`
instead of an arena validation abort (#3413).
`c_test_large_function_machine_scratch_runtime` compiles and runs a
5,000-entry local table, about 555,000 machine rows in one block, under both
allocators; `codegen_test_machine_scratch_plan` checks the plan, the
replacement and its restoration. Backend time, not memory, is the next limit
for such functions: 128,000 and 256,000 `sink += x + i;` statements in one
function take about 10 s and 42 s.

Windows ARM64 stores function length in 18 bits of four-byte instructions. Larger functions therefore use aligned `.pdata`/`.xdata` fragments strictly shorter than 1 MiB; epilogs remain whole, their scopes are relative to each fragment, and continuation records carry the parent's unwind state through `end_c`. A registered large C fixture exercises compilation, linking, and execution across those fragment boundaries on native Windows ARM64.

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
generation with FAST or QUALITY;
preprocessing, syntax-only and direct non-native output reject the flag.
Successful compilation prints a versioned `CODEGEN_VERIFY` line with module,
selected-function and scheduled-function counts and the effective allocator.
Normal compilation keeps its existing validation certificates and fast paths.
The [native differential runner](../differential-testing.md) consumes this
explicit opt-in evidence and compares executable observations independently.

The x86-64 System V mixed-object stack fixture checks aggregate and floating
returns and live dynamic allocations in both compiler directions. Its exact
sixteen-byte allocation-distance check is specific to Buster's caller lowering
and is guarded by `__BUSTER__`; host callers retain the value and live-byte
checks without assuming their `__builtin_alloca` layout.

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

A lone `-` is an input naming standard input, as for GCC and Clang. It has no
suffix to classify, so it needs `-x c` or `-x cpp-output`, or `-E`, which reads
it as C source; without either, or under another language, the parser refuses
it, and it may appear only once. The source text travels in
`CompilerDriverInvocation.standard_input`: the `cc` command reads standard
input to EOF into it after parsing, and API callers fill it themselves. A null
pointer there fails the input as a read error. Diagnostics and `__FILE__` name
the input `-`, and `-c` without `-o` writes `-.o`, as Clang does.

The embedding facade instead sets the API-only
`CompilerDriverInvocation.input_source` for one C input. A non-null pointer
means the bytes are used directly and `input_paths[0]` supplies only the
virtual filename for diagnostics, `__FILE__`, and quoted-include lookup; the
driver does not open that path and assigns no filesystem identity. An empty
source is valid when its pointer is non-null. `retain_object` is another
API-only field: for a single `-c` input it keeps `result.object` in the caller's
arena and suppresses file output. The public embedding module owns the arena
through object serialization or JIT release.
`compiler_driver_test_probe_spellings` covers the admission rules, both routes
and the `-static`/`-mtune` spellings (GitHub #2851).

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
bounded too. A missing or unreadable file, or a directory (refused by path
kind without being opened, so no platform logs an open failure for it), is a
`driver.file-read` error, `could not read response file <path>`, which
`ide cc` prints after `cc: error:` before exiting nonzero. Expanded arguments
are NUL-terminated copies in the invocation arena.
`compiler_driver_test_response_file_arguments` covers the grammar and bounds;
`compiler_driver_test_response_file_batch` checks that a 400-input `-c` batch
writes the same objects through `@file` as on the command line.

## Opt-in source lex cache

`-fsource-cache` requests one bounded 16 MiB raw translation/lex cache for the
current invocation; `-fno-source-cache` cancels it (last wins, default disabled).
Use it for serial multi-input builds; separate CLI processes start empty.
Embedding callers can retain their own `CSourceCache` across serial invocations
through `CompilerDriverInvocation.source_cache`. Cache presence clamps TU workers
to one. `-v` prints a versioned `SOURCE_CACHE` record separately from conceptual
SOURCE input metrics. Include resolution, preprocessing, semantics, canonical
IR validation, backends and publication run fresh. See
[bounded raw source reuse](../source-lex-reuse.md), including ownership and
qualified-host performance acceptance, which remains pending.


## Native fixture-derived AArch64 assembly census

`./build.sh test_aarch64_assembly_census --ide build/Release/ide` compiles the
sorted `tests/*.c` fixture set for AArch64 Linux at O0, O1 and O2, records
producer failures and include exclusions, and compares the deduplicated constant
instruction rows with `llvm-mc` through `llvm-objdump`. Symbolic and nonconstant
rows are explicit exclusions and need the separate relocation suites. This is a
fixture-derived correctness census, not a full ISA census or runtime test.

Use `--clang`, `--llvm-mc`, `--llvm-objdump`, `--out` and `--timeout-seconds`
to pin tools and retain evidence. The output directory must be new. Process,
capture, fixture, instruction and elapsed-time bounds fail closed. Unknown
assembler refusals, observer failures, empty comparisons and encoding differences
fail; every successful row partition accounts for each row as a comparison or an
unknown refusal. Producer exclusions are reported as `PASS_WITH_EXCLUSIONS`, which
does not establish coverage for the excluded C fixtures. No refusal is exempt:
the by-element FMLA spelling once documented by #2688 is encoded since #2932.

`./build.sh test_aarch64_assembly_census --self-test` runs deterministic native
subprocess controls for refusal detection, observer failures, empty and
malformed disassembly, raw-word parsing and unequal encodings. The hosted workflow
retains source/tool/binary identities, listings, constant rows, object files,
disassembly and the report on success or failure.

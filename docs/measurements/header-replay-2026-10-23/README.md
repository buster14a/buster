# Header inclusion transcript experiment, 2026-10-23

This is the isolated experiment for [#111](https://github.com/buster14a/buster/issues/111).
The measured source is Buster `635434f2a4d153c6de1dd2f1149055c4507c6f46`.
No transcript cache is enabled in the production compiler by this snapshot.
The locked 9700X comparisons reject both the initial layout and the refined
read-column layout: neither repays recording, validation and complete
provenance replay. Native corpus equivalence passed, but the refined layout
slows the musl batch by 3.17–5.18% and Lua by 20.30–21.89%. Retain this
reproducible negative result and leave the production preprocessor unchanged.

## Final 9700X decision

The [compact read-column variant](soa/README.md) implements the proposed
separate identifier and exact-definition columns. Compared with the first
layout it removes cold location metadata from validation and reduces musl's
retained transcript storage from 96,150,694 to 84,930,294 bytes. This improvement
still does not make replay worthwhile.

The final boundary is one contiguous interval per batch. Every pass constructs
and releases a fresh file cache and intern table; candidate odd passes also
construct, populate and release an initially empty transcript cache. It charges
the common token-digest consumer. OS page caches are not flushed. Startup,
option parsing and once-per-process discovery remain outside the timer; this
is a multi-unit preprocessing comparison, not a full compiler invocation.

| Input / process round | Original ms | Hooks, replay off ms | Fresh replay ms | Replay versus original |
| --- | ---: | ---: | ---: | ---: |
| musl / 1 | 412.759 | 416.971 | 431.124 | +4.45% |
| musl / 2 | 415.382 | 417.655 | 428.558 | +3.17% |
| musl / 3 | 405.610 | 414.171 | 426.641 | +5.18% |
| Lua / 1 | 30.318 | 30.857 | 36.791 | +21.35% |
| Lua / 2 | 29.943 | 30.818 | 36.497 | +21.89% |
| Lua / 3 | 30.242 | 31.158 | 36.379 | +20.30% |

The three rounds alternate process order. Each process has eight passes;
original medians use passes 1–7, hooks-off use 2/4/6, and fresh-replay medians
use 1/3/5/7. The median of an even count averages its middle two values.
Across all musl observations used for those medians, original batches span
404.967–416.687 ms and replay batches 426.138–433.009 ms. Lua spans
29.414–31.802 ms and 35.885–37.212 ms, respectively. There is no gain beyond
spread: all replay observations are slower. The
[process medians and ranges](final-process-medians.tsv) and
[native logs](raw/final-target/) retain the full observations.

Musl's process peak RSS increases from about 32.6 MiB to 116.8–117.0 MiB;
Lua increases from about 33.0 MiB to 43.1 MiB. Musl still retains 1,100
transcripts, with 1,908 hits, 1,163 eligible misses, 2,374,363 identity checks,
565,716 recorded reads and 318,205 writes per fresh candidate batch. Lua has
165 transcripts, 1,520 hits, 569 eligible misses, 43,336 checks and 4,244,642
retained bytes. Header-guard skips are not counted as replay attempts.

The primary preprocessing gate is sufficient to reject the candidate under
#111's stated alternative: document numbers showing that reuse is not worth
retaining. The supplemental parser/100 ms conditioning matrix was stopped
after this failure, withdrawing its final still-unacquired Lua parser request;
completed supplemental logs are retained,
but there is no claim of a complete matrix, power-state qualification or
successful full compilation. No acquired command was interrupted and CI
retained priority. A future design needs new end-to-end evidence rather than
inheriting an acceptance claim from this correct but slower prototype.

## Per-header diagnostics and input identity

The final distribution pass is explicitly **local diagnostic evidence**, from
the Ryzen 9 7940HS with Clang 23.1.3 and the same frozen source/selected inputs.
It uses `-O1 -g -march=native -fwrapv -funsigned-char -fno-strict-aliasing
-Wall -Werror`, one CPU at niceness 19 with idle I/O priority, a 30-second
per-command bound, and no timing observations. See the complete
[musl CSV](raw/local-metrics/musl.csv) and [Lua CSV](raw/local-metrics/lua.csv).

| Diagnostic input | Transcripts | Header path spellings | Maximum variants per file-cache record | Read-set min / median / 90th percentile / max |
| --- | ---: | ---: | ---: | ---: |
| musl | 1,100 | 184 | 64 | 0 / 283 / 1,083 / 5,280 |
| Lua | 165 | 76 | 7 | 1 / 9 / 176 / 1,160 |

The CSV's `variants` counts the file-cache record, including alternate path
spellings; the path-spelling count is not a count of distinct physical files.
The 90th percentile is the nearest rank. Rows retain each transcript's read,
write, output, range, scratch and provenance sizes. Musl's aggregate hits,
misses, reads, writes, checks and retained bytes match the native fresh-batch
observations exactly. Local Lua records 11,366 reads, 6,458 writes and
4,244,514 retained bytes, versus the native 11,368 / 6,460 / 4,244,642; installed
headers differ, so those local distributions are not target qualification.

Continuous CI arrivals delayed the optional target metadata pass, which was
withdrawn along with the unacquired supplemental parser command. Native
performance and corpus qualification were already complete. The native
`status.txt` / `controller.status` therefore record intentional exit 143 from
that waiter; this is not a failed acquired correctness or performance run.
No target command or CI job was interrupted while holding the host lock.

The [musl input hashes](raw/final-target/musl-inputs.sha256) and
[Lua input hashes](raw/final-target/lua-inputs.sha256) identify all 1,564 / 199
resolved path spellings from the original native first-round file graphs.
All original/candidate initial path sets match across the three rounds.
They were captured after the experiment as low-priority file reads, using each
corpus's working directory. They are not a completed final graph comparison.
The abandoned hash attempt from the experiment root could not resolve relative
paths and was discarded; `input-hash-note.txt` records the corrected capture.

## Native equivalence qualification

The compact layout was built on the 9700X with Clang 23.1.1, `-O3 -g
-march=native -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror`.
Each build, check and measurement acquired its own shared lock; timings were
pinned to CPU 2. Frozen source identities, binary hashes and build dependencies
are retained with the native logs.

| Native input, three passes each | Unit runs | Output tokens |
| --- | ---: | ---: |
| musl, selected static-libc C units | 3,945 | 15,013,785 |
| Lua 5.4.8 | 102 | 1,764,198 |
| Zig 0.14.1 libc, all 8,983 C/header inputs in groups of 512 | 26,949 | 36,297,048 |
| #109 seed 111, 500 C17 programs | 1,500 | 152,619 |
| #109 seed 111, 500 C23 programs | 1,500 | 151,446 |
| Location-copy scalar twin, same C17 set | 1,500 | 152,619 |

All comparisons passed for the four output columns, complete provenance
arrays and runs, expansion starts, diagnostics, macro dumps, and both output
printers. Equally rejected malformed or foreign-target inputs count as
equivalence, not as valid C or independent Clang acceptance. Native checks also
passed 8,580 guarded location-copy cases, 131 read-column scalar/tail cases,
45 macro/guard/include-boundary unit runs and 12 virtual-source identity cases.
The local token totals below belong to separate diagnostic environments;
they are not substituted for the target's corpus results.

## Initial 9700X observations

The three alternating process rounds each ran eight passes, with a warm file
cache after pass 0. Every odd candidate pass allocated a fresh transcript cache
and charged its recording, validation, replay and destruction. Reference
medians use passes 1 through 7; candidate-off medians use 2/4/6 and candidate-on
medians use 1/3/5/7. An even-sized median averages its middle two observations.
These are observations and their spread, not confidence intervals.

| Input / process round | Original total ms | Hooks, replay off ms | Fresh replay total ms | Replay versus original |
| --- | ---: | ---: | ---: | ---: |
| musl / 1 | 383.410 | 384.327 | 389.548 | +1.60% |
| musl / 2 | 382.443 | 379.448 | 388.716 | +1.64% |
| musl / 3 | 384.119 | 383.458 | 391.637 | +1.96% |
| Lua / 1 | 26.080 | 26.969 | 31.393 | +20.37% |
| Lua / 2 | 26.303 | 26.981 | 31.223 | +18.70% |
| Lua / 3 | 26.262 | 27.262 | 30.978 | +17.96% |

The original musl warm observations span 381.120–388.310 ms; fresh replay spans
387.934–396.106 ms. Thus there is no speedup beyond the observed spread. Lua's
corresponding ranges are 25.882–26.837 and 30.123–32.663 ms. Machine-readable
[process medians](initial-process-medians.tsv) and the full
[initial logs](raw/initial-screen/) retain the underlying observations.

The musl input is 1,315 static-libc C units selected using the x86-64 Makefile
replacement rules, including mallocng; assembly, CRT and the dynamic loader
are separate products. Common preprocessing flags are C99, freestanding,
`-nostdinc -Os -D_XOPEN_SOURCE=700` and musl's include paths. The unsupported
`-pipe` compiler transport option is omitted. This is a multi-unit preprocessing
comparison, not a complete musl build. Each pass produces 5,004,595 tokens.
Lua 5.4.8 uses its 34 C units with `-std=gnu99 -O2 -DLUA_COMPAT_5_3 -DLUA_USE_LINUX`
and produces 588,066 tokens.

Headers account for 95.4–95.7% of the original musl **main preprocessing**
interval and 78.7–79.1% on Lua. That does not make them the same fraction of
the complete frontend: musl main preprocessing is about 154–156 ms of the
382–384 ms total, and Lua about 19 ms of 26 ms. Initialization, predefines and
release remain substantial, and parsing is outside this initial screen.

Every fresh musl candidate batch retains 1,100 transcripts, performs 1,908
replays and 1,163 eligible misses, and checks 2,374,363 read identities. It
records 565,716 initial reads and 318,205 ordered writes, retaining 96,150,694
bytes (91.7 MiB). Process peak RSS rises from 36.8 to 127.0 MiB. Lua retains
165 transcripts / 4,428,298 bytes, with 1,520 hits, 569 eligible misses and
43,336 identity checks; peak RSS rises from about 33.0 to 45.3 MiB. Guard skips
are not replay attempts, so these hit/miss counts do not describe all includes.
The resource helper reports Linux `wait4` observations for the whole process.

This initial screen completed at 11:43:21 UTC. It rejects the first layout;
the final experiment above also rejects the smaller read-column layout.
Neither result proves that every possible future inclusion cache must lose.

## Compared paths

`header-clocks.patch` adds only the outer header timing probes to the original
preprocessor. `replay-hooks.patch` adds the hooks for the prototype, whose
implementation is `c_preprocessor_replay.c`. Both use the same lexer,
refinement, file cache, intern table, parser and preprocessing kernels from the
identified revision. `probe.c` supplies a serial multi-unit consumer because
the general compiler driver in #152 does not yet provide that boundary.

The batch shares one `FileCache` and `InternTable`, with a fresh `Preprocessor`
for each unit. Pass 0 fills the file cache. The candidate's odd passes start
with **zero transcripts**; their total includes cache allocation, recording,
validation, replay and destruction, as well as per-unit initialization,
predefines, main preprocessing and release. File-cache and intern-table
construction and final release are outside these warm-file-cache batch totals
for both paths. These totals sum timed unit intervals rather than one continuous
wall-clock batch: output digests run between each unit's preprocessing/optional
parse and its separately timed release. This intervening scalar checking is
present in every variant. Even candidate passes disable recording and replay,
exposing the hooks' cost against the separately built original reference.

`PROBE_PARSE=1` additionally times the existing tree-only parser and its
allocation/release. Its rejection count is explicit. A run with parser
rejections is a consumer-cost observation, not evidence of successful full
compilation. The printed `main_ns` is the narrower main-file preprocessing
interval; `total_ns` is the acceptance comparison. All paths enable provenance
and expansion-start tables.

Header clocks start **after entering** the outermost included file and end
**before popping** it; nested files are included in that interval. This excludes
the outer include directive, its path lookup and initial preparation; nested
lookup/preparation still occurs inside the interval. Thus the
header/main ratio describes repeated execution, not all inclusion overhead,
and the total batch comparison remains necessary. The probes use the same
timing instructions on both paths. `__rdtsc` values are TSC ticks, not measured
core cycles.

The controlled runner pins measurements to CPU 2 and alternates original and
candidate process order across three rounds. The completed primary comparisons
use no added conditioning. The adapter also supports work after 100 ms of
dependent scalar conditioning and a parser consumer; that supplemental matrix
was ended early as described above. Each build, test or
measurement separately takes the shared 9700X lock with CI priority. Never put
an outer lock around this runner.

## Prototype contract

One serial owner holds a cache tied to one file cache and monotonically growing
intern table. Options, search paths, environment and all source bytes are
immutable for that lifetime. Macro identities are exact 64-bit file-record and
definition-location pairs, with distinct tags for fixed predefines, fixed
command-line definitions and builtins; undefined is zero. Arbitrary virtual
sources cannot masquerade as the command-line buffer. No spelling hash decides
a hit. A different definition identity causes ordinary execution.

Recording covers macro reads, including undefined names, and ordered
definitions/removals; nested included paths and ranges; kinds, flags, values and
locations; expansion, substitution, generated-token and source history; origin
runs and expansion-start boundaries; scratch spellings in original allocation
order; and guard/included state. Root path, include search index, inclusion
depth, initial guard/once state and prior system-header classification are
validated. Fixed options and the resolved root's exact path make ordinary
relative include resolution deterministic. Include queries, dynamic builtins,
pragmas, line directives and repaired lexical views fall back to ordinary
execution. A diagnostic or unsupported provenance reference prevents publishing
a transcript. A child can record independently after a parent becomes unsafe.

The optional cache holds at most 4,096 transcripts, 64 variants per header,
32,768 read/write/scratch/event items per recording and 256 MiB of retained
storage. It uses 8 MiB of temporary storage plus sparse virtual arrays for
identifier generations and file heads. Capacity exhaustion abandons recording.
A fatal allocation error after committed replay remains a failed unit.

Validation uses scalar identity loads and AVX-512 comparisons of eight exact
64-bit identities. The location producer stores source/offset pairs; the
consumer handles sixteen locations using bounded masked loads, in-register
permutations and one base lookup for homogeneous blocks. Mixed blocks use
scalar base loads. The logical input length authorizes every access; no padding
is required. A separately selected scalar location-copy twin checks the new
kernel, including protected-page tails. Macro changes remain ordered.

## Diagnostic checks completed before target qualification

`verify.c` executes three complete passes with replay off and on, using the same
preprocessing kernels, and compares all four token columns byte for byte. It
also compares the complete expansion/substitution/generated/source tables,
history runs, expansion-start tables, diagnostics, macro dumps and printed
output with and without line markers. Successful comparison includes equally
rejected malformed inputs; it does not assert that the corpus is valid C.

| Local diagnostic input | Unit runs | Output tokens |
| --- | ---: | ---: |
| musl 1.2.5 C enumeration | 4,071 | 15,374,781 |
| Lua 5.4.8 | 102 | 1,764,198 |
| Zig 0.14.1 bundled libc C/header corpus, 8,983 inputs in groups of 512 | 26,949 | 36,728,361 |
| #109 generator, seed 111, 500 C17 programs | 1,500 | 152,619 |
| #109 generator, seed 111, 500 C23 programs | 1,500 | 151,446 |

There are also 8,580 protected-page checks of the new location kernel,
guard-state transitions and scalar-copy comparisons. `source-verify.c` checks
twelve caller-supplied virtual-source variants named `<command line>`; they
must safely decline reuse. The raw local logs are labelled as local evidence.
They are not the 9700X acceptance run.

The existing whole-preprocessor scalar and vector paths have a diagnostic-column
disagreement on generated C17 case 236, even with the cache entirely absent
(224:16 versus 224:11 on this revision). Therefore forcing the entire baseline
scalar path is a separate existing-oracle question, not the new location-copy
twin. This experiment does not choose which existing diagnostic column is
correct, and does not change that code.

## Reproduction layout

Use an unmodified checkout of the identified revision as `tree`. Copy its
`src/buster/compiler/c_preprocessor.c` and unchanged `c_preprocessor_history.c`
into `base-probe/buster/compiler/` and `final-probe/buster/compiler/`; apply the respective patches with `patch -p1`
inside each overlay. Copy this directory's C files into `final-probe/`, with
`c_preprocessor_replay.c` under `final-probe/buster/compiler/` instead. Includes
not present in the overlays resolve from `tree/src`. No full copied compiler
or external corpus is needed in the evidence directory.

The original reference wrapper **must live outside `final-probe`**:
`reference-harness/probe.c` is a byte-identical copy of `final-probe/probe.c`.
Quote-include search would otherwise select the candidate before `-Ibase-probe`.
The recorded `reference.d` confirms that the final reference compiled
`base-probe/buster/compiler/c_preprocessor.c` and its unchanged history module,
with no replay implementation. The initial wrong-path build failed its unused
function checks and produced no accepted reference result. Setup failure logs
are retained separately from successful observations.

`run-9700x.sh` is the initial runner and records the x86-64 musl selection,
commands, host identity, executable fingerprints, raw process results and
resolved input-file lists. Its `source.sha256` covers `tree/src` and the
compiled primary overlays before the builds; `soa.sha256` identifies the final
column variant. Final commands and successful completion times are under
`raw/final-target/`, reusing the initial `musl-units.txt` / `lua-units.txt`.
Both campaigns set `SOURCE_DATE_EPOCH=1767225600` and clear `CPATH`,
`C_INCLUDE_PATH`, `CPLUS_INCLUDE_PATH` and `OBJC_INCLUDE_PATH`. The reference
`cold-batch.c` wrapper, like `probe.c`, must be outside the candidate overlay.

The musl and Lua inputs are runtime corpora, not vendored files. The independent
musl copy reports version 1.2.5; its resolved source/header hashes identify the
actual measured bytes. The libc qualification uses
`/var/lib/buster-9700x/corpora/preprocessor-391e729af658/libc`: the frozen Zig
0.14.1 snapshot, whose documented C/header subset is 8,983 files / 93,836,361
bytes. Its musl portion includes Zig's patches, and is distinct from the
selected standalone musl batch. Generated programs use this repository's
original #109 generator, seed 111 and case ids 0 through 499 in each language
mode. The record preserves original observations, memory use, source identity
and the decision alongside the reduced medians and their spread.

`metrics.c` is a separate, untimed diagnostic that includes the same frozen
cache and harness. It first warms the file cache without replay, then starts a
fresh transcript cache for one batch. Its CSV lists every retained transcript,
its header's variant count, read/write-set sizes, output and provenance sizes;
stderr records hits, misses and total retained storage. Build and run it under
their own locks, with the primary experiment's flags and input lists. Its
observations are separate from the timed executables and are not a substitute
for their measurements.

`measure-run.c` replaces the unavailable `/usr/bin/time` for external process
resource observations. It does not replace the benchmark's timing intervals.
`cold-batch.c` supplies the final boundary that constructs and releases the file
cache and intern table inside each contiguous whole-batch interval, charging
the common output-digest consumer too. It uses the frozen kernels and the
compact-layout patches for the candidate. Initial warm-file-cache observations
must not be presented as fresh whole-batch results. Process startup, option parsing
and once-per-process discovery are outside the new batch timer; file-cache,
intern-table and transcript-cache setup/release are inside it.

## Provenance and status

The probe, cache, original fixtures, runner and observations are original work
under the project default license. The fuzz generator is included from Buster
at the recorded revision and retains its existing notices. musl, Lua, the libc
corpus and installed Clang headers are external runtime inputs; their source
files and implementations are not imported by this evidence package.

The final decision is to reject this cache and close #111 with the measurements
recorded here and in `docs/c-preprocessor.md`. The production implementation,
driver interface and preprocessing options gain no cache or dormant hooks.

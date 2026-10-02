# Native ELF TLS link comparison (#1915)

Baseline: `6fc08ec475694aeb3b1062c1d390483a69668508`, tree
`47a70e9cc59bfbfae7b939587c7c2eefd2ba03c9`. This package targets x86-64 Linux
ELF executables, starting with four host-built objects containing 64 independent
general-dynamic TLS access sites. It also covers PIE and positional archives.
The registered local-dynamic and shared-image fixtures retain those contracts.

## Cost reconstruction at the baseline

This is a source-derived work ledger, not measured phase durations. `Q` is input
section count, `S` symbol count, `R` relocation count, `H` eligible helper
references, `B` loaded payload bytes and `D` debug bytes.

| Stage | Existing work and ownership | Effect of this change |
| --- | --- | --- |
| Mapping and ELF parsing | driver maps inputs; object_read_elf64 traverses section headers four times, copies admitted payload, decodes symbols/names and relocations with bounds checks | unchanged |
| Archive selection | archive.c indexes names and schedules a pass-ordered provider worklist; only selected lazy members receive full admission | unchanged; positional selection preserved |
| Merge and resolution | link_objects plans sections, hashes global names, builds per-input u32 symbol maps, copies section contributions and rebases relocations | unchanged; duplicate/weak precedence preserved |
| Layout/imports | writer plans fixed section families; shared-library index supplies import definitions, aliases and versions | unchanged |
| TLS helper relationship | each helper query scans R candidates; fixed-address import classification and relocation application repeat it | one sparse exact site index, reused by both consumers |
| Relocations/unwind | all loaded relocations retain bounds and encoding checks; existing unwind index and CFI processing remain | unchanged; helper patches skip only exact companion sites |
| Debug/symbol tail | copy D bytes, apply debug relocations, emit section and ordinary symbol tables; existing arena-tail growth avoids a full prefix copy | unchanged |
| Publication | complete image goes through file_publish_checked; producer must first have LINK_ERROR_NONE | fix omitted success guards tracked by #2235 |

With T interleaved TLSGD/helper pairs and R=2T, the baseline companion scanner
visits T² rows per classification/patch phase, or 2T² for fixed-address linking.
At T=4096 this is 33,554,432 candidate visits, derived from code, **not timed**.
The new table scans relevant input twice, then probes exact section/u64-offset
keys. Open addressing has expected linear aggregate work; adversarial collisions
can still increase probing. Its capacity is at most the next power of two above
2T, with 16-byte slots, and includes checked scratch admission. Tests report
actual scan/query/probe populations through T=1024 against an independent scalar
oracle. Test builds include counters; tests-disabled production builds do not.

No helper means one additional symbol-name scan and no relocation scan/table.
A helper without TLS sites means one count scan and no table. Shared images build
no relaxation table. Nothing is cached across links or input revisions.

## Alternatives and applicability

Caching the helper spelling or filtering candidate kinds would reduce constants
but retain nested scans. Adjacency is invalid for reordered foreign relocations.
Cross-object symbol interning, archive replacement, a second linker and parallel
layout were rejected as wider semantic/ownership changes. Global symbol, archive,
DSO and unwind indexes and ELF image-tail growth already exist.

| Input shape | Expected implication; evidence still required |
| --- | --- |
| Cold inputs | page faults and reads are unchanged; any first-touch penalty may dominate |
| Warm inputs with many TLS sites | removes repeated relationship reconstruction; measure complete relink before claiming latency benefit |
| Many small objects | merged keys include section and rebased offset; setup is paid once per writer, while parsing/merge costs remain |
| Large archives | admission/extraction is unchanged; unselected members must remain unselected |
| Debug-heavy inputs | debug/unwind bytes and relocations remain; TLS searches previously included unrelated debug rows |
| No TLS or very small links | extra symbol scan can outweigh saved work; no blanket speedup claim |

## Reproduction and independent controls

Use a cloud checkout, never the laptop or benchpress/9700X for this task. Build
serially through build.c in the same path; preserve each trusted Clang binary
before replacing its sources. The normal hosted matrix, sanitizer trees,
tests-disabled/self-host builds and byte-identical fixed point remain required.

```sh
./build.sh generate --cc clang --ci
./build.sh build --config Release -t ide
BUSTER_TEST_JOBS=1 BUSTER_LINK_TLS_BENCH=1 BUSTER_LINK_TLS_OUTPUT=/tmp/link-tls-package build/Release/ide test --ci=1 --verbose=1
```

The registered `link_test_tls_membership` oracle independently spells GD/LD
offsets and GOT kinds; it checks original/reversed/even-odd row orders, duplicate
keys, exact section/name/high-offset identity, overflow, genuine helper references,
dirty outputs, unchanged inputs, no-table cases and scratch exhaustion.
`LINK_TLS_WORK` records deterministic mechanism work; no assertion gates on time.

`compiler_driver_test_link_tls_sites` constructs its own C fixtures, four -g
-O0 -fPIC objects (direct and -fno-plt variants), main.o and a positional archive.
Its independent system-linker image executes first. Each Buster ET_EXEC/PIE image
must execute successfully, preserve .debug_info and .eh_frame, and pass the
independent readelf/llvm-readelf process. Inspection reports remain in the output
directory. This parses ELF headers/sections/relocations/symbols and checks retained
section presence; the new workload does not independently decode DWARF/CFI or
exercise stack unwinding. Structural inspection and loader execution are separate
assertions.
The existing local-dynamic fixture additionally loads shared images through both
a Buster PIE and a host-toolchain executable.

`LINK_TLS_ARTIFACT` times only object/archive driver invocations. `compiler_ns=0`
states there are no C inputs in that interval; `link_ns` and
`total_artifact_ns` cover `compiler_driver_execute_invocation` through its return:
object/DSO reads, parsing, archive selection, merging, writer work and transactional
publication. They exclude process startup, argument parsing, output reread/hash,
inspection and loader execution. Host preparation is a partial preparation timer:
it covers main.o compilation plus variant module source generation, compilation
and object metadata inspection. It excludes main source generation/write, archive
creation, reference links/execution and independent output inspection. It is not
Buster compiler throughput or total fixture preparation. Eight opt-in samples retain the first sample and seven warm replays;
the first sample is **not** labelled cold because construction already touched
the filesystem. Sample 0 creates an output; later samples replace it, and intervening
output hashing, loader execution and readelf also affect cache state. The harness
asserts replay checksum agreement, not byte identity. The emitted 64-bit hash is a
replay checksum, not cryptographic provenance.

For baseline/candidate comparison, freeze SHA-256 identities, source/tree, complete
Clang commands, host/toolchain versions and all input hashes. Retained main.o,
module-0..3-V.o and sites-V.a are pristine matched inputs. Replay each binary's
`cc -g0 -no-pie main.o module-0-V.o ... -o image` and `-pie` form, replacing
the four objects with sites-V.a for the archive leg. Inspect, execute and require
byte-identical **baseline/candidate** outputs separately before interpreting
paired A/B times. External `cc` process wall time includes startup, argument
parsing and exit; report it as process artifact latency and compare like boundaries
on both revisions, never against the internal `LINK_TLS_ARTIFACT` interval.
Also retain same-source rebuild controls; one immutable A/A
does not control build-root or code-layout sensitivity. A true cold-cache campaign
requires an independently controlled fresh filesystem/cache protocol; never drop
host caches from this task. Whole-source compile, -c, object-only link and total
source-to-artifact latency must be measured separately.

## Publication negative controls

`link_test_elf_failed_publication` supplies a valid main and an eight-byte debug
section. An ABS64 field at offset 1 fails only in the final debug tail, while
offset 0 succeeds. Static/dynamic x86 ELF, PIE and AArch64 static cases each cover
existing sentinel and absent output paths. Success publishes the complete image;
failure must preserve the sentinel or absent destination.

## Provenance and licensing

All new fixture C is generated by the repository's own test code; no external
source/test asset was copied or vendored. Buster's complete pinned recursive tree
contains no LICENSE/LICENCE/COPYING/UNLICENSE file. Its `LICENSES/` third-party
notices do not establish a license for Buster as a whole: repository license is
**missing/unverified**, not inferred from public access.

The primary GNU ld archive-order contract was inspected in
[ld/ld.texi at binutils-2_35](https://gnu.googlesource.com/binutils-gdb/+/refs/tags/binutils-2_35/ld/ld.texi),
blob `2a93e9456aca3c2bed36f539ba223c9955ea4cdd`. Its own copying notice verifies
**GFDL-1.3-or-later**, no invariant sections or cover texts, for that document;
this is not a claim that all Binutils components share that license.
The x86 psABI source at GitLab revision
`3177443c4f5862d48f371d91ab36209f73cfe69c` was searched but raw retrieval was
blocked; its license remains **unverified** and no material was copied or
depended on for the implementation. The retained Buster encoding metadata and
independent installed-toolchain objects/executions supply the TLS controls.

## Evidence status

At implementation commit `176e3b550f4c5572a469c1ecda0f7e9c326ece2b`
(tree `6cb2dd4d498c9d99bcdcf250460a783fa70bcbc6`), the hosted direct-SSA
census passed link_tests 151,074/151,074 and compiler_driver_tests
138,529/138,529. This includes the eight real object/archive ET_EXEC/PIE cells,
independent readelf inspection, loader execution and publication controls.
At 1,024 pairs the scalar oracle visited 1,048,576 candidates; the index recorded
4,096 build-row visits, 1,024 queries and 3,144 bucket probes. These are actual
instrumented mechanism counts, not an elapsed-time speedup. The eight single
warmed diagnostic links took 0.807–0.975 ms, with compiler_ns=0; no paired A/B
latency result is claimed.

The TCC/source-size gate passed on PR merge
`c7dc644b592439aef3bcaf924d59435fb3c3288b`. The hosted self-host workflow
passed a 41,188,016-byte stage-1/stage-2 fixed point and its three-generation,
two-repetition bootstrap audit. The throughput guard reported zero confirmed
regressions and 19 inconclusive cases; that does not establish equivalence.
[PR #2247](https://github.com/buster14a/buster/pull/2247) retains exact job links
and subsequent check/integration state. This documentation follow-up does not
change implementation sources.

No local build/test/timing was run. Baseline/candidate byte comparison,
cold-cache experiments and complete phase-duration profiling remain unrun.
Qualified-host performance acceptance is explicitly pending and unavailable in
this session. CI timestamps alone are not linker timings. Results and actionable
findings belong on #1915 / its PR.

# QUALITY candidate-selection research packet

Research-only evidence for #313 and PR #2550. This packet is confined to the
`codex/313-candidate-measurement` branch and is not part of compiler production
code, the default test corpus, or required CI. Its push workflow has read-only
repository permissions and uses one ordinary Ubuntu runner. It does not dispatch
work, access the dedicated 9700X, or change the source PR's workflows.

Pinned subjects:

- Baseline: `97fb07f42b432864bee9fd38b70eb69b15252991`.
- Candidate: `735d270728441245c5a1d1721220c16408b101b9`.

Both trusted Clang Release compilers build serially in the same source/build
path, with tests, unity compilation, and allocation instrumentation disabled.
Their immutable copies are frozen before any measurements. The third build
enables only the allocation census and is excluded from every timed trial.
Configuration, full compile commands, source/tree/compiler hashes, platform
metadata, frozen generated inputs, and all observations remain in the artifact.

## Frozen inputs and independent oracle

`generate.c OUTPUT_DIRECTORY` writes four C sources with 4,097 logical values:
`late-hot`, `early-hot`, `equal-forward`, and `equal-reverse`. Logical value V
reads `input[V % 16]` and XORs it with `V * 7 + 123456789`, then publishes it to
`output[V]`. The cold shape has two physical-clobber barriers and two volatile
stores. Its barriers clobber caller-saved GPRs and rbx, leaving r12-r15
available. The hot logical value, V=4096, has ten caller-saved barriers/stores
and leaves rbx available too. Equal shapes contain only cold values. Ordering
changes preserve logical input/output identities.

These source forms are hypotheses about allocator pressure, not a claim that
the frontend admits exactly 4,097 candidates. Explicit candidate diagnostics
report the actual QUALITY population, cap hits, exclusions, and potential
weighted benefit. Unsupported assembly, structured compiler rejection, no cap
hit, a failed oracle, or changed instrumentation output remain observed outcomes.

`driver.c` is compiled independently by host Clang and linked to each Buster
object. It initializes every input, poisons every output, and checks every
one of the 4,097 values against an independent integer oracle over four seeds.
It performs one additional warmup, measures exactly 256 probe calls, and checks
the final complete output again. The reported `probe_ns` excludes initialization,
oracle work, process launch, and linking. The collector's separate `wall_ns`
includes the complete runtime child process and its oracle.

The SHA-256 manifest of generated files is produced once before compilation;
both subjects consume those exact files with `-g0 -O0`: `none` is the direct
reference with `-fmachine-fallback`, while `fast` and `quality` require
`-fno-machine-fallback`. The driver deliberately rejects strict NONE. No
unsupported FAST/QUALITY result is converted to a successful fallback.
Source-order comparisons are reported per shape/mode.

## Collection and scope

The collector chooses the first CPU in its inherited affinity mask and pins
itself and every child there. It retains one warmup A/B pair, then twelve
fixed alternating A/B pairs per shape/mode/operation. Timing order, wall time,
child CPU time, Linux child high-water RSS, per-variant deterministic output
FNV-1a hashes, runtime oracle checksum, and probe time appear in CSV. Raw child
logs are retained for every sample. A failed or timed-out child fails collection;
the process alarm is sixty seconds. There is no adaptive rerun or timing gate.

Example invocations, from a prepared immutable packet directory:

```sh
generate evidence/frozen
collect compile evidence/samples/late-hot quality /absolute/ide-baseline /absolute/ide-candidate /absolute/evidence/frozen/late-hot.c
collect runtime evidence/samples/late-hot quality /absolute/baseline-probe.exe /absolute/candidate-probe.exe
```

Untimed `-v` compilations retain exact `CODEGEN_ALLOCATOR` spill, reload, pin,
and split totals. Explicit QUALITY diagnostic compilations retain all raw
source metrics and require byte equality with the ordinary candidate object.
Additional QUALITY unity-input census cells cover direct SSA and shared local
promotion. Those unity cells describe incidence in current candidate code;
they are not matched-input timing cells.

`llvm-size` records object/executable text, data, and BSS separately. It does not
measure compiler source size. SHA-256 identifies the frozen compilers, sources,
objects, and executables; FNV hashes merely check within-trial determinism.
Raw compiler-process RSS can include inherited collector footprint, especially
for small runtime children; it is a diagnostic, not standalone memory admission.

The existing ordinary PR guard separately covers six generated workloads in
all four allocator modes. Its initial candidate attempt recorded zero confirmed
regressions and twenty-two inconclusive cells, with no allocation replay. Its
direct-SSA unity census selected FAST and recorded zero QUALITY invocations.
Neither result supplies real QUALITY cap incidence or generated-runtime
acceptance for this issue.

Workflow success means completed successful collection and independent oracles.
It does not establish a speedup, equivalence, profitability, or a dedicated-host
budget. This bounded corpus cannot establish cap-hit prevalence across external
projects. No AArch64, Windows, macOS, Zen 5, PMU, or real-project performance
claim is made. Admission-only latency remains a separate optional experiment.

## Preserved rejected corpus and distinct corrected corpus

The first hosted attempt, run
[37150651728](https://github.com/buster14a/buster/actions/runs/37150651728), used
packet `4cff40618ae5886fe705c1788ededbec9524e458`. Both ordinary subject builds
and the census build passed, but all 312 stress compiler trials were rejected
for unsupported GNU assembly clobbers. No stress objects, valid compile-time
comparisons, generated-runtime results, code-byte results, or spill comparisons
exist from that attempt. Every rejection log and CSV row remains in artifact
`quality313-research-37150651728-1`, ID 11283692821, ZIP SHA-256
`8ba6680dc70c0a002be9f8ccd67afb45b09fb2b080a8d5e87a57000cacdb8bb4`.

The pinned semantic parser, `c_gen.c` blob
`e9e60e90effefe5c0492f680c0d43f9128a63129`, accepts named base GPRs and
numeric r8-r11, but rejects the original cold barriers' r12-r15. The corrected
generator removes only those four clobber strings. Subjects, four shapes,
input/output identities, store/barrier counts, driver, collector, workflow,
and fixed sample population remain unchanged. Cold intervals now have part of
the callee-saved file available; actual eligibility, cap incidence and benefit
must be observed rather than inferred from the rejected MIR hypothesis.

Original generator Git blob: `d9ab8db03dd3246225b5e46add2dd75bb091ece8`;
SHA-256: `2cd0c3ccf0151d0195daa45c64668766e0f4b6cff31bb8f8ac8a8816ab904a09`.
Corrected generator Git blob: `86b1fd73be5a1889a50671885bcafe2eae4f4b5a`;
SHA-256: `9d1309dfc0bd8c2b72e4d62255326b8118e718e2763ef022b19c0367416d6e59`.
The original four generated input hashes remain in the first artifact's
`frozen-inputs.sha256`. The corrected attempt will generate its own immutable
files and SHA-256 manifest once on the runner; no corrected generated source
has been executed locally or treated as identical to the rejected corpus.

The first attempt's two real candidate unity QUALITY census cells succeeded.
Direct SSA observed 6,129 QUALITY functions, 5,815 prepassed functions and
86,080 eligible candidates; shared local promotion observed the same function
counts and 86,626 eligible candidates. Both recorded 314 switch fallback
functions, zero cap functions, zero excluded candidates and zero excluded
potential traffic. These observations apply to those candidate-source cells,
not to the rejected stress corpus or external projects.

Validation: Clang C11 warnings-as-errors syntax checks pass for all three packet
sources; all seven Bash literal steps pass syntax checks and the staged diff
passes whitespace checks. No generated stress source, Buster compiler, runtime
binary, or measurement was executed locally. Corrected hosted execution remains
pending at this documentation checkpoint.

Buster first-party licensing remains unselected, verified at the pinned
`LICENSES/README.md` and #621. The packet imports no third-party implementation
and adds no dependency.

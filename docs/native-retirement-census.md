# Native retirement object census

`build.c` owns `native_retirement_census`, implemented in
`tools/native_retirement_census.c`. It freezes the complete tracked C test-input
inventory before running a selected portion of the native object-compilation
matrix. It complements the strict coverage floor and the native
differential runner; it does not replace either or authorize backend retirement.

The admitted input inventory is
[`native-retirement-support-v1.tsv`](native-retirement-support-v1.tsv). Its 559
explicit SHA-256 rows bind every tracked test byte at the approval point: 411
subject inputs (405 supported-object subjects and 6 registered non-object
controls), 12 registered rejection controls, 72 support files and 64
dormant custom-language files. An added, removed, renamed, reclassified
or byte-changed test input makes manifest generation fail. Updating the contract
is therefore a reviewed support decision, not an automatic side effect of adding
a fixture. Merging a contract change is the maintainer approval record.

The September 17, 2026 integration retains every subject admitted by the
current support ledger. The integrated profile has 559 inputs,
411 subjects, 19,728 groups and 78,912 rows: 405 supported-object
subjects plus 6 registered non-object controls. The original 192-row
historical gap ledger and the original 341 applicability classifications remain
explicit and source-bound. Eighteen additional source-bound target exclusions
brought the applicability ledger to 359 entries in the earlier repair:

| Fixture | Object targets retained | Why the other targets are inapplicable |
| --- | --- | --- |
| `tests/basic_c_aarch64_abi_contract.c` | All six AArch64 targets | The fixture explicitly rejects non-AArch64 targets. |
| `tests/uefi_boot.c` | Both UEFI targets | Its included ABI contract asserts the freestanding UEFI data model. |
| `tests/issue36_target_wchar.c` | All ten hosted targets | Its expected typedef covers hosted ABIs; UEFI's unsigned 16-bit wchar contract has separate UEFI coverage. |

These additions preserve every fixture byte and census row. Their 288
non-executed rows carry authenticated skip provenance; the 288 rows for the
three fixtures' applicable targets still execute. They do not waive candidate compiler or evidence failures. Both clean-candidate and clean-acceptance
requirements remain active. The validator prints every failing fixture and its
row count before rejecting an incomplete census, with full detail in the JSON
and TSV evidence. The producer also counts failed MIR children when their
direct reference fails, instead of omitting them from its failure counter.

When integrating an admitted inventory update, keep the producer's full-profile
dimensions and the validator dimensions synchronized. The historical gap ledger
stays unchanged, but its digest of expanded row numbers must be recomputed from
the new ordered cross product. Regenerate the dependency receipt from the pinned
checkouts before updating the descriptor, receipt, project-closure, and ledger
trust anchors in both consumers. The checked-in contract tests bind these
dimensions, row identities, and dependency declarations to the reviewed files.

```sh
./build.sh native_retirement_census --self-test
./build.sh native_retirement_census --manifest-only --out build/census-inventory
python3 tools/native_retirement_materializer.py materialize \
  --manifest docs/native-retirement-dependencies-v1.json \
  --source-root "$(pwd)" --output "$(pwd)/build/dependency-materialized"
./build.sh native_retirement_census --ide build/Release/ide --compiler-revision <40-hex-commit> \
  --resource-include "$(clang -print-resource-dir)/include" \
  --project-include build/dependency-materialized/dependencies/project-include \
  --dependency-manifest docs/native-retirement-dependencies-v1.json \
  --dependency-receipt build/dependency-materialized/dependency-manifest.json --out build/census-full
```

Use a new output directory each time. Existing directories are atomically
refused, including directories left by interrupted runs. The command does not
build the compiler, change allocator defaults, permit production fallback on MIR
rows, or modify test expectations. `--compiler-revision` identifies the source
revision supplied by the operator; the binary's measured fingerprint and frozen
copy, rather than the supplied string alone, identify what actually ran.

An independently frozen earlier compiler can establish the support baseline:

```sh
./build.sh native_retirement_census --ide build/Release/ide --compiler-revision <candidate-commit> \
  --baseline-ide /path/to/reference-ide --baseline-revision <reference-commit> \
  --resource-include "$(clang -print-resource-dir)/include" \
  --project-include build/dependency-materialized/dependencies/project-include --out build/census-comparison
```

Without `--baseline-ide`, the same compiler supplies its direct `none` reference.
Compiler copies preserve executable permissions and are verified against their
original byte count and fingerprint before any compilation starts. Keep the
original build provenance with the output; the harness cannot infer which source
revision produced an arbitrary executable.

## Frozen inventory and matrix

`git ls-files -z -- tests` supplies the sorted input inventory. Every tracked
`.c` file is a subject except twelve explicitly named diagnostic-rejection
fixtures. Every input's role, byte count and `buster_hash_64` fingerprint
is recorded, including excluded inputs. The decision uses exact paths:
`basic_c_negative_constant_widening.c` remains a positive subject. Dormant `.bbb`
files never become compilation rows. Headers and other support files are copied
with the subjects, preserving their relative paths. Compilation reads these
snapshots, including the snapshot's `tests` include directory.

The exact excluded identities are:

- `tests/basic_c_invalid_labels.c`
- `tests/basic_c_invalid_asm_goto.c`
- `tests/basic_c_invalid_bit_field_width.c`
- `tests/basic_c_preprocessor_error.c`
- `tests/self_host_bootstrap_invalid.c`
- `tests/differential/reject_syntax.c`
- `tests/differential/reject_type.c`
- `tests/basic_c_bit_field_alignas.c`
- `tests/basic_c_function_pointer_conflict.c`
- `tests/basic_c_asm_literal_register.c`
- `tests/basic_c_sizeof_missing_member.c`
- `tests/basic_c_sizeof_parenthesized_type.c`

The driver tests explicitly require the last five inputs to fail, including
their diagnostic wording. The literal-register assembly case is an intentional
codegen rejection; it is not a supported native subject. Excluded source bytes
remain in `inputs.tsv` and the snapshot. Counts are discovered afresh; newly
tracked C fixtures are rejected until a reviewed contract update explicitly
classifies and hashes them. Nothing untracked silently enters a run.

Known language requirements use these exact-path recipes on both compiler legs:

| Fixture | Added flags |
| --- | --- |
| `tests/basic_c_constexpr.c` | `-std=c23` |
| `tests/basic_c_constexpr_leaf.c` | `-std=c23` |
| `tests/basic_c_nullptr.c` | `-std=c23` |
| `tests/basic_c_typeof.c` | `-std=c23` |
| `tests/basic_c_dialect.c` | `-std=c23 -DEXPECTED_STDC_VERSION=202311L -DEXPECTED_GNU=0` |
| `tests/basic_c_predicate_bank.c` | x86-64 uses `-mcpu=skylake-avx512`; other architectures retain the manifest CPU |
| `tests/basic_c_atomic_aggregate.c` | x86-64 uses `-mcpu=haswell` for the required `cx16`; other architectures retain the manifest CPU |

The first three settings match their registered driver tests. The `typeof`
fixture uses `typeof_unqual`, whose C23 keyword gate is covered by the C frontend
tests. The dialect recipe selects the registered strict C23 variant; the driver's
other seven GNU/strict dialect variants remain separate coverage. Other inputs
retain the pinned compiler's dialect default, including similarly named files.
`inputs.tsv` freezes each recipe name and exact added flags; `rows.tsv` repeats
the recipe name, and executed children retain every argument byte in `.argv`.

Executed rows require `--resource-include`. The harness refuses symbolic links,
copies that complete tree into `dependencies/resource-include`, records every
file's SHA-256 in `dependencies.tsv`, and hashes the ordered closure. Both
compiler legs then receive `-nostdinc -isystem <frozen-copy>`; the resource path
compiled into either executable and host include search paths are not used.
Object rows intentionally have no host sysroot. Linux GNU rows receive only the
target-matched, authenticated musl include roots from the project snapshot;
Windows, Apple, Android and UEFI rows do not receive those Linux headers.

Repo-owned project dependency policy is reviewed in
[`native-retirement-dependencies-v1.json`](native-retirement-dependencies-v1.json),
while exact repository byte/hash identities are generated in
[`native-retirement-repository-sources-v1.json`](native-retirement-repository-sources-v1.json).
Before `evidence-candidate.txt` is created, the workflow runs the
offline external-closure verifier against the exact candidate checkout first.
That verifier proves pristine GitHub worktrees at the seven descriptor pins
(cJSON, DoomGeneric, LZ4, yyjson, stb, zlib and musl), checks each admitted
path as a tracked blob at that revision, and derives only musl's upstream
`alltypes.h`/`syscall.h` outputs for x86-64 and AArch64. It performs no fetch and
does not vendor upstream files into the repository. The materializer then admits
only the listed paths after matching their authenticated byte count and SHA-256,
rejects source symlinks, hard links, descriptor/output-parent symlinks, TOCTOU
changes, and all network provenance (including SCP/SSH spellings), and publishes
the tree atomically at `evidence/dependency-materialized`. Generated metadata
names are reserved. Every destination is exactly its kind's compiler include
root (`dependencies/resource-include` or `dependencies/project-include`) and the
include-relative path is one global namespace: a `same.h` resource and project
record cannot coexist. Normalized duplicate destinations or provenance,
kind/root mismatches, arbitrary destinations, and metadata collisions fail
closed; an exact repeated resource is emitted once in deterministic order. A
full census must provide both the project include and the descriptor/receipt
paths shown above. The C producer replays the materializer's authenticated
project closure and ledger digests and copies the exact descriptor, receipt, and
materializer ledger into evidence; alternate self-consistent dependency trees
are rejected by the independent validator.

The materializer also requires a canonical descriptor-relative `source_root`
spelling and rejects control characters in fixture labels before serializing
its ledger. During census startup, each resource/project header copy is read
back and SHA-256 checked before it is admitted to the compiler include closure;
a truncated or otherwise altered snapshot therefore fails the dependency
contract before any object row runs.

The archived replay inventory is an authenticated row-level projection, not an
arithmetic assertion. Its 28 fixture identities include source SHA-256 values
and a fixture-to-project-header mapping. The frozen matrix expands each identity
across the exact target, frontend, PIC, and MIR allocator axes, assigns every
row a disposition, and authenticates the projection, input map, and row
identity digests. It proves exactly 4,032 archived MIR candidate rows: 264
repo-owned project-header rows are closed, 3,768 remain diagnostic, and 24 iOS
SIMD rows remain pending an authenticated `TargetConditionals.h`; those external
SDK headers are not fabricated by the materializer. These counts do not change
the 558 input rows, 411 subject inputs (405 supported-object subjects and six
non-object controls), 78,720 support-contract identities,
or the support ledger bytes and digests. These are historical replay dispositions. The current full census supplies
the authenticated 692-record project closure, including the target SDK headers
and the project-owned MinGW varargs adapter described below.

The live dependency aggregate is generated once in
[`../tools/native_retirement_dependency_binding.generated.h`](../tools/native_retirement_dependency_binding.generated.h).
That artifact binds the reviewed policy and generated repository snapshot to the
materializer receipt, project closure, and materializer ledger. The C producer
includes it directly and the independent Python validator parses the same bytes;
neither consumer nor this prose carries a separately edited live quartet.
Malformed, stale, truncated, or mismatched generated state fails closed. The
immutable legacy descriptor and validator compatibility constants continue to
authenticate evidence produced before this representation change.
The archived fixture-input map is
`bef841ade0921ffe9293440171b1d0d8dd6c3cf798f2535d8790b4ad26542500`, the
fixture-to-project-header map is
`8d79504f67d48fd27698c6897b00fc9347dd60a538a6198e53e42970c799bc4f`, and the
expanded 4,032-row identity is
`9604102b75a14631aeb1d6a3652d36506a05928a0046c52cc50a00b942826ce6`.

Compiler children receive a replacement environment recorded in
`environment.tsv`: `LC_ALL=C`, `LANG=C`, and `TZ=UTC`, plus the minimal explicit
Windows process/temp values on Windows. They do not inherit `PATH`, compiler or
include flags, SDK variables, preload settings, sanitizer settings, or user
configuration. The candidate, direct reference, support contract, tracked input
files, resource-header closure and (when supplied) project-header closure all
carry SHA-256 identities. Manifest-only runs state that dependency/environment
capture was not executed.

The full product is:

- x86-64 and AArch64, each targeting Linux, Windows, macOS, Android, iOS and UEFI;
- every explicit allocator in `BUSTER_CODEGEN_ALLOCATORS`;
- frontend SSA enabled and disabled;
- PIC enabled and disabled.

The current 403-subject support contract therefore freezes exactly
`403 x 12 x 2 x 2 x 4 = 77,376` row identities. Applicability evidence is an
additional validator-owned projection; it never removes a row or changes the
input-byte ledger.

The six subject-level non-object controls are `tests/basic_c_macro_options.c`,
`tests/ebpf_scalar_regression.c`, `tests/gpu/metal_reader.c`,
`tests/runtime_boundary_regression.c`, `tests/wasm_memory_alignment_regression.c`
and `tests/windows_unicode_regression.c`.
Their source registration is authenticated as
`registered-non-object-control`: every matrix identity remains in `rows.tsv`,
but the producer emits a retained-control record and does not invoke an object
compiler for that harness. The valid-C `basic_c_sizeof_anonymous_aggregate.c`
fixture remains a supported-object subject; its compiler debt stays admitted
and fatal until the compiler supports it.

Every row supplies `-c -g0 -v -fwrapv -fno-strict-aliasing -funsigned-char
-fverify-codegen -nostdinc`, the frozen resource include, an explicit target,
CPU model and allocator. Linux GNU rows additionally receive the frozen
target-specific musl include followed by musl's common include; no host sysroot
or ambient SDK path is admitted. The default CPU
profile is `baseline`; `--cpu` selects another named profile for a separate
manifest. MIR rows additionally require `-fno-machine-fallback`. Optimization and
promotion defaults are those of the pinned compiler; there is no claim that one
CPU profile covers every optional instruction feature.

`rows.tsv` records the ABI convention and effective CPU feature set computed
from the target/model contract. The child's `TARGET` telemetry must match both
before its object can pass. Each row also names its C lowering configuration,
PIC mode, compile obligation, separate link/native-execution owner and exact
`.argv` evidence path. Link and native execution stay in #509's platform and ABI
gates; object success does not silently satisfy them. Rejection controls retain
their source hashes and registered diagnostic obligation in `inputs.tsv` rather
than being counted as successful object programs.

External real-project obligations remain references, not copied descriptors. The
retirement contract consumes the admitted descriptors from #423 and the pinned
compatibility harnesses indexed in [`compatibility.md`](agents/compatibility.md).
The repo-owned project closure above is only the authenticated include boundary
for the archived test fixtures; it does not duplicate #423 pins, setup,
correctness or timing contracts. Until #423 publishes passing descriptors, those
external workloads remain an explicit pending gate.

One group contains the direct reference followed by every MIR allocator for the
same subject, target, frontend and PIC settings. `rows.tsv` freezes every group
and row **before compilation starts**, recording execution selection explicitly.
These options affect execution selection without truncating that manifest:

```sh
./build.sh native_retirement_census --ide build/Release/ide --compiler-revision <commit> --fixture basic_c_operations.c --target aarch64-unknown-linux-gnu --out build/census-focused
./build.sh native_retirement_census --ide build/Release/ide --compiler-revision <commit> --shard-index 0 --shard-count 8 --out build/census-shard-0
```

`--fixture` is a path substring and `--target` is one exact triple from
`rows.tsv`. Stable group ordinal modulo shard count selects a shard, keeping its
baseline with its MIR rows. A selection matching no groups fails. Match input
fingerprints, binary identities and row identity columns when combining shards;
their `selected` columns intentionally differ. Row IDs from different inventories
are not interchangeable. `--timeout` sets the bounded
deadline for each child, from 1 to 3600 seconds (default 30).

After joining any shards, independently check an executed evidence directory:

```sh
python3 tools/native_retirement_contract.py validate build/census-full
```

The validator recomputes the support/input/resource/compiler/object SHA-256
identities, checks the replacement environment and exact argv, and rejects row,
selection, target, ABI, lowering, PIC or obligation mismatches. To preserve the
required common-row transition accounting between a pinned reference and the
exact rerun candidate:

```sh
python3 tools/native_retirement_contract.py compare build/census-reference build/census-candidate \
  --out build/common-row-transitions.json --require-clean-candidate --require-clean-acceptance
```

For a sharded census, `validate-shards` independently validates each directory,
requires byte-exact equality of the full `rows.tsv` identity map and the full
`inputs.tsv` recipe ledger, then proves a disjoint complete selected-row
partition. Its aggregate report is written even when a disposition is dirty so
reference, setup and candidate failures remain inspectable:

```sh
python3 tools/native_retirement_contract.py validate-shards build/census-shard-* \
  --out build/census-aggregate.json --require-clean-candidate --require-clean-acceptance
```

`--require-clean-candidate` rejects candidate-side setup or execution failures,
supported candidate gaps, nonzero fallbacks, and invalid function, target,
counter or artifact telemetry. It deliberately does not reinterpret a
candidate success as a failure solely because its paired direct reference is
unresolved. `--require-clean-acceptance` is the separate final-acceptance gate:
it rejects both candidate-side failures and every unresolved semantic
baseline/reference row. The report records the requested gates independently as
`require_clean_candidate` and `require_clean_acceptance`, and records their
results as `clean_candidate` and `clean_acceptance`. Passing only the candidate
gate is not full retirement acceptance.

Rows whose exact
`execution_obligation` is `unavailable-platform-control` remain explicit
inapplicable controls; they are retained in the report and waive only the
separate native-execution obligation. They do not excuse a reference compile,
process status, object, fallback or telemetry defect.

The acceptance workflow invokes this current validator directly from its second
exact-candidate checkout. It does not reuse the historical archive's older join
or schema reader. The retained `census-validation-v2.json` therefore records the
complete current-schema partition even when `--require-clean-candidate` rejects
real compiler or reference gaps. The final invocation supplies both
`--require-clean-candidate` and `--require-clean-acceptance`: the former keeps
candidate cleanliness separate from unresolved direct-reference rows, while the
latter is the independent final acceptance gate.

The report records added and removed identities separately from the common-row
disposition transitions. Comparison schema 2 preserves separate candidate,
reference and combined-acceptance common-row failure counts and row lists. The
candidate gate rejects only `candidate_common_failure_rows`; the acceptance gate
rejects `acceptance_common_failure_rows`, including unresolved references.

### Schema-2 applicability and admission evidence

The Python contract validator derives a closed applicability class for every
selected cell; the producer cannot provide or override this field. The derived
classes are:

| Class | Meaning | Owner |
| --- | --- | --- |
| `admitted-supported` | The supported-object obligation is admitted to the candidate compiler. | candidate |
| `retained-control` | The allocator-`none` direct compilation, or an authenticated non-object control, is retained as the control. | reference/control |
| `retained-reference` | Candidate evidence is retained, but its direct reference is unresolved. | reference |
| `platform-inapplicable` | Native execution belongs to a platform owner that is unavailable for this object cell. | platform |
| `unavailable` | Compiler/process/evidence admission is unavailable or not admitted. | admission |

`validate-shards` writes `applicability.tsv` next to its JSON output. It has
one deterministic row for every selected cell, including the original fixture,
target, CPU, frontend, allocator and PIC identity plus the producer disposition,
validator reason, ownership and separate candidate/reference/acceptance failure
bits. The original `rows.tsv` identity map and the frozen input-byte ledger are
unchanged. A supported-native gap remains `admitted-supported`; its candidate
failure cannot be hidden by relabeling it as a reference or unavailable row.
The validator also records the exact supported-gap row list and rejects any
attempt to move those rows to another class (the current evidence has 192).
This candidate-owned rule takes precedence over the platform execution-control
label for a declared gap; the object evidence still has to explain the gap.

The candidate gate applies candidate cleanliness to admitted-supported cells;
reference-only retained rows do not become candidate failures merely because
their direct reference is unresolved. A retained row whose candidate side has
an unexpected defect is still fatal. The acceptance gate additionally requires
every retained reference/control to resolve. An authenticated `unavailable`
row preserves the exact missing-resource or CPU-profile provenance, but always
fails clean acceptance until exact supplemental evidence closes that same
fixture/target obligation. It is not a waiver and is not charged as a compiler
defect. This class distinction does not
waive unexpected compile, object, process, fallback or telemetry defects: those
remain fatal for any applicable cell, including platform-inapplicable controls.
Authenticated platform-inapplicable and unavailable cells are instead
represented by explicit non-executed results and row-bound
`applicability-skips.tsv` provenance. A skip is valid only with the exact
zero-artifact/zero-counter shape; malformed status, process, fallback, object
or telemetry evidence remains fatal, and a caller-supplied disposition cannot
select the skip path. A production manifest is admitted only as
`profile=full-census`: it must bind
the exact 558-input/410-subject/78,720-row, four-shard population. The producer
also copies `docs/native-retirement-supported-gaps-v1.tsv` into the evidence
directory and binds its SHA-256 in the manifest. The validator checks that
authenticated seven-column ledger, including all 192 immutable row identities,
before deriving applicability; result `disposition` text cannot add, remove or
reclassify a declared gap. Smaller fixtures must explicitly use
`profile=self-test` and can never satisfy production profile acceptance.
The producer also copies the immutable
`docs/native-retirement-applicability-v1.tsv` projection and binds its SHA-256.
For the full profile it must contain the exact authenticated 374 fixture/target
entries, each tied to the subject's input SHA-256 and a source-reviewed reason.
Only this projection can classify a target-specific residual as
`platform-inapplicable` or `unavailable`; a result disposition, row count,
fallback counter or diagnostic cannot forge applicability. Every 78,720 row
identity and input byte remains in the manifest and validation partition.
The six whole-fixture non-object controls are authenticated by the support
contract instead of receiving row outcome classes in this fixture/target
ledger. The aggregate report also records the skip evidence path alongside
`applicability.tsv` and `residual.tsv`.
A bounded, deterministic
`residual.tsv` is emitted beside `applicability.tsv`; it retains at most 256
diagnostic rows and joins fallback attribution to fixture, function, target,
CPU, frontend, allocator and PIC. The JSON report records both evidence paths,
class counts and whether residual attribution was truncated.

## Observations and failure accounting

`results.tsv` joins to `rows.tsv` by row ID and records process kind/status,
function/fallback counts, the matching baseline function count, artifact size,
fast fingerprint and SHA-256 identity. Successful MIR objects require explicit
verifier telemetry and zero fallbacks. Different function counts from a
successful direct baseline are a failure. Empty/data-only units are recorded
separately from nonempty strict successes.

A failed direct reference is `baseline-unresolved`, not proof that its source is
unsupported. The broad input inventory includes platform-specific observers and
external-compatibility fixtures that need extra headers or harness setup; missing
dependencies, frontend errors and failed verification must be resolved before
using such rows as a support baseline. A strict failure after a successful direct
reference is a supported-native gap. Crashes, timeouts, missing objects, invalid
verification/counter telemetry and evidence-writing errors cannot count as passes.
The command returns failure for any unresolved baseline, supported-native gap,
protocol/infrastructure failure, or missing selected execution. It still attempts
the remaining rows and retains their observations.

An older baseline that omits `CODEGEN` counters for an empty translation unit
also remains unresolved. The census does not invent a zero function count or
relax verifier/telemetry requirements to turn that observation into support.

Verbose codegen now retains its aggregate statistics after a codegen error,
including strict fallback rejection. `fallback-counters.tsv` preserves every
reported reason/opcode/stage total, keyed by row. Successful and failed children
also retain byte-exact `.stdout`, `.stderr` and NUL-delimited `.argv`; the shared
`processes.tsv` preserves raw native statuses, sanitizer detection and elapsed
time. Objects and both compiler executables are retained.

Every candidate MIR row requests `-fcodegen-fallback-census`. This diagnostic
opt-in allocates a compact source-order record array in the current codegen
attempt; retries discard the old array. Ordinary compilation allocates no
per-function records. Before each translation-unit arena is released, the driver
retains each observed fallback's function ID, name, source path/coordinates,
reason and opcode. The first strict diagnostic and production fallback policy
are unchanged.

`CODEGEN_FALLBACK_CENSUS version=1 records=N` precedes the versioned
`CODEGEN_FALLBACK_FUNCTION` rows. Each row includes target, allocator, function
ID, reason, stage and opcode ID (`UINT32_MAX` means no applicable opcode).
The retained `fallback-functions.tsv` telemetry also carries `version=1 row=N`;
the authenticated inner row must equal the TSV row key. `source_hex` and
`function_hex` encode non-empty exact UTF-8 bytes as lowercase hex. The `-`
sentinel is invalid for a retained function record, so missing source/function
identity cannot be admitted as fallback evidence. This preserves spaces, tabs,
quotes and non-ASCII names without ambiguous escaping. The rows remain keyed
to their exact matrix configurations.

The census requires one supported record-count marker, valid fields, strictly
increasing function IDs within its single-TU invocation, and exactly as many
attribution records as aggregate fallbacks. Missing, duplicate, malformed or
out-of-order attribution cannot be counted as complete evidence. Older baseline
compilers do not need the new flag; candidate compilers do.

All **observed fallbacks** are attributed. A fatal frontend/verifier/encoding
error can stop compilation before later functions are visited. Its first fatal
diagnostic and partial counters are retained; an unvisited function is not
declared covered. The command does not rerun failures through permissive MIR.

`summary.txt` states whether the entire frozen cross product actually executed;
manifest-only and filtered/sharded runs do not claim completeness. Missing result
rows remain unexecuted, never successes. `retirement_accepted=0` is intentional:
this command measures object coverage. Fatal-stop/unvisited functions, self-host
fixed points, independent mixed-compiler/runtime correctness, native unwinding,
representative external workloads and accepted throughput/memory/size/runtime
budgets remain separate requirements of #36.

The `Native retirement evidence` workflow is the exact-candidate execution
entry point for this census and the strict semantic differential. A pull request
uses GitHub's exact integration commit; a manual dispatch uses its selected
revision. The workflow rejects a checkout that does not match that identity and
carries the commit, tree and compiler binary digest in the retained evidence.
It executes the complete census through four disjoint shards and retains all
four shard directories for the current-schema aggregate validator before the
strict differential's six native lanes run.
Its strict differential is a native six-host matrix: Linux, macOS and Windows
on both x86-64 and AArch64. A successful object census on the Linux x86-64
coordinator is therefore not mislabeled as runtime evidence for the other five
hosts. Each host builds its own immutable candidate and independent O0/O2
oracle. Sanitizer coverage is required on Linux, macOS and Windows x86-64; the
workflow prepends Clang's matching resource-runtime directory on Windows so a
different installed ASan DLL cannot satisfy the run. The Windows Arm64 runner's
LLVM package does not ship an AArch64 ASan runtime, so that lane records
`oracle_sanitizer=not-run` and the exact reason instead of reporting an
unsanitized run as a sanitizer pass. Both Windows lanes preserve every Visual
Studio `LIB` directory as an explicit, ordered `--library-path` input in the
harness manifest and child argv. This lets the Buster driver find the UCRT
legacy stdio definitions used by the intentionally headerless programs without
silently inheriting the parent environment. The Arm64 clear-cache caller also
supplies the compiler-rt `__clear_cache` boundary omitted by the runner package,
backed by Windows' `FlushInstructionCache`. Windows Arm64 lowering calls that
boundary because hosted processes cannot execute DC CVAU / IC IVAU directly;
the original empty and nonempty ranges, argument side effects and live-register
checks all remain. Other native hosts retain inline maintenance, and the
independent Linux AArch64 byte oracle continues to check its complete sequence.
No case or matrix row is pruned for either host-toolchain limitation. The stable
`Native retirement acceptance complete` check rejects a missing, skipped,
cancelled or failed census or native matrix. Historical direct-reference
results remain preserved in the durable archives; current acceptance uses the
same-source direct backend and independently bound Clang controls.

The census upload is scoped to the generated `candidate/evidence/` tree and
explicit build recipes, with hidden-file inclusion enabled for that evidence
path. This preserves ledger-declared tracked inputs such as
`inputs/tests/.gitignore` through the package/download boundary without
uploading the checkout's `.git` directory or unrelated hidden files. The
registered CI-tools regression packages a representative shard, downloads and
extracts it with the archive reader, then replays the input ledger; it also
proves that removing the hidden input fails validation rather than being
silently repaired.

The workflow does not synthesize #508's support decision or make the census a
retirement verdict. After that manifest is approved, the final candidate run
must be joined to its exact manifest identity and archived through the durable
evidence pipeline before #509 or #36 can be closed.

`--self-test` checks exact exclusions and language recipes, strict decimal/counter/attribution protocols,
revision/path fields and complete, disjoint shard selection without needing a
compiler. Real smoke runs must additionally exercise successful objects, known
strict failures and refusal to reuse an evidence directory. The deadline and
child-evidence implementation is shared with `test_differential --self-test`.

## Current frontend reference and target SDK closure

The acceptance workflow now builds the exact integrated candidate once and
uses its `none` backend as the direct reference. The separately frozen
`3e912a3c5ce9b3e905f3096ff0a0d3e68d80e46e` compiler remains historical evidence,
including its unresolved rows; it predates attribute queries, assembly forms
and ABI repairs in the current fixtures. Comparing different frontend behavior
as a function-count oracle rejected valid MIR objects. This change does not
rewrite any archived run or the historical 192-row supported-gap ledger.

Some operations were added only to MIR and remain unsupported by the frozen
direct emitter. `native_retirement_reference.py` compiles those exact source
snapshots with Clang, using the same resource/project closure, CPU, PIC and C
dialect. Buster-specific mode/verification switches are removed, CPU spellings
are translated, and Clang selects its own supported GNU SDK branch. The
supplement retains the Clang binary, version, explicit environment, argv,
status, object and raw streams. The independent validator checks their hashes,
exact census/input binding, complete unresolved-group inventory, and target
object headers. A successful control resolves only the reference obligation;
no candidate, fallback, telemetry or object failure is cleared. The report
retains `direct_reference_failure_rows` and every supplement digest. Both clean
acceptance gates remain required. Missing, failed or altered supplements fail
acceptance.

Clang's UEFI controls use the corresponding freestanding C ABI: x64 MSVC/LLP64
and ARM64 AAPCS64/LP64, with short wchar. The ARM64 control is an ELF object; it
is not claimed to validate a PE loader. `basic_c_asm_goto_range.c` deliberately
requires a far TBZ relaxation that Clang's inline assembler lacks. Its Clang
control selects the explicit portable bit-test oracle, while every Buster row
still compiles the full 9,216-NOP assembly body. Registered native driver tests
execute both branch outcomes in every strict allocator and frontend mode.
The ordinary assembly fixture omits ELF-only `.type` metadata on Darwin.

`native_retirement_sdks.py` is a network setup step, before the offline
materializer. `native-retirement-sdks-v1.json` pins complete archive SHA-256 and
size plus every extracted header's identity; each record must also match the
dependency descriptor. It selects 311 unmodified headers from the existing
Zig 0.16.0 distribution (MinGW and Darwin) and Android NDK r27c. It never copies
upstream source into the repository. Linux continues to use the pinned musl
closure. The seven hosted-library fixtures receive their target-specific include
paths. MinGW's non-GNU ARM64 varargs gap uses a small project-owned adapter to
Buster's existing va_list builtins, and Buster selects the SDK's non-GNU path
to avoid GCC-only inline intrinsics. Clang retains its own GNU SDK path.
Darwin does not ship uchar.h, so the header smoke tests the supplied headers
there and retains uchar.h coverage on the other hosted targets.

The hosted-library fixtures require a hosted libc and are explicitly
platform-inapplicable on the two freestanding UEFI targets. The unchanged
`host_sysv_unnamed_bitfields.c` source is a host-compiler-only ABI observer,
as its source and driver registration state; its census rows retain that
platform ownership, and the native driver continues to build and execute it
with the host compiler. The ARM64 floating-state observer uses `unsigned long
long` for its 64-bit system-register operands, including LLP64 Windows.
Every original fixture and all 78,912 row identities remain present.

Main's Android x86-64 ABI repair selects binary128 long double. Format-specific
fixtures now test `__LDBL_MANT_DIG__`, retaining the complete x87 bodies on x87
targets and the existing float/double cases elsewhere. AArch64 binary128
widening stays scoped to AArch64. The signbit driver explicitly enables the
unsupported x86 binary128 operation and checks its exact load rejection in
every allocator/frontend/PIC/CPU combination, then compiles the float/double
positive control with zero fallback. Frontend initializer tests retain Android
as an explicit rejection control instead of expecting x87 byte images there.

# Native retirement object census

`build.c` owns `native_retirement_census`, implemented in
`tools/native_retirement_census.c`. It freezes the complete tracked C test-input
inventory before running a selected portion of the native object-compilation
matrix. It complements the strict coverage floor and the native
differential runner; it does not replace either or authorize backend retirement.

The admitted input inventory is
[`native-retirement-support-v1.tsv`](native-retirement-support-v1.tsv). Its 553
explicit SHA-256 rows bind every tracked test byte at the approval point: 406
supported object subjects, 12 registered rejection controls, 71 support files
and 64 dormant custom-language files. An added, removed, renamed, reclassified
or byte-changed test input makes manifest generation fail. Updating the contract
is therefore a reviewed support decision, not an automatic side effect of adding
a fixture. Merging a contract change is the maintainer approval record.

```sh
./build.sh native_retirement_census --self-test
./build.sh native_retirement_census --manifest-only --out build/census-inventory
./build.sh native_retirement_census --ide build/Release/ide --compiler-revision <40-hex-commit> \
  --resource-include "$(clang -print-resource-dir)/include" --out build/census-full
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
  --resource-include "$(clang -print-resource-dir)/include" --out build/census-comparison
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
Object rows intentionally have no sysroot or additional system include path.
Fixtures needing libc, SDK, project or generated headers remain visible failing
subjects until an explicit reviewed classification or existing harness supplies
their setup; the census never manufactures a successful baseline for them.

Compiler children receive a replacement environment recorded in
`environment.tsv`: `LC_ALL=C`, `LANG=C`, and `TZ=UTC`, plus the minimal explicit
Windows process/temp values on Windows. They do not inherit `PATH`, compiler or
include flags, SDK variables, preload settings, sanitizer settings, or user
configuration. The candidate, direct reference, support contract, tracked input
files and resource-header closure all carry SHA-256 identities. Manifest-only
runs state that dependency/environment capture was not executed.

The full product is:

- x86-64 and AArch64, each targeting Linux, Windows, macOS, Android, iOS and UEFI;
- every explicit allocator in `BUSTER_CODEGEN_ALLOCATORS`;
- frontend SSA enabled and disabled;
- PIC enabled and disabled.

Every row supplies `-c -g0 -v -fwrapv -fno-strict-aliasing -funsigned-char
-fverify-codegen -nostdinc`, the frozen resource include, an explicit target,
CPU model and allocator. The default CPU
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

Real-project obligations are references, not copied descriptors. The retirement
contract consumes the admitted descriptors from #423 and the pinned compatibility
harnesses indexed in [`compatibility.md`](agents/compatibility.md). Until #423
publishes passing descriptors, those workloads remain an explicit pending gate;
this object census does not duplicate their pins, setup, correctness or timing
contracts.

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

The report records added and removed identities separately from the common-row
disposition transitions. Comparison schema 2 preserves separate candidate,
reference and combined-acceptance common-row failure counts and row lists. The
candidate gate rejects only `candidate_common_failure_rows`; the acceptance gate
rejects `acceptance_common_failure_rows`, including unresolved references.

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
`source_hex` and `function_hex` encode the exact UTF-8 bytes as lowercase hex;
`-` denotes an empty string. This preserves spaces, tabs, quotes and non-ASCII
names without ambiguous escaping. `fallback-functions.tsv` retains the rows
keyed to their exact matrix configurations.

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

`--self-test` checks exact exclusions and language recipes, strict decimal/counter/attribution protocols,
revision/path fields and complete, disjoint shard selection without needing a
compiler. Real smoke runs must additionally exercise successful objects, known
strict failures and refusal to reuse an evidence directory. The deadline and
child-evidence implementation is shared with `test_differential --self-test`.

# Public assembler instruction-form coverage

Buster's checked encoders and its public assembler have separate coverage
contracts. A metadata representative proves an encoder route only. Public
coverage requires a legal mnemonic and operands, the applicable target
features, and an independently checked encoding or relocation interpretation.

This document describes the evidence interfaces inspected at
`a555b470ca17de0a027be1c19ffd8bf9f55819a7`. It does not claim exhaustive
Intel/AMD long-mode or Arm A64 support. The owning completion contract is
[#2930](https://github.com/buster14a/buster/issues/2930).

## x86-64 snapshot and dialects

The admitted snapshot has 11,013 forms, with 10,607 normalized direct encoder
representatives and 406 non-normalized rows. These are the constants in
[`x86_64_completion_census.h`](../src/buster/lib/compiler/assembly/x86_64_completion_census.h);
they are not an exhaustive architectural instruction denominator. Intel and
AT&T source reachability are evaluated independently.

Run the existing source-enabled census from a configured Release build:

```sh
build/Release/ide x86_64_completion_census --output=build/x86-form-census.txt
build/Release/ide test --module=x86_64_completion_census_tests --ci=1 --verbose=1
```

The schema-4 report includes every form's identity and each dialect's
classification, complete diagnostic observations, target features and checked
row/aggregate partitions. It also retains the exact synthesized Intel/AT&T
source as escaped bytes, each public assembler's complete output bytes and
relocations, and the selected x86 metadata form observed after its checked
emitter succeeds. The direct metadata emitter's bytes and relocation fields
are retained separately. A generated source that cannot be assembled still
has its source text, class and full diagnostics; no selected-form identity is
invented for a failed or non-metadata path. Witness counts, drops, per-entry
bounds and report integrity are explicit. The manifest has a fixed 64 MiB
capacity; overflow fails the command rather than publishing a truncated report.
These witnesses describe the admitted source and encoder paths, not an
independent architectural oracle for every form.

The command's successful report generation does not establish complete public
source coverage. Add `--require-source-complete` to require source-capable
results in both dialects; `--output` remains mandatory in that mode so a
failing source result retains its report. Inspect each dialect's exact, relocation-normalized, proven
alias, unresolved, byte-mismatch and relocation-mismatch classes. A different
legal encoding requires independent justification; it is not automatically
wrong code or an alias. Feature refusals remain visible.

## Admitted authority identities

The [generated manifest](../src/buster/lib/compiler/assembly/generated/manifest.json)
names Intel XED `v2026.07.15` at commit
`519c843c86547e2003f5a404a53358a7dcfb82f3` and LLVM `llvmorg-22.1.8`
at `ca7933e47d3a3451d81e72ac174dcb5aa28b59d1`. Both retained inputs are
reduced/checked-in projections with `raw_snapshot_provenance=false`.
The A64 LLVM projection has 7,491 rows and no alias source; its 2,881
SVE/SVE2 and 669 SME/SME2 family rows do not prove encoder or public support.

The [Arm canonical manifest](../src/buster/lib/compiler/assembly/generated/arm-a64-canonical-manifest.json)
names Arm A64 XML 2026-06, archive SHA-256
`63a01a1696483bbe2edfef9e0f0cd053d6c1c619ec0587876cb7a60bb344f354`
and top-level source-tree SHA-256
`0ee17fd2fe7ed165adda377d90f8f284d009e14d2300577f231c87ca6a45916d`.
The importer verifies the defined tree digest scope, not the archive bytes.
Its Apple-M1 feature closure selects 1,695 rows (1,523 canonical and 172
aliases); that is neither the complete A64 universe nor a silicon claim.
The [semantic manifest](../src/buster/lib/compiler/assembly/generated/arm-a64-semantic-manifest.json)
separately reports binding completion and `executable_semantic_rows=0`.
Existing specialized typed encoders supply their own checked semantics;
binding metadata alone does not supply them.

## AArch64 routes

At the inspected revision, source route and target profile must be reconciled:

| Forms | Internal authority and public boundary |
| --- | --- |
| CASP/CASPA/CASPL/CASPAL | Eight W/X memory candidates have LSE requirements; the existing public pair route admits only LDXP/LDAXP/STXP/STLXP. Candidate presence is not a public CASP witness. |
| LDAPR/LDAPRB/LDAPRH | Four memory candidates use RCPC; the scalar public memory lookup and fallback do not expose them. |
| LDTR/STTR | Nine LDTR and four STTR memory candidates lack a public text route. |
| BFC | A BFM/BFI source alias remains outside the base spelling set at this revision; count aliases separately from canonical opcodes. |
| CRC32 | Eight projected GPR rows use the generic public source/standalone route with CRC feature checks. The M1-named typed API retains its M1 contract. Generic-source regression coverage is tracked separately. |
| Register pointer authentication | Twenty-seven projected GPR rows use the generic public source/standalone route with PAuth feature checks. The M1-named typed API retains its M1 contract. Fixed PAuth spellings also use a generic-A64 route with per-row feature checks. |
| SVE/SVE2 and SME/SME2 | Present in the reduced LLVM snapshot and excluded from the provisional M1 profile. No full public-form completion is established by these projections or a target feature enum. |

These boundaries derive from
[`assembly.c`](../src/buster/lib/compiler/assembly/assembly.c),
[`aarch64_encoding.c`](../src/buster/lib/compiler/assembly/aarch64_encoding.c)
and the [memory projection](../src/buster/lib/compiler/assembly/generated/aarch64-memory-semantics.manifest.json).
Direct/complex SIMD and memory row universes are M1-derived; their typed
encoders validate requirements through canonical decoding. They must not be
described as universally M1-target-gated. The M1-named GPR typed API and
scalar/system projections have distinct profile restrictions; the public GPR
route accepts generic feature-valid A64 targets. Fixed/control/system-register
owners have distinct target rules.

Public A64 assembly routes through specialized semantic owners and the base
fallback. A fallback exclusion is not a whole-assembler exclusion. Inspect the
selected route and target features before claiming that a mnemonic is missing.
The registered single-instruction and standalone-object tests exercise different
surfaces; neither alone establishes an exhaustive form census.

```sh
build/Release/ide test --module=assembly_tests --ci=1 --verbose=1
build/Release/ide test --module=aarch64_base_assembly_tests --ci=1 --verbose=1
```

The fixture-derived native C corpus runner is owned by
[#2695](https://github.com/buster14a/buster/issues/2695). Its requested corpus
check is distinct from the native C codegen/runtime differential harness.
Historical constant-only corpus observations do not prove symbolic operands,
all target features, or a full Arm ISA denominator.

## Optional-extension inventory

The [revision-bound optional-extension inventory](agents/aarch64-optional-extension-source-inventory.json)
contains all 92 predicate tokens outside the provisional M1 policy. The pinned
reduced LLVM projection has 7,491 rows. Its SVE_SVE2 family has 2,881 rows,
all outside the Apple-M1 profile. The SME_SME2 family has 669 rows: 665 outside
and four in-profile. Those four are AUTDZA, AUTIZA, PACDZA and PACIZA, each
tagged HasPAuth; they do not establish SME support on Apple M1.

Predicate counts overlap across rows and tokens. Names containing _or_ encode
alternative requirements, so their totals must not be added as unique forms.
The provisional Arm XML cross-check has 1,695 selected rows (1,523 canonical
and 172 aliases) and is a separate denominator. Metadata classification and
profile exclusion do not prove public source reachability or rejection.

The hosted workflow records independent LLVM words and current public source
results for PTRUE_S (`ptrue p0.s, pow2`) and ADDHA_MPPZ_S
(`addha za0.s, p0/m, p0/m, z0.s`). These bounded seeds leave broader SVE/SVE2,
SME/SME2 and optional-extension source coverage unfinished. The per-row
owner reconciliation is tracked by [#3150](https://github.com/buster14a/buster/issues/3150).

## Hosted observations

[`assembler-form-evidence.yml`](../.github/workflows/assembler-form-evidence.yml)
builds the exact candidate on a GitHub-hosted x86-64 correctness executor,
captures both x86 dialects, and runs the registered assembly modules. Its
artifact binds the source commit/tree, x86 snapshot blob, compiler binary
digests, toolchain and executor specifications to the observations. The
workflow retains failed observations and leaves required CI unchanged. Its
independent ELF control assembles coherent ADRP+ADD and ADRP+LDR pairs,
links them against a second object's definition using LLVM lld, and compares
the relocated text against Clang at placements around a page boundary.

Assembly is encode-only for privileged and optional instructions: an executor's
ability to run those instructions is not an assembly legality check. These
observations are correctness evidence, not performance validation.

## Provenance and license boundaries

[`generated/README.md`](../src/buster/lib/compiler/assembly/generated/README.md)
owns metadata provenance and import boundaries.
[`LICENSES/README.md`](../LICENSES/README.md) records that Buster's first-party
license remains unselected (#621), the Intel XED and LLVM notices, and unresolved
Arm input/derived-projection permissions. This evidence path adds no external
assembler production dependency and imports no specification dataset.

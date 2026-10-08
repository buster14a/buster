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

The command's successful report generation does not establish complete public
source coverage. Inspect each dialect's exact, relocation-normalized, proven
alias, unresolved, byte-mismatch and relocation-mismatch classes. A different
legal encoding requires independent justification; it is not automatically
wrong code or an alias. Feature refusals remain visible.

## AArch64 routes

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

## Hosted observations

[`assembler-form-evidence.yml`](../.github/workflows/assembler-form-evidence.yml)
builds the exact candidate on a GitHub-hosted x86-64 correctness executor,
captures both x86 dialects, and runs the registered assembly modules. Its
artifact binds the source commit/tree, x86 snapshot blob, compiler binary
digests, toolchain and executor specifications to the observations. The
workflow retains failed observations and leaves required CI unchanged.

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

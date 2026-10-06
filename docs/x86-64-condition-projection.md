# Ordinary x86 condition projection

The ordinary Jcc, SETcc and CMOVcc families share one 16-row condition identity
and spelling table, `src/buster/lib/compiler/assembly/x86_64_conditions.inc`.
It replaces the assembler's suffix-to-nibble table, alias-to-canonical table,
four individual aliases and CMOV name list, plus the backend's separate
JA/JNE canonicalization and SETcc name projections. This is a syntax/identity
projection over the existing XED encoding authority, not another ISA database.

## Authority and consumers

| Fact | Owner |
| --- | --- |
| Established condition nibble, canonical condition-family names and accepted suffix aliases | `x86_64_conditions.inc` |
| Opcode bytes, relative/immediate fields, register/memory legality, prefixes, features and architectural effects | Existing generated XED metadata and checked selection/emission in `x86_64_metadata.c` |
| Intel/AT&T operand order, typed CMOV suffixes, source diagnostics and branch layout | Existing source assembler |
| IR comparison mapping, floating unordered handling, branch inversion/layout and scheduler policy | Existing machine backend |
| Snapshot-specific form IDs/hashes, fixed operands and compound sequence shapes | Existing exact recipe bindings, checked against the shared expected condition before publication |

The C preprocessor derives `BusterX86Condition`, the existing
`MachineX64Condition` numeric identities and the immutable spelling description.
`buster_x86_metadata_condition_parse` accepts an exact ASCII case-insensitive
suffix; failed parsing preserves its output.
`buster_x86_metadata_condition_mnemonic` gives the static canonical name for a
family and condition, or an empty string for an invalid index.
`buster_x86_metadata_condition_canonical_mnemonic` leaves excluded or unknown
full names unchanged. Metadata lookup, physical selection and explicit-form
emission all use that same canonicalization boundary. The source assembler's
literal-mnemonic precedence and width rules remain separate.

No new generator, grammar, import, dependency or checked-in generated snapshot
is needed for sixteen rows. A separate generator would duplicate parsing and
require new stale-output ownership for this small projection. Compile-time
inclusion means the enum and spelling consumers cannot retain an old generated
copy. Ordinary unit/build/self-host gates compile the current rows; independent
witness tests reject disagreement with the unchanged XED snapshot. Existing
XED import checksum/count validation remains responsible for its own generated
artifacts. Dense form IDs, hashes, shape-cache IDs and native-retirement bindings
are unchanged.

## Bounded contract

There are sixteen nibbles and thirty accepted suffixes, giving exactly ninety
ordinary full names across the three families. Complementary conditions differ
in bit zero. The encoding/effect facts below are a description of the existing
XED contract and are deliberately not new fields in the spelling table.

| Family in 64-bit execution mode | Operand/field contract | Feature and effects |
| --- | --- | --- |
| Jcc short/near | Signed rel8/rel32; short range -128 through 127, near range -2147483648 through 2147483647. Opcodes 70+cc or 0F 80+cc. Relative distance is from the end of the instruction; unresolved-symbol width/layout remain source/object policy. No arithmetic immediate. | Base instruction; reads condition flags, conditionally changes RIP; no flag write. |
| SETcc | One r/m8 destination, no immediate. 0F 90+cc, ModRM destination. AH/CH/DH/BH exclude REX; SPL/BPL/SIL/DIL and r8b-r15b require the corresponding prefix/extension. LOCK is invalid. | Base 386 family; reads condition flags, writes the destination byte, preserving other register bits; memory destination writes a byte. |
| CMOVcc | GPR16/32/64 destination and equal-width GPR or memory source; no byte destination or immediate. 0F 40+cc, ModRM.reg destination, ModRM.r/m source, operand-size and REX.W determine width. LOCK is invalid. | CMOV feature; reads condition flags and source; XED describes the destination operand role as conditional write. This does not describe all width-dependent full-register effects, particularly the 32-bit form in 64-bit mode. A memory source is read regardless of whether the destination operand changes. The table does not infer memory/scheduling effects from its condition name. |

Intentional exceptions: J[ER]CXZ, LOOP/LOOPE/LOOPNE, x87 FCMOV, APX
CCMP/CTEST and SETccZU are not ordinary condition-family names here. Jcc and
SETcc do not gain typed AT&T width suffixes. `setb`, `setl`, `jb` and `jl`
retain their literal condition meanings before suffix stripping; typed CMOV
aliases such as `cmoveq` remain source syntax. The backend's zero sentinel for
an unsupported comparison remains compiler policy even though overflow is
architectural nibble zero. Floating comparisons retain their predicate order
and separate parity handling.

## Independent checks and regeneration

The registered `x86_64_metadata_tests` module uses an independently authored
inventory of thirty suffix/nibble pairs and sixteen canonical byte witnesses;
it never includes the production condition table to create its expectations.
It joins all ninety aliases through physical selection and emission, and both
source dialects, then checks operand widths, relative limits, prefix rejection,
wrong-family/wrong-condition form rejection and failure atomicity. Machine tests
join retained sequence bindings to the expected condition and deliberately
substitute a neighboring valid ID/hash so identity-only validation cannot pass.

`docs/x86-64-condition-oracle.s` contains explicitly authored instructions,
not byte directives or macro expansions of the production table. Independent
assembler regeneration on a Linux x86-64 cloud runner is:

```sh
as --64 docs/x86-64-condition-oracle.s -o gas.o
clang --target=x86_64-linux-gnu -c docs/x86-64-condition-oracle.s -o llvm.o
objcopy -O binary --only-section=.text gas.o gas.bin
objcopy -O binary --only-section=.text llvm.o llvm.bin
cmp gas.bin llvm.bin
objdump -dr -Mintel gas.o
objdump -dr -Mintel llvm.o
```

Inspect relocation records separately for external PC-relative references;
agreement on zero placeholders alone does not validate relocation semantics.
Use the ordinary hosted full regression, sanitized, mode and self-host gates
for Buster itself. Exact revisions and actual results belong on the owning
issue/PR; these commands are not a claim that an unrun check passed. This change
makes no throughput claim. Qualified-host performance acceptance is pending
unless separately measured; the laptop and benchpress/9700X are not test hosts
for this work.

## Provenance and expansion boundary

The mapping is consolidated from Buster at
`6fc08ec475694aeb3b1062c1d390483a69668508`. Instruction contracts and canonical
names were checked against Intel XED at
`519c843c86547e2003f5a404a53358a7dcfb82f3`, specifically
[datafiles/xed-isa.txt](https://github.com/intelxed/xed/blob/519c843c86547e2003f5a404a53358a7dcfb82f3/datafiles/xed-isa.txt).
That source carries Copyright (c) 2026 Intel Corporation and Apache-2.0 terms;
the pinned [LICENSE](https://github.com/intelxed/xed/blob/519c843c86547e2003f5a404a53358a7dcfb82f3/LICENSE)
matches `LICENSES/intel-xed-LICENSE.txt`, Git blob
`7b1fcae7b322e9270a48a68ddc374870069f3533`. Existing attribution is retained in
`THIRD_PARTY_NOTICES.md`. No upstream prose, source implementation or restricted
instruction manual was imported. The oracle and test witnesses are original
test material. Buster's first-party license remains unselected as documented
in `LICENSES/README.md` and #621; public access does not supply permission.

The existing generated manifest declares XED's pin separately from its
checked-in input identity and sets `raw_snapshot_provenance=false`. This
bounded primary-source check does not establish raw regeneration provenance
for the entire existing snapshot.

Expand only when another closed family has a concrete duplicated fact, a clear
encoding/syntax/policy ownership split, and an independent witness that detects
the same plausible mistake in multiple consumers. Derive consumers from the
existing normalized metadata where possible. Keep target-specific lowering and
compound recipe policy readable; do not add a specification language or pack
unrelated semantics into the condition table.

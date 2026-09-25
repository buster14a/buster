# C frontend and canonical IR reference

[Agent instructions](../../AGENTS.md) · Source paths are relative to the repository root.

Search the guide matching the affected symbols or behavior. These notes preserve
the original investigation order; cross-references to “above” or “below” follow
the table. Historical failure descriptions are evidence to reproduce, not a
substitute for inspecting the current implementation and fixtures.

| Area | Guide |
|---|---|
| Pipeline, ownership, diagnostics, canonical IR, expression places and qualifiers | [Foundations](frontend/foundations.md) |
| Semantic-only validation, lowering constraint inventory, diagnostic and allocation regression contract | [Semantic validation](frontend/semantic-validation.md) |
| Relocations, weak/alias symbols, constructors/destructors, object formats and linker | [Linkage](frontend/linkage.md) |
| Packed/aligned types, bit-fields, layout engines | [Layout](frontend/layout.md) |
| Atomic layout, argument classification, loads/stores, conversions | [Atomics](frontend/atomics.md) |
| Declarators, typeof, conditional types, calls, JIT/driver boundaries | [Calls](frontend/calls.md) |
| `_Float16`, long double, x87, static folding, global/inline assembly, Wasm boundary | [Wide floats and assembly](frontend/wide-floats-assembly.md) |

Layout and atomic ABI work often needs both the layout and atomics guides.
Changes to places or calls also need the foundations guide. Native selection and
allocation invariants live in [the machine guide](machine.md); command-line
options and action dispatch live in [the driver guide](driver.md).

## Preprocessor include identity

The once-file index shared by `#import`, `#pragma once` and proven whole-file
include guards keys descriptor-backed files by `FileIdentity`, not by their
resolved path spelling. POSIX identity is device/inode and Windows identity is
volume serial/file index, captured from the same descriptor that supplied the
bytes. Lexical aliases, hard links, followed symbolic links and case aliases on
case-insensitive filesystems therefore share one suppression record. Builtin
headers and Android APK assets remain in their normalized path namespaces.

The first resolved spelling remains the diagnostic/source-map spelling and the
per-path metrics key; canonical identity never rewrites user-facing paths. The
open-addressed table stays at most half full, diagnoses invalid identity or
arena exhaustion, and records probe counts only in tests. `c_once_tests` covers
real aliases for all three suppression mechanisms plus an end-to-end fan-out
and depth workload whose actual slot examinations must scale near-linearly.
The end-to-end workload bounds probes against its own include operations;
physical device/inode hashes vary between simulator app containers, so probe
counts from two independently created file sets are not a stable ratio. The
direct table workload retains its cross-size ratio check on fixed path keys.

## Builtin capability queries

`c_conditional_builtin_supported` answers `__has_builtin` for implemented
operations, not every recognized identifier. Its complex and atomic branches
reuse the exact `c_symbol_builtin_from_spelling` classification: adding a new
spelling there requires checking its real lowering and its query regression.
Never admit an arbitrary `__atomic_` or `__c11_atomic_` suffix by prefix.

The active target admits atomic builtins on x86-64/AArch64, and complex
construction there and on Wasm64. Wasm64 and eBPF reject atomic IR; eBPF also
rejects floating IR. Operand types and access widths are still validated by
lowering. A positive runtime builtin query does not assert that the builtin
can be folded in every constant initializer; the complex global-initializer
work is tracked separately in #675.

`c_test_has_builtin` covers exact positive/negative spellings through real
preprocessing and parsing on eight targets, plus fence IR on both frontend SSA
paths. `compiler_driver_test_has_builtin_targets` checks non-native output;
`tests/basic_c_has_builtin.c` exercises every new positive operation under
strict verification and all native allocator modes. New tracked fixtures also
need an explicit, reviewed identity in `docs/native-retirement-support-v1.tsv`;
do not bypass its unreviewed-input rejection to make a query test pass.

`__builtin_ffs`, `__builtin_ffsl`, and `__builtin_ffsll` convert their single
evaluated argument to `int`, `long`, and `long long` respectively, return an
`int` one-based least-significant-set-bit index, and return zero for zero.
Lowering uses canonical CTZ with a nonzero operand even for the zero case.
The capability query admits all three implemented spellings. The `long`
conversion follows the target data model, including 32-bit `long` on Windows.
`__builtin_clz`, `__builtin_ctz`, and `__builtin_popcount` and their `l`/`ll`
variants instead take unsigned int/long/long long respectively and return
signed int. Semantic expression queries and lowering share the spelling policy;
the canonical count operation runs at the converted operand width, and its
result converts to int before the surrounding C expression uses it. Keep
clz/ctz runtime oracles on nonzero inputs.

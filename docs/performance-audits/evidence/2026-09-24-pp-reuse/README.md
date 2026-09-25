# Preprocessing reuse: definition-owned argument demand, not expanded results

## Decision and exact scope

Retain compact, definition-owned ordinary-argument demand in the existing
per-parameter count storage. Do not introduce a result cache. This fixes the
observed compatibility defect #1091; it does not claim a measured speedup.

Investigation base: `2e942e80a87666409cf29d3e24a68d322b9e71fd`, tree
`f976bfd7cdb8830a8a15d959ef36f0152f1b767d`. Baseline `c_source.c` blob:
`64533fb96983978e7cd3f481ce79491143ea515d`.

The tested two-file implementation was published as
`8e3ad8a17528f0acfee16bfec78bd090c8b46d7d`, tree
`8c367ddcffd84b3a3c09c3d80fe58cfe962aacff`. Its exact source blobs are:

- `src/buster/lib/compiler/frontend/c/c_source.c`:
  `ca90f7e29ac1cb866b8567e5f1c3e400bdcedb1b`.
- `src/buster/tests/compiler/frontend/c/macro_conditional_test.c`:
  `48449e8153560248bedd989d5a7238cfe75f6210`.

Read AGENTS, frontend/foundations, testing, build and benchmarking guides,
`tools/throughput/DEDICATED.md`, and latest audit at the pinned base,
`2026-09-23T150600Z.md`. Reconciled #51, already-landed task-stack work, #575 and
#583's direct-production proposal. The inspected #1008 changed-file list has no
overlap. Ownership and coordination are recorded on #51 and #1091.

All execution was standard GitHub-hosted Linux correctness/source counting.
There was no desktop/SSH benchmark, 9700X job, execution-policy change, generated
binding change or merge. The current 9700X guide admits only a fixed smoke recipe,
not this full-compile performance experiment. Performance remains UNMEASURED.

## Actual repeated work and an important negative result

The final successful capture is run `36002221738`; counts below are its own
frozen self-host object compile, not pooled with earlier hosted captures.

| Observation | Count |
| --- | ---: |
| Replacement calls / distinct macro names | 133,844 / 1,234 |
| Existing paste/stringize-free replacements | 107,864 |
| Argument preparations | 81,351 |
| Arguments already aliasing raw tokens | 80,265 |
| Arguments entering a child expansion | 1,086 |
| Arguments with no ordinary expanded use | 26,875 |
| Raw tokens in those arguments | 27,373 |
| Unnecessary child expansions among those arguments | 1 |

More than 98% of arguments already avoid child expansion. The single unnecessary
child expands `MACHINE_QUALITY_MAXIMUM_CANDIDATES`, passed to the ignored argument
of `BUSTER_QUALITY_COUNT`. This falsifies a large repeated-full-argument-expansion
opportunity on this input. Most removable work is a short raw-token scan.

The existing macro-paste regression input has 90 preparations, 78 without
expanded use, 42 empty arguments, and three unnecessary child expansions.
`basic_c_operations.c` is a no-benefit control: 30 preparations, none raw-only.

Paste lexing is a separate real repetition: 26,680 lexer calls over 4,816 joined
spellings, with 21,864 repeats after each spelling's first occurrence. The most
frequent spelling is `0x0000001fU`, 3,620 times. This supports a lexical-reuse
hypothesis, not proof that hashing, equality, misses and retained storage beat the
lexer or a simpler direct numeric-paste path. Macro-name repetition is not a
cache-hit count: definitions and expansion environments can differ.

Earlier run `36001346822` observed 81,515 preparations and 26,692 paste calls.
These are separate hosted populations. No cross-run byte identity, normalized
include/target environment or timing comparison is asserted. All object equality
checks below compare compilers within the same capture on identical frozen input
paths, flags and output path.

## Existing sharing and the chosen dependency boundary

Definitions already retain replacement tokens, formal indices, use counts,
spacing and paste/stringize flags. Symbols already provide dense macro lookup.
Expanded arguments are already computed once per invocation and shared by its
ordinary occurrences; no-defined-macro arguments already alias their raw tokens.
Physical-file once/guard identity already suppresses eligible include aliases.

The correction repurposes `parameter_use_count` as `parameter_expand_count`,
counting ordinary occurrences only. A use under `#` or immediately beside `##`
needs the raw view. ANY ordinary occurrence still demands one prescan, including
mixed ordinary/stringized/pasted use. Pragma-like operands preserve their explicit
expanded-argument contract despite an empty replacement list.

The retained metadata depends only on the current definition's formal-index
mapping, replacement operator positions and pragma-like mode. It does not depend
on live macro lookups, argument spelling, invocation location or disabled state.
Definition/redefinition constructs it; undefinition prevents use; push/pop
snapshots copy and restore it with the definition. A reused `CMacro *` is not a
version key. Counts are unchanged on the paste/stringize-free capacity path,
where every formal occurrence is ordinary.

There is no extra retained field, array or allocation. Construction adds local
operator checks at formal occurrences. Every argument pays the demand guard,
including ordinary-only and low-reuse inputs; zero-demand arguments avoid their
raw scan and any erroneous child expansion. Raw collection, ordinary prescan,
rescanning and source-map work remain. The guard can be comparable to a one-token
scan: no net full-compile time or peak-RSS improvement is claimed. Correctness,
not a presumed performance gain, justifies this implementation.

## Counterexamples before implementation

The unchanged compiler rejects these three cases in C17 and GNU17; Clang 21.1.8
and GCC 15.2.0 produce the shown tokens:

```c
#define BAD(x,y) x+y
#define RAW(x) #x
#define UNUSED(x) 7
#define PREFIX(x) prefix##x
RAW(BAD(1))       // "BAD(1)"
UNUSED(BAD(1))    // 7
PREFIX(BAD(1))    // prefixBAD ( 1 )
```

The erroneous prescan diagnoses an invocation that must never happen. Mixed-use
controls deliberately retain that nested diagnostic when an ordinary use exists.
Registered tests check exact token kinds/spellings, expansion counts and limits,
logical file/line/column attribution, redefinition, snapshots, empty/variadic
arguments, nested disabled macros, pasted-name lookup and expanded pragma operands.

Independent CLI counterexamples also cover positive and negative binding changes;
`A -> F` before/after function-like definition and with a pending `(argument)`;
recursive no-expand painting; whitespace-sensitive stringization; different
`#line` mappings; pragma effects; symlinked once/guard includes; guard undefinition;
and deliberately reincluded unguarded headers under different `ITEM` definitions.
The CLI token comparator is restricted to these ASCII fixtures, not a general C
lexer. The registered C tests use the actual lexer and source-location APIs.

## Why not retain larger results?

A lexical entry could retain only classification/validity for exact joined bytes
under the same lexer configuration and length policy. It must not retain a
scratch `CLexResult`, source offsets/stamps, diagnostic location, no-expand paint,
macro binding or side effects. New tokens need correct lifetime, spacing, symbol
handling and rescan; invalid-paste diagnostics belong to the current invocation.
This boundary is semantically plausible but economically unqualified here.

A fully expanded result requires transitive positive AND negative lookups,
define/undef/push/pop versions, disabled and painted-token state, raw argument
spacing, empty/variadic distinctions, names created by pasting, and the pending
input/task suffix and context floor. `A` expanding to function-like `F` can consume
following parentheses, so its replacement and arguments alone are insufficient.

Semantic tokens, source provenance, observable effects and accounting must remain
separate. The current dynamic builtin enum implements `__LINE__` and `__FILE__`.
`__DATE__` and `__TIME__` are fixed-epoch ordinary definitions. The runtime probe
leaves `__COUNTER__`, `__BASE_FILE__` and `__INCLUDE_LEVEL__` as unbound identifiers;
no nonexistent counter effect is assumed. `_Pragma` effects execute anew. Windows
`__pragma` shares the source-confirmed pragma-like path but was not executed here.
Conditional feature/include queries remain outside retained metadata and must
still use current context; no new coverage claim is made for those queries.
Expansion budgets must fail at the actual invocation, not after replaying an
aggregate count. Reusing spellings never permits reusing earlier source stamps.

All final expansions remain uncached, even constants. Dynamic builtins, pragma
effects, pasted-identifier rescans, diagnostics and deliberate reinclusion are
never bypassed. No persistent/cross-TU cache, PCH, new IR or task-storage rewrite
is introduced. Lexical reuse versus direct numeric-paste handling is an optional
future experiment, not a prerequisite and not part of #36.

## Reproducible validation

Successful [run 36002221738](https://github.com/buster14a/buster/actions/runs/36002221738)
used transport commit `4b87c876e025e276f4a618c12dcb05357f8c8ad3`, standard
`ubuntu-26.04`, image `20260920.143.1`, Clang 21.1.8 and GCC 15.2.0.
[Artifact 10809146785](https://github.com/buster14a/buster/actions/runs/36002221738/artifacts/10809146785)
has ZIP SHA-256
`bdb334c1625660ba21d90780acd51487b35f429f066126ce5e31f9a64a21b0d5`.
Its GitHub retention expires October 1, 2026; retain a downloaded copy for replay.

| Check actually run | Result |
| --- | --- |
| Original 15 counterexamples, C17 and GNU17 | Baseline 24/30; candidate 30/30; Clang and GCC 30/30 each |
| 19 additional cases, both dialects | 38/38 three-compiler agreements (114 process executions) |
| Registered macro/conditional module with added regressions, unchanged compiler | 590/624 assertions; 34 failures |
| Same registered module, candidate | 632/632 assertions; 1/1 module |
| Frozen self-host, macro-paste and basic-operations objects | All three baseline/probe/candidate objects byte-identical within this run |
| Two-generation self-host correctness, baseline and candidate separately | Both fixed points byte-identical |
| Patch checks and final `git diff --check` | Passed |

The selected-module runner change was diagnostic-only, recorded in
`diagnostic-module-selection.patch`, and restored. It is not published; ordinary
CI remains unchanged. Existing module Clang/GCC and native runtime mode checks
also ran. The baseline/candidate assertion totals differ because comparison loops
and guarded assertions depend on output shape.

Candidate binary SHA-256:
`09b8fd0a0e91a9db5b9ce7cdca1967e59b9262f86d287358f4c729aee1183495`.
Self-host stage1 = stage2 for baseline:
`9a4aeb8f07e16e86b6b3f62e68cc8f0854aca6ccf197d5e67a25e653168d98f8`;
for candidate:
`0ad605e0b836a0468ed0499bd61bd0bf1e6cb3264592603703f9a39f0cea9bee`.
Matched objects:

- Self-host: `fdfe9603695755323f78c03ed10cb772f9412eeb9abfdb6f1c5ba104107ca54e`.
- Macro-paste: `70d05b7df0aa42478af20ababa884ef4f1ee82e6691b028bfb169ff14c66070d`.
- Basic operations: `67283fb129737a3c0bc98e1895b81436178d9ab3bf5e2a732afe6e0cbe08cb59`.

The artifact contains exact argv, compiler banners, frozen repository inputs,
raw outputs, patches, JSON observations and executable hashes. The historical
transport commit retains `probe.py`, `probe_v2.py`, `change.patch` and
`.github/workflows/pp-reuse-probe.yml`. In a disposable standard-hosted checkout of
that exact commit, execute its normalization/preflight step and then
`python3 docs/performance-audits/evidence/2026-09-24-pp-reuse/probe_v2.py`.
Do not execute the historical publisher or reset the owner branch to replay it.
The native build driver owns construction. Self-host runs the documented two
`cc ... ide.c -lm -o stage` invocations, without the benchmark subcommand.

Attempts `35999613904` and `36001346822` failed respectively on a diagnostic
source anchor and multi-file diff transport, before candidate validation. Their
partial observations were retained, not relabeled passing. The successful capture
reran those observations and validated the candidate. Temporary workflow and
transport files are removed from the final proposed tree, preserving history.

Full regression, sanitizer and platform/mode matrix completion are not established
by this focused job. Ordinary PR checks and review remain required. Performance,
including construction, validation, misses, retained memory and low-reuse cost in
full compilation, remains UNMEASURED; nothing in this result waives those costs.

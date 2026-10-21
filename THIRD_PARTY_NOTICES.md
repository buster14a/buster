# Provenance and third-party notices

Reviewed October 20, 2026 against all 30 tracked files at
[`c7eadb9989c48767f81c51a3b052a136bbb436ec`](https://github.com/buster14a/buster/tree/c7eadb9989c48767f81c51a3b052a136bbb436ec).
The earlier issue pin `d13038574531b064cc87b133bb97665d1728f60a` differs only
in `.github/workflows/9700x.yml`, `AGENTS.md` and `docs/coding-style.md`.
Those changes add the C-language rule, Markdown-only CI exclusion and stable
repository-ID check, with no new imported implementation. The latter is the
only change since the initial inventory at `76cab868a233fb81dac33933861a7b7d8d554649`.
This adoption adds legal texts, documentation
and source comments; it changes no executable statements.

The first-party grant and maintainer's historical-authority confirmation are
in [LICENSING.md](LICENSING.md). The inventory distinguishes provenance from
that confirmation. Existing upstream grants authorize the identified imports;
the maintainer's confirmation does not purport to grant anyone else's rights.

## Path and portion inventory

Paths below are relative to the repository root. "Default" means
`Apache-2.0 WITH LLVM-exception` for original material.

| Paths or portions | Origin and disposition |
| --- | --- |
| `src/buster/compiler/c_lexer.c` | Adapted/generated Accelerated-Zig-Parser lexer, with simdjson, Clang and identifier-data provenance described below; component terms, not the first-party default over the whole file. |
| `src/buster/compiler/c_lexer_test.c`, `src/buster/compiler/c_lexer_benchmark.c` | Ports of upstream `c/c_tokenizer_test.c`; MIT, including local changes. |
| `src/buster/compiler/lane.c` | Adapted RAD Debugger interface/base-layer patterns with a locally implemented barrier, scheduling and allocation; MIT, including local changes. |
| `src/buster/compiler/x86_64_encoder_benchmark.c`, `src/buster/compiler/object_writer_benchmark.c`, `src/buster/compiler/lane_benchmark.c` | Original benchmark bodies under Default; shared counter/timing/sort helpers adapted through the lexer benchmark retain MIT. |
| `src/buster/compiler/x86_64_encoder.c`, `src/buster/compiler/x86_64_encoder_test.c` | Original encoder, handwritten form families and tests; Default. Tool-oracle and catalogue distinctions below. |
| `src/buster/compiler/main.c`, `src/buster/compiler/main_test.c`, `src/buster/compiler/lane_test.c` | Original integration, input handling, loader regressions and lane tests; Default. The loader fixtures and address-space-limit checks are written for this project; no external fixtures or implementation are imported. |
| `src/buster/compiler/dataflow.c`, `src/buster/compiler/dataflow_program.c`, `src/buster/compiler/dataflow_test.c`, `src/buster/compiler/dataflow_benchmark.c`, `docs/dataflow.md` | Original liveness/interference kernels, synthetic CFG generator, tests, benchmark and documentation; Default, with the public-domain SplitMix64 seed-mixing reference below. No external corpus is incorporated. |
| `src/buster/compiler/preprocessor_fuzz*.c`, `docs/preprocessor-fuzz.md` | Original independent test harness, generator, comparator, minimizer, tests and benchmark helpers; Default. Generated test programs are produced by this repository's C generator from a recorded seed, case ID and language mode. Clang 23 is an external oracle; no compiler implementation or external test fixtures are imported. |
| `src/buster/compiler/call_graph.c`, `src/buster/compiler/call_graph_test.c`, `src/buster/compiler/call_graph_capture/*` | Original call graph check, tests and IR-capture fixture; Default. LLVM IR's text format and Tarjan's algorithm are referenced, not copied. The tests' IR is hand-written, or clang 23's output for C snippets written for them and recorded beside it, cut down by hand; clang runs externally and is not vendored. |
| `src/buster/compiler/link.c`, `src/buster/compiler/link_program.c`, `src/buster/compiler/link_test.c` | Original representation, synthetic program generators and tests; Default, with the public-domain generator reference noted below. |
| `src/buster/compiler/object_writer.c`, `src/buster/compiler/object_writer_test.c` | Original ELF writer and tests; Default. ELF constants describe the format; no external linker implementation is vendored. |
| `src/buster/compiler/executable.c`, `src/buster/compiler/executable_test.c`, `src/buster/compiler/executable_benchmark.c` | Original writer of static executables and dynamic PIEs, its vector passes and exact twins, and its tests and benchmarks; Default, with the generator reference below. The ELF dynamic sections, RELR and `.gnu.hash` are written from their specifications, and the layout, tags and encodings from lld's and GNU ld's documented behavior and observed output, not copied. |
| `src/buster/compiler/dynamic.c`, `src/buster/compiler/dynamic_test.c` | Original inputs of a dynamic PIE from the C runtime and binding, and the dynamic PIE's tests; Default. The program interpreter's path is the one GCC's driver passes for x86-64 glibc, a fact, not copied code. The tests make their programs themselves and run them under the system's loader; `ld.lld`, `cc` (which builds the preloaded library whose `write` walks the stack with libgcc's unwinder), `objdump` and `readelf` are external oracles for `--cross-check`, not vendored. |
| `src/buster/compiler/snapshot.c`, `src/buster/compiler/snapshot_test.c`, `src/buster/compiler/snapshot_benchmark.c` | Original runtime snapshot (its file, writer, loader and validation) and its tests and benchmark; Default. The cache directory follows the XDG base directory specification, a specification, not copied code. The tests and the benchmark read the system's C runtime at run time without copying it. The benchmark's clock and median helpers are its own. |
| `src/buster/compiler/import.c`, `src/buster/compiler/runtime.c`, `src/buster/compiler/import_test.c`, `src/buster/compiler/import_benchmark.c` | Original readers of ELF relocatable objects, archives, shared libraries and linker scripts, their vector and exact paths, the C runtime's search and import, and their tests and benchmark; Default. The formats are read from their specifications. The archive header's vector size parse uses the widely published multiply-add method for decimal digits (as in simdjson's and Wojciech Muła's write-ups), written here from the method, not copied. The tests and the benchmark make their inputs themselves, and read the system's C runtime files and the libraries and archives named on the command line at run time without copying them; `readelf` is an external oracle for `--cross-check`, not vendored. The benchmark's clock, counter and median helpers are its own, not the lexer benchmark's MIT ones. |
| `src/buster/compiler/bind.c`, `src/buster/compiler/bind_test.c`, `src/buster/compiler/bind_benchmark.c` | Original binding of the C runtime into a program's link, its vector and exact paths, and its tests and benchmark; Default. GNU ld's and lld's symbol resolution are described from their documented behavior and observed output, not copied. The tests make their programs themselves and read the system's C runtime at run time without copying it; `ld.lld`, `ld.bfd`, `cc` and `readelf` are external oracles for `--cross-check`, not vendored. The benchmark's clock, counter and median helpers are its own. |
| `src/buster/compiler/c_intern.c`, `src/buster/compiler/c_refine.c`, `src/buster/compiler/c_refine_test.c` | Original intern table, token refinement and tests; Default. The hash's constants are wyhash's public-domain ones, and the keyword list is clang-generated data; both are noted below. |
| `src/buster/compiler/c_preprocessor.c` | Original preprocessor; Default, with the clang-generated predefined macros and feature tables noted below. |
| `src/buster/compiler/c_preprocessor_test.c` | Original preprocessor tests; Default. Some expected texts are clang 23's `-E` output for the cases' own inputs. `pp_test_keyword_candidates` and `pp_test_feature_names` list names from clang 23's `TokenKinds.def` and companions and from its `Features.def` (names only, noted below). |
| `src/buster/compiler/c_preprocessor_benchmark.c` | Original benchmark body under Default; the counter/timing/sort helpers adapted through `c_lexer_benchmark.c` retain MIT. |
| `src/buster/compiler/c_refine_benchmark.c` | Original benchmark body under Default; the corpus loader and the counter/timing/sort helpers adapted through `c_lexer_benchmark.c` retain MIT. |
| `src/buster/compiler/c_types.c`, `src/buster/compiler/c_types_test.c`, `src/buster/compiler/c_types_benchmark.c`, `docs/c-types.md` | Original type table, hash-consing, record layout, conversion kernels, compatibility, composite types and printer, and their tests, benchmark and documentation; Default, with the public-domain SplitMix64 reference below. They follow clang 23's behaviour: clang's source (`ASTContext`, `ItaniumRecordLayoutBuilder`, `SemaExpr`'s conversions, `mergeTypes`, `TypePrinter`) was read for its rules and none of it is copied. clang runs externally as the tests' oracle on C the tests generate, and is not vendored. The benchmark's clock, counter and median helpers are its own, not the lexer benchmark's MIT ones. |
| `build.sh`, `.gitignore`, `.github/workflows/9700x.yml` | Original build/configuration; Default. The referenced GitHub action runs externally and is not vendored. |
| `.claude/settings.json`, `.claude/hooks/join-tailnet.sh` | Maintainer configuration: settings adapted from his older Buster configuration; locally added tailnet hook. Default. See the recorded import below. |
| `.githooks/commit-msg`, `.githooks/enable-sign-off.sh`, `.codex/hooks.json` | Original hooks that add the maintainer's sign-off where his opt-in is set, and Codex's configuration that runs one; Default. |
| `AGENTS.md`, `CLAUDE.md`, `docs/coding-style.md` | Original project/contributor guidance; Default. The style adapts conventions, not a grant to import old compiler code. |
| `docs/c-lexer.md`, `docs/x86-64-encoder.md`, `docs/object-writer-and-linker.md`, `docs/c-refinement.md`, `docs/c-preprocessor.md` | Original design/measurement reports; Default. Referenced tools, articles and measurements are distinguished from vendored source. |
| `README.md`, `LICENSING.md`, `CONTRIBUTING.md`, this file | Original adoption documentation; Default, except quoted upstream notices retain their terms. |
| `LICENSE`, `DCO`, `LICENSES/*` | Retained legal/attribution texts; their own terms. These are not original project prose. |

## Accelerated-Zig-Parser and simdjson

Source: [davidgmbb/Accelerated-Zig-Parser at
`f4482e2dd53413522d75d8c254b9a824021238ce`](https://github.com/davidgmbb/Accelerated-Zig-Parser/tree/f4482e2dd53413522d75d8c254b9a824021238ce).
The exact root license is retained as
[LICENSES/Accelerated-Zig-Parser.txt](LICENSES/Accelerated-Zig-Parser.txt),
including **Copyright (c) 2023 Niles Salter**, the MIT grant and the Apache-2.0
terms for marked simdjson/simdjzon-derived portions.

`c_lexer.c` combines `c/c_tokenizer.h`, `c/c_tokenizer_tables.h`,
`c/c_tokenizer_unicode.h`, `c/c_tokenizer_scalar.c` and `c/c_tokenizer.c`.
Import commit `6c602cd` and its section markers identify them. The lexer tests
and benchmark come from `c/c_tokenizer_test.c` (local commits `832eaf8` and
`d89d63e`). Local formatting, bounds handling, command integration, tests and
optimizations are modifications; the source headers and Git history record
them. The embedded upstream license is retained.

The upstream C implementation was introduced in `d0c062147011361411d1677eaec9bf69a3881bf8`
after the compacting-tokenizer port in
`178a2d27fe7a50d642cb0a48f193ee9723f49633`. That port identifies Niles Salter's
MIT-licensed compacting-tokenizer work and retains his 2024 notice. Both the
2023 license notice and **Copyright (c) 2024 Niles Salter** are retained here.
The reviewed fork's C changes were made by the maintainer and his agent.

The backslash-run calculation in `c_lexer.c`'s `classify` identifies John
Keiser's simdjson algorithm. The upstream Zig port links
[simdjson PR #2042](https://github.com/simdjson/simdjson/pull/2042), merged as
`9408298e0e64fbfe79b178278b58d81bc81eb7af`; its
`src/generic/stage1/json_escape_scanner.h` is the reference. That source uses
Apache-2.0 without the LLVM exception. Its author attribution (Daniel Lemire,
Geoff Langdale, John Keiser) is retained verbatim in
[LICENSES/simdjson-AUTHORS.txt](LICENSES/simdjson-AUTHORS.txt).
The examined simdjson revision and lexer import have no separate root `NOTICE`.
The upstream license also names simdjzon; no separately copied simdjzon file
was identified among the five imported C files. That upstream statement is
preserved, not narrowed or removed.

The MIT benchmark helper lineage extends beyond the lexer: the counter-open
and counter-read routines and clock sample in `x86_64_encoder_benchmark.c`
match the imported harness after renaming; the object writer benchmark uses
the same pattern. Their insertion-sort helpers, also used by
`lane_benchmark.c`, follow the local lexer benchmark. These portions and local
changes to them remain MIT; the rest of those benchmark files uses Default.

### Generated tables and Clang-following code

`c/gen_c_tokenizer_tables.py` generates the byte-property, operator and
punctuator tables from `c/c_tokenizer.h` and explicit C/C++ character rules.
It reads no external instruction or Unicode database. Those generated
representations follow the imported component's MIT terms.

`c/gen_c_tokenizer_unicode.py` instead probes every nonsurrogate non-ASCII
code point using `clang -cc1 -std=c23 -dump-raw-tokens`, then coalesces accepted
code points into ranges. The imported header records **clang 23.1.3**, with
695 start and 812 continue ranges. It is not output licensed solely by the
generator's own terms.

The scalar lexer explicitly follows Clang's `clang/lib/Lex/Lexer.cpp` and
names corresponding routines. Preserve LLVM's existing terms for any adapted
expression in those portions and the identifier-data representation:
[LICENSES/LLVM.txt](LICENSES/LLVM.txt) is the exact LLVM license from
[`llvmorg-23.1.3`, commit `0d261d1ca552c95a8f007e061c787ac7132fbcbc`](https://github.com/llvm/llvm-project/tree/0d261d1ca552c95a8f007e061c787ac7132fbcbc).
The corresponding `Lexer.cpp` and `UnicodeCharSets.h` carry
`Apache-2.0 WITH LLVM-exception`. The local changes are a C translation with
bounded slices, simplified diagnostics and different storage/control flow.
These existing LLVM terms are not the new first-party grant.

The range data reflects Unicode identifier properties plus Clang's
mathematical-notation extensions. Retain the Unicode data notice as well:
**Copyright © 1991-2026 Unicode, Inc.** and the complete
[Unicode License V3](LICENSES/Unicode-3.0.txt), copied from
[Unicode's license](https://www.unicode.org/license.txt).
This preserves data rights without asserting that all output of a compiler
inherits its implementation license.

The generator recorded a version string, not a hash of the Clang binary or an
independent Unicode input manifest. The LLVM tag above is the review
comparison pin, not a claim to have recovered that binary. The imported table
bytes and generator are pinned by the lexer revision; retain all these terms
when redistributing them. Regeneration must record the actual tool and inputs.

## RAD Debugger

The lane interface/base-layer reference is
[RAD Debugger at `62b715a30e5da6b8d7e02372a8e7458feab1df97`](https://github.com/EpicGames/raddebugger/tree/62b715a30e5da6b8d7e02372a8e7458feab1df97),
the upstream revision preceding the October 19 lane implementation.
The reviewed paths are `src/base/base_thread_context.{h,c}`,
`src/base/base_math.c`, `src/base/base_arena.{h,c}`,
`src/radbin/radbin.c` and `src/rdi_from_pdb/rdi_from_pdb.c`.
They carry **Copyright (c) Epic Games Tools** and MIT terms, retained verbatim
in [LICENSES/RAD-Debugger.txt](LICENSES/RAD-Debugger.txt).

Local commit `f72863b` and [PR #72](https://github.com/buster14a/buster/pull/72)
identify that reference. Lane metadata, scratch arenas, modulo task assignment
and equal-range partitioning follow those interfaces/patterns. The local
barrier, one-barrier broadcast, caller-as-lane-zero startup, bounded arena
allocation, cost partitioning and `mwaitx`/futex waiting differ substantially.
Keep the upstream MIT notice and provide the entire modified `lane.c` under
MIT instead of requiring a disputed line-by-line originality boundary.

The original implementation consulted `master` without retaining a commit
hash. The pin above is the historical source comparison used in this review;
it is not presented as a recovered original checkout identifier. Both the
referenced interface and inspected implementation have the same MIT grant.

## Encoder, tools and other generated material

[PR #14](https://github.com/buster14a/buster/pull/14), introducing the
encoder in `55f05ac`, explicitly records that no code was copied from
`buster14a/bloat-buster`'s historical encoder at
`e9b3dea9429996c7ad492d80a4923e75126ada87`. That implementation was an external
comparison, not a vendored component.

The early encoder document proposed generating a future table from the
original Buster catalogue. The subsequent implementation uses handwritten
X-macro rows/families, checked against GNU as. The reviewed tree contains
neither a XED/uops.info data projection nor that catalogue. Issue
[#17](https://github.com/buster14a/buster/issues/17) uses XED and
uops.info's March 29, 2026 list for coverage research; this does not itself
establish a copied implementation. GNU as 2.47's output bytes/relocations are
test oracles for the locally authored streams; no GNU as implementation is
included. Future generated imports require a fresh input/notice review.

`link_program.c` emits original synthetic support routines and fixtures;
the executable's hash constants are generated from SplitMix64, not imported
test corpora. Recognizable SplitMix64 arithmetic also appears in link/lane/
executable tests and benchmarks, in the dataflow generator's seed mixer, and in
the type table's tests and benchmark. Its finalizer also ends `link.c`'s name
hash and `c_types.c`'s key hash. The public-domain reference is
[Sebastiano Vigna's 2015 implementation](https://prng.di.unimi.it/splitmix64.c);
its dedication, permission and disclaimer are retained in
[LICENSES/SplitMix64.txt](LICENSES/SplitMix64.txt). This does not import the
Java implementation referenced by that source. No runtime or system header
package is vendored at this revision.

The concrete older-Buster import is `.claude/settings.json`, recorded by
`02a1ad65e157c9cb1affc0293c7ab4a6fe21fe0b`. Its allowlist matches the maintainer's
configuration at Buster commit `5180a65a57ef19e9a2b5251eb33356e57b7120e1`, with
the repository name changed and local Tailscale permissions added. This is
maintainer configuration under his confirmed contribution authority, not an
import of the older compiler's frameworks or implementation.

## wyhash's constants

`c_intern.c`'s identifier hash uses the four 64-bit constants of wyhash's
default secret, `_wyp` in `wyhash.h`, as the whitening of its lanes and the
round key of its chain of AES rounds
(`0xa0761d6478bd642f`, `0xe7037ed1a0b428db`, `0x8ebc6af09c88c6e3`,
`0x589965cc75374cc3`). They are as in
[wangyi-fudan/wyhash at `e92bc3e9adf862aad364ff74a24ef2cae854b0b1`](https://github.com/wangyi-fudan/wyhash/tree/e92bc3e9adf862aad364ff74a24ef2cae854b0b1),
the last revision that has them (later ones changed the default secret). Only
the constants are used, not wyhash's code. wyhash is released into the public
domain under the Unlicense; its text at that revision is retained in
[LICENSES/wyhash.txt](LICENSES/wyhash.txt).

## Clang's keywords

`c_intern.c`'s keyword list (`BUSTER_C_KEYWORDS`) is generated data:
- **Generator:** clang 23.1.3, the local package `clang 23.1.3-2` for target
  `x86_64-pc-linux-gnu`.
- **Inputs:** every identifier named in the entries of that package's
  `TokenKinds.def` and `RegularKeywordAttrInfo.inc` (`/usr/include/clang/Basic`).
  Only the names were read.
- **Procedure:** each name's `__is_identifier` was asked of
  `clang -E -P -std=MODE` in the ten modes from `c89` to `gnu23`. The list keeps
  the 148 names it answers 0 for in some mode, with the modes as one of six
  rules. `docs/c-refinement.md` records this, and LLVM's terms are retained in
  [LICENSES/LLVM.txt](LICENSES/LLVM.txt).

The same answers are behind `c_refine_test.c`'s spellings chosen apart.
`c_preprocessor_test.c` keeps the keyword candidates it asks clang about again
under `--clang` (`pp_test_keyword_candidates`): the spellings among those
names, with the ones of `TransformTypeTraits.def`, `OpenCLImageTypes.def` and
`HLSLIntangibleTypes.def` from the same directory. Only the names were read.

## Clang-generated predefines and feature tables

`c_preprocessor.c`'s predefined macros (`pp_predefines_base`,
`pp_predefines_c23`) and feature tables (`pp_features_*`) are generated data,
the output of **clang 23.1.3** (the local package `clang 23.1.3-2`, target
`x86_64-pc-linux-gnu`), which the configuration decided in #117 takes as the
compiler's own. LLVM's terms are retained in [LICENSES/LLVM.txt](LICENSES/LLVM.txt).

- The predefines are `clang -dM -E -x c /dev/null` (gnu17), without the macros
  the language mode and options decide, and the difference of its output
  under `-std=c23`. `__BUSTER__` is the compiler's own.
- The feature tables are clang's nonzero answers, under `-std=gnu17` and
  `-std=c23` (the same), to `__has_attribute`, `__has_builtin`,
  `__has_c_attribute` and `__has_warning`. The names asked about are those that
  `/usr/include` (glibc 2.44), clang 23's resource headers, musl 1.2.5, SQLite
  3.53's amalgamation and Zig 0.14.1's `lib/libc` query, and a list of common
  GNU attribute names. Only the names were read from those inputs.
- The `__has_feature` and `__has_extension` tables are clang's nonzero answers
  for every `FEATURE` and `EXTENSION` name of clang 23.1.3's
  `clang/include/clang/Basic/Features.def`, in the ten modes from `-std=c89`
  to `-std=gnu23`, with the first standard of each answer that depends on the
  mode. `c_preprocessor_test.c` keeps the same names (`pp_test_feature_names`)
  to ask clang again under `--clang`. Only the names were read from
  `Features.def`.
- A local script ran the queries; `docs/c-preprocessor.md` records its
  procedure.

SQLite, musl, Lua and the system's headers are test oracle inputs only (the
tests read them where they are installed): none of their code is included.

## Review limits and distribution

The record covers the pinned tree and this adoption's changes. Unmerged PRs,
future imports and external system libraries are not silently covered.
Unrecorded original tool hashes are identified above; the retained compatible
component terms are the disposition for those portions, rather than an
assertion that they are entirely first-party.

Preserve source attribution/modification notices and distribute the applicable
license and notice files as described in [LICENSING.md](LICENSING.md).
Excluding something from the first-party grant would not by itself authorize
redistributing it. Any later discovery of other incorporated material must be
resolved before claiming coverage for it.

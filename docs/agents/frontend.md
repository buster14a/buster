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
| Phase arenas, the preprocessing seal and semantic layout queries | [Compiler phase lifetimes](../compiler-lifetime.md) |
| Relocations, weak/alias symbols, constructors/destructors, object formats and linker | [Linkage](frontend/linkage.md) |
| Packed/aligned types, bit-fields, layout engines | [Layout](frontend/layout.md) |
| Atomic layout, argument classification, loads/stores, conversions | [Atomics](frontend/atomics.md) |
| Declarators, typeof, conditional types, calls, JIT/driver boundaries | [Calls](frontend/calls.md) |
| `_Float16`, long double, x87, static folding, global/inline assembly, Wasm boundary | [Wide floats and assembly](frontend/wide-floats-assembly.md) |

Layout and atomic ABI work often needs both the layout and atomics guides.
Changes to places or calls also need the foundations guide. Native selection and
allocation invariants live in [the machine guide](machine.md); command-line
options and action dispatch live in [the driver guide](driver.md).
The cross-frontend/backend ownership map is in [compiler phase and state](compiler-phase-state.md).

## Preprocessor include identity

The once-file index shared by `#import`, `#pragma once` and proven whole-file
include guards keys descriptor-backed files by `FileIdentity`, not by their
resolved path spelling. POSIX identity is device/inode and Windows identity is
volume serial/file index, captured from the same descriptor that supplied the
bytes. Lexical aliases, hard links, followed symbolic links and case aliases on
case-insensitive filesystems therefore share one suppression record. Builtin
headers and Android APK assets remain in their normalized path namespaces.

The driver passes the supplying root descriptor's identity through
`CPreprocessOptions.source_identity`, so root `#pragma once` and self-imports
share the include key, including physical aliases. An in-memory caller leaves
that identity invalid and keeps its own path namespace; preprocessing never
opens the root path to infer an identity for different supplied bytes. Imports
suppress any previously entered identity, while ordinary unguarded includes
still repeat. The root comparison is lazy, avoiding a once table allocation for
an include-free source.

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

## Builtin stddef inclusion requests

The embedded `<stddef.h>` supports independent `__need_ptrdiff_t`,
`__need_size_t`, `__need_rsize_t`, `__need_wchar_t`, `__need_NULL`,
`__need_max_align_t`, `__need_offsetof` and `__need_nullptr_t` requests.
A partial include defines only requested entities and consumes every request
macro. Separate declaration guards allow repeated requests and a later full
include; a partial include after a full one retains established declarations.
C23 `nullptr_t` has its own guard and is absent from earlier dialects.
`rsize_t` is available on an explicit request, or a full include with
`__STDC_WANT_LIB_EXT1__ >= 1`; this does not advertise Annex K library functions.
The existing target typedefs, NULL spelling and max_align_t layout are unchanged.

Registered `c_test_stddef_need_protocol` independently checks each name's
presence or absence, combined requests, helper consumption and both include
orders in C17/C23 on six target layouts and both frontend SSA forms. Linux
hosted execution also compiles and runs namespace controls through real
`string.h`, `stdio.h`, `stdlib.h` and `time.h` with Buster in both frontend forms
and every allocator, plus independent GCC and Clang controls.

## Builtin capability queries

Direct angle-bracket header operands preserve their translated-source
characters, including internal spaces, rather than joining token spellings.
The same policy applies to `#include`, `#include_next`, `#import`, and literal
`__has_include`/`__has_include_next` operands. Include-query builtins recognize
literal operands before argument prescan, including when an alias reaches the
builtin during rescanning, so identifiers inside a direct header name do not
expand. Quoted operands retain their literal spelling.

Macro-produced angle header operands use the existing implementation-defined
policy of concatenating the surviving token spellings. A wrapper that prescans
its own argument uses this policy too; substituted or removed tokens cannot
recover the original source span. The query builtin's endpoint `no_expand`
bits carry literal provenance through its replacement without increasing
token size. `c_test_header_operands` covers simultaneous spaced/unspaced files,
repeated spaces, line splicing, identifier collisions, aliases, wrapper
prescan, expanded operands and include-next search origins.

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

## Target ABI predefined macros

The prelude exposes C library typedef identities, rather than choosing a
spelling solely from its width. The signed types below also select the matching
unsigned macro and literal constructor; Darwin's `__INT64_C` uses `LL` while
`__INTMAX_C` retains `L`.

| Target | `__WCHAR_TYPE__` | `__WINT_TYPE__` | `__INT64_TYPE__` | `__INTMAX_TYPE__` |
|---|---|---|---|---|
| x86-64 Linux/Android | int | unsigned int | long | long |
| AArch64 Linux/Android | unsigned int | unsigned int | long | long |
| macOS/iOS | int | int | long long | long |
| Windows | unsigned short | unsigned short | long long | long long |
| x86-64 UEFI | unsigned short | unsigned short | long long | long long |
| AArch64 UEFI | unsigned short | unsigned short | long | long |
| Wasm32/Wasm64 | int | int | long long | long long |

`__SIZEOF_WCHAR_T__`, `__SIZEOF_WINT_T__`, their width macros and
`__WCHAR_MAX__`/`__WINT_MAX__` agree with those types. The supported targets
evaluate float/double at their declared precision, so `__FLT_EVAL_METHOD__`
is zero, including when a resource header uses it in an ordinary C expression.

The current prelude keeps `__OPTIMIZE__` and `__OPTIMIZE_SIZE__` undefined
and defines `__NO_INLINE__` as one, following Buster's existing optimization
macro policy and preventing optimized header paths from assuming inline
support. `__VERSION__` expands to the existing `__clang_version__` compatibility
string, `"18.0.0 (buster)"`; this does not establish an implemented driver version
query. The remaining driver-query work belongs to #1418.

`c_test_target_abi_macros` has fixed expectations for thirteen target triples
in GNU17/C23 and both frontend forms. It checks type compatibility, literal
constructor identity, sizes, widths, maxima, raw preprocessing spellings and
canonical validity, including the `FLT_EVAL_METHOD` resource-header spelling.

The hosted ABI expectations were verified from Clang 18.1.8 at
[`3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff`](https://github.com/llvm/llvm-project/tree/3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff):
[Windows target types](https://github.com/llvm/llvm-project/blob/3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff/clang/lib/Basic/Targets/OSTargets.h),
[Darwin x86-64](https://github.com/llvm/llvm-project/blob/3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff/clang/lib/Basic/Targets/X86.h),
[Darwin AArch64](https://github.com/llvm/llvm-project/blob/3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff/clang/lib/Basic/Targets/AArch64.cpp),
[Wasm target types](https://github.com/llvm/llvm-project/blob/3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff/clang/lib/Basic/Targets/WebAssembly.h),
[default integer types](https://github.com/llvm/llvm-project/blob/3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff/clang/lib/Basic/TargetInfo.cpp)
and [macro construction](https://github.com/llvm/llvm-project/blob/3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff/clang/lib/Frontend/InitPreprocessor.cpp).
LLVM's verified [license](https://github.com/llvm/llvm-project/blob/3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff/llvm/LICENSE.TXT)
is `Apache-2.0 WITH LLVM-exception`; no implementation was imported.
UEFI retains Buster's [documented target contract](../uefi-target.md).
Buster's first-party license remains unspecified under
[the license inventory](../../LICENSES/README.md).

## Trigraph translation policy

Raw root and included source in strict C99, C11 and C17 modes replaces all
nine trigraphs during phase one, including within comments and literals.
A trigraph backslash participates in the following line-splice phase for
LF, CR and CRLF. Scanning consumes raw input only, so a question-mark
sequence formed by splicing is not translated again. Checkpoints retain
original byte offsets and columns after each three-byte replacement.

GNU modes (including GNU89) and C23/GNU23 leave trigraphs unchanged.
This matches [GCC's pre-C23 standard-mode policy](https://gcc.gnu.org/onlinedocs/cpp/Initial-processing.html)
and [Clang's GNU-mode defaults](https://clang.llvm.org/docs/UsersManual.html#differences-between-various-standard-modes);
C23 follows [N2940's removal implemented in Clang 18](https://releases.llvm.org/18.1.1/tools/clang/docs/ReleaseNotes.html).
No separate trigraph override option is exposed. Command definitions,
synthesized spellings and already-preprocessed input do not repeat phase one.
Public standalone lexers retain their GNU17 translation policy.

`c_test_trigraph_translation` pins substitutions, partial/overlapping input,
all 64 scanner phases, splice ordering, original positions and phase boundaries.
Registered `c_trigraph_preprocess_tests` checks directives, literals, comments,
stringizing and included source, and compares fixed semantic token expectations
with both GCC and Clang on hosted Linux x86-64 in C99/C11/C17/GNU17 modes.

## Digraph token identity

All C dialects recognize `<:`, `:>`, `<%`, `%>`, `%:` and `%:%:` as the
ordinary bracket, brace, hash and double-hash punctuators. The scalar emitter
and prewarmed SIMD spelling tables publish canonical ids in both token rows
and shape sidecars; no later normalization pass or parser-specific alternative
checks are needed. The spelling table retains its longest-match scan order,
including the four-byte `%:%:` form.

Token offsets and lengths preserve the original digraph bytes. Macro `#` and
`##`, stringification, diagnostic positions and preprocessing output therefore
retain physical spelling. The printer's separator check distinguishes `%:`
from `#` by those bytes, since adjacent `%:` tokens must not merge into `%:%:`.
Registered `c_test_digraphs` pins all six ids at every 64-byte scanner phase,
overlapping maximal munch, literal/comment controls, directives, stringification,
paste, separators, nine dialects, canonical IR through both frontend forms and
a self-checking native driver program.

## Preprocessed punctuator boundaries

The shared lexical separator predicate preserves separate `%` and `=` tokens,
and separate `=` and `=` tokens, including when a macro expansion's source
column makes the printed spellings appear adjacent. These boundaries must not
become the single `%=` or `==` token when another compiler reads `-E` output.
The assignment punctuator owns the equality join rule; an existing `==` token
does not require a separator before another `=` merely for maximal munch.

Registered `c_punctuator_separator_tests` uses fixed spelling/id expectations
for joins and neighboring/digraph/comment controls. It checks exact stdout and
file preprocessing output, re-lexes both against independent punctuator ids and
ordinary preprocessing, and requires object compilation to reject invalid
separate-token expressions after an unmodified `.i` round trip while preserving
the output sentinel. Hosted Linux x86-64 also
compiles and executes valid `%=` and `==` controls directly and through `.i`
with both frontend forms and strict code-generation verification.

## Universal character names in identifiers

C99/GNU99 identifier escapes use [N1256 Annex D](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1256.pdf),
including its initial-digit exclusion. C11/C17 and GNU11/GNU17 use
[N1570 Annex D](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf):
the four combining ranges are continuation-only, and supplementary ranges
end at each plane's FFFD through EFFFD. The public lexers use GNU17 admission.
C23/GNU23 and GNU89 retain their previous identifier-escape behavior; this
change does not implement C23 XID or NFC. Raw UTF-8 retains encoding-only
validation, without retroactively imposing escape-specific Annex D ranges.

Lexing follows phase-one translation and splicing. A valid identifier UCN
is interned as UTF-8; raw and escaped spellings share the same symbol.
Original token bytes remain available to macro stringization and paste.
After macro replacement, the existing final identifier-respelling pass copies
canonical bytes under source-map stamps, preserving original physical columns.
Literal UCN decoding is unchanged. Synthesized and preprocessed input still
skip phase one, while their identifier grammar uses the selected dialect.

Registered `c_test_ucn_lex`, `c_test_ucn_preprocess`, `c_test_ucn_semantic`
and `c_test_ucn_runtime` cover range differences, errors, chunk boundaries,
macros, paste, labels, members, raw equivalence and source locations. The
runtime fixture exercises both frontends and all four allocation modes in
C99/C11/C17; hosted Linux x86-64 also requires GCC and Clang to compile and
execute the same self-checking source. These are registered validation paths,
not claims that a local compiler or external performance host was run.

## Lexer diagnostic reservation failure

Diagnostic rows allocate lazily. If their worst case does not fit scratch and
the dedicated arena reservation fails, they grow in the caller's result arena;
lexing still emits the complete token stream and EOF. Formatted messages and
the returned rows remain owned by the result arena.

Registered `c_test_lex_diagnostic_reserve_failure` warms scratch, then uses the
existing one-shot arena reserve failure on a 3 MiB source. Both public lexer
entries check fixed token/EOF and diagnostic expectations for 1, 65 and 200
errors, including growth, source positions and lifetime after scratch reuse.
A clean-source control must leave the failure pending for a no-pool probe;
malformed sources must consume it, and the next reservation must recover.

## Source translation limits

The source translator accepts at most `UINT32_MAX - 2` raw bytes so its
terminator, checkpoint count and original-source offsets remain representable.
`c_source_allocation_plan` checks that bound before source reads or
length-derived allocation counts. Root preprocessing rejects oversized input
before phase setup with `C_DIAGNOSTIC_SOURCE_TOO_LARGE` (`c.source-too-large`),
an error count and the root file's diagnostic path. Both lexer implementations
and included-file lexing use the same bound. An include must also fit the
remaining shared 32-bit spelling-offset space; its error retains the include's
source-map anchor without allocating another byte in that space.

`c_test_source_size_limit` queries the exact standalone allocation boundary,
passes oversized sentinel lengths through preprocessing and all lexer entries,
and checks bounded allocation, structured errors, shared-space exhaustion and
valid empty/declaration controls. It never allocates or maps a multi-gigabyte
source to exercise the limit.

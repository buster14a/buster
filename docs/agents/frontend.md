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

## Macro argument rescan boundaries

Argument collection preserves `no_expand` on identifiers whose definition is
disabled when the token is collected. Collection can consume the producer's
ENABLE marker before argument prescan begins; clearing that definition's
disabled bit must not make the captured identifier eligible again. Raw
stringization and token-paste construction retain their existing rules.
The registered argument-demand controls in `macro_conditional_test.c` cover
this boundary in C17/GNU17, including duplicate substitution and raw/paste
controls. The external Clang `macro_disable.c` assertion remains unchanged.

## Line-control filenames

`#line` and GNU linemarkers decode ordinary string-literal filenames with the
shared literal decoder before storing their logical path. Escaped quotes,
backslashes, numeric escapes and universal character names therefore denote
the same bytes in source maps, diagnostics, `__FILE__` and `__FILE_NAME__`.
`__BASE_FILE__` continues to name the main input. File builtins quote control
bytes with three-digit octal escapes, keeping their output valid without
absorbing a following digit. Encoding-prefixed or malformed filename literals
receive the existing invalid-line diagnostic.

Registered `c_test_line_filename_escapes` pins these byte values, builtin token
spellings, re-lexing, diagnostic paths and source locations in C17/GNU23,
including macro operands and already-preprocessed GNU linemarkers.

## Conditional directive comments

The `#if`/`#elif` operand range ends at the first newline outside a block comment.
The lexer retains physical newline rows for source metrics and locations;
`c_preprocess_directive_line_end` applies the same comment-gap policy as `#define`
after either the class-mask or row-scan physical endpoint. Conditional wrapping
drops interior newline rows before `defined`, feature-query and macro processing,
and keeps a physical-line stamp for builtin locations such as `__LINE__`.
The top-level driver and conditionals encountered inside a multiline macro
invocation both advance to the complete operand endpoint.

`c_test_multiline_comment_conditionals` fixes the expected branch and canonical
constant in both frontend forms for operator, parenthesized, leading, trailing,
repeated, `defined` and CRLF comments, with line-comment and one-line controls.
It also covers builtin line attribution and conditionals inside a macro invocation.
The existing `c_test_pp_class_masks_agree` seam compares the physical and extended
directive endpoints supplied by the mask and row paths.

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

Each translation unit also keeps a probe cache (`CIncludeProbeTable`) keyed by
(search directory, header name). It records misses and hits, and a hit keeps
its resolved spelling plus the identity captured by the probe that opened it.
`#include`, `#include_next`, `#import` and `__has_include` consult it before
the file system, so each missing path is opened at most once per TU.
A cached hit is decided by `c_include_suppressed` on that identity's record
before anything is opened, so a suppressed re-include makes no system call.
An inclusion that lexes maps the path again. If the new descriptor's identity
differs, because the file was replaced, that identity governs. The cache
assumes search directories do not gain or lose headers during one TU.
`file_map_read` likewise does not reopen a path through its read fallback
after POSIX `open()` reports `ENOENT` or `ENOTDIR`.

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
operations, not every recognized identifier. Its complex, atomic and integer-transform branches
reuse the exact `c_symbol_builtin_from_spelling` classification: adding a new
spelling there requires checking its real lowering and its query regression.
Never admit an arbitrary `__atomic_` or `__c11_atomic_` suffix by prefix.

The active target admits atomic builtins on x86-64/AArch64, and complex
construction there and on Wasm64. Wasm64 and eBPF reject atomic IR; eBPF also
rejects floating IR. Operand types and access widths are still validated by
lowering. `__builtin_return_address(0)` lowers to `IR_OPCODE_RETURN_ADDRESS`
(not `IR_OPCODE_STACK_SAVE`, which is the stack pointer): the x86-64 and
AArch64 MIR selectors read the frame record every non-Windows MIR function
builds (`[rbp+8]`, `[x29+8]`), so the query answers 1 only there. Windows
frames, Wasm64 and eBPF refuse it with a structured diagnostic, as does any
non-zero or non-constant level; `compiler_driver_test_return_address` runs it.
A positive runtime builtin query does not assert that the builtin
can be folded in every constant initializer; the complex global-initializer
work is tracked separately in #675.

`__builtin_bswap16/32/64` and `__builtin_rotateleft8/16/32/64` /
`__builtin_rotateright8/16/32/64` use fixed unsigned parameter and result
types. The 64-bit C rank follows `__UINT64_TYPE__` (`unsigned long` on LP64,
`unsigned long long` on LLP64); narrow results undergo ordinary C promotions
only when a surrounding operator requires them. Arithmetic scalar arguments
convert to those types, and rotate counts reduce modulo the named width,
including negative integer counts after their unsigned conversion.

`c_semantic_integer_transform_builtin` shares the exact signatures among
semantic type queries, arity/type validation and lowering.
`c_integer_transform_bits` supplies bounded constant folding to the parser's
explicit task stack and lowering's suspended constant-query stack. Runtime
lowering evaluates each argument once and expands through canonical shifts,
ANDs and ORs. Both rotation shift counts are masked; zero never produces a
shift by the type width. This introduces no backend operation or library call.

The capability query advertises these eleven names on x86-64/AArch64 only.
The semantic call pass checks their arity and arithmetic operands throughout
the translation unit, including file-scope and unevaluated expressions;
result-type prediction alone does not certify a valid call.
The registered `c_test_integer_transform_builtins` covers semantic-only
diagnostics, constant contexts, result type/rank and canonical validation
across six native target layouts and both frontend forms. Its embedded
executable checks use independent bit/byte-loop oracles, all four allocators,
modulo/negative counts, arithmetic conversions and single evaluation; they
run on supported desktop hosts. Wasm/eBPF capability promises await their own
backend execution witnesses. No tracked external fixture or retirement
support identity is added by this builtin extension.

`c_test_has_builtin` covers exact positive/negative spellings through real
preprocessing and parsing on eight targets, plus fence IR on both frontend SSA
paths. `compiler_driver_test_has_builtin_targets` checks non-native output;
`tests/basic_c_has_builtin.c` exercises its tracked builtin census under
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
`__builtin_clrsb`/`l`/`ll` share that policy with signed int/long/long long
operands; lowering counts leading zeros of `((x ^ (x >> (w - 1))) << 1) | 1`,
which is never zero.

`__builtin_parity`/`l`/`ll` share the popcount operand policy and lower to
`popcount(x) & 1`. `__builtin_bswap16/32/64` take and return `unsigned short`,
`unsigned int` and `unsigned long long` (`c_semantic_byte_swap_kind`) and lower
to masked-shift stages (`c_ir_emit_byte_swap`); canonical IR has no byte-swap
operation. `__builtin_copysign`/`f`/`l` rewrite the sign field of the stored
image of the first operand from the second (`c_ir_emit_float_with_sign`, shared
with `fabsl` and complex division), so NaN payloads survive and no libm or
`__*tf2` runtime call appears for x87 or binary128. Long-double math results
are selected by `c_semantic_math_link_is_long_double`, not by a trailing `l`
(which `ceil` and `huge_val` also have). `c_test_gnu_library_builtins_runtime`
covers all of these (#3037).

The typed `__builtin_{s,u}{add,sub,mul}{,l,ll}_overflow` checks
(`c_ir_overflow_builtins`) convert both operands to the spelling's type, store
the wrapped result through the third argument and answer `_Bool`. Lowering
computes in the unsigned counterpart: sign tests for add/sub, and for multiply
a divide-back check of the magnitudes' product, so no wider type, trap or
runtime helper is needed. The generic `__builtin_*_overflow` forms (#1394) and
`__builtin_return_address` are not implemented and answer `__has_builtin` 0.
`__builtin_fabsl` clears the stored sign bit, `__builtin_fmax`/`fmin` and their
`f`/`l` forms read NaN-ness from the stored bits and select the other operand,
and `__builtin_powi`/`powif`/`powil` run an inline square-and-multiply loop;
none of them imports libm or a compiler-runtime `__powi*f2` helper. The
`__builtin_strcmp`/`strcpy`/`strchr` forms and a non-constant `__builtin_strlen`
share `c_ir_emit_library_call` with the memory family: they prefer a
translation-unit declaration and otherwise import the standard prototype from
`c_ir_memory_builtin_signatures`, so no `<string.h>` is needed.
`c_test_gnu_library_builtins_runtime` checks all of these against exact oracles
in every native allocator mode and both frontend forms.

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

`<tgmath.h>` is a builtin header (`c_include_builtin` in `c_source.c`, searched
before the system and Clang resource directories). Clang's resource header
needs `__attribute__((overloadable))`, which this frontend does not implement,
so on a glibc host (`__GLIBC__`, known only after the header's own
`<math.h>` include) the builtin supplies the C11 7.25 macros with `_Generic`:
the real `<math.h>` functions with `f`/`l` variants, the complex-capable
ones mapped to `<complex.h>` `c*` names (`fabs` to `cabs`), and
`carg`/`cimag`/`conj`/`cproj`/`creal`. Integer arguments select the `double`
function. Each arm calls its function by name rather than selecting a function
designator: an indirect call would need the address of glibc's IFUNC libm
functions (`floor`, `sin`, ...), which the linker cannot relocate. Elsewhere
(musl, Darwin, MinGW) the builtin forwards with `#include_next`. The source
must stay under the 4095-byte portable string-literal limit, so the arms are
generated by the `__TG_R`/`__TG_C` helper macros. `c_test_tgmath_runtime`
runs the type and value checks under every register allocator.

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

## GNU-common predefined macros

The C prelude supplies GCC/Clang-common atomic lock-free, UTF, inline-mode,
integer type/limit/width, and target-feature macros. Values come from the
target's data layout and type spellings; x86-64 feature and small-code-model
macros are architecture-gated, while `__k8` follows the baseline CPU model.
`linux` and `unix` are defined only for GNU dialects on Linux/Android.
`__BIGGEST_ALIGNMENT__` follows Clang-suitable alignment (8 for BPFEL and
Apple AArch64, 16 otherwise), independently of `abi_max_alignment`.
PIC/PIE macros reflect the driver's `-fpic`/`-fPIC` and `-fpie`/`-fPIE`
level and executable-mode fields.

These values intentionally differ from Clang 18.1.8 in several places:
default fixed-address output leaves PIC/PIE undefined even where Ubuntu GCC
and Clang default to PIE, and Darwin/Windows do not inherit Clang's always-PIC
default; Windows receives GCC atomic and inline macros because Buster defines
`__GNUC__` on every target; `__k8` follows Buster's baseline CPU model,
including macOS, rather than Clang's default `core2`; and
`__SIG_ATOMIC_TYPE__` is defined, as GCC does, although Clang 18 omits it.
Type macros use Buster's short spellings, and FAST integer types follow Clang
(`short`/`int`) rather than GCC's `long`.
The default native CPU model does not define `__k8`/`__k8__`; only the
baseline CPU model does. x86-64 UEFI limits and 64-bit type spellings follow
Buster's documented LLP64 target contract, unlike Clang 18's LP64
`x86_64-unknown-uefi` target.

The prelude omits `__GCC_HAVE_SYNC_COMPARE_AND_SWAP_*` because `__sync`
compare-and-swap builtins are unsupported, `__SIZEOF_FLOAT128__` because
`__float128` is unmodeled, `__SEG_FS`/`__SEG_GS` because address-space
keywords are unsupported, and `__PRAGMA_REDEFINE_EXTNAME` because that pragma
is unimplemented. `c_test_gnu_common_predefined_macros` pins the reference
spellings for thirteen target triples in GNU17 and C23, plus type/limit
consistency, GNU89 inline, and PIC/PIE behavior.

`__float128`, `_Float128`, `_Float64x` and `_Float128x` are recognized as builtin
type words but have no lowering. A declaration that would define something with
one fails with `unsupported type '<name>'`: file-scope object definitions
(tentative and static included), struct/union members, block-scope declarations,
function definitions (return or parameter type) and function-pointer objects.
Declarations that create no storage stay accepted and are silently ignored, as
before: typedefs, function prototypes that are not definitions, and `extern`
object declarations without an initializer. glibc requires this: `bits/floatn.h`
contains `typedef __float128 _Float128;` and `_GNU_SOURCE` adds `_Float128`
prototypes (`strtof128`, the math functions) to `<stdlib.h>`, `<math.h>` and
`<Python.h>` users. The ignored typedef declares no name, so a later
`typedef __float128 T; T x;` fails with `unknown type name 'T'`, and a use of
the spelling itself is diagnosed as above. A function-pointer parameter inside a
struct or union member (including nested, array and function-returning-function-pointer
declarators) is diagnosed the same way, as is an unknown type name there; the
type-machine parameter frame reports it while `member_declarator_depth` is nonzero,
which aggregate members and storage-creating parenthesized declarations set.
An identifier-list parameter such as `void (*fp)(a)` is not accepted in a member
(Clang: only valid in a function definition) and reports `unknown type name 'a'`;
plain prototypes such as `int legacy(old_style_argument);` keep the GNU acceptance.
Typedef, prototype and `extern` declarations of function pointers stay lenient.
`c_test_unsupported_float_extension_diagnostics` and
`c_test_unknown_type_name_diagnostics` pin this.

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

## GNU local labels

`__label__ a, b;` at the start of a block scopes those label names to the
block (GCC "Local Labels"), so statement-expression macros can define labels
once per expansion. Lowering keys a function's labels by spelling, so the
final preprocessing pass `c_preprocess_rename_local_labels` (beside
`c_preprocess_respell_identifiers`) respells each declared name's label uses
inside the block -- definitions after a statement boundary, `goto`, unary
`&&`, and `asm goto` label lists -- to a translation-unit-unique identifier,
and turns the declaration into empty statements. Ordinary identifiers of the
same spelling keep theirs. Inner declarations are processed first, so a nested
redeclaration shadows the outer one. Malformed and file-scope declarations are
left untouched for the parser to diagnose. Only units that intern `__label__`
enter the pass. The driver sets `CPreprocessOptions.preserve_spellings` for
`-E`, which keeps the source spelling. `c_test_local_labels` covers both token
forms, macro expansions, shadowing, label addresses, `asm goto` and a rejected
use outside the block.

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

## Finite vendor builtin admission

The private `c_vendor_builtin.c/.h` descriptor module pins exact x86
resource-header spellings and prototype shapes to LLVM 21.1.8. Descriptors
preserve lane types, vector pointers and pointee qualifiers; admission alone
does not grant `__has_builtin` or permit a reachable unsupported operation.
The all-context semantic pass checks arguments and source integer constants,
including unused inline bodies, globals and unevaluated operands.

Clang 23.1.2's F16C wrappers use bare `__fp16` as a bit-cast destination and
as the element type of local four- and eight-lane GNU vector typedefs. The
frontend keeps its two-byte storage identity distinct from `_Float16` and
admits those vector sizes and alignments, type identity and `typeof` queries,
and eager equal-lane `__builtin_convertvector` validation in unused wrappers.
Ordinary storage-half objects, members, parameters and function results receive
a named semantic refusal; reached scalar casts, bit-casts and vector
conversions receive a named canonical-lowering refusal. `sizeof` and alignment queries use recorded frontend layout facts without
creating a canonical storage-half value type. The lowering budget skips only
proved fixed-size `sizeof`/`typeof` operands and separately skips unevaluated
alignment operands, leaving variably modified bounds visible. Its `_Generic`
evaluation-context handling is not complete. No scalar arithmetic, promotion,
or ABI support is implied. `c_test_vendor_storage_half_admission` checks exact
scalar/vector layouts, VLA-bound effects and LZCNT lowering in both frontend forms.

Generic operators have their own explicit type-machine stages: bit-cast and
vector conversion parse their type-name slots, elementwise operators preserve
narrow integer operands, reductions return a lane, and shuffles retain logical
output lanes with target-derived rounded storage. Bit-cast preserves complete
object representations, including arrays. Nondeterministic-value operands are
unevaluated; the canonical emitter chooses a defined zero of the requested
scalar/vector type.

The fixed lane selector also expands `shufps` and `pblendw128` with literal
eight-bit controls. SHUFPS selects two lanes from each input through integer
representation views, then restores the float-vector type, preserving NaN
payloads and signed zero. PBLENDW selects each word from its corresponding
input lane. The registered `c_test_vendor_fixed_lane_selection` checks all
256 controls against scalar bit expectations on both SSA forms and FAST/QUALITY;
nonconstant and out-of-range neighbors retain all-context diagnostics.

The 128-bit `pslldqi128_byteshift` and `psrldqi128_byteshift` spellings also
accept literal byte counts in 0..255. They select bytes from the entire
128-bit representation, zero vacated bytes, and restore the original two
64-bit lanes; counts at or above sixteen produce all zero bytes. Their input
evaluates once even when every output byte is zero. The registered
`c_test_vendor_immediate_byte_shifts` checks both directions at all 256 counts,
both SSA forms and FAST/QUALITY, with cross-lane byte patterns and scalar
expectations; nonconstant, negative, out-of-range and wrong-shape calls remain
diagnosed, including unused and unevaluated contexts.

`c_vendor_lowering.c`, `c_vendor_sha.c`, `c_vendor_x86_query.c`,
`c_vendor_generic.c` and `c_vendor_sse2_shift.c` expand the implemented subset through existing scalar,
vector, memory, CFG and fixed-register assembly contracts. Reachability uses
the existing function dependency worklist after semantic validation. A reached
unsupported intrinsic produces a diagnostic containing its exact name. Vector
signature validation checks object layout; target ABI transport limits apply
when a reachable definition or call is lowered, after unused wrappers are pruned.

Per-call instruction/value/block reservations supplement token-derived body
capacity. Masked loads reserve their conditional byte accesses and additional
SSA parameters at their joins for ambient named locals and function
parameters. Generic lane conversions reserve the existing software floating
conversion paths. Every sum/product is checked against the canonical row
limits; unused wrapper bodies receive no expansion reservation.

Scalar `__builtin_ia32_lzcnt_u16/u32/u64` use canonical CLZ with defined
zero-input handling. The operand converts once to its prototype's unsigned
width; a zero bit makes the CLZ operand nonzero, then restores the zero result
to 16/32/64. The 16-bit form widens to the native 32-bit count width and removes
its sixteen padding bits. These public Clang intrinsics intentionally permit
baseline x86 targets, so the lowering needs no LZCNT target feature or library
call. Results retain unsigned short/int/long long rank on LP64 and LLP64.
`c_test_vendor_lzcnt` checks all 16-bit inputs, 32/64-bit powers and sampled
patterns, truncation, arithmetic conversions, argument effects, both frontend
forms and FAST/QUALITY; invalid calls retain all-context diagnostics and
non-x86 capability queries remain false. The external Clang suite's LZCNT
family lane separately checks all five public stock-header spellings against
the pinned Clang 23.1.2 contract; neither lane completes the Zen 5 census.

The five preexisting SSE2 scalar-count shift spellings accept ordinary `int`
arguments. Both operands are evaluated once, including count copy conversion;
the emitted scalar shifts use a bounded count. Logical shifts choose zero
outside their lane width, while arithmetic right shifts retain sign-fill.
Signed and unsigned integer input lanes with the required shape preserve their
bits, and results use the signed vector type of the header prototype.

In GNU dialects, a void function may return an expression whose semantic C
type is void. The existing expression child evaluates it once before active
cleanups and the zero-operand return. An expression that terminates control
flow keeps its terminator.

Protected type-constant queries keep growable type/cache state in model scratch.
Their three fixed append buffers retain the full unit capacities in a separate
private arena sized with checked allocation arithmetic and released after the
query; published rows and caller spare slots remain unchanged.

The exact `__builtin_inf()` spelling belongs to the existing math-constant
path: its result is double, its canonical bits are positive IEEE infinity,
and its signature takes no arguments in evaluated, unused and unevaluated
contexts. Its existing float counterpart is `__builtin_inff()`; admission
and availability use the fixed math spelling census, without vendor metadata
changes or a runtime library import.

Nested vector lane subscripts retain the original vector storage, including
plain parenthesized groups. Standalone vector reads still produce copied values.
The registered runtime fixture checks local arrays, globals, member arrays,
pointer bases, evaluation counts, neighboring guards and captured values across
allocator and frontend memory modes.

Brace elision descends through enclosing records until an aggregate expression
matches a complete subobject. Record identity and existing qualified views
determine whole-object copies. Runtime initialization and incomplete-array
inference share the same type predicate and retain cursor advancement, string
initializers and scalar elision.

Transparent-union pointer members use the existing pointer conversion rules:
matching pointees may gain const or volatile qualifiers, while qualifier loss,
atomic mismatch and distinct record tags remain incompatible. The selected
member keeps the existing first-member ABI and complete union storage.

Constant `__builtin_offsetof` expressions follow promoted anonymous members
through the existing bounded member-path query and retain the selected type
across array subscripts. A bound builtin name is accepted in a static initializer
only when the typed evaluator proves the complete type/member expression to be
an integer constant; ordinary function calls retain their diagnostic.

Aggregate-expression brace elision also descends through array destinations
until the expression matches an element. Whole-array admission is retained for
array expressions. Runtime initialization and inferred array bounds share this
rule, including a union value initializing a one-element array member.

The exact `__builtin_ia32_tzcnt_u32` and `__builtin_ia32_tzcnt_u64` spellings
return their unsigned operand width for zero. The canonical count receives a
nonzero guarded value; each source operand is evaluated once and requires no
BMI instruction support.

Runtime compound literals keep one captured operand per selected member or
array element. A later designator replaces the earlier captured value within
the existing slot capacities. Source expressions follow ordinary lowering;
regressions leave effects of overridden initializers unconstrained.

Translation-unit IR queries retain all five complete append-buffer capacities
in a private arena sized with checked alignment and allocation arithmetic.
`c_lower_to_ir_run` releases that arena after every lowering-core result.
Reservation or initial-commit failure produces a structured diagnostic before
persistent IR tables are initialized. The ownership regression checks failure,
recovery, scratch preservation, canonical IR and retained aggregate bytes.

Constant initializers also own their fixed context array in a checked private
arena. The existing `UINT32_MAX / sizeof(context)` ceiling is retained, and the
wrapper destroys the reservation after every core result. Dynamic frame and
range work continues to use task scratch and its existing rewind boundary.

Unbound declaration prefixes use the token's lexical scope to recognize local
typedefs, including macro-expanded `for` initializers. Existing bound entities
remain authoritative, so a local object can shadow a typedef spelling.

## Opt-in raw source reuse

`CPreprocessOptions.source_cache` reuses only exact captured raw translation/lex
results, before fresh symbol interning and preprocessing. It imports owned
copies into the current phase/spelling arenas; no cache pointer reaches a sealed
result or canonical IR. Read [bounded raw source reuse](../source-lex-reuse.md)
for the input model, limits, ownership, replay contract and pending cost gates.

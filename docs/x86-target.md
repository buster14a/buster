# Generated-code instruction set

The default is **x86-64 baseline**, as the owner chose for #180 on
2026-10-21. The compiler executable still requires the Linux/Zen 5 host that
`build.sh` builds for. Its host intrinsics do not enable instructions in an
output program.

The preprocessor and assembler accept the same target options:

```sh
./build/compiler -E -march=x86-64 input.c
./build/compiler -E -march=x86-64-v2 input.c
./build/compiler -E -march=x86-64-v3 input.c
./build/compiler -E -march=x86-64-v4 input.c
./build/compiler -E -march=znver5 input.c
./build/compiler -E -march=native input.c
./build/compiler assemble -march=x86-64-v3 -o output.o input.S
```

The last `-march=` wins. Both take every x86-64 CPU clang 23 knows (79, and
`native`), and the preprocessor every `-m` feature switch, resolved as clang
resolves them by `c_predefines.c` (see "The predefined macros" in
[the preprocessor's design](c-preprocessor.md)). It writes their macros and
fills the `X86Target` from the result (`predefines_target`): the features this
file knows by name, with RDTSCP and CLFLUSH, which clang has no feature for,
from the profile where `-march` names one of the six above, and that profile's
architecture (`X86_ARCH_OTHER` for clang's other CPUs). An unknown name or
switch is an error, as in clang (the preprocessor takes only the `-m` options
clang 23 uses for x86-64 Linux), so no option silently leaves baseline macros
active. `-mtune=` accepts clang's names and `generic`, independently of the
ISA. It is recorded for the future scheduler, and currently changes no costs,
scheduling or allowed instructions. As with Clang 23, CPU identity macros,
including `__tune_*__`, follow `-march`. `--predefines` remains an explicit
replacement of the entire macro preamble.

`x86_target.c` supplies the bitset and target parser. A zero-initialized
`X86Target` means baseline. The v2/v3/v4 profiles are cumulative; the named Zen 5
profile includes its optional extensions. `native` reads CPUID at option parsing
time. It requires XSAVE/OSXSAVE and XCR0's XMM/YMM bits before exposing AVX,
and the opmask/ZMM state bits before exposing AVX-512. Missing CPUID leaves
remain zero. Its feature catalogue covers the supported Zen 5 host; this is not
a CPU-dispatch mechanism or a promise of support for other compiler hosts.

Target macros come from that same resolution, without invoking another
compiler. `test-x86-target` compares the complete preamble with Clang 23 for all
six target choices, alongside the existing language/optimization configurations
(26 option sets total), and `test-preprocessor --clang` for every CPU and switch. Only `__BUSTER__` and the three Clang patch-version
macros are excluded from comparison. The preprocessor also queries the explicit
`x86_target_builtins` table for target-sensitive `__has_builtin` answers. Its
eleven names are compared with Clang at every target. This extends the existing
bounded query table; it is not a complete x86 builtin catalogue or implemented
builtin lowering (#148).

## Record eligibility

Include `x86_target_forms.c` after the encoder and build a byte table with
`x86_target_forms(&target, allowed)`, where `allowed` has `X86_FORM_COUNT` bytes.
It visits sixteen contiguous 32-bit feature-mask rows at once, using AVX-512 on the host;
`x86_target_forms_scalar` is its exact twin. There is no gather, allocation or
change to the 16-byte `X86Instruction` or the encoder's templates.

`x86_target_rows.c` is the single source for both tables. The scalar reference
keeps all 64 feature bits (66,816 bytes); the vector table projects the currently
used features into 32 bits (33,408 bytes). Both are constant and cache-line
aligned. Their combined requirement data is 100,224 bytes; the vector builder
reads only the compact table, and the output remains 8,352 bytes. Every row has
an exact round-trip test. An unrepresentable new requirement fails a compile-time
assertion, so extending the policy outside the projection requires deliberately
extending that projection. It cannot silently discard an instruction's feature.

The executable's main units share the frontend's immutable pair through hidden
data symbols. Their scalar/vector kernels remain local and read the same aligned
rows directly. Standalone benchmark and helper inclusions retain static tables;
sharing adds no initialization, allocation or mutable state.
The scalar loop's iterations are independent: its source is immutable and each
writes a different output byte. The public builders still allow target/output
overlap because they capture the feature word first; regression tests check both
paths with the target stored inside the destination.

The assembler builds its mnemonic index for one target, excluding ineligible
forms before either matching path runs. Its syntax subset remains separate:
ISA support does not imply that the frontend understands every encoding. Its
final resolved records pass `x86_target_validate` at the external-input
boundary. Tests cover every indexed form, all six profiles, `.S` macro-driven
instruction choices, option order and independent tuning.
Initialization reuses syntax results only when the spelling pointer, family,
vector length, encoding, register class, r/m class and syntax are identical, and computes operand roles only for
admitted rows. ISA filtering and final record validation still apply per form.
The canonical policy retains #565's baseline string instructions and 512-bit
quadword moves; the latter require AVX512F, independently of AVX512DQ.

`x86_target_form_allowed` tests a form including its FS/GS and fixup flags.
`x86_target_validate` checks both ISA eligibility and `x86_validate`'s operands,
returning the first failing record index, or the count on success. Initialize
the encoder before operand validation, as for its existing validator. Use this
at external-input boundaries and in every selector test; the trusted encoder
and branch-sizing loops retain their existing contract.

The requirement table is explicit. It covers the current Tier 0 templates and
generated deferred/ABI/frame records, including scalar SSE compares, XMM integer
moves and exchanges; baseline locked arithmetic/bit/atomic operations; x87
arithmetic, conversions and state operations; SSE3/SSSE3 families and bounded
SSE4 operations; scalar AVX moves, arithmetic and conversions; and the FMA
families in VEX and EVEX. `FISTTP` requires SSE3 even though its encoding uses
x87 registers. A zero row means **unclassified and
unavailable**, including on `native`; it never means baseline. A newly added
form therefore cannot silently become eligible. The table is not a claim that
all of the encoder's catalogue has been classified. Extend it, with independent
ISA expectations, as selectors and the assembler adopt additional families.

The tests distinguish BMI's VEX forms from AVX, APX's EVEX forms from AVX-512,
128-bit AVX integer forms from their AVX2 256-bit counterparts, scalar EVEX
from packed AVX512VL, and KMOVW (F), KMOVB (DQ), and KMOVD/Q (BW). FMA eligibility
does not grant permission to contract an expression: floating-point semantics
still belong to the selector.

```sh
./build/compiler test-x86-target
taskset -c 2 ./build/compiler benchmark-x86-target
```

Run each through `EXCLUSIVE_OWNER=... ~/buster-fast-bench/exclusive.sh` on the
9700X. Tests cover synthetic CPUID/OS state, bounded target names, invalid
options, all 65,536 encoded form values at six targets, random valid operands
for every admitted form, scalar/vector table identity and protected page edges.
They also cover every subset of each distinct requirement mask, each profile
with each individual feature removed, and first-failure rejection for every
form at all six profiles. Every raw feature bit alone, reserved target bits,
unaligned output sentinels and every compact-row round trip are checked too.
These kernel checks use the requirement table as
their scalar specification; independent named-form expectations and GNU as's
architecture gate establish instruction legality. The assembler oracle checks
every classified form at baseline/v2/v3/v4/Zen 5 and with individual features
removed, including FMA and AVX512VL/BW/DQ. An unavailable tool, malformed input,
unknown architecture or unexpected diagnostic fails the oracle.
GNU as 2.47 has one explicitly reported coverage gap: its dual FMA/AVX512F
templates accept the 144 packed EVEX XMM/YMM FMA forms with AVX512VL disabled.
Those cases remain denied by the policy and covered by independent F|VL
minimal-feature negatives; they are counted separately from external assembler
comparisons. If a later assembler rejects them, they follow the normal comparison.

`test-x86-target` runs policy, profile and preprocessor checks in the frontend
unit, with that unit's encoder and preprocessor state. The separate
`test-x86-target-selection` command runs selector and native-emission fixtures
in the backend unit, with its encoder and selector state. Each command reaches
one copy of mutable state, and policy checks can run independently of selectors.

Both matcher paths validate a candidate before encoding it to choose the
shortest instruction. The scalar matcher does not repeat that same validation
inside a length-only call. Catalogue tests compare its length and every emitted
byte with the checked scalar encoder, alongside malformed operands and ISA
rejection checks.

The policy command compares every matcher's admitted form with the uncached
syntax predicate and scalar ISA policy over all six profiles, every individual
feature removal and an all-feature target. These checks have no selector dependency.

`test-x86-target-selection` replays the existing Tier 1 selector and native-emission
fixtures at baseline/v3/v4, with both covers and both allocation paths. Its
policy-owned Tier 0 consumer checks every compiled template and generated
records from both paths, including floating values, calls, aggregate objects,
indirect/TLS accesses and dynamic frames. The selector fixtures and their command stay in the
backend translation unit, reaching only one copy of mutable encoder state. These are bounded selector tests, separate
from full source/corpus acceptance.

Target feature macros are compared with both Clang and GCC for all six profiles,
including live `native`; compiler-specific macro differences are recorded
explicitly. GCC 16.2.1 has no `__INVPCID__` or `__RDPRU__` macro, and its named
`znver5` omits `__SHSTK__`, which its native resolver reports on the 9700X.
Those macro differences do not remove the actual target capabilities. Full
predefined-preamble equality remains a Clang contract.
The benchmark checks table equality before reporting 21 interleaved medians
for building a whole table, including the scalar source as optimized by Clang.
`--only scalar|avx512` isolates one path. `--after-predefines MS` emits target
macros for 100–300 ms before each sample, modeling the lighter configuration
work preceding selection. `--iterations 1` isolates the initial table after
that conditioning; the default 10,000 measures sustained repetition. Output
includes nanoseconds and hardware cycles (zero with an explicit notice if
unavailable). Both include timing/counter overhead, most visible with one table.
Compare several independent processes under the lock; a single interleaved
run does not establish both power states.
This is configuration work, not an end-to-end compiler speed claim.

## Historical 9700X measurements

The following 2026-10-21 results describe the earlier 64-bit vector table.
The compact-table and matcher follow-up is recorded separately with exact
revisions and qualification status in
[the optimization campaign](https://github.com/buster14a/buster-performance/tree/codex/180-isa-policy-20261025/docs/measurements/isa-policy-180-optimization-2026-10-26).

Measured on 2026-10-21 UTC with Clang 23.1.1, `build.sh`'s `-O3 -march=native`
and CPU 2 of the Ryzen 7 9700X, taking the shared lock separately for each
command. The workload builds all 8,352 eligibility bytes for `znver5`.
`6a84160` expanded each predicate to eight qwords, shifted them to ones and
narrowed them to bytes. `0e056d1` expands the predicate directly to bytes.
Each process reports the median of 21 samples. These are ranges of medians
from three independent processes per row and build.
Each cell is **ns/table (core cycles/table)**.

| Preceding work and sample size | Qword expansion | Direct bytes | Scalar source in the direct-byte build |
| --- | ---: | ---: | ---: |
| Isolated AVX-512, 10,000 tables | 384.0–389.5 (2,046–2,076) | 318.0–329.2 (1,688–1,758) | Not measured |
| 100 ms predefines, one table | 491–531 (2,890–3,066) | 431 (2,505–2,552) | 441 (2,565–2,566) |
| 100 ms predefines, 10,000 tables | 499.7–501.9 (2,756–2,771) | 410.0–411.3 (2,254–2,261) | 441.3–442.6 (2,433–2,439) |

In the qword-expansion build, the scalar source took 441 ns for one table and
435.9–438.9 ns/table for 10,000 tables after lighter work.
The scalar source is a correctness oracle, not a claim of scalar machine code:
Clang vectorized it with AVX-512 in both builds. Disassembly of the measured
binary confirms that direct byte packing removes `vpmovm2q`, `vpsrlq` and
`vpmovqb`, using masked byte moves instead. It wins in sustained repetition and
avoids the previous loss against optimized C after lighter work. These timings
alone do not identify a power mechanism; no energy or throttle counter was
measured. The one-table result includes instrumentation overhead.

Reproduce each command in a separate process under the shared lock, repeating
the set across processes:

```sh
taskset -c 2 ./build/compiler benchmark-x86-target --only avx512
taskset -c 2 ./build/compiler benchmark-x86-target --after-predefines 100 --iterations 1
taskset -c 2 ./build/compiler benchmark-x86-target --after-predefines 100
```

## Remaining acceptance for #180

Tier 0 and the bounded Tier 1 selectors are on main. Tier 1 takes the shared
eligibility table and refuses denied forms before publishing records; its native
consumer checks frame/spill records as well. Tier 0's current templates use
baseline instructions. Its implementation and source driver still need the
configured target at a final validation boundary, and every Tier 0-owned test
must adopt the check as families grow. The independent policy test audits the
current output but does not add that production guard. This integration stays
with #136/#133; the existing interface and handoff are recorded on
[#136](https://github.com/buster14a/buster/issues/136#issuecomment-6395663929).

Selectors must choose legal alternative sequences when they adopt optional
instructions, and extend the explicit requirements with independent negative
tests. Full target-sensitive builtin lowering remains #148, and per-function
target attributes remain #147. Complete c-testsuite and SQLite acceptance at
baseline/v3/v4 remains #145/#146. The bounded policy work does not close #180.

## Sources and reproduction

The instruction-set levels follow the
[x86-64 psABI feature table](https://gitlab.com/x86-psABIs/x86-64-ABI/-/blob/master/x86-64-ABI/low-level-sys-info.tex).
Feature names, CPUID bits and instruction requirements are architectural facts;
the implementation and tests are original C. LLVM's
[X86 target parser](https://github.com/llvm/llvm-project/blob/main/llvm/lib/TargetParser/X86TargetParser.cpp),
[target macros](https://github.com/llvm/llvm-project/blob/main/clang/lib/Basic/Targets/X86.cpp)
and [host detection](https://github.com/llvm/llvm-project/blob/main/llvm/lib/TargetParser/Host.cpp)
were consulted for behavior, without copying implementation. Macro spellings
and profile membership were checked with Clang 23.1.3 locally and Clang 23.1.1
on the 9700X:

```sh
clang -march=x86-64-v3 -dM -E -x c /dev/null
clang -march=znver5 -dM -E -x c /dev/null
clang -march=native -dM -E -x c /dev/null
```

The live tests construct their own `__has_builtin` inputs from the recorded
name table. Clang is an external oracle; no implementation or external test
corpus is vendored.

## Boundary audit #298

Audit pin: `42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`;
[scope and evidence](simd-boundary-audit.md).
[The form table producer and readers](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/x86_target_forms.c#L351)
are `x86_target_forms`/scalar → one byte per `X86_FORM_COUNT` entry →
`x86_target_form_allowed` and `x86_target_validate`. The selected generated ISA
is independent of the host's AVX-512 capability. Unknown requirement rows fail
closed; encoder support alone does not make a form legal for a target.

Build the table once for an unchanged target, retain it through all selecting
consumers, and do not rebuild it per instruction. The complete table extent,
not a caller-chosen short slice, is the output capacity. The record validator
consumes a counted instruction array; scalar/vector table construction must
agree, including final partial groups. Initialization/ownership is external.
Disposition: retain the shared eligibility table and baseline default.
`test-x86-target` validates policy and compiler predefines; [#180](https://github.com/buster14a/buster/issues/180)/[#141](https://github.com/buster14a/buster/issues/141) own
integration of all real selectors and corpus ISA checks. Table-construction
ns/table is not a measured selection or whole-compiler saving.

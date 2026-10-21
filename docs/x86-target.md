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
switch is an error, as in clang, so no option silently leaves baseline macros
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
(22 option sets total), and `test-preprocessor --clang` for every CPU and switch. Only `__BUSTER__` and the three Clang patch-version
macros are excluded from comparison. The preprocessor also queries the explicit
`x86_target_builtins` table for target-sensitive `__has_builtin` answers. Its
eleven names are compared with Clang at every target. This extends the existing
bounded query table; it is not a complete x86 builtin catalogue or implemented
builtin lowering (#148).

## Record eligibility

Include `x86_target_forms.c` after the encoder and build a byte table with
`x86_target_forms(&target, allowed)`, where `allowed` has `X86_FORM_COUNT` bytes.
It visits eight contiguous feature-mask rows at once, using AVX-512 on the host;
`x86_target_forms_scalar` is its exact twin. There is no gather, allocation or
change to the 16-byte `X86Instruction` or the encoder's templates.

The assembler builds its mnemonic index for one target, excluding ineligible
forms before either matching path runs. Its syntax subset remains separate:
ISA support does not imply that the frontend understands every encoding. Its
final resolved records pass `x86_target_validate` at the external-input
boundary. Tests cover every indexed form, all six profiles, `.S` macro-driven
instruction choices, option order and independent tuning.

`x86_target_form_allowed` tests a form including its FS/GS and fixup flags.
`x86_target_validate` checks both ISA eligibility and `x86_validate`'s operands,
returning the first failing record index, or the count on success. Initialize
the encoder before operand validation, as for its existing validator. Use this
at external-input boundaries and in every selector test; the trusted encoder
and branch-sizing loops retain their existing contract.

The requirement table is deliberately explicit and incomplete. It starts with
the assembler's admitted forms, common scalar integer/control/SSE2 forms and representative v2, BMI/BMI2, AVX,
AVX2, FMA, AVX-512 and mask-move families. A zero row means **unclassified and
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

## 9700X measurements

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

Tier 0 #136 and Tier 1 #141 have not yet supplied instruction selection on main.
They must consume this target, extend the requirement table for their emitted
families, choose legal alternative sequences and run the record check on every
test. Full builtin availability/lowering remains #148, and per-function target
attributes remain #147. The c-testsuite and SQLite runs at baseline/v3/v4 need
the unfinished compiler pipeline (#145/#146). This change does not discharge
those acceptance items or close #180.

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

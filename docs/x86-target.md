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
architecture (`X86_ARCH_OTHER` for clang's other CPUs). Its `extensions` are
the features past those (`X86Extension`: AVX512-FP16 so far), which form
eligibility doesn't classify and only the lowering's rows read (the x86
builtins' and half-precision vectors': [the checker's guide](c-checker.md)). An unknown name or
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
and the opmask/ZMM state bits before exposing AVX-512 and AVX512-FP16's
extension, which tests read to run its code. Missing CPUID leaves
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
change to the 16-byte `X86Instruction` or the encoder's templates. The rows that
need a feature the 32-bit word doesn't project (AVX512BF16's, AVX512BITALG's,
AVX512IFMA's, AVX512VBMI2's, AVX512VNNI's, AVX512VPOPCNTDQ's and AVX-VNNI's: 125,
#760) are its rare case: `BUSTER_X86_TARGET_EXTENDED`'s rows, last in `x86_target_rows.c`, whose
compact words carry a mark no target has, so that the vector loop denies them,
and an exact pass over their full requirements then decides them.

`x86_target_rows.c` is the single source for immutable policy data. The vector
builder reads the compact32 table (33,408 bytes), and its exact pass the
extended rows' full requirements and forms. The independent scalar
reference keeps all 64 feature bits densely for 3,463 classified rows (27,704
bytes), with their form indices (6,926 bytes). It clears all 8,352 output bytes
then checks those complete requirements directly. Its loop stays scalar to
avoid widening and extracting indices for byte stores.

A uint16 inverse map (16,704 bytes) gives full-width metadata queries their
canonical dense slot plus one. Zero means unknown, and the bounded query returns
zero for out-of-range forms. Forward/inverse/requirement/projection checks cover
every form; independent named-form expectations and external ISA checks remain.
The four arrays contain 84,742 payload bytes before alignment (66,782 before
#760's rows), replacing 116,894 bytes of sparse64, compact32 and dense-reference
data. A compact row with a requirement outside the 32-bit projection, an
extended row without one or before a compact row, and a row without the
classified marker fail at compile time.

The executable's main units share these arrays through the frontend's existing
hidden policy-data home; standalone inclusions stay static. Kernels read them
directly, with no initialization, allocation or mutable state. Both builders
capture the target feature word before any output write, preserving tested
target/output overlap and exact unknown-form denial.

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

Default64 initialization builds base and FS entries, copies FS entries to GS,
and patches the optional segment byte in both byte halves. Layout and fallback
decisions are identical; the direct builder is retained when the expected prefix
location changes. Other modes and fixup copies keep their existing construction.
The backend selection/native test compares all 50,112 default64 entries, including segment variants
and fixups, with direct per-form construction. Frozen before/after comparisons
cover the complete bytes of all five modes.

The default64 full-store encoder stages its final one-to-fifteen-record tail
in an inlined source helper. Tails of one to four records use one quad; longer
tails keep the existing computation, using only one- and four-quad source paths. It returns
the final byte count and takes the fixup cursor by value; no further encoding
follows that result. Actual stack retention and spills are checked in compiled
code and measurements. Full groups, other modes and alternative gather/store
paths keep their behavior, as do record format, bytes, offsets/fixups and capacity.

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
SSE4 operations; scalar AVX moves, arithmetic and conversions; the FMA
families in VEX and EVEX; and Tier 0's vector values' 128- and 256-bit
`vmovdqu64` moves and `vpxord` (AVX512F and AVX512VL, #221: their slots' moves
through zmm16, and its zeroing); and the front end's X86 rows' VEX and EVEX
forms with their families (#760: VEX's 256-bit integer forms and those AVX2
has at every length, the broadcasts, permutes, masked moves, variable shifts
and gathers, by AVX2; the opmask registers' forms and their tests by their
sizes' AVX512F, DQ and BW; the byte and word elements' BW, the quadwords'
multiplies, conversions and mask moves' DQ, AVX512CD's conflicts and leading
zero counts; AVX512VBMI's byte permutes and AVX512VBMI2's byte and word
compresses and expands and concatenating shifts, each with BW, as
AVX512BITALG's vpshufbitqmb and byte and word counts and AVX512BF16's
conversions and dot product are, since clang's options and GNU as take BW with
them; AVX512VPOPCNTDQ, AVX512IFMA, AVX512VNNI, AVX-VNNI, AVX512VP2INTERSECT,
GFNI, AES, VAES, F16C's conversions and PREFETCHI's prefetches), each form's own requirement written out in
`x86_target_rows.c`'s last blocks. The compact word keeps the features to AES at
their bits and then PCLMUL, AVX512VBMI, AVX512VP2INTERSECT, GFNI, VAES and
PREFETCHI at the next six; AVX512-FP16, an extension the table doesn't
classify, leaves its forms unclassified, and so do AVX-IFMA and AVX-NE-CONVERT,
which the table has no bit for, their VEX forms of vpmadd52luq, vpmadd52huq and
vcvtneps2bf16. VEX's vbroadcastss and vbroadcastsd from a register are AVX2's,
from memory AVX's: the form's requirement is its register operand's.
`FISTTP` requires SSE3 even though its encoding uses x87 registers. A zero row means **unclassified and
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
removed, including FMA and AVX512VL/BW/DQ, AVX2 from v3, and (#760) AVX512VBMI,
AVX512VBMI2, AVX512BITALG, AVX512IFMA, AVX512VNNI, AVX512BF16,
AVX512VP2INTERSECT, GFNI, VAES and AVX-VNNI from Zen 5. An unavailable tool,
malformed input, unknown architecture or unexpected diagnostic fails the oracle.
GNU as 2.47 has one explicitly reported coverage gap: its dual FMA/AVX512F
templates accept the 144 packed EVEX XMM/YMM FMA forms with AVX512VL disabled,
and its dual AVX/AVX2 and AVX512F ones 50 of the X86 rows' (#760: the variable
shifts, vpermd, vpermq, vpermps, vpermpd, vpermilps, the broadcasts, vaesenc,
vaesdec and vgf2p8mulb, their broadcast forms too). Those cases remain denied by
the policy and covered by independent F|VL minimal-feature negatives; they are
counted separately from external assembler comparisons. If a later assembler
rejects them, they follow the normal comparison.

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
tests. Full target-sensitive builtin lowering remains #148. Complete c-testsuite
and SQLite acceptance at baseline/v3/v4 remains #145/#146. The bounded policy
work does not close #180.

## A function's target attribute

GNU's `__attribute__((target("...")))` gives a function an instruction set of
its own, which the lowering's x86 rows and leaves and the vector builtins' and
operators' rows use (`c_check_program.c`'s `check_program_targets`). The
features follow clang's rule (`ASTContext::getFunctionFeatureMap`): the
attribute's `arch=` CPU (or the unit's), the command line's features as written
(`-march=native`'s host features, read once for the unit, then the `-m` flags),
then the attribute's items in their order, then the soft rules, which leave a
feature any of them turned off as it is (`c_predefines.c`'s `predefines_apply`).
`predefines_target_attribute` reads the string as clang does: items between
commas, trimmed; `no-NAME` turns a feature off; `tune=` changes no feature. A
string clang ignores, with a warning, leaves the function the unit's: an item
that names no feature with a flag (an unknown one, `cmov`, `64bit`, an empty
one), an `arch=` or `tune=` of no x86-64 CPU or after another, `fpmath=`
anywhere, and `branch-protection=`. So does `"default"`.

The attribute applies as clang merges it: from the definition or a declaration
before it; one after the definition is dropped where none came before. A
function of several versions (two strings, or `target_clones`) is GNU's
function multiversioning, whose symbols and resolver this compiler doesn't
make: it is refused by name. The calling convention stays the unit's, as
clang's x86-64 ABI level is the unit's, and so a function has vector values only
where both the unit's target and its own have AVX-512F and AVX-512VL.

`test-preprocessor --clang` checks the features against clang's IR
(`"target-features"`) for 298 attributes in units of eight command lines:
clang's headers' strings, `arch=`, `tune=`, the soft rules in each order, APX's,
what clang ignores, and every flagged feature on and off.
`test-checker --program-vectors` runs a baseline unit's BMI, BMI2 and CRC32
functions, each by an attribute of its own, inherited, `arch=`, trimmed and
`"default"`, against clang -O0's code, and checks each refusal by name.

### Code generation's check of target features

Clang's code generation refuses a call that would inline a function into one
without its features (`CodeGenFunction::EmitCall` and `checkTargetFeatures`):
at `-c` and when it links, not at `-fsyntax-only`, and so does the C driver
after `-c`'s selection, before lowering (#775, `c_check_target_feature.c`'s
`check_program_target_features`). A direct call of an `always_inline` function
where either function has a target attribute, or a call in a `flatten` function
where either has one, is checked: where the callee's attribute applies (one
version), the first feature of its map that the caller's lacks is an error,
the attribute's own features first in its order; otherwise, where the caller's
applies, each feature of the callee's map that the caller's lacks is one. The
maps are `predefines_apply`'s, and the attributes those of the declarations
before the call (the callee's) or up to the definition (the caller's), as clang
inherits them; `CheckProgram.attribute_keys` keeps them, in a unit with a target
attribute alone. The calls are those clang emits: the walk of the x86 builtins'
range errors (#487) reaches what a constant condition leaves running, and a
direct callee is the function's name alone, not `(*f)` or `(&f)`.

Clang's errors are unrecoverable. It emits the functions it must emit where the
unit defines them (`ASTContext::DeclMustBeEmitted`: an external definition, by
GNU's and C99's rules of inline functions, or a function with a `used`,
`constructor` or `destructor` attribute) and none after the first that has an
error, nor the functions it defers to the unit's end; where none of those has
one, it emits every deferred function, past their errors. So the driver reports
the first such function's errors alone, or else every deferred function's, by
the selection's classification of each body (`CheckProgram.body_eager`, from the
same rules that decide which inline definitions `-c` emits): each function's in
clang's emission order, by statement in the lowering's order of them (a for
loop's increment after its body) and in a statement by the calls' ends (an
argument's call before its own), after its closing brace, as clang reports them
when it emits the function, or at the unit's end for a deferred one. Clang emits
the deferred functions depth first from where its emission first referred to
them, and the driver in source order; where clang evaluates a statement out of
source order (an assignment's right side first), the order of several errors in
it may differ; and where an error's feature is the first of the rest of a map
(an `arch=` CPU's, or a caller's attribute that turns several features off),
clang's order is its `StringMap`'s and the driver's `predefines_features`', so
the two may name different features. `--lower-all`, #129's test option, checks
every definition it lowers, those clang never emits among them.

`test-checker-driver` compares each fixture's first error at `-c` with clang
`-c`'s, by both paths and both routes, and checks that `-fsyntax-only` takes
it: callees' attributes before and after the call, callers' that add and turn
off features, `flatten`, calls through the address, dead branches, which
function's errors clang reports and in which order, and `<immintrin.h>`'s
`_bzhi_u32` at targets with BMI2 and without. Eleven fixtures' every error, line
for line, must be clang's: the first external function's alone, calls in their
arguments in the order of their ends, an external function's that leave a
deferred one's unreported, two deferred functions', where no external function
has any, loops' and a statement expression's, a for loop's increment after its
body, and which functions clang emits where they are: GNU89's plain inline,
C99's extern inline and a `gnu_inline` plain inline definition before an
ordinary function, inline-only definitions, `used` beside `retain`, and a
destructor and a constructor.

A builtin's own required features (`'__builtin_ia32_palignr128' needs target
feature ssse3`) aren't checked yet.

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

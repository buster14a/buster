# Assembly frontend

The frontend in [#222](https://github.com/buster14a/buster/issues/222)
is a callable GNU-syntax assembler in the existing compiler executable. Its
default host path uses AVX-512 and has an exact scalar twin. It selects the
existing 16-byte `X86Instruction` records and emits the existing `Link` tables
and ELF writer's output. The direct compiler path still emits records itself.
No production command invokes another assembler or a C parser.

## Commands and runnable fixture

On the approved 9700X, with **each command separately** inside the host's
`EXCLUSIVE_OWNER=<name> ~/buster-fast-bench/exclusive.sh` wrapper:

```sh
./build.sh
./build/compiler assemble -o /tmp/sum.o examples/assembler/sum.s
clang examples/assembler/caller.c /tmp/sum.o -o /tmp/assembler-example
/tmp/assembler-example
./build/compiler test-assembler --cross-check
```

The example returns success only when both calls compute the right value. Its
function has arithmetic (including positive 200), indexed memory, forward and
backward local branches, an external function call and RIP-relative read-only
data. The tests also import an assembled `_start` object into Buster's own
linker, write its executable and run it.

`compiler -c input.s -o output.o` is an alias for `assemble`. Without `-o`,
the output is the input's basename with `.o` in the current directory.
Default input syntax is AT&T; `--intel` selects Intel without register
prefixes. `.intel_syntax noprefix` and `.att_syntax prefix` switch syntax in
the source. The `prefix`/`noprefix` arguments control register spelling.
`--scalar` selects the reference path, and `--timing` prints initialization,
read, frontend phase, object/write and total times plus arena usage.

Output feature admission is separate from the host ISA:

- `--isa=x86-64` (default): the integer and SSE2 subset below;
- `--isa=avx2`: also its VEX families;
- `--isa=avx512f`: also its 512-bit AVX-512F families.

These are explicit subset levels, not a replacement feature database for
[#180](https://github.com/buster14a/buster/issues/180).

An uppercase `.S` extension enables the shared preprocessor's explicit assembly
mode before assembly. `-I`, `-D`, `-U`, `-include` and the preprocessor's include
directory options are available. It defines `__ASSEMBLER__`, expands `$MACRO`
as an immediate, and accepts unknown `#` lines as assembly comments. Literals
remain opaque. Includes, conditionals, function macros, stringizing and token
pasting use the existing engine; C preprocessing keeps its existing behavior.
The adapter uses both existing vector/scalar kernels and printers. A bounded
in-memory stream carries line markers to assembly diagnostics; no subprocess
or temporary source file is used. `compiler -E -x assembler-with-cpp input.S`
also exposes this preprocessing mode.

## Supported input

Mnemonics and registers use lowercase. GPRs of 8/16/32/64 bits (including
high-byte registers), XMM/YMM/ZMM and mask registers have explicit classes.
The frontend checks the encoder's high-byte/REX, address and decoration rules
before trusted encoding. Addressing is 64-bit base plus index times 1/2/4/8
plus a signed 32-bit displacement, RIP relative, or absolute displacement;
FS and GS overrides are supported. Intel memory can specify `byte` through
`zmmword ptr`. Ambiguous memory width is an error. Intel `offset symbol`
requests an immediate address; a bare symbol in an ordinary operand denotes
memory. AT&T uses `%register`, `$immediate`, displacement/base/index/scale,
and `*` for indirect calls/jumps. Integer width suffixes are checked.

The admitted families are:

- `mov`, `movabs`, `lea`, `movzx`, `movsx`, `movsxd`; integer
  `add/adc/sub/sbb/and/or/xor/cmp/test`, `inc/dec/neg/not`, `mul/imul/div/idiv`,
  `shl/sal/shr/sar/rol/ror/shld/shrd`;
- `push/pushw/pop`, near `call/jmp`, all canonical Jcc/CMOVcc/SETcc conditions,
  common Jcc aliases, `loop/loope/loopne/jrcxz`, `ret/leave/nop/int3/ud2/syscall`,
  `clc/stc/cmc/cld/std`, `cbw/cwde/cdqe/cwd/cdq/cqo`;
- SSE2 `movaps/movups/movapd/movupd/movdqa/movdqu`, packed float
  `add/sub/mul/div` on `ps/pd`, `xorps/xorpd/pxor/paddd/psubd`;
- the corresponding VEX forms and `vzeroupper`; 512-bit AVX-512F packed float
  arithmetic/moves, `vpaddd/vpsubd/vpxord/vmovdqa32/vmovdqu32`, with destination
  masks/zeroing and supported `{1to8}`/`{1to16}` memory broadcasts.

Only forms covered by the frontend's operand-role grammar are admitted.
Missing families, rounding/SAE, explicit instruction-prefix syntax, far
branches, 16/32-bit execution modes, other segment registers, VSIB, arbitrary
sections, GOT/TLS relocation modifiers, CFI/debug directives and macro-assembler
directives are diagnosed. An unsupported form never reaches the trusted encoder.

Statements end at newline or semicolon. `#` and `//` start line comments;
`/* ... */` comments preserve statement newlines. Strings preserve embedded
comment characters and support quote/backslash, `n/r/t/b/f`, and one to three
octal escape digits. Source slices need no terminator or padding. Diagnostics
report the physical source line and byte column of the first error in `.s`.
For `.S`, line markers recover the presumed filename and line, including
includes and `#line`; the column refers to the expanded assembly line.

Named labels and numeric labels `0` through `9` support forward/backward
references. Numeric labels have a namespace distinct from user names.
An undefined ordinary symbol becomes an external; undefined `.L` names,
numeric labels and explicitly local symbols are errors. Global/weak symbols
retain relocations where interposition or linking can change their address.

Directives:

| Purpose | Supported directives |
| --- | --- |
| Sections | `.text`, `.data`, `.bss`, `.section .text/.rodata/.data/.bss` |
| Symbols | `.globl`/`.global`, `.local`, `.weak`, `.type` (`function/object/notype`), `.size` |
| Constants | `.equ`, `.set` (absolute and defined before use; `.set` may reassign) |
| Data | `.byte`, `.word`, `.long`, `.quad`, `.ascii`, `.asciz`/`.string`, `.zero` |
| Alignment | `.balign` with power-of-two alignment, `.p2align`; optional byte fill and maximum skip |

Section flags/types, when supplied, must match the canonical section. `.bss`
accepts only zero fill. Exported absolute symbols and forward constants are
diagnosed; the shared object writer currently lacks the former's unit ownership.
The writer automatically emits a non-executable-stack note.
Default text alignment uses single-byte NOPs; other sections use zeros.
Direct branches require label or symbol targets. Absolute numeric destinations
are diagnosed because the shared writer has no absolute-symbol relocation;
they are never interpreted as relative instruction displacements.

Expressions use decimal, octal, hex and binary integers; parentheses, unary
`+/-/~`, multiplication/division/remainder, bitwise `&/|/^`, and addition/
subtraction. Precedence follows GAS: multiplicative, then bitwise, then
additive. There is no C parser assumption. Intermediate values must stay
within `[INT64_MIN, UINT64_MAX]`; each output field has a separate range check.
Bitwise operations use 64-bit two's-complement values (`~0` is `-1`).
One symbol plus an addend is relocatable. Same-section label differences can
be used in data and `.size`; instruction operands require an absolute value
or symbol plus addend. Constant values are captured at their use point.

Instruction selection preserves immediate values: an imm8 sign-extending
form can represent `-128..127`, so `add rax,200` uses a wider form. Whole-width
fields accept signed values or unsigned bit patterns of their width.
Relocations are PC32/PLT32, ABS32/ABS32S and ABS64 with explicit signed 64-bit
addends. Symbolic byte/word data, nonrepresentable expressions and oversized
fields fail instead of truncating. Absolute memory uses signed disp32.

## Data-parallel design and scalar twin

`asm_assemble(source, index, options, arena)` returns `AsmResult`, containing a
`Link`, a diagnostic byte offset/message and phase timings. The source, index
and output arena must remain alive while the result is consumed. Initialize
the caller-owned `X86MatchIndex` and encoder once. Failure restores the caller's
arena position and clears the output link. Sources are bounded to 16 MiB,
sections to 256 MiB, and expression stacks to 128 entries.

The vector path is selected explicitly, without host dispatch:

1. The lexer classifies 64 source bytes, computes word/token boundary masks,
   compresses starts/ends/kinds in registers and transposes them into 16-byte
   token records. Masked tails respect the logical slice. Long words and
   comments/strings have bounded rare paths; the full scalar lexer is the oracle.
2. Common Intel register/immediate statements form batches of up to 16
   instructions. Packed register keys and decimal integers are classified and
   converted eight operands at a time. Labels and state-changing directives
   delimit batches. Addresses, expressions, decorations and AT&T grammar use
   the exact parser. The benchmark exposes that remaining cost separately.
3. Mnemonic buckets index compact form columns. Sixteen candidates compare
   operand kind/class/width, fixed registers, feature level, broadcasts and
   signed/unsigned immediate ranges in parallel. Only survivors construct
   final records and run `x86_validate`; the scalar matcher independently
   checks roles and values. Encoder rows remain the encoding authority. This
   helper is the bounded shared slice of #221, not its IR semantics/effects work.
4. Instruction-only layouts use the existing vector/scalar branch sizer after
   proving its index-based contract. Mixed assembly partitions fragments by
   section and prefix-scans 16 sizes between alignment operations. The exact
   path sums them individually. Scalar column loads/stores avoid Zen 5 gathers.
5. Encoding batches up to 256 contiguous instructions through the existing
   AVX-512 encoder, or its scalar twin. The section allocation includes the
   encoder's required capacity and store padding beyond its logical size.
   Data and relocation fields are explicitly written after preceding batches.

Labels, declarations and expression state retain their source order. For
mixed fragments, branch promotions remain an ordered pass; layout timings
include it. No claim is made that a stage cannot be vectorized further.

Relaxation starts short and only promotes a branch once. It recomputes all
alignment padding each round, stopping within `branch_count + 1` rounds.
Alignment can absorb growth, so this scheme guarantees a correct stable layout,
not the globally shortest encoding for every alignment arrangement. Rel8-only
branches are range-checked against the final layout. There is no implicit
expansion that changes flags or registers.

## Tests and measurements

`test-assembler [--cross-check] [--seed N] [--fuzz N]` checks complete
scalar/vector token streams, diagnostics and byte-identical ELF output, valid
batches of 0..65 instructions at all 64 source misalignments, malformed text
and binary bytes, and both ends of protected pages for lengths 0..1099.
Fixtures cover immediate/operand boundaries, section changes, expressions,
labels, relocation addends, branch growth across data/alignment and failures.
The cross-check compares section bytes/properties, symbols and relocations
with GNU as using the existing object comparator, and links/runs the examples.
Different ELF file layouts and automatic stack notes are normalized. Existing
independent encoder/GNU-as tests remain required.
The `.S` fixtures compare both complete driver paths, independently preprocess
with Clang's assembler-with-preprocessor mode, compare objects with GNU as,
and check included-source diagnostics and preservation of existing output
when assembly fails.

```sh
./build/compiler benchmark-assembler --external
./build/compiler benchmark-assembler --workload registers
./build/compiler benchmark-assembler --workload simd
./build/compiler benchmark-assembler --workload att
./build/compiler benchmark-assembler --only vector
./build/compiler benchmark-assembler --only vector --after-scalar 100
```

Run each separately under the 9700X lock with `taskset -c 2`. The default
workload has 256 generated functions, including branches, memory, alignment,
external references and section changes. These are controlled synthetic
workloads, not a compatibility corpus. `--functions 1..2048` changes size.
There are 21 interleaved samples after a warmup; output is checked outside the
timed interval. Phase medians and whole-run medians are reported independently.
Initialization and index/work-arena sizes are included in the report.
`--only` measures one path back-to-back; `--after-scalar` warms scalar assembly
outside each sample to expose the known Zen 5 transition state.

`--external` rotates fresh Buster vector, Buster scalar, GNU as and Clang
integrated-assembler processes. They read the same warm `.s` file and overwrite
the same object path. All include startup, input and output I/O; none preprocess
the input. The component benchmark excludes file I/O and reusable initialization,
so its numbers must not be compared directly to those subprocess times.

The implementation is original; the syntax references are the GNU assembler's
[x86 syntax](https://sourceware.org/binutils/docs/as/i386_002dVariations.html),
[expressions](https://sourceware.org/binutils/docs/as/Expressions.html) and
[alignment](https://sourceware.org/binutils/docs/as/Align.html) documentation.
No GNU implementation code was imported.

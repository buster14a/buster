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
the output is the input's basename with `.o` in the current directory. The
input is read to its end, so a pipe or `/dev/stdin` works too.
Default input syntax is AT&T; `--intel` selects Intel without register
prefixes. `.intel_syntax noprefix` and `.att_syntax prefix` switch syntax in
the source. The `prefix`/`noprefix` arguments control register spelling.
`--scalar` selects the reference path, and `--timing` prints initialization,
read, frontend phase, object/write and total times plus arena usage.

Output feature admission uses the shared [target policy](x86-target.md), separate
from the host ISA. `-march=` accepts `x86-64` (the default), `x86-64-v2`,
`x86-64-v3`, `x86-64-v4`, `znver5` and `native`. `-mtune=` does not enable
instructions. The last target option wins. The earlier `--isa=x86-64`,
`--isa=avx2` and `--isa=avx512f` spellings remain aliases for baseline, v3 and
v4 respectively, within the same documented syntax subset.

`x86_match_initialize(index, target)` intersects understood syntax with the
target's allowed forms once, keeping unsupported instructions out of both
matching paths. An index belongs to that target and can be reused for its
inputs. Final resolved records also pass the shared target/operand validator
before encoding. The encoder's hot loop has no new checks.

An uppercase `.S` extension enables the shared preprocessor's explicit assembly
mode before assembly, using the same target for feature macros and form
eligibility. `-I`, `-D`, `-U`, `-include` and the preprocessor's include
directory options are available. It defines `__ASSEMBLER__`, expands `$MACRO`
as an immediate, and accepts unknown `#` lines as assembly comments. As with
GCC and Clang, a paste that doesn't form one token leaves both tokens without
an error, so `$ ## x` gives `$` and the expanded `x`, and `% ## x` a register.
Literals remain opaque. Includes, conditionals, function macros, stringizing and token
pasting use the existing engine; C preprocessing keeps its existing behavior.
The adapter uses both existing vector/scalar kernels and printers. A bounded
in-memory stream carries line markers to assembly diagnostics; no subprocess
or temporary source file is used. `compiler -E -x assembler-with-cpp input.S`
also exposes this preprocessing mode.

## Supported input

Mnemonics use lowercase. Register names are read as GNU as reads them: in any
case (`RAX` is `rax`), and with canonical numbers only (`r08`, `xmm00` and
`k00` are symbol names). GPRs of 8/16/32/64 bits (including
high-byte registers), XMM/YMM/ZMM and mask registers have explicit classes.
The frontend checks the encoder's high-byte/REX, address and decoration rules
before trusted encoding. Addressing is 64-bit base plus index times 1/2/4/8
plus a signed 32-bit displacement, RIP relative, or absolute displacement;
FS and GS overrides are supported. An Intel scale can come before or after its
index (`4*rsi`, as LLVM writes it, or `rsi*4`), and a term can carry a sign
(`[rax + -8]`); a register term takes only unary pluses (`[rax + +rsi]`). As in
GNU as, without register prefixes a register's name is never a symbol: in an
expression (`.quad rsi`, `dword ptr eax`, `[rax + -rsi]`) it is refused as an
invalid use of a register. Address expressions retain registers through
parentheses: `[rax+(rsi)]` and `[rax+2*(rsi)]` have their usual addressing
meaning. Subtracting registers, multiplying registers together and scales
outside 1/2/4/8 are rejected. Intel memory can specify `byte` through
`zmmword ptr`, including GCC's uppercase `QWORD PTR`. Both `[rip+symbol]`
and `symbol[rip]` are accepted.
Ambiguous memory width is an error. Intel `offset symbol` requests an
immediate address, as does GCC's `OFFSET FLAT:symbol`; a bare symbol in an ordinary operand denotes memory, and in
a branch its destination, unless a width asks for memory: `call qword ptr
callback` calls through the pointer at `callback`. AT&T uses `%register`,
`$immediate`, displacement/base/index/scale, and `*` for indirect calls/jumps.
Integer width suffixes are checked; LEA's sizes only its destination, and a
complete alias (`jnb`, `jnl`, `sal`) is never read as a suffix. `pushw` covers
the 16-bit register and memory forms as well as immediates. AT&T extension
spellings (`movzbl`, `movzwl`, `movsbl`, `movsbq`, `movslq`, and their other
source/destination widths) check both widths. `cbtw/cwtl/cltq/cwtd/cltd/cqto`
name the corresponding sign-extension instructions.

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
  masks/zeroing and supported `{1to8}`/`{1to16}` memory broadcasts;
- the processor-state instructions C's inline assembly uses (#147): `cpuid`,
  `xgetbv`, `xchg`, `bswap`, the x87's `fnstcw/fldcw/fnstsw/fnclex`,
  `stmxcsr/ldmxcsr`, and SSE2's `pcmpeqd` in its legacy forms. An `xchg` takes
  its memory operand first in Intel order, as the catalogue has it. The operand
  grammar has no accumulator role, so `fnstsw` takes memory, not `%ax`, and an
  `xchg` of the accumulator with another register takes the two-byte form,
  where GNU as takes the one-byte one;
- operandless `pause`, `lfence`, `mfence`, `sfence`, `rdtsc`, and `rdtscp`
  (#798). The assembler requires a target with RDTSCP for `rdtscp`; inline
  templates admit it independently of the target, so their author is responsible
  for selecting instructions supported by the CPU and declaring register outputs;
- what naked functions' templates use (#147): `pushfq/popfq`, SSE2's `movq`
  between an XMM register and a general one or memory, the x87's `fld` and
  `fstp` of 4, 8 and 10 bytes (AT&T's `flds/fldl/fldt` and
  `fstps/fstpl/fstpt`), the string instructions `movs/stos/lods` alone and with
  `rep` and `cmps/scas` alone and with `repe/repne` (`rep` and its prefix's
  aliases one mnemonic with the instruction, AT&T's `l` the dword form), and
  AVX-512F's `vmovdqa64/vmovdqu64`. In AT&T syntax `movq` is SSE2's when an
  operand is an XMM register and `mov`'s 64-bit form otherwise. Between memory
  and an XMM register it takes SSE2's own forms (`F3 0F 7E`, `66 0F D6`), as GNU
  as does, rather than the general register's `REX.W` ones of the same length.

Only forms covered by the frontend's operand-role grammar are admitted.
Missing families, rounding/SAE, explicit instruction-prefix syntax, far
branches, 16/32-bit execution modes, other segment registers, VSIB, section
groups, GOT/TLS relocation modifiers, debug directives and macro-assembler
directives are diagnosed. An unsupported form never reaches the trusted encoder.

Statements end at newline or semicolon. `#` and `//` start line comments;
`/* ... */` comments preserve statement newlines. Strings preserve embedded
comment characters and support quote/backslash, `n/r/t/b/f`, and one to three
octal escape digits. Source slices need no terminator or padding. Diagnostics
report the physical source line and byte column of the first error in `.s`.
For `.S`, line markers recover the presumed filename and line, including
includes and `#line`; the column refers to the expanded assembly line.

Named labels, GCC's quoted symbol names, and numeric labels `0` through `9` support forward/backward
references. Numeric labels have a namespace distinct from user names.
An undefined ordinary symbol becomes an external; undefined `.L` names,
numeric labels and explicitly local symbols are errors. Global/weak symbols
retain relocations where interposition or linking can change their address.
As with GNU as without `-mshared`, same-section global `jmp`/`jcc` targets
resolve in place. Global calls retain PLT32, and cross-section local branches
use PC32.

Directives:

| Purpose | Supported directives |
| --- | --- |
| Sections | `.text`, `.data`, `.bss`, named `.section` with `a/w/x/M/S/T` flags, `@progbits`/`@nobits`, merge entry sizes |
| Symbols | `.globl`/`.global`, `.local`, `.weak`, `.type` (`function/object/notype`), `.size` |
| Constants | `.equ`/`.set` (absolute and defined before use; either may reassign), `.equiv` (rejects redefinition) |
| Data | `.byte`, `.word`/`.short`/`.value`, `.long`/`.int`, `.quad`, `.ascii`, `.asciz`/`.string`, `.zero` |
| Alignment | `.align`/`.balign` with power-of-two byte alignment, `.p2align`; optional byte fill and maximum skip (0 is none) |
| Metadata | `.file "name"`, `.ident`, `.addrsig`, `.addrsig_sym` |
| Unwind | `.cfi_startproc [simple]`, `.cfi_endproc`, CFA/register/offset rules, state save/restore, escapes, return column and signal-frame metadata |

Each section keeps its own name, properties, alignment, layout and atom, including
`.text.*`, `.data.*`, `.rodata.*`, `.bss.*` and mergeable string sections.
Redeclarations must preserve its properties; zero-fill sections accept only zero
fill. Section groups, subsections, non-byte string characters and additional ELF
section types are diagnosed. Exported absolute symbols and forward constants are
diagnosed; the shared object writer currently lacks the former's unit ownership.
The writer automatically emits a non-executable-stack note. Both GCC's
`.section .note.GNU-stack,"",@progbits` and Clang's quoted section name are
accepted; the note must remain empty and non-executable.
Code alignment without a fill, or with `0x90`, pads with GNU as's NOPs: a
one-byte nop first when the section's last statement was data, a jump over the
rest past seven NOPs' worth, then the remainder's NOP and 11-byte ones. Other
fills, and other sections, use the fill byte (zero by default). `.` in a data
directive is the address of its own datum.
Direct branches require label or symbol targets. Absolute numeric destinations
are diagnosed because the shared writer has no absolute-symbol relocation;
they are never interpreted as relative instruction displacements.

Expressions use decimal, octal, hex and binary integers; parentheses, unary
`+/-/~/!`, multiplication/division/remainder, shifts, bitwise `&/|/^/!`,
addition/subtraction, comparisons, and logical `&&/||`. Precedence follows
[GAS](https://sourceware.org/binutils/docs/as/Infix-Ops.html): multiplication
and shifts, then bitwise, additive, comparisons, `&&`, and `||`. Comparisons
are signed and return -1 or 0; logical operations return 1 or 0. There is no C parser assumption. Intermediate values must stay
within `[INT64_MIN, UINT64_MAX]`; each output field has a separate range check.
Bitwise operations use 64-bit two's-complement values (`~0` is `-1`).
One symbol plus an addend is relocatable. Same-section label differences can
be used in data and `.size`; instruction operands require an absolute value
or symbol plus addend. Constant values are captured at their use point.

Instruction selection preserves immediate values: an imm8 sign-extending
form can represent `-128..127`, so `add rax,200` uses a wider form. Whole-width
fields accept signed values or unsigned bit patterns of their width.
The assembler canonicalizes a whole-width unsigned spelling before matching:
`andw $65535,%si` chooses the same sign-extended imm8 as `andw $-1,%si`.
The matcher takes a value's immediate by the compiler's own rule
(`x86_immediate_encodes`, [the x86 semantics](x86-semantics.md)) and writes its
field's bits (`x86_immediate_field`), so an unsigned spelling of a sign-extended
pattern takes the shorter field without a register to give its width too:
`pushw $0xffc8` is `66 6a c8`, as GNU as assembles it. A count or a control
byte takes the field's bits, signed or unsigned. `x86_match_initialize` needs
`x86_semantics_initialize` first.
Relocations are PC32/PLT32, ABS32/ABS32S and ABS64 with explicit signed 64-bit
addends. Symbolic byte/word data, nonrepresentable expressions and oversized
fields fail instead of truncating. Absolute memory uses signed disp32; 16/32-bit
LEA destinations use ABS32 because those instructions truncate the address.

Assembler-local `.L*`, numeric labels and `.` do not appear in the object symbol
table unless a relocation needs the symbol itself, such as certain string-pool
references. Ordinary named locals remain visible.

CFI emits `.eh_frame` with GNU-as-compatible CIE/FDE bytes and PC32 relocations.
Supported rules are `def_cfa`, `def_cfa_register`, `def_cfa_offset`,
`adjust_cfa_offset`, `offset`, `rel_offset`, `val_offset`, `register`, `restore`,
`undefined`, `same_value`, `remember_state`, `restore_state` and `escape` (each
with the `.cfi_` prefix). Registers may be DWARF numbers or supported x86 names.
The default x86-64 initial CFA/return-address rules can be suppressed by `simple`.
`return_column`, `signal_frame` and `.cfi_sections .eh_frame` are supported.
Personality/LSDA augmentation, `.debug_frame`/SFrame output and procedures that
cross sections are diagnosed. Unsupported CFI is never silently discarded.

## Code inside another function

`asm_assemble_code(source, index, options, output, scratch)` assembles a source
whose records another function holds: C's inline assembly, after the
lowering substitutes its operands ([the inline assembly guide](c-inline-assembly.md)).
It returns `AsmCode`:

- the instructions' records, in order;
- each `jmp` and `jcc` to the source's own label without an addend as branch
  sizing's columns: a rel8 at displacement 0, its target the label's next
  record or the count for the end;
- each other symbolic field as a reference by name, with its field, kind and
  addend, the record marked `BUSTER_X86_FORM_FIXUP`: a name the source doesn't
  define (`.L` ones among them, which another source may define), or one of its
  own named labels as data, as a call's target or with an addend (a branch's
  from its rel32 form);
- its named labels, each with its next record: the caller makes them symbols.

It takes instructions, labels and the syntax directives, and refuses:

- every other directive: the code has no sections, data or symbols of its own;
- its numeric labels and `.` but as a `jmp`'s or `jcc`'s target without an
  addend, and `loop` and `jrcxz` to its labels;
- undefined numeric labels.

Lexing, parsing and matching are `asm_assemble`'s, by `AsmOptions.vectorized`'s
path. The parser's tables come from `scratch`, which is as it was after. The
result comes from `output`, which a failure leaves as it was too.
`test-assembler` checks it by both paths, which must give the same records,
columns, references and diagnostics.

## Data-parallel design and scalar twin

`asm_assemble(source, index, options, arena)` returns `AsmResult`, containing a
`Link`, a diagnostic byte offset/message and phase timings. The source, index
and output arena must remain alive while the result is consumed. Initialize
the caller-owned `X86MatchIndex` and encoder once. Failure restores the caller's
arena position and clears the output link. Sources are bounded to 16 MiB,
sections to 256 MiB, and expression stacks to 128 entries.

The section adapter in `assembler_sections.c` hands `object_build` optional
`ObjectOptions.atom_sections` indexed by AtomId and `temporary_symbols` indexed
by SymbolId. Both counted tables cover their entire Link column, including id
zero, and belong to the same result arena. Names are counted slices; no table
or name requires padding. Every described atom already contains one complete
section, so the writer need not reconstruct grouping. A zero-initialized
`ObjectOptions` retains the compiler writer's existing grouping and symbols.
The writer checks table counts, section types and merge entry sizes before
emitting bytes, then compacts temporary locals only after seeing every fixup.

`assembler_cfi.c` retains ordered directives as events attached to fragment IDs.
`asm_cfi_emit` consumes final fragment positions after monotone branch relaxation,
deduplicates identical initial CIE programs and emits FDEs in procedure order.
Layout and emission share stable per-section fragment ranges. The single
post-layout CFI fragment is inserted into their reserved extra slot, preserving
all source fragment identities and each section's order. Emission visits only
that section's range; adding function sections does not multiply full-stream
rescans. Instruction encoding may batch across intervening declarations of
other sections because their final bytes are contiguous in the code section.
Empty CFI emits no atom; malformed state, capacities and representability limits
produce diagnostics and restore the assembly arena. The native code generator's
fixed-frame CFI producer remains separate.
CFI byte capacity depends on its event count, procedure headers and actual escape
payload, with separate scratch for the largest understood initial CIE program.
Unrelated source bytes do not inflate either buffer. A sparse-CFI stress case
compares retained arena use with a 16-byte and a 1 MiB comment prefix.

The vector path is selected explicitly, without host dispatch:

1. The lexer classifies 64 source bytes, computes word/token boundary masks,
   compresses starts/ends/kinds in registers and transposes them into 16-byte
   token records. Masked tails respect the logical slice. Long words and
   comments/strings have bounded rare paths; the full scalar lexer is the oracle.
2. Eight bounded register spellings are classified together and cached in the
   token's existing reserved field. Two 32-entry tables in ZMM registers resolve
   short names by hash and full-key comparison; vector arithmetic handles
   numbered GPRs, XMM/YMM/ZMM and opmasks. The complete scalar spelling grammar
   remains independent. Both paths accept mixed-case register names and leave
   noncanonical numbers such as `r08` and `xmm00` as symbols. Intel and AT&T
   register/address/decorator parsing reuse this cache. Common Intel statements
   still form batches of up to 16 instructions; their decimal operands reduce
   digit pairs and quartets instead of an eight-step multiply chain. Leading-zero
   integers retain the scalar octal grammar. Labels and directives delimit
   batches, and expression/state decisions keep their exact source order.
3. An open-addressed dictionary gives each canonical mnemonic its own contiguous
   form group, preserving catalogue order. `movabs` and `pushw` retain their
   explicit alias predicates. Sixteen candidates compare
   operand kind/class/width, fixed registers, feature level, broadcasts and
   signed/unsigned immediate ranges in parallel. Only survivors construct
   final records and run `x86_validate`; the scalar matcher independently
   checks roles and values. Precomputed role/spelling columns eliminate repeated
   string and syntax interpretation. A surviving vector candidate runs complete
   validation once, then trusted scalar length encoding; the scalar oracle keeps
   the public validating encoder call. Final resolved records are validated
   separately as before. Encoder rows remain the encoding authority. This
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

The register tables use Zen 5's measured
[`VPERMW`](https://uops.info/html-instr/VPERMW_ZMM_ZMM_ZMM.html) cost:
one micro-op, four-cycle register latency and 0.50-cycle reciprocal throughput.
This avoids a memory gather; the measured ZMM
[`VPGATHERDD`](https://uops.info/html-instr/VPGATHERDD_ZMM_K_VSIB_ZMM.html)
is 64 micro-ops and about 12.6 cycles per instruction. Full-key comparison
after the permute rejects unknown spellings that hash to the same slot.
The decimal reductions use
[`VPMADDUBSW`](https://uops.info/html-instr/VPMADDUBSW_ZMM_ZMM_ZMM.html) and
[`VPMADDWD`](https://uops.info/html-instr/VPMADDWD_ZMM_ZMM_ZMM.html), each
one micro-op on FP0/1 and 0.50-cycle reciprocal throughput. Both have measured
latencies of three cycles from operand 2 and two from operand 3 to the result.
These guide the algorithm; the complete
assembler measurements decide whether its extra token pass and temporaries pay.

Relaxation starts short and only promotes a branch once. It recomputes all
alignment padding each round, stopping within `branch_count + 1` rounds.
Alignment can absorb growth, so this scheme guarantees a correct stable layout,
not the globally shortest encoding for every alignment arrangement. Rel8-only
branches are range-checked against the final layout. There is no implicit
expansion that changes flags or registers.

## Tests and measurements

`test-assembler [--cross-check] [--keep] [--seed N] [--fuzz N]` checks complete
scalar/vector token streams, diagnostics and byte-identical ELF output, valid
batches of 0..65 instructions at all 64 source misalignments, malformed text
and binary bytes, and both ends of protected pages for lengths 0..1099.
Fixtures cover immediate/operand boundaries, section changes, expressions,
labels, relocation addends, branch growth across data/alignment and failures,
the defects of #235, and code padding of every length to 135 bytes after an
instruction and after data, which the cross-check compares with GNU as byte
for byte when GNU as pads as binutils 2.47 does (2.42 orders its long NOPs
differently; the test says so and keeps its known-byte padding checks).
The cross-check compares section bytes/properties, symbols and relocations
with GNU as using the existing object comparator, and links/runs the examples.
Different ELF file layouts, GNU as's relocation order and automatic stack notes are normalized. Existing
independent encoder/GNU-as tests remain required.
The `.S` fixtures compare both complete driver paths, independently preprocess
with Clang's assembler-with-preprocessor mode, compare objects with GNU as,
and check included-source diagnostics and preservation of existing output
when assembly fails. Without external tools, the test also preprocesses pastes
and 20,000 pp-numbers that `$` splits into five tokens each through both
kernels, and reads assembly from a pipe.

The matcher tests also compare independent scalar and indexed-vector selection
on every admitted catalogue row with 64 operand variations: range limits,
symbols, invalid registers, implicit accumulators with inconsistent class/width,
memory widths, broadcasts and decorations. Register classification checks every
two-byte key, every mixed-case three-letter key, and all case combinations of
numbered/suffixed spellings, including leading zeroes and register limits. Decimal tests replace every digit
position with all 256 byte values at lengths 0..8 and all partial batch widths,
checking both values and untouched inactive stores against the scalar grammar.
Another 576 inputs place every RIP case spelling in either operand position,
after 0..17 valid rows and with or without a later malformed directive. They
check the exact diagnostic and byte location across the 16-row batch boundary:
the cache recognizes RIP for addresses, but the simple batch must leave a
standalone RIP operand to the full parser.

```sh
./build/compiler benchmark-assembler --external
./build/compiler benchmark-assembler --workload registers
./build/compiler benchmark-assembler --workload simd
./build/compiler benchmark-assembler --workload att
./build/compiler benchmark-assembler --only vector
./build/compiler benchmark-assembler --only vector --after-scalar 100
./build/compiler benchmark-assembler --input /path/to/compiler-output.s --external
```

Run each separately under the 9700X lock with `taskset -c 2`. The default
workload has 256 generated functions, including branches, memory, alignment,
external references and section changes. These are controlled synthetic
workloads, not a compatibility corpus. `--functions 1..2048` changes size.
There are 21 interleaved samples after a warmup; output is checked outside the
timed interval. Phase medians and whole-run medians are reported independently.
`--input` reads a counted source once before timing, so the same phase/object
measurements can cover frozen compiler output. Its printed function count is
zero (unknown), not an inferred symbol count. The CFI column is a subset of
encode/link time, not an additional phase to add to the total.
Initialization and index/work-arena sizes are included in the report.
`--only` measures one path back-to-back; `--after-scalar` warms scalar assembly
outside each sample to expose the known Zen 5 transition state.

`--external` rotates fresh Buster vector, Buster scalar, GNU as and Clang
integrated-assembler processes. They read the same warm `.s` file and overwrite
the same object path. All include startup, input and output I/O; none preprocess
the input. The component benchmark excludes file I/O and reusable initialization,
so its numbers must not be compared directly to those subprocess times.

### 9700X measurements, 2026-10-21

Revision `83291a2a7e96489b714266f04d3429b97f4c6746`, production `build.sh`
(`-O3 -march=native`), Clang 23.1.1, GNU binutils 2.47, Linux 6.18.50-2-lts,
Ryzen 7 9700X, CPU 2. Each command held the shared exclusive lock separately.
The following are 21-pass medians with 256 generated functions:

| Workload | Instructions | Source bytes | Scalar total ms | Vector total ms | Scalar / vector |
| --- | ---: | ---: | ---: | ---: | ---: |
| Mixed Intel | 9,984 | 153,295 | 3.519 | 2.145 | 1.64 |
| Intel registers/immediates | 8,448 | 115,261 | 3.118 | 1.718 | 1.81 |
| AVX-512 masks/broadcasts | 8,448 | 317,029 | 3.684 | 3.384 | 1.09 |
| AT&T | 9,984 | 177,355 | 3.325 | 2.581 | 1.29 |

Mixed Intel phase medians, in milliseconds:

| Path | Lex | Parse | Match | Layout | Encode/link | Object |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Scalar | 0.107 | 1.373 | 1.584 | 0.167 | 0.276 | 0.010 |
| Vector | 0.052 | 0.864 | 0.968 | 0.167 | 0.082 | 0.010 |

The mixed run's one-time encoder/index initialization was 0.939 ms. Its index
used 401,928 bytes, work arena 16,816,496 bytes and ELF output 99,760 bytes.
Work-arena usage is allocated space, not total RSS: source/reference buffers,
shared encoder tables and unused virtual arena reservations are separate.
Register, SIMD and AT&T work arenas used 13,039,928, 28,538,736 and 20,633,968
bytes respectively. Memory remains a useful optimization target.

The common Intel batch parser reduces register/immediate parsing from 1.162
to 0.523 ms. Decorated SIMD still uses the exact grammar (2.230 ms scalar,
2.302 ms vector), as does AT&T (1.291 versus 1.292 ms). Those are measured
remaining costs, not claims that these grammars cannot be vectorized. Layout
also remains essentially equal at these sizes; ordered symbol/fragment work
and frequent alignment boundaries limit the benefit of its prefix scans.

For the mixed input, fresh-process medians were 4.222 ms Buster vector,
5.436 ms Buster scalar, 4.772 ms GNU as and 12.872 ms Clang integrated assembler.
These include the same warm input/output file boundary and each tool's startup;
Clang's driver cost is included. This is one synthetic workload, not a general
assembler throughput ranking or a `.S` preprocessing measurement.

Back-to-back vector mixed runs measured 2.111 ms; runs following 100 ms of
scalar assembly measured 2.121 ms. This experiment showed no large transition
penalty on this workload; it does not establish that Zen 5 transition state is
irrelevant for other workloads.

For attribution, a separate 2,048-function vector benchmark (79,872 instructions
per assembly) under `perf stat` recorded 2,475,482,186 cycles, 11,880,699,707
instructions, 2,092,414,844 branches, 3,286,114 branch misses and 6,583,238 cache
misses over the entire 0.447 s benchmark process. These counts include its
initialization, warmups and output checks, rather than just a timed kernel.
At 999 Hz, a separate profile collected 415 samples with none lost: approximately
25% were in `asm_assemble` (including inlined matching/layout), 16% in
`asm_parse`, 11% in `encode_one`, and 24% combined in `instruction_is_valid`,
`mode_register_is_valid` and `template_checks`. Candidate validation and scalar
length encoding are visible remaining costs. The generated lexer code contains
masked ZMM byte comparisons and register-form `vpcompressd`; source intrinsics
were not optimized into a scalar byte loop.

The implementation is original; the syntax references are the GNU assembler's
[x86 syntax](https://sourceware.org/binutils/docs/as/i386_002dVariations.html),
[expressions](https://sourceware.org/binutils/docs/as/Expressions.html) and
[alignment](https://sourceware.org/binutils/docs/as/Align.html) documentation.
No GNU implementation code was imported.

### Parser and matcher optimization, 2026-10-22 (#234)

The baseline is `7c291ed22e2993b53f0bd2a9202b9e1c32f779e0`: main's decoder
implementation plus the unchanged assembler fixes from PR #237 at `b7db5bf`.
The optimized code is `c13f5a6d013375922daeeeaab39654ed4ad1d5e4`. Both use the
same production build and benchmark on the 9700X above, CPU 2, with a separate
exclusive lock for every command. The baseline was rebuilt in an isolated
directory from its exact tracked revision. The following SHA-256 values identify
the two immutable executables used for that experiment's repeats:

| Executable | SHA-256 |
| --- | --- |
| Baseline | `8a6d0c5cc28bf060d1be15f34c1c46967bf1f8531663467d84ed848add43ab58` |
| Optimized | `6badd67c1b52dfb81a032b526acaf70299c2207071f2701698c2c45aa56f9304` |

Each entry below is the median of three independent process medians, each of
21 interleaved passes. Brackets give the range of those three medians; they
are not confidence intervals. All retained processes are included, including
the slower register-workload sample. Workloads and instruction counts are the
same as the preceding table.

| Workload | Baseline vector ms [range] | Optimized vector ms [range] | Less total time |
| --- | ---: | ---: | ---: |
| Mixed Intel | 2.209 [2.190, 2.218] | 1.528 [1.512, 1.556] | 30.8% |
| Intel registers/immediates | 1.743 [1.742, 1.786] | 1.266 [1.247, 1.546] | 27.4% |
| AVX-512 masks/broadcasts | 3.580 [3.450, 3.743] | 1.793 [1.788, 1.799] | 49.9% |
| AT&T | 2.703 [2.697, 2.723] | 1.685 [1.682, 1.708] | 37.7% |

The repeated vector phase medians show where the changes pay:

| Workload | Parse baseline / optimized ms | Match baseline / optimized ms |
| --- | ---: | ---: |
| Mixed Intel | 0.902 / 0.522 | 0.985 / 0.681 |
| Intel registers/immediates | 0.539 / 0.306 | 0.853 / 0.586 |
| AVX-512 masks/broadcasts | 2.487 / 0.864 | 0.579 / 0.418 |
| AT&T | 1.398 / 0.671 | 0.962 / 0.676 |

Layout, encoding/linking and object construction are essentially unchanged.
The scalar reference totals were 3.778 to 3.878 ms for mixed Intel (+2.6%),
3.273 to 3.367 ms for registers (+2.9%), 3.902 to 3.697 ms for SIMD (-5.3%),
and 3.467 to 3.488 ms for AT&T (+0.6%). Its semantic matching and validating
encoder calls remain independent; the default-path improvement is not a claim
that every scalar workload improved.

Separate `--only vector` runs, again three processes per cell, distinguish
sustained work from work immediately following 100 ms of scalar assembly:

| Workload/state | Baseline ms [range] | Optimized ms [range] |
| --- | ---: | ---: |
| Mixed, sustained | 2.214 [2.164, 2.244] | 1.511 [1.504, 1.553] |
| Mixed, after scalar | 2.211 [2.208, 2.230] | 1.538 [1.517, 1.558] |
| SIMD, sustained | 3.536 [3.522, 3.626] | 1.834 [1.793, 1.854] |
| SIMD, after scalar | 3.610 [3.587, 3.627] | 1.789 [1.784, 1.870] |

The gain survives both states; these samples do not show a consistent large
transition penalty. Fresh-process mixed assembly also improves, with startup
and file I/O included. These are three independent `--external` processes,
each rotating 21 samples of every tool:

| Tool | Baseline experiment ms [range] | Optimized experiment ms [range] |
| --- | ---: | ---: |
| Buster vector | 4.294 [4.273, 4.295] | 3.569 [3.552, 3.581] |
| Buster scalar | 5.647 [5.642, 5.653] | 5.732 [5.710, 5.762] |
| GNU as | 4.815 [4.792, 4.857] | 4.816 [4.799, 4.818] |
| Clang integrated assembler | 12.383 [12.373, 12.421] | 12.380 [12.346, 12.529] |

This is a 16.9% reduction in Buster's complete vector command time on the
mixed synthetic workload. It is not a general assembler ranking or a `.S`
preprocessing result. Repeated mixed initialization medians were 0.734 ms
baseline and 0.715 ms optimized, with overlapping ranges. The matcher index
grows from 376,872 to 419,496 bytes (+42,624); cached register descriptors use
existing token storage. Work arenas and output sizes are unchanged. Index size
is not total RSS or the encoder's shared table storage.

The production candidate passed 360,077 checks with GNU-as cross-checks.
An `-O1 -fsanitize=address,undefined -fno-omit-frame-pointer` build passed
396,161 checks with `--cross-check --fuzz 20000 --seed 7`, leak detection and
undefined-behavior failures enabled, with no sanitizer diagnostics. That
executable's SHA-256 is
`a7b492a39d051edfb0494dc9a43d2856903308513f285656fbdd189d6077c90b`.
The call-graph check found no cycles across 18 translation units and 6,630
functions. Independent review found an implicit-accumulator class/width
mismatch in the vector prefilter; it was corrected and added to the direct
matcher tests before these final builds and measurements.

Generated code inspection confirms register-form ZMM `vpermw` table lookups
in `asm_assemble` and ZMM `vpmaddubsw`/`vpmaddwd` decimal reductions in
`asm_parse`. A separate 9,999 Hz cycle profile of the 2,048-function vector
benchmark collected about 3,000 samples with none lost: `asm_assemble` 23.81%,
`x86_match` 13.32%, `asm_parse` 11.79%, `encode_one` 10.32%, and validation
helpers 16.11% combined. These are whole-process samples, including setup and
checks. Candidate construction/validation, ordered grammar and layout remain
substantial costs; the results do not establish a throughput ceiling.

A separate pair of `perf stat` runs on the same 2,048-function workload
recorded 2,614,092,053 to 1,932,733,889 user cycles, 11,985,796,733 to
9,114,257,064 instructions, 2,126,225,601 to 1,423,351,515 branches and
3,239,703 to 1,030,654 branch misses, baseline to optimized. Counters ran
100% of the enabled time. These whole-process counts include initialization,
warmups, validation and the 21 passes; they are attribution evidence, not
per-instruction kernel counters or a substitute for the repeated timings.

### Final register syntax and diagnostic validation, 2026-10-22

The final PR #237 fixes add case-insensitive register names, canonical register
numbers, register-expression diagnostics and unary-plus address terms. Revision
`2e36d2b6b9603fed5872dd2cfad4259bdb439181` preserves those rules in the scalar
grammar and the vector cache. The latter folds ASCII letters in parallel and
rejects noncanonical numbers such as `r08` and `xmm00`. RIP's cache entry remains
available for addresses, while a vector class mask excludes it from standalone
operand batches. This preserves the parser's diagnostic location and source-order
error reporting, including when a later directive is also invalid.

Both production and `-O1 -fsanitize=address,undefined -fno-omit-frame-pointer`
builds passed 1,909,405 checks on the 9700X with
`test-assembler --cross-check --fuzz 20000 --seed 7`. This includes the 576 RIP
inputs above, GNU-as cross-checks, executable and `.S` fixtures, guarded memory,
and exhaustive register/decimal tests. The sanitized run enabled leak detection
and stopped on undefined behavior; no sanitizer diagnostics occurred. Every
build and test held the shared lock separately. A focused follow-up review
found no other cached register class bypassing a parse-time restriction in the
Intel simple batch.

The full [9700X CI run](https://github.com/buster14a/buster/actions/runs/39755230518)
also passed for this head, including 1,873,321 assembler checks with seed 1 and
2,000 fuzz cases. Its call-graph check found no cycles across 18 translation
units and 6,860 functions. The combined disassembler/GNU-tool tests passed too.
CI tested merge `52d8d53d3a15b86107616abd1a550acd4f347f1e`, combining this
head with main `9713624349981908171fae1eb73d0d2bf05e351e`.

| Executable at `2e36d2b` | SHA-256 |
| --- | --- |
| Production | `0ad5957568379c99aa4f63e86bacc0ca5a1f08fa7c0482a10d9f5aedd60d4eec` |
| ASan/UBSan | `6313e295ce88a4ce85ec1bccdca2aef39835970ea67675606ce5929ea6756320` |

Inspection of that exact production executable confirms ZMM `vpermw` register
table lookups and masked byte case folding in `asm_assemble`. `asm_parse`
contains the register-class mask that excludes standalone RIP, followed by
ZMM `vpmaddubsw` and `vpmaddwd` decimal reductions. `x86_match` uses masked
ZMM column loads and comparisons for its candidate prefilter. The executable
copied back for inspection has the same SHA-256 as the frozen benchmark candidate.

### Measurements after the final syntax fixes, 2026-10-22

The following comparison includes the final register rules and RIP diagnostic
fix. Its baseline is main `50350966fe85a88947457d7ce765d1c2b70c9148`, with
production executable SHA-256
`2bb8107d0def189496b920c318941c2b5aaeb0cbc97636b7433b54442768588d`.
The candidate is the production `2e36d2b` executable identified above. Both
are isolated builds, frozen before measurement, using the same benchmark
source (SHA-256
`64dcd006393830127191ddbdd589d28d960f84f5a7037043730f4226dba5f102`).
The toolchain, host and CPU pinning are the same as above. Every invocation
acquires and releases the shared host lock separately, including between
baseline and candidate; waiting CI and other owners run between invocations.

There are three processes per workload and variant, each with 21 internal
passes. The second round reverses baseline/candidate order. The mixed workload
also runs `--external`; separate `--only vector` runs measure sustained work
and work immediately after `--after-scalar 100`. Values are medians of the
three process medians, with their full ranges in brackets; no samples are
discarded. Input sizes and instruction counts match the earlier tables.

| Workload | Baseline vector ms [range] | Optimized vector ms [range] | Less total time |
| --- | ---: | ---: | ---: |
| Mixed Intel | 2.227 [2.220, 2.244] | 1.527 [1.526, 1.545] | 31.4% |
| Intel registers/immediates | 1.793 [1.780, 1.810] | 1.256 [1.254, 1.265] | 29.9% |
| AVX-512 masks/broadcasts | 3.568 [3.493, 3.637] | 1.823 [1.806, 1.885] | 48.9% |
| AT&T | 2.644 [2.631, 2.767] | 1.697 [1.696, 1.704] | 35.8% |

The optimized totals are 152.9, 148.7, 215.8 and 170.0 ns per instruction,
respectively. The reductions exceed the observed process ranges for every
workload. The repeated phase medians attribute the gains to parsing and
matching:

| Workload | Parse baseline / optimized ms | Match baseline / optimized ms |
| --- | ---: | ---: |
| Mixed Intel | 0.921 / 0.528 | 0.980 / 0.673 |
| Intel registers/immediates | 0.561 / 0.310 | 0.866 / 0.590 |
| AVX-512 masks/broadcasts | 2.474 / 0.887 | 0.580 / 0.419 |
| AT&T | 1.339 / 0.681 | 0.965 / 0.677 |

The independent scalar reference has modestly higher medians, so these gains
apply to the default vector path:

| Workload | Baseline scalar ms [range] | Optimized scalar ms [range] | More total time |
| --- | ---: | ---: | ---: |
| Mixed Intel | 3.834 [3.751, 3.856] | 3.998 [3.905, 4.000] | 4.3% |
| Intel registers/immediates | 3.327 [3.259, 3.354] | 3.385 [3.351, 3.422] | 1.7% |
| AVX-512 masks/broadcasts | 3.874 [3.805, 3.907] | 3.917 [3.863, 3.919] | 1.1% |
| AT&T | 3.418 [3.418, 3.550] | 3.519 [3.519, 3.534] | 3.0% |

The corrected candidate also retains the gain in both vector states:

| Workload/state | Baseline ms [range] | Optimized ms [range] |
| --- | ---: | ---: |
| Mixed, sustained | 2.214 [2.195, 2.266] | 1.547 [1.516, 1.563] |
| Mixed, after scalar | 2.258 [2.237, 2.263] | 1.534 [1.523, 1.574] |
| SIMD, sustained | 3.525 [3.522, 3.621] | 1.815 [1.806, 1.873] |
| SIMD, after scalar | 3.613 [3.474, 3.650] | 1.815 [1.810, 1.824] |

The complete mixed-input command includes startup and file I/O:

| Tool | Baseline experiment ms [range] | Optimized experiment ms [range] |
| --- | ---: | ---: |
| Buster vector | 4.317 [4.294, 4.323] | 3.565 [3.530, 3.588] |
| Buster scalar | 5.558 [5.551, 5.621] | 5.772 [5.743, 5.798] |
| GNU as | 4.821 [4.794, 4.829] | 4.815 [4.772, 4.849] |
| Clang integrated assembler | 12.247 [12.228, 12.673] | 12.460 [12.339, 12.774] |

The corrected vector command uses 17.4% less time. The external tools are
controls run alongside each experiment, not changed implementations. These
synthetic `.s` inputs do not measure `.S` preprocessing or general compatibility.
Mixed initialization is 0.860 ms [0.788, 0.868] baseline and 0.870 ms
[0.785, 0.906] optimized, with overlapping ranges. The matcher index remains
376,872 versus 419,496 bytes; token storage, work arenas and output sizes are
unchanged. The earlier source-pinned hardware counters describe the original
optimization experiment, not new counter measurements of this corrected binary.

### Main integration check, 2026-10-22

Revision `1034885a8e4090e42049e833e484b22bce7f0152` integrates the corrected
candidate with main `d9ee4394a758fc8d897ff4d70ea5b36571584ae0`. The assembler,
matcher, assembler tests and benchmark sources are unchanged; main adds other
translation-unit content and build inputs. The separately locked production
build has SHA-256
`374aa01093e1b9b47d133b224cba301eabfdd335d54b62cb655ab1a5ef2c3092`.

Following the [profiling guide](profiling.md), a further 18 invocations check
the integration: three processes each for mixed input with `--external` and
SIMD input, plus three per mixed/SIMD and sustained/after-scalar cell. Each
process uses 21 passes, CPU 2 and its own lock. The middle round reverses the
workload order. This is a focused comparison with the frozen corrected
candidate; it does not rerun registers or AT&T or replace the controlled
baseline comparison above.

| Case | Corrected candidate ms [range] | Integrated ms [range] |
| --- | ---: | ---: |
| Mixed, interleaved | 1.527 [1.526, 1.545] | 1.530 [1.526, 1.542] |
| SIMD, interleaved | 1.823 [1.806, 1.885] | 1.839 [1.818, 1.898] |
| Mixed, sustained | 1.547 [1.516, 1.563] | 1.546 [1.531, 1.555] |
| Mixed, after scalar | 1.534 [1.523, 1.574] | 1.543 [1.532, 1.569] |
| SIMD, sustained | 1.815 [1.806, 1.873] | 1.803 [1.803, 1.806] |
| SIMD, after scalar | 1.815 [1.810, 1.824] | 1.881 [1.871, 1.884] |

The mixed command is 3.568 ms [3.540, 3.578], against the corrected candidate's
3.565 ms [3.530, 3.588]. The integrated scalar medians are 3.919 ms
[3.895, 3.928] for mixed input and 3.876 ms [3.809, 3.929] for SIMD input.
The vector gains remain, but integration is not free in every state: SIMD
after scalar work takes 3.6% more time, with non-overlapping process ranges.
These runs establish the observed difference, not its cause. The other vector
ranges overlap or touch. This check covers the named integration revision;
it is not a measurement of subsequent main changes.

## Boundary audit #298

Audit pin: `42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`;
[scope and evidence](simd-boundary-audit.md). Sources:
[frontend](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/assembler.c#L4)
and [I/O/object consumer](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/assembler_driver.c#L4).
Assembly is a real alternative producer of encoder records and Link data and
must remain in the shared-interface inventory.

| Producer → consumer | Contract |
| --- | --- |
| Counted assembly bytes → `asm_lex` → statements/operands | `AsmToken` is four u32 fields: start/end/kind/reserved. Vector register classification caches its bounded result in reserved; source spans remain for diagnostics. |
| `.S` preprocessing → printed assembly bytes → assembly lexer | Uses the PP assembly mode and spacing rules. This materialized text handoff is real; it is not C parser token conversion. |
| Statements/expressions → shared `x86_match` → fragments/records | Symbols and relative expressions retain identity/addends. Public input rejects unsupported forms and validates records before trusted encoding. |
| Fragments/labels/alignment → monotone layout → encoding/Link | Variable fragment counts and final offsets; branches promote, alignment is recalculated. `asm_object` passes the shared Link to object preparation/building. |

The public source is bounded (16 MiB limit); the driver can read streams and
keeps its own terminator, while reusable `asm_assemble` takes a counted slice.
Caller arenas own tokens, fragments, symbols, emitted bytes and Link, and must
outlive object construction. Logical counts, allocated capacities and section
size limits (256 MiB) are distinct. Internal vector operations remain bounded;
spare arena space is not public readable padding. Failure returns diagnostics
rather than sending an invalid record to the unchecked encoder.

A logical assembly unit/section contains ordered directives, labels and
alignment, so sorting instructions or independently sizing arbitrary fragments
is invalid. Layout terminates by monotone promotion and checks rel8-only
instructions after final placement. This differs from C `size_branches`, whose
record order is fixed and has no assembly alignment fragments; replacing one
with the other would lose a real dependency.

Disposition: retain shared form matching, cached register classification and
direct Link output. `.S` text materialization and per-fragment layout are
candidates only with producer + PP printing + lex/match/layout + object costs;
a faster matcher alone cannot justify another token representation.
`test-assembler --cross-check` checks scalar/vector/GNU as bytes and execution;
`benchmark-assembler` includes its documented phase/initialization/memory costs.
The #233 compatibility corpus checks compiler-generated assembly through the
same real producer/writer boundary, including exceptional syntax, capacities
and output order.

## Compiler-output compatibility corpus (#233)

`examples/assembler/compiler-corpus.c` and its independent caller are original
fixtures. `test-assembler --cross-check` invokes GCC and Clang at O0/O2, in
AT&T/Intel syntax, with ordinary and separate function/data sections: 16 cases.
It targets baseline x86-64, disables CET and PIC/PIE for this corpus, and asks
Clang for GNU-compatible spelling with `-fno-integrated-as`. LLVM-only address
significance annotations have separate acceptance/refusal fixtures.

Every case requires byte-identical scalar/vector objects, GNU-as section
properties/bytes/symbol/relocation comparison, and successful execution of all
three objects linked to independently calculated results. The unwind callback
must identify both the assembled `corpus_call` and the caller's `main` by
`_Unwind_GetRegionStart`, as well as reach the expected frame depth. GNU
relocation records are sorted by offset using the existing importer sorter;
only the GNU copy's record order is normalized, never relocation meaning or
section bytes. `--keep` retains every generated source and object; a failing
case is retained regardless of that option.

Focused independent GNU-as fixtures additionally cover all expression operator
pairs, AT&T extension/conversion aliases, uppercase Intel operands, parenthesized
register addresses, constant reassignment, quoted names, local symbols,
same/cross-section branches, unsigned whole-width immediates, narrow LEA and CFI.
CFI cases exercise common/different CIEs, simple/signal frames, saved-state
transitions, signed/extended offsets/registers and all advance-location widths,
including a relaxed branch and alignment before a rule. A native command check
links and executes a 32-bit LEA against an address at `0x80000000`.

Syntax and encoding rules are checked against the
[GNU assembler manual](https://sourceware.org/binutils/docs/as/), including its
[section](https://sourceware.org/binutils/docs/as/Section.html) and
[CFI](https://sourceware.org/binutils/docs/as/CFI-directives.html) descriptions.
The compiler-output corpus establishes its stated boundary; it does not imply
support for every instruction, assembler extension or CFI augmentation.

# x86 semantics

`src/buster/compiler/x86_semantics.c` says what the x86 forms a code generator
emits mean to the compiler, and which form keeps the value a selector asks for
([#221](https://github.com/buster14a/buster/issues/221)). The encoder
(`x86_64_encoder.c`, [`docs/x86-64-encoder.md`](x86-64-encoder.md)) writes a
record already selected and trusts it: an imm8 field takes [−128, 255], so a
record with `ADD_RM64_IMM8` and the byte `0xc8` encodes, and adds −56. Which form
implements `add rax, 200` (the imm32's), which registers and flags a form reads
and writes, and what a substitution may change are questions the selectors
(#136, #141), the allocator (#140), the scheduler (#172) and the assembler
(#222) all ask; this file answers them once, in tables of its own keyed by
`X86Form`, so that `X86Instruction` stays the encoder's 16 bytes and the
encoder's input stays trusted.

This document records the contract, the form choice, the design of the vector
paths and their scalar twins, the tests (whose oracle is the processor), the
covered subset and how to add a form, and the 9700X's measurements.

## What is where

- `x86_semantics.c`: the effects (`X86Effects`, `BUSTER_X86_EFFECT_ROWS`,
  `x86_effects`), the immediate's contract (`x86_immediate_encodes`,
  `x86_immediate_field`, `x86_immediate_value`), the flags
  (`x86_instruction_flags`, `x86_flags_substitutable`), operand-resolved effects
  (`x86_instructions_effects` and its twin `x86_instructions_effects_scalar`),
  and form choice (`x86_choice_row`, `x86_choose_forms` and its twin
  `x86_choose_forms_scalar`, `x86_choice_instruction`).
  `x86_semantics_initialize` builds the tables.
- `x86_semantics_test.c`: `test-x86-semantics`.
- `x86_semantics_benchmark.c`: `benchmark-x86-semantics`.

## The contract

### Effects

`x86_effects(form)` gives a form's `X86Effects`, 16 bytes; a segment override
and the fixup flag change nothing. A form outside the covered subset gets the
conservative record: it reads and writes every register, flag and byte of
memory, may fault, and may go anywhere (`BUSTER_X86_CONTROL_UNKNOWN`). So a form
nobody has described never looks free of effects, and covering one is
`operation != X86_OPERATION_NONE`.

**Explicit operands** are by encoding role, as `X86Instruction` names them:
`reg` (ModRM.reg), `rm` (ModRM.rm, a `+r` form's register, or a memory operand)
and `vvvv`. Each role has `BUSTER_X86_ROLE_BITS` bits (`x86_effects_role`):

- `READ` and `WRITE`; both is a tied operand (`add rdx, rcx`'s rm).
- `VECTOR`: an XMM, YMM or ZMM register; otherwise a general one.
- `REST`, for a register write: what happens to the register's bits past the
  `write_bytes` the form defines.

| `REST` | What happens to the rest | Forms |
| --- | --- | --- |
| `WHOLE` | nothing is left | 64-bit writes; a whole XMM register |
| `ZERO` | zeroed | 32-bit writes; `movd`, `movq` |
| `MERGE` | kept, so the write reads the register too | 8- and 16-bit writes, AH among them; SSE's scalar operations |
| `MERGE_IF_REGISTER` | kept from a register source, zeroed from memory | `movss` and `movsd` loads |

With a memory operand in rm, `READ` and `WRITE` are a load and a store,
except for `lea` (`BUSTER_X86_MEMORY_EFFECT_ADDRESS`), which only computes the
address.

`READ` and `MERGE` are different facts:

- `addsd xmm0, xmm1` reads xmm0's low element. Its `reg` is tied.
- `sqrtsd xmm0, xmm1` only keeps xmm0's upper half: its `reg` is `WRITE` and
  `MERGE`. That is the false dependency #172 wants to break, but not a tie.

Legacy SSE forms also keep a vector register's bits above 127
(`BUSTER_X86_EFFECT_KEEP_UPPER`).

**Implicit registers** are general-register masks besides the explicit
operands, for the fixed registers #140 pre-colors:

- div's and idiv's RDX:RAX;
- mul's RAX and RDX;
- a shift's CL (RCX);
- cqo's RDX;
- push's, pop's, call's, ret's and leave's RSP (and leave's RBP);
- cmpxchg's RAX.

An implicit write of a form narrower than 4 bytes (`div r/m8`'s AX, cwd's DX)
keeps the register's other bits, so it is a read too.

**Flags** come as three masks over CF, PF, AF, ZF, SF, OF and DF:

- read;
- written with a defined value;
- left undefined: written with an unknown value, or maybe kept, and nothing
  after the instruction may rely on it.

A status flag in none of the three is kept: inc and dec keep CF. The masks
follow Intel's and AMD's manuals, and where those differ they take the union:
bt's ZF is kept by Intel and undefined by AMD, so it is undefined here. A shift
or rotate by a count (`BUSTER_X86_EFFECT_COUNT`) is described for any count:
a count of 0 keeps every flag, so its record reads the flags it may keep and
leaves undefined those it may change.

**Memory** (`BUSTER_X86_MEMORY_EFFECT_*`):

- `ADDRESS`: lea computes the address and accesses nothing;
- `ALIGNED`: legacy SSE's 16-byte operands fault unless aligned, so a load folds
  into `xorps` only when aligned;
- `STACK_READ` and `STACK_WRITE`;
- `ATOMIC`: lock prefixes, and xchg with memory;
- `FENCE`: mfence;
- `BIT_STRING`: bt with a register offset reaches past its operand.

**Properties** (`BUSTER_X86_EFFECT_*`):

- `FAULT`: div's divide error, ud2's and int3's traps;
- `ZERO_IDIOM`: xor, sub and their vector kin with reg and rm the same register
  read nothing. This is kept only where the write leaves no old bits, so
  `xor al, al` reads RAX;
- `MXCSR`: reads the rounding, DAZ and FTZ controls;
- `FP_EXCEPTION`: may raise floating-point exceptions;
- `ZERO_SOURCE`: bsf and bsr keep or leave undefined their destination for a
  zero source, so they read it;
- `CONDITIONAL`: cmov and cmpxchg write under a condition (a 32-bit cmov
  zeroes the upper half either way);
- `KEEP_UPPER`, above.

**Control**: none, jump, conditional branch, call, return or trap.

A call has only its architectural effects: RSP, the return address on the
stack, and a call. The callee's clobbers are the calling convention's (#137).
`BUSTER_X86_RESOURCE_CALL` marks where to apply them.

`width` is the operation's operand size: an integer form's, or the destination
register's for movzx, movsx, lea and cvttsd2si; a floating-point form's
element. `operation` (`X86Operation`) groups the forms by what they compute.

### What the operands decide

The record of a form describes any operands. Two functions resolve them for a
record:

- `x86_instruction_flags` refines a shift's flags by its immediate count,
  masked as the processor masks it:
  - with a count of 0, it touches no flag;
  - with another count, it defines CF (and for a shift PF, ZF and SF), defines
    OF only for a count of 1, and leaves AF undefined;
  - an 8- or 16-bit shift by its width or more leaves CF undefined.
- `x86_instructions_effects` gives each record's resources as two 64-bit sets,
  read and written. `BUSTER_X86_RESOURCE_*` lays them out: general registers,
  vector registers, the flags, MXCSR, memory, control and call. This is what
  #172's dependency graph and #140's checks compare, 16 records at a time. The
  operands resolve as follows:
  - a register operand is its register;
  - a memory operand reads its base and index, and lea's do too;
  - a narrower write reads its register;
  - a zero idiom's register isn't read;
  - the stack and fences are memory.

Memory is one resource: alias analysis is the consumer's. A form's memory read
alone doesn't prove that a load may move or fold; that stays #136's and #141's
(single use, no store between, not volatile or atomic, and `ALIGNED`).

## The immediate's contract

A selector asks for a *value*: an integer of the operation's width, any value
in [−2^(8w−1), 2^(8w) − 1], whose pattern (the value modulo 2^(8w)) is what the
operation computes with. At 32 bits, 0xffffffff and −1 are one value. A form's
field holds *bits*, which its kind (`BUSTER_X86_IMMEDIATE_*`) turns into a
value:

| Kind | The field's bits | `x86_immediate_encodes` takes |
| --- | --- | --- |
| `VALUE` | sign-extended to the width | a value whose pattern the extension gives back: an imm8 in a 64-bit form takes [−128, 127], an imm32 there [−2^31, 2^31 − 1], and a field as wide as the form any value of the width |
| `COUNT` | masked to 5 bits, or 6 for a 64-bit operand | a count in [0, 31] or [0, 63]; past that it is refused, not masked |
| `BIT` | masked to the width's bits | a bit index below the width |
| `CONTROL` | an unsigned number (ret's byte count, roundsd's mode, cmpsd's predicate) | the field's range |

`x86_immediate_field` gives the bits for a value it takes, as `X86Instruction`
holds them. `x86_immediate_value` gives back the value a field stands for. For
`VALUE`, `x86_immediate_value(form, x86_immediate_field(form, v)) == v` (modulo
the width) exactly when the form takes `v`. The issue's boundaries:

| Value | 8 bits | 16 bits | 32 bits | 64 bits |
| --- | --- | --- | --- | --- |
| −129 | no value | imm16 | imm32 | imm32 |
| −128 | imm8 | imm8 | imm8 | imm8 |
| 127 | imm8 | imm8 | imm8 | imm8 |
| 128 | imm8 | imm16 | imm32 | imm32 |
| 200 | imm8 | imm16 | imm32 | imm32 |
| 255 | imm8 | imm16 | imm32 | imm32 |
| 256 | no value | imm16 | imm32 | imm32 |
| 0xffffffff | no value | no value | imm8 (it's −1) | none: needs a register |

The assembler's matcher (`x86_match.c`, #222) keeps ranges of its own.
`test-x86-semantics` checks that they agree with this rule on the 112 forms it
admits whose immediate is a value. The one difference: the matcher refuses the
unsigned spelling of a sign-extended pattern that this rule takes. One case is
`add eax, 0xffffffff` with an imm8; the test counts 18 such pairs. Switching
`x86_match.c` to `x86_immediate_encodes` would remove its copy; that is #222's
call, while it is in progress.

## Form choice

A selector asks for a machine operation with an immediate: an operation and a
width (a *row*, `x86_choice_row`), a value, and a 16-bit context:

- the status flags live after the instruction (`BUSTER_X86_CONTEXT_LIVE_FLAGS`;
  all of them is the conservative answer);
- whether the destination is a register (`REGISTER`), and RAX
  (`ACCUMULATOR`);
- whether inc and dec may stand for add and sub (`INC_DEC`).

The rows are the operations a selector gives immediates: the arithmetic group,
test, mov, three-operand imul, the shifts and rotates, and push, each at 1, 2,
4 and 8 bytes where it has them. That makes 64 rows of at most 8 candidates,
written as a C table (`x86_choice_sources`). The chosen form is the first
candidate whose conditions the request meets; none (`X86_FORM_NONE`) means the
value needs a register, or isn't a value of the width.

- **Encodings of the same instruction** come shortest first:
  - the sign-extended imm8;
  - the accumulator's form (a byte shorter than an imm32's);
  - the imm32 (at 64 bits only a sign-extended one);
  - a shift by 1 without its immediate.
- **Constants:**
  - `xor r32, r32` for 0 when no flag is live;
  - `mov r32, imm32` for a 64-bit value below 2^32 (the 32-bit write
    zero-extends);
  - the sign-extended `mov r/m64, imm32`;
  - `movabs`.

  Into memory, only the imm forms, and past an imm32 the value needs a
  register.
- **Other instructions, where the live flags allow:**
  - `test r, r` for `cmp r, 0` (AF dead: cmp's AF is 0, test's undefined);
  - a test narrowed on a register: to its low byte for a value in [0, 127],
    and at 64 bits to its low 32 bits for one in [0, 2^31) (identical flags),
    or with SF dead [0, 2^32). Not to 16 bits, where 66 with an imm16 stalls
    Intel's decoders, and not in memory, where the narrower access is another
    access;
  - inc and dec for add and sub of 1 and −1, which need CF dead, and for −1 AF
    too.

  inc and dec are smaller and run as fast on the 9700X (below), but whether
  they are faster is the target's (#181), so the caller turns them on.

A substitution's conditions aren't written by hand. Each candidate of another
instruction names the status flags it defines as the operation would (inc and
`add 1`: all but CF). Initialization then asks `x86_flags_substitutable`, for
each status flag live alone, whether the candidate can stand for the row's own
form with that flag live, using the two forms' effects. Every live flag must
be one both keep, one both define and the pair defines alike, or one the
original leaves undefined. So the choice reads the effects table, rather than
keeping a second copy of the flags. A candidate that claims too much is still
caught: inc's CF is undefined to the table, so naming CF among its alike flags
changes nothing.

`x86_choice_instruction` builds the record of a chosen form from a destination
operand and the value:

- the accumulator's forms and push's take no rm;
- a zero idiom or `test r, r` takes the destination twice;
- the immediate is the field's bits.

Expression selection stays with its owners: lea for adds and multiplications,
covering trees, and folding loads are #136's and #141's patterns. This file
gives their constraints.

### ISA and costs

Legality and profitability are separate. Every form in the rows is baseline
x86-64. The effects cover popcnt, lzcnt, tzcnt and roundsd too, since
describing a form doesn't permit emitting it. Which forms a target may use is
#180's target policy, and which sequence is faster is #181's cost model; both
key their tables by the same `X86Form`.

## Data-parallel design

**Choice.** `x86_choose_forms` takes columns (rows, values, contexts), 32
requests at a time:

- The rows' info and the 8 candidates' forms and conditions are tables of 64
  words, looked up in registers (`vpermi2w` over two ZMM registers each).
- The values are classified 8 per ZMM register in 64-bit lanes
  (`x86_choice_classes`): the pattern at the width, its sign extension from 8
  and 32 bits, the width's range by a bias and an unsigned compare, zero, one
  and minus one. The classes are then narrowed to words (`vpmovqw`).
- The context's live flags become the dead-flag bits.
- Each candidate in turn takes the requests it fits that no earlier one took.

The scalar twin, `x86_choose_forms_scalar`, classifies one request at a time
and walks its row's candidates. The two must give the same forms for any
input, every bit of the context too.

**Effects.** `x86_instructions_effects` works 8 records at a time, one per
64-bit lane:

- the records' halves are transposed by two `vpermt2q`;
- each lane's effects record comes from a scalar lookup of its index and a
  16-byte load into its part of a ZMM register;
- each lane computes what `x86_instruction_effects_scalar` does, with masks for
  its branches.

Gathers would cost more here: a `vpgatherdd` of 16 is 64 µops on Zen 5
(`docs/coding-style.md`). The records and outputs are read and written with
masked loads and stores, so nothing past either array is touched.

## Tests

`test-x86-semantics` takes about 0.2 s on the 9700X. Its oracle for the tables
is the processor. A harness runs records in the test's own process:

- `x86_semantics_test_enter` loads all 16 general registers, the flags and ZMM0
  to ZMM15 from a state, runs the code, and stores them back;
- memory operands are RIP-relative into a data page, and RSP points into a
  stack of the harness's;
- a fault names the record.

**The effects table.** Every covered form but those that branch or trap runs
12 times (`--runs`), its operands random registers and memory in turn. Each
run checks that:

- what the form doesn't write stays as it was: registers, vector registers to
  bit 511, kept flags, and the bytes of memory around its operand and below the
  stack;
- a narrower write keeps or zeroes the rest as its record says, and legacy SSE
  keeps the upper lanes;
- defined flags and results don't change when everything the form doesn't read
  changes;
- a destination that only merges doesn't read the bytes it defines;
- every write changes something in some run.

**The fixtures:**

- the effects the issue names: division, variable shifts, partial writes,
  32-bit zero extension, inc's CF, the SSE merges, a call's architectural
  effects;
- resolved register and memory operands, zero idioms and shift counts;
- the substitution matrix of every live-flag set;
- a sample of uncovered forms, each of which must get the conservative record.

cmov and setcc run on all 64 combinations of the status flags, against a model
of the conditions in C, at each width.

**The immediate's contract**, on all 189 covered forms with an immediate,
against about 60 boundary values: a field that takes a value gives it back,
and one that doesn't can't.

**Form choice:**

- fixtures of the boundaries above, constant materialization (INT32_MAX,
  0x80000000, UINT32_MAX, negative 64-bit values), shift counts and the
  substitutions;
- every row with every boundary value and 24 random ones, six live-flag
  contexts and three destinations: 93,312 requests. Each chosen record is
  validated, then run against a reference sequence: the value into R11 by
  movabs (whose field is the value itself), then the operation's register form.
  The two must leave the same registers, memory and live flags. The check is
  sensitive: a record with an imm8 of `0xc8` standing for 200 must fail it;
- every candidate of every row must be chosen by some request.

**The twins:**

- both vector paths against their scalar twins, on 20,000 random requests and
  records, the context's unused bits and uncovered forms among them;
- with every column and output ending at a `PROT_NONE` page, at every count to
  100.

**With `--cross-check`:** each chosen record, printed by the encoder test's
printer, must assemble with GNU as to the chosen form's bytes, 18,603 of them.
So GNU as picks the same encoding for the instruction the chooser picked.

Mutations of the tables, each built and run once, are caught. Among them:

- inc writing CF;
- adc not reading CF;
- div not writing RDX;
- a 32-bit write merging;
- addsd untied;
- an 8-bit xor as a zero idiom;
- cqo not reading RAX;
- ucomisd keeping OF;
- the imm8 taking any value;
- and each of the vector paths' masks.

## The covered subset

612 forms:

- **The integer ALU:** add, or, adc, sbb, and, sub, xor and cmp (every form,
  with their locked forms on memory); test; not, neg, inc and dec (and their
  locked forms); mul, imul (one, two and three operands), div and idiv.
- **Moves and extensions:** mov (but the moffs forms), movzx, movsx and
  movsxd, lea, xchg; cbw, cwde and cdqe, cwd, cdq and cqo.
- **Shifts and rotates:** rol, ror, shl, shr and sar, by 1, CL and an imm8.
- **Control and the stack:** push and pop (r64, r/m64, imm8 and imm32), call
  (rel32, r/m64), jmp (rel8, rel32, r/m64), ret (and imm16), jcc (rel8,
  rel32), setcc and cmovcc of all 16 conditions, leave, nop (and the
  multi-byte ones), int3, ud2, endbr64, mfence.
- **Bits:** bswap, bt (register and imm8), bsf, bsr, tzcnt, lzcnt and popcnt.
- **Atomics:** lock cmpxchg and lock xadd.
- **SSE and SSE2:**
  - the moves: movss and movsd, movaps, movups, movapd, movupd, movdqa and
    movdqu, movd and movq;
  - scalar arithmetic: add, sub, mul, div, min, max and sqrt, in ss and sd;
  - ucomiss, ucomisd, comiss, comisd, cmpss and cmpsd;
  - the conversions: cvtsi2ss and cvtsi2sd, cvttss2si and cvttsd2si,
    cvtss2sd and cvtsd2ss;
  - roundss and roundsd (SSE4.1);
  - the bitwise operations: xorps, xorpd, andps, andpd, andnps, andnpd, orps,
    orpd, pxor, pand, pandn and por.

VEX and EVEX forms are uncovered for now (#180's levels will bring them, with
their merges from vvvv and their zeroed upper lanes), and so are x87, string
instructions and the moffs forms.

## Adding a form

1. Add its run to `BUSTER_X86_EFFECT_ROWS` in `x86_semantics.c` (consecutive
   `X86Form`s that share their meaning in one row), giving:
   - its operation, its pattern and, for a vector destination, its write;
   - the flags read, written and undefined, by the manuals, the union where
     they differ;
   - its implicit registers, memory, immediate kind, control and properties.

   The form table supplies the rest: the operands' roles and classes, a general
   register's rest, the width. Initialization refuses a row that doesn't fit
   its form, and rows that overlap.
2. If a fixture of the issue's kind fits (a fixed register, a partial write, a
   flag), add one to `x86_semantics_fixtures`.
3. Run `test-x86-semantics`: it executes the form from random states. A form
   that branches or traps isn't executed; add a fixture of its effects instead,
   as call's and ret's are. A form that needs operands the harness doesn't
   generate gets them where `x86_semantics_check_run` makes division and bt
   fault-free.
4. If a selector gives it immediates, add its row and candidates to
   `x86_choice_sources`, shortest first. A candidate of another instruction
   names the flags it defines alike. The test then runs every candidate against
   its reference, and `--cross-check` against GNU as.

## Measurements

On the 9700X (CPU 2, under the lock), with `benchmark-x86-semantics`'s
defaults: 65,536 requests, and 2,048 functions of 96 records shaped like
clang's code.

**Tables:**

- the records take 9,808 bytes (613 of 16);
- the index takes 16,704 bytes (a u16 per form, of 8,352), of which the
  compiler touches the lines of the forms it emits;
- the choice's tables take 2,176 bytes, and the scalar twin's rows 2,304 more;
- `x86_semantics_initialize` takes 5.9 µs, 33,000 cycles.

**Choice and effects**, per request or record, both states:

| Path | Interleaved | Back to back (`--only`) | After 200 ms of the scalar twin |
| --- | --- | --- | --- |
| choose, scalar | 48.7 cycles, 8.84 ns | 48.9 cycles | 48.5 cycles |
| choose, AVX-512 | 3.32 cycles, 0.60 ns | 3.33 cycles, 0.60 ns | 3.44 cycles, 0.62 ns |
| effects (-O0's shape), scalar | 48.9 cycles, 8.82 ns | 48.6 cycles | 48.5 cycles |
| effects (-O0's shape), AVX-512 | 5.74 cycles, 1.04 ns | 5.74 cycles, 1.04 ns | 5.74 cycles, 1.04 ns |
| x86_encode of the same records | 3.21 cycles, 0.59 ns | 3.12 cycles, 0.58 ns | 3.23 cycles |

Choice is 15 times the scalar twin's speed and effects 8.5 times; -O2's shape
measures the same. Both loops are light enough that the power cap hardly takes
them: choice is 3% slower after the scalar twin, effects unchanged (#199's
method).

The chosen forms take 4.74 bytes per instruction on the request stream,
against 6.39 for a selector that always used the imm32. 1.8% of the requests
need a register.

**The choices that change the instruction sequence or its encoding** run in
blocks of 64 over 8 registers:

| Instruction | Cycles per instruction | Bytes |
| --- | --- | --- |
| `add r64, imm8` (chosen for 5) | 0.193 | 4 |
| `add r64, imm32` (the same 5) | 0.193 | 7 |
| `add r64, 1` | 0.193 | 4 |
| `inc r64` (for add 1, CF dead) | 0.193 | 3 |
| `mov r32, 0` | 0.170 | 5.6 |
| `xor r32, r32` (for mov 0, flags dead) | 0.142 | 2.6 |
| `cmp r64, 0` | 0.173 | 4 |
| `test r64, r64` (for cmp 0, AF dead) | 0.170 | 3 |
| `test r64, imm32` (100) | 0.170 | 7 |
| `test r8, imm8` (for it) | 0.173 | 3.9 |
| `movabs r64, imm64` (100) | 0.339 | 10 |
| `mov r32, imm32` (for it) | 0.170 | 5.6 |

The smaller encodings cost nothing, and the zero idiom and `mov r32` over
movabs are faster too. Whole-program run time waits for the code generators
(#136, #141) and #142's programs.

## Next steps

- **#136 and #141:** use the rows and the effects, and add the forms they emit
  to the subset as they grow, with the selector integration tests the issue
  lists: single-use and multi-use loads, interfering writes, volatile and
  atomic accesses.
- **#180:** apply its levels' check to the chosen records, and cover the VEX
  and EVEX forms it allows.
- **#181:** key its latencies by the same forms, and decide inc and dec.
- **#222:** may switch `x86_match.c`'s immediate ranges to the shared rule.

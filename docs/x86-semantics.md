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
paths and their scalar twins, the consumers, the tests (whose oracle is the
processor), the covered subset and how to add a form, and the 9700X's
measurements.

## What is where

- `x86_semantics.c`: the effects (`X86Effects`, `BUSTER_X86_EFFECT_ROWS`,
  `x86_effects`), the immediate's contract (`x86_immediate_encodes`,
  `x86_immediate_field`, `x86_immediate_value`), the flags
  (`x86_instruction_flags`, `x86_flags_substitutable` and
  `x86_instruction_flags_substitutable`), operand-resolved effects
  (`x86_instructions_effects` and its twin `x86_instructions_effects_scalar`),
  and form choice (`x86_choice_row`, `x86_choose_forms` and its twin
  `x86_choose_forms_scalar`, `x86_choice_instruction`).
  `x86_semantics_initialize` builds the tables: each covered form's record from
  its row and its form's syntax and operands, the part its operands decide (its
  shape) made once for each distinct one and kept by a hash of its inputs, which
  the 3,148 forms share 328 of (#760).
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
(`BUSTER_X86_EFFECT_KEEP_UPPER`). A write of an XMM register is then a partial
write of its ZMM register, which the resolved effects below count as a read. A
VEX or EVEX write zeroes the register past the bytes it defines (`ZERO`, to bit
511), or defines all of it (`WHOLE`, a ZMM register's 64 bytes), so it reads
nothing of the destination.

An x87 register operand (ST(i)) is no role: it is part of the x87's state,
which the `X87` property below says the form reads and writes. An x87 form's
memory operand is a role as any other's.

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
leaves undefined those it may change. Those are not flags nothing may rely on:
with a count of 0 they are kept, which `x86_flags_substitutable` respects.

**Memory** (`BUSTER_X86_MEMORY_EFFECT_*`):

- `ADDRESS`: lea computes the address and accesses nothing;
- `ALIGNED`: legacy SSE's 16-byte operands fault unless aligned, so a load folds
  into `xorps` only when aligned;
- `STACK_READ` and `STACK_WRITE` (push's and call's 8 bytes below RSP, pushw's
  2);
- `ATOMIC`: lock prefixes, and xchg with memory;
- `FENCE`: mfence;
- `BIT_STRING`: bt with a register offset reaches past its operand.

**Properties** (`BUSTER_X86_EFFECT_*`):

- `FAULT`: div's divide error, ud2's and int3's traps;
- `ZERO_IDIOM`: xor, sub and their vector kin with their two sources the same
  register read nothing of what they write. The sources are reg and rm, or for
  a VEX or EVEX form vvvv and rm (`vpxord xmm1, xmm2, xmm2` writes xmm1 and reads
  nothing; `vpxord xmm2, xmm1, xmm2` is no idiom). A general register's is kept
  only where the write leaves no old bits, so `xor al, al` reads RAX; a legacy
  SSE one's (`pxor xmm3, xmm3`) keeps its register's bits above 127, so its
  resolved effects still read the register;
- `MXCSR`: reads the rounding, DAZ and FTZ controls;
- `FP_EXCEPTION`: may raise floating-point exceptions (MXCSR's, or an x87
  form's in its status word);
- `ZERO_SOURCE`: bsf and bsr keep or leave undefined their destination for a
  zero source, so they read it;
- `CONDITIONAL`: cmov and cmpxchg write under a condition (a 32-bit cmov
  zeroes the upper half either way);
- `X87`: an x87 form reads and writes the x87's state (its registers, the
  stack's top and tags, and its status word, whose condition codes every x87
  instruction may change) and reads its control word (precision and rounding);
  `X87_CONTROL`: fldcw writes the control word too;
- `ZERO_UPPER`: vzeroupper zeroes the bits above 127 of ZMM0 to ZMM15 and keeps
  their low 128, so it reads and writes all 16 (ZMM16 to ZMM31 keep theirs);
- `IMPLICIT_VECTORS`: Key Locker's forms read and write XMM registers that no
  role names, whose sets `x86_implicit_vectors` gives (LLVM's `X86InstrKL.td`
  has the same): `aesencwide128kl` to `aesdecwide256kl` read and write XMM0 to
  XMM7, their 8 blocks; `encodekey128` reads its key from XMM0 and writes its
  handle to XMM0 to XMM2 and zeros to XMM4 to XMM6, `encodekey256` its key from
  XMM0 and XMM1 and its handle to XMM0 to XMM3; `loadiwkey` reads XMM0;
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
  - an 8- or 16-bit SHL or SHR by its width or more leaves CF undefined.
    That's the SDM's exception, and it isn't SAR's: SAR's CF is the last bit
    shifted out, by then the sign bit (`sar bx, 16` of 0x8000 gives 0xffff and
    CF 1).
- `x86_instructions_effects` gives each record's resources as two 64-bit sets,
  read and written. `BUSTER_X86_RESOURCE_*` lays them out: general registers,
  vector registers, the flags, MXCSR, memory, control and call, and the x87's
  state and its control word. This is what #172's dependency graph and #140's
  checks compare, 16 records at a time. The operands resolve as follows:
  - a register operand is its register: a vector one the whole ZMM register,
    its XMM and YMM parts included;
  - a memory operand reads its base and index, and lea's do too;
  - a narrower write reads its register, and so does a legacy SSE write: it
    keeps its register's bits 511 to 128, which stay live through it;
  - a zero idiom's register isn't read, but a legacy SSE one's is, for those
    upper bits (its low 128 bits still depend on nothing, which
    `BUSTER_X86_EFFECT_ZERO_IDIOM` says);
  - the stack and fences are memory;
  - an x87 form reads the x87's state and control word and writes the state,
    fldcw the control word too; vzeroupper reads and writes ZMM0 to ZMM15;
  - an /is4 operand (`vpblendvb`'s, in the immediate's bits 7 to 4) is read; a
    VSIB operand's index is a vector register; a VEX gather reads and clears
    its mask, vvvv's register, and keeps the destination's elements the mask
    leaves (#760);
  - a record that touches an opmask register reads and writes everything, as
    an uncovered form does: one with an opmask (an EVEX form's: `x86_validate`
    refuses one in any other's), and a form with an opmask register among its
    operands (kmov, kand, a compare into an opmask).
- `x86_instructions_effects_opmasks` and its twin
  `x86_instructions_effects_opmasks_scalar` (#760) give the same sets with
  those records resolved too, and each record's opmask registers in a 16-bit
  column: bits 0 to 7 the ones it reads (K0 to K7), bits 8 to 15 the ones it
  writes. The sets have no room for them: 61 of their 64 bits are taken.
  - an opmask role's register is in the column, not the sets
    (`BUSTER_X86_EFFECT_OPMASK_REG`, `_RM` and `_VVVV`, which the
    initialization gives the roles of the form's opmask operands);
    vp2intersect writes its even register and the next;
  - the record's opmask is read; a vector destination of it merges into its
    old elements, so it reads the register too, unless it zeroes them
    (`BUSTER_X86_DECORATION_ZEROING`); a gather or a scatter clears its
    opmask, so writes it too;
  - an uncovered form's column is 0xffff, every opmask register read and
    written.

  A consumer that may reorder records with opmask registers takes the column;
  one that doesn't (#172's scheduler of Tier 1's records, the unwind analyzer)
  keeps `x86_instructions_effects`, whose answer for them stays everything.

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

The assembler's matcher (`x86_match.c`, #222) takes a value's immediate by this
rule (`x86_match_immediate` calls `x86_immediate_encodes`, once the operand is a
value of the operation's width) and writes the field's bits
(`x86_immediate_field`). So the unsigned spelling of a sign-extended pattern
takes the shorter field, as GNU as gives it: `add eax, 0xffffffff` is `83 c0
ff`, `pushw 0xffc8` is `66 6a c8`. It used ranges of its own until #221's
follow-up, which refused 18 such pairs. Its vector path keeps the rule as two
intervals per form (`x86_match_range`): the field's signed range, and the
unsigned spellings of its negative values at the width. Any other immediate (a
count, a control byte) takes the field's bits, signed or unsigned, as GNU as
takes them. `test-x86-semantics` checks the scalar rule and the intervals against
`x86_immediate_encodes` on the 114 forms the matcher supports with a value
immediate, at every boundary value and at `__int128`s past 64 bits, and that
every form the matcher reads as sign-extending is such a form; `test-assembler`
compares the two paths, and with `--cross-check` GNU as's bytes on the spellings.

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
  they cost more is the target's, so the caller turns them on: Tier 0 asks
  for them where the Zen 5 model's observations of them cost no more than add's
  and sub's with an imm8 (#181, `tier0_inc_dec`; `x86-zen5.md`'s "inc and dec
  against add and sub", before a conditional branch too), which they don't.

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

Both substitution helpers require covered effects for both forms whenever a
status flag is live. An uncovered form's conservative may-clobber mask proves
neither undefined outputs nor equivalent flags, even when the caller names
all flags in `same`. For example, RORX keeps CF whereas ROR changes it; the
uncovered RORX record cannot authorize that replacement with CF live.

A counted shift's flags are the exception: its record leaves undefined the
flags it may change, but a count of 0 keeps them. So for a counted original
those flags are counted, not free, and only a substitute counted alike stands
for them: another encoding of the same shift. `or r/m32, imm8` doesn't stand
for `shl r/m32, imm8` with CF live, since `shl ebx, 0` keeps CF and
`or ebx, 0` clears it. `x86_instruction_flags_substitutable` answers the same
for two records, resolving an immediate count first
(`x86_instruction_flags`): `shl r/m32, 1` stands for `shl r/m32, imm8` with a
count of 1 (or 33) whatever is live. That's how initialization takes a shift
row's candidate for the value 1; a count in CL stays counted.

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
x86-64: `test-x86-semantics` checks each against #180's baseline
(`x86_target_forms` of a zero `X86Target`), and each chosen record with
`x86_target_validate`. The effects cover popcnt, lzcnt, tzcnt and roundsd too,
since describing a form doesn't permit emitting it. Which forms a target may
use is #180's target policy (`x86_target.c`), and which sequence is faster is
#181's cost model; both key their tables by the same `X86Form`.

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
- each lane's effects record comes from a scalar lookup by its form's number
  (the fixup flag masked off) in `x86_effects_by_form`, the forms' index three
  times over (the forms', their FS copies' and their GS copies'), and a 16-byte
  load into its part of a ZMM register. `x86_semantics_row`'s arithmetic for
  the segment overrides, a compare and a subtraction per lane, had taken a
  quarter of the loop's instructions (below);
- each lane computes what `x86_instruction_effects_scalar` does, with masks for
  its branches;
- a legacy SSE form's read of the vector registers it writes is `write &
  vectors` in the lanes of `BUSTER_X86_EFFECT_KEEP_UPPER`, the record's top bit,
  which one arithmetic shift (`vpsraq`) spreads over the lane, and it joins the
  reads at the end, with an uncovered form's everything. The loop has many
  mask registers in flight already: a `vptestmq` for the property, or a join
  inside the chain of masked ORs that builds the read set, measured slower
  (below);
- vvvv is a lane's third role, as reg and rm are: the X86 rows' three-operand
  VEX and EVEX forms (#760) are most of vector code's records, which the scalar
  twin would take one at a time;
- a record that touches an opmask register is an uncovered one's lane
  (`vptestnmq` of the records' last bytes and of the opmask roles' properties,
  joined with the covered forms' mask), but in
  `x86_instructions_effects_opmasks`, whose lanes compute the opmask column and
  the merging destinations' reads. The two entry points inline one body
  (`x86_instructions_effects_lanes`, `always_inline`) with the column's pointer
  null and not, so that the first has none of the column's instructions;
- the rare records are the scalar twin's: an x87 form's, vzeroupper's,
  `BUSTER_X86_EFFECT_RARE`'s (an /is4 operand, a VSIB one's vector index, and
  vp2intersect's pair of opmask registers) and `IMPLICIT_VECTORS`' (Key
  Locker's XMM registers). A masked `vpternlogq` ORs the effects
  records' properties into a register, one `vptestmq` of it after the loop says
  whether any lane was rare, and then `x86_instruction_resolve` overwrites the
  rare lanes after the stores. A count in a general register cost more
  (below): the lanes' lookups take every general register the loop has.
  Code generators emit them rarely (Tier 0's long double code, its vector
  constants' zeroing, a function's vzeroupper, a gather), so the common lane
  pays the test alone, rather than the x87's resources in every lane. An EVEX
  form isn't one: clang -O2's AVX-512 code has many (`vmovdqu64`), and sending
  them there cost the loop 20% (below).

Gathers would cost more here: a `vpgatherdd` of 16 is 64 µops on Zen 5
(`docs/coding-style.md`). The records and outputs are read and written with
masked loads and stores, so nothing past either array is touched.

## The consumers

Each consumer reads the tables rather than keeping a copy of their rules:

- **Tier 0** (#136, `tier0.c`) marks each template record with an immediate a
  request and chooses all of a function's at once (`x86_choose_forms`, or its
  twin on the scalar path), with the flags its template reads live. The
  records its emitter writes with a computed constant (the frame's reservation
  and release, its realignment, `ALLOCA`'s rounding, a vector constant's words)
  take their imm8 or imm32 form by `x86_immediate_encodes`. Every record it
  emits has a covered form, an X86 entry's too: `test-tier0` checks it on every
  program it compiles (the x87's, the 128-bit integers', the vectors' and the
  deep values' too). `tier0_operation_form_valid` asks the tables which VEX and
  EVEX forms a row may have (#760): a covered one whose effects name no register
  its syntax doesn't, neither implicit registers (mulx's rdx) nor vzeroupper's
  every vector register; an uncovered form's conservative record has both
  (vzeroall, vmaskmovdqu, the string compares). `tier0_initialize` builds the
  tables where they aren't built, so that a caller of Tier 0 alone (a test of
  its rows) reads them too. The legacy forms the x86
  builtins take (`tier0_operation_legacy_forms`, #148) stay a list of their own:
  most aren't covered (the shadow stack's, RDRAND, the FS and GS bases' writes,
  WBINVD, INVPCID), and the harness can't run many of them.
- **Tier 1's selector** (#141, `tier1_select.c`) chooses its immediates' forms
  by the rows, a function's requests 32 at a time through `x86_choose_forms`
  (on the scalar path each as it comes, by the twin), and every record it emits
  has a covered form, which its tests check; the scheduler (#172,
  `tier1_schedule.c`) builds its dependency graph from the resolved effects.
- **The unwind tables' analyzer** (`unwind_tier0.c`) refuses a function with an
  uncovered form, which every Tier 0 function with long doubles, 128-bit
  integers or vectors had; their forms are covered now, so only the analyzer's
  own frame rules decide. (The C driver reads the frame Tier 0 publishes
  instead, `unwind_tier0_frame`.)
- **The assembler's matcher** (#222, `x86_match.c`) takes value immediates by
  the shared rule (above), and so does C's inline assembly, which matches its
  templates' instructions through it.

**Where Tier 0 folds loads.** It folds a local's home (a register or a frame
slot) into the entry that uses it, and `x = x op v` into one read-modify-write;
it folds no load through a pointer, and every fold takes entries next to each
other. test-checker's folds section (`c_check_program_folds_test.c`) checks
this through the C driver's lowering, from Tier 0's records and their resolved
effects: a volatile object read as often as the source reads it (twice for
`g + g`, once for a value used twice), a volatile increment one read and one
write, a load through a pointer kept on its side of a store through it, and an
atomic increment written by a locked form in its helper. A program whose locals'
homes are read and written through pointers to them between their uses runs
natively against clang. Folding loads through pointers is Tier 1's (#141): its
selector folds a load into its only consumer in its block past nodes that
neither write memory, call nor access a volatile object, and past none that do
(`docs/tier1-selection.md`).

## Tests

`test-x86-semantics` takes about 0.3 s on the 9700X. Its oracle for the tables
is the processor. A harness runs records in the test's own process:

- `x86_semantics_test_enter` saves the C code's x87 and SSE state (FXSAVE64),
  loads the state's x87 state (FXRSTOR64: the control and status words, the tags
  and the registers, with every exception masked), then all 16 general
  registers, the flags and ZMM0 to ZMM15, runs the code, and stores them back,
  the x87's state last, before restoring the C code's;
- memory operands are RIP-relative into a data page, and RSP points into a
  stack of the harness's;
- a fault names the record.

**The effects table.** Every covered form but those that branch or trap runs
12 times (`--runs`), its operands random registers and memory in turn, where
#180's table lets the host's target run it (`x86_target_native`): a form the
table leaves unclassified (`endbr64`) or whose features the host lacks is
listed instead. Each run checks that:

- what the form doesn't write stays as it was: registers, vector registers to
  bit 511, kept flags, the x87's state and its control word, and the bytes of
  memory around its operand and below the stack;
- a narrower write keeps or zeroes the rest as its record says, legacy SSE
  keeps the upper lanes, a VEX or EVEX write zeroes them to bit 511, and
  vzeroupper zeroes ZMM0 to ZMM15's above bit 127 and keeps the rest;
- defined flags and results don't change when everything the form doesn't read
  changes, every register and vector register compared whole, the x87's state
  among them (a random one, of mostly normal numbers near 1, a random precision,
  rounding, stack top and tags);
- a destination that only merges doesn't read the bytes it defines;
- every write shows a change in some run. A run can't always show one: a byte
  rotated by 8, a value that is its own result, a condition that didn't hold.
  So a write no run showed gets up to 256 more runs, with register operands,
  until it does, and one that never does is listed as missing coverage, not
  failed. A write the table lacks still fails, as a register changed that the
  form doesn't write.

**The fixtures:**

- the effects the issue names: division, variable shifts, partial writes,
  32-bit zero extension, inc's CF, the SSE merges, a call's architectural
  effects;
- resolved register and memory operands, zero idioms and shift counts;
- the substitution matrix of every live-flag set;
- a sample of uncovered forms, each of which must get the conservative record;
- resolved x87 records (their memory, the x87's state and control word, fucomip's
  flags, fcmovbe's), vzeroupper's, `vpxord`'s idiom of its sources and the same
  record with reg and rm alike, a masked EVEX record, AVX moves, `shld` by CL and
  `shrd` by an immediate count of 32 (masked to 0), stmxcsr and pushw.

cmov and setcc run on all 64 combinations of the status flags, against a model
of the conditions in C, at each width.

Shifts' CF runs against a model too: SAR, SHR and SHL at every width, by an
immediate and by CL, at counts around their widths and the masks', from
values with the sign bit clear and set and with CF clear and set. SAR's CF must
be the last bit shifted out (the sign bit by the width or more), and SHR's and
SHL's below the width, and `x86_instruction_flags` must define CF exactly where
the model does. Then pairs of records go through
`x86_instruction_flags_substitutable` for every set of live flags and run from
every state of the status flags. Where it takes a live flag the original doesn't
leave undefined, the two must leave that flag alike. `shl ebx, 0` and
`or ebx, 0`, which it refuses with CF live, must show CF apart in some run.
Uncovered forms fail closed in both helper APIs for every live-flag set, in
both substitution directions and against themselves. A deterministic RORX
and ROR witness rotates EAX = 2 to 1 after setting CF: RORX keeps it set, and
ROR clears it, so the helpers must refuse that substitution with CF live.

**The immediate's contract**, on all 195 covered forms with an immediate,
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

**#180's target:** every candidate of every row must be allowed at the baseline
target, and each chosen record must pass `x86_target_validate` there. The test
lists the covered forms #180's table doesn't allow at the baseline, 29 of them,
all forms that need a feature: popcnt, lzcnt, tzcnt, roundss and roundsd, the AVX
and AVX-512 moves, vpxord and vzeroupper, and endbr64. The forms a host can't
run (its native target lacks their features) are listed too, and not executed.

**The twins:**

- both vector paths against their scalar twins, on 20,000 random requests and
  records, the context's unused bits and uncovered forms among them;
- with every column and output ending at a `PROT_NONE` page, at every count to
  100.

**With `--cross-check`:** each chosen record, printed by the encoder test's
printer, must assemble with GNU as to the chosen form's bytes, 18,603 of them.
So GNU as picks the same encoding for the instruction the chooser picked.

**`x86_match.c`:** its scalar rule and its vector path's intervals against
`x86_immediate_encodes` (above), exactly.

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

3,148 forms:

- **The integer ALU:** add, or, adc, sbb, and, sub, xor and cmp (every form,
  with their locked forms on memory); test; not, neg, inc and dec (and their
  locked forms); mul, imul (one, two and three operands), div and idiv.
- **Moves and extensions:** mov (but the moffs forms), movzx, movsx and
  movsxd, lea, xchg; cbw, cwde and cdqe, cwd, cdq and cqo.
- **Shifts and rotates:** rol, ror, shl, shr and sar, by 1, CL and an imm8.
- **Control and the stack:** push and pop (r64, r/m64, imm8 and imm32), pushw
  (imm8 and imm16), call
  (rel32, r/m64), jmp (rel8, rel32, r/m64), ret (and imm16), jcc (rel8,
  rel32), setcc and cmovcc of all 16 conditions, leave, nop (and the
  multi-byte ones), int3, ud2, endbr64, mfence.
- **Bits:** bswap, bt (register and imm8), bsf, bsr, tzcnt, lzcnt and popcnt;
  shld and shrd at 32 and 64 bits, by CL and by an imm8 (at 16 bits a count past
  the width leaves the result undefined, which the record can't say).
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
    orpd, pxor, pand, pandn and por;
  - stmxcsr. It stores all of MXCSR, its exception flags too, which the
    `FP_EXCEPTION` forms before it may set: the resolved effects have no
    resource for those flags, so a consumer that moves stmxcsr keeps its order
    against those forms.
- **The x87**, Tier 0's long double: fld, fild and the constants (fld1 to
  fldz); fst, fstp, fist and fistp; fadd, fsub, fsubr, fmul, fdiv and fdivr in
  each of their five shapes; fchs and fabs; fcmovcc of the 8 conditions; fcomi,
  fcomip, fucomi and fucomip; fldcw and fnstcw.
- **VEX and EVEX**, the forms Tier 0's own vector code emits: vmovdqu (128 and
  256 bits), vmovdqu64 (128, 256 and 512), EVEX's vmovd and vmovq (between an
  XMM register and a general one or memory, the narrow vectors' loads and
  stores), vpxord (each width, and its broadcasts) and vzeroupper.
- **The front end's X86 rows** (#760: every VEX and EVEX form the rows of
  `c_check_program_x86.c`, `c_check_program_vectors.c`,
  `c_check_program_vector_builtins.c` and the lowering's own
  (`c_check_program_lower.c`) may have, 1,321 of their 1,404 forms, and
  test-tier0's vector check's), with their families' other widths, broadcasts and rounding forms,
  VEX and EVEX:
  - the moves: vmovups, vmovupd, vmovaps, vmovapd, vmovdqa32, vmovdqa64,
    vmovdqu8, vmovdqu16 and vmovdqu32, vlddqu, vmovss, vmovsd and vmovsh,
    vmovd, vmovq, vmovshdup, the non-temporal moves (vmovntps, vmovntpd,
    vmovntdq, vmovntdqa), and VEX's masked loads and stores (vmaskmovps,
    vmaskmovpd, vpmaskmovd, vpmaskmovq);
  - floating arithmetic in ps, pd and ph and their scalars (ss, sd, sh): vadd,
    vsub, vmul, vdiv, vmin, vmax and vsqrt; vaddsub, vhadd and vhsub; the fused
    multiply-adds (vfmadd213, vfmadd231, vfmsub231, vfmaddsub213,
    vfmaddsub231, vfmsubadd231) and AVX512-FP16's complex multiplies (vfmulc,
    vfcmulc, vfmaddc, vfcmaddc); the approximations (vrcp, vrsqrt, their 14-bit
    and half-precision forms); vgetexp, vscalef and vrndscale; AVX512BF16's
    vdpbf16ps;
  - the conversions: between floating formats (vcvtss2sd, vcvtsd2ss,
    vcvtpd2ps, the half-precision ones, F16C's vcvtph2ps and vcvtps2ph,
    vcvtps2pd, AVX512BF16's vcvtneps2bf16 and vcvtne2ps2bf16), to integers
    (vcvt and vcvtt from ps, pd, ph, ss, sd and sh to dq, udq, qq, uqq, w, uw,
    si and usi) and from them (dq, udq, qq and uqq to ps, pd and ph, and w and
    uw to ph);
  - the compares: vcmp (VEX's into a vector, EVEX's into an opmask), vcomis
    and vucomis into the flags, vpcmp and vpcmpu in b, w, d and q, vpcmpeqb,
    vptestm and vptestnm, vp2intersectd and vp2intersectq; the tests into the
    flags: vptest, vtestps, vtestpd, kortest and ktest;
  - integer arithmetic: vpadd and vpsub in b, w, d and q and their
    saturating forms (vpadds, vpaddus, vpsubs, vpsubus in b and w), vpmin and
    vpmax (signed and unsigned, b, w, d and q), vpabs, vpavgb and vpavgw,
    vpmaddwd, vpmaddubsw, vpmullw, vpmulld, vpmullq, vpmulhw, vpmulhuw,
    vpmulhrsw, vpmuldq, vpmuludq, vpsadbw, the horizontal vphadd and vphsub
    (w, d, sw), vpsign, vphminposuw, vpconflictd, vpconflictq, vplzcntd,
    vplzcntq, vpopcnt (b, w, d, q), vpmultishiftqb,
    and the accumulating dot products (AVX512VNNI's and AVX-VNNI's vpdpbusd,
    vpdpbusds, vpdpwssd, vpdpwssds) and multiply-adds (AVX512IFMA's
    vpmadd52luq and vpmadd52huq);
  - the logic: vpandq, vporq, vpxorq, vpternlogd and vpternlogq;
  - the shifts: vpsll, vpsrl and vpsra in w, d and q, by an immediate and by an
    XMM register's count, vpsraq, the variable ones (vpsllv, vpsrlv, vpsrav in
    w, d and q), vpslldq, vpsrldq, vprold, and AVX512VBMI2's concatenating
    vpshldv and vpshrdv (w, d, q);
  - the moves of elements: the permutes (vpermb, vpermw, vpermd, vpermq,
    vpermps, vpermpd, vpermilps, vpermilpd, vpermt2 in b, w, d, q, ps and pd),
    vpshufb, vpshufd, vpshuflw, vpshufhw, vshufps, vpalignr, valignd, valignq,
    vshufi32x4, vshufi64x2, the broadcasts
    (vbroadcastss, vbroadcastsd, vpbroadcast in b, w, d and q, from a general
    register too), the inserts and extracts (vinserti32x4, vinserti64x4, f32x4,
    f64x4, i32x4, i64x4, vpinsr and vpextr in b, w, d, q), the packs (vpacksswb,
    vpackssdw, vpackuswb, vpackusdw), the narrowing moves (vpmov, vpmovs and
    vpmovus from d and q to b, w and d, and from w to b) and the widening ones
    (vpmovzx and vpmovsx from b, w and d), and the compresses
    and expands (vcompress and vexpand in ps and pd, vpcompress and vpexpand in
    b, w, d and q);
  - a vector's signs, bytes or bits into a register: vpmovb2m, vpmovw2m,
    vpmovd2m, vpmovq2m, vpmovmskb, vmovmskps, vmovmskpd, vpshufbitqmb; an
    opmask's bits into a vector's elements: vpmovm2b, vpmovm2w, vpmovm2d,
    vpmovm2q; the selects by a register's signs (vpblendvb, vblendvps,
    vblendvpd) and by an opmask (vpblendmd, vpblendmq);
  - gathers and scatters: vpgatherdd, vpgatherdq, vpgatherqq, vpscatterdd,
    vpscatterqd, vpscatterqq (VEX and EVEX);
  - AES and GF(2^8): vaesenc, vaesenclast, vaesdec, vaesdeclast, vaesimc,
    vgf2p8affineqb, vgf2p8mulb;
  - the opmask registers: kmov (to and from general registers, memory and each
    other), kand, kandn, kor, kxor, kxnor, kadd, knot, kunpck and kshiftl;
  - BMI's andn, bextr, bzhi, pdep and pext (their VEX forms), paddd (SSE2's,
    the vector check's) and prefetchit0 and prefetchit1;
  - AMD's XOP (#136): vfrczps, vfrczpd, vfrczss and vfrczsd (whose scalar
    forms zero the rest of the register), the shifts and rotates by a vector
    of counts (vprot, vpshl and vpsha in b, w, d and q, both encodings) and
    vprot by an immediate, the horizontal sums and differences into wider
    elements (vphaddbw to vphaddudq, vphsubbw, vphsubwd and vphsubdq), the
    multiply-accumulates (vpmacs and vpmadcs) and vpperm;
  - AVX-NE-CONVERT's loads (vbcstnebf162ps, vbcstnesh2ps, vcvtneebf162ps,
    vcvtneeph2ps, vcvtneobf162ps and vcvtneoph2ps), without MXCSR or
    exceptions, as vcvtneps2bf16's, and AVX10.2's EVEX forms of AVX-VNNI-INT8's
    and AVX-VNNI-INT16's dot products (vpdpbssd to vpdpwuuds), as
    AVX-512VNNI's;
  - SHA512's vsha512msg1, vsha512msg2 and vsha512rnds2 and SM3's vsm3msg1 and
    vsm3msg2, which modify their first operand, and AVX10.2's EVEX forms of
    SM4's vsm4key4 and vsm4rnds4 of 64 bytes;
  - AVX10.2's saturating conversions with truncation (vcvttps2dqs to
    vcvttpd2uqqs), as the AVX-512 ones they saturate; its bf16 operations
    (vscalefbf16, vgetexpbf16, vrcpbf16, vrsqrtbf16, vmaxbf16 and vminbf16),
    without MXCSR or exceptions, as vcvtneps2bf16's, and vcomisbf16, as
    vcomiss; its conversions to and from 8-bit floats (vcvtph2bf8 to
    vcvt2ph2hf8s and vcvthf82ph), without MXCSR or exceptions; and vcvt2ps2phx
    and vdpphps, which read MXCSR;
  - MOVRS's vector loads (vmovrsb to vmovrsq), as vmovdqu8's, and AVX512-BMM's
    vbmacor16x16x16 and vbmacxor16x16x16, which modify their accumulator;
  - the extensions' system instructions the x86 rows take: RAO-INT's aadd, aand,
    aor and axor, as the locked arithmetic, atomic but leaving the flags;
    cldemote and prefetchrst2, as the prefetches; serialize, a fence; MOVRS's
    integer loads, as mov; and a SYSTEM operation of its own for those whose
    effects are on state the table doesn't model, a transaction's (xend, a fence
    too, xtest, xsusldtrk and xresldtrk), the user interrupts' (clui, stui,
    testui and senduipi), the tiles' configuration (ldtilecfg and sttilecfg, of
    64 bytes of memory, and tilerelease), a monitor's address (umonitor), LWP's
    control block (llwpcb, slwpcb), trace packets (ptwrite), enqueued commands
    (enqcmd and enqcmds) and MSRs (urdmsr, uwrmsr), each but ptwrite and xtest
    with FAULT, since the processor's or the system's state may fault them, and
    xtest's, testui's and enqcmd's flags;
  - the leaves' (`c_builtin_x86_helpers.c`): Key Locker's aesenc128kl to
    aesdec256kl as crypto operations of their register and the handle's memory
    (48 bytes, or 64, the row's width, which the form table doesn't give), and
    their wide forms with `IMPLICIT_VECTORS`, each defining every status flag (ZF
    says whether the handle was valid) and keeping the bits above 127 of what it
    writes (legacy encodings); encodekey128 and encodekey256 (a register's
    information, the handle in XMM0 to XMM3) and loadiwkey (EAX and XMM0
    implicit) as SYSTEM, with FAULT; tpause and umwait as SYSTEM, reading EDX:EAX
    and defining the status flags (CF says whether the OS's time limit ended the
    wait), with FAULT; and xbegin's rel32 form as SYSTEM with a conditional
    branch's control, a fence, and RAX read and written under a condition (the
    abort status, where the transaction aborts to its fallback).

The other VEX and EVEX forms are uncovered, and so are mulx (whose rdx the rows
can't name, as Tier 0's rows refuse it), v4fmaddps (a register block), APX's
EVEX forms of general registers and their NF and ND forms, MMX's, the string
instructions and the moffs forms. The families' rows come from a generator's
rules on the form table, executed and corrected on hardware: the harness, not
the generator, is their check. It runs every covered form its host's target
allows, which #180's table decides: neither the 9700X nor the Zen 4s run
AVX512-FP16's forms, an extension the table doesn't classify, nor the VEX forms
of AVX-IFMA and AVX-NE-CONVERT (vpmadd52luq's and vcvtneps2bf16's), features it
has no bit for. Their rows follow their EVEX and single-precision twins' by
rule, unexecuted. AVX512VP2INTERSECT's and AVX-VNNI's run on the 9700X alone.
XOP's, AVX-NE-CONVERT's, AVX10.2's, SHA512's, SM3's, SM4's, MOVRS's and
AVX512-BMM's run on no host the tests have (Zen 5 has none of them), nor does
the table classify them, nor the system instructions' and the leaves':
`test-x86-intrinsics` compares the compiler's code of their intrinsics with
clang's instead (`x86_intrinsics_test.c`).

## Adding a form

1. Add its run to `BUSTER_X86_EFFECT_ROWS` in `x86_semantics.c` (consecutive
   `X86Form`s that share their meaning in one row), giving:
   - its operation, its pattern and, for a vector destination, its write;
   - the flags read, written and undefined, by the manuals, the union where
     they differ;
   - its implicit registers, memory, immediate kind, control and properties.

   The form table supplies the rest: the operands' roles and classes, a general
   register's rest, the width. An x87 row has `X87` (and fldcw's
   `X87_CONTROL`), and its ST(i) operands get no role. Initialization refuses a
   row that doesn't fit its form, an x87 register outside an x87 row among them,
   and rows that overlap.
2. If a fixture of the issue's kind fits (a fixed register, a partial write, a
   flag), add one to `x86_semantics_fixtures`.
3. If the vector resolver should leave the form's records to its scalar twin
   (an operand no role names: an /is4 register, a VSIB index, an opmask pair),
   the initialization gives it `BUSTER_X86_EFFECT_RARE` from its operands; a new
   kind of such operand goes there and in `x86_instruction_resolve`. An opmask
   operand's role gets its `BUSTER_X86_EFFECT_OPMASK_*` from its class.
4. If #180's table doesn't classify it, add its row to `x86_target_rows.c` with
   its features: the harness runs only a form its host's target allows, and
   `test-x86-target`'s GNU as gate checks the features.
5. Run `test-x86-semantics`: it executes the form from random states, with
   XMM16 to XMM31 and an opmask (zeroing too) where an EVEX form takes them. A form
   that branches or traps isn't executed; add a fixture of its effects instead,
   as call's and ret's are. A form that needs operands the harness doesn't
   generate gets them where `x86_semantics_check_run` makes division and bt
   fault-free.
6. If a selector gives it immediates, add its row and candidates to
   `x86_choice_sources`, shortest first. A candidate of another instruction
   names the flags it defines alike. The test then runs every candidate against
   its reference, and `--cross-check` against GNU as.

## Measurements

On the 9700X (CPU 2, a hold of the lock per run), with `benchmark-x86-semantics`'s
defaults: 65,536 requests, and 2,048 functions of 96 records shaped like
clang's code. Main is `9bb5060e` and the head `cff7bc98`, each built by
`build.sh` with `BUSTER_DEBUG_INFO=0`, five rounds in an order alternating by
round ([the record](https://github.com/buster14a/buster-performance/tree/a74e2a6ee49e31ea9f948f597ecefc9c02f3e835/docs/measurements/x86-semantics-221-2026-10-28)). The final head (`ecb3fcfd`) takes the scalar
twin's x87 and vzeroupper resources from a table, measured below as a variant.

**The VEX and EVEX coverage (#760)**, on the 9700X: main `5c7a0bd7` against
`62c638ac`, five rounds in an order alternating by round
([the record](https://github.com/buster14a/buster-performance/blob/ae896f8bedeca1419e8d384e26a221326d255396/docs/measurements/x86-semantics-760-2026-10-29/README.md)):

- the records take 50,384 bytes (3,149 of 16: the 3,148 covered forms' and the
  conservative one), and `x86_semantics_initialize` 97,285 cycles (17.5 µs)
  against 45,750 (8.3 µs), the operands' part of a record made once per
  distinct shape (328);
- the choice is unchanged: 3.33 cycles per request;
- the AVX-512 effects loop takes 6.19 cycles per record on -O0's shape and
  6.23 on -O2's, against 5.45 and 5.50, at 25.50 instructions against 23.63,
  back to back and after 200 ms of its twin alike: it takes a `vvvv` operand as
  a lane's third role, which every record pays for, rather than sending its
  record to the twin. The twin takes 72.43 cycles against 46.86, walking three
  roles and checking the opmask roles, `/is4` operands, VEX gathers and merges
  for every record, and the opmask column (`x86_instructions_effects_opmasks`)
  8.89. The default compile runs neither: their consumers, the Tier 1
  scheduler and the unwind analyzer, are opt-in. The benchmark's records are
  legacy forms almost all, so this is their cost, not vector code's;
- #180's table takes 795 cycles to build, against 707, classifying 3,463
  forms from 1,675;
- `-c` of the frozen `sqlite3.c` writes the same object, with 281,969
  instructions more (+0.021%; `tiny.c`'s two functions take 314,928 more: the
  initialization's fixed cost) and 0.58% more cycles (rounds +0.23% to
  +0.70%), which no placement comparison attributes.

The rest of this section is #761's: main `9bb5060e` against `cff7bc98`.

**Tables:**

- the records take 11,408 bytes (713 of 16: the 712 covered forms' and the
  conservative one), against main's 9,808 (613);
- the index takes 16,704 bytes (a u16 per form, of 8,352), and the lookup by
  form number (`x86_effects_by_form`, the index with its FS and GS copies)
  65,536; the compiler touches the lines of the forms it emits;
- the choice's tables take 2,176 bytes, and the scalar twin's rows 2,304 more;
- `x86_semantics_initialize` takes 45,900 cycles (8.3 µs) against main's 37,600
  (6.8 µs): the hundred rows more and the lookup's three copies.

**Choice and effects**, per request or record, medians of the five rounds (the
effects on -O0's shape; -O2's within 1%):

| Path | Main | Head | Head alone (`--only`) | Head after 200 ms of the scalar twin |
| --- | --- | --- | --- | --- |
| choose, scalar | 48.75 cycles | 49.30 cycles | | |
| choose, AVX-512 | 3.32 cycles, 0.60 ns | 3.32 cycles, 0.60 ns | 3.32 cycles | 3.33 cycles |
| effects, scalar | 49.63 cycles | 49.04 cycles | | |
| effects, AVX-512 | 5.84 cycles, 1.05 ns | 5.45 cycles, 0.98 ns | 5.44 cycles | 5.45 cycles |
| `x86_encode` of the same records | 3.25 cycles | 3.26 cycles | | |

Choice is 15 times its scalar twin's speed and effects 9 times. The scalar
choice runs main's code and instructions: its 1% is placement. Both loops are light
enough that the power cap hardly takes them (#199's method).

**The effects loop's lookup and rare lanes.** Each loop built alone in
`benchmark-x86-semantics`'s unit, `--only effects-avx512`, five processes of
each in a rotating order, the medians of their medians in cycles per record on
-O0's shape and -O2's:

| Loop | Back to back | After 200 ms of the scalar twin | Instructions | IPC |
| --- | --- | --- | --- | --- |
| main: each lane's record by `x86_semantics_row`'s row | 5.85, 5.90 | 5.83, 5.88 | 31.38 | 5.36 |
| main by form number (`x86_effects_by_form`) | 5.24, 5.30 | 5.24, 5.32 | 23.25 | 4.43 |
| the first head (`c2885004`): rare lanes, EVEX forms among them, counted in a general register | 7.11, 7.12 | 7.03, 7.10 | 35.66 | 5.02 |
| the head by form number, its rare lanes counted in a general register | 5.53, 5.61 | 5.55, 5.63 | 23.88 | 4.32 |
| the head: its rare lanes ORed in a vector register | 5.44, 5.49 | 5.44, 5.52 | 23.63 | 4.35 |
| the same with an opmask's records rare lanes too (`known` without its `vptestnmq`), a second series | 5.45, 5.54 | 5.47, 5.52 | 24.13 | 4.42 |

- `x86_semantics_row`'s arithmetic, a compare and two subtractions in every
  lane, was a quarter of main's instructions: by form number, its loop takes 10%
  fewer cycles.
- The head's rare lanes (an x87 record, vzeroupper's, a vvvv operand) and its
  opmask records' conservative effects cost three vector instructions per eight
  records, 0.2 cycles per record over main by form number; the loop runs at 4.4
  instructions per cycle. Folding the opmask's test into the rare lanes measured
  no better (5.45 against the final head's 5.42 in that series).
- A count of the rare lanes in a general register cost 0.1 cycles more: the
  lanes' lookups take every general register the loop has. Sending EVEX records
  to the scalar twin (the first head) cost 20%: clang -O2's AVX-512 code has
  many, and so has the benchmark's mix.

**The scalar twin** (`--only effects-scalar`, the same five processes): main
48.81 and 48.28 cycles per record at 164.6 instructions; the head, with a test per
x87 or vzeroupper property, 47.97 and 47.51 at 189.9; with what they add from a
table of their bits, as the final head takes it, 46.74 and 46.27 at 179.0. It resolves
more of the benchmark's records whole than main (99.0% against 98.4%), with
their x87 state, vzeroupper's registers and three-operand zero idioms, in fewer
cycles: the lookup by form number serves it too.

**On real functions** (`benchmark-backend-source` on the frozen `sqlite3.c`,
whose 2,602 functions make 396,872 typed entries): Tier 0 asks the choice 71,198
times, 0.179 requests per entry, and the choice takes 1.79 cycles per entry
(10 per request, a function's 27 at a time from the copies that
`Tier0Function.choice_requests` keeps), 1.50 after 200 ms of its scalar twin,
which takes 7.70. Of the requests, 3.3% kept their template's form, 44.0% took
another encoding of the instruction and 52.7% another instruction (xor for
mov 0, test r, r for cmp r, 0, a narrower test): their records' 450,190 bytes
became 234,542, 15% of the code the functions would make with each template's
widest form (`docs/tier0.md`).

**The upper-lane read** (the read of the vector registers a legacy SSE form
writes, from #239's review) costs about 2% of the effects loop. Cycles per
record back to back (`--only effects-avx512`), medians of runs alternating with
the loop without it, but for the two marked, which ran on their own:

| How the loop takes it | -O0's shape | -O2's shape | Instructions | IPC |
| --- | --- | --- | --- | --- |
| not at all (before the fix) | 5.75 | 5.76 | 31.00 | 5.39 |
| a mask per role in the operands' masked ORs (on its own) | 6.28 | 6.34 | 34.00 | 5.42 |
| a masked `vpternlogq` from a `vptestmq` after the operands (on its own) | 6.13 | 6.18 | 31.50 | 5.14 |
| a zero-masked `vpandq`, joined with the memory operand's registers | 5.97 | 6.03 | 31.25 | 5.23 |
| the same, joined at the end | 5.91 | 5.93 | 31.38 | 5.31 |
| a shift of the property's bit 2 to a lane mask, at the end | 5.86 | 5.93 | 31.50 | 5.37 |
| `KEEP_UPPER` as the record's top bit, one shift, at the end | 5.85 | 5.87 | 31.38 | 5.36 |

The instructions hardly change. What costs is a `vptestmq`, one more mask
register in a loop that has many in flight, and a join inside the read set's
chain of masked ORs, which run at 5.4 instructions per cycle.

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
movabs are faster too.

**Whole programs:** SQLite's speedtest1 (its `test/speedtest.tcl` options at
`--size 5`, `sqlite3.c` 3.53.2 and `speedtest1.c` each `-c`'d by Tier 0 and
linked by cc, from a database on tmpfs), built by the head and by the head
without the substitutions (the choice's candidates of another instruction
removed: xor, test r, r, the narrower test, inc and dec; `nosub`). Both give
the same verification hash.

| | Head | Without the substitutions |
| --- | --- | --- |
| `sqlite3.o`'s `.text` | 1,559,506 bytes | 1,625,410 bytes |
| the executable's `.text` | 1,689,765 bytes | 1,756,917 bytes |
| run time, the second run of each round (7 and 8 rounds) | 256.67 ms | 257.80 ms |
| fastest of 15 runs | 256.03 ms | 257.02 ms |
| user-space instructions | 5,685.89 M | 5,685.94 M |
| user-space cycles | 1,320.0 M | 1,329.0 M |

The substitutions take 4.1% out of `sqlite3.o`'s code, in as many
instructions, and the program runs 0.4% to 0.7% faster with them. Each of the
15 rounds ran both builds in an order alternating by round; the run that opens
a round, after the lock's other holders, is slower whichever build it is, so
the table compares the second ones. The instructions and cycles are one
`perf stat` run of each.

## Next steps

- **The X86 rows' legacy forms** (`tier0_operation_legacy_forms`, #148): most
  are uncovered (the shadow stack's, RDRAND and RDSEED, the FS and GS bases'
  writes, the cache flushes, WBINVD, INVPCID), so a record of them reads and
  writes everything; the deterministic user-mode ones (CRC32, SHA, the
  non-temporal and direct stores, EXTRQ and INSERTQ) can get rows the harness
  runs. The extensions' system instructions have rows no harness runs, whose
  code `test-x86-intrinsics` compares with clang's.
- **A consumer of the opmask column:** a scheduler of records with opmask
  registers takes `x86_instructions_effects_opmasks`; Tier 1 has none yet
  (#748 brings its vector masks).
- **#181:** key its latencies by the same forms.

## Boundary audit #298

Audit pin: `42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`;
[scope and evidence](simd-boundary-audit.md).
[The shared semantic tables](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/x86_semantics.c#L4)
are side-table producers for several consumers:

| Producer → consumer | Contract |
| --- | --- |
| Form catalogue → `x86_semantics_initialize` → `x86_effects` | Immutable per-form effects after initialization; unknown forms get conservative all-effects records, never assumed pure. |
| Allocated record operands + form effects → `x86_instructions_effects` → scheduler/verifier | Two u64 resolved resource sets per record; physical registers, zero idioms and immediate shifts matter. Eight-record vector groups have bounded tails. |
| Tier 0 semantic immediate requests → `x86_choose_forms` → final machine records | Value/width/destination/live-flags information drives 32-request classification; chosen form must preserve value and all live flags. |
| Forms/records → `x86_target_forms` / model lookups / encoding | ISA legality, performance observations and encoding are separate consumers; effect coverage does not imply target legality or measured cost. |

Keep the instruction record at 16 bytes. Counted natural-aligned arrays and
separate result columns have no implied readable padding; initialization
precedes parallel reads. Call clobbers must additionally come from ABI metadata:
architectural CALL effects do not encode a callee's convention. Live flags are
producer knowledge that cannot safely be reconstructed from opcode alone.

Disposition: retain shared effects and semantic form requests. An emitter that
already has resolved effects may avoid rebuilding them, but any cached columns
must be invalidated after register assignment, form substitution or operand
changes. Measure requests/effects construction, selection/scheduling and final
encoding together before retaining more per-record data. `test-x86-semantics`
uses scalar twins, native flags/value checks and GNU as; [#221](https://github.com/buster14a/buster/issues/221)/[#141](https://github.com/buster14a/buster/issues/141)/[#172](https://github.com/buster14a/buster/issues/172) retain
source-corpus consumer integration and generated-program quality. Unknowns
remain conservative, not candidates for unproved reordering.

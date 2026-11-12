# Tier 1 instruction selection

`tier1_select.c` implements a bounded integer consumer of the actual
[`IrFunction`](ir.md) representation for
[#141](https://github.com/buster14a/buster/issues/141). It plans before register
allocation and emits actual `X86Instruction` records after allocation. It does
not create another source or SSA IR.

The implementation is original project code. Architectural immediate and flag
rules come from the existing [x86 semantics](x86-semantics.md) module; the
[Intel instruction reference](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
was consulted to check comparison, immediate-extension and addressing rules.
No implementation, pattern tables or benchmark harness was copied from another
compiler.

## Supported slice

The current body emitter handles 32-bit and 64-bit integer values, pointers,
canonical `i1` comparisons, and bounded byte/word value transport. The supported
patterns are:

- Constants, register copies, truncation to 8/16/32 bits, zero extension from
  8/16/32 bits or canonical `i1`, and sign extension from 8/16/32 bits to a
  wider supported integer type. Pointer/integer conversions preserve 64 bits.
- 32/64-bit negation, complement, add, subtract, multiply, and/or/xor;
  immediate forms; shifts by a constant
  strictly below the operand width; multiplication by powers of two and by
  3, 5 or 9; add/shift and three-operand addition through `lea`. After
  allocation, an unscaled base-plus-index sum with zero displacement uses
  `add` when the destination is either input register (#1217), removing the
  SIB byte, and a base plus a nonzero displacement whose destination is the
  base's register is an `add` of the displacement (or the `inc` or `sub` the
  immediate's batch chooses), as clang writes `p += c` and `i + 1` in place: of
  Tier 1's dynamic non-frame `lea`s at `33f3f442`, 57.4 M of speedtest1's 182.2 M
  and 52.3 M of Lua's scripts' 311.5 M were these. The instruction count is the
  same; the address cause shrinks and the arithmetic grows by as much.
  A signed power-of-two division adds its bias in place the same way
  (`TIER1_RULE_DIVIDE_POWER`, a pointer difference's division by its element's
  size). Flags are dead between IR nodes. Folded frame operands have their
  final base and object offset before this decision; any other displacement
  form or an `rsp` base keeps `lea`.
- Explicit `IR_ADDRESS` base + index × scale + signed disp32, loads and stores,
  and folding those addresses into memory operands. Byte/word loads zero-extend
  into registers, while stores write exactly one/two bytes. An address whose
  constant index, scaled, and offset don't fit a disp32 (#141: `p[1ull << 30]`,
  a struct array's element past 2 GB) is its own `lea`, the constant in a
  register (its node's `mov`) as the index and the offset the displacement,
  which no user folds; one whose offset alone doesn't fit 32 bits is still
  refused. An
  access's address is walked (`tier1_select_address`, #140) through pure
  address nodes in its block, at most 8 of them: the `inttoptr` and `ptrtoint` the
  typed stream's integers put around every address, addresses (one after
  another while the operand has room for their one index), constant indices
  and 64-bit additions of constants, which join the displacement while it fits
  32 bits. `s->z[2]` is then one `mov` from `[s + 24]` rather than three
  conversions, two `lea`s and the constants' moves. Memory operands can each
  fold a shared address chain (#1217); its producer remains when another
  selected user still needs the address value. A pointer that is a value
  (a ptrtoint or an inttoptr of a single-use chain) is one `lea`; address-valued
  roots retain the single-use restriction. The index sheds the
  constants added to it (`tier1_select_index`): a 64-bit addition of a constant
  on either side, or a subtraction of one, joins the displacement scaled while
  it fits 32 bits, so that `p[i + 100]` of a 64-bit `i` is `[p + i * 4 + 400]`
  and `p[i - 100]` is `[p + i * 4 - 400]`; a subtraction from a constant stays
  a node of its own. A 32-bit `int` index
  keeps its addition, which comes before its sign extension: `(i + c) * s` isn't
  `i * s + c * s` when `i + c` overflows, which the IR doesn't rule out (and
  `-fwrapv` defines). A walk that reaches a frame object's address
  (`IR_STACK_ADDR`) ends there instead (#982): the load, store or address takes the frame's base register in its memory operand,
  with the object's offset plus the walked displacement, which the CFG consumer
  fills in at emission from the frame's layout, as it does the object's `lea`.
  The folded pattern reads no base value, so the object needs no `lea` of its
  own; realigned frames and base-register frames work through the same base and
  offset the `lea` takes. The object may live in the entry block past the
  walk's block: the producer materializes every slot address in the preheader,
  which dominates every use, and the address is pure with a function-global
  offset. Memory operands can fold this leaf even when it has several users;
  an escaping address still materializes. A direct symbol's address
  (`IR_SYMBOL_ADDR`) ends the walk the same way (#2573), from any block, since
  it reads no value either: the operand is rip-relative, the symbol plus the
  walked displacement, and the CFG consumer gives the access's one rip-relative
  record a `PC32` reference whose addend is that displacement
  (`tier1_emit_cfg_kernel`; `link_add_code_fixups` takes off the distance to
  the instruction's end, an immediate's bytes after the field among it, 5 for
  `cmp byte ptr [rip + x], 3`). SQLite's `mem5`, `sqlite3Config` and
  `sqlite3Stat` fields are then one `mov` from `mem5+off(%rip)`, as clang's,
  rather than a `lea` of the symbol and an access through its register. Only a
  symbol the unit's access column reaches directly folds: the column reaches
  selection with the plan (`tier1_select_plan_access`,
  `tier1_select_plan_verified`), and a GOT slot's or a thread-local's address
  keeps its sequence, its accesses going through the register it leaves. A
  rip-relative operand has no index, so a table reached through one
  (`yy_lookahead`, `sqlite3Stat.nowValue[op]`) keeps its symbol's `lea` as the
  base. The walk takes either leaf one node past its bound: the typed stream
  puts a ptrtoint and an inttoptr around each member's address, so
  `sqlite3Config.m.xSize`, an indirect call's callee in `sqlite3MallocSize`, is
  eight nodes above its symbol, and the leaf reads no value whose life it could
  lengthen. An address-valued root takes the leaf under the single-use rule:
  `&g.member` is one `lea g+off(%rip)`, which a CFG fragment recomputes as it
  does a symbol's own `lea`. The pattern carries the symbol in `stack_object`
  with `rip`; the symbol's node keeps the register leaf's class in the
  bottom-up grammar, and a load's or a store's walked candidate, the only one
  through the leaf, is its rule's. The consumer refuses a folded symbol its own
  column doesn't reach directly, and the straight-line consumer, which the
  chain rule never gives a function with a symbol's address, refuses one too.
  A memory operand also takes a pure
  address node (an address, a 64-bit addition or a conversion) of an earlier
  block when every user of that node takes it as an address, a load's or a
  store's or another such node's (`tier1_select_sinkable`, #1217): SSA makes the
  node and the values it reads dominate the access, the effective uses keep those
  values live to it, and no node computes the address any more. Lua's `ra =
  RA(i)`, computed at a handler's start and stored through in the arms its type
  tests choose, is then `base + index` in each store. An address some other
  user keeps (a phi's operand, a call's argument, an integer) stays computed and
  its accesses read its value: folding it would only lengthen its inputs' lives
  past it, which cost speedtest1 37 M instructions in spills before this rule.
  The index walk (`tier1_select_index`) stays within the access's tree. A
  displacement the object's offset pushes past 32 bits refuses emission rather
  than truncating.
- C's truth values (`tier1_select_truth`, #140): the lowering gives `a < b` as
  `zext (icmp lt a, b)`, which `if`, `!`, `&&` and `||` compare with zero again,
  `icmp ne (zext x), 0` (`eq` for `!`), once for each operator the source
  nests. A branch on such a chain is its comparison's `cmp` and `jcc`, and its
  zero extension to an integer the comparison's zero-initialized destination,
  `cmp` and `setcc` (an aliased destination is cleared by a flag-preserving move after `cmp`),
  negated for an odd number of `eq` links, where every link is consecutive and
  used only by the next, so that nothing the pattern doesn't compute runs
  between the comparison and its test. `if (a < b)` had been
  `cmp; setl; movzx; test; setne; movzx; test; jne`.
- Division and remainder by a power of two 2^k: unsigned, a shift right by k or
  a mask of k bits (a mask fitting an imm32); signed, for 1 ≤ k ≤ bits − 2,
  `TIER1_RULE_DIVIDE_POWER`'s sequence, rounded toward zero as C's: the bias
  2^k − 1 where the dividend is negative, from its sign in the scratch register
  (`mov`, `sar`, `shr`), added by a `lea`, then an arithmetic shift, or for the
  remainder a mask and the bias subtracted. A divisor of 1 is a copy (a zero
  remainder).
- Division and remainder by any other constant but those `div` and `idiv` keep
  (zero, and for a signed one −1, which raise C's undefined division by zero and
  `INT_MIN / -1` as the hardware does, and −2^k and the sign bit): a
  multiplication by the divisor's reciprocal and shifts
  (`TIER1_RULE_DIVIDE_RECIPROCAL`), after Granlund and Montgomery's "Division by
  Invariant Integers using Multiplication" (PLDI 1994), whose CHOOSE_MULTIPLIER
  gives the multiplier and its shift (`tier1_select_reciprocal`). For N-bit words:
  - unsigned, the high N bits of the dividend times the multiplier, shifted right.
    Where the multiplier needs N + 1 bits, an even divisor's dividend is shifted
    right by the divisor's zero bits first, which leaves its odd part's multiplier
    N bits, and an odd one's quotient is `(((n − t) >> 1) + t) >> (s − 1)`, t the
    high product of its low N bits;
  - signed, by the divisor's magnitude, the signed high product, plus the dividend
    where the multiplier is 2^(N−1) or more, shifted right arithmetically, plus its
    own sign bit, which makes the floor C's rounding toward zero; negated for a
    negative divisor;
  - a remainder, the dividend less the quotient times the divisor (its magnitude
    for a signed one, whose quotient then stays unnegated).

  The high product is `mul`'s or `imul`'s `rdx`, so the rule takes x86's fixed
  registers as `TIER1_RULE_DIVIDE` does: `tier1_select_fixed` writes its records
  (at most `BUSTER_TIER1_SELECT_FIXED_RECORDS`, 13: a remainder whose multiplier
  needs the add sequence, whose divisor is past an imm32 and whose dividend is in
  `rax` or `rdx`), reading the dividend before writing `rax` or `rdx`, through the
  scratch register where it is read after the multiplication, and the destination
  last; each consumer saves the other values that live past the node in `rax` and
  `rdx` around them (`tier1_emit_fixed`). Every other division stays
  `TIER1_RULE_DIVIDE`.
- A nonvolatile load whose only use is an arithmetic consumer in its block
  can become that consumer's memory operand (the right operand, or a
  commutative operation's left where its right isn't a load), its access moved
  to the consumer (#141). It moves past the nodes between them that don't touch
  memory, call, fence or move rsp (arithmetic, constants, addresses) and past
  none that do: a store to any address, a call, any other load (#1059's audit:
  the typed stream doesn't carry a C access's volatility, so that a load the IR
  doesn't flag `BUSTER_IR_VOLATILE` may still be a volatile object's, and two
  volatile reads keep their order: `tier1_select_fold_barrier`), an atomic
  read/modify/write, a fence of any order or scope (a signal fence's too), an
  allocation or a restore of the dynamic stack; a volatile or atomic load (#1059:
  `BUSTER_IR_OBSERVABLE`) folds into nothing. On `main.c`'s `--tier1` build the
  rule costs 6 records of 239,884 and 32 bytes of code. A pass of its own after the plan's pass over
  the nodes keeps each one's last such barrier (`Tier1SelectForest.barriers`),
  a running maximum 16 nodes at a time (`tier1_select_barriers`, its scalar
  twin `tier1_select_barriers_scalar`), so the check is a comparison. Inside the
  plan's pass, its store made the operands' walk reload the function's columns
  (the build's `-fno-strict-aliasing`): 40 instructions more per node. C's aligned atomic loads and stores of a constant
  order reach it as atomic accesses (#1059). Loads and relaxed/release stores select ordinary accesses (`mov`, `movzx`),
  since x86-64's total store order gives their orders. A seq_cst `store` keeps its semantic IR operation and selects a
  copy of its desired value into reserved `r11`, then a width-matched memory `xchg`, without `mfence` (#1396). The copy
  preserves the desired SSA value if it remains live, and the final memory record retains folded and frame addresses.
  Both selection paths and the emitted-code certificate use this kernel; tests reject replacing the exchange by `mov`
  or dropping the preserving copy. An exchange, an addition or a subtraction of a constant
  order reaches it as the IR's `rmw` (`TIER1_RULE_ATOMIC_RMW`): the operand copied to the scratch register, `xchg` or
  `lock xadd` of the object's width (a byte's and a short's at their own, never their 32-bit register's, which
  `test-tier1-select` runs on guard bytes), and the old value zero-extended into its register as a load's is, a barrier
  no load folds past. Other atomic operations reach it as calls of the unit's leaves. A fence of a constant order is the IR's `fence` (`TIER1_RULE_FENCE`): `mfence` for a thread fence of `seq_cst`
  and no records for the others, a barrier no selection moves an access across. A volatile load remains a separate
  access; a store retains its original order and access width.
- Integer comparisons can fold a single-use load into `cmp` under the same
  access-order proof (#1218), including a materialized boolean, an extended
  truth value and a fused conditional branch. Either operand may be a direct
  32- or 64-bit load; a load originally on the left swaps the predicate when
  the other register becomes the left operand. The latest legal load is
  selected when both operands load, preserving their original access order.
  A zero/sign extension of a byte, word or dword load can be removed when
  comparing with an immediate that the same extension represents exactly.
  The memory access keeps the source width, and a signed predicate on a
  removed zero extension becomes unsigned. Arbitrary register comparisons
  through an extension, truncations, shared values and observable accesses
  remain unfolded. Immediate admission still uses the comparison width, so
  some representable narrowed immediates, such as an I32 zero extension to
  I64 compared with `0xffffffff`, remain conservative misses. Direct-load
  productions include their savings in the BURS grammar; promoted-load
  removal is a legality-dependent candidate refinement whose grammar cost
  conservatively still includes the removed load/extension. Exact pricing
  would need comparison-aware promotion classes.

  The proof runs at the final consumer, so a branch or truth extension cannot
  move an access past an operation after the original comparison. Both address
  values remain effective uses. A boolean destination overlapping an address
  register or the other operand is cleared with a flag-preserving move after
  `cmp`. Frame addresses are resolved before pattern expansion, exposing the
  actual base to this alias check and placing the displacement on `cmp`
  rather than its following `setcc` or branch records. Guard-page predicate
  tests, access-order fixtures and realigned frame-object execution cover
  these cases in `test-tier1-select` and `test-tier1-emit`. This implements
  compare-load folding; #1218's broader branch-layout, workload instruction
  count and runtime acceptance remain separate.
- The IR's `intrinsic` (#141): `mulhu` is `TIER1_RULE_MULTIPLY_HIGH` (one operand in `rax`, `mul` of the other, its
  `rdx` the result, saved around as a division's fixed registers are), and the x86 builtins' `pdep`, `pext`, `bzhi`
  and `bextr` of an `I32` or an `I64` are `TIER1_RULE_INTRINSIC`, their one VEX instruction of the width
  (`tier1_select_intrinsic`), `pdep`'s and `pext`'s source in `vvvv` and mask in `rm`, `bzhi`'s and `bextr`'s source in
  `rm` and index or control in `vvvv`, the destination any register, an operand's too. Neither folds an operand. The
  emission's target allow-list refuses the instruction for a target without BMI2 (BMI1 for `bextr`), the lowering's
  own admission having refused its row there already; `test-tier1-select` runs each of both widths from both operand
  orders into its own register and each operand's natively on both paths where the machine has them. The bit
  operations (#1374), `tzcnt`, `bsf`, `lzcnt`, `bsr` and `popcnt`, are `TIER1_RULE_INTRINSIC` too: their one
  instruction of the width, the operand in `rm` and the destination in `reg`, any register, the operand's too; and
  `bswap`, the operand moved into the destination where it isn't there, then swapped in place. The allow-list refuses
  `tzcnt` without BMI, `lzcnt` without LZCNT and `popcnt` without POPCNT; `test-tier1-select` runs each of both widths
  into its operand's register and into two others, natively on both paths. The signed overflows (#1385), `saddo`, `ssubo` and
  `smulo`, are `TIER1_RULE_INTRINSIC` too (`tier1_select_overflow`): `add` or `imul` into the destination, which takes
  the left operand first unless it holds an operand already (both operations and their flags commute), or `cmp` of
  the two, which writes no register; then `mov` of 0 into the destination, which keeps the flags, and `seto` into its
  low byte, the destination any register. `test-tier1-select` runs each of both widths from both operand orders, of a
  masked operand and of one value twice, into a register apart and into each operand's, natively on both paths
  against the host compiler's builtins.
- Signed/unsigned integer comparisons, comparisons with zero through the shared
  flags-aware form chooser, comparison/branch fusion, simple selects using
  `cmov`, unconditional branches, returns and `IR_UNREACHABLE` as `ud2`.
- Labels and indirect jumps (#789): GNU C's labels as values and computed gotos,
  and the jump tables C's switches lower to. A label's address is its symbol's
  (`TIER1_RULE_SYMBOL_ADDRESS`). A `br_indirect` is a `jmp` through its target's
  register (`TIER1_RULE_JUMP_INDIRECT`). Its target is never a load in its own
  block that the jump could fold. Computed gotos go through their function's
  dispatch block, which the lowering puts last and the optimizer keeps. A jump
  table's entry is an offset added to the table's address. The CFG consumer takes
  every function with labels. It places the jump's edges' moves at its labels'
  blocks, which that edge alone enters, or in stubs after the jump. Each label's
  first record, where the driver defines its symbol, is its block's, or its
  stub's where that edge has one (`tier1_emit_cfg_labels`), since the jump
  enters it through the label's address. A `br_indirect` that can go to one label
  alone jumps there.
- Floating values (#141): `F32` and `F64` in the vector class's registers that
  the CFG consumer allocates, by SSE2's scalar forms, the baseline target's
  (`TIER1_RULE_FLOAT_CONSTANT` to `TIER1_RULE_FLOAT_RETURN`). A constant is zero's
  `xorps`, or its bits through the scratch register (`mov`, then `movd` or
  `movq`), and an integer constant's conversion is the floating constant it
  converts to. A copy is a `movaps`. Addition, subtraction, multiplication and
  division are the two-operand `ss` or `sd` forms, the right operand from memory
  where it folds a load, as an integer's. A negation `xorps` the sign bit, which
  the scratch register moves into xmm15. Loads and stores are one `movss` or
  `movsd`, folded through their addresses as an integer's. A comparison's truth
  value is `ucomis`, `setcc` and `movzx`, where `<` and `<=` swap their operands
  so that `seta` and `setae` are false for unordered ones; `==` and `!=` combine
  `sete` or `setne` with the parity flag's `setnp` or `setp`, by `and` or `or`,
  since unordered operands set ZF too. A branch on `<`, `<=`, `>` or
  `>=` folds the comparison into `ja` or `jae`, and its negation through C's
  truth values into `jbe` or `jb`, which are true for unordered operands as
  `!(a > b)` is: the unsigned conditions, whose negations the integer table
  already has. `==` and `!=` read the parity flag too, which one jump can't, so
  a branch tests their truth value. The conversions from and to integers of 8
  to 64 bits are `cvtsi2s*` and `cvtts*2si`, a narrow signed operand
  sign-extended into the scratch register first and an unsigned `I32` converted
  as its zero extension's 64 bits; the precisions convert by `cvtss2sd` and
  `cvtsd2ss`, and a bitcast is `movd` or `movq`. A conversion to a floating value
  zeroes its destination first, but a resize: `cvtsi2s*` writes its low element
  alone, which would otherwise wait for the register's last writer. An unsigned
  64-bit integer's conversions, whose values past `INT64_MAX` no single
  conversion takes, are sequences of the fixed-register path's room
  (`TIER1_RULE_FLOAT_CONVERT_WIDE`, through the scratch registers alone). To a
  floating value, the source converts as a signed value where its sign bit is
  clear. Otherwise it is halved with its last bit kept as a sticky bit, so
  rounding the half rounds the source, then converted, and `paddq` or `paddd`
  adds 1 to the result's exponent. From a floating value, one truncation runs,
  raising the floating exceptions C's conversion raises and no others (#2562):
  `ucomis*` compares the value with 2^63; below it, or unordered, the value's
  own bits are truncated, and at 2^63 or more its half (its bits less one in
  the exponent's lowest bit, exact there), whose result is doubled (`cmovb`
  chooses the bits and `lea` and `cmovae` the double, none of which writes the
  flags). No floating arithmetic runs, so a value in range raises no invalid,
  and an integral one no inexact; the half's truncation raises invalid exactly
  where the value is 2^64 or more. A return moves its value into xmm0. Still
  refused: a select of floating values.
- Long double (#141): `F80`, the x87's, whose values the CFG consumer keeps in
  frame objects of their own, 16 bytes each, one per value, and computes on the
  x87 stack between them, leaving it empty after each node, as Tier 0 does
  between its slots (`TIER1_RULE_X87`, its kind in the pattern's `scale`). Its
  long double operands are no pattern's values, so no allocation sees them: the
  consumer reads them from the IR's operands' objects (`tier1_emit_cfg_x87`), and
  the plan marks them needed as it marks a pattern's values. The values are the
  other operands alone: a load's and a store's address, a conversion's integer
  or floating operand. A constant stores its 10 bytes as immediates; a load and a
  store are `fld` and `fstp` of them through the address; arithmetic loads both
  operands and `faddp` (`fsubp`, `fmulp`, `fdivp`) `st(1), st(0)`; a negation is
  `fchs`. A comparison is `fucomip` of the operands in the order that makes
  each condition one unordered operands fail, as `ucomis` has them, `setcc` and
  `movzx`, `==` and `!=` with the parity flag. A float or a double goes through
  the x87 scratch object (`fld` or `fstp` of 32 or 64 bits); an integer through
  it too by `fild` of 32 or 64 bits, a narrow signed one sign-extended and an
  unsigned 32-bit one zero-extended to 64 first, and an unsigned 64-bit one read
  as signed with 2^64 added where its top bit is set. To an integer, the control
  word is saved and loaded with rounding toward zero around `fistp` of 64 bits
  (`fisttp` is SSE3's, past the baseline), and an unsigned 64-bit result of 2^63
  or more takes 2^63 away first and sets its top bit after (`fucomi`, `fcmovbe`
  and `setbe`), as Tier 0's. That conversion is a node's longest sequence, 20
  records against every node's share of 6 (`BUSTER_TIER1_EMIT_X87_RECORDS`):
  the CFG consumer's capacity counts each long double node's 20, so that a block
  dense with conversions fits, and it refuses a node's sequence past them. A phi
  of long doubles is the edges' copies, every operand loaded onto the x87 stack
  and then every phi's object stored in reverse (a parallel copy; more than 8 on
  an edge refuse the function). A call
  passes a long double in its 16-byte stack slots aligned to 16, its object's
  bytes copied there by `fld` and `fstp` (`tier1_select_next_place`), and its
  long double result comes in st0, which the call stores into its own object; a
  return loads its operand's object into st0 before the epilogue. A function's
  long double parameters are by-value aggregates, as before, and so is an
  aggregate returned in the x87's registers (`tier1_select_x87_parts`): a
  struct of a long double in st0, a `_Complex long double` in st0 and st1, each
  part 16 bytes of the aggregate. The call stores st0 into the first part and
  then st1 into the second (`fstp`); the return loads them the last first, so
  that st0 holds the first (`fld`). A long double argument whose call has ABI
  records takes its stack slots the same way.
- Call arguments and parameters take their places by the System V classes
  (`tier1_select_next_place`, #141): the k-th integer or pointer in the k-th of
  the six general argument registers, the k-th floating value in xmm k of eight,
  and each one past its class's registers in the stack's next slot. A variadic
  call's `al` is the number of vector registers its arguments take, an
  aggregate's xmm parts among them (`AbiCall.vector_registers`).
- Variadic definitions (#789): the anchors that `va_start`'s lowering reaches,
  the register save area and the overflow area. The save area is a frame object
  (`TIER1_RULE_STACK_ADDRESS`, folded into its users' memory operands like any
  other). The CFG consumer asks for it with `BUSTER_TIER1_FRAME_VARIADIC`: the
  frame planner reserves its 176 bytes, and the prologue stores the six argument
  registers there and, after testing `al`, the eight vector registers
  (`tier1_frame_emit.c`). The overflow area is in the caller's frame, so its
  address is a `lea` of its own and never a memory operand's base. The lea is
  16 bytes past RBP where the frame has a frame pointer, and otherwise past
  RSP's reservation, the callee-saved registers the prologue pushed and the
  return address, plus the named arguments' stack bytes
  (`tier1_emit_cfg_incoming`). Every variadic definition that selection
  accepts goes to the CFG consumer.
- The dynamic stack (#141): `alloca` (`TIER1_RULE_ALLOCA`) rounds its size up to
  the alignment rsp keeps, takes it from rsp and gives rsp as its address
  (`lea d, [size + alignment - 1]`, `and d, -alignment`, `sub rsp, d`,
  `mov d, rsp`), or with a constant size of up to 1 GB a `sub` of the rounded
  constant (`TIER1_RULE_ALLOCA_IMMEDIATE`); `stack_save` copies rsp and
  `stack_restore` sets it. A frame address (#1847, `stack_save frame`,
  `TIER1_RULE_FRAME_ADDRESS`) copies rbp, the frame pointer its function keeps
  (`tier1_emit_cfg_frame_flags`' `KEEP_POINTER`, whatever the leaf rule says),
  without the dynamic stack's places; the CFG consumer emits it, and the code's
  certificate decodes it as a move from the frame pointer. The alignment is the most the function's calls'
  stack arguments need: 16 bytes, or a 32- or 64-byte vector's or an
  over-aligned aggregate's. The CFG consumer sets it in the holes' `scale`, so
  that rsp keeps it at every call after an allocation. An object's own
  alignment is the frame base's, not rsp's: a frame realigned to a megabyte for
  an object (#1450) doesn't round each allocation and call area to a megabyte. An allocation and a restore are
  memory barriers: no load folds past either. Every function with one is the CFG
  consumer's, in a dynamic frame (the frame guide's "Frame and prologue rules"):
  a frame pointer, its places rbp's (or r15's in a realigned frame), and each
  call with stack arguments takes its outgoing area, its stack bytes rounded up
  to the alignment, from rsp before its stores and gives it back after it
  returns (`tier1_emit_cfg_call_area`). With stack-clash protection, a call
  area of a page or more uses the frame emitter's page loop and final partial
  touch, with reserved r11 as its bound; all argument registers and the stable
  frame base survive it (#1450). Object alignments through the frame planner's
  `1 << 30` bound are accepted, including alignments larger than a page.
- By-value aggregates (#789): a call or a function with ABI records takes each
  argument and parameter where its record says (`tier1_select_argument_place`,
  `tier1_select_parameter_place`): a scalar in its register or stack slot, and
  an aggregate, whose IR value is a pointer to its bytes, in parts of up to 8
  bytes, one per eightbyte (`tier1_select_location_parts`, as Tier 0's
  `tier0_parts_of`), or on the stack whole (up to 512 bytes). A part is in a
  general or an xmm register, or (#141) in a stack slot: a `SPLIT` location's
  other part, after a register clang doesn't count (an `_Atomic` scalar's or
  `nullptr_t`'s) left LLVM without one for it, and a `STACK` location's high or
  low eightbyte alone (`HIGH`, `LOW`), the other one padding. In registers, a
  high eightbyte of padding takes none, and with `HIGH` the only register holds
  eightbyte 1. A vector register alone of 16 bytes or more holds the aggregate
  whole, as Tier 0's parts have it: a vector passed by value (SSE then SSEUP), 16
  bytes in an xmm register, or 32 or 64 in a ymm or zmm one where the target's
  registers are that wide, moved by `movups`, `vmovdqu` or `vmovdqu64`
  (`tier1_select_part_form`); and a struct's whose high eightbyte is padding,
  which its copy keeps as padding. A result in registers comes back as parts in rax and rdx or xmm0
  and xmm1 (`tier1_select_result_parts`, eightbyte 1 alone with `HIGH`), and
  one of the memory class through the hidden pointer (up to 4,096 bytes for a
  function's own, which its return copies). An aggregate parameter's value is
  its copy's address, a frame object past the IR's slots that the entry fills
  from its registers and slots, or its stack slots in the incoming area when it
  is there whole, selected as a frame object's address
  (`TIER1_RULE_STACK_ADDRESS`). An aggregate the convention ignores (#141:
  `IGNORE`, an empty struct or one of unnamed bit-fields) is passed and returned
  nowhere: a call copies nothing for it, and a parameter's copy keeps no bytes it
  was given. One of no size that clang passes `byval` (#1879: a record of a
  flexible array member alone, MEMORY) takes an 8-byte stack slot, which holds
  its address as a scalar would: its call keeps its ABI records even with no
  other aggregate (`ir_typed_aggregate_call`, an argument of a general class on
  the stack while a general register is free), since a call without them would
  place that address in the next general register. Refused: a result LLVM
  demotes to a hidden pointer (`DEMOTED`), which only a vector type makes. A call's parts in registers take staging
  slots of 8 bytes, a whole vector register's its bytes'
  (`tier1_select_part_slots`), but a 64-byte vector's, which waits in
  zmm16 and on instead (#141, `tier1_select_part_in_zmm`: Tier 1 allocates no
  register past xmm15, and only an AVX-512F target passes a zmm one), so that a
  call takes all eight of its vector registers' 64-byte vectors. Up to 14
  staging slots use the red zone. If any call needs more, the CFG consumer
  reserves one frame object sized for the largest call (#1450), with a
  whole-function lifetime and the stable frame base, so neither live values
  nor a dynamic outgoing area overlap it. This also supports all eight
  32-byte vector arguments on x86-64-v3, without extra vector registers.
  Every function and call with records goes to the CFG consumer. Before a call's moves, it
  copies the call's aggregates from their addresses into their stack slots, or
  into the staging slots or a 64-byte part's zmm register, whose
  argument registers it loads or moves after the moves. An address
  in a slot, or recomputed, goes through the scratch register, with rax saved
  below rsp, so its use needn't be required: a call may pass more aggregates than
  its first allocation gives its uses registers. The consumer stores an
  aggregate result's parts into the result object, the call's into operand's
  frame object, byte for byte, and points rdi at it for a hidden pointer. A
  return loads the parts staged below rsp (the frame stays out of the red zone),
  or copies the bytes through the hidden pointer that the entry stored, which
  rax returns.
  `test-checker-driver`'s overflow-staging fixture cross-links four, five,
  eight and nine vector arguments, mixed integer aggregate parts, repeated
  values, and dynamic and indirect callers with clang. Both v3 and v4 policies,
  both host paths and both rematerialization choices require Tier 1 under
  `--verify`; fixed callers retain emitted-code certificates.
- Arguments and parameters past the registers (#141): a call takes up to
  sixteen `I32`, `I64` or pointer arguments, and a function up to sixteen such
  parameters (`BUSTER_TIER1_SELECT_MAX_ARGUMENTS`); those past the sixth are in
  the stack's 8-byte slots. A variadic call's unnamed argument has its value's
  type (`tier1_select_argument_type`), since `call_types` holds only the fixed
  parameters' types. The CFG consumer takes every function with one, as follows.
  - A call's argument past the registers needn't be in a register at the call.
    The call stores it into its slot of the outgoing area, before its own moves:
    from its register, from its spill slot through R11, or recomputed into R11
    (`tier1_emit_cfg_stack_argument`). The frame planner reserves the area for
    the calls' most stack bytes.
  - A parameter past the registers is defined at the entry without a register.
    After the register parameters' moves, the entry loads it from its incoming
    slot into its first fragment's register, or through R11 into its own slot
    (`tier1_emit_cfg_entry`). A narrow one is zero-extended as it loads. The
    incoming slots are 16 bytes past RBP where the frame has a frame pointer, a
    realigned frame's among them, and otherwise past RSP's reservation and the
    return address (`tier1_emit_cfg_incoming`).

`I8` and `I16` values occupy canonical zero-extended 32-bit registers and
four-byte spill slots. Their logical memory widths remain one/two bytes.
Signed extension interprets the source sign bit, then normalizes a narrow
destination again; for example, `I8(-1)` to `I16` has register bits `0xffff`.
Narrow arithmetic/comparisons remain unsupported, as do raw `i8`/`i16`
parameter, return and call ABI forms; the C lowering gives a call's narrow
arguments and parameters to Tier 1 as `i32` (#141, the [IR guide](ir.md)'s
"Typed-stream lowering"). `i1` memory and signed-extension forms are also
declined explicitly.

### Proven upper bits (#1219)

Selection keeps two semantic range bounds per integer value: the number of
possibly nonzero low bits and the smallest signed width. Constants, masks,
shifts, bounded arithmetic, extensions and truncations supply the bounds; copies, selects and phis
propagate them. They start unknown, including on loop backedges, and improve
in at most eight sweeps per round. Unresolved cycles retain the full range.
Widths need seven bits each; the spare low-byte bit records a proved I32 value
strictly below `INT_MAX` without adding a scratch column.

A checked induction certificate can then establish a nonnegative I32 loop
index. Every initial edge must supply a nonnegative constant or a run-time value
already independently proved nonnegative, such as a mask or zero extension.
These facts begin with unknown backedges. Every other edge
must add a known nonnegative stride to that same phi. A strict upper-bound
guard may check the sum on the backedge: a walk of at most eight blocks may
follow unconditional bridges to that guard, each bridge with exactly one
incoming predecessor. An alternate entrance refuses the proof. The predecessor
map reuses selection's positions workspace before level ordering. With indirect
control flow, every address-taken label is conservatively ambiguous; marking
them once preserves unrelated unique edges without enumerating labels per jump.
A header guard before the increment also
works when the selected successor has only the header as its predecessor and
the existing verified dominance intervals prove that successor dominates the
increment. This bounds the increment by the admitted index plus its stride,
including the terminating index; merely dominating it with the header does
not prove that the guarded edge executes. One maximum covers all starts and all
admitted backedge values. Every increment must fit signed I32: after a header
guard, its admitted index plus stride; before a sum guard, the common maximum
plus stride. Unsigned guards also require a
proven upper limit at most `INT32_MAX`. Equality, non-strict bounds, unrelated
tests, unknown strides and possible wrapping sums retain their extensions.
Only a complete certificate supplies facts, followed by one additional bounded
propagation round. The candidate scan is masked SIMD with a scalar twin.

A separate phi certificate excludes `INT_MAX` when every incoming edge supplies
an independently bounded value or necessarily tests that incoming value with
a strict upper guard. Arithmetic may wrap before the guard: only the selected
value entering the phi receives the endpoint fact. A guard of the old phi
does not constrain its increment. Constant `INT_MAX` seeds and unsigned guards
that can admit it refuse the certificate.

A sign extension of a single-use register truncation reads the original
register's low bits directly. This removes the intermediate mask without
narrowing a load's memory access or assuming that the original value fits
the truncated signed range.

A separate byte certifies physical register storage. A 32-bit ALU result or
ordinary 32-bit load clears the upper half; an incoming `I32`, a call result and a machine row
make no such promise. Copies and phis need their inputs' certificates. A shift
whose count can be zero, multiplication or division by one, and a truncation
to `I32` can leave a register untouched, so their proofs account for that.

The same byte can instead certify a sign-preserving `I32` representation:
all 64 register bits equal its signed value. A real signed-widening consumer
requests it from an ordinary `I32` load or a byte/word sign extension to `I32`,
through a worklist of at most eight nodes: copies, selects, small phis and
the admitted expression patterns below. The defining load or extension writes 64 bits
once, preserving its exact memory width. Later signed widening is a copy;
unsigned widening still clears the upper half. Copies, selects and phis
propagate only proven representations, and a nonnegative value makes the two
certificates equivalent. A sign-only value records eight-byte storage in its
selected pattern: both native consumers preserve all eight bytes through
spills, reloads and CFG edge moves. Edge moves use the source's width, so an
I32 phi and every incoming value in its connected component share eight-byte
spill storage if any member needs it. Union by rank and path halving closes
arbitrarily long phi chains in the dead positions workspace. This width
obligation grants no upper-bit fact to opaque inputs. Calls and opaque rows
receive no sign certificate. An I32 parameter with two distinct signed widening
roots (directly or through same-type copies) explicitly establishes its signed
representation once at entry. General-register inputs receive `movsxd` before
entry moves or reads; a stack input uses a four-byte `movsxd` load. Unsigned
widening still clears the upper half, and a single signed use stays local.
The dead positions workspace counts these requests before its storage-component
role; this adds no allocation and assumes nothing about incoming ABI upper bits.
For a requested loop state with that strict endpoint, an ordinary I32 ALU
definition can establish signed storage once: perform its wrapping 32-bit
operation, then sign-extend its result. Later widening of the state is a copy,
and its separately proved `+1` can use a 64-bit `lea`. The original loop update
retains its I32 wrapping semantics. A nonzero constant arithmetic right shift
can establish storage the same way. Multiplication, division, dynamic or
zero-count shifts and no-write copy identities do not use this definition form. The selected pattern
records the extra operation, and both emitters preserve its eight-byte storage.

A branch can prove a value nonnegative only at a guarded conversion. The
selected successor must have the conditional block as its only predecessor
and dominate the conversion, using the verified dominator intervals. Signed
lower bounds, equality to a nonnegative value and unsigned upper bounds below
the sign bit supply such facts. The source must independently have real
zero-upper-bit storage. A separate use annotation removes that conversion;
the source's unguarded uses retain their signed extensions. Alternate
entrances and address-taken labels refuse the proof. The pass keeps one
conservative guard per value and reuses positions and pending workspace.

With both facts, a nonnegative signed extension becomes a zero extension,
and a zero extension of already canonical storage becomes a coalescible copy.
A truncation of a value already in the unsigned destination range is a copy;
sign extension can bypass a truncation of an already sign-extended value.
Successive sign extensions read the original narrow value directly, including
a folded signed load through an intermediate integer promotion.
A single-use ordinary load followed by sign extension becomes one exact-width
`movsx`/`movsxd` load, under the existing memory-order and address-fold rules.
Volatile and atomic accesses retain their separate access.
An exact-width store can read the original value of a single-use truncation:
the memory instruction itself discards the unwanted upper bits, preserving
the same address, byte count and observable access.
For a signed 32-bit offset followed by extension to 64 bits, selection can
combine a single-use chain of constant additions/subtractions into one 64-bit
`lea`. Two signed fields that each fit fewer than 32 signed bits also add
without I32 overflow; their sum and walked constants can use one 64-bit `lea`.
Every intermediate operation must be proven nonoverflowing and the remaining
operands must have physical sign-preserving storage, including nonnegative
canonical values. An unbounded
32-bit subtraction retains its extension and wrapping semantics.
For a single-use expression followed by signed widening, bitwise operations,
NOT and constant arithmetic right shift commute with sign extension when the
inputs already have that representation. Negation also does so when its input
needs fewer than 32 signed bits. A difference of two values each needing fewer
than 32 signed bits, or a product whose signed widths sum to at most 32, also
fits without I32 overflow and can operate directly on their signed 64-bit
representations. A positive power-of-two multiplier adds only its shift to
the input's signed width; an immediate multiply keeps its constant folded.
The native expression matrix checks negative
fields and constants, full-width signed loads, poisoned ABI bits, and wrapping
addition/negation controls that must retain their extensions.

A byte or word truncation of a single-use I32 add, subtract, negation or NOT can
instead use a byte or word ALU operation when its inputs already fit that
unsigned width and have physical zero storage. The initial 32-bit copy, when
needed, preserves those zero upper bits; the narrow operation wraps only the
low result and leaves the rest zero. Both binary inputs must satisfy the proof
because commutative register selection can exchange them on an alias. This
removes the separate truncation while preserving canonical narrow storage.

The bounds occupy two scratch bytes per node and the storage certificate one.
The independent range seeds are classified in 32-node SIMD batches with masked
tails and an exact scalar twin; dependency propagation reuses bounded ordered
sweeps on both selector paths. `test-tier1-select` checks poisoned ABI upper
halves, signed boundaries, zero-count shifts, identity operations, retruncation,
volatile and exact-width loads, cyclic phis, and every tail through 65 nodes
against protected pages. The bounds do not assume an ABI extension or infer a
loop's numeric range from its initial value alone. `test-tier1-emit` executes
positive and unsafe induction near-matches through both selection/allocation
paths with constrained registers and independent unsigned-bit arithmetic.
Mixed signed/unsigned widening tests keep eight loaded values live before
either use, require real three-register spills, check the stored results
independently, and put exact-width input at a protected page boundary. A
volatile `I32` load checks the conservative representation.
Mixed signed, positive, volatile and opaque phi inputs check uniform spill
widths without inventing sign facts. Byte/word wrapping and guarded conversion
fixtures observe the complete 64-bit result, including poisoned ABI inputs,
negative bypass paths and unsafe unsigned bounds.

Comparisons used by a branch alone do not produce `setcc`. Their comparison and
conditional jump are consecutive records. A comparison producing a value uses
canonical 32-bit storage and `setcc`. An independent destination is cleared with
`xor` before `cmp`/`test`; an aliased input is read by the comparison first, then
`mov r32, 0` clears the destination while preserving the flags. A floating
comparison's result is in the general class and its inputs in the vector class,
so it can always clear the result before `ucomis`. Its parity combination for
unordered equality/inequality touches only the already canonical low byte.

The body uses only RAX, RCX, RDX, RSI, RDI and R8–R10. R11 is reserved for
operand-overlap repairs. Parameters must be an entry-block prefix, with up to
six integer/pointer parameters in the System V registers RDI, RSI, RDX, RCX,
R8 and R9. Their intervals start at function entry, because parameter nodes
emit no late copies. This emitter has no frame or callee-saved-register changes.

Calls, phis, switches, labels and indirect jumps, variadic definitions,
floating values, byte/word arithmetic, dynamic shifts, division/remainder,
stack/symbol addresses, spill fragments and rematerialized fragments are
explicit unsupported/allocation results in `tier1_select_emit`:
it has neither call preservation nor the edge moves a phi needs.
No semantic error silently falls back to another tier. The separate
`tier1_emit.c` consumer described below handles allocated spill fragments
without weakening that API's checks.

## Producer, plan and allocation handoff

`tier1_select_plan(function, dominance, output, scratch, vector, result,
error_node)` returns `Tier1SelectError`. It verifies its input first, by its
path's IR verifier: `ir_verify` on the vector path, `ir_verify_scalar` on the
scalar one. A cross-block value use requires the supplied dominator intervals;
structural verification alone is insufficient.
`tier1_select_plan_verified`, with the same arguments, gives the same plan
without the verification: it takes a function its caller has verified with
those dominator intervals, as the C driver's `--tier1` takes `ir_optimize`'s
result, which `ir_optimize` verifies before it returns (`c_check_tier1.c`). On
the frozen `sqlite3.c`, the verification was a quarter of selection's cycles
(#141). The selector's tests plan their functions both ways, and the pipeline
test plans each optimized function as the driver does and checks it against the
verifying plan, field by field. A value defined in a node's own block must
precede the node in physical instruction order. With dominance supplied, a value
of another block may follow it: a computed goto's dispatch block, which the
lowering puts last, defines phis that the labels' blocks read (#789). A phi's
operands may follow it too (a loop's back edge). An integer phi
selects `TIER1_RULE_PHI` (#140): no records and no effective uses, since its
operands are uses on its block's incoming edges, which the CFG consumer's edge
moves read (`tier1_emit_cfg.c`, in the [pipeline guide](tier1-pipeline.md)).

The borrowed function remains immutable and alive until emission finishes.
Input, result and scratch storage must not overlap unused arena capacity;
output and scratch are separate arenas. A plan can precede other retained
objects in the same output arena. Failure restores output and scratch lengths
and leaves the result view unchanged. `error_node` identifies a node when
available. There is a bound of 2^20 input instructions, before multiplication
or allocation; counts and capacities cannot silently wrap.

`Tier1SelectPlan` retains:

| Field | Producer/consumer contract |
| --- | --- |
| `function` | Borrowed, original `IrFunction`, preserving node and block IDs |
| `patterns[n]` | Compact selected-rule metadata, consumed together by the emitter |
| `covered[n]` | Byte flag: a covered node emits no instruction and has no allocated definition |
| `effective_use_offsets[n+1]` | u32 CSR offsets by original node |
| `effective_use_values[effective_use_count]` | u32 value IDs actually read by that node's chosen pattern |
| `costs[n]` | u32 working candidate costs; includes children already built, not complete subtree costs |
| `estimated_cost` | Sum of intrinsic costs of uncovered nodes |
| `arena_bytes` | Exact retained arena charge, including alignment and reserved output capacity |

Every uncovered nonvoid node defines its original value ID. Effective reads
occur at machine position `2*i`, definitions at `2*i+1`; incoming parameters
start at zero. Duplicate source operands remain duplicate uses. Covered nodes
have empty use slices. The liveness adapter consumes these columns directly,
then the allocator returns locations for those same IDs.

**Folding extends lifetimes.** For example, folding a shifted index and address
into a later load makes the original base and index live until that load. It is
incorrect to allocate using the original IR's use list and fold afterwards.
The memory regression fixture checks that the effective uses are the base,
index and arithmetic source, with the folded load/address definitions absent.

## Matching and cost comparison

The planner counts original uses and forms single-use expression trees within
each block. Dependencies produce depth columns, then a count/prefix/fill pass
groups ready nodes by depth. All nodes in a depth are ready before the next
depth begins. Candidate generation checks the bounded C patterns and their
operand, type, immediate, memory-order and single-use constraints.

Both host paths cover the integer core by bottom-up rewriting (#1395)
(`tier1_select_burs.c`, #141's design: Pelegrí-Llopart and Graham's BURS, as
iburg and burg build it). Its grammar is a C table of patterns, each
a nonterminal's derivation from an operator and its children's nonterminals, or
from another nonterminal, at the structural cost below; a pattern stands for one
candidate kind of `tier1_select_candidates`, an inner one (an address's piece,
an immediate, a scaled index, a truth value's link) folding into its root's. Its
eighteen nonterminals are a statement, a register, the constants' classes (an
immediate, a lea's scale, a power of two, 3, 5 or 9, a shift's count, zero, a
disp32), a load a consumer folds, a shift a lea scales, addresses without an
index and with one, an index, and the comparisons a branch or a truth value
takes, integer and floating, and their zero extensions.

`tier1_select_initialize` compiles the grammar into state tables once, by burg's
algorithm: a state is each nonterminal's cheapest rule and its cost, normalized
to the cheapest and closed under the chain rules; each operator's child position
projects a child's state onto the nonterminals the operator reads there (its
representer); and each operator's table gives the state of each pair of
representers. The grammar isn't finite-state as it stands (a chain of additions
grows a register's cost against a fold's zero without bound), so normalized
costs saturate at 8, past which a difference no longer picks another rule in
these patterns. `test-tier1-select` prints the generated state, rule, representer,
and 16-bit pool entry counts. The tables are in
the baked section (`baked.c`), so a compile starts with them built; `--tier1`
and `test-baked-tables` build them where the executable isn't baked. The bound
and table sizes are `BUSTER_TIER1_BURS_*`; table generation fails if the
grammar exceeds a table's fixed capacity.

A plan then takes four passes over the function's nodes. On a vector host,
classification, labeling and rule lookup process 16 nodes at a time; `--scalar`
and baseline hosts run the independent `tier1_burs_*_scalar` kernels. Both use
the same costs and deterministic ties: fixed-order grammar expansion and chain
closure replace a rule only for a strictly cheaper derivation, keeping the first
derivation on a tie. Then `tier1_burs_pick` takes its first matching candidate, except
that a walked load, store or copy supersedes its plain candidate. Host ISA
therefore changes how selection runs, not the selected cover or its effective
uses. The passes are:

1. classes: each node's operator, from its IR operation and type and what the
   candidates' rules read of its operands (a constant's class bits, whether a
   right operand is a constant, a load or a shift, whether a load folds into its
   single user past no barrier, whether an address's displacement fits 32 bits);
   and its visible children, those a pattern may fold (a tree child, a constant,
   or a frame object's address in its block or the entry block), any other operand
   being the register leaf's state. A frame object's address, and an address
   whose constant index or offset doesn't fit 32 bits, take the scalar rule;
2. labels: each node's state, a table lookup on its operator and its children's
   representers; vector gathers visit one depth level at a time, while the
   scalar twin visits nodes in order, since every visible child precedes its
   parent;
3. rules: each node's rule for its goal, a statement's or a register's;
4. patterns: the node's candidate of its rule's kind, which
   `tier1_select_candidates`' walks fill (`tier1_burs_pick`). A node of an
   operator the grammar doesn't cover (calls, phis, floating arithmetic, masks
   and the rest) takes maximal munch's, made for every such node in the levels'
   order on both paths, so that a node either path refuses is refused at the
   same place; the covered nodes' patterns are made as the walk below needs them. A rule whose candidate the walks didn't make (a
   walk past `BUSTER_TIER1_SELECT_WALK`) takes munch's too.

The cover is the cheapest the grammar has for each tree, and the grammar has
every candidate the generator makes for the operators it covers, so it is never
costlier than munch's in the grammar's cost. That cost is each pattern's
structural bound on its records, not the records the consumer writes: an
addition of two loads costs 3 as `lea` of the two loads' registers and 4 as a
load and an `add` from memory, whose records are 3 and 2 (`mov`, `mov`, `lea`
against `mov`, `add`). Before #1395, only the vector path used the costed
cover, and the scalar production path used maximal munch. The following
historical measurements compare those different algorithms. At #1190's head,
by `--tier1 --tier1-report`: on the frozen `sqlite3.c` with the
records' flags (`-DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0
-DSQLITE_ENABLE_MEMSYS5 -DSQLITE_ENABLE_RTREE`, its 2,668 functions), 346,741
against the scalar twin's 348,554 (338,926 against 340,658 without the flags,
its 2,602 functions); on Lua 5.4.8's 34 sources with its Makefile's
`-std=gnu99 -DLUA_COMPAT_5_3 -DLUA_USE_LINUX`, 76,475 against 76,681 (76,124
against 76,332 without the two defines). "Remaining acceptance" below compares
the vector path at two earlier heads (#1117's post-merge review).
On Lua 5.4.8's 1,081 functions as `--tier1` plans them, 80,411 of 98,565 nodes
take their rule's candidate and 42 munch's.

A reverse node pass then marks what the code computes:
the nodes with an effect (stores, calls, terminators, volatile loads), the
parameters, the phis and their operands, the nodes nothing uses (an unused load
or division the optimizer kept for the fault it may raise), and each value a
marked node's chosen pattern reads. Every other node is covered, its users'
patterns computing it themselves: a constant as an immediate, a load as a
memory operand, an address as a displacement, a truth value's chain as a
comparison, however deep their walks went (#140). A pattern's `fold_count` is
only maximal munch's rank. An iterative pass publishes the effective-use CSR.
There is no recursive tree walk or speculative input load.

Maximal munch remains a separate test reference and both paths' fallback
outside the grammar. It ranks candidates by coverage and a switch's rule
priority, independently of the BURS costs and tables. The tests execute its
cover and both costed covers against the original IR and require the costed
cover's estimate to be no higher than the reference's. The scalar production
path does not use maximal munch for grammar-covered nodes. Both algorithms
prefer `lea` for multiplication by 3, 5 and 9, and a shift for a power of two
under #1217's immediate-`imul` penalty. A division by a register ranks below
every constant-divisor rule even when the constant has other uses and does not
count among the folds, as the costed cover, whose `TIER1_RULE_DIVIDE` costs 8
against the others' 1, 2, 5 and 7, always did.

The current cost is a **structural upper bound on record count**, including
possible copies and operand-overlap repairs. It is not a measured latency,
throughput, encoded-byte count, or guarantee about final runtime. Allocation
can remove copies from either path. The Zen 5 model (#181, `x86_zen5.c`) has a
row for every case the selector emits, which its tests check, but the cover's
cost doesn't read it: replacing the bound is a decision of its own, to be
measured on generated programs. One rule departs from it: a
division by a constant's reciprocal costs 7, below `div`'s 8, though its records
(up to 13) may outnumber `div`'s, since `div`'s and `idiv`'s latency, which no
count of records sees, is the larger.

Candidate generation for the patterns the walk needs, depth construction and
metadata emission still use scalar loops. The benchmark exposes the difference
between a narrow dependency chain and a wide ready frontier.

## Records and downstream consumers

Call `x86_encoder_initialize`, `x86_semantics_initialize` and
`x86_zen5_initialize` before selection/emission tests. Build the permitted-form
column with `x86_target_forms` or its scalar twin. A zero-feature
`X86_ARCH_BASELINE` target is independent of the compiler host's native ISA.

`tier1_select_emit(plan, value_registers, allowed_forms, output, result,
error_node)` consumes the allocator's direct byte register column. A value
without one stable supported register is rejected. The caller supplies an
allocation verified against the selected-use liveness; this API does not
reconstruct or independently solve allocation.

The immediates' forms are the shared form choice's (#221). Expansion leaves
each record with an immediate empty, with its request (the operation's row,
the value, the live flags and the destination); both emitters collect a
function's requests and choose their forms 32 at a time, one pass of
`x86_choose_forms`'s lanes, before checking the records; on the scalar path
(the plan's, `Tier1SelectPlan.vector`, for this emitter; the options' for
`tier1_emit_function`) each request's form is its scalar twin's, chosen as
the request comes, and none is held (#141). This emitter
checks them in order after the batch, so a node's invalid record or a form
the target refuses is reported before a later node's allocation failure, as
when each node's records were checked as it was selected. Every record either
emitter writes has a form `x86_semantics.c` covers, which the selector, emitter
and pipeline tests check.

`Tier1SelectCode` publishes:

- `records[count]`, the actual encoder records, validated for operand legality
  and generated-target eligibility;
- `cost_ids[count]`, each record's exact `x86_zen5_id`, with zero preserved as
  unknown, and an explicit `unknown_costs` count;
- `node_first[n+1]`, monotonically increasing record boundaries for original
  nodes, including empty ranges for covered nodes;
- ascending `branches[branch_count]` and corresponding `targets`, in record
  indices. Branches are rel8 with displacement zero, directly accepted by
  `branch_sizing_validate` and `size_branches`.

The output reserves at most six records per original node and two branches per
block. Its logical lengths are the returned counts, not the reserved capacity.
Every record a rule's expansion writes goes through `tier1_select_append`, which
checks it against the node's room (`BUSTER_TIER1_SELECT_RECORDS_PER_NODE`): a
sequence longer than the room writes nothing past it and marks the node's records
overflowed, which the three consumers refuse as an invalid function. The
signed power-of-two remainder's six records are the longest, the room's whole.
The tests take a node's sequence to its room and past it with no-ops before its
records (`Tier1SelectPattern.nops`, which the selector never sets):
`test-tier1-select` through the selector's emitter and `test-tier1-emit` through
both consumers, a conditional branch's sequence among them. At the room the
function emits and runs; one record past it, it fails at the node with its
arenas and result as they were.
Input columns and output records require no padding. Only the encoder's own
documented output buffer capacity/padding contract applies when encoding them.
All retained arrays belong to the caller's arena. Emission failure restores its
length and leaves the result view unchanged. Empty functions accept a null
register column and emit no records.

Any later scheduler must preserve node/branch metadata and compare/branch
adjacency, and run before branch sizing. This slice produces no symbolic
references, calls, frame metadata or unwind information.

## Fragment and frame consumer

`tier1_emit_function(plan, options, allowed_forms, output, scratch, result,
error_node)` connects the selected plan to actual native spill code. Its options
choose scalar/vector producer paths, integer-constant rematerialization,
a subset of the selector's caller-saved register mask, and the fixed-frame
KEEP_POINTER, NO_RED_ZONE and PROBE_STACK policies. The default mask is available
through `tier1_liveness_register_mask()`; an explicit subset can test pressure.

The supported control flow is a straight-line physical chain. Every block
before the last must unconditionally branch to the immediately following block.
The last returns, ends in `IR_UNREACHABLE`, or contains exactly one branch to
itself. Forward identity jumps emit no record. The empty terminal sink emits
a real self-jump targeting its block after the prologue; a preceding call is
not assumed to terminate. Terminal paths emit no return epilogue. This admits
the typed lowerer's parameter preheader and terminal suffix. Every other
function, with general joins, nonempty backedges and phis, goes through the CFG
consumer, `tier1_emit_cfg.c` (#140), which allocates over the CFG
(`tier1_cfg.c`) and moves values along its edges.

A division by a constant's reciprocal takes `rax` and `rdx` in either consumer,
through one helper (`tier1_emit_fixed`): `tier1_select_fixed`'s records, with the
other values that live past the node in those registers stored in call-save rows
before them and loaded after. The straight-line consumer gives the registers rows
(`tier1_emit_prepare`) and finds those values by their intervals' ends, as it
finds a call's; the CFG consumer by its intervals at the node's definition phase.
A division and a shift by a register keep their functions on the CFG consumer, as
before the helper (`tier1_emit_cfg_chain`).

Direct nonvariadic calls support up to six `I32`, `I64` or pointer arguments
(64 through the CFG consumer since #1847, those past the sixth on the stack: #141)
and a void, `I32`, `I64` or pointer result. The call pattern keeps the symbol
ID in `immediate`; its three ordinary value holes are empty. Its complete
ordered argument list is the effective-use CSR slice, including duplicates.
Arguments are evaluated by their existing IR definitions exactly once. Calls
remain ordered barriers, and no load is folded across one.

The native consumer captures argument locations before definition-phase
evictions, saves surviving caller registers in dedicated fixed-frame rows,
resolves parallel argument moves using reserved R11, and emits the call. All
argument shuffles use full-width copies, so mixed-width cycles preserve their
original sources. The return value moves from RAX, and stays in RAX where
allocation put it there. Existing allocation keeps the result's destination
distinct from the saved survivors. A survivor is loaded back where something
first needs it, not right after the call, and a later call doesn't store it
again while its row still holds it (#1116, the [allocation
guide](tier1-allocation.md)'s "Values live across calls"). Call preservation
counts are reported separately as `call_stores` and `call_loads`.

An `I32` value's upper half is unspecified in Tier 1's registers, as System V
leaves an `int` parameter's and result's (#1116). Every rule reads an `I32`
operand at 32 bits (an address's index is an `I64`), and a zero extension to
`I64` writes its `mov r32, r32`, even in place, unless selection proves that
the upper half is already zero (#1219). Nothing else needs to clear it:

- an `I32` argument is passed as the argument moves leave its register;
- an `I32` result stays in RAX;
- a copy of an `I32`, and a truncation to `I32`, is nothing in its own register.

Narrow values keep their canonical storage (#981): a narrow result is still
zero-extended from RAX, and a narrow truncation clears in place unless its
discarded bits are already proven zero.

Call-containing functions disable the red zone and use the frame planner's
System V call alignment, including zero-argument calls. The bounded register
save rows cannot share storage with ordinary spill homes. Leaf functions keep
their existing frame policy. Argument moves are a dependency-constrained
operation over at most six registers; the existing scalar/vector liveness,
allocation and frame producers still consume the complete function.

`Tier1EmitResult.references[reference_count]` contains ordinary
`LinkReference` entries: the call instruction index, displacement field,
`BUSTER_LINK_FIXUP_CALL`, the original symbol ID and zero source addend.
The corresponding records carry `BUSTER_X86_FORM_FIXUP`. After branch sizing
and encoding, publish them through `link_add_code_fixups` with the encoder's
instruction ends. The references retain instruction indices, not guessed byte
offsets. The older frameless `tier1_select_emit` API still rejects call plans.

Indirect and variadic calls, arguments and parameters past the registers, and
floating-point and aggregate arguments/results are outside this bounded path.
Calls in general CFGs, loops among them, and functions with arguments or
parameters past the registers or floating ones (#141) go through the CFG
consumer, which saves the registers of values live past each call. Neither establishes acceptance of
source functions.
Unsupported calls and allocation/target/capacity errors publish no partial
result. Correct execution of these component fixtures does not establish
atomic-builtin, full-source ABI or corpus acceptance.

The complete producer/consumer sequence is:

1. Build liveness from the plan's effective uses. Add one required register
   position at each definition and mark every effective source use required.
   Parameters still begin at position zero in their fixed incoming registers.
2. Build integer-constant recipes from actual selected definitions and obtain
   rematerialization marks. Prequery prospective spill-frame requirements, then
   exclude their reserved registers and R11 before allocation.
3. Allocate intervals and plan frame objects for exactly `allocation.spill_values`.
   Object widths are 4 or 8 bytes, with conservative whole-value lifetimes.
   Stack slots use `frame.base_register + frame.offset[spill_id]`.
4. Emit the frame prologue. At each machine phase, emit all register-to-spill
   stores before spill reloads or rematerialized copies reuse any register.
   Capture source registers at `2*i`, then perform definition-phase evictions
   at `2*i+1` before expanding the selected instruction. Destructive destination
   aliases use the existing selector's repairs.
5. Emit the frame epilogue before the final return. Validate every record and
   target form, and retain exact operand-case cost IDs. The returned branch
   columns include frame probe branches, ready for branch sizing and encoding.

Required definition positions mean initial definitions always have registers;
this consumer does not pretend an initially spilled definition was emitted.
After eviction a certified constant can be recomputed without a frame slot.
Every copy conservatively treats flags as live, so zero uses a move. Symbol,
TLS and stack-address recipes are not manufactured by this adapter.
Direct register-to-different-register transitions and odd-phase reloads are
explicit errors; the current required-use allocator does not produce them.

`Tier1EmitResult` retains the actual liveness input, required-location columns,
allocation fragments, frame, records and spill/reload/rematerialization counts.
All failure paths restore both arenas and preserve the result object. Transition
events use count/prefix/fill CSR over bounded machine positions. Their ordered
stores and copies are scalar emission work; the existing liveness, allocation,
frame and rematerialization producers retain their vector/scalar paths. No
compile-time speedup is claimed for adding this consumer.

## AVX-512's masks (#179)

The IR's masks, `m8` to `m64` (#732), are the opmask class's, which the CFG
consumer allocates (the [allocation guide](tier1-allocation.md)'s "The opmask
class"). The selector takes a function with masks, and one with vectors ("Vectors",
below): a mask's operations need no vector register. Its mask rules, each of the mask's width, 1 to 8 bytes:

| IR | Rule | Records |
| --- | --- | --- |
| `const` (and `undef`, any mask: zero) | `TIER1_RULE_MASK_CONSTANT` | `kxor k, k, k` for zero, `kxnor k, k, k` for every bit, else `mov r11d, imm32` (or `movabs r11`) and `kmov k, r11` |
| `copy` | `TIER1_RULE_MASK_COPY` | `kmov k, k`, none where allocation gave both one register |
| `and`, `or`, `xor` | `TIER1_RULE_MASK_LOGIC` | `kand`, `kor`, `kxor` `k, k, k` |
| `not` | `TIER1_RULE_MASK_NOT` | `knot k, k` |
| `bitcast` of an `iN` to `mN` | `TIER1_RULE_MASK_FROM_INTEGER` | `kmov k, r32` (`kmovq k, r64` for `m64`), the integer's register's low bits |
| `bitcast` of an `mN` to `iN` | `TIER1_RULE_MASK_TO_INTEGER` | `kmov r32, k` (`kmovq r64, k`), the bits past the mask's zero, as `iN`'s canonical 32-bit storage has them |
| `bitcast` of an `mN` to `mN` | `TIER1_RULE_MASK_COPY` | as `copy`'s |
| `load`, `store` | `TIER1_RULE_MASK_LOAD`, `_STORE`, and their `_ADDRESS` folds | `kmov k, m` and `kmov m, k`, the address folded as an integer access's, a frame object's among them |
| `phi` | `TIER1_RULE_PHI` | none: its operands are its edges' moves |

The opmask instructions of a width leave the bits past it zero, as the IR's masks
have them, so no record extends a mask. The `bitcast`s are the boundary the typed
stream's conversions between masks and integers lower through (the [IR SIMD
guide](ir-simd.md)'s "Masks from the typed stream"): a `kmovb` or `kmovw` from a
general register reads its low byte or word, so an `i8` or `i16` in canonical
32-bit storage converts with no extension, and a `kmov` into one zeroes the
register past the mask's bits. A node's register is its allocation's
location, class 2's `64 + k`, whose low three bits the expansion takes as k. A
mask's `select`, `vcmp` and `vselect` have no rule and are refused at their node:
`cmov` has no opmask form, and the comparisons and selection need vectors.

Only the CFG consumer takes the rules (`tier1_emit_cfg.c`): the straight-line
consumer sends any function with a mask there, and the uniform-register
`tier1_select_emit` refuses them. There a mask's value is class 2's, allowed k1
to k7, with k0 the class's scratch. Its spills, reloads, edge and split moves are
`kmov` of the mask's width (`tier1_select_mask_forms`): to and from its slot,
whose size is the mask's bytes, and between opmask registers; a cycle goes through
k0, and a slot-to-slot move through k0 too, in two moves, as `tier1_cfg.c` stages
them. No opmask register survives a call, so a call saves every mask live past it
in a frame row of its register's (`Tier1EmitPrepared.mask_save_mask`, the
registers some mask holds across some call) and reloads it after, by `kmov` of
the mask's width; a mask never takes a callee-saved general register. The rows
come after the general registers' call-save rows, then the general class's
temporary slot and the opmask class's, then the frame objects. The records' forms
are the target's to allow, as every record's: `kmovw` and the word forms need
AVX512F, the byte forms AVX512DQ, the doubleword and quadword ones AVX512BW, and
a target without them is refused at emission (`TIER1_EMIT_TARGET`). A width no
mask has gets no form at all (`X86_FORM_NONE`, which no target allows), so it is
refused the same way rather than taking another width's forms.

A mask load's or store's `kmov` is its node's only record, so its last, which the
CFG consumer's frame-object patch rewrites with the frame's base and the
object's offset (`stack_object`, #996). No mask is recomputed (#178): the
masks' rules are none of the recipes' (`tier1_emit_cfg_recipes`), which skip a
mask and any chain through one, and no mask value is marked for the allocator,
whose dead definitions read the marks. A mask recipe would need its own `kxor`,
`kxnor` or `kmov` forms. A division's fixed registers (#141's `rax` and `rdx`)
don't touch a mask, which keeps its opmask register across the sequence; their
saves take the general call-save rows, before the masks' own.

`test-tier1-masks [--seed N] [--print]` (`tier1_masks_test.c`) takes
hand-written IR of masks through this path: each fixture is a function of a
pointer to a 256-byte buffer, and of a count where it loops, that loads masks of
every width from the buffer's first half and stores masks into its second half
(the IR has no mask argument or result, and no conversion between a mask and an
integer). Each is parsed by both of `ir_parse.c`'s paths into the same function,
planned and emitted by both of Tier 1's paths for x86-64-v4, its allocation
accepted by `tier1_cfg_verify`, its records by the target's allow-list and
`x86_semantics.c`'s coverage, and encoded by both encoders into the same bytes.
On a host with every feature of x86-64-v4 (AVX512F, BW, DQ and VL, which the
vectors' fixtures' 128- and 256-bit EVEX forms need, among them), each runs in a
child on 16 random buffers (a quarter all zeros, a quarter all ones), whose
bytes after the run, the ones no store writes among them, must be the shared IR
interpreter's. The fixtures:

- `and`, `or`, `xor`, `not` and copies at the four widths;
- constants: zero and every bit at each width, and others through r11;
- a loop whose two mask phis swap every iteration, a cycle through k0, beside a
  mask accumulated from the buffer by a loaded index;
- ten masks live at once, more than k1 to k7 hold, spilled and reloaded at their
  widths;
- masks live across two calls, and a mask phi across a call in a loop, of a helper
  that sets every opmask register (`kxnorq` into k0 to k7) before it returns its
  argument plus one: generated code beside the function, where the interpreter
  calls a C function that returns the same;
- masks of every width stored into a frame object and loaded back, each access's
  object folded into its `kmov`'s memory operand, the frame's base at the object's
  offset, as an integer access's is (`stack_object`): the test checks each such
  node's `kmov` there;
- masks live across a division by a register, one by a constant's reciprocal and
  a call, beside eight 64-bit constants live across all three, too wide for an
  immediate.

Each fixture runs with rematerialization (as the C driver's `--tier1` compiles)
and without, and in each no mask may have a recipe or be marked to be recomputed;
with it, some of the general values beside the masks must be recomputed, which
the division fixture's constants are.

And the refusals, on both paths: a vector result at selection, a mask's `select` at
selection, and at emission the baseline target (no opmask form) and AVX512F alone
for the four widths (no `kandb`, `kmovd` or `kmovq`), where a function of word
masks alone is taken; and each width's `kmov` forms, none for a width no mask
has. `--print` prints each fixture's records by the vector path, as GNU as reads
them.


## Vectors (#141)

The IR's vectors, `v4i32`, `v8i32` and `v16i32` (#732), are the vector class's,
as the floating values are: xmm0 to xmm14, xmm15 the class's scratch, a node's
register its location's low four bits. Their rules (`TIER1_RULE_VECTOR`, its kind
in the pattern's `scale`) are AVX-512's EVEX forms at the vector's width, 16, 32
or 64 bytes, the xmm, ymm or zmm one (`tier1_select_vector_forms`):

| IR | Kind | Records |
| --- | --- | --- |
| `vconst` of zeros, `undef` | `ZERO` | `vpxord x, x, x` |
| `vconst` of every bit | `ONES` | `vpternlogd x, x, x, 0xff` |
| `vconst` of one lane in all | `SPLAT` | `mov r11d, imm32` and `vpbroadcastd x, r11d` |
| any other `vconst` | `CONSTANT` | its words into the red zone below rsp (a `mov qword` of a sign-extended imm32, or two `mov dword`) and `vmovdqu32` from there (`tier1_emit_cfg_vector_constant`) |
| `copy` | `COPY` | `vmovdqa64` (`movaps` for 16 bytes), none where allocation gave both one register |
| `add`, `sub`, `mul`, `and`, `or`, `xor` | `BINARY` | `vpaddd`, `vpsubd`, `vpmulld`, `vpandq`, `vporq`, `vpxord`: three operands, the destination any register |
| `not` | `NOT` | `vpternlogd x, a, a, 0x55`, the complement of its third operand |
| `vcmp` | `COMPARE` | `vpcmpd k, a, b, imm` (`vpcmpud` for an unsigned predicate) into the mask's opmask register |
| `vselect` | `SELECT` | `vpblendmd x{k}, f, t` |
| `vselect m, op(a, b), w` of an `add`, `sub`, `mul`, shift, floating operation or `lane_op` only the select reads, at its lanes (#1840) | `MASKED_BINARY` | the operation's EVEX form under `k`: `vpaddd x{k}, a, b` merging into `w`, which `x` takes first by a `vmovdqa64` unless it holds it (xmm15 keeping an operand that move would overwrite), or `vpaddd x{k}{z}, a, b` where `w` is a zero vector |
| the same at other lanes ([the SIMD IR guide](ir-simd.md)'s "Lanes") | `BINARY`, `COMPARE`, `SELECT` | the lanes' forms (`tier1_select_lane_forms`, `_compares`, `_blends`): `vpaddb` to `vpaddq`, `vpsubb` to `vpsubq`, `vpmullw`, `vpmullq`, `vpsllvw` to `vpsllvq`, `vpsrlv*`, `vpsrav*`, `vaddps` to `vdivpd`; `vpcmp[u]b` to `vpcmp[u]q`, `vcmpps` and `vcmppd` (the rows' EQ_OQ, NEQ_UQ, LT_OS, LE_OS, GT_OS and GE_OS); `vpblendmb` to `vpblendmq` |
| `load`, `store` | `TIER1_RULE_FLOAT_LOAD`, `_STORE` and their `_ADDRESS` folds | `vmovdqu32` at the width, the address folded as a floating value's |
| `phi` | `TIER1_RULE_PHI` | none: its edges' moves |

A masked operation (`tier1_select_masked_binary`) has four values: the
operation's two operands, the mask and the passthrough `w` (`values[3]`, the
`fourth` hole; `BUSTER_TIER1_SELECT_VALUES`), which the selection's costs and
needs, the effective uses, liveness, both emitters, the code certificate and
the straight-line check walk with the other three. The bitwise operations don't
fold: their forms (`vpandq`, `vporq`, `vpxord`) mask a dword or a qword, not the
select's lanes. Each form the fold can make has a complete Zen 5 row,
`test-tier1-select`'s "every masked operation's form" check; zeroing on
registers takes the unmasked row, as the model has it. On `main_unit_frontend.c`
and `main_unit_backend.c`, compiled by `--tier1 -march=znver5`, the fold takes
31 of 261 `vpblendm` and 82 of 411 into their operations.

Unmasked, a quadword `and` or `or` is the doublewords' bits, and `x86_semantics.c`
models those forms. A function with a vector constant keeps its frame out of the
red zone (`tier1_emit_cfg_frame_flags`). Every function with a vector is the CFG
consumer's. Its vector values' slots are their widths, aligned to 16 at most (their
accesses are unaligned, and a wider alignment would realign the frame), spilled,
reloaded and moved along edges by `vmovdqu32` and `vmovdqa64` at their widths;
the class's call-save rows and temporary slot take the widest vector's bytes,
which the saves around a call move (`Tier1EmitPrepared.vector_bytes`). The forms
need AVX512F, and below 64 bytes AVX512VL, which a target without them refuses at
emission, as any record's.

### At targets without AVX-512 (#1281)

A target without AVX512VL, such as x86-64-v3, has no EVEX form of a 16- or 32-byte
vector, and no register past xmm15. There the CFG consumer
(`Tier1EmitPrepared.vex`, from the target's allow-list: whether it allows
`vmovdqa64` of an xmm register) takes VEX forms of AVX and AVX2 for them
(`tier1_select_vector_forms` and `tier1_select_class_forms` with `vex`, and
`Tier1SelectHoles.vex`), which name xmm0 to xmm15 alone, as the class's values
are:

| Kind | Records |
| --- | --- |
| `ZERO` | `vpxor x, x, x` of the xmm register, which zeroes a ymm one's upper half too |
| `ONES` | `vpcmpeqd x, x, x` |
| `SPLAT` | `mov r11d, imm32`, `vmovd x, r11d`, then `vpshufd x, x, 0` (16 bytes) or `vpbroadcastd y, x` (32 bytes) |
| `CONSTANT`, loads, stores, spills, reloads, call saves | `vmovdqu` at the width |
| `COPY`, edge moves | `vmovdqa` of a ymm register (`movaps` for 16 bytes) |
| `BINARY` | `vpaddd`, `vpsubd`, `vpmulld`, `vpand`, `vpor`, `vpxor`, and at other lanes AVX's and AVX2's (`tier1_select_lane_forms`' VEX column: no 16-bit shift and no 64-bit arithmetic right shift, which VEX hasn't) |

At a target with AVX but not AVX2 (Sandy Bridge's, `Tier1EmitPrepared.avx2`
from the allow-list's `vpcmpeqd` of a ymm register), a ymm register's integer
forms are AVX2's, so a 32-byte vector's bitwise operations take the packed
singles' `vandps`, `vorps` and `vxorps` (the same bits), its ones `vpcmpeqd x,
x, x` and `vinsertf128 y, y, x, 1`, and its splat `vmovd`, `vpshufd x, x, 0` and
`vinsertf128 y, y, x, 1`: no record reads the register's old bits as floats,
which a signaling NaN or a denormal among them would raise MXCSR's flags for; its additions, subtractions and multiplications are AVX2's alone,
which the decoder admits only where the target has them.

VEX has no ternary logic, no compare into a mask and no blend under one: a target
without AVX-512 has no masks (their types still refuse there), and the IR's
`not`, `vcmp` and `vselect` come of EVEX rows alone, which it has none of. Its
typed stream's rows are VEX's (`c_check_program_vectors_baseline.c` and the
builtins'): the 32-bit integers' addition, subtraction, multiplication and
bitwise operations decode into the IR's own operations by their VEX forms
(`ir_simd_typed_operation`), and the others, a comparison or a complement among
them, are machine rows. The lowering admits a 16- or 32-byte vector where the
target has AVX512F and VL, or else AVX (`ir_typed_vector_moves`: the features of
the moves Tier 1 then takes), and a 64-byte one with AVX512F. A vector C passes or returns by value is an aggregate
to the lowering, a pointer to its bytes, in a vector register whole ("By-value
aggregates"); an IR function whose own parameter or result is a vector value has
no convention's place of Tier 1's and is refused at selection
(`tier1_select_vector_signature`), as is a call with a vector argument or
result. A vector local the checker initializes through its address (as C's vector
locals) stays in its frame object, as a 128-bit integer's does.

`test-tier1-masks` takes five fixtures of vectors too, natively against the IR's
interpreter on both paths, with and without rematerialization: every operation
and all ten predicates at each width; constants of every kind; a loop whose vector
phis swap, a cycle through xmm15; eighteen 64-byte vectors live at once, spilled
and reloaded; and vectors of each width live across two calls of the helper. And
four emitted for x86-64-v3 (#1281), every record one that target has: the
operations and constants of every kind at 16 and 32 bytes; a loop whose 32-byte
phis swap through xmm15; seventeen 32-byte vectors live at once, spilled and
reloaded; and vectors of both widths live across two calls. One more is emitted
for x86-64-v2 with AVX, Sandy Bridge's target: 32-byte bitwise operations and
constants of every kind by AVX's forms, beside 16-byte arithmetic.

### At targets without AVX (#141's SSE targets)

At the x86-64 baseline and x86-64-v2, which have no AVX, the vector registers
are xmm0 to xmm15 and every form is legacy SSE's, whose destination is its first
operand. The lowering admits a 16-byte vector there with no feature past the
target's (`ir_typed_vector_moves`), and still refuses a 32- or 64-byte one, which
such a target computes in halves (Tier 0's), and an SSE carrier, whose zeroing
moves are AVX's. The decoder takes the typed stream's legacy rows
(`c_check_program_vectors_baseline.c`'s `op v0, v1`) into the IR's operations of
their lanes: every element type's `paddb`/`w`/`d`/`q` and `psubb`/`w`/`d`/`q`,
`pmullw` and SSE4.1's `pmulld`, `pand`, `por`, `pxor` and the packed singles'
`andps`, `orps` and `xorps`, and `addps` to `divpd`; the comparisons (`pcmpeq`
and `pcmpgt` sequences, `cmpps`), complements and splats stay machine rows. The
CFG consumer's encoding level is legacy SSE's there (`Tier1EmitPrepared.sse`,
from the target's allow-list: whether it allows `vmovdqu` of an xmm register;
`BUSTER_TIER1_ENCODING_SSE`, `Tier1SelectHoles.sse`):

| Kind | Records |
| --- | --- |
| `ZERO`, `ONES` | `pxor x, x`, `pcmpeqd x, x` |
| `SPLAT` | `mov r11d, imm32`, `movd x, r11d`, `pshufd x, x, 0` |
| `CONSTANT`, loads, stores, spills, reloads, call saves | `movups` |
| `COPY`, edge moves | `movaps` |
| `BINARY` | the lanes' legacy form (`tier1_select_lane_forms`' SSE column) after `movaps x, left` where the destination isn't the left operand: a commutative integer operation's operands swapped where the right one is in the destination, and any other's right operand copied into xmm15 first, a floating one's too, so that a NaN result is its first operand's, as the IR's is |
| `NOT` | `pcmpeqd x, x` and `pxor x, operand` where the operand is elsewhere; `pcmpeqd xmm15, xmm15` and `pxor x, xmm15` where it is the destination (#1425's review) |

`COMPARE` and `SELECT` have no SSE records: their masks are opmasks, which no SSE
target has. They take AVX-512's forms, which the target's allow-list refuses at
emission (`TIER1_EMIT_TARGET`), as at a VEX target, so a function the decoder
didn't give them (an IR client's) goes to Tier 0 rather than lose them.

A machine row's roles are placed below 16 as at a VEX target, moved by `movaps`.
`test-tier1-masks` runs two fixtures natively, at x86-64-v2 and at the baseline
(without `pmulld`), of every lane's legacy operation in every operand order,
against the interpreter; their records must all be the target's (its
allow-list). A third, at the baseline, complements a vector into another register
and in place, and a comparison and selection there must be refused at emission;
`test-tier1-select` checks those records directly. `test-checker-driver`'s SSE fixture compiles C's operators on
16-byte vectors of every element type by `--tier1 -march=x86-64` and
`-march=x86-64-v2`, every function Tier 1's, by both paths and with and without
rematerialization, and matches clang -O0's output.

## Machine rows (#141)

A row call ([the IR guide](ir.md)'s "Machine rows") is `TIER1_RULE_ROW`, its
`immediate` the row's index and its operands the call's arguments, the node's
effective uses, each in a register (the CFG problem's uses of it are required,
as any operand's): a row call isn't a call of a function's
(`tier1_select_function_call`), so nothing is saved around it, no argument moves,
and it makes no frame a caller's. The selector takes a row Tier 0 places
(`tier0_operation_valid`, which needs x86_semantics.c's tables), whose vector
and floating roles fit: a row whose records name vector registers in EVEX forms
alone has its vector roles in zmm16 and on, which no value takes, and its floating
roles past them below 32; a row with a VEX or legacy form's vector register has
its vector roles below 16, each that doesn't stay in place in a free register;
and a row that names a
floating role in a VEX or legacy form's field (F16C's conversions,
`tier1_select_row_low_floating`, #1281) has its floating roles below 16 too, its
vector roles and its floating ones at most 16 (`tier1_select_row_supported`). Its narrow integer result isn't canonical
(`tier1_select_canonical`): its field's bits past the class are the row's. Only
the CFG consumer takes it (`tier1_emit_cfg_row`), which places the row at the
node's definition phase as Tier 0 does (`Tier0Placement`, `tier0_operation_place`)
but for its vector roles, which it places one at a time (#1283,
`tier1_emit_cfg_row_place`), so that most rows emit their own records alone:

| Role | Register |
| --- | --- |
| `VECTOR(0)` of a vector result | the destination, where nothing else there is read after the role's first write (no value live past the node, no floating operand, and no other vector operand that stays there whose role a later record names, or the first-writing one where its destination can't be a source: a gather's, a complex multiplication's), and every record that writes it is a VEX or EVEX form's (which zero the register past their field; one wider than the result is followed by a move of the destination into itself at the result's width). The first operand is copied in first (no other operand may then be there); or, where the first write is a pure one (no tied operand, no merge into the rest of the register, no merging opmask) and nothing reads the operand past its width up to it, it is read in its own register up to that write, which writes the destination: the two-operand forms' `op V0, V0, V1` with the result in a dying second operand's register is `op d, a, d` |
| other `VECTOR(j)` of a vector operand | its own register where nothing reads it past its width up to the role's first write (whose bits the move at its width would have zeroed) and no record writes it, or they write it by VEX and EVEX forms alone and it dies at the node, no other operand in its register and not the destination the result is written in |
| other `VECTOR(j)` | zmm16 + j, which no value takes and an EVEX form names, or for a row with a VEX or legacy form's vector register, and at a target without AVX512VL a row with a vector narrower than 64 bytes or any row where there is no AVX512F (#1281; a 64-byte EVEX row at Knights Landing's AVX512F without VL keeps zmm16 and on), a free xmm register below 16 (xmm15 among them, the class's scratch), each taken as its role is placed rather than a run of them, which register pressure could leave none of, and none the destination where the result is written there; a vector operand copied in at its width first (`vmovdqa64`, or `vmovdqa` at a target without AVX512VL), a vector result moved into the destination at its width last. An SSE carrier (#1280) is copied and moved zero-extended (`tier1_select_carrier_move`, by VEX's `vmovq` or `vinsertps` at a target without AVX512VL): its value's register may hold anything past its bytes, which every vector field reads past, so it never stays in its own register |
| `FLOATING(j)`, `FLOATING_RESULT` | its operand's own xmm register where no record writes it, else the zmm registers past the vector roles' (moved there first); the result's past those, the first floating operand's too (copied there first), moved into the destination last. For a row that names a floating role in a VEX or legacy form's field, and every row at a target without AVX512VL (#1281), all below 16: a written operand's a free xmm register no vector role takes (nor the destination a vector result is written in), from the vector roles' pool, and the result's the destination itself where no other operand is there, else such a free register; each copy `movaps` |
| `INTEGER(j)`, `MASK(j)` | its operand's own register where no record writes it (`tier1_select_row_written`, by x86_semantics.c's effects of each field, and a gather's or scatter's decoration mask), else a free register of its class, moved into first |
| `INTEGER_RESULT`, `MASK_RESULT` | the destination, where no other operand is; else a free register, moved into the destination last. As Tier 0 places them, it is also its class's first operand's register (that operand copied into it first), so that a row may read the result as that operand, or the operand as the result once written: `MASK_RESULT` the first mask operand's, `INTEGER_RESULT` the first operand's of Tier 0's integer stack where that is an integer (a vector's place on that stack is no general register: after one, `INTEGER_RESULT` shares no operand's register, #1284) |
| `SCRATCH(k)`, `MASK_SCRATCH(k)` | free registers |

`tier1_select_row_vectors` reads what the records do with each vector role from
x86_semantics.c's effects: which they write and by which family, how wide each
role is read up to its first write and written, which record writes it first and
whether purely, which names it last, and which records' destinations can't be a
source. `--tier1-report` counts the rows and the moves around them
(`machine_rows`, `row_moves`): the machine rows' fixture's unit below took 150
moves around its 58 rows by Tier 0's placement and takes 18 (13 by the scalar
path's allocation).

The free registers are the caller-saved general registers (rax, rcx, rdx, rsi,
rdi, r8, r9, r10 and the scratch r11) and k1 to k7 that hold no value live past
the node, no operand and not the destination. The opmask registers a row names by
number (its own scratch: a row with a mask operand or result names none) are
renamed to free ones too (`tier1_select_row_rename`), but a pair's (VP2INTERSECT's
k and k + 1), which must be free where it is. Short of them, the row is refused as unsupported
(`TIER1_EMIT_UNSUPPORTED`), and its function is Tier 0's: the CFG consumer keeps a
node's own error rather than the block's owed call saves' (`TIER1_EMIT_INVALID`,
which a refusal mid-block left owed). A move into place never
overwrites another's source: its destination is free, a vector role's or the
node's destination with no other operand in it. Each row
node takes `BUSTER_TIER1_EMIT_ROW_RECORDS` records past its share. The free xmm
registers below 16 are those that hold no value live past the node and no operand.

The emitter publishes each row's placement under `Tier1EmitOptions.certify`
(`Tier1RowPlacement`, `Tier1EmitResult.row_placements`, by node): each role's
register as the records name it, VECTOR(0)'s early register up to the result's
first write where the first operand is read in its own register (#1283), and the
renamed opmask registers. The code's certificate takes it as a claim (#1284, [the
certificate guide](tier1-certificate.md)'s "Machine rows"): it places the template
by it itself and compares, refuses roles in one register while both hold values
that Tier 0's placement keeps apart, and proves each operand is at its role's
register when the row reads it; the staging copies and the borrowed masks' saves
are moves at the node's definition phase (`TIER1_ORIGIN_MOVE`), the row's records,
its result's move and the reloads its node's (`TIER1_ORIGIN_NODE`). A row whose
records read or write a role's register implicitly, or of a form x86_semantics.c's
effects don't cover, leaves its function uncertified (`TIER1_CERT_EMITTED_VECTOR`).

An SSE carrier (#1280), a vector of 4 or 8 bytes, is an f32 or f64 in the IR
([the IR guide](ir.md)'s "Machine rows"), so it is the vector class's value as
those are: their loads and stores (`movss`, `movsd`), constants (`xorps`, or the
bits through the scratch register and `vmovd` or `vmovq`), copies, phis and
spills move its bits alone, and no floating operation reads it, since only a row
does. A row sees zeros past its bytes in its role's register, as Tier 0's row
does (Tier 0 reloads every operand by `vmovd` or `vmovq`). Rows rely on those
zeros: a row of no records, the intrinsics' narrowing shuffle, has its wider
operand's register as its result, and a narrow vector's floating operation at a
16-byte form computes its other lanes too, which must raise no exception (an
sNaN there raises invalid). A carrier's own register needn't have the zeros: a
`param.f64` or a call's `f64` result is the convention's, whose bytes past the
double are undefined, and Tier 1 copies it by `movaps`. So each carrier operand
moves into its role by `tier1_select_carrier_move`, which zero-extends it:
`vmovq` for 8 bytes, and for 4 bytes `vinsertps` of element 0 with elements 1
to 3 zeroed (immediate 0x0e), a shuffle that raises no exception. That move
replaces the whole register's copy that staging takes anyway, so it costs
nothing. A row's carrier result moves into its destination the same way, also
at no cost. So a carrier that a row, a load or a constant makes has the zeros
in its own register too, but one from a parameter or a call doesn't. A
placement that leaves a carrier operand in place must zero-extend it unless its
producer is one of the former.

`test-checker-driver`'s machine rows' fixture compiles a unit of x86-64-v4's
vector builtins and vector operators on bytes, words and quadwords by `--tier1`,
every function Tier 1's, by both paths and with and without rematerialization,
and runs it with clang -O2's other unit, against clang -O0's build of both: compares
into masks of every width, masked and zero-masked operations and blends,
permutes and shuffles, shifts by immediates, by counts in general registers and by
vectors, masked loads and stores, a store of no result and a compress, inserts,
extracts and widening conversions of every width, broadcasts, reductions and
extractions into general registers, and a loop whose row results cross its back
edge. Its unit takes at most a move into or out of place for every two rows, by
each path and with and without rematerialization (#1283). Its SSE carriers' fixture (#1280) does the same for vectors of 4 and 8
bytes:
- the intrinsics' widening conversions of each width (`cvtepu8`, `cvtepi8`,
  `cvtepu16`, `cvtepi16`);
- `loadl` and `storel`, `loadu` and `storeu` of 4 and 8 bytes, `move_epi64`, and
  `loadl_pi` and `storel_pi`;
- the `movq` forms;
- MMX's `__m64` operations, by value to and from clang's unit;
- GNU C's operators on vectors of 4 and 8 bytes;
- a signaling NaN's and -0's bits as constants;
- a loop whose carriers cross its back edge;
- under `FENV_ACCESS`, a product of two-float shuffles whose vectors' other
  lanes would overflow. It raises no exception only where the zeros hold
  (without `tier1_select_carrier_move`'s zeroing it raised overflow and inexact).

`test-tier1-masks`' "SSE carriers with dirty upper lanes" fixture takes the
carriers no row makes. A `param.f64`, a `param.f32` and a call's `f64` result,
whose registers hold signaling NaNs past their bytes (the native entry's xmm0
and xmm1, the helper's `vpbroadcastq` of its result), are each read whole by a
row of no records. Each must read zeros past its bytes; with a whole register's
staging copy it read the NaNs.

Its half-precision check builds F16C's conversions at x86-64-v3 by both
paths: `_cvtsh_ss`'s row names a float in a VEX form's XMM field, which Tier 1
places below 16 (#1281), every function Tier 1's, and the program runs as clang
-O0's does. With `--verify`, the fixture's every function's code is certified
(#1284).

Its VEX fixture (#1281) compiles a unit of x86-64-v3's AVX2 and F16C builtins and
vector operators the same way, every function Tier 1's, by both paths and with and
without rematerialization, against clang -O0's build: the 32-bit integers'
operators (the IR's own operations by their VEX forms, and the comparisons,
shifts and complements as machine rows) on 16- and 32-byte vectors; the operators
on words, bytes, quadwords, floats and doubles; AVX2's shuffles, blends, permutes,
shifts, unpacks, maxima, tests and movemasks, lane extracts and inserts, widening
conversions, masked loads and stores and gathers; F16C's scalar and vector
conversions and FMA's; constants of every kind; a loop whose vectors cross its
back edge and live across a call; and more vectors live at once than xmm0 to
xmm14. A floating expression clang would contract into an FMA (its
`-ffp-contract=on`, even at -O0) is split into statements. `_mm256_set1_epi8`'s
32 stack arguments are refused at every target, so the fixture takes other
builtins in its place; its SSE carriers (#1280, `_mm_cvtph_ps`'s and
`_mm256_cvtepu8_epi32`'s operands) move by VEX's `vmovq` and `vinsertps`. Its AVX fixture
does the same at `-march=sandybridge` (AVX without AVX2): 32-byte bitwise
operators and constants, 16-byte arithmetic and AVX's float builtins.

`test-tier1-masks` takes hand-written IR of machine rows too, natively against a C
model of each (the interpreter has none of x86 records) by both paths, with and
without rematerialization: rows beside seven word masks live in k1 to k7, one
naming k1 and k2 by number and one taking a mask scratch register, which borrow
live masks' registers and reload them; rows that read their integer and mask
results as their first operands, as Tier 0 places them; a row of no records, its
result its operand; an EVEX floating row, its result read as its first
operand; its VEX twin (#1281), for x86-64-v4 and for x86-64-v3, its floating
roles below 16; and vector rows placed one role at a time (#1283), with the moves
around them counted by each path: a two-operand form's, its result in a dying
second operand's register; a tied operand's; a merging opmask's; an operand
written and dying under an integer result; a VEX form's /is4 blend in the run
below xmm16; a narrow result written at a wider width and a narrow operand read
at one; and a widening conversion. Two more take the forms whose destination
can't be a source's register (#1283's review): gathers whose source and index
are one value dying at the row, and AVX512-FP16's complex multiplications whose
factors die at it, the allocation giving the result such a register. The test
checks that every record is valid and that each such destination is none of
its sources, and runs them against a model, the complex multiplications only
where the host has AVX512-FP16 (not the 9700X), on a forms table that allows
that form, which no target of Tier 1's does yet. For x86-64-v3 it takes a fixture
of rows too: a VEX row of 32-byte vectors, one live past it; a floating row whose
result is read as its first operand and one that writes its second floating
operand first, both operands live past them; and a 16-byte vector's row. Its
random mask programs whose rows the decoders refuse run as machine rows against
the generator's model. Every row's code is certified, and its corruptions refused
(#1284, [the certificate guide](tier1-certificate.md)'s tests): a borrowed mask's
reload left out, a staging copy onto a live register, a claimed placement whose
roles share a register Tier 0 keeps apart or whose opmask register named by number
is k0.

## Asm bodies (#1839)

A body call ([the IR guide](ir.md)'s "Asm bodies") is `TIER1_RULE_ASM`, its
`immediate` the body's index, of no result: its block argument is the
`stack_addr` of a frame object, which no register holds (no effective use, so the
address is computed only where something else reads it), and its register
operands (#2144, `IrAsmBody.passed`) are its effective uses, each in a register of
its class, as a row call's operands are (`tier1_select_asm_skipped` leaves the
block out). The selector takes a body whose block is a `stack_addr`, which touches
none of rsp, rbp and r15, and whose register operands are an `i32`'s, an `i64`'s
or a `ptr`'s in a general register but the scratch r11, or a `float`'s or a
`double`'s in xmm0 to xmm14, each register once (`tier1_select_asm_supported`).
Like a row call it is no call of a function's: no outgoing area. Only the CFG
consumer takes it (`tier1_emit_cfg_asm`), which writes, at the node's definition
phase:

1. the stores of the values that live past the node in the general registers the
   body touches into their call-save rows, a callee-saved register's among them
   (rbx for `cpuid`, where a value crosses a call there), and of the floating
   values in the xmm registers it touches into theirs, as a call saves them;
2. its register operands' moves into their own registers
   (`tier1_emit_cfg_asm_operands`), from where they are at its use phase: one
   parallel move per class through that class's scratch register, as a call's
   arguments' (`tier1_emit_call`);
3. the body's records, each that addresses the operand block
   (`BUSTER_TIER0_ASM_BLOCK`) addressing the block's frame object from the frame's
   base register, its own branches the function's branches to their records (one
   to the body's end the first restore's), sized with the function's;
4. the loads back.

The general registers it touches are its fixed registers
(`tier1_select_node_fixed`), so that the frame gives them call-save rows where a
value crosses the body in one (`tier1_emit_cfg_saves`, a callee-saved register's
too where the function calls), owed values are taken back before it, and its rows
are dropped after it, as a fixed rule's sequence has them; the xmm registers' rows
are a call's. Its frame is a calling function's (rsp aligned at the template, no
leaf frame), and the prologue saves the callee-saved registers it touches
(`Tier1EmitPrepared.asm_callee`), as clang's saves `cpuid`'s rbx. The template's
own records aren't bounded by the target's forms (as the assembler takes any
instruction whatever `-march` says). The values that cross a body are kept out
of the general registers it touches where the allocation can
(`tier1_emit_cfg_callee`'s second allocation, which takes them for values that
cross calls too): such a value may take a callee-saved register, which the
prologue saves once, rather than a register the body would save and restore
around itself each time it runs; a phi's web keeps out of every body's registers
any member crosses. If that allocation fails, the first stands and the saves
stay. The code's certificate (`--verify`) places and compares the body's records,
its own branches among them, reads its register operands in their registers, and
kills the registers it touches ([the certificate guide](tier1-certificate.md)'s "Asm
bodies").

Registers outside `touched` hold no value across the node: the allocator gives
values xmm0 to xmm14 alone (xmm15 is the vector class's scratch within a node,
zmm16 and on a row's or a call's staging within a node), and the x87 stack is
empty after every node (`tier1_emit_cfg_x87`), so a template may use all eight
of its slots. The masks live in the opmask registers its records write
(`IrAsmBody.masks`, from `x86_semantics.c`'s opmask effects) are saved into their
rows around it and reloaded, by kmov of their widths, as around a call; a mask in
another opmask register stays there. `test-tier1-masks`' "masks across an asm
body" fixture runs masks live across records that write k1 to k7.

Some bodies keep their function on Tier 0, which then compiles it whole:

- a template that returns from its function (`IrAsmBody` from
  `Tier0AsmBody.leaves`, which `ir_lower` refuses), whose frame is Tier 0's;
- a body `tier1_select_asm_supported` refuses: a block that isn't a
  `stack_addr`, or a template that touches rsp, rbp or r15.

## Tests and benchmark

`test-tier1-select` checks identical scalar/vector costed plans and emitted
records, plus an independent maximal-munch reference. All three execute in a
child process against the original IR. Its independent arithmetic/IR oracles cover
constant boundaries, aliasing registers, shifts, signed/unsigned comparisons,
selects, addresses, exact memory access widths, volatile loads, intervening
stores and multiply-used loads. Failure tests distinguish invalid IR,
unsupported operations, missing allocation, target exclusion and arena
exhaustion. Empty functions and guarded, unpadded columns cover lengths around
16 and 32 lanes; wide frontiers and dependency chains cover distinct groupings.

The bottom-up rewrite has checks of its own (`tier1_select_burs_test.c`, in
`test-tier1-select`): the tables' invariants (the register leaf's state first,
every state's rule of a nonterminal one that derives it, every map and table
entry a state, each constant class deriving exactly its nonterminals); the
labeling and the rule choice against their scalar twins on 64 rounds of random
columns, with child states drawn from every state, 16-lane tails and lone
levels; the IR's text fixtures (`tests/ir`, `tests/ir/optimize`); and addresses
around a disp32's edges, constant indices and offsets inside and past 32 bits at
every scale, among them pairs past it whose sum fits. On each function
(`tier1_burs_check_function`) the vector classifier's classes and visible
children must be the scalar one's, both kernels' states and rules alike, and
every node of a class the grammar covers must have a candidate. The same check
runs on every function `test-tier1-pipeline` plans and every function
`benchmark-tier1-select-source` selects, outside its time, where a difference
fails the benchmark. `test-tier1-pipeline` also replans each function through
the other host path and compares patterns, coverage, working costs and effective
uses field by field. The independent maximal-munch reference builds children
first, so its `costs` column contains complete subtree costs; the selector test
checks those against the definition worked out from the function alone
(`tier1_select_test_plan_costs`). Both production paths keep the previous costed
path's lazy working costs: when a pattern is built before its children, their
costs are left out. Nothing reads this column after the plan. There is no new
subtree recomputation pass; historically that pass took selection on the frozen
`sqlite3.c` from 222.9 core cycles per IR instruction to 241.3 on the 9700X
(`Tier1SelectPlan`'s header).

`test-checker-driver` compiles the #1395 arithmetic, address and load-add-loop
regressions through `--tier1` and `--tier1 --scalar` for the same baseline
target, with and without rematerialization. Every fixture function must select
through Tier 1, the complete object files must be byte-identical, and each
linked object must print the independent Clang build's output. Baseline-host
builds exercise the scalar production route; native builds exercise both kernel
routes when their host supports the full vector path. A parameter-free leaf
with four volatile loads isolates unrestricted register allocation from phi
ordering. Both production allocators now use the same level-rule policy; the
independent scan remains a test reference ([allocation guide](tier1-allocation.md)).

`test-tier1-select --manifest` writes CSV with separate origins for actual
`plan` emission, direct `expansion` fixtures and test `wrapper` records. Every
record includes form, mnemonic, all memory/register/immediate fields, model ID
and each cost column. Summaries report unknown IDs and missing fields for each
origin separately; UINT16_MAX denotes a missing field. Summaries go to stderr.
This observed emitter inventory is generated independently of the model's case
table. Every record of the plans and expansions must have a complete Zen 5 row
(#181, `x86_zen5_accounts`), which the test checks after the semantics'
coverage; the wrappers' records are the test's own. The emitters' tests
(`test-tier1-emit`, `test-tier1-masks`, `test-tier1-pipeline`) check every
complete function's records the same way.

`test-tier1-emit` executes real pressure plans through liveness, allocation,
frame planning, fragment emission, branch sizing and encoding. Original
32/64-bit fixtures keep up to 600 values live with only three allocatable
registers. Both paths run against an independent integer oracle with stored
values and with rematerialized constants, fixed/red-zone/frame-pointer frames,
and stack probes. Independent allocation verification checks the produced
fragments. Failures cover target denial, scratch-register conflicts, capacity
and unsupported frame policies; a backedge through the entry now goes through
the CFG consumer, after an added entry block. Narrow-value fixtures
check canonical upper bits, exact byte/word accesses beside guard pages,
sign boundaries, real spills and rematerialization. Terminal fixtures check
explicit traps and empty self-loop sinks in bounded child processes, including
a returning helper whose sink must remain a loop. Empty input is accepted.
The separate pipeline tests cover upstream typed-stream lowering and SSA
promotion; neither test suite is the pending full C-program differential corpus.

Divisions by constants' reciprocals are tested at both levels. `test-tier1-select`
plans each of the four divisions at both widths by 52 divisors (small ones, C
programs' usual ones, the ends of 32 and 64 bits, negative ones, and those `div`,
`idiv` and the power rules keep), by both paths, which must pick the reciprocal
exactly where `tier1_select_reciprocal_divisor` does; runs each reciprocal's
records natively from four dividend registers (`rax` and `rdx` among them) into
five destinations (the dividend's own among them) on the dividend's edges, against
its interpreter; checks that the longest sequence fills the room exactly; and that
a divisor whose constant has two uses still takes its rule by both paths.
`test-tier1-emit` (`tier1_divide_test.c`) runs whole functions through
`tier1_emit_function` by both consumers: a straight-line one whose dividend and a
parameter that lives past both divisions arrive in `rdx`, and a loop's digit sum,
by both paths, with constants rematerialized and not, and with every register and
a few, against C's own division; both consumers must save values around the
sequences.

`benchmark-tier1-select [--steps N] [--functions N] [--only scalar|vector]
[--after-scalar MS]` freezes original synthetic IR with a deterministic seed.
Its chain, wide-frontier and live-pressure shapes report selection separately
from the complete backend path: plan, selected-use liveness, required register
positions, allocation, rematerialization recipes, frame planning, fragment and
frame emission, branch sizing and encoding. Scalar rows use the scalar encoder.
Both paths are
validated before timing. Samples alternate order, and report seven-sample
medians of nanoseconds and invariant TSC ticks per IR instruction. TSC ticks
are **not core cycles**. The output also reports actual record/code-byte counts,
unknown model IDs, spill slots, stores, reloads, rematerialized copies, peak
retained arena bytes and the maximum of liveness and transition scratch bounds.
The latter is not a whole-pipeline peak-memory measurement.

`benchmark-tier1-select-source [--only select|select-scalar] [--after-scalar MS]
[preprocessor options] file...` (`tier1_select_source_benchmark.c`, #141) times
selection on real C: every function `-c` compiles of the files, lowered, promoted
and optimized as the C driver's `--tier1` takes it, with its dominance, outside
the time; then `tier1_select_plan_verified` of each, as the driver plans it,
by both paths, in 21 passes that alternate which path runs first, as nanoseconds,
core cycles, instructions and the throttle (PMCx1A0's umask 0x08, #199) per IR
instruction and cycles per typed entry. A function a step before selection
refuses, or either path's selection refuses, is left out of both and counted;
any other failure fails the command, which then reports no time. `--only` times
one path alone, and `--after-scalar MS` runs the scalar path before each of the
vector path's passes. Of the frozen `sqlite3.c`'s 2,668 functions, both paths
select the 2,666 that the driver's `--tier1-report` gives Tier 1 (478,163 IR
instructions).

Run each build, test and measurement on the approved 9700X under its shared
`~/buster-fast-bench/exclusive.sh` lock, with owner/title/link metadata; pin each
benchmark to CPU 2. Use separate fresh processes for sustained `--only vector`
and `--only vector --after-scalar 200`, and compare both against scalar runs.
Local runs are diagnostic only. The current slice supplies the reproducible
measurement path; it does not establish a performance gain.

#141's batched choice and load fold, against main `0dd6fbc3` over six
placements of the benchmark's unit
([the record](https://github.com/buster14a/buster-performance/blob/6441e0df565d3c21ee4413848f75ce2a3927a7d3/docs/measurements/tier1-select-141-2026-10-29/README.md)),
each placement against each of main's as `docs/profiling.md` compares builds:
the plan's medians 1.5% to 2.5% lower on every shape and path, outside the
comparison's floor on four rows (the vector path's three shapes and `wide`'s
scalar twin); the whole pipeline's 0.3% to 2.1% lower, within the floor (one
placement of the head's executable misses its op cache). The fold's barriers
kept inside the plan's pass over the nodes had made the plan up to 10% slower,
37 to 42 instructions more per node, which their pass of their own doesn't
cost. The shapes hold no loads, so these are the costs, not the fold's gains.

## Remaining acceptance

Issue #141's Done-when is met; whether it closes is the owner's call. The 9700X
measurements its Done-when asks for are recorded in
[buster-performance](https://github.com/buster14a/buster-performance/tree/70a6b824ba7917e62968b0baabed847dd7d9fd0c/docs/measurements/tier1-reciprocal-141-2026-11-01),
at #1001's heads and at its merge with main, `cc3387f`. These measurements
predate #1395's scalar production BURS path; they do not measure that change:

- **Programs**, by the driver's `--tier1` (each function Tier 1's pipeline
  takes by Tier 1, the others by Tier 0), against Tier 0, clang -O0 and clang
  -O2: speedtest1 in 223.5 ms against 256.8, 291.0 and 131.8; Lua 5.4.8's
  interpreter, five scripts, in 286.1 ms against 337.6, 376.0 and 167.1;
  c-testsuite's one long program (00040) in 23.6 ms against 25.6, 33.2 and
  12.6. In user-space instructions, `--tier1` runs 4,413 M of speedtest1
  against Tier 0's 5,694 M and clang -O2's 2,010 M, and 8,749 M of Lua's
  scripts against 10,763 M and 4,281 M. Each program's hottest function
  (`sqlite3VdbeExec`, `luaV_execute`) was still Tier 0's then; with floating
  values and blocks' lifetimes (#141) both are Tier 1's, as every function of
  `sqlite3.c` but the two 128-bit multiplies is (and those two too since the
  128-bit integers' split, #141).
- **Selection's cycles per IR instruction**, `benchmark-tier1-select-source` on
  the frozen `sqlite3.c`, since the bottom-up rewrite (medians of five processes
  of both paths interleaved, CPU 2): 225.0 by the vector path and 277.2 by the
  scalar twin, against 261.7 and 275.2 at `d1e8a5f` before it, on its 2,666
  functions (478,163 IR instructions); the vector path's instructions per IR
  instruction 503.2 against 690.0, the twin's 751.6 against 747.3. On #1001's
  2,181 functions they were 243 and 253 once the plan stopped verifying the
  optimizer's verified result
  ([record](https://github.com/buster14a/buster-performance/tree/0aa27457a95dd2ee4e9b5ac26e67429c2490380b/docs/measurements/tier1-plan-verification-141-2026-11-01)),
  and 338 and 340 with that verification. `--tier1 -c` of the frozen
  `sqlite3.c` took 475.2 ms against 476.6 (`ab` of `time-runs`, 5 rounds of 21
  runs), 6,345.3 M instructions against 6,438.6 M, and its 2,666 functions'
  records are 346,741 at the bottom-up rewrite's head (`153497c`) against 346,753
  at `d1e8a5f`, both by the vector path; speedtest1's and Lua's code by `--tier1`
  ran alike (3,865.1 M instructions in speedtest1 by both)
  ([record](https://github.com/buster14a/buster-performance/tree/e73dcc345d67ef236098d29757c7218d0aeb86e8/docs/measurements/tier1-burs-lifetimes-141-2026-11-02),
  with commit 6's blocks' lifetimes and the review's measurements).
- **#142**: 240 Csmith and YARPGen programs at `e0ae799c` and `cc3387f`, with
  `--tier1` and with `--tier1 --scalar`: 195 pass at both tiers, and the 45
  others fail in clang's own steps (links past the small code model, which
  Buster refuses too, compiles past the output limit, runs past 30 s), none in
  Buster's.

Whole C is Tier 1's: every function of `sqlite3.c` compiles by `--tier1
--tier1-strict` (`sqlite3Multiply128` and `sqlite3Multiply160`, which selection
refused whole for a 128-bit constant each, since the 128-bit integers' split,
[the Tier 1 guide](tier1.md)'s "The 128-bit integers"), as does every function
of Lua 5.4.8 (#1204's runs, at `c1f08986`), and every function of c-testsuite's
subset. Since #1204's merge, Tier 1 also takes thread-locals' addresses, calls
of up to eight 64-byte vectors in registers, and addresses whose constant index
doesn't fit a disp32 (#141); the compiler's own `main.c` by `--tier1
-march=native` then compiles 983 of its 1,100 functions by Tier 1. Since the
x86 builtins' scalar bit manipulations became the IR's `intrinsic` (#141), it
compiles 1,004 of its 1,117 (at `48ac6d71`, 1,001: `_pdep_u64`, `_pext_u64`
and `_bzhi_u64` are the three), and the others stop before selection: one
naked, and 112 at the lowering to the IR, for vectors of elements other than
32-bit integers and the x86 builtins' rows of vectors (the Tier 1 guide's
list). What Tier 1 still refuses is that list's. The run-time gap to clang -O2 is #1189's.

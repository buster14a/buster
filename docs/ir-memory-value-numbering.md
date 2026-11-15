# Value numbering with memory

`ir_optimize_memory.c` is [#2718](https://github.com/buster14a/buster/issues/2718)'s
pass: the memory half of #139's value numbering, which [#866's
solver](ir-value-numbering.md) leaves out. It removes a load whose value an
earlier access of the same bytes already has: a repeated load of a field, and a
load of what was just stored there. It is a pass of the shared optimizer, the
bit `BUSTER_IR_OPTIMIZE_MEMORY` of `ir_optimize` and `ir_optimize_verified`
(`ir_optimize.c` includes the file), and runs after SCCP and the identities
(#2554) and before dead-code removal, on the analyses the optimizer has already
built: the CFG, the use lists, the dominator tree and SCCP's constants. It makes
no function of its own. A load it removes forwards its value as an identity
does (`IrOptimizeWork.forward`): its users read the value, dead-code removal
keeps the value for them, and the rewrite leaves the load out. The C driver's
`--tier1` runs it ("The pipeline" below).

## Keys

A plain load or store is an access: not volatile, not atomic and not in a
segment (`BUSTER_IR_OBSERVABLE`), of a block the dominator tree reaches. Its
address is read back to a root value through the instructions that make
addresses in the lowering's IR, each a step of `ir_memory_step`:

- a copy, a bitcast, an `inttoptr` of an `i64` and a `ptrtoint` to an `i64`
  (the lowering's pointers go through `i64`s: a field's address is
  `inttoptr (ptrtoint (address p + 8))`);
- an `address` with no index, or with an index SCCP knows, whose product with
  the scale joins the offset, or with the address's first index of a value;
- an `i64` addition of a constant, either way round, and a subtraction of one.

An address is then a root, an optional index value with its scale, and a
constant offset. Every value it is read through is the identities' forwarded
one. A step whose offset would leave `int64_t`'s range doesn't happen: the value
it would have left is the root. A slot's `stack_addr`s all have the slot's first
one as their root. The access's family is (root, index, scale), and its key the
family, the offset and the access's type. Two accesses of one key read and
write the same bytes: their addresses are the same values' same arithmetic.

## Alias facts

The corpora compile with `-fno-strict-aliasing`, and two symbols may name one
object (musl's `weak_alias`), so every two accesses may meet but where these
facts say otherwise:

- **One family, bytes apart.** Two keys of one family whose byte ranges
  `[offset, offset + size)` don't meet are apart: their addresses differ by a
  constant. A key that another key of its family meets is *overlapped*
  (`ir_memory_overlaps`, by a sort of the keys and a sweep of each family), and
  any store of its family but its own key writes it. A store to a key that isn't
  overlapped writes no other key of its family.
- **Private slots.** A slot is private where its address goes nowhere but into
  loads' and stores' addresses: every value read from its `stack_addr`s through
  the steps above (`IrMemoryWork.derived`) is used only as a plain access's
  address or by the step that reads an address through it
  (`ir_memory_kept`). A call, a return, a phi, a select, a comparison, a stored
  value, an index or any other use makes it escape, and so do the volatile and
  variadic frames' slot flags. Only a private slot's own stores and its
  `lifetime` markers write it; it is a class of its own, and every other access
  is of general memory.
- **Volatile slots.** A volatile or `_Atomic` declaration's local keeps its
  accesses: the checker's lowering marks it `BUSTER_TYPED_LOCAL_VOLATILE` (with
  `ADDRESS_TAKEN`), and the IR's lowering makes its slot
  `BUSTER_IR_SLOT_VOLATILE`, which SROA and promotion refuse. Its plain loads
  and stores are observable writes here: neither forwarded nor a source. Before
  #2718 such a local was marked address-taken alone, and the IR had no way to
  tell `volatile int leaf = x; return leaf;` from any frame object
  (`test-check-debug`'s GDB read of `leaf` found the load gone).
- **Writes of general memory.** A call (and a row call and an asm body's), a
  fence, an atomic read/modify/write, a masked, compressing or scattering
  store, `alloca` and `stack_restore`, every observable load or store, a store
  of general memory and the `lifetime` marker of a slot that isn't private.

Under these facts an access A is a load L's source when it has L's key and
class, and on every path from A to L the only writes of L's class are stores of
L's own family that don't meet its bytes (none at all where L's key is
overlapped). The source's value is the load's, or the value the store stored. A
store of an `undef` is no source: a load reads one value of it, and each use of
an `undef` may be another.

## The walk

`ir_memory_walk` visits the dominator tree in preorder, each block's
instructions in order, with each key's last access: its value, its class's and
its family's write counts after it, and the epoch of its class's kind (general
memory or the private slots). A write adds to its class's count, a store to its
family's too, and either enters its key. A load whose key's entry has the
current epoch, and since which its class's writes are all its family's stores
(or, overlapped, none came), takes the entry's value; any other load enters
itself. Leaving a block's subtree undoes its entries and counts, by an undo log,
so a sibling subtree sees its dominator's state. Counting writes rather than
clearing entries keeps a store's work constant whatever the table holds.

Across a block boundary an entry survives where no path from the block's
immediate dominator D to the block, without D again, may write that kind's
memory (`ir_memory_clear`). Every such path ends at a predecessor P that D
dominates and passes through P and every block between P and D on the
dominator tree, each entered by such a path: the greatest fixed point of those
equations over each block's writes, in reverse postorder. Entering a block
that isn't clear for a kind takes a new epoch of that kind. So a load after a
diamond whose arms write nothing takes the value from before it, a loop's
header takes nothing across a latch that writes, and a loop's exit takes the
header's value. SCCP's unreachable blocks are walked (their writes count) but
forward nothing.

## Twins

The data-parallel phases before the walk have a vector version and an exact
scalar twin. The caller's explicit request chooses between them, not the
optimizer's AUTO size bound (`IrOptimizeWork.scalar`): this pass's vector
phases have no set-up that a small function doesn't repay, as the verifier's
don't (#786).

- **Classification:** each instruction's kind by a 128-byte table's
  `vpermi2b` lookup, refined by its flags and type, and the operand its address
  is read through, by gathers of its operands' info words (`info`, a dword per
  instruction: kind, type, SCCP's constant and reachability).
- **Derivation:** the slot each value's chain starts at, by pointer jumping
  along those operands until no jump moves (the scalar twin: reverse
  postorder).
- **Escapes:** the values derived from a slot, found 16 at a time by a mask,
  and their uses (the use lists the optimizer built).
- **Events:** each block's accesses, writes and markers, compressed into one
  list 16 instructions at a time (`vpcompressd`), and the accesses into
  another; the walk and the blocks' summaries read these, not every
  instruction.
- **Hashes:** the families' and keys' 64-bit hashes 8 at a time; the grouping
  (an open-addressed table numbering them in access order, comparing whole
  keys) is shared.

Shared by both: the addresses' chains, the grouping, the overlaps (a counting
sort of the keys by family, then each family's few keys by offset), the blocks'
writes and clear entries, and the walk. The walk is the ordered part: each
block's state is its dominator's, and a key's chain of entries follows the
tree. Both versions give the same forwarded loads (`test-ir-optimize`,
`test-ir-source --passes`). Value spaces past 2^30 take the scalar twin, since
the gathers' indices are signed dwords.

### SIMD: what pays

SIMD_PAYS

## Tests

- **Text fixtures** (`tests/ir/optimize/memory-*.ir`, #175's pairs): one
  block's forwarding (a load's value, a store's, a family's other offset, a
  call and another family's store between); the clobbers (an overlapping store,
  another type at one offset, volatile and atomic loads, a fence, an `undef`'s
  store, a slot's marker); private slots (a call between, an escape to a call
  and through a stored address, markers, an indexed store of the same slot);
  blocks (a diamond's clean and written arms, a loop's header, latch and exit);
  addresses (an index's family, a constant index, a constant out of
  `int32_t`'s range, a second index, an `inttoptr` of an `i32`); and a volatile
  slot's loads, kept beside a private slot's forwarded one. Both parsers, both
  twins, the canonical bytes and the shared executor on eighteen inputs.
- **Generated programs** (`ir_optimize_memory_test.c`): 400 programs of
  straight blocks, diamonds and bounded loops whose loads and stores of `i32`s
  and `i64`s reach a private slot and two escaped ones at constant and indexed
  places, some through addresses that wrap around `int64_t`'s range, with calls
  and fences. Both twins must give the same function, with dead-code removal
  alone and in the optimizer's whole set, and an independent interpreter whose
  calls rewrite every escaped byte must return the same values before and after.
  An overflow fixture checks where the arithmetic stops. Breaking the calls'
  writes, the clear entries, the overlap rule or the escapes each fails it
  (150, 3,324, 27 and 211 failures).
- **Real C:** `test-ir-source --passes`'s `memory` and `tier1-memory` rows
  ([the IR guide](commands/ir.md)) run the pass alone and in Tier 1's set by
  both versions on every function of c-testsuite's subset, the frozen
  `sqlite3.c` and musl, which must give the same function and forwarded loads.
- **The driver:** `test-checker-driver`'s `--tier1-passes=` fixture reads an
  element twice in one sum, `test-ir-metamorphic`'s Tier 1 row runs the pass,
  and `test-check-debug`'s GDB reads a `volatile` local's value at its return.

## Measurements

MEASUREMENTS

The implementation is original. Forwarding loads along a dominator tree with a
scoped table is the textbook available-expression technique of early CSE
passes; no source implementation was imported.

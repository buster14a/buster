# Tier 1 loop store motion (#3309)

Status: implemented, `src/buster/compiler/ir_store_motion.c`, and part of
`--tier1`'s default pipeline; `--tier1-passes=-storemotion` turns it off. `test-ir-store-motion` and
`tests/driver/tier1_store_motion.c` are its tests
([the IR optimization guide](commands/ir-optimization.md#loop-store-motion-3309)).
The issue is [#3309](https://github.com/buster14a/buster/issues/3309).
MEASUREMENTS below has the 9700X's record and what it decided.

**In one paragraph.** A loop that updates a global kept it in memory under
`--tier1`: each iteration loaded what the last one stored, so the loop ran at
the store's forwarding latency whatever its arithmetic cost, and Tier 1 gained
nothing over Tier 0 on it. This pass runs on the lowered function, before SSA
promotion. For each such global it adds one load before the loop and a private
stack slot: the loop's loads of the global read the slot, and each of its
stores also stores its value in the slot. Promotion then turns the slot into
values and phis, as it does a local variable's. The loop's loads are gone, and
its stores write a register's value. Every store the source makes still
happens where it did, so memory is never stale and no store is added.

## What a loop keeps in memory, and why the stores stay

```c
long g_a, g_b;
void f(int n) { for (int i = 0; i < n; i++) { g_a += i; g_b ^= g_a; } }
```

`--tier1`'s loop before the pass, and with it:

```text
mov    %rax,%rcx                    add    %rax,%rcx
add    g_a(%rip),%rcx               mov    %rcx,g_a(%rip)
mov    %rcx,g_a(%rip)               xor    %rcx,%rdx
mov    g_a(%rip),%rcx               mov    %rdx,g_b(%rip)
xor    g_b(%rip),%rcx               add    $0x1,%eax
mov    %rcx,g_b(%rip)               cmp    %edi,%eax
add    $0x1,%eax                    jl     ...
cmp    %edi,%eax
jl     ...
```

On the left each iteration's `add` waits for the store before it to forward:
the dependency goes through memory. On the right it is `add` and `xor` on
registers, and the two stores are off the dependency chain.

clang -O2 also removes the stores: it loads before the loop and stores once
after it. This pass doesn't, for three reasons.

- **Loads through pointers stay right.** With the stores in place, memory
  holds the global's value after every store, so a load through any other
  address reads what the source's would. `g_sum += p[i]` is taken though `p`
  may point at `g_sum`. Sinking the stores needs every load in the loop to be
  of a known object, which rules that loop out, and it is the common one: a
  loop that updates a global usually reads its data through a pointer.
- **No store is added.** A store at the loop's exit is a store the source may
  not have made: a loop of no iteration stores nothing, and a compiler may not
  add a write of an object another thread may read. Avoiding it takes a flag
  or a compare for each key at each exit.
- **The exits are left alone.** No block is added and no phi's entries change.

What the stores cost is in MEASUREMENTS. A follow-up can sink them where every
access in the loop is known; this pass's keys and slots are what it would
start from.

## What it takes

A *key* is a symbol, a constant offset and a scalar type: the bytes one access
reads or writes. The pass looks at the loops that store to a listed symbol's
constant offset and at nothing else. `--tier1-report`'s `tier1-store-motion`
line counts each such loop under its first failing rule, and each stored key
of a loop that passed under its own.

| Rule for the loop | Outcome |
| --- | --- |
| It is reducible. | `irreducible` |
| It has no barrier: a call of any kind (a row call and an asm body are calls), a fence, an atomic read/modify/write, an observable (volatile, atomic or segment) load or store, a masked, compressing or scattering store, `alloca` or `stack_restore`. | `barrier` |
| Its header has one reachable predecessor outside it, which ends in `br` to the header (its preheader). | `preheader` |
| Every store in it is to a stack slot, to a symbol at a constant offset or at an index, or to a symbol that isn't listed. A store through any other address (a loaded pointer, a parameter) may write any key. | `unknown-store` |
| It has at most 64 keys and indexed symbols. | `many` |
| Some key of it is taken. | `no-key` |

| Rule for a stored key | Refusal |
| --- | --- |
| No other store to its symbol in the loop meets its bytes: one at an index, or one whose bytes overlap it at another offset or type. | `key.met` |
| Its bytes are inside its symbol's object. | `key.bounds` |
| Its type is an integer of 1 to 8 bytes, a pointer, a float or a double. | `key.type` |
| The loop loads it. | `key.unread` |
| Fewer than four keys of the loop were taken before it. | `key.limit` |

A load that meets a key (an indexed load of its array, a load of half of it)
refuses nothing: it reads memory, which is current. The outermost loop that
can take a key takes it, with the key's accesses in the loops inside, which
then find none of their own.

**Addresses.** An access's address is read back to what it is based on
(`ir_store_motion_resolve`): through copies, conversions between pointers and
`i64`s, `address` nodes and `i64` additions and subtractions of a constant, to
a `symbol_addr` or a `stack_addr`. A constant index joins the offset. An index
that isn't a constant makes the address *indexed*: C's index stays inside the
object the address is based on, so it names its symbol's bytes and no other's.
A sum of two values that aren't constants is unknown, since either may be the
base. So is an address past 24 steps.

**The limit of four keys** is a policy bound, not a timing: each key holds a
register across its loop, and [#2719](https://github.com/buster14a/buster/issues/2719)'s
record of LICM is what long ranges across a loop cost when they spill.

## The rewrite

For each taken key, with `P` the loop's preheader:

```text
P:    ...
      a  = the key's address         ; copies of what the loop computed of it
      v0 = load a
      t  = stack_addr $slot
      store t, v0
      br header
loop: x = load a      becomes   x = load t
      store a, y      becomes   store a, y ; store t, y
```

The slot is typed and private, so promotion takes it. The address's
instructions inside the loop (the lowering makes a `symbol_addr` at each
access, and a field's `inttoptr`, `address`, `ptrtoint` chain) are copied into
the preheader; those outside it dominate the preheader, since they dominate an
access in the loop and aren't in it. The result is a new view of the function
with new instruction columns, block offsets, extra slices, calls and slots,
which borrows the input's other tables; the input isn't changed. A function
nothing moves in is returned as it was given, with nothing written.

Promotion checks the result as it does any input that isn't known verified
(`c_check_tier1.c`). A result it refuses or finds past its limits is dropped
for the function it was made from.

## Which symbols are listed

The pass trusts one fact from its caller, by symbol: the object's bytes, where
the unit knows the symbol for a *distinct object*
(`IrStoreMotionOptions.symbol_bytes`). Two things rest on it. A store through
another object's address can't write the key, and the preheader's load, which a
loop of no iteration never made, reads bytes that exist.

The emission makes the list (`check_program_store_motion_symbols`). A symbol
is listed where all of these hold:

- it names an object, not a function, a literal or a label, that the unit
  defines or tentatively defines;
- the unit reaches it directly: it isn't thread-local, and in
  position-independent code another module can't preempt it (it is static or
  has hidden, internal or protected visibility);
- its type is complete and not volatile-qualified; the type's size is the
  listed bytes;
- none of its declarations has an attribute other than `aligned`, `alignas`,
  `unused`, `used`, `deprecated`, `nonstring`, `visibility` and `nocommon`.
  `weak`, `selectany`, `alias`, `weakref`, `section`, `common` and an asm label
  keep it out, and so does any attribute not on that list.

And a unit lists nothing where a symbol may have two names in it: it has an
alias, ifunc or weakref, a `#pragma weak` or `redefine_extname`, an asm
statement (a template can define or `.set` a symbol), a section pragma,
`-fcommon`, or an object's declaration with an asm label (`extern long y
asm("x")` names `x`'s bytes).

This is narrower than clang's rule, which takes any two global variables for
distinct objects, another unit's declarations included. Two declarations may
be one object's names (musl's `weak_alias`), which only the defining unit can
see, and [value numbering with memory](ir-memory-value-numbering.md#alias-facts)
assumes for that reason that any two symbols may meet. Here a listed symbol is
the unit's own definition, which no other name in the unit reaches. An extern
declaration is never listed, so a loop over another unit's global is left as
it is.

**Volatile accesses.** The typed stream's indirect accesses carry no volatile
flag ([#2805](https://github.com/buster14a/buster/issues/2805),
[#3310](https://github.com/buster14a/buster/issues/3310)). The driver keeps the
pass off for a body with any volatile expression, its own or an inlined
callee's, by the guard the loop vectorizer uses
(`check_program_vectorize_volatile`).

## Data-parallel parts

Two scans read every instruction, and both are vector code:

- **The symbols' filter** (`ir_store_motion_symbols`): the `symbol_addr`s found
  64 opcodes at a time, each one's symbol looked up in the list. A function
  that names no listed symbol is done after this pass over its opcodes, and
  most name none: the strings and functions they name aren't listed.
- **The classification** (`ir_store_motion_classify`): the kinds by a 128-byte
  table's `vpermi2b` lookup, an observable access a barrier by its flags, and
  the plain accesses' and the barriers' indices compressed into two lists
  (`vpcompressd`), 64 instructions at a time.

A function with no store to a listed symbol's constant offset builds no CFG.
What follows reads the lists and is scalar: the addresses' chains (dependent
walks of a few steps; [#2718's guide](ir-memory-value-numbering.md#simd-what-pays)
measured a gathered version of the same walk at 3.5 times the scalar one's
time), a loop's keys (its accesses of listed symbols, a few), and the rewrite
of a function that changed.

## What it leaves

- **The stores.** Sinking them to the exits where every access in the loop is
  known, as above.
- **Keys behind a pointer** (`p->count += 1`). The preheader's load would be a
  load through `p` that a loop of no iteration never made, and `p` may be
  null there. It needs the loop's test before the preheader (a rotated loop)
  or a proof that `p` was dereferenced on the way in.
- **Loops with a row call.** A vector builtin on elements the IR has no
  operation for is a machine row, which every pass takes for a call.
- **Another unit's globals**, by the list's rule.

## Tests

- **`test-ir-store-motion`** (`ir_store_motion_test.c`): 27 text fixtures and
  the callee of those that call, `tests/ir/store-motion/*.ir`, by both parser
  paths. Each expects its moved
  loops and keys, the loads and stores given a slot, and the outcome or the
  refusal it is there for; the results and counts fold into a frozen digest.
  A changed result must verify, promote and optimize. It then runs in the
  shared interpreter as it is, promoted and optimized, against the input, on
  equal memory, with a pointer parameter at the first symbol's bytes, in
  another's or in a buffer of its own, at bounds from zero up. The promoted
  result must have no slot the input's promotion lacks, and the input's loads
  less those the pass gave a slot, plus one a key. Six faults
  (`BUSTER_IR_STORE_MOTION_MUTATE_*`: a barrier passed over, an unknown store
  passed over, a key beside an indexed store, a key beside an overlapping
  one, a store the slot doesn't get, a key past its object's bytes) must each
  be rejected on some fixture. The two scans are checked against their
  definitions on random columns at every length to 200, and the entry point
  with arenas of every short size.
- **`tests/driver/tier1_store_motion.c`**: 21 functions of loops over
  globals (23 loops that store to one, with `main`'s), compiled by clang -O0 and by `--tier1 --tier1-strict --verify` with the pass
  and without it on both host paths; the programs print the same lines. The
  report's counts are pinned, and `objdump` finds the issue's kernel with four
  references to its two globals with the pass and five without. Then 29
  declarations, each in a unit whose one loop updates the object: the list
  takes the unit's own definitions and leaves the others.
- **`test-ir-source --passes`**'s `store-motion` row runs the pass on every
  function of c-testsuite's subset, the frozen `sqlite3.c` and musl with every
  symbol listed (the files have no emission to say which are the unit's own,
  and the run executes nothing): a changed function must promote and its
  promotion pass the optimizer's final verification. `benchmark-ir-source
  --passes --only store-motion` times it.

## MEASUREMENTS

Pending: the 9700X's record goes here with the pull request.

## Algorithm sources and provenance

The implementation and fixtures are original C under the project default
license. Keeping a loop's memory location in a register is classical (scalar
promotion in LLVM's LICM, store motion in GCC's loop-invariant motion); this
pass takes the idea and none of their code, and differs from both in keeping
the stores.

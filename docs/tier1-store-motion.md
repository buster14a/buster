# Tier 1 loop store motion (#3309, #3346)

Status: implemented, `src/buster/compiler/ir_store_motion.c`, and **off by
default**: `--tier1-passes=+storemotion` turns it on, and so does `+all`.
`test-ir-store-motion` and `tests/driver/tier1_store_motion.c` are its tests
([the IR optimization guide](commands/ir-optimization.md#loop-store-motion-3309)).
The issues are [#3309](https://github.com/buster14a/buster/issues/3309) and,
for sinking the stores to the loop's exits,
[#3346](https://github.com/buster14a/buster/issues/3346). MEASUREMENTS below
has the 9700X's records and what they decided.

**In one paragraph.** A loop that updates a global keeps it in memory under
`--tier1`: each iteration loads what the last one stored. This pass runs on
the lowered function, before SSA promotion. For each such global it adds one
load before the loop and a private stack slot: the loop's loads of the global
read the slot, and its stores write the slot — alone where they sink to the
loop's exits (#3346), and the global too where they stay.
Promotion then turns the slot into values and phis, as it does a local
variable's. The loop's loads are gone, and its stores write a register's
value. No store the source didn't make is ever added. On the 9700X a loop over
a `double` global runs 2.4 times as fast with it, and the integer kernel, once
its stores sink, as fast as clang's build that stores after the loop. Keeping a
loop's integer global in a register alone changed nothing there, because the
trip through memory was costing Zen 5 nothing: the issue's 7 times was a cloud
Xeon's. And of `sqlite3.c`'s, Lua's and musl's loops the pass
changes one, which runs once at start-up: their loops over globals call
functions. A `--tier1` compile of `sqlite3.c` costs 0.4% more with it. So it
is opt-in.

**Sinking the stores (#3346).** Where every read of memory in the loop is a
load the pass places, the stores move too: the loop's stores of the global
write the slot alone, and each of the loop's exits writes the slot's value
back once, through the global's address where its bits changed and through a
dummy frame slot where they didn't, so that a path that changed nothing stores
nothing. The issue's integer kernel then runs as clang's does
([Sinking the stores](#sinking-the-stores-3346) below).

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

On the left each iteration's `add` reads what the store before it wrote: the
dependency goes through memory. On the right it is `add` and `xor` on
registers, and the two stores are off the dependency chain. What that is worth
depends on the processor and on the value's type (MEASUREMENTS): nothing for
these integers on the 9700X, 2.4 times for a `double`.

clang -O2 also removes the stores: it loads before the loop and stores once
after it. #3309's pass didn't, for three reasons, which #3346's sinking answers
where it can ([Sinking the stores](#sinking-the-stores-3346)); where it can't,
these still hold.

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

What the stores cost is in MEASUREMENTS: on the 9700X they were what was left
of the gap to clang on the integer kernel, which sinking them closes.

## Sinking the stores (#3346)

```c
long g_a, g_b;
void f(int n) { for (int i = 0; i < n; i++) { g_a += i; g_b ^= g_a; } }
```

`--tier1 --tier1-passes=+storemotion`'s code for it, with the stores sunk:

```text
     xor    %eax,%eax
     lea    g_a(%rip),%rcx           ; the keys' addresses and first values
     mov    g_a(%rip),%rdx
     lea    g_b(%rip),%rsi
     mov    g_b(%rip),%r8
     mov    %rdx,%r9
     mov    %r8,%r10
     jmp    test
loop:add    %rax,%r9
     xor    %r9,%r10
     add    $0x1,%eax
test:cmp    %edi,%eax
     jl     loop
     lea    -0x8(%rsp),%rax          ; the dummy slot
     cmp    %rdx,%r9                 ; g_a changed?
     cmovne %rcx,%rax
     mov    %r9,(%rax)               ; to g_a where it did, to the dummy where it didn't
     lea    -0x8(%rsp),%rax
     cmp    %r8,%r10
     cmovne %rsi,%rax
     mov    %r10,(%rax)
     ret
```

**What it takes.** A key's stores sink where all of these hold, and stay as
the section before says otherwise (`--tier1-report`'s `sink.` counts name the
first that failed):

| Rule | Otherwise |
| --- | --- |
| Every load in the loop has an address the pass places (a stack slot's, or a symbol's), and no masked load or gather is in it. A load through any other address may read the key's bytes, which memory would no longer hold. | `sink.load` |
| No load in the loop that isn't the key's meets its bytes: one of half of it, or of its array at an index. | `sink.met` |
| Each exit edge either goes to a block whose every predecessor is in the loop, or leaves by a `cbr` to two blocks, so that the edge can be split; at most 16 exits. | `sink.exit` |

A key the loop stores and never loads, which the pass without sinking refuses
(`key.unread`: it would gain nothing), is taken where its stores sink.

**The write-back.** Each exit gets, for each sunk key:

```text
x  = load slot
c  = icmp ne x, v0        ; v0 the preheader's load; a float's bits as an integer's,
                          ; an i8's or an i16's widened to an i32 (Tier 1 compares those)
d  = stack_addr $dummy    ; a frame slot no one reads, its address taken
a  = select c, key, d
store a, x
```

- **No store the source didn't make.** A loop of no iteration, or one whose
  stores left the key's bits as they were, stores to the dummy. That is not a
  store skipped: nothing in the loop synchronizes (no call, fence or atomic
  access), so another thread that wrote the object while the loop ran would
  race with the loop's own store, and memory holds `v0`'s bits. A store
  through a `select` keeps the decision off the branches: `cmov` and one
  store, as above.
- **Where.** An exit block whose every predecessor is in the loop gets the
  write-back before its first instruction that isn't a phi: the block after a
  `for` loop, a `return`'s block in the loop. An exit edge to a block that
  other paths reach too (the block after the loop, where a `break`'s block
  also goes, or a `goto`'s label after the loop) is split: a block of its own
  after the function's last, with the write-backs and a `br` to the exit
  block; the loop block's `cbr` goes to it, and the exit block's phis take its
  value from it. A switch's or an indirect branch's edge isn't split, since
  its other cases may go to the same block.
- **Order.** An exit block that is the next loop's preheader writes the key
  back before that preheader loads it.

A sunk key's value at an exit is the slot's, which promotion makes a phi of the
loop's values, as for any local. `v0` and the key's address are the
preheader's, which dominates every exit block the pass writes to.

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
with new instruction columns, block offsets, extra words, calls and slots,
which borrows the input's other tables; the input isn't changed. The input's
extra words keep their places in the new array, where a switch's and an
indirect branch's lists are read, and the phis', calls' and extra values'
operands get slices after them. A function nothing moves in is returned as it
was given, with nothing written.

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

The parts that read every instruction are vector code:

- **The symbols' filter** (`ir_store_motion_symbols`): the `symbol_addr`s found
  64 opcodes at a time, each one's symbol looked up in the list. A function
  that names no listed symbol is done after this pass over its opcodes, and
  most name none: the strings and functions they name aren't listed.
- **The classification** (`ir_store_motion_classify`): the kinds by a 128-byte
  table's `vpermi2b` lookup, an observable access a barrier by its flags, and
  the plain accesses' and the barriers' indices compressed into two lists
  (`vpcompressd`), 64 instructions at a time.
- **The rewrite** (`ir_store_motion_rewrite`), for a function that changed.
  The instructions an added one comes beside are few: the preheaders'
  terminators and the taken keys' stores. Between two of them the input's
  instructions move by one distance, so each such run's new indices are
  filled 16 at a step, and its columns copied and its operands remapped by
  the unroller's remap (`ir_unroll_remap`: a gather through the index map, 16
  instructions at a step). The operands the extra array holds (a phi's, a
  call's, an instruction's extra values) are remapped an instruction at a
  time, as the unroller's are (`ir_unroll_extras`).

A function with no store to a listed symbol's constant offset builds no CFG.
What reads the lists is scalar: the addresses' chains (dependent walks of a
few steps; [#2718's guide](ir-memory-value-numbering.md#simd-what-pays)
measured a gathered version of the same walk at 3.5 times the scalar one's
time) and a loop's keys (its accesses of listed symbols, a few).

## What it leaves

- **Loops with a load through a pointer.** Their stores stay (`sink.load`).
  Sinking them needs the load's address apart from the key's: a pointer
  parameter that the function is known not to have received for the global.
- **A switch's exit to a shared block.** Splitting a switch's edge needs a
  block for each case that goes there.
- **Keys behind a pointer** (`p->count += 1`). The preheader's load would be a
  load through `p` that a loop of no iteration never made, and `p` may be
  null there. It needs the loop's test before the preheader (a rotated loop)
  or a proof that `p` was dereferenced on the way in.
- **Loops with a row call.** A vector builtin on elements the IR has no
  operation for is a machine row, which every pass takes for a call.
- **Another unit's globals**, by the list's rule.

## Tests

- **`test-ir-store-motion`** (`ir_store_motion_test.c`): 33 text fixtures and
  the callee of those that call, `tests/ir/store-motion/*.ir`, by both parser
  paths. Each expects its moved
  loops and keys, the loads and stores given a slot, and the outcome or the
  refusal it is there for; the results and counts fold into a frozen digest.
  A changed result must verify, promote and optimize. It then runs in the
  shared interpreter as it is, promoted and optimized, against the input, on
  equal memory, with a pointer parameter at the first symbol's bytes, in
  another's or in a buffer of its own, at bounds from zero up; a quarter of
  the trials bind the symbols read-only, so that a store the source didn't
  make faults, even of the value the object held. The promoted result must
  have no slot the input's promotion lacks but the dummy, and the input's
  loads less those the pass gave a slot, plus one a key and one a write-back.
  Nine faults
  (`BUSTER_IR_STORE_MOTION_MUTATE_*`: a barrier passed over, an unknown store
  passed over, a key beside an indexed store, a key beside an overlapping
  one, a store the slot doesn't get, a key past its object's bytes; #3346's
  key sunk past a load it can't place, exit given no write-back, and
  write-back to the key on a path that didn't change it) must each be
  rejected on some fixture. The two scans are checked against their
  definitions on random columns at every length to 200, and the entry point
  with arenas of every short size.
- **`tests/driver/tier1_store_motion.c`**: 26 functions of loops over
  globals (26 loops that store to one; `main` has a volatile store, which keeps
  the pass off its body), compiled by clang -O0
  and by `--tier1 --tier1-strict --verify` with the pass (`+storemotion`) and
  without it on both host paths; the programs print the same lines. The
  report's counts are pinned, and `objdump` finds the issue's kernel with four
  references to its two globals with the pass, all outside the loop, and five without. #3346's: a
  loop left by a break, a return and a goto; the issue's `double` kernel; a
  page `mprotect`ed read-only while a loop that stores nothing to it runs (of
  no iteration, and of iterations whose condition never holds); a `volatile
  sig_atomic_t` flag a signal handler sets; a global's address that escapes to
  a pointer the loop reads through; and a program built without
  `--tier1-strict`, whose loop's write-backs must come before its callee's
  `longjmp` to a `setjmp` (Tier 1 doesn't admit the function that calls
  `setjmp`). Then 29
  declarations, each in a unit whose one loop updates the object: the list
  takes the unit's own definitions and leaves the others.
- **`test-ir-source --passes`**'s `store-motion` row runs the pass on every
  function of c-testsuite's subset, the frozen `sqlite3.c` and musl with every
  symbol listed (the files have no emission to say which are the unit's own,
  and the run executes nothing): a changed function must promote and its
  promotion pass the optimizer's final verification. `benchmark-ir-source
  --passes --only store-motion` times it.

## MEASUREMENTS

On the 9700X under the lock, 2026-11-23: a Release build of `f16b0523a`, the
pull request's head at the time, with the pass on by default as it then was.
"Without the pass" is that compiler with `--tier1-passes=-storemotion`. The
record, with every run, script and log, is buster-performance's
[`store-motion-3309-2026-11-22`](https://github.com/buster14a/buster-performance/tree/2b293ab52a9927d1ec1c13fffed6303535399b49/docs/measurements/store-motion-3309-2026-11-22).

### Four kernels

Each is one `noinline` function of one loop, run 200,000,000 times, with its
state in globals:

- **k1**, the issue's: `g_a += i; g_b ^= g_a;` (two `long`s);
- **k2**: k1 and `g_arr[i & 15] += 1;`
- **k3**: `g_sum += p[i & 1023]; g_hash = g_hash * 31 + p[i & 1023];`, with
  `p` a parameter, which a pass that sinks the stores must refuse;
- **k4**: `g_x = g_x * 1.0000001 + 0.5; g_y += g_x; s_n += i & 3;` (two
  `double`s and a static `long`).

Each of the compiler's builds was linked at four placements: 0, 16, 32 and 48
bytes of padding before the function, the places a 16-aligned function takes
in a 64-byte line. Placement alone moved k1's loop from 1.36 to 2.00 cycles an
iteration, more than the pass does, so one placement's reading decides
nothing. The table has cycles an iteration: the least and the most of the four
placements' medians (5 rounds each, CPU 2, `perf stat`'s user cycles), or the
one figure where they agree to three decimals.

| Build | k1 | k2 | k3 | k4 |
| --- | ---: | ---: | ---: | ---: |
| Tier 0 | 2.001 to 3.001 | 3.001 | 4.002 | 15.002 |
| `--tier1` without the pass | 1.364 to 2.001 | 2.185 | 4.001 | 15.597 to 15.605 |
| `--tier1` with it | 1.365 to 1.408 | 2.147 | 4.001 | 6.571 to 6.587 |
| `--tier1 --tier1-passes=+all` | 1.275 to 1.276 | 1.950 | 4.001 | 6.001 |
| clang -O2 | 1.877 | 1.516 | 3.001 | 6.001 |
| clang -O2 without unrolling or vectors | 1.001 | 1.473 | 3.003 | 6.001 |
| Instructions an iteration, without the pass and with it | 9 and 7 | 15 and 13 | 14 and 12 | 17 and 16 |

- **Integers: the trip through memory costs the 9700X nothing.** k3 takes 4.0
  cycles an iteration by every build of this compiler, Tier 0's included,
  with the pass or without it: the store and the load between one iteration's
  multiply and addition and the next's add nothing a build without them
  saves. With the pass k1's time is unchanged at three placements (without it
  over with it: 1.002, 0.983 and 0.999) and k2's is 1.8% less. At the fourth
  placement k1's loop took 2.00 cycles without the pass and 1.37 with it; the
  shorter loop has no such placement among the four. What the pass removes
  from these loops is 2 of their 9 to 15 instructions.
- **Doubles: 2.37 times.** k4 goes from 15.6 cycles an iteration to between
  6.57 and 6.59, against 6.00 for clang and for `+all`. A `double`'s
  store is not forwarded to its load for nothing: without the pass the trip
  adds nine and a half cycles to a chain of six.
- **What the stores cost.** With the loads gone k1 takes 1.365 cycles an
  iteration, and clang's build that stores once after the loop 1.001; k4
  takes 6.57 against 6.00. The store-sinking variant of this pass that
  another session measured on k1 read 200.2M cycles, the same 1.001
  ([its record on the issue](https://github.com/buster14a/buster/issues/3309#issuecomment-7087516036)).
- **The issue's figures were a cloud Xeon's**: 5.8 cycles an iteration by
  either tier against clang's 0.8. In this work's cloud container (a Xeon at
  2.3 GHz, not the record) the pass took k1 from 424 ms to between 92 and 97.
  The pass is worth to generated code what the processor charges for a store
  and a load of the same bytes, and Zen 5 charges nothing for an integer's.

### The corpora

**The code it changes.** By `--tier1` with the pass and without it, 33 of 34
objects are the same bytes: Lua's 33 units, where 2 loops store to a listed
object and both call a function, against speedtest1's unit (`sqlite3.c` and
`speedtest1.c` as one). There 68 loops store to a listed object: 66 call a
function, 1 stores through a pointer and 1 is moved, a loop of
`memsys5Init`, which runs once and is the one function of the unit's 1,422
whose code differs. In a cloud container, the frozen `sqlite3.c` alone has 28
such loops and none moved, and musl's 1,355 units have 4, all with a call. A
loop that keeps its state in globals and calls nothing is the kernels' shape,
not these programs'.

**What it costs a compile.** `--tier1 --lanes 1 -c` of the frozen
`sqlite3.c`, 11 runs each, medians:

| | With the pass | Without it | With over without |
| --- | ---: | ---: | ---: |
| Instructions | 8,020,707,614 | 7,998,769,414 | 1.0027 |
| User cycles | 3,265,293,627 | 3,251,386,846 | 1.0043 |
| Wall, ms | 607.901 | 604.853 | 1.0050 |
| Peak memory, KiB | 326,996 | 324,884 | 1.0065 |

The pass alone on the 2,602 lowered functions, every symbol listed
(`benchmark-ir-source --passes --only store-motion`), takes 0.476 ms, 0.85 ns
an instruction. The compile's cost is more than that because the pass runs
after inlining, on larger functions, and for a callee's summary as well as
for the function. Counters added for the measurement (a cloud container, the
proportions only) say where it goes: 949 function bodies name a listed symbol,
with 773,538 instructions among them; 169 of those have a store to a listed
symbol's constant offset, and 95 of those a loop. Of the pass's time, 46% is
the first of those steps (the scan and the walk of every store's address, in
949 functions), 37% is the dominator analysis for the loops and 8% the CFG
before it (169 functions), and 7% the loops' rules.

### What it decided

**Off by default.** A default pass has to pay for itself on programs, and
this one changes none of the corpora's code that runs more than once, while
it adds 0.27% instructions and 0.43% cycles to a `--tier1` compile of
`sqlite3.c`. Where a program's loops do have this shape it is worth turning
on: 2.37 times on the `double` kernel on the 9700X, and on processors that
don't forward an integer's store for nothing, as much as the issue found.

Two things would change the decision, and both are follow-ups:

- ~~**Sinking the stores**, for the loops whose every access is known.~~ Done
  in #3346, above: the integer kernel now reads 1.001 cycles an iteration.
- **A cheaper refusal.** Nine tenths of the pass's time on `sqlite3.c` is
  spent before any loop's rule is applied. A store's address needs its walk
  only where the walk can end at a listed symbol, and a loop with a call
  needs no dominator analysis: a candidate store's block that is on no cycle
  of call-free blocks is in no loop the pass can take.

### What sinking the stores added (#3346)

On the 9700X under the lock, 2026-11-23: Release builds (`BUSTER_DEBUG_INFO=0`)
of `e5f3e304b` (this pass as it is), `42770575f` (its first commit, where the
write-back compared a value held in a register across the loop) and
`0766f0a44` (#3328's head, where the stores stay). The record, with every run,
script and log, is buster-performance's
[`store-sinking-3346-2026-11-23`](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/store-sinking-3346-2026-11-23).

The four kernels above, each linked at the same four placements, cycles an
iteration (the least and the most of the four placements' medians):

| Build | k1: `g_a += i; g_b ^= g_a;` | k2: k1 and `g_arr[i & 15] += 1;` | k3: sums through a pointer | k4: two `double`s and a `long` |
| --- | ---: | ---: | ---: | ---: |
| `--tier1` without the pass | 1.365 to 2.001 | 2.185 | 4.001 | 15.594 to 15.599 |
| With #3328: the stores stay | 1.365 to 1.408 | 2.147 | 4.001 | 6.569 to 6.589 |
| With sinking, the value held in a register | 1.001 to 1.155 | 2.186 to 2.193 | 4.001 | 6.385 to 6.386 |
| **With sinking (this PR)** | 1.001 | 1.696 to 1.710 | 4.001 | 6.385 to 6.387 |
| clang -O2 | 1.877 | 1.516 | 3.001 | 6.001 |
| clang -O2, no unrolling or vectors | 1.001 | 1.473 | 3.003 | 6.001 |
| Instructions an iteration: without the pass, with #3328, with sinking | 9, 7, 5 | 15, 13, 11 | 14, 12, 12 | 17, 16, 13 |

- **The integer kernel is clang's.** k1 reads 1.001 cycles an iteration at
  every placement, which is what clang -O2 reads when it doesn't unroll or
  vectorize, and its five instructions an iteration are clang's. That closes
  the gap #3309's record left.
- **k2**, with an indexed store beside the two globals, goes from 2.147 to
  between 1.696 and 1.710; **k4**, the `double`s, from 6.57 to 6.385.
- **k3 is refused** (`sink.load=2`: it reads through a pointer) and takes
  4.001 cycles an iteration by every build of this compiler, as before.
- **Why the write-back reads the key instead of a held register.** The first
  version compared the slot against the preheader's loaded value, so each key
  held a register across the whole loop. k2 then spilled and reloaded one
  every iteration: 2.186 to 2.193 cycles an iteration, slower than `--tier1`
  without the pass, with 14 instructions an iteration against 13. Reading the
  key at the exit costs a load there and keeps the loop's registers.

The corpora are as #3309 measured, since the pass still moves nothing in them:
`sqlite3.c`'s object is byte-identical and its loops are `barrier=27
unknown-store=1`, and a `--tier1 --lanes 1 -c` of it costs 1.0028 instructions
and 1.0046 user cycles with the pass (11 runs each). Lua's 33 units are
byte-identical too and cost 1.0008 instructions and 1.0016 cycles (12 runs
each way).

## Algorithm sources and provenance

The implementation and fixtures are original C under the project default
license. Keeping a loop's memory location in a register is classical (scalar
promotion in LLVM's LICM, store motion in GCC's loop-invariant motion); this
pass takes the idea and none of their code, and differs from both in keeping
the stores.

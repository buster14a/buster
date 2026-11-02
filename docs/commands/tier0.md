# tier0

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/tier0.c` is Tier 0 (#136): a function's typed stream (the
  checker's output, #133, which `Tier0Function` defines until #133 produces it:
  entries in evaluation order, control flow as branches to entry indices, and
  the locals and calls tables with #137's locations) to `X86Instruction`
  records by expanding a template per entry, with each value's register its
  depth on an integer and a floating value stack, after fusing leaves,
  addresses, compares and x = x op v into their consumers' instructions; locals
  get registers or frame slots, and objects (local arrays, structs and unions,
  from optional size and alignment columns) places of their own in the frame,
  reached through their addresses; calls, and division and shifts at depths where
  their fixed registers are taken, are emitted entry by entry, and so are
  variable-length arrays' allocations and the stack's saved and restored
  positions, which give their function a frame pointer. A `TAIL_CALL` (#147's
  musttail) calls its row as the function's return: its arguments where the
  function's own arrived, the frame released, then a jump
  (`Tier0Code.tail_records`). An object, or a call's
  outgoing area, aligned past 16 bytes realigns its function's frame (rbp the
  frame pointer, `and rsp` after the reservation, the frame's places rsp's or,
  where rsp moves, r15's, and its incoming places rbp's: docs/tier0.md's
  "Realigned frames"), and `LOCAL`, `STORE_LOCAL` and `SET_LOCAL` of a class
  may name an object, a value that must live in memory. An optional per-unit
  access column reaches the symbols a unit doesn't define through their GOT
  slots, and thread-locals by local-exec or initial-exec. Long double's values
  (class F80) live in a 16-byte frame slot per integer depth, each entry of
  them deferred: loaded onto the x87, computed and stored back, its locals and
  parameters objects and its constants an optional column. The 128-bit
  integers (I128 and U128, `__int128`'s) live in the same slots, each entry of
  them deferred too, their halves through scratch registers; a front end
  divides them, converts them to and from floating values and passes them
  through calls and 16-byte objects, as clang's code does. Values past the
  depth tables (15 integer, 14 with a frame pointer, and 16 floating ones)
  live in the frame's deep slots, a slot per depth: the entries whose values
  cross that line or that use registers past it are deferred, their operands
  loaded from the slots into the stacks' first registers around their own
  records (`tier0_select_deep`); long double's, the 128-bit integers' and the
  vectors' values keep their own slots there, by their true depths (#780).
  AVX-512's masks (MASK, #179) are a third stack beside the integer and the
  floating ones, of 1 to 8 bytes each: depths 0 to 6 in k1 to k7, deeper ones in
  frame slots (`Tier0Frame.mask_area`), saved around calls (every opmask
  register is caller-saved) and around rows that name opmask registers by
  number; rows take them by role (`MASK(j)`, `MASK_RESULT`, `MASK_SCRATCH(k)`,
  borrowing k1 on past k7), and the stack's depths are a column of their own
  in a function with masks (`Tier0Function.masks`, `tier0_vector_mask_depths`
  and its twin). `interpreter.c` runs them; `ir_lower.c` lowers them to the
  IR's `m8` to `m64`, its rows of the front end's `_k*` builtins through
  `ir_simd_typed.c`'s decoder, so `--tier1` compiles them by Tier 1
  (docs/ir-simd.md's "Masks from the typed stream"); the
  front end's AVX-512 builtins are their first user (`check_x86_lower_masks`);
  `test-tier0`'s masks check (`tier0_mask_test.c`) runs random programs over
  them natively against the interpreter and a model of its own. The
  output is
  the records with #144's branch columns, `LinkReference`s, the frame
  (`Tier0Code.frame_saved`, `frame_size` and `frame_pointer`), for unwind
  tables, and, when asked for (`Tier0Function.entry_records`), each entry's
  first record, for source rows. `tier0_compile`
  finds the fusions, effects and depths 64 entries at a time in AVX-512, then
  selects, measures and emits 8 at a time from a 64-bit word per entry,
  expanding each template with one `vpermb`; `tier0_compile_scalar` is its
  twin. `tier0_validate`, the stream's rules (the compile paths'
  precondition, which the lowering checks on every function it lowers), runs a
  vector path 64 entries at a time (`tier0_validate_accepts`, #569) and leaves
  every stream it doesn't take to the scalar validator (`tier0_validate_scalar`,
  its twin, which `--scalar` runs alone), so that a refusal and its rule are
  always the scalar validator's.
  `tier0_generate.c` makes random programs as typed streams and as C,
  `tier0_test.c` holds `test-tier0`, `tier0_benchmark.c` provides
  `benchmark-backend` and `tier0_source_benchmark.c` `benchmark-backend-source`,
  the same measurement (`tier0_benchmark_measure.c`) on the functions `-c`
  compiles of C files. `docs/tier0.md` has the contract, the design and the
  9700X measurements.
## Tier 0 tests

`./build/compiler test-tier0 [--seed N] [--count N] [--cross-check
[--cross-count N] [--clang PATH] [--keep]]` (about 1.6 s on the 9700X, and
14.6 s with `--cross-check`, on CPUs 0 to 7 as its job runs it) checks `tier0.c` on programs from
`tier0_generate.c`, `--count` (300) of each of seven shapes: one function of
integers; integers and calls; everything (floating values, pointers,
address-taken locals, objects); larger; larger with structs and unions
passed and returned by value (#137), from a catalog of records whose parts
load every way (byte arrays of 1 to 17 bytes, floats and doubles mixed with
integers both ways round, unions, a member aligned to 16, the empty struct,
records aligned to 32 and records past 16 bytes), whose programs must reach
every kind of part, location and result the catalog makes, and functions with
a frame pointer (variable-length arrays) that pass, take or return records;
larger past the depth tables (calls of 13 to 32 arguments, each evaluated over
the ones before it, and deeper expressions, whose deep entries must be at
least 1% of the entries); and that with objects aligned past 16 bytes (to 32,
64 and 4096, `over_aligned`), in realigned frames, a base (r15) among them
where variable-length arrays move rsp, many of each. Their calls pass up to 12
arguments (32 in the deep shape), past the argument registers, a third of them (in the shapes with
calls) through pointers to functions, and their pointer arithmetic has
element sizes for each of its templates, past imul's imm32 too. Their objects
are local arrays and structs, some `_Alignas(16)` (or past it, in the last
shape), read, written and pointed
into, copied (twins' bytes, all or a prefix, in statements and before a call's
arguments, its C sequenced), each address's remainder by its alignment a value,
and every object's bits folded into a global at its function's end (a
floating part's masked to zero for a NaN, and a union's floating members
written only with integers' values, since C leaves a NaN's bits open). In the
last two shapes they have variable-length arrays too, whose sizes and addresses
are values, with the stack's position saved and restored around the loops'
constructs that declare them, as a producer does. Before them
it checks
the call emitter's move resolver on 20,000 random sets of moves (paths and
cycles of every length) run on a register file, the vector path's application
of chosen forms against the scalar path's on random records, that both paths
refuse unsupported entries alike and compile up to 240 values at once alike,
refusing more, and the
objects: `tier0_validate` refusing each break of their rules, and 2,000 random
frames of objects, values and `VA_LIST`s whose places both paths must give
alike, aligned, inside the frame and apart, every other round's realigned for
objects aligned past 16 (the prologue's `and rsp` by the most they need, which
the frame publishes); the frame's alignment, `tier0_frame_alignment` and its
vector twin, on random columns of every count of locals to 40 and of calls to
12, each ending at a `PROT_NONE` page; copies: their rules, and a copy of
each size class above every count of values, whose scratch register both
selections must count among the frame's clobbers; and variadic calls through a
pointer, run natively: a probe must see `al` 3, `r11` its own address and the
call's arguments, after moves that are cycles and that run through `r11`, with
values saved around the call; and deep values (`tier0_deep_test.c`): eleven
functions that fill the depth tables and cross their line with an entry of each
kind (a dropped value, `?:`, a fused compare and branch, floating values,
division by a local and by a value and shifts by a register, a frame pointer's
14, both stacks at once, rsp moved past the line, a conversion past the
floating line, a realigned frame's 14 and a realigned frame with a base's 13,
an allocation past them that keeps rsp aligned to 4096), run natively against
the file's model; and the
operands: `tier0_validate` refusing each
stream whose entry's last operand has another class than the entry before it,
which both paths read for it, and taking the rest (postorder, and values left
under a store of their class); and the stack: `tier0_validate`'s rules for
`ALLOCA`, `STACK_SAVE`, `STACK_RESTORE` and `FRAME_ADDRESS`, a frame pointer's 14 integer
depths (the 15th value in its deep slot), and a function that allocates 64 KB and up a thousand times, each in a
scope of its own, around calls with stack arguments, run natively; the frame's
address (#148): a callee's saved rbp its caller's `FRAME_ADDRESS`, unmoved by an
allocation, run natively; and the
aggregates at locations written by hand, 14 fixtures (each kind of general and
XMM part, the high eightbyte alone in each kind of register and on the stack,
the low eightbyte alone of a larger one on the stack, SPLIT both ways, an ignored one, aggregates on the stack whole, in place and
misaligned, and results in each kind and order of register, short ones, a
vector and through a hidden pointer), run natively both ways: as calls, whose
probe records every register and stack slot it gets from bytes that end or
start at a `PROT_NONE` page, and as functions, which a driver calls and which
copy their parameters out; and `tier0_validate` refusing 61 breaks of their
rules, as both paths do (the end reached without a RETURN of an aggregate
result, by falling off it, a jump or a branch, a hidden result's RETURN of
nothing, two parts or two parameters in one register, and registers outside
the convention's, among them), and a function of value parameters in one
register or outside the convention's; then tail calls (#147, `tier0_tail_test.c`):
a function per aggregate fixture that passes its parameters on to the probe by a
`TAIL_CALL`, in five frames (plain, an `ALLOCA`'s frame pointer, saved registers
across a call of a function that clobbers every register a call may, a realigned
frame and one with a base), run natively from a driver: the probe must find the
arguments where their locations say, the driver's slots past them, rsp and the
callee-saved registers as the driver left them, and what it returns must reach
the driver; and `tier0_validate` refusing each of the tail calls' rules once.
Every function must pass `tier0_validate`, both
paths must compile it to the same records, branch columns and references, and
the records must pass `x86_validate` and the branch columns
`branch_sizing_validate`, and every record but an X86 entry's must have a form
`x86_semantics.c` covers (#221: what the effects' consumers need of Tier 0's
code), here and in the x87, 128-bit, vector and deep checks below. Each program
is then sized, encoded, loaded into
fresh pages and run in a child process on eight inputs (stopped by an alarm
after 10 s), and its result and every global's bytes must equal what the test's
interpreter of the typed stream gives (the interpreter converts floating values
to integers as cvttsd2si does). The shapes but one function's also reach
symbols through the access column: globals through the GOT and thread-locals
of both TLS models (`_Thread_local` in C), and functions' addresses through the
GOT. The native run gives each symbol a GOT slot and the thread-locals a block
of their own, which FS points at for the entry's call alone. An `access` check
before the programs compiles every global kind of every class by each access:
both paths alike, with the access's references, and validation's rule. After
the programs, the `x87` check (`tier0_x87_test.c`, #136's item 8) compiles long
double code by both paths into the same records, encoded by both encoders into
the same bytes and run natively, called from C through function pointers, so
that clang's code is the oracle of the convention and the x87's arithmetic of
the values: each break of the class's rules refused at its entry; functions of
long double parameters and results (on the stack around an integer in a
register, a struct of one returned in st0 and one received from C into its
object, long doubles to a variadic C function, one kept across a call); the
conversions to and from the 64-bit integers and `int` on named cases around
2^63, at the limits and on fractions, each called under rounding upward, which
must truncate and leave the control word as it was; and
`--count` random programs of statements writing long doubles and what converts
from them (integers, floats, doubles, compares) from inputs of every class,
through constants, arithmetic, `?:`, `DUP`, `STORE`'s kept value, conversions
both ways, calls of C functions and frame objects (`LOCAL`, `STORE_LOCAL` and
`SET_LOCAL` of the class on objects of 16 bytes and more), against the check's
own stack machine. The programs that compiled, ran and matched must execute every
kind and conversion of the class and both arms of a `?:`. Any refusal except a
random program's excess depth fails the check. Then the `int128`
check (`tier0_int128_test.c`) does the same for the 128-bit integers, I128 and
U128, against clang's `__int128`: each break of the classes' rules refused at
its entry; stacks of a memory class's values to the last 16-byte slot of the
integer table, one and two past it and 40, which run (past the table through
the deep slots, #780), of 238, which compiles, and of 239, past the deep slots
with the output's address, which both paths refuse (for long double too);
functions of 128-bit parameters and results in register pairs and on the
stack, called from C, and calling C; each class's compares and shifts on every
pair of ten boundary values, against C's own operators too; and `--count`
random programs over every kind of the classes, frame objects as the x87
check's, shifts by constant and variable counts and conversions to and from the
other integers, under live values where
`MUL` saves rax and rdx and a shift rcx. Any error but a random program's depth
past the deep slots fails it, and only the entries that ran in the programs both
paths compiled, that ran and matched the check's stack machine count toward its
coverage, which must reach each class's every kind, conversion and save and
both arms of a `?:`. Then the `vectors` check (`tier0_vector_test.c`) does the
same for VECTOR values and `X86` entries, its rows' model plain C (each
instruction's elements as its definition has them): the rows' rules
(`tier0_operation_valid`: each of 20 breaks refused, integer operands narrower
than their fields among them, every row of the check and the narrow operands'
neighbours taken) and the stream's, each break refused at its entry (a vector
converted and two compared among them); each row under no
live values, under 8 floats and 3 integers, and under as many floats as leave
its VECTOR registers past the XMM ones (EVEX rows) or at a VEX row's edge, and
one past it and a scratch register past the integer table, which both paths
take through the deep slots (#780); functions of vectors of 16, 32 and 64 bytes in XMM, YMM and ZMM
registers and on the stack, called from C and calling C (a vector argument on
the stack among them), a float parameter's register that a row of no records
loads its vector operand into, and a 16-byte operand that a ZMM form reads
whole, its upper bytes zero; and `--count` random programs of vector (of 4, 8,
16, 32 and 64 bytes, those of 4 and 8 through XMM forms that read them with
their upper bytes zero), integer and float statements, masked stores and
dropped vectors over the rows (operands and results on both stacks, masks,
scratch registers, `/is4`, memory and VSIB operands, a row of no records, moves
between general registers and vectors of 4 and 8 bytes), constants, frame
objects of each width, DUP,
STORE's kept value and `?:`, under live values on both stacks, every output
byte against the check's stack machine. Its coverage must reach every VECTOR
kind at each width it has, every row and both arms. Then the `validation`
check (`tier0_validate_test.c`, #569): the validation's vector path
(`tier0_validate_accepts`) against the scalar validator, which must agree on
every stream but where the vector path leaves one to the scalar validator by
design (a backward arrival with values, the arrivals of `x ?: y`, its limits).
Every kind (NONE, X86 and one past the kinds among them) of every class after
no operand, one or two of every class, with values at its kind's bounds and
past them, in a function of every kind of local, direct and indirect calls,
a result object, a long double result, the access column and a label, with
and without the memory classes (562,944 streams); joins of `?:` and `&&` at
depths and across blocks, loops, values at the end; values carried across
64-entry blocks at every depth to 40, calls spanning blocks, the limits, and
more than 128 locals and calls (the gathers); each stream with its columns
ending at a `PROT_NONE` page; each field of the locals', the calls', the
arguments', the parameters' and the result's tables at its edges, on a plain
function and one with a result object; and in every program above, the
vector path must take each function, and one change of each in every fourth
program (a kind, a class or a value, two entries swapped, one removed or
repeated) only where the scalar validator takes it. Then the `masks` check
(`tier0_mask_test.c`, #179): `--count` random programs over the mask class,
each compiled by both paths into the same records and both encoders into the
same bytes, linked with a callee whose masks fill k1 to k7, and run natively
where the host has AVX-512 F, VL, BW and DQ, every output byte against the
check's stack machine and, for the programs without rows or frame objects,
`interpreter.c`'s: masks of every width loaded, converted from every integer
class (constants, loads and calls' results) and back, in frame objects, under
DUP, STORE's kept value and `?:`, chains of 6 to 12 live masks (past k7, where
the logic meets masks in their slots), calls with masks live and past k7, and
rows by role (with scratch registers, from and to integers, past k7) and by
number (under live masks). Its coverage must reach every mask kind of each
width, conversions from each, every row, both arms, the mask stack past 9,
both kinds of call and a row past k7. A zero-masked vector move whose
decoration is a role runs under no live mask and under seven, against C; and
each of the class's rules is refused (`tier0_validate`'s and
`tier0_operation_valid`'s), with its valid neighbours taken.

With `--cross-check`, which needs clang (`--clang PATH`), `size`, `as` and
`ld`, each of `--cross-count` (60) programs of the larger shape, keeping to what C defines
(with `-fwrapv` and `-fno-strict-aliasing`, as the repository builds), is written as Tier 0's object (the link representation and the object writer)
and as C, which clang -O0 compiles; a generated harness links both, runs them
on the same inputs and compares the results and every global (a floating
one's NaNs alike whatever their bits, as C leaves them), then times both; then
as many programs of the records shape linked mixed, their even functions Tier
0's and their odd ones clang's, so that calls pass and return structs and
unions by value between the two compilers both ways, and as many of the deep
shape. It prints the code's sizes (clang's `.text` against Tier 0's functions) and the
run times. A shared library generated beside them (GNU as source, which `as`
and `ld -shared` link) defines the globals Tier 0 reaches through the GOT, so
that the executable's slots for them are the loader's to fill (`GLOB_DAT`, and
`TPOFF64` for a thread-local's offset). The
files go in a directory under `$TMPDIR` (or `/tmp`), kept with
`--keep` or on a failure. The 9700X job's test step runs it with
`--cross-check`.

## Backend benchmark

`./build/compiler benchmark-backend [--entries N] [--seed N] [--only
vector|scalar|sizing|encode|validate|validate-scalar|choose|choose-scalar]
[--after-scalar MS]` (about 4 s) generates programs
until their functions hold `--entries` typed entries (262,144), checks that both
of Tier 0's paths give every function the same code, then reports 21-pass
medians, per typed entry and per record, of `tier0_compile`,
`tier0_compile_scalar`, `size_branches` on their records, `x86_encode` of the
sized records, `tier0_validate` and `tier0_validate_scalar` (#569), and the
immediates' form choice on each function's own requests as Tier 0 makes them
(`x86_choose_forms` and `x86_choose_forms_scalar`, #221, from
`Tier0Function.choice_requests`, which copies them into the code: read from
there, colder than in `tier0_compile`'s own call): nanoseconds, cycles,
instructions, IPC and the throttle (PMCx1A0's umask 0x08, #199), with the share
of entries deferred, the functions the validation's vector path takes alone,
and the choice's requests (per entry, the share whose form it kept, gave another
encoding of the instruction or another instruction, and the bytes it saved
against each template's imm32 form). `--only` runs one variant alone, and
`--after-scalar MS` runs the scalar twin for MS ms before each pass of the vector
path, of the validation and of the choice. Run it under the 9700X's lock with `taskset -c 2`.
`docs/tier0.md` has the numbers. The default 9700X job runs it after the tests,
pinned to one allowed CPU; like the others, its measurements are informational.

`./build/compiler benchmark-backend-source [--only
vector|scalar|sizing|encode|validate|validate-scalar|choose|choose-scalar]
[--after-scalar MS] [preprocessor options] file...` (about 1 s for `sqlite3.c`)
measures the same variants on real C. Each file is checked as the driver checks
it, and the definitions `-c` lowers are lowered and emitted for the target its
options give (`-march`, the `-m` features: #1041), each function with a target
attribute for the attribute's, outside the time;
their typed streams, with the symbols' access the emission gave them
(`check_program_access`), make one corpus with the other files' functions, which
both paths must agree on before the same medians are reported. A file the
checker rejects, or whose checked program or emission refuses a feature by
name, is left out with its reason; one that fails otherwise (it doesn't
preprocess, a resource runs out, a function doesn't join the corpus) fails the
command, and so does a run left with no function or no typed entry, or a
timed run that failed on a function (both benchmarks: `tier0_benchmark_measure`),
and both refuse an unknown `--only` wherever it comes on the line. Its last line
totals the files measured, left out and failed and the measurement's outcome
(`measurement=reported`, `failed` or `none`), as the exit status takes them.
`test-checker` checks these outcomes (`tier0_source_benchmark_test.c`), with the
resources' failures injected, and the corpus's add at its capacity. Run it under the 9700X's lock with
`taskset -c 2`. The default 9700X job runs it on the frozen `sqlite3.c`, pinned
to one allowed CPU, where a pull request changes what it measures.

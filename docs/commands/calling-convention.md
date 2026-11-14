# calling-convention

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/calling_convention.c` is the System V AMD64 calling
  convention (#137, contract 3 for #136): every `TypeId`'s classes as clang 23
  classifies them, a row of 16 offsets per type, records and arrays folded
  four at a time in vectors with an exact twin; arguments' and results'
  registers and stack slots as clang and LLVM's `CC_X86_64_C` give them, by
  prefix sums 16 arguments at a time with an exact twin; and `va_arg`'s plan.
  `calling_convention_test.c` holds its tests (`main.c` includes both;
  `./build/compiler test-calling-convention` runs them) and
  `calling_convention_benchmark.c` provides `benchmark-calling-convention`.
  `docs/calling-convention.md` records the design, where clang's LLVM types
  and GCC decide otherwise than the classes, and its 9700X measurements.
## Calling convention tests

`./build/compiler test-calling-convention [--seed N] [--count N]
[--cross-check] [--signatures N] [--clang PATH] [--gcc PATH] [--buster PATH]
[--keep]` (about 0.05 s on the 9700X, and 4.8 s with `--cross-check` before
its compiled runs) checks
`calling_convention.c`, by its vector paths and their twins at 16-, 32- and
64-byte vectors:

- merge: clang's merge and post-merge, written again as clang writes them,
  against the module's 64-entry merge table and post-merge, on every pair of
  classes;
- fixtures: types whose classes, pass bits and results clang's IR shows
  (scalars, vectors, complex types, `_BitInt`s, records with unaligned and
  zero-size members, bit-fields, flexible array members, `_Atomic` and
  `nullptr_t` members, `i128` storage, which LLVM never splits, whatever the
  high eightbyte under an INTEGER low one and refused under another, runs of
  bit-fields that are an `i128` and that aren't, bit-fields' access units
  as clang's IR has them (merged units in an INTEGER high eightbyte, which
  widen a narrow SSE low part or don't, units that decide a union's storage
  member, units stopped by a struct's end, a `char` or a zero-width
  bit-field, and an `_Atomic` of a struct of no size whose element holds one),
  unions whose storage member a zero-length array of a larger record or of a
  record with a flexible array member and a size decides (each of the walk's
  rules alone among them), the narrow low parts clang widens into a general
  register and the unions stored as a `__float128` whose `fp128` low part it
  widens, and the records SSE, SSEUP of 17 to 63 bytes it passes as `<size /
  8 x double>`: 17–23 bytes in xmm0, with full C-sized stack copies, and the
  larger irregular widths refused;
  and the `_Atomic` types whose LLVM types decide: of types of no size, which
  LLVM makes larger (to 32 KB, and refused past the alignment the module
  keeps), as the records ending in one, with their slots, the records it lays
  out otherwise than C, and vectors LLVM keeps whole, splits or, of bools,
  passes as integers; and the vectors that aren't `_Atomic` which LLVM breaks
  up or packs: of bools, with their integer storage and layout, refused from
  16 bytes up where clang passes them as `<N x i1>`, with the records of one,
  and smaller copies past 128 (`SMALLER`), of `_BitInt`s narrower than a byte,
  of long doubles (refused in registers, st0 and st1 as results), of
  `__float128`s, of `_BitInt`s of 128 bits and more, and of lengths that
  aren't a power of two in memory, returned in rax, rdx and rcx, in xmm0, xmm1
  and st0, or through a hidden pointer), and calls whose locations clang and
  LLVM give (an `UNCOUNTED` argument that splits the next record, an
  `__int128` without two registers left, a hidden pointer, a record of a
  flexible array member alone as an `i0`, a struct whose narrow low part takes
  a general register, unions stored as an `i128`, which clang counts as two
  registers with a NO_CLASS or SSE high eightbyte, a union whose storage
  member a bit-field's access unit decides, whole on the stack, an `_Atomic`
  of a struct of no size whose access unit is a byte, in a slot of 8, runs of
  bit-fields where an
  uncounted `_Atomic int` leaves one register, split or whole on the stack,
  the larger copies' slots, byval or an integer by clang's count, `_Atomic`
  bool vectors, a vector of bools that isn't `_Atomic` returned through a
  hidden pointer, a result LLVM demotes to a hidden pointer clang doesn't
  count, and 513 bools, an 80-byte copy, and as a result a hidden pointer
  clang doesn't count), `va_arg`'s plans for an aligned `long` and an `i128`
  with an SSE high eightbyte, and the levels across calls (a record larger
  than 64 bytes that waits for a `LARGER` member, and records of no size that
  an `_Atomic` of them in a later call reads: a flexible one, and two that
  hold a struct of 128 bytes by an array of none, which wait for what its
  alignment reads), the walk without scratch memory for its stack, after which
  every type must be classified and an `_Atomic` it aligns be right or
  refused, and the walk over 64 unions of two members of the one before, 2^64
  paths, under an alarm;
- worlds: random worlds of `--count` types (2,000 by default), classified by
  both paths into tables that must be identical, and every type at every
  offset it can be at compared with an oracle of the test's own, clang's
  `classify` transcribed with an explicit stack of frames; then classified
  again by the twin, every type at once, into the same table (zero-length
  arrays of large records among the types, which wait in the levels for the
  types their alignment reads), which a world without `_Atomic`s of types of
  no size checks, its levels ordered by the waits alone;
- GCC's bit-fields, whatever `--seed` says: the rule on fixed fields, then
  GCC's worlds (the record runs' and the compiled run's) from the fixed seeds
  1 to 16, which must hold no bit-field aligned below its type's, where GCC and
  clang lay one out apart, and clang's worlds from the same seeds, which must
  hold some;
- assignment: random calls over a world's types, named and variadic, by both
  paths, which must give the same locations and totals, also with the columns
  ending at a `PROT_NONE` page; and calls of 65,542 `long`s and of 32,767 and
  32,768 two-register arguments, around the most the vector path's 16-bit
  register sums count (`BUSTER_ABI_COUNTED_ARGUMENTS`), past which it leaves a
  call to its twin.

With `--cross-check`, `tests/calling-convention/short-sse.c` first checks
#2113's 17–23-byte SSE/SSEUP records against clang in both directions, with
Tier 0 and strict Tier 1, by vector and scalar paths, at x86-64, x86-64-v3
and x86-64-v4. Its 16-byte control, 17/23-byte boundaries, aligned20 and
wrapped23 records check arguments, returns, exhausted vector registers and
`va_arg`. Register transport compares the first 16 bytes clang defines;
stack transport compares every C byte and following scalars check the
24-byte overflow stride. Every target is compiled and linked; execution is
skipped only when the host lacks its features. Run from the repository root
so the fixture path resolves.

The remaining `--cross-check` runs need clang 23 and GCC 15 or later on `PATH` (or
`--clang PATH` and `--gcc PATH`; 16.2.1 is the qualified 9700X version) and
libatomic: for each compiler and vector width (the C built with `-march=x86-64`,
`x86-64-v3` and `x86-64-v4`), a world and
`--signatures` signatures (400) over its passable types, random but for
clang's first five, which pass runs of bit-fields where its count of registers
runs ahead of LLVM's, are written as C (the types, every argument's and
result's bytes, a C callee and a C caller of ours) and as our side, a caller
and a callee per signature as `X86Instruction` records through `x86_encode`,
`link_add_code_fixups` and the object writer. GCC is probed once before the
runs; when it is missing or older than 15, the command prints
`cross-check gcc: skipped: needs gcc >= 15, found 13.3.0` (or `not found on
PATH`) and skips GCC's runs, while clang's runs still run and fail on wrong
output. The compiler's driver links them
and the program runs: each side checks every argument, result, hidden pointer,
`al` and `va_arg` value against the bytes, both ways. Four compiled runs
(#137), against clang and against GCC at 16 bytes and at 64 (x86-64-v4), put
this compiler on our side, and each links our side by Tier 1 too (#177's
cross-linking at Tier 1: `--tier1 --verify`, without a frame pointer and with
`-fno-omit-frame-pointer`), whose program must pass the same checks, its line
saying how many of our side's functions Tier 1 compiled: a world of what its C passes (integers to 64 bits,
`_BitInt`s of every width (past 128 bits in memory, a limb at a time), `__int128`
and `unsigned __int128`, enums, pointers,
`nullptr_t` in clang's, `_Float16`, `__bf16`, `float`, `double`, `long double`,
their complex types (`_Complex __bf16`, which the usual arithmetic conversions
make, and `_Complex __float128` in clang's),
GNU's complex integer types (of `__int128` too) and in clang's complex
`_BitInt`s of every width, `__float128`, in clang's `_Atomic`s of every type C
makes atomic, but `_BitInt`s (themselves, records holding them and aligned
typedefs of them, as in the record runs), the vectors of 4, 8, 16, 32 and 64
bytes the producer lowers (with SSE's registers at 16 bytes, AVX-512's at 64),
and those of 1 and 2 bytes and of `bool`, which it passes by their bytes (#137:
`check_lower_transport_type`; what computes with them is
`c_check_program_transport.c`'s, outside these runs), and the
records, arrays and aligned typedefs of them, aligned to 64 as the clang
worlds' are, with C23's
enums of a fixed underlying type, structs whose only named member is a
flexible array member and bit-fields' attributes after their widths), and
signatures with long doubles, 128-bit integers or vectors within Tier 0's
depths (12 integer arguments, a `_BitInt` of 65 to 127 bits never the 12th,
an `_Atomic` or an aligned typedef of one never past the 8th, a realigned frame's rbp
past them, and 15 floating ones), and the others past
them, in its deep slots (#712), the
first against clang `(nullptr_t, long × 5, U, long)` for a union U whose high
eightbyte is padding, which passes in one 8-byte stack slot before the last
`long`; our caller and callee per signature as C, which this executable (or
`--buster PATH`) compiles for the run's target with `--verify -c` and with
`--scalar` into the same bytes, our callee storing each named parameter as it
arrived, then writing each back from the data, the last first, and storing it
again, and the C side checks both stores and every argument and result both
ways (the al and the hidden pointer, which C doesn't
name, are the record runs' checks); the 64-byte runs execute where the host has x86-64-v4's
features, and elsewhere compile and link. Two
extension runs (#475's review), with plain char signed and with it unsigned
(`-funsigned-char`, `TypeTable.char_unsigned`), pass and return the types clang
extends (the character types, enums of a fixed underlying char and signed char,
short, unsigned short and bool) between clang's code and ours, both ways, at
values with bits set past the type's: records that return the 32 bits an
argument arrives in (the extension the module's flags say clang's caller
makes), that extend as the flags say before calling clang's callee, which takes
the 32 bits as they come, and the other way round (the control), and C that
this executable compiles with each signedness by both paths. clang's callee
returns a narrow result unextended, though its IR says `signext` or `zeroext`,
so a caller extends it itself, as Tier 0 does. The twelve runs (clang's, then
GCC's, each at 16, 32 and 64 bytes, then the compiled ones, then the extension
ones) go at once, a child process each, and the test prints their lines in that
order; a run that fails keeps its files.
GCC's worlds leave out
what GCC lays out or passes otherwise than clang (the test names each, GCC's
own bugs among them, and `docs/calling-convention.md` lists them), and clang's
a type in registers that holds an array of elements of no size, on which clang
23 crashes now and then, an
aligned typedef of an `_Atomic` vector of bools, whose atomic loads crash it,
and a union of a vector LLVM packs into fewer bits than its bytes, whose copies lose
the other members' bits; an argument clang would pass as an `i0` becomes a
long (its passes crash on them now and then), as does an unnamed one clang's
`va_arg` reads elsewhere than its call puts it; bytes clang doesn't pass
aren't compared; and the C side copies a `SMALLER` type's LLVM bytes, all that
clang's objects of it hold. The files go in a new directory under `$TMPDIR`
(or `/tmp`), removed unless `--keep` or a failure. The 9700X job's test step
runs it with `--cross-check`.

## Calling convention benchmark

`./build/compiler benchmark-calling-convention [--seed N] [--count N]
[--only classification|assignment] [--path vector|scalar] [--after-scalar
MS]` (about 1 s on the 9700X) checks that each vector path of
`calling_convention.c` agrees with its twin (the classification's rows, pass
bits, stack bytes, alignments, storage, layouts and results; the
assignment's locations and calls), then reports 21 interleaved-pass medians
per item (time, cycles, instructions, IPC, branch mispredictions and the
throttle, PMCx1A0's umask 0x08, #199):

- classification: a table of `--count` types (32,768), records and unions of
  0 to 8 members mostly, with the pointers, arrays, vectors and aligned
  typedefs their members have, classified from nothing by `abi_classify_all`
  and its twin at 16-, 32- and 64-byte vectors, per type and per record
  member; then by `abi_classify` and its twin on the batches a front end
  hands them: windows of the 1, 4 or 16 types made since its last call, a
  call per dependency level;
- assignment: `--count` calls over those types at 16 bytes, most of 0 to 6
  arguments, some of 4 to 14 and a few of 15 to 22, one in four variadic, by
  `abi_assign` and its twin per argument, with the share of arguments the
  vector path left to its twin and why (from a mirror of its rules,
  `abi_benchmark_deferred`, to keep in step with `abi_assign`); then the same
  shapes with integers, pointers and doubles alone (`assign-simple`).

Each run takes the whole table or every call once. Below 16,384 types the
branch predictor learns the table from run to run: with 4,096, the
classification mispredicts 3 to 10 times less often and runs up to 2.3 times
as fast. `--path` times one path alone, back to back, and `--after-scalar MS`
runs the twin for MS ms before each sample of the vector path. Run it under
the 9700X's lock with `taskset -c 2`; `docs/calling-convention.md` has the
numbers. The default 9700X job runs it after the tests, pinned to one allowed
CPU; like the others, its measurements are
informational.

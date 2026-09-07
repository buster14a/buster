# libc-test compatibility harness

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

[Compatibility harness index](../compatibility.md). Read only the harness you are working on; pins, measurements, and past failure counts describe the revision recorded below and must be rechecked before reuse.

The retained narrative spans multiple revisions. References to portable
substitution for sixteen architecture units, a trapping `setjmp` as the
`dlopen` blocker, and a project-owned thread-pointer replacement on static
link lines describe earlier harness states. Check the current manifest and
link construction in `build.c` and reproduce the relevant stage before
treating any of those descriptions as a current failure or required workaround.

### libc-test

Given a second path, the harness then runs libc-test, musl's own test suite.
It carries no tags and no version file, so the pin is a bare commit on
upstream's master, `68edb8bd73dab8147ee54c8bec638f4d2b3cff37`, verified
pristine the same way musl's checkout is. Upstream's canonical remote is
`https://git.musl-libc.org/cgit/libc-test`; `https://repo.or.cz/libc-test.git`
is the same history and is what a clone usually resolves to. Upstream's own
makefile builds in-tree, which is exactly what the pin exists to prevent, so
the harness drives the sources from where they sit and writes every artefact
into `libc-test/` under the run directory.

One generated header stands between the suite and musl: `src/common/options.h`,
which upstream derives by preprocessing `options.h.in` against the libc under
test and turning what survives into defines. The harness runs that recipe --
the four `sed` expressions written at the top of `options.h.in` itself, which
upstream's makefile spells again in awk -- and both sides compile against the
one copy. The reference preprocessor generates it, and that is not a statement
about which compiler is trusted: `ide cc -E` emits a token stream with the line
structure removed, the whole file coming back as one line of space-separated
tokens, and this recipe like musl's own three is line-oriented. The header
describes the musl under test rather than either compiler, so one copy is also
the right shape for a comparison.

One flag set again drives both compilers, and it is upstream's own
`config.mak.def` `CFLAGS` minus what only one of them takes, plus the
`-nostdinc` and musl include set that makes a test see the musl under test
instead of the host libc: `-std=c99 -nostdinc -fno-builtin
-fno-strict-aliasing -fno-stack-protector -D_POSIX_C_SOURCE=200809L -O2 -g0`,
with `-D_XOPEN_SOURCE=700` added for `src/api` because that is what upstream's
own api rule adds. `-pedantic-errors` and `-frounding-math` are dropped
because Buster rejects both, which is the same `tryflag` reasoning musl's flag
set is built on; the warning flags and `-g` change nothing about what is
compiled; `-D_FILE_OFFSET_BITS=64` is marked glibc-specific by upstream and
musl has one `off_t`; and the `-lpthread -lm -lrt` link libraries have no
meaning where there is one archive and no compiler driver. Clang alone gets
`-Werror=implicit-function-declaration`, which is upstream's and is not a
divergence: Buster rejects an undeclared callee outright, so the flag is what
makes the two compilers agree about a missing declaration -- which is most of
what the api subset is testing.

Two objects sit on every static link line that neither archive supplies, and
each side uses its own. `crt1.o` is musl's startup object, which is not an archive
member on either side — a program links it explicitly and `libc.a` separately —
so a libc-test program is one compiler's code from `_start` down. If Buster
ever stops producing it the reference's copy stands in on both sides, and the
`LIBCTEST_MANIFEST` line says which of the two arrangements is in force rather
than leaving it to be inferred. That is the only such object now. A
project-owned `__set_thread_area` used to sit beside it, because the harness
substituted the portable C sibling for every architecture-assembly unit and
that sibling is written for architectures with a `SYS_set_thread_area` system
call -- x86-64 has none, so it returned `-ENOSYS`, `__init_tp` failed, and
`__init_tls` crashed the process before `main` in *both* archives. Both
archives now hold musl's own `arch_prctl(ARCH_SET_FS)`, and the replacement
is gone.

Nine units are built differently, and upstream's sibling `.mk` is what says so.
It is a make fragment of one to four lines, and the harness reads it for the
three facts it states rather than interpreting it: `$(N).LIBS:=$(B)/$(N).so`
means the unit is a shared object instead of a program, `-rdynamic` means its
program has to export its own symbols because the library it opens resolves
against them, and any other `name.so` in the file is a sibling its program
needs beside it. Each of the four shapes upstream writes states its fact in a
token, so the file is scanned for tokens; anything a later release adds that
this does not understand shows up as a unit that fails to build rather than as
one quietly held out, which is the trade this stage wants.

What comes out of that is three link shapes rather than one. A shared-object
unit is compiled `-fPIC -DSHARED`, which is upstream's own `.lo` rule, and
linked `-shared` against the shared musl; upstream never runs one, so both
sides building it is the pass. That flag is the same code model on both sides
now, and this stage is where it is measured against a real linker: what a
shared object demands of the code generator is a thread-local model the loader
can place and a GOT-indirect reference for every other symbol another object
could interpose, and `ld` says so by name for each one that is missing. A
program with a `.mk` is linked against the shared
musl rather than the archive, with that musl as its interpreter and its own
directory as its run path, which is upstream's `-rpath='$ORIGIN'` spelled as a
directory this harness knows. Every other program is linked `-static` exactly
as before. The thread-pointer replacement is on the static link lines only: the
shared musl already carries it, and a second definition would be a duplicate
symbol rather than a substitution. Each side's dynamic programs are laid out
under `src/<subset>/<stem>` in that side's own tree and run from its root,
because three of the nine find the library they open by a path rather than by a
name -- two spell it from the working directory and one derives it from
`argv[0]` -- and that layout is upstream's own.

Each unit is classified rather than merely passed or failed, because most of
the suite cannot be reached yet and a single total would hide which wall it is
behind. The classification is derived from the checkout and from what the two
sides do, not written down:

- `excluded-reference` — the Clang-built musl of this same configuration
  cannot compile, link or run it green. It says nothing about Buster, so it is
  held out of the comparison. This used to be most of the suite -- musl's
  x86-64 assembly was in neither archive nor either shared object, so `fenv`
  was a stub, `clone` was absent and the thread tests hung out their
  ten-second deadline, and `setjmp` was a trap so nothing that called `dlopen`
  could run -- and building the assembly is what released it: 144 of the 199
  `src/math` units and 34 of the 69 `regression` ones were in this state, 31
  and 1 remain, and the suite's run time fell from 53,6 seconds to 3,9.
- `blocked-compile` — Buster cannot compile the test itself.
- `blocked-link` — Buster compiled it and the link against the Buster-built
  libc could not be made. Every undefined symbol is recorded, not just the
  first: the link stops at the startup object every time, so a ranking built
  from first symbols would name one symbol and hide the rest. A link that
  failed for another reason — a relocation a shared object cannot carry, or a
  sibling shared object this side did not build — carries the linker's own
  words instead of claiming zero unresolved symbols, and `ld` reports its
  warnings before the error that stopped it, so the line kept is the first one
  that is not a warning.
- `fail` — it ran, and its transcript or exit status differs from the Clang
  reference. This is the only state that is a defect in generated code.
- `pass` — it ran and matched; in `src/api` it compiled; in a shared-object
  unit both sides built the library.

Five units were what building the assembly newly put in front of Buster, and
each was filed rather than absorbed. Three are closed: `functional/setjmp`
compiles once an aggregate can be assigned across a `volatile` qualifier
(issue 735), and `functional/pthread_robust` and
`regression/pthread-robust-detach` pass once a walked-through aggregate member
is not loaded (issue 737). The other two, `functional/tls_align_dlopen` and
`functional/tls_init_dlopen`, joined the shared-object group that was already
there rather than being anything new; `tls_init_dlopen` is closed with the
rest of that group and `tls_align_dlopen` was one of the two the dropped
constructor held until issue 771 closed it. 243 to 388, and nothing that
passed before stopped passing.

Applying `ARCH_SRCS` whole moved the reference and not the suite. The Clang
archive holds musl's own x86-64 `sqrt`, `fabs`, the `lrint` family and the x87
`long double` remainders now instead of the portable files, and Buster's holds
the portable files for the sixteen it cannot compile. `src/math` is the subset
that would show it -- these are its implementations -- and all 168 of its
passing units answer the same as before, which is what a correctly rounded
portable implementation standing in for a hardware one should do.

Buster compiles every unit first and unconditionally, before the reference
decides reach, because its diagnostic is inventory in its own right, and
`buster_compile_failed` on each `LIBCTEST_SUBSET` line reports that count
independently of the state. The `long double` test tables under `src/math`
are what that separation was for: 63 of the 199 units failed to compile while
only 21 of them were classified `blocked-compile`, the rest being
`excluded-reference` already, so the state counts alone would have hidden
two thirds of one compile gap.

The four subsets are reported separately rather than as one total.
`src/api` is compile-only by upstream's own design: its units are declaration
conformance checks with no runtime, and upstream links a single `main.exe`
from all of them, so there a successful compile is the pass. `src/functional`,
`src/math` and `src/regression` are compiled, linked and run, and a run is
green when the program exits zero and prints nothing, which is the protocol
upstream writes down in its README. Upstream's `src/common` becomes a support
archive on each side, minus `runtest.c`, which is the process supervisor the
harness performs itself; upstream gives each test five seconds there, and the
harness gives it ten so a loaded runner cannot turn a slow test into a
classified failure. `src/musl` is not a subset: its one unit tests musl
internals through a header this build does not publish.

The gate is the passing count together with a hash of the newline-joined
`state subset/unit` lines of everything that did not pass, taken in manifest
order — the four subsets in the order above, each sorted by unit name —
pinned as `LIBC_TEST_EXPECTED_PASSING` and `LIBC_TEST_EXPECTED_STATE_HASH`
in `build.c` and printed on the `LIBCTEST_INVENTORY` line, which is what a
deliberate rebaseline needs. The hash covers the excluded states as well as
the blocked ones, so a change in the reference's reach is a deliberate edit
too.

Every unit that is not passing prints a `LIBCTEST_UNIT` line carrying its
state and the reason — a compiler diagnostic, the count and the first of the
unresolved symbols, or the reference's own reason for being out of reach — and
each subset then prints one `LIBCTEST_SUBSET` line with its counts and its
compile, link and run time. `LIBCTEST_SUPPORT` reports upstream's support
library per side, including its archive time, and any support unit that did
not compile is named on a `LIBCTEST_SUPPORT_UNSUPPORTED` line. All nine
compile on both sides today, so the line is absent.

`LIBCTEST_BLOCKER` is the stage's most useful output while most of the suite
is out of reach: the symbols the Buster-built archive could not supply, ranked
by how many tests wanted each. It is the work list in the order that unblocks
the most tests, and it is why this stage grows by itself as the compiler
improves rather than needing to be extended by hand.

The suite is built twice, and the second time is the only coverage the NONE
register allocator has against a whole libc. Both compile inventories — musl's
1356 units and libc-test's 424 — are compiled under FAST, and building either
of them four times over would produce four object sets and one answer. All
four allocators do run on the freestanding probe, each required to reproduce
the reference transcript byte for byte, but that is one program of about two
kilobytes against FAST's 424. So one subset is built and run a second time
under `-fregister-allocator=none`: `src/functional`, 77 units of ordinary C
whose programs run in a couple of seconds. Compiling the musl manifest a
second time under NONE and gating the unit count would have been cheaper and
could only ever have caught a refusal — the half of the compiler the register
allocator is not in — where this compiles, links, runs and compares generated
code.

Only the test's own object is rebuilt. It links against the same Buster-built
libc, startup object and support archive as the pass above, so a unit that
answers differently here answers differently because of the allocator and not
because of anything underneath it. Each unit is classified from scratch,
against the same reference transcript the first pass recorded, rather than by
comparing the two Buster passes: a unit FAST cannot compile says nothing about
NONE, and the reference's reach is the one thing the two passes do share. The
gate is its own count and its own hash — `LIBC_TEST_ALLOCATOR_EXPECTED_PASSING`
and `LIBC_TEST_ALLOCATOR_EXPECTED_STATE_HASH`, printed on
`LIBCTEST_ALLOCATOR_INVENTORY` beside a `LIBCTEST_ALLOCATOR` line of counts and
cost, with one `LIBCTEST_ALLOCATOR_UNIT` line for each unit that is not
passing — because folding it into `LIBC_TEST_EXPECTED_PASSING` would make a
difference between two allocators and a regression under both the same moved
number. The pass runs whether or not the first inventory moved, so one run
reports both.

`src/functional` classifies identically under both allocators today: 74
passing, none failing, and the same three excluded-reference. That
agreement across 74 running programs is what the pass exists to be able to
state.

Today 388 of 424 units pass (measured 2026-08-30, on one machine).
Seventy-eight are `src/api`: 78 of its 79 units compile against musl's headers
under both compilers, and one — `api/unistd` — is held out because musl
defines neither `_PC_TIMESTAMP_RESOLUTION` nor `_SC_XOPEN_UUCP`, which the
reference fails on too. The other 310 are runtime tests that link and run
green: `src/functional` is 74 passing against none failing and 3
excluded-reference; `src/math` is 168 passing and 31 excluded-reference;
`src/regression` is 68 passing against none failing and 1 excluded-reference.
Every subset passes every unit the reference can run, and **no unit of the
suite is in any state but `pass` or `excluded-reference`**: nothing is blocked
on a compile, nothing on a link, and nothing answers differently.

Most of that total is the reference's reach rather than Buster's, and it
arrived with the assembly stage: both archives hold musl's own x86-64 assembly
now, so `fenv` is real, `clone` is present and `setjmp` is not a trap. The
number the assembly stage itself reaches is 377; the eleven above it are this
tree's, and nine of them are a fix — see the `LIBC_TEST_EXPECTED_PASSING`
note in `build.c`, which is where each step is written down.

The nine units with a sibling `.mk` are what this stage's newest counts are,
and eight of them pass. Both compilers build all four shared objects —
`functional/tls_align_dso`, `functional/tls_init_dso`,
`functional/dlopen_dso` and `regression/tls_get_new-dtv_dso` — and the two
programs that open one of them and had been waiting on it,
`functional/tls_init_dlopen` and `regression/tls_get_new-dtv`, run and match.
Two code models between them are why. Buster picks between all three ELF
thread-local models now — local-exec for a definition in an executable,
initial-exec for a declaration it does not define, general-dynamic under
`-fPIC` — so nothing here is behind a thread-local relocation `ld` refuses;
and `-fPIC` is the rest of the position-independent model, so a reference to
any other symbol another object could interpose goes through the GOT and a
direct call to one through the PLT. An unwind record was the last of those
references and the least obvious: an FDE named the function it describes,
where clang names `.text` plus an offset, and that is a PC-relative reference
to an interposable symbol like any other. `functional/dlopen` is
`excluded-reference`, because musl's loader saves a jump buffer around
`dlopen` and the reference dies on the same trapping `setjmp` the Buster
build does. The last two, `functional/tls_align` and `tls_align_dlopen`, were
neither code model: `tls_align_dso.c` is one `__attribute__((constructor))`
filling the table the test reads, nothing in this tree emitted `.init_array`,
and the attribute was accepted and dropped, so that object's `.text` came out
empty. Both pass since the attribute started reaching the object file and the
image (issue 771).

`LIBCTEST_BLOCKER` still prints nothing, and now neither does any
blocked-link or failing state: every unit in the suite compiles, links and
answers as the reference does under both compilers. The work list this stage
generates for itself is a list of missing components, and it is empty.

The last 22 blocked-compile units went together, 21 in `src/math` and
`functional/strtold`, when the x87 static-initializer folder stopped refusing
an overflowing literal and a division by zero. musl spells `INFINITY` as
`1e5000f` and `NAN` as `(0.0f/0.0f)` for a compiler that does not advertise
the GNU builtins, and every `long double` table libc-test writes opens with
one of them, so those units refused at their first element and all 22 pass
now. The state counts had understated that gap by three: 63 of the 199
`src/math` units failed to compile while only 21 were `blocked-compile`,
because an `excluded-reference` unit is classified by the reference's reach
and its own diagnostic never reaches a state count. `buster_compile_failed` on
the `LIBCTEST_SUBSET` line is the number to read for a compile gap, and it is
0 in every subset but `api` now.

The last two failing units, `functional/tls_align` and
`functional/tls_align_dlopen`, went with `__attribute__((constructor))`, and
they are worth keeping as the shape of a dropped feature: the attribute was
parsed and accepted, so nothing diagnosed it, and because the function that
carried it was static and nothing called it, the unused-static elimination
took the only function in the translation unit with it. The evidence was an
object with an empty `.text` rather than any diagnostic. Keeping the function
then exposed the gap behind it, which had never been reached: `__alignof__`
took an expression operand only for a compound literal, and that constructor
fills its table with `__alignof__(x)` over four thread-local objects. Every
one of the five units this stage started with is attributed, and no unit in
the suite is in any state but `pass` or `excluded-reference`.

`functional/fcntl` was the fourth of the original five to go, and it was a
lazy operand inside a call argument. Its child process exits on
`fcntl(fd, F_SETLK, &fl)==0 || (errno!=EAGAIN && errno!=EACCES)`, and the
`errno` reads came out ahead of the `fcntl` call that sets them, so the child
saw the value from before the call — 0, from the parent's own `TESTE` — and
reported the lock its parent held as not held. Nothing about `struct flock`,
the syscall wrapper or the fork was involved: Buster's own `fcntl.o` relinked
against the *reference* archive failed the same way, and the reference's
object against `libc-buster.a` passed, which named the one translation unit in
a single link rather than a bisect over members.

Two prepasses run over a statement before its expression is lowered — one
hoists calls (`c_ir_prepare_calls_discover`), one lowers parenthesized control
groups (`c_ir_prepare_control_expressions_step`) — and either one runs an
operand that only a taken branch should run. Only the call prepass had that
rule; they share one `CIrLazyOperandScan` now, and a prepass that jumps past a
group it will not enter folds the close it never sees back into the scan so
the depth it carries stays the group's own. A call argument is where this was
visible at all: an argument is lowered by the arithmetic core, which is what
runs the control prepass, while an assignment, an `if` or a `while` condition
reaches the condition machine directly and was always lazy — so
`f(x || (n = 1))` stored and `if (x || (n = 1))` did not.
`tests/basic_c_lazy_operand_argument.c` pins the whole class — both
short-circuit operators, both conditional arms, a call in a lazy operand, and
the eager groups that must keep running — under all four allocators.

Three of the original five went before it. `regression/sem_close-unmap`
and `functional/mntent` had one cause between them: neither `main` contains a
return statement, and reaching the `}` that terminates `main` returns 0
(C 5.1.2.2.3) where every other function's fall-off is undefined. The C
frontend terminated every non-void body's fall-off with the IR's unreachable —
`ud2` on x86-64, BRK on AArch64 — so both programs did all of their work
correctly and then died on the brace with SIGILL, exit status 132.
`sem_close-unmap` is nineteen lines ending in a bare `sem_post(sem);`, so
there was nothing else it could have been. `main` is the one function that
gets the implicit zero, decided once with its signature rather than by
matching a name at the terminator, and `tests/basic_c_main_implicit_return.c`
pins it under all four allocators: exit zero is reachable in that fixture only
by falling off the closing brace, so a trap faults and a bare `ret` exits with
what the last call left behind.

`functional/strftime` came in separately and is collateral of #719, which is
why the passing count had already moved once without a rebaseline. musl
formats every specifier through
`const char *__strftime_fmt_1(char (*s)[100], size_t *l, ...)`, which
`snprintf`s into `*s` and returns it, and `__strftime_l` calls it with `&buf`
of its own `char buf[100]`. The C frontend lowered `*p` on a pointer to an
array as an rvalue load, which copies the whole array into a frame temporary,
so the decayed argument named the copy, `snprintf` filled a buffer nobody
could read, and the returned pointer named a frame that was already gone: all
64 of the unit's checks reported a mismatch, most of them against the empty
string, the rest against the padding `__strftime_l` writes into its own output
before the `memcpy` that reads the stale pointer. An array lvalue is never
loaded (C 6.5.3.2p4). The bisect is the one this stage is for and it lands on
one object -- building musl's own `src/time/strftime.c` with the compiler from
before the fix and dropping it ahead of `libc-buster.a` puts the whole
transcript back -- and `tests/basic_c_pointer_to_array_place.c` pins the shape
under all four allocators: the pointee crossing a call boundary by decay and
the place surviving the return, beside the three store spellings
`tests/basic_c_packed_layout.c` already carries.

`functional/pthread_robust` and `regression/pthread-robust-detach` pass beside
it and are *not* what fixed them, which matters because the number moved by
four rather than by two. Both segfaulted inside `pthread_exit`, which walks the
robust list through `offsetof` on a null pointer. Without `__GNUC__`
<stddef.h> spells that as pointer arithmetic through a nested member, and
lowering `&(((type *)0)->a.b)` loaded the member instead of walking through it;
with `__GNUC__` musl takes `__builtin_offsetof` and never reaches the defect,
which is why these two moved at the predefine rather than at the fix. The
defect itself is described below and is fixed one commit later, so the claim
above that "both spellings mean the same offset" holds for a nested designator
too now.

`functional/setjmp` is the fourth, and unlike those two it is a fix: an
aggregate assigned to a `volatile`-qualified object of the same type produced
two IR types for one struct and the conversion was refused. libc-test's
`functional/setjmp` writes `volatile sigset_t oldset; ... oldset = set2;` at
its third assignment, which is the whole of it.

`functional/tgmath` was the last of the original five, and it went the way
`ide cc` predefines `__GNUC__`: in every dialect now, rather than only in a
GNU one. That is not a dialect question. Clang and gcc both report `__GNUC__`
beside `__STRICT_ANSI__` under `-std=c99`, because the macro says which
extensions the compiler implements and not which ones the dialect permits, and
without it the two compilers read *different* source out of one musl header
under the suite's own `-std=c99`. <tgmath.h> is where that shows: it puts
every `__typeof__` return cast behind `#ifdef __GNUC__` — its own header
comment says "the return types are only correct with gcc" — so each
type-generic macro took the type of the widest arm of its selection chain and
`sizeof pow(2.0, 0.5)` came back as `long double _Complex`, the return type of
`cpowl`. `tests/basic_c_type_generic_math.c` pins the machinery under all four
allocators, `-std=c99` included, because the `__GNUC__` half only has
something to check outside a GNU dialect.

Three frontend gaps sat behind that flip, each of them a construct musl only
reaches on the GNU path, so each was latent rather than new. `0 ? (t *)0 :
(void *)1` kept `t *` where C11 6.5.15p6 makes it `void *` — `(void *)0` is
one of the two spellings of a null pointer constant (6.3.2.3p3) and only the
integer one was recognized — and that conditional *is* musl's `__type1(c,t)`,
which is what made every return cast name its first type; the same fixture
carries it. A GNU attribute directly after a typedef name in a *block-scope*
declaration ended the specifier run and became the declared name, which is
musl's `typedef size_t __attribute__((__may_alias__)) word;` in twelve string
and allocator units (`tests/basic_c_local_typedef_attribute.c`). `offsetof`
with a subscripted member designator did not fold in a static initializer,
which is `ioctl.c`'s compatibility table
(`tests/basic_c_offsetof_subscript.c`). And the x87 `long double` folder knew
no calls at all, so `__builtin_inff()` and `__builtin_nanf("")` — what a
hosted <math.h> spells `INFINITY` and `NAN` as once the builtins are
advertised — refused where `1e5000f` and `(0.0f/0.0f)` already folded, which
put the same 21 `src/math` units and `functional/strtold` back into
blocked-compile the moment the predefine changed
(`tests/basic_c_long_double_static_special.c` now carries both spellings
against one set of expected bytes).

`regression/malloc-oom`, `regression/malloc-brk-fail`,
`regression/setenv-oom` and `regression/pthread_create-oom` used to sit here
too, hanging out the ten-second deadline once the allocator was out of memory,
and none of them was about the allocator. (`regression/fpclassify-invalid-ld80`
was a sixth, and went when the folder learned to produce the positive quiet
NaN.) Each of the four fills memory with libc-test's `t_memfill`, which mmaps
until the kernel refuses; musl's `MAP_FAILED` is
`((void *) -1)`, and an integer narrower than a pointer reached
INTEGER_TO_POINTER without being widened first, so the constant arrived as
`0x00000000ffffffff` and no caller of `mmap` could ever compare equal to it.
`t_vmfill` therefore mapped memory forever. The C frontend now widens ahead of
the conversion — sign-extending a signed operand — because all four backends
lower INTEGER_TO_POINTER as a plain register copy and LLVM's own `inttoptr`
zero-extends; `ir_canonical_conversion_valid` holds every producer to a
pointer-width operand so the ambiguous form cannot be built again.
`tests/basic_c_integer_to_pointer.c` pins it under all four allocators. The
bisect that found it is worth keeping: the hang reproduced with the
*reference's* own `malloc-oom.o` against `libc-buster.a`, which named the
archive, and dropping Clang-built `src/malloc` objects ahead of that archive
did not move it, which cleared the allocator and left `mmap`'s own return.

`LIBCTEST_BLOCKER` is empty for the first time. `__libc_start_main` (141),
`__syscall_cp` (107), `vfprintf` (142) and `__procfdname` (15) headed the list
in turn, then `lfind` and `lsearch` with one test each; every one of them is
now supplied. What the stage reports from here is wrong answers and the
`src/math` compile gap, not missing components — the list will come back the
moment a test reaches for something new, which is the point of generating it
rather than maintaining it.

For scale, one recorded run without libc-test left 26 MB behind: the
Buster pass spent 60,6 seconds of child time over 1349 units — 45 ms each — for
116,9 MB of preprocessed source, 4.306.806 lines and 5.167.422 tokens, and
produced 1344 archive members in 5.744.046 bytes against Clang's 1344 in
2.600.342. Both counts are the manifest minus its `crt/` and `ldso/` units,
which the startup-object and archive notes above keep out. The Buster archive
is the larger of the two, which is what an emitter that spills through the
frame rather than through registers looks like at this scale, and the two
shared objects repeat it: 2.922.384 bytes against 929.584 over the same 1347
members. The two archives take 0,71 seconds to write, the two shared links
0,26, and each probe link about 3 ms.

Adding libc-test takes the run to about 131 seconds wall and 160 MB (measured
2026-08-30, twice on one machine). The suite's 424 units cost 32,0 seconds of
compiler time across both compilers for 35,7 MB of preprocessed source,
1.122.642 lines and 3.523.458 tokens; the links cost 4,0 seconds and the runs
3,3. The runs used to dominate at 74,3 seconds, almost all of it the
reference's own structural hangs waiting out the ten-second deadline because
`clone` was architecture assembly and in neither archive; building musl's
x86-64 assembly into both ended that, and what is left is the compile.
`src/functional` under the second register allocator adds 5,0 seconds of that
compiler and child time — 3,3 compiling its 77 units, 0,5 linking them and 1,3
running them — and 17 MB of objects and programs. That is about four per cent
of the run and inside its run-to-run spread: 130,5 and 135,3 seconds wall with
the pass against 132,1 and 128,2 without it, on the same machine.

Generated headers, objects, archives, metrics and logs remain under
`build/musl-v1.2.6-<pid>/` and are not cleaned up on the way out, so delete the
directories of runs you are done with. Do not run `./build.sh generate` while a
harness run is in flight: it recreates `build/` from scratch, which takes the
in-progress run directory and the `ide` the run is invoking with it, and the
run carries on reporting the missing output files as compiler failures.

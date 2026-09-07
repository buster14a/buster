# musl compatibility harness

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

[Compatibility harness index](../compatibility.md). Read only the harness you are working on; pins, measurements, and past failure counts describe the revision recorded below and must be rechecked before reuse.

The opt-in musl compatibility harness takes an external, pristine musl v1.2.6
checkout; upstream sources are never copied into or patched in this repository:

```sh
./build.sh build --config Release -t ide
./build/build test_musl --config Release /path/to/musl-v1.2.6
./build/build test_musl --config Release /path/to/musl-v1.2.6 /path/to/libc-test
```

The checkout must be tag `v1.2.6` at commit
`9fa28ece75d8a2191de7c5bb53bed224c5947417` with no tracked or untracked
changes. The optional second path is a pristine checkout of libc-test, musl's
own test suite, at commit `68edb8bd73dab8147ee54c8bec638f4d2b3cff37`; without
it the run stops after the shared musl and its dynamic probe. This is the
stretch compatibility target: musl is a whole libc, so the harness is written
to make partial coverage legible and gated rather than to claim a pass it has
not earned.

The harness first runs musl's own three header recipes with `sed`
(`bits/alltypes.h` through `tools/mkalltypes.sed`, `bits/syscall.h`, and
`src/internal/version.h`), then enumerates the manifest from the checkout the
way musl's makefile globs it — musl's `BASE_SRCS`, every `.c` under each
immediate subdirectory of `src/`, plus `src/malloc/mallocng`, `crt/` and
`ldso/`, and musl's `ARCH_SRCS`, every `.c`, `.s` and `.S` under those
directories' `x86_64` subdirectory — rather than carrying a written-down source
list, so a release that adds or removes a translation unit shows up as a
manifest change instead of a silent omission. On x86-64 that is 1356 portable
`.c` files and 50 architecture files, and because every architecture file
replaces a portable one of the same name the manifest is 1356 units: 1306
portable C, 18 architecture C and 32 assembly. The replacement rule and what
Buster does with it are described with the archive below.

One flag set drives both compilers, and it is musl's own `CFLAGS_ALL` minus the
flags musl's `configure` only offers a compiler that accepts them: `-std=c99
-nostdinc -fno-builtin -fno-strict-aliasing -fno-stack-protector
-D_XOPEN_SOURCE=700 -O2 -g0` plus musl's seven include paths.
`-ffreestanding` is absent because musl's configure falls back to
`-fno-builtin`, which is in the set; `-fexcess-precision=standard`,
`-frounding-math`, `-ffunction-sections`, `-fdata-sections`,
`-fomit-frame-pointer`, `-fno-unwind-tables`,
`-fno-asynchronous-unwind-tables` and `-Wa,--noexecstack` are `tryflag`-only,
so a compiler that rejects them gets a build without them, which is the shape
here. `-fno-stack-protector` is on both sides because Buster emits no canary
and the reference build must not either: its canary load reads the thread
pointer, which a program with no thread-local storage does not have.

The inline-assembly vocabulary the harness needs is the one a libc's atomics
and thread pointer are written in, and it is bounded by two rules rather than
by a list of accepted templates. A memory constraint (`m`, `=m`) carries the
operand's storage rather than its value: the register the emitter assigns
holds the address, the template reference expands to a memory reference
through it, and no value is loaded or stored around the assembly. A literal
register in a template is still refused, because one the emitter can also hand
to an operand could be overwritten under the template's feet; the two
exceptions are registers it can never hand out, each allowed only where it
cannot alias anything -- the stack pointer as a memory base, which is the
`lock orl $0,(%rsp)` fence idiom, and `%fs`/`%gs` before a colon, which is how
a thread-pointer read is spelled. GNU's semicolon separates statements while
this assembler reads one as a comment, so the inline-assembly path rewrites
the separators, folding `lock ; insn` into the single statement the assembler
wants and splitting the rest onto their own lines. The operand classes are the
fixed general registers, `r`, `m`, `x`, `t` and `u`, and `X` for an operand a
template does not care about the placement of, which is how musl's `remquol`
keeps the addresses of its arguments from being discarded. Every unit in the
manifest below compiles.

A literal register a template names has a third exception beside those two, and
it is the one that makes the reason above stop applying: a register the asm has
*already* committed to -- pinned by a fixed-class operand, or in its clobber
list -- is one the emitter cannot hand to anything else, so a template naming it
cannot overwrite anything. musl's `fmodl` is the shape: `fnstsw %%ax` beside an
`"=a"` output, where the literal register is that operand's own.

`x` is GNU's SSE register class, and it is a second register file rather than
another name for a general register. An operand in it is allocated a vector
register out of its own pool -- so a general and a vector operand in one
template can never collide -- carried in and out of its frame slot by a scalar
`MOVSS`/`MOVSD`, and spelled in the template by the register name alone,
because the SSE file has one name per register rather than a name per access
width and the instruction the template wrote is what says how much of it is
read. It carries a `float` or a `double` and nothing else: an x87 `long double`
is the other file, and an integer would need a move between files that no
operand here performs; both halves -- the value's type and the target having
the file at all -- are refused by the frontend with a source diagnostic rather
than left to the emitter. The vector register names joined the general ones in
the literal-register refusal for the same reason the general ones are there:
an operand is allocated one, and a template that also wrote one by hand could
overwrite it. `tests/basic_c_asm_sse_output.c` and
`tests/basic_c_asm_sse_input.c` are musl's own `sqrt`, `fabs` and `lrint`
reduced to their operands and run under every allocator, with their answers
checked -- an operand carried into the wrong register still assembles and still
hands back a number.

`t` and `u` are the top of the x87 register stack and the one below it, and
`st` is the clobber a template that pops declares. That file is where an
x86-64 `long double` already lives, so these operands need no conversion around
them at all -- and it is a stack rather than a set of registers, which is the
whole of the emitter's model: the operands are pushed deepest first, so `u`
lands in ST(1) and `t` on top; the template runs; an output in ST(0) is stored
and popped; and whatever is still standing is discarded. An `st` clobber says
the template popped what it was handed, so the depth is one shallower than the
pushes left it and nothing is read back -- musl's `llrintl` is `fistpll`, which
pops. The unwind runs *after* the general-file stores rather than straight
after the template, because the six padding bytes a stored eighty-bit value is
followed by are zeroed through RAX and `fmodl` reads its status word back out
of AX. What the frontend refuses is everything that model does not hold for: a
position named twice, a `u` without a `t`, an `st` clobber that is not beside
exactly one `t` input, an operand that is not the 80-bit spelling, an operand
reached through a pointer rather than sitting in a frame slot, and the class on
a target with no x87 file. A literal `%st` in a template stays refused whatever
the operands are, because a template that moved the stack itself would leave
that accounting wrong. `tests/basic_c_asm_x87_output.c` and
`tests/basic_c_asm_x87_clobber.c` are the fixtures, and `remquol` is the reason
their answers are checked rather than assembled: its quotient bits are decoded
out of the x87 status word, so a `fprem1` against the wrong stack position
returns a plausible remainder beside a wrong quotient.

Two more things a template may do, and they are what a libc's startup and its
dynamic loader are written in rather than its atomics. It may **name a
symbol**: a line beginning with a dot goes through the same directive table
module-level assembly uses, and every other line reaches `assembly_encode`
with the relocations it reports recorded against the module, so musl's
`GETFUNCSYM` -- `.hidden sym` followed by `lea sym(%rip),%0` -- reaches the
object as a PC-relative relocation against a hidden symbol the block may be
the only thing that names. The name a new symbol record keeps is the one in
the instruction's IR literal rather than the one in the substituted copy: the
retry that grows the code buffer rewinds the attempt arena, and a record that
outlives the attempt cannot point into it (`codegen_assembly_durable_name`).
And it may **write the stack pointer**, on the one condition that its last
statement is an unconditional `jmp`, which is musl's
`CRTJMP` -- `mov %1,%%rsp ; jmp *%0`. The rule the exception hangs on is not
where the name appears but that nothing the emitter puts after the block is
reached: RSP is never handed to an operand, so writing it cannot land under
one, and the frame it does move is never read again. The AT&T dereference star
comes with it and only directly in front of an operand reference, so `*` can
only ever dereference a value the C side computed.
`tests/basic_c_inline_asm_symbol.c` carries all of it, run under every
allocator: the three PC-relative references are checked against the addresses
they should have produced, and the hand-off ends the process itself because
nothing after it runs.

One flag the set does not carry is a dialect, and it used to make the two
compilers read different source: `ide cc` predefined `__GNUC__` only in a GNU
dialect while Clang predefines it in every one, so under `-std=c99` musl's
`<stddef.h>` gave Clang `__builtin_offsetof` and gave Buster the portable
`((size_t)((char *)&(((type *)0)->member) - (char *)0))`, and every `offsetof`
in the archive was a different expression on the two sides. That is fixed —
the macro describes which extensions the compiler implements, not which ones
the dialect permits, and both reference compilers report it beside
`__STRICT_ANSI__`; `__STRICT_ANSI__` is the switch that still flips. Both
sides now take the same branch of every one of these headers. The habit the
divergence taught is still worth keeping: a class that looks like a Buster gap
can be a branch the reference never took, so check what the unit actually
preprocessed to (`ide cc -E` with the same flags) before naming a construct.
`tests/basic_c_null_pointer_offsetof.c` still pins that the two `offsetof`
spellings agree for the single-member designator it covers; they did not agree
for a nested one, which is what `pthread_exit` walking the robust list stopped
reaching when the predefine changed and is fixed one commit later, and
`tests/basic_c_type_generic_math.c` pins the predefines themselves.

`<math.h>` splits on the same macro, and there the two branches are not
interchangeable, which is why the fold below outlived the divergence. `NAN` is
`__builtin_nanf("")` for a compiler that advertises the builtins and
`(0.0f/0.0f)` for one that does not; both sides take the first branch now, but
the second is still what any freestanding or older header hands the frontend,
and it is an operation that creates a NaN rather than a constant that already
is one — and IEEE-754 leaves the sign of an invalid operation's NaN
unspecified, which the two available answers use:
x86 hardware produces the negative default NaN, Clang folds to the positive
quiet one. The frontend therefore folds the four operations that create a NaN
out of operands that are not NaN — `0/0` and `inf/inf`, `0*inf`, `inf-inf`
and `inf+(-inf)` — to the positive quiet NaN at every width, in the
in-function path and in the x87 static-initializer folder alike, and reads
constant operands through a negate and through a conversion between float
widths so a source-level `-0.0f` and a widened literal fold the same way.
Ordinary constant arithmetic is still left to the backend: only the invalid
operations fold, because only their answer is a compatibility choice rather
than a value. `tests/basic_c_created_nan_sign.c` carries the created and the
propagated signs, and the created NaN's bytes are in
`tests/basic_c_long_double_static_initializer.c` beside the rest of the x87
initializer bytes Clang writes. `INFINITY` splits the same way, and its
non-GNU spelling is the overflowing literal `1e5000f`, which the x87 folder
refused until it learned that an overflow is a value — the infinity C names —
rather than a failure. Twenty-one `src/math` units and `functional/strtold`
stopped on that literal, and `fpclassify`'s `1/0.0` on the division by zero
beside it, which creates an infinity rather than a NaN. The same 21 units and
the same `strtold` stopped a second time on the *other* spelling when
`__GNUC__` started being predefined everywhere and the header began choosing
`__builtin_inff()` and `__builtin_nanf("")`: the folder is a recursive descent
over arithmetic and knew no calls at all, so the two constant-valued
intrinsics fold there now the way they already folded in
`c_ir_constant_evaluate`, with only the empty NaN payload accepted.
`tests/basic_c_long_double_static_special.c` carries both spellings against one
set of expected bytes, and the `long double` bullet under "Platform and backend
boundaries" is where the folder's whole boundary is written down.

One flag the reference carries and Buster does not is
`-fcomplex-arithmetic=improved`. Clang's default for C lowers a complex
multiply or divide to the compiler-runtime helpers -- `__mulxc3`, `__divxc3`
and their siblings -- which live in libgcc or compiler-rt, and this harness
links neither, so a reference object that calls one cannot be linked at all;
musl's `cpowl` is such an object. `improved` is the inline Smith form the
Buster frontend emits, bit-identical to it over the operand matrix described
under `_Complex` below, so the flag is the reference-side counterpart of the
probe's `-mstackrealign`: it makes the comparison possible rather than
changing what is compared. It is the one option in the set that dates the
reference compiler -- Clang grew `-fcomplex-arithmetic=` in 20 -- and an
older one rejects it as an unknown option rather than miscompiling, which the
reference build reports before any Buster invocation runs.

The Clang reference build runs first and every unit must compile — a musl unit
Clang cannot build under this flag set is a broken workspace, not a Buster
defect, and finding that out before a thousand Buster invocations keeps the two
apart. Buster then compiles the same manifest under FAST, and every unit that
fails is printed as a `MUSL_UNSUPPORTED` line carrying the first line of its
diagnostic. That list is the inventory: it names every component the archive
below is missing and why.

The gate is the compiled-unit count together with a hash of the newline-joined
sorted failing paths. The count alone would accept a change that fixed one unit
and broke another, so both are pinned as `MUSL_EXPECTED_COMPILED_UNITS` and
`MUSL_EXPECTED_FAILURE_HASH` in `build.c` and both are printed on the
`MUSL_INVENTORY` line, which is what a deliberate rebaseline needs. A fix and a
regression therefore both move a number that has to be updated on purpose.

Both object sets are archived with `ar` through a response file — a musl
archive is a thousand members, which is past what a Windows command line takes
— under musl's own `AOBJS` rule, which puts the `src/` units in `libc.a` and
nothing else. The `crt/` units are excluded for the reason in the
startup-object note below, and `ldso/` because a static libc that carries the
dynamic loader hands the linker a `dlopen` that wants `setjmp`, which is
architecture assembly this build does not have; both are still built, and
`MUSL_ARCHIVE` counts them as `startup_emitted`/`startup_absent` and
`loader_excluded`. That is what makes the archive linkable by an ordinary
program rather than only by a program that defines its own entry.
`MUSL_ARCHIVE` also carries the wall time both archives took. The freestanding
probe is then linked against each, and every link in the harness reports its
own `link_us`. The probe,
`tests/basic_musl_freestanding.c`, is a project-owned program with no include
of any kind: it is compiled `-nostdinc` against musl's own headers, entered at
`_start`, and linked with `ld -static` and nothing else, so no compiler driver,
no startup file and no host libc are on the link line and everything it
resolves comes out of the musl archive. It exercises the string, memory,
search and character routines, the seventeen x87 `long double` units named
below -- sixteen of them called directly and `__rem_pio2l` through `sinl` --
and the twenty-two under `src/complex` that take or return a
`long double _Complex`, each wide result recorded as the sign, exponent and
significand fields of musl's own `union ldshape` so that a result one ulp off
cannot pass, and a complex one recorded as both of its halves that way. It
writes a transcript through raw `write` and `exit` system calls; the Clang-built and Buster-built transcripts must be
identical byte for byte, so a routine that computes a different answer fails
the run where a link-and-exit check would not. The probe runs under FAST, NONE,
MIR_STACK and QUALITY against the one Buster-built archive, because the four
allocators have to produce the same answers rather than each produce some
answer. The reference is compiled with `-mstackrealign`: at process entry the
stack pointer carries the alignment the kernel leaves rather than the one a
`call` leaves, and Clang's aligned vector spills need the realignment while
Buster's emitters, which spill through plain moves, do not.

Two more links follow, and they are the ABI report. Two separately compiled
object sets calling each other across musl's own declarations is a direct test
of the calling convention, the struct layouts and the return shapes the two
compilers agree on, and a disagreement surfaces as a wrong answer rather than
as a link error. The first link is the Clang-compiled probe against the
Buster-built archive: a Clang caller into Buster-built musl. The second is that
same probe against a mixed archive, built by taking Buster's object for every
second unit and Clang's for the rest, so musl's own internal calls — `strstr`
into `memchr`, the character tables, the search routines — cross the boundary
in both directions inside one program. Both must reproduce the reference
transcript, and each prints a `MUSL_ABI` line.

The Buster-compiled probe is deliberately not linked against the Clang
archive. That pair does crash, and the reason is not an ABI disagreement: the
probe is entered at `_start` with the alignment the kernel leaves rather than
the one a `call` leaves, which is why the reference is compiled with
`-mstackrealign`, and the Clang probe built without that flag crashes against
Clang's own archive in exactly the same way. The Buster driver has no such
flag, so that direction would measure the probe's entry rather than the two
compilers, and the mixed archive covers what it was meant to cover.

Everything above is linked `-static`, and a shared object is the other half of
what a libc is. `libc.so` is built from the same object set on both sides, with
the three differences musl's own `libc.so` recipe states. `ldso/dlstart` and
`ldso/dynlink` join it, because musl's `LDSO_OBJS` rule puts the loader in the
shared library and its `AOBJS` rule keeps it out of the archive.
Nothing is substituted into it: `src/thread/__set_thread_area` used to leave
it for a project-owned object, and six more names used to need one, and both
sets are musl's own assembly now.

The link is `ld -shared -Bsymbolic --no-undefined -e _dlstart`, and the first
three of those are musl's own.

- `-e _dlstart`, because musl's `libc.so` *is* its dynamic loader and that is
  the entry point the kernel jumps to when the file is a program's `PT_INTERP`.
  It is what makes a shared musl testable here at all: nothing else on the
  machine can load one.
- `--no-undefined`, because a shared object may carry unresolved names and
  musl's loader will not. It reports each one it cannot relocate and leaves at
  exit 127, before the program's first instruction; upstream's `configure` asks
  for the flag for exactly that reason, and here it turns a name the object set
  is missing from a silent runtime death into a link error naming the symbol.
- `-Bsymbolic`, because this library is linked from the one object set the
  rest of the harness already measured and that set is compiled without
  `-fPIC`. The flag is a code model now rather than an accepted no-op --
  described under machine selection below -- so what forces `-Bsymbolic` here
  is the single object set and not a missing flag: a second,
  position-independent set would retire it, at the cost of another two
  thousand compiles. What the non-PIC set does emit is PC-relative, so the only
  references `ld` refuses to place in a shared object are the ones to symbols
  another object could interpose. Binding those at link time is what musl's own
  build does for everything except its public data, through `--dynamic-list`;
  taking the whole set costs the copy relocations that list exists to preserve
  and buys a shared musl out of the object set that is already built. The probe
  below references no libc data object, so nothing it measures turns on the
  difference.

One object set rather than two is the other departure. musl compiles a second,
`-fPIC` copy of every unit for `libc.so`; this harness links the one set both
compilers already produced, which keeps the shared stage at two links instead
of another two thousand compiles, and it is what makes the shared link a
statement about the code generation the rest of the harness already measured.

`--no-undefined` is what used to need a project-owned file beside the object
set. A static link pulls only the archive members a program reaches, so the
seven assembly-only units cost nothing until something calls one; every object
handed to `ld -shared` is in the result and every relocation in it is resolved
when the library loads, and six names were left over -- `__syscall_cp_asm` with
the three labels `__cp_begin`, `__cp_end` and `__cp_cancel` that bound its
cancellation window, and `setjmp`/`longjmp`. Every one of them is musl's own
x86-64 assembly now, so the shared object set is the compiled one and nothing
else. `setjmp` and `longjmp` in particular used to trap -- neither is
expressible in C -- and musl's loader saves a jump buffer around `dlopen`, so
that substitution is what held every libc-test unit that opens a library out of
the comparison.

The dynamic probe is `tests/basic_musl_freestanding.c` again, resolved against
the shared musl instead of the archive and started by the loader inside it. The
interpreter is named by absolute path because there is no installed musl to
point at: upstream installs `libc.so` as `/lib/ld-musl-x86_64.so.1` and links
every program against that name. Its gate is the static probe's, and
deliberately so: producing a shared object that links says nothing, and the
transcript has to be the reference's byte for byte under all four allocators,
so a routine that computes a different answer once it has been relocated rather
than linked fails here. A `MUSL_SHARED` line reports each side's library and a
`MUSL_DYNAMIC` line each program. The Buster-built library is about three times
the size of the reference's, which is the archive's ratio and the same emitter
spilling through the frame rather than through registers.

One compiler change came out of this and it is the only one that did: a
read-only object that carries a relocation is laid out with the writable data
rather than in the read-only section, described with the rest of the C rules
below. Twenty-one musl units hold one -- `__ctype_b_loc`'s `ptable`, the
`FILE *const stdout` trio, the locale tables -- and each of them put a
write-when-relocated word on a page the loader maps read-only, which is a
`DT_TEXTREL` musl's loader does not undo for the file it was itself started
from. `-fPIC` does not retire this one: a relocation into a read-only object
still has to be applied when the image loads whatever the code model is, and
what would retire it is a relocated-read-only section of its own, which this
object writer does not have.

The manifest is musl's own x86-64 configuration, replacement rule included and
whole. Every `.c` in a source directory is collected, and so is every `.c`,
`.s` and `.S` in its architecture subdirectory: musl's `ARCH_SRCS` is
`src/*/$(ARCH)/*.[csS]`, not its assembly half. Where a portable file and an
architecture file name the same unit the architecture file wins and the
portable one is not built -- musl's `REPLACED_OBJS`. That is 1356 units, 32 of
them assembly and 18 of them architecture C. Each replacement is named, on a
`MUSL_ASSEMBLY` or a `MUSL_ARCHITECTURE` line, because which units the archive
holds musl's own x86-64 implementation for is inventory worth having rather
than an exclusion. One unit's `.c` is empty with no architecture file to
replace it -- `src/thread/tls`, which x86-64 does not need -- and it is the
empty translation unit musl itself compiles rather than a reported gap.

All 1356 build, so the failing set is empty and its hash is the hash of
nothing. The last class to go was the inline-assembly operand classes musl's
own math is written in, and it went in two halves: the SSE register class as an
output (`"=x"`, `"+x"`: `sqrt`, `sqrtf`, `fabs`, `fabsf`) and as an input
(`"x"`: `llrint`, `llrintf`, `lrint`, `lrintf`), which was issue 765; and the
x87 stack as an operand (`"+t"`, `"u"`: `fabsl`, `fmodl`, `remainderl`,
`remquol`, `rintl`, `sqrtl`) with `st` as a clobber (`llrintl`, `lrintl`),
which was issue 766. Those were two register files rather than four gaps, and
both are described with the vocabulary above. The other two architecture C
units, `fma` and `fmaf`, build for a different reason: neither `__FMA__` nor
`__FMA4__` is defined under this flag set, so both fall through to
`#include "../fma.c"` and are the portable code reached by an architecture
path. Clang compiles all eighteen, and so does Buster now, so the two archives
hold musl's own implementations of the same units. The four classes are pinned
as fixtures -- `tests/basic_c_asm_sse_output.c`,
`tests/basic_c_asm_sse_input.c`, `tests/basic_c_asm_x87_output.c` and
`tests/basic_c_asm_x87_clobber.c` -- each musl's own templates reduced to their
operands and run under every allocator with their answers checked, so a class
that regresses moves the musl count and one fixture together.

A refused architecture unit does not leave a hole in the archive, and the
fallback that makes that true is still here even though nothing takes it now.
`hypot` calls `sqrt`, so a `libc.a` without one is a `libc.a` nothing links
against, and every stage after the archive -- the freestanding probe, both ABI
links, the shared object, the whole libc-test suite -- would stop measuring
anything at all. The archive therefore falls back to the portable file the
architecture source displaced, and says so: each fallback is a
`MUSL_SUBSTITUTED` line naming the file refused and the file taken, beside the
`MUSL_UNSUPPORTED` line carrying the diagnostic. The unit still does not count
as compiled, so the substitution is a reported gap rather than a repaired one.
There are none today -- `substituted=0` on the `MUSL_SUMMARY` line -- which is
what makes this archive musl's own rather than a portable stand-in for it.
Both archives are 1349 members.

Before the driver took assembly input this was three groups of reported
exclusions: seven `assembly-only` units whose `.c` is empty because the
architecture supplies the implementation in assembly, thirty-two
`architecture-assembly` files the harness replaced with the portable C, and
whatever else failed. The last of the third group to go was
`vfprintf`/`vfwprintf`, both on wide floating-point `va_arg`, described with
the type below.

Static initializers are no longer a class, and neither are the three
singletons that stood beside them: 1344 to 1347. `src/misc/ioctl`'s
`compat_map` sizes its entries with `sizeof(struct { ... })`, and the parser
registered an aggregate an expression defines only inside a function body, so
the same definition in a file-scope initializer had no type for the sizeof
fold to resolve and the whole initializer was refused. `res_msend` writes
`int qpos[nqueries], apos[nqueries];`, a variable-length array in a
comma-separated declarator list, which the list path could not lower --
its diagnostic named an alignment, which is what an unresolved layout leaves
behind, and not the over-aligned automatic that reads like. `lsearch` and
`lfind` walk their tables through `char (*p)[width]`, a pointer to a variably
modified array, which is the type `char p[][width]` adjusts to and now becomes
the same local an array parameter does. Fixing the third also fixed a
wrong-code defect the compile inventory could not see: indexing either shape
with fewer subscripts than it has dimensions leaves an array, and the decay
that turns one into the address of its first element keys on the IR array
type a variably modified array does not have, so `p[i]` loaded one element
where the row's address was meant. Inline assembly is no
longer a class: `crt/rcrt1`, `ldso/dlstart`, `ldso/dynlink` and
`src/thread/__unmapself` were the four units that reached code generation and
stopped on a template, and they compile now that the inline path relocates a
symbol reference and takes musl's two hand-off shapes. Neither is an
inline-assembly *operand* class: the sixteen architecture C units under
`src/math/x86_64` stopped on the SSE register class and on the x87 stack, and
both are implemented, which is what took the count to the whole manifest.

`src/complex` is no longer a class either. Eighteen units returned a
`long double _Complex` and were refused at the signature boundary until the
System V COMPLEX_X87 result class reached the canonical emitter, described
with the type below: 1326 to 1344, and `functional/tgmath` in libc-test moved
from `blocked-compile` to the `vfprintf` link blocker with it. Neither is
wide floating-point `va_arg` a class any more, which is what `vfprintf` and
`vfwprintf` stopped on until the read described with the type below was
implemented: 1344 to 1346. Conflicting
declarations are no longer a class: C11 6.2.7p3 makes an unprototyped
`long f();` compatible with a non-variadic prototype for the same function,
which is what musl's `pthread_cancel.c` and `__libc_start_main.c` write.

`_Complex` is a two-field aggregate of its real type, real part first. That is
the layout every psABI specifies and, with one exception, also the argument
and result shape each of them classifies a complex value into, so the existing
struct rules produce the right registers with nothing added to the backends --
cross-probed against Clang for System V x86-64, Win64, AAPCS64 and Darwin
AArch64, and differenced against a Clang build across the boundary on the
first three. The exception is System V x86-64's `long double _Complex`
*result*, which the psABI returns in ST(0)/ST(1) under its COMPLEX_X87 class
where the equivalent `struct { long double a, b; }` is returned in memory
through a hidden pointer. It is the one place the aggregate model is not the
ABI, and it is keyed on the type being complex rather than on its shape,
because the two spellings have identical layouts and different conventions --
which is what Clang compiles, and what `ir_classify_abi_value` now keys the
class on. The classifier gives that result four eightbyte parts, one
X87/X87_UP pair per half in layout order; `ir_abi_value_is_complex_x87_result`
is the predicate over that shape and the only thing the backends ask. The
canonical x86-64 emitter pushes the imaginary half and then the real one
before its `RET`, so the real half is on top, and pops them in that order into
the result slot after a call. Every other position is the two-field aggregate
unchanged: the argument is a thirty-two-byte memory slot under the same psABI,
which is why `cabsl`, `cargl`, `creall` and `cimagl` compiled before any of
this, and the loads, stores and copies move bytes.
`tests/basic_c_complex_x87_caller.c` and its callee are the pair compiled by
one compiler and linked against the other in both directions, which is the
only thing that pins the register order to the platform's; the single
translation unit in `tests/basic_c_complex_arithmetic.c` cannot see a
disagreement about it.

Multiplication and division are lowered inline -- the naive product and
Smith's algorithm, which is what Clang emits for
`-fcomplex-arithmetic=improved` -- rather than as the `__muldc3`/`__divdc3`
calls Clang emits by default, because this toolchain neither ships nor links a
compiler runtime to resolve them. Against `improved` the two agree bit for bit
over an operand matrix covering the signed zeroes, the subnormal and overflow
edges and the infinities, `long double` included;
`c_ir_emit_complex_divide` in `c_gen.c` records where the default's library
helpers differ. Smith's algorithm compares the magnitudes of the two
denominator halves, and there is no absolute-value opcode, so
`c_ir_emit_float_magnitude` clears the sign bit through a stack slot. The
80-bit x87 spelling has no integer of its own width to pun through, so it is
punned one halfword at a time -- bit 15 of the sixteen-bit sign/exponent field
at byte eight, the `se` member of musl's own `union ldshape` -- rather than
whole, which is what lets `cpowl` compile with the other seventeen. GNU's imaginary literal suffix
comes with the type, in both orders and both letters, because it is the only
way a `<complex.h>` can define `I`: musl spells `_Complex_I` as
`(0.0f+1.0fi)` and glibc as `(1.0iF)`. Two shapes stay unsupported and
diagnosed: a complex global initializer, and a complex operand inside an
integer constant expression, which the parser's folder reduces to the
operand's real part rather than refusing -- C does not admit one there either
way.

`long double` is no longer among them. Seventeen units were static
initializers until the folder learned the two shapes musl writes -- `floorl`,
`ceill`, `roundl`, `truncl`, `rintl`, `modfl` and `__rem_pio2l` open with
`static const long double toint = 1/LDBL_EPSILON;`, and `atanl`, `expl`,
`logl`, `log2l`, `log10l`, `log1pl`, `powl`, `tgammal`, `erfl` and `exp10l`
carry `long double` coefficient tables -- and then stopped in the canonical
emitter instead, which rejected a whole function when one of its values had a
type that *contained* an x87 `long double` without *being* one, so an array of
them was refused whether or not it carried an initializer. Classifying those
aggregates by the ABI rather than by that shape test released them along with
every unit reaching a value through musl's `union ldshape`: 54 units, 1192 to
1246, and the whole x87 code-generation class with them. The four left in
that class were inline assembly -- `crt/rcrt1` and `ldso/dlstart` on
`GETFUNCSYM`, `src/thread/__unmapself` and `ldso/dynlink` on `CRTJMP`, the
last of them having joined once the portable `offsetof` in its `MIN_TLS_ALIGN`
folded and it stopped failing earlier -- and they closed together, 1322 to
1326, when the inline-assembly path learned to relocate a symbol reference and
to take a stack hand-off. Nothing in the inventory stops on an assembly
template now.

musl's startup objects are their own report now that module-level assembly
goes through the real assembler. `crt/crt1.c` and `crt/Scrt1.c` compile, and
the harness writes `crt1.o` and `Scrt1.o` beside the archive rather than into
it, the way musl's own build keeps startup objects out of `libc.a`; a
`MUSL_STARTUP` line names each object it produced, and each one it did not
with the reason. The produced `crt1.o` is a complete startup object:
`_start`'s bytes are Clang's, `_DYNAMIC` is weak and hidden, `_init` and
`_fini` are weak undefined, and `ld -static` links it into a program the
kernel enters and that exits cleanly.

`rcrt1.o` joined them once the inline-assembly arm learned the same
relocation plumbing: `crt/rcrt1.c` and `ldso/dlstart.c` stop on x86-64's
`GETFUNCSYM` in `arch/x86_64/reloc.h`, which is *inline* assembly carrying a
`.hidden` directive and a RIP-relative `lea` against a symbol with an output
operand, and that shape now reaches the object as a relocation like any other.
`crti.o` and `crtn.o` are there too, and they are the whole reason a section
keeps its own name through the assembler: each contributes one and two bytes
to `.init` and `.fini`, which the system linker concatenates in order.
`startup_absent` is zero.

`ldso/dlstart.c` and `ldso/dynlink.c` compile now too, and stay out of
`libc.a` under musl's own `AOBJS` rule rather than because of anything Buster
cannot build, for the reason the archive note above gives.

The archive is linkable, which it was not until `__attribute__((weak))` and
`__attribute__((alias))` reached the object writer. musl publishes `malloc`,
`free`, `errno` and most of its pthread surface as weak aliases of internal
names -- `weak_alias(old, new)` is
`extern __typeof(old) new __attribute__((__weak__, __alias__(#old)))`, which
also needs the `__typeof` spelling -- and while the attribute went
unimplemented every one of those names was absent from the objects even
though every unit holding them compiled. 250 weak symbols now come out of
`libc-buster.a`, `malloc` among them. The compiled-unit inventory did not
move by a single unit when they appeared, which is exactly why the probe
exists: the gap was invisible from the compile side and showed up only when
something linked. The probe therefore calls `stpcpy`, `stpncpy`, `strchrnul`
and `memrchr`, four names musl publishes *only* through `weak_alias`, so the
link that used to fail is now part of the gate. It does not call `malloc`:
musl's allocator takes a lock through the thread pointer, which a program
entered at `_start` with no startup object never established, and the
Clang-built archive faults in the same place.

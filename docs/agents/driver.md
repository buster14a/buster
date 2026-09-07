# Compiler driver and target options

[Agent instructions](../../AGENTS.md) · Paths and commands below are relative to the repository root.

The Clang-like `ide cc` driver accepts `-march=<model>` and
`-mcpu=<model>` (or their separated forms), ordered target-feature overrides
through `-mattr=+feature,-feature`, and x86 assembly dialect selection through
`-masm=att|intel`. CPU and feature options also accept separated values. CPU names use the canonical
spellings printed by `cpu_model_to_string_os`, such as `baseline`, `native`,
`haswell`, `znver5`, and `apple-m4`; incompatible target/model pairs are
diagnosed. `-v` reports the selected CPU, the sorted effective feature set,
and maximum native vector width. `-target`/`--target` strings are
`arch[-vendor][-os][-environment]`: the vendor and environment components stay
free-form, but a CPU model there is rejected in favor of `-march=`, and so is
anything past the fourth component. Both used to be dropped silently, which
left baseline code generation and no hint that the request was ignored.
Native x86-64 and AArch64 compilation uses the FAST register allocator at
every optimization level, including the default and `-O0`, while
`-fno-register-allocator` selects the canonical stack emitter. Advanced and
diagnostic callers may select `none`, `mir-stack`, `fast`, or `quality` with
`-fregister-allocator=<mode>`; when several allocator-affecting options are
present, the last one wins.
The allocators run on x86-64 under both System V and Win64, and on AArch64
everywhere but the PE-unwind targets, which still take the canonical path
whole. Win64 differs from System V in the file it allocates — RSI and RDI are
callee-saved there, so the allocator has seven callee-saved registers instead
of five and the vector class keeps only the volatile ZMMs — and in how a call
is built: the outgoing arguments and the callee's shadow space are written
into a fixed area at the bottom of the frame instead of pushed, because the
stack pointer must not move inside the body of a function whose unwind data
can only carry a frame-pointer offset up to 240 bytes. Its prologue pushes the
callee-saved registers before establishing the frame pointer for the same
reason. Shapes the Win64 subset does not build yet — variadic definitions and
calls, 128-bit integers, vector signatures, indirect (non 1/2/4/8-byte)
aggregate arguments, and dynamic stack allocation — fall back per function,
which `-v`'s `fallback_functions` and `CODEGEN_FALLBACK` lines report.
A `.s` input, or any input under `-x assembler`, is an assembly translation
unit rather than a C one. `assembly_unit_encode` (`assembly_unit.c`) is the
layer above `assembly_encode`: it interprets the directive vocabulary, tracks
one offset per section, resolves labels, and hands each instruction line to
the instruction layer beneath, and the driver turns its sections, symbols and
relocations into an `ObjectFile` like any other. The vocabulary is `.text`,
`.data`, `.bss`, `.rodata` and `.section`; `.globl`/`.global`, `.weak`,
`.hidden`, `.type` and `.size`; `.align`, `.balign` and `.p2align`; `.byte`,
`.short`/`.word`/`.hword`/`.value`, `.long`/`.int`, `.quad`, `.ascii`,
`.asciz`/`.string`, and `.zero`/`.skip`/`.space`; `.intel_syntax noprefix` and
`.att_syntax prefix`; and the `.cfi_*` family, accepted and dropped because it
describes unwinding rather than bytes. Anything else -- a directive the table
does not claim, or an operand form one of these does not cover -- is a
diagnostic naming the directive and its line, the way every other unsupported
construct here is reported rather than silently dropped.

Three things that layer owns rather than the instruction layer. Local numeric
labels: `1:` becomes a generated name and `1f`/`1b` resolve to the nearest
following or preceding definition in source order, and those names leave the
symbol table again once every reference to one is folded, the way GNU as drops
its own `.L` locals. A repeat or lock prefix alone on a line joins the
instruction on the next one. And a same-section PC-relative reference to a
label defined in the file is written into the bytes; only a cross-section or
undefined name becomes a relocation. `@PLT` is dropped: a static link resolves
such a call the same way it resolves a plain one. Sections keep their own
names -- `.init` and `.fini` are neither `.text` nor absent -- and a
hand-written section gets alignment 1, because `crti.o` and `crtn.o`
contribute one and two bytes to `.init` and any padding between them would
run as code.

One deviation from GNU as, and one refusal. A forward branch to a label is
always the near form, because the instruction layer sizes a statement before
the label is known and this assembler does not relax; the bytes are correct
and a few longer than GNU as writes. And a `.S` -- assembly the C
preprocessor runs over first -- is refused by name: this frontend's
preprocessor hands back C tokens, and `%rax`, `$1` and `1f` do not survive
that round trip, so the input is reported rather than mis-assembled.

`-emit-llvm` emits binary LLVM bitcode directly from canonical typed IR for C
inputs. It writes `<input>.bc` by default, accepts `-o` for a single
input, and rejects native objects, archives, libraries, frameworks, linker
arguments, `-E`, `-S`, and `-fsyntax-only`. The writer has no LLVM dependency;
see `LLVM_BITCODE.md` for its target metadata, API, and supported boundary.

Every hosted ELF link reads the shared libraries' own dynamic symbol tables.
`compiler_driver_elf_library_exports` looks `libc.so.6` and each requested
library up where the loader would — the `-L` paths, then the sysroot or host
`lib`/`usr/lib` roots, multiarch first — and rejects a file whose ELF machine
disagrees with the target, so a cross link never reads the host's own libc.
`compiler_driver_elf_dynamic_symbols` walks that table once and produces two
things.

The first is the defined global and weak **objects** with their addresses and
sizes, as `NativeDynamicDataSymbol` arrays, which `link.c` uses to reserve
copy-relocation slots: a slot stands for the library's object rather than the
one name the program spelled, so it carries every name the library exports at
that address, takes the library's own size, and is shared by two imported names
for one object. The alias set is what makes `extern char **environ` work. A
definition in the executable takes precedence over the library's for every one
of its names, and glibc stores the environment through `__environ` after
startup; an executable that defined only `environ` left that store in libc's
own storage while the program read a copy taken before startup ran, which is to
say null. This half is still collected only for a link with an undefined data
symbol, since a link with none has nothing to copy, and a library that cannot
be found or parsed leaves the pointer-sized slots the writer reserved before
alias sets existed.

The second is the **symbol version** of every defined entry, functions
included, read from the library's `.gnu.version` and `.gnu.version_d` into
`NativeDynamicVersionedSymbol` arrays. That half is collected on every hosted
ELF link, because versioning applies to functions and there is no cheaper way
to know: reading `libc.so.6` where nothing did before costs about 0,65 M
instructions, a tenth of a percent of the smallest hosted compile. It buys two
things in the x86-64 dynamic writer, and the AArch64 one through it:

- **A reference records the version it bound to.** `.gnu.version` carries one
  index per dynamic symbol, `.gnu.version_r` names per library the versions
  the image needs, and `DT_VERSYM`/`DT_VERNEED`/`DT_VERNEEDNUM` publish both.
  The alias names a copy slot defines are versioned too, because each of them
  is a name the library publishes. Without this the image binds by name to
  whatever the running glibc calls default, which is the versioning
  mechanism's whole purpose: `readelf -W --version-info` on a Buster
  executable now agrees with GNU ld's for the same program, `stat@GLIBC_2.33`
  included. Both sections are omitted when nothing needed a version, and the
  layout then collapses to exactly what it was before, so an unversioned
  library's image is byte-for-byte unchanged.
- **A name with no default version is refused** rather than linked, as
  `LINK_ERROR_SYMBOL_VERSION`. glibc publishes `sys_errlist` four times, once
  per historical layout, and every one of them is a non-default `name@VER`; an
  unversioned reference has nothing to bind to, GNU ld reports it undefined,
  and Buster linked it and let the loader pick. **Do not use `sys_errlist` as a
  Clang-differential fixture** — a harness that reads "Clang refuses, Buster
  accepts" as a Buster success measures nothing (issue #660).

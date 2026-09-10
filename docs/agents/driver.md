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
reason. Windows/UEFI variadic definitions and calls use the positional home
area and float-register duplication described in the [machine guide](machine.md).
Shapes the Win64 subset does not build yet — 128-bit integer signatures,
vector signatures, indirect (non 1/2/4/8-byte)
aggregate arguments, and dynamic stack allocation — fall back per function,
which `-v`'s `fallback_functions` and `CODEGEN_FALLBACK` lines report.
`CODEGEN_FALLBACK_REASON` additionally identifies the target, allocator and
stable reason name for every fallback. Its disjoint counts sum to
`fallback_functions`: `target-excluded`, `signature`, `opcode`,
`selection-other`, `verification`, `placement`, `encoding`, `output-capacity`,
and `unwind`. `signature` means the target's function ABI gate rejected the
signature; `opcode` retains the first rejected canonical opcode in the legacy
`CODEGEN_FALLBACK` census. `selection-other` is deliberately unclassified,
while `verification` identifies an implementation failure. The allocator,
stage, opcode and reason counters all survive multi-input compilation.

For two-operand EVEX vector loads/conversions, an ordinary memory qualifier
names the source tuple, not the destination register width. A broadcast
qualifier names its scalar element. The metadata selector projects the
candidate's element width and validates the source qualifier independently;
for example, masked `vcvtps2pd zmm0, m256` reads eight 32-bit elements while
masked `vcvtpd2ps ymm0, m512` reads eight 64-bit elements. AT&T's unqualified
memory spelling uses the same candidate contract. Unsized ordinary loads
whose destination permits multiple source tuple widths are rejected as
ambiguous; encoding length and candidate order cannot choose input lanes.
This bounded projection
requires a mask/broadcast, ZMM destination, or high vector register and covers
FULL/HALF EVEX tuples; it does not replace all legacy/VEX source inference.

`-fno-frontend-ssa` selects the original memory-form C lowering;
`-ffrontend-ssa` restores direct SSA for the bounded supported subset. The last
flag wins. These controls are independent of `-fno-canonical-local-promotion`
and `-fno-target-local-promotion`: disabling shared promotion does not undo
SSA already built by the frontend. For a fully memory-form differential input,
disable frontend SSA as well. Verbose compilation reports `IR_FRONTEND_SSA`
counters beside `IR_LOCAL_PROMOTION`; see the
[frontend ownership contract](frontend/foundations.md#direct-local-ssa-github-34).
`IR_LOCAL_PROMOTION_WORK` reports shared-promotion parameter-cleanup sweeps and
actual visits, separately from removed rows. The [middle-end pass map](../middle-end-pass-map.md)
defines their scope, invalidation rules and separate diagnostic replay protocol.

`-fno-machine-fallback` makes native C coverage strict: after code generation
succeeds, any fallback fails the translation unit before object writing and
reports its first function, source, target, allocator, opcode and reason.
`-fmachine-fallback` restores the normal differential-oracle behavior; the last
of these two flags wins. Strict mode requires a native target and a machine
allocator (`mir-stack`, `fast` or `quality`); NONE, direct non-native emission,
preprocessing and syntax-only checks cannot satisfy the gate. Assembly inputs
and linked prebuilt objects have no canonical C functions to gate.
For example, `build/Release/ide cc -fregister-allocator=mir-stack -fno-machine-fallback -target aarch64-unknown-linux -c tests/basic_c_call_abi.c -o build/mir-coverage.o`.
`compiler_driver_test_machine_fallback` runs a curated arithmetic, control-flow
and call-ABI corpus through this gate for x86-64 and AArch64 Linux under all
three machine allocators in `test_all`, including CI. Deliberate PE AArch64
target exclusion, Win64 indirect aggregate parameters, and Darwin AArch64
variadic signatures are separate
negative tests. This corpus is a coverage floor, not a claim of complete MIR
lowering or permission to retire the canonical oracle.

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

Text alignment without an explicit fill uses x86-64 NOP bytes or complete
little-endian AArch64 NOP instructions. A partial AArch64 instruction boundary
is zero-filled before the NOPs; explicit fills remain repeated bytes on both
targets. Data alignment defaults to zero fill.

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

A forward branch to a label always uses the near form: the instruction layer
sizes a statement before the label is known and this assembler does not relax.
`.S` inputs run through C preprocessing with assembly comment-line handling.
The printer preserves line structure and token adjacency; the root input splits
unquoted dollar prefixes before lexing. Assembly errors resolve lazily back to
originating tokens and physical positions, including `#line` identities.

C, assembly and backend failures publish the shared
[diagnostic contract](../diagnostics.md). Strict fallback uses symbolic opcode
names and `not-applicable` for signature/target exclusions, while tooling retains
internal IDs in the optional backend context. Source locations name the resolved
file, including remapped or included source, rather than always the top-level input.

`-emit-llvm` emits binary LLVM bitcode directly from canonical typed IR for C
inputs. It writes `<input>.bc` by default, accepts `-o` for a single
input, and rejects native objects, archives, libraries, frameworks, linker
arguments, `-E`, `-S`, and `-fsyntax-only`. The writer has no LLVM dependency;
see `LLVM_BITCODE.md` for its target metadata, API, and supported boundary.

An undefined weak ELF reference does not select a static archive member.
It may bind to a member selected for a separate strong dependency, to a
direct object input, or to an already included shared library. Keep archive
selection separate from those later resolution rules (GitHub #226).

ELF executable data placement honors both page and requested object alignment.
Align the final virtual address, not only its file offset: an initialized
global may require alignment larger than a page or the fixed image base.
The object writer already carries that requirement into the section metadata
(GitHub #225).

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

## Native invariant verification

`-fverify-codegen` validates canonical IR even when the frontend certified it,
then checks selected and changed scheduled MIR and placement validity. Invalid
verified states fail compilation before fallback can hide them. It applies to
native x86-64/AArch64 code generation, including the `none` canonical path;
preprocessing, syntax-only and direct non-native output reject the flag.
Successful compilation prints a versioned `CODEGEN_VERIFY` line with module,
selected-function and scheduled-function counts and the effective allocator.
Normal compilation keeps its existing validation certificates and fast paths.
The [native differential runner](../differential-testing.md) consumes this
explicit opt-in evidence and compares executable observations independently.

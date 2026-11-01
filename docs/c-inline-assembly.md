# Inline assembly

The C driver's lowering compiles GNU C's `asm` statements (#147's "Inline
assembly" item). `c_check_program_asm.c` plans each statement, `assembler.c`'s
`asm_assemble_code` assembles its template into records, and
`c_check_program_lower.c`'s `check_lower_asm` lowers the statement's operands.
Tier 0 doesn't change: it sees an ordinary call. A naked function's statements
are its whole code instead, and the names templates define and refer to become
symbols of the unit's object or executable.

## The shape: a helper per statement

A statement whose template assembles to instructions becomes a per-unit helper,
a leaf function made of its own records, which the statement's code calls
through Tier 0's `CALL`. That is the shape of the atomic, fence and
floating-environment helpers (`c_check_program_helpers.c`), and
`check_program_emit` publishes the asm helpers with theirs: a local function
each, named `.Lbuster_asm_N` after its statement, sized, encoded and given its
unwind entry.

A template that assembles to no instructions makes no helper and no call. These
are the compiler barriers, `asm volatile("" : : "r"(x) : "memory")`, and comments
with operands substituted into them. But one with x87 operands has a helper,
whose loads and stores pass their values through the x87's stack as clang's
code does: `asm volatile("" : "+t"(x))` of a `float` holding a signaling NaN
quiets it (fld raises the invalid operation, masked, and loads the quiet NaN),
as clang's build does. Their operands are evaluated in the same
order as a helper's. A matching input's value is stored into its output as the
register would hold it: an integer narrower than its output is zero-extended
from its own width, as GCC and clang place it (`"=r"(long) : "0"(-1)` is
4294967295), as a helper's narrower loads extend it too. A `+` output's value is
loaded and stored back, as clang's code does. An output that nothing writes
keeps its value; GCC and clang leave it unspecified.

Such a template also takes a vector in a vector register's constraint, which a
helper doesn't load or store yet: `main.c`'s barriers `__asm__("" : "+v"(x))`
of an `__m512i` (`c_lexer.c`'s `shift_in_previous`, `x86_64_encoder.c`'s
`encode_group`). The planning holds such an operand aside
(`BUSTER_CHECK_ASM_HELD`) until the template is assembled. A template of
instructions, or one that refers to the operand, is then refused. In one of
none, an output is evaluated as its object's address, which the template leaves
as it was, and an input as its value, which is dropped. A volatile output isn't
held, since clang's code loads and stores it.

Why a call rather than records in Tier 0's own code:

- The template's registers stay out of Tier 0's depth registers and homes, and
  out of both of its paths. Tier 0 already saves the values live across a call.
- A helper has its own frame and unwind entry, whatever its template does.
- The cost is a call and a ret per statement run, and nothing for the barriers.

What differs from clang: rsp inside the template is the helper's, not the
function's. It is 16-byte aligned there, as clang -O0's function bodies have it.
A template can't reach the function's frame, which Tier 0 lays out otherwise than
clang anyway. Moving a helper's records into its caller's code would be a
follow-up, if a measurement shows the call matters.

## Naked-function checking

The semantic reader rejects naked functions' non-asm statements and references
to their parameters in asm operands (#554), including under `-fsyntax-only`.
`c_check_naked_input.c` uses the existing statement tree and resolved parameter
identities. It permits empty bodies, null statements and uninitialized variables
with asm register labels, including declarators whose inline type defines
members or enumerators. Only the statement's own declarators are inspected.
Ordinary functions after a naked definition retain
their ordinary rules. Later declarations inherit a naked prototype's attribute;
an attribute declared after a definition does not change that earlier body's
checking.

Parameter references in unevaluated or discarded expression operands still
diagnose. A statement expression's compound body and a type-only `typeof` query
are outside that expression walk, as in Clang. Type-form `sizeof` and `alignof`
visit their retained inline VLA bounds, including dimensions below a constant
array dimension; constant bounds and pointer/typeof type spellings do not
become parameter uses. A bounded scratch worklist preserves the rightmost-first
expression order for nested type queries. Operand errors become available
at the asm's closing parenthesis, body-statement errors at the body's closing
brace. A rejected input keeps its diagnostic spelling separately, so errors
within a statement precede the body constraint while the displayed location is
the offending statement or parameter use.

The new function bit shares the existing flag column. Only units declaring
naked functions run the body scan, advancing once through the source-ordered
asm list rather than rescanning it per body; only a rejected input allocates the small
diagnostic record. Attribute inheritance copies the small attribute accumulator
before appending the inherited record. No token column or syntax replay is added.
The checker and its scalar twin consume the same resolved metadata. Lowering's
unsupported-feature checks remain the backstop for accepted bodies.

`test-checker-asm` compares the original fixtures with Clang, including exact
messages and columns for these two errors. `test-checker-deferred` also compares
valid, rejected and inherited naked bodies across its binding modes and lanes.
The behavior references are Clang/LLVM `llvmorg-23.1.1`'s
[`SemaDecl.cpp`](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/Sema/SemaDecl.cpp)
and [`SemaStmtAsm.cpp`](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/Sema/SemaStmtAsm.cpp),
checked with original probes against installed Clang 23.1.4. No LLVM
implementation or test fixture is copied.
## Naked-function lowering

A function is naked when any of its declarations up to its definition has the
attribute, as clang takes it. That includes an unannotated prototype before a
naked definition, and a naked redeclaration before an unannotated definition.
Clang ignores the attribute after the definition, with a warning, and so does
the lowering. `check_program`'s pass over the declarations keeps each
function's first declaration with the attribute (`CheckProgram.object_naked`),
in a column it makes only for a unit that has one.

A naked function's body may hold only `asm` statements: basic ones, and
extended ones whose operands are integer constants (`"i"(7)`).
`check_program_asm_plan_naked` substitutes their templates and joins them in
order into one source. Between statements it restores AT&T syntax, since clang
parses each statement afresh. It assembles that source once, into the
function's whole code: no prologue, epilogue or trailing trap of the
compiler's, as with clang. The lowering gives the body a bare `void` function,
with no parameters, locals or statements, which the emission doesn't compile.
It takes the planned code instead.

The function's unwind entry has no CFA instructions, as clang's has (its CIE's
rule, the call's), whatever the templates push. `unwind_tier0_frame` and
`--verify`'s analyzer don't see the function. Its one source row is at its
first statement.

`main.c`'s two naked functions, `interpreter_native_bridge` and
`x86_semantics_test_enter`, compile into clang's bytes, at clang's offsets in an
object of the three functions this part of #147 claimed (but the padding
between functions: ours `int3`, clang's a `nop`).

## Labels and names

A template's named labels, `.L` ones among them, are local symbols with no type,
each at its record's offset in its helper's or its naked function's code, as GNU
as and clang make them. Every other symbolic field refers to a name, which the
emission resolves (`check_program_emit_asm_symbols`):

- a label some template of the unit defines;
- else the unit's own symbol of that link name (its asm label or its
  identifier), such as a static that `__attribute__((used))` keeps;
- else an undefined global symbol of its own, as GNU as and clang make one. The
  direct path binds it, as it binds the unit's other undefined names.

A branch to a label of the template's own, with no addend, is sized as before.
A call to it, or a branch with an addend, is a reference from the branch's
rel32 form. `check_program_asm_names` gives the unit's names one table, which
the emission's symbols follow. Its pass over the program's symbols looks up
only names of the asm names' lengths.

A label in a helper is inside the helper, not the statement's function: its
address is right, as data and as a call's target. Jumping to it from outside
runs the helper's epilogue, which only a template that leaves the helper as it
came in can rely on.

## Placement

`check_program_asm_plan` places each operand by its constraint's first
alternative (`check_asm_constraint_parse`):

| Constraint | Place |
| --- | --- |
| `r`, `q`, `l`, `R`, `Q` | a general register of the class (`Q`: rax, rbx, rcx, rdx; `R`: the first eight) |
| `a b c d S D` | that register |
| `x`, `v` | an XMM register, for a `float` or a `double`; a vector in a template of no instructions |
| `t`, `u` | the x87's st(0) and st(1), for a `float`, a `double` or a `long double` |
| `m`, `o`, `V`, `<`, `>` | memory at an lvalue's address, which a general register holds |
| `i`, `n`, `I J K M N O`, `e`, `Z` | an integer constant, within its letter's range |
| a digit, `[name]` | the output's register (a matching input) |
| `g`, `X` | a constant where one is allowed, else a register, else memory |

A constant goes where its constraint's first alternative takes it: as an
immediate if one of its letters' ranges takes it, else in a register if the
alternative allows one, as GCC and clang place it. So `"rI"(31)` is `$31`, and
`"rI"(40)` is a register.

Every operand gets a register of its own, but matching pairs. No operand takes a
clobbered register. Caller-saved registers come first, so most templates cost
their helper no saves. An input fixed to an output's fixed register, as in
`"=a"(r) : "a"(n)`, shares it, as GCC and clang let it, unless the output is
early-clobbered or `+`. It is that output's matching input then.

### Explicit register variables

An operand that names an explicit register variable, `register long r10
__asm__("r10") = a4;` and then `"r"(r10)` (musl's syscall wrappers pass a
system call's fourth to sixth arguments so), takes the variable's register, as
clang's code gives it (CodeGen's `AddVariableConstraints`;
`check_asm_variable_register`):

- the operand is the variable itself, or a cast of it that keeps its value: to
  its own type, or between integers and pointers of one width (clang's
  `IgnoreParenNoopCasts`), so `"r"((unsigned long)r10)` is in r10, but
  `"r"((int)r10)` and `"r"(r10 + 0)` are ordinary operands;
- the register replaces the constraint's letters: `"a"(r10)` is in r10, an
  output of a memory constraint (`"=m"(r10)`) is r10 too, and a matching input
  is in its variable's register rather than its output's;
- a general register by any of its names (`"ebx"` is rbx; an `int`'s operand
  prints `%r10d`), and xmm0 to xmm15 for a `float` or a `double`.

The variable is an ordinary local otherwise: the operand block takes its value,
or an output's address, as any operand's, and the helper loads it into the
register (a callee-saved one among the helper's pushes).

### The x87's operands

`t` and `u` place a `float`, a `double` or a `long double` at st(0) and st(1),
as clang's `{st}` and `{st(1)}`, by the rules LLVM's stackifier applies to an
asm (`X86FloatingPoint.cpp`), which clang's code keeps and refuses to break:

- the inputs (a `+` operand's and a matching input's among them) take the
  stack's depths from st(0) without a gap, and so do the outputs, alone and
  with the clobbered x87 registers (`"st"`, `"st(1)"`);
- an input in an output's or a clobbered register is popped by the template
  (`fistpll %0` of a `"t"` input, clobbering `"st"`; `"=t"(r) : "0"(x)`), and
  the popped inputs lie above the others;
- the template finds the inputs at their depths, and leaves the outputs from
  st(0) down above the inputs it didn't pop (fscale's st(1), fprem's).

The helper loads the inputs from the operand block, the deepest first, by their
types' widths (`flds`, `fldl`, `fldt`). After the template it stores the
outputs from st(0) down, each popped through its address, then pops the inputs
left below them: the stack is empty at the return, as the convention has it,
and as Tier 0 has it at every call (its long doubles live in memory). musl's
`fmodl` (`"fprem; fnstsw %%ax" : "+t"(x), "=a"(fpsr) : "u"(y)`) loads y, then
x; stores x and pops y.

The operand block, a frame object of the caller's, holds 8 bytes per slot, but
an x87 operand's `long double` value 16:

- each register output's address, and a `+` one's value;
- each memory operand's address;
- each input's value.

The caller fills it in clang's order. First come the outputs' addresses, with a
`+` operand's value loaded through its address, then the inputs. Then it calls
the helper with the block's address in rdi.

## Substitution

`check_asm_substitute` prints the template as GCC and clang print it in AT&T
syntax:

| In the template | Printed |
| --- | --- |
| `%0`, `%[name]` | a register by the operand's size, memory as `(%reg)`, a constant as `$value` |
| `%b`, `%w`, `%k`, `%q` | the register's 8-, 16-, 32- or 64-bit name; a constant as it is |
| `%h` | the high byte register (ah, bh, ch, dh) |
| `%c`, `%P`, `%p`, `%a` (constant) | the constant without `$` |
| `%n` | the negated constant, without `$` |
| `%a` (register) | `(%reg)` |
| `%A` | `*%reg`, or `*(%reg)` for memory |
| `%H` | memory 8 bytes on |
| `%z` | the operand's size suffix |
| `%V` | the 64-bit register's name without `%` |
| `%x`, `%t`, `%g` | an XMM operand's xmm, ymm or zmm name |
| `%%`, `%=` | `%`, and the statement's own number in the unit |
| `%{`, `%|`, `%}` | the character |
| `{att|intel}` | the AT&T alternative |

A constant prints its type's bits, sign-extended, as clang prints it:
`"i"(0xffffffffu)` is `$-1`. Its letter's range is checked as clang checks it:
on that sign-extended value, so `"N"((unsigned char)255)` is refused, as clang
refuses it, but `Z`'s on the zero-extended bits, as clang's backend checks `Z`,
so `"Z"(0xffffffffu)` is taken and prints `$-1`. A basic `asm` statement's
template is printed as it is, without substitution.

## Assembly

`asm_assemble_code` takes the text through the assembler's lexer, parser and
matcher. It runs the vector path or the scalar twin, as the checker's path does.
It returns the records, the local branches as branch sizing's columns, its
symbolic fields as references by name, and its named labels. It accepts
instructions, labels and the syntax directives. It refuses:

- every other directive, which the planner assembles whole instead (below);
- its numeric labels and `.`, but as a `jmp`'s or `jcc`'s target without an
  addend, and `loop` and `jrcxz` to any of its labels;
- undefined numeric labels;
- what the matcher doesn't take.

### Templates that take directives

A template with a directive besides the syntax switches is not records: the
planner assembles it with `asm_assemble`, the `assemble` command's assembler,
which already takes the sections, data, alignment and symbols of a `.s` file
and file-scope `asm` (#619). A template that emits code (`AsmResult.entry_code`)
is assembled as the whole helper: its frame, its operand block's loads, the
template, its stores and its epilogue are one source, so a `.p2align` pads
inside the helper's own code and its atoms carry the alignment. Its atoms,
symbols and fixups are published by the emission
(`check_program_emit_asm_unit`), its labels and names joining the unit's as
file-scope assembly's do. The directives are the assembler's: `.p2align`,
`.balign`, `.align`, `.nops`, `.skip`, `.space`, `.zero`, `.byte`, `.word`,
`.long`, `.quad`, `.ascii`, `.asciz`, `.string`, the section switches
`.section`, `.text`, `.data`, `.bss`, `.pushsection`, `.popsection`,
`.previous`, the bindings `.globl`, `.global`, `.local`, `.weak`, and `.set`,
`.equ`, `.equiv`, `.type` and `.size`. Data and hand-encoded bytes execute in
the statement's helper, as clang's inline bytes execute in its function; a
`.pushsection` body's bytes go into the named section's atom, as file-scope
assembly's, and `.popsection` or `.previous` returns to the template's own
code. A pushed `.note.GNU-stack` is the writer's empty note, as `.section`'s is.

A template of alignment, section and symbol directives alone has no code
(`entry_code` is false): its sections, data and defined labels are published,
and the statement calls nothing, so zstd's `__asm__(".p2align 5")` before a hot loop
adds no call. The alignment aligns a helper's own code or atom, not the
caller's. A naked function's templates are one source too, without a prologue
or epilogue; CFI directives stay refused, since a helper's frame is its own
unwind entry's and a naked function's unit would carry an `EH_FRAME` atom the
emission doesn't publish.

The templates of a unit are assembled apart, so a directive's symbols are the
template's: another template's reference to a `.set`/`.equ` absolute symbol is
refused by name (`inline assembly absolute symbols are per-template`), while
two templates may define the same absolute name, each its own; a `.globl` or
visibility declaration upgrades a label another template defines, while a
`.type` or `.size` declaration applies to its own template's label alone.
These match what one template does; clang's module-wide assembler would carry
the rest across statements. A `.globl`, `.local` or visibility directive on one
of the unit's C symbols changes that symbol's own binding or visibility, while
`.weak` applies to a function's, an extern's or a static or block-scope
variable's (a defined variable of external linkage is refused, as clang
refuses it), and `.size` applies once the definition's atom exists: a function's or a variable's, where an extern's is the no-op both
compilers make it. A `.size` past that atom is refused by name. `.nops`'s
optional control is accepted and ignored, an undefined
`.globl`, `.weak` or visibility declaration that no template refers to is left
out of `-c`'s object (clang keeps its undefined symbol), and so is a
`.set`/`.equ` absolute symbol (clang writes a local `ABS` symbol).

The matcher's index admits every form the assembler's syntax takes, whatever
the unit's target. GNU as and clang's integrated assembler take any instruction
in a template. The lowering makes the index on its scratch arena, once per unit
with asm statements, and frees it after the planning.

The matcher admits `cpuid`, `xgetbv`, `xchg`, `fnstcw`, `fldcw`, `fnstsw`,
`fnclex`, `stmxcsr`, `ldmxcsr`, `fxsave` and `fxrstor` (and their `64` forms)
and SSE2's `pcmpeqd`, which `main.c`'s statements use, and the `bswap` of
`<linux/swab.h>`'s helpers. For `main.c`'s naked functions it also admits
`pushfq`, `popfq`, SSE2's `movq`, the x87's `fld` and `fstp` of every width, the
string instructions with their prefixes (`rep movsb`) and AVX-512F's
`vmovdqa64` and `vmovdqu64`, and VEX's `kmovb/kmovw/kmovd/kmovq` with `k0`
through `k7` as ordinary operands. The mask moves retain each form's target
requirements. Its other families are in
[the assembler's guide](assembler.md).
The operandless `pause`, `lfence`, `mfence`, `sfence`, `rdtsc` and `rdtscp`
are also available (#798), with register outputs and CPU support chosen by the
template's author.
`test-assembler --cross-check` compares them with GNU as. The native
inline-assembly fixtures also run alone with `test-checker --program-asm`,
including the eight mask saves and restores at the semantics harness's offsets.
An `xchg` takes its memory operand first in Intel order, as the catalogue has
it: `xchgl %ecx,(%rdi)`,
not `xchgl (%rdi),%ecx`. An `xchg` of the accumulator with another register
takes the two-byte form, where GNU as takes the one-byte one.

For musl's templates (`arch/x86_64/atomic_arch.h`, `pthread_arch.h` and
`src/math/x86_64`) the matcher also admits the `lock` prefix, as a statement of
its own (`lock ; cmpxchg %3, %1`, which GNU as applies to the next instruction)
or on the instruction's line, on `cmpxchg`, `xadd` and the arithmetic of memory
destinations; `bsf`, `bsr` and `hlt`; SSE's `sqrtsd`, `cvtsd2si`, `andps` and
SSE2's `psrlq` with their siblings; FMA3's scalar forms; the x87's operations on
the stack's top (`fsqrt`, `fprem`, `frndint` and the rest), `fnstsw %ax` and
`fistpll`. `rep; bsr` (dr_flac's spelling of `lzcnt`) is `lzcnt`'s form, its
bytes. [The assembler's guide](assembler.md) lists the families, and the
bytes of a prefix written alone before a 16-bit instruction.

## The helper's frame

`check_asm_helper_code` makes:

1. pushes of the callee-saved registers the statement touches: its operands',
   its clobbers' and the stores';
2. a reservation of 8 or 16 bytes, which aligns rsp to 16 and keeps the block's
   address;
3. the loads of the operands from the block: the x87's first, the deepest
   first, a narrower integer zero-extended, and the load into rdi last;
4. the template's records;
5. the register outputs' stores through their addresses, from two registers no
   output holds, then the x87's from st(0) down, and the pops of the x87 inputs
   the template left;
6. the epilogue.

`unwind_tier0_frame` describes it (`Tier0Code.frame_saved` and `frame_size`).

## File-scope assembly

A file-scope `asm` statement (#619: `main.c`'s puts the checker's code in a
page-aligned `.text.buster_checker`) makes no helper and no code of a function.
The emission (`check_program_emit_file_asm`, in `c_check_program_emit.c`)
joins the unit's file-scope templates in source order, a line each, and
assembles them with `asm_assemble`, the `assemble` command's assembler, as
clang emits a module's `asm` before its functions. Each section it makes is an
atom of its class, before the functions' atoms in the link, and in `-c`'s
object a section of its own name, type and flags
(`CheckProgramCode.asm_sections`, `check_program_sections`); the direct path's
executable merges it into its class's output section. Refused by name:
templates the assembler refuses; templates that define or refer to a symbol (a
label, an instruction's symbolic operand, CFI), which the unit's symbols don't
join yet; and a section of a class C's definitions don't have, or of another
type than its class's (an init array's, a note).

## What the lowering refuses

Each refusal names its feature, as the driver prints it ("unsupported: ..."):

- `asm goto`;
- flag outputs (`=@cc...`);
- MMX's, the opmasks' and the rax and rdx pair's constraints (`y`, `k`, `A`),
  `f` (an x87 register of the compiler's choice), and the other letters it
  doesn't place;
- x87 operands and clobbers off the stack's top (an input at st(1) without one
  at st(0), a clobber below the outputs), which clang's code refuses too; two
  operands at one depth, which clang compiles as one register's; an integer in
  an x87 register; and an x87 register named in the template (`%st`,
  `%st(1)`), whose registers the assembler's operand grammar doesn't take;
- symbolic immediates, and constants outside their letter's range;
- operands of types no general or XMM register holds by value, outside memory,
  but a vector in a template of no instructions that doesn't refer to it;
- bit-fields;
- explicit register variables in registers the helper doesn't place one in
  (rsp, which is the helper's own, the high bytes, APX's r16 to r31, XMM16 to
  XMM31 and the other classes' registers), as memory inputs (`"m"(r10)`, which
  clang's code refuses too), and of other types than a general register's or
  an XMM register's (an integer in an XMM register's variable);
- an operand fixed to another's register or to a clobber;
- APX's registers r16 to r31;
- statements of more than 64 operands;
- templates the assembler refuses, with its message, and a template's CFI
  directives, whose frame would be the helper's own unwind entry's;
- a label two templates define, a `.L` name no template defines, and a label
  with one of the unit's symbols' names, which GNU as and clang reject;
- a label in a template of no instructions, which would be a place in its
  function's code;
- a template that returns before any branch (`ret`, `rep ret`, a `leave` and
  `ret`), which would leave the helper's frame reserved; a template that jumps
  over its return, as a called snippet's label does, stays admitted. The
  assembler takes these instructions in `.s` files and file-scope assembly;
- in a naked function, statements other than `asm` statements, which clang
  rejects, operands other than integer constants, and a body of no
  instructions.

## Why it is scalar

The planning, the substitution and the helper's records are scalar, a statement
at a time. The assembler's lexer, parser and matcher run by the checker's path.
A unit has few statements: each of `main.c`'s units has 58, of which 30 make
helpers. On the 9700X, pinned to CPU 2, `-c` of 1,000 functions of four
statements each (registers with a modifier, a barrier, cpuid's fixed registers,
a memory output: 3,000 helpers) takes 29.0 ms. The same functions with C in the
statements' place take 18.2 ms (medians of 21 runs each). That is 2.7 µs and
47,000 user instructions a statement.

Most of it is what any function costs: the helpers are 3,000 more functions to
size, encode, describe and emit. A profile of the user cycles puts 7.6% of the
compile in the statements' own work: the planning and substitution 3.9%, the
assembler's passes 3.4% (most of it `asm_section_index`'s setup per template)
and the plan's walk 0.3%. That is about 0.55 µs a statement. The matcher's
index costs its initialization once per unit, 0.7 to 0.9 ms in
`benchmark-assembler`. For `main.c`'s 58 statements, the statements' own work
is about 30 µs. Vectorizing it across statements would save no more than that.

The names are scalar too: a unit's templates have few of them (`main.c`'s units
have 6). `check_program_asm_names` makes one hash table of them, and the
emission makes one pass over the program's symbols. That pass looks up only
names of the asm names' lengths, and only in a unit whose templates name
anything. Finding the naked functions costs a unit without one a test of each
function declaration's attributes, in a pass over the declarations that
`check_program` makes anyway. [The second part's record](https://github.com/buster14a/buster-performance/tree/e487da1e92944fe9c1db61d81dfd70c24a39724e/docs/measurements/inline-asm-naked-147-2026-10-27)
has its default path's numbers against three mains. Each time, `sqlite3.c`'s
`-c` writes the same object, and the differences are inside what code
placement moves this compile. On one executable, the naked attributes' test
costs 109,213 user instructions (0.006%) and no measurable time.
[The measurement record](https://github.com/buster14a/buster-performance/tree/681f0282cc14230610b11c8defd91afc6cbc910b/docs/measurements/inline-asm-147-2026-10-27)
has the commands, the logs and the default path's numbers against main.

## Tests

- `test-checker` compiles programs of each shape with both of the checker's
  paths and runs them against clang -O0's build of the same source
  (`c_check_program_asm_test.c`):
  - barriers;
  - registers, matching inputs, early clobbers, constants and modifiers;
  - named operands;
  - `cpuid` with fixed registers, and clang's `<cpuid.h>` form with `{att|intel}`
    and `%q1`;
  - the x87's control and status words and MXCSR;
  - `syscall`;
  - branches to the template's own labels;
  - callee-saved clobbers inside a loop of live values, and a statement
    expression's asm under a live value;
  - every callee-saved register but rbp clobbered under eight live values,
    which Tier 0 keeps in callee-saved registers across the call;
  - memory operands;
  - outputs of every width, and `%h`;
  - `xgetbv`, rsp's alignment and basic asm;
  - `fxsave`, `fxrstor` and their `64` forms on aligned arrays, as `main.c`'s
    test of DAZ saves the state: MXCSR's control bits and its mask from the
    saved state, against `stmxcsr`'s;
  - XMM operands;
  - vectors in vector registers' constraints in templates of no instructions,
    for x86-64-v4: `+v` and `+x` outputs of 64, 32 and 16 bytes that keep their
    values, one through a call's pointer, evaluated once, an input that is
    evaluated and dropped, and a comment without operands. Their refusals: a
    template of instructions with an output or an input, a comment that refers
    to the vector, a volatile output, and an input matching a vector output;
  - the operands' order of evaluation, each once, through functions that
    record their calls;
  - rdi and rsi as operands, constants sign-extended as clang prints them, a
    memory input after a register output, and an XMM matching input;
  - matching inputs narrower and wider than their outputs, in templates of no
    instructions and of one, below 0 too;
  - `Z`'s constants at its boundaries;
  - constraints of a register and bounded immediates, whose constants their
    letters take and don't, in templates that assemble only with an immediate
    (`%c1`'s displacement) or only with a register (`%b1`'s `%cl`);
  - naked functions: an AT&T one with a constant operand; one whose label is a
    second entry, which another function's template takes the address of;
    `interpreter_native_bridge`'s shape (its own frame, the flags, `rep movsb`,
    `movq` to and from XMM, `fldl` and `fstpt`, a call through a register, a
    numeric label); and `x86_semantics_test_enter`'s, in Intel syntax (statics
    RIP-relative, a jump through one to another naked function, which jumps
    back to the first one's label, and AVX-512's moves);
  - templates that call the harness's function and read its data, call the
    unit's static function and update its static, and take a `.L` label's
    address that another statement's template defines;
  - a label inside a helper at its exact record, whose snippet another
    statement's template calls;
  - the forms of musl's atomics (`atomic_arch.h`), in functions of the test's
    own (lock prefixes as statements of their own on `cmpxchg`, `xadd` and the
    arithmetic, `xchg`, `bsf`, `bsr` and a `%fs` load), the SSE templates of its
    x86_64 math, likewise
    (`sqrtsd`, `cvtsd2si` into 64- and 32-bit registers, `fabs`'s mask),
    dr_flac's `rep; bsr` in GCC's dialect alternatives, and FMA3's
    `vfmadd132sd` and `vfmadd132ss`, run where the host has FMA;
  - explicit register variables: wrappers of the test's own in the shape of
    musl's syscall wrappers (r10, r8 and r9 beside the fixed registers) on
    harmless system calls, and each shape of a
    variable's operand: a constraint's letter replaced, a cast that keeps the
    width, a 32-bit name, outputs of a register's and a memory constraint, a
    `+` operand in rbx, a matching input in its variable's register, an XMM
    register's, and a template of no instructions;
  - the x87's operands: the shapes of musl's long double functions, in
    functions of the test's own (`fabs`, `fsqrt` and `frndint` on `+t`, fprem's
    and fprem1's loops with the status word in `%ax`, a remainder's quotient bits
    from that word, `fistpll`'s popping store, and `X` operands); each shape of
    the stack's rules (fsincos's two outputs, fyl2x's popped st(1), fscale's kept
    one, outputs pushed by the template, a `float`'s and a `double`'s operands,
    templates of no instructions); and a `float`'s and a `double`'s signaling
    NaNs through `+t` in templates of no instructions, the results' bits and the
    status word's invalid-operation flag as clang's build gives them;
  - templates that take directives: alignment and fill, bytes and `.long`,
    `.pushsection`/`.popsection` and `.section`/`.previous` records read back,
    `.weak`/`.globl` labels and an undefined `.globl` declaration,
    `.hidden`/`.type`/`.size` on a data label, `.equ` with `.set`, `.ascii`,
    `.asciz`, `.string`, `.quad`, `.word`, `.zero`, `.space` and `.bss`, a
    `.nops` control, and a naked function's `.p2align` and `.byte`; and the
    refusals of a `.set` used by another template, a template that returns
    before any branch, and CFI.

  Each refusal above is checked by name, but the label with a unit symbol's
  name, which the emission refuses: `test-checker-driver` checks that one
  through the driver, beside an executable the direct path links, from both
  paths into the same bytes. That executable's naked function has a label
  another template takes the address of, and its template calls libc's `puts`
  by name. The driver's `-c` object of a template's directives is compared with
  clang's too: its `.L` and numeric labels are the assembler's temporary
  symbols, its undefined `.globl` declaration and `.set` absolute are left out,
  and its `.hidden` label has its visibility as clang's object gives it.
- `test-assembler` checks `asm_assemble_code` by both paths: the records, the
  branch columns, the references, each refusal, and the arenas after a failure.
  `--cross-check` compares the newly admitted instructions with GNU as.

## Pending

What the lowering still refuses of what GCC and clang take:

- labels in templates of no instructions (a place in the caller's code), and
  numeric labels or `.` as data;
- inline emission of a template that returns, which clang places in its
  function's own code; this compiler's helper design refuses it by name;
- naked functions of no instructions, and naked functions' operands other than
  integer constants (clang's `"m"(global)`, printed RIP-relative);
- the matcher's gaps: `fstp` without a suffix (GNU as assumes `fstps`), Intel's
  `tbyte ptr`, MMX's `movq`, the x87's stack registers as operands
  (`fxch %st(1)`, and an x87 operand named in a template), `lock xchg`, and the
  instructions outside its families;
- `f`'s x87 registers of the compiler's choice, two operands at one x87
  depth, and explicit register variables in the registers the helper doesn't
  place them in.

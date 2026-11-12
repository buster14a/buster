# Inline assembly

The C driver's lowering compiles GNU C's `asm` statements (#147's "Inline
assembly" item). `c_check_program_asm.c` plans each statement, `assembler.c`'s
`asm_assemble_code` assembles its template into records, and
`c_check_program_lower.c`'s `check_lower_asm` lowers the statement's operands.
A statement of plain register, memory and constant operands is placed in its
caller, as one of Tier 0's machine operations (#1059: "Placed in the caller",
below); most others are placed in their caller as an asm body, the helper's
loads, template and stores in the call's place (#1839: "Asm bodies", below),
and under `--tier1` every statement placed in its caller is one; the rest,
templates of references to names and of directives (and asm goto under
`--tier1`), are ordinary calls of a helper. A naked function's statements
are its whole code instead, and the names templates define and refer to become
symbols of the unit's object or executable.

## The shape: a helper per statement

A statement whose template assembles to instructions, and which isn't placed in
its caller (below), becomes a per-unit helper, a leaf function made of its own
records, which the statement's code calls through Tier 0's `CALL`. That is the shape of the atomic, fence and
floating-environment helpers (`c_check_program_helpers.c`), and
`check_program_emit` publishes the asm helpers with theirs: a local function
each, named `.Lbuster_asm_N` after its statement, sized, encoded and given its
unwind entry.

A template that assembles to no instructions makes no helper and no call, but
one that defines a label, an asm body of no records that marks its place in its
caller (#1839, below). These are the compiler barriers, `asm volatile("" : : "r"(x) : "memory")`, and comments
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

Such a template also takes a vector in a vector register's constraint without
moving it: `main.c`'s barriers `__asm__("" : "+v"(x))` of an `__m512i`
(`c_lexer.c`'s `shift_in_previous`, `x86_64_encoder.c`'s `encode_group`). An
output is then evaluated as its object's address, which the template leaves as
it was, and an input as its value, which is dropped. A vector of 16, 32 or 64
bytes is otherwise moved through its register as clang's code moves it ("Vector
operands", below): a volatile output (clang's code loads and stores it), and an
input matching an output (whose value its output takes). A vector of another size
(`BUSTER_CHECK_ASM_HELD`) is held aside until the template is assembled, and
refused in a template of instructions or one that refers to it.

Why a call, for what isn't placed in the caller:

- The template's fixed registers, clobbers and the x87's stack stay out of Tier
  0's depth registers and homes, and out of both of its paths. Tier 0 already
  saves the values live across a call.
- A helper has its own frame and unwind entry, whatever its template does.
- The cost is a call and a ret per statement run, its operand block's stores and
  loads, and nothing for the barriers.

What differs from clang: rsp inside a helper's template is the helper's, not the
function's. It is 16-byte aligned there, as clang -O0's function bodies have it.
A template can't reach the function's frame, which Tier 0 lays out otherwise than
clang anyway.

## Placed in the caller (#1059)

A statement whose template's records Tier 0's rows can take is no helper and no
call: its records are a row of the typed stream's `X86` entry
(`Tier0Operation`), which names its registers by role, as an x86 builtin's does,
and which Tier 0 places among the caller's values at the statement's place
(`check_asm_row`, `check_lower_asm`). Its operands are the entry's, in clang's
order of evaluation, the order a helper's operand block is filled in: each
output's address, a `+` register output's value loaded through it, then each
input's value (a matching input's converted to its output's type, a narrower
integer zero-extended) or a memory input's address. In the template:

- a general register operand is its value's operand, `INTEGER(j)`, and an XMM
  one of a `float` or a `double` `FLOATING(j)`; a memory operand's base is its
  address's operand; a constant is the immediate it prints;
- a register output is its `+` value's operand or its matching input's, or else
  a scratch register (`SCRATCH(k)`, or the floating stack's free `VECTOR(k)`),
  which no operand is in, as an early clobber needs (and as an output that
  shares no register asks no more of);
- after the template, the row stores each register output through its
  address, at its width (`mov` of 1, 2, 4 or 8 bytes, `movss`, `movsd`), in
  order.

The entry's result is `VOID`. Tier 0 places the operands in its depth registers
and the scratch ones past them, so the values live below them, in depth
registers, homes and deep slots, are no register the row touches. The function
reserves its vectors' slots only for a `VECTOR` value
(`tier0_vector_slots`), so a placed statement's function keeps its frame's
size. `bswap %0` of a `+r` `unsigned` is `mov ecx, [rax]; bswap ecx; mov [rax],
ecx` after its address; `__asm__ volatile("mfence\n\tlfence\n\tsfence\n\tpause")`
in a function of nothing else is those four instructions and its `ret`.

A row reads and writes its operands, its scratch registers, the flags and
memory, and nothing else Tier 0 keeps (`tier0_operation_valid`). So a statement
is placed only where its records name no register but its operands', once the
roles replace them, and every form is one a row takes, by `x86_semantics.c`'s
effects: a VEX, EVEX or SSE form of named operands, or a general-purpose one
(`tier0_operation_general_effects`) without implicit registers (mul's rdx, a
shift's cl, cpuid's, rdtsc's, syscall's), the stack (push, pop, leave),
control flow (a branch, a jump or call through a register, a return, a trap),
the direction flag (cld, std) or the x87's state, and covered: a form the tables
don't cover (clflush, ldmxcsr) has the conservative record of every register.
`lfence`, `sfence`, `pause` and the data prefetches have rows of their own
there (#1059), beside `mfence`'s.

To find the literal registers, the template is assembled a second time for the
row, its operands in registers of `check_asm_row_registers` (none rax, whose
accumulator forms name it implicitly) and xmm1 to xmm15. A literal register is
then one no role maps: an extended statement's template writes one `%%rax`, so
it takes no `%%`, nor a syntax switch, which Intel's bare names would need. A
high byte register (`%h0`), an /is4 operand, a YMM or ZMM register, MMX's and the
segment registers are refused too; the opmask registers by number are a row's
as they are. The clobbers are then the records': `cc` and `memory` need
nothing, as Tier 0 keeps no flags and no memory values across an entry, and a
register clobber names one the records don't write.

The operands it takes are a general register's of any of the sixteen (`r`,
`q`, `g` where it isn't a constant: not a fixed register, `Q`'s or `R`'s smaller
class, or an explicit register variable's), an XMM register's of a `float` or a
`double` (`x` and `v`, not fixed), memory (`m`, `o`, `V`), and constants; at
most 8 operands and 8 records with the stores, 4 scratch registers. A template
with a label, a branch or a symbol, an asm goto, the x87's operands and flag
outputs keep their helpers. A statement the planner doesn't place keeps its
exact helper: nothing changes for it.

The template's rsp is its function's, but no placed template names it: rsp is
no operand's register and no literal is placed. A placed statement changes
neither the frame nor its unwind entry, which `unwind_tier0_frame` reads from
the function's records as it reads Tier 0's own. A row is Tier 0's alone:
Tier 1's IR has no operation for a placed statement's row (`ir_simd_typed_row`
decodes only the vector and mask rows of the x86 builtins), so the C driver's
`--tier1` places such a statement as an asm body instead
(`CheckProgramOptions.asm_bodies`, "Asm bodies" below), which Tier 1 takes, and
the IR's source tools (`test-ir-source`, `test-tier1-cfg-source`), whose text
form has no syntax for a body either, keep every statement's helper
(`CheckProgramOptions.asm_helpers`). `--helper-report` counts each placed
statement as the `asm` family's direct form.

## Asm bodies (#1839)

Clang places every asm statement in its caller, and the owner decided that
this compiler does too ([#1447](https://github.com/buster14a/buster/issues/1447#issuecomment-6736401463),
item 2). A statement that isn't a row (above), and whose template has no
reference to a name (but an asm goto's to its labels, below, outside `--tier1`) is an asm body: the records a helper would have
between its frame and its return, which run in the statement's place in its
function (`check_asm_inline_code`, `Tier0AsmBody`):

1. the x87 operands' loads from the operand block, the deepest first, each from
   the block's frame place (`BUSTER_TIER0_ASM_BLOCK`, which Tier 0 replaces with
   the block's home). The register operands (an input's value, a `+` output's,
   a memory operand's address) aren't loaded: they are the call row's
   arguments, each located in its template register, which Tier 0's moves put
   there (`CheckProgramAsm.inline_passed`, `check_lower_asm_call`). On the
   9700X the block's round trip cost the `"c"` shift kernel 12% against its
   helper: a load of a slot the lowering had just stored through another base
   register failed store-to-load forwarding once a run
   (`ls_bad_status2.stli_other`, 99 million in 100 million iterations);
2. the template's records, its own branches to its numeric labels among them
   (`1:` and `jnz 1b`), which Tier 0 emits as local branches of the function,
   sized by branch sizing with its own (`Tier0AsmBody.branches`); a label at the
   template's end is the first store's record, as in a helper;
3. the flag outputs' `setcc` stores, the register outputs' stores through their
   addresses, the x87 outputs' stores from st(0) down and the pops of the x87
   inputs the template left, each address loaded from the block into a store
   register that no output holds.

Its named labels (`.Lx%=:`, or a global name another template refers to) are
the function's: each at its record in the body (`Tier0AsmBody.label_records`,
past its loads; a template of no instructions but a label has no records, and
its label is the next record's), whose place in the function's code Tier 0
writes as it emits the body's call (`Tier0Code.asm_label_records`), and whose
symbol the emission defines at that record's offset, as a naked function's
labels are (`EmittedCode.asm_label_records`). Such a function isn't inlined by
Tier 0 (`tier0_inline_callee_valid`), so each label is defined once. The IR's
body call carries no labels, so under `--tier1` a template of instructions and
labels keeps its helper, which Tier 1 calls, and a function with a label of a
template of no instructions is Tier 0's (`ir_lower`'s `IR_ERROR_UNSUPPORTED`). A template that returns and has a label keeps the
helper's refusal.

The lowering stores the outputs' addresses (and the x87 inputs) in the block
as for a helper, leaves the register operands' values on the stack in the
operands' order, and ends the statement with a `CALL` whose callee is the body
(`Tier0Function.call_asm`) and whose arguments are those values, each in its
register (a narrow integer as an unsigned one, so that it is zero-extended as
the block's load did). Tier 0 takes it as it takes a call:

- it saves the values live across it, every one in a register where the body
  touches a callee-saved one;
- the registers the body touches (`Tier0AsmBody.touched`) are no home's;
- the prologue saves the callee-saved ones among them, as clang's prologue
  saves `cpuid`'s rbx;
- rsp is 16-byte aligned at the template, as in a function that calls.

There is no call, return or frame of the template's own. `cpuid` with its four
fixed outputs runs as two loads, `cpuid` and four stores in its function.
`rdtsc`, `syscall` (musl's wrappers' explicit register variables among them), a
shift's count in `"c"`, Q's class, x87 operands (`fsqrt` on `"+t"`, fprem's
loops), flag outputs, `ldmxcsr` and the rest place the same way. Tier 0's
always_inline splicing carries a function's bodies into its callers, each copy
of a callee's with its block rebased to the site's locals.

The touched registers are the operands', the clobbers', the stores' and what
`x86_semantics.c` says the template's records write. Some forms common in
inline assembly have only the tables' conservative record: `cpuid`, `rdtsc`,
`rdtscp`, `rdpmc`, `xgetbv`, `syscall`, `fnstsw %ax`, `ldmxcsr`, `fldcw`, the
x87's control forms, `clflush`, `serialize`, `xsetbv` and `vzeroupper`/`vzeroall`.
These take their implicit registers from `check_asm_implicit`. An x87 operation
(`fsqrt`, `fxch`, `fld %st(1)`, `fxsave`) writes no general register. A record
of any other unknown form keeps the helper. So does one that touches the stack
or control flow (a push, a call, a return, a jump or branch but the template's
own to its labels), as `unwind_tier0_frame` refuses in a body, and one that
writes rsp, rbp or r15, which hold the frame.

A template that returns leaves its function, as clang's leaves its own (the
owner's decision; a helper's return would leave the helper, which is refused by
name). Its `ret`, and each record before it on a straight path to it (no branch
among them, no label after the record), run only on the way out. Their effects
on the stack, rbp and control stay out of the checks above, and what they write
stays out of the touched set, since no later code of the function runs after
them. `movl $7, %eax; leave; ret` returns 7 from its function. A function with
such a body (`Tier0AsmBody.leaves`) keeps no local and no value in a
callee-saved register, as one that calls `__builtin_setjmp` keeps none, so that
the way out leaves the caller's registers as clang -O0's does, but for those the
template writes. `--verify`'s frame verifier doesn't read its frame events, as
it doesn't read a naked function's. With `-fno-omit-frame-pointer`, Tier 0's
frame is rbp's and `leave; ret` returns to the caller, as from clang -O0's
frame; without it, the template meets Tier 0's frame, which differs from clang's
as any template that reads the frame does.

What differs from the helper: the template's rsp is its function's, as clang's
is, and the statement costs no call, return, pushes or block address.

On the 9700X ([the measurement record](https://github.com/buster14a/buster-performance/tree/181db9f04/docs/measurements/asm-bodies-1839-2026-11-09)),
loops of one statement each, against the same compiler's helpers
(`--helper-forms=asm`):
- a flag output is 38% faster, a `"c"` shift count 43%, `mulq`'s fixed
  registers 39% and cpuid 2%;
- rdtsc and x87 `fsqrt`, dominated by their own latency, are level;
- `-c` of `main.c` is unchanged (2.197 s → 2.189 s).

Tier 1 takes asm bodies too (#1839, the owner's decision in
[#1447](https://github.com/buster14a/buster/issues/1447#issuecomment-6736401463),
item 2), and the C driver's `--tier1` places every statement it places in its
caller as one, none as a row (`CheckProgramOptions.asm_bodies`), so that Tier 1's
code runs the statements in their places, as Tier 0's does. The lowering to the IR
makes the body's `CALL` a body call ([the IR guide](ir.md)'s "Asm bodies"): an
opaque call of the operand block's address, which every pass takes as an unknown
callee's of an escaped object, so that the block's stores stay before it and no
access moves across it. Tier 1's selection makes it `TIER1_RULE_ASM` and its CFG
consumer writes its records in place ([the selection guide](tier1-selection.md)'s
"Asm bodies"): the values live past it in the registers it touches saved into
their rows around it, its block's base the frame's base register, its branches
the function's, and the callee-saved registers it touches pushed by the prologue.
A function whose body returns (`Tier0AsmBody.leaves`) is still Tier 0's
(`ir_lower`'s `IR_ERROR_UNSUPPORTED`, which `--tier1` falls back from), as one
that calls `__builtin_setjmp` is. A template that writes opmask registers is a
body under `--tier1` too: `check_asm_inline_touched` records the ones its records
write (`CheckProgramAsm.inline_masks`, by `x86_semantics.c`'s opmask effects), and
Tier 1's CFG consumer saves the masks live in them around the body, as it saves
them around a call (`IrAsmBody.masks`, `tier1_emit_cfg_asm`).
`--helper-report` counts each body as the `asm` family's direct form. The
interpreter (`interpret-c`) refuses a function with a body, as it doesn't run
machine code.

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
rule, the call's), whatever the templates push, unless the templates give CFI
(#1848). Clang splices a naked function's CFI directives into the FDE of its
implicit `.cfi_startproc`, with no prologue rows. So a source with a `.cfi_`
directive is assembled between `.cfi_startproc` and `.cfi_endproc`, and
`check_asm_naked_frame_rows` decodes the frame description the assembler wrote
into the emission's frame rows (`CheckProgramNaked.frame_rows`), which become
the function's unwind entry, a row program
(`BUSTER_UNWIND_ROWS`); the unit's own `.eh_frame` is left out
(`check_program_emit_asm_unit`'s `omit_frames`). The rows are clang's: a
backtrace through the function walks to its caller, which
`test-checker-driver --asm-definitions-only` checks on both paths. The bytes
encode the same rules with the row writer's advances (`DW_CFA_advance_loc4`
where clang's is `DW_CFA_advance_loc`). Refused by name: CFI beyond a frame's
rows (expressions, escapes, signed offsets) and a frame description that isn't
a function's (another CIE, several FDEs, a template's own `.cfi_startproc`).
`unwind_tier0_frame` and `--verify`'s analyzer don't see the function. Its one
source row is at its first statement.

`main.c`'s two naked functions, `interpreter_native_bridge` and
`x86_semantics_test_enter`, compile into clang's bytes, at clang's offsets in an
object of the three functions this part of #147 claimed (but the padding
between functions: ours `int3`, clang's a `nop`).

## Labels and names

A template's named labels, `.L` ones among them, are local symbols with no type,
each at its record's offset in its helper's, its naked function's or (an asm
body's) its caller's code, as GNU as and clang make them. Every other symbolic field refers to a name, which the
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

A label in a helper (a template that refers to a name keeps one) is inside the
helper, not the statement's function: its address is right, as data and as a
call's target. Jumping to it from outside
runs the helper's epilogue, which only a template that leaves the helper as it
came in can rely on.

## Placement

`check_program_asm_plan` places each operand by its constraint's first
alternative (`check_asm_constraint_parse`):

| Constraint | Place |
| --- | --- |
| `r`, `q`, `l`, `R`, `Q` | a general register of the class (`Q`: rax, rbx, rcx, rdx; `R`: the first eight) |
| `a b c d S D` | that register |
| `A` | rax for up to 8 bytes, printed by its 64-bit name; rdx:rax for an `__int128` |
| `x`, `v` | an XMM register, for a `float` or a `double`; an XMM, YMM or ZMM register for a vector of 16, 32 or 64 bytes |
| `t`, `u` | the x87's st(0) and st(1), for a `float`, a `double` or a `long double` |
| `y` | an MMX register, mm0 up, for an integer, a pointer, a `float` or a `double` of up to 8 bytes |
| `k`, `Yk` | an opmask register, k0 up (`Yk`: k1 up), for an integer or a pointer of up to 8 bytes |
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
  prints `%r10d`), and xmm0 to xmm15 for a `float`, a `double` or a 16-byte vector;
- xmm16 to xmm31 too (#1846), where the body's instruction set has AVX-512F
  (its function's target attributes among them): the helper moves them by
  EVEX's `vmovd` and `vmovq`. Without it clang can't place one ("could not
  allocate output register"), and the lowering refuses it by name;
- ymm0 to ymm31 and zmm0 to zmm31 (#1846), for a vector of their size ("Vector
  operands", below);
- mm0 to mm7, k0 to k7, and the x87's st (st(0)) and st(1) (#1846, the owner's
  decision on #1447): the variable's operand is an MMX, opmask or x87 one in that
  register (st at t's depth, st(1) at u's), whatever its constraint's letters,
  as clang's code places it, of the types that class takes (an opmask's with
  its moves' features);
- a high byte register (#1846), `register unsigned char c __asm__("ah")`: a
  byte's operand is the high byte, which the helper loads and stores by its
  name (through a register of the first eight, as a REX prefix can't name it)
  and the template prints by it, `%ah` (under a modifier, the register's other
  names: `%b0` is `%al`, `%k0` `%eax`, as clang prints them); a wider value's is
  its register's low bytes, as clang's code takes a `short` in "ah" as ax. The
  variable takes its whole register: an operand fixed to rax beside an "ah"
  variable is refused, where clang's code loads both and the byte's load
  overwrites the other's bits 8 to 15.

A statement of a high byte's or an XMM16-up variable is an asm body (#1839) that
loads its operands from its block by those names, as the helper does, rather
than take them as its call's arguments (`check_asm_inline_passes`).

rsp (`"rsp"`, `"esp"` or `"sp"`, #1846, the owner's decision on #1447) is placed
as clang's code places it: an output is read from rsp after the template, by
the variable's width (`movw %sp`), even after a template of no instructions
(`asm("" : "=r"(sp))` reads rsp, as clang's does); an input's value is moved
into rsp before the template, after every other load and at the variable's
width (a `short`'s keeps rsp's other bits, as clang's `movw %ax, %sp` does), and
the template names it `%rsp` (`%esp`, `%sp`); an input beside an rsp output that
isn't early-clobbered or `+` shares it. An input that isn't rsp's own value
leaves the frame as clang's code leaves it: broken (an uninitialized local's
`"r"(sp)` is garbage there too, and clang's build crashes). Such a statement is
always an asm body (#1839), whose records load the input themselves, never a
helper, whose rsp would be its own: a template that takes directives, or one
that keeps a helper for another reason, refuses it by name. A function with a
body that moves a value into rsp (`Tier0AsmBody.stack`) leaves its frame to the
program, as one whose template returns does: `--verify` doesn't read its frame
events, and Tier 1's code certificate leaves it uncertified (`IrAsmBody.stack`,
a dynamic frame's reason). (`spl` isn't a register variable's name for clang, which rejects it.)

The variable is an ordinary local otherwise: the operand block takes its value,
or an output's address, as any operand's, and the helper loads it into the
register (a callee-saved one among the helper's pushes).

### Global register variables

`register long sp __asm__("rsp");` at file scope (#1846, the owner's decision on
#1447) is a global register variable, as clang 23 takes one: only in rsp and rbp
of 8 bytes, or esp and ebp of 4, of an integer or a pointer type. The checker
gives clang's errors otherwise (`check_input_register_label`), each at clang's
place, and goes on past them as clang does: "illegal storage class on
file-scoped variable" without a label, "unknown register name 'X' in asm" for a
name clang's x86-64 target doesn't know (a local register variable's too:
`TargetInfo::isValidGCCRegisterName`'s names, an index of up to 180, past one
`%` or `#`), "register 'X' unsuitable for global register variables on this
target" for any other register (r12 among them: clang reserves no register
for one), "size of register 'X' does not match variable size", and
"unsupported type for named register variable" at the type specifier. A
global register variable of a constraint that allows memory is clang's
"reference to a global register variable in asm input|output with a memory
constraint" (its words exchanged, as for a bit-field), and `&sp` is "address of
register variable requested".

### Clobbers', operand sizes' and asm labels' errors

A clobber is checked as clang's `TargetInfo::isValidClobber` checks it (#2262):
`memory`, `cc`, `unwind`, or a register name clang's x86-64 target knows
(`check_input_register_name_valid`, the register variables' set: past one `%`
or `#`, GCCRegNames and AddlRegNames, or an index of up to 180 in
`getAsInteger`'s radixes). So `sil`, `dil`, `spl`, `bpl`, the segment
registers, `rip`, `st0` and `st(0)` are clang's "unknown register name 'X' in
asm" at the first such clobber's string, after the operands' constraints, and
`dr0`, `cr0`, `fpcr`, `bnd0`, `tmm0` or `"17"` are taken. The lowering reads a
clobber as clang normalizes it (`check_asm_clobber_name`: an index becomes its
name, `"0x10"` `argp` and `"38"` `r8`); the control, debug, bound and tile
registers and the flags' names need nothing of a helper or a body.

A redeclaration whose asm label differs from an earlier declaration's is
clang's "conflicting asm label" at its name, noted "previous declaration is
here" at the earlier label's string: a linked one at the unit's end
(`check_input_asm_labels`), and one that conflicts otherwise (a redefinition,
another type, a local register variable declared again in its scope) before
that conflict's own error, as clang's `mergeDeclAttributes` reports it
(`check_input_asm_label_conflict`). `test-checker-driver`'s asm units compare
both with clang's whole output.

An operand too wide for its constraint is clang's "invalid output|input size
for constraint 'C'" (#2231, `X86TargetInfo::validateOperandSize`), at the
operand, with the constraint as written: by its first letter past `=`, `+` and
`&`, `x`, `v` and `Yz` take the function's widest vector register (512 bits
with AVX-512F, 256 with AVX, else 128, by the unit's features or a `target`
attribute's on the function's definition, `CheckInputAsm.vector_bits`); `y`,
`k`, `Ym` and `Yk` 64 bits; `f`, `t` and `u` 128; and `Yi`, `Yt` and `Y2` any
size, as SSE2 is every target's here. The check follows an operand's own
(`check_asm_operand_checks` without `whole`) and precedes its tie's, and the
statement's first error alone is given, as clang returns there.

A constraint's or a clobber's error drops its statement, and checking goes on
past it as clang's `ActOnGCCAsmStmt` returns there (#2369): the error is
recovered (`check_input_recover_reports` in `check_input_asm_statement`), and
the statement is no asm statement of the unit's. A later syntax error's
repaired view keeps it, the statement's `)` being the original's
(`check_repair_input_report`).

Differences left: a `target` attribute on an earlier declaration alone, not
the definition, doesn't widen an operand's limit, where clang's
`getFunctionFeatureMap` reads the inherited attribute.

An asm label on a block-scope object of automatic storage (none or `auto`,
C23's inferred `auto` and `constexpr` too), not `register`, is clang's
"ignored asm label 'X' on automatic variable" (#2370,
`warn_asm_label_on_auto_decl`, in no group: `-Werror` makes it an error, `-w`
silences it), at the label's first string, X its decoded text
(`check_input_asm_labels`, `CHECK_WARNING_ID_ASM_LABEL_ON_AUTO`). A static,
extern, function or typedef label is not warned, as clang's. A `for`
statement's declarations take asm labels as a block's do (#2419: the scalar
parser's declarator reads one in the for header's frame too). Left: a
block-scope `_Thread_local` object's error is "invalid or unsupported
declaration", not clang's (#2420).

Such a variable has no object and no symbol (an initializer is ignored, as
clang's): a read is its register's value, rsp's by `STACK_SAVE` and rbp's by
`FRAME_ADDRESS`, which gives its function a frame pointer (clang's code reads rbp
in one; under `--tier1`, such a function is Tier 0's, as `__builtin_frame_address`'s
is), the low half for esp and ebp; an assignment of rsp or esp sets rsp
(`STACK_RESTORE`, esp's value zero-extended, as clang's `movl` does). An
assignment of rbp or ebp, and an update (`sp += 8`, `sp++`), are refused by name.
As an asm operand it is the register itself (`CheckAsmPlace.global`): the
template names `%rsp` or `%rbp`, and nothing is loaded or stored, since clang's
code moves the register's own value into it and back (but for esp and ebp
inputs, whose `movl` clears the register's upper half: clang's build crashes);
rbp's gives its function a frame pointer too.

### A's operands

`A` is clang 23's on x86-64 (#1846): a value of up to 8 bytes in rax,
zero-extended there as clang's code widens it to 64 bits, which the template
names by rax's 64-bit name (`%rax` for an `int` too; `%k0` is `%eax`); and an
`__int128` in rdx:rax, its low half in rax, its high half in rdx, which the
template names `%rax` (GCC's "ax:dx pair"). An input fixed to rax or rdx shares
its register with a pair output that isn't early-clobbered or `+`, as clang's
code shares it: mulq's `"=A"(r) : "a"(x), "rm"(y)` multiplies x in rax. The
helper loads and stores the pair by its halves, from a slot of 16 bytes, and so
does an asm body (#1839), which loads its operands from its block.

### Vector operands

A vector of 16, 32 or 64 bytes in `x` or `v` (#1846, the owner's decision on
#1447) takes an XMM, YMM or ZMM register, xmm0 up, as clang's code places it:
32 bytes need AVX and 64 AVX-512F (clang's Sema refuses the sizes otherwise),
and the template names it by its size, `%xmm0`, `%ymm0` or `%zmm0` (`%x0`,
`%t0` and `%g0` name the others). So does an explicit register variable in
its size's register (`register __m256 y __asm__("ymm7")`, xmm16 up's and
ymm16 up's with AVX-512VL); one of another size is clang's "does not match
required type", refused by name. Its block slots hold addresses
(`CheckAsmPlace.packed`): an output's of its object, an input's of a copy the
lowering stores its value into (a transport vector's own), and a `+`
operand's value's of a copy loaded through the output's address, so that the
operands are evaluated in clang's order. The asm body, or the helper, loads
each address into rax before the general registers' loads and the vector
through it by movups (VEX's with AVX, EVEX's for ZMM and for XMM16 up), and
stores an output through its address after the template. clang's code moves
them by movaps; the slots' addresses needn't be aligned. A template that takes
directives refuses them by name.

### MMX's operands

`y` places an operand in an MMX register (#1846), mm0 up but the ones the
clobbers name (`"mm0"`): an integer, a pointer, a `float` or a `double` of up
to 8 bytes, as clang takes them, and an `__m64` (#1846, the owner's decision
on #1447): its 8 bytes as an unsigned long, an input's read through its
object's address (`CheckAsmPlace.m64`), since Tier 0 has no 8-byte vector load.
An `__m64` input of no object (`v + v`, a call's) is read through a copy's
address, which the lowering stores it into (#1846). The template
names it `%mm0`; clang refuses every modifier of one, and so does the lowering.
The helper loads it as clang's code does, zero-extended into rax and moved into
its register (`movzwl`, then `movq %rax, %mm0`), before the general registers'
loads, and stores an output's low bytes through its address (`movq` or `movd`,
a narrower one's through a general register). As in clang's code, nothing runs
`emms`: the template's author leaves the x87's state as the code after it needs
it. A statement of MMX and x87 operands (whose registers are the same) loads its
x87 operands first and its MMX ones after, and stores its MMX outputs before
its x87 ones, as clang's code orders them (#1846, the owner's decision on
#1447): what the x87's stack holds after an MMX load is the template's to know.
A statement of MMX
operands is an asm body (#1839) that loads them as the helper does, through rax
before its other loads, from its block. MMX's forms, which `x86_semantics.c`
doesn't cover, are taken to write the general and XMM registers their fields
name and nothing else, and `emms` nothing. A template of no
instructions has no helper, as for general registers: clang's code would move
the values through an MMX register and back, unchanged.

### The opmasks' operands

`k` places an operand in an opmask register, k0 up, and `Yk` in one a write
mask takes, k1 up (#1846), but the ones the clobbers name (`"k1"`): an integer
or a pointer of 1, 2, 4 or 8 bytes, as clang takes them (a `float` crashes
clang's code generation; the lowering refuses it by name). The template names
it `%k0`; clang refuses every modifier of one, and so does the lowering. The
helper loads it zero-extended into rax and moves it in by its width (`kmovw
%eax, %k0`, `kmovd`, `kmovq`), and stores an output's bits through a general
register, as clang's code does, and so does an asm body (#1839), whose
operands are then its block's and whose operands' registers are among the masks
Tier 1 saves around it. kmovw needs AVX-512F, kmovd and kmovq
AVX-512BW, by the body's instruction set (its function's target attributes
among them): without them, clang can't place the operand ("could not allocate
output register"), and the lowering refuses it by name.

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
  st(0) down above the inputs it didn't pop (fscale's st(1), fprem's);
- two operands at one depth are one register's, as clang's code makes them
  (#1846): two inputs' depth holds the first matching or `+` input's value in
  clang's order of the inputs (the explicit ones, then the `+` outputs'), as
  LLVM copies the tied ones into the register last, else the last other
  input's; the others aren't loaded. Two outputs at one depth each take its
  value: the helper, or the asm body in its caller, stores all but the last
  from a copy (`fld %st(0)`). GCC refuses such a statement ("impossible
  constraints").

A template names a `t` operand `%st` and a `u` operand `%st(1)`, as GCC and
clang print them (#1115: `"fmul %1, %0" : "+t"(x) : "u"(y)`), which the
assembler reads with GNU as's operands and AT&T names of the x87's operations
([the assembler's guide](assembler.md)); a modifier of one is refused.

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
  addend, and `loop` and `jrcxz` to its labels with an addend or across a `jmp`
  or `jcc` that branch sizing sizes: the planner assembles these whole too
  (#1845), as their failure asks (`AsmCode.directive`), so that `leaq
  1f(%rip)`, `.`'s address and such a `loop 1b` are the assembler's own fixups
  and branches, as clang's;
- undefined numeric labels;
- what the matcher doesn't take.

A `loop` or a `jrcxz` to a label of the template's own, without an addend and
with no `jmp` or `jcc` that branch sizing sizes between it and its label, is a
record whose rel8 displacement the assembler fixes once the records are, by
their lengths (#1839's item 4: these have no rel32 form, which branch sizing
would need to grow them, and nothing between them can grow). A body or a
helper takes it as any record; `check_asm_implicit` gives `loop` its rcx and
`jrcxz` nothing, and `check_asm_inline_touched` takes it as the template's
own branch. So `1: addq %rcx, %0; loop 1b` is an asm body in its caller, as
clang places it.

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

Such a whole helper is the record helper's own code (`check_asm_helper_code`,
for a template of no records) around the template, its records written as the
bytes they encode, a `.byte` line each (`check_asm_unit_helper`, #1839's item
10): its pushes, frame and every operand's load (the x87's, MMX's, the
opmasks', A's pairs' and high bytes' among them) before the template, and after
it, in the helper's default text section, an asm goto's exits, the flag
outputs' `setcc` stores, every output's store, the x87 pops and the epilogue.
An asm goto's labels are its exits' labels there (`.Lbuster_goto_S_L`, the
names the substitution printed for them), so that the template's jumps reach
them. The template is first assembled alone with those names made global
(`__buster_goto_S_L`). Any use of a label is its exit's address there: a
jump's, and data's (#1845's item 1: the Linux kernel's `__jump_table` entries,
`.quad 1b, %l[yes]` and `.long %l[yes] - .`), a rip-relative address's or a
call's, as clang's use is the address of its `callbr`'s indirect target, the
block that stores the outputs and goes to the label. A template of directives
alone with x87 operands or flag outputs has this helper too, as a template of
no instructions does. A held vector (a vector type in a vector register) is
refused in a helper, as on the record path. A template with a 32-bit address
(`leal (%1,%2), %0` of `int` operands, #2339) is assembled whole in such a
helper too, since a record has no room for its 67 (`docs/assembler.md`).

A template of alignment, section and symbol directives alone has no code
(`entry_code` is false): its sections, data and defined labels are published,
and the statement calls nothing, so zstd's `__asm__(".p2align 5")` before a hot loop
adds no call. The alignment aligns a helper's own code or atom, not the
caller's. A naked function's templates are one source too, without a prologue
or epilogue, whose CFI directives are the function's frame rows (#1848, in
"Naked-function lowering"). A function's template's CFI is refused by name
("inline assembly CFI directives in a function's template"): clang adds the
directives to the enclosing function's own frame description at the
template's place, which a helper of its own, with its own unwind entry,
can't do. It would need the template placed in its caller as an asm body
(#1839) and its rows merged into the caller's `EmissionFrameRow`s.

The templates of a unit are assembled apart, so a directive's symbols are the
template's: another template's reference to a `.set`/`.equ` absolute symbol is
refused by name (`inline assembly absolute symbols are per-template`), while
two templates may define the same absolute name, each its own; a `.globl` or
visibility declaration upgrades a label another template defines, while a
`.type` or `.size` declaration applies to its own template's label alone.
These match what one template does; clang's module-wide assembler would carry
the rest across statements. A `.globl`, `.local` or visibility directive on one
of the unit's C symbols changes that symbol's own binding or visibility, while
`.weak` applies to a function's, an extern's or a static variable's (under the
link name the compiler gives it: a block-scope static's is `function.name`); a
defined variable of external linkage is refused, as clang refuses it, and a
zero-filled static is accepted where clang's assembler refuses it, while an
initialized static matches clang's object. `.size` applies once the definition's
atom exists: a function's or a variable's, where an extern's is the no-op both
compilers make it. A `.size` past that atom is refused by name. `.nops`'s
optional control is accepted and ignored, an undefined
`.globl`, `.weak` or visibility declaration that no template refers to is left
out of `-c`'s object (clang keeps its undefined symbol), and so is a
`.set`/`.equ` absolute symbol (clang writes a local `ABS` symbol).

A template's label, or its `.comm` (#1848), may define a symbol the unit
declares without defining (`void f(void);` and `f:`, `extern long n;` and
`.comm n,8,8`): the program's symbol is then the template's, as clang's
assembler binds the C references to it. It has the template's type (none
without `.type`, a `.comm`'s `SHN_COMMON` one), binding (local without a
binding directive, as clang's object has it) and visibility, or else the
declaration's; it is no undefined name for the direct path's runtime to
supply, and C's code reaches it as it reaches any symbol it declares (through
the GOT for an extern variable, as clang's code does). A `.comm` of a name the
unit doesn't declare is the template's own common symbol. Both are
`BUSTER_LINK_SYMBOL_COMMON` symbols of their own atoms, as C's commons are,
which `-c`'s object writes `SHN_COMMON` and the direct path and the units' merge
place as they place C's.

A template's name of a symbol the unit defines is what clang 23's assembler
makes of it (#1848, `check_program_emit_asm_symbols`):

- A `.comm` of an external variable the unit defines (`int x = 1;`, a tentative
  `int x;` without `-fcommon`, `char x[32];`, `_Alignas(32) int x;`), in a
  file-scope or a statement template, before or after the definition, makes the
  name clang's common symbol: one `SHN_COMMON` symbol of the C definition's size
  at the `.comm`'s alignment (`.comm x,8,8` makes 8, `.comm x,64,16` 16), the
  `.comm`'s own size ignored. The definition's bytes stay in their section
  (`.data`, `.bss`) without a symbol (`int x = 7; int y = 3;` keeps `.data`'s 8
  bytes, `y` at 4), and C's references, a static pointer's relocation among them,
  are the common's, so the linked program reads the common's zeros, not the
  initializer. The static data defines the symbol at C's atom as usual; the
  template's common atom is recorded instead of defining it
  (`CheckProgramCode.commoned_symbols`, `check_program_emit_asm_unit`), and
  `check_program_sections` then moves the symbol to that atom, sized as C's, of
  the common type, on `-c`, the direct path and both checker paths alike.
  Refused by name: a C common symbol's `.comm` (`int x;` under `-fcommon`, or the
  common attribute), since clang's backend crashes for another size or alignment
  ("Symbol: x redeclared as different type") and keeps the one common for the same
  (buster refuses both); a function's (clang keeps its code without its symbol and
  makes a `FUNC` common); a static (clang reports a zero-filled one's "changed
  binding to STB_LOCAL" and makes an initialized one a global common), weak
  (clang warns and makes it a weak common), thread-local (a `TLS` common), alias's
  or selectany definition's; and a read-only one's (`const int x = 1;`), whose
  reads clang's code folds to the initializer where the program would read the
  common's zeros. Without an alignment operand, a C unit's `.comm` and every
  `.lcomm` align to 1, as clang's integrated assembler aligns them, where GNU
  as takes the size's power of two (#2304; `docs/assembler.md`).
- A file-scope label of an external, non-weak definition (`int x = 1;` with
  `asm(".globl x\nx: .long 2")`, `int f(void)` with `f: ret`, `int x;` with a
  `.data` label), without a binding directive but `.globl`, is clang's
  assembler error, as clang prints it, without a source location, at `-c` and
  the direct path but not at `-fsyntax-only` (`CheckProgramRedefined`,
  `check_driver_emission_failed`): `error: symbol 'x' is already defined`, once
  for a function's or a common symbol's definition (`int x;` under `-fcommon`)
  and twice for another variable's, as clang streams the module's assembly
  before its definitions. Refused by name ("inline assembly labels named as the
  unit's symbols", or "... as several of the unit's definitions"), where clang's
  output depends on what buster doesn't model: labels of several definitions
  (clang reports them in its module's emission order, functions first), a
  statement template's label (clang reports an earlier function's at the
  template, with a note, and a later one's without a location), a static
  definition's (clang emits none for an unused one, and the label stands), an
  `.lcomm`'s or a `.local`/`.weak` label's (clang adds "x changed binding to
  STB_GLOBAL"), a weak definition's (clang adds a binding warning) and an
  absolute (`.set`) symbol's (which clang takes, the C definition keeping the
  name).
`test-checker-driver --asm-definitions-only`
(`c_check_asm_definition_driver_test.c`) compares each fixture's `-c` symbols
and data relocations with clang 23's, on both paths, and runs the objects
linked by clang and the direct path's executables against clang's build.

The matcher's index admits every form the assembler's syntax takes, whatever
the unit's target. GNU as and clang's integrated assembler take any instruction
in a template. The lowering makes the index on its scratch arena, once per unit
with asm statements, and frees it after the planning.

The matcher admits every form the form table has whose operands the
assembler's grammar reads (#1134: [the assembler's guide](assembler.md)'s "The
admitted forms"): `main.c`'s statements' `cpuid`, `xgetbv`, `fnstcw`, `stmxcsr`
and the others, its naked functions' `pushfq`, SSE2's `movq`, the x87's loads and
stores, the string instructions with their prefixes and the mask moves, and the
instructions common in inline assembly that it refused before: the bit tests,
`popcnt`, `crc32`, `rdrand`, `rdseed`, `cmpxchg16b`, the prefetches, `movnti`,
`clflushopt`, `movbe`, ADX's and BMI's, AES's, `pclmulqdq`, SSSE3's and SHA's,
the scalar SSE family, `movd`, `pextrd`, `pinsrq`, and AVX's, AVX2's and
AVX-512's (`vpbroadcastd`, `vpermq`, `vpternlogd`, `vpcompressd {%k1}`). The
index has every feature, so the template's author chooses the instructions the
CPU runs; among an instruction's encodings the matcher takes VEX's before
EVEX's, so that an AVX instruction stays AVX's (its guide's choice of forms).
The operandless `pause`, `lfence`, `mfence`, `sfence`, `rdtsc` and `rdtscp`
are also available (#798), with register outputs and CPU support chosen by the
template's author.
`test-assembler --cross-check` compares them with GNU as. The native
inline-assembly fixtures also run alone with `test-checker --program-asm`,
including the eight mask saves and restores at the semantics harness's offsets.
An `xchg` takes its memory operand first in Intel order, as the catalogue has
it: `xchgl %ecx,(%rdi)`,
not `xchgl (%rdi),%ecx`. An `xchg` of the accumulator with another register
takes the one-byte form, as GNU as does.

For musl's templates (`arch/x86_64/atomic_arch.h`, `pthread_arch.h` and
`src/math/x86_64`) the matcher also admits the `lock` prefix, as a statement of
its own (`lock ; cmpxchg %3, %1`, which GNU as applies to the next instruction)
or on the instruction's line, on `cmpxchg`, `xadd`, the arithmetic of memory
destinations and `xchg` (#1846: `lock xchg %0, %1`, F0 before 87, which GNU as
and clang keep though an exchange with memory is locked without it); `bsf`, `bsr` and `hlt`; SSE's `sqrtsd`, `cvtsd2si`, `andps` and
SSE2's `psrlq` with their siblings; FMA3's scalar forms; the x87's operations on
the stack's top (`fsqrt`, `fprem`, `frndint` and the rest), `fnstsw %ax` and
`fistpll`. Since #1115 the x87's register operands (`fld %st(0)`, `fxch %st(1)`,
`faddp %st, %st(1)`), the TLS models' relocation modifiers (`movl
%fs:x@tpoff, %eax`) and the compare-predicate names (`cmpltsd`) are the
template's too. `rep; bsr` (dr_flac's spelling of `lzcnt`) is `lzcnt`'s form, its
bytes. [The assembler's guide](assembler.md) has the admitted forms, and the
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
5. an asm goto's exits (below);
6. the flag outputs' stores, while the flags are the template's (no record
   before them sets any, nor do these): each condition's `setcc` into a
   register, zero-extended, then one store over its object at its width, an
   `_Atomic` object's an exchange, through its address in the block's register,
   which is loaded again after it;
7. the register outputs' stores through their addresses, from two registers no
   output holds, then the x87's from st(0) down, and the pops of the x87 inputs
   the template left;
8. an asm goto's exit number moved into eax;
9. the epilogue.

`unwind_tier0_frame` describes it (`Tier0Code.frame_saved` and `frame_size`):
an asm goto's branches are inside the frame, and its exits share one epilogue.

## The x87's f

An input of `f`, an x87 register of the compiler's choice (clang refuses `f`
for an output), takes the shallowest depth no `t` or `u` input, and no `f`
input before it, holds at the template's start, in the operands' order. A
template names it by `%N`, which prints its depth's register, so the depth is
the lowering's to choose, as clang's register allocation chooses it. As LLVM's
stackifier has it, the template doesn't pop it, and the helper pops it after
the stores. GCC's rules require the outputs beside an `f` input to be
early-clobbered; clang's code for a pushed output without the `&` pops the
wrong register after the template, so such a statement's result is clang's
own mistake, not a lowering to follow.

## Flag outputs

A flag output, `"=@ccz"(r)` (clang's 28 conditions: `a`, `ae`, `b`, `be`, `c`,
`e`, `z`, `g`, `ge`, `l`, `le`, `o`, `p`, `s` and their `n` forms, but `pe` and
`po`, which clang refuses), is a place of its own (`BUSTER_CHECK_ASM_FLAG`) with
no register: after the template, the helper writes the condition's value, 0 or
1, over the whole object, as clang's code stores `setcc`'s byte converted to the
output's type: one store of the object's width (two 8-byte ones for 16 bytes,
as clang's are), so that a volatile object takes one, and an `_Atomic` one an
exchange, clang's sequentially consistent store (#1126's review; a 16-byte
`_Atomic` one, which clang stores by a library call, is refused by name). The
object may be of any type of 1, 2, 4, 8 or 16 bytes (a
`bool`, an `int`, an `__int128`, a `float`'s bits, a struct of 4 bytes), as
clang's code stores the value's integer bits into it; clang's Sema takes any
type but `_BitInt`, which it refuses as every asm operand, and its code
generation refuses other sizes, which the lowering refuses by name. A `+` flag
output reads nothing: clang's code loads its value where the template can't
name it. A template of no instructions with a flag output has a helper too, as
clang sets the output after one. With flag outputs lowered, the preprocessor
defines `__GCC_ASM_FLAG_OUTPUTS__`, as clang does.

A template that names a flag output (#1846) prints the register its condition
is set into after the template, as clang prints its own: the setcc register of
its helper or its asm body, 32 bits wide whatever the object, as clang's code
sets an `i32` (`%0` is `%ecx`, `%b0` `%cl`, `%q0` `%rcx`). Nothing reads it
before the setcc, so a template may write it (clang's `setz %b0` beside the
output is overwritten too); which register it is follows each compiler's
allocation.

## asm goto

An asm goto's template refers to its labels by `%l[name]` or `%lN`, N
counting after the operands and the `+` outputs' inputs, as GCC and clang
number them. The substitution prints each label as a name of the statement's
own, `.Lbuster_goto_S_L`, which the assembler takes for an undefined symbol: a
jmp or a jcc to it is a reference from its rel32 form. The planning
(`check_program_asm_plan_one`) turns each such reference into a branch, its rel8
form back, and `check_asm_helper_code` adds after the template an exit per way
out: the fall-through's, a move of 0 into a register no output or store holds
and a jump to the stores, then each label's, a move of its index plus one and a
jump to the stores (the last one falling into them). The template's jumps go to
their labels' exits, branch sizing sizes them all with the template's own
branches, and every way out stores the outputs, so an output's value reaches
each label, as clang's code gives it (GCC's since 11). The helper returns the
number in eax, and the statement's code (`check_lower_asm`) keeps it in a
temporary and branches to each label whose number it is, falling through on 0.
A label as anything but a jmp's or a jcc's target (data, a call's target, an
addend, a rip-relative address: #1845's item 1) makes the template one
assembled whole in its helper (`check_asm_unit_helper`), whose exits the uses
take, as a template that takes directives is; a template's jump through such an
address (`leaq %l[x](%rip), %rax; jmp *%rax`) runs the label's exit, which
stores the outputs, as clang's indirect target's block does. A template of no
instructions never jumps, and has no helper.

Outside `--tier1`, an asm goto is an asm body (#1839's item 2): the same exits
are the body's records after the template (`check_asm_inline_code`), the
template's jumps and the exits' jumps the body's branches, and the exit's move
into eax its last record. Its call row returns that int (`check_lower_asm_call`'s
result, a body call of class I32 to Tier 0's validator), and the statement's
code dispatches on it as on the helper's. The statement costs no call, return,
frame or block address, but still the exit's move, compare and branch: clang's
`callbr` jumps from the template straight to the labels. Jumping straight would
make the body's call row a block's terminator with several successors, which
Tier 0's validator and `ir_lower` don't take yet. Under `--tier1` the statement
keeps its helper, as the IR's body call returns nothing. Tier 0 sorts its branch
columns where a body has branches: a body's are a deferred entry's, written after
the branches of the entries that follow it.

An asm goto's possible targets are its own labels, as clang's
JumpScopeChecker takes them (`check_input_asm_goto_refused`). The checker
refuses one that would enter the scope of a variable-length array or of a
cleanup variable it isn't in, or that would leave the scope of a cleanup
variable it is in, with clang's "cannot jump from this asm goto statement to
one of its possible targets". It may leave a variable-length array's scope,
as clang allows. Like clang's `callbr`, it then goes straight to the label,
without the stack restore that the scope's normal end makes.

## File-scope assembly

A file-scope `asm` statement (#619: `main.c`'s puts the checker's code in a
page-aligned `.text.buster_checker`) makes no helper and no code of a function.
The asm planning (`check_program_asm_plan_file`, in `c_check_program_asm.c`)
joins the unit's file-scope templates in source order, a line each, and
assembles them with `asm_assemble`, the `assemble` command's assembler, as
clang emits a module's `asm` before its functions. Its symbols join the
templates' names as an assembled template's do (#1848): its labels are local
unless a binding or visibility directive names them, its `.type` and `.size`
apply to them, a label or `.comm` of a name the unit declares without defining
defines that symbol, so that C calls and reads what the assembly defines
(`asm(".globl f\nf: ret")` with `void f(void);`), and its references are the
unit's symbols of those names (its functions and variables, static ones too)
or undefined globals. The emission publishes it with the templates' units
(`check_program_emit_asm_unit`): each section it makes is an atom of its class,
before the functions' atoms in the link, and in `-c`'s object a section of its
own name, type and flags (`CheckProgramCode.asm_sections`,
`check_program_sections`); the direct path's executable merges it into its
class's output section. Its sections are any the assembler makes (#1848):
custom flags (`.pushsection __ex_table,"a"`), merged strings, notes (`-c`'s
`SHT_NOTE`; the direct path's executable gives a `.note*` section an `SHT_NOTE`
output section of its name and a `PT_NOTE`, as lld does, sharing one output
section between names of one alignment), and the arrays of constructors and
destructors (`.section .init_array,"aw"`), whose entries the direct path's executable runs
as C's constructors'. Its CFI's frames are an `.eh_frame` atom of their own
beside the functions' unwind table, its CIE and FDEs as the assembler makes them
(clang's object shares one CIE between the module's assembly and its
functions), which the direct path's `.eh_frame_hdr` indexes with the
functions'. Its section groups (COMDAT,
`.section .text.f,"axG",@progbits,f,comdat`) are the link's groups beside
selectany's (`check_program_emit_asm_groups`, `check_program_sections`), and
`-c`'s object has clang's `.group` sections: their signatures (the program's
symbol of the name, which may be C's, a section's, or for a name no symbol has,
the local symbol in the group's section that clang's assembler makes), flags
and members, a member's relocation section among them. One unit's direct path
keeps every group's members, as a link of one object does; a link of several C
units keeps each signature's first group in the command's order and drops the
others' members, as a linker does (`check_units_select_groups`), and a mixed
link's objects go to the linker whole. That selection keys groups by their
signatures' global names, so in a link of several C units a group whose
signature isn't a global symbol defined in it (a local label, a section, a name
of its own, a symbol defined outside it) is refused by name. clang's
`-Winline-asm` warning about a `.globl` symbol made weak ("changed binding to
STB_WEAK") isn't modelled, as no assembler binding warning is. Refused by name,
at the first file-scope statement: templates the assembler refuses, and what the
templates' names refuse (a label another template defines too, or that the unit
defines, a reference to another template's `.set` symbol).

## What the lowering refuses

Each refusal names its feature, as the driver prints it ("unsupported: ..."):

- flag outputs of a size no integer store covers, and with another
  alternative (`"=r@ccz"`, which clang takes as `r`);
- the letters it doesn't place, and `A` of other sizes than up to 8 bytes and
  an integer's 16;
- MMX operands with a modifier (clang refuses each);
- opmask operands with a modifier, and
  without the moves' features (AVX-512F's for 1 or 2 bytes, AVX-512BW's for 4
  or 8), where clang can't place one;
- x87 operands and clobbers off the stack's top (an input at st(1) without one
  at st(0), a clobber below the outputs), which clang's code refuses too; and
  an integer in an x87 register. An x87 operand named in the template prints as
  its depth's register, `%st` or `%st(1)`, as GCC and clang print it;
- symbolic immediates, and constants outside their letter's range;
- operands of types no general or XMM register holds by value, outside memory,
  but a vector of 16, 32 or 64 bytes (as clang can't place another: "could not
  allocate"), or of any size in a template of no instructions that doesn't
  refer to it; and vectors of 32 bytes without AVX or 64 without AVX-512F, or
  in XMM16 up or YMM16 up of 16 or 32 bytes without AVX-512VL (the checker
  gives clang's Sema error for the sizes first, "invalid input size for
  constraint", #2231, so only a `v` operand's register class reaches this);
- bit-fields;
- explicit register variables in registers the helper doesn't place one in
  (APX's r16 to r31, st(2) to st(7)), in rsp in a statement
  that keeps a helper (a template that takes directives among them), in XMM16 to
  XMM31 without AVX-512F,
  as memory inputs (`"m"(r10)`, which clang's code refuses too), and of other
  types than a general register's or an XMM register's (an integer in an XMM
  register's variable);
- an operand fixed to another's register or to a clobber;
- APX's registers r16 to r31;
- statements of more than 64 operands;
- templates the assembler refuses, with its message, and a template's CFI
  directives, whose frame would be the helper's own unwind entry's;
- a label two templates define, a `.L` name no template defines, and a label
  (or a `.set`) with the name of a symbol the unit defines, which GNU as and
  clang reject;
- a label in a template of no instructions, which would be a place in its
  function's code;
- a template that returns before any branch (`ret`, `rep ret`, a `leave` and
  `ret`) and keeps its helper (a named label's, a directive's), which would leave
  the helper's frame reserved; one that is an asm body returns from its caller
  (#1839, above), and a template that jumps over its return, as a called
  snippet's label does, stays admitted. The assembler takes these instructions
  in `.s` files and file-scope assembly;
- in a naked function, statements other than `asm` statements, which clang
  rejects, and operands other than integer constants and static objects'
  symbols (#1845: `"m"(g)` prints `g(%rip)`, `"i"(&g)` prints `$g`, bare
  with `c`, `p` or `P`, and `g(%rip)` with `a`; a block-scope static's or a
  literal's, whose names the emission makes, stays refused). A naked function
  of no instructions is clang's function of no bytes, with an FDE of no
  range.

## Why it is scalar

The planning, the substitution and the helper's records are scalar, a statement
at a time. The assembler's lexer, parser and matcher run by the checker's path.
A statement placed in its caller assembles its template a second time, in the
row's registers (`check_asm_row`); one placed costs that assembly, and saves the
helper's whole function (its sizing, encoding, unwind entry and emission) and
its call's operand block.
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
  - statements placed in their callers (#1059): values live across them in
    depth registers and homes, in a loop and under an expression 18 values deep
    (the deep slots); tied operands (`+`, a matching input narrower than its
    output) and early clobbers (an output written before the inputs are read,
    two at once); narrow outputs (a byte from `%b`, a `bool` from `setz`); memory
    operands under locked forms, an array element whose index has a side effect,
    and a memory input; the flags changed inside a condition's chain and between
    a loop's compares; XMM operands with floating values live across; fences,
    pause and prefetches in a loop. The placement test plans each statement by
    both paths into the same rows, checks their records, roles and stores, and
    the statements that are asm bodies (#1839): fixed registers (cpuid's body
    record by record, and its touched rbx), a literal register, a syntax switch,
    more records than a row takes, an x87 operand, ldmxcsr (uncovered), an
    explicit register variable and `Q`'s class; and the branch that keeps its
    helper. Every native fixture below runs each statement's asm body where
    it has one, by both paths, against clang -O0's build;
  - barriers;
  - registers, matching inputs, early clobbers, constants and modifiers;
  - named operands;
  - `cpuid` with fixed registers, and clang's `<cpuid.h>` form with `{att|intel}`
    and `%q1`;
  - the x87's control and status words and MXCSR;
  - `syscall`;
  - branches to the template's own labels;
  - flag outputs named in their templates (#1846), by each modifier and
    written there before the condition is set;
  - flag outputs: each of clang's 28 conditions after compares of values
    around their edges, into objects of every size an integer store covers
    (`bool`, `char`, `short`, `int`, `long`, `__int128`, a `float`'s bits, a
    struct of 4 bytes), a `+` one, several beside register outputs and an x87
    one;
  - asm goto: labels by name and by number (after a `+` operand's input),
    jcc's and jmp's, several labels and one named twice, a label before the
    statement (a loop), a template that falls through, outputs whose values
    reach every label, a flag output beside them, and statements in a loop of
    live values and in a function with a variable-length array;
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
    evaluated and dropped, and a comment without operands;
  - vectors of 16, 32 and 64 bytes in templates of instructions (#1846): inputs,
    `+` operands, outputs, matching inputs (in a template of no instructions
    too), a volatile output, `%x0` of a YMM operand, and explicit register
    variables in xmm30, ymm7 and zmm21; and 16-byte ones under SSE alone. Their
    refusals: 32 bytes without AVX, 64 without AVX-512F, a variable of another
    size than its register's, and a template that takes directives;
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
  - explicit register variables in high byte registers (#1846): a `+` one, an
    input by its modifiers, an output, a matching input, and a `short` in
    "ah" as ax; and in XMM16 to XMM31, in functions whose target attributes
    have AVX-512F, run where the host has it;
  - explicit register variables: wrappers of the test's own in the shape of
    musl's syscall wrappers (r10, r8 and r9 beside the fixed registers) on
    harmless system calls, and each shape of a
    variable's operand: a constraint's letter replaced, a cast that keeps the
    width, a 32-bit name, outputs of a register's and a memory constraint, a
    `+` operand in rbx, a matching input in its variable's register, an XMM
    register's, and a template of no instructions;
  - A's operands (#1846): mulq's 128-bit product into rdx:rax beside an `"a"`
    input that shares rax, a `+` pair, a pair's input matching its output, and
    values of 2 to 8 bytes in rax, printed by their 64-bit names (an
    `unsigned` zero-extended there, a `double`'s bits); and the refusal of a
    pair in a template that takes directives;
  - MMX's operands (#1846): `y`'s of a `long`, an `unsigned`, a `short`, a
    byte, a `double`'s bits, an early-clobbered output, a
    matching input, MMX clobbers that the operands avoid, an MMX input into a
    general output, and a template of no instructions; their refusals: a
    modifier, x87 operands beside them, and a template that takes directives;
  - opmask operands (#1846): `k`'s and `Yk`'s of a `short`, a byte, an
    `unsigned` and an `unsigned long long` in functions whose target
    attributes have AVX-512F or AVX-512BW, a `k` clobber the operands avoid, a
    matching input, and an opmask input into a general output, run where the
    host has AVX-512BW; their refusals: no AVX-512F, 4 bytes without
    AVX-512BW, and a modifier;
  - `f`'s inputs, below the fixed operands, each in a register of its own,
    beside a `+t` operand, early-clobbered outputs the template pushes, and a
    template of no instructions;
  - the x87's registers named in templates: `%0` of `t` as `%st` and of `u`
    as `%st(1)`, `fxch`, `fadd %%st(1), %%st`, `fld %%st(0)` and `fstp
    %%st(1)`, AT&T's reversed `fsubp %%st, %%st(1)` popping a clobbered st(1),
    and `fcomi` into a flag output;
  - two x87 operands at one depth (#1846): inputs where a `+` or a matching
    input's value wins, in either order, and where the last of two plain ones
    does, at st(0) and st(1); and two outputs of one depth, a template's push
    among them;
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
    `.nops` control, and a naked function's `.p2align` and `.byte`; numeric
    labels and `.` as data, their distances as clang's, and a `loop` to a
    numeric label; and the refusals of a `.set` used by another template, a
    template that returns before any branch, and CFI.

  Each refusal above is checked by name, but the label with a unit symbol's
  name, which the emission refuses: `test-checker-driver` checks that one
  through the driver, beside an executable the direct path links, from both
  paths into the same bytes. That executable's naked function has a label
  another template takes the address of, and its template calls libc's `puts`
  by name. The driver's `-c` object of a template's directives is compared with
  clang's too: its `.L` and numeric labels are the assembler's temporary
  symbols, its undefined `.globl` declaration and `.set` absolute are left out,
  and its `.hidden` label has its visibility as clang's object gives it.
  Every local `.L` label in code is a temporary of `-c`'s object too
  (#1845, `check_driver_code_temporary`): the labels of templates assembled
  by their own statements and of naked functions and `&&label`'s
  `.Lbuster_label_N` (clang's `.Ltmp`), whose relocations then name their
  section, as clang's assembler leaves its `.L` labels out. The helpers' own
  function symbols (`.Lbuster_asm_N`, `.Lbuster_builtin_*`), which clang's
  code has none of, stay: `test-x86-intrinsics` finds their calls by them; `test-checker-driver
  --asm-definitions-only` checks that on both paths and runs the object.
- `test-checker-driver` reads a placed statement's instructions from its
  function's own records in `-c`'s object, with no call, by both paths and with
  `--tier1` (its asm bodies, #1839, the function not refused);
  and runs an executable whose callee between two placed statements counts its
  stack by glibc's `backtrace` through the function's unwind entry, one frame
  more than main's, by both paths and with `--tier1`. Its asm bodies' fixture
  (#1839, `c_check_tier1_driver_test.c`) compiles by `--tier1 --verify`, every
  function Tier 1's, by both paths with and without rematerialization: `cpuid` in
  a loop of eight live values and in always_inline wrappers beside `rdtsc`'s,
  musl's syscall wrappers' shapes (r10, r8 and r9 explicit register variables),
  the x87's `fsqrt` and fprem's loop, flag outputs, a template's own loop, a
  bswap and an lfence, callee-saved clobbers in a loop of values live across a
  call, xmm clobbers beside live floating values and rsp's alignment, against
  clang -O0's build; and reads `tier1_cpuid`'s `cpuid`s in its own records, no
  call, and rbx pushed by its prologue. Its returning templates
  (#1839, `c_check_frame_pointer_driver_test.c`) build `leave; ret` on a branch
  of the function and on the template's own branch with
  `-fno-omit-frame-pointer`, by both paths under `--verify`, and link them with
  clang's harness. The output must be clang's build's, the caller's rbx kept
  through a template's return.
- `test-tier0` checks which general-purpose forms a row takes and refuses.
- `test-assembler` checks `asm_assemble_code` by both paths: the records, the
  branch columns, the references, each refusal, and the arenas after a failure.
  `--cross-check` compares the newly admitted instructions with GNU as.

## Pending

What placement in the caller doesn't take yet (#1839), each keeping its helper:
templates of references to names, asm goto under `--tier1`, `loop` and `jrcxz` across a sized branch (assembled whole, as templates that take directives are),
templates that return but keep a helper for another reason (refused by name), templates
that take directives, and forms of unknown effects outside `check_asm_implicit`.
Under `--tier1`, templates of named labels keep their helpers (a body's labels
would make its function Tier 0's), and a function with a template that returns or a
label of a template of no instructions is Tier 0's. The
code's certificate (`--verify`) certifies a function with asm bodies
([the certificate guide](tier1-certificate.md)'s "Asm bodies").

What the lowering still refuses of what GCC and clang take:

- a template that returns and has a named label, or keeps its helper for
  another of the reasons above (a reference, a directive), which the helper
  design refuses by name;
- naked functions' operands other than integer constants and static objects'
  symbols (a register's, a block-scope static's);
- the matcher's gaps: the
  instructions outside its families (an x87 memory operand without a width,
  `fstp (%rax)`, is refused as clang refuses it);
- explicit register variables in rsp in statements that keep a helper (a
  template that takes directives), and in st(2) up.

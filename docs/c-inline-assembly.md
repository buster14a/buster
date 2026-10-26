# Inline assembly

The C driver's lowering compiles GNU C's `asm` statements (#147's "Inline
assembly" item). `c_check_program_asm.c` plans each statement, `assembler.c`'s
`asm_assemble_code` assembles its template into records, and
`c_check_program_lower.c`'s `check_lower_asm` lowers the statement's operands.
Tier 0 doesn't change: it sees an ordinary call.

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
with operands substituted into them. Their operands are evaluated in the same
order as a helper's. A matching input's value is stored into its output, and a
`+` output's value is loaded and stored back, as clang's code does. An output
that nothing writes keeps its value; GCC and clang leave it unspecified.

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

## Placement

`check_program_asm_plan` places each operand by its constraint's first
alternative (`check_asm_constraint_parse`):

| Constraint | Place |
| --- | --- |
| `r`, `q`, `l`, `R`, `Q` | a general register of the class (`Q`: rax, rbx, rcx, rdx; `R`: the first eight) |
| `a b c d S D` | that register |
| `x`, `v` | an XMM register, for a `float` or a `double` |
| `m`, `o`, `V`, `<`, `>` | memory at an lvalue's address, which a general register holds |
| `i`, `n`, `I J K M N O`, `e`, `Z` | an integer constant, within its letter's range |
| a digit, `[name]` | the output's register (a matching input) |
| `g`, `X` | a constant where one is allowed, else a register, else memory |

Every operand gets a register of its own, but matching pairs. No operand takes a
clobbered register. Caller-saved registers come first, so most templates cost
their helper no saves. An input fixed to an output's fixed register, as in
`"=a"(r) : "a"(n)`, shares it, as GCC and clang let it, unless the output is
early-clobbered or `+`. It is that output's matching input then.

The operand block, a frame object of the caller's, holds 8 bytes per slot:

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
| `%b`, `%w`, `%k`, `%q` | the register's 8-, 16-, 32- or 64-bit name |
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
`"i"(0xffffffffu)` is `$-1`. A basic `asm` statement's template is printed as it
is, without substitution.

## Assembly

`asm_assemble_code` takes the text through the assembler's lexer, parser and
matcher. It runs the vector path or the scalar twin, as the checker's path does.
It returns the records, the local branches as branch sizing's columns, and its
symbolic fields as references by name. It accepts instructions, labels and the
syntax directives. It refuses:

- every other directive: a template has no sections, data or symbols of its own;
- its own labels, but as a `jmp`'s or `jcc`'s target without an addend;
- undefined local labels;
- what the matcher doesn't take.

The matcher's index admits every form the assembler's syntax takes, whatever
the unit's target. GNU as and clang's integrated assembler take any instruction
in a template.

The matcher admits `cpuid`, `xgetbv`, `xchg`, `fnstcw`, `fldcw`, `fnstsw`,
`fnclex`, `stmxcsr`, `ldmxcsr` and SSE2's `pcmpeqd`, which `main.c`'s statements
use, and the `bswap` of `<linux/swab.h>`'s helpers, besides its families in
[the assembler's guide](assembler.md).
`test-assembler --cross-check` compares them with GNU as. An `xchg` takes its
memory operand first in Intel order, as the catalogue has it: `xchgl %ecx,(%rdi)`,
not `xchgl (%rdi),%ecx`. The matcher's operand grammar has no accumulator role:
`fnstsw` takes memory, not `%ax`, and an `xchg` of the accumulator with another
register takes the two-byte form, where GNU as takes the one-byte one.

## The helper's frame

`check_asm_helper_code` makes:

1. pushes of the callee-saved registers the statement touches: its operands',
   its clobbers' and the stores';
2. a reservation of 8 or 16 bytes, which aligns rsp to 16 and keeps the block's
   address;
3. the loads of the operands from the block: a narrower integer zero-extended,
   and the load into rdi last;
4. the template's records;
5. the register outputs' stores through their addresses, from two registers no
   output holds;
6. the epilogue.

`unwind_tier0_frame` describes it (`Tier0Code.frame_saved` and `frame_size`).

## What the lowering refuses

Each refusal names its feature, as the driver prints it ("unsupported: ..."):

- `asm goto`;
- flag outputs (`=@cc...`);
- the x87's, MMX's, the opmasks' and the rax and rdx pair's constraints (`f t u`,
  `y`, `k`, `A`), and the other letters it doesn't place;
- symbolic immediates, and constants outside their letter's range;
- operands of types no general or XMM register holds by value, outside memory;
- bit-fields, and explicit register variables (`register long r asm("r10")`);
- an operand fixed to another's register or to a clobber;
- APX's registers r16 to r31;
- statements of more than 64 operands;
- templates that refer to symbols, and templates the assembler refuses, with its
  message;
- naked functions.

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
  - XMM operands;
  - the operands' order of evaluation, each once, through functions that
    record their calls;
  - rdi and rsi as operands, constants sign-extended as clang prints them, a
    memory input after a register output, and an XMM matching input.

  Each refusal above is checked by name.
- `test-assembler` checks `asm_assemble_code` by both paths: the records, the
  branch columns, the references, each refusal, and the arenas after a failure.
  `--cross-check` compares the newly admitted instructions with GNU as.

## Pending

Naked functions, whose basic `asm` is the whole function, and templates that
refer to the unit's symbols by name or to labels another statement defines, are
the second part of [#147's claim](https://github.com/buster14a/buster/issues/147#issuecomment-6427604943).

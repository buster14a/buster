# checker-asm

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/c_check_program_asm.c` lowers GNU C's `asm` statements
  (#147). It places each operand by its constraint and substitutes the template
  as GCC and clang print it in AT&T syntax. `assembler.c`'s `asm_assemble_code`
  assembles it, by the checker's path, into records, branch columns,
  references and labels. The statement's code calls the result, a per-unit
  helper; a template of no instructions, a barrier's, is no code at all, but
  one with x87 operands, whose helper passes them through the x87's stack as
  clang's code does. A template that takes a directive besides the syntax
  switches is instead one source assembled whole by `asm_assemble` (the
  helper's frame and operand loads around it, or a naked function's templates),
  whose atoms, symbols and fixups the emission merges; a template of alignment
  or NOP fill alone publishes its atoms and calls nothing. `.set` absolute
  symbols are the template's; `.globl` or a visibility directive upgrades a
  label another template defines, `.type`/`.size` apply to its own template's
  label, a binding or visibility directive applies to the unit's C symbol of
  that name (`.weak` a function's, an extern's or a static variable's; an
  external definition's is refused, and a zero-filled static is accepted where
  clang refuses it), a `.size` once its definition's atom exists, and a
  reference from another template is refused by name. A naked function's
  templates are its whole code. Templates' labels are local unless a binding
  or visibility directive names them, and the other names they refer to are
  the unit's symbols of those link names or undefined globals
  (`check_program_emit_asm_symbols`). A file-scope `asm`'s templates are
  assembled by the emission into atoms of their sections before the functions'
  (`check_program_emit_file_asm`, #619), each in its section in `-c`'s object.
  `test-checker` runs each shape natively against clang -O0's build on both
  paths and checks each refusal by name (`c_check_program_asm_test.c`);
  its naked mask-save fixture uses all eight `kmovq` registers at the real
  semantics harness's offsets, executing where AVX-512BW is available and
  compiling/linking everywhere. `test-assembler --cross-check` compares all
  five operand forms of `kmovb/w/d/q` in both syntaxes with GNU as; eligible
  targets also check malformed register and memory widths.
  `test-checker --program-asm` runs the inline-assembly source fixtures alone;
  the full `test-checker` command continues to include them.
  `test-checker-driver` links and runs naked functions' labels and a template's
  call of `puts` by the direct path, and compares a template's `-c` symbols
  with clang's. `test-assembler` checks
  `asm_assemble_code`, and with `--cross-check` compares the instructions it
  admits for templates with GNU as. [The inline assembly
  guide](../../docs/c-inline-assembly.md) has the placement, the substitution, the
  helper's frame, naked functions, names, file-scope assembly, the refusals and
  what is pending.

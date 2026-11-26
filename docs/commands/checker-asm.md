# checker-asm

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/c_check_program_asm.c` lowers GNU C's `asm` statements
  (#147). It places each operand by its constraint and substitutes the template
  as GCC and clang print it in AT&T syntax. `assembler.c`'s `asm_assemble_code`
  assembles it, by the checker's path, into records, branch columns,
  references and labels. A statement of register, memory and constant operands
  whose records name no register but its operands' is placed in its caller, a
  Tier 0 `X86` row by role (#1059, `check_asm_row`); other eligible statements
  are asm bodies at both tiers, including named labels, register results and
  asm goto (#1839, #2351). A barrier's empty template emits no code, while
  operands that need loads or stores, including x87's, keep those moves in a
  body. A template with a directive besides the syntax switches is assembled
  whole by `asm_assemble`. `check_asm_unit_place` embeds its bytes in the caller
  where the matched instructions pass the effects checks; calls, returns,
  stack mutations and unsupported forms keep the helper path. Raw data bytes
  rely on the GNU operands and clobbers. The emission applies alignment at
  the actual statement, compacts the maximum sizing reservation before debug
  and unwind data, rebases entry-atom symbols and fixups into the function,
  and publishes other atoms in their sections. This also handles trailing or
  directive-only alignment, local `loop`/`jrcxz` across sized branches, and
  32-bit addresses. A hidden anchor prevents duplication of a byte unit;
  functions containing opaque bytes skip Tier 1 post scheduling. Units with
  section groups or their own CFI procedures (including `.debug_frame`-only
  ones) retain the existing helper or no-code path, as do directive-only held
  operands before normalization. Inline frame directives that require the
  ordinary caller's frame remain refused because those rows are not merged.
  Naked functions still assemble their templates as their whole code.
  `.set` absolute symbols are the template's; `.globl` or a visibility directive upgrades a
  label another template defines, `.type`/`.size` apply to its own template's
  label, a binding or visibility directive applies to the unit's C symbol of
  that name (`.weak` a function's, an extern's or a static variable's; an
  external definition's is refused, and a zero-filled static is accepted where
  clang refuses it), a `.size` once its definition's atom exists, and a
  reference from another template is refused by name. A naked function's
  templates are its whole code. Templates' labels are local unless a binding
  or visibility directive names them, and the other names they refer to are
  the unit's symbols of those link names or undefined globals
  (`check_program_emit_asm_symbols`). A label or `.comm` (a
  `BUSTER_LINK_SYMBOL_COMMON` symbol) of a name the unit declares without
  defining defines that symbol (#1848); a `.comm` of an external variable the
  unit defines makes it clang's common symbol of C's size at the `.comm`'s
  alignment, C's bytes left without a symbol, and a file-scope label of an
  external definition is clang's location-less "symbol 'x' is already defined"
  error (other shapes are refused by name: `docs/c-inline-assembly.md`);
  `test-checker-driver --asm-definitions-only` compares such fixtures' `-c`
  symbols with clang's, runs them and compares the errors' whole output. A file-scope `asm`'s templates are
  assembled together by the planning (`check_program_asm_plan_file`, #619),
  their names joining the templates' (#1848), and published as atoms of their
  sections before the functions', each in its section in `-c`'s object, and
  its COMDAT groups as the link's groups (`check_program_emit_asm_groups`),
  which `--asm-definitions-only` compares with clang's `.group` sections and
  links across two units both ways.
  `test-checker` runs each shape natively against clang -O0's build on both
  paths and checks each refusal by name (`c_check_program_asm_test.c`);
  its naked mask-save fixture uses all eight `kmovq` registers at the real
  semantics harness's offsets, executing where AVX-512BW is available and
  compiling/linking everywhere. `test-assembler --cross-check` compares all
  five operand forms of `kmovb/w/d/q` in both syntaxes with GNU as; eligible
  targets also check malformed register and memory widths.
  The lowering reserves its existing non-asm operation allowance plus one
  row for each placed statement in the largest selected body; lanes reuse that
  room between functions. `test-checker-driver`'s capacity section compiles and
  executes distinct/repeated-template bodies across the former 4096-row
  boundary and compares both kernels with Clang and independent unsigned
  models (#1359).
  `test-checker --program-asm` runs the inline-assembly source fixtures alone;
  the full `test-checker` command continues to include them. The
  `checker/programs/asm-directive-placement` fixture requires helper-free
  planning at both tiers and checks bytes, alignment, sections and relocations
  by native execution; its dead blocks exercise missing-anchor handling, and
  the planner capacity sweep covers whole-unit allocation failure. A `target_clones`
  body's copies, a naked one's among them, each plan their statements for
  their version's instruction set (#2247); `test-checker-driver
  --target-versions-only` runs such bodies at both tiers against clang's build
  and compares their operands' diagnostics under several command lines.
  `test-checker-driver` links and runs naked functions' labels and a template's
  call of `puts` by the direct path, and compares a template's `-c` symbols
  with clang's; it reads a placed statement's instructions from its function's
  records at both tiers, and backtraces through a function of placed statements.
  `driver/tier1_asm_control.c` requires Tier 1 with verification for goto
  outputs, live values, register results, named labels and label-only bodies,
  with rematerialization off, post scheduling and unrolling as well. It checks
  execution against clang, no helper or leaf call, exact label addresses and
  uniqueness. Record bodies keep emitted-code certification; opaque byte
  spans report `opaque-asm`, while allocation certification still runs
  ([the certificate guide](../tier1-certificate.md)). The driver also compares
  (`c_check_register_variable_driver_test.c`, #1846) global register
  variables' and register labels' errors with clang's, whole output on both
  paths and on deferred bodies' lanes, and runs a program of rsp, rbp, esp and
  ebp variables by both tiers against clang -O0's build. `test-assembler` checks
  `asm_assemble_code`, and with `--cross-check` compares the instructions it
  admits for templates with GNU as. [The inline assembly
  guide](../../docs/c-inline-assembly.md) has the placement, the substitution, the
  helper's frame, naked functions, names, file-scope assembly, the refusals and
  what is pending.


The C driver's `-S` mode also preserves constant offset markers after
substitution. A raw `->` marker uses a zero-width asm body anchored at its
emitted record. A standalone `.ascii` marker retains the byte range that the
assembler gives its directive in the assembled unit. The [driver guide](driver.md#-s-1362)
describes the supported operands and output.

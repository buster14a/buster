# x86-target

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/x86_target.c` defines generated-code ISA profiles, with
  x86-64 baseline as the default, independently of the compiler host.
  `x86_target_forms.c` supplies explicit, fail-closed form eligibility and
  scalar/vector table builders. `test-x86-target` checks the policy and Clang
  predefines; `benchmark-x86-target` measures table construction.
  `docs/x86-target.md` records the API, current catalogue coverage and the
  selector/corpus acceptance still pending for #180. Its "A function's target
  attribute" has #772's per-function features and code generation's check of
  them, and of x86 builtins' required features (#148), which `-c` and the
  direct path make after selection as clang does (`c_check_target_feature.c`,
  #775; `test-checker-driver` against clang `-c`).

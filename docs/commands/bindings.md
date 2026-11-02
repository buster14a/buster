# bindings

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Scoped binding kernels

`test-environments [--seed N]` checks the immutable file-scope position index,
tag-completion positions and parser-role body ranges for #113, including
guarded tails and queries on 1, 2, 3 and 8 lanes. `benchmark-environments`
measures the synthetic index/query handoff and completeness checks; use
`--only full-vector [--after-scalar 100]` for its vector states, under the
9700X's per-run lock and pinned to CPU 2. `check_input` publishes the real
source reader's retained events, declaration types, tag completion positions
and body slots through `c_check_environment.c` (the C driver's
`check_input_compile` doesn't, as nothing in a compile reads them: #572);
`test-checker` checks their
scratch lifetime and scalar/vector replay. `c_check_deferred.c` is their
consumer: the deferred reader below ([Deferred function bodies](deferred.md#deferred-function-bodies)).
[docs/c-environments.md](../../docs/c-environments.md) records the API and boundaries.
`c_check_body_bindings.c` supplies the next independent name-resolution
consumer: private per-body views/replay, source-position file fallbacks and
stable declaration identities. `test-checker` compares both kernels on
reversed one/eight-lane body schedules and guarded columns.
[docs/c-body-bindings.md](../../docs/c-body-bindings.md) records its contract.

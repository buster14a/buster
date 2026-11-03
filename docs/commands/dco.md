# DCO sign-off tooling

`tools/dco/dco.c` sits beside the Git hooks in `.githooks/`. It needs no
compiler build:

```sh
cc -O2 -o build/dco tools/dco/dco.c
build/dco status
build/dco audit --base origin/main --head HEAD
build/dco audit --api-pull pull.json --api-commits commits.json
build/dco compose
build/dco uncompose
sh .githooks/test-sign-off.sh
```

- `status` reports this worktree's effective opt-in, its source and scope, and
  the `commit-msg` Git will run. It reads `.githooks/sign-off-lib.sh`'s
  report, the same resolver the hook uses, and changes nothing.
- `audit` reports the sign-off trailers of the commits the head has and the
  base doesn't, from a full clone or from GitHub's pull request API pages. It
  exits 0 when every commit has a sign-off record, 1 with gaps, 2 when the
  input is incomplete. The `DCO sign-off audit` job of the 9700X workflow runs
  it on each pull request, informationally.
- `compose` and `uncompose` chain the sign-off hook with hooks a checkout
  already has, without replacing them.

[CONTRIBUTING.md](../../CONTRIBUTING.md) owns the rules: "Automatic mechanics
and revocation" (the opt-in and its worktree scope), "Existing hooks"
(composition) and "Auditing sign-offs" (the report and its limits).
`sh .githooks/test-sign-off.sh` runs every fixture in disposable repositories,
in the required `DCO hook regression` job. It takes a few seconds, so it needs
no `cores` grant; on the 9700X, run it under the host lock like anything else.

# Issue 1341: bounded generated-text handoff prototype

This branch is an experiment carrier, not a production change or merge request. Research state and the full fact-flow ledger belong in https://github.com/buster14a/buster/issues/1341. No merge is authorized.

The source base is ade6ac4b6ecb21f30b61b656439bac476c145e2f, tree 4c5306221fdb22fccc929b55e333163742de17d0. The six-file candidate tree is 809408ab548e5ad206669cf9431fdf66ca86aca2. Its full patch SHA-256 is d63ddf659d55230a04374300b98392151b3a54f2579480cecbf5ec7c420027d0.

`1341-apply.py.gz` is a compressed UTF-8 source-edit script, not a binary compiler. Inspect with `gzip -dc .github/research/1341-apply.py.gz`. Its decompressed SHA-256 is 42acadfa6a275600cbac6c34394e81cff20420fd3d1d0bf5632236b7bdc184d3. The workflow applies it only to an isolated exact-base worktree, verifies the complete candidate tree and patch hashes, and retains the readable script, patch, identities and logs as artifacts even on failure. Build/test policy stays in existing build.c commands.

Only docs/agents/driver.md, the object implementation/private header, native C driver call and registered object/driver tests change in the source candidate. The large whitespace-only portion of object.c converges the existing shared builder onto one return; descriptor, metadata, relocation and diagnostic checks remain in order. No generated binding, permanent IR, production dependency, concurrency contract, service, deployment, admission or retirement policy changes.

Tests distinguish public text snapshots from private immutable aliases, repeat all three object format writers and assembly printing on x86-64/AArch64, compare linker copy/alias policies, tear down the producer only after borrowed consumers finish, preserve malformed-module failures and empty text, and re-serialize returned driver objects after TU arena recycling. The hosted Release and sanitized Debug jobs are correctness evidence only; their outcome must be read before any pass claim.

Performance, peak RSS and time-to-artifact are NOT MEASURED. No desktop benchmark, unleased 9700X run or ad-hoc SSH is part of this experiment. One integrator; no subagent executor is available in this session.

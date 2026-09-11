# buster audit — 2026-09-07

26 issue drafts from a full-tree audit of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b), **rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8)**. **Nothing here has been published to GitHub.** No patches in this bundle — these are reports, not fixes.

> **Rebase, 2026-09-07.** Main moved 10 commits after the audit, touching four cited files, and `publish.py --check` correctly refused to file 11 issues on stale line numbers. The tree was rebuilt at `44a936a8` and all 14 runnable reproducers were re-run: **every one still reproduces — nothing was fixed upstream.** Line references throughout the bundle were then re-derived for `44a936a8` and each cited site re-read to confirm it still says what the issue claims. The 12 findings that came from subsystem passes were re-located but not re-run; each says so in its own preamble.

Start with [AUDIT_REPORT.md](AUDIT_REPORT.md) for the ranked narrative; `manifest.json` maps every issue key to its file, priority, marker, content hash, and the git blob SHA of every production file the issue cites. [`publish.py`](publish.py) files them as GitHub issues when you decide to.

## Method and limits

Full clone, built with clang 18.1.3 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror … build.c`) because TCC is unavailable on the audit host. `ide test` was green at **322668/322668 unit and 39/39 module tests** before auditing began, so nothing in this bundle is something the existing suite catches.

**Release self-host was not run** — it needs TCC for the trusted bootstrap. Windows, macOS and mobile targets were not exercised; the Windows-target findings (`codeview-*`) were produced by cross-compiling with `--target=x86_64-pc-windows-msvc` and validating the emitted containers, not by running them.

17 of the 26 findings were reproduced first-hand against `build/Debug/ide` and `build/Release/ide`, differentially against clang 18. The other 9 came from subsystem passes and are traced in source but were not independently re-executed for this bundle — each says so in its **Evidence** line and opens its validation section with "first confirm the reproduction". Treat that distinction as load-bearing.

Areas covered by the [2026-09-06](../2026-09-06/) bundle — the eBPF emitter, Wasm alignment, runtime file/string boundaries — were excluded from this audit.

## Issues

### S1 — silent miscompilation of valid C (7)

| Key | Priority | Summary |
|---|---|---|
| [c-enum-underlying-type](issues/c-enum-underlying-type.md) | P1 | enum with no fixed underlying type is always lowered as `int` |
| [x64-i128-signed-divide](issues/x64-i128-signed-divide.md) | P1 | signed `__int128` divide negates the divisor by the wrong sign mask |
| [c-float-to-integer-constant-fold](issues/c-float-to-integer-constant-fold.md) | P1 | negative floating constants fold to −1 when converted to an integer |
| [c-bool-aggregate-initializer](issues/c-bool-aggregate-initializer.md) | P1 | `_Bool` members keep only bit 0; Debug aborts on valid C |
| [c-unsigned-to-float-constant-fold](issues/c-unsigned-to-float-constant-fold.md) | P1 | unsigned constants ≥ 2^63 fold to a negative float |
| [c-nested-initializer-string](issues/c-nested-initializer-string.md) | P1 | `char[]` initialized by a string inside a nested initializer stores one byte |
| [c-i128-aggregate-initializer](issues/c-i128-aggregate-initializer.md) | P1 | 128-bit members of aggregate static initializers drop their high half |

### S2 — the assembler emits wrong machine code (6)

| Key | Priority | Summary |
|---|---|---|
| [x86-apx-evex-prefix-fields](issues/x86-apx-evex-prefix-fields.md) | P1 | APX EVEX hardcodes `pp` and `vvvv`; `adcx`/`adox` collide and decode invalid |
| [x86-evex-broadcast-disp8-scale](issues/x86-evex-broadcast-disp8-scale.md) | P1 | EVEX broadcast operands address the wrong memory |
| [x86-rex2-memory-width-w](issues/x86-rex2-memory-width-w.md) | P1 | REX2 forms drop `W`; `addq` emits `addl` |
| [x86-att-s-suffix-alias](issues/x86-att-s-suffix-alias.md) | P1 | AT&T `s` suffix is generic, so `bts` assembles as `bt` |
| [x64-i128-float-cast-unsupported](issues/x64-i128-float-cast-unsupported.md) | P2 | x86-64 rejects `__int128` ↔ float casts AArch64 supports |
| [x64-atomic-16-byte-aggregate](issues/x64-atomic-16-byte-aggregate.md) | P2 | 16-byte `_Atomic` structs rejected while `__int128` is accepted |

### S3 — memory safety and robustness (8)

| Key | Priority | Summary |
|---|---|---|
| [release-check-macro-is-unreachable](issues/release-check-macro-is-unreachable.md) | P2 | `BUSTER_CHECK` is `__builtin_unreachable()` in Release — shared root of four P1s |
| [c-parse-initializer-infinite-loop](issues/c-parse-initializer-infinite-loop.md) | P1 | 18 bytes of malformed source hang the compiler forever |
| [c-parse-identifier-use-overflow](issues/c-parse-identifier-use-overflow.md) | P1 | repeated typedef-name statements write past the identifier-use array |
| [c-import-once-path-unchecked](issues/c-import-once-path-unchecked.md) | P1 | `#import` writes into `once_paths` with no capacity check |
| [os-write-error-counted-as-success](issues/os-write-error-counted-as-success.md) | P1 | a failed `write(2)` counts as `UINT64_MAX` bytes written |
| [arena-commit-failure-and-granularity](issues/arena-commit-failure-and-granularity.md) | P1 | commit failure returns a pointer into uncommitted memory |
| [file-read-unsized-source](issues/file-read-unsized-source.md) | P1 | a source with no reported size compiles as an empty TU, exit 0 |
| [os-read-error-indistinguishable-from-eof](issues/os-read-error-indistinguishable-from-eof.md) | P2 | a read error is reported as zero bytes, i.e. as EOF |

### S4 — debug information and driver output (5)

| Key | Priority | Summary |
|---|---|---|
| [dwarf-ranges-double-count-base](issues/dwarf-ranges-double-count-base.md) | P1 | range lists double-count the text base; no function findable by address |
| [codeview-fieldlist-length-overflow](issues/codeview-fieldlist-length-overflow.md) | P1 | `LF_FIELDLIST` length overflows `u16` at ~2500 members |
| [codeview-gproc32-pend-offsets](issues/codeview-gproc32-pend-offsets.md) | P1 | `S_GPROC32` `pEnd` computed against the wrong base after the PDB split |
| [codeview-scope-stack-quadratic](issues/codeview-scope-stack-quadratic.md) | P2 | scope stack reallocated per function; quadratic arena growth |
| [driver-default-object-path](issues/driver-default-object-path.md) | P1 | `-c` without `-o` writes the object next to the source |

## Suggested order

1. **`release-check-macro-is-unreachable` first**, or at least decide its policy first — four P1s below are instances of it, and fixing them individually without settling the macro question invites a fifth.
2. **S1 as one batch.** Five of the seven are in `c_gen.c` constant/initializer handling and share test scaffolding. `x64-i128-signed-divide` is a two-row reorder with a correct AArch64 reference to copy.
3. **S2 next** — `x86-apx-evex-prefix-fields` is the worst of them (invalid instructions, silently) and is on the Zen 5 target line.
4. **S3 memory safety**, then **S4**, which affects debuggability rather than generated code.

## Publishing to GitHub

`publish.py` reaches GitHub one of two ways, chosen automatically: an authenticated **`gh` CLI** when one is on PATH — preferred, because the issues are then authored by you — or the **REST API** with `GH_TOKEN`/`GITHUB_TOKEN` from the environment, which is what an agent sandbox provides. No token is accepted on the command line or stored. The default run touches the network not at all.

If the REST path reports 403 on the repository, the session running it does not have that repository attached. Repository access is fixed when a session starts, so authorizing access to a *running* session does not help — start a fresh one with the repository attached, or run this from a machine with `gh`.

```sh
python3 publish.py                    # offline plan, no network
python3 publish.py --check            # online preflight: drift + duplicates, still no writes
python3 publish.py --publish          # file the issues
python3 publish.py --link-references  # second pass: rewrite key references as #numbers
```

Three things it refuses to do, each because getting it wrong is expensive:

**File a stale line number.** Every issue records the git blob SHA of each production file it cites. Before any write, those are compared against `--ref` (default `main`). If a cited file has changed since `d0ecad0d`, the run stops and names the issues involved — the defect may well still be there, but the line numbers are no longer trustworthy. `--allow-drift` files anyway and appends an explicit drift notice to the body; `--skip KEY` leaves those out instead.

**File a duplicate.** Each body carries a `<!-- buster-audit-2026-09-07:key -->` marker. Existing issues are matched on that marker or on exact title, across all states; a closed match stops the run for review. This is not semantic duplicate detection — read the open issues yourself before a first publication.

**Publish a body you did not review.** Bodies are checked against the `sha256` in the manifest and the run aborts if one was edited without the manifest being updated deliberately.

GitHub writes cannot be atomic across 26 issues, so each creation is recorded in `publication-results.json` the moment it succeeds. A run interrupted halfway is resumed by re-running the same command: filed issues are skipped, not re-filed. `--only KEY` files a single issue.

Both transports are tested offline against stubs covering the whole flow — plan, preflight, drift refusal, `--allow-drift`, closed-match abort, duplicate reuse, mid-run failure and resume, cross-reference linking, and the 403 case. **Neither has been exercised against live GitHub.** Do a `--check` first, then `--publish --only <one key>`, and look at the result before filing the rest.

## Two structural notes

Both are argued in `AUDIT_REPORT.md` and worth reading before picking up any individual issue.

**Debug and Release disagree about what is a bug.** Two findings abort the Debug compiler on valid C while Release silently miscompiles it; three more are contained in Debug and memory-unsafe in Release. The `#if !BUSTER_OPTIMIZE` differential gates are doing real work — they caught both miscompiles — but they gate *aborts*, not *corrections*, so the shipped configuration has no defence.

**Self-hosting cannot catch any S1 defect.** `test_self_host` requires stage 1 and stage 2 to be byte-identical, and a compiler that miscompiles enums identically in both generations still reaches a fixed point. Byte-identity proves determinism, not correctness. Every S1 finding passes self-host by construction; a differential gate against a reference compiler is what would have caught them, and CI has none.

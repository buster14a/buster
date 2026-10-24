# Issue 109 qualification, October 24, 2026

#109's done criteria: ten million generated cases in each of C17 and C23 pass,
over several seeds, on the 7940HS and on the 9700X; every difference found on
the way is fixed with its minimized case as a permanent fixture; and the
default `test-preprocessor` runs a short Clang-free fuzz batch. This
directory holds the receipts of the campaigns that met the first criterion.

## The freeze

Both campaigns ran one frozen source, PR #369's commit `35638fe` (tree
`9cfcc7dac5718749e7e3dfa91f19e45240eb7f1d`, merged with main `b28cb89`),
extracted with `git archive` into a tree of its own on each host and built
there once by `./build.sh` and `tools/build-preprocessor-pragma-oracle.sh`.
Neither tree changed while its campaign ran. `-march=native` makes each
host's executable its own (`binaries.sha256` in each host's directory).

| Host | CPU | Kernel | Clang | Seeds |
| --- | --- | --- | --- | --- |
| Local | Ryzen 9 7940HS | 7.3.0-2-cachyos | 23.1.3 | 4001-4010 |
| 9700X | Ryzen 7 9700X | 6.18.50-2-lts | 23.1.1 | 4101-4110 |

## Method

`campaign.sh` runs each seed's million cases per mode as five slices of the
public command, each with the pragma oracle (`BUSTER_PPF_PRAGMA_ORACLE`) and
`TMPDIR` on disk:

```sh
./build/compiler test-preprocessor --clang --fuzz 200000 --first FIRST --seed SEED --jobs JOBS
```

Before a seed's slices it runs the seed's other `--clang` checks alone
(`--fuzz 0`, the seed's screen), whose random option sets depend on the seed.
A slice counts only if it exits with status 0 and prints exactly one summary
per mode that names the seed, 200,000 cases and 200,000 compared with clang,
with 0 unverified, 0 differences, 0 batches that differ from their cases
alone and 0 cases without a result. Any other outcome stops the campaign.

On the 9700X every slice took the shared lock (`exclusive.sh`) for itself,
with CI first and a minute between slices, at 15 jobs. Locally the compiler
ran at `SCHED_IDLE` with idle I/O priority, at 8 jobs and from seed 4005's
last slice at 16 (`local/jobs-change.txt`; a case's result doesn't depend
on the job count).

`audit.sh` then re-reads every slice's own output and status, independently
of the runner's ledger (`receipts.tsv`), and passes only with all fifty slices
and every screen. A copy with one summary changed to `1 unverified` fails it.

## Results

| Host | Slices | Fully verified cases per mode (C17, C23) | Unverified | Differ | Without a result |
| --- | ---: | ---: | ---: | ---: | ---: |
| 9700X | 50 | 10,000,000, 10,000,000 | 0 | 0 | 0 |
| Local | 50 | 10,000,000, 10,000,000 | 0 | 0 | 0 |

Each host's directory has `audit.txt`, the campaign's log with every slice's
two summaries (`campaign.log`), the ledger and final state, and each seed's
screen output (`screens/`).

## The differences found on the way

Campaigns on earlier freezes stopped on each of these; their counts are not
reused. Each is fixed in #369 with permanent fixtures.

- Seed 201, cases 20983, 30319 and 89431 (freeze `a0c6176`, Codex's
  handoff): unknown `_Pragma` operands whose ends nothing confirmed. The
  oracle confirms them (`docs/preprocessor-fuzz.md`, "Independent pragma token
  ends"); [the handoff](../preprocessor-fuzz-109-handoff-2026-10-23/README.md).
- Seed 1101 (freeze `a2ab751`, 9700X): `-Ofast` as the last `-O` ignores an
  unknown `-ffp-model=`, which buster refused. Predefines fixtures.
- Seed 2106, case 190642, C23 (freeze `bcc3991`, 9700X, after 5.0 million
  per mode): clang's `InMacroArgs` is one flag, and an invocation in a `#if`
  among a macro's arguments clears it, which buster's check didn't model;
  collection then also goes on past an included file's end. Five
  `pp_test_cases`; the line markers for such an expansion followed in #373
  (#370), which David merged into #369's branch after the freeze.
- Seed 3110, case 84082, C23 (freeze `94ad3e4`, 9700X, after 9.0 million
  per mode): the comparison dropped a case's last whole token whose spelling
  ends in whitespace. Three end controls; and clang's NUL spelling at a
  file's end, `pp_test_trailing_splices`.

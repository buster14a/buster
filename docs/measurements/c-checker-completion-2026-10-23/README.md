# Checker completion reproduction protocol

This directory freezes the original C launcher for issue #133. It contains no
target result or acceptance claim. Record the compiler commit, binary hash,
launcher hash, commands, exit status, complete logs, and every generated report
before adding measured results. Local diagnostics do not establish 9700X
performance or qualification.

The [#125 frozen-input record](../../preprocessor-oracle.md#inputs-and-reproduction)
describes the source/header provenance and original handoff. Keep that bundle's
`README.txt`, `MANIFESTS.sha256`, `manifests/`, `config/`, and `provenance/` intact;
all corpus files and headers remain outside Git. The
[checker contract](../../c-checker.md) and
[earlier checker measurements](../c-checker-2026-10-23/README.md) remain separate
evidence.

## Identities and configuration

| Input | Required identity |
| --- | --- |
| Bundle `MANIFESTS.sha256` | `391e729af6587355050881752558e790299cfe638c7dc7803bd028a8535ebcbd` |
| Clang 23.1.1 binary | `939a882527432ec23b094c289e7f170bf2d6ec282e74dde75e31b08602fa3eae` |
| `config/libc-ch.txt` | `1c1f215dd07bb2235a098e25a7d05ae74b53a65f2939457ffb50a806cf76f904` |
| `provenance/gnu17-pinned-predefines.txt` | `a39036d8215977b5a205f7fd3c4e7dfe6576b58eaa76a7bb0b0e214a164e8d33` |
| c-testsuite checkout | Git commit `5c7275656d751de0e68b2d340a95b5681858ed07` |

The launcher checks the first four hashes, the selected musl list/configuration
hashes, and the own-main configuration hash. `verify` also checks every listed
SQLite, libc, bundled-header, generated-musl-header, and frozen-repository byte.
Verify the external c-testsuite checkout separately; its path is
`tests/single-exec` within that checkout.

System modes use `--no-default-config --target=x86_64-linux-gnu -march=znver5
-std=gnu17 -nostdinc`, with these `-isystem` paths in order under the bundle:
`headers/usr/lib/clang/23/include`, `headers/usr/local/include`,
`headers/usr/include`. Syntax checking replaces #125's `-E -H` with the corpus
harness's `-fsyntax-only`. Both sides use the selected Clang's predefines and
feature answers; the benchmark uses the pinned predefine file and
`--clang-features`. `CPATH` and `C_INCLUDE_PATH` are cleared and
`SOURCE_DATE_EPOCH=1792454400` is fixed.

Musl uses C99, `-ffreestanding -D_XOPEN_SOURCE=700 -Os -pipe`, and the exact seven
include directories from the original musl configuration, in original order.
CRT adds `-DCRT`; CRT-PIC adds `-DCRT -fPIC`; ldso adds `-fPIC`. Own-main uses the
frozen `repository/src/buster/compiler/main.c` plus `-I repository/src` under the
bundle, with no added unit define or unsigned-char option. It does not substitute
the current repository's compiler source.

## Commands and lock boundaries

Build the launcher separately under the shared lock. All paths below are
absolute; each output prefix must be new. Substitute the actual checkout and
output directory, then retain these assignments with the run log:

```sh
check_root=/absolute/path/to/buster
check_out=/absolute/path/to/new-evidence
check_compiler=/absolute/path/to/immutable/compiler
check_clang=/usr/bin/clang
check_bundle=/var/lib/buster-9700x/corpora/preprocessor-391e729af658
check_suite=/absolute/path/to/c-testsuite/tests/single-exec
check_launcher="$check_out/checker-target-run"
EXCLUSIVE_OWNER=checker-133 ~/buster-fast-bench/exclusive.sh \
    "$check_clang" -std=c17 -O2 -Wall -Wextra -Werror \
    "$check_root/docs/measurements/c-checker-completion-2026-10-23/checker-target-run.c" \
    -o "$check_launcher"
EXCLUSIVE_OWNER=checker-133 ~/buster-fast-bench/exclusive.sh \
    "$check_launcher" verify "$check_compiler" "$check_clang" "$check_bundle" \
    "$check_suite" "$check_out/verify" > "$check_out/verify.log" 2>&1
EXCLUSIVE_OWNER=checker-133 ~/buster-fast-bench/exclusive.sh \
    "$check_launcher" libc-all "$check_compiler" "$check_clang" "$check_bundle" \
    "$check_suite" "$check_out/libc-all-01-of-16" 1 16 \
    > "$check_out/libc-all-01-of-16.log" 2>&1
```

Run shards 2 through 16 as separate invocations, each with its own lock and
prefix. Use the same invocation shape for the other modes below. Never hold the
lock around a loop, series, or compilation plus measurement. Honor coordinator
holds and CI priority. The launcher does not acquire or bypass the lock.

Each checker child runs on CPU 2 under `timeout --signal=TERM --kill-after=5s
115s`; identity checks and manifest preparation precede that child. `verify`
performs hashes only. Increase the number of inventory shards if a checker run
times out; do not extend the timeout or count partial output as a completed
shard. The optional shard arguments are one-based and select original row
indices modulo the shard count, with the full denominator checked first.

| Mode | Frozen inventory and purpose |
| --- | --- |
| `libc` | 2,315 C files; compare every Clang syntax-accepted input |
| `libc-all` | 8,983 files: 2,315 C and 6,668 headers; same validity selection |
| `libc-rejections` | All 8,983; classify preprocessing exclusions separately, compare preprocessing-accepted syntax rejections |
| `musl-ordinary`, `musl-crt`, `musl-crt-pic`, `musl-ldso` | 1,587 + 1 + 2 + 1 configured inputs, respectively |
| `musl-ordinary-rejections`, `musl-crt-rejections`, `musl-crt-pic-rejections`, `musl-ldso-rejections` | Same denominators, with preprocessing/syntax rejection classification |
| `ctests` | 220 external files, 128 original random programs, 40 token mutations, 40 semantic mutations; seed 133 |
| `sqlite`, `own-main` | One frozen whole translation unit each |
| `bodies` | SQLite body dependency closure, exact full/extracted scalar/vector metadata proof, both timing paths |
| `scalar`, `vector`, `after` | Isolated SQLite body graph timings; `after` measures vector after a 100 ms scalar warm-up |

Only inventory modes accept shards. Use 16 shards initially for `libc-all` and
`libc-rejections`, and 8 for ordinary musl; tiny CRT/ldso inventories need none.
All benchmark modes use 21 passes and retain preparation costs separately from
graph-only cycles/node, scalar-node share, and vector-node share. Alternate
isolated scalar/vector/after invocations on the same immutable inputs and binary;
compare repeated distributions before making a performance claim.

## Reports and failure states

The launcher prints exact child argv and checked identities. Preserve stdout,
stderr, exit status, `.tsv`, selected-list, generated C-only inventory, and shard
list files. The historically named `.accepted.list` contains compared semantic
rejections in rejection mode. Do not discard excluded rows or replace an older
report. Aggregate all shards exactly once; accepted subsets are measured
outcomes, not fixed denominators. In particular, do not trim a local selection
to force the historical 733-C count.

Corpus TSV rows retain Clang/scalar/vector acceptance, first diagnostic
file/line, phase, exact-path agreement, and the resource flag. Exit 1 means the
comparison failed; a resource row or preprocessing failure is an infrastructure
failure, never a legitimate semantic rejection. Exit 2 denotes invalid
configuration/identity/inventory or invocation. Timeout is exit 124; exit 137
denotes a killed process and requires the log to distinguish timeout escalation
from another kill. Exit 125 denotes supervisor/wait/signal failure; 126/127
indicate invocation/missing-tool failures. None proves source rejection. A zero
exit still requires the expected full inventory, zero resource/parity failures,
and complete reports before a result is usable.

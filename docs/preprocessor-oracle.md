# Frozen preprocessor oracle

Issue [#125](https://github.com/buster14a/buster/issues/125) uses:

```sh
./build/compiler test-preprocessor --compare-clang [--clang PATH] [options] corpus...
```

Inputs are files or directories of `.c` and `.h` regular files. Directory walks
use a bounded explicit stack, ignore symlinks and fail on inaccessible entries
or inventory exhaustion. `--files-from LIST --root DIR` reads one path per line,
relative to `DIR` (default `.`); spaces are preserved. Inputs are sorted and
duplicate paths removed. An empty inventory fails.

The shared configuration is `--no-default-config --target=x86_64-linux-gnu`, C
input, and `-std=gnu17` unless overridden. Without `-nostdinc`, the harness asks
the selected Clang for its include search and passes that order explicitly to
both sides. Frozen runs use `-nostdinc` and the pinned `-isystem` paths. Clang's
`CPATH` and `C_INCLUDE_PATH` are cleared; Buster does not read them. One
`clang -dM -E` dump per configuration supplies Buster's predefines, without
adding `__BUSTER__`. Both sides inherit `SOURCE_DATE_EPOCH`, defaulted to
`1792454400` (2026-10-20 UTC) for this run. The existing Clang feature tables
supply the feature queries in oracle mode.

For each Clang-accepted file, both kernels must give identical streams and
directive tables. Both printers must give identical bytes with and without
markers, and their text must lex back into the stream. Clang's output must have
the same lexical token kinds and spellings (keywords normalize to identifiers,
and Buster's generated integer kind to a pp-number). Every token's presumed
line, file and include stack, including system flags, must match Clang's
markers. The first difference includes nearby tokens.

Clang exit 1 is an explicit skip, with its first error and full diagnostics.
Its partial output is never counted as identical. A failed macro dump, missing
executable, signal, unexpected exit, unreadable input or output/I/O failure
fails the run. A run with no accepted files also fails. The summary reports
`compared`, `identical`, `different`, `skipped`, `errors` and output tokens.
`--report NEW.tsv` writes one row per file, including each skip's first reason,
and refuses to overwrite a report. Full Clang diagnostics go to stderr.
`--keep` retains the temporary macro dump. The older fixture interface,
`test-preprocessor --clang`, still takes `--clang` as a flag without a path.

The first full libc pass exposed three differences. Builtin/feature/target
query names now remain unexpanded, while attribute queries still expand
macros. The Clang table now includes `ext_vector_type`, queried in both its
ordinary and underscored spellings in C99, GNU17 and C23 on Clang 23.1.1, and
cross-checked with local Clang 23.1.3 (`clang 23.1.3-5`).

The table-entry query, repeated with `-std=c99` and `-std=c23`, prints `1 1`:

```sh
printf '%s\n' '__has_attribute(ext_vector_type) __has_attribute(__ext_vector_type__)' | \
    /usr/bin/clang --no-default-config --target=x86_64-linux-gnu -std=gnu17 -E -P -x c -
```

Clang's closing nullability callback supplies an invalid location, so its
printer keeps the current presumed line. Both printers reproduce that placement
within the current presumed context; token locations retain their source
positions. The behavior references are Clang 23.1.1's
[pragma handler](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/Lex/Pragma.cpp#L1843)
and [output callback](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/Frontend/PrintPreprocessedOutput.cpp#L691).
The regression inputs are original, including macro operands and both forms of
the pragma, with ordinary tokens and a presumed-file change between them.

## Inputs and reproduction

The [completed frozen-input handoff](https://github.com/buster14a/buster/issues/125#issuecomment-6290654388)
is preserved at `/home/david/buster-fast-bench/preprocessor-corpora-astra-01a158dd/`
on benchpress (Ryzen 7 9700X). A byte-identical CI-readable copy lives at
`/var/lib/buster-9700x/corpora/preprocessor-391e729af658/`. The historical
configuration/evidence keeps its original absolute paths; new invocations
use the chosen root explicitly. Neither corpora nor system headers are committed.

| Manifest | SHA-256 |
| --- | --- |
| `MANIFESTS.sha256` | `391e729af6587355050881752558e790299cfe638c7dc7803bd028a8535ebcbd` |
| `manifests/sqlite.sha256` | `9179b7ad7db7366d12a214b562e478ac703e7ec0376f38f4516756bcf7ee0047` |
| `manifests/libc.sha256` | `bf593a94954f3ed9431dd12e60233ec0229f7226a1fc270a701614a20f86baeb` |
| `manifests/musl-generated.sha256` | `89dbe35f0324cf08d0cb9cf31ecf15092fc20783a9e470e80e5f41b1d48251d3` |
| `manifests/headers.sha256` | `8e6e646951cbd08c07bbc76ff46d6796a83a58e9b1c4959278b60193718cd10c` |
| `manifests/headers-bundle.sha256` (headers and package provenance) | `e307b836448ef20e603a617a4b5023831c72ff5f307f614c50f4f8833088717c` |

These identify SQLite **3.53.2**, Zig **0.14.1**'s libc tree and patched musl
**1.2.5**, its copy-only x86-64 generated-header overlay, and **778** reached
installed headers. Header packages are glibc `2.44+r50+g1848099f063e-1`,
linux-api-headers `7.2-1`, Clang `23.1.1-1`, libxcrypt `4.5.2-1` and
util-linux-libs `2.42.4-1`. The bundle's `README.txt`,
`headers/package-owners.txt` and `headers/provenance.tsv` record exact origins.
The reference is `/usr/bin/clang` **23.1.1**, SHA-256
`939a882527432ec23b094c289e7f170bf2d6ec282e74dde75e31b08602fa3eae`.
CI checks that reference binary's hash too.

For an on-demand libc run, `bundle` names the original bundle, `compiler` an
absolute executable path, and `report` a new TSV path:

```sh
mapfile -t options < "$bundle/config/system-pinned.args"
EXCLUSIVE_OWNER=oracle ~/buster-fast-bench/exclusive.sh "$compiler" test-preprocessor \
    --compare-clang --clang /usr/bin/clang "${options[@]}" \
    --files-from "$bundle/config/libc-ch.txt" --root "$bundle/libc" --report "$report"
```

Use the same flags for `config/sqlite-c.txt` relative to `sqlite/` and
`config/repository-c.txt` relative to `repository/` (add
`-I "$bundle/repository/src"`). Musl's four list/flag pairs are
`musl-ordinary.txt`/`musl.args`, `musl-crt.txt`/`musl-crt.args`,
`musl-crt-pic.txt`/`musl-crt-pic.args` and `musl-ldso.txt`/`musl-ldso.args`,
all under `config/`, with source paths relative to `libc/musl/`. They preserve
upstream's include order, C99, freestanding mode, `_XOPEN_SOURCE=700` and the
CRT/PIC options. The libc inventory is the **8,983 C/header files** recorded by
[the lexer benchmark](c-lexer.md); the
handoff's forced-C scan of non-C files is supplemental evidence.

Every build and comparison takes the shared lock separately, never around a
series. The 9700X workflow verifies the manifests, package provenance and every
source/header byte, and rejects additional files or nonregular entries,
then compares SQLite, four musl sources and seven libc headers, including the
three regressions. The libc subset must produce five identical files and two
explicit skips. Its input lists live
under `tests/preprocessor-oracle/`. The runner hooks hold the lock for the job.
The `buster-fast-9700x` concurrency prefix is scoped per PR/ref, so an older
branch's repository-wide pending slot cannot cancel another PR's qualification.
Each queue keeps one pending run, GitHub's default `queue: single`, as
[GitHub documents](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/control-workflow-concurrency):
a newer push to the same PR replaces its pending run, whose commit is no longer
the PR's to qualify, and a running one finishes. The runner hooks serialize
all CI jobs with the shared machine lock.

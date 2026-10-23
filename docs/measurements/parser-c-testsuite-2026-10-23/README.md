# Pinned c-testsuite parser qualification, 2026-10-23

This is the separately claimed corpus qualification for #126. It uses
c-testsuite revision `5c7275656d751de0e68b2d340a95b5681858ed07` from
<https://github.com/c-testsuite/c-testsuite>, with all 220 C sources listed in
[the source manifest](source.sha256). The upstream checkout, including its
per-test origin, tag and license files, is retained on the target host; test
source contents are not imported here.

The target directory's complete `.c` and `.h` enumeration and content hashes
must exactly match that 220-entry C manifest: no missing files, extra source
inputs or headers. A zero-file or reduced-input run is not acceptance. The
parser's original-file count must be 220, and the preprocessor TSV must list
those same 220 paths exactly once. Corpus hashes and enumeration were checked
against the retained manifest before evaluating results.

The compiler source revision is `5a9e05048c7c4494491bf2eb62d9f326cf940218`.
Its approved-host executable has SHA-256
`4c634879d239980d271ddb60cd64f3f44c18c685457ef0756b759e7c2b77d5d2`.
[The lowering qualification record](../c-lowering-2026-10-23/README.md)
retains the full compiler source manifest and host details.
[The qualification-input manifest](qualification-inputs.sha256) records the
adapter, control inputs and predefined macros; all four hashes match the
files used on the target host.

## Results

The locked 9700X runs completed successfully on October 23, 2026:

| Check | Result |
| --- | --- |
| Preprocessor token and location oracle | 220 identical files, 254,294 tokens, zero differences, skips or infrastructure errors |
| Original-source parser comparison | 220 files, all accepted by Clang, zero mismatches |
| Parser paths and kernel twins | All 220 units agree; fallback handles 17 top-level declarations |
| Mutation comparison | 2,200 mutations, zero mismatches under the rules below |
| Direct Clang syntax inventory | Exit 0, no diagnostics; all 220 original sources accepted |
| Argument and feature-query controls | All expected statuses and diagnostic comparisons pass |

The [preprocessor TSV](c-testsuite-preprocessor.tsv) contains exactly the
220 manifest paths, each once, with no extra inputs. Its token total matches
the [parser output](c-testsuite-parser-qualified.log). The independent Clang
inventory's [exit status](c-testsuite-clang-syntax.exit) and empty diagnostics
agree with the parser harness's accepted/rejected counts. There are no
original-source rejection reasons or preprocessing exclusions to account for.

For the mutations, Clang accepts 309, reports a parse issue first in 1,496,
and reports another issue first in 395. All 1,496 parse-error cases are rejected
on the matching first-error line. Of the 395 other-error cases, the parser
rejects 258; that last category imposes no accept/reject requirement on a
syntax-only parser. This run does not compare scalar/vector paths on mutations.

[The control summary](oracle-controls.log) records both success and expected
failure statuses. Direct and adapted invalid-input diagnostics are retained
in [the direct log](control-invalid-direct.log) and
[the adapter log](control-invalid-adapted.log); they are byte-identical.
The [near-match flag](control-near-match.log) remains rejected.
The [feature-enabled output](control-feature-mode.i) succeeds, while
[feature mode disabled](control-native-mode.log) reports the canary's expected
error. The records omit only shared-lock waiting messages;
[the preprocessor output](c-testsuite-preprocessor.log) retains its actual
result and temporary-directory identity.

## Matching the source configuration

Both sides use GNU17, target `x86_64-pc-linux-gnu`, `-nostdinc`, explicit
frozen Clang/glibc include directories and `SOURCE_DATE_EPOCH=1792454400`.
`CPATH` and `C_INCLUDE_PATH` are cleared. The header bundle is the immutable
`preprocessor-391e729af658` copy documented in
[the preprocessor oracle guide](../../preprocessor-oracle.md).
The two `-isystem` paths, in order, are:

- `/var/lib/buster-9700x/corpora/preprocessor-391e729af658/headers/usr/lib/clang/23/include`
- `/var/lib/buster-9700x/corpora/preprocessor-391e729af658/headers/usr/include`

The oracle is `/usr/bin/clang` 23.1.1, SHA-256
`939a882527432ec23b094c289e7f170bf2d6ec282e74dde75e31b08602fa3eae`.
[The generated predefined macros](predefines.h) have SHA-256
`3cbbe9d5dd71baa51a0c5fefb7182f2ab82bc2d9df50bb6fdf12fd6d6653dc72`.
They were captured under the shared lock with:

```sh
/usr/bin/clang --no-default-config -dM -E -std=gnu17 \
  -target x86_64-pc-linux-gnu -x c /dev/null
```

This unmodified generated dump retains the LLVM terms in
[LICENSES/LLVM.txt](../../../LICENSES/LLVM.txt). The adapter, control inputs
and qualification report are original work under the repository's default
terms. The external corpus retains its own per-test license metadata.
The dump deliberately preserves Clang's trailing spaces on seven empty macro
definitions so its bytes match the target input and recorded SHA-256.

The parser test runner at this revision forwards bare `--clang-features` to
Clang, although it is a Buster-only option. `--predefines` alone does not turn
on Clang's feature-query answers. [The original C argument adapter](clang-oracle-adapter.c)
therefore removes only exact `--clang-features` arguments, injects
`--no-default-config`, and replaces itself with `/usr/bin/clang`. All other
arguments keep their order; diagnostics, environment, exit status and signals
pass through. This adapter is for the controlled argument profile below and
does not change the actively owned parser or checker files.

The adapter was compiled on the locked 9700X using `-std=c17 -O2
-march=native -fwrapv -funsigned-char -Wall -Werror`; its SHA-256 is
`484f0610e7a89857d01dcbee90d415334b252de7c3f073012d6e60bec0b576bf`.
The adapter source SHA-256 is
`cb93a0954c68a0d90ad7c7cb743222262e0c1c8f6314e81c3be61991dfa348c4`.

The control inputs are [a feature-query canary](feature-oracle-canary.c)
and [an original invalid declaration](parse-oracle-invalid.input). The latter
is copied to `inputs/parse-oracle-invalid.c` on the target. Direct and adapted
Clang must accept the canary, reject the invalid declaration with identical
diagnostics and exit 1, and reject the near-match flag `--clang-features=1`.
Buster must preprocess the canary successfully with feature mode and reject
it without that mode, even when both runs use the same predefined macros.

## Reproduction and interpretation

The target working directory is
`/home/david/buster-fast-bench/codex-lowering-129`. The compiler checkout is
`tree/`, the adapter is `clang-oracle-adapter`, and the corpus is
`inputs/c-testsuite/tests`. Each independent build, control test and corpus
run acquires `~/buster-fast-bench/exclusive.sh` separately. CI keeps priority.

The parser command uses the original C sources, the frozen predefined macros,
`--clang ./clang-oracle-adapter --clang-features --mutations 2200 --seed 126
--keep --reports 40` and the shared configuration above. Its original-file
pass compares the scalar/vector parser paths and their kernels. Its mutation
pass compares the vector parser with Clang; an aggregate mutation count alone
does not establish equal coverage per source. A Clang-accepted file must parse.
For a Clang parse error, the first error line must also match. Other semantic
Clang rejections are tallied without requiring the parser to reject them.

The independent `test-preprocessor --compare-clang` run uses the same source
directory, target and include configuration, with a per-file TSV report. It
enables Clang feature mode internally and captures its own matching macro dump;
it does not receive `--clang-features`. The report distinguishes token/marker
agreement from explicit skips and infrastructure errors.

The full corpus commands are:

```sh
env -u CPATH -u C_INCLUDE_PATH SOURCE_DATE_EPOCH=1792454400 \
  EXCLUSIVE_OWNER=codex-126-lowering \
  ~/buster-fast-bench/exclusive.sh \
  ./tree/build/compiler test-preprocessor --compare-clang --clang /usr/bin/clang \
  -std=gnu17 -target x86_64-pc-linux-gnu -nostdinc \
  -isystem /var/lib/buster-9700x/corpora/preprocessor-391e729af658/headers/usr/lib/clang/23/include \
  -isystem /var/lib/buster-9700x/corpora/preprocessor-391e729af658/headers/usr/include \
  --report logs/c-testsuite-preprocessor.tsv --keep inputs/c-testsuite/tests

env -u CPATH -u C_INCLUDE_PATH SOURCE_DATE_EPOCH=1792454400 \
  EXCLUSIVE_OWNER=codex-126-lowering \
  ~/buster-fast-bench/exclusive.sh \
  ./tree/build/compiler test-parser --clang ./clang-oracle-adapter --clang-features \
  --mutations 2200 --seed 126 --keep --reports 40 \
  -std=gnu17 -target x86_64-pc-linux-gnu -nostdinc \
  -isystem /var/lib/buster-9700x/corpora/preprocessor-391e729af658/headers/usr/lib/clang/23/include \
  -isystem /var/lib/buster-9700x/corpora/preprocessor-391e729af658/headers/usr/include \
  --predefines inputs/c-testsuite-oracle-predefines.h inputs/c-testsuite/tests
```

A separate direct-Clang syntax inventory uses the same environment, language,
target and header arguments, plus `--no-default-config -fsyntax-only -w
-fdiagnostics-show-category=name -fno-caret-diagnostics -fno-color-diagnostics
-ferror-limit=0`, on `inputs/c-testsuite/tests/single-exec/*.c`. These match
the parser harness's diagnostic options and retain the reasons for every
original-file rejection. The inventory wrapper permits only exits 0 and 1;
that status alone does not qualify an exit-1 result as a source rejection.
Its error must name an inventoried source or a reached pinned header. Driver,
configuration, missing-input and unclassified failures are infrastructure
failures. The validated input manifest and explicit diagnostic attribution
are required before reconciling accepted/rejected counts with the parser run.

This qualification concerns preprocessing and parsing. It does not establish
source-checker completeness, emitted-symbol agreement, executed-program
correctness or the source-to-lowering acceptance of #129.

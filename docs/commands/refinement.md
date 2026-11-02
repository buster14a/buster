# refinement

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/c_intern.c` is the identifier intern table (an
  `IdentId` per distinct spelling) and the predefined names that get the first
  ids (keywords, directive names, the preprocessor's names), and
  `src/buster/compiler/c_refine.c` is token refinement: per file, after the
  lexer, each identifier's `IdentId` (in place of its end) and each token's
  exact leading-space bit. The preprocessor lexes a file of two windows (256 KB)
  or more a 128 KB window at a time (`c_tokenize_lines`, cut where a logical
  line starts) and refines each window's completed blocks while they are in the
  caches (`refine_tokens_blocks`, `pp_prepare_windows`), into the whole file's
  records. `docs/c-refinement.md` explains the design and its 9700X
  measurements. `c_refine_test.c` holds their tests (`main.c` includes it;
  `./build/compiler test-refinement` runs them) and `c_refine_benchmark.c`
  provides `benchmark-refinement`.
## Refinement tests

`./build/compiler test-refinement [--count N] [--seed N] [path...]` (about
1.3 s without paths) compares `refine_tokens`, with each variant of its batched
interning (`RefineVariant`), with `refine_tokens_scalar` and with an oracle of
its own (splices and UCNs removed in plain passes, and a different table): every
identifier must get the same id and every token the same leading-space bit, on:

- 8,192 guarded bucket patterns covering every prefix dword match combination,
  misses and multiple candidates, with low and near-limit IDs;
- the hash's four forms on spellings of 0 to 200 bytes (the one that hashes
  four spellings at once, up to 32 bytes, with their first lanes);
- 88,064 structured spellings that must all hash differently: repeated and
  swapped 64-byte chunks and 16-byte lanes, and #192's review's 32-byte
  identifiers with paired changes in two lanes;
- about 14,800 spellings where the same bit flipped in two lanes must change
  the hash;
- four fixed paired keys that cancelled in the old hash's low half, requiring
  separation after final mixing and distinct stable IDs;
- the predefined names, every name's keyword answer in each of the ten modes
  from `c89` to `gnu23` and in five of them with `-fgnu-keywords` or
  `-fno-gnu-keywords` (#281), and 24 spellings chosen apart from the predefined
  names with clang's answer (17 keywords, 7 not);
- 300,000 random spellings that grow the table three times;
- growth after 1, 4, 15, 16, 31, 32, 63 or 64 new names, with preceding-batch
  repeats and long common-prefix names, then the same input again;
- 30 boundary fixtures (splices between and inside tokens, with `\r\n`, blanks
  and trigraphs; comments; UCNs; named UCNs, which stay as written, apart from
  the `\u`, `\U` and UTF-8 spellings of their characters (#452); identifiers of
  62 to 65 bytes), each at all 64 misalignments from a cache line;
- identifiers of every length from 1 to 130 bytes with a line splice (four
  spellings, one a trigraph's) at every position inside, or a UCN at every
  position, each followed by the same identifier written plainly: 50,570
  inputs;
- 20,000 random inputs (`--count N`) of identifiers, splices, comments,
  digraphs, pp-numbers and other tokens, one in 16 with trigraphs;
- 9,600 prefixes and suffixes of random inputs that end exactly at a readable
  page's end, before a `PROT_NONE` page, or start at a page's start: a read
  outside the source faults, and the test names the input and exits with 1;
- every `.c` and `.h` file under each path. On the 9700X, `src` and the C
  library corpus (`~/azp-bench/c-tokenizer/corpus/libc`) pass.

`--seed N` picks another random sequence. The 9700X job's test step runs it on
`src`.

## Refinement benchmark

`./build/compiler benchmark-refinement [--cplusplus] [--trigraphs]
[--after-scalar MS] [--only NAME] path...` lexes every `.c` and `.h` file once,
checks every implementation against `refine_tokens_scalar`, and reports the
share of identifiers the table finds and how often the vectorized path takes a
scalar one. Then come 21 interleaved passes, each from a table reset to the
predefined names, with the medians per token and per identifier (time, cycles,
instructions, IPC) of the variants (`refine_tokens_16`, `_32`, `_64` and
`_fused`; `refine_tokens` is the 64 one), the scalar path, `c_tokenize` and
`c_tokenize` then refinement per file. `--only NAME` runs one of them, its
passes back to back, and `--after-scalar MS` runs the scalar path over the
corpus for MS ms (100 to 300) before each timed pass of a vectorized one: the
state after lighter code, in which Zen 5's power cap slows a heavy AVX-512 loop
(#199, as the encoder's benchmark does). The columns, table and source stay in
the caches from pass to pass, unlike the preprocessor's, whose refine phase is
what #115's budget measures. `docs/c-refinement.md` has the numbers for
`sqlite3.c`, the C library corpus and `src`. Run it pinned to one CPU; the
default 9700X job runs it on `src`, and like the others its measurements are
informational.

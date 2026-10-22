# Token refinement and interning

`src/buster/compiler/c_refine.c` and `src/buster/compiler/c_intern.c` turn the
lexer's output for a file (`c_lexer.c`: kinds, starts and ends) into what the
preprocessor and every later stage read: an `IdentId` for every identifier and
an exact leading-space bit for every token. It runs once per file, however many
translation units include the file. This is #15's stage 1 and roadmap item 1,
tracked in #116. This document records the design, the Zen 5 measurements
behind each choice, and what comes next.

## Results

Measured on 2026-10-20 on the Ryzen 7 9700X (`benchpress`, clang 23.1.1,
`build.sh`'s `-O3 -march=native`), pinned to CPU 2, each run under the
`exclusive.sh` lock. `benchmark-refinement` takes the median of 21 interleaved
passes. Each pass refines the whole corpus from an intern table reset to the
predefined names, so the table fills up through the pass as it would in a
compiling process.

| Corpus | Tokens | Identifiers | Found in the table | `refine_tokens` | `refine_tokens_scalar` | `c_tokenize` (for scale) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `sqlite3.c` 3.53 | 1,168,130 | 454,717 | 96.31% | **10.50** cycles/token, 2.21 ms | 44.55 | 7.89 |
| C library (Zig 0.14.1 `lib/libc`) | 13,152,141 | 6,115,884 | 90.23% | **18.55** cycles/token, 44.0 ms | 47.71 | 6.78 |
| Repository `src` (on top of `main` at 75261db) | 366,756 | 124,234 | 93.17% | **9.85** cycles/token, 0.65 ms | 38.06 | 6.19 |

| Corpus | Instructions/token | IPC | Cycles/identifier | Lex, then refine each file |
| --- | ---: | ---: | ---: | ---: |
| `sqlite3.c` | 38.79 | 3.69 | 26.97 | 18.34 cycles/token: refinement adds 10.45 |
| C library | 51.09 | 2.75 | 39.89 | 22.67 cycles/token: refinement adds 15.89 |
| Repository | 37.15 | 3.77 | 29.08 | 15.91 cycles/token: refinement adds 9.72 |

- The vectorized path is 4.2 times as fast as the scalar one on `sqlite3.c`, and
  2.6 times on the C library. The C library has 597,439 distinct spellings, so
  its table grows to 262,144 buckets (16 MB) besides the records. Its probes
  miss the L2 cache, and one identifier in ten is new and inserts.
- Rare cases take a scalar path. Across the corpora, 2,036 identifiers in the C
  library are longer than the 63 bytes the masked load reads; no identifier has
  a backslash (a line splice or a UCN); and 1,073 tokens in the C library (34 in
  `sqlite3.c`) follow a line splice directly, so `refine_gap_is_splices`
  decides their leading space.
- Budget: the front-end plan (#115) proposed 1.5 ms to refine `sqlite3.c` and
  its headers, about 12 cycles per output token. The kernel takes 2.21 ms for
  `sqlite3.c` alone, which is 17.3 cycles per output token of its 707,153. In
  the preprocessor (`docs/c-preprocessor.md`), the refinement phase takes
  30% of 12.0 ms, 3.6 ms, for `sqlite3.c` and its headers (1,243,768 tokens).
  That figure includes the kernel clearing the fresh pages the phase writes,
  and the table's first inserts. So the budget is not met yet. "Next steps"
  lists where the remaining time could come from.

## What it produces

For token `i` of a file:

- `values[i]` is the identifier's `IdentId` for an identifier. For any other
  token it is the token's end, which keeps a literal's or punctuator's spelling
  reachable without a separate `ends` array. Refinement writes `values` in place
  of the lexer's `ends`, so a file's tokens take 9 bytes each (kind, start,
  value) plus a bit.
- Bit `i % 64` of `space[i / 64]` is its leading-space bit. It is set when
  whitespace or a comment separates the token from the one before it. A gap
  made only of line splices is no whitespace, because translation phase 2
  removed it. So `#define F\<newline>(x)` defines a function-like macro, and
  stringizing `a\<newline>+b` gives `"a+b"`. A comment is whitespace, so
  `#define F/**/(x)` defines an object-like one. clang and GCC agree on each of
  these cases (#116 lists them, with trigraphs too).
- Kinds and starts stay as the lexer wrote them. A keyword stays a
  `C_TOKEN_RAW_IDENTIFIER`, because a keyword can be a macro name. The
  preprocessor gives keywords their kinds in its output
  (`pp_resolve_keywords`).

`IdentId`s are dense `u32` handles given in the order spellings are first
interned, so in token order within a file. Id 0 is none. The first ids belong
to the names the later stages switch on (`BUSTER_C_NAMES` in `c_intern.c`):

1. the keywords of C, as clang 23 has them in C: the standard's, GNU's and
   clang's spellings (`__attribute__`, `__asm__`, `__typeof__`,
   `__extension__`, `__inline`, `__restrict` and the others), and clang's other
   keywords:
   - calling conventions such as `__cdecl`;
   - builtins that are keywords, such as `__builtin_bit_cast`;
   - ARM and Objective-C keyword attributes;
   - `_ExtInt` and `_Countof`.

   That is 148 keywords. They were generated rather than written by hand
   (#192's review found 40 missing from the hand-written list):
   - take every name in clang 23.1.3's `TokenKinds.def` and in the
     `RegularKeywordAttrInfo.inc` it includes;
   - ask `clang -E -P -std=MODE` for each one's `__is_identifier`, in the ten
     modes from `c89` to `gnu23`;
   - keep the names it answers 0 for in some mode, with those modes. They fall
     into the six rules of `c_name_is_keyword`.
2. the directive names that aren't keywords;
3. the preprocessor's own names (`defined`, `__has_include`, `__VA_ARGS__`,
   `__VA_OPT__`, `_Pragma`, `__FILE__`, `__LINE__`, `__COUNTER__` and the
   rest).

Telling a keyword is then one compare against `C_NAME_KEYWORD_COUNT`. Whether a
name is a keyword depends on the language mode (`bool`, `true` and `typeof`
come with C23; `asm` and `typeof` come with GNU modes), so `c_name_is_keyword`
reads a rule per keyword, the rules of clang's `TokenKinds.def`. A mode's
`CLanguage` holds the standard's year in full (1989 to 2023), so that a later
standard compares greater. Nothing may be
emitted in id order, so that interning from several threads later (#150) can't
change the compiler's output.

An identifier's lookup key is its spelling after translation phases 1 and 2:
trigraphs (when enabled) and line splices are removed, and each UCN becomes the
UTF-8 of its code point, as clang interns it. So `fo\<newline>o` is `foo`, and
`é`, `é` and `\U000000E9` are one identifier. A named UCN (`\N{...}`)
stays as written, because the table has no character names (an open question in
#116).

Differences from #116's sketch, each deliberate:

- A literal's value is its end, not an index into a per-file table of
  spellings. The preprocessor reads a literal's spelling from the source when
  it compares, pastes or stringizes it, and removes splices then (`pp_spelling`
  does this). Most literals are never compared, so a table would cost every
  literal for the sake of a few.
- The table is not a Swiss table of 1-byte tags. It uses 64-byte buckets of
  prefixes; "The intern table" below explains why, with the measurements.
- The hash is not `link_hash`. It is a fold of 16-byte lanes, each whitened
  and put through two AES rounds, which hashes an identifier from the same
  64-byte load that reads it.
  Whether the link representation's names adopt it, now that its runtime
  snapshot would make the function part of a file format, is #84's question.

## The vectorized path

`refine_tokens` works on blocks of `BUSTER_REFINE_BLOCK_TOKENS` (1,024) tokens
in two passes. Each block's dense identifier arrays stay in the L1 cache.

### Pass A: gaps, leading spaces and the identifiers

Pass A works on 64 tokens at a time: one vector of kinds and four of starts and
ends.

- The gap bits come from one compare per 16 tokens, `starts[i] !=
  ends[i - 1]`, with `valignd` supplying the previous token's end across the
  vector boundary.
- A gap made only of splices needs a backslash. Before the first block,
  `refine_backslash_chunks` marks the source's 64-byte chunks that hold one
  (with trigraphs on, a `?` too, since `??/` is a backslash). A step whose gaps
  lie entirely in unmarked chunks is done after the compare. Backslashes are
  rare in C outside macro definitions: `docs/c-lexer.md` finds none in 97% of
  the C corpus's 512-byte chunks.
- In a marked step, a gap can only be splices if the token doesn't start a line
  and the byte before it is a newline. A newline outside a comment starts a
  logical line, and a block comment ends with `*/`. The bytes before 16 starts
  come from a 128-byte window of the source with one `vpermi2b`. The few tokens
  that qualify have the scalar `refine_gap_is_splices` decide their bit.
- The identifiers' starts, lengths and token indices are compressed
  (`vpcompressd`) into dense arrays for pass B.

On its own, pass A costs 1.66 cycles per token on `sqlite3.c` and 3.43 on the C
library. Those figures were measured with pass B removed, in the first version
below.

### Pass B: interning in batches

Pass B interns the block's identifiers in batches of 64, in three stages.

1. **Load and hash.** Each identifier is read with one masked 64-byte load,
   bounded by its length (`bzhi`). The load never reads outside the token, so
   never outside the source. An identifier with a backslash (or, with
   trigraphs, a `?`), or one longer than `BUSTER_INTERN_FAST_LENGTH` (63 bytes),
   is marked for the scalar path. The others are hashed from the loaded vector
   (`intern_hash_vector`), and their buckets are prefetched.
2. **Probe.** One compare of each bucket's four slots against the identifier's
   first 12 bytes, zero-padded, finds the slots with its prefix. A spelling of up
   to 11 bytes is identified by its prefix alone, because its zero padding
   includes the zero at its length, and identifiers have no NUL bytes. This
   covers 86% of identifier occurrences in `sqlite3.c` and 73% in the C library.
   A longer spelling's first candidate is confirmed in stage 2b with one masked
   compare of its bytes and terminating zero against its record. Nothing
   branches on the outcome. An identifier that isn't found this way is deferred
   as a bit of a mask: a new spelling, a second candidate, a probe past the
   first bucket, or the scalar path.
3. **Deferred.** The deferred identifiers are interned in token order with
   `intern_find_or_add`, so new spellings get exactly the ids
   `refine_tokens_scalar` gives them, and the two paths' outputs can be
   compared value for value.

Separating the stages lets a batch's 64 cache misses overlap. The variants
`benchmark-refinement` compares measure this directly: on `sqlite3.c` and the C
library, batches of 64 cost 10.50 and 18.55 cycles per token, batches of 16 cost
11.51 and 22.61, and `refine_tokens_fused` costs 13.13 and 25.03. The fused
variant probes each identifier right after hashing it, without a prefetch, and
issues fewer instructions (35.46 and 47.04 per token), but stalls on every miss.

### What is per identifier, and what could be across identifiers

Both passes are data-parallel over tokens, as the lexer is. Pass A works on 64
tokens per step, with no branch per token but the rare splice check. Pass B
works on 64 identifiers per batch, so that their cache misses overlap. Only
stage 3 is ordered: it gives new spellings their ids in token order, which keeps
the ids the same as the scalar twin's.

Inside a batch, the hash is computed per identifier from that identifier's own
64-byte load. Hashing across identifiers, 8 per vector, was considered:

- The fold has to stay per identifier: it reduces one identifier's vector to
  128 bits.
- The rounds run on that vector, all four lanes at once: two `vaesenc zmm` per
  identifier, before the fold.
- Before #192's review moved the rounds there, they mixed the folded 128 bits.
  The 9700X profile of `sqlite3.c` put that mix, with the move of its result
  to a general register, at 5.5% of `refine_tokens`' samples. That bounds what
  hashing 8 identifiers per vector could save, and their transpose would cost
  about a uop each.

Stage 2's per-identifier bookkeeping (the slot's id to its token, and the
candidate and deferred masks) takes about 18% of the samples, so it is the
larger target ("Next steps").

### The intern table

The table (`c_intern.c`) uses open addressing over 64-byte buckets of four
16-byte slots. Each slot holds the first 12 bytes of its spelling, zero past
the spelling's end, and the spelling's id. Spellings and a terminating NUL live
in a record arena, and `record_of` and `length_of` give each id's record.
Buckets are probed in order from the hash's bucket. Spellings are never
removed, so a key is absent once a probe meets a bucket with an empty slot. The
table starts at 8,192 buckets and doubles at a load of 5/8 of its slots.

The first version used a Swiss table, as #116 sketched: groups of 16 one-byte
tags, a slot array of record offsets, and the records. A hit needed three
dependent loads (tags, slot, record). With buckets, one cache line answers most
lookups. At the same batch size the buckets measured slower on `sqlite3.c`
(13.90 against 12.96 cycles per token, batches of 16) and about equal on the C
library (24.07 against 23.79). But they scaled with the batch, reaching 12.96
and 20.07 with batches of 64, so they were kept and tuned further:

| Version | `sqlite3.c` (cycles/token) | C library (cycles/token) |
| --- | ---: | ---: |
| Swiss tags, batches of 16, a two-multiply hash | 12.96 | 23.79 |
| Buckets, batches of 16 / 32 / 64 | 13.90 / 13.20 / 12.96 | 24.07 / 20.99 / 20.07 |
| Buckets, AES hash, file-level backslash chunks, prefixes kept from stage 1, batches of 16 / 32 / 64 | 11.42 / 10.74 / **10.48** | 23.04 / 20.01 / **18.52** |

(The last row was measured before the latest round and re-measured at 10.50
and 18.55 for this document's results. Each figure is the median of 21 passes;
runs differ by about 1%.)

The hash is defined by `intern_hash_bytes_scalar`:
- **Lanes.** Each 64-byte chunk of the spelling, zero past its end, has four
  16-byte lanes. Each lane is XORed with a whitening of its own (a pair of
  wyhash's constants per lane) and put through two `aesenc` rounds. The length
  is XORed into the first lane first.
- **Fold.** The chunk's fold is the XOR of the four results.
- **Chain.** Each chunk after the first is XORed into an `aesenc` round of the
  state.
- **Result.** The hash is the state's low quadword.

Each piece guards against a way of cancelling that #192's review found:
- *The chain.* A plain XOR of the chunks would hash every identifier `X X` of
  two equal 64-byte halves by its length alone.
- *The lanes' rounds.* A fold of the lanes as they are is linear: the first
  version rotated each lane by its own amount, and a bit flipped in one lane
  with the bit its rotation lines up with in another left the hash as it was.
  The review gave 65,536 such 32-byte identifiers that all shared one hash.
- *Two rounds, not one.* After one round, the change a flipped bit makes
  depends on its byte alone. A bit flipped in two lanes over bytes that are
  equal once whitened still cancels: 6,740 of 14,856 flips did in the test.
- *The whitening.* It keeps two equal lanes from giving equal results.

An identifier of up to 64 bytes, nearly all of them, is one chunk. Its whole
hash is a XOR with the length, a XOR with the whitening, two `vaesenc zmm`, the
fold's two lane swaps, and a move to a general register. That is about as many
uops as the rotation, fold and two-round mix it replaced. On the 9700X,
`refine_tokens` measured within noise of the reviewed commit, as medians of
five interleaved rounds: 10.42 → 10.35 cycles/token on `sqlite3.c`, and
18.53 → 18.46 on the C library.
`intern_hash_bytes` computes the same hash 64 bytes at a time, and
`intern_hash_vector` computes it from the masked load of up to 64 bytes.
`test-refinement` checks that all three agree.

### Zen 5 costs that shaped it

These come from uops.info's Zen 5 data, checked against the measurements
above:

- `vpgatherdd zmm` is 64 uops with a throughput of about 12.6 cycles. Nothing
  gathers: per-identifier loads are scalar, and the bucket compare is one
  vector against one cache line.
- `aesenc xmm` is 1 uop with a latency of 3, on FP0 or FP1. Two rounds replaced
  the first version's two 64-bit multiplies; the change was measured together
  with the others in the table's last row.
- A masked 64-byte load is 1 uop, so reading each identifier whole, bounded by
  its length, costs one load whatever the length.
- Also looked up: `vpcmpeqb k, zmm` has a latency of 6, `vpcompressd` to a
  register is 2 uops, `vpextrq` is 2 uops, `vpmullq zmm` is 1 uop, and
  `kmovw r32, k` is 1 uop.

After the last round, the profile on `sqlite3.c` is flat: no instruction takes
more than 1.5% of the samples. With an IPC of 3.7, the kernel is bound by
instruction count: 38.79 instructions per token, about 100 per identifier.

Identifier lengths per occurrence, which set the prefix and the load width:

| Length | `sqlite3.c` | C library |
| --- | ---: | ---: |
| ≤ 11 bytes (found by prefix) | 85.7% | 73.3% |
| ≤ 15 bytes | 92.0% | 82.6% |
| ≤ 27 bytes | 99.6% | 95.4% |
| ≤ 63 bytes (vectorized) | 100% | 99.97% |

## Correctness

`./build/compiler test-refinement [--count N] [--seed N] [path...]` (about 1.3 s
without paths)
compares `refine_tokens`, with every variant of pass B, against
`refine_tokens_scalar`. It also compares both against an oracle of its own: the
oracle removes splices and decodes UCNs in two plain passes, and interns with a
different table (FNV-1a and linear probing). Each path keeps its own table
across inputs, so most identifiers are found rather than added, and every path
must give every identifier the same id and every token the same leading-space
bit. The checks are:

- **hash:** the three hash functions agree on 200,000 spellings of 0 to 200
  bytes.
- **structured hashes:** 88,064 spellings made of repeated, swapped or paired
  parts must all hash differently:
  - `X X` for 8,192 64-byte `X`;
  - a 16-byte lane eight times;
  - `X Y` and `Y X`;
  - `X` two, three and four times;
  - the review's 65,536 32-byte identifiers, whose two lanes change in bits
    that a rotation lines up.
- **aligned flips:** the same bit flipped in two lanes of a chunk must change
  the hash, in about 14,800 random spellings of 17 to 200 bytes. Half of them
  repeat one letter, and in a third the two bytes are equal once whitened.
- **names:** every predefined name has its id and spelling, and every name is a
  keyword in exactly the modes clang makes it one in, in all ten modes from
  `c89` to `gnu23`: 2,080 answers. The modes of the names that depend on them
  (`inline`, `restrict`, `typeof`, `asm`, C23's keywords) are written out mode
  by mode, not computed from years.
- **spellings chosen apart:** 24 spellings picked independently of the
  predefined names, with clang's answer. Seventeen are keywords from each part
  of `TokenKinds.def` that C has, and seven are not: `__int64`, `__declspec`,
  `__is_pod`, `__builtin_expect` and others. So a keyword missing from the list
  fails, which the per-name check can't show.
- **table:** 300,000 random spellings of 1 to 200 bytes, enough to grow the
  table seven times, get ids in first-seen order and the same id again. Absent
  spellings are not found.
- **fixtures:** 22 boundary cases, each at all 64 misalignments from a cache
  line: splices between tokens (with whitespace, `\r\n` and trigraphs),
  comments, spliced identifiers and identifiers written with UCNs, and
  identifiers of 62 to 65 bytes. Each case checks its leading-space bits and
  identifier equalities.
- **spellings:** identifiers of every length from 1 to 130 bytes, 50,570
  inputs. Each has a line splice at every position inside (`\`+newline,
  `\`+`\r\n`, `\` with trailing blanks, and the trigraph `??/`), or `é` or
  `\U000000E9` at every position. Each is followed by the same identifier
  written plainly, and the two must be one identifier.
- **random:** 20,000 inputs of identifiers (spliced, with UCNs, long, and
  fresh random ones), splices, comments, digraphs, pp-numbers and other tokens,
  one in 16 with trigraphs on.
- **page edges:** 9,600 prefixes and suffixes of random inputs that end exactly
  at the end of a readable page, before a `PROT_NONE` page, or start exactly at
  the start of one. A read outside the source faults, and the handler names the
  input.
- **files:** every `.c` and `.h` file under each path.

On the 9700X, with `src` and the C library corpus as paths, every check
passed: 9,009 files and 96,197,420 bytes, 1,022,326 ids in use.
`benchmark-refinement` also checks every file on every path before it times
anything.

## Reproduction

```sh
./build.sh
./build/compiler test-refinement src
taskset -c 2 ./build/compiler benchmark-refinement inputs/sqlite3.c
taskset -c 2 ./build/compiler benchmark-refinement ~/azp-bench/c-tokenizer/corpus/libc
taskset -c 2 ./build/compiler benchmark-refinement src
```

On the 9700X, run each one under `EXCLUSIVE_OWNER=<name>
~/buster-fast-bench/exclusive.sh`. `--only NAME` runs one implementation (for
`perf record`), and `--trigraphs` and `--cplusplus` change the lexer's flags.
`sqlite3.c` is the 3.53 amalgamation, 9,515,341 bytes.

## Next steps

- **Instructions per identifier.** At an IPC of 3.7, the remaining cost is
  about 100 instructions per identifier. Three candidates:
  - Do stage 2's bookkeeping (slot ids to values, the deferred and candidate
    masks) 16 identifiers at a time instead of one at a time. These lines are
    about 18% of the samples.
  - Hash identifiers of up to 16 bytes from their first lane without the fold.
    This is 92% of `sqlite3.c`'s occurrences, and zero lanes fold to nothing.
    It needs a branch-free selection, because lengths are unpredictable.
  - Mix 8 identifiers' folds per vector with `vpmullq`, which is bounded by the
    mix's 5.5% (above).
- **The C library corpus** misses in the L2 cache: its 597,439 spellings make
  16 MB of buckets. Prefetching deeper (two batches ahead) or smaller slots
  would trade instructions for misses; measure both.
- **Interning from several threads** (#150) is `docs/c-files.md`'s: each lane
  refines into a table of its own, and a merge gives the shared table's ids in
  the order of first occurrences, so that they don't depend on the lanes.
- **Named UCNs:** intern them by their code point, which needs Unicode's name
  table, or diagnose them (#116's open question).

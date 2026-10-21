# Token refinement and interning

`src/buster/compiler/c_refine.c` and `src/buster/compiler/c_intern.c` turn the
lexer's output for a file (`c_lexer.c`: kinds, starts and ends) into what the
preprocessor and every later stage read: an `IdentId` for every identifier and
an exact leading-space bit for every token. It runs once per file, however many
translation units include the file. This is #15's stage 1 and roadmap item 1,
tracked in #116. This document records the design, the Zen 5 measurements
behind each choice, and what comes next.

## Results

Measured on 2026-10-21 and 22 on the Ryzen 7 9700X (`benchpress`, clang 23.1.1,
`build.sh`'s `-O3 -march=native`), pinned to CPU 2, each run under the
`exclusive.sh` lock. "Before" is `main` at 0077380, with this design's two
benchmark files, which print the phases' times and take `--after-scalar`;
"after" is this design. The two ran by turns. `benchmark-refinement` takes the
median of 21 interleaved passes. Each pass covers the whole corpus from an
intern table reset to the predefined names, so the table fills up through the
pass as it would in a compiling process. `benchmark-preprocessor`
preprocesses a file from nothing in each of 21 runs: a new table and
preprocessor, with every file read, lexed, refined and indexed, as a compile
of one translation unit does. A range is the rounds' medians: three rounds for
`sqlite3.c` (alone in `benchmark-refinement`, with its headers in
`benchmark-preprocessor`), one for `src` and for `main.c`'s files in
`benchmark-refinement`, and two for the others.

The budget's measure is `benchmark-preprocessor`'s refine phase on `sqlite3.c`
and its headers (1,243,772 tokens):

| | Refine phase | Whole run | Directive tables |
| --- | ---: | ---: | ---: |
| Before | 3.47-3.54 ms | 12.08-12.10 ms | 0.22 ms |
| After | **1.67 ms** | 10.00-10.03 ms | 0.26 ms |

The directive tables' first writes now fault in a huge page of the
preprocessor's arena. The leading-space words used to fault it in during
refinement ("Where the time went"). The lexing and main loop phases also took
less time, 2.42 to 2.30 ms and 5.61 to 5.44 ms; this work didn't analyze why.

On this compiler's `main.c` (1,205,899 tokens from 360 files, the compiler's
sources and clang's intrinsics headers among them), the refine phase went from
3.36-3.40 ms to 2.24 ms, and the whole run from 20.83-21.28 ms to 19.41 ms.
`benchmark-refinement` over the same files takes 1.82 ms, so the phase takes
0.42 ms more than the kernel does warm (on `sqlite3.c` and its headers, 0.12 ms
more). The kernel takes 21.2 cycles per identifier on those files, against 17.5
on `sqlite3.c` and its headers, and more of their identifiers are new
spellings: 7.0%, against 4.4%.

The kernel, `refine_tokens`:

| Corpus | Tokens | Identifiers | Found in the table | Before | After | `refine_tokens_scalar`, after | `c_tokenize` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `sqlite3.c` 3.53 | 1,168,130 | 454,717 | 96.31% | 10.45-10.46 cycles/token, 2.20 ms | **6.45-6.52** cycles/token, 1.36-1.38 ms | 39.60-40.61 | 8.00-8.03 |
| `sqlite3.c` and its headers (170 files) | 1,241,996 | 492,458 | 95.56% | 10.61-10.69, 2.38-2.40 ms | **6.92-6.94**, 1.55-1.56 ms | 40.44-40.66 | 8.19-8.30 |
| `main.c` and its headers (360 files) | 1,203,887 | 476,138 | 92.95% | 12.50, 2.72 ms | **8.37**, 1.82 ms | 37.24 | 6.94 |
| Repository `src` (67 files) | 870,039 | 318,084 | 95.40% | 10.16, 1.60 ms | **6.60**, 1.04 ms | 38.95 | 6.00 |
| C library (Zig 0.14.1 `lib/libc`) | 13,152,141 | 6,115,884 | 90.23% | 18.46-18.54, 43.8-44.0 ms | **11.76-11.77**, 27.9 ms | 42.28-42.63 | 6.74-6.78 |

| Corpus | Instructions/token, before → after | IPC, after | Cycles/identifier, after | Lex, then refine each file (after): what refinement adds |
| --- | ---: | ---: | ---: | ---: |
| `sqlite3.c` | 38.82 → 22.35 | 3.43-3.46 | 16.6-16.8 | 14.41-14.49 cycles/token: 6.4 (10.3-10.4 before) |
| `sqlite3.c` and its headers | 39.71 → 23.31 | 3.36-3.37 | 17.5 | 15.09-15.19: 6.9 (10.5-10.6 before) |
| `main.c` and its headers | 43.76 → 26.76 | 3.20 | 21.2 | 15.09: 8.2 (12.0 before) |
| Repository `src` | 37.95 → 22.77 | 3.45 | 18.1 | 12.48: 6.5 (10.1 before) |
| C library | 51.08 → 30.23 | 2.57 | 25.3 | 17.62-17.67: 10.9 (16.0 before) |

- The vectorized path is 6.2 times as fast as the scalar one on `sqlite3.c`,
  and 3.6 times on the C library. The scalar path took the table's changes
  too: it ran at 41.78-42.22 cycles per token on `sqlite3.c` before. The C
  library has 597,637 distinct spellings, so its table grows four times, to
  262,144 buckets (16 MB) besides the records. Its probes miss the L2 cache,
  and one identifier in ten is new and inserts.
- Rare cases take a scalar path:
  - 2,036 identifiers in the C library are longer than the 63 bytes the masked
    load reads;
  - no identifier in the corpora has a backslash (a line splice or a UCN);
  - 1,073 tokens in the C library (34 in `sqlite3.c`) follow a line splice
    directly, so `refine_gap_is_splices` decides their leading space.

**Both power states.** With `--after-scalar 100`, the scalar path runs for
100 ms before each pass. That is the state after lighter code, in which Zen 5's
power cap slows a heavy AVX-512 loop. In it, `refine_tokens` runs at 6.52-6.57
cycles per token on `sqlite3.c` (10.45-10.55 before) and 11.75-11.77 on the C
library (18.26-18.30 before). Back to back, it runs at 6.45-6.52 and
11.76-11.77. So the cap costs it about 1% at most, as it did the first version.

The variants, after: batches of 16, 32 and 64 take 8.05-8.11, 7.08-7.13 and
6.45-6.52 cycles per token on `sqlite3.c` (15.54-15.70, 13.26-13.30 and
11.76-11.77 on the C library). `refine_tokens_fused`, which probes each
identifier right after hashing it, without a prefetch, takes 7.62-7.69
(14.71-14.72).

### Budget

#115 budgets 1.5 ms for refining and interning `sqlite3.c` and its headers on
the 9700X. Measured, it takes 1.67 ms. Counting the arena page fault that the
directive tables now take, it is about 1.70 ms. That is 2.1 times as fast as
before, and 11 to 13% over the budget. The rest is here:

- **Cold data.** The preprocessor lexes a whole file before it refines it,
  and the table is new in each run. So for `sqlite3.c` the columns, the
  source and the table come from memory. Two diagnostics, not changes, read
  them first at the step "Pass A: the values only when separate, splices out
  of line" (1.97-1.98 ms). Reading the columns and the source saved the
  refine phase 0.08 ms, and reading the table's buckets and spans saved
  0.11 ms. Prefetching the next block and pipelining pass B recovered part of
  that. With everything in the caches, the same 170 files take 1.55 ms in
  `benchmark-refinement`, 0.12 ms less than the phase.
- **The kernel itself.** With everything in the caches, it takes 6.9 cycles
  per token on those files, at an IPC of 3.4. That is 23.3 instructions per
  token. A profile of its instructions by function (`perf record -e
  instructions:u`) puts 77% of them in pass B, about 46 per identifier: the
  hash, the probe, the confirmation of longer spellings, the deferred
  identifiers and the bookkeeping. The scan and the passes over tokens take
  the rest, 5.3 per token. Even warm, it is 3% over the budget.

"Next steps" has what would close it: refinement fed a block at a time by the
lexer (a change to `c_tokenize`'s interface), and a table kept across
translation units (#152).

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
  and put through two AES rounds, which hashes identifiers from the loads that
  read them, four at a time.
  Whether the link representation's names adopt it, now that its runtime
  snapshot would make the function part of a file format, is #84's question.

## The vectorized path

`refine_tokens` works on blocks of `BUSTER_REFINE_BLOCK_TOKENS` (1,024) tokens.
Before a block, `refine_scan_chunks` marks its source bytes' 64-byte chunks that
hold a backslash. Then come three passes: A over the block's tokens, B over its
identifiers, and C over its tokens again. Each block's dense identifier arrays
stay in the L1 cache.

### The backslash chunks

A gap made only of line splices needs a backslash (with trigraphs on, a `?`
too, since `??/` is a backslash). `refine_scan_chunks` marks the chunks that
hold one, a bit per chunk, 4 KB of the source per word. A run of 8 chunks is
tested whole, by the least of its bytes XORed with `'\\'`, and only a run that
has one is tested chunk by chunk (5% of `sqlite3.c`'s 512-byte runs). Each block
scans its own bytes, just before its passes, which then find them in the
caches. The first version scanned the whole file first: that read the source
from memory a second time, at 34 instructions per chunk, 0.45 ms of the
preprocessor's 3.5 ms refine phase on `sqlite3.c`.

### Pass A: gaps, leading spaces and the identifiers

Pass A works on 64 tokens at a time: one vector of kinds and four of starts and
ends.

- The gap bits come from one compare per 16 tokens, `starts[i] !=
  ends[i - 1]`, with `valignd` supplying the previous token's end across the
  vector boundary.
- The identifiers' starts and lengths are compressed (`vpcompressd`) into dense
  arrays for pass B, and each step's identifier mask is kept for pass C.
- A step whose gaps lie entirely in unmarked chunks is done after the compare.
  The others go to `refine_step_splices`. There a gap can only be splices if
  the token doesn't start a line and the byte before it is a newline: a
  newline outside a comment starts a logical line, and a block comment ends
  with `*/`. The bytes before 16 starts come from a 128-byte window of the
  source with one `vpermi2b`, and the few tokens that qualify have the scalar
  `refine_gap_is_splices` decide their bit.
- Pass A writes `values` only when they aren't the ends (in place, as the
  preprocessor refines, a non-identifier's value is its end already).
- Each step prefetches the next block's step of the three columns, and 512
  bytes of the source past what the scan has read. The preprocessor lexes a
  whole file before refining it, so for `sqlite3.c` the columns (10.5 MB) and
  the source (9.5 MB) come back from memory; pass B's work hides their fetch.

### Pass B: interning in batches

`refine_pass_b` interns a block's identifiers in batches of 64, in three
stages, pipelined: a batch's stage 1 runs in the middle of the batch before's
stages 2 to 3.

1. **Load and hash** (`refine_hash_batch`). Each identifier's first 32 bytes
   are read with one masked load, bounded by its length (`bzhi`), so the load
   never reads outside the token. Four identifiers are hashed at a time
   (`intern_hash_vectors4_short`, below), and their buckets prefetched, with
   the offsets taken from the vector's lanes. The rare identifiers of 33 to 63
   bytes (0.1% of `sqlite3.c`'s) are hashed again from all their bytes. The
   batch's loads are checked for a backslash (or with trigraphs a `?`) with a
   vector minimum of the bytes XORed with `'\\'`, on the two vectors that hold
   four identifiers' bytes. A
   batch that has one (no batch in the C corpora does) checks each identifier
   (`refine_batch_specials`), and those with one, and those past
   `BUSTER_INTERN_FAST_LENGTH` (63 bytes), go to the scalar key.
2. **Probe** (`refine_probe`). One compare of each bucket's four slots against
   the identifier's first 12 bytes, zero-padded, finds the slots with its
   prefix. A spelling of up to 11 bytes is identified by its prefix alone,
   because its zero padding includes the zero at its length, and identifiers
   have no NUL bytes. This covers 86% of identifier occurrences in `sqlite3.c`
   and 73% in the C library. The key's last dword is all ones, which no id is
   (ids are below 2^28), so the compare of all 16 dwords needs no mask. The id
   is selected with a `cmov` after its load: Clang turns a plain `?:` there into
   a branch around the load, which mispredicts for every new spelling. Four
   identifiers are probed per iteration. The bookkeeping (which identifiers are
   slow, unmatched or longer than 11 bytes) is done 16 identifiers at a time
   from their lengths and ids, as bits of masks.
3. **2b, confirm.** A longer spelling whose prefix matched is confirmed with one
   masked compare of its bytes and terminating zero against its record. Its two
   loads depend on each other (the id's span, then the record), so 2b
   prefetches every candidate's record from its span, then runs the next
   batch's stage 1, then compares. Skipping 2b altogether (a wrong answer for
   31 of 57,354 candidates) measured 0.18 ms of the refine phase before this
   split.
4. **Deferred.** Nothing above branches on an identifier's outcome. An
   identifier that isn't found this way (a new spelling, a repeat of one in the
   same batch, a second candidate, a probe past the first bucket, or the scalar
   path) is deferred, and the deferred ones are interned in token order, so new
   spellings get exactly the ids `refine_tokens_scalar` gives them.
   `intern_find_or_add_vector` interns one from one masked load: its key, the
   record's compare and its write all come from that vector, and its probe
   starts at stage 1's bucket (hashing again only if an insert has grown the
   table since). A stage 3 that grows the table makes the next batch, hashed
   for the table as it was, hash again.

Separating the stages lets a batch's 64 cache misses overlap. The variants
`benchmark-refinement` compares measure this directly: batches of 16, 32 and
64, and `refine_tokens_fused`, which probes each identifier right after hashing
it, without a prefetch.

### Pass C: the ids into the values

Pass B leaves the block's ids in a dense array. Pass C stores them into the
identifiers' values, 16 tokens at a time: an expand (`vpexpandd`) of the next
ids by the identifier mask, and a masked store. The first version stored each id
at its token through an array of token indices, which pass A compressed: that
cost a compress and a store per 16 tokens and a load and a store per
identifier.

### What is per identifier, and what could be across identifiers

Both passes over tokens are data-parallel, as the lexer is: 64 tokens per step,
with no branch per token but the rare splice check. Pass B is data-parallel over
the identifiers of a batch: the hash runs on four identifiers per vector (their
lanes transposed), the probe compares a bucket per identifier with one
instruction, and the bookkeeping runs on 16 identifiers per vector. Only stage 3
is ordered: it gives new spellings their ids in token order, which keeps the ids
the same as the scalar twin's.

### The intern table

The table (`c_intern.c`) uses open addressing over 64-byte buckets of four
16-byte slots. Each slot holds the first 12 bytes of its spelling, zero past
the spelling's end, and the spelling's id. Spellings and a terminating NUL live
in a record arena, and each id's span (`InternSpan`: its record's offset and
its length) says where. Buckets are probed in order from the hash's bucket.
Spellings are never removed, so a key is absent once a probe meets a bucket
with an empty slot. The table grows (doubles) when its ids reach 5/8 of its
slots.

What a fresh table costs is mostly memory the kernel clears. `lane.c`'s arenas
use huge pages past their first 64 KB, and the kernel clears a whole 2 MB page
on its first touch: in the preprocessor, where the caches are full of a file's
tokens, about 100 µs a page. The first table touched five of them for
`sqlite3.c` and its headers: the buckets, the record offsets, the lengths, the
records, and the buckets again when it outgrew its 8,192 (20,480 spellings; it
needs 24,000). Three of those were during refinement. Now:

- the table starts at 16,384 buckets, room for 40,960 spellings, in the same
  huge page as 512 KB would take (past the arena's first 64 KB);
- an id's offset and length are one span, and the spans of the ids the table
  holds before it grows are in the buckets' mapping, past them;
- the first buckets and spans are at the start of the record arena, before the
  records. With the first 700 KB of records, the whole table for a translation
  unit of up to 40,960 spellings takes the arena's first 64 KB, in base pages,
  and one huge page, which the predefined names touch. A growth moves the
  buckets and spans to a mapping of their own; the records never move, so a
  pointer to one (`intern_bytes`) stays valid.

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

(Those were measured on 2026-10-20, before the round "Where the time went"
describes.)

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

Four forms compute it, which `test-refinement` checks against each other:
`intern_hash_bytes` 64 bytes at a time, `intern_hash_vector` from one masked
load of up to 64 bytes, and `intern_hash_vectors4_short` for four spellings of
up to 32 bytes at once, which pass B uses. A spelling that short has zero lanes
2 and 3, whose rounds give the same constant for every spelling
(`intern_hash_zero_lanes`). So the four spellings' first two lanes are
transposed (two lane shuffles: one vector holds the four first lanes, another
the four second ones), each vector is whitened and goes through its two rounds,
and the four folds are one three-way XOR with that constant. That is 13
instructions for four identifiers, where the one-at-a-time form took 10 for each
(a whitening, two rounds of the whole chunk, the fold's two lane swaps and two
XORs, and a move to a general register).

The hash is not `link_hash`, and nothing pins its values: the link
representation's names and the runtime snapshot use `link_hash`, which this
work leaves alone. Whether those adopt this one is #84's question.

### Zen 5 costs that shaped it

These come from uops.info's Zen 5 data and the 9700X's counters, checked
against the measurements above and below:

- `vpgatherdd zmm` is 64 uops with a throughput of about 12.6 cycles. Nothing
  gathers: per-identifier loads are scalar, and the bucket compare is one
  vector against one cache line.
- `aesenc xmm` is 1 uop with a latency of 3, on FP0 or FP1. The four-at-a-time
  hash runs four rounds of whole vectors for four identifiers, where the
  one-at-a-time form ran two per identifier.
- A masked load is 1 uop, so reading each identifier whole, bounded by its
  length, costs one load whatever the length.
- `vpcompressd` and `vpexpandd` to and from a register are 2 uops each, and
  `vpermi2b` is 1.
- The kernel clears a huge page on its first touch. `benchmark-lanes` measures
  15 µs for 2 MB with the caches idle; in the preprocessor, whose caches hold a
  file's tokens, `clear_page_erms` ran at about 20 GB/s, 100 µs a page.
- Stage 1's software prefetches mostly find their line in L1 already: of 10.8
  million in a run of `benchmark-refinement` on `sqlite3.c`, 8.9 million were
  `ls_inef_sw_pref`, and 1.9 million fetched from L2 or L3. Without them the
  preprocessor's refine phase is still 4% slower, because its table is cold.
- A 64-byte vector store reloaded as four 8-byte loads didn't measurably stall
  (the prefetch offsets' two ways measured 6.63 and 6.60 cycles per token).
- Clang turns `matches != 0 ? id : 0` into a branch around the id's load, and
  keeps it with `__builtin_unpredictable`. An empty `asm` on the loaded id and
  a mask instead of the `?:` give a `cmov`.

Identifier lengths per occurrence, which set the prefix and the loads' widths:

| Length | `sqlite3.c` | C library |
| --- | ---: | ---: |
| ≤ 11 bytes (found by prefix) | 85.7% | 73.3% |
| ≤ 15 bytes | 92.0% | 82.6% |
| ≤ 27 bytes | 99.6% | 95.4% |
| ≤ 32 bytes (hashed from two lanes) | 99.9% | 97% |
| ≤ 63 bytes (vectorized) | 100% | 99.97% |

## Where the time went

The starting point (`main` at 0077380, 2026-10-21): `benchmark-preprocessor`'s
refine phase took 3.48 to 3.52 ms on `sqlite3.c` and its headers, and
`benchmark-refinement`'s `refine_tokens` 10.35 to 10.41 cycles per token on
`sqlite3.c` alone. The 9700X profile of the preprocessor (DWARF call graphs,
20 kHz) put 26% of the run's samples under `refine_tokens`, about 3.2 ms:

- pass B, 1.84 ms: stages 1 and 2 took about 58 instructions per identifier
  (the bookkeeping one identifier at a time, a 64-byte hash per identifier,
  spilled pointers), at an IPC of 3.7;
- the whole-file backslash scan, 0.45 ms: it streamed the source from memory
  a second time, at 34 instructions per 64-byte chunk;
- pass A, 0.36 ms;
- the deferred identifiers, 0.54 ms, 0.3 ms of it the kernel clearing fresh
  huge pages: the table outgrew its first buckets (a new mapping, and every
  spelling placed again), and the records, the record offsets and the new
  buckets each faulted in a huge page during refinement. With 8,192 buckets
  and a load near 5/8, a fifth more identifiers were deferred than with
  16,384 (41,700 against 33,000 of 493,000).

Each step below was measured against the one before it, on the 9700X, by
`EXCLUSIVE_OWNER=... ~/buster-fast-bench/exclusive.sh taskset -c 2`, one run
per lock, two or three interleaved rounds of `benchmark-refinement --only
refine_tokens_64 sqlite3.c` (21 passes each) and `benchmark-preprocessor --only
vectorized sqlite3.c` (21 passes each). The ranges are the rounds' medians.

| Step | Refine phase (ms) | `refine_tokens` (cycles/token) | Instructions/token |
| --- | ---: | ---: | ---: |
| `main` at 0077380 | 3.48-3.52 | 10.35-10.41 | 38.82 |
| Backslash chunks per block, pass A without token indices, pass B's bookkeeping 16 at a time, pass C | 3.03-3.07 | 8.51-8.62 | 27.26 |
| Pass B a function per variant (no spills in its loops); the probe's id by a `cmov` | 2.76-2.81 | 7.74-7.81 | 28.48 |
| 16,384 buckets from the start | 2.49-2.51 | 7.28-7.33 | 27.87 |
| Spans; the buckets and spans in one mapping | 2.21-2.23 | 6.97-7.06 | 27.22 |
| The first buckets and spans at the start of the record arena | 2.05-2.07 | 6.98-7.03 | 27.22 |
| The backslash scan 512 bytes at a time | 2.01-2.05 | 6.93-7.03 | 26.76 |
| Four identifiers hashed at a time (all four lanes) | 2.00-2.01 | 6.81-6.82 | 25.35 |
| Pass A: the values only when separate, splices out of line | 1.97-1.98 | 6.63-6.64 | 23.97 |
| Stage 3's probe from stage 1's bucket | 1.93-1.96 | 6.48-6.58 | 23.92 |
| Identifiers up to 32 bytes hashed from two lanes | 1.83-1.87 | 6.15-6.23 | 22.86 |
| Stage 2 four per iteration | 1.84-1.87 | 6.13-6.19 | 23.07 |
| The next block's columns and source prefetched | 1.74-1.81 | 6.26-6.32 | 23.89 |
| Pass B pipelined | 1.68-1.72 | 6.35-6.42 | 22.35 |
| The leading-space words after the kinds | 1.66 | 6.36-6.39 | 22.35 |

`benchmark-refinement`'s columns, table and source stay in the caches from pass
to pass, which the preprocessor's don't, so the two last but one steps cost it
1 to 2% while they save the preprocessor 4 to 7%; the preprocessor's refine
phase is the measure this work answers to. The last step only moved its 0.03 ms:
the directive tables' first writes now fault in the page of the preprocessor's
arena that the leading-space words did (that phase went from 0.22 to 0.26 ms),
and the whole run stayed the same (10.06-10.07 against 10.08-10.12 ms). It is
kept because the words now take no page of their own, but counted the old way
the refine phase is about 1.70 ms.

## What didn't pay

- **A probe cache.** 1,024 direct-mapped entries of a 16-byte key and an id,
  probed for every identifier before the table (one compare and a select),
  with only the misses hashed and probed: simulated, they would answer 84% of
  `sqlite3.c`'s identifiers and 74% of the C library's. Measured, 3% fewer
  instructions (23.92 to 23.16 per token) but 9% slower (6.48-6.54 to
  7.06-7.11 cycles per token, refine phase 1.92-1.94 to 2.11-2.12 ms, IPC 3.69
  to 3.26). The table's hot buckets are in the L1 cache already (82% of stage
  1's prefetches find their line there), the cache's 32 KB compete with them,
  and the misses' second pass (compressed, hashed, probed and filled) costs
  what the hits save.
- **Stage 3 without a branch between "found" and "insert"** (the home bucket's
  match or empty slot, masked stores): 3.5% more instructions, and within
  noise to 2% slower (refine phase 1.94 to 1.96 ms).
- **Stage 3's find-or-add inlined** into pass B: within noise.
- **No prefetch** in stage 1: the same in `benchmark-refinement`, 4% slower in
  the preprocessor (1.93-1.95 to 2.02-2.05 ms), where the table is cold.
- **The prefetches' offsets from the vector's lanes** instead of loads from the
  64-byte store just made: 6.63 to 6.60 cycles per token, the refine phase the
  same. The loads didn't wait for the store as feared; the extraction is kept
  for its fewer loads.
- **Only the columns prefetched**, not the source: the refine phase 4% slower
  in the one round that ran (1.72 against 1.66 ms).

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

- **hash:** the four forms of the hash agree on 200,000 spellings of 0 to 200
  bytes: `intern_hash_bytes`, `intern_hash_vector` up to 64 bytes and
  `intern_hash_vectors4_short` up to 32, four at a time (with the four first
  lanes it returns), against `intern_hash_bytes_scalar`.
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
  table three times, get ids in first-seen order and the same id again. Absent
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
passed. `benchmark-refinement` also checks every file on every path before it
times anything, and `test-preprocessor` and the preprocessor's oracle runs
(`docs/c-preprocessor.md`) run on what refinement gives. The C library corpus's
table grows four times, from 16,384 buckets to 262,144, and each growth in a
batch's stage 3 makes the batch after it hash again.

## Reproduction

```sh
./build.sh
./build/compiler test-refinement src
taskset -c 2 ./build/compiler benchmark-refinement inputs/sqlite3.c
taskset -c 2 ./build/compiler benchmark-refinement --after-scalar 100 --only refine_tokens_64 inputs/sqlite3.c
taskset -c 2 ./build/compiler benchmark-refinement ~/azp-bench/c-tokenizer/corpus/libc
taskset -c 2 ./build/compiler benchmark-refinement src
taskset -c 2 ./build/compiler benchmark-refinement $(clang -M inputs/sqlite3.c | tr ' \\' '\n\n' | grep -E '\.(c|h)$' | sort -u)
taskset -c 2 ./build/compiler benchmark-refinement $(clang -M -march=native -Isrc -fwrapv -funsigned-char src/buster/compiler/main.c | tr ' \\' '\n\n' | grep -E '\.(c|h)$' | sort -u)
taskset -c 2 ./build/compiler benchmark-preprocessor --only vectorized inputs/sqlite3.c
clang -dM -E -march=native -fwrapv -funsigned-char -x c /dev/null > build/predefines.h
taskset -c 2 ./build/compiler benchmark-preprocessor --only vectorized -march=native -Isrc -fwrapv -funsigned-char --predefines build/predefines.h src/buster/compiler/main.c
```

On the 9700X, run each one under `EXCLUSIVE_OWNER=<name>
~/buster-fast-bench/exclusive.sh`. `--only NAME` runs one implementation (for
`perf record`, and for its passes back to back), `--after-scalar MS` runs the
scalar path before each timed pass of a vectorized one, and `--trigraphs` and
`--cplusplus` change the lexer's flags. `sqlite3.c` is the 3.53 amalgamation,
9,515,341 bytes. "`sqlite3.c` and its headers" are the 170 files `clang -M`
lists for it, which the preprocessor reads, and "`main.c` and its headers" are
the 360 files it lists for `main.c` with the compiler's options.

## Next steps

- **Refine a file as it is lexed.** The preprocessor lexes a whole file, then
  refines it, so for `sqlite3.c` the columns and the source come back from
  memory and the table has gone cold: reading them first (a diagnostic, not a
  change) saved 0.08 ms and 0.11 ms of the refine phase. A lexer that hands
  over its tokens a block at a time would let refinement read them, and keep
  the table, in the caches. That is a change to `c_tokenize`'s interface, and
  so to `c_lexer.c`.
- **The table across translation units** (#152): a process that compiles many
  of them keeps the table and its pages, warm, and pays its first inserts once.
- **Fewer candidates for 2b.** Three slots of 20 bytes (a 16-byte prefix) would
  identify spellings of up to 15 bytes by their prefix (92% of `sqlite3.c`'s,
  83% of the C library's, against 86% and 73%), for two more instructions per
  probe and a fourth fewer slots per bucket.
- **Interning from several threads** (#150): per-file refinement in parallel,
  with ids from a shared table or a merge that keeps them independent of the
  schedule.
- **Named UCNs:** intern them by their code point, which needs Unicode's name
  table, or diagnose them (#116's open question).

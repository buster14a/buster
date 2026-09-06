# PR139 SIMD kernel check on the available Zen 4-class VM

Source: `a8454d842687419ff5023755fc0dd20243119088`. Host: AMD EPYC 9V74,
family 25/model 17, under KVM, AVX-512 F/BW/VBMI/VBMI2 exposed. Clang 21.1.8,
`-O3 -march=native -fwrapv -fno-strict-aliasing -funsigned-char`; CPU 0 pinned.
Seven alternating scalar/SIMD pairs, 64 complete corpus sweeps per arm.
No concurrent agent build/test workloads during timing. Hardware PMU events
are unavailable, so these are provisional native elapsed times, not retired
instructions, measured cycles, bare-metal Zen 4 results, or Zen 5 projections.

The final packaged harness passed **1,105,026 checks, zero failures**. Its
production `c_lex` corpus contains all 160 library C/H files: 29,224,146 source
bytes, 3,308,365 tokens, 699,056 identifiers (21.13%), and 23,828 narrow literal
spellings containing 12,568,044 bytes. All 957 x86 metadata base64 chunks contain
3,892,388 encoded bytes. This physical-source corpus is not the stage-1 trace;
it includes inactive text and differs in include multiplicity and file closure.

| Kernel | Scalar median | SIMD median | Scalar / SIMD |
| --- | ---: | ---: | ---: |
| Base64 | 0.40862 ns/encoded byte | 0.03347 ns/encoded byte | 12.208x |
| Quoted literals | 0.65897 ns/spelling byte | 0.21931 ns/spelling byte | 3.005x |
| Identifier selection | 1.31554 ns/token | 0.37734 ns/token | 3.486x |

Quoted decoding reuses its output buffer, excluding arena allocation. The
identifier loop is the exact production loop, with a direct ordered-index
recorder replacing the hash/interner operation; both index order and token
symbol writes are differentially checked. Its speedup is for that selection
plus recorder, not full symbol interning. Decoder entry points are noinline
to prevent loop folding; their helpers retain ordinary optimization.

The negative synthetic populations must travel with the aggregate results:

- Base64 chunks of 4 and 8 characters make SIMD **1.875x and 1.396x slower**.
  Sixteen characters are near break-even (1.036x scalar/SIMD in the final
  run, 0.970x in the earlier exploratory run); no robust crossover claim.
- A 4-byte plain literal body makes SIMD **1.451x slower**. Empty literals
  differ by about 11%, but the entire call is only about 3 ns and should not
  support a compiler policy decision alone.
- A 128-byte literal body consisting solely of `\n` escapes makes SIMD
  **6.766x slower** (0.57490 versus 3.88981 ns/spelling byte). There is almost
  no literal prefix to copy, so each escape pays a new masked load, comparison,
  zero-prefix store instruction, and scalar escape call. This is the clearest
  follow-up population for a measured dense-escape strategy.
- At 100% identifiers, the sidecar path is **3.6% slower** in the final run;
  it has no rows to skip. At 0%/1%/20% identifiers, ratios are
  15.063x/7.795x/1.331x respectively. Distribution matters.

Generated assembly was inspected. The base64 loop retains its two table halves
in vector registers, processes 64 input bytes with `vpcmpnltb`, `vpermi2b`, two
`vpmultishiftqb`, `vpternlogq`, `vpermb`, and masked load/store, producing 48
output bytes. No vector spills appear. The quoted loop is masked load,
`vpcmpeqb`, masked store, with an out-of-line scalar escape call and scalar
index/count stack traffic. The identifier loop is masked load plus byte
comparison, followed by `tzcnt`/`blsr` visits; Clang unrolls its outer window
loop twice and the scalar row loop four times. No vector spills appear.

An existing implementation gap remains: the base64 kernel is written using
`__m512i`/`immintrin.h`; `simd.h` has no multishift operation. This round checks
the shipped code without rewriting it or claiming that it already satisfies
the newer vocabulary rule. Literal and identifier paths use `simd.h`.

The attached `provenance.json`, `manifest.json`, `results.json`, `check.log`,
`timing.log`, assembly dumps and `regimes/*.log` refer to the final packaged
harness. Generated corpus files and binaries are intentionally not proposed for
tracking. Earlier exploratory logs are superseded by these final results.

## 9700X direct workload run

head `a48bb26f642fb192050d54a11abf1f5c87f7f7b7`, base `2913d22246cc493ec7e8923c18d085b1f013575a`, CPU 2, 2 warmups and 9 samples, 10 s limit per run.

Observed host: `AMD Ryzen 7 9700X 8-Core Processor`.

Diagnostic process latency (fork through wait4). No service lease is held; other host activity is not excluded.

progress:     0.006s PLAN 4 workloads, 2 warmups and 9 samples each: benchmarks/9700x/tls-companion-index-t32768.c, benchmarks/9700x/tls-companion-index-t8192.c, benchmarks/9700x/tls-companion-scan-t32768.c, benchmarks/9700x/tls-companion-scan-t8192.c
progress:     0.011s START benchmarks/9700x/tls-companion-index-t32768.c: preparing the run directory
progress:     0.016s START benchmarks/9700x/tls-companion-index-t32768.c: staging the source
progress:     0.022s START benchmarks/9700x/tls-companion-index-t32768.c: compilation
progress:     0.091s START benchmarks/9700x/tls-companion-index-t32768.c: reading the input data
progress:     0.098s START benchmarks/9700x/tls-companion-index-t32768.c: hashing the executable
progress:     0.104s START benchmarks/9700x/tls-companion-index-t32768.c: measurement
progress:     0.114s RUN benchmarks/9700x/tls-companion-index-t32768.c 1 of 11: exit 0, timed out 0, wall 2.760 ms
progress:     0.118s RUN benchmarks/9700x/tls-companion-index-t32768.c 2 of 11: exit 0, timed out 0, wall 2.222 ms
progress:     0.123s RUN benchmarks/9700x/tls-companion-index-t32768.c 3 of 11: exit 0, timed out 0, wall 3.634 ms
progress:     0.130s RUN benchmarks/9700x/tls-companion-index-t32768.c 4 of 11: exit 0, timed out 0, wall 5.609 ms
progress:     0.135s RUN benchmarks/9700x/tls-companion-index-t32768.c 5 of 11: exit 0, timed out 0, wall 3.589 ms
progress:     0.140s RUN benchmarks/9700x/tls-companion-index-t32768.c 6 of 11: exit 0, timed out 0, wall 3.298 ms
progress:     0.146s RUN benchmarks/9700x/tls-companion-index-t32768.c 7 of 11: exit 0, timed out 0, wall 4.994 ms
progress:     0.152s RUN benchmarks/9700x/tls-companion-index-t32768.c 8 of 11: exit 0, timed out 0, wall 4.182 ms
progress:     0.156s RUN benchmarks/9700x/tls-companion-index-t32768.c 9 of 11: exit 0, timed out 0, wall 3.699 ms
progress:     0.160s RUN benchmarks/9700x/tls-companion-index-t32768.c 10 of 11: exit 0, timed out 0, wall 2.454 ms
progress:     0.164s RUN benchmarks/9700x/tls-companion-index-t32768.c 11 of 11: exit 0, timed out 0, wall 2.024 ms
progress:     0.164s START benchmarks/9700x/tls-companion-index-t32768.c: reporting
progress:     0.169s DONE benchmarks/9700x/tls-companion-index-t32768.c: valid, 11 runs
### `benchmarks/9700x/tls-companion-index-t32768.c`

Validity: all 11 planned runs completed and were valid.

source sha256 `d18385e77cad0e881c0ed124d3f3d54e5558778394129d43f663996e9d849487`, executable sha256 `d3d6b84d18c283dc97bd4bf3ae3667ce9b69207f8345130ec4c82ec7729ae87f`, `input.data` sha256 `c12108faab4f9d3333be302e2f2e5f65793ae9126d62e699e3a9d27e3c5ebd2e`

Wall over 9 measured runs: median 3.634 ms, min 2.024 ms, max 5.609 ms. CPU median 1.974 ms. Peak RSS 36098048 bytes.

| run | exit | timed out | wall ms | cpu ms | rss bytes | output sha256 |
|---|---|---|---|---|---|---|
| warmup 0 | 0 | 0 | 2.760 | 2.089 | 34787328 | `53c14fa4cc058dc4` |
| warmup 1 | 0 | 0 | 2.222 | 2.005 | 34803712 | `53c14fa4cc058dc4` |
| sample 0 | 0 | 0 | 3.634 | 1.974 | 36098048 | `53c14fa4cc058dc4` |
| sample 1 | 0 | 0 | 5.609 | 2.018 | 36098048 | `53c14fa4cc058dc4` |
| sample 2 | 0 | 0 | 3.589 | 1.964 | 36098048 | `53c14fa4cc058dc4` |
| sample 3 | 0 | 0 | 3.298 | 1.909 | 36098048 | `53c14fa4cc058dc4` |
| sample 4 | 0 | 0 | 4.994 | 2.018 | 36098048 | `53c14fa4cc058dc4` |
| sample 5 | 0 | 0 | 4.182 | 1.991 | 36098048 | `53c14fa4cc058dc4` |
| sample 6 | 0 | 0 | 3.699 | 1.869 | 36098048 | `53c14fa4cc058dc4` |
| sample 7 | 0 | 0 | 2.454 | 2.214 | 36012032 | `53c14fa4cc058dc4` |
| sample 8 | 0 | 0 | 2.024 | 1.804 | 36098048 | `53c14fa4cc058dc4` |

All 11 runs printed identical output:

every run:

```text
TLS_COMPANION method=index rows=65537 sites=32768 helper_references=32768 relaxed=32768 only_relaxed=1 digest=41f1f44027d02325 self_check=pass
```

progress:     0.180s START benchmarks/9700x/tls-companion-index-t8192.c: preparing the run directory
progress:     0.185s START benchmarks/9700x/tls-companion-index-t8192.c: staging the source
progress:     0.191s START benchmarks/9700x/tls-companion-index-t8192.c: compilation
progress:     0.260s START benchmarks/9700x/tls-companion-index-t8192.c: reading the input data
progress:     0.266s START benchmarks/9700x/tls-companion-index-t8192.c: hashing the executable
progress:     0.272s START benchmarks/9700x/tls-companion-index-t8192.c: measurement
progress:     0.280s RUN benchmarks/9700x/tls-companion-index-t8192.c 1 of 11: exit 0, timed out 0, wall 1.468 ms
progress:     0.282s RUN benchmarks/9700x/tls-companion-index-t8192.c 2 of 11: exit 0, timed out 0, wall 1.350 ms
progress:     0.283s RUN benchmarks/9700x/tls-companion-index-t8192.c 3 of 11: exit 0, timed out 0, wall 0.891 ms
progress:     0.285s RUN benchmarks/9700x/tls-companion-index-t8192.c 4 of 11: exit 0, timed out 0, wall 0.913 ms
progress:     0.287s RUN benchmarks/9700x/tls-companion-index-t8192.c 5 of 11: exit 0, timed out 0, wall 0.864 ms
progress:     0.288s RUN benchmarks/9700x/tls-companion-index-t8192.c 6 of 11: exit 0, timed out 0, wall 0.988 ms
progress:     0.290s RUN benchmarks/9700x/tls-companion-index-t8192.c 7 of 11: exit 0, timed out 0, wall 0.831 ms
progress:     0.291s RUN benchmarks/9700x/tls-companion-index-t8192.c 8 of 11: exit 0, timed out 0, wall 0.831 ms
progress:     0.293s RUN benchmarks/9700x/tls-companion-index-t8192.c 9 of 11: exit 0, timed out 0, wall 0.823 ms
progress:     0.294s RUN benchmarks/9700x/tls-companion-index-t8192.c 10 of 11: exit 0, timed out 0, wall 0.849 ms
progress:     0.296s RUN benchmarks/9700x/tls-companion-index-t8192.c 11 of 11: exit 0, timed out 0, wall 1.125 ms
progress:     0.297s START benchmarks/9700x/tls-companion-index-t8192.c: reporting
progress:     0.302s DONE benchmarks/9700x/tls-companion-index-t8192.c: valid, 11 runs
### `benchmarks/9700x/tls-companion-index-t8192.c`

Validity: all 11 planned runs completed and were valid.

source sha256 `d18385e77cad0e881c0ed124d3f3d54e5558778394129d43f663996e9d849487`, executable sha256 `2082ab393357cb7abf9311951349f4c3d20ebbcad2a0a9713207ec74f1459345`, `input.data` sha256 `a8cbf8ee3969796b2f9c602a99a2a3da4f7c458be4be49749dd595aac76df065`

Wall over 9 measured runs: median 0.864 ms, min 0.823 ms, max 1.125 ms. CPU median 0.676 ms. Peak RSS 36098048 bytes.

| run | exit | timed out | wall ms | cpu ms | rss bytes | output sha256 |
|---|---|---|---|---|---|---|
| warmup 0 | 0 | 0 | 1.468 | 0.978 | 36098048 | `dc225d412103fb10` |
| warmup 1 | 0 | 0 | 1.350 | 1.112 | 36098048 | `dc225d412103fb10` |
| sample 0 | 0 | 0 | 0.891 | 0.688 | 36098048 | `dc225d412103fb10` |
| sample 1 | 0 | 0 | 0.913 | 0.715 | 36012032 | `dc225d412103fb10` |
| sample 2 | 0 | 0 | 0.864 | 0.676 | 36098048 | `dc225d412103fb10` |
| sample 3 | 0 | 0 | 0.988 | 0.788 | 36098048 | `dc225d412103fb10` |
| sample 4 | 0 | 0 | 0.831 | 0.654 | 36098048 | `dc225d412103fb10` |
| sample 5 | 0 | 0 | 0.831 | 0.661 | 36098048 | `dc225d412103fb10` |
| sample 6 | 0 | 0 | 0.823 | 0.651 | 36098048 | `dc225d412103fb10` |
| sample 7 | 0 | 0 | 0.849 | 0.673 | 36098048 | `dc225d412103fb10` |
| sample 8 | 0 | 0 | 1.125 | 0.730 | 36098048 | `dc225d412103fb10` |

All 11 runs printed identical output:

every run:

```text
TLS_COMPANION method=index rows=16385 sites=8192 helper_references=8192 relaxed=8192 only_relaxed=1 digest=6b8046173263a325 self_check=pass
```

progress:     0.313s START benchmarks/9700x/tls-companion-scan-t32768.c: preparing the run directory
progress:     0.318s START benchmarks/9700x/tls-companion-scan-t32768.c: staging the source
progress:     0.324s START benchmarks/9700x/tls-companion-scan-t32768.c: compilation
progress:     0.392s START benchmarks/9700x/tls-companion-scan-t32768.c: reading the input data
progress:     0.399s START benchmarks/9700x/tls-companion-scan-t32768.c: hashing the executable
progress:     0.405s START benchmarks/9700x/tls-companion-scan-t32768.c: measurement
progress:     1.022s RUN benchmarks/9700x/tls-companion-scan-t32768.c 1 of 11: exit 0, timed out 0, wall 610.726 ms
progress:     1.631s RUN benchmarks/9700x/tls-companion-scan-t32768.c 2 of 11: exit 0, timed out 0, wall 607.501 ms
progress:     2.246s RUN benchmarks/9700x/tls-companion-scan-t32768.c 3 of 11: exit 0, timed out 0, wall 613.064 ms
progress:     2.853s RUN benchmarks/9700x/tls-companion-scan-t32768.c 4 of 11: exit 0, timed out 0, wall 605.594 ms
progress:     3.471s RUN benchmarks/9700x/tls-companion-scan-t32768.c 5 of 11: exit 0, timed out 0, wall 616.238 ms
progress:     4.087s RUN benchmarks/9700x/tls-companion-scan-t32768.c 6 of 11: exit 0, timed out 0, wall 615.056 ms
progress:     4.711s RUN benchmarks/9700x/tls-companion-scan-t32768.c 7 of 11: exit 0, timed out 0, wall 622.055 ms
progress:     5.330s RUN benchmarks/9700x/tls-companion-scan-t32768.c 8 of 11: exit 0, timed out 0, wall 618.097 ms
progress:     5.947s RUN benchmarks/9700x/tls-companion-scan-t32768.c 9 of 11: exit 0, timed out 0, wall 614.862 ms
progress:     6.566s RUN benchmarks/9700x/tls-companion-scan-t32768.c 10 of 11: exit 0, timed out 0, wall 617.864 ms
progress:     7.183s RUN benchmarks/9700x/tls-companion-scan-t32768.c 11 of 11: exit 0, timed out 0, wall 615.301 ms
progress:     7.183s START benchmarks/9700x/tls-companion-scan-t32768.c: reporting
progress:     7.206s DONE benchmarks/9700x/tls-companion-scan-t32768.c: valid, 11 runs
### `benchmarks/9700x/tls-companion-scan-t32768.c`

Validity: all 11 planned runs completed and were valid.

source sha256 `15608ae289ca1864efea0649e323723e73eb603e5a8bd4a1ad22ecc30a2f8cfb`, executable sha256 `e829237526c2396ffbe302096523fce95602d99315c6de448cae620c03653ecc`, `input.data` sha256 `c12108faab4f9d3333be302e2f2e5f65793ae9126d62e699e3a9d27e3c5ebd2e`

Wall over 9 measured runs: median 615.301 ms, min 605.594 ms, max 622.055 ms. CPU median 614.731 ms. Peak RSS 36253696 bytes.

| run | exit | timed out | wall ms | cpu ms | rss bytes | output sha256 |
|---|---|---|---|---|---|---|
| warmup 0 | 0 | 0 | 610.726 | 609.849 | 36253696 | `b7f9240b7f66e987` |
| warmup 1 | 0 | 0 | 607.501 | 606.915 | 36098048 | `b7f9240b7f66e987` |
| sample 0 | 0 | 0 | 613.064 | 612.377 | 36253696 | `b7f9240b7f66e987` |
| sample 1 | 0 | 0 | 605.594 | 604.909 | 36098048 | `b7f9240b7f66e987` |
| sample 2 | 0 | 0 | 616.238 | 615.614 | 36098048 | `b7f9240b7f66e987` |
| sample 3 | 0 | 0 | 615.056 | 614.524 | 36098048 | `b7f9240b7f66e987` |
| sample 4 | 0 | 0 | 622.055 | 621.460 | 36098048 | `b7f9240b7f66e987` |
| sample 5 | 0 | 0 | 618.097 | 617.619 | 36098048 | `b7f9240b7f66e987` |
| sample 6 | 0 | 0 | 614.862 | 614.386 | 36098048 | `b7f9240b7f66e987` |
| sample 7 | 0 | 0 | 617.864 | 617.280 | 36098048 | `b7f9240b7f66e987` |
| sample 8 | 0 | 0 | 615.301 | 614.731 | 36098048 | `b7f9240b7f66e987` |

All 11 runs printed identical output:

every run:

```text
TLS_COMPANION method=scan rows=65537 sites=32768 helper_references=32768 relaxed=32768 only_relaxed=1 digest=41f1f44027d02325 self_check=pass
```

progress:     7.217s START benchmarks/9700x/tls-companion-scan-t8192.c: preparing the run directory
progress:     7.222s START benchmarks/9700x/tls-companion-scan-t8192.c: staging the source
progress:     7.227s START benchmarks/9700x/tls-companion-scan-t8192.c: compilation
progress:     7.296s START benchmarks/9700x/tls-companion-scan-t8192.c: reading the input data
progress:     7.303s START benchmarks/9700x/tls-companion-scan-t8192.c: hashing the executable
progress:     7.309s START benchmarks/9700x/tls-companion-scan-t8192.c: measurement
progress:     7.354s RUN benchmarks/9700x/tls-companion-scan-t8192.c 1 of 11: exit 0, timed out 0, wall 39.380 ms
progress:     7.394s RUN benchmarks/9700x/tls-companion-scan-t8192.c 2 of 11: exit 0, timed out 0, wall 38.931 ms
progress:     7.434s RUN benchmarks/9700x/tls-companion-scan-t8192.c 3 of 11: exit 0, timed out 0, wall 38.734 ms
progress:     7.473s RUN benchmarks/9700x/tls-companion-scan-t8192.c 4 of 11: exit 0, timed out 0, wall 38.052 ms
progress:     7.513s RUN benchmarks/9700x/tls-companion-scan-t8192.c 5 of 11: exit 0, timed out 0, wall 38.896 ms
progress:     7.553s RUN benchmarks/9700x/tls-companion-scan-t8192.c 6 of 11: exit 0, timed out 0, wall 38.858 ms
progress:     7.593s RUN benchmarks/9700x/tls-companion-scan-t8192.c 7 of 11: exit 0, timed out 0, wall 38.917 ms
progress:     7.633s RUN benchmarks/9700x/tls-companion-scan-t8192.c 8 of 11: exit 0, timed out 0, wall 39.012 ms
progress:     7.674s RUN benchmarks/9700x/tls-companion-scan-t8192.c 9 of 11: exit 0, timed out 0, wall 39.029 ms
progress:     7.714s RUN benchmarks/9700x/tls-companion-scan-t8192.c 10 of 11: exit 0, timed out 0, wall 39.102 ms
progress:     7.754s RUN benchmarks/9700x/tls-companion-scan-t8192.c 11 of 11: exit 0, timed out 0, wall 39.102 ms
progress:     7.754s START benchmarks/9700x/tls-companion-scan-t8192.c: reporting
progress:     7.769s DONE benchmarks/9700x/tls-companion-scan-t8192.c: valid, 11 runs
### `benchmarks/9700x/tls-companion-scan-t8192.c`

Validity: all 11 planned runs completed and were valid.

source sha256 `15608ae289ca1864efea0649e323723e73eb603e5a8bd4a1ad22ecc30a2f8cfb`, executable sha256 `a991660ad38c57b51bd6c134e206844eb7f189fc0bbe6a372f80198f89fb5182`, `input.data` sha256 `a8cbf8ee3969796b2f9c602a99a2a3da4f7c458be4be49749dd595aac76df065`

Wall over 9 measured runs: median 38.917 ms, min 38.052 ms, max 39.102 ms. CPU median 38.662 ms. Peak RSS 36253696 bytes.

| run | exit | timed out | wall ms | cpu ms | rss bytes | output sha256 |
|---|---|---|---|---|---|---|
| warmup 0 | 0 | 0 | 39.380 | 38.711 | 36098048 | `235bc9f0f4b54708` |
| warmup 1 | 0 | 0 | 38.931 | 38.650 | 36098048 | `235bc9f0f4b54708` |
| sample 0 | 0 | 0 | 38.734 | 38.463 | 36098048 | `235bc9f0f4b54708` |
| sample 1 | 0 | 0 | 38.052 | 37.789 | 36098048 | `235bc9f0f4b54708` |
| sample 2 | 0 | 0 | 38.896 | 38.644 | 36130816 | `235bc9f0f4b54708` |
| sample 3 | 0 | 0 | 38.858 | 38.584 | 36098048 | `235bc9f0f4b54708` |
| sample 4 | 0 | 0 | 38.917 | 38.662 | 36098048 | `235bc9f0f4b54708` |
| sample 5 | 0 | 0 | 39.012 | 38.788 | 36098048 | `235bc9f0f4b54708` |
| sample 6 | 0 | 0 | 39.029 | 38.779 | 36098048 | `235bc9f0f4b54708` |
| sample 7 | 0 | 0 | 39.102 | 38.859 | 36253696 | `235bc9f0f4b54708` |
| sample 8 | 0 | 0 | 39.102 | 38.841 | 36098048 | `235bc9f0f4b54708` |

All 11 runs printed identical output:

every run:

```text
TLS_COMPANION method=scan rows=16385 sites=8192 helper_references=8192 relaxed=8192 only_relaxed=1 digest=6b8046173263a325 self_check=pass
```

progress:     7.780s END exit status 0
Compiler: `/usr/bin/clang`. Flags: `-std=c11 -O2 -static -fwrapv -fno-strict-aliasing -funsigned-char -Wall -Wextra -Werror`.

Load average before: `0.80 1.00 1.03 1/345 2483996`; after: `0.82 1.00 1.02 1/347 2484146`.

# Compact read-set columns: qualified and rejected

The first experiment retained 32-byte `ReplayRead` records containing names,
keys and source-span metadata, although validation only needs names and keys.
This variant records names and exact 64-bit keys in separate contiguous arrays.
The temporary 12-byte source spans are discarded after location normalization.
Publication copies the two columns directly. Validation uses scalar current-key
loads and one bounded, masked load of eight expected keys. External-definition
locations find the current macro through the name column. Identity, ordering,
fallback, capacities and cache-lifetime rules are otherwise unchanged.

Apply [read-columns.patch](read-columns.patch) to the original frozen
`c_preprocessor_replay.c`, and [guard-columns.patch](guard-columns.patch) to
`guard.c`, each with `patch -p1` in the containing directory. The core
preprocessor/history overlays, probe, equivalence checks and generator stay
byte-identical to the initial experiment. The independent `metrics.c` and
`cold-batch.c` are the current diagnostic/whole-batch adapters.

The guarded read test places both arrays immediately before inaccessible pages,
checks lengths 0 through 65 against its scalar twin, and flips the last key to
check a mismatch at every vector-tail position. These 131 checks supplement
the 8,580 original guarded location-copy checks.

Local diagnostic checks used the Ryzen 9 7940HS, Clang 23.1.3, `-O1 -g
-march=native -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror`, one
CPU at absolute niceness 19 with idle I/O priority, disk-backed temporary files,
bounded commands and a `work/STOP` check between groups. The source is the
identified 635434f snapshot. They passed:

- syntax checks for the experiment, metrics and both whole-batch adapters;
- 8,580 location and 131 read-column protected-page/scalar comparisons;
- 45 state/read-set/include-boundary unit runs and 12 virtual-source cases;
- 3,945 musl unit runs / 15,013,785 tokens and 102 Lua unit runs / 1,764,198 tokens;
- 26,949 libc unit runs / 36,728,361 tokens in 512-input groups;
- 1,500 unit runs each in generated C17 and C23, plus the location-copy scalar
  mode over the C17 set.

Local musl retention is 84,930,294 bytes, with the same 1,100 transcripts;
the initial layout retained 96,150,694 bytes. This is a storage observation,
not a speed result. Local logs are under [raw/local-columns](../raw/local-columns/).

The native 9700X build, guarded tails, state/include-boundary cases and complete
corpus equivalence checks passed. The native libc comparison covers 26,949 unit
runs / 36,297,048 output tokens; its environment differs from the local logs.
The [main report](../README.md) records every native corpus count and the
fresh whole-batch timings. Musl slows by 3.17–5.18% and Lua by 20.30–21.89%
across three process rounds, while peak process memory rises substantially.
The smaller columns preserve correctness and reduce retention, but still fail
the performance gate. This variant is rejected with the original layout.

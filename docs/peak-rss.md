# Peak RSS: compiler address space versus subprocess accounting

Issue #865 separates two useful but different measurements. All `kb`/`KiB`
fields here are kibibytes (1,024 bytes), including Linux's `kB` fields.

## `--time-stages`

`time-process peak-rss-kb` now reports the maximum observed
`/proc/self/status` **VmHWM** at the driver's resource marks, from the current
address space, not `getrusage(RUSAGE_SELF).ru_maxrss`. The address space is
created at exec; its peak includes the loader and startup before the clock
starts. Coverage ends at the **last resource mark**, not process exit.
The report publishes:

| Field | Meaning |
| --- | --- |
| `peak-rss-source` | `proc-self-status-vmhwm` |
| `peak-rss-scope` | `post-exec-through-last-mark` |
| `peak-rss-precision` | `approximate` |
| `peak-rss-status` | `available` or `unavailable` |
| `peak-rss-kb` | Post-exec high-water mark, or `-1` |
| `peak-rss-start-kb` | High-water mark when the clock starts, or `-1` |
| `rusage-peak-rss-kb` | Unmodified `RUSAGE_SELF` peak, which can include the pre-exec image |

The last field is a combined accounting high-water mark, **not** an isolated
measurement of pre-exec memory. Subtracting either peak or the startup baseline
from it does not recover the launcher's exact contribution.

`peak-kb-added` charges only increases above the preceding mark's high-water
mark. Thus, within one available report, the sum of all stages' additions equals
`peak-rss-kb - peak-rss-start-kb`. It is not a stage's allocation volume, current
RSS, or independent peak; a stage can allocate and free below an earlier peak
and add zero. Existing per-function laps still charge resource use to the next
resource mark. Multiple compilation lanes retain the existing aggregate stage.

**Precision.** `VmHWM` is the kernel's approximate RSS high-water accounting.
Reading it at stage boundaries is not polling current `VmRSS`: a transient peak
followed by unmapping can survive in the high-water mark. Nevertheless, Linux
documents RSS accounting inaccuracies; these numbers are not an exact page-table
census, and no fixed numerical error bound is promised. Marks clamp the observed
peak monotonically so an inconsistent lower reading cannot cause double counting.
The report reads no new peak after formatting starts, preserving its accounting
identity and its declared end point.

**Unavailable.** A failed open/read, missing or malformed field, overflow,
duplicate field, incomplete line, or status file filling the bounded 8 KiB
reader makes RSS unavailable. There is no fallback to `ru_maxrss` or to zero.
A failure at any resource mark invalidates RSS attribution for the whole report:
a recovered reading must not charge an unobserved interval to a later stage.
Every stage addition and both post-exec process values then print `-1`, with
`peak-rss-status unavailable`. Timing and fault reporting continue. The report
reader requires all RSS columns to agree on availability. In `measure-compile`,
any unavailable run makes the affected RSS median and range unavailable rather
than sorting `-1` into an apparently valid result.

Only `--time-stages` reads procfs. The ordinary compiler path still passes a null
clock and does no extra resource reading. Reading procfs adds instrumentation
overhead; this change makes no claim about its measured cost.

## `measure-compile` and `time-runs`

`ProfileTimed.peak_kb` remains the raw Linux `wait4` value. Both commands label
their per-run and median numbers **`KiB wait4 peak`**, and state their scope.
These peaks can include the child's pre-exec image and accounting for waited
descendants; they must not be presented as executable-only peaks. No sampling
thread, tracing launcher, or inferred subtraction has been added to these tools.

`measure-compile` additionally consumes the timed compiler's separate
post-exec stage report. Its default and external reference runs do not acquire
an executable-only RSS measurement. `time-runs` explicitly reports that such
a measurement is unavailable. CPU time and faults retain their existing rusage
scope; user-mode hardware counters retain their separate exec-to-exit scope.

Historical measurements are not rewritten or silently reinterpreted. #865
demonstrated contamination under a large touched launcher, not that every
published run was contaminated.

## Regression

`tests/peak-rss.c` is an original correctness fixture, run by
`tools/ci/correctness.tests` on the existing approved CI host:

```sh
clang -std=c17 -O2 -Wall -Wextra -Werror tests/peak-rss.c -o build/test-peak-rss && ./build/test-peak-rss ./build/compiler
```

Without the compiler argument it tests the production stage clock with an
identical fresh-exec workload launched from a small parent and one with 256 MiB
of touched anonymous memory. The workload touches 32 MiB, then 64 MiB, and
unmaps the latter before its resource mark. Tests check parent independence,
the separately exposed inherited peaks, nonzero attribution in both stages,
and the stage-total identity. RSS bounds have broad tolerances, not assumed
exact `/proc` values.

With the compiler argument it also generates 4,096 original C functions,
compiles that same source with the same flags from both launchers, checks
byte-identical objects and comparable per-stage attribution, and verifies the
subprocess tools' scope labels. Padding is sized above the small compile's peak
and bounded to avoid an unbounded test allocation. Parser fixtures exercise
guard-page tails, missing/truncated/oversized fields and overflow. Descriptor
limits inject a real failed procfs open at startup and mid-report, followed by
recovery; the report must retain explicit unavailability.

## Basis

The implementation and fixtures are original; no kernel implementation is
copied. The relevant Linux contracts are
[getrusage(2)](https://man7.org/linux/man-pages/man2/getrusage.2.html)
(resource accounting is preserved across exec),
[proc_pid_status(5)](https://man7.org/linux/man-pages/man5/proc_pid_status.5.html)
(`VmHWM`, including its documented inaccuracy), and the
[proc filesystem documentation](https://www.kernel.org/doc/html/latest/filesystems/proc.html).

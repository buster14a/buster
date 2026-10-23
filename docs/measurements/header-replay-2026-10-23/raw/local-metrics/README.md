# Local distribution capture

These are diagnostic counts, not performance measurements or native target
qualification. Host: Ryzen 9 7940HS, Clang 23.1.3. The frozen source and overlays
are those identified in the main report; applying the two `soa/` patches
reproduces the local cache and guard source exactly.

The metrics executable was built with:

```sh
nice -n 19 ionice -c 3 taskset -c 0 timeout 90s clang \
    -O1 -g -march=native -fwrapv -funsigned-char -fno-strict-aliasing \
    -Wall -Werror -Iwork/soa-probe -Iwork/frozen-tree/src \
    work/soa-probe/metrics.c -o work/metrics-soa-local
```

Each workload used its initial native `*-units.txt` list and the same options
shown in the main report, from the corresponding local musl/Lua directory.
Each invocation ran under `nice -n 19 ionice -c 3 taskset -c 0 timeout 30s`,
with `SOURCE_DATE_EPOCH=1767225600`, disk-backed `TMPDIR`, and the four external
include-path environment variables cleared. A stop-file check preceded each.
`PROBE_FILES_MANIFEST` captured the resolved path list. The first attempted
invocation used an invalid joined CPU option and did not execute the probe;
the retained CSV and statistics come from the corrected commands.

The local musl aggregates match the native batch; Lua's installed headers
differ slightly. The CSV's `variants` counts all path aliases sharing that
file-cache record. Keep the local/native distinction when reusing this data.

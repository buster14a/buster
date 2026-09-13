# Canonical validation audit evidence

`final-znver3` is the authoritative run. Start with `CHECKPOINT.md`, then inspect:

- `build.log`, caches, and compressed compile databases for host/tool/build provenance;
- `inputs.sha256`, `generated.sha256`, `outputs.sha256`, and `*.metrics` for frozen workloads, parity, and exact work populations;
- `unity.callgrind.gz` and `small.callgrind` for raw instruction-reference events
  (the unity stream is losslessly packaged with deterministic gzip);
- `unity-functions.txt`, `unity-lines.txt`, `small-functions.txt`, and
  `small-lines.txt.gz` for the original `callgrind_annotate` output (the final
  file is likewise losslessly gzip-packaged);
- `attribution.json` for the reproducible source-family breakdown.

Regenerate the attribution from the repository root:

```sh
python3 docs/performance-audits/evidence/2026-09-13T130555Z/summarize_callgrind.py \
  docs/performance-audits/evidence/2026-09-13T130555Z/final-znver3/unity.callgrind.gz \
  --validator-total 472211973
```

`preliminary-haswell` is intentionally retained. It demonstrates why target
selection must be explicit under Valgrind: the census path selected `znver3`
from the host while the Callgrind path saw `haswell`. It is incomplete evidence,
not a second result to average with the authoritative run.

`diagnostic-parity` retains the later invalid-input parity result. Its census
step passed; the subsequent profile repeat failed because that runner's native
producer contains an EVEX instruction unsupported by Valgrind 3.26. The
failure is evidence, not an attribution result.

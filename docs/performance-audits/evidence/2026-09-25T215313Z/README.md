# Evidence for audit 2026-09-25T215313Z

Research material for [the audit](../../2026-09-25T215313Z.md). Nothing here is build or benchmark
infrastructure; the probe scripts patch a **copy** of the tree and must never be committed to source.

| File | What it is |
|---|---|
| `screen_gen.py`, `screen_run.py`, `screen_analyze.py` | 27 deterministic geometric C families, the callgrind runner, and the per-function growth analyzer |
| `screen_summary.txt` | analyzer output for every family (sizes 1k/2k/4k/8k; `<<<` marks exponent ≥ 1.3) |
| `fast_gen_fixed.py`, `fast_grid_fixed.tsv` | F escaping, upward-exposed aggregates × B arms; baseline vs prototype FAST counts |
| `fast_gen_fixed_shareable.py`, `fast_grid_fixed_shareable.tsv` | the same plus C arms of `__int128` arithmetic (shareable slots) |
| `frontend_gen.py`, `frontend_grid.txt` | filler type rows T × queries Q for the three C-semantic whole-table sites |
| `unity_probe_counts.txt` | both probe compilers on the frozen self-compile, with object SHA-256 |
| `callgrind_frozen_unity.txt` | matched counting-build comparison table (totals, largest deltas, inclusive rows) |
| `callgrind.frozen-unity.{base,prototype}.out.gz` | the raw callgrind outputs behind that table |
| `probe_frontend.py`, `probe_fast_base.py`, `probe_fast_prototype.py` | anchor-checked counter patches (print on exit when `BUSTER_PROBE` is set) |
| `build_variant.py`, `build_counting.py` | the exact compile recipes of the native and counting variants |
| `SHA256SUMS` | hashes of every other file here |

Reproducing a FAST grid point:

```sh
git archive d9e736e2cf08804a2c603d153e9fb608f18c5c49 src | tar -x -C base-tree
cp -r base-tree probe-tree
python3 probe_frontend.py probe-tree/src/buster/lib/compiler/frontend/c/c_parse.c
python3 probe_fast_base.py probe-tree/src/buster/lib/compiler/codegen/register_allocator_fast.c
BUSTER_REPO=<configured tests-off checkout> python3 build_variant.py probe-tree ide-probe --no-werror
python3 fast_gen_fixed_shareable.py 1024 1024 16 f.c
BUSTER_PROBE=1 ./ide-probe cc -g0 -O0 -c f.c -o f.o   # FASTPROBE ... bits_total=7007155 ...
```

`probe_fast_prototype.py` applies the same counters to a tree containing the prototype
(`machine_fast_close_slot_ranges`). The frozen self-compile input is the pinned commit's `src` from
`git archive` plus the generated headers of a configured build tree, compiled as
`ide cc -Isrc -I<generated> -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g0 -c src/buster/apps/ide/ide.c`.

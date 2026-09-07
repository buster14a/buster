# stb compatibility harness

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

[Compatibility harness index](../compatibility.md). Read only the harness you are working on; pins, measurements, and past failure counts describe the revision recorded below and must be rechecked before reuse.

The opt-in stb compatibility harness takes an external, pristine nothings/stb
checkout; upstream headers are never copied into or patched in this repository:

```sh
./build.sh build --config Release -t ide
./build/build test_stb --config Release /path/to/stb
```

The checkout must be commit
`2c980bb59875b0d32144a71867fbdebb2f77cd20` with no tracked or untracked
changes. The harness compiles `stb_image.h`, `stb_image_write.h`, `stb_ds.h`,
and `stb_truetype.h` implementations separately and in one combined stress
translation unit, then links a deterministic probe. It compares decoded PNG
pixels, PNG/BMP/TGA encoded bytes and hashes, synthetic-font glyph bitmaps, and
`arrput`/`hmput` data-structure results against Clang. The applicable upstream
image-write test and the `test_ds.c` churn workload run from pristine
per-compiler output directories; the `STBDS_UNIT_TESTS` block (including its
array-key `STBDS_ADDRESSOF` assertion) is rejected by Clang at this pinned
revision, so it is intentionally excluded without modifying upstream sources.
The upstream truetype demo is also skipped because it hard-codes host font
paths, while the probe builds a project-owned sfnt fixture in memory.
FAST and NONE run first, followed by MIR_STACK and QUALITY. Every Buster unit
records unique/lexed file and byte counts (include amplification) plus
preprocessed token and spelling-byte totals. Generated wrappers, objects,
metrics, synthetic fonts, and logs remain under `build/stb-<hash>-<pid>/`.

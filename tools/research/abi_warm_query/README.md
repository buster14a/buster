# ABI warm-query replay (#279)

This opt-in Linux experiment prices the production ABI service. It is outside
ordinary build/test registration. The registered IR tests own API correctness.
No timing threshold gates a test.

`service.c` preserves the historical
`docs/performance-audits/2026-09-07T233720Z-service.c` corpus and schedule:
23 types, argument/result for every type in the dense trace, and ids 0/2/6/14
in the sparse trace. The only replay adapters are the current classifier policy
argument and the current IR timing dependency. It measures logical cold-cache
preparation and first queries separately, then 32768 repetitions of the warm
trace. Allocator/scratch pages have been warmed; this is not a process or
hardware cold-start measurement. Each preflight compares every returned byte
with the uncached production classifier. Checksum, fingerprint and zero extra
classifications guard every paired service sample.

For two complete checkouts `baseline` and `candidate`, compile the **same**
candidate harness source with each checkout's includes:

```sh
for version in baseline candidate; do
    clang -O3 -g -march=native -fwrapv -fno-strict-aliasing -funsigned-char \
        -Wall -Werror -Wno-unused-function -Wno-unused-variable \
        -ffunction-sections -fdata-sections -I"$version/src" \
        candidate/tools/research/abi_warm_query/service.c \
        -Wl,--gc-sections -lm -pthread -o "$PWD/service-$version"
done
python3 candidate/tools/research/abi_warm_query/compare_service.py \
    "$PWD/service-baseline" "$PWD/service-candidate" "$PWD/service-samples.json"
```

The runner pins itself and its children to one permitted CPU, alternates order
for 12 pairs per trace, and retains all 48 rows. This reduces migration noise;
it does not isolate a shared hosted VM or establish a Zen 5 result.
The historical replay leaves `BUSTER_OPTIMIZE=0` at its default even with
host `-O3`; its inline hints therefore differ from normal Release builds.
Keep this macro identical between service subjects. The whole-compiler
comparison below uses normal Release configuration in both checkouts.
For ASan/UBSan use `-O1 -fsanitize=address,undefined
-fno-omit-frame-pointer` instead of `-O3 -march=native`, then run both traces.

After both trusted Clang Release compilers are built through the existing
build driver, run a separate same-source whole-object comparison:

```sh
python3 candidate/tools/research/abi_warm_query/compare_compiler.py \
    "$PWD/baseline/build/Release/ide" "$PWD/candidate/build/Release/ide" \
    "$PWD/baseline" "$PWD/compiler-samples.json"
```

It performs one warmup pair and six alternating measured pairs on the baseline
unity source and its generated headers, records exact argv, binary/output
hashes, fresh-process wall/user time and peak RSS, and requires identical object
bytes. It keeps failed attempts and diagnostics. Both subjects use `-march=native`;
`-march=baseline` cannot compile this unity input while
[#1487](https://github.com/buster14a/buster/issues/1487) remains unresolved. It does not compare compilation
of each version's own changed source. `/usr/bin/time` has 0.01-second elapsed
resolution. Service ns/query and complete compilation seconds have different
denominators; never infer the latter from the former.

License: Buster has no selected first-party/project-wide license.
[LICENSES/README.md](../../../LICENSES/README.md) describes the upstream notices.
No external source or dependency is imported by this experiment.

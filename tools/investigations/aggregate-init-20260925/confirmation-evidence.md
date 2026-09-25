# Reduced aggregate initializer confirmation

## Exact executed identities

Production main **dce1b8caa3d368dede16ad4aa9da6760e26fe704**, tree **486099e282770e64b0a4e3844e2f39d91fd720d1**; `c_gen.c` blob **6d38c34fefb4a121a0f3c1e6e97358f57b6a5248**. The source pin is unchanged from the initial campaign, but the compiler was rebuilt on this run's standard GitHub-hosted Ubuntu 26.04.1 x86-64 runner. No production fix.

Transport commit **80a44fb002129c2a75c55b91753538690224190b**, tree **5269f53b28002a038d2992829468f8c8a8055cee**. The branch-scoped push workflow validates that production sources/build inputs match the pin before building. This is not a test-merge commit.

[Run 36147610033](https://github.com/buster14a/buster/actions/runs/36147610033), job **108112574800**. Trusted Clang Release `ide` SHA-256 **ff5b1c8270585d81de5c722d4ef64b8a72c063698e706f8424dc94716620e0ae**. Ubuntu Clang **21.1.8 (6ubuntu1)**, GCC **15.2.0 (15.2.0-16ubuntu1)**, CMake **4.4.3**, Ninja **1.13.2**. Build and subjects ran on the same runner; no desktop, SSH or measurement host.

[Exact minima.c](https://github.com/buster14a/buster/blob/80a44fb002129c2a75c55b91753538690224190b/tools/investigations/aggregate-init-20260925/minima.c), SHA-256 **2e7de42bf0a5c4b9535db8e9351e75905353386e279447934f1961cc0d80cfbe**.

[Exact confirm.c](https://github.com/buster14a/buster/blob/80a44fb002129c2a75c55b91753538690224190b/tools/investigations/aggregate-init-20260925/confirm.c), SHA-256 **abd4f49072cab7ce598846144b7fc04d49242f61f3a9ea98b2fc7fccddfd3098**.

[Raw artifact 10869483273](https://github.com/buster14a/buster/actions/runs/36147610033/artifacts/10869483273), ZIP SHA-256 **4b03bd5d59def88b1b8abe2a038c070b6939c3e2ad146f48eaa30bfa270f0c1a**, 32,134,208 bytes. ZIP digest and all **7,314 manifest-listed files** were independently verified after retrieval. Exact row-level `results.tsv` SHA-256 **a5fdacc26328187230e62b8b6ea84141749a7096e757d54595287991a9ec25cc**. The artifact retains separate stdout/stderr, one-argument-per-line command records, statuses, executables, source/transport/tool identities, build/cache records, original inputs, bitcode and decoded LLVM text. Retention ends **2026-10-25**; this committed record preserves the complete grouped outcomes and decisive raw observations independently of that expiry.

## Replay

Use a fresh checkout on an authorized standard correctness runner, not an existing working branch or transferred native-tuned executable.

```sh
git checkout 80a44fb002129c2a75c55b91753538690224190b
mkdir -p evidence
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -g build.c -o /tmp/buster-build
/tmp/buster-build generate --cc clang --ci --linker DEFAULT
/tmp/buster-build build --config Release -t ide -- -j2
clang -std=c17 -Wall -Wextra -Werror tools/investigations/aggregate-init-20260925/confirm.c -o /tmp/aggregate-confirm
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 /tmp/aggregate-confirm \
  "$PWD/build/Release/ide" "$PWD/tools/investigations/aggregate-init-20260925/minima.c"
```

The observer returns nonzero because it records real compiler defects. It does not bless their outputs or convert valid-input compilation failures into passing negative tests. It bounds compilation to 30 seconds and execution to 5 seconds, each with a two-second kill grace. Each compiled subject's expected exit is zero. `minima.c` removes the original printf/check-loop wrapper; it observes only named fields through a direct return mask.

One exact mode/case replay, after the build:

```sh
build/Release/ide cc -std=c17 -fwrapv -fno-strict-aliasing -funsigned-char -g0 -O0 \
  -target x86_64-linux -fregister-allocator=fast -fverify-codegen -fno-machine-fallback \
  -ffrontend-ssa -DCASE=0 -DSTORAGE=0 \
  tools/investigations/aggregate-init-20260925/minima.c -o /tmp/initializer-case
/tmp/initializer-case
# Actual exit 1; expected exit 0. No stdout or runtime stderr.
```

Replace CASE with 4 or 6 for omitted-member zeroing or chained-designator continuation. Replace STORAGE with 1 for file-static or 2 for an automatic compound literal. Independent examples retain the same semantic flags, defines and source:

```sh
clang --target=x86_64-linux-gnu -std=c17 -fwrapv -fno-strict-aliasing -funsigned-char -g0 -O2 \
  -DCASE=0 -DSTORAGE=0 tools/investigations/aggregate-init-20260925/minima.c -o /tmp/clang-case
gcc -m64 -std=c17 -fwrapv -fno-strict-aliasing -funsigned-char -g0 -O2 \
  -DCASE=0 -DSTORAGE=0 tools/investigations/aggregate-init-20260925/minima.c -o /tmp/gcc-case
/tmp/clang-case
/tmp/gcc-case
# Both exit 0.
```

## Complete profile definitions and outcomes

22 profiles x 16 cases x 3 storage contexts = **1,056 compilation attempts**, verified complete with unique `(profile,case,storage)` keys.

Profiles 0/1/2: Clang O0/O2/O1+ASan+UBSan. Profiles 3/4/5: GCC O0/O2/O1+ASan+UBSan. Sanitized profiles additionally use `-fsanitize=address,undefined -fno-sanitize-recover=all`. Clang target is `--target=x86_64-linux-gnu`; GCC uses `-m64`.

Buster profiles 6-21: for `k=profile-6`, allocator is `[none,mir-stack,fast,quality][k/4]`; frontend is direct SSA when `(k/2)%2==0`, otherwise memory form; optimization is O0 when `k%2==0`, otherwise O2. All use `-target x86_64-linux -fverify-codegen`; `-fno-machine-fallback` is supplied for MIR_STACK/FAST/QUALITY, not NONE. All profiles use `-std=c17 -fwrapv -fno-strict-aliasing -funsigned-char -g0`.

Storage 0: automatic `TYPE obj=INIT`; 1: file-scope `static TYPE obj=INIT`; 2: automatic `TYPE obj=(TYPE)INIT`.

**All 288 reference rows compile and execute with exit zero**, including 96 reference-sanitizer executions. All 16 Buster profiles have exactly the same per-case results below. Therefore this table plus the profile definitions is a lossless grouping of all 1,056 outcomes. Every Buster file-static row passes. Columns are `compile/run`; `1/-1` means compilation failed and execution was not attempted.

| CASE | Discriminator | Storage 0 | Storage 1 | Storage 2 |
|---:|---|---|---|---|
| 0 | `.a={1},.a[0]=2` | 0/1 | 0/0 | 0/1 |
| 1 | `.a[0]=2,.a={1}` reverse control | 0/0 | 0/0 | 0/0 |
| 2 | `.a={1},.a={2}` two deferred lists | 0/1 | 0/0 | 0/1 |
| 3 | `.a[0]=1,.a[0]=2` scalar-only control | 0/0 | 0/0 | 0/0 |
| 4 | `.a[0]=1,.a={[1]=2}` isolated missing reset | 0/1 | 0/0 | 0/1 |
| 5 | `.a={1,2},.a[0]=3` sibling-preserving leaf override | 0/1 | 0/0 | 0/1 |
| 6 | `.a[0]=1,2` with an outer tail | 0/6 | 0/0 | 0/6 |
| 7 | `.a[1]=2,3` exhausted-inner control | 0/0 | 0/0 | 0/0 |
| 8 | `.a[0]=1,2` without an outer tail | 1/-1 | 0/0 | 1/-1 |
| 9 | explicit scalar designators control | 0/0 | 0/0 | 0/0 |
| 10 | flat positional control | 0/0 | 0/0 | 0/0 |
| 11 | `.a={&anchor},.a[0]=0` pointer override | 0/1 | 0/0 | 0/1 |
| 12 | `.a={1,2},.a={3}` whole override, combined symptoms | 0/3 | 0/0 | 0/3 |
| 13 | disjoint braced aggregate/outer field control | 0/0 | 0/0 | 0/0 |
| 14 | `.a.x=1,2` named-member continuation | 0/6 | 0/0 | 0/6 |
| 15 | `.a={1,2},.a.x=3` named-member override | 0/1 | 0/0 | 0/1 |

Buster totals: **768 compilation attempts; 736 successful, 32 rejected; 736 executions, 448 passing and 288 wrong-result exits**. Per profile: 28 passes, 18 wrong-result executions, two compilation failures. No timeout or runtime signal occurred. Successful Buster compilations emit no diagnostics. Reference sanitizer silence supports, but does not replace, the defined-behavior analysis.

Raw case-8 diagnostics, identical across Buster profiles:

```text
# STORAGE=0
cc: error: /home/runner/work/buster/buster/tools/investigations/aggregate-init-20260925/minima.c:82:16: in function 'main': could not lower initializer expression for local 'obj'
# STORAGE=2
cc: error: /home/runner/work/buster/buster/tools/investigations/aggregate-init-20260925/minima.c:84:22: in function 'main': could not lower logical expression core
```

## Bitcode observations: separate from native execution

Six Buster `-emit-llvm` operations and six Clang textual decodes completed with exit zero: cases 0, 4 and 6, storage 0, both frontend forms, O0. No native allocator participates in this export. The LLVM artifacts were decoded/inspected, **not separately linked/executed**. Clang reports its ordinary target-triple override warning during decoding. Each case's two decoded bodies are identical apart from the first ModuleID line.

```sh
build/Release/ide cc -target x86_64-linux -std=c17 -O0 -g0 -ffrontend-ssa -emit-llvm \
  -DCASE=0 -DSTORAGE=0 tools/investigations/aggregate-init-20260925/minima.c -o case.bc
clang -S -emit-llvm -O0 case.bc -o case.ll
```

Hashes of the original decoded files:

```text
a599c0d3e8328ea469f65a46e622a9324af69382c0921e3ac6cfe4c6c84ea810  ir-c0-ffrontend-ssa.ll
80a24a11250b33b96b75bea8dff89a72604b61904d55058d17fdde9cbe73826d  ir-c0-fno-frontend-ssa.ll
208b641f78b45451dba7f9b372f6d3ca038bc8b1076c560417b1e2c74866a446  ir-c4-ffrontend-ssa.ll
830c7183c080444c17dae6f9d21131f8b0d85db2f82fabc32dc1c3b99aef279a  ir-c4-fno-frontend-ssa.ll
8b9c0692129a1094ba669844d45ce552e4fff111af48559d540662aa0d474d60  ir-c6-ffrontend-ssa.ll
a11d83e8b6fff17cc86bcbdb3bf453176692d5e7a0ed56f4c5bac2eb6e5bb0d3  ir-c6-fno-frontend-ssa.ll
```

Decisive raw instruction spans follow, with numbering preserved. They corroborate the actual C-to-canonical lowering source trace; they are not a substituted copy of the lowering algorithm.

### CASE 0: the later value 2 is overwritten by the earlier value 1

```llvm
  %1 = alloca <{ [1 x i32] }>, align 4
  %2 = alloca <{ [1 x i32] }>, align 4
  %3 = insertvalue [1 x i32] undef, i32 0, 0
  %4 = insertvalue <{ [1 x i32] }> undef, [1 x i32] %3, 0
  store <{ [1 x i32] }> %4, ptr %2, align 4
  %5 = getelementptr i8, ptr %2, i64 0
  %6 = getelementptr i8, ptr %2, i64 0
  %7 = getelementptr [1 x i32], ptr %6, i64 0, i32 0
  store i32 2, ptr %7, align 4
  %8 = getelementptr [1 x i32], ptr %5, i64 0, i32 0
  store i32 1, ptr %8, align 4
  %9 = load <{ [1 x i32] }>, ptr %2, align 4
  store <{ [1 x i32] }> %9, ptr %1, align 4
```

### CASE 4: only the initial root is zeroed; later whole-array initializer leaves a[0]=1

```llvm
  %1 = alloca <{ [2 x i32] }>, align 4
  %2 = alloca <{ [2 x i32] }>, align 4
  %3 = insertvalue [2 x i32] undef, i32 0, 0
  %4 = insertvalue [2 x i32] %3, i32 0, 1
  %5 = insertvalue <{ [2 x i32] }> undef, [2 x i32] %4, 0
  store <{ [2 x i32] }> %5, ptr %2, align 4
  %6 = getelementptr i8, ptr %2, i64 0
  %7 = getelementptr [2 x i32], ptr %6, i64 0, i32 0
  store i32 1, ptr %7, align 4
  %8 = getelementptr i8, ptr %2, i64 0
  %9 = getelementptr [2 x i32], ptr %8, i64 0, i32 1
  store i32 2, ptr %9, align 4
  %10 = load <{ [2 x i32] }>, ptr %2, align 4
  store <{ [2 x i32] }> %10, ptr %1, align 4
```

There is only one pending nested brace task in this fixture. The explicit stores already occur in source order, so reversing/FIFO-ordering tasks cannot repair this distinct missing-zero-initialization invariant.

### CASE 6: the positional 2 goes to the outer tail at byte offset 8

```llvm
  %1 = alloca <{ [2 x i32], i32 }>, align 4
  %2 = alloca <{ [2 x i32], i32 }>, align 4
  %3 = insertvalue [2 x i32] undef, i32 0, 0
  %4 = insertvalue [2 x i32] %3, i32 0, 1
  %5 = insertvalue <{ [2 x i32], i32 }> undef, [2 x i32] %4, 0
  %6 = insertvalue <{ [2 x i32], i32 }> %5, i32 0, 1
  store <{ [2 x i32], i32 }> %6, ptr %2, align 4
  %7 = getelementptr i8, ptr %2, i64 0
  %8 = getelementptr [2 x i32], ptr %7, i64 0, i32 0
  store i32 1, ptr %8, align 4
  %9 = getelementptr i8, ptr %2, i64 8
  store i32 2, ptr %9, align 4
  %10 = load <{ [2 x i32], i32 }>, ptr %2, align 4
  store <{ [2 x i32], i32 }> %10, ptr %1, align 4
```

## Interpretation and limits

This separates three first violations: deferred initializer application overriding later values; loss of the continuation cursor inside an explicitly designated nested aggregate; and failure to apply implicit zero initialization when an entire subaggregate supersedes earlier leaf initialization. Cases 0/2, 6/8/14, and 4 isolate those mechanisms respectively; case 12 intentionally combines symptoms and must not be treated as a fourth issue.

The sources use bounded constants, live static addresses, named-field observations and no side effects in initializer expressions. Expected semantics follow WG14 N1570 6.7.9p17-21 (C11 committee draft), tested with C17 commands. No unspecified initializer-expression ordering or implementation-defined integer result is needed.

No production patch, full registered suite, Debug or sanitized Buster, TCC bootstrap, self-host fixed point, other architecture/OS execution, source-language feature outside this C17 family, representative-package incidence, performance measurement or speedup claim. The first campaign's compiler binary and evidence remain separately identified. The investigation does not validate all aggregate initialization or establish regression-introduction history.

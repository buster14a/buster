# Issue 2645: Wild / mold hosted qualification

## Recommendation and completion status

Retain Wild as an optional external linker and preserve existing defaults and Buster's native linker. On the qualified release-package host, Wild saves about 8.9 ms in a real one-source-edit-to-executable build; clean builds have no detectable improvement. Warm one-thread Buster links regress. These observations do not justify changing the default.

The existing C driver already supports `--linker WILD` through CMake 4.4.3. The implementation adds C-based hosted qualification and [owning support documentation](../agents/wild-linker.md), without redundant production plumbing, upstream vendoring or mandatory dependencies. PR #2647 consolidates the unchanged parent C harness from #2646 on its separately owned branch, plus a two-character quoted-scalar workflow compatibility fix. The original branch remains untouched with its original writer. Neither PR is merged at this report revision. #2645 must remain open until documentation/integration are merged and the exact-revision evidence and remaining scope satisfy its acceptance criteria.

Release-package supported campaign: **PASS**, [37222364481](https://github.com/buster14a/buster/actions/runs/37222364481), head `25a51ab98ff930d15aa056bffba302181da9bd21`. GC-enabled hot_reload strict full-DWARF is explicitly **FAIL with both linkers**, not hidden by this supported-cell status. A second optimized, source-built comparison is pending at [37224311631](https://github.com/buster14a/buster/actions/runs/37224311631); no source-built ranking is claimed yet.

## Pinned identities and licenses

| Item | Pin / verified authoritative source |
| --- | --- |
| Frozen Buster main | `3b79a042e00bade6072a47628deeaed18982ba04`; captured once in parent campaign head `d6469ecd1465c761fd41307d5375c1bee0d01d02`. Later main revisions are not substituted into these objects. |
| Wild 0.10.0 source | `e9a9b0cb6fd63a23cc7b93aafc931aadd5746be6`; [Cargo.toml](https://github.com/wild-linker/wild/blob/e9a9b0cb6fd63a23cc7b93aafc931aadd5746be6/Cargo.toml) declares MIT OR Apache-2.0. Exact source LICENSE-MIT / LICENSE-APACHE retained by isolated build. |
| mold 2.42.1 source | `9b376bc6a9899d4a16b41777de1f013989459fbc`; [LICENSE](https://github.com/rui314/mold/blob/9b376bc6a9899d4a16b41777de1f013989459fbc/LICENSE) is MIT. |
| Wild release executable SHA-256 | `5b2baebf62de0f8c185711b808c3f553da3b7166b1c289420df5276b027f5597` |
| mold release executable SHA-256 | `4b18553c959a792a91a77ec6448408300bbb04ad54e0d1a013453149583cce69` |
| Wild / mold release archive SHA-256 | `641265506a7c06cfb03181b8916ab663ec8407855db6d4db7f8450667d105283` / `6ff270c9bf07d2bec5c98aa324eb7c4daf6a1a4d815c05ff1708049616047855` |
| CMake 4.4.3 archive SHA-256 | `d6c83076c575bc00b823522ac974bda66d0af05d6ddc30e739c12385cf32c6cc` |
| Buster license | [LICENSES/README.md](https://github.com/buster14a/buster/blob/3b79a042e00bade6072a47628deeaed18982ba04/LICENSES/README.md) / #621 leave first-party selection unresolved. Third-party notices are not a first-party grant. |

No upstream code is imported into the repository. Research executables stay outside its dependency graph. This evidence publishes text/logs and identities, not redistributed upstream binary bundles. Component terms are separate from the top-level licenses; Wild's locked resolved package metadata, Cargo.lock and mold's exact CMake configuration are retained in source-build artifacts. Binary redistribution would need its own component review.

## Hosted environment and method

Release campaign host: GitHub-hosted Ubuntu 26.04.1, Microsoft Azure VM, AMD EPYC 7763, four logical CPUs (two exposed cores, SMT2), affinity 0–3, 16 GiB RAM, Linux 7.0.0-1012-azure, ext4 /dev/root about 151 GB. Clang 21.1.8 (Ubuntu 6ubuntu1), GCC 15.2.0-16ubuntu1, glibc 2.43-2ubuntu2.4, GDB 17.1, llvm-dwarfdump 21.1.8. Full host/process/load/affinity data are retained. Cgroup quota files were unavailable; exposed CPU/affinity is not proof of dedicated physical CPU allocation. Competing hypervisor load/storage cache are uncontrolled. No laptop or physical Ryzen 9700X was used.

The C harness reuses the parent's existing first-party bounded arena/process/sample infrastructure. It captures Clang's actual linker argv using `-###`, then invokes each linker on the identical frozen objects, archives, startup objects, shared libraries, scripts and order. Direct timings include /bin/sh startup plus fork/wait overhead (<1 ms process self-control), exclude Clang startup, and include output close without fsync. Compiler-driver timings from the parent are separately exploratory.

All successful read-open traces are retained; a conservative union of real regular files is SHA-256 checked before and after the campaign. Virtual /proc and /sys host telemetry is recorded in traces but cannot be frozen and is excluded from immutable file manifests. The release package campaign did not encounter the source mold/TBB /proc/meminfo read.

Policy controls: no LTO; identical PIC/PIE, compiler flags, DWARF, stripping/compression and existing hardening options from the captured command. Explicit `--no-gc-sections` for ide/small, required `--gc-sections` for hot_reload; `--icf=none --no-fork`; identical fixed 20-byte benchmark-only `--build-id=0x2645264526452645264526452645264526452645`. A fixed ID avoids measuring unlike hash algorithms and is not a production build-ID policy. Both default threading and explicit `--threads=1` are measured. Worker concurrency is otherwise not assumed equivalent.

Warm cells: two warmups per variant, 21 randomized interleaved AB/BA pairs. A/A controls: nine pairs. Clean-build pairs: seven. Guest-cold cells: nine pairs, no warmups, guest sync + drop_caches=3 before every observation, reset excluded from timer; hypervisor/storage caches remain uncontrolled. A guest-cold cell is not physical-cold acceptance.

Raw status/wall/user/system/maxRSS/page faults are retained for every command. Linux wait4 covers the complete no-fork linker lifetime and its individual process high-water RSS across threads. Build-tree RSS is not a sum of process peaks. Reported ratios are median paired Wild/mold ratios; intervals are a deterministic 4001-resample paired bootstrap 95% interval. MAD is unscaled median absolute deviation. Intervals describe this finite hosted sample, not cross-host uncertainty. Absolute savings below subtract the two medians and are not the median of paired differences.

## Release-package measurements

All times are ms. A=mold, B=Wild. MAD and every raw resource sample are in the retained CSVs.

| Cell | Pairs | mold median | Wild median | Saved median ms | Paired B/A [95% CI] |
| --- | ---: | ---: | ---: | ---: | --- |
| ide Debug, default threads | 21 | 26.962 | 22.614 | 4.348 | 0.873 [0.739, 0.913] |
| ide Debug, one thread | 21 | 33.573 | 40.268 | -6.695 | 1.200 [1.192, 1.210] |
| ide Release unity, default | 21 | 27.329 | 21.643 | 5.685 | 0.776 [0.755, 0.828] |
| ide Release unity, one thread | 21 | 29.626 | 32.838 | -3.213 | 1.112 [1.099, 1.137] |
| ide Release separate, default | 21 | 30.104 | 25.337 | 4.767 | 0.789 [0.728, 0.869] |
| ide Release separate, one thread | 21 | 38.205 | 47.623 | -9.418 | 1.248 [1.167, 1.262] |
| Small archive executable, default | 21 | 10.191 | 6.637 | 3.555 | 0.643 [0.614, 0.684] |
| Buster-produced small objects, default | 21 | 9.954 | 7.776 | 2.178 | 0.777 [0.727, 0.804] |
| hot_reload Debug required GC, default | 21 | 18.802 | 11.873 | 6.929 | 0.636 [0.600, 0.656] |
| Real source edit → executable | 21 | 220.244 | 211.312 | 8.932 | 0.962 [0.953, 0.969] |
| Clean application build | 7 | 8709.772 | 8794.212 | -84.439 | 1.009 [0.999, 1.024] |
| ide Debug guest-cold, default | 9 | 132.743 | 127.546 | 5.197 | 0.909 [0.851, 1.049] |
| ide Debug guest-cold, one thread | 9 | 135.792 | 120.251 | 15.541 | 0.872 [0.859, 0.906] |

The debug-heavy ide graph has 108 CMake target objects and 50 DWARF compile units, about 23.5 MB output. Debug is non-unity; Release compares real unity and separate-object inputs. This is substantial within Buster, not a claim about large C++ applications. Buster-produced objects here are a tiny two-object fixture: interoperability is demonstrated, large generated-object ranking is NOT RUN.

A/A medians: mold 25.855 / 26.331 ms, paired ratio 1.064 [0.924, 1.113]; Wild 18.596 / 18.475, ratio 0.996 [0.976, 1.014]. The broad mold A/A interval tempers the ide default-thread improvement; do not treat it as dedicated-host acceptance. Guest-cold default CI includes 1. The clean-build CI also includes 1.

End-to-end timers include the existing C build driver, Ninja and compilation. Each edit changes actual ide.c content and restores it; this is a source edit followed by full relinking, not incremental linking. Linker selection/configure and baseline setup are outside the timer. Actual Ninja link-step medians include compiler-driver startup: clean mold 48 ms / 0.55% of total versus Wild 43 ms / 0.49%; edits mold 47 ms / 21.34% versus Wild 38 ms / 18.14%. Fixed baseline input identity applies to the direct study; edit/clean compile outputs necessarily belong to the end-to-end study and are not substituted into direct cells.

| Direct warm resource cell | mold user / system ms | Wild user / system ms | mold / Wild peak KiB |
| --- | --- | --- | --- |
| ide Debug default | 33.866 / 29.747 | 26.957 / 23.659 | 93980 / 67264 |
| ide Debug one thread | 18.948 / 14.087 | 17.315 / 22.726 | 88524 / 65004 |
| Buster-produced small default | 7.382 / 9.227 | 3.495 / 8.207 | 28100 / 11496 |

| Output | mold / Wild total bytes | mold / Wild .text bytes | mold / Wild sum PT_LOAD file bytes |
| --- | --- | --- | --- |
| ide Debug | 23526792 / 23519631 | 4486009 / 4486016 | 18482297 / 18487976 |
| ide Release unity | 27635600 / 27640557 | 4260982 / 4260990 | 14750806 / 14767166 |
| ide Release separate | 30497376 / 30500626 | 4278885 / 4278919 | 18199949 / 18215999 |
| small archive executable | 9672 / 8297 | 307 / 320 | 2323 / 5208 |
| Buster-produced small | 13168 / 11130 | 354 / 368 | 2418 / 5632 |
| hot_reload GC | 8712408 / 8718293 | 422676 / 422688 | 7505444 / 7523040 |

Output layout/alignment and segment packing differ; both preserve tested behavior. No runtime performance ranking is derived from these layout differences. Same-linker repeated bytes are checked separately and pass; different linkers are not expected to produce equal bytes.

## Correctness and unsupported cells

| Check on supported release head | mold | Wild |
| --- | --- | --- |
| Executable/archive/shared runtime; symbol resolution smoke | PASS | PASS |
| Frozen physical regular-file hashes before/after; same-linker output repeatability | PASS | PASS |
| ide Debug and Release full llvm-dwarfdump verification under no GC | PASS | PASS |
| Actual ide source breakpoint and entry_point → buster_entry_point → main unwind | PASS | PASS |
| Buster-produced objects runtime, full DWARF, probe → main source/unwind | PASS | PASS |
| hot_reload self-test lifecycle (101 checks) | PASS | PASS |
| hot_reload required-GC full DWARF | FAIL / unsupported strict debug | FAIL / unsupported strict debug |
| Seven affected sanitized modules | 7/7; 464752/464752 assertions PASS | 7/7; 464752/464752 assertions PASS |

Affected modules: debug_model_tests, dwarf_tests, object_tests, jit_tests, link_tests, compiler_driver_tests, compiler_driver_object_path_tests. Sanitized test execution took 254034 ms mold / 247505 ms Wild; these are correctness runs, not repeated performance observations. No complete sanitizer/test matrix claim.

Actual negative statuses: C-driver GCC15/WILD generation 1, unsupported Wild option 255, missing explicit Clang linker path 1. No successful fallback. CMake <4.4, GCC16+, Windows/macOS Wild, LTO, large Buster-produced-object links and physical-host performance remain NOT RUN or unsupported as specified in the support guide.

## Reproduction, retained raw evidence and failed attempts

The optional workflow at the tested revision downloads checksum-pinned tools and the frozen input artifact, compiles the C harness through existing infrastructure and executes only GitHub-hosted ubuntu-26.04. The current source/release matrix additionally builds both upstream executables outside the repository with optimized O3, no LTO/PGO, generic x86-64, stripped, libc allocator and LLVM linker. Wild uses Rust 1.94.0 Cargo --locked; mold uses Clang21 and system TBB/zlib/zstd. Different language/compiler/runtime/library configurations are disclosed rather than presented as identical upstream production recipes. Source-built results will be kept separate from release-package results and hosts.

Permanent release evidence is under [evidence/2026-10-04-issue-2645/release](evidence/2026-10-04-issue-2645/release/): raw samples, complete command identity mapping, summary/MAD/CI, host, toolchain, manifests, trace paths and selected diagnostics. The complete compact text/log ZIP is retained there; it contains every original command log and the parent's exploratory CSVs, not upstream executables.

| Campaign / artifact | Disposition / SHA-256 |
| --- | --- |
| [37219746626](https://github.com/buster14a/buster/actions/runs/37219746626), artifact 11309284207 | Original inputs and exploratory compiler-driver campaign. ZIP `5d6a68e18197af0e82f2b3620049a3715d62534395c47deb5d83532ba883941b`. |
| [37222364481](https://github.com/buster14a/buster/actions/runs/37222364481), full 11311201918 | Supported release PASS, ZIP `3e39416caed4d9477ddf51478e662f728c47db5981eea06f79136744f7093e28`. |
| Same run, compact 11311241663 | Permanent text/log snapshot, ZIP `93ce42a4a38655be969474c6746465fe941b379ce4f66396882b95891e148f5b`. |
| [37222100794](https://github.com/buster14a/buster/actions/runs/37222100794), source full 11311610104 | Both source linkers built; freeze validation FAIL because TBB reads mutable /proc/meminfo. No ranking. ZIP `edbd01881b5a186496072e125ed9a62affb548d0c5653a975f9511416117ff01`. |
| Same source run, compact 11310698711 | ZIP `be3aadccfa1107ca8ba10f1f2a8093576ae6b2be5835282f72aef66a0a24140c`. |

Parent [37218792170](https://github.com/buster14a/buster/actions/runs/37218792170) failed setup grep; [37219407554](https://github.com/buster14a/buster/actions/runs/37219407554) exposed missing gdb / wrong expected linker spelling. Supplement [37220734233](https://github.com/buster14a/buster/actions/runs/37220734233), [37220871120](https://github.com/buster14a/buster/actions/runs/37220871120), [37221223407](https://github.com/buster14a/buster/actions/runs/37221223407) retained DWARF failures under unmatched default GC policies. [37221761003](https://github.com/buster14a/buster/actions/runs/37221761003) correctly matched ide policies but incorrectly disabled hot_reload's required GC, exposing unused ir_symbol_from_id undefined references. [37221954214](https://github.com/buster14a/buster/actions/runs/37221954214) failed absent cgroup quota-file setup; [37221954221](https://github.com/buster14a/buster/actions/runs/37221954221) failed the initial Cargo package spelling. These are recorded failures, not silently removed samples.

Original performance numbers are exploratory because Wild default GC differs from mold, build-ID algorithms differ, and default linker forking hides resource accounting. They are not used for the equal-policy ranking. The corrected source campaign excludes virtual host telemetry from immutable input hashing while preserving all successful-open traces; it does not relax checks on objects/libraries/scripts/startup inputs.

Normal CI initially rejected +41670 build-category bytes from duplicate workflows. Consolidation into a single matrix reduced this branch's parent-relative addition to 32318 bytes and restored the unchanged 32-KiB-per-category gate. No baseline/gate was weakened. Parent-first integration and ordinary protected queue admission remain required; a stacked PR cannot be merged into the original owner's branch by this writer.

## AArch64 hosted qualification

**PASS:** [37225436360](https://github.com/buster14a/buster/actions/runs/37225436360), exact research head `776b62bbc6ad84c0f982535d40f41e270c20bfb4`, reusing parent C process/smoke infrastructure. GitHub-hosted ubuntu-24.04-arm, Clang/LLVM 18.1.3, glibc 2.39-0ubuntu8.9, GDB15.1 and CMake4.4.3. Both pinned AArch64 linkers pass archive/executable/shared runtime, independent full-DWARF and fixture source debugging. Actual C-driver WILD selection builds ide, with generated `--ld-path=wild` and AArch64 ELF inspected; runtime object emission, full DWARF and entry_point/buster_entry_point source/unwind PASS. The same Buster-produced ARM main/probe objects link externally and run correctly with both linkers, with independent DWARF PASS. Native linking remains unchanged. ARM performance, sanitizer and LTO matrices are NOT RUN.

ARM Wild executable SHA-256 `861b51fbaec4c2737ee6a9d8bbbd25475ab87b0522c9c2ef13d3e0fabcb17806`; mold `7b78f4dcd2709dcab1695a3237cf33b270224ee94177487cd43c572a63521b8d`. ARM package archive hashes: Wild `e9d670e41f76481a68984f816e25bd2f124664db3ac935053e1a6fc41d2894c2`, mold `16b025652d3d7456689e6025a77e1903bb2a15e7630877c26cc133f5df95b9c6`, CMake `2efc974dbd63b4444c0e8494b92f2e80c2d7e635b4b80eac2916985ddd8f72a6`. Same exact upstream source/license pins as x86. Full artifact 11311458119 ZIP SHA-256 `5c44ce7c96b5e5a98eeb0d6926daaae2673ce173a4ef9bef134b661ddfd696e0`; permanent text/log snapshot and raw commands/statuses under evidence/2026-10-04-issue-2645/arm. First smoke-only run37225289790 also passed (artifact11312215667, SHA-2568426b28bf20695fca029653bc69156233d545e9fd3ca62ce1c2ae4c8d4f577d5); no timing comparison is made from these correctness commands.

## Normal integration failures and repairs

Parent Buster CI run37219910136 failed restricted workflow parsing because the slash-containing flow-list branch scalar was unquoted; the same inherited file blocked downstream CI. #2647 quotes that scalar only in its own copy. The original branch remains untouched. The optional research workflow also used a pinned but unapproved download-artifact action; it now downloads the fixed artifact with the runner's existing gh CLI and read-only token instead. The approved-action policy is unchanged. The research workflow is now manual-only to avoid repeating long campaigns after documentation edits. Ordinary CI still runs; successful research is not proof of protected merge-group success.

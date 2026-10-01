# Pinned external C capability portfolio

Area: `compiler`. Owning issues: [#2185](https://github.com/buster14a/buster/issues/2185)
and [#1397](https://github.com/buster14a/buster/issues/1397).
Compiler baseline: `133e11167a0e6818a6e226aedab68560add0673c`,
tree `78f03b9fb1bfe39eca536ab927e5707138dabbc9`.
The baseline experiment `8926a2a91b1899cbbcf966216d2834e39fee933b` adds
only the isolated hosted workflow; production compiler and harness sources
are unchanged.

This experiment measures correctness on GitHub-hosted Ubuntu 26.04 x86-64.
It does not use a laptop or benchpress/9700X and is not performance acceptance.
No upstream source is patched or vendored. The native C build driver owns
compiler generation, building, self-hosting and harness orchestration.

## Portfolio and independent evidence

| Target and immutable source | Independent requirement | Baseline result |
|---|---|---|
| cJSON 1.7.19, `c859b25da02955fef659d658b8f324b5cde87be3` | Compact allocated object graph and floating formatting; positive control | Full existing harness passes; final `CJSON_IDE_LINK` compiles, links and executes through Buster |
| zlib 1.3.1, `51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf` | Multi-file static archive, bitwise compression, gzip I/O and cross-link directions | All four allocator runtime rows pass; subsequent upstream configure/make gate fails |
| stb, `2c980bb59875b0d32144a71867fbdebb2f77cd20` | Header implementations, macro places, image/font layouts and hash-table churn | Four implementations, four allocators and two applicable upstream tests pass |
| Lua 5.4.8, Git `6e22fedb74cf0c9b6656e9fce8b7331db847c605` plus official release archive below | Multi-unit VM, protected errors, closures, coroutines, varargs and bytecode | 34 production units, four allocator link/workload gates and upstream FAST suite pass |
| QuickJS 2026-06-04, `3d5e064e9dd67c70f7962836505a7fa067bf0a4e` | Tagged values, computed control flow, generated REPL, POSIX callbacks/workers and native executable linking | Five FAST library units compile; `quickjs-libc.c:196:63` is rejected for void/function-pointer conversion |

The Lua archive SHA-256 is
`4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae`.
The harness authenticates release-only `luac.c` and compares overlapping
production files against the pristine Git input.

cJSON's upstream tests, all stb links and zlib's existing runtime links use
host Clang. Lua's interpreter/compiler and QuickJS's executables use Buster
linking; Lua's fixture shared libraries and first-party atexit shim use Clang.
Object emission alone is not a successful external execution path.

## Blocker-to-target map and selected investment

| Finding | Category | Targets and evidence | Disposition |
|---|---|---|---|
| #1397 void/function-pointer assignment | Intentional language-extension policy; ISO source constraint violation | Current pristine QuickJS `quickjs-libc.c:196`; owning issue also records Janet callback storage consumers | Selected: one shared semantic predicate, existing canonical conversions |
| #1399 configure script observes an absolute path and enables `-include zconf.h` | Harness/configuration error | Current zlib configure/make failure after passing library runtime tests | Reported on existing owner; build driver/spawn policy untouched |
| #1418 `-include` | Driver capability gap | zlib's out-of-tree path requests it; not the documented intended in-tree harness configuration | Separate from #1399; not implemented here |
| Old #1421/#1486 comma and prefix-store failures | Previously reported compiler defects | Current stb/Lua/zlib units pass those points after merged #1354/#1393 | Not treated as live portfolio blockers |
| QuickJS NONE/MIR_STACK stack limits and previous NONE exec crash | Generated-frame/runtime behavior | Historical #1397 scratch experiment; must be rechecked after admission | Retain full harness verdicts, never silently drop failing modes |
| Host headers and runtime libraries | Environment/configuration inputs | Hosted glibc headers; Lua readline; QuickJS libm/pthreads/libdl | Record actual tools and dependency installation receipt |

The selected fix applies only in GNU dialects on x86-64/AArch64 Linux/macOS.
ISO modes retain diagnostics. Other targets, direct incompatible function
signatures, non-void object/function-pointer pairs, nested pointer slots and
qualifier loss retain the previous rejection. This is an explicit extension,
not an assertion that ISO C permits these implicit conversions or that POSIX
requires them. N1570 6.5.16.1p1, 6.7.9p11 and J.5.7 define the ISO boundary;
POSIX `dlsym` describes callable conversion through `void *`.
Only real function addresses recovered with their original compatible
signature are execution witnesses. No arbitrary data-address or wrong-signature
call is a differential correctness claim.

The reproduction and negative controls are embedded in the existing frontend
and driver test modules. The driver witness exercises a real callback through
local/global storage, assignment, arguments and returns, source linking and
separate object linking, both frontend SSA forms and each native allocator.
No second IR, frontend bypass, new dependency or production callback dispatch
is added.

## Reproduction and retained evidence

Build the Release `ide`, then invoke the unchanged existing commands:

```sh
./build.sh test_cjson --config Release /path/to/cjson
./build.sh test_zlib --config Release /path/to/zlib
./build.sh test_stb --config Release /path/to/stb
./build.sh test_lua --config Release /path/to/lua-5.4.8/src /path/to/lua-git
./build.sh test_quickjs --config Release /path/to/quickjs
./build.sh test_self_host --config Release
./build.sh build --config Release -t test_all
```

The isolated `External C capability portfolio` workflow reproduces pins,
records commit/tree/file-object identities, authenticates the Lua transport,
copies license notices, hashes first-party fixtures and records compiler/tool
identities. Generated wrappers, objects, archives, corpora, bytecode and logs
are retained as artifacts. It records independent step outcomes even when
another harness fails. Optional Test262 is not supplied; that stage reports
skipped explicitly.

Baseline [run 36921162420](https://github.com/buster14a/buster/actions/runs/36921162420),
[job 110567275786](https://github.com/buster14a/buster/actions/runs/36921162420/job/110567275786).
Artifact `11192027736`, SHA-256
`ce4ae9c8f77e9c8905964f1131a4e0d6390f95614f49fc87cdeadfe71d77c1bc`.
Self-host stage1/stage2 are byte-identical, 41,121,024 bytes.
Candidate execution and complete-path qualification remain pending until
the immutable candidate's cloud run finishes.

## Licenses and fixture provenance

| Project/component | Verified terms at the inspected pin | Provenance/exception |
|---|---|---|
| [cJSON](https://github.com/DaveGamble/cJSON/blob/c859b25da02955fef659d658b8f324b5cde87be3/LICENSE) | MIT | Pristine Git tree; generated probe is first-party |
| [cJSON Unity](https://github.com/DaveGamble/cJSON/blob/c859b25da02955fef659d658b8f324b5cde87be3/tests/unity/docs/license.txt) | MIT | Built upstream test support has its own notice |
| [JSON Patch fixtures](https://github.com/DaveGamble/cJSON/blob/c859b25da02955fef659d658b8f324b5cde87be3/tests/json-patch-tests/README.md) | Apache-2.0 | Consumed fixture directory has separate upstream attribution |
| [zlib](https://github.com/madler/zlib/blob/51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf/LICENSE) | Zlib | Deterministic first-party corpus; unused contrib/dotzlib is BSL-1.0 |
| [stb](https://github.com/nothings/stb/blob/2c980bb59875b0d32144a71867fbdebb2f77cd20/LICENSE) | MIT OR Unlicense | Four selected headers repeat terms; unused PngSuite/herringbone assets have separate permissive grants |
| stb probe fixtures | First-party source identity retained | Synthetic font generated by `stb_build_fixture_font`; embedded PNG in `tests/basic_stb_compat.c` has no separate origin annotation, so earlier origin remains unverified |
| [Lua](https://github.com/lua/lua/blob/6e22fedb74cf0c9b6656e9fce8b7331db847c605/lua.h) and suite | MIT | `testes/all.lua` repeats MIT; release transport authenticated; workload generated by first-party `lua_write_workload` |
| [QuickJS](https://github.com/bellard/quickjs/blob/3d5e064e9dd67c70f7962836505a7fa067bf0a4e/LICENSE) | MIT | Nine selected C units and `repl.js` carry MIT; inspected scripts have no override |
| QuickJS Unicode table | Unicode-3.0 for Unicode data; MIT generator | Pinned generated table source retained; downloader selects Unicode17.0.0, but exact original generation inputs/receipt are absent and remain unverified |

External source is not incorporated into Buster. License notices and exact
Git objects establish inspected-source provenance; missing earlier asset or
table-generation provenance is not silently promoted to verified status.
The next shared blocker will be ranked from candidate runtime evidence,
with source-reported untested targets kept separate from completed paths.

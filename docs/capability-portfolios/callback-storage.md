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
| SQLite 3.53.4, Fossil `bf7c7f30031888f4e796e429ab3978879485813aaca6f641c7b33e4e09459bcc` | Serialized database bytes, transactions, backup/restore and multi-process recovery; independent of VM execution | Current baseline rejects math-callback `aBuiltinFunc` initializer in first library stage |

The Lua archive SHA-256 is
`4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae`.
The harness authenticates release-only `luac.c` and compares overlapping
production files against the pristine Git input.

cJSON's upstream tests, all stb links and zlib's existing runtime links use
host Clang. Lua's interpreter/compiler and QuickJS's executables use Buster
linking; Lua's fixture shared libraries and first-party atexit shim use Clang.
Object emission alone is not a successful external execution path.

## Configurations actually exercised

All executed targets use native Linux x86-64, the hosted Clang-built Release
compiler and existing harness mode/flags. No macOS, AArch64, Windows or mobile
external execution is inferred from semantic tests.

| Target | Existing configuration and boundary |
|---|---|
| cJSON | `cJSON.c`, utilities and 21 upstream tests; host-linked upstream suite plus Buster's final own-link probe |
| zlib | Static library, `-O2 -D_LARGEFILE64_SOURCE=1 -DHAVE_HIDDEN`; four allocators and host cross-links; configure/make remains failing |
| stb | Four selected implementation headers with **`-DSTBI_NO_SIMD`**, host-linked probes and two applicable upstream tests; no SIMD claim |
| Lua | `-O2 -DLUA_USE_LINUX -DLUA_USE_READLINE`, authenticated release/Git overlap, Buster-linked interpreter/compiler; upstream suite under FAST |
| QuickJS | Upstream default JSValue configuration, `-O2 -funsigned-char -fwrapv -D_GNU_SOURCE`, no LTO/sanitizer/shared-library variant; inherited 512 MB host stack and symmetric `--stack-size 64M`; optional Test262 absent |
| SQLite | Unchanged harness: `-O2 -DSQLITE_ENABLE_MATH_FUNCTIONS -DSQLITE_ENABLE_COLUMN_METADATA`, threadsafe 1/0, native default GNU17; `SQLITE_DEBUG`, configure/make and Tcl `testfixture` excluded by existing scope |

Existing stb scope excludes `STBDS_UNIT_TESTS` because the reference Clang
also rejects that pinned upstream test shape, and the truetype demo requiring
hardcoded host font paths. The first-party synthetic font retains its generator.
These are existing explicit harness choices; this change does not remove
failing tests or alter upstream programs. The QuickJS fallback is reached only
in its failed NONE path; the newly qualified FAST path generates its own
bytecode identically.

## Blocker-to-target map and selected investment

| Finding | Category | Targets and evidence | Disposition |
|---|---|---|---|
| #1397 void/function-pointer assignment | Intentional language-extension policy; ISO source constraint violation | Current pristine QuickJS `quickjs-libc.c:196`; pinned SQLite math callback table and Janet's NANBOX64 callback argument share the conversion | Selected: one shared semantic predicate, existing canonical conversions |
| #1399 configure script observes an absolute path and enables `-include zconf.h` | Harness/configuration error | Current zlib configure/make failure after passing library runtime tests | Reported on existing owner; build driver/spawn policy untouched |
| #1414 SQLite `aBuiltinFunc` initializer | Compiler rejection from the shared callback conversion policy | Current baseline rejects pinned SQLite 3.53.4; candidate differs only in the shared production predicate and passes the unchanged full harness | Same fix unlocks the second complete path |
| #1418 `-include` | Driver capability gap | zlib's out-of-tree path requests it; not the documented intended in-tree harness configuration | Separate from #1399; not implemented here |
| Old #1421/#1486 comma and prefix-store failures | Previously reported compiler defects | Current stb/Lua/zlib units pass those points after merged #1354/#1393 | Not treated as live portfolio blockers |
| QuickJS NONE/MIR_STACK stack limits and previous NONE exec crash | Generated-frame/runtime behavior | Historical #1397 scratch experiment; must be rechecked after admission | Retain full harness verdicts, never silently drop failing modes |
| Host headers and runtime libraries | Host-header assumptions and runtime facilities | Hosted glibc headers; Lua readline; QuickJS libm/pthreads/libdl | Record actual tools and dependency installation receipt; no missing runtime facility observed in the passing paths |
| C99 inline external definition (#1277) | ABI/linking gap | Current emission source is consistent with the reported duplicate-symbol issue, but no portfolio target demonstrates it | Retain as a bounded fallback; do not claim it blocks these pins |

The selected fix applies only in GNU dialects on x86-64/AArch64 Linux/macOS.
ISO modes retain diagnostics (the ISO requirement is a diagnostic, not mandatory refusal; Buster retains its error policy). Other targets, direct incompatible function
signatures, non-void object/function-pointer pairs, nested pointer slots and
qualifier loss retain the previous rejection. This is an explicit extension,
not an assertion that ISO C permits these implicit conversions or that POSIX
requires them. [WG14 N1570](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf)
6.5.16.1p1, 6.7.9p11 and J.5.7 define the ISO boundary;
[POSIX.1 `dlsym`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dlsym.html)
describes callable conversion through `void *`.
The [GNU manual](https://www.gnu.org/software/c-intro-and-ref/manual/html_node/Function-Pointers.html)
does not promise automatic void/function-pointer conversions. Existing #1397
records default GCC/Clang acceptance and strict-pedantic rejection; those are
implementation observations, not a portable ISO contract.
Only real function addresses recovered with their original compatible
signature are execution witnesses. No arbitrary data-address or wrong-signature
call is a differential correctness claim.

The reproduction and negative controls are embedded in the existing frontend
and driver test modules. The driver witness exercises a real callback through
local/global storage, assignment, arguments and returns, source linking and
separate object linking, both frontend SSA forms and each native allocator.
No second IR, frontend bypass, new dependency or production callback dispatch
is added.

## Additional shared source consumers

SQLite 3.53.4 is added as a sixth execution target because its prescribed
database bytes, transactions, backup/restore and multi-process recovery add
evidence independent of either VM. Its Fossil manifest is
`bf7c7f30031888f4e796e429ab3978879485813aaca6f641c7b33e4e09459bcc`;
the official Git mirror `b09c88c14082339b66c7b7158d609a771e64ca69`
records that same origin. The unchanged harness checks all consumed file
identities and enables math functions under default GNU17. In pinned
`src/func.c` (blob `d86588d853bb0cf71d575e2a70177f7c917a9081`),
`MFUNCTION` stores `xCeil` in `void *pUserData` and later casts it back to
the original `double (*)(double)` signature. `sqliteInt.h` is blob
`093cb1a548e222edce7fc05e46375b09742734e8`.
The current baseline reproduces #1414's initializer rejection; the candidate
passes the unchanged full harness. Exact evidence follows below.

Janet 1.38.0 is a separately inspected source consumer, not an execution
acceptance target: pin `73334f34857b0124546ce79b4bda094a4b18a019`.
On its default x86-64 NANBOX64 path, `janet_wrap_cfunction` passes a
`JanetCFunction` to `janet_nanbox_from_pointer(void *, uint64_t)`.
Its upstream Makefile uses **`-std=c99`**; this GNU-only policy does not
unlock that default build. A GNU dialect configuration requires explicit
documentation and fresh build/runtime testing.

| Janet source consumer | Retained Git blob |
|---|---|
| `src/core/capi.c` | `5a6a2e68770ba259f6636a5e5554df049cd2c290` |
| `src/core/ffi.c` | `81483b6a43e13ec1080c7ea90be55b076d232090` |
| `src/core/marsh.c` | `13b3344bf7597c78ff74102b1abdbd4cbbb2f8c9` |
| `src/core/util.c` | `db65deb23ed16d9dcfb26df5027ea2245a5fa82d` |
| `src/core/wrap.c` | `c4fc1d16f5614556248e493d478976c82754ab41` |
| `src/mainclient/shell.c` | `3d95ee2da14e678e6f4feebfb2c753d0b37b5914` |

## Reproduction and retained evidence

Build the Release `ide`, then invoke the unchanged existing commands:

```sh
./build.sh test_cjson --config Release /path/to/cjson
./build.sh test_zlib --config Release /path/to/zlib
./build.sh test_stb --config Release /path/to/stb
./build.sh test_lua --config Release /path/to/lua-5.4.8/src /path/to/lua-git
./build.sh test_quickjs --config Release /path/to/quickjs
./build.sh test_sqlite --config Release /path/to/sqlite-amalgamation-3530400 /path/to/sqlite-src-3530400
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
Candidate `ebbed5725a5d0c01a5f4ae0edd5c0114b8847d85`, tree
`b67f14fdb4e23313dd09f4f42c1fa96579c978da`:
[run 36923707874](https://github.com/buster14a/buster/actions/runs/36923707874),
[job 110575722692](https://github.com/buster14a/buster/actions/runs/36923707874/job/110575722692).
Artifact `11193073685`, SHA-256
`6b37f10bfbc27f9d81082e4565e8e85924c43c4009cf796b2fb5b18026bafc05`.
The full Release suite passes **5,038,299/5,038,299 assertions, 56/56 modules**.
Both new registered fixtures execute. The 13-target × 9-dialect × 18-source
semantic matrix checks admission without relying on the production predicate;
the 16 linked runtime routes cover four allocators, two frontend SSA forms and
source/separate-object linking on Linux x86-64. Self-host stage1/stage2 remain
byte-identical, 41,121,328 bytes. Source-size growth is production +1,051 bytes
and build +8,344 bytes, both within the 32 KiB limit; whitespace passes.

The unchanged QuickJS **FAST** path compiles all nine C units with no library
machine fallback, links `qjsc`, `qjs` and `run-test262` through Buster,
produces byte-identical REPL/workload bytecode, passes all nine upstream scripts,
matches the 1,563-byte workload transcript and the 914-byte memory report.
NONE compiles and links but fails `tests/test_std.js`. Its generated REPL
uses the existing explicitly logged reference fallback after engine-stack
exhaustion. MIR_STACK and QUALITY are not reached because the unchanged harness
stops on this failure. Optional Test262 was not supplied and is explicitly
skipped. **The full QuickJS harness remains failing**; only FAST has this
complete new execution qualification. Other target families and GNU dialects
are semantic policy coverage, not external runtime qualification.

cJSON, stb and Lua remain passing. zlib's four-allocator library execution
still passes before the known configure/make failure. The workflow stays red
for those genuine external failures; no failing test is disabled or ignored.
Its artifact expires after 30 days; immutable logs, source/pin receipts and
artifact digests identify the exact reproduced result.

## Extended immutable cloud result

Candidate `dce10d91f8b0e8920ae838f948b1a79fa00d10a5`, tree
`21486376855766d01f466a43a7c6f4720ca7e6cd`, changes only the workflow relative to `ebbed5725`;
production/compiler-test blobs are identical.
[Run 36925474093](https://github.com/buster14a/buster/actions/runs/36925474093)
has a separately pinned baseline
[job 110581616112](https://github.com/buster14a/buster/actions/runs/36925474093/job/110581616112)
and candidate
[job 110581616329](https://github.com/buster14a/buster/actions/runs/36925474093/job/110581616329).

| Gate | Actual result |
|---|---|
| Baseline SQLite | Original `aBuiltinFunc` rejection; no Buster library object emitted |
| Candidate SQLite | **PASS**, configurations=2, allocators=4, workloads=8, upstream_scripts=4 |
| SQLite execution evidence | Buster-linked CLI and upstream programs; paired SQL transcripts/database files, backup/restore/integrity checks and threadsafe mptest recovery scripts |
| QuickJS | FAST complete path passes again; NONE still fails `test_std.js`; later modes not reached |
| Positive controls | cJSON, stb and Lua pass; zlib library rows pass then known configure gate fails |
| Full Release suite | **5,046,790/5,046,790 assertions, 56/56 modules** |
| Self-host | Stage1/stage2 byte-identical, **41,141,744 bytes** |
| Source size/whitespace | PASS; production +1,051 bytes, build +12,798 bytes; both within 32 KiB |

Candidate artifact
[`11194526415`](https://github.com/buster14a/buster/actions/runs/36925474093/artifacts/11194526415),
SHA-256 `4cb3b2e497d3bcd8b39366d44887da232cabcb6803771822f2e0350aa46a7b92`.
Baseline SQLite artifact
[`11194460380`](https://github.com/buster14a/buster/actions/runs/36925474093/artifacts/11194460380),
SHA-256 `eb1de2258a45d692b92de88abfe4bf7f6f4defbf9c8f257a8b382c4e59a8931e`.
Artifacts retain both official SQLite transports, their hashes, complete
per-file receipts, Fossil identity, licenses, generated databases/fixtures,
objects and logs. Amalgamation ZIP SHA-256:
`1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d`.
No `SQLITE_DEBUG`, Tcl testfixture, configure/make, other platforms, or optional
Test262 execution is claimed.

The workflow repeats the corpus on compiler source/test changes and supports
manual dispatch. Documentation-only receipt updates use the PR's checks.
This does not convert the recorded zlib/QuickJS failures to successful verdicts.

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
| [SQLite](https://github.com/sqlite/sqlite/blob/b09c88c14082339b66c7b7158d609a771e64ca69/LICENSE.md) core, shell and tests | Public-domain dedication; not an invented SPDX license | Official transports, Fossil identity and all consumed file hashes retained; generated SQL/database fixtures are first-party harness output |
| SQLite `configure`, `autosetup/`, `autoconf/` (unused) | BSD-style exceptions in the pinned license inventory; `autosetup/LICENSE` BSD-2-Clause | Outside the amalgamation harness; notice retained, blob `4fe636c9d9126a307007629072f30e59e383f7b2` |
| [Janet](https://github.com/janet-lang/janet/blob/73334f34857b0124546ce79b4bda094a4b18a019/LICENSE) (source inspection only) | MIT root and each of six consumers | License blob `ceaa6a0eb911f2cd9358bdce9e658e9f8929ceb3`; no fixtures inspected |
| Janet embedded SipHash in `util.c` | Pinned source attributes public-domain adaptation | Original `veorq/SipHash` import revision absent; earlier provenance remains unverified |

External source is not incorporated into Buster. License notices and exact
Git objects establish inspected-source provenance; missing earlier asset or
table-generation provenance is not silently promoted to verified status.
## Next shared blocker

The highest-leverage next **shared capability census** is
[#1419](https://github.com/buster14a/buster/issues/1419), host intrinsic-header
interoperability. Its issue reports a cross-target compile-only barrier and a
later Clang20 reproduction; current `c_source.c` still lacks the reported
`__builtin_ia32_emms`/`__builtin_ia32_clui` registrations. This session has not
executed that census or inspected its external project sources/licenses.
Freeze the exact resource-header source/license and target configuration,
separate unused wrappers from required intrinsics, then obtain complete
unchanged target paths before choosing a repair. Do not stub unknown operations
or stop checking invalid bodies.

The immediate newly observable QuickJS gate is NONE's `test_std.js` failure.
Historical #1397 locates a `js_os_exec` crash, but this cloud transcript proves
only the script failure, not its current signal/backtrace or root cause.
Reduce it with preserved pinned input, mode flags and function/object identities
before calling it the same defect. It is not evidence of a shared cross-target
blocker yet. #1399 remains the separate zlib configuration owner; #1277's C99
inline linkage is a smaller normative candidate with less demonstrated
real-target leverage.

The fixture/table provenance limits above are also reported on
[#620](https://github.com/buster14a/buster/issues/620); no license or origin is
silently inferred from public availability.

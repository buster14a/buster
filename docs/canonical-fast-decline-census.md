# FAST decline census for #1602

Observed 2026-10-12 on Linux x86-64, Clang 23.1.1 trusted Release `ide`,
tests enabled. Baseline `fab5f0ed259c424f6f7a5a8fcf2d42e5621bd781`;
candidate source `fe4b7e45919048e8d6fcfc865120b47dce1f8ac7`.
Compiler SHA-256 values and every attempted source/result are in the
[complete census evidence](compatibility-censuses/1602-fast-declines.json).
This is a compile-only diagnosis, with no compiler-throughput or runtime
performance claim. Generated build output and upstream sources are not committed.

## Inputs and procedure

The scope is the five pinned project families explicitly named in
[#1602](https://github.com/buster14a/buster/issues/1602), using the
[compatibility index](agents/compatibility.md) and current `build.c` recipes:

| Input | Pin and verification | License at the inspected pin |
|---|---|---|
| SQLite amalgamation | 3.53.4 official archive; all four file sizes and XXH64 values match `sqlite_verify_files` | Public domain, verified from the source blessing; no single SPDX license identifier asserted |
| Lua production sources | 5.4.8 official tarball SHA-256 `4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae`; release-only `luac.c` matches size 15,145 and XXH64 `6cdc1a7db5393273` | MIT, `doc/readme.html` license section |
| QuickJS | pristine `3d5e064e9dd67c70f7962836505a7fa067bf0a4e`, VERSION 2026-06-04 | MIT, [LICENSE](https://github.com/bellard/quickjs/blob/3d5e064e9dd67c70f7962836505a7fa067bf0a4e/LICENSE) |
| musl | pristine v1.2.6 `9fa28ece75d8a2191de7c5bb53bed224c5947417` | MIT for musl as a whole, plus component notices in [COPYRIGHT](https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT?h=v1.2.6) |
| CPython | pristine v3.13.9 `8183fa5e3f78ca6ab862de7fb8b14f3d929421e0` | PSF-2.0 plus historical and bundled component terms; [LICENSE](https://github.com/python/cpython/blob/8183fa5e3f78ca6ab862de7fb8b14f3d929421e0/LICENSE) is the authoritative inventory |

SQLite archive SHA-256 is
`1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d`.
Buster first-party licensing remains unselected per
[LICENSES/README.md](../LICENSES/README.md).

Each case uses `ide cc -v -march=native -c SOURCE -o OBJECT`, default FAST
allocator, the same source/flags for baseline and candidate, and a separate
object/log path. A failure is retained; the census continues with later TUs.
There are two concurrent compiler processes, a 120-second per-case diagnostic
deadline, and no retries or altered upstream files. No case timed out.

- SQLite: `sqlite3.c` and `shell.c` under both `SQLITE_THREADSAFE=1` and `0`,
  `-O2 -g0 -DSQLITE_ENABLE_MATH_FUNCTIONS -DSQLITE_ENABLE_COLUMN_METADATA`
  and the amalgamation include path. The four optional upstream test programs
  and runtime/database comparisons are outside this compile-only scope.
- Lua: all 34 release `src/*.c` files, `-O2 -g0 -DLUA_USE_LINUX
  -DLUA_USE_READLINE` and its source include path. The separate ltests-enabled
  interpreter and upstream Lua runtime tests are not run by this census.
- QuickJS: the nine nongenerated manifest sources, with the exact common
  `-D_GNU_SOURCE -DCONFIG_VERSION="2026-06-04" -DCONFIG_CC="clang"
  -DCONFIG_PREFIX="/usr/local" -O2 -funsigned-char -fwrapv -g0` flags and
  source include path. Generated REPL bytecode and runtime tests are excluded.
- musl: upstream's configured x86-64 object manifest supplies architecture
  replacements and generated headers; all 1,324 C TUs, no assembly. Buster
  uses the harness's `-std=c99 -nostdinc -fno-builtin -fno-strict-aliasing
  -fno-stack-protector -D_XOPEN_SOURCE=700 -O2 -g0` plus its seven include
  roots (architecture, generic, generated internal, source include, source
  internal, generated include, public include). No archive/runtime probe.
- CPython: out-of-source Clang configure/build supplies generated headers and
  compile commands. Configure selects `MODULE_BUILDTYPE=static`,
  `CFLAGS=-g -O3 -std=gnu11`, and the harness's disabled `Modules/Setup.local`
  entries. The census covers the union of 323 unique C sources emitted by the
  bootstrap/core and static-module builds. Every object-specific flag is
  retained and `-std=gnu11` restated last, matching the harness's make override.
  This host's optional dependency detection determines the module population;
  no interpreter/runtime or extension-ABI qualification is claimed.

Other compatibility-index families are outside this named-input census.
Sources failing before FAST have no admission verdict; their absence from the
decline list is not evidence that their canonical IR validates.

## Results and ownership

Both revisions have exactly the same per-case exit status: 1,667 compile,
27 fail before FAST. Baseline `validation_skips` is a module flag; candidate
`validation_skips` counts declined functions, so the values are not directly
interchangeable. All 16 formerly declined TUs remain observable; valid
neighbors now receive FAST.

| Project | Cases | Compile success | TUs with FAST declines | Candidate declined functions |
|---|---:|---:|---:|---:|
| sqlite | 4 | 4 | 0 | 0 |
| lua | 34 | 34 | 0 | 0 |
| quickjs | 9 | 8 | 0 | 0 |
| musl | 1324 | 1324 | 8 | 9 |
| cpython | 323 | 297 | 8 | 18 |

All 27 declined functions report `IR_VALIDATION_OPERATION` (11), unavailable
block, and the defining instruction of an incomplete external object's PLACE.
The common shape is taking an address without needing the object's layout.
The strict value validator nevertheless requires a resolved layout.
[#3777](https://github.com/buster14a/buster/issues/3777) owns this separate
producer/validator disagreement and its positive/negative regression follow-up.
This PR does not weaken validation or optimize those bodies.

| Translation unit | Declined functions | Candidate functions receiving FAST |
|---|---|---:|
| `musl/src/legacy/err.c` | `vwarn`, `vwarnx` | 6 |
| `musl/src/network/herror.c` | `herror` | 0 |
| `musl/src/stdio/printf.c` | `printf` | 0 |
| `musl/src/stdio/vprintf.c` | `vprintf` | 0 |
| `musl/src/stdio/vscanf.c` | `vscanf` | 0 |
| `musl/src/stdio/vwprintf.c` | `vwprintf` | 0 |
| `musl/src/stdio/vwscanf.c` | `vwscanf` | 0 |
| `musl/src/exit/assert.c` | `__assert_fail` | 0 |
| `cpython/Modules/_testlimitedcapi/bytearray.c` | `bytearray_check`, `bytearray_checkexact` | 10 |
| `cpython/Modules/_testlimitedcapi/bytes.c` | `bytes_checkexact`, `bytes_concat`, `bytes_concatanddel` | 17 |
| `cpython/Modules/_testlimitedcapi/complex.c` | `complex_check`, `complex_checkexact` | 7 |
| `cpython/Modules/_testlimitedcapi/dict.c` | `dict_checkexact` | 27 |
| `cpython/Modules/_testlimitedcapi/float.c` | `float_check`, `float_checkexact` | 10 |
| `cpython/Modules/_testlimitedcapi/list.c` | `list_check_exact` | 20 |
| `cpython/Modules/_testlimitedcapi/long.c` | `pylong_checkexact` | 45 |
| `cpython/Modules/_testlimitedcapi/set.c` | `set_check`, `set_checkexact`, `frozenset_check`, `frozenset_checkexact`, `anyset_check`, `anyset_checkexact` | 17 |

For all 27 functions, ELF symbol ranges have identical emitted bytes before
relocation between baseline and candidate. This comparison does not assert
whole-object equality or replace runtime compatibility tests. The focused IR
regression separately compares all declined instruction/value bytes, including
under transform-output checks. For example, `musl/src/legacy/err.c` now runs
FAST on six functions and declines only `vwarn` and `vwarnx`; the limited-API
`long.c` now runs FAST on 45 functions and declines only `pylong_checkexact`.

Pre-FAST blockers are unchanged on the candidate and have owning records:

- QuickJS `quickjs.c:60600` rejects the statement keyword `asm` under the
  harness's default flags: [#3778](https://github.com/buster14a/buster/issues/3778).
- CPython: 23 TUs reject required `Py_DECREF`/`Py_INCREF` expansion through
  existing bounded-inline budget/shape checks; two TUs (`socketmodule.c`,
  `_ssl.c`) reject this host's packed anonymous-union attribute in
  `/usr/include/linux/can.h:129`; `Programs/_testembed.c:702` cannot lower
  the local `warnoptions` initializer. Full per-source diagnostics are in
  the evidence. The [#3183 qualification report](https://github.com/buster14a/buster/issues/3183#issuecomment-6123988072)
  retains these blockers and [#48](https://github.com/buster14a/buster/issues/48)
  owns the mandatory-inline contract. No budgets or timeouts were increased.

The #1602 session owns this census report. Separate producer, compatibility
and OS fixes remain on their owning issues; no successor's acceptance or
completed implementation is implied by filing them.

## Focused reproduction

```c
struct Opaque;
extern struct Opaque stream;
extern int sink(struct Opaque*);
int wrapper(void) { return sink(&stream); }
int add(int a,int b) { int x=a+0; return x*1+b; }
```

`ide cc -v -g0 -c repro.c -o repro.o` changes from baseline
`functions=0 validation_skips=1` to candidate
`functions=1 validation_skips=1`, and prints
`IR_FAST_DECLINE source=repro.c function=wrapper scope=function error=11 ...`.
`add` goes from eight canonical instructions to four. `-fverify-codegen` still
rejects the strict defect, as it should until #3777 is resolved.

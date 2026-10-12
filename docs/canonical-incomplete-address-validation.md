# Strict validation of opaque object addresses

Issue [#3777](https://github.com/buster14a/buster/issues/3777), observed and
validated on 2026-10-12. This corrects the producer/validator disagreement
recorded in the [original FAST decline census](canonical-fast-decline-census.md).
The [complete before/after census evidence](compatibility-censuses/3777-incomplete-addresses.json)
retains compiler identities, every attempted source, flags, exit status, FAST
admission counts and all affected strict-validation results.

## Contract and regression coverage

An incomplete struct or union can name an external object, and `&object` or
`&*pointer` requires only an address. Canonical `GLOBAL` and `DEREFERENCE`
may therefore produce an incomplete aggregate `PLACE`, consumed only by
`ADDRESS_OF`. All other consumers still require a resolved storage layout.
Incomplete values, local allocations, field/index results and explicit
alignment claims still reject. Both label-metadata and metadata-free value
validation use the same layout predicate; symbol, pointer-element, category,
result-binding and provenance checks remain active.

The registered `ir_validate_incomplete_place_tests` exercises malformed raw
IR in both provenance paths and valid C in both frontend forms across Linux
x86-64, Win64, Linux AArch64, macOS AArch64, Wasm64 and eBPF layouts.
The valid source covers opaque struct/union extern addresses, a sink call,
pointer dereference/address cancellation and an ordinary arithmetic neighbor.
The real driver fixture formerly used to demonstrate this decline now requires
strict success and zero decline records. Its independent synthetic decline
publication and arena-lifetime controls remain in place.
Separate Linux native execution compiles the opaque-only unit with strict
codegen verification under FAST and QUALITY, links it with a Clang-built unit
that owns the complete definitions, and checks exact addresses, sink output
and the null-pointer round trip.

The issue's original reproducer fails on baseline main
`9a5378eabf0d92a8eff27b9e05e5c518efbaed09` with error 11 at `wrapper`'s
`GLOBAL` result. The candidate passes `-fverify-codegen` and reports
`IR_FAST functions=2 validation_skips=0`.

## Pinned compatibility census

The procedure repeats the musl and CPython portions of the original census,
using isolated pristine checkouts and copies of its generated header inputs.
The JSON records all 1,647 source/flag combinations and header SHA-256 values.
`COMPAT_ROOT` denotes the directory containing both pinned checkouts and their
out-of-source `musl-build` / `cpython-build` configuration trees. `WORK_ROOT`
is the otherwise irrelevant working directory for absolute-source musl commands.
Each case runs `ide cc -v -march=native FLAGS -c SOURCE -o OBJECT`, with two
concurrent compiler processes, a 120-second diagnostic deadline and no retries.
A separate pass adds `-fverify-codegen` for all 16 originally declining units.

| Input | Units | Baseline compiles | Candidate compiles | Declining units before/after | Declined functions before/after |
|---|---:|---:|---:|---:|---:|
| musl v1.2.6 | 1,324 | 1,324 | 1,324 | 8 / 0 | 9 / 0 |
| CPython v3.13.9 | 323 | 297 | 298 | 8 / 0 | 18 / 0 |

All 16 affected units pass strict verification. No previously compiling unit
fails, and no case times out. CPython `Modules/_testlimitedcapi/tuple.c` also
changes from a required-`Py_DECREF` inlining rejection to successful compilation.
The remaining 25 pre-FAST failures retain their baseline outcomes and owning
records on [#3183](https://github.com/buster14a/buster/issues/3183) and its linked
mandatory-inline work. This compile-only census does not qualify a complete
musl archive, CPython interpreter, runtime suite or performance result.

## Licenses and input identities

- Buster first-party licensing remains unselected, verified from
  [LICENSES/README.md](../LICENSES/README.md).
- musl v1.2.6 is pristine commit `9fa28ece75d8a2191de7c5bb53bed224c5947417`:
  MIT for musl as a whole, with component notices retained in the verified
  [COPYRIGHT](https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT?h=v1.2.6).
- CPython v3.13.9 is pristine commit `8183fa5e3f78ca6ab862de7fb8b14f3d929421e0`:
  PSF-2.0 plus historical and bundled component terms, retained in the verified
  [LICENSE](https://github.com/python/cpython/blob/8183fa5e3f78ca6ab862de7fb8b14f3d929421e0/LICENSE).

No upstream source or generated build output is added to Buster. Source/test
revision and compiler SHA-256 values are bound in the JSON; later report-only
commits do not change those inputs. Required full-suite and self-host results
are recorded with the exact submitted revision on the issue and PR.

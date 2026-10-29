# c-testsuite, end to end

[#145](https://github.com/buster14a/buster/issues/145) runs
[c-testsuite](https://github.com/c-testsuite/c-testsuite)'s programs through the
whole compiler: the checker and its typed-stream producer
([#133](https://github.com/buster14a/buster/issues/133),
[the checker guide](c-checker.md)), Tier 0
([#136](https://github.com/buster14a/buster/issues/136), [the Tier 0 guide](tier0.md))
and the direct path's link against the C runtime
([the linker design](object-writer-and-linker.md)). `c_testsuite.c` is the
harness.

## The command

```sh
./build/compiler test-c-testsuite [--subset FILE] [--jobs N] [--timeout SECONDS] [--compiler PATH] [--clang PATH] [--id-order N] [--cross-check] [--keep] DIR
./build/compiler test-c-testsuite --self-test
```

DIR is a checkout of c-testsuite or its `tests/single-exec` directory, relative
to the current directory or not. Each `NAME.c` there must exit with status 0
having written `NAME.c.expected`'s bytes to its standard output and errors.
For each, the harness:

1. compiles and links it with `compiler --verify NAME.c -o program -lm` (the
   vector paths, every function's records checked by `x86_validate`, with libm
   as the cross-check's `cc` links it) and with `--scalar` too (the scalar
   twins), its identifiers' IDs in another order (the driver's `--id-order`,
   [#153](https://github.com/buster14a/buster/issues/153)'s test option, the
   Nth test's from `--id-order N` on, 153 by default): the two executables must
   be the same bytes, or the two refusals the same diagnostics;
2. runs the program in a directory of its own, with standard input from
   `/dev/null`, under `--timeout` (10 s; a compile gets 120 s), killing its
   process group at the deadline;
3. with `--cross-check`, which needs `readelf`, `cc` and clang (`--clang PATH`,
   `clang` by default), also writes the object with `-c`, lists it with
   `readelf -a -W` (no warnings or errors), links it with `cc ... -lm` and runs
   that, which must give the expected output too. `-c` may refuse a test only
   where the direct path refused it too; a test the direct path can't link but
   `-c` takes must still run right. Then the object must define the functions and
   objects that `clang -O0 -c`'s object of the test defines, no more and no fewer:
   [#129](https://github.com/buster14a/buster/issues/129)'s selection of what to
   lower, compared as [the lowering guide](c-lowering.md) describes ("Against
   clang -O0's objects").

A test passes when everything it was put through gave the expected status and
output. A refusal that names a feature (`error: unsupported: FEATURE`) makes it
unsupported, with that feature, when both paths exit as the compiler does for a
unit with errors (status 1). Anything else fails it: another error, a crash or a
timeout of the compiler or the program, even after it named a feature, a wrong
status or output, the two paths disagreeing. `-c`'s refusals in the cross-check
count only when they exit so too.

The tests run `--jobs` at a time (one per core by default), in worker
processes that write into a shared mapping; the report lists every test in name
order with its outcome and detail, then the counts. `--subset FILE` lists the
tests that must pass (a name a line; `#` starts a comment). The exit status is
1 if a test fails, if one of the subset's doesn't pass or isn't in DIR, or if
the harness can't run. `--compiler PATH` tests another compiler executable with
the same options, and `--keep` keeps the work directory (under `$TMPDIR` or
`/tmp`), which a failure keeps anyway.

`--self-test` (about 6 s) runs the harness on three tests of its own with a
stand-in compiler, a shell script, in six scenarios: from a DIR and a `$TMPDIR`
relative to the current directory (the processes run elsewhere, so the harness
makes every path absolute); waiting by polling as it does where `pidfd_open`
fails; with `--cross-check` where `-c` refuses a test the direct path compiles,
which must fail; with refusals that name their feature but are then killed (by
a signal in the scalar path alone and in both, and for their time in both),
each of which must fail; with `--cross-check` where `-c`'s refusals are
then killed, which must fail whether or not the direct path compiled the test;
and with `--cross-check` where `-c` writes a real object (`cc`'s) that defines a
function more than `clang -O0 -c`'s, which must fail though its program passes.
The stand-in's third program, which sleeps 30 s, must be killed at a one-second
timeout, and so must its compiles where they sleep.

The harness is test tooling outside the compile path and has no vector path.

## The pinned tests and the subset

The tests are c-testsuite at commit `5c7275656d751de0e68b2d340a95b5681858ed07`,
220 programs in `tests/single-exec`:

```sh
git init -q --template= build/c-testsuite
git -C build/c-testsuite fetch -q --depth 1 https://github.com/c-testsuite/c-testsuite 5c7275656d751de0e68b2d340a95b5681858ed07
git -C build/c-testsuite checkout -q FETCH_HEAD
./build/compiler test-c-testsuite --cross-check --subset tests/c-testsuite/subset.txt build/c-testsuite
```

`tests/c-testsuite/subset.txt` declares all 220: every test must pass.

00140 joined the subset with [#137](https://github.com/buster14a/buster/issues/137)'s
structs and unions by value and variadic function definitions, and 00204 with
`long double` ([#136](https://github.com/buster14a/buster/issues/136)'s item 8:
its values on the x87, passed on the stack and returned in st0, structs of them
returned in x87 registers, and `va_arg` of them), which it needs besides those.

A test moves into the subset in the change that makes it pass.

The reference runners (`runners/single-exec`) pass no `-lm`: gcc and clang at
`-O2` fold 00174's `sin(2)`, and Tier 0 folds no library call, so the harness
links libm by name, the direct path taking `-l` libraries as GCC's driver does
([the checker guide](c-checker.md)).

The 9700X job fetches the commit in a step of its own and runs the self-test
and the subset among its tests at once, as two commands (#522): the self-test
on one CPU, since most of its 7 s waits out its timeouts, and the subset with
`--cross-check` on four, `--jobs "$BUSTER_PARALLEL_CPUS"` (a worker per CPU of
its heading's).

## Results

On the cloud VM (Intel, clang 23.1.5), not a 9700X measurement: 220 tests, 219
passed, 1 unsupported, 0 failed; the vector and scalar paths wrote the same
executable for every test that compiles, and `--cross-check` agreed. With the
symbols' stage (on a Zen 4 laptop, clang 23.1.3), every object of the 219
defines `clang -O0 -c`'s functions and objects.

This establishes correct programs, not their speed or size: Tier 0's code
against `clang -O0`'s and the whole compile against tcc's are #136's and
#138's measurements.

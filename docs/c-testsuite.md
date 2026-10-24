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
./build/compiler test-c-testsuite [--subset FILE] [--jobs N] [--timeout SECONDS] [--compiler PATH] [--cross-check] [--keep] DIR
./build/compiler test-c-testsuite --self-test
```

DIR is a checkout of c-testsuite or its `tests/single-exec` directory, relative
to the current directory or not. Each `NAME.c` there must exit with status 0
having written `NAME.c.expected`'s bytes to its standard output and errors.
For each, the harness:

1. compiles and links it with `compiler --verify NAME.c -o program` (the vector
   paths, every function's records checked by `x86_validate`) and with
   `--scalar` too (the scalar twins): the two executables must be the same
   bytes, or the two refusals the same diagnostics;
2. runs the program in a directory of its own, with standard input from
   `/dev/null`, under `--timeout` (10 s; a compile gets 120 s), killing its
   process group at the deadline;
3. with `--cross-check`, which needs `readelf` and `cc`, also writes the object
   with `-c`, lists it with `readelf -a -W` (no warnings or errors), links it
   with `cc ... -lm` and runs that, which must give the expected output too.
   `-c` may refuse a test only where the direct path refused it too; a test the
   direct path can't link but `-c` takes must still run right.

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
stand-in compiler, a shell script, in five scenarios: from a DIR and a `$TMPDIR`
relative to the current directory (the processes run elsewhere, so the harness
makes every path absolute); waiting by polling as it does where `pidfd_open`
fails; with `--cross-check` where `-c` refuses a test the direct path compiles,
which must fail; with refusals that name their feature but are then killed (by
a signal in the scalar path alone and in both, and for their time in both),
each of which must fail; and with `--cross-check` where `-c`'s refusals are
then killed, which must fail whether or not the direct path compiled the test.
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

`tests/c-testsuite/subset.txt` declares the 217 that must pass. The other three
are refused by name, each waiting on work of its own:

| Test | Refused as | Waits on |
| --- | --- | --- |
| 00140 | variadic function definitions | [#136](https://github.com/buster14a/buster/issues/136)'s item 2 and [#137](https://github.com/buster14a/buster/issues/137) |
| 00174 | libm's functions (`sin`): the direct path links `libc.so.6` alone | `-l` libraries in the direct path |
| 00204 | structs and unions passed or returned by value | [#136](https://github.com/buster14a/buster/issues/136)'s item 2 and [#137](https://github.com/buster14a/buster/issues/137) |

With `--cross-check`, the one of them that `-c` takes, 00174, is linked by `cc`
and must run right; `-c` refuses the other two as the direct path does. A test
moves into the subset in the change that makes it pass.

The 9700X job fetches the commit in a step of its own and runs the self-test
and the subset with `--cross-check --jobs 4` among its tests at once.

## Results

On the cloud VM (Intel, clang 23.1.5), not a 9700X measurement: 220 tests, 217
passed, 3 unsupported, 0 failed; the vector and scalar paths wrote the same
executable for every test that compiles, and `--cross-check` agreed.

This establishes correct programs, not their speed or size: Tier 0's code
against `clang -O0`'s and the whole compile against tcc's are #136's and
#138's measurements.

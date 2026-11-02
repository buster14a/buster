# streaming

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/streaming.c` is #88's experiment on streaming the output:
  units of Tier 0's generated programs, compiled function by function into a
  static executable laid out data first (headers, `.data`, `.text`, then the
  unwind tables in a segment of their own, and `debug.c`'s DWARF with a line
  per function, in units made as the functions compile), written while they
  compile (by the
  compiling lane, or by a writer lane it hands chunks to) or once they have
  compiled, the same bytes either way; and the same code as a Link through
  `executable_write`, in lld's order. `streaming_test.c` holds `test-streaming`
  and `streaming_benchmark.c` `benchmark-streaming` ([Streaming tests](#streaming-tests) and
  [Streaming benchmark](#streaming-benchmark)); the design's "Streaming the output" has the
  measurements.
## Streaming tests

`./build/compiler test-streaming [--seed N] [--units N] [--cross-check]
[--keep]` checks `streaming.c` on two programs of generated units (3, and
`--units`, 40), each in both orders (callees first, as generated, and callers
first, every call a forward one), with 256 KB and 4 KB chunks, written by the
three schedules (at the end, streamed, streamed by a writer lane):

- the files must be byte for byte the same, with the build ID their note holds;
- each must run and print the fold of every unit's entry as Tier 0's tests'
  interpreter computes it, and so must the direct path's executable of the same
  code (`executable_write` on 1 and 3 lanes, in lld's order);
- each read back: the segments in the data-first order (R, RW, RX, R), the entry
  in `.text`, `.data`'s bytes, `.eh_frame_hdr`'s table as long as the functions,
  sorted, each FDE at its function;
- a streamed write with several chunks must have written some before the last
  function compiled, and forward fields come only in the callers' order.

`--cross-check`, which needs readelf, llvm-dwarfdump, gdb and perf, reads a
streamed file with `readelf -a` (no warnings) and `llvm-dwarfdump --verify`
(its DWARF, a unit per 256 functions), stops gdb in every program function and
checks
that each stop's backtrace goes through the program's functions to `_start`,
two of them above `_start` at least once (the unwinder used an FDE), each frame
at its function's line of `streamed.c` (the DWARF's: function f is line f + 1),
and that at least four separate perf samples with DWARF call graphs name the program's functions,
reach its actual `_start` and give lines of `streamed.c`. The larger cross-check fixture repeats the entry's
callee/fold block 128 times in the generated typed stream and C, keeping the same function set and normal DWARF/unwind generation
long enough for sampling. Other generator callers keep the ordinary single pass; zero and one repetitions produce
identical streams and C. There is no sampling retry. Collection failures print exit statuses, data size and bounded
stderr/profile text; negative controls reject one sample, duplicate frames and wrapper names.
The files go in a directory under `$TMPDIR` (or `/tmp`), removed
unless `--keep` or a failure. The 9700X job's test step runs it with
`--cross-check`.

## Streaming benchmark

`./build/compiler benchmark-streaming [--units N] [--functions N] [--seed N]
[--runs N] [--chunk-bytes N] [--order callees|callers] [--lanes N] [--cpus A,B]
[--directory PATH] [--keep]` generates one program of `--units` units (400) of
`--functions` functions (32), checks that every variant's file runs and prints
the same and that the schedules' files are the same bytes, then times each
variant `--runs` times (11) in an order that rotates by run, from the write's
start to the file closed with Tier 0 compiling every function in it: at the
end, streamed, streamed by a writer lane (on `--cpus`), and the direct path's
`executable_write` of the same code on one lane and on `--lanes` (8). It
reports the medians of the whole, the compiling, the Link's building (the
direct path) and what follows the last function compiled, which for the
schedules it splits into `.text`'s last chunks, unwind pairs and DWARF units,
the tail built, the tail written, the headers, and the close. Run it under the
9700X's lock with `taskset -c 2` for serial rows, or `taskset -c 2,3` with
`--lanes 2 --cpus 2,3` for parallel rows. The benchmark clamps lanes to its
inherited CPU affinity and reports the requested and selected counts. A writer
pair must name two distinct allowed CPUs; an unavailable writer or parallel
direct row prints `not run` and is excluded from validation and timing. Default
writer CPUs are the first two allowed CPUs. CI selects one allowed CPU per
physical core (up to eight), avoiding SMT siblings, and passes that placement
to the benchmark. Manual placements should do the same. The outputs are
overwritten in place, as `executable_write`
overwrites an executable: a file truncated on open and written again is flushed
when it is closed on btrfs and ext4.

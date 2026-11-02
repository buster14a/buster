# parallel

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/parallel.c` is the runner the 9700X job runs its tests
  through, `parallel`: a list of commands at once, each one's output printed
  in the list's order. `parallel_test.c` holds its tests (`main.c` includes
  both; `./build/compiler test-parallel` runs them); [The parallel runner](#the-parallel-runner)
  below describes both.
## The parallel runner

`./build/compiler parallel [--jobs N] [--timeout SECONDS] [--github] [LIST]`
runs a list of commands at once. LIST (standard input without one) holds them:
each starts at a heading, a line `== TITLE`, and its script is the lines after
it, up to the next heading; before the first heading there may be blank lines
and comments. A title may end with `[N CPUs]`, N from 2 (#522): the CPUs its
command keeps busy with lanes or processes of its own; any other runs on 1, and
a title that ends with "CPUs]" otherwise is an error. Each script runs in a bash
of its own, as GitHub Actions runs a step's `run` (`bash --noprofile --norc -e
-o pipefail`), in a process group of its own, with standard input from
`/dev/null`, its standard output and errors into one capture, no other
descriptor, and `BUSTER_PARALLEL_CPUS` set to its CPUs. The commands start in
the list's order, each once the commands running leave it room: their CPUs come
to at most `--jobs`, and a command of more runs alone, holding all of them; one
that doesn't fit yet holds back the ones after it, so the list's order is the
schedule. They run unpinned (a command's CPUs are what it says it keeps busy,
not a mask), and `--jobs` is by default one per core of the CPUs the process may
run on, 8 on the 9700X, whose 15 CPUs online would put two tests on a core's SMT
siblings, which slow each other down, as do the lanes' spinning waits (the job's
tests took 21.3 to 22.4 s with 8 jobs, and 22.9 to 23.3 s with 15, test-lanes
2.3 to 2.9 s of them rather than 0.2). The report gives each one's heading, with
its outcome, its time and its CPU time (`wait4`'s, of its shell and of the
processes under it that were waited for: over its time, the CPUs it kept busy),
then its output, in the list's order as soon as it and every command before it
have ended; with `--github`, between `::group::` and `::endgroup::`, and an
`::error::` annotation for each that failed. Then come a summary line, with the
commands' CPU time and their passed, failed, skipped and not-started counts, and
a line naming each command that failed. A command that exits with status 77
(Automake's skip code) is reported as skipped and counted apart, and a test
that leaves a check to another stage says so in its output and exits 77. The
exit status is 1 if a command failed or couldn't start, a signal stopped the
run, or the report was lost; a skipped command doesn't fail it. The report never
holds up the run (#276's review):
a command's output is read into memory when it ends and its capture closed, so
that only running commands hold one, and the report goes out only as fast as its
reader takes it, at most `PIPE_BUF` bytes at a time into a pipe or a terminal
once `poll` says it has room, while the runner keeps starting commands,
enforcing their timeouts and taking signals, all from one `poll` on a
`signalfd`. Whatever a command's shell leaves running in its group is killed
when the shell ends, and a command still running `--timeout` seconds after it
started is killed with its group and fails, though its shell may end before the
runner reaps it. SIGINT, SIGTERM and SIGHUP stop the run: the signal goes on to
every running command's group (a second one sends SIGKILL), nothing more starts,
and the report gives what every command wrote; once every command has ended, one
drops the rest of the report rather than wait for its reader. A report that
can't be written (EPIPE: `parallel | head`, whether or not SIGPIPE is ignored)
stops the run, and its commands are killed.

The 9700X job's test step ("Run complete native vector correctness before benchmarks") runs every
test through it, with `--timeout 600`, before the benchmarks, which run one at
a time and pinned. Its commands share CPUs 0 to 7 (`taskset`), one per core,
and the runner holds them to those 8 CPUs together (#522). Tests such as
test-lanes and test-files start lanes of their own and the call-graph check
starts compiler captures, so commands run together without counting their CPUs
multiply their CPU and memory budgets. From 1a529bc the step ran one command at
a time (`--jobs 1`) to keep them apart, which left 7 of the 8 CPUs idle through
most of its 213 s, since most of its commands are single-threaded. Now each
command holds one CPU, or the `[N CPUs]` its heading declares:

- all 8 for the call-graph check's eight compiler captures, the calling
  convention's twelve runs, and the tests whose lanes wait for each other in
  loops, which lose their time slices on CPUs they share: test-lanes, the
  executable writers' 8-lane groups, test-foreign's resolutions,
  `link-foreign --lanes=8` in test-foreign-corpus, test-link's checks and the
  short test-files runs. On the 9700X, beside other tests, test-lanes took 2.3
  to 2.9 s rather than 0.2 s alone (and 32 s on a 4-CPU cloud VM whose CPU has
  no mwaitx), test-foreign 28.3 s on 2 CPUs rather than 6.5 s, and test-link
  0.8 s rather than 0.02 s (#526's first run). They come first, where nothing
  runs before them to wait for;
- 4, 3 or 2 for those whose processes keep that many busy for much of their
  run: the parts that follow `BUSTER_PARALLEL_CPUS` (`test_process_cpus`), the
  `--jobs` the step passes from it (test-preprocessor's fuzz workers,
  test-c-testsuite's), and gdb's and perf's beside test-unwind's own. They come
  next, the most CPUs first, while the pool is emptiest. test-preprocessor's
  cases, its repository's units and the SSA promotion of c-testsuite's programs
  declared 4, 4 and 3 and kept about 2 busy on every machine (hosted, baseline,
  TSan and the 9700X): they declare 2, which frees the rest for the commands of
  one CPU;
- the commands of one CPU follow, the longest first by their times on the hosted
  runners and the 9700X. A long command that checks independent things runs as
  parts, each a command of its own that the runner packs beside the others
  (`--part K/N`): test-checker-driver as 4 (the list's longest, 197 s on a hosted
  runner and 48 s on the 9700X, whose start late in the list ended the step) and
  test-checker as 4 (141 s on a hosted runner, holding 2 CPUs while it kept 1.03
  busy). The parts together run each check once, with the whole command's
  random samples: test-checker starts each unit's numbers afresh from the seed
  and the unit's place (`check_test_part_random`), and no check of
  test-checker-driver's draws from the shared stream. c-testsuite's self-test,
  about 7 s of waiting out its timeouts, is a command of its own on one CPU,
  apart from the subset's four.

A heading declares the CPUs its command keeps busy for much of its run, which
its CPU time over its time shows (the report gives both). Brief phases on more
don't count, as the 8-lane checks of the longer test-files runs and of
test-checker, whose CPU time stays near their time, but lanes that wait for each
other in loops do. A command whose report shows it keeping more busy than its
heading says needs a larger count or less work. Run together, the step is bound
by the machine's CPU rather than by its longest test (#465: its tests took 622 s
of CPU, their times in the job 404 s, in a step of 52.5 s when 8 ran at once on
15 CPUs; #526's first run: 73.9 s, against 213.5 s one at a time, with 441.5 s
of CPU), so what shortens it is less work: the clang processes the tests start
as oracles are most of it.
The runner sets `BUSTER_PARALLEL_CPUS` to each command's CPUs, and a test that
runs processes of its own (`test_process_cpus`: test-preprocessor's parts,
`parse_test_clang`'s clang processes) runs at most that many at once (all the
CPUs it may run on without it), so that the commands' processes together come
to about one per CPU (#468's review). To run the list as the job does (on the
9700X, under its lock, as one run):

```sh
taskset -c 0-7 ./build/compiler parallel --jobs 8 --timeout 600 < tools/ci/correctness.tests
```

`./build/compiler test-parallel` (about 7 s) checks it, each report exactly
as expected with its times and CPU times written as `*`:

- lists: headings, scripts, blank lines and comments before the first heading,
  a heading without its space, a last line without a newline, `==` inside a
  line, and titles' `[N CPUs]` (after blanks the title loses, as a title alone,
  with leading zeros, at the largest `--jobs`, and not taken where it doesn't
  end the title or "CPUs]" doesn't end it); then each error at its line: a line
  before the first heading, a heading without a title, a command without a
  script (before another heading and at the end), a title that repeats, a title
  ending in "CPUs]" without a count from 2 before it (1, 0, past the largest
  `--jobs`, letters, no blank before "CPUs]", no `[` or a blank after it, and
  "CPUs]" alone), a NUL byte, and no commands;
- order: commands that end out of order, printed in the list's order, each with
  its standard output and errors in the order it wrote them, output that
  doesn't end in a newline and none at all, an exit status and a signal; then
  `--github`'s groups and annotations, with a title GitHub must read escaped;
  and a command's descriptors, without `/proc` (whose PID namespace may not be
  the shell's): standard input reads nothing though the runner's is a file
  with a line in it, and no other than the three is open (an external `true`
  redirected to each from 3 to 63), though the runner has one open without
  `FD_CLOEXEC`; a command that prints its reason and exits 77 is skipped,
  counted apart, and doesn't fail the run;
- at once: three commands that each wait for the other two pass with 3 jobs;
  with one job, four commands' starts and ends in a log come in turn; and eight
  with 3 jobs are never more than 3 at once, by the runner's times;
- CPUs (#522): with 4 jobs, commands of 2, 2, 3, 1, 1, 8, 1 and 1 CPUs start in
  the list's order and never hold more than 4 at once, each waiting for the room
  it needs, the one of 8 alone, holding the 4; a command that doesn't fit holds
  back the next, which would; each command's `BUSTER_PARALLEL_CPUS`, once in its
  environment, is its own CPUs (the 4 for the one of 8), though the runner's
  environment has another value; and the CPU time of a shell's loop and of a
  process it waits for counts, a sleep's comes to next to none, and the
  summary's is their sum;
- output: 1 MB from one command, past a pipe's 64 KB;
- processes: a command past its timeout killed with the process it started,
  what a command leaves running killed when its shell ends, and a shell that
  can't start;
- a slow reader (#276's review): a report whose pipe its reader leaves full for
  1 s while a command passes its timeout, which must be killed at its deadline
  and fail, and while a command sends SIGTERM, which must go on to it at once;
  SIGTERM once every command has ended, which must drop the rest of the report
  rather than wait for the reader; and 61 commands with room for 32
  descriptors, 60 of them ending before the first, whose captures must close
  as they end;
- signals: SIGTERM from a command stops the run, goes on to the command's
  group, and what didn't start is reported so; a run stopped while its command
  passes fails; SIGINT then SIGTERM, from a command that ignores both, kill it;
  SIGHUP while ignored (as under nohup) doesn't stop the run, and SIGCHLD
  ignored doesn't keep the runner from its commands; each run leaves the signal
  mask and the dispositions as they were;
- the command: its report and exit status for lists on standard input and in a
  file that pass and fail, a report with no reader (the run stops, and the
  runner ends at once with its command killed), with SIGPIPE at its default and
  ignored, a reader that leaves after the first line (the run fails though its
  command passes), a command that ends past its deadline while the runner is
  stopped (SIGSTOP), which must fail, a list's error, a list that can't be
  read, and invalid options;
- the cores: `--jobs`' default against the distinct physical and core ids that
  `/proc/cpuinfo` gives the CPUs the process may use.

The runner is test tooling outside the compile path and has no vector path: a
list is a few dozen commands, and the time goes into them. The 9700X job's
test step runs its tests too.

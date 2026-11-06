# compile-time

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `--time-stages` (`c_check_clock.c`, #518) makes the C driver report, on standard error after the command, each
  stage's milliseconds (startup, preprocessing and its phases, parsing, checking, lowering and its phases (#1048),
  Tier 0, branch sizing, encoding, the object's build and write, the direct path's runtime, binding and write, with the write's phases on
  `time-write` lines (#778), the release; on several lanes their run is one stage, `lanes`), with its system time,
  page faults and peak resident set added, and the process's resource use, with the lanes its session asked for and
  ran on (#905: `lanes-asked` and `lanes`), whether transparent huge pages were off for it (#1109: `huge-pages`, `off`,
  `busy` or `system`; `lane.c`'s `arena_page_policy` turns them off on a virtual machine or by `BUSTER_HUGE_PAGES=0`,
  `BUSTER_HUGE_PAGES=1` leaves them as the system has them, and `busy` means the process's arenas alone took base pages
  on a host busy when it started, #672: its tasks runnable at least a quarter of its CPUs online), and with
  `--check-transitions` the memoized transitions'
  hits, misses, folds and entries (#1050: `time-check transition-*`); off by default, its laps test a null pointer.
  `measure-compile [--runs N] [--reference PATH] -- ARGUMENT...` times `ARGUMENT... -o FILE` as whole processes, with
  and without the option and against a reference compiler (tcc), in a rotating order, with each stage's medians and
  the lowering's coverage; the 9700X job runs it on
  `sqlite3.c` against tcc, with perf's counters and profile, where `changes`' `compile_time` says. [The compile-time
  guide](../../docs/compile-time.md) has the attribution. `test-checker-driver --executable` checks both.

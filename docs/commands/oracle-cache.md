# oracle-cache

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/oracle_cache.c` keeps the results of the clang and gcc
  runs that tests compare with between runs in the cache directory (#542),
  each with every path its processes looked at, which a seccomp listener
  traces: `BUSTER_ORACLE_CACHE=use` takes a kept result rather than run the
  tool again, and `check` runs every one and fails on a kept result that
  differs.
  `oracle_cache_test.c` holds its tests (`./build/compiler test-oracle-cache`);
  [The oracle cache](#the-oracle-cache) describes both.
## The oracle cache

Most of the 9700X job's test step is clang and gcc processes that tests run as
oracles, on inputs that change little from one run to the next (#465, #542):
on the run #542 measured, 12,599 processes took 204.8 s of the step's 401.5 s
of CPU, 175.8 s of them clang's. `oracle_cache.c` keeps their results between
runs. A test runs a tool through it as an `OracleRun` (`oracle_run_start` and
`oracle_run_finish`, or `oracle_run`): the tool, its arguments, environment,
standard input and working directory, and the files it writes. Its result,
its wait status, standard output and errors and those files' bytes, is kept
as an entry in its key's directory in `oracle/` under the cache directory (the
runtime snapshot's: `$XDG_CACHE_HOME/buster` or `$HOME/.cache/buster`). The
key is SHA-256 of:

- the kind of run, as its caller names it;
- the tool: its path on `PATH` and what `stat` gives of the file it resolves
  to, as the runtime snapshot's probes; the same of the package database
  (pacman's, dpkg's or rpm's), which any package's update changes, so that a
  new clang, its libraries or its headers make new keys; and the CPU's model
  and flags, which `-march=native` reads;
- its arguments, its working directory, the names of the files it writes and
  the environment variables that change a compiler's result
  (`oracle_environment_names`) with their values, each with its roots made
  alike (below); its time limit; and the bytes of its standard input.

A compiler's run (`OracleRun.dependencies`) is traced: it runs under a seccomp
filter that stops each call of its processes that looks at a path (`open`,
`stat`, `access`, `readlink`, `execve` and their kin), lists a directory, or
makes, removes, renames or changes one (`mkdir`, `unlink`,
`rename`, `link`, `chmod` and their kin), whose listener the cache answers
(`oracle_trace_answer`, which reads each call's paths as `oracle_calls`
describes): it reads the paths from the process (`process_vm_readv`), looks at
each first if the run hasn't before (`oracle_probe_look`), and lets the call
through (`SECCOMP_USER_NOTIF_FLAG_CONTINUE`). The entry lists every path the
run looked at, each as it was when the run first looked, before the run could
change it:
a file by SHA-256 of its bytes (`oracle_file_digest`: a file in one of the
run's roots (below), which tests write anew for each run, read each time, and
any other once per process for each identity, what `stat` gives of it, which a
write changes; so a corpus fetched anew, or a header a checkout rewrote with
the bytes it had, is the same file); a path that isn't there, and why; a
symbolic link and its target; a directory as one; another kind (a device
node, a FIFO) by its kind and device number; each with the permissions of what
it leads to (its mode's bits, owner and group, which `access` and `execve`
read) and its extended attributes (`oracle_hash_attributes`: ACLs, security
labels and capabilities among them), and the kind, permissions and extended
attributes of the directory it is in (an `mkdir` in a directory that isn't
there yet fails, and once it is succeeds, for a path that isn't there either
way); a directory, where the run listed it or needed it empty (`rmdir`'s, and
`rename`'s target's), as `getdents64` gives it (`oracle_listing`: each entry
in the file system's order, with its inode, offset, type and name, so a file
made anew with the same name and bytes is another listing); and which of them
is the same file (a hard link, which `#pragma once` takes once). So every
place an `#include`, `__has_include` or `#embed` looked for a header and found
none counts, in any directory (one that other runs' files go into, or a nested
header's), and so does a missing `#include`, whose run is kept. A file the run
makes was one that wasn't there; a file it changes after it read it isn't
kept. The files it writes are among what it looked at, as they were before it:
their bytes are read at the run's first look at each, while its call waits,
so that a run that reads its output before it writes it (an `#embed` of it)
runs again when those bytes change, and a compile into the path the last one
wrote is taken when the object there is the one that compile left. What the
cache leaves out (`oracle_path_rule`, by what each call does at a path,
`OracleUse`): the package manager's files (under `/usr` but `/usr/local`, and
`/bin`, `/sbin` and `/lib*`), the package database's identity standing for
them. The kernel's (`/proc`, `/sys`), which describe the processes and the
machine, change with no file changing (a process's id in `/proc/self/stat`,
the time in `/proc/uptime`, the inode and times `stat` gives of `/proc/self`,
its process's): a check for one (`access`, `faccessat2` and `chdir`, which say
whether it is there, as LLVM checks for `/proc/self/fd` and `/proc/self/exe`)
is left out, and a run that stats, opens, lists or changes one, or reads a
link there, isn't kept. A path in one of the run's roots is
kept wherever `$TMPDIR` is, and so is the rest of `/dev`: its device nodes, and
`/dev/shm`'s files, an ordinary tmpfs's. But a run that opens a device, whose
data change with no file changing (`/dev/urandom`'s), a FIFO or a socket
isn't kept (`oracle_opens_unseen`), but for `/dev/null`, `/dev/zero` and
`/dev/full`, whose data never change, and `/dev/tty`, which bash opens as it
starts: a run has no controlling terminal (below), so its open fails. Nor is
a run that opens a file of the kernel's through another path (a symbolic link
in a root to `/proc/self/stat`), whose file system `statfs` gives (procfs,
sysfs and the other file systems whose files the kernel makes as they are
read). A
process's links into the file system (`/proc/PID`'s `cwd`, `root`, `exe`,
`fd/N` and `map_files`, which `/dev/fd` and `/dev/stdin` lead to) are what that
process sees, which the cache, another process, can't look at: its own read as
links (`readlink`, which LLVM reads its files' real paths through) are left
out, but a descriptor's that names a pipe's or a socket's inode (`pipe:[N]`,
another in every run), and so is following its own `exe` or a standard
descriptor, but by `stat` (a standard descriptor's numbers are a capture's,
made anew for each run); a run that uses another process's paths at all,
follows another link, or reads a path past one (`/proc/self/cwd/x.h`), isn't
kept (`oracle_process_link`: a process's own are `self`'s, `thread-self`'s and
its pid's, or its process's, whose tasks it is among).

The cache answers a traced run's `getrandom` calls itself
(`oracle_trace_random`): its nth answered call gets SHA-256 of the run's key,
n and each 32-byte block's number, written into the process's memory (through
`/proc/PID/mem`, opened before the notification is checked, so that a pid that
went to another process isn't written to), so that its bytes are the same each
time the run runs, and its result can't depend on bytes that differ from one
run to the next. Every glibc process takes 8 bytes for malloc's tcache key as
it starts, and clang 8 per output file for its temporary file's name, so a run
that called it couldn't otherwise be kept; glibc's `getrandom` through the
vDSO takes its key from such a call. A call for more than 256 bytes
(`BUSTER_ORACLE_RANDOM_BYTES`), or whose bytes can't be written, goes on to
the kernel, and its run isn't kept. A run's random bytes are thus the cache's,
the same each time: a tool that needs them to differ from one run to the next
isn't one to run traced. The tool's child keeps no descriptor past exec but
the standard three (`close_range`'s `CLOSE_RANGE_CLOEXEC`) and gives up its
controlling terminal (`TIOCNOTTY`), so that what it reads comes through the
paths the filter stops and its key's input; a child that can't installs no
filter, and its run isn't kept. What the tracing can't see, a tool's result
must not depend on: the clocks (the vDSO's), its process and thread ids, its
addresses and the kernel's random bytes for its start (`AT_RANDOM`), the
processor's (`rdrand`, `rdtsc`), the machine's figures (`uname`, `sysinfo`,
the CPUs it may run on), what other processes send it (sockets, shared memory:
bash, with no `SHELL` in its environment, connects to nscd's socket as it
starts, so a socket can't make a run unkept), and the numbers the kernel gives
the files it finds and makes, which a path's state doesn't have: their inode
and device numbers, link and block counts and times (but the modification
times, for a tool whose entries have them), which `stat` gives beside a file's
kind, size, owner and permissions, and `fstat` of any descriptor (a pipe's, a
capture's), with no path to trace. They differ from one run to the next for
every file a test or a run makes anew (clang's driver stats its source, and
`cc`'s link the executable it wrote), so that a state that held them would
never be found again. A compiler's result depends on none of them: with
`SOURCE_DATE_EPOCH`, clang's and gcc's outputs are the same from run to run,
which check mode verifies, and a run whose result depends on one fails there,
its entry stale (#550's sixth review). A process answers its traced runs'
calls while it waits for any of them, so that runs under way at once all go
on. A call the tracing can't follow makes its run unkept
(`oracle_unfollowed_calls`): one that reaches files it can't name (io_uring's,
files opened by handle), that changes how its process resolves paths
(`chroot`, `pivot_root`, the mount calls, `unshare`, `setns`, and `openat2`
with resolution flags, whose `RESOLVE_IN_ROOT` reads an absolute name from its
descriptor's directory), that reads or changes extended attributes, that lists
the mounts, `statfs`, whose answer is its file system's figures (its free
blocks and files, which any file written changes), and another architecture's.
On this container, the filter's stops took a `clang -E` of `<stdio.h>` from 34
ms to 47 ms, which only a run that isn't taken pays.

A compiler's run is kept only if its environment sets `SOURCE_DATE_EPOCH`
(`oracle_run_key`): without it, its `__DATE__` and `__TIME__` would be when
it ran, however its sources spell them (pasted from two halves, say, in a
header in no root, or on its standard input). With it, which the key has,
they are the epoch's, and so is clang's `__TIMESTAMP__`; a tool whose
`__TIMESTAMP__` doesn't follow the epoch (GCC's is the file's modification
time), as one run of `-E -P -x c -` on it shows once per tool in a process
(`oracle_tool_epoch`), has its files' modification times in its entries, and
a test gives the files it writes for such a tool the epoch's time
(`oracle_epoch_file`: test-calling-convention's C sides, which GCC compiles
too). The tests that compare with compilers through the cache fix the
epoch where it isn't set (`oracle_epoch_begin`: test-preprocessor,
test-parser, test-checker, test-checker-asm, test-checker-driver, test-tier0,
test-calling-convention, test-c-testsuite; test-checker-corpus fixes its
own), for the compilers and their own preprocessor alike. A run with `-M`
options of its own, or more than 8 roots, isn't kept.

The roots: a test makes its files in directories of `mkdtemp`'s under
`$TMPDIR` (or `/tmp`), whose names differ from one run to the next. A root is
such a name in a run's strings, `$TMPDIR/NAME`, NAME's last 6 characters (or
the 6 before its last `.`) letters and digits, as `mkdtemp`'s random ones are.
A key has a placeholder for each root's 6 characters, by the root's place
among the run's; an entry holds the roots it was made with, which a run that
takes it replaces with its own, as long, in the result (an object's bytes as
much as a text's) and in the names of what it looked at.

A key keeps up to 32 entries (`BUSTER_ORACLE_VARIANTS`), one for each state
of what its runs looked at, each named by its variant: SHA-256 of what its run
looked at, its roots made alike (`oracle_entry_variant`). A test that writes
one file anew with other bytes for each of its runs, with the same arguments
(the preprocessor's header sets' `headers.c`, Tier 0's groups'
`program_N.c`), has one key for them all. A run takes the first of its key's
entries whose inputs are as it finds them (`oracle_entry_find`), and writing
one past 32 removes the key's oldest (`oracle_key_trim`). An entry is written
into a file of its own and renamed over its name, so that a reader sees a
whole entry or none whatever runs at once. `BUSTER_ORACLE_CACHE` picks the
mode: `use` takes an entry rather than run the tool and writes an entry for
each run; `check` runs every one and writes it, and where the entry it would
have taken, found before it runs, holds another result reports it as stale,
which fails the run; anything else, or unset, keeps nothing (local runs). The
hosted native and baseline correctness jobs use `check` on every revision.
Only after both succeed does Benchpress run native correctness: its persistent
cache uses `use` on pushes and pull requests, and `check` on daily and
dispatched runs. Thus reuse never replaces the triggering revision's independent
hosted oracles. Check runs then remove the entries no
test has written for two days, and the keys left without one. An entry is only
as right as the run that wrote it: the daily check finds one a pull request's
run left wrong. The counts of what runs did (taken, run, stale, not kept) are
in shared memory, which the parts a test forks after it sets up the cache add
to, and a test prints them at its end (`oracle_cache_report`).

The twelve standalone `test-check-builtins` commands (the base command and
`-misc`, `-query`, `-bit-cast`, `-float`, `-x86`, `-vector`, `-math`, `-atomic`,
`-atomic-rmw`, `-typed-overflow`, `-rounds`) run their cheap references freshly
in `use`: native qualification found validation cost more than execution.
`main_unit_check_builtins` applies this exact-name policy before dispatch and
restores the prior mode after success, failure or argument rejection. It does
not affect other commands or timed competitors. `check` is unchanged, so both
hosted variants and daily/manual native runs still execute and validate these
oracles independently. Backend capture/status/cache plumbing is retained for
that path. An inactive scope prints no cache counters; the independent tools
still execute. Assembler reuse remains separate, with its cold/check costs
retained in #542's evidence rather than inferred from hit counts.

The tool runs in a process group of its own: what it leaves running there is
killed when it ends, and the whole group at its time limit
(`OracleRun.timeout`), on the monotonic clock, waited for on a pidfd (a
millisecond at a time without one); a run killed so fails
(`OracleRun.killed`) and isn't kept. A test of a harness whose processes must
run turns the process's cache off (`oracle_cache_disable`).

Its users:

- the predefines' tests (`c_predefines_test.c`): the option sets' `clang -dM
  -E` and the generator's `-S -emit-llvm`, each reading its input on its
  standard input, at most `test_process_cpus()` at once;
- the preprocessor's tests' `clang -E` (`pp_test_clang_process`): the cases,
  the header, keyword and feature sets, and the files given to it;
- `parse_test_clang`, the parser's and the checker's `clang -fsyntax-only` of
  their snippets and fixtures: with the cache on, a file per process, as many
  at once as its batches have, each file's result the one a batch gives it;
- test-checker-corpus's clang (`check_corpus_clang_phase`, its first error,
  and `check_corpus_clang_object`, `-O0 -c`'s objects), which test-checker-asm
  and test-checker-driver use too, and its semantic mutants' preprocessed
  text (`-x cpp-output`);
- test-tier0's cross-check's compiles of each program's C and of its harness,
  each then linked with Tier 0's object (`tier0_test_compile`);
- test-calling-convention's cross-check's compiles of the C side, each then
  linked with ours (`abi_test_compile`, its runs sharing their directory, its
  sources given the epoch's time for GCC's runs);
- test-c-testsuite's `clang -O0 -c` of each test (`c_testsuite_symbols`), but
  in `--self-test`, which turns the cache off.
- the standalone checker-builtin commands, including their diagnostic-preserving
  Clang boundaries (atomic/float warnings, vector constraints, x86 ranges and
  atomic RMW). Each command fixes the epoch only when absent and restores that
  ambient state on return. Existing parser-oracle calls therefore cache too;
- the assembler's independent Clang/GCC assembly generation and C harness
  object compiles, Clang's `.S` preprocessing, and GNU as's object oracles.
  Test-authored assembly inputs receive the epoch's mtime for tools whose
  `__TIMESTAMP__` ignores the epoch. Repeated fixtures have stable distinct
  source paths to avoid churning one key's 32 variants.

These adapters cache object/text bytes. Links and generated-program execution
always run fresh, observing the current runtime. The cache does not store an
artifact's executable permission, so an executable must not be a cached output.
The standalone adapter preserves normal nonzero statuses and bounded diagnostic
prefixes, but launcher, timeout, capture and stale-entry failures remain failures
(`oracle_test_run`). Timed benchmark competitors are not routed through it.

Each of these tests prints a line of what its runs did (`oracle_cache_report`).

`./build/compiler test-oracle-cache` (about 8 s, 5 s of it the process
groups' waits) checks it with sh, clang and a program of its own as the tools
(`oracle_test_helper`, which clang builds, for the calls sh doesn't make and
for what coreutils' programs do that read the kernel's files as they start,
libselinux's `/proc/filesystems` on Ubuntu), in a directory of its own under
`$TMPDIR` (or `/tmp`) and two of one template beside it, with
`SOURCE_DATE_EPOCH` fixed where it isn't set:

- SHA-256 against FIPS 180-4's examples, and every length to 300 bytes whole
  and in random pieces;
- roots: a directory's, an option's and a file's, names that aren't roots, two
  in order, more than 8, a root followed by a name's character (not it), two
  replaced by each other in one pass, and keys alike only where the random
  characters differ;
- runs: a run's status, output, errors and written file (NUL bytes among its
  own), taken by the same run and by one in the other directory, naming that
  one's directory; each part of a key making a new one (the input's bytes, an
  argument's byte, where the arguments split, a variable's value, one set to
  nothing against one not set, the purpose, the time limit, the tool's path and
  its file's times), and `HOME` not; a tool not on `PATH`; a file the run
  didn't write, removed when its entry is taken;
- dependencies (traced `clang -fsyntax-only`): a compile taken in its
  directory and in the other, run again after a header changes, and taken
  after a file it doesn't look for is added beside it; run again after
  `#embed`'s resource or what `__has_include` found changes; `__has_include`'s
  header created where it looked, in a directory other files go into; a nested
  header created in its includer's directory, before the one `-I` found;
  preprocessed text's `#include`; errors naming absolute paths, taken with the
  other directory's; a missing `#include` kept, and run again once its header
  is there; `-M` options, not kept;
- identities: a hard link replaced by a copy of its bytes; clang's
  `__TIMESTAMP__`, the epoch's, its file's times changed and its entry taken;
  `__DATE__`, and `__TIME__` pasted from two halves in a header in no root and
  in a source on standard input, none kept without `SOURCE_DATE_EPOCH` and each
  with it, giving the epoch's date and time; a header in no root checked by its
  bytes: written anew with the same bytes, taken, and with other bytes, and in
  place with other bytes of the same size, run again; its extended attribute,
  and a missing path's directory's, each run again (where the file system has
  user attributes);
- tracing (traced sh): a file a child process read and a path a test found
  absent, each run again when it changes; a file the run makes and reads back,
  taken while it isn't there and run again when it is; a file the run changes
  after it read it, not kept; two runs at once, each waiting on a file the
  other writes, finished in the other order; clang's `__TIMESTAMP__` following
  the epoch and sh's not, so that a run of such a tool (the test's program)
  that prints a file's time has the time in its entry;
  a file given the epoch's time (`oracle_epoch_file`), written anew with its
  bytes, taken once given it again;
- variants (traced sh): a file with other bytes for each run of one key, each
  state's entry taken in turn; past 32 states, the key's oldest entry removed
  and the others taken;
- permissions (traced sh): a directory's mode, which `test -x` reads, and a
  script's in a root made 0644 from 0755 with the same bytes, each run again
  when they change; and as root, a file given another owner;
- the kernel's paths: `oracle_path_rule`'s answers for 64 paths by six uses
  (`stat`, `lstat`, `access`, `readlink`, `open` and a change), for a process
  of the test's whose descriptors it knows (a pipe's among them), its paths by
  its pid and the test's, another process's, among them (and a path in a root
  under `/usr`); a source in a root in `/dev/shm` (`$TMPDIR` there), changed
  to give another object, and a header in `/dev/shm` in no root, each compiled
  again; runs that follow `/proc/self/cwd` and `/dev/fd/3` to a file, not
  kept; and one that reads `/proc/self/fd/3`'s link and `/dev/stdin` and
  writes `/dev/null`, kept;
- random bytes, devices and the kernel's files (#550's fifth review): the
  test's program's `getrandom` bytes, run, taken, and the same in another
  cache's run; two programs' calls in one run, each its own bytes, the same in
  another cache's; another key's, others; a call for 300 bytes, the kernel's,
  never taken; `/dev/urandom`'s and `/dev/random`'s bytes and a FIFO's, never
  taken; `/dev/zero`, `/dev/null` and `/dev/tty`, kept, `/dev/tty` too where the
  test has a controlling terminal (a pseudo-terminal's, in a session of its
  own), which the run can't open; `/proc/self/stat`, `/proc/uptime`,
  `/proc/self`'s link, another process's (`/proc/$PPID/cwd`), a `/sys`
  attribute, `/proc/self/fd`'s names, a pipe's link and `/proc/self/stat`
  through a symbolic link in a root, never taken; what `stat` gives of
  `/proc/self` (coreutils' `stat -L`, #550's sixth review's case, and the
  test's program's), of `/dev/stdout` (a capture) and of a `/sys` directory,
  and `test -e` of `/proc/self/stat`, never taken, and `test -r` of it
  (`access`), kept; and a descriptor the test has open, which the run doesn't
  inherit;
- the kernel's numbers (#550's sixth review): a file's inode, which no entry
  has, printed by the test's program's `stat` of a file of the same bytes in
  each directory, taken in use mode by the other directory's run, as the
  contract has it, and stale in check mode, which fails the run;
- outputs: a run that reads its output before it writes it (traced sh), run
  again when those bytes differ and taken when they are an entry's; clang's
  object where the last compile left one, run again once, then taken, and in
  the other directory, without one, taken;
- listings (the test's program's `getdents64`, as `ls -i` lists): a file made
  anew with the same name and bytes, its inode another, run again;
- changes (the test's program's `mkdir`, and traced sh's `rmdir`): `mkdir`'s
  path, which the run made, there the next time; `mkdir` in a directory made
  since; and `rmdir` of a directory whose file goes; each run again;
- what the tracing can't follow, with the test's program: `openat2`
  with `RESOLVE_IN_ROOT`, not kept, and without resolution flags kept and run
  again when its file changes; `chroot`, `unshare`, `getxattr` and `statfs`,
  not kept;
- process groups: what a run leaves running when it ends, and what a run
  killed at its time limit started, killed (the markers they would write later
  aren't there); the limit on the monotonic clock, with a pidfd and without; a
  run kept without a limit, killed at a limit given later;
- check mode: an entry made stale fails its run and is written again (the test
  prints that it makes one on purpose), one whose run read other bytes isn't
  stale, of a key's two entries, the one the run would have taken is checked,
  and a stale entry another variant's wouldn't replace (a file named apart by
  the shell's pid), removed;
- damaged entries: another magic, key, body length, root or dependency count,
  a flag it doesn't know, cut short, cut inside its header or a byte longer,
  each run again and written whole;
- the modes: off, a name that is no mode and no directory keep nothing and
  make no directory;
- 8 processes running the same 4 runs 16 times each, whose counts add up in
  the parent's and whose entries are whole after, with nothing beside them;
- the process's cache, from `BUSTER_ORACLE_CACHE` and `XDG_CACHE_HOME`, its
  directory made private three levels deep, and turned off.

The cache is test tooling outside the compile path and has no vector path: a
run hashes what it read. The 9700X job's test step runs its tests.

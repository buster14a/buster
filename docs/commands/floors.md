# floors

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Floors benchmark

`./build/compiler benchmark-floors [--directory PATH] [--cpu N] [--only
files|threads|calls|memory|teardown] [--size-kb N] [--method NAME]`
measures what the machine and the kernel charge a linker however it works,
each figure a median, pinned to `--cpu` (CPU 2):

- files: the output written from memory into `--directory` (`TMPDIR` or
  `/tmp`) at 16 KB, 256 KB, 4 MB, 64 MB and 512 MB, as a new file, with
  `O_TRUNC`, in place, as a temporary file renamed over it, and through a
  shared mapping (new, with `fallocate`, with `MADV_HUGEPAGE`, with both,
  and in place), each time with its open and its close, 15 runs, or 5 with a `syncfs` after
  each from 64 MB;
- threads: 64 MB and 512 MB written by 1, 2, 3, 4 and 8 threads, on `--cpu`
  first and then the first other CPUs this process may use, each a contiguous
  share, by `pwrite` (new and in place) and through a shared mapping, starting
  the threads outside the time;
- calls: what a linker pays per input file, on this process's `libc.so.6`:
  `open` and `close`, `stat`, `mmap` and `munmap` with and without touching
  every page, and `pread` of the whole file;
- memory: 256 MB of anonymous memory touched for the first time with base
  pages and with `MADV_HUGEPAGE`, and `memcpy`, non-temporal stores and
  `memset` from 256 KB to 256 MB;
- teardown: a child, on another CPU when this process may use one, touches
  64 MB, 1 GB and 4 GB, sends this process the time through a pipe and exits:
  the time from then to `waitpid`'s return, which a linker that forks hides
  from its caller.

The final file size and every byte are checked against the source after each
method, size and lane count, outside timing. Threads print the actual CPU
order. `--size-kb` selects 16, 256, 4096, 65536 or 524288 KiB; threads only
support the last two. `--method` selects `new`, `truncate`, `in-place`,
`rename`, `mmap`, `fallocate`, `huge`, `fallocate-huge` or `mmap-in-place`;
threads omit `truncate` and `rename`. Both filters require `--only files`
or `--only threads`. Use them to release the shared lock between bounded
measurements; full matrices can take several minutes on btrfs.

It exits with status 1 on invalid arguments, differing file bytes, and when a
call fails: any lane's write or huge-page advice, naming its `errno`, or a
child that dies before its message or its exit.

Run it under the 9700X's lock without `taskset`, and with `--directory` on each
file system to measure. `docs/object-writer-and-linker.md` ("The floors") has
the original numbers; [linker scaling](../../docs/linker-scaling.md) records the
frozen physical-core and SMT lane sweeps, independently controlled write
factors, and filesystem limits from #87. The default 9700X job runs it after
benchmark-link, where that runs the linkers' side (the machine's floors don't
depend on a pull request either); like the others, its measurements are informational.

# Default foreign-link interpreter

The raw `link-foreign -pie` driver needs a path for `PT_INTERP` when the caller
does not supply `--dynamic-linker`. `runtime_discover_interpreter` searches the
existing system library directories for `libc.so`, parses its existing script
subset, and selects the script's as-needed loader entry. It keeps the absolute
path and regular-file checks already used by runtime discovery. Missing scripts,
missing inputs, malformed scripts and a missing loader fail before output.

The lookup reserves only the file/probe arena. It does not find GCC, require
startup objects, load archive members, read the shared libraries, construct
symbol tables or bind the runtime. The caller still supplies the startup objects
and libraries to the raw linker. An explicit `--dynamic-linker` bypasses this
lookup completely, as before; static output does not use it.

The returned path and probes belong to the `Runtime` file arena through output
writing and are released with `runtime_release`. This is path discovery, not
validation that the loader's ELF contents can run a particular executable.
Actual foreign inputs are still parsed through their ordinary import paths.
The previous full-runtime import incidentally checked unrelated runtime files;
the path lookup intentionally no longer depends on their importability.

The driver tests compare the discovered path with complete runtime import,
verify that runtime object/library bytes and symbol tables remain unloaded,
exercise valid, missing-loader, relative-input, malformed, missing-file and
exhausted-probe script cases, and retain the existing default/explicit-loader
PIE execution checks. Script failures publish no partial interpreter path.

The first source checkpoint is based on the qualified foreign-driver snapshot
whose source manifest has SHA256
`0e69e4d9b905156c4fbefe998cc49cdf47c659f0af308cbd03639905241c75ed`.
On the 9700X, with the tree of [#380](https://github.com/buster14a/buster/pull/380)'s
head `b3c7bf1` ([the record](https://github.com/buster14a/buster-performance/tree/0e3f0a0708f0c01e8bfd2348beefb7fce400fc16/docs/measurements/foreign-link-2026-10-24)), the driver tests pass, and `--time`
puts the lookup at 10 to 11 µs of a hello world's 0.22 ms link. 41 alternating
pairs of whole processes (lookup, linking, output and cleanup, each timed by
the shell around `taskset -c 2`) took medians of 937 µs with the lookup and
943 µs with an explicit `--dynamic-linker /lib64/ld-linux-x86-64.so.2`, a
difference inside the processes' spread. Both outputs run; their `PT_INTERP`
paths differ, since glibc's script names `/usr/lib/ld-linux-x86-64.so.2`.

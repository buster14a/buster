# sbase compatibility harness

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

[Compatibility harness index](../compatibility.md). Read only the harness you are working on; pins, measurements, and past failure counts describe the revision recorded below and must be rechecked before reuse.

The opt-in sbase compatibility harness takes an external, pristine sbase
checkout; upstream sources are never copied into or patched in this
repository:

```sh
./build.sh build --config Release -t ide
./build/build test_sbase --config Release /path/to/sbase
```

The checkout must be commit
`c546c3a5724c81cee9a11d816a38ccdf17472129` with no tracked or untracked
changes. sbase is the breadth target of the compatibility set rather than a
depth one: about a hundred small POSIX programs over one shared static
library, so its failures are declaration and system-header failures spread
thin across many translation units instead of one deep code path. The harness
therefore reports a status for every utility — `SBASE_UTILITY` names its
compile, link and runtime result separately — instead of one aggregate
verdict.

It owns an explicit manifest of upstream's own Makefile lists: 19 libutf
units, 38 libutil units, the 5 units of `make`, and one translation unit per
utility, checked for existence before anything is built so a source upstream
renames is a manifest error rather than a compile error. Two sources upstream
generates during its build are produced once and shared by both compilers:
`getconf.h`, from `scripts/getconf.sh`, and `bc.c`, from `bc.y` through
`yacc`. Everything is compiled with upstream's own CPPFLAGS plus `-O2`, each
utility is linked through its own compiler driver — `ide cc` for Buster,
Clang for the reference — and the two archives are built with `ar`.

Correctness is decided by comparison with a Clang build of the same manifest,
never by absolute output. Three layers run against it: a probe per utility,
100 of them, each a deterministic shell command run from that build's own
directory over its own copy of the corpus; 24 cross-cutting cases covering
pipelines, redirected standard input, binary data with every byte value
including NUL, error exits, 5.000-argument command lines and locale-pinned
sorting; and the unmodified upstream test suite, all 54 scripts, copied
beside the binaries so they find `../echo` and `../bc.library` where they
expect them. Test verdicts are compared with the Clang build's rather than
required to be passes: `0051-grep.sh` depends on the host locale, and a
harness that demanded a pass there would be reporting the host rather than
the compiler. Every child runs under a deadline, because a miscompile does
not always crash — the first symptom of the `dc` defect this harness found
was an upstream test that never finished.

FAST and NONE run the complete set; MIR_STACK and QUALITY are sampled over 13
of the larger utilities once the complete rows pass, and their run directories
borrow the reference build's other programs so the utility under test is the
only Buster program in its own probe. After every allocator row passes, the
driver gate runs upstream's own makefile with `CC` set to `ide cc` against a
clean `git archive` export of the pinned commit, which covers make's implicit
rules, its generated header, its yacc source and its recursive sub-make.

All 100 utilities match the Clang build. `env` and `find` were the last two
that did not, and both failed for one linker reason rather than a codegen one:
they read `extern char **environ`, which reached the non-PIE executable through
a copy relocation whose slot the loader filled before glibc's startup code
stored the environment pointer. That is fixed by the alias sets described under
the driver below, and the two are ordinary passing rows now. Generated objects,
archives, metrics, corpora, the per-allocator run directories and the makefile
export remain under `build/sbase-<pid>/`, which is about 37 MB and is not
cleaned up on the way out.

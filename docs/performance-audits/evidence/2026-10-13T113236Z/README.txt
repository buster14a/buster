Raw evidence for audit 2026-10-13T113236Z (hosted Callgrind, diagnostic only).

arms.sh              the four arms (A base default, B base pilot, C candidate pilot,
                     D candidate default) on -fsyntax-only and -c -g0, the -g objects,
                     and the -v C_AST_* rows of B and C, as run
tcc-shim.sh          the bootstrap's "tcc", a Clang shim (this container has no TCC)
arms-summary.txt     the script's output: Ir per arm, object hashes, counter diff
stats-B.txt/-C.txt   the -v C_AST_* rows under -fc-ast-pilot (timings differ by run)
identities.txt       revisions, binary sha256s, object sha256s and sizes
callgrind-raw.tar.xz callgrind.out files and logs of all eight arms
Objects are not retained; their hashes are.
Replay: tar -xJf callgrind-raw.tar.xz; callgrind_annotate --inclusive=yes C.s.cg

Raw evidence for audit 2026-10-13T092848Z (hosted Callgrind, diagnostic only).

arms.sh           the first four arms (A, B, C and D of candidate 1) and their -g objects, as run
arms-candidate.sh the C/D arms of a later candidate, as run for candidate 2 (C2, D2);
                  variants 3-6 ran the same script with the names C3/D3 ... C6/D6
candidate1.patch  git show of a5aac2b5 (the predicate gates; negative, reverted)
fnself.py         self Ir per function difference between two callgrind outputs
incl.py           call counts and inclusive/self Ir of callees matching a regex
call-counts.txt   incl.py output per arm for the predicates, the prefix build and the root conditional
corpus.sh         base vs shipped objects for every tests/**/*.c (-g0/-g, default/pilot)
corpus2.sh        shipped default vs shipped pilot objects for the same files
corpus-*.txt      their summaries
stats-*.txt       the -v C_AST_* statistics lines under -fc-ast-pilot, base and shipped
identities.txt    revisions, binary sha256s, object sha256s and sizes
callgrind-raw.tar.xz  callgrind.out files of A, B, C/D (candidate 1), C2/D2 and
                  C6/D6; the arm logs of every arm including C3-C5/D3-D5; the build,
                  test_all, test_self_host and positive-control (mut.log) logs

Candidates: C/D = a5aac2b5; C2/D2 = a561611d; C3/D3, C4/D4 and C5/D5 = uncommitted
variants described in the audit (patches not retained); C6/D6 = 0ee5ec48 (shipped).
Object files are not retained; their hashes are.
Replay: tar -xJf callgrind-raw.tar.xz; python3 fnself.py A.cg D6.cg.

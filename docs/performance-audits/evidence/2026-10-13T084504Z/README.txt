Raw evidence for audit 2026-10-13T084504Z (hosted Callgrind, diagnostic only).

census.patch      instrumentation applied to base 446f742e for the census build
                  (git diff of the census worktree; not committed code)
census.py         script that wrote census.patch
arms.sh           the four Callgrind arms and the -g object builds, as run
calls.py          callgrind call-graph reader (from evidence/2026-10-12T220456Z)
call-counts.txt   caller -> callee call counts, inclusive Ir and I refs per arm
callgrind-raw.tar.gz  callgrind.out files (A, B, C, D, census) and arm logs
identities.txt    sha256 and size of every binary and object, and revisions
stmt_expr_probe.c Debug probe that reaches site 2's reference check

Arms: A base default, B base -fc-ast-pilot, C candidate -fc-ast-pilot,
D candidate default. Census = base + census.patch, default flags.
Object files are not retained (31.5 MB / 50.1 MB each); their hashes are.
Replay: tar -xzf callgrind-raw.tar.gz; python3 calls.py A.cg '<regex>'.

Census agreement: census_type_word calls census_tw1_mismatch or
census_tw2_mismatch whenever the token answer differs from the spelling
answer. Neither marker appears in census.cg (see call-counts.txt), so all
48,315 answers agreed. census_tw2_call never appears: site 2 is not
reached on the unity input. census_tw1_nosymbol never appears either.

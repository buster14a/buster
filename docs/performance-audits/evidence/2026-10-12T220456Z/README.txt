Raw evidence for audit 2026-10-12T220456Z (hosted Callgrind, diagnostic only).

census.patch      instrumentation applied to base 7bc92b2d for the census build
                  (git diff of the throughput census worktree; not committed code)
census.py         script that wrote the census.patch c_gen.c hunks
expose.py         script that made c_parse_type_word_for_dialect_token shared
                  (the same edit is in the candidate)
arms.sh           the four Callgrind arms and the -g object builds, as run
calls.py          callgrind call-graph reader that produced call-counts.txt
call-counts.txt   caller -> callee call counts and inclusive Ir per arm
callgrind-raw.tar.gz  callgrind.out files (A, B, C, D, census) and arm logs
identities.txt    sha256 and size of every binary and object, and revisions

Arms: A base default, B base -fc-ast-pilot, C candidate -fc-ast-pilot,
D candidate default. Census = base + census.patch, default flags.
Object files are not retained (31.5 MB / 50.0 MB each); their hashes are.
Replay: tar -xzf callgrind-raw.tar.gz; python3 calls.py A.cg '<regex>'.

Census agreement: census_type_word calls census_tw1_mismatch or
census_tw2_mismatch whenever the token answer differs from the spelling
answer. Neither marker appears in census.cg (see call-counts.txt), so all
103,554 answers agreed.
In A, c_ir_lower_body_advance -> c_ir_named_label_proven_at is 18,894:
the 12 first-token label proofs (inlined test) plus the 18,882 of the
dead-code hop.

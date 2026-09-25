# Workload families (declared 2026-09-25T21:17:01Z, before any prototype measurement)

Base: main d9e736e2cf08804a2c603d153e9fb608f18c5c49 (tree ae9f274b6768b023fe6e0ef80a161e14b290e0a2).

Tuning/observation families (already looked at before the prototype):
- F1 tiny synthetic: tiny.c (one function), hello.c (stdio + printf), hello link.
- F2 repository fixture: tests/basic_c_operations.c (-c).
- F3 one Buster library TU non-unity: src/buster/lib/string.c.
- F4 lazy-shape regression fixture (driver test source, all allocators).

Held-out family (not measured on the prototype until the prototype is frozen):
- H1 every other src/buster/lib/*.c compiled non-unity
  (-Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=0 -DBUSTER_INCLUDE_TESTS=0 -c), list in heldout_files.txt.
- H2 stage-1 self-compile (unity ide.c, -g), large-input family.

Correctness corpus (byte equality, not tuning): every tests/*.c fixture, -c, default target.

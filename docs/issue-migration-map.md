# Forgejo to GitHub issue migration

The 63 issues that were open on 2026-08-31 were copied to GitHub. Numbers could
not be preserved: GitHub had already issued #1-30 during buster's earlier public
life, and Forgejo draws issues and pull requests from one sequence that had
reached #861. Every migrated issue therefore carries a provenance line linking
back to its Forgejo original, and the table below is the reverse index.

Historical references in code comments, commit messages, and `AGENTS.md` name
**Forgejo** numbers. Read them through this table, or through the closed-issue
archive beside it (`forgejo-issue-archive.md`), which preserves the issues that
were already closed and were not copied.

| Forgejo | GitHub | Title |
|---|---|---|
| [#584](https://code.buster14a.com/buster/buster/issues/584) | [#31](https://github.com/buster14a/buster/issues/31) | compiler: converge on a dense SSA middle/backend pipeline |
| [#585](https://code.buster14a.com/buster/buster/issues/585) | [#32](https://github.com/buster14a/buster/issues/32) | compiler: enforce a single-definition machine-IR contract |
| [#586](https://code.buster14a.com/buster/buster/issues/586) | [#33](https://github.com/buster14a/buster/issues/33) | compiler: promote eligible locals in canonical IR |
| [#587](https://code.buster14a.com/buster/buster/issues/587) | [#34](https://github.com/buster14a/buster/issues/34) | compiler: build pruned SSA directly in the C frontend |
| [#588](https://code.buster14a.com/buster/buster/issues/588) | [#35](https://github.com/buster14a/buster/issues/35) | compiler: measure and close machine-IR coverage gaps |
| [#589](https://code.buster14a.com/buster/buster/issues/589) | [#36](https://github.com/buster14a/buster/issues/36) | compiler: retire the direct native canonical backend |
| [#590](https://code.buster14a.com/buster/buster/issues/590) | [#37](https://github.com/buster14a/buster/issues/37) | x86-64: add an allocatable AVX-512 mask register bank |
| [#591](https://code.buster14a.com/buster/buster/issues/591) | [#38](https://github.com/buster14a/buster/issues/38) | ir: publish a finalized dense canonical CFG |
| [#592](https://code.buster14a.com/buster/buster/issues/592) | [#39](https://github.com/buster14a/buster/issues/39) | abi: move decomposition into active-target side tables |
| [#593](https://code.buster14a.com/buster/buster/issues/593) | [#40](https://github.com/buster14a/buster/issues/40) | compiler: define a compact FAST canonical pipeline |
| [#594](https://code.buster14a.com/buster/buster/issues/594) | [#41](https://github.com/buster14a/buster/issues/41) | scheduler: add proven machine-memory alias classes |
| [#595](https://code.buster14a.com/buster/buster/issues/595) | [#42](https://github.com/buster14a/buster/issues/42) | isel: make declarative selection authoritative or remove it |
| [#596](https://code.buster14a.com/buster/buster/issues/596) | [#43](https://github.com/buster14a/buster/issues/43) | ir: separate vector operations, exact intrinsics, and predicates |
| [#597](https://code.buster14a.com/buster/buster/issues/597) | [#44](https://github.com/buster14a/buster/issues/44) | ir: build shared address-expression facts with provenance |
| [#598](https://code.buster14a.com/buster/buster/issues/598) | [#45](https://github.com/buster14a/buster/issues/45) | machine-ir: audit dormant metadata and MachineAddress abstractions |
| [#601](https://code.buster14a.com/buster/buster/issues/601) | [#46](https://github.com/buster14a/buster/issues/46) | perf: establish a reproducible Zen 5 performance acceptance harness |
| [#602](https://code.buster14a.com/buster/buster/issues/602) | [#47](https://github.com/buster14a/buster/issues/47) | optimizer: connect native -O levels to the CPU optimization pipeline |
| [#603](https://code.buster14a.com/buster/buster/issues/603) | [#48](https://github.com/buster14a/buster/issues/48) | optimizer: implement tiny-function and always_inline inlining |
| [#604](https://code.buster14a.com/buster/buster/issues/604) | [#49](https://github.com/buster14a/buster/issues/49) | optimizer: add a compact local native optimization pipeline |
| [#605](https://code.buster14a.com/buster/buster/issues/605) | [#50](https://github.com/buster14a/buster/issues/50) | ir: add a trusted inlinable instruction-append fast path |
| [#606](https://code.buster14a.com/buster/buster/issues/606) | [#51](https://github.com/buster14a/buster/issues/51) | preprocessor: replace pointer-rich macro expansion with range-based storage |
| [#607](https://code.buster14a.com/buster/buster/issues/607) | [#52](https://github.com/buster14a/buster/issues/52) | c frontend: estimate IR block, instruction, and value capacities separately |
| [#608](https://code.buster14a.com/buster/buster/issues/608) | [#53](https://github.com/buster14a/buster/issues/53) | driver: parallelize independent translation-unit compilation |
| [#609](https://code.buster14a.com/buster/buster/issues/609) | [#54](https://github.com/buster14a/buster/issues/54) | codegen: parallelize per-function machine-code generation |
| [#610](https://code.buster14a.com/buster/buster/issues/610) | [#55](https://github.com/buster14a/buster/issues/55) | build: add a production Clang PGO/LTO profile with debug info disabled |
| [#611](https://code.buster14a.com/buster/buster/issues/611) | [#56](https://github.com/buster14a/buster/issues/56) | ir: reduce values and live ranges before redesigning register allocation |
| [#612](https://code.buster14a.com/buster/buster/issues/612) | [#57](https://github.com/buster14a/buster/issues/57) | c frontend: fuse quoted-literal decoding and add an AVX-512 fast path |
| [#613](https://code.buster14a.com/buster/buster/issues/613) | [#58](https://github.com/buster14a/buster/issues/58) | x86-64: compare helper-based, branched, and branchless register-load selection |
| [#614](https://code.buster14a.com/buster/buster/issues/614) | [#59](https://github.com/buster14a/buster/issues/59) | x86-64: prototype SIMD encoding for homogeneous instruction batches |
| [#615](https://code.buster14a.com/buster/buster/issues/615) | [#60](https://github.com/buster14a/buster/issues/60) | preprocessor: prototype SIMD scanning after contiguous token storage lands |
| [#616](https://code.buster14a.com/buster/buster/issues/616) | [#61](https://github.com/buster14a/buster/issues/61) | epic: close the self-hosted compiler performance gap |
| [#618](https://code.buster14a.com/buster/buster/issues/618) | [#62](https://github.com/buster14a/buster/issues/62) | ir: make canonical validation total over malformed call/type graphs |
| [#619](https://code.buster14a.com/buster/buster/issues/619) | [#63](https://github.com/buster14a/buster/issues/63) | runtime: separate optimizer assumptions from fallible resource checks |
| [#620](https://code.buster14a.com/buster/buster/issues/620) | [#64](https://github.com/buster14a/buster/issues/64) | debug: validate caller-supplied location indexes before use |
| [#621](https://code.buster14a.com/buster/buster/issues/621) | [#65](https://github.com/buster14a/buster/issues/65) | tests: add fatal prerequisites for dependent assertions |
| [#622](https://code.buster14a.com/buster/buster/issues/622) | [#66](https://github.com/buster14a/buster/issues/66) | tests: make external compiler fixtures family-aware |
| [#623](https://code.buster14a.com/buster/buster/issues/623) | [#67](https://github.com/buster14a/buster/issues/67) | build: bound peak memory of the x86 metadata tests |
| [#624](https://code.buster14a.com/buster/buster/issues/624) | [#68](https://github.com/buster14a/buster/issues/68) | build: reproduce and gate strict-Clang bootstrap portability |
| [#810](https://code.buster14a.com/buster/buster/issues/810) | [#69](https://github.com/buster14a/buster/issues/69) | aarch64: i128 multiply, divide, clz, float conversions and variable shifts on the machine path |
| [#812](https://code.buster14a.com/buster/buster/issues/812) | [#70](https://github.com/buster14a/buster/issues/70) | codegen: inline assembly never selects on either machine path |
| [#813](https://code.buster14a.com/buster/buster/issues/813) | [#71](https://github.com/buster14a/buster/issues/71) | codegen: the machine census's small-class tail (reversed-subscript store, VLA loads, vector unary/divide, packed encode, f128 cast) |
| [#815](https://code.buster14a.com/buster/buster/issues/815) | [#72](https://github.com/buster14a/buster/issues/72) | aarch64: long double is binary128 and the frontend does not lower it |
| [#816](https://code.buster14a.com/buster/buster/issues/816) | [#73](https://github.com/buster14a/buster/issues/73) | frontend: non-power-of-two vector sizes are refused in every signature |
| [#836](https://code.buster14a.com/buster/buster/issues/836) | [#74](https://github.com/buster14a/buster/issues/74) | c: the generic and nand spellings of the GNU __atomic_* family are still unbound |
| [#837](https://code.buster14a.com/buster/buster/issues/837) | [#75](https://github.com/buster14a/buster/issues/75) | test_quickjs: test_std.js segfaults under the NONE allocator |
| [#838](https://code.buster14a.com/buster/buster/issues/838) | [#76](https://github.com/buster14a/buster/issues/76) | c: a conditional directive inside a macro's arguments ends the invocation |
| [#840](https://code.buster14a.com/buster/buster/issues/840) | [#77](https://github.com/buster14a/buster/issues/77) | link: ELF reader refuses relocatable objects carrying DWARF 5 debug sections |
| [#841](https://code.buster14a.com/buster/buster/issues/841) | [#78](https://github.com/buster14a/buster/issues/78) | link: GOTPCRELX conversion vocabulary is one mov form |
| [#842](https://code.buster14a.com/buster/buster/issues/842) | [#79](https://github.com/buster14a/buster/issues/79) | codegen: eval-loop-sized frames are 10x clang's and break CPython's recursion budget |
| [#843](https://code.buster14a.com/buster/buster/issues/843) | [#80](https://github.com/buster14a/buster/issues/80) | link: executables carry no .symtab, so gdb sees a nameless image |
| [#849](https://code.buster14a.com/buster/buster/issues/849) | [#81](https://github.com/buster14a/buster/issues/81) | preprocessor: replace fixed once_paths and fix recursive #import overflow |
| [#850](https://code.buster14a.com/buster/buster/issues/850) | [#82](https://github.com/buster14a/buster/issues/82) | io: propagate write and close failures through file_write |
| [#851](https://code.buster14a.com/buster/buster/issues/851) | [#83](https://github.com/buster14a/buster/issues/83) | compiler: publish artifacts atomically instead of truncating in place |
| [#852](https://code.buster14a.com/buster/buster/issues/852) | [#84](https://github.com/buster14a/buster/issues/84) | runtime: make process spawning transactional and hermetic |
| [#853](https://code.buster14a.com/buster/buster/issues/853) | [#85](https://github.com/buster14a/buster/issues/85) | runtime: bound captured output and contain timed-out process trees |
| [#854](https://code.buster14a.com/buster/buster/issues/854) | [#86](https://github.com/buster14a/buster/issues/86) | gpu: use private owned directories for temporary artifacts |
| [#855](https://code.buster14a.com/buster/buster/issues/855) | [#87](https://github.com/buster14a/buster/issues/87) | scheduler: evaluate queue growth once per insertion |
| [#856](https://code.buster14a.com/buster/buster/issues/856) | [#88](https://github.com/buster14a/buster/issues/88) | x86-64: fix the GCC Release maybe-uninitialized failure |
| [#857](https://code.buster14a.com/buster/buster/issues/857) | [#89](https://github.com/buster14a/buster/issues/89) | build: compile modularly and generate registries from one manifest |
| [#858](https://code.buster14a.com/buster/buster/issues/858) | [#90](https://github.com/buster14a/buster/issues/90) | compiler: split phase-spanning functions behind explicit contracts |
| [#859](https://code.buster14a.com/buster/buster/issues/859) | [#91](https://github.com/buster14a/buster/issues/91) | tests: measure and narrow fixture arena lifetimes |
| [#860](https://code.buster14a.com/buster/buster/issues/860) | [#92](https://github.com/buster14a/buster/issues/92) | build: shard Clang static analysis with fail-closed aggregation |
| [#861](https://code.buster14a.com/buster/buster/issues/861) | [#93](https://github.com/buster14a/buster/issues/93) | ci: pin every external Forgejo action to an immutable commit |

# bind

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/bind.c` is binding (the second part of step 5): the C
  runtime that `runtime.c` imports joined to a program's link in the order
  GCC's driver links it, one symbol per global name, and each name of the link
  bound as GNU ld binds it: to the program, the runtime's objects, the linker,
  an export of `libc.so.6` at its version (an import), a member of
  `libgcc.a` (`__udivti3`) or `libc_nonshared.a` (`atexit`), which joins, an
  export of `libgcc_s.so.1` (the unwinder: `_Unwind_Resume`,
  `_Unwind_Backtrace`), which the link then needs, as GCC's driver names it
  as needed after `libgcc.a` (#489), or for a weak reference to nothing; with
  the link's imports and exports listed. On a system without
  `libgcc_s.so.1`, a name that only the members which joined refer to and
  nothing defines is refused, naming the member
  (`BIND_ERROR_MEMBER_UNDEFINED`): `libgcc.a`'s split-stack `morestack.o`'s
  unwinder. It has a vector path and an exact
  one, its twin, which make the same link. `link_program.c`'s programs leave
  room for it (`link_program_generate_begin`, `_atoms` and `_end`), and the C
  driver's links take room for the crt objects and the members their names
  may make join (`bind_capacity_needed`), rather than for every member
  (`bind_capacity`).
  It binds against the static runtime too (step 6's second part): the
  archives' members the link needs, found as GNU ld's group finds them, joined
  in archive order with the COMDAT groups after the first of each signature
  discarded, and no name bound to a shared library. And against a shared
  library's runtime (step 6's fifth part), as GNU ld and lld bind `-shared`:
  no start object, a strong reference to a name no library exports left
  undefined for the loader (but a member's, refused as in a PIE), and every
  definition with default or protected visibility exported
  (`bind_shared_exports`).
  `bind_test.c` holds its tests (`main.c` includes it; `./build/compiler
  test-bind` runs them) and `bind_benchmark.c` provides `benchmark-bind`.
## Bind tests

`./build/compiler test-bind [--seed N] [--cross-check] [--keep]` (about
0.11 s on the 9700X, and 1.35 s with `--cross-check`) checks `bind.c` against
this system's runtime (`runtime_import`). Every check binds by both paths,
which must make the same link, column by column, and the same imports,
exports and members:

- programs: test-link's two sets that call libc, each generated with room for
  the runtime, Scrt1.o, crti.o and crtbeginS.o joined after the program's
  symbols, its atoms, then the rest (`link_program_generate_begin`, `_atoms`
  and `_end`, which runs `link_check` on the whole link). Then: one symbol per
  global name; every name's bytes in the link's own arena and every atom's in
  the program's; every fixup's target defined, imported from `libc.so.6` at
  its export's version and with its type, the linker's, or a weak reference
  to nothing; `main` the program's, and what the crt objects refer to bound
  as GNU ld binds it (`__libc_start_main` at `GLIBC_2.34`, `__cxa_finalize`
  weakly, `__gmon_start__` and the `_ITM_` functions to nothing,
  `_GLOBAL_OFFSET_TABLE_` to the linker, `__TMC_END__` to `crtendS.o`,
  `__dso_handle` in `crtbeginS.o`, hidden); the crt objects' atoms before the
  program's and after it in GCC's order, each with the runtime's bytes and
  fixups; `.init`'s and `.fini`'s fragments `crti.o`'s first and `crtn.o`'s
  last, and `crtendS.o`'s `.eh_frame` last;
- names: every name of the programs and every export of `libc.so.6`, looked
  up in the runtime's index, must find what a scan of the runtime's names
  finds;
- rules: links made by hand, each exercising one rule, with what binding
  reports: a strong definition of `_start` (a duplicate); one of `data_start`,
  which Scrt1.o defines weakly (the program's wins), and a weak one and a weak
  absolute one (Scrt1.o's wins, first in link order, at `__data_start`'s place
  over the absolute one); weak definitions of `_init`, with their atoms
  before and after `bind_begin` (crti.o's strong one takes the symbol, with
  its atom in `.init` and its visibility); a reference to
  `__dso_handle` (defined by crtbeginS.o's atom, hidden); strong and weak
  references to a name nothing defines (an error, and nothing); definitions
  of `strlen`, which libc.so.6 defines, and `_dl_argv`, which it refers to
  (exports), a hidden `qsort` and a name of the program's own (not); two
  symbols named `write`; references added after `bind_begin` of a name the
  runtime has and of `memcpy`; strong and weak references to `__cxa_finalize`,
  which crtbeginS.o refers to weakly; strong and weak references to
  `pthread_atfork`, which a member defines (it joins, and doesn't), and a
  hidden one (it joins); hidden and protected references to `puts`, which only
  libc.so.6 defines (an error), and a weak hidden one (nothing, not an
  import); references to the init and fini arrays' bounds (the linker's);
  references to `__morestack` and to `__splitstack_getcontext`, whose
  members of `libgcc.a` make `morestack.o` join (its `_Unwind_Resume` a
  strong import of `libgcc_s.so.1`'s, and against the runtime without it,
  `bind_test_without_gcc_s`, refused, naming `_Unwind_Resume` and the
  member: `BIND_ERROR_MEMBER_UNDEFINED`), and one to `__morestack` beside the
  program's own unwinder (bound); a
  local `_init`; a symbol of the program's that it never defines (left to
  `link_check`); and a link without room. Then the rules this system's runtime
  can't reach through the entry points, on binding's own steps: a strong
  reference joining a weak one that binding left to nothing (an error, but
  not for a name left to `crtendS.o`, nor for one that a member of the current
  round added, still to bind: `Bind.resolved`), a name left to `crtendS.o`
  with source `RUNTIME_ARCHIVE`, and an import a member came to define dropped
  from the imports;
- libraries named with `-l`: links made by hand bound with this system's libm
  (`runtime_libraries_import`): references to libm's names imports of libm's
  at their versions, `ldexp`'s before libc.so.6's, a weak reference alone
  needing no library, and a definition of a name libm exports exported; the
  import's refusals and an empty archive; and libraries made by hand, two that
  refer to `b_func` and one that exports it: without a definition of
  `b_func`, or with a hidden one or a reference, the link needs all three, and
  with a global, weak, protected or absolute one, which the executable
  exports, the two alone (the global one after 0 to 15 other definitions);
  without the third, a needed library's reference to `b_func` is
  `BIND_ERROR_LIBRARY_UNDEFINED` naming the library (but with a global
  definition, or neither needed), and a library whose `DT_NEEDED` names one
  the link lacks isn't checked; and libraries made by hand that refer
  strongly to `_Unwind_Backtrace` without and with a `DT_NEEDED` for
  `libgcc_s.so.1`, to `__tls_get_addr`, and weakly to `_Unwind_Backtrace`:
  as GNU ld's `--as-needed` keeps it, `libgcc_s.so.1` (or the loader) is
  needed (`Bind.runtime_needed`) for a needed library's strong reference its
  `DT_NEEDED` doesn't cover, and not when main defines the name, the library
  isn't needed, the reference is weak or another needed library's `DT_NEEDED`
  names `libgcc_s.so.1` (without `libgcc_s.so.1`, refused);
- an atexit program: `main` registers a handler with `atexit` and returns 3,
  the handler exits with 0. Bound, it must pass `link_check`, and `atexit.oS`
  must join, defining `atexit`, its `__cxa_atexit` imported at `GLIBC_2.2.5`
  and its `__dso_handle` bound to crtbeginS.o's;
- a libgcc program: `main` calls `__udivti3`, `__fixunsdfti` and `__fixdfti`
  (100 by 7, 3.0 and -2.0) and returns their results' sum, its references of
  source NONE, as the C driver's are. Bound, it must pass `link_check`, and
  the three names' members of `libgcc.a` must join, and no others, each
  defining its name hidden, which no import stands for; `__fixdfti`'s member
  calls `__fixunsdfti` too, and joins first;
- the room (`bind_capacity_needed`), against this runtime and a shared
  library's: for each of the members with a global name that no library
  exports (300), a link of `main` and a strong reference to that name, bound
  with every member's room, must bind (or, without `libgcc_s.so.1`, be
  refused for `morestack.o`'s reference to the unwinder) adding no more
  records, names' bytes and atoms'
  bytes than `bind_capacity_needed` counts for the name, and that count no
  more than `bind_capacity`'s. Members that join in one round refer to names
  none of them defines yet (`memset`, `__sfp_handle_exceptions`, the decimal
  floating point's thread-local flags), which mustn't be taken for weak
  references left to nothing;
- dirty arenas (#490's review): the programs above and the atexit and libgcc
  programs, bound once more by each path over binding's arena dirtied first
  (`bind_test_dirty_arena`: filled with 0xa5 as far as the clean binding
  pushed and as far again, then popped), must give the clean binding's link
  and binding, and keep the pattern in an entry of `Bind.symbols` of a piece
  that didn't join: binding doesn't clear that array, and writes each piece's
  entries as the piece joins, before its fixups read them;
- the linkers' cases, programs whose binding GNU ld and lld decide for
  themselves: a weak `_init` that exits with 7 if it runs, against crti.o's;
  `main` returning a weak `data_start` of 7, after Scrt1.o's of 0; a call
  through the GOT to a hidden `puts` (refused); `main` returning whether a
  weak hidden `puts` isn't 0; references to the arrays' bounds; and `main`
  returning `_r_debug`'s `r_version` plus 10 when `__libc_stack_end` isn't 0,
  which only the loader exports (status 11), with both references strong,
  both weak and one of each. GNU ld and lld need a library `AS_NEEDED` only
  for a strong reference, so with weak ones alone the loader isn't needed and
  both are left to nothing, unversioned, and with a strong one both are
  imports at the loader's versions. Each must bind, or fail to, as the
  linkers link it;
- with `--cross-check`, which needs `ld.lld`, `ld.bfd`, `cc` and `readelf`:
  each bound program written as objects, one per unit, linked by ld.lld and
  ld.bfd as a PIE with only `libc.so.6` and, as needed, the loader besides,
  in GCC's order and with `--eh-frame-hdr` as GCC's driver asks, without
  errors or warnings, must run and print what its model predicts (the atexit
  program must exit with 0, the libgcc program with 15, the loader's cases
  with 11); and the program's own objects, linked by `cc` with lld, must have
  in readelf's `.dynsym` the imports at their versions and with their types,
  the weak references with default visibility and the exports binding found,
  and lld's `--why-extract` the members that joined, of either archive. Each linkers' case's own objects, linked by `cc`
  with GNU ld and with lld, must link, or fail to, as binding did, and run
  with the case's status (0), and so must its bound link.

Then the same against the static runtime (`runtime_import_static`), by both
paths, which must make the same link and join the same members:

- static programs: test-link's two sets that call libc. The members that
  join must be those GNU ld's archive scan loads, and no others: an oracle of
  the test's own that scans the archives' symbol indices in the group's order
  until a pass loads nothing, rather than reading binding's references. Then
  `link_check`; one symbol per global name and none of a shared library;
  every fixup's target defined, absolute, the static linker's, a weak
  reference to nothing, or `__tls_get_addr`, left to the relaxation; `main`
  the program's, `__libc_start_main` a member's, `_init` crti.o's,
  `__dso_handle` crtbeginT.o's and `_dl_relocate_static_pie` crt1.o's; and
  each joined object's atoms, in link order, but those of a COMDAT group
  whose signature an object before it has, with their priorities; and each
  bound again over a dirty arena, as above;
- static rules, on links made by hand: a strong reference to a name nothing
  defines (an error naming it); strong and weak references to
  `__tls_get_addr` (left undefined); the static linker's names (the
  linker's), and `_DYNAMIC`, which only a PIE's linker defines (an error
  naming it); weak and strong references to `puts` (the weak one binds to its
  member only if the closure loads it for another reason); a weak definition
  of `__libc_start_main` (crt1.o's reference takes it, and its member doesn't
  join); a weak absolute definition of `data_start` (crt1.o's weak one, first
  in link order, takes its place, at `__data_start`'s, as GNU ld and lld
  bind it); a reference to a name a member defines absolutely; and weak
  definitions of 4 and of 24 names the members refer to (none thread-local),
  which the vector path's closure compares with, or past 16 leaves to the
  exact path's;
- with `--cross-check`: each static program's own objects linked by
  `cc -static` with GNU ld (its `--trace` given twice) and with lld
  (`--why-extract`), which must load the members that joined and no others,
  and run printing what the model predicts; and five C programs that `cc`
  compiles (hello, stdio, threads, jumps and weak-puts), each imported as the
  program, bound and checked as the static programs are, and linked by both
  linkers, with the same members, to run with the program's status.

Then the static programs, the static rules, and with `--cross-check` the C
programs (compiled with `-fPIE`), against the static PIE's runtime
(`runtime_import_static_pie`), by both paths, which must make the same link
and join the same members, checked as against the static runtime: the
members the oracle's scan loads, among them `libc.a`'s
`dl-reloc-static-pie.o`, which `rcrt1.o` makes join by not defining
`_dl_relocate_static_pie`; that name the member's, its `_DYNAMIC` the
linker's and `__dso_handle` crtbeginS.o's; the rules' linker's names a static
PIE's (`_DYNAMIC` among them), and a strong reference to
`__rela_iplt_start`, which a static PIE's linker leaves to libc.a's weak
references, an error naming it; and with `--cross-check`, each
linked by `cc -static-pie` with GNU ld (`--trace` twice) and with lld
(`--why-extract`), which must load the same members and run as the model
predicts, or with the program's status.

Then against a shared library's runtime (`runtime_import_shared`), by both
paths, which must make the same link and binding:

- shared programs: test-link's two sets that call libc, bound as libraries:
  `link_check`; no `_start`, as there is no start object; `_init`, `_fini` and
  `__dso_handle` hidden and not exported; and the exports exactly the link's
  definitions with default or protected visibility, global or weak, in
  symbol order, `main` among them; and each bound again over a dirty arena;
- shared rules, on links made by hand without `main`: a strong reference
  with default visibility to a name nothing defines stays undefined, neither
  an import nor an export, and a hidden one is an error naming it;
  definitions with default, protected and hidden visibility and a weak one,
  all but the hidden one exports; references to `__tls_get_addr` and
  `puts`, imports of the loader's at `GLIBC_2.3` and of `libc.so.6`'s at
  `GLIBC_2.2.5`; and a reference to `__morestack`, whose member's reference
  to the unwinder binds to `libgcc_s.so.1`, as in a PIE, rather than being
  left to the loader (and without `libgcc_s.so.1` is refused);
- with `--cross-check`, three C libraries that `cc` compiles with `-fPIC`
  (exports of every visibility; imports from `libc.so.6` and the loader,
  general-dynamic TLS with `-mtls-dialect=gnu`, and undefined and weak
  references; and `atexit`, whose member joins), each imported, bound and
  checked as the programs are, then linked by `cc -shared` with GNU ld, which
  must link it, and with lld, whose `.dynsym` must have the imports at their
  versions, the undefined and weak references and the exports binding found,
  and whose `--why-extract` the members that joined;
- with `--cross-check`, split-stack code (`bind_test_split_stack`): C
  compiled with `-fsplit-stack` (and `-fcf-protection=none`, whose `endbr64`
  keeps lld from adjusting a split-stack prologue) as a library and as a
  PIE, which binding binds by both paths alike, `morestack.o` joined and its
  `_Unwind_Resume` a strong import of `libgcc_s.so.1`'s; the bound links'
  objects linked by ld.lld and ld.bfd with `libgcc_s.so.1` as needed run,
  the library in a harness that doesn't need `libgcc_s.so.1` (`dlopen` with
  `RTLD_NOW`). Against the runtimes without `libgcc_s.so.1` binding refuses
  both, naming `_Unwind_Resume` and `libgcc.a(morestack.o)`. `cc`'s links of
  the same objects with GNU ld and with lld need `libgcc_s.so.1`, load into
  the harness and run; and lld's link of the library with `libgcc.a` and
  libc but not `libgcc_s.so.1`, which leaves the unwinder to the loader, must
  fail to load there.

The objects and programs go in a new directory under `$TMPDIR` (or `/tmp`),
removed at the end unless `--keep`. The 9700X job's test step runs it with
`--cross-check`.

## Bind benchmark

`./build/compiler benchmark-bind [--functions LIST] [--seed N]` binds
generated programs that call libc, of 1, 64, 2,000 and 102,400 workers by
default (`--functions` takes a list), 200 workers per unit as synth's
translation units have, by each path, against the dynamic PIE's runtime, a
static executable's, a static PIE's and a shared library's, and against the
dynamic PIE's with libm on the line as `-lm` puts it there, which these
programs don't need (each name looked up in libm's and libmvec's exports too),
and reports 21-run medians (time,
cycles, instructions, IPC) of libm's import (`runtime_libraries_import`), the
match (the program's global symbols looked
up in the runtime's names, per global symbol), `bind_begin`, the static
runtimes' closure (the members the link needs, per member) and `bind_end`,
each run on the same link put back as it was. The paths must make the same
links. Run it under the 9700X's lock with `taskset -c 2`; the design's "Step
5's phases, data-parallel", "Static links, as built" and "Libraries named with
`-l`" have the numbers. The
default 9700X job runs it after the tests, pinned to one allowed CPU;
like the others, its measurements are informational.

# call-graph

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/call_graph.c` is the call graph check, the coding
  style's rule against recursion checked mechanically: `check-call-graph`
  reads the LLVM IR of the translation units `build.sh` compiles, builds their
  call graph and reports every cycle (Tarjan's algorithm, iterative) that isn't
  in its allowlist, `call_graph_allowed`. `./build.sh check-call-graph` writes
  that IR to a temporary directory and runs it. `call_graph_test.c` holds its
  tests (`main.c` includes both; `./build/compiler test-call-graph` runs them),
  and `call_graph_capture/` the C fixture that checks the IR capture itself.
## Call graph check

The coding style forbids recursion over input-dependent structures, and
`./build.sh check-call-graph` checks it on every function of the compiler. It
writes the LLVM IR of `build.sh`'s translation units, with the same flags and
optimization level, `-O3`, but `-Xclang -disable-llvm-passes` (no pass, not
even the always-inliner, so a call in the IR is a call in the source, while
the IR holds every body the build may inline: at `-O0`, clang doesn't emit an
`extern inline` body at all) into a temporary directory, and runs
`./build/compiler check-call-graph FILE.ll...` on them (so it needs a build).
First it captures `src/buster/compiler/call_graph_capture` the same way, a
cycle through an `extern inline` body beside a strong definition in another
unit, and fails unless the check reports it.
The command reads each `define` and the direct `call`, `invoke` and `callbr`
instructions in its body; a callee is its translation unit's function or alias
of that name first (static functions are per unit: the benchmarks' copies of
`lane.c` are separate functions), then the external symbol's strong
definitions, or all its weak ones when it has none (the linker takes one of
them by link order), and every definition of the name in the calling unit,
which clang may inline into the call whatever the linker takes (an
`available_externally` or `*_odr` body, `always_inline`, `flatten`). A
replaced weak body that clang wouldn't inline then counts too: that can only
add a cycle, never hide one; an alias, through any chain of them, is its target's
definition in its own module, whatever replaces that name elsewhere. A second strong definition of a name fails the check. Calls of functions defined
nowhere (libc, intrinsics) and calls through pointers, which the style allows
only at the thread-entry boundary, are left out; a call of an ifunc or of an
alias that leads to no definition fails the check.

It reports each strongly connected component of more than one function and
each function that calls itself, with the shortest call path through the
component's first function, as `unit.ll:function -> ... -> unit.ll:function`,
then a summary line, and exits with status 1 on a cycle not in
`call_graph_allowed`, on an allowlist entry that matches no cycle, and on IR it
can't read. An entry names a component's functions, each as many times as
the component has a function of that name; add one only for the
style's "trivial statically bounded recursion", with a comment saying what
bounds its depth. The list has three entries, the preprocessor fuzz generator's
recursions (conditionals in groups, expressions, invocations in arguments),
each bounded by its depth argument. The 9700X job runs
`test-call-graph` and the check after the build; the check, which captures the
units' IR at once, takes about 2 s.

The same command checks that no command splits a static variable between
translation units, which the homes of `main.c`'s modules could do (`main.c`:
a command runs in its own unit's code and in the homes of the modules it
calls, each with its own copies of the static variables). From each command,
each function that a unit's `main_command` calls or names, it walks every
function and variable the command may reach: through calls, into other units
as they resolve, and through the functions and variables that function bodies
and variable initializers name (a function passed as a lane group's entry, a
table of pointers), and through aliases to the variables they lead to, each
target in its alias's own module, but not through quoted strings (inline
assembly templates, section names). A variable that is a unit's own
(`internal` or `private`) and `global` rather than `constant`, reached in two
units' copies by the same name, is reported once, at the first command that
splits it: `split state: unit.ll:command reaches a.ll:name from a.ll:function,
and b.ll:name from b.ll:function`, and fails the check. A variable that units
share (defined in one and declared in the others, as `lane.c`'s thread
context) is never split. A home's function that splits one is one to compile
in every unit again, or whose module's state its units must share. The summary
line counts the split variables.

`./build/compiler test-call-graph` (about 0.4 s) checks the report, exactly,
on IR of the call forms clang writes and of what must be left out, a
self-call, a two-function cycle, a cycle with a chord (whose path must be the
shortest), a cycle through three translation units beside static and external
functions of the same name, clang's IR of recursion through aliases and of a
call of an ifunc, aliases that lead nowhere, clang's IR of weak and strong
functions and aliases of one name, of an alias of a replaced weak function, and
of `available_externally`, `always_inline` and `flatten` inlining past a
strong definition elsewhere, in both orders, replaced weak bodies, two units' copies of a cycle
allowed by one entry, a cycle grown past its entry with an unused entry, a
cycle through two units' static functions of the same name that entries naming
it once mustn't match, malformed IR, and a chain of a million functions and a
ring of 100,000, deeper than a recursive walk's stack. Then the split-state
walk: a table that the driver's unit writes and a home reads (split), each
unit's own state (not), two commands each with its own unit's copy (not),
copies reached through a function passed as an argument and through a constant
table of pointers (split, each reported at its own command), a constant and a
shared variable of one name in both units, and a variable that only an inline
assembly template, a section's name and a quoted name spell (not), the driver's
copy reached through an alias of it and through a chain of two whose middle
one's name another unit's external variable has, beside two aliases of each
other (split), and a static function's thread-local static compiled in both
units (split).

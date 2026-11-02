# Linker options in the C driver

The C command treats `-Wl,a,b,...` as comma-separated linker arguments and
`-Xlinker ARG` as one literal linker argument. They are not warning options.
Forwarded arguments and native `-l` / `-L` arguments retain their relative
order. Another driver's option still owns its operand: for example,
`-o -Wl,result` names an output file rather than forwarding an option.

```sh
build/compiler main.c -L ./lib -lwidget '-Wl,-rpath,$ORIGIN/lib' -o app
build/compiler main.c -Xlinker -rpath -Xlinker '/a,path/with,commas' -o app
```

The shell quotes above preserve `$ORIGIN` and commas. Buster does not expand
runtime paths; the loader interprets them. `-L` selects libraries at link time,
whereas `-rpath` records directories to search when the executable runs.

## Supported direct-link options

| Linker arguments | Effect |
| --- | --- |
| `-rpath PATH`, `--rpath PATH`, `-rpath=PATH`, `--rpath=PATH`, `-R PATH`, `-RPATH` | Append a runtime search path. Repeated paths are joined with `:` in command-line order; empty components are retained. |
| `--enable-new-dtags`, `--disable-new-dtags` | Select `DT_RUNPATH` (the default) or `DT_RPATH`; the last selection wins. |
| `-e NAME`, `--entry NAME`, `--entry=NAME` | Select a defined, non-local code symbol as the entry point. The last selection wins. An undefined entry is an error, not a fallback to `_start`. |
| `--as-needed`, `--no-as-needed` | Select whether subsequent named libraries need a reference to be retained. The default preserves the driver's existing as-needed behavior. A repeated library explicitly retained by any no-as-needed occurrence is kept once. |
| `-l LIB`, `-lLIB`, `-L DIR`, `-LDIR` | The same library and search-directory requests as native driver options. |
| `-z now`, `-z relro`, `-z noexecstack`, `-z text` (also joined, for example `-znow`) | Request existing writer policies: eager GOT binding, RELRO, a non-executable stack, and no relocations into read-only memory. |
| `--build-id`, `--eh-frame-hdr`, `--hash-style=gnu` | Request metadata already emitted by the writer. Other build-ID or hash-style modes are not implied. |

Entry selection does not omit the normal C runtime startup inputs or convert
a C function into a process-entry ABI. A custom entry must implement that ABI.

Unknown options and unimplemented behaviors are errors naming the requested
option. In particular, the direct C path does not implement `--defsym`, `-Map`,
version scripts, SONAME selection, whole-archive, or section garbage collection.
These requests are no longer silently dropped. Invalid commands do not create
an output or replace an existing destination. This is an explicit supported
subset, not arbitrary passthrough to a system linker.

Commands that do not link (`-c`, `-fsyntax-only`, `-E`, and checker-only modes)
consume linker forwarding and leave its payload unused. A missing operand to
`-Xlinker` remains a malformed driver command. Forwarded operands such as `-E`,
`-c`, `--version`, or a filename do not become driver actions or source inputs.

## Raw linker and writer

[`link-foreign`](foreign-driver.md) accepts raw linker arguments without the
`-Wl` / `-Xlinker` transport. It shares entry, runtime-path, dtags and fixed-output
policy parsing with the C driver; its existing additional input/output options
remain available. Runtime paths apply to dynamic PIE and shared-library output.
The raw linker refuses them for static executables and static PIE, which have
no dynamic loader to interpret the path.

`ExecutableDynamic.runpath` is a borrowed bounded slice retained until writing
finishes. A null pointer with zero length means no tag; a non-null pointer with
zero length requests an explicit empty path. Embedded NUL bytes, null pointers
with nonzero lengths, and overflowing ELF string sizes are refused before
publication. `old_dtags` selects the tag without changing the path bytes.

## Regression coverage

`test-checker-driver --executable ./build/compiler` covers both forwarding
forms, mixed and repeated options, actual ELF tags, scalar/vector byte identity,
entry execution, loading a temporary DSO through `$ORIGIN`, ordered library
retention, compile-only consumption, and named failures with unchanged output.
The raw foreign-driver suite inspects both `DT_RUNPATH` and `DT_RPATH`.

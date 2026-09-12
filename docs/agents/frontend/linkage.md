# Symbols, objects, linking, and initialization

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

Read the matching sections; [the frontend index](../frontend.md) lists these notes in their original order. Cross-references such as “above” and “below” follow that order.

- **Windows x64 frame records describe instruction-time RSP.**
  `object_windows_x64_unwind_layout` retains SET_FPREG only when no fixed
  allocation follows frame establishment; its displacement is the action's
  own value. Current fixed-stack producers establish RBP before allocation
  and keep RSP stable in the body. They therefore use PUSH/ALLOC/SAVE records
  without a declared frame register, with SAVE slots relative to final RSP.
  MIR emits the documented ADD/pops/RET fixed epilogue. The direct dynamic-stack
  producer allocates first, then establishes RBP, and retains its frame record
  and LEA epilogue. Do not drop that dynamic-frame information.
  `object_test.c` pins both action orders, real nonzero frame offsets, saved
  registers and small/large allocations. The existing native Windows x64
  stack-walk fixture also checks fixed-frame instruction boundaries using
  `RtlVirtualUnwind` in all four allocators, with and without debug information
  (GitHub #363); object parsing alone is not runtime-unwind evidence.
  Its metadata checker accepts both SAVE_NONVOL slot widths, rejects truncated
  saves, and keeps saved-register offsets separate from stack-allocation sizes.
- **Merged file-backed sections have zeroed background bytes.** `link_objects`
  initializes alignment gaps and each input's virtual tail before copying its
  data, so reused arenas produce the same bytes as fresh mappings. The zeroed
  arena allocation clears only the dirty overlap. BSS and thread-local BSS
  keep their virtual sizes without allocating serialized storage (GitHub #303).
- **A read-only object that carries a relocation is laid out with the writable
  data.** `const` is the frontend's answer and the object writer's read-only
  section is where it usually goes, but those bytes are written when the
  program is relocated, and in a shared object that write lands on a page the
  loader mapped read-only — a `DT_TEXTREL` the loader has to undo before it can
  process the relocation, and one musl's loader does not undo for the file it
  was itself started from. Clang answers the same question with a fourth data
  section, `.data.rel.ro`; this writer has three, so
  `codegen_global_is_read_only` in `codegen.c` folds "has a relocation" into
  the placement instead. Nothing in the C object model moves with it — writing
  through a `const` lvalue is undefined either way — and a static link, whose
  relocations are all resolved before the program runs, cannot tell the
  difference. `static const unsigned short *const ptable = table+128;` in
  musl's `__ctype_b_loc.c` is the shape, and `FILE *const stdout` is the one
  every program has.
- **`__attribute__((weak))` and `__attribute__((alias("target")))`** reach the
  object file, because musl publishes `malloc`, `free`, `errno` and most of
  its pthread surface as weak aliases of internal names. Weak is
  `IrSymbol.is_weak` and becomes `ObjectSymbol.weak`, which ELF writes as
  `STB_WEAK` and Mach-O as `N_WEAK_DEF`. COFF spells a weak definition as a
  selectany COMDAT, which needs a section per symbol while this model merges
  sections by kind, so a COFF object reads `weak` back but cannot write it and
  carries such a symbol as an ordinary external. That is the one gap of the
  three formats, and it predates aliases: `object.c`'s header states it. An
  alias is a pair in `IrModule.aliases` rather than a field on every symbol:
  it is a relation between two symbols rather than a property of one, and
  nearly every module has none. The object writer gives
  the alias its target's section, offset, size and kind and only its own
  binding, which is what Clang produces for the same source, and
  `ir_validate_canonical_module` requires the target to be a definition in
  that same module. The frontend keeps a static alias target alive -- an
  attribute names it, no identifier use does -- and diagnoses an alias whose
  target this translation unit does not define rather than emitting an
  import. Both attributes are read inside the declaration's
  `__attribute__((...))` list by `c_declaration_binding` in `c_gen.c`, never
  anywhere in its token range the way `section` and `asm` are matched: a
  marker attribute has no argument shape to recognise it by, and `weak` and
  `alias` are ordinary identifiers, so `int weak;` must stay a strong
  definition.
- **`__attribute__((constructor))` and `__attribute__((destructor))`** run a
  function before and after `main`. They are read out of the declaration's
  attribute list by the same `c_declaration_binding` walk as `weak` and
  `alias`, with the reserved `__constructor__`/`__destructor__` spellings
  beside the plain ones and the optional `constructor(101)` priority; the
  attribute may be written on any declaration of the function, so the flag is
  collected per entity the way `noreturn` is. Three consequences follow, and
  each was a separate hole before issue 771 closed them. A registered function
  is **reachable by definition** -- no expression names it -- so it is a root
  of the unused-static elimination in `c_gen.c`, without which a translation
  unit whose only function is a static constructor came out with an empty
  `.text`. The registration is a module-level list, `IrModule.initializers`,
  for the reason aliases are: it is a relation, not a property of a symbol,
  and nearly every module has none. And the two targets with no initializer
  array at all -- core Wasm, which starts one function of its own, and eBPF,
  which has no startup -- **diagnose** the attribute rather than dropping it.
- **`.init_array` and `.fini_array` are section kinds**,
  `OBJECT_SECTION_INIT_ARRAY` and `OBJECT_SECTION_FINI_ARRAY`, holding one
  pointer-wide slot per initializer with an `ABSOLUTE64` relocation against
  the function. ELF writes them `SHT_INIT_ARRAY`/`SHT_FINI_ARRAY`, Mach-O
  `__DATA,__mod_init_func`/`__mod_term_func` with the `S_MOD_*_FUNC_POINTERS`
  types, and COFF the `.CRT$XC*`/`.CRT$XT*` group MSVC's C runtime walks.
  **Priority orders the whole program**, and the priority travels beside the
  array rather than in it. A linker gets that order off the *section name* --
  `ld` places every `.init_array.NNNNN` ahead of the unsuffixed
  `.init_array`, ascending, and a PE linker concatenates a `$` group in
  lexicographic order of the whole name -- and this model has one section per
  kind, so a translation unit's whole array is one section, its entries sorted
  into that order by `object_from_canonical_codegen_module` (ascending
  priority, an attribute that named none last, equal priorities in declaration
  order) and each entry's priority recorded beside it in
  `ObjectFile.initializer_priorities`, one `u32` per slot. Three places carry
  that array, and they are the same fact from different sides.
  `object_split_initializer_priorities` splits it back into one section per
  priority group for the ELF and COFF writers, so an external linker orders
  two Buster objects exactly as it orders Clang's (issue #782).
  `object_read_elf64` and `object_read_coff` recover it from those section
  names -- the padded `.init_array.00101` written here and the unpadded
  `.init_array.101` Clang writes are both read -- and
  `object_reader_merge_initializer_arrays` merges the sections of a kind in
  that order rather than in section header order. And
  `link_initializer_arrays_order` sorts the *merged* array, stably, after
  `link_objects` has concatenated its inputs: without it a `constructor(101)`
  in the second object ran after an unprioritized constructor in the first,
  for Clang's objects as much as for this compiler's, which was issue #789.
  Ordered arrays only need a scan. Unordered arrays use four stable byte-wise
  radix passes over the `u32` priorities, reusing the inverse-permutation
  buffer as sorting scratch (GitHub #107).
  That sort moves each entry's relocation and any symbol defined at its slot
  with the entry, and it runs on the merged object rather than in
  `link_initializer_plan_build` so the Mach-O writer -- which keeps the
  initializer array for dyld to walk instead of reading a plan for it -- gets
  the same order the entry-stub writers do. An input that states no priorities
  (the Mach-O reader, the assembler's objects) has every entry unprioritized,
  which leaves it in link order.
- **A relocatable object keeps its arrays in all three formats; ELF and COFF
  can also state a priority, Mach-O cannot.** The COFF spelling is
  `.CRT$XCA00101` for a group, `.CRT$XCU` for what named none, and
  `.CRT$XTA00101`/`.CRT$XTX` for the terminators, which is what Clang emits
  and what `link.exe` and `lld-link` order lexicographically -- `A` sorts
  before `U`, so it says exactly what `ld`'s suffix says. Taking the
  platform's convention rather than a private `.init_array.NNNNN` suffix is
  what makes an object written here order correctly under a PE linker as well,
  and what lets `object_read_coff` recover the priorities out of an object
  Clang wrote; `object_coff_initializer_section_kind` reads the group back,
  and only that `A`-plus-five-digits spelling is a priority (`.CRT$XCU` and
  the runtime's marker sections are unprioritized, and a marker's null slot is
  dropped because no relocation fills it). The Mach-O reader classifies by the
  `S_MOD_INIT_FUNC_POINTERS`/`S_MOD_TERM_FUNC_POINTERS` section types, the way
  `SHT_INIT_ARRAY` names `.init_array` in ELF. Without those the arrays came
  back as `.data` and read-only data, and a program linked from an object ran
  **no constructor at all** on Windows and macOS -- the entries reached no
  initializer plan, and nothing diagnosed it. **Mach-O states no priority and
  is not going to**: `__mod_init_func` is 15 of a section name's 16 bytes, and
  the platform has no carrier elsewhere -- Clang emits one unsuffixed
  `__mod_init_func` per translation unit with the priorities sorted only
  inside it, so ld64 orders two objects by link order. A private carrier there
  would make `ide cc a.o b.o` disagree with `clang a.o b.o` on the same
  objects, so `ide cc a.o b.o` concatenates in link order on macOS, as the
  platform toolchain does (issue #795). That is why `driver_test`'s
  cross-translation-unit object route is gated off Apple hosts alone, while
  its source route -- where the priorities never touch a format -- runs on
  every host.
- **An image this linker produces calls its initializers from the entry
  stub.** There is no libc startup object in it -- the stub *is* the startup,
  which is why `link_x86_build_elf_entry_stub` exists at all -- so nothing
  would walk the arrays, and the linker already knows every entry's target at
  layout time. `link_initializer_plan_build` reads the two sections into a
  plan and hands back the object with them removed; each writer then emits one
  direct call per constructor before `main`, patched exactly like the call to
  `main` beside them. ELF passes argc, argv and envp to each constructor as
  GNU does; PE passes nothing, which is MSVC's `.CRT$XCU` contract and all
  that is available before the argv machinery runs. The **destructors** are
  not called where `main` came back: a program that reaches `exit` from inside
  `main` never comes back, and GNU runs a destructor either way, so the hosted
  stubs synthesize a **runner** past the trap that ends the stub -- one call
  per destructor and a return -- and register it with the C runtime before the
  constructors run. This leaves the runner behind every handler the program
  registered itself (issue 781). Hosted ELF startup first registers the
  finalizer supplied by the dynamic loader in RDX on x86-64 or X0 on AArch64,
  then the executable's runner: reverse exit order runs user handlers, the
  executable's destructors, and finally the startup-loaded shared libraries'
  destructors. Registering only the executable's runner silently omitted the
  last group (GitHub #227). A null loader finalizer is skipped. The registration
  is `__cxa_atexit` on ELF and `_crt_atexit` on PE, because glibc's `libc.so.6`
  and Windows' `ucrtbase.dll` both keep plain `atexit` in a static library
  this linker does not read; `link_elf_hosted_exit_symbol` appends the ELF
  import beside `exit` for every hosted ELF image, including one without its
  own destructor array. Both ELF dynamic writers must agree on that import
  list because AArch64 re-derives x86-64's numbering. The freestanding ELF shape
  keeps its destructors inline after `main`: it has no `exit` to call and no
  runtime to register with, and a
  `-nostdlib` program that reaches the raw exit syscall runs no handler
  either. Two writers synthesize no entry point of their own, and they answer
  differently. **Mach-O** needs none for its constructors: LC_MAIN hands
  `main` straight to dyld, which runs the main executable's
  `__DATA,__mod_init_func` before it enters `main`, so that writer keeps that
  array, gives it the section type dyld dispatches on, and lets the loader
  call it (issue 779) -- which is why the merged array has to be in GNU's
  order before any writer sees it (issue 789), rather than only in the plan
  the other writers read. **dyld does not run a main executable's
  `__mod_term_func`**, which was measured rather than assumed (issue 798): on
  macOS 26.3.2 a pointer placed by hand in a `__mod_term_func` of an image
  *ld64* wrote never ran while the `__mod_init_func` slot beside it did, and
  Clang emits no terminator array on Darwin at all -- it registers every
  `__attribute__((destructor))` with `__cxa_atexit` from a
  `__GLOBAL_init_65535` initializer it synthesizes. So the Mach-O writer emits
  no `__mod_term_func` either. `link_mach_initializer_prepare` takes that
  array off the object -- the one half of this writer that does read a plan,
  built from the same merged array issue 789 ordered -- the writer synthesizes
  a **runner** over its entries past the import stubs, and a **registrar**
  prepended as the *first* `__mod_init_func` slot hands the runner to `atexit`
  before any constructor of the program runs. Registering first is what leaves
  the walk behind every handler the program registered itself, so Apple gets
  the same order the two entry stubs produce and
  `tests/basic_c_destructor_exit.c` runs there; Clang's own Darwin shape does
  not have that property, because it registers each destructor as its
  initializer is reached and a handler registered by a constructor therefore
  runs *after* the destructors. The prepended slot carries no relocation --
  its value is an address this writer chooses -- so it is written directly and
  named directly in the rebase stream, and the registrar reaches `atexit`
  through the import stub, appending that import when the program never named
  it. **UEFI** has no such third party -- a
  firmware image has no C runtime, and its entry is the firmware's call with
  the image handle and the system table -- so it still **refuses** a program
  with initializers, naming the first one, rather than placing an array
  nothing will call; `link_initializer_plan_empty` is that refusal. The
  objects it produces are correct and link through the system linker.
- **Every blob in a Mach-O image's `__LINKEDIT` starts on an eight-byte
  boundary.** dyld runs a layout check over the whole segment before it reads
  any of it, and a blob that starts mid-word makes it refuse the image with
  `mis-aligned LINKEDIT content '<name>'` -- which `dyld_info` prints in place
  of the fixup table while dyld itself still loads and runs the image, so
  nothing but a tool, a signing pass or a notarisation pass ever notices
  (issue 800). The rebase stream opens the segment at a page boundary, but its
  length is one uleb128 run per rebased address, so `bind_offset`,
  `symbol_table_offset` and the code signature are each aligned rather than
  laid straight after the blob before them. Anything added to that chain --
  export info, function starts, data in code -- has to be aligned the same
  way; the gap bytes are already zero because the image buffer is memset
  before anything is written, and every blob carries its own size, so the
  padding is read by nothing. `link_test.c` walks LC_DYLD_INFO_ONLY, LC_SYMTAB
  and LC_CODE_SIGNATURE of five images and requires one fixture to keep ending
  its rebase stream mid-word, without which the alignment would hold by
  accident; on macOS the same image goes through `dyld_info -fixups`.
- **An undefined weak symbol resolves to address zero**, which is what a
  program asks for by declaring one: musl's startup takes the address of a
  weak hidden `_DYNAMIC` and reads zero to learn it is static. Which party
  answers is the question of whether the reference can be preempted, and the
  ELF writers decide it with `link_elf_symbol_resolves_to_zero` in `link.c`:
  a hidden reference, and any reference in a static image, is relocated
  against zero here, while a default-visibility one in a dynamic image stays
  an import entered in `.dynsym` as `STB_WEAK`, so the loader binds it when a
  shared library defines it and leaves it zero rather than refusing the image
  when none does. `link_elf_symbol_needs_dynamic_import` is the same question
  asked by the three places that choose between the static and dynamic
  writers and by the two that number imports; they must not disagree, because
  the AArch64 dynamic writer re-derives the x86-64 writer's import numbering.
  Merging inputs follows ELF: a symbol only references name is weak while
  every one of them is, and one hidden occurrence makes it hidden.
  A default-visibility reference is promoted to an import **only when a
  library the image names is known to define the name** with a default
  version, which is the same question `link_elf_symbol_version` answers for
  the version to record; asking the loader for a name nothing has is how such
  a reference came back as its own PLT thunk or copy slot. What knows is
  `compiler_driver_elf_dynamic_symbols`, which records every defined global
  and weak entry of every shared library on every hosted ELF link, functions
  included, and sets `exports_known`: `versioned_symbols` is the ELF export
  list, `exported_symbols` stays PE's. Absence is evidence only when **every**
  library was read — `link_elf_exports_complete` — because a library the
  driver could not open exports whatever it happens to export; a link missing
  one of them keeps the import it made before, which is also why a target
  whose libraries are never read, Android today, is unchanged.
- **A C library keeps some of its own names out of its shared object**, and
  this linker imports from the shared object alone, so it has to supply the
  rest itself. glibc puts `atexit` and `at_quick_exit` in libc_nonshared.a as
  one call apiece to `__cxa_atexit` and `__cxa_at_quick_exit`; UCRT puts the
  same two names in its import library as one call apiece to `_crt_atexit` and
  `_crt_at_quick_exit`. `link_elf_libc_runtime_object` and
  `link_windows_libc_runtime_object` build those stubs — weak, so a program's
  own definition wins, and a bare tail branch, so the C ABI hands the
  arguments through — over the shared `link_forwarding_runtime_object`. The
  driver adds either one **the way it selects an archive member**
  (`compiler_driver_archive_member_needed`): only when something references a
  stub and nothing defines it. That selection is the contract, not a
  refinement of it. The stub carries an undefined `__cxa_`/`_crt_` reference,
  so adding it unconditionally would put that import in every executable, and
  on a host with no readable `ucrtbase.dll` it would fail every Windows link
  outright. `link_windows_runtime_object` is the counterexample that has to
  stay separate: `_fltused` is a four-byte marker with no reference of its
  own, so it is added to every hosted Windows link unconditionally.
  `_onexit` is deliberately absent — it answers with the handler rather than
  with a status, so it cannot be a tail branch, and a stub that called and
  then chose would need Windows unwind data of its own.

- Ordinary AArch64 ELF ADRP/ADD address pairs use distinct `ELF_PAGE21` and
  `ELF_ADD_LO12` object kinds (AAELF64 relocations 275/277). The reader keeps
  RELA's explicit addend and clears the encoded immediate. REL ADRP's initial
  addend is its **unscaled signed imm21**, unlike the executed displacement
  or the Mach-O contract; REL ADD uses the unshifted unsigned imm12. Reject
  misaligned sites, wrong instruction classes and shifted ADD forms.
  `object_aarch64_elf_page_relocate` shares checked address arithmetic with
  in-memory and native ELF linking, and the generated A64 ADD plan owns the
  immediate bits. The dynamic writer omits these sites from x86 layout
  staging and patches them after layout; imported data uses its dynamic
  symbol's copy-slot address, including aliases. Untyped exported AArch64
  ELF text labels can serve as assembly entry points; explicit object types
  remain data. Mach-O, PE and TLS relocation contracts remain separate.

  Independent Clang/QEMU fixtures for GitHub #355:

  ```sh
  clang -target aarch64-unknown-linux -c tests/basic_aarch64_elf_page.s -o /tmp/page.o
  build/Release/ide cc -target aarch64-unknown-linux /tmp/page.o -o /tmp/page
  qemu-aarch64 /tmp/page
  clang -target aarch64-unknown-linux -O2 -c tests/basic_c_aarch64_elf_page_caller.c -o /tmp/page-caller.o
  build/Release/ide cc -target aarch64-unknown-linux -fverify-codegen -fregister-allocator=quality tests/basic_c_aarch64_elf_page_callee.c /tmp/page-caller.o -o /tmp/page-caller
  qemu-aarch64 /tmp/page-caller
  ```

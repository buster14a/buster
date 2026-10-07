# Symbols, objects, linking, and initialization

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

Read the matching sections; [the frontend index](../frontend.md) lists these notes in their original order. Cross-references such as “above” and “below” follow that order.

- **TLS classification consumes canonical keyword spellings.** Preprocessing
  rewrites C23/GNU23 `thread_local` to `_Thread_local`, including preprocessed
  input. A remaining raw `thread_local` is an ordinary identifier; its use as
  an object name, typedef, enumerator or initializer member cannot confer TLS.
  File-scope semantic entities, canonical symbols and globals recognize only
  `_Thread_local` and `__thread` for thread storage. Static initializers still
  reject addresses of genuine TLS objects. `c_test_file_tls_dialect` checks
  these facts and pointer initializer identity through both frontend forms;
  `c_test_file_tls_dialect_runtime` checks ordinary object placement, address
  relocations and execution under every native allocator in C17/GNU17.

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
  `RtlVirtualUnwind` in FAST and QUALITY, with and without debug information
  (GitHub #363); object parsing alone is not runtime-unwind evidence.
  Its metadata checker accepts both SAVE_NONVOL slot widths, rejects truncated
  saves, and keeps saved-register offsets separate from stack-allocation sizes.
- **Executable TLS definitions participate in dynamic lookup.** Fixed-address
  Linux x86-64/AArch64 executables and x86-64 PIEs export public `.tdata` and
  `.tbss` definitions requested by a linked DSO, or all public TLS definitions
  under `-rdynamic`. Their dynamic symbols carry `STT_TLS`, their loaded
  section index and size, and an offset in the module's TLS block: initialized
  data first, then zero-fill at its required alignment. A runtime image address
  cannot serve as that offset. Fixed images reuse `link_elf_thread_local_offset`
  and the packed loaded-section map shared with the section table. The TLS
  block starts at the maximum `.tdata`/`.tbss` alignment so that each section's
  address agrees with its block-relative symbol offsets. Fixed writers align
  both TLS class starts by final virtual address, including an alignment larger
  than the image base; file-offset alignment alone leaves the base's residue. Local-exec relocations
  round the whole block to that same alignment before computing x86-64 TP
  offsets. AArch64 places its block after the 16-byte TCB rounded to this
  alignment before adding the module offset and relocation addend.
  The PIE emitter uses the same map, including copy-created `.bss` and omitted
  empty sections. Hidden definitions remain private, undefined hidden references
  fail, and TLS/non-TLS object identities retain their mismatch diagnostic.
  `compiler_driver_tls_export_tests` uses a configured host-built DSO to read
  and modify both initialized and zero-fill executable TLS. Source/object,
  fixed/PIE, and demand/`-rdynamic` routes have host-linker/runtime controls
  and independent raw ELF checks for type, binding, visibility, section index,
  size, block-relative value, initialized bytes and `PT_TLS` bounds. Sole-class
  `.tdata`/`.tbss` controls check `p_align`, `sh_addralign`, initialized-template
  load coverage and native DSO/local-exec pointer identity at 32 bytes and
  8 MiB; weak definitions preserve binding 2. An unused
  public TLS symbol distinguishes demand export from `-rdynamic`; a hidden
  definition stays absent in both modes. Linux AArch64 covers its supported
  fixed-address routes; other image writers retain their existing TLS scope.
- **AArch64 ELF variant procedure-call metadata is refused explicitly.**
  `object_read_elf64` refuses every non-null, non-FILE symbol carrying
  `STO_AARCH64_VARIANT_PCS` (st_other bit 0x80), naming the symbol and table
  index. The neutral object model cannot preserve this marking or the
  intermediary register/state guarantees required by
  [AAELF64's symbol-table contract](https://github.com/ARM-software/abi-aa/blob/2025Q4/aaelf64/aaelf64.rst#symbol-table).
  The check precedes reserved-index and unallocated-section skipping, so those
  paths cannot silently erase the ABI requirement. An invalid name remains a
  malformed-input error. Formatting is bounded by the remaining arena capacity;
  when that space is unavailable, the unsupported-target error remains and no
  diagnostic bytes are allocated. Ordinary AArch64 visibility, ignored FILE/null records
  and other architectures retain their existing behavior; this is a refusal
  boundary, with variant-PCS execution support still open under GitHub #1243.
  `object_test_elf_variant_pcs` uses original raw ELF records to cover defined
  and undefined FUNC/NOTYPE entries, local/global/weak bindings, every visibility,
  reserved/discarded sections, unnamed/malformed names and x86-64 controls.
- **Program-symbol identity crosses the object boundary.**
  `object_from_canonical_codegen_module` resolves a relocation's `IrSymbolId`
  through `entry_by_symbol`. Entries map to their own index; globals and
  aliases are seeded with the definition their link name resolves to (the
  first definition carrying it), which the name-index build records as it
  places each definition, so their references hash no name. Externs keep the
  name path, whose first lookup also claims the insertion slot it ended on.
  DWARF `DW_OP_addr` and CodeView `S_GDATA32` relocations carry the
  variable's `IrSymbolId` and resolve through the same map when it names a
  definition; the name path remains for everything else. Non-optimized
  builds cross-check every such answer against the name lookup, and
  `compiler_driver_test_debug_global_relocations` pins the first-definition
  contract with block-scope statics, an asm label, a completed tentative
  definition and a block-scope extern on ELF x86-64, ELF AArch64 and COFF.
- **Merged file-backed sections have zeroed background bytes.** `link_objects`
  initializes alignment gaps and each input's virtual tail before copying its
  data, so reused arenas produce the same bytes as fresh mappings. The zeroed
  arena allocation clears only the dirty overlap. BSS and thread-local BSS
  keep their virtual sizes without allocating serialized storage (GitHub #303).
- **AMD64 COFF TLS-index REL32 fields use the ordinary inline addend convention.**
  The reader normalizes the signed inline displacement B to canonical A=B-4,
  including references named `__tls_index`. The writer restores B=A+4 for
  `OBJECT_RELOCATION_X86_64_PE_TLS_INDEX_PC32`, just as for ordinary PC32.
  The registered object regression constructs raw COFF bytes independently,
  checks both signed boundaries and a near-name ordinary-symbol control,
  and inspects serialized fields across repeated read/write cycles. This
  preserves addends within the current TLS model; platform TLS symbol spelling,
  section conventions and runtime interoperability remain separate contracts
  tracked by GitHub #1323.
- **COFF section alignment is a linker placement contract.** A nonzero
  `ObjectSection.alignment` is preserved in `IMAGE_SCN_ALIGN_*`; zero resolves
  through `object_section_default_alignment` for that kind. COFF represents
  powers of two from 1 through 8192 bytes, so the writer rejects a stronger
  requirement instead of clamping it. Initializer and unwind sections keep
  their 8-byte defaults, Windows pdata/xdata keep 4-byte defaults, and a
  stronger valid request still wins. `PointerToRawData` remains four-byte
  aligned because its file offset is separate from the linker's in-memory
  placement. The registered object test parses emitted section-header bytes
  directly. On Windows x64, the driver regression puts a prior data contribution
  before an aligned C global, links both into a DLL, and checks the exported
  symbol's RVA; it also checks that a rejected alignment does not replace output.
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
- **GNU attribute feature queries follow semantic support, not parser tolerance.**
  `c_conditional_attribute_supported` shares spelling sets with the binding
  walk and the `noreturn` reader. Plain/reserved `noreturn` answers true;
  native aliases and lifecycle attributes answer true where their consumers
  preserve them. `weak` remains false on Windows/UEFI because COFF cannot
  serialize weak definitions. Core Wasm/eBPF do not advertise these native
  binding families, and UEFI does not advertise lifecycle attributes because
  its image linker refuses registrations. `_Noreturn` is a specifier, not a
  GNU query spelling. Keep `__has_c_attribute` separate; this change does not
  expand its namespace/version contract. `c_test_gnu_attribute_queries` pins
  the target/spelling matrix and guarded noreturn control flow;
  `compiler_driver_test_attribute_queries` reads emitted ELF/Mach-O/COFF
  symbols and initializer arrays, then runs the guarded fixture through
  native source and object links in FAST and QUALITY (GitHub #666).
- **ELF unwind records name the producing object's instruction bytes.**
  `object_append_dwarf_cfi` uses local text-section symbols plus function
  offsets on x86-64 and AArch64, with or without PIC and debug information.
  Named text sections get their own local anchors. A weak default's FDE
  therefore stays with its own code when a strong definition overrides it.
  `link_elf_eh_frame_header_write` refuses duplicate initial locations with
  `LINK_ERROR_RELOCATION`, including legacy function-symbol FDEs that resolve
  to the same winner; no unwinder search order chooses between their rules.
  Registered driver tests inspect serialized relocations in FAST and QUALITY
  and exercise host-compiled overrides under GNU ld, available LLD and
  Buster's linker in both input orders on native Linux x86-64 and AArch64.
  Link tests cover duplicate refusal and distinct local-anchor controls for
  both architectures on every test host.
- **`__attribute__((weak))` and `__attribute__((alias("target")))`** reach the
  object file, because musl publishes `malloc`, `free`, `errno` and most of
  its pthread surface as weak aliases of internal names. Weak is
  `IrSymbol.is_weak` and becomes `ObjectSymbol.weak`, which ELF writes as
  `STB_WEAK` and Mach-O as `N_WEAK_DEF`. COFF spells a weak definition as a
  selectany COMDAT, which needs a section per symbol while this model merges
  sections by kind, so the writer still cannot synthesize it. The reader does
  preserve each source contribution's key, selection, associated parent, byte
  range and relocation range. `link_objects` resolves those groups first:
  ANY keeps one, SAME_SIZE and EXACT_MATCH validate their contracts, LARGEST
  chooses by size independently of input order, and ASSOCIATIVE follows its
  parent. Associative chains resolve after every non-associative winner is final,
  through `link_comdat_associations_resolve`: an object-local parent walk marks
  its current path, rejects cycles, and publishes the terminal KEEP/DISCARD state
  along that path. A shared ancestor is resolved once. The walk uses the existing
  state bytes and follows the immutable parent links again to finish a path;
  it needs no recursion, auxiliary allocation or persistent cache. Its work is
  O(C + A), where C is the COMDAT population and A the associative population,
  independent of record order and association depth. This bound describes only
  association resolution, not key hashing, exact-match comparison or the whole
  linker. The registered `link_test_comdat_association_scaling` generates ordinary
  and reverse/permuted parent graphs, compares an independent pass-scan control,
  checks real group/symbol/relocation survival and rejects malformed associations.
  Its deterministic work bounds run in ordinary tests without timing thresholds.
  The [PE/COFF contract](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#comdat-sections-object-only)
  permits associative parents that are themselves associative, requires a final
  non-associative root and forbids cycles (GitHub #2232).
  Only then does the surviving definition enter ordinary weak/strong
  arbitration. A COFF object therefore reads `weak` back but cannot write it
  and carries a compiler-produced weak symbol as an ordinary external. That is the one gap of the
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
- **LLVM weak linkage distinguishes definitions from imports.**
  `llvm_bc_linkage` emits weak definitions as wire linkage 16 (`weak`) and
  unresolved declarations as 7 (`extern_weak`), for both data and functions.
  Ordinary external symbols keep linkage 0; internal definitions keep 3 and
  default visibility. Hidden external symbols retain their separate visibility
  operand. These encodings follow LLVM 23.1.2
  [getEncodedLinkage](https://github.com/llvm/llvm-project/blob/85ac560262434c9ccfc0c183ec22d4138ed647fb/llvm/lib/Bitcode/Writer/BitcodeWriter.cpp#L1333-L1360),
  whose legacy weak value 1 implies old COMDAT behavior and is not emitted.
  Registered `llvm_bitcode_test_weak_records` checks both ELF target triples,
  deterministic bytes, definition/import and visibility controls. On Linux
  x86-64/AArch64, `llvm_bitcode_test_weak_consumers` requires independent Clang
  O0/O2 consumers and llvm-readelf/readelf: missing and supplied optional data
  and functions, direct/indirect guarded use, strong overrides, repeated weak
  definitions, ordinary/internal controls, and required-import/duplicate-strong
  negative controls. LLVM's inspected source is
  [Apache-2.0 WITH LLVM-exception](https://github.com/llvm/llvm-project/blob/85ac560262434c9ccfc0c183ec22d4138ed647fb/llvm/LICENSE.TXT);
  no LLVM implementation is copied into this serializer.
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
  and nearly every module has none. And the targets with no initializer
  array at all -- wasm32 and wasm64, whose direct output is a finished module
  that starts one function of its own, and eBPF, which has no startup --
  **diagnose** the attribute rather than dropping it. The message names the
  actual target (`wasm32`, `wasm64` or `eBPF`; issue 2679, covered by
  `c_test_gnu_attribute_queries`). Clang accepts the attribute on Wasm by
  recording `InitFunctions` (subsection 6, priority then symbol index) in the
  relocatable object's `linking` custom section for `wasm-ld` to turn into
  `__wasm_call_ctors`; Buster refuses instead because `wasm.c` writes no
  relocatable object, so there is no `linking` section, symbol table or
  `wasm-ld` step to carry the entries. Destructors stay refused: the object
  format has no finalizer list, and Buster has no atexit-registration lowering.
- **`__attribute__((section("name")))` places a definition in a section of
  its own name on ELF and is refused elsewhere** (issue #1276). The frontend
  records it in `IrSymbol.section_name`: from the definition, else from any
  other declaration of the entity (`c_entity_section_name`), and for
  block-scope statics too. COFF and Mach-O targets, and thread-local
  variables, are diagnosed at the declaration
  (`c_section_attribute_unsupported_output`). Codegen lays each named group
  out after its image's ordinary contents (`codegen_section_group`); a module
  that names none keeps its layout byte for byte.
  `object_from_canonical_codegen_module` splits those tails into sections
  past `OBJECT_SECTION_COUNT` (`object_named_section_plan`). A name that is
  `.init_array`, `.fini_array` or `.preinit_array`, optionally with a
  `.NNNNN` suffix, becomes an initializer-array section, and the ELF writer
  types it `SHT_INIT_ARRAY`, `SHT_FINI_ARRAY` or `SHT_PREINIT_ARRAY`.
  Functions moved out of `.text` leave the one-range DWARF unit.
  `object_read_elf64` keeps every C-identifier-named input section as its
  own section. `link_objects` places each such set contiguously after the
  ordinary sections of its kind, and defines the `__start_NAME`/`__stop_NAME`
  references GNU `ld` would (`link_section_sets_define`). The registered
  `compiler_driver_test_section_attribute` keeps the host-produced set bounds
  weak on both Linux architectures and executes present and absent bounds
  through Buster and host links. AArch64 Clang's weak references exercise ELF
  GOT 311/312 instead of substituting strong bounds (GitHub #1719).
  The LLVM bitcode writer records the names. Wasm names data segments and
  accepts function section attributes without effect (GitHub #1717).
  The merge collects each named contribution once and heapsorts name/original
  ordinal records (`link_section_set_members_sort`), then forms one range per
  name. Placement follows the first input appearance of each name and input
  order within it; mixed ordinary data kinds become DATA, while code/data
  mixtures retain the earliest conflicting input's refusal. Bound references
  binary-search the same name index; a program's own definition is unchanged.
  For N named contributions and B bound queries, indexing/lookup has
  O(N log N + B log N) name comparisons plus linear collection/placement,
  independent of hash collisions. Name-byte comparison cost, ordinary symbol
  resolution and output copying remain separate. Index records use scoped
  scratch and are released on success/refusal; inputs without extra sections
  keep their scratch-free route. `link_test_section_set_scaling` checks an independent
  numeric placement/bounds oracle, reused dirty output, immutable input records
  and actual production work counters against a bounded legacy negative
  control. `link_test_section_set_edges` covers pure code/zero-fill, refusal
  identity, malformed alignment/overflow and absent/invalid-name bounds.
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
- **Initializer collection does not search the relocation list once per slot.**
  `link_initializer_entries_collect` uses the caller's output reservation as a
  transient slot table, then compacts it in place. Sparse arrays instead sort a
  bounded prefix of matching relocation indices in that same storage, so holes
  do not fault in the full reservation. At most E/32 matches take this path;
  32-bit identities bound heap height to 32. For E complete slots and R
  relocations, all scans, construction, sorting, compaction and reversal are
  therefore O(E + R), with no added allocation. The dense transition can scan
  relocations twice. Zero slots or zero relocations do no collection work, and
  one slot keeps the first-match early exit. Both paths keep the first aligned
  `ABSOLUTE64` per slot, omit holes, clear transient keys, and reverse fini
  results. Input metadata is immutable; priorities were ordered at merge time.
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
  library was read — `LinkElfIndex.exports_complete` — because a library the
  driver could not open exports whatever it happens to export; a link missing
  one of them keeps the import it made before, which is also why a target
  whose libraries are never read, Android today, is unchanged.
- **Linux x86-64 fixed-address imported function pointers preserve provider
  identity** (#1275). GOT address references use separate loader-filled
  `GLOB_DAT` slots; the PLT's `.got.plt` slots (eagerly bound, `DF_BIND_NOW`) remain call-only. Pointer-wide
  literals use `R_X86_64_64`, preserving the signed addend, including weak
  and protected providers. Read-only literal sites publish `DT_TEXTREL` so
  the loader can write them during relocation. A direct `PC32`, `PC64`, or
  32-bit absolute address instead needs the psABI's canonical PLT value:
  undefined `STT_FUNC`, nonzero `st_value`, unchanged jump-slot binding.
  Only complete provider metadata proving a strong, default-visible
  `STT_FUNC` permits that representation. Protected, weak, IFUNC or unknown
  direct addresses are refused by name; use PIC GOT references or pointer-wide
  literals. An absent weak address stays zero. The ELF reader preserves
  explicit `PLT32` references rather than collapsing them to `PC32`; arbitrary
  preceding instruction bytes cannot prove a call. PIE/shared imported or
  preemptible-function direct addresses are refused, while GOT/literal
  references remain loader-bound and same-image direct calls retain the
  documented local binding policy. AArch64 and Android staging explicitly
  retain their prior address policy and remain #1275 acceptance work.
  `compiler_driver_test_imported_function_addresses` uses an independent host
  PIC provider and preload library, typed pointer getters, static and constant
  pointer tables, protected call-only functions, weak-present/absent functions
  and imported data. Both frontend forms with FAST and QUALITY execute PIC
  and non-PIC forms under lazy/eager binding and default/preloaded definitions.
  In the default Linux x86-64 code model, undefined default-visible weak
  function addresses use GOT references in native MIR emission; direct calls
  retain PLT32. The existing weak/null runtime fixture
  keeps its default flags and assertions. Foreign or handwritten direct weak
  address relocations still receive the named representability refusal.
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
  or the Mach-O contract; REL ADD sign-extends its unshifted imm12 (2047,
  2048 and 4095 become 2047, -2048 and -1). REL-to-RELA rewrites preserve
  that signed canonical addend even when low12 truncation leaves linked
  instruction bytes unchanged. Reject
  misaligned sites, wrong instruction classes and shifted ADD forms.
  `object_aarch64_elf_page_relocate` shares checked address arithmetic with
  in-memory and native ELF linking, and the generated A64 ADD plan owns the
  immediate bits. The dynamic writer omits these sites from x86 layout
  staging and patches them after layout; imported data uses its dynamic
  symbol's copy-slot address, including aliases. Untyped exported AArch64
  ELF text labels can serve as assembly entry points; explicit object types
  remain data. `ELF_ADR_PREL_LO21` (relocation 274, an assembly `adr` to a
  target the unit could not fold, #2706) rides the same family: its REL addend
  is the unscaled signed imm21 byte displacement and the relocated value is
  S + A - P with no page truncation. Mach-O, PE and TLS relocation contracts
  remain separate.

- AArch64 ELF `ELF_GOT_PAGE21`/`ELF_GOT_LD64_LO12` (types 311/312)
  use `GDAT(S)` and require zero addends under AAELF64. The importer rejects
  nonzero REL instruction fields and nonzero RELA addends; canonical ELF
  writing, assembly printing and the shared image patcher enforce the same
  rule. A valid pair relaxes to ADRP/ADD of the symbol, including an imported
  data symbol's copy slot; an undefined weak zero target becomes MOVZ/ADD
  zero. Direct ADRP/ADD and scaled memory relocations retain their distinct
  signed-addend rules. See [AAELF64 addends and relocation definitions](https://github.com/ARM-software/abi-aa/blob/a5e86d3fec7342719f3bd1f939ec1b8ac6438c4f/aaelf64/aaelf64.rst).

- Direct AArch64 ELF unsigned-immediate memory references use distinct
  `ELF_LDST8_LO12`, `ELF_LDST16_LO12`, `ELF_LDST32_LO12`, `ELF_LDST64_LO12`
  and `ELF_LDST128_LO12` kinds (AAELF64 types 278/284/285/286/299). The
  relocation's access size must match the instruction's encoding, including
  sign-extending scalar, SIMD and Q-register forms; scale-three PRFM is
  accepted too. Reserved, unscaled and register-offset forms fail. The reader
  clears imm12, keeps RELA's explicit signed addend and sign-extends REL's
  imm12 before access-size scaling. The shared ELF page helper applies only
  bits `[11:scale]` of `S+A`, checks arithmetic and final-address alignment,
  and preserves operation/register bits in object, in-memory and static or
  dynamic native links. Dynamic imported data uses its copy slot, including
  aliases. These direct memory references remain separate from GOT relaxation,
  TLS, Mach-O and PE contracts. See
  [AAELF64 addends and relocation definitions](https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst).

- Ordinary Windows ARM64 address pairs use the PE-specific
  `PAGEBASE_REL21`/`PAGEOFFSET_12A` object kinds (COFF types 4/6), never the
  loader-owned TLS kinds. COFF's ADRP field is a signed, unscaled imm21 byte
  addend and its ADD field is an unsigned imm12 byte addend; the reader validates
  and clears both before publishing a relocation, and the writer restores
  them without replacing opcode or register bits. This implementation admits
  the exact zero-shift ADD form emitted by VC14.44 and rejects shifted ADD,
  SUB, ADDS and unrelated instructions. Type 7 ordinarily accepts the scaled
  unsigned-immediate load/store and PRFM encodings, preserving the operation
  and registers while clearing or restoring the immediate. Reserved,
  unscaled and register-offset forms fail closed. The exact `__tls_index`
  symbol remains a distinct DATA-symbol contract restricted to a 32-bit
  unsigned-immediate LDR; the writer binds index-pair relocations to that
  loader symbol. TLS section offsets use shifted ADD type 10
  (`SECREL_HIGH12A`) followed by unshifted ADD type 9 (`SECREL_LOW12A`).
  Native MIR producers emit both halves, preserving offset bits 12..23
  beyond 4 KiB. COFF's inline imm12 addend is an unscaled byte count even in
  the shifted ADD; the PE linker adds it before splitting the final template
  offset. Offsets beyond 24 bits, malformed ADD forms and arithmetic overflow
  fail before executable publication. Registered codegen, original raw-COFF
  and final PE byte tests cover both frontend forms, all allocators, carries,
  initialized/zero-fill placement and output retention. This fixes #1323 W2;
  platform TLS-index spelling/section interoperability and Mach-O descriptors
  remain separate work. Type 15 is `BRANCH19` and is not treated as TLS. ARM64
  CodeView uses `SECREL` type 8 and the two-byte `SECTION` type 13, retaining
  checked inline addends. The PE linker applies all of these only after final
  layout and returns no executable bytes on a relocation failure.

  Independent Clang/QEMU fixtures for GitHub #355:

  ```sh
  clang -target aarch64-unknown-linux -c tests/basic_aarch64_elf_page.s -o /tmp/page.o
  build/Release/ide cc -target aarch64-unknown-linux /tmp/page.o -o /tmp/page
  qemu-aarch64 /tmp/page
  clang -target aarch64-unknown-linux -O2 -c tests/basic_c_aarch64_elf_page_caller.c -o /tmp/page-caller.o
  build/Release/ide cc -target aarch64-unknown-linux -fverify-codegen -fregister-allocator=quality tests/basic_c_aarch64_elf_page_callee.c /tmp/page-caller.o -o /tmp/page-caller
  qemu-aarch64 /tmp/page-caller
  ```

// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Binding: the C runtime (runtime.c) joined to a program's link, and each name of the link bound
// (docs/object-writer-and-linker.md, "Step 5's phases, data-parallel"), for the dynamic PIE that step 5 writes.
//
// The runtime's objects join the link in the order GCC's driver links them, so that the classes whose atoms keep their
// id order (.init's fragments, .eh_frame and the terminator that crtendS.o ends it with, the init and fini arrays) come
// out as lld lays them out: bind_begin appends Scrt1.o, crti.o and crtbeginS.o, the caller then adds the program's
// atoms, and bind_end appends the members of libgcc.a and libc_nonshared.a that the link needs, then crtendS.o and
// crtn.o. The program's global symbols should all exist before bind_begin; one added later is matched in bind_end. An
// object joins as a block: its names, atoms, symbols, fixups and wide addends, with their ids moved by the link's
// counts, its units numbered from the caller's unit_base on, and its atoms' bytes copied into the caller's arena, since
// an executable patches atoms where they are.
//
// A global or weak symbol of the runtime takes the link's symbol of its name if there is one, so that the link keeps
// one symbol per global name: bind_begin matches the link's global symbols against the runtime's global names
// (RuntimeNames) once, and each of the runtime's that joins without a match becomes its name's symbol. Two of the
// link's global symbols with one of those names are an error. Two definitions of a name resolve as GNU ld and lld
// resolve them: a strong one takes a weak one's place, of two weak ones the first in link order stays (Scrt1.o's,
// crti.o's and crtbeginS.o's come before the program's, the others' after it), and two strong ones are an error. The
// program's definitions are its symbols of source PROGRAM and the absolute ones the link had before bind_begin, whose
// source says only that they are absolute (Bind.program_symbols). A
// definition of the runtime's takes the link's symbol with its atom, value, size, type, binding and source, whether the
// symbol only referred to the name or had a weak definition; one that takes the program's definition's place waits in
// Bind.overrides until bind_end, since the program defines its symbols as its atoms join, after bind_begin. Two
// references keep the stronger binding and the type either has. Whichever stays takes the most constraining visibility
// of every reference to the name and definition of it, as the gABI gives the resolving symbol.
//
// bind_end binds each name that nothing in the link defines, in the order GNU ld resolves the driver's link line:
//   - to the linker, for a name it defines (_GLOBAL_OFFSET_TABLE_): source LINKER;
//   - to a crt object after the program (__TMC_END__), which defines it when it joins;
//   - to libc.so.6's export, or else to the loader's (ld-linux-x86-64.so.2, which libc.so's script names as needed:
//     __tls_get_addr, _r_debug, __libc_stack_end): an import, with source RUNTIME_SHARED, the export's type, and its
//     version in the linker's column (the loader's indices follow libc.so.6's, runtime.c), listed in Bind.imports; but
//     not for a hidden, internal or protected reference, which needs a definition within the executable, as GNU ld and
//     lld bind one. The loader is needed only for a strong reference to one of its names, as GNU ld and lld take a
//     library AS_NEEDED: without one, the weak references to its names are left to nothing, unversioned
//     (bind_keep_loader);
//   - before libc.so.6, to an export of a shared library the link names with -l (Bind.libraries, which the caller sets
//     between bind_begin and bind_end): an import of that library's, as GCC's driver puts -l libraries before libc.so
//     on the line. A link needs such a library only for a strong reference that binds to it, as GNU ld takes one
//     --as-needed: the program's, or a needed library's that its own DT_NEEDED doesn't cover and no definition the
//     executable exports satisfies (bind_needed_libraries, bind_keep_libraries); and it exports its definitions of
//     names a needed library defines or refers to (bind_library_exports);
//   - for a strong reference, to a member of libgcc.a (__udivti3, which a compiler's code of a 128-bit integer's
//     division calls) or of libc_nonshared.a (atexit), which joins the link. The driver names libgcc.a before libc.so,
//     and it defines no name libc.so.6 exports (runtime.c), so that the export's binding comes first all the same;
//   - to libgcc_s.so.1's export (the unwinder: _Unwind_Resume, _Unwind_Backtrace, __gcc_personality_v0), which the
//     driver names as needed after libgcc.a, as libc.so.6's is bound: after the members, since libgcc.a, before it on
//     the line, defines most of its other names (runtime_export_after_members). Like the loader it is needed only for a
//     strong reference to one of its names (bind_keep_as_needed), the program's or a needed named library's that the
//     library's own DT_NEEDED doesn't cover (Bind.runtime_needed), and like libc.so.6 it defines none of the names the
//     others export (runtime.c), so that its place among them doesn't matter;
//   - for a weak reference, to nothing: source NONE. As with GNU ld, a weak reference loads no member.
// Anything else is an error naming it. Members that join can refer to names nothing else does, so binding goes in
// rounds, each over the symbols the round before added, until no member joins: a frontier walk over a bitset of the
// members. A name that only the members that joined refer to and nothing defines is an error of its own, naming the
// member too (BIND_ERROR_MEMBER_UNDEFINED): on GCC's link line only libgcc_s.so.1 could define it, which a runtime
// without it lacks: the unwinder that libgcc.a's split-stack member (morestack.o) refers to. crtendS.o's and crtn.o's
// references bind without members, as GNU ld scans libc's group before them. A member's strong reference to a name that
// a weak one left to nothing is an error, where GNU ld would scan the archive again; libc_nonshared.a's members refer
// only to libc.so.6 and crtbeginS.o, and the members of libgcc.a that a C program's code needs to each other, to
// libc.so.6, to the loader's __tls_get_addr and to libgcc_s.so.1's unwinder. Last, each definition with default or
// protected visibility of a name that libc.so.6, the loader or libgcc_s.so.1 defines too, or that libc.so.6 refers to,
// is an export (Bind.exports), as GNU ld and lld export them, the loader's and libgcc_s.so.1's names even when it isn't
// needed: those among the names that took a link symbol, which binding lists as they do.
//
// A shared library's runtime (runtime_import_shared) joins as a dynamic PIE's, without a start object: bind_begin
// appends crti.o and crtbeginS.o, and nothing refers to main. A name nothing defines binds as a dynamic PIE's, but a
// strong reference with default visibility that no library exports stays undefined (source NONE), a dynamic symbol for
// the loader to bind in the libraries the program loads, as GNU ld and lld leave it with -shared (-z undefs); but not a
// name that only members refer to, which binds to libgcc_s.so.1 as GCC's driver binds it, the library then needing it,
// or without it stays the error it is in a PIE: left to the loader it would make a library that loads only beside one
// that defines it (morestack.o's _Unwind_Resume and __gcc_personality_v0, which an isolated dlopen refuses). And every
// definition with default or protected visibility is an export (bind_shared_exports), not only those of names the
// libraries define or libc.so.6 refers to.
//
// A static executable's runtime (runtime_import_static) joins the same way: bind_begin appends crt1.o, crti.o and
// crtbeginT.o, and bind_end the members of libgcc.a, libgcc_eh.a, libatomic.a and libc.a that the link needs, then
// crtend.o and crtn.o. Which members the link needs comes first (the closure), as GNU ld finds them scanning the
// driver's group (--start-group ... --end-group) until it loads nothing more: a strong reference to a name that nothing
// in the link defines (the program, the crt objects before it, the linker) loads the member that defines it, whose own
// strong references load more in turn, and a weak reference loads nothing. Every name but two weak COMDAT ones is
// defined by at most one member (docs/object-writer-and-linker.md, "A static executable's runtime, as built"), so the
// set is GNU ld's and lld's in whatever order they scan. The members then join in archive order, and each COMDAT group
// once: the first of its signature in link order. A later one's atoms don't join, its symbols' definitions become
// references to the first's, and a fixup from an atom that joins to one of its local symbols is an error
// (BIND_ERROR_DISCARDED). Then each name nothing defines binds as above but without libc.so.6 or members: to the
// linker, to crtend.o, or for a weak reference to nothing; and __tls_get_addr, which the general- and local-dynamic
// accesses of libgcc.a's PIC members call, stays undefined: a static executable relaxes each such access to local-exec,
// as GNU ld and lld do, and the call with it. A member's absolute symbol (SHN_ABS) defines its name as any definition
// does.
//
// Two paths, which make the same link (bind_test.c compares them column by column):
//   - BIND_PATH_VECTOR: the symbols to match tested eight at a time against a filter of the runtime's names (their
//     hashes and the filter's words gathered), and those to bind selected eight at a time by a compress over the
//     symbol columns; each object joined as a block of columns: copies, with the ids moved eight or sixteen at a
//     time, the symbols that join new compressed into place, and the targets of its fixups moved by scalar loads in a
//     pass of their own.
//     For a static runtime, the closure in frontier rounds over a bitset of the members: each round's references 16
//     at a time, the members they load compressed onto a list and marked a byte each after the round, and the next
//     round's members 64 at a time from the marks.
//     An object with a COMDAT group to discard joins as the exact path joins it.
//     With -l libraries whose closure follows a library's reference to another's name, the definitions that satisfy
//     it found 16 symbols at a time against a filter of the names (bind_library_satisfied_vector).
//   - BIND_PATH_EXACT: the symbols tested one at a time and each looked up in the index, without the filter, and the
//     objects joined one record at a time through link.c's checked adds; for a static runtime, the closure from a
//     queue of the members, each reference tested against the link's symbols and the members loaded so far; and the
//     definitions that satisfy a library's reference, each symbol tested and looked up alone. It is the vector path's
//     twin and oracle.
// Both look names up in the runtime's index (runtime_find_global_name), which bind_test.c checks against a scan.
//
// main.c includes this file after runtime.c. bind_begin and bind_end take BUSTER_LINKER_ENTRY rather than static:
// main.c's units compile them in one unit alone (link.c). Layout, in this order:
//   - API: BindError, BindPath, BindOverride, Bind and the entry points' prototypes.
//   - Matching: bind_lane_mask, bind_visibility_rank, bind_fail, bind_name_symbol, bind_take_name, bind_match_vector,
//     bind_match_exact and bind_match.
//   - Joining an object: bind_visibility, bind_install, bind_merge, bind_piece_fits, bind_join_block, bind_join_records,
//     bind_discards and bind_join.
//   - Binding: bind_relaxed_name, bind_referring_member, bind_resolve_symbol, bind_resolve_vector, bind_resolve_exact,
//     bind_resolve, bind_members, bind_keep_imports, bind_keep_as_needed, bind_find_exports, bind_exported_lanes,
//     bind_shared_exports, BindLibraryEdges, bind_library_edges, bind_library_satisfy, bind_library_satisfied_vector,
//     bind_library_satisfied_exact, bind_library_satisfied, bind_library_versions, bind_needed_libraries,
//     bind_keep_libraries and bind_library_exports.
//   - The static closure: bind_symbol_defined, bind_entry_defined, bind_closure_need, bind_closure_vector,
//     bind_closure_exact and bind_static_members.
//   - Entry points: bind_capacity, bind_capacity_piece, bind_capacity_needed, bind_begin and bind_end.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// CPU architecture dependent includes
#include <immintrin.h>

// ---------------------------------------------------------------------------------------------------------------------
// API

typedef enum BindError
{
    BIND_ERROR_NONE,
    // A name that nothing defines, referred to without being weak, or that only a library (libc.so.6, the loader)
    // defines and a reference with a visibility other than default refers to: Bind.name.
    BIND_ERROR_UNDEFINED,
    // A name that the link and the runtime both define, neither of them weakly: Bind.name.
    BIND_ERROR_DUPLICATE,
    // A name of the runtime's that two of the link's global symbols have: Bind.name.
    BIND_ERROR_SYMBOLS,
    // The link's tables or arenas, or the binding's own memory, have no room.
    BIND_ERROR_FULL,
    // A fixup of an atom that joined refers to a local symbol of a COMDAT group that was discarded.
    BIND_ERROR_DISCARDED,
    // A piece of a runtime loaded from a snapshot that binding was about to read doesn't hash as when the snapshot was
    // written (runtime_piece_current): the snapshot is damaged, and the runtime must be imported again.
    BIND_ERROR_SNAPSHOT,
    // A strong reference of a library the link names with -l and needs that nothing defines: not the executable,
    // another named library, libc.so.6 or the loader (Bind.library and Bind.reference), as GNU ld and lld refuse it
    // (--no-allow-shlib-undefined) where the program would fail to load.
    BIND_ERROR_LIBRARY_UNDEFINED,
    // A version that a library the program loads needs of another (its .gnu.version_r) which that one doesn't define, as
    // glibc's loader refuses the program at startup (bind_library_versions): Bind.library, Bind.need and Bind.provider.
    BIND_ERROR_LIBRARY_VERSION,
    // A strong reference of a member that joined a dynamic PIE's or a shared library's link (libgcc.a's or
    // libc_nonshared.a's) to a name nothing defines and that only the members refer to: Bind.name and Bind.member. GCC's
    // driver names libgcc_s.so.1 after libgcc.a, as needed (-lgcc_s_asneeded), and on its link line only that library
    // could define the name: it defines the unwinder that libgcc.a's split-stack member refers to (morestack.o's
    // _Unwind_Resume and __gcc_personality_v0). On a system whose runtime has no libgcc_s.so.1 (runtime.c), refused,
    // where a shared library would otherwise leave it to the loader and load only beside a library that defines it.
    BIND_ERROR_MEMBER_UNDEFINED,
} BindError;

typedef enum BindPath
{
    BIND_PATH_VECTOR,
    BIND_PATH_EXACT,
} BindPath;

// A definition of the runtime's that takes the program's definition of a name over: the name's entry among the
// runtime's global names, the runtime's symbol, and the link's atom of its definition (0 for an absolute one).
typedef struct BindOverride BindOverride;
struct BindOverride
{
    uint32_t entry;
    SymbolId symbol;
    AtomId atom;
};

typedef struct Bind Bind;
struct Bind
{
    const Runtime* runtime;
    BindPath path;
    // Where the runtime's atoms' bytes are copied.
    Arena* bytes;
    // The link's unit for the runtime's unit 0: the runtime's units follow it in their order.
    uint32_t unit_base;
    // By entry of the runtime's global names: the link's symbol of that name, or 0; and the entries that have one, in
    // the order they took it.
    SymbolId* name_symbols;
    uint32_t* named;
    uint32_t named_count;
    // The definitions of the runtime's that take the program's over in bind_begin, which bind_end installs once the
    // program's atoms have joined, and how many (almost always none): room for one per global name that a symbol of the
    // program's has, the fewer of the global names and of the link's symbols when bind_begin began. A program's own
    // definitions are the only ones the head objects' can take over before bind_end, so that a small program's room
    // stays small: one per global name took 40 KB with libgcc.a's names, which pushed the arrays after it past the base
    // pages that the caller's arena starts with, and made every link of the C driver fault and clear a huge page.
    BindOverride* overrides;
    uint32_t override_count;
    // By symbol of the runtime's link: the link's symbol it became, written as its object joins, before anything reads
    // it (the object's fixups' targets are its own symbols). Nothing clears it: a link that joins the crt objects alone
    // would otherwise touch an entry for every member's symbol (#490).
    SymbolId* symbols;
    // The runtime's members that the link needs, and those that joined it, a bit each.
    uint64_t* members_needed;
    uint64_t* members_joined;
    // The COMDAT signatures of the groups that have joined, a bit each: a later group of one is discarded.
    uint64_t* signatures;
    // A static runtime's closure: the members of a round, a bit each, the members its references load, compressed, and
    // those marked a byte each in 64-byte rows that the round leaves clear (the vector path's); and a queue of the
    // members (the exact path's).
    uint64_t* frontier;
    uint32_t* loads;
    uint8_t* marks;
    uint32_t* queue;
    // The link's symbols count when bind_begin began, below which every symbol is the program's (an absolute one's source
    // says only that it is absolute); and after bind_begin, from which bind_end matches those up to its own start.
    uint32_t program_symbols;
    uint32_t symbols_begun;
    // The link's symbols count when the first of libgcc.a's or libc_nonshared.a's members joined (bind_members): the
    // symbols from it on, up to crtendS.o's and crtn.o's, are names that only the members refer to (0 before one joins,
    // and for a static runtime, whose members join otherwise).
    uint32_t member_symbols;
    // Whether bind_end has begun, after the program's atoms joined; whether names have been bound, after which a link
    // symbol with source NONE below `resolved` is a weak reference to nothing (a static runtime's members all join
    // before); and whether crtendS.o and crtn.o have joined. `resolved` is the end of the symbols bind_resolve has bound
    // so far: one past it is a name that a member which joined in the current round added, still to bind, which another
    // member of the round may refer to as well (libgcc.a's members' memset, __sfp_handle_exceptions).
    bool resolving;
    bool bound;
    bool tail_joined;
    bool deferred_checked;
    SymbolId resolved;
    // The link's imports from libc.so.6, the loader and the libraries it names, and the definitions it exports, room for
    // `capacity` of each.
    SymbolId* imports;
    uint32_t import_count;
    SymbolId* exports;
    uint32_t export_count;
    uint32_t capacity;
    // The shared libraries the link names with -l (runtime_libraries_import), which the caller sets between bind_begin
    // and bind_end, or 0; and the ones the link needs, a bit each: those a strong reference binds to, and from
    // bind_keep_libraries on, those a needed library's strong reference does that the executable's own definitions
    // don't satisfy (bind_needed_libraries).
    const RuntimeLibraries* libraries;
    uint32_t libraries_needed;
    // The libraries the driver names as needed that a needed named library's strong reference needs, a bit each
    // (BUSTER_BIND_NEEDS_LOADER, BUSTER_BIND_NEEDS_GCC_S): a reference to a name of theirs that no DT_NEEDED entry of a
    // library the program loads covers, nor the executable's definition satisfies, as GNU ld's --as-needed counts it
    // (bind_needed_libraries), from bind_keep_libraries on. bind_keep_as_needed keeps such a library whatever the
    // program's own references, and dynamic_inputs makes it required.
    uint32_t runtime_needed;
    // The first error, and the link's name it is about (0 for none); with BIND_ERROR_LIBRARY_UNDEFINED, the library and
    // its reference; with BIND_ERROR_LIBRARY_VERSION, the library, its version need (RuntimeVersionNeeds) and the named
    // library that doesn't define the version, or UINT32_MAX for libc.so.6 or the loader; with
    // BIND_ERROR_MEMBER_UNDEFINED, the first member that joined, in the runtime's order, with a strong reference to the
    // name (its index among Runtime.members).
    BindError error;
    NameId name;
    uint32_t library;
    uint32_t reference;
    uint32_t need;
    uint32_t provider;
    uint32_t member;
};

// How many records the runtime adds to a link at most, and how many bytes of names and of atoms (with their
// alignments): the room a link that binds against it needs besides its own.
static __attribute__((unused)) LinkCapacity bind_capacity(const Runtime* runtime, uint64_t* name_bytes, uint64_t* atom_bytes);
// bind_capacity for a link whose undefined global names, its own and its code's, are `names` (`count` of them): the crt
// objects' records, and of the archives' members only the ones binding may load for it, where bind_capacity counts every
// member (libgcc.a's 298 make a link's columns spread past the base pages its arena starts with). A member joins for a
// strong reference to a name it defines, from the link, a crt object or another member that joins, so the closure of the
// names' members and the crt objects' references' over the members' references (Runtime.reference_members) bounds them:
// a name the link defines or a library exports counts a member that may not join. Its bitset and queue are pushed onto
// `scratch` while it runs. It reads only what a snapshot's directory holds, which loading checks whole (the global
// names, the pieces' references, marks and atoms' rooms): none of a piece's records, which binding checks as it joins
// the piece (runtime_piece_current), so that a damaged piece can't size a link wrong, or past what an arena takes, before
// its join fails as damaged (BIND_ERROR_SNAPSHOT) and the caller opens the runtime again.
static __attribute__((unused)) LinkCapacity bind_capacity_needed(const Runtime* runtime, const String8* names, uint32_t count, Arena* scratch, uint64_t* name_bytes,
                                                                 uint64_t* atom_bytes);
// Begins binding `link` against `runtime` by `path`: matches the link's global symbols against the runtime's names and
// appends Scrt1.o, crti.o and crtbeginS.o (crt1.o, crti.o and crtbeginT.o for a static runtime), numbering the
// runtime's units from unit_base and copying atoms' bytes into `bytes`. Bind's arrays come from `arena`. False, with
// bind->error, if it fails.
BUSTER_LINKER_ENTRY __attribute__((unused)) bool bind_begin(Bind* bind, Link* link, const Runtime* runtime, BindPath path, Arena* bytes, uint32_t unit_base, Arena* arena);
// Binds every name of the link, appending the archives' members it needs (libgcc.a's and libc_nonshared.a's, or a
// static runtime's), then crtendS.o (crtend.o) and crtn.o, and lists the imports and exports. False, with bind->error
// and bind->name, if it fails; the link is then unfit to link.
BUSTER_LINKER_ENTRY __attribute__((unused)) bool bind_end(Bind* bind, Link* link);

// ---------------------------------------------------------------------------------------------------------------------
// Matching

#define BUSTER_BIND_BATCH 8

static uint32_t bind_lane_mask(uint32_t count)
{
    return _bzhi_u32(0xff, count);
}

// How constraining a visibility is: default, protected, hidden, then internal.
static uint32_t bind_visibility_rank(uint8_t flags)
{
    const uint8_t visibility = flags & BUSTER_LINK_SYMBOL_VISIBILITY;
    return visibility == BUSTER_LINK_SYMBOL_DEFAULT     ? 0
           : visibility == BUSTER_LINK_SYMBOL_PROTECTED ? 1
           : visibility == BUSTER_LINK_SYMBOL_HIDDEN    ? 2
                                                        : 3;
}

static void bind_fail(Bind* bind, BindError error, NameId name)
{
    if (bind->error == BIND_ERROR_NONE)
    {
        bind->error = error;
        bind->name = name;
    }
}

// Makes `symbol` the link symbol of the runtime's global name `entry`, which has none, and lists the entry.
static void bind_name_symbol(Bind* bind, uint32_t entry, SymbolId symbol)
{
    bind->name_symbols[entry] = symbol;
    bind->named[bind->named_count] = entry;
    bind->named_count += 1;
}

// Makes `symbol` its name's link symbol if the runtime has the name: an error if another symbol is.
static void bind_take_name(Bind* bind, const Link* link, SymbolId symbol)
{
    const NameId name = link->symbols.name[symbol];
    const uint32_t entry = runtime_find_global_name(&bind->runtime->global_names, link->names.bytes[name], link->names.length[name], link->names.hash[name]);
    if (entry != 0 && bind->name_symbols[entry - 1] == 0)
    {
        bind_name_symbol(bind, entry - 1, symbol);
    }
    else if (entry != 0 && bind->name_symbols[entry - 1] != symbol)
    {
        bind_fail(bind, BIND_ERROR_SYMBOLS, name);
    }
}

// The link's global symbols from `first` to `end`, eight at a time: those whose binding isn't local and that have a
// name, their names' hashes gathered by name, and their words of the runtime's names' filter gathered by the hashes'
// top bits. Only a symbol whose two bits are both set, about one in a hundred of those the runtime doesn't have, goes
// on to the index (bind_take_name). Gathers win here where the patch kernel's didn't: the alternative is two dependent
// scalar loads a symbol, and the filter's words stay in L1.
static void bind_match_vector(Bind* bind, const Link* link, SymbolId first, SymbolId end)
{
    const LinkSymbols* symbols = &link->symbols;
    const RuntimeNames* names = &bind->runtime->global_names;
    const __m512i ones = _mm512_set1_epi64(1);
    const __m512i low6 = _mm512_set1_epi64(63);
    for (SymbolId batch = first; batch < end; batch += BUSTER_BIND_BATCH)
    {
        const uint32_t n = end - batch < BUSTER_BIND_BATCH ? end - batch : BUSTER_BIND_BATCH;
        const __mmask8 loaded = (__mmask8)bind_lane_mask(n);
        const __m512i flags = _mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8(loaded, symbols->flags + batch));
        const __m256i name = _mm256_maskz_loadu_epi32(loaded, symbols->name + batch);
        const __mmask8 kept = (__mmask8)(_mm512_test_epi64_mask(flags, _mm512_set1_epi64(BUSTER_LINK_SYMBOL_BINDING)) & _mm256_test_epi32_mask(name, name) & loaded);
        const __m512i hash = _mm512_mask_i32gather_epi64(_mm512_setzero_si512(), kept, name, link->names.hash, 8);
        const __m512i word_index = _mm512_srli_epi64(hash, 64 - names->filter_bits);
        const __m512i word = _mm512_mask_i64gather_epi64(_mm512_setzero_si512(), kept, word_index, names->filter, 8);
        const __m512i bits = _mm512_or_si512(_mm512_sllv_epi64(ones, _mm512_and_si512(hash, low6)), _mm512_sllv_epi64(ones, _mm512_and_si512(_mm512_srli_epi64(hash, 6), low6)));
        uint32_t maybe = _mm512_mask_cmpeq_epi64_mask(kept, _mm512_and_si512(word, bits), bits);
        while (maybe != 0)
        {
            bind_take_name(bind, link, batch + (SymbolId)__builtin_ctz(maybe));
            maybe &= maybe - 1;
        }
    }
}

static void bind_match_exact(Bind* bind, const Link* link, SymbolId first, SymbolId end)
{
    const LinkSymbols* symbols = &link->symbols;
    for (SymbolId symbol = first; symbol < end; symbol += 1)
    {
        if ((symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL && symbols->name[symbol] != 0)
        {
            bind_take_name(bind, link, symbol);
        }
    }
}

static void bind_match(Bind* bind, const Link* link, SymbolId first, SymbolId end)
{
    if (bind->path == BIND_PATH_VECTOR)
    {
        bind_match_vector(bind, link, first, end);
    }
    else
    {
        bind_match_exact(bind, link, first, end);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Joining an object

// The more constraining of two symbols' visibilities, in their flags' bits.
static uint8_t bind_visibility(uint8_t a, uint8_t b)
{
    return bind_visibility_rank(a) > bind_visibility_rank(b) ? a & BUSTER_LINK_SYMBOL_VISIBILITY : b & BUSTER_LINK_SYMBOL_VISIBILITY;
}

// The link's `symbol` takes the runtime's definition `from_symbol`, whose atom became the link's `atom` (0 for an
// absolute one), over the definition it has: its atom, value, size, type, binding and source, with the more
// constraining visibility. The atom of the definition it had stays in the link, without the name.
static void bind_install(Bind* bind, Link* link, SymbolId symbol, SymbolId from_symbol, AtomId atom)
{
    const LinkSymbols* from = &runtime_symbol_link(bind->runtime, from_symbol)->symbols;
    LinkSymbols* symbols = &link->symbols;
    const uint8_t from_flags = from->flags[from_symbol];
    symbols->atom[symbol] = atom;
    symbols->value[symbol] = from->value[from_symbol];
    symbols->size[symbol] = from->size[from_symbol];
    symbols->flags[symbol] = (uint8_t)((from_flags & ~BUSTER_LINK_SYMBOL_VISIBILITY) | bind_visibility(from_flags, symbols->flags[symbol]));
    symbols->source[symbol] = from->source[from_symbol];
}

// Joins the runtime's symbol `from_symbol`, of an object that has just joined, to the link's `symbol` of the same name
// (see the orientation comment): `atom` is the link's atom of its definition (0 for an absolute one), and a definition
// in a COMDAT group that was `discarded` joins as a reference.
static void bind_merge(Bind* bind, Link* link, SymbolId from_symbol, SymbolId symbol, AtomId atom, bool discarded)
{
    const LinkSymbols* from = &runtime_symbol_link(bind->runtime, from_symbol)->symbols;
    LinkSymbols* symbols = &link->symbols;
    const uint8_t from_flags = from->flags[from_symbol];
    const uint8_t flags = symbols->flags[symbol];
    const bool from_absolute = from->source[from_symbol] == BUSTER_LINK_SOURCE_ABSOLUTE;
    const bool from_defined = !discarded && (from->atom[from_symbol] != 0 || from_absolute);
    // A program symbol is the program's definition, before or after its atom joins: of source PROGRAM, or an absolute
    // one the link had before bind_begin. A definition of the runtime's that took it over in bind_begin waits in
    // Bind.overrides for bind_end, and stands for the link's definition meanwhile.
    const uint8_t source = symbols->source[symbol];
    const bool program = source == BUSTER_LINK_SOURCE_PROGRAM || (source == BUSTER_LINK_SOURCE_ABSOLUTE && symbol < bind->program_symbols);
    const bool waiting = program && !bind->resolving;
    const uint32_t entry = waiting ? runtime_symbol_names(bind->runtime, from_symbol)[from_symbol] - 1 : 0;
    uint32_t found = bind->override_count;
    for (uint32_t i = 0; waiting && i < bind->override_count; i += 1)
    {
        found = bind->overrides[i].entry == entry ? i : found;
    }
    const SymbolId pending = found < bind->override_count ? bind->overrides[found].symbol : 0;
    const uint8_t defining_flags = pending != 0 ? runtime_symbol_link(bind->runtime, pending)->symbols.flags[pending] : flags;
    const bool defined = symbols->atom[symbol] != 0 || program || source == BUSTER_LINK_SOURCE_ABSOLUTE;
    const bool from_weak = (from_flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
    const bool link_weak = (defining_flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
    // In link order, Scrt1.o, crti.o and crtbeginS.o come before the program, and the rest after everything that has
    // joined: the link's definition comes first unless it is the program's own and the object is one of the first three.
    const bool link_first = !waiting || pending != 0;
    // Of two definitions, a strong one takes a weak one's place, and of two weak ones the first in link order stays, as
    // GNU ld and lld resolve them.
    const bool replaces = from_defined && defined && link_weak && (!from_weak || !link_first);
    const uint8_t visibility = bind_visibility(from_flags, flags);
    if (from_defined && defined && !from_weak && !link_weak)
    {
        bind_fail(bind, BIND_ERROR_DUPLICATE, symbols->name[symbol]);
    }
    else if (replaces && waiting)
    {
        // The program adds its atoms after bind_begin, defining its symbols; bind_end installs this once it has.
        bind->overrides[found] = (BindOverride){
            .entry = entry,
            .symbol = from_symbol,
            .atom = atom,
        };
        bind->override_count += found == bind->override_count ? 1 : 0;
        symbols->flags[symbol] = (uint8_t)((flags & ~BUSTER_LINK_SYMBOL_VISIBILITY) | visibility);
    }
    else if (replaces)
    {
        bind_install(bind, link, symbol, from_symbol, atom);
    }
    else if (from_defined && !defined)
    {
        // An absolute definition has no atom for link_define_symbol: its value is the symbol's.
        if (from_absolute || link_define_symbol(link, symbol, atom, from->value[from_symbol], from->size[from_symbol]))
        {
            symbols->value[symbol] = from->value[from_symbol];
            symbols->size[symbol] = from->size[from_symbol];
            symbols->flags[symbol] = (uint8_t)((from_flags & ~BUSTER_LINK_SYMBOL_VISIBILITY) | visibility);
            symbols->source[symbol] = from->source[from_symbol];
        }
        else
        {
            bind_fail(bind, BIND_ERROR_FULL, symbols->name[symbol]);
        }
    }
    else if (!from_defined && !defined)
    {
        const bool weak = from_weak && (flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
        const uint8_t type = (flags & BUSTER_LINK_SYMBOL_TYPE) != BUSTER_LINK_SYMBOL_NOTYPE ? flags & BUSTER_LINK_SYMBOL_TYPE : from_flags & BUSTER_LINK_SYMBOL_TYPE;
        symbols->flags[symbol] = (uint8_t)(type | (weak ? BUSTER_LINK_SYMBOL_WEAK : BUSTER_LINK_SYMBOL_GLOBAL) | visibility);
        // Once names are bound, a link symbol of source NONE is a weak reference that nothing defines, unless a member
        // of the current round added it and binding hasn't reached it (Bind.resolved).
        if (bind->bound && !weak && symbols->source[symbol] == BUSTER_LINK_SOURCE_NONE && symbol < bind->resolved)
        {
            bind_fail(bind, BIND_ERROR_UNDEFINED, symbols->name[symbol]);
        }
    }
    else
    {
        // The link's definition stays, with the more constraining visibility: the gABI gives the resolving symbol that of
        // every reference to the name and every definition of it.
        symbols->flags[symbol] = (uint8_t)((flags & ~BUSTER_LINK_SYMBOL_VISIBILITY) | visibility);
    }
}

// Whether the link has room for piece `piece` of the runtime: its records, its names' bytes, and its atoms' bytes with
// their alignments (a piece that discards a group needs less).
static bool bind_piece_fits(const Bind* bind, const Link* link, uint32_t piece)
{
    const Link* from = runtime_piece_link(bind->runtime, piece);
    const ImportMark begin = bind->runtime->pieces[piece].begin;
    const ImportMark end = bind->runtime->pieces[piece].end;
    uint64_t atom_bytes = 0;
    for (AtomId atom = begin.atoms; atom < end.atoms; atom += 1)
    {
        atom_bytes += from->atoms.bytes[atom] != 0 ? from->atoms.size[atom] + ((uint64_t)1 << from->atoms.alignment_log2[atom]) - 1 : 0;
    }
    const Arena* names = link->name_arena;
    return link->names.capacity - link->names.count >= end.names - begin.names && link->atoms.capacity - link->atoms.count >= end.atoms - begin.atoms &&
           link->symbols.capacity - link->symbols.count >= end.symbols - begin.symbols && link->fixups.capacity - link->fixups.count >= end.fixups - begin.fixups &&
           link->fixups.wide_capacity - link->fixups.wide_count >= end.wide_addends - begin.wide_addends &&
           link->priorities.capacity - link->priorities.count >= end.priorities - begin.priorities &&
           names->capacity - names->length >= end.name_bytes - begin.name_bytes && bind->bytes->capacity - bind->bytes->length >= atom_bytes;
}

// The vector path's join: each table's columns copied as a block (see the orientation comment).
static void bind_join_block(Bind* bind, Link* link, uint32_t piece)
{
    const Runtime* runtime = bind->runtime;
    const Link* from = runtime_piece_link(runtime, piece);
    const uint32_t* symbol_names = runtime_piece_symbol_names(runtime, piece);
    const ImportMark begin = runtime->pieces[piece].begin;
    const ImportMark end = runtime->pieces[piece].end;
    const uint32_t name_count = end.names - begin.names;
    const uint32_t atom_count = end.atoms - begin.atoms;
    const uint32_t symbol_count = end.symbols - begin.symbols;
    const uint32_t fixup_count = end.fixups - begin.fixups;
    const uint32_t wide_count = end.wide_addends - begin.wide_addends;
    const NameId name_first = link->names.count;
    const AtomId atom_first = link->atoms.count;
    const SymbolId symbol_first = link->symbols.count;
    const FixupId fixup_first = link->fixups.count;
    const uint32_t name_delta = name_first - begin.names;
    const uint32_t atom_delta = atom_first - begin.atoms;
    const uint32_t fixup_delta = fixup_first - begin.fixups;
    const uint32_t wide_delta = link->fixups.wide_count - begin.wide_addends;

    // Names: the piece's bytes are one run in the runtime's name arena, copied at once, and the pointers moved with them
    // (a loaded runtime's offsets by its delta too: Runtime.byte_delta).
    const uint64_t name_bytes = end.name_bytes - begin.name_bytes;
    const uint8_t* source = runtime_piece_names(runtime, piece) + begin.name_bytes;
    uint8_t* copy = arena_push(link->name_arena, name_bytes, 1);
    memcpy(copy, source, name_bytes);
    const __m512i moved = _mm512_set1_epi64((long long)((uintptr_t)copy - (uintptr_t)source + runtime->byte_delta));
    for (uint32_t i = 0; i < name_count; i += BUSTER_BIND_BATCH)
    {
        const __mmask8 lanes = (__mmask8)bind_lane_mask(name_count - i < BUSTER_BIND_BATCH ? name_count - i : BUSTER_BIND_BATCH);
        const __m512i pointers = _mm512_maskz_loadu_epi64(lanes, from->names.bytes + begin.names + i);
        _mm512_mask_storeu_epi64(link->names.bytes + name_first + i, lanes, _mm512_add_epi64(pointers, moved));
    }
    memcpy(link->names.length + name_first, from->names.length + begin.names, name_count * sizeof(uint32_t));
    memcpy(link->names.hash + name_first, from->names.hash + begin.names, name_count * sizeof(uint64_t));
    link->names.count += name_count;

    // Atoms: units and fixup ranges moved sixteen at a time, bytes copied one atom at a time.
    LinkAtoms* atoms = &link->atoms;
    memcpy(atoms->class + atom_first, from->atoms.class + begin.atoms, atom_count);
    memcpy(atoms->alignment_log2 + atom_first, from->atoms.alignment_log2 + begin.atoms, atom_count);
    memcpy(atoms->size + atom_first, from->atoms.size + begin.atoms, atom_count * sizeof(uint64_t));
    memcpy(atoms->fixup_count + atom_first, from->atoms.fixup_count + begin.atoms, atom_count * sizeof(uint32_t));
    memset(atoms->address + atom_first, 0, atom_count * sizeof(uint64_t));
    for (uint32_t i = 0; i < atom_count; i += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(0xffff, atom_count - i < 16 ? atom_count - i : 16);
        const __m512i units = _mm512_maskz_loadu_epi32(lanes, from->atoms.unit + begin.atoms + i);
        _mm512_mask_storeu_epi32(atoms->unit + atom_first + i, lanes, _mm512_add_epi32(units, _mm512_set1_epi32((int)bind->unit_base)));
        const __m512i counts = _mm512_maskz_loadu_epi32(lanes, from->atoms.fixup_count + begin.atoms + i);
        const __m512i firsts = _mm512_maskz_loadu_epi32(lanes, from->atoms.fixup_begin + begin.atoms + i);
        const __mmask16 ranged = _mm512_test_epi32_mask(counts, counts);
        _mm512_mask_storeu_epi32(atoms->fixup_begin + atom_first + i, lanes, _mm512_mask_add_epi32(firsts, ranged, firsts, _mm512_set1_epi32((int)fixup_delta)));
    }
    for (uint32_t i = 0; i < atom_count; i += 1)
    {
        const AtomId atom = begin.atoms + i;
        const uint8_t* bytes = runtime_atom_bytes(runtime, atom);
        uint8_t* placed = bytes != 0 ? arena_push(bind->bytes, from->atoms.size[atom], (uint64_t)1 << from->atoms.alignment_log2[atom]) : 0;
        if (bytes != 0)
        {
            memcpy(placed, bytes, from->atoms.size[atom]);
        }
        atoms->bytes[atom_first + i] = placed;
    }
    atoms->count += atom_count;

    // Symbols: each one's link symbol, if its name has one, by scalar loads in a pass of their own; then eight at a time,
    // those without one compressed into place with their name and atom ids moved, and every one's link symbol stored.
    Scratch scratch = scratch_begin(bind->bytes);
    SymbolId* taken = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, SymbolId, symbol_count + BUSTER_BIND_BATCH);
    for (uint32_t i = 0; taken != 0 && i < symbol_count; i += 1)
    {
        const uint32_t entry = symbol_names[begin.symbols + i];
        taken[i] = entry != 0 ? bind->name_symbols[entry - 1] : 0;
    }
    LinkSymbols* symbols = &link->symbols;
    const __m512i lane_index = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
    uint32_t added = 0;
    for (uint32_t i = 0; taken != 0 && i < symbol_count; i += BUSTER_BIND_BATCH)
    {
        const uint32_t n = symbol_count - i < BUSTER_BIND_BATCH ? symbol_count - i : BUSTER_BIND_BATCH;
        const __mmask8 lanes = (__mmask8)bind_lane_mask(n);
        const SymbolId first = begin.symbols + i;
        const __m512i owner = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(lanes, taken + i));
        const __mmask8 fresh = _mm512_mask_testn_epi64_mask(lanes, owner, owner);
        const uint32_t fresh_count = (uint32_t)__builtin_popcount(fresh);
        const __mmask8 placed = (__mmask8)bind_lane_mask(fresh_count);
        const __m512i names = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(lanes, from->symbols.name + first));
        const __m512i atom_ids = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(lanes, from->symbols.atom + first));
        const __m512i moved_names = _mm512_mask_add_epi64(names, _mm512_test_epi64_mask(names, names), names, _mm512_set1_epi64(name_delta));
        const __m512i moved_atoms = _mm512_mask_add_epi64(atom_ids, _mm512_test_epi64_mask(atom_ids, atom_ids), atom_ids, _mm512_set1_epi64(atom_delta));
        const SymbolId to = symbol_first + added;
        _mm256_mask_storeu_epi32(symbols->name + to, placed, _mm512_cvtepi64_epi32(_mm512_maskz_compress_epi64(fresh, moved_names)));
        _mm256_mask_storeu_epi32(symbols->atom + to, placed, _mm512_cvtepi64_epi32(_mm512_maskz_compress_epi64(fresh, moved_atoms)));
        _mm512_mask_storeu_epi64(symbols->value + to, placed, _mm512_maskz_compress_epi64(fresh, _mm512_maskz_loadu_epi64(lanes, from->symbols.value + first)));
        _mm512_mask_storeu_epi64(symbols->size + to, placed, _mm512_maskz_compress_epi64(fresh, _mm512_maskz_loadu_epi64(lanes, from->symbols.size + first)));
        const __m512i flags = _mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8(lanes, from->symbols.flags + first));
        const __m512i sources = _mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8(lanes, from->symbols.source + first));
        _mm_mask_storeu_epi8(symbols->flags + to, placed, _mm512_cvtepi64_epi8(_mm512_maskz_compress_epi64(fresh, flags)));
        _mm_mask_storeu_epi8(symbols->source + to, placed, _mm512_cvtepi64_epi8(_mm512_maskz_compress_epi64(fresh, sources)));
        // A fresh symbol's id is the next of the block's, by its rank among the fresh ones: an expand of consecutive ids.
        const __m512i fresh_ids = _mm512_maskz_expand_epi64(fresh, _mm512_add_epi64(lane_index, _mm512_set1_epi64(to)));
        _mm256_mask_storeu_epi32(bind->symbols + first, lanes, _mm512_cvtepi64_epi32(_mm512_mask_blend_epi64(fresh, owner, fresh_ids)));
        added += fresh_count;
    }
    memset(symbols->got_slot + symbol_first, 0, added * sizeof(uint32_t));
    memset(symbols->dynamic_index + symbol_first, 0, added * sizeof(uint32_t));
    memset(symbols->version + symbol_first, 0, added * sizeof(uint16_t));
    memset(symbols->reference + symbol_first, 0, added * sizeof(uint32_t));
    symbols->count += added;
    // The names of the fresh global symbols are theirs now; the others join their link symbols.
    for (uint32_t i = 0; taken != 0 && i < symbol_count; i += 1)
    {
        const SymbolId from_symbol = begin.symbols + i;
        const uint32_t entry = symbol_names[from_symbol];
        if (taken[i] == 0 && entry != 0)
        {
            bind_name_symbol(bind, entry - 1, bind->symbols[from_symbol]);
        }
        else if (taken[i] != 0)
        {
            const AtomId from_atom = from->symbols.atom[from_symbol];
            bind_merge(bind, link, from_symbol, taken[i], from_atom != 0 ? from_atom + atom_delta : 0, false);
        }
    }
    if (taken == 0)
    {
        bind_fail(bind, BIND_ERROR_FULL, 0);
    }
    scratch_end(scratch);

    // Priorities: the piece's entries, after the link's, their atoms moved sixteen at a time.
    LinkPriorities* priorities = &link->priorities;
    const uint32_t priority_count = end.priorities - begin.priorities;
    for (uint32_t i = 0; i < priority_count; i += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(0xffff, priority_count - i < 16 ? priority_count - i : 16);
        const __m512i prioritized = _mm512_maskz_loadu_epi32(lanes, from->priorities.atom + begin.priorities + i);
        _mm512_mask_storeu_epi32(priorities->atom + priorities->count + i, lanes, _mm512_add_epi32(prioritized, _mm512_set1_epi32((int)atom_delta)));
    }
    memcpy(priorities->priority + priorities->count, from->priorities.priority + begin.priorities, priority_count * sizeof(uint16_t));
    priorities->count += priority_count;

    // Fixups: offsets and kinds copied, wide addends' indices moved sixteen at a time, targets through the symbols' link
    // symbols by scalar loads, and the wide addends copied.
    LinkFixups* fixups = &link->fixups;
    memcpy(fixups->offset + fixup_first, from->fixups.offset + begin.fixups, fixup_count * sizeof(uint32_t));
    memcpy(fixups->kind + fixup_first, from->fixups.kind + begin.fixups, fixup_count);
    for (uint32_t i = 0; i < fixup_count; i += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(0xffff, fixup_count - i < 16 ? fixup_count - i : 16);
        const __m512i kinds = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(lanes, from->fixups.kind + begin.fixups + i));
        const __mmask16 wide = _mm512_test_epi32_mask(kinds, _mm512_set1_epi32(BUSTER_LINK_FIXUP_WIDE));
        const __m512i addends = _mm512_maskz_loadu_epi32(lanes, from->fixups.addend + begin.fixups + i);
        _mm512_mask_storeu_epi32(fixups->addend + fixup_first + i, lanes, _mm512_mask_add_epi32(addends, wide, addends, _mm512_set1_epi32((int)wide_delta)));
    }
    // The piece's symbols have their link symbols unless the scratch above had no room (BIND_ERROR_FULL).
    for (uint32_t i = 0; taken != 0 && i < fixup_count; i += 1)
    {
        fixups->target[fixup_first + i] = bind->symbols[from->fixups.target[begin.fixups + i]];
    }
    memcpy(fixups->wide_addends + fixups->wide_count, from->fixups.wide_addends + begin.wide_addends, wide_count * sizeof(int64_t));
    fixups->count += fixup_count;
    fixups->wide_count += wide_count;
}

// The exact path's join: one record at a time, through link.c's adds. `discarded`, if not 0, marks the piece's atoms
// (a byte per atom, by its index in the piece) in COMDAT groups that don't join: they join with no atom, fixups or
// priorities, their symbols' definitions join as references, and their local symbols don't join, so that a fixup to
// one from an atom that joins is an error.
static void bind_join_records(Bind* bind, Link* link, uint32_t piece, const uint8_t* discarded)
{
    const Runtime* runtime = bind->runtime;
    const Link* from = runtime_piece_link(runtime, piece);
    const uint32_t* symbol_names = runtime_piece_symbol_names(runtime, piece);
    const ImportMark begin = runtime->pieces[piece].begin;
    const ImportMark end = runtime->pieces[piece].end;
    const NameId name_first = link->names.count;
    bool result = true;
    for (NameId name = begin.names; result && name < end.names; name += 1)
    {
        result = link_add_name_hashed(link, runtime_name_bytes(runtime, name), from->names.length[name], from->names.hash[name]) == name_first + (name - begin.names);
    }
    // Each of the piece's atoms' atom in the link, or 0 for a discarded one.
    Scratch scratch = scratch_begin(bind->bytes);
    AtomId* atoms = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, AtomId, (uint64_t)(end.atoms - begin.atoms) + 1);
    result = result && atoms != 0;
    for (AtomId atom = begin.atoms; result && atom < end.atoms; atom += 1)
    {
        const bool dropped = discarded != 0 && discarded[atom - begin.atoms] != 0;
        const uint8_t* bytes = runtime_atom_bytes(runtime, atom);
        uint8_t* placed = bytes != 0 && !dropped ? arena_push(bind->bytes, from->atoms.size[atom], (uint64_t)1 << from->atoms.alignment_log2[atom]) : 0;
        if (placed != 0)
        {
            memcpy(placed, bytes, from->atoms.size[atom]);
        }
        const AtomId expected = link->atoms.count;
        const AtomId added = dropped ? 0 : link_add_atom(link, from->atoms.class[atom], from->atoms.alignment_log2[atom], from->atoms.size[atom], placed, from->atoms.unit[atom] + bind->unit_base);
        atoms[atom - begin.atoms] = added;
        result = dropped || (added == expected && (bytes == 0 || placed != 0));
    }
    for (SymbolId symbol = begin.symbols; result && symbol < end.symbols; symbol += 1)
    {
        const uint32_t entry = symbol_names[symbol];
        const SymbolId taken = entry != 0 ? bind->name_symbols[entry - 1] : 0;
        const AtomId from_atom = from->symbols.atom[symbol];
        const AtomId atom = from_atom != 0 ? atoms[from_atom - begin.atoms] : 0;
        const bool dropped = from_atom != 0 && atom == 0;
        const uint8_t flags = from->symbols.flags[symbol];
        const bool local = (flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL;
        if (taken == 0 && dropped && local)
        {
            bind->symbols[symbol] = 0;
        }
        else if (taken == 0)
        {
            // A definition in a discarded group joins as a reference of its name.
            const NameId name = from->symbols.name[symbol];
            const SymbolId added = link_add_symbol(link, name != 0 ? name_first + (name - begin.names) : 0, atom, dropped ? 0 : from->symbols.value[symbol],
                                                   dropped ? 0 : from->symbols.size[symbol], flags, dropped ? BUSTER_LINK_SOURCE_NONE : from->symbols.source[symbol]);
            bind->symbols[symbol] = added;
            result = added != 0;
            if (entry != 0)
            {
                bind_name_symbol(bind, entry - 1, added);
            }
        }
        else
        {
            bind->symbols[symbol] = taken;
            bind_merge(bind, link, symbol, taken, atom, dropped);
        }
    }
    for (AtomId atom = begin.atoms; result && atom < end.atoms; atom += 1)
    {
        const FixupId first = from->atoms.fixup_begin[atom];
        const AtomId to = atoms[atom - begin.atoms];
        for (uint32_t i = 0; result && to != 0 && i < from->atoms.fixup_count[atom]; i += 1)
        {
            const FixupId fixup = first + i;
            const uint8_t kind = from->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
            const SymbolId target = bind->symbols[from->fixups.target[fixup]];
            if (target == 0)
            {
                bind_fail(bind, BIND_ERROR_DISCARDED, 0);
            }
            result = target != 0 && link_add_fixup(link, to, from->fixups.offset[fixup], kind, target, link_fixup_addend(from, fixup)) != 0;
        }
    }
    for (uint32_t entry = begin.priorities; result && entry < end.priorities; entry += 1)
    {
        const AtomId to = atoms[from->priorities.atom[entry] - begin.atoms];
        result = to == 0 || link_add_priority(link, to, from->priorities.priority[entry]);
    }
    scratch_end(scratch);
    if (!result)
    {
        bind_fail(bind, BIND_ERROR_FULL, 0);
    }
}

// The atoms of piece `piece` in COMDAT groups that don't join, those of a group whose signature's first group in link
// order has joined, marked in a byte per atom of the piece pushed on `arena`; 0 if the piece discards none. The
// signatures of the piece's groups are then the link's.
static uint8_t* bind_discards(Bind* bind, uint32_t piece, Arena* arena)
{
    const Runtime* runtime = bind->runtime;
    const ImportMark begin = runtime->pieces[piece].begin;
    const uint32_t atom_count = runtime->pieces[piece].end.atoms - begin.atoms;
    uint8_t* result = 0;
    bool fits = true;
    for (uint32_t g = runtime->group_begin[piece]; fits && g < runtime->group_begin[piece + 1]; g += 1)
    {
        const RuntimeGroup* group = &runtime->groups[g];
        const uint64_t bit = 1ull << (group->signature % 64);
        const bool joined = (bind->signatures[group->signature / 64] & bit) != 0;
        bind->signatures[group->signature / 64] |= bit;
        if (joined && group->count != 0 && result == 0)
        {
            result = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)atom_count + 1);
            fits = result != 0;
            if (fits)
            {
                memset(result, 0, atom_count);
            }
        }
        for (uint32_t i = 0; joined && fits && i < group->count; i += 1)
        {
            result[runtime->group_atoms[group->first + i] - begin.atoms] = 1;
        }
    }
    if (!fits)
    {
        bind_fail(bind, BIND_ERROR_FULL, 0);
    }
    return result;
}

// Joins piece `piece` of the runtime (a crt object, or RUNTIME_OBJECTS + m for member m) to the link, if it has room,
// once a loaded runtime's piece is found as it was written (runtime_piece_current): binding reads none of a piece's
// records before. A piece with a COMDAT group to discard, which few have, joins one record at a time on either path.
static bool bind_join(Bind* bind, Link* link, uint32_t piece)
{
    const bool current = runtime_piece_current(bind->runtime, piece);
    const bool fits = current && bind_piece_fits(bind, link, piece);
    Scratch scratch = scratch_begin(bind->bytes);
    const uint8_t* discarded = fits ? bind_discards(bind, piece, scratch.arena) : 0;
    if (!current)
    {
        bind_fail(bind, BIND_ERROR_SNAPSHOT, 0);
    }
    else if (!fits)
    {
        bind_fail(bind, BIND_ERROR_FULL, 0);
    }
    else if (bind->error != BIND_ERROR_NONE)
    {
        // bind_discards had no room.
    }
    else if (bind->path == BIND_PATH_VECTOR && discarded == 0)
    {
        bind_join_block(bind, link, piece);
    }
    else
    {
        bind_join_records(bind, link, piece, discarded);
    }
    scratch_end(scratch);
    return bind->error == BIND_ERROR_NONE;
}

// ---------------------------------------------------------------------------------------------------------------------
// Binding

// Whether a static executable's relaxation consumes every reference to the name: __tls_get_addr, which the general-
// and local-dynamic TLS accesses of libgcc.a's PIC members call and nothing in a static link defines. GNU ld and lld
// relax each such access to local-exec, call and all, as the writer does; it reports a reference it can't relax.
static bool bind_relaxed_name(const Link* link, NameId name)
{
    static const char relaxed[] = "__tls_get_addr";
    return link->names.length[name] == sizeof(relaxed) - 1 && memcmp(link->names.bytes[name], relaxed, sizeof(relaxed) - 1) == 0;
}

// The first member that joined, in the runtime's order, among whose strong references (Runtime.references) is the
// runtime's global name `entry` (an index plus 1), or UINT32_MAX for none: BIND_ERROR_MEMBER_UNDEFINED's member, read
// once, when binding fails.
static uint32_t bind_referring_member(const Bind* bind, uint32_t entry)
{
    const Runtime* runtime = bind->runtime;
    uint32_t result = UINT32_MAX;
    for (uint32_t m = 0; result == UINT32_MAX && entry != 0 && m < runtime->member_count; m += 1)
    {
        const bool joined = (bind->members_joined[m / 64] >> (m % 64) & 1) != 0;
        const uint32_t end = joined ? runtime->reference_begin[RUNTIME_OBJECTS + m + 1] : 0;
        for (uint32_t r = joined ? runtime->reference_begin[RUNTIME_OBJECTS + m] : 0; r < end; r += 1)
        {
            result = result == UINT32_MAX && runtime->references[r] == entry - 1 ? m : result;
        }
    }
    return result;
}

// Makes `symbol` an import of a library's export, `index` among `exports`: source RUNTIME_SHARED, the export's version
// and type, listed in Bind.imports.
static void bind_import(Bind* bind, LinkSymbols* symbols, SymbolId symbol, const ImportExports* exports, uint32_t index)
{
    symbols->source[symbol] = BUSTER_LINK_SOURCE_RUNTIME_SHARED;
    symbols->version[symbol] = exports->version[index];
    symbols->flags[symbol] = (uint8_t)((symbols->flags[symbol] & ~BUSTER_LINK_SYMBOL_TYPE) | (exports->flags[index] & BUSTER_LINK_SYMBOL_TYPE));
    if (bind->import_count < bind->capacity)
    {
        bind->imports[bind->import_count] = symbol;
        bind->import_count += 1;
    }
    else
    {
        bind_fail(bind, BIND_ERROR_FULL, symbols->name[symbol]);
    }
}

// Imported runtimes already read these files. A loaded snapshot checks the optional library once per binding, only
// when a strong import or an otherwise unresolved name can depend on it. Weak-only imports are discarded below.
static void bind_check_deferred(Bind* bind)
{
    if (!bind->deferred_checked)
    {
        bind->deferred_checked = true;
        if (!runtime_deferred_probes_current(bind->runtime))
        {
            bind->error = BIND_ERROR_SNAPSHOT;
            bind->name = 0;
        }
    }
}

static void bind_check_undefined(Bind* bind)
{
    if (bind->error == BIND_ERROR_UNDEFINED || bind->error == BIND_ERROR_MEMBER_UNDEFINED || bind->error == BIND_ERROR_LIBRARY_UNDEFINED)
    {
        bind_check_deferred(bind);
    }
}

// Binds `symbol`, which nothing in the link defines; with `members`, an archive member defining it is marked needed.
static void bind_resolve_symbol(Bind* bind, Link* link, SymbolId symbol, bool members)
{
    const Runtime* runtime = bind->runtime;
    const RuntimeNames* names = &runtime->global_names;
    LinkSymbols* symbols = &link->symbols;
    const NameId name = symbols->name[symbol];
    const uint32_t entry = runtime_find_global_name(names, link->names.bytes[name], link->names.length[name], link->names.hash[name]);
    const SymbolId definition = entry != 0 ? names->definition[entry - 1] : 0;
    // A definition in an atom of crtendS.o or crtn.o (crtend.o or crtn.o), whose pieces bind_begin found current: the
    // pieces say which symbols are theirs, so that no other piece's records are read before it joins.
    const RuntimePiece* pieces = runtime->pieces;
    const bool tail_piece = definition >= pieces[RUNTIME_OBJECT_END].begin.symbols && definition < pieces[RUNTIME_OBJECT_INIT_EPILOGUE].end.symbols;
    const bool after = definition != 0 && !bind->tail_joined && tail_piece && runtime_symbol_link(runtime, definition)->symbols.atom[definition] != 0;
    const uint32_t export = entry != 0 ? names->export[entry - 1] : 0;
    // libgcc_s.so.1's export, which a member of libgcc.a that defines the name comes before.
    const bool after_members = export != 0 && runtime_export_after_members(runtime, export - 1);
    const uint32_t member = entry != 0 ? names->member[entry - 1] : 0;
    const bool weak = (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
    // A reference with a visibility other than default needs a definition within the executable: a library's can't be
    // one, as GNU ld and lld resolve it.
    const bool shareable = (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_VISIBILITY) == BUSTER_LINK_SYMBOL_DEFAULT;
    // A name that the members which joined brought into the link (bind_members), which none of the program's symbols,
    // the head crt objects' or the tail's names: only members refer to it.
    const bool member_name = bind->member_symbols != 0 && symbol >= bind->member_symbols && !bind->tail_joined;
    // A library the link names with -l comes before libc.so's on the link line, as GCC's driver puts it.
    uint32_t library_export = 0;
    const uint32_t library = runtime_libraries_export(bind->libraries, link->names.bytes[name], link->names.length[name], link->names.hash[name], &library_export);
    if (entry != 0 && names->linker[entry - 1])
    {
        symbols->source[symbol] = BUSTER_LINK_SOURCE_LINKER;
    }
    else if (after)
    {
        // crtendS.o or crtn.o defines it when it joins.
        symbols->source[symbol] = BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE;
    }
    else if (library != 0 && shareable)
    {
        bind_import(bind, symbols, symbol, &bind->libraries->libraries[library - 1].exports, library_export);
        bind->libraries_needed |= weak ? 0 : 1u << (library - 1);
    }
    else if (export != 0 && !after_members && shareable)
    {
        uint32_t index = 0;
        const ImportLibrary* library = runtime_export_library(runtime, export - 1, &index);
        bind_import(bind, symbols, symbol, &library->exports, index);
    }
    else if (member != 0 && members && !weak)
    {
        // Its member defines it when it joins, at the end of this round. Until then the symbol isn't a reference left to
        // nothing (source NONE), which bind_merge would take it for when another member that joins first refers to the
        // name: libgcc.a's __fixdfti's to __fixunsdfti, of a program that calls both.
        const uint32_t m = member - 1;
        bind->members_needed[m / 64] |= 1ull << (m % 64);
        symbols->source[symbol] = BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE;
    }
    else if (after_members && shareable)
    {
        // libgcc_s.so.1's, needed only for a strong reference (bind_keep_as_needed).
        if (!weak)
        {
            bind_check_deferred(bind);
        }
        if (bind->error == BIND_ERROR_NONE)
        {
            const uint32_t index = export - 1 - runtime->libc.exports.count - runtime->loader.exports.count;
            bind_import(bind, symbols, symbol, &runtime->gcc_s.exports, index);
        }
    }
    else if (weak)
    {
        symbols->source[symbol] = BUSTER_LINK_SOURCE_NONE;
    }
    else if (runtime_kind_is_static(runtime->kind) && bind_relaxed_name(link, name))
    {
        // Left undefined, to the relaxation (bind_relaxed_name).
    }
    else if (runtime->kind == RUNTIME_KIND_SHARED && shareable && !member_name)
    {
        // A shared library's reference that nothing defines stays undefined, a dynamic symbol for the loader to bind in
        // the libraries the program loads, as GNU ld and lld leave it (-shared allows it: -z undefs); but not one that
        // only members refer to (BIND_ERROR_MEMBER_UNDEFINED).
        symbols->source[symbol] = BUSTER_LINK_SOURCE_NONE;
        // A newly installed library could define this strong name even though a shared link permits it undefined.
        bind_check_deferred(bind);
    }
    else if (member_name)
    {
        const bool first = bind->error == BIND_ERROR_NONE;
        bind_fail(bind, BIND_ERROR_MEMBER_UNDEFINED, name);
        bind->member = first ? bind_referring_member(bind, entry) : bind->member;
    }
    else
    {
        bind_fail(bind, BIND_ERROR_UNDEFINED, name);
    }
}

// The symbols from `first` to `end` that nothing defines (no atom, a name, and a source that isn't the program's, the
// linker's or absolute), selected eight at a time by a compress, then bound in symbol order.
static void bind_resolve_vector(Bind* bind, Link* link, SymbolId first, SymbolId end, bool members)
{
    const LinkSymbols* symbols = &link->symbols;
    const __m512i lane_index = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
    for (SymbolId batch = first; batch < end; batch += BUSTER_BIND_BATCH)
    {
        const uint32_t n = end - batch < BUSTER_BIND_BATCH ? end - batch : BUSTER_BIND_BATCH;
        const __mmask8 loaded = (__mmask8)bind_lane_mask(n);
        const __m512i atoms = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(loaded, symbols->atom + batch));
        const __m512i names = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(loaded, symbols->name + batch));
        const __m512i sources = _mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8(loaded, symbols->source + batch));
        const uint32_t undefined = _mm512_testn_epi64_mask(atoms, atoms) & _mm512_test_epi64_mask(names, names) &
                                   _mm512_cmpneq_epi64_mask(sources, _mm512_set1_epi64(BUSTER_LINK_SOURCE_PROGRAM)) &
                                   _mm512_cmpneq_epi64_mask(sources, _mm512_set1_epi64(BUSTER_LINK_SOURCE_LINKER)) &
                                   _mm512_cmpneq_epi64_mask(sources, _mm512_set1_epi64(BUSTER_LINK_SOURCE_ABSOLUTE)) & loaded;
        SymbolId candidates[BUSTER_BIND_BATCH];
        _mm256_mask_storeu_epi32(candidates, (__mmask8)bind_lane_mask((uint32_t)__builtin_popcount(undefined)),
                                 _mm512_cvtepi64_epi32(_mm512_maskz_compress_epi64((__mmask8)undefined, _mm512_add_epi64(lane_index, _mm512_set1_epi64(batch)))));
        for (uint32_t i = 0; i < (uint32_t)__builtin_popcount(undefined); i += 1)
        {
            bind_resolve_symbol(bind, link, candidates[i], members);
        }
    }
}

static void bind_resolve_exact(Bind* bind, Link* link, SymbolId first, SymbolId end, bool members)
{
    const LinkSymbols* symbols = &link->symbols;
    for (SymbolId symbol = first; symbol < end; symbol += 1)
    {
        const uint8_t source = symbols->source[symbol];
        if (symbols->atom[symbol] == 0 && symbols->name[symbol] != 0 && source != BUSTER_LINK_SOURCE_PROGRAM && source != BUSTER_LINK_SOURCE_LINKER &&
            source != BUSTER_LINK_SOURCE_ABSOLUTE)
        {
            bind_resolve_symbol(bind, link, symbol, members);
        }
    }
}

static void bind_resolve(Bind* bind, Link* link, SymbolId first, SymbolId end, bool members)
{
    if (bind->path == BIND_PATH_VECTOR)
    {
        bind_resolve_vector(bind, link, first, end, members);
    }
    else
    {
        bind_resolve_exact(bind, link, first, end, members);
    }
}

// Binds the symbols from `first` on, then joins the members they need and binds those members' symbols, round after
// round, until no member joins.
static void bind_members(Bind* bind, Link* link, SymbolId first)
{
    const Runtime* runtime = bind->runtime;
    const uint32_t words = (runtime->member_count + 63) / 64;
    SymbolId from = first;
    bool joined = true;
    while (joined && bind->error == BIND_ERROR_NONE)
    {
        const SymbolId to = link->symbols.count;
        bind_resolve(bind, link, from, to, true);
        bind->resolved = to;
        joined = false;
        for (uint32_t w = 0; bind->error == BIND_ERROR_NONE && w < words; w += 1)
        {
            uint64_t pending = bind->members_needed[w] & ~bind->members_joined[w];
            bind->members_joined[w] |= pending;
            while (pending != 0 && bind->error == BIND_ERROR_NONE)
            {
                const uint32_t m = w * 64 + (uint32_t)__builtin_ctzll(pending);
                pending &= pending - 1;
                bind->member_symbols = bind->member_symbols == 0 ? link->symbols.count : bind->member_symbols;
                bind_join(bind, link, RUNTIME_OBJECTS + m);
                joined = true;
            }
        }
        from = to;
    }
}

// The imports that are still imports: a member that joined may have defined a name libc.so.6 exports too, which its
// definition takes, as GNU ld lets an object's definition take a name from a shared library.
static void bind_keep_imports(Bind* bind, const Link* link)
{
    uint32_t kept = 0;
    for (uint32_t i = 0; i < bind->import_count; i += 1)
    {
        const SymbolId symbol = bind->imports[i];
        bind->imports[kept] = symbol;
        kept += link->symbols.source[symbol] == BUSTER_LINK_SOURCE_RUNTIME_SHARED ? 1 : 0;
    }
    bind->import_count = kept;
}

// The imports of a library the driver names as needed: the loader, which libc.so's script names AS_NEEDED, or
// libgcc_s.so.1 (-lgcc_s_asneeded). GNU ld and lld need such a library only for a strong reference that binds to it,
// and GNU ld also for a needed named library's that its own DT_NEEDED doesn't cover (`forced`, Bind.runtime_needed).
// Without one, the weak references to its names are left to nothing, unversioned (source NONE), and drop out of the
// imports, keeping its export's type: GNU ld and lld write them so, OBJECT WEAK UND for _r_debug, lld demoting them in
// demoteSymbolsAndComputeIsPreemptible. With one, every reference to its names is an import at its version, the weak
// ones among them. An import is the library's by its version, whose index follows libc.so.6's and the other's
// (runtime.c): their exports all have versions, as glibc's and GCC's do. A pass over the imports, which are tens,
// shared by both paths.
static void bind_keep_as_needed(Bind* bind, Link* link, const ImportLibrary* library, bool forced)
{
    LinkSymbols* symbols = &link->symbols;
    // runtime_move_versions gives each library a disjoint range; gaps cannot contain another library's version.
    uint16_t first = UINT16_MAX;
    uint16_t last = 0;
    for (uint32_t v = 0; v < library->version_count; v += 1)
    {
        const uint16_t version = library->versions[v].index;
        first = version < first ? version : first;
        last = version > last ? version : last;
    }
    bool needed = forced;
    for (uint32_t i = 0; i < bind->import_count; i += 1)
    {
        const SymbolId symbol = bind->imports[i];
        const bool strong = (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_GLOBAL;
        const uint16_t version = symbols->version[symbol];
        needed = needed || (strong && version >= first && version <= last);
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < bind->import_count; i += 1)
    {
        const SymbolId symbol = bind->imports[i];
        const uint16_t version = symbols->version[symbol];
        const bool dropped = !needed && version >= first && version <= last;
        symbols->source[symbol] = dropped ? BUSTER_LINK_SOURCE_NONE : symbols->source[symbol];
        symbols->version[symbol] = dropped ? 0 : symbols->version[symbol];
        bind->imports[kept] = symbol;
        kept += dropped ? 0 : 1;
    }
    bind->import_count = kept;
}

// The link's definitions of names that libc.so.6, the loader or libgcc_s.so.1 defines too, or libc.so.6 refers to, with
// default or protected visibility: among the runtime's names that took a link symbol, in the order they took it.
static void bind_find_exports(Bind* bind, const Link* link)
{
    const RuntimeNames* names = &bind->runtime->global_names;
    const LinkSymbols* symbols = &link->symbols;
    for (uint32_t i = 0; bind->error == BIND_ERROR_NONE && i < bind->named_count; i += 1)
    {
        const uint32_t e = bind->named[i];
        const SymbolId symbol = bind->name_symbols[e];
        const bool defined = symbol != 0 && symbols->atom[symbol] != 0;
        const bool visible = defined && bind_visibility_rank(symbols->flags[symbol]) <= 1;
        if (visible && (names->export[e] != 0 || names->referenced[e]))
        {
            bind->exports[bind->export_count] = symbol;
            bind->export_count += 1;
        }
    }
}

// Whether each of 16 symbols' flags, atoms and sources make it a shared library's export: global or weak, with default
// or protected visibility, and defined, in an atom or absolutely.
static __mmask16 bind_exported_lanes(__m512i flags, __m512i atoms, __m512i sources)
{
    const __m512i binding = _mm512_and_si512(flags, _mm512_set1_epi32(BUSTER_LINK_SYMBOL_BINDING));
    const __m512i visibility = _mm512_and_si512(flags, _mm512_set1_epi32(BUSTER_LINK_SYMBOL_VISIBILITY));
    const __mmask16 global = _mm512_cmpneq_epi32_mask(binding, _mm512_set1_epi32(BUSTER_LINK_SYMBOL_LOCAL));
    const __mmask16 visible = _mm512_cmpeq_epi32_mask(visibility, _mm512_set1_epi32(BUSTER_LINK_SYMBOL_DEFAULT)) |
                              _mm512_cmpeq_epi32_mask(visibility, _mm512_set1_epi32(BUSTER_LINK_SYMBOL_PROTECTED));
    const __mmask16 defined = _mm512_test_epi32_mask(atoms, atoms) | _mm512_cmpeq_epi32_mask(sources, _mm512_set1_epi32(BUSTER_LINK_SOURCE_ABSOLUTE));
    return global & visible & defined;
}

// A shared library's exports (Bind.exports): every definition of the link's that is global or weak, with default or
// protected visibility, in an atom or absolute, as GNU ld and lld export them with -shared: the program's, since the
// crt objects and libc_nonshared.a's members define only hidden names, and so do libgcc.a's but its decimal
// floating-point and split-stack members, whose definitions GNU ld and lld export too. In symbol order: by the vector
// path 16 symbols at a time, compressed from their flags, atoms and sources, by the exact one a symbol at a time.
static void bind_shared_exports(Bind* bind, const Link* link)
{
    // The columns and the count in locals: with the build's -fno-strict-aliasing, each store into the exports would make
    // clang load them from the structures again.
    const uint8_t* flags_column = link->symbols.flags;
    const AtomId* atom_column = link->symbols.atom;
    const uint8_t* source_column = link->symbols.source;
    const uint32_t count = link->symbols.count;
    SymbolId* exports = bind->exports;
    uint32_t export_count = bind->export_count;
    if (bind->path == BIND_PATH_VECTOR)
    {
        const __m512i lane_index = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
        for (SymbolId batch = 1; batch < count; batch += 16)
        {
            const uint32_t n = count - batch < 16 ? count - batch : 16;
            const __mmask16 loaded = (__mmask16)((1u << n) - 1);
            const __m512i flags = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(loaded, flags_column + batch));
            const __m512i atoms = _mm512_maskz_loadu_epi32(loaded, atom_column + batch);
            const __m512i sources = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(loaded, source_column + batch));
            const __mmask16 exported = bind_exported_lanes(flags, atoms, sources) & loaded;
            // Compressed in a register and stored with a mask of its count.
            const uint32_t taken = (uint32_t)__builtin_popcount(exported);
            const __m512i ids = _mm512_maskz_compress_epi32(exported, _mm512_add_epi32(lane_index, _mm512_set1_epi32((int)batch)));
            _mm512_mask_storeu_epi32(exports + export_count, (__mmask16)((1u << taken) - 1), ids);
            export_count += taken;
        }
    }
    else
    {
        for (SymbolId symbol = 1; symbol < count; symbol += 1)
        {
            const uint8_t flags = flags_column[symbol];
            const bool global = (flags & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL;
            const bool defined = atom_column[symbol] != 0 || source_column[symbol] == BUSTER_LINK_SOURCE_ABSOLUTE;
            if (global && bind_visibility_rank(flags) <= 1 && defined)
            {
                exports[export_count] = symbol;
                export_count += 1;
            }
        }
    }
    bind->export_count = export_count;
}

// The edges of the closure over the libraries the link names with -l (bind_needed_libraries): each a needed library's
// strong reference to a name that another named library exports, the first on the line that does, unless the
// referring library's own DT_NEEDED names that library, which the loader then loads for it; and each one to a name no
// named library, libc.so.6, the loader or libgcc_s.so.1 exports, which leads to no library (BUSTER_BIND_NO_LIBRARY):
// only the executable's own definition can satisfy it; and each one that the loader or libgcc_s.so.1 satisfies, which the
// driver names as needed, unless the referring library's DT_NEEDED names it: the edge leads to that library
// (BUSTER_BIND_LOADER, BUSTER_BIND_GCC_S), which GNU ld then keeps for the library's sake (its --as-needed counts a
// needed library's references), where lld drops it and the program fails to load. By edge: the referring library, its reference, the library it would
// need, the name's hash, and whether the link's own definition of the name satisfies it, a byte; with the edges by
// their names' hashes (import_index_build) and a filter of them for the vector path, as RuntimeNames' filter: two bits
// a name in 2^filter_bits words, chosen by the hash's top bits.
#define BUSTER_BIND_NO_LIBRARY UINT32_MAX
#define BUSTER_BIND_LOADER (UINT32_MAX - 1)
#define BUSTER_BIND_GCC_S (UINT32_MAX - 2)
#define BUSTER_BIND_NEEDS_LOADER 1u
#define BUSTER_BIND_NEEDS_GCC_S 2u

typedef struct BindLibraryEdges BindLibraryEdges;
struct BindLibraryEdges
{
    uint32_t* from;
    uint32_t* reference;
    uint32_t* to;
    uint64_t* hash;
    uint8_t* satisfied;
    uint32_t count;
    ImportNameIndex index;
    uint64_t* filter;
    uint32_t filter_bits;
};

// Adds the edges of library `l`'s strong references and returns the libraries they would need, a bit each.
static uint32_t bind_library_edges(const Bind* bind, uint32_t l, BindLibraryEdges* edges)
{
    const RuntimeLibraries* libraries = bind->libraries;
    const RuntimeNames* names = &bind->runtime->global_names;
    const ImportReferences* references = &libraries->libraries[l].references;
    uint32_t result = 0;
    for (uint32_t r = 0; r < references->count; r += 1)
    {
        uint32_t export = 0;
        const bool strong = !references->weak[r];
        // The version the reference requires, if any, which only an export at it satisfies, as GNU ld and lld bind it:
        // of libc.so.6, the loader or libgcc_s.so.1, theirs, unchecked, since the runtime keeps only their default
        // exports and the loader checks it at load; of another library, a named library's.
        const String8 version = libraries->reference_versions[l][r];
        const String8 file = libraries->reference_files[l][r];
        const ImportLibrary* const runtime_libraries[] = { &bind->runtime->libc, &bind->runtime->loader, &bind->runtime->gcc_s };
        const ImportLibrary* runtime_file = 0;
        for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(runtime_libraries); s += 1)
        {
            const String8 soname = runtime_libraries[s]->soname;
            const bool same = version.length != 0 && soname.length != 0 && file.length == soname.length && memcmp(file.pointer, soname.pointer, soname.length) == 0;
            runtime_file = same ? runtime_libraries[s] : runtime_file;
        }
        const bool runtime_version = runtime_file != 0;
        const uint32_t m = !strong || runtime_version ? 0
                           : version.length != 0      ? runtime_libraries_export_at(libraries, references->name[r], references->length[r], references->hash[r], version)
                                                      : runtime_libraries_export(libraries, references->name[r], references->length[r], references->hash[r], &export);
        const bool edge = m != 0 && m - 1 != l && !runtime_libraries_lists(libraries, l, m - 1);
        // An unversioned name no named library exports: libc.so.6's or the loader's, or else the executable's to define;
        // a versioned one no named library exports at its version, the executable's to define.
        const uint32_t entry = strong && m == 0 && version.length == 0 ? runtime_find_global_name(names, references->name[r], references->length[r], references->hash[r]) : 0;
        const bool open = strong && m == 0 && !runtime_version && (version.length != 0 || entry == 0 || names->export[entry - 1] == 0);
        // The runtime's library that satisfies it, by the file a versioned reference names or by the export of the name:
        // the loader or libgcc_s.so.1, unless the library's own DT_NEEDED names it.
        uint32_t index = 0;
        const ImportLibrary* provider = runtime_file != 0 ? runtime_file
                                        : strong && m == 0 && entry != 0 && names->export[entry - 1] != 0 ? runtime_export_library(bind->runtime, names->export[entry - 1] - 1, &index)
                                                                                                           : 0;
        const bool as_needed = strong && m == 0 && (provider == &bind->runtime->loader || provider == &bind->runtime->gcc_s) &&
                               !runtime_libraries_lists_name(libraries, l, provider->soname);
        edges->from[edges->count] = l;
        edges->reference[edges->count] = r;
        edges->to[edges->count] = edge ? m - 1 : as_needed ? (provider == &bind->runtime->loader ? BUSTER_BIND_LOADER : BUSTER_BIND_GCC_S) : BUSTER_BIND_NO_LIBRARY;
        edges->hash[edges->count] = references->hash[r];
        edges->satisfied[edges->count] = 0;
        edges->count += edge || open || as_needed ? 1 : 0;
        result |= edge ? 1u << (m - 1) : 0;
    }
    return result;
}

// Whether every library that library `l`'s DT_NEEDED entries name is in the link, a named library, libc.so.6, the
// loader or libgcc_s.so.1, so that the definitions its references may bind to are all the link's to see.
static bool bind_library_dependencies_known(const Bind* bind, uint32_t l)
{
    const RuntimeLibraries* libraries = bind->libraries;
    const String8 runtime[] = { bind->runtime->libc.soname, bind->runtime->loader.soname, bind->runtime->gcc_s.soname };
    bool known = true;
    for (uint32_t d = 0; known && d < libraries->dependency_count[l]; d += 1)
    {
        const String8 dependency = libraries->dependencies[l][d];
        bool found = false;
        for (uint32_t m = 0; !found && m < libraries->count + BUSTER_ARRAY_LENGTH(runtime); m += 1)
        {
            const String8 name = m < libraries->count ? runtime_libraries_name(libraries, m) : runtime[m - libraries->count];
            found = dependency.length == name.length && memcmp(dependency.pointer, name.pointer, name.length) == 0;
        }
        known = found;
    }
    return known;
}

// Marks the edges whose reference names the name of `symbol`, a definition the executable exports: but for a reference
// that requires a version, which the executable's definitions, without one, don't satisfy, as GNU ld and lld refuse
// them.
static void bind_library_satisfy(const RuntimeLibraries* libraries, const Link* link, SymbolId symbol, BindLibraryEdges* edges)
{
    const NameId name = link->symbols.name[symbol];
    const uint8_t* bytes = link->names.bytes[name];
    const uint32_t length = link->names.length[name];
    const uint64_t hash = link->names.hash[name];
    uint32_t begin = 0;
    uint32_t end = 0;
    import_index_bucket(&edges->index, hash, &begin, &end);
    for (uint32_t place = begin; place < end; place += 1)
    {
        const uint32_t e = edges->index.order[place];
        const ImportReferences* references = &libraries->libraries[edges->from[e]].references;
        const uint32_t r = edges->reference[e];
        const bool same = edges->index.hash[place] == hash && references->length[r] == length && import_same_bytes(references->name[r], bytes, length);
        edges->satisfied[e] |= same && libraries->reference_versions[edges->from[e]][r].length == 0 ? 1 : 0;
    }
}

// The vector path's scan: 16 symbols at a time, the definitions the executable exports among them (bind_exported_lanes)
// with a name, their names' hashes gathered eight at a time, and their words of the edges' filter gathered by the
// hashes' top bits. Only a symbol whose two bits are both set, of a name an edge has or at most about 1.6% of the
// others, goes on to the index (bind_library_satisfy), as bind_match_vector takes the runtime's names.
static void bind_library_satisfied_vector(const RuntimeLibraries* libraries, const Link* link, BindLibraryEdges* edges)
{
    const LinkSymbols* symbols = &link->symbols;
    const __m512i ones = _mm512_set1_epi64(1);
    const __m512i low6 = _mm512_set1_epi64(63);
    for (SymbolId batch = 1; batch < symbols->count; batch += 16)
    {
        const uint32_t n = symbols->count - batch < 16 ? symbols->count - batch : 16;
        const __mmask16 loaded = (__mmask16)((1u << n) - 1);
        const __m512i flags = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(loaded, symbols->flags + batch));
        const __m512i atoms = _mm512_maskz_loadu_epi32(loaded, symbols->atom + batch);
        const __m512i sources = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(loaded, symbols->source + batch));
        const __m512i names = _mm512_maskz_loadu_epi32(loaded, symbols->name + batch);
        const __mmask16 candidates = bind_exported_lanes(flags, atoms, sources) & _mm512_test_epi32_mask(names, names) & loaded;
        uint32_t maybe = 0;
        for (uint32_t half = 0; half < 2; half += 1)
        {
            const __mmask8 lanes = (__mmask8)(candidates >> (8 * half));
            const __m256i name = half == 0 ? _mm512_castsi512_si256(names) : _mm512_extracti64x4_epi64(names, 1);
            const __m512i hash = _mm512_mask_i32gather_epi64(_mm512_setzero_si512(), lanes, name, link->names.hash, 8);
            const __m512i word_index = _mm512_srli_epi64(hash, 64 - edges->filter_bits);
            const __m512i word = _mm512_mask_i64gather_epi64(_mm512_setzero_si512(), lanes, word_index, edges->filter, 8);
            const __m512i bits = _mm512_or_si512(_mm512_sllv_epi64(ones, _mm512_and_si512(hash, low6)), _mm512_sllv_epi64(ones, _mm512_and_si512(_mm512_srli_epi64(hash, 6), low6)));
            maybe |= (uint32_t)_mm512_mask_cmpeq_epi64_mask(lanes, _mm512_and_si512(word, bits), bits) << (8 * half);
        }
        while (maybe != 0)
        {
            bind_library_satisfy(libraries, link, batch + (SymbolId)__builtin_ctz(maybe), edges);
            maybe &= maybe - 1;
        }
    }
}

// The exact path's scan, its twin: each symbol's flags, atom, source and name tested alone, and each definition the
// executable exports looked up in the index, without the filter.
static void bind_library_satisfied_exact(const RuntimeLibraries* libraries, const Link* link, BindLibraryEdges* edges)
{
    const LinkSymbols* symbols = &link->symbols;
    for (SymbolId symbol = 1; symbol < symbols->count; symbol += 1)
    {
        const uint8_t flags = symbols->flags[symbol];
        const bool global = (flags & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL;
        const bool defined = symbols->atom[symbol] != 0 || symbols->source[symbol] == BUSTER_LINK_SOURCE_ABSOLUTE;
        if (global && bind_visibility_rank(flags) <= 1 && defined && symbols->name[symbol] != 0)
        {
            bind_library_satisfy(libraries, link, symbol, edges);
        }
    }
}

// Marks the edges that the link's own definitions satisfy (#398's review): a definition the executable exports (global
// or weak, with default or protected visibility, in an atom or absolute: bind_exported_lanes) of the reference's name,
// which the loader binds the library's reference to (bind_library_exports exports it), so that GNU ld and lld need no
// library for it, nor find it undefined. A hidden or internal one satisfies none, since the loader can't see it: GNU
// ld refuses such a link, lld refuses it without the library that exports the name and drops that library from it
// otherwise, so that the program fails to load; binding keeps the library, whose definition the loader then binds the
// reference to, or without one refuses the link (BIND_ERROR_LIBRARY_UNDEFINED). By the vector path or the exact one
// over every symbol of the link, after the index and the filter of the edges' names are built in `arena`. False if
// they don't fit.
static bool bind_library_satisfied(Bind* bind, const Link* link, BindLibraryEdges* edges, Arena* arena)
{
    // As RuntimeNames' filter: a word per four names at most, a power of two of them and 16 at least.
    uint32_t filter_bits = 4;
    while (((uint64_t)1 << filter_bits) * 4 < edges->count)
    {
        filter_bits += 1;
    }
    edges->filter_bits = filter_bits;
    edges->filter = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)1 << filter_bits);
    const bool fits = edges->filter != 0 && import_index_build(&edges->index, edges->hash, edges->count, arena);
    if (fits)
    {
        memset(edges->filter, 0, sizeof(uint64_t) << filter_bits);
        for (uint32_t e = 0; e < edges->count; e += 1)
        {
            const uint64_t hash = edges->hash[e];
            edges->filter[hash >> (64 - filter_bits)] |= (UINT64_C(1) << (hash & 63)) | (UINT64_C(1) << ((hash >> 6) & 63));
        }
        if (bind->path == BIND_PATH_VECTOR)
        {
            bind_library_satisfied_vector(bind->libraries, link, edges);
        }
        else
        {
            bind_library_satisfied_exact(bind->libraries, link, edges);
        }
    }
    return fits;
}

// The libraries the program loads, a bit each: the `needed` ones, and the named libraries their DT_NEEDED entries name,
// which the loader loads for them, to the closure.
static uint32_t bind_libraries_loaded(const RuntimeLibraries* libraries, uint32_t needed)
{
    uint32_t loaded = needed;
    uint32_t added = needed;
    while (added != 0)
    {
        uint32_t next = 0;
        for (uint32_t l = 0; l < libraries->count; l += 1)
        {
            for (uint32_t m = 0; (added & (1u << l)) != 0 && m < libraries->count; m += 1)
            {
                next |= runtime_libraries_lists(libraries, l, m) ? 1u << m : 0;
            }
        }
        added = next & ~loaded;
        loaded |= added;
    }
    return loaded;
}

// The version needs of the libraries the program loads, checked as glibc's loader checks them when it loads them
// (dl-version.c's _dl_check_map_versions), #398's review: the `needed` libraries, and the named libraries their
// DT_NEEDED entries name, which the loader loads for them. Each auxiliary entry of a library's .gnu.version_r requires
// its version of the library its entry's file names (vn_file, runtime_libraries_provider): a named library, libc.so.6
// or the loader, which must define it (runtime_library_defines_version), unless it defines no versions at all, or the
// need is weak (VER_FLG_WEAK), both of which the loader only warns about. A file the link doesn't have is left
// unchecked, as the references of a library whose DT_NEEDED names one are (bind_needed_libraries). The first that fails,
// in the libraries' order and each one's walk, is BIND_ERROR_LIBRARY_VERSION. This is apart from which export a
// versioned reference binds to (runtime_libraries_export_at): the first named library's at that version, whatever the
// file, as GNU ld and lld bind it and the loader looks it up. Neither linker checks the needs, so that both link a
// program the loader refuses ("version `V1' not found"): libA's foo@V1, which libA requires of libB.so, bound to libC's
// foo@V1 against a libB that defines V2 alone. A scalar pass over the loaded libraries' few needs, shared by both paths,
// as the closure's rounds are: libm.so.6 has six, each a version of libc.so.6's or the loader's.
static void bind_library_versions(Bind* bind, uint32_t needed)
{
    const RuntimeLibraries* libraries = bind->libraries;
    const uint32_t loaded = bind_libraries_loaded(libraries, needed);
    for (uint32_t l = 0; bind->error == BIND_ERROR_NONE && l < libraries->count; l += 1)
    {
        const RuntimeVersionNeeds* needs = &libraries->needs[l];
        for (uint32_t n = 0; (loaded & (1u << l)) != 0 && bind->error == BIND_ERROR_NONE && n < needs->count; n += 1)
        {
            uint32_t provider = UINT32_MAX;
            const ImportLibrary* library = needs->weak[n] ? 0 : runtime_libraries_provider(libraries, bind->runtime, needs->file[n], &provider);
            if (library == &bind->runtime->gcc_s)
            {
                bind_check_deferred(bind);
            }
            if (bind->error == BIND_ERROR_NONE && library != 0 && !runtime_library_defines_version(library, needs->name[n], needs->hash[n]))
            {
                bind->library = l;
                bind->need = n;
                bind->provider = provider;
                bind_fail(bind, BIND_ERROR_LIBRARY_VERSION, 0);
            }
        }
    }
}

// The loader and libgcc_s.so.1, a bit each (BUSTER_BIND_NEEDS_LOADER, BUSTER_BIND_NEEDS_GCC_S), that a DT_NEEDED entry
// of a library the program loads (bind_libraries_loaded, from the `needed` libraries) names, so that the loader loads it
// for that library: GNU ld's --as-needed keeps neither for another needed library's reference then (#511's review).
static uint32_t bind_runtime_covered(const Bind* bind, uint32_t needed)
{
    const RuntimeLibraries* libraries = bind->libraries;
    const uint32_t loaded = bind_libraries_loaded(libraries, needed);
    uint32_t covered = 0;
    for (uint32_t l = 0; l < libraries->count; l += 1)
    {
        const bool in = (loaded & (1u << l)) != 0;
        covered |= in && runtime_libraries_lists_name(libraries, l, bind->runtime->loader.soname) ? BUSTER_BIND_NEEDS_LOADER : 0;
        covered |= in && runtime_libraries_lists_name(libraries, l, bind->runtime->gcc_s.soname) ? BUSTER_BIND_NEEDS_GCC_S : 0;
    }
    return covered;
}

// The libraries the link names with -l that it needs, from Bind.libraries_needed, the ones the program's strong
// references bind to. GNU ld's --as-needed counts a needed library's strong references too: a library is needed for one
// that it satisfies, the first named library that exports the name, unless the referring library's own DT_NEEDED names
// it, which the loader then loads for it, or the link's own definition satisfies the reference (#398's review). GNU ld
// counts a library's references only against the libraries after it on the line, and refuses a link that names them
// the other way round (-lB -lA, libA referring to libB's name); this counts them against every library, so that such a
// link works. The references of libc.so.6 and the loader, which come after every named library on the line, count
// against none. A needed library's strong reference to a name of the loader's or libgcc_s.so.1's, which the driver names
// as needed, makes that library needed too (Bind.runtime_needed), as GNU ld keeps it, unless the referring library's
// own DT_NEEDED names it, or another's that the program loads does (bind_runtime_covered), or the executable's
// definition satisfies the reference: lld drops it, and the program fails to load. A needed library's strong reference that nothing defines, neither a named library, libc.so.6, the
// loader, libgcc_s.so.1 nor the executable, is an error (BIND_ERROR_LIBRARY_UNDEFINED), as GNU ld and lld refuse it
// (--no-allow-shlib-undefined), where the program would fail to load (#398's review). Then the version needs of the
// libraries the program loads (bind_library_versions).
//
// The closure goes in rounds, each over the references of the libraries the round before added, at most as many as
// libraries, which records the edges it follows (BindLibraryEdges), and the references that lead to no library. Both
// are rare: -lm has none, libm's 16 references being libc.so.6's and the loader's names, which no named library exports,
// and libmvec's 59 those and libm's, which its DT_NEEDED covers. With any, one scan of the link marks those its
// definitions satisfy, and the closure goes again from the program's libraries over the others: every library it
// reaches the first rounds reached, with every edge recorded. The rounds are a scalar pass over the needed libraries'
// references, a few hundred for libm, shared by both paths, and the scan the paths' own.
static void bind_needed_libraries(Bind* bind, const Link* link)
{
    const RuntimeLibraries* libraries = bind->libraries;
    const uint32_t program = bind->libraries_needed;
    uint32_t needed = program;
    if (libraries != 0 && program != 0)
    {
        Scratch scratch = scratch_begin(bind->bytes);
        uint64_t references = 0;
        for (uint32_t l = 0; l < libraries->count; l += 1)
        {
            references += libraries->libraries[l].references.count;
        }
        BindLibraryEdges edges = {
            .from = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, references + 1),
            .reference = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, references + 1),
            .to = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, references + 1),
            .hash = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, references + 1),
            .satisfied = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, references + 1),
        };
        bool fits = edges.from != 0 && edges.reference != 0 && edges.to != 0 && edges.hash != 0 && edges.satisfied != 0;
        uint32_t added = fits ? needed : 0;
        while (added != 0)
        {
            uint32_t next = 0;
            for (uint32_t l = 0; l < libraries->count; l += 1)
            {
                next |= (added & (1u << l)) != 0 ? bind_library_edges(bind, l, &edges) : 0;
            }
            added = next & ~needed;
            needed |= added;
        }
        fits = fits && (edges.count == 0 || bind_library_satisfied(bind, link, &edges, scratch.arena));
        needed = edges.count != 0 ? program : needed;
        added = fits && edges.count != 0 ? program : 0;
        while (added != 0)
        {
            uint32_t next = 0;
            for (uint32_t e = 0; e < edges.count; e += 1)
            {
                const bool leads = (added & (1u << edges.from[e])) != 0 && edges.satisfied[e] == 0 && edges.to[e] < libraries->count;
                next |= leads ? 1u << edges.to[e] : 0;
            }
            added = next & ~needed;
            needed |= added;
        }
        if (!fits)
        {
            bind_fail(bind, BIND_ERROR_FULL, 0);
        }
        // A needed library's strong reference that nothing defines, the first in the rounds' order, as GNU ld and lld
        // refuse it (--no-allow-shlib-undefined): unless the library's DT_NEEDED names a library the link doesn't have,
        // whose definitions it can't see, which lld leaves unchecked too (GNU ld searches the system's directories for
        // it and checks the reference against it).
        // And the loader and libgcc_s.so.1, for a needed library's reference that the executable doesn't satisfy.
        for (uint32_t e = 0; bind->error == BIND_ERROR_NONE && e < edges.count; e += 1)
        {
            const uint32_t l = edges.from[e];
            const bool live = edges.satisfied[e] == 0 && (needed & (1u << l)) != 0;
            bind->runtime_needed |= live && edges.to[e] == BUSTER_BIND_LOADER ? BUSTER_BIND_NEEDS_LOADER : 0;
            bind->runtime_needed |= live && edges.to[e] == BUSTER_BIND_GCC_S ? BUSTER_BIND_NEEDS_GCC_S : 0;
            const bool open = edges.to[e] == BUSTER_BIND_NO_LIBRARY && live;
            if (open && bind_library_dependencies_known(bind, l))
            {
                bind->library = l;
                bind->reference = edges.reference[e];
                bind_fail(bind, BIND_ERROR_LIBRARY_UNDEFINED, 0);
            }
        }
        // But not one that a library the program loads already names in its DT_NEEDED, so that the loader loads it.
        bind->runtime_needed &= ~bind_runtime_covered(bind, needed);
        scratch_end(scratch);
    }
    bind->libraries_needed = needed;
    if (libraries != 0 && needed != 0)
    {
        bind_library_versions(bind, needed);
    }
}

// The imports from libraries named with -l that the link doesn't need, which only weak references bound to: as GNU ld
// and lld take a library --as-needed, those are left to nothing, unversioned, keeping their export's type, as
// bind_keep_loader leaves the loader's. An import is a library's when one exports its name, since binding looks in the
// libraries first. A pass over the imports, which are tens, shared by both paths.
static void bind_keep_libraries(Bind* bind, Link* link)
{
    LinkSymbols* symbols = &link->symbols;
    bind_needed_libraries(bind, link);
    uint32_t kept = 0;
    for (uint32_t i = 0; i < bind->import_count; i += 1)
    {
        const SymbolId symbol = bind->imports[i];
        const NameId name = symbols->name[symbol];
        uint32_t export = 0;
        const uint32_t library = runtime_libraries_export(bind->libraries, link->names.bytes[name], link->names.length[name], link->names.hash[name], &export);
        const bool dropped = library != 0 && (bind->libraries_needed & (1u << (library - 1))) == 0;
        symbols->source[symbol] = dropped ? BUSTER_LINK_SOURCE_NONE : symbols->source[symbol];
        symbols->version[symbol] = dropped ? 0 : symbols->version[symbol];
        bind->imports[kept] = symbol;
        kept += dropped ? 0 : 1;
    }
    bind->import_count = kept;
}

// The link's definitions of names that a library the link needs (Bind.libraries) defines too or refers to, global or
// weak with default or protected visibility, which the executable exports, as GNU ld and lld export a definition that a
// shared library on the line defines or refers to: after binding's other exports, each once, in symbol order. The
// candidates by the vector path 16 symbols at a time (bind_exported_lanes), by the exact one a symbol at a time; each
// then looked up in the libraries' exports and references, a scalar pass both share, run only with -l libraries.
static void bind_library_exports(Bind* bind, const Link* link)
{
    const LinkSymbols* symbols = &link->symbols;
    const RuntimeLibraries* libraries = bind->libraries;
    const uint32_t found = bind->export_count;
    const __m512i lane_index = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    for (SymbolId batch = 1; libraries != 0 && bind->libraries_needed != 0 && bind->error == BIND_ERROR_NONE && batch < symbols->count; batch += 16)
    {
        const uint32_t n = symbols->count - batch < 16 ? symbols->count - batch : 16;
        SymbolId candidates[16];
        uint32_t taken = 0;
        if (bind->path == BIND_PATH_VECTOR)
        {
            const __mmask16 loaded = (__mmask16)((1u << n) - 1);
            const __m512i flags = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(loaded, symbols->flags + batch));
            const __m512i atoms = _mm512_maskz_loadu_epi32(loaded, symbols->atom + batch);
            const __m512i sources = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(loaded, symbols->source + batch));
            const __mmask16 exported = bind_exported_lanes(flags, atoms, sources) & loaded;
            taken = (uint32_t)__builtin_popcount(exported);
            _mm512_mask_storeu_epi32(candidates, (__mmask16)((1u << taken) - 1), _mm512_maskz_compress_epi32(exported, _mm512_add_epi32(lane_index, _mm512_set1_epi32((int)batch))));
        }
        else
        {
            for (SymbolId symbol = batch; symbol < batch + n; symbol += 1)
            {
                const uint8_t flags = symbols->flags[symbol];
                const bool global = (flags & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL;
                const bool defined = symbols->atom[symbol] != 0 || symbols->source[symbol] == BUSTER_LINK_SOURCE_ABSOLUTE;
                candidates[taken] = symbol;
                taken += global && bind_visibility_rank(flags) <= 1 && defined ? 1 : 0;
            }
        }
        for (uint32_t c = 0; c < taken; c += 1)
        {
            const SymbolId symbol = candidates[c];
            const NameId name = symbols->name[symbol];
            const uint8_t* bytes = link->names.bytes[name];
            const uint32_t length = link->names.length[name];
            const uint64_t hash = link->names.hash[name];
            bool shared = false;
            for (uint32_t l = 0; !shared && name != 0 && l < libraries->count; l += 1)
            {
                const bool needed = (bind->libraries_needed & (1u << l)) != 0;
                shared = needed && (import_library_find(&libraries->libraries[l], bytes, length, hash) != 0 || runtime_libraries_refer(libraries, l, bytes, length, hash));
            }
            bool listed = false;
            for (uint32_t e = 0; shared && !listed && e < found; e += 1)
            {
                listed = bind->exports[e] == symbol;
            }
            if (shared && !listed && bind->export_count < bind->capacity)
            {
                bind->exports[bind->export_count] = symbol;
                bind->export_count += 1;
            }
            else if (shared && !listed)
            {
                bind_fail(bind, BIND_ERROR_FULL, name);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The static closure

// Whether the closure takes a symbol of the link as a definition: one in an atom, or of the program, the linker, an
// absolute one or a foreign library's. References aren't: source NONE, or RUNTIME_SHARED, which the program's
// references to libc.so.6's names may have already, and binding binds again.
static bool bind_symbol_defined(const Link* link, SymbolId symbol)
{
    const uint8_t source = link->symbols.source[symbol];
    return link->symbols.atom[symbol] != 0 || (source != BUSTER_LINK_SOURCE_NONE && source != BUSTER_LINK_SOURCE_RUNTIME_SHARED);
}

// Whether the link defines the runtime's global name `entry` without the archives' members: its link symbol is a
// definition (of the program's, a crt object's before it, or absolute), or the linker defines the name.
static bool bind_entry_defined(const Bind* bind, const Link* link, uint32_t entry)
{
    const SymbolId symbol = bind->name_symbols[entry];
    return (symbol != 0 && bind_symbol_defined(link, symbol)) || bind->runtime->global_names.linker[entry];
}

// The exact path's step: a strong reference to the runtime's global name `entry` loads the member that defines it,
// unless the link defines it or the member is loaded already; a new one goes on the queue, whose new length it returns.
static uint32_t bind_closure_need(Bind* bind, const Link* link, uint32_t entry, uint32_t tail)
{
    const uint32_t member = bind->runtime->global_names.member[entry];
    const uint32_t m = member - 1;
    const bool loads = member != 0 && !bind_entry_defined(bind, link, entry) && (bind->members_needed[m / 64] >> (m % 64) & 1) == 0;
    if (loads)
    {
        bind->members_needed[m / 64] |= 1ull << (m % 64);
        bind->queue[tail] = m;
    }
    return tail + (loads ? 1 : 0);
}

// The closure's exact path (see the orientation comment): the link's strong references, among the runtime's names that
// took a link symbol, then each loaded member's, a member at a time from the queue, one reference at a time.
static void bind_closure_exact(Bind* bind, const Link* link)
{
    const Runtime* runtime = bind->runtime;
    uint32_t tail = 0;
    for (uint32_t i = 0; i < bind->named_count; i += 1)
    {
        const uint32_t entry = bind->named[i];
        const bool strong = (link->symbols.flags[bind->name_symbols[entry]] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_GLOBAL;
        tail = strong ? bind_closure_need(bind, link, entry, tail) : tail;
    }
    for (uint32_t head = 0; head < tail; head += 1)
    {
        const uint32_t piece = RUNTIME_OBJECTS + bind->queue[head];
        for (uint32_t r = runtime->reference_begin[piece]; r < runtime->reference_begin[piece + 1]; r += 1)
        {
            tail = bind_closure_need(bind, link, runtime->references[r], tail);
        }
    }
}

// The most names that the program defines and a member defines too (so that a reference to one doesn't load the member)
// that the vector path's closure takes; with more, the exact path's runs.
#define BUSTER_BIND_BLOCKED 16

// The closure's vector path (see the orientation comment). The link's names, among the runtime's that took a link
// symbol, give the first round: the members their strong references load unless the link defines them, and the names
// the link defines that a member defines too, whose references don't load the member (a few hundred names, by scalar
// loads, since the symbols' flags and sources are bytes). A round takes each member of the frontier, a bit each, and
// its references 16 at a time, the first 16 without a branch (most members have fewer): the members they load
// (Runtime.reference_members, a load rather than a gather of each name's member), but for those names, compressed onto
// the round's list. The list is marked a byte per member after the round, when the stores that wrote it have left
// the store queue: marked at once, each batch's loads would wait on its masked store. Then 64 members at a time, the
// marked bytes as a mask, less the members loaded already, are the next frontier's word, and are cleared.
static void bind_closure_vector(Bind* bind, const Link* link)
{
    const Runtime* runtime = bind->runtime;
    const RuntimeNames* names = &runtime->global_names;
    const uint32_t words = (runtime->member_count + 63) / 64;
    uint32_t* loads = bind->loads;
    uint8_t* marks = bind->marks;
    uint32_t blocked[BUSTER_BIND_BLOCKED];
    uint32_t blocked_count = 0;
    bool overflow = false;
    memset(bind->frontier, 0, words * sizeof(uint64_t));
    for (uint32_t i = 0; i < bind->named_count; i += 1)
    {
        const uint32_t entry = bind->named[i];
        const SymbolId symbol = bind->name_symbols[entry];
        const bool defined = bind_symbol_defined(link, symbol);
        const bool strong = (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_GLOBAL;
        const uint32_t load = names->linker[entry] ? 0 : names->member[entry];
        const uint32_t m = load - 1;
        overflow = overflow || (defined && load != 0 && blocked_count == BUSTER_BIND_BLOCKED);
        if (defined && load != 0 && blocked_count < BUSTER_BIND_BLOCKED)
        {
            blocked[blocked_count] = entry;
            blocked_count += 1;
        }
        else if (!defined && strong && load != 0)
        {
            bind->members_needed[m / 64] |= 1ull << (m % 64);
            bind->frontier[m / 64] |= 1ull << (m % 64);
        }
    }
    __m512i blocked_entries[BUSTER_BIND_BLOCKED];
    for (uint32_t k = 0; k < blocked_count; k += 1)
    {
        blocked_entries[k] = _mm512_set1_epi32((int)blocked[k]);
    }
    const __m512i one = _mm512_set1_epi32(1);
    bool more = !overflow;
    while (more)
    {
        uint32_t count = 0;
        for (uint32_t w = 0; w < words; w += 1)
        {
            for (uint64_t pending = bind->frontier[w]; pending != 0; pending &= pending - 1)
            {
                const uint32_t piece = RUNTIME_OBJECTS + w * 64 + (uint32_t)__builtin_ctzll(pending);
                const uint32_t end = runtime->reference_begin[piece + 1];
                uint32_t r = runtime->reference_begin[piece];
                do
                {
                    const __mmask16 lanes = (__mmask16)_bzhi_u32(0xffff, end - r < 16 ? end - r : 16);
                    const __m512i load = _mm512_maskz_loadu_epi32(lanes, runtime->reference_members + r);
                    __mmask16 loading = _mm512_test_epi32_mask(load, load);
                    if (blocked_count != 0)
                    {
                        const __m512i entries = _mm512_maskz_loadu_epi32(lanes, runtime->references + r);
                        for (uint32_t k = 0; k < blocked_count; k += 1)
                        {
                            loading &= (__mmask16)~_mm512_cmpeq_epi32_mask(entries, blocked_entries[k]);
                        }
                    }
                    _mm512_storeu_si512(loads + count, _mm512_maskz_compress_epi32(loading, _mm512_sub_epi32(load, one)));
                    count += (uint32_t)__builtin_popcount(loading);
                    r += 16;
                } while (r < end);
            }
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            marks[loads[i]] = 1;
        }
        uint64_t any = 0;
        for (uint32_t w = 0; w < words; w += 1)
        {
            const __m512i marked = _mm512_load_si512(marks + 64 * (uint64_t)w);
            const uint64_t next = _mm512_test_epi8_mask(marked, marked) & ~bind->members_needed[w];
            _mm512_store_si512(marks + 64 * (uint64_t)w, _mm512_setzero_si512());
            bind->members_needed[w] |= next;
            bind->frontier[w] = next;
            any |= next;
        }
        more = any != 0;
    }
    // Too many of the names to compare with in registers: the exact path's closure, from the start.
    if (overflow)
    {
        memset(bind->members_needed, 0, words * sizeof(uint64_t));
        bind_closure_exact(bind, link);
    }
}

// A static runtime's members: the closure by the path's way, then each needed member joined in archive order.
static void bind_static_members(Bind* bind, Link* link)
{
    const Runtime* runtime = bind->runtime;
    const uint32_t words = (runtime->member_count + 63) / 64;
    if (bind->path == BIND_PATH_VECTOR)
    {
        bind_closure_vector(bind, link);
    }
    else
    {
        bind_closure_exact(bind, link);
    }
    for (uint32_t w = 0; bind->error == BIND_ERROR_NONE && w < words; w += 1)
    {
        for (uint64_t pending = bind->members_needed[w]; pending != 0 && bind->error == BIND_ERROR_NONE; pending &= pending - 1)
        {
            const uint32_t m = w * 64 + (uint32_t)__builtin_ctzll(pending);
            bind->members_joined[w] |= 1ull << (m % 64);
            bind_join(bind, link, RUNTIME_OBJECTS + m);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Entry points

static LinkCapacity bind_capacity(const Runtime* runtime, uint64_t* name_bytes, uint64_t* atom_bytes)
{
    const Link* link = &runtime->link;
    const LinkCapacity result = {
        .names = link->names.count - 1,
        .atoms = link->atoms.count - 1,
        .symbols = link->symbols.count - 1,
        .fixups = link->fixups.count - 1,
        .wide_addends = link->fixups.wide_count - 1,
        .priorities = link->priorities.count - 1,
    };
    // The import's sums (Runtime.name_room and atom_room), so that no link reads every piece for them.
    *name_bytes = runtime->name_room;
    *atom_bytes = runtime->atom_room;
    return result;
}

// Piece `piece`'s records and its names' and atoms' bytes added to a capacity (bind_capacity_needed), from its marks and
// its atoms' room alone (RuntimePiece).
static void bind_capacity_piece(const Runtime* runtime, uint32_t piece, LinkCapacity* capacity, uint64_t* name_bytes, uint64_t* atom_bytes)
{
    const ImportMark begin = runtime->pieces[piece].begin;
    const ImportMark end = runtime->pieces[piece].end;
    capacity->names += end.names - begin.names;
    capacity->atoms += end.atoms - begin.atoms;
    capacity->symbols += end.symbols - begin.symbols;
    capacity->fixups += end.fixups - begin.fixups;
    capacity->wide_addends += end.wide_addends - begin.wide_addends;
    capacity->priorities += end.priorities - begin.priorities;
    *name_bytes += end.name_bytes - begin.name_bytes;
    *atom_bytes += runtime->pieces[piece].atom_room;
}

static LinkCapacity bind_capacity_needed(const Runtime* runtime, const String8* names, uint32_t count, Arena* scratch, uint64_t* name_bytes, uint64_t* atom_bytes)
{
    const RuntimeNames* global_names = &runtime->global_names;
    const uint32_t member_count = runtime->member_count;
    const uint64_t length = scratch->length;
    uint8_t* marked = arena_push(scratch, (uint64_t)member_count + 1, 1);
    uint32_t* queue = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)member_count + 1);
    LinkCapacity result = { 0 };
    *name_bytes = 0;
    *atom_bytes = 0;
    if (marked == 0 || queue == 0)
    {
        // Without room for the closure, every member's.
        result = bind_capacity(runtime, name_bytes, atom_bytes);
    }
    else
    {
        memset(marked, 0, member_count);
        uint32_t queued = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint8_t* bytes = (const uint8_t*)names[i].pointer;
            const uint32_t entry = runtime_find_global_name(global_names, bytes, (uint32_t)names[i].length, link_hash(bytes, (uint32_t)names[i].length));
            const uint32_t member = entry != 0 ? global_names->member[entry - 1] : 0;
            if (member != 0 && member - 1 < member_count && marked[member - 1] == 0)
            {
                marked[member - 1] = 1;
                queue[queued++] = member - 1;
            }
        }
        // The crt objects' references, then each queued member's, which may queue more.
        for (uint32_t step = 0; step < RUNTIME_OBJECTS + queued; step += 1)
        {
            const uint32_t piece = step < RUNTIME_OBJECTS ? step : RUNTIME_OBJECTS + queue[step - RUNTIME_OBJECTS];
            for (uint32_t r = runtime->reference_begin[piece]; r < runtime->reference_begin[piece + 1]; r += 1)
            {
                const uint32_t member = runtime->reference_members[r];
                if (member != 0 && member - 1 < member_count && marked[member - 1] == 0)
                {
                    marked[member - 1] = 1;
                    queue[queued++] = member - 1;
                }
            }
            bind_capacity_piece(runtime, piece, &result, name_bytes, atom_bytes);
        }
    }
    arena_pop_to(scratch, length);
    return result;
}

BUSTER_LINKER_ENTRY bool bind_begin(Bind* bind, Link* link, const Runtime* runtime, BindPath path, Arena* bytes, uint32_t unit_base, Arena* arena)
{
    const uint32_t words = (runtime->member_count + 63) / 64;
    const uint32_t signature_words = (runtime->group_signatures + 63) / 64;
    *bind = (Bind){
        .runtime = runtime,
        .path = path,
        .bytes = bytes,
        .unit_base = unit_base,
        .program_symbols = link->symbols.count,
        .capacity = link->symbols.capacity,
    };
    const uint32_t override_room = runtime->global_names.count < link->symbols.count ? runtime->global_names.count : link->symbols.count;
    bind->name_symbols = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, runtime->global_names.count);
    bind->named = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, runtime->global_names.count);
    bind->symbols = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, runtime->link.symbols.count);
    bind->members_needed = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, words + 1);
    bind->members_joined = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, words + 1);
    bind->signatures = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, signature_words + 1);
    bind->frontier = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, words + 1);
    bind->loads = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)runtime->reference_begin[RUNTIME_OBJECTS + runtime->member_count] + 16);
    bind->marks = arena_push(arena, 64 * (uint64_t)words + 64, 64);
    bind->queue = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)runtime->member_count + 1);
    bind->imports = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, bind->capacity);
    bind->exports = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, bind->capacity);
    bind->overrides = BUSTER_ARENA_PUSH_ARRAY(arena, BindOverride, override_room);
    const bool pushed = bind->name_symbols != 0 && bind->named != 0 && bind->overrides != 0 && bind->symbols != 0 && bind->members_needed != 0 &&
                        bind->members_joined != 0 && bind->signatures != 0 && bind->frontier != 0 && bind->loads != 0 && bind->marks != 0 &&
                        bind->queue != 0 && bind->imports != 0 && bind->exports != 0;
    if (!pushed)
    {
        bind_fail(bind, BIND_ERROR_FULL, 0);
    }
    else
    {
        memset(bind->name_symbols, 0, runtime->global_names.count * sizeof(SymbolId));
        memset(bind->members_needed, 0, words * sizeof(uint64_t));
        memset(bind->members_joined, 0, words * sizeof(uint64_t));
        memset(bind->signatures, 0, signature_words * sizeof(uint64_t));
        memset(bind->marks, 0, 64 * (uint64_t)words);
        bind_match(bind, link, 1, link->symbols.count);
    }
    const uint32_t head[] = {
        RUNTIME_OBJECT_START,
        RUNTIME_OBJECT_INIT_PROLOGUE,
        RUNTIME_OBJECT_BEGIN,
    };
    for (uint32_t i = 0; bind->error == BIND_ERROR_NONE && i < BUSTER_ARRAY_LENGTH(head); i += 1)
    {
        bind_join(bind, link, head[i]);
    }
    // crtendS.o and crtn.o join last, but binding reads which of their symbols are in atoms before (bind_resolve_symbol).
    const uint32_t tail[] = {
        RUNTIME_OBJECT_END,
        RUNTIME_OBJECT_INIT_EPILOGUE,
    };
    for (uint32_t i = 0; bind->error == BIND_ERROR_NONE && i < BUSTER_ARRAY_LENGTH(tail); i += 1)
    {
        if (!runtime_piece_current(runtime, tail[i]))
        {
            bind_fail(bind, BIND_ERROR_SNAPSHOT, 0);
        }
    }
    bind->symbols_begun = link->symbols.count;
    bind_check_undefined(bind);
    return bind->error == BIND_ERROR_NONE;
}

BUSTER_LINKER_ENTRY bool bind_end(Bind* bind, Link* link)
{
    // The runtime's definitions that took the program's over, now that the program's atoms have joined (Bind.overrides).
    for (uint32_t i = 0; bind->error == BIND_ERROR_NONE && i < bind->override_count; i += 1)
    {
        const BindOverride* override = &bind->overrides[i];
        bind_install(bind, link, bind->name_symbols[override->entry], override->symbol, override->atom);
    }
    if (bind->error == BIND_ERROR_NONE)
    {
        // The global symbols added since bind_begin, then every name nothing defines, with the members they need.
        bind_match(bind, link, bind->symbols_begun, link->symbols.count);
    }
    bind->resolving = true;
    // A static runtime's members all join before anything binds: GNU ld's group scan, then archive order.
    // libgcc.a's and libc_nonshared.a's join round by round, each after the names that need it bind.
    const bool static_runtime = runtime_kind_is_static(bind->runtime->kind);
    bind->bound = !static_runtime;
    if (bind->error == BIND_ERROR_NONE && static_runtime)
    {
        bind_static_members(bind, link);
    }
    bind->bound = true;
    if (bind->error == BIND_ERROR_NONE && static_runtime)
    {
        bind_resolve(bind, link, 1, link->symbols.count, false);
        bind->resolved = link->symbols.count;
    }
    else if (bind->error == BIND_ERROR_NONE)
    {
        bind_members(bind, link, 1);
    }
    const uint32_t tail[] = {
        RUNTIME_OBJECT_END,
        RUNTIME_OBJECT_INIT_EPILOGUE,
    };
    const SymbolId before_tail = link->symbols.count;
    for (uint32_t i = 0; bind->error == BIND_ERROR_NONE && i < BUSTER_ARRAY_LENGTH(tail); i += 1)
    {
        bind_join(bind, link, tail[i]);
    }
    bind->tail_joined = true;
    if (bind->error == BIND_ERROR_NONE)
    {
        bind_resolve(bind, link, before_tail, link->symbols.count, false);
        bind->resolved = link->symbols.count;
    }
    bind_keep_imports(bind, link);
    bind_keep_libraries(bind, link);
    if (bind->error == BIND_ERROR_NONE && (bind->runtime_needed & BUSTER_BIND_NEEDS_GCC_S) != 0)
    {
        bind_check_deferred(bind);
    }
    bind_keep_as_needed(bind, link, &bind->runtime->loader, (bind->runtime_needed & BUSTER_BIND_NEEDS_LOADER) != 0);
    bind_keep_as_needed(bind, link, &bind->runtime->gcc_s, (bind->runtime_needed & BUSTER_BIND_NEEDS_GCC_S) != 0);
    if (bind->error == BIND_ERROR_NONE && bind->runtime->kind == RUNTIME_KIND_SHARED)
    {
        bind_shared_exports(bind, link);
    }
    else
    {
        bind_find_exports(bind, link);
        bind_library_exports(bind, link);
    }
    bind_check_undefined(bind);
    return bind->error == BIND_ERROR_NONE;
}

// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 0's inlining of always_inline functions (#1398): a caller's typed stream with its calls of inline callees spliced
// in, the callees' entries copied in place of each call and their locals, calls, operations and constants appended to
// the caller's tables, so that an x86 intrinsic's wrapper costs its operation rather than a call. docs/tier0-inlining.md
// has the design. Include after tier0.c; every function here is static.
//
// The splice reads a caller's stream F and, per call row, the stream of the callee it inlines (or none), and writes a
// new stream G that tier0_validate accepts or the caller keeps F. It knows nothing of C: every entry's value has one
// meaning per kind and class (tier0.c, "The kinds"), and a copied entry is rebased by its meaning's base
// (tier0_inline_meanings).
//
// A site is a CALL whose row names a callee, with as many arguments as the callee has parameters, not variadic, whose
// result the callee returns the way the call takes it. Each argument is bound to its parameter right after its own
// evaluation, before the next argument's first entry (the argument's start, found by tier0_inline_starts), and the last
// one at the call: binding them all at the call would put a SET_LOCAL of one class after another of another, whose
// operand's class Tier 0 reads from the entry before (tier0.c's "the operand"). A value takes SET_LOCAL; an aggregate,
// whose value is its address, a LOAD of a vector of its bytes and a SET_LOCAL of them into the parameter's object where
// its size is a vector's that the transport has (an intrinsic's __m256i), else a COPY of its bytes through one U64
// temporary of the caller's (`t`). A COPY moves 16 bytes at a time, and a vector's next read of the object takes them
// whole: on Zen 5 a load that two stores wrote isn't forwarded, which made #1398's kernels 8 times slower than their
// calls. The callee's entries follow, but its last, a RETURN, whose operand the call's result then is; an aggregate
// result in registers goes into the call's result object the same way. With --tier0-inline-shapes (step 5, off by
// default), a RETURN before it is a JUMP, or with a value a JUMP_VALUE, to the copy's end, which finds the value where
// the last RETURN's operand falls through, and a result in memory is written through the call's object's address.
//
// A callee's locals are appended once per caller and level: a site inside another site's arguments, of the same
// callee, after that one's first binding, takes the next level's copy, since both sites' locals are live at once.
// Its calls' rows are appended once per site, its operations and constants once per caller.
//
// tier0_inline is AVX-512 throughout the entries: the sites from a compare of 64 kinds at a time, the starts by an
// in-register prefix sum, the caller's segments copied 64 kinds and 8 values at a time with their branch targets
// remapped lane by lane, and the callee's entries rebased by a vpermb of their kinds to meanings, a vpermq of the bases
// by meaning and an add. The plan's other parts (the sites' arguments, levels and insertions) are per site, not per
// entry (#2924: the scalar twin went with the 9700X's gate, 1.09 to 1.23 times the vector path's ticks at every size).
//
// Layout: the meanings (tier0_inline_meaning), the callees' rule (tier0_inline_callee_valid), the plan
// (Tier0InlinePlan, tier0_inline_plan), the fill (tier0_inline_segment, tier0_inline_body) and the driver
// (tier0_inline).

// A site's levels at most: a deeper nesting of one callee's calls in its own arguments keeps the deeper calls.
#define BUSTER_TIER0_INLINE_LEVELS 8
// An aggregate argument or result copied by a COPY, at most (tier0.c's COPY).
#define BUSTER_TIER0_INLINE_COPY_BYTES BUSTER_TIER0_MAX_COPY_BYTES
// How a binding moves its argument, or the result: none (no result), a value (SET_LOCAL), a vector of an aggregate's
// bytes (LOAD, SET_LOCAL), or a copy of them (SET_LOCAL t, LOCAL_ADDRESS p, LOCAL t, COPY): its kind is its entries.
#define BUSTER_TIER0_INLINE_BIND_NONE 0u
#define BUSTER_TIER0_INLINE_BIND_VALUE 1u
#define BUSTER_TIER0_INLINE_BIND_VECTOR 2u
#define BUSTER_TIER0_INLINE_BIND_COPY 4u
// One local home per general or XMM register. The sparse loop-home list uses their combined register set.
#define BUSTER_TIER0_INLINE_REGISTER_HOMES (BUSTER_TIER0_XMM(0) + BUSTER_TIER0_FLOATING_DEPTHS)

// What an entry's value means, which the copy of a callee's entry rebases: nothing, a local, an entry (a target), a call
// row, an operation, an x87 constant, a vector constant, or an ATOMIC_CAS's local in its high bits.
#define BUSTER_TIER0_INLINE_NONE 0
#define BUSTER_TIER0_INLINE_LOCAL 1
#define BUSTER_TIER0_INLINE_ENTRY 2
#define BUSTER_TIER0_INLINE_CALL 3
#define BUSTER_TIER0_INLINE_OPERATION 4
#define BUSTER_TIER0_INLINE_X87 5
#define BUSTER_TIER0_INLINE_VECTOR 6
#define BUSTER_TIER0_INLINE_CAS 7
#define BUSTER_TIER0_INLINE_MEANINGS 8

// The meanings by kind (a CONST's is its class's: tier0_inline_meaning); one ZMM register for vpermb.
// A copy or fill of a function's column, which an empty column may leave without storage: memcpy's and memset's pointers
// must not be null even for no bytes.
static void tier0_inline_copy(void* destination, const void* source, uint64_t bytes)
{
    if (bytes != 0)
    {
        memcpy(destination, source, bytes);
    }
}

static void tier0_inline_fill(void* destination, int value, uint64_t bytes)
{
    if (bytes != 0)
    {
        memset(destination, value, bytes);
    }
}

static const uint8_t tier0_inline_meanings[BUSTER_TYPED_KIND_LIMIT] = {
    [BUSTER_TYPED_LOCAL] = BUSTER_TIER0_INLINE_LOCAL,
    [BUSTER_TYPED_LOCAL_ADDRESS] = BUSTER_TIER0_INLINE_LOCAL,
    [BUSTER_TYPED_STORE_LOCAL] = BUSTER_TIER0_INLINE_LOCAL,
    [BUSTER_TYPED_SET_LOCAL] = BUSTER_TIER0_INLINE_LOCAL,
    [BUSTER_TYPED_BRANCH_FALSE] = BUSTER_TIER0_INLINE_ENTRY,
    [BUSTER_TYPED_BRANCH_TRUE] = BUSTER_TIER0_INLINE_ENTRY,
    [BUSTER_TYPED_JUMP] = BUSTER_TIER0_INLINE_ENTRY,
    [BUSTER_TYPED_JUMP_VALUE] = BUSTER_TIER0_INLINE_ENTRY,
    [BUSTER_TYPED_CALL] = BUSTER_TIER0_INLINE_CALL,
    [BUSTER_TYPED_X86] = BUSTER_TIER0_INLINE_OPERATION,
    [BUSTER_TYPED_ATOMIC_CAS] = BUSTER_TIER0_INLINE_CAS,
};

// The meaning of an entry of kind `kind` and class `class`, by a switch, for the forwarding check and pass (the fill reads the table).
static uint32_t tier0_inline_meaning(uint32_t kind, uint32_t class)
{
    uint32_t meaning = BUSTER_TIER0_INLINE_NONE;
    switch (kind)
    {
        case BUSTER_TYPED_LOCAL:
        case BUSTER_TYPED_LOCAL_ADDRESS:
        case BUSTER_TYPED_STORE_LOCAL:
        case BUSTER_TYPED_SET_LOCAL:
            meaning = BUSTER_TIER0_INLINE_LOCAL;
            break;
        case BUSTER_TYPED_BRANCH_FALSE:
        case BUSTER_TYPED_BRANCH_TRUE:
        case BUSTER_TYPED_JUMP:
        case BUSTER_TYPED_JUMP_VALUE:
            meaning = BUSTER_TIER0_INLINE_ENTRY;
            break;
        case BUSTER_TYPED_CALL:
            meaning = BUSTER_TIER0_INLINE_CALL;
            break;
        case BUSTER_TYPED_X86:
            meaning = BUSTER_TIER0_INLINE_OPERATION;
            break;
        case BUSTER_TYPED_ATOMIC_CAS:
            meaning = BUSTER_TIER0_INLINE_CAS;
            break;
        case BUSTER_TYPED_CONST:
            meaning = class == BUSTER_TYPED_CLASS_F80 ? BUSTER_TIER0_INLINE_X87 : class == BUSTER_TYPED_CLASS_VECTOR ? BUSTER_TIER0_INLINE_VECTOR : BUSTER_TIER0_INLINE_NONE;
            break;
        default:
            break;
    }
    return meaning;
}

// Whether a kind is a branch's (BRANCH_FALSE, BRANCH_TRUE, JUMP or JUMP_VALUE: one range), whose value is a target.
static bool tier0_inline_branch(uint32_t kind)
{
    _Static_assert(BUSTER_TYPED_BRANCH_TRUE == BUSTER_TYPED_BRANCH_FALSE + 1 && BUSTER_TYPED_JUMP == BUSTER_TYPED_BRANCH_FALSE + 2 && BUSTER_TYPED_JUMP_VALUE == BUSTER_TYPED_BRANCH_FALSE + 3,
                   "the branches' kinds are one range");
    return kind - BUSTER_TYPED_BRANCH_FALSE < 4u;
}

// The branch entries in the next at most 64 entries from first, bounded by end. The kinds' range is tier0_inline_branch's.
static uint64_t tier0_inline_branches(const Tier0Function* function, uint32_t first, uint32_t end)
{
    const __mmask64 lanes = tier0_lanes(first, end);
    const __m512i kinds = _mm512_maskz_loadu_epi8(lanes, function->kinds + first);
    return lanes & _mm512_cmplt_epu8_mask(_mm512_sub_epi8(kinds, _mm512_set1_epi8(BUSTER_TYPED_BRANCH_FALSE)), _mm512_set1_epi8(4));
}

// A back edge, including a self edge, makes a callee subject to the experimental shapes' home check (#2841).
// Expanded scalar-return wrappers may contain a memory-result callee's loop, so this is independent of the ABI.
static bool tier0_inline_has_loop(const Tier0Function* function)
{
    const uint32_t count = function->count;
    const uint64_t* values = function->values;
    bool loop = false;
    for (uint32_t first = 0; !loop && first < count; first += 64)
    {
        uint64_t branches = tier0_inline_branches(function, first, count);
        while (!loop && branches != 0)
        {
            const uint32_t entry = first + (uint32_t)__builtin_ctzll(branches);
            branches &= branches - 1;
            loop = values[entry] <= entry;
        }
    }
    return loop;
}

// Whether `function`'s stream can be a callee's (docs/tier0-inlining.md, "Eligibility"): nothing about its own frame or
// code addresses (variadic, setjmp or a call of its kin, eh_return, unwind_init, an interrupt or preserving convention,
// a realigned stack, labels (an asm body's too), ALLOCA and the stack's kinds, FRAME_ADDRESS, tail calls, JUMP_INDIRECT).
// Its entries are its body, but its last entry where that is a RETURN (`*body`). Without `shapes` it has no result
// through memory and at most one RETURN, its last entry; with them (step 5, --tier0-inline-shapes) a RETURN before it
// goes to the copy's end (tier0_inline_body), and a result through memory is the site's to bind
// (tier0_inline_site_valid). They are off by default: on the 9700X the vector lexer's spliced loops lost their locals'
// registers to their large caller's (docs/tier0-inlining.md, "Several returns and results in memory").
static bool tier0_inline_callee_valid(const Tier0Function* function, bool shapes, uint32_t* body)
{
    const uint32_t count = function->count;
    bool valid = function->incoming_variadic == 0 && !function->returns_twice && !function->calls_returns_twice && !function->eh_return && !function->unwind_init && function->interrupt == 0 &&
                 function->preserve_general == 0 && function->preserve_vectors == 0 && function->preserve_mask_bytes == 0 && !function->realign_stack &&
                 function->label_count == 0 && function->table_count == 0 && (shapes || function->result.kind != BUSTER_ABI_RESULT_MEMORY);
    const bool returns = count != 0 && function->kinds[count - 1] == BUSTER_TYPED_RETURN;
    // An asm body's labels (#1839, Tier0AsmBody.labels) are defined once, where its own code has them.
    for (uint32_t b = 0; valid && b < function->asm_body_count; b += 1)
    {
        valid = function->asm_bodies[b].label_count == 0;
    }
    for (uint32_t e = 0; valid && e < count; e += 1)
    {
        const uint32_t kind = function->kinds[e];
        valid = kind != BUSTER_TYPED_ALLOCA && kind != BUSTER_TYPED_STACK_SAVE && kind != BUSTER_TYPED_STACK_RESTORE && kind != BUSTER_TYPED_FRAME_ADDRESS &&
                kind != BUSTER_TYPED_TAIL_CALL && kind != BUSTER_TYPED_JUMP_INDIRECT && (shapes || kind != BUSTER_TYPED_RETURN || e + 1 == count);
    }
    *body = returns ? count - 1 : count;
    return valid;
}

// What entry `e` of `function` pops and pushes, as values of any stack (tier0_shapes', but a DUP's, which reads the
// value it copies: one popped and two pushed, so that the copied value is in the DUP's own subtree).
static void tier0_inline_effect(const Tier0Function* function, uint32_t e, uint32_t* pops, uint32_t* pushes)
{
    const uint32_t kind = function->kinds[e];
    const uint32_t class = function->classes[e];
    const uint64_t value = function->values[e];
    const Tier0Shape shape = tier0_shapes[kind < BUSTER_TYPED_KIND_LIMIT ? kind : 0];
    uint32_t popped = (shape.first != BUSTER_TIER0_STACK_NONE ? 1u : 0u) + (shape.last != BUSTER_TIER0_STACK_NONE && !(kind == BUSTER_TYPED_RETURN && class == BUSTER_TYPED_CLASS_VOID) ? 1u : 0u);
    uint32_t pushed = shape.push != BUSTER_TIER0_STACK_NONE ? 1u : 0u;
    if (kind == BUSTER_TYPED_CALL || kind == BUSTER_TYPED_TAIL_CALL)
    {
        const bool row = value < function->call_count;
        popped = row ? function->call_argument_counts[value] + (function->call_targets[value] == 0 ? 1u : 0u) : 0;
        pushed = kind == BUSTER_TYPED_CALL && class != BUSTER_TYPED_CLASS_VOID ? 1u : 0u;
    }
    else if (kind == BUSTER_TYPED_X86)
    {
        popped = value < function->operation_count ? function->operations[value].operand_count : 0;
        pushed = class != BUSTER_TYPED_CLASS_VOID ? 1u : 0u;
    }
    else if (kind == BUSTER_TYPED_DUP)
    {
        popped = 1;
        pushed = 2;
    }
    *pops = popped;
    *pushes = pushed;
}

// The starts of the last `count` values that entries before `end` of `function` leave on the stacks, the last value's
// root at end - 1, into starts[0..count) (the first value's start not needed and not found: starts[0] is end for
// count 0, else the entry after the value before it ends, which the scan doesn't look for). A value's start is where,
// scanning back from its root, the values its entries need are all found, and the entry before pushes a value: one
// that pushes nothing (a branch, a jump, a store) is an arm's or a statement's of the value (an arm of ?:, a statement
// expression's statements), whose entries the scan takes too. Returns false where the scan runs out of entries.
static bool tier0_inline_starts(const Tier0Function* function, uint32_t end, uint32_t count, uint32_t* starts)
{
    bool valid = true;
    uint32_t root = end;
    for (uint32_t k = count; valid && k > 1; k -= 1)
    {
        int64_t need = 1;
        uint32_t j = root;
        bool found = false;
        while (valid && !found)
        {
            valid = j != 0;
            if (valid)
            {
                j -= 1;
                uint32_t pops = 0;
                uint32_t pushes = 0;
                tier0_inline_effect(function, j, &pops, &pushes);
                need += (int64_t)pops - (int64_t)pushes;
                uint32_t before_pops = 0;
                uint32_t before_pushes = 0;
                if (need == 0 && j != 0)
                {
                    tier0_inline_effect(function, j - 1, &before_pops, &before_pushes);
                }
                found = need == 0 && j != 0 && before_pushes != 0;
                valid = need >= 0 && (need != 0 || j != 0);
            }
        }
        starts[k - 1] = j;
        root = j;
    }
    if (count != 0)
    {
        starts[0] = 0;
    }
    return valid;
}

// The splice's input: the caller and, per call row of it, 0 for a call it keeps or 1 + the index in `callees` of the
// stream that the call inlines, each of which tier0_inline_callee_valid accepts with `shapes` (step 5's, several returns
// and results in memory: --tier0-inline-shapes); and the vector transport for a caller without vectors whose callees
// have them (the caller's target's, Tier0Function.vector_bytes).
typedef struct Tier0InlineInput Tier0InlineInput;
struct Tier0InlineInput
{
    const Tier0Function* caller;
    const uint32_t* row_callees;
    const Tier0Function* const* callees;
    uint32_t callee_count;
    uint8_t vector_bytes;
    bool shapes;
};

// A site: its CALL entry and row, its distinct callee (Tier0InlinePlan.distinct), its level, the entry before which its
// first binding goes (its second argument's start, or the call for fewer than two arguments), its arguments' starts in
// the plan's `arguments` from `argument`, and its bases in G: its locals', its call rows' and its argument rows'.
typedef struct Tier0InlineSite Tier0InlineSite;
struct Tier0InlineSite
{
    uint32_t entry;
    uint32_t row;
    uint32_t callee;
    uint32_t level;
    uint32_t first;
    uint32_t argument;
    uint32_t local_base;
    uint32_t row_base;
    uint32_t argument_base;
    uint32_t body_base;
    // Whether its arguments are forwarded (tier0_inline_forwards): its callee's first entries, which read its
    // `parameters`, are left out, and its arguments stay on the stacks as the CALL took them.
    bool forward;
    uint32_t parameters;
    // 1 where its callee's result goes through a hidden pointer, parameter 0 (step 5), which takes the address of the
    // call's result object, so that argument k binds parameter k + 1; else 0.
    uint32_t hidden;
};

// A distinct callee of the caller's sites: its index in the input's callees, its body (its entries but its RETURN), its
// levels, and its bases in G: its locals' first level's, its operations', its x87 and vector constants'.
typedef struct Tier0InlineCallee Tier0InlineCallee;
struct Tier0InlineCallee
{
    uint32_t index;
    uint32_t body;
    uint32_t levels;
    uint32_t local_base;
    uint32_t operation_base;
    uint32_t x87_base;
    uint32_t vector_base;
    // Whether a site's arguments may be forwarded (tier0_inline_forwards).
    bool forwards;
};

// An insertion before an entry of F: site `site`'s binding of argument `argument`, or with BUSTER_TIER0_INLINE_CALL_INSERTION
// its call (its last argument's binding, its body and its result's binding, in place of the CALL); `next` the
// insertion after it at the same entry, plus one, or 0.
#define BUSTER_TIER0_INLINE_CALL_INSERTION UINT32_MAX
typedef struct Tier0InlineInsertion Tier0InlineInsertion;
struct Tier0InlineInsertion
{
    uint32_t site;
    uint32_t argument;
    uint32_t next;
};

// The plan of a splice: its sites (in entry order), distinct callees, arguments' starts, insertions (heads: per entry of
// F and its end, the first insertion before it plus one, or 0; `positions`: the entries with insertions, increasing),
// the starts in G of F's entries (`starts`, count + 1 of them: where entry i's first insertion, or its copy, goes) and
// G's sizes.
typedef struct Tier0InlinePlan Tier0InlinePlan;
struct Tier0InlinePlan
{
    Tier0InlineSite* sites;
    uint32_t site_count;
    Tier0InlineCallee* distinct;
    uint32_t distinct_count;
    uint32_t* arguments;
    Tier0InlineInsertion* insertions;
    uint32_t insertion_count;
    uint32_t* heads;
    uint32_t* positions;
    uint32_t position_count;
    uint32_t* starts;
    uint32_t temporary;
    uint64_t count;
    uint64_t local_count;
    uint64_t call_count;
    uint64_t argument_count;
    uint64_t operation_count;
    uint64_t x87_count;
    uint64_t vector_count;
    bool aggregates;
    // G's vector transport (Tier0Function.vector_bytes), which its vector bindings must have.
    uint8_t transport;
    bool sizes;
    bool result_objects;
    bool argument_sizes;
    bool vectors;
    bool masks;
    bool x87;
    bool int128;
    // Whether a LOAD, STORE or SET of G has a segment override (#1447, Tier0Function.segments).
    bool segments;
    // Whether a LOAD, STORE or SET of G is a volatile access (#3310, Tier0Function.volatiles).
    bool volatiles;
    // G's stack protector (#3558, Tier0Function.stack_protector and stack_check_fail): guarded where the caller or a
    // spliced callee is, as LLVM's inliner raises its caller's protector to its callee's, but where the caller's
    // no_stack_protector attribute exempts it; and the report its checks call, the first guarded one's.
    uint8_t stack_protector;
    SymbolId stack_check_fail;
};

// How an aggregate of `size` bytes binds (BUSTER_TIER0_INLINE_BIND_*): as a vector where G has vectors and its transport
// one of the size (4, 8, 16, 32 or 64 bytes; the EVEX path's to 64), else as a copy.
static uint32_t tier0_inline_aggregate(const Tier0InlinePlan* plan, uint32_t size)
{
    const uint32_t limit = !plan->vectors ? 0 : plan->transport == 0 ? 64u : plan->transport;
    const bool vector = size >= 4 && (size & (size - 1)) == 0 && size <= limit;
    return vector ? BUSTER_TIER0_INLINE_BIND_VECTOR : BUSTER_TIER0_INLINE_BIND_COPY;
}

// How argument `k` of site `site` binds (BUSTER_TIER0_INLINE_BIND_*), before forwarding.
static uint32_t tier0_inline_binding(const Tier0Function* caller, const Tier0InlinePlan* plan, const Tier0InlineSite* site, uint32_t k)
{
    const uint32_t a = caller->call_first[site->row] + k;
    const uint32_t size = caller->argument_sizes != 0 ? caller->argument_sizes[a] : 0;
    return size != 0 ? tier0_inline_aggregate(plan, size) : BUSTER_TIER0_INLINE_BIND_VALUE;
}

// The entries argument `k` of site `site` binds with: its binding's, or forwarded, a vector's LOAD alone and nothing
// for a value.
static uint32_t tier0_inline_binding_entries(const Tier0Function* caller, const Tier0InlinePlan* plan, const Tier0InlineSite* site, uint32_t k)
{
    const uint32_t how = tier0_inline_binding(caller, plan, site, k);
    return site->forward ? how - 1u : how;
}

// Whether a callee's sites may forward their arguments (docs/tier0-inlining.md, "Forwarding"): its first entries are
// LOCAL 0 to LOCAL n - 1, its parameters' only references, which no branch reaches, so
// that its parameters' values are read once each, in order, right where the CALL took the arguments: an intrinsic's
// wrapper's shape. A site forwards where each argument's binding is a value's or a vector's of the class its read has.
static bool tier0_inline_forwards(const Tier0Function* callee, uint32_t body)
{
    const uint32_t n = callee->parameter_count;
    // A result through a hidden pointer reads parameter 0 where it writes the result, not first.
    bool valid = n <= body && callee->result.kind != BUSTER_ABI_RESULT_MEMORY;
    for (uint32_t k = 0; valid && k < n; k += 1)
    {
        valid = callee->kinds[k] == BUSTER_TYPED_LOCAL && callee->values[k] == k;
    }
    for (uint32_t e = n; valid && e < callee->count; e += 1)
    {
        const uint32_t kind = callee->kinds[e];
        const uint64_t value = callee->values[e];
        const uint32_t meaning = tier0_inline_meaning(kind, callee->classes[e]);
        const uint64_t local = meaning == BUSTER_TIER0_INLINE_LOCAL ? value : meaning == BUSTER_TIER0_INLINE_CAS ? value >> BUSTER_TYPED_CAS_LOCAL_SHIFT : UINT64_MAX;
        const bool object = (kind == BUSTER_TYPED_CALL || kind == BUSTER_TYPED_TAIL_CALL) && value < callee->call_count && callee->call_result_objects != 0 &&
                            callee->call_result_objects[value] != 0 && callee->call_result_objects[value] - 1u < n;
        valid = local >= n && !object && !(tier0_inline_branch(kind) && value < n);
    }
    for (uint32_t k = 0; valid && k < n; k += 1)
    {
        valid = (callee->local_flags[k] & BUSTER_TYPED_LOCAL_ADDRESS_ONLY) == 0;
    }
    return valid;
}

// Whether site `site` (its callee `callee` forwarding) forwards: every argument a value of its read's class, or a
// vector of an object of its read's bytes.
static bool tier0_inline_site_forwards(const Tier0Function* caller, const Tier0Function* callee, const Tier0InlinePlan* plan, const Tier0InlineSite* site)
{
    const uint32_t n = caller->call_argument_counts[site->row];
    bool valid = true;
    for (uint32_t k = 0; valid && k < n; k += 1)
    {
        const uint32_t a = caller->call_first[site->row] + k;
        const uint32_t how = tier0_inline_binding(caller, plan, site, k);
        const uint32_t size = caller->argument_sizes != 0 ? caller->argument_sizes[a] : 0;
        valid = how == BUSTER_TIER0_INLINE_BIND_VALUE ? callee->classes[k] == caller->argument_classes[a] :
                how == BUSTER_TIER0_INLINE_BIND_VECTOR && callee->classes[k] == BUSTER_TYPED_CLASS_VECTOR && callee->local_sizes[k] == size;
    }
    return valid;
}

// How site `site`'s result binds: none, an aggregate's into the call's object (a vector only into an object of its
// size, all of whose bytes a SET_LOCAL writes), or through a hidden pointer, already in the object, the pointer the
// callee returns discarded (a DISCARD, one entry as a value's binding).
static uint32_t tier0_inline_result_entries(const Tier0Function* caller, const Tier0Function* callee, const Tier0InlinePlan* plan, const Tier0InlineSite* site)
{
    const uint32_t object = caller->call_result_objects != 0 ? caller->call_result_objects[site->row] : 0;
    const uint32_t how = site->hidden != 0 ? BUSTER_TIER0_INLINE_BIND_VALUE :
                         object != 0       ? tier0_inline_aggregate(plan, callee->result_size) :
                                             BUSTER_TIER0_INLINE_BIND_NONE;
    return site->hidden == 0 && how == BUSTER_TIER0_INLINE_BIND_VECTOR && caller->local_sizes[object - 1] != callee->result_size ? BUSTER_TIER0_INLINE_BIND_COPY : how;
}

// The entries of site `site`'s hidden pointer's binding, after its last argument's (step 5): LOCAL_ADDRESS of the call's
// object, SET_LOCAL of the callee's parameter 0; none without one.
static uint32_t tier0_inline_hidden_entries(const Tier0InlineSite* site)
{
    return 2 * site->hidden;
}

// Whether site `site` (its entry, row and callee `callee`, body `body` set) is one: the arguments the parameters take,
// the result the call takes, and the arguments' starts, into arguments[0..n).
static bool tier0_inline_site_valid(const Tier0Function* caller, const Tier0Function* callee, Tier0InlineSite* site, uint32_t body, uint32_t* arguments)
{
    const uint32_t row = site->row;
    const uint32_t n = caller->call_argument_counts[row];
    const uint32_t class = caller->classes[site->entry];
    const bool returns = body != callee->count;
    const uint32_t returned = returns ? callee->classes[callee->count - 1] : BUSTER_TYPED_CLASS_VOID;
    const uint32_t object = caller->call_result_objects != 0 ? caller->call_result_objects[row] : 0;
    const uint32_t hidden = callee->result.kind == BUSTER_ABI_RESULT_MEMORY ? 1u : 0u;
    site->hidden = hidden;
    bool valid = n + hidden == callee->parameter_count && caller->call_variadic[row] == 0 && caller->call_targets[row] != 0 &&
                 (caller->call_tails == 0 || caller->call_tails[row] == 0);
    if (valid && hidden != 0)
    {
        // A result through a hidden pointer (step 5): parameter 0, a U64, takes the address of the call's object, whose
        // address the call needs taken, where the callee writes its result; its RETURN gives the pointer back.
        const uint32_t local = object - 1;
        valid = object != 0 && class == BUSTER_TYPED_CLASS_VOID && returned == BUSTER_TYPED_CLASS_U64 && local < caller->local_count && caller->local_flags != 0 &&
                (caller->local_flags[local] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0 && callee->local_count != 0 && callee->local_classes[0] == BUSTER_TYPED_CLASS_U64;
    }
    else if (valid && object != 0)
    {
        // An aggregate result in registers, into the call's object: the callee's RETURN gives its bytes' address.
        const uint32_t local = object - 1;
        valid = class == BUSTER_TYPED_CLASS_VOID && returned == BUSTER_TYPED_CLASS_U64 && callee->result_size != 0 &&
                callee->result_size <= BUSTER_TIER0_INLINE_COPY_BYTES && callee->result.kind == BUSTER_ABI_RESULT_REGISTERS && local < caller->local_count &&
                caller->local_sizes != 0 && caller->local_sizes[local] >= callee->result_size;
    }
    else if (valid)
    {
        valid = class == returned && callee->result_size == 0;
    }
    for (uint32_t k = 0; valid && k < n; k += 1)
    {
        const uint32_t a = caller->call_first[row] + k;
        const uint32_t size = caller->argument_sizes != 0 ? caller->argument_sizes[a] : 0;
        const uint32_t own = callee->local_sizes != 0 ? callee->local_sizes[k + hidden] : 0;
        valid = size == 0 ? own == 0 || (callee->local_flags[k + hidden] & BUSTER_TYPED_LOCAL_ADDRESS_ONLY) == 0 :
                            size <= BUSTER_TIER0_INLINE_COPY_BYTES && own == size && caller->argument_classes[a] == BUSTER_TYPED_CLASS_U64;
    }
    valid = valid && tier0_inline_starts(caller, site->entry, n, arguments);
    // Each binding's operand is its argument's root, the entry before it, whose class it reads.
    for (uint32_t k = 0; valid && k < n; k += 1)
    {
        const uint32_t root = (k + 1 < n ? arguments[k + 1] : site->entry) - 1;
        valid = site->entry != 0 && caller->classes[root] == caller->argument_classes[caller->call_first[row] + k];
    }
    site->first = n >= 2 ? arguments[1] : site->entry;
    return valid;
}

// Pushes `count` elements of `Type` on `arena`, or null.
#define BUSTER_TIER0_INLINE_PUSH(arena, Type, count) BUSTER_ARENA_PUSH_ARRAY((arena), Type, (uint64_t)(count) + 1)

// The plan of the splice of `input` (Tier0InlinePlan), in `arena`: false where there is no site, or no room.
static bool tier0_inline_plan(const Tier0InlineInput* input, bool keep_loops, Arena* arena, Tier0InlinePlan* plan)
{
    const Tier0Function* caller = input->caller;
    const uint32_t count = caller->count;
    *plan = (Tier0InlinePlan){ 0 };
    // The candidates: CALL entries whose rows name a callee.
    uint32_t candidates = 0;
    uint32_t* calls = BUSTER_TIER0_INLINE_PUSH(arena, uint32_t, count);
    bool valid = calls != 0 && input->row_callees != 0 && caller->call_count != 0;
    if (valid)
    {
        const __m512i call = _mm512_set1_epi8(BUSTER_TYPED_CALL);
        for (uint32_t e = 0; e < count; e += 64)
        {
            const uint32_t take = count - e < 64 ? count - e : 64;
            const __mmask64 lanes = _bzhi_u64(UINT64_MAX, take);
            const __m512i kinds = _mm512_maskz_loadu_epi8(lanes, caller->kinds + e);
            uint64_t found = _mm512_mask_cmpeq_epi8_mask(lanes, kinds, call);
            while (found != 0)
            {
                const uint32_t entry = e + (uint32_t)__builtin_ctzll(found);
                const uint64_t row = caller->values[entry];
                calls[candidates] = entry;
                candidates += row < caller->call_count && input->row_callees[row] != 0 ? 1u : 0u;
                found &= found - 1;
            }
        }
    }
    valid = valid && candidates != 0;
    // The sites and their arguments' starts.
    uint64_t argument_total = 0;
    for (uint32_t s = 0; valid && s < candidates; s += 1)
    {
        argument_total += caller->call_argument_counts[caller->values[calls[s]]];
    }
    plan->sites = valid ? BUSTER_TIER0_INLINE_PUSH(arena, Tier0InlineSite, candidates) : 0;
    plan->distinct = valid ? BUSTER_TIER0_INLINE_PUSH(arena, Tier0InlineCallee, candidates) : 0;
    plan->arguments = valid ? BUSTER_TIER0_INLINE_PUSH(arena, uint32_t, argument_total) : 0;
    uint32_t* stack = valid ? BUSTER_TIER0_INLINE_PUSH(arena, uint32_t, candidates) : 0;
    valid = valid && plan->sites != 0 && plan->distinct != 0 && plan->arguments != 0 && stack != 0;
    uint32_t argument = 0;
    for (uint32_t s = 0; valid && s < candidates; s += 1)
    {
        const uint32_t entry = calls[s];
        const uint32_t row = (uint32_t)caller->values[entry];
        const uint32_t index = input->row_callees[row] - 1;
        const Tier0Function* callee = index < input->callee_count ? input->callees[index] : 0;
        uint32_t body = 0;
        Tier0InlineSite site = { .entry = entry, .row = row, .argument = argument };
        bool taken = callee != 0 && tier0_inline_callee_valid(callee, input->shapes, &body) &&
                     (!keep_loops || !tier0_inline_has_loop(callee)) && tier0_inline_site_valid(caller, callee, &site, body, plan->arguments + argument);
        // A callee's vectors on the caller's transport: the EVEX path takes every one's, another only its own.
        const uint8_t transport = caller->vectors ? caller->vector_bytes : input->vector_bytes;
        taken = taken && (!callee->vectors || transport == 0 || transport == callee->vector_bytes) && (!callee->masks || transport == 0);
        uint32_t d = 0;
        while (taken && d < plan->distinct_count && plan->distinct[d].index != index)
        {
            d += 1;
        }
        if (taken && d == plan->distinct_count)
        {
            plan->distinct[d] = (Tier0InlineCallee){ .index = index, .body = body };
            plan->distinct_count += 1;
        }
        site.callee = d;
        plan->sites[plan->site_count] = site;
        plan->site_count += taken ? 1u : 0u;
        argument += taken ? caller->call_argument_counts[row] : 0;
    }
    // Levels, from the last site back: the stack holds the sites whose binding regions ([first, entry]) contain the
    // current one's call, innermost on top (the regions nest or are apart). A site past the most levels stays a call.
    uint32_t depth = 0;
    uint32_t kept = 0;
    for (uint32_t s = plan->site_count; valid && s > 0; s -= 1)
    {
        Tier0InlineSite* site = plan->sites + s - 1;
        while (depth != 0 && plan->sites[stack[depth - 1]].first > site->entry)
        {
            depth -= 1;
        }
        uint32_t level = 0;
        for (uint32_t k = 0; k < depth; k += 1)
        {
            const Tier0InlineSite* outer = plan->sites + stack[k];
            level = outer->callee == site->callee && outer->level + 1 > level ? outer->level + 1 : level;
        }
        site->level = level < BUSTER_TIER0_INLINE_LEVELS ? level : UINT32_MAX;
        if (site->level != UINT32_MAX)
        {
            stack[depth] = s - 1;
            depth += 1;
            kept += 1;
        }
    }
    // The kept sites, in entry order.
    uint32_t out = 0;
    for (uint32_t s = 0; valid && s < plan->site_count; s += 1)
    {
        plan->sites[out] = plan->sites[s];
        out += plan->sites[s].level != UINT32_MAX ? 1u : 0u;
    }
    plan->site_count = valid ? out : 0;
    valid = valid && kept != 0;
    // G's tables: the caller's, then the temporary, then each distinct callee's locals per level, operations and
    // constants; each site's call and argument rows.
    plan->local_count = caller->local_count;
    plan->call_count = caller->call_count;
    plan->argument_count = caller->argument_count;
    plan->operation_count = caller->operation_count;
    plan->x87_count = caller->x87_constant_count;
    plan->vector_count = caller->vector_constant_count;
    plan->sizes = caller->local_sizes != 0;
    plan->result_objects = caller->call_result_objects != 0;
    plan->argument_sizes = caller->argument_sizes != 0;
    plan->vectors = caller->vectors;
    plan->masks = caller->masks;
    plan->x87 = caller->x87;
    plan->int128 = caller->int128;
    plan->segments = caller->segments;
    plan->volatiles = caller->volatiles;
    plan->stack_protector = caller->stack_protector;
    plan->stack_check_fail = caller->stack_check_fail;
    for (uint32_t s = 0; valid && s < plan->site_count; s += 1)
    {
        const Tier0InlineSite* site = plan->sites + s;
        Tier0InlineCallee* callee = plan->distinct + site->callee;
        callee->levels = site->level + 1 > callee->levels ? site->level + 1 : callee->levels;
    }
    for (uint32_t d = 0; valid && d < plan->distinct_count; d += 1)
    {
        const Tier0Function* callee = input->callees[plan->distinct[d].index];
        plan->sizes = plan->sizes || callee->local_sizes != 0;
        plan->result_objects = plan->result_objects || callee->call_result_objects != 0;
        plan->argument_sizes = plan->argument_sizes || callee->argument_sizes != 0;
        plan->vectors = plan->vectors || callee->vectors;
        plan->masks = plan->masks || callee->masks;
        plan->x87 = plan->x87 || callee->x87;
        plan->int128 = plan->int128 || callee->int128;
        plan->segments = plan->segments || callee->segments;
        plan->volatiles = plan->volatiles || callee->volatiles;
        const bool raised = plan->stack_protector == BUSTER_TIER0_GUARD_NONE && callee->stack_protector == BUSTER_TIER0_GUARD_ON;
        plan->stack_protector = raised ? BUSTER_TIER0_GUARD_ON : plan->stack_protector;
        plan->stack_check_fail = raised ? callee->stack_check_fail : plan->stack_check_fail;
    }
    plan->transport = caller->vectors ? caller->vector_bytes : plan->vectors ? input->vector_bytes : caller->vector_bytes;
    for (uint32_t d = 0; valid && d < plan->distinct_count; d += 1)
    {
        Tier0InlineCallee* distinct = plan->distinct + d;
        distinct->forwards = tier0_inline_forwards(input->callees[distinct->index], distinct->body);
    }
    for (uint32_t s = 0; valid && s < plan->site_count; s += 1)
    {
        Tier0InlineSite* site = plan->sites + s;
        const Tier0InlineCallee* distinct = plan->distinct + site->callee;
        const Tier0Function* callee = input->callees[distinct->index];
        site->forward = distinct->forwards && tier0_inline_site_forwards(caller, callee, plan, site);
        site->parameters = caller->call_argument_counts[site->row];
        for (uint32_t k = 0; k < caller->call_argument_counts[site->row]; k += 1)
        {
            plan->aggregates = plan->aggregates || tier0_inline_binding_entries(caller, plan, site, k) == BUSTER_TIER0_INLINE_BIND_COPY;
        }
        plan->aggregates = plan->aggregates || tier0_inline_result_entries(caller, callee, plan, site) == BUSTER_TIER0_INLINE_BIND_COPY;
    }
    plan->temporary = (uint32_t)plan->local_count;
    plan->local_count += plan->aggregates ? 1u : 0u;
    for (uint32_t d = 0; valid && d < plan->distinct_count; d += 1)
    {
        Tier0InlineCallee* distinct = plan->distinct + d;
        const Tier0Function* callee = input->callees[distinct->index];
        distinct->local_base = (uint32_t)plan->local_count;
        distinct->operation_base = (uint32_t)plan->operation_count;
        distinct->x87_base = (uint32_t)plan->x87_count;
        distinct->vector_base = (uint32_t)plan->vector_count;
        plan->local_count += (uint64_t)distinct->levels * callee->local_count;
        plan->operation_count += callee->operation_count;
        plan->x87_count += callee->x87_constant_count;
        plan->vector_count += callee->vector_constant_count;
    }
    // The insertions: every call's first, then every argument's, each put first in its entry's list, so that an
    // argument's binding comes before a call's at one entry (the call of a function without arguments that is another
    // site's argument).
    const uint32_t insertions = argument;
    plan->insertions = valid ? BUSTER_TIER0_INLINE_PUSH(arena, Tier0InlineInsertion, (uint64_t)insertions + plan->site_count) : 0;
    plan->heads = valid ? BUSTER_TIER0_INLINE_PUSH(arena, uint32_t, (uint64_t)count + 1) : 0;
    plan->starts = valid ? BUSTER_TIER0_INLINE_PUSH(arena, uint32_t, (uint64_t)count + 1) : 0;
    valid = valid && plan->insertions != 0 && plan->heads != 0 && plan->starts != 0;
    uint32_t* growth = plan->starts;
    if (valid)
    {
        tier0_inline_fill(plan->heads, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        tier0_inline_fill(growth, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
    }
    for (uint32_t s = 0; valid && s < plan->site_count; s += 1)
    {
        Tier0InlineSite* site = plan->sites + s;
        const Tier0InlineCallee* distinct = plan->distinct + site->callee;
        const Tier0Function* callee = input->callees[distinct->index];
        const uint32_t n = caller->call_argument_counts[site->row];
        site->local_base = distinct->local_base + site->level * callee->local_count;
        site->row_base = (uint32_t)plan->call_count;
        site->argument_base = (uint32_t)plan->argument_count;
        plan->call_count += callee->call_count;
        plan->argument_count += callee->argument_count;
        const uint32_t skipped = site->forward ? n : 0;
        const uint32_t size = (n != 0 ? tier0_inline_binding_entries(caller, plan, site, n - 1) : 0) + tier0_inline_hidden_entries(site) + distinct->body - skipped +
                              tier0_inline_result_entries(caller, callee, plan, site);
        plan->insertions[plan->insertion_count] = (Tier0InlineInsertion){ .site = s, .argument = BUSTER_TIER0_INLINE_CALL_INSERTION, .next = plan->heads[site->entry] };
        plan->insertion_count += 1;
        plan->heads[site->entry] = plan->insertion_count;
        growth[site->entry] += size - 1;
    }
    for (uint32_t s = 0; valid && s < plan->site_count; s += 1)
    {
        const Tier0InlineSite* site = plan->sites + s;
        const uint32_t n = caller->call_argument_counts[site->row];
        for (uint32_t k = 0; k + 1 < n; k += 1)
        {
            // A forwarded value's binding is no entry, and no insertion.
            const uint32_t position = plan->arguments[site->argument + k + 1];
            const uint32_t entries = tier0_inline_binding_entries(caller, plan, site, k);
            if (entries != 0)
            {
                plan->insertions[plan->insertion_count] = (Tier0InlineInsertion){ .site = s, .argument = k, .next = plan->heads[position] };
                plan->insertion_count += 1;
                plan->heads[position] = plan->insertion_count;
                growth[position] += entries;
            }
        }
    }
    // The positions with insertions, increasing, and the starts: start[i] = i + the growth before i.
    plan->positions = valid ? BUSTER_TIER0_INLINE_PUSH(arena, uint32_t, plan->insertion_count) : 0;
    valid = valid && plan->positions != 0;
    uint32_t sum = 0;
    if (valid)
    {
        const __m512i iota = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
        const __m512i zero = _mm512_setzero_si512();
        for (uint32_t e = 0; e <= count; e += 16)
        {
            const uint32_t take = count + 1 - e < 16 ? count + 1 - e : 16;
            const __mmask16 lanes = (__mmask16)_bzhi_u32(0xffffu, take);
            const __m512i heads = _mm512_maskz_loadu_epi32(lanes, plan->heads + e);
            const __mmask16 inserted = _mm512_mask_cmpneq_epi32_mask(lanes, heads, zero);
            _mm512_mask_compressstoreu_epi32(plan->positions + plan->position_count, inserted, _mm512_add_epi32(iota, _mm512_set1_epi32((int)e)));
            plan->position_count += (uint32_t)__builtin_popcount(inserted);
            // An exclusive prefix sum of the growth, plus the entry's own index.
            __m512i x = _mm512_maskz_loadu_epi32(lanes, growth + e);
            const __m512i own = x;
            x = _mm512_add_epi32(x, _mm512_maskz_permutexvar_epi32(0xfffe, _mm512_sub_epi32(iota, _mm512_set1_epi32(1)), x));
            x = _mm512_add_epi32(x, _mm512_maskz_permutexvar_epi32(0xfffc, _mm512_sub_epi32(iota, _mm512_set1_epi32(2)), x));
            x = _mm512_add_epi32(x, _mm512_maskz_permutexvar_epi32(0xfff0, _mm512_sub_epi32(iota, _mm512_set1_epi32(4)), x));
            x = _mm512_add_epi32(x, _mm512_maskz_permutexvar_epi32(0xff00, _mm512_sub_epi32(iota, _mm512_set1_epi32(8)), x));
            const __m512i starts = _mm512_add_epi32(_mm512_add_epi32(_mm512_sub_epi32(x, own), _mm512_set1_epi32((int)sum)), _mm512_add_epi32(iota, _mm512_set1_epi32((int)e)));
            sum += (uint32_t)_mm512_reduce_add_epi32(own);
            _mm512_mask_storeu_epi32(plan->starts + e, lanes, starts);
        }
    }
    plan->count = (uint64_t)count + sum;
    plan->count &= UINT32_MAX;
    valid = valid && plan->count <= BUSTER_TIER0_MAX_ENTRIES && plan->local_count <= BUSTER_TIER0_MAX_LOCALS && plan->call_count <= UINT32_MAX &&
            plan->argument_count <= UINT32_MAX && plan->operation_count <= UINT32_MAX && plan->x87_count <= UINT32_MAX && plan->vector_count <= UINT32_MAX;
    // Each site's body's first entry in G: after its call's first insertion's start, its last argument's binding and its
    // hidden pointer's.
    for (uint32_t s = 0; valid && s < plan->site_count; s += 1)
    {
        Tier0InlineSite* site = plan->sites + s;
        const uint32_t n = caller->call_argument_counts[site->row];
        uint32_t at = plan->starts[site->entry];
        for (uint32_t i = plan->heads[site->entry]; i != 0; i = plan->insertions[i - 1].next)
        {
            const Tier0InlineInsertion* insertion = plan->insertions + i - 1;
            at += insertion->argument != BUSTER_TIER0_INLINE_CALL_INSERTION ? tier0_inline_binding_entries(caller, plan, plan->sites + insertion->site, insertion->argument) : 0;
        }
        site->body_base = at + (n != 0 ? tier0_inline_binding_entries(caller, plan, site, n - 1) : 0) + tier0_inline_hidden_entries(site);
    }
    return valid;
}

// G's columns, which the fill writes.
typedef struct Tier0InlineColumns Tier0InlineColumns;
struct Tier0InlineColumns
{
    uint8_t* kinds;
    uint8_t* classes;
    uint64_t* values;
};

// Writes entry `at` of G.
static void tier0_inline_put(Tier0InlineColumns* g, uint32_t at, uint32_t kind, uint32_t class, uint64_t value)
{
    g->kinds[at] = (uint8_t)kind;
    g->classes[at] = (uint8_t)class;
    g->values[at] = value;
}

// An aggregate's binding of `size` bytes, whose address is on the stack, into the object `local` at G's entry `at`, as
// `how` says (tier0_inline_aggregate); returns the entry after it.
static uint32_t tier0_inline_bind_aggregate(const Tier0InlinePlan* plan, uint32_t how, uint32_t size, uint32_t local, Tier0InlineColumns* g, uint32_t at)
{
    if (how == BUSTER_TIER0_INLINE_BIND_VECTOR)
    {
        tier0_inline_put(g, at, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, size);
        tier0_inline_put(g, at + 1, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_VECTOR, local);
    }
    else
    {
        tier0_inline_put(g, at, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, plan->temporary);
        tier0_inline_put(g, at + 1, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
        tier0_inline_put(g, at + 2, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, plan->temporary);
        tier0_inline_put(g, at + 3, BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, size);
    }
    return at + how;
}

// The binding of argument `k` of site `site` at G's entry `at`; returns the entry after it.
static uint32_t tier0_inline_bind(const Tier0Function* caller, const Tier0InlinePlan* plan, const Tier0InlineSite* site, uint32_t k, Tier0InlineColumns* g, uint32_t at)
{
    const uint32_t a = caller->call_first[site->row] + k;
    const uint32_t size = caller->argument_sizes != 0 ? caller->argument_sizes[a] : 0;
    const uint32_t local = site->local_base + site->hidden + k;
    if (site->forward)
    {
        // A forwarded vector's bytes are loaded where its address was, and a value stays.
        if (size != 0)
        {
            tier0_inline_put(g, at, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, size);
            at += 1;
        }
    }
    else if (size != 0)
    {
        at = tier0_inline_bind_aggregate(plan, tier0_inline_aggregate(plan, size), size, local, g, at);
    }
    else
    {
        tier0_inline_put(g, at, BUSTER_TYPED_SET_LOCAL, caller->argument_classes[a], local);
        at += 1;
    }
    return at;
}

// The callee's entries a site's copy leaves out: a forwarded one's parameters' reads.
static uint32_t tier0_inline_skipped(const Tier0InlineSite* site)
{
    return site->forward ? site->parameters : 0;
}

// The bases of site `site`'s copy of its callee, by meaning.
static void tier0_inline_bases(const Tier0InlinePlan* plan, const Tier0InlineSite* site, uint64_t* bases)
{
    const Tier0InlineCallee* distinct = plan->distinct + site->callee;
    bases[BUSTER_TIER0_INLINE_NONE] = 0;
    bases[BUSTER_TIER0_INLINE_LOCAL] = site->local_base;
    // A forwarded site's copy leaves out its callee's first n entries, which no target but the first's reaches.
    bases[BUSTER_TIER0_INLINE_ENTRY] = site->body_base - tier0_inline_skipped(site);
    bases[BUSTER_TIER0_INLINE_CALL] = site->row_base;
    bases[BUSTER_TIER0_INLINE_OPERATION] = distinct->operation_base;
    bases[BUSTER_TIER0_INLINE_X87] = distinct->x87_base;
    bases[BUSTER_TIER0_INLINE_VECTOR] = distinct->vector_base;
    bases[BUSTER_TIER0_INLINE_CAS] = (uint64_t)site->local_base << BUSTER_TYPED_CAS_LOCAL_SHIFT;
}

// The result's binding of site `site` at G's entry `at` (none, an aggregate's copy into its object, or a DISCARD of the
// hidden pointer the callee returns); returns the entry after it.
static uint32_t tier0_inline_result(const Tier0Function* caller, const Tier0Function* callee, const Tier0InlinePlan* plan, const Tier0InlineSite* site,
                                    Tier0InlineColumns* g, uint32_t at)
{
    const uint32_t object = caller->call_result_objects != 0 ? caller->call_result_objects[site->row] : 0;
    if (site->hidden != 0)
    {
        tier0_inline_put(g, at, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0);
        at += 1;
    }
    else if (object != 0)
    {
        at = tier0_inline_bind_aggregate(plan, tier0_inline_result_entries(caller, callee, plan, site), callee->result_size, object - 1, g, at);
    }
    return at;
}

// G's tables but the entries: the caller's, the temporary, each distinct callee's locals per level, operations and
// constants, and each site's call and argument rows, their indices rebased (an exact copy, then a rebase per row).
static bool tier0_inline_tables(const Tier0InlineInput* input, const Tier0InlinePlan* plan, Arena* arena, Tier0Function* g)
{
    const Tier0Function* caller = input->caller;
    const uint64_t locals = plan->local_count;
    const uint64_t rows = plan->call_count;
    const uint64_t arguments = plan->argument_count;
    uint8_t* local_classes = BUSTER_TIER0_INLINE_PUSH(arena, uint8_t, locals);
    uint8_t* local_flags = BUSTER_TIER0_INLINE_PUSH(arena, uint8_t, locals);
    uint32_t* local_sizes = plan->sizes ? BUSTER_TIER0_INLINE_PUSH(arena, uint32_t, locals) : 0;
    uint8_t* local_alignments = plan->sizes ? BUSTER_TIER0_INLINE_PUSH(arena, uint8_t, locals) : 0;
    SymbolId* call_targets = BUSTER_TIER0_INLINE_PUSH(arena, SymbolId, rows);
    uint32_t* call_first = BUSTER_TIER0_INLINE_PUSH(arena, uint32_t, rows);
    uint32_t* call_argument_counts = BUSTER_TIER0_INLINE_PUSH(arena, uint32_t, rows);
    uint8_t* call_variadic = BUSTER_TIER0_INLINE_PUSH(arena, uint8_t, rows);
    AbiCall* calls = BUSTER_TIER0_INLINE_PUSH(arena, AbiCall, rows);
    uint32_t* call_result_objects = plan->result_objects ? BUSTER_TIER0_INLINE_PUSH(arena, uint32_t, rows) : 0;
    uint8_t* call_tails = caller->call_tails != 0 ? BUSTER_TIER0_INLINE_PUSH(arena, uint8_t, rows) : 0;
    AbiLocation* argument_locations = BUSTER_TIER0_INLINE_PUSH(arena, AbiLocation, arguments);
    uint8_t* argument_classes = BUSTER_TIER0_INLINE_PUSH(arena, uint8_t, arguments);
    uint32_t* argument_sizes = plan->argument_sizes ? BUSTER_TIER0_INLINE_PUSH(arena, uint32_t, arguments) : 0;
    Tier0Operation* operations = plan->operation_count != 0 ? BUSTER_TIER0_INLINE_PUSH(arena, Tier0Operation, plan->operation_count) : 0;
    uint64_t* x87_constants = plan->x87_count != 0 ? BUSTER_TIER0_INLINE_PUSH(arena, uint64_t, 2 * plan->x87_count) : 0;
    uint64_t* vector_constants = plan->vector_count != 0 ? BUSTER_TIER0_INLINE_PUSH(arena, uint64_t, 8 * plan->vector_count) : 0;
    uint8_t* vector_constant_bytes = plan->vector_count != 0 ? BUSTER_TIER0_INLINE_PUSH(arena, uint8_t, plan->vector_count) : 0;
    // The asm bodies (#1839): the caller's, then each site's copy of its callee's, its block rebased to the site's locals.
    uint64_t body_count = caller->asm_body_count;
    bool bodies_column = caller->call_asm != 0;
    for (uint32_t s = 0; s < plan->site_count; s += 1)
    {
        const Tier0Function* callee = input->callees[plan->distinct[plan->sites[s].callee].index];
        body_count += callee->asm_body_count;
        bodies_column |= callee->call_asm != 0;
    }
    uint32_t* call_asm = bodies_column ? BUSTER_TIER0_INLINE_PUSH(arena, uint32_t, rows) : 0;
    Tier0AsmBody* asm_bodies = body_count != 0 ? BUSTER_TIER0_INLINE_PUSH(arena, Tier0AsmBody, body_count) : 0;
    bool valid = (!bodies_column || call_asm != 0) && (body_count == 0 || asm_bodies != 0) && body_count <= UINT32_MAX;
    valid = valid && local_classes != 0 && local_flags != 0 && (!plan->sizes || (local_sizes != 0 && local_alignments != 0)) && call_targets != 0 && call_first != 0 &&
                 call_argument_counts != 0 && call_variadic != 0 && calls != 0 && (!plan->result_objects || call_result_objects != 0) &&
                 (caller->call_tails == 0 || call_tails != 0) && argument_locations != 0 && argument_classes != 0 && (!plan->argument_sizes || argument_sizes != 0) &&
                 (plan->operation_count == 0 || operations != 0) && (plan->x87_count == 0 || x87_constants != 0) &&
                 (plan->vector_count == 0 || (vector_constants != 0 && vector_constant_bytes != 0));
    if (valid)
    {
        // The caller's own.
        const uint32_t own = caller->local_count;
        tier0_inline_copy(local_classes, caller->local_classes, own);
        tier0_inline_copy(local_flags, caller->local_flags, own);
        if (plan->sizes)
        {
            if (caller->local_sizes != 0)
            {
                tier0_inline_copy(local_sizes, caller->local_sizes, (uint64_t)own * sizeof(uint32_t));
                tier0_inline_copy(local_alignments, caller->local_alignments, own);
            }
            else
            {
                tier0_inline_fill(local_sizes, 0, (uint64_t)own * sizeof(uint32_t));
                tier0_inline_fill(local_alignments, 0, own);
            }
        }
        const uint32_t own_rows = caller->call_count;
        tier0_inline_copy(call_targets, caller->call_targets, (uint64_t)own_rows * sizeof(SymbolId));
        tier0_inline_copy(call_first, caller->call_first, (uint64_t)own_rows * sizeof(uint32_t));
        tier0_inline_copy(call_argument_counts, caller->call_argument_counts, (uint64_t)own_rows * sizeof(uint32_t));
        tier0_inline_copy(call_variadic, caller->call_variadic, own_rows);
        tier0_inline_copy(calls, caller->calls, (uint64_t)own_rows * sizeof(AbiCall));
        if (plan->result_objects)
        {
            if (caller->call_result_objects != 0)
            {
                tier0_inline_copy(call_result_objects, caller->call_result_objects, (uint64_t)own_rows * sizeof(uint32_t));
            }
            else
            {
                tier0_inline_fill(call_result_objects, 0, (uint64_t)own_rows * sizeof(uint32_t));
            }
        }
        if (call_tails != 0)
        {
            tier0_inline_copy(call_tails, caller->call_tails, own_rows);
            tier0_inline_fill(call_tails + own_rows, 0, rows - own_rows);
        }
        if (call_asm != 0)
        {
            if (caller->call_asm != 0)
            {
                tier0_inline_copy(call_asm, caller->call_asm, (uint64_t)own_rows * sizeof(uint32_t));
            }
            else
            {
                tier0_inline_fill(call_asm, 0, (uint64_t)own_rows * sizeof(uint32_t));
            }
        }
        if (caller->asm_body_count != 0)
        {
            tier0_inline_copy(asm_bodies, caller->asm_bodies, (uint64_t)caller->asm_body_count * sizeof(Tier0AsmBody));
        }
        uint32_t body_base = caller->asm_body_count;
        const uint32_t own_arguments = caller->argument_count;
        tier0_inline_copy(argument_locations, caller->argument_locations, (uint64_t)own_arguments * sizeof(AbiLocation));
        tier0_inline_copy(argument_classes, caller->argument_classes, own_arguments);
        if (plan->argument_sizes)
        {
            if (caller->argument_sizes != 0)
            {
                tier0_inline_copy(argument_sizes, caller->argument_sizes, (uint64_t)own_arguments * sizeof(uint32_t));
            }
            else
            {
                tier0_inline_fill(argument_sizes, 0, (uint64_t)own_arguments * sizeof(uint32_t));
            }
        }
        if (caller->operation_count != 0)
        {
            tier0_inline_copy(operations, caller->operations, (uint64_t)caller->operation_count * sizeof(Tier0Operation));
        }
        if (caller->x87_constant_count != 0)
        {
            tier0_inline_copy(x87_constants, caller->x87_constants, (uint64_t)caller->x87_constant_count * 2 * sizeof(uint64_t));
        }
        if (caller->vector_constant_count != 0)
        {
            tier0_inline_copy(vector_constants, caller->vector_constants, (uint64_t)caller->vector_constant_count * 8 * sizeof(uint64_t));
            tier0_inline_copy(vector_constant_bytes, caller->vector_constant_bytes, caller->vector_constant_count);
        }
        // The temporary: a U64 value.
        if (plan->aggregates)
        {
            local_classes[plan->temporary] = BUSTER_TYPED_CLASS_U64;
            local_flags[plan->temporary] = 0;
            if (plan->sizes)
            {
                local_sizes[plan->temporary] = 0;
                local_alignments[plan->temporary] = 0;
            }
        }
        // Each distinct callee's locals per level (a parameter a plain local), operations and constants.
        for (uint32_t d = 0; d < plan->distinct_count; d += 1)
        {
            const Tier0InlineCallee* distinct = plan->distinct + d;
            const Tier0Function* callee = input->callees[distinct->index];
            const uint32_t count = callee->local_count;
            for (uint32_t level = 0; level < distinct->levels; level += 1)
            {
                const uint64_t base = distinct->local_base + (uint64_t)level * count;
                tier0_inline_copy(local_classes + base, callee->local_classes, count);
                tier0_inline_copy(local_flags + base, callee->local_flags, count);
                if (plan->sizes && callee->local_sizes != 0)
                {
                    tier0_inline_copy(local_sizes + base, callee->local_sizes, (uint64_t)count * sizeof(uint32_t));
                    tier0_inline_copy(local_alignments + base, callee->local_alignments, count);
                }
                else if (plan->sizes)
                {
                    tier0_inline_fill(local_sizes + base, 0, (uint64_t)count * sizeof(uint32_t));
                    tier0_inline_fill(local_alignments + base, 0, count);
                }
            }
            if (callee->operation_count != 0)
            {
                tier0_inline_copy(operations + distinct->operation_base, callee->operations, (uint64_t)callee->operation_count * sizeof(Tier0Operation));
            }
            if (callee->x87_constant_count != 0)
            {
                tier0_inline_copy(x87_constants + 2 * (uint64_t)distinct->x87_base, callee->x87_constants, (uint64_t)callee->x87_constant_count * 2 * sizeof(uint64_t));
            }
            if (callee->vector_constant_count != 0)
            {
                tier0_inline_copy(vector_constants + 8 * (uint64_t)distinct->vector_base, callee->vector_constants, (uint64_t)callee->vector_constant_count * 8 * sizeof(uint64_t));
                tier0_inline_copy(vector_constant_bytes + distinct->vector_base, callee->vector_constant_bytes, callee->vector_constant_count);
            }
        }
        // Each site's copy of its callee's call and argument rows, their arguments' and result objects' indices rebased.
        for (uint32_t s = 0; s < plan->site_count; s += 1)
        {
            const Tier0InlineSite* site = plan->sites + s;
            const Tier0Function* callee = input->callees[plan->distinct[site->callee].index];
            const uint32_t base = site->row_base;
            const uint32_t count = callee->call_count;
            if (count != 0)
            {
                tier0_inline_copy(call_targets + base, callee->call_targets, (uint64_t)count * sizeof(SymbolId));
                tier0_inline_copy(call_argument_counts + base, callee->call_argument_counts, (uint64_t)count * sizeof(uint32_t));
                tier0_inline_copy(call_variadic + base, callee->call_variadic, count);
                tier0_inline_copy(calls + base, callee->calls, (uint64_t)count * sizeof(AbiCall));
            }
            for (uint32_t c = 0; c < count; c += 1)
            {
                call_first[base + c] = callee->call_first[c] + site->argument_base;
            }
            for (uint32_t c = 0; plan->result_objects && c < count; c += 1)
            {
                const uint32_t object = callee->call_result_objects != 0 ? callee->call_result_objects[c] : 0;
                call_result_objects[base + c] = object != 0 ? object + site->local_base : 0;
            }
            for (uint32_t c = 0; call_asm != 0 && c < count; c += 1)
            {
                const uint32_t body = callee->call_asm != 0 ? callee->call_asm[c] : 0;
                call_asm[base + c] = body != 0 ? body + body_base : 0;
            }
            for (uint32_t b = 0; b < callee->asm_body_count; b += 1)
            {
                asm_bodies[body_base + b] = callee->asm_bodies[b];
                asm_bodies[body_base + b].block += callee->asm_bodies[b].block != BUSTER_TIER0_ASM_NO_BLOCK ? site->local_base : 0;
            }
            body_base += callee->asm_body_count;
            const uint32_t arguments_count = callee->argument_count;
            if (arguments_count != 0)
            {
                tier0_inline_copy(argument_locations + site->argument_base, callee->argument_locations, (uint64_t)arguments_count * sizeof(AbiLocation));
                tier0_inline_copy(argument_classes + site->argument_base, callee->argument_classes, arguments_count);
            }
            if (plan->argument_sizes && callee->argument_sizes != 0)
            {
                tier0_inline_copy(argument_sizes + site->argument_base, callee->argument_sizes, (uint64_t)arguments_count * sizeof(uint32_t));
            }
            else if (plan->argument_sizes)
            {
                tier0_inline_fill(argument_sizes + site->argument_base, 0, (uint64_t)arguments_count * sizeof(uint32_t));
            }
        }
        g->local_classes = local_classes;
        g->local_flags = local_flags;
        g->local_count = (uint32_t)locals;
        g->local_sizes = local_sizes;
        g->local_alignments = local_alignments;
        g->call_targets = call_targets;
        g->call_first = call_first;
        g->call_argument_counts = call_argument_counts;
        g->call_variadic = call_variadic;
        g->calls = calls;
        g->call_result_objects = call_result_objects;
        g->call_tails = call_tails;
        g->call_count = (uint32_t)rows;
        g->call_asm = call_asm;
        g->asm_bodies = asm_bodies;
        g->asm_body_count = (uint32_t)body_count;
        g->argument_locations = argument_locations;
        g->argument_classes = argument_classes;
        g->argument_sizes = argument_sizes;
        g->argument_count = (uint32_t)arguments;
        g->operations = operations;
        g->operation_count = (uint32_t)plan->operation_count;
        g->x87_constants = x87_constants;
        g->x87_constant_count = (uint32_t)plan->x87_count;
        g->vector_constants = vector_constants;
        g->vector_constant_bytes = vector_constant_bytes;
        g->vector_constant_count = (uint32_t)plan->vector_count;
    }
    return valid;
}

// The caller's labels, tables' jumps and addresses, remapped through the starts (entries of F to G's).
static bool tier0_inline_labels(const Tier0Function* caller, const Tier0InlinePlan* plan, Arena* arena, Tier0Function* g)
{
    uint32_t* labels = caller->label_count != 0 ? BUSTER_TIER0_INLINE_PUSH(arena, uint32_t, caller->label_count) : 0;
    uint32_t* jumps = caller->table_count != 0 && caller->table_jumps != 0 ? BUSTER_TIER0_INLINE_PUSH(arena, uint32_t, caller->table_count) : 0;
    uint32_t* addresses = caller->table_count != 0 && caller->table_addresses != 0 ? BUSTER_TIER0_INLINE_PUSH(arena, uint32_t, caller->table_count) : 0;
    const bool valid = (caller->label_count == 0 || labels != 0) && (caller->table_count == 0 || caller->table_jumps == 0 || jumps != 0) &&
                       (caller->table_count == 0 || caller->table_addresses == 0 || addresses != 0);
    for (uint32_t l = 0; valid && l < caller->label_count; l += 1)
    {
        const uint32_t entry = caller->label_entries[l];
        labels[l] = entry <= caller->count ? plan->starts[entry] : entry;
    }
    for (uint32_t t = 0; valid && jumps != 0 && t < caller->table_count; t += 1)
    {
        const uint32_t entry = caller->table_jumps[t];
        jumps[t] = entry <= caller->count ? plan->starts[entry] : entry;
    }
    for (uint32_t t = 0; valid && addresses != 0 && t < caller->table_count; t += 1)
    {
        const uint32_t entry = caller->table_addresses[t];
        addresses[t] = entry <= caller->count ? plan->starts[entry] : entry;
    }
    if (valid)
    {
        g->label_entries = labels != 0 ? labels : caller->label_entries;
        g->table_jumps = jumps != 0 ? jumps : caller->table_jumps;
        g->table_addresses = addresses != 0 ? addresses : caller->table_addresses;
    }
    return valid;
}

// The caller's entries [from, to) at G's `at`: 64 kinds and classes at a time, and their values 8 at
// a time, the branches' targets remapped lane by lane (a stream's branches are a small share of it); returns G's entry
// after them. Every load and store is masked to the segment.
static uint32_t tier0_inline_segment(const Tier0Function* caller, const Tier0InlinePlan* plan, uint32_t from, uint32_t to, Tier0InlineColumns* g, uint32_t at)
{
    const uint32_t count = caller->count;
    const __m512i first = _mm512_set1_epi8(BUSTER_TYPED_BRANCH_FALSE);
    const __m512i four = _mm512_set1_epi8(4);
    for (uint32_t e = from; e < to; e += 64)
    {
        const uint32_t take = to - e < 64 ? to - e : 64;
        const __mmask64 lanes = _bzhi_u64(UINT64_MAX, take);
        const __m512i kinds = _mm512_maskz_loadu_epi8(lanes, caller->kinds + e);
        _mm512_mask_storeu_epi8(g->kinds + at, lanes, kinds);
        _mm512_mask_storeu_epi8(g->classes + at, lanes, _mm512_maskz_loadu_epi8(lanes, caller->classes + e));
        for (uint32_t q = 0; q < take; q += 8)
        {
            const __mmask8 values = (__mmask8)_bzhi_u32(0xffu, take - q);
            _mm512_mask_storeu_epi64(g->values + at + q, values, _mm512_maskz_loadu_epi64(values, caller->values + e + q));
        }
        uint64_t branches = _mm512_mask_cmplt_epu8_mask(lanes, _mm512_sub_epi8(kinds, first), four);
        while (branches != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(branches);
            const uint64_t value = caller->values[e + lane];
            g->values[at + lane] = value <= count ? plan->starts[value] : value;
            branches &= branches - 1;
        }
        at += take;
    }
    return at;
}

// Site `site`'s copy of its callee's body at G's `at`, rebased (the vector path): a RETURN's lanes made a JUMP or a
// JUMP_VALUE to the end by masked moves of their kinds and values (step 5), each lane's meaning a vpermb of its kind (a
// CONST's by a compare of its class), its base a vpermq of the bases by meaning, a target past the body clamped to it,
// and an add; returns G's entry after it.
static uint32_t tier0_inline_body(const Tier0Function* callee, const Tier0InlinePlan* plan, const Tier0InlineSite* site, Tier0InlineColumns* g, uint32_t at)
{
    const uint32_t body = plan->distinct[site->callee].body;
    uint64_t base_words[BUSTER_TIER0_INLINE_MEANINGS];
    tier0_inline_bases(plan, site, base_words);
    const __m512i bases = _mm512_loadu_si512(base_words);
    const __m512i table = _mm512_loadu_si512(tier0_inline_meanings);
    const __m512i constant = _mm512_set1_epi8(BUSTER_TYPED_CONST);
    const __m512i f80 = _mm512_set1_epi8(BUSTER_TYPED_CLASS_F80);
    const __m512i vector = _mm512_set1_epi8(BUSTER_TYPED_CLASS_VECTOR);
    const __m512i entry = _mm512_set1_epi64(BUSTER_TIER0_INLINE_ENTRY);
    const __m512i end = _mm512_set1_epi64(body);
    for (uint32_t e = tier0_inline_skipped(site); e < body; e += 64)
    {
        const uint32_t take = body - e < 64 ? body - e : 64;
        const __mmask64 lanes = _bzhi_u64(UINT64_MAX, take);
        __m512i kinds = _mm512_maskz_loadu_epi8(lanes, callee->kinds + e);
        const __m512i classes = _mm512_maskz_loadu_epi8(lanes, callee->classes + e);
        const __mmask64 returns = _mm512_mask_cmpeq_epi8_mask(lanes, kinds, _mm512_set1_epi8(BUSTER_TYPED_RETURN));
        const __mmask64 voids = _mm512_cmpeq_epi8_mask(classes, _mm512_set1_epi8(BUSTER_TYPED_CLASS_VOID));
        kinds = _mm512_mask_mov_epi8(kinds, returns & voids, _mm512_set1_epi8(BUSTER_TYPED_JUMP));
        kinds = _mm512_mask_mov_epi8(kinds, returns & ~voids, _mm512_set1_epi8(BUSTER_TYPED_JUMP_VALUE));
        _mm512_mask_storeu_epi8(g->kinds + at, lanes, kinds);
        _mm512_mask_storeu_epi8(g->classes + at, lanes, classes);
        const __mmask64 constants = _mm512_cmpeq_epi8_mask(kinds, constant);
        __m512i meanings = _mm512_permutexvar_epi8(kinds, table);
        meanings = _mm512_mask_mov_epi8(meanings, constants & _mm512_cmpeq_epi8_mask(classes, f80), _mm512_set1_epi8(BUSTER_TIER0_INLINE_X87));
        meanings = _mm512_mask_mov_epi8(meanings, constants & _mm512_cmpeq_epi8_mask(classes, vector), _mm512_set1_epi8(BUSTER_TIER0_INLINE_VECTOR));
        uint8_t meaning_bytes[64];
        _mm512_storeu_si512(meaning_bytes, meanings);
        for (uint32_t q = 0; q < take; q += 8)
        {
            const __mmask8 valid = (__mmask8)_bzhi_u32(0xffu, take - q);
            const __m512i meaning = _mm512_cvtepu8_epi64(_mm_loadl_epi64((const __m128i*)(meaning_bytes + q)));
            __m512i values = _mm512_maskz_loadu_epi64(valid, callee->values + e + q);
            values = _mm512_mask_mov_epi64(values, (__mmask8)(returns >> q), end);
            values = _mm512_mask_min_epu64(values, _mm512_cmpeq_epi64_mask(meaning, entry), values, end);
            values = _mm512_add_epi64(values, _mm512_permutexvar_epi64(meaning, bases));
            _mm512_mask_storeu_epi64(g->values + at + q, valid, values);
        }
        at += take;
    }
    return at;
}

// G's entries from the plan: each position's segment of the caller and its insertions. Returns G's count.
static uint32_t tier0_inline_entries(const Tier0InlineInput* input, const Tier0InlinePlan* plan, Tier0InlineColumns* g)
{
    const Tier0Function* caller = input->caller;
    uint32_t at = 0;
    uint32_t cursor = 0;
    for (uint32_t p = 0; p < plan->position_count; p += 1)
    {
        const uint32_t position = plan->positions[p];
        at = tier0_inline_segment(caller, plan, cursor, position, g, at);
        cursor = position;
        for (uint32_t i = plan->heads[position]; i != 0; i = plan->insertions[i - 1].next)
        {
            const Tier0InlineInsertion* insertion = plan->insertions + i - 1;
            const Tier0InlineSite* site = plan->sites + insertion->site;
            if (insertion->argument != BUSTER_TIER0_INLINE_CALL_INSERTION)
            {
                at = tier0_inline_bind(caller, plan, site, insertion->argument, g, at);
            }
            else
            {
                const Tier0Function* callee = input->callees[plan->distinct[site->callee].index];
                const uint32_t n = caller->call_argument_counts[site->row];
                at = n != 0 ? tier0_inline_bind(caller, plan, site, n - 1, g, at) : at;
                if (site->hidden != 0)
                {
                    tier0_inline_put(g, at, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, caller->call_result_objects[site->row] - 1u);
                    tier0_inline_put(g, at + 1, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, site->local_base);
                    at += 2;
                }
                at = tier0_inline_body(callee, plan, site, g, at);
                at = tier0_inline_result(caller, callee, plan, site, g, at);
                cursor = position + 1;
            }
        }
    }
    at = tier0_inline_segment(caller, plan, cursor, caller->count, g, at);
    return at;
}

// Forwarding (docs/tier0-inlining.md's later steps 1 and 2): the objects an inlined call moves its values through
// taken out of G. Two shapes carry a value from one entry to the next through a local x:
//   - A: SET_LOCAL x (VECTOR), LOCAL_ADDRESS x, LOAD (VECTOR, x's bytes): a vector stored into the object x and loaded
//     back through its address, as a vector argument's temporary and its binding, and a callee's result object and the
//     call's result's binding. It becomes nothing: the vector stays on the stack.
//   - B: SET_LOCAL x, LOCAL x, of one class: a value stored and read right back, as a scalar parameter's binding and its
//     callee's first read of it, and a call's result object and the caller's copy of it. It becomes nothing: the value
//     stays on the stack.
// A local is forwarded where every reference to it in G (its entries, an ATOMIC_CAS's, a kept call's result object) is
// in such a shape whose entries but the first no branch or label reaches: its contents are then only ever read right
// after they are written, by the shape's own read. With -g's source rows (Tier0Function.entry_records), only the
// locals from `first` (the callees' copies, not the caller's own, which the debugger shows) are. The pass is one scan of
// G's entries and a histogram of its locals, shared by both paths: it runs once a spliced body, over entries the splice
// has just written.
static uint32_t tier0_inline_shape(const Tier0Function* g, const uint64_t* targets, uint32_t e)
{
    const uint32_t count = g->count;
    const uint8_t* kinds = g->kinds;
    const uint8_t* classes = g->classes;
    const uint64_t* values = g->values;
    uint32_t shape = 0;
    // An asm result's SET_LOCAL belongs to its indivisible capture group (#2144), even when the next row returns it.
    if (kinds[e] == BUSTER_TYPED_SET_LOCAL && e + 1 < count && (targets[(e + 1) / 64] >> ((e + 1) % 64) & 1) == 0 && !tier0_asm_result_interior(g, e))
    {
        const uint64_t x = values[e];
        const bool b = kinds[e + 1] == BUSTER_TYPED_LOCAL && classes[e + 1] == classes[e] && values[e + 1] == x;
        const bool a = !b && e + 2 < count && classes[e] == BUSTER_TYPED_CLASS_VECTOR && kinds[e + 1] == BUSTER_TYPED_LOCAL_ADDRESS && values[e + 1] == x &&
                       kinds[e + 2] == BUSTER_TYPED_LOAD && classes[e + 2] == BUSTER_TYPED_CLASS_VECTOR && x < g->local_count && g->local_sizes != 0 &&
                       g->local_sizes[x] == values[e + 2] && (targets[(e + 2) / 64] >> ((e + 2) % 64) & 1) == 0;
        shape = b ? 2u : a ? 3u : 0u;
    }
    return shape;
}

static bool tier0_inline_forward(Tier0Function* g, Tier0InlineColumns* columns, uint32_t first, uint32_t* starts, uint32_t start_count, Arena* arena)
{
    const uint64_t mark = arena->length;
    const uint32_t count = g->count;
    const uint32_t locals = g->local_count;
    uint64_t* targets = BUSTER_TIER0_INLINE_PUSH(arena, uint64_t, (uint64_t)count / 64 + 1);
    uint32_t* refs = BUSTER_TIER0_INLINE_PUSH(arena, uint32_t, locals);
    uint32_t* covered = BUSTER_TIER0_INLINE_PUSH(arena, uint32_t, locals);
    uint32_t* index = BUSTER_TIER0_INLINE_PUSH(arena, uint32_t, (uint64_t)count + 1);
    bool valid = targets != 0 && refs != 0 && covered != 0 && index != 0;
    uint32_t kept = count;
    if (valid)
    {
        tier0_inline_fill(targets, 0, ((uint64_t)count / 64 + 1) * sizeof(uint64_t));
        tier0_inline_fill(refs, 0, (uint64_t)locals * sizeof(uint32_t));
        tier0_inline_fill(covered, 0, (uint64_t)locals * sizeof(uint32_t));
        for (uint32_t e = 0; e < count; e += 1)
        {
            const uint32_t kind = g->kinds[e];
            const uint64_t value = g->values[e];
            const uint32_t meaning = tier0_inline_meaning(kind, g->classes[e]);
            const uint64_t local = meaning == BUSTER_TIER0_INLINE_LOCAL ? value : meaning == BUSTER_TIER0_INLINE_CAS ? value >> BUSTER_TYPED_CAS_LOCAL_SHIFT : UINT64_MAX;
            const bool call = (kind == BUSTER_TYPED_CALL || kind == BUSTER_TYPED_TAIL_CALL) && value < g->call_count && g->call_result_objects != 0 && g->call_result_objects[value] != 0;
            const uint64_t object = call ? g->call_result_objects[value] - 1u : UINT64_MAX;
            refs[local < locals ? local : 0] += local < locals ? 1u : 0u;
            refs[object < locals ? object : 0] += object < locals ? 1u : 0u;
            if (tier0_inline_branch(kind) && value <= count)
            {
                targets[value / 64] |= UINT64_C(1) << (value % 64);
            }
        }
        for (uint32_t l = 0; l < g->label_count; l += 1)
        {
            const uint32_t entry = g->label_entries[l];
            targets[entry / 64] |= entry <= count ? UINT64_C(1) << (entry % 64) : 0;
        }
        for (uint32_t t = 0; t < g->table_count; t += 1)
        {
            const uint32_t jump = g->table_jumps != 0 ? g->table_jumps[t] : count;
            const uint32_t address = g->table_addresses != 0 ? g->table_addresses[t] : count;
            targets[jump / 64] |= jump <= count ? UINT64_C(1) << (jump % 64) : 0;
            targets[address / 64] |= address <= count ? UINT64_C(1) << (address % 64) : 0;
        }
        for (uint32_t e = 0; e < count; e += 1)
        {
            const uint32_t shape = tier0_inline_shape(g, targets, e);
            const uint64_t x = g->values[e];
            covered[shape != 0 && x < locals ? x : 0] += shape != 0 && x < locals ? 2u : 0u;
        }
        // The entries kept: index[e] is entry e's place in the forwarded stream, or the next kept entry's.
        uint32_t drop = 0;
        uint32_t w = 0;
        for (uint32_t e = 0; e < count; e += 1)
        {
            const uint32_t shape = tier0_inline_shape(g, targets, e);
            const uint64_t x = g->values[e];
            const bool forward = shape != 0 && x < locals && x >= first && refs[x] == covered[x] && (g->local_flags[x] & BUSTER_TYPED_LOCAL_ADDRESS_ONLY) == 0;
            drop = forward ? (shape == 3u ? 3u : 2u) : drop;
            index[e] = w;
            w += drop == 0 ? 1u : 0u;
            drop -= drop != 0 ? 1u : 0u;
        }
        index[count] = w;
        kept = w;
    }
    if (valid && kept != count)
    {
        uint32_t w = 0;
        for (uint32_t e = 0; e < count; e += 1)
        {
            const bool keep = index[e + 1] != index[e];
            const uint32_t kind = columns->kinds[e];
            const uint64_t value = columns->values[e];
            columns->kinds[w] = (uint8_t)kind;
            columns->classes[w] = columns->classes[e];
            columns->values[w] = tier0_inline_branch(kind) && value <= count ? index[value] : value;
            w += keep ? 1u : 0u;
        }
        uint32_t* labels = (uint32_t*)g->label_entries;
        uint32_t* jumps = (uint32_t*)g->table_jumps;
        uint32_t* addresses = (uint32_t*)g->table_addresses;
        for (uint32_t l = 0; l < g->label_count; l += 1)
        {
            labels[l] = labels[l] <= count ? index[labels[l]] : labels[l];
        }
        for (uint32_t t = 0; t < g->table_count; t += 1)
        {
            if (jumps != 0)
            {
                jumps[t] = jumps[t] <= count ? index[jumps[t]] : jumps[t];
            }
            if (addresses != 0)
            {
                addresses[t] = addresses[t] <= count ? index[addresses[t]] : addresses[t];
            }
        }
        for (uint32_t i = 0; i < start_count; i += 1)
        {
            starts[i] = index[starts[i]];
        }
        g->count = kept;
    }
    arena_pop_to(arena, mark);
    return valid;
}

// Plan the real homes without measuring or emitting code. The normalized function and every job column stay in
// scratch for the comparison; decorated accesses are stripped exactly as tier0_compile_segments strips them.
static bool tier0_inline_home_plan(const Tier0Function* function, Arena* scratch, Tier0Function* normalized, Tier0Job* job)
{
    *normalized = *function;
    uint8_t* segments = 0;
    bool valid = tier0_initialize();
    if (valid && (function->segments || function->volatiles))
    {
        uint64_t* values = BUSTER_TIER0_INLINE_PUSH(scratch, uint64_t, function->count);
        segments = BUSTER_TIER0_INLINE_PUSH(scratch, uint8_t, function->count);
        valid = values != 0 && segments != 0;
        if (valid)
        {
            tier0_segments_strip(function, values, segments);
            normalized->values = values;
            normalized->segments = false;
            normalized->volatiles = false;
        }
    }
    tier0_job_begin(job, normalized, function->segments || function->x87 ? segments : 0, scratch);
    if (valid)
    {
        tier0_job_front(job, scratch);
        tier0_job_homes(job, scratch);
        valid = job->error == TIER0_OK;
    }
    else
    {
        job->error = TIER0_ERROR_MEMORY;
    }
    return valid;
}

// Collect register-homed locals referenced in [first, end). Homes are unique per register and register bank, so a
// 32-bit set deduplicates the sparse list (at most 32 locals), without a local-count-sized annotation column.
static void tier0_inline_range_homes(const Tier0Function* function, const Tier0Job* job, uint32_t first, uint32_t end, uint32_t* seen, uint32_t* locals, uint32_t* count)
{
    const uint64_t kinds_referring = UINT64_C(1) << BUSTER_TYPED_LOCAL | UINT64_C(1) << BUSTER_TYPED_LOCAL_ADDRESS |
                                     UINT64_C(1) << BUSTER_TYPED_STORE_LOCAL | UINT64_C(1) << BUSTER_TYPED_SET_LOCAL |
                                     UINT64_C(1) << BUSTER_TYPED_ATOMIC_CAS | UINT64_C(1) << BUSTER_TYPED_CALL;
    const __m512i referring = _mm512_set1_epi64((int64_t)kinds_referring);
    const uint8_t* kinds = function->kinds;
    const uint64_t* values = function->values;
    const Tier0Home* homes = job->homes;
    const uint32_t* references = job->references;
    const uint8_t* classes = function->local_classes;
    for (uint32_t at = first; at < end; at += 64)
    {
        const __mmask64 lanes = tier0_lanes(at, end);
        uint64_t bits = _mm512_mask_bitshuffle_epi64_mask(lanes, referring, _mm512_maskz_loadu_epi8(lanes, kinds + at));
        while (bits != 0)
        {
            const uint32_t entry = at + (uint32_t)__builtin_ctzll(bits);
            bits &= bits - 1;
            const uint32_t kind = kinds[entry];
            const uint64_t value = values[entry];
            const bool call = kind == BUSTER_TYPED_CALL;
            const bool hidden = call && function->call_result_objects != 0 && function->call_result_objects[value] == BUSTER_TIER0_RESULT_HIDDEN;
            const uint32_t local = call ? 0 : (uint32_t)(value >> (kind == BUSTER_TYPED_ATOMIC_CAS ? BUSTER_TYPED_CAS_LOCAL_SHIFT : 0));
            if ((!call || hidden) && local < function->local_count && references[local] != 0 && homes[local].is_register)
            {
                const uint32_t reg = homes[local].reg + (tier0_class_floating(classes[local]) ? BUSTER_TIER0_XMM(0) : 0);
                const uint32_t bit = 1u << reg;
                if ((*seen & bit) == 0)
                {
                    locals[*count] = local;
                    *count += 1;
                    *seen |= bit;
                }
            }
        }
    }
}

// The register homes used inside the union of backward-edge ranges. Descending edge ends let each range visit only
// the prefix not already covered by a later edge, so nested and overlapping loops scan each covered entry once.
// This protects existing homes; it assigns no loop weights and makes no assumption about a loop's frequency.
static uint32_t tier0_inline_loop_homes(const Tier0Function* function, const Tier0Job* job, uint32_t* locals)
{
    uint32_t covered_first = function->count;
    uint32_t seen = 0;
    uint32_t count = 0;
    for (uint32_t end = function->count; end != 0;)
    {
        const uint32_t first = (end - 1) & ~63u;
        uint64_t branches = tier0_inline_branches(function, first, end);
        while (branches != 0)
        {
            const uint32_t lane = 63u - (uint32_t)__builtin_clzll(branches);
            branches &= ~(UINT64_C(1) << lane);
            const uint32_t entry = first + lane;
            const uint64_t target = function->values[entry];
            if (target <= entry)
            {
                const uint32_t past = entry + 1 < covered_first ? entry + 1 : covered_first;
                tier0_inline_range_homes(function, job, (uint32_t)target, past, &seen, locals, &count);
                covered_first = target < covered_first ? (uint32_t)target : covered_first;
            }
        }
        end = first;
    }
    return count;
}

// The shapes' loop-home guard (#2841). Compare the actual allocation after forwarding with each expanded callee's
// standalone allocation. Local IDs survive forwarding; a zero reference count permits an eliminated local. The
// plan shares locals per distinct callee and argument-nesting level, and every retained level must preserve them.
static bool tier0_inline_homes_preserved(const Tier0InlineInput* input, const Tier0InlinePlan* plan, const Tier0Function* function, Arena* arena)
{
    bool loops = false;
    for (uint32_t d = 0; !loops && d < plan->distinct_count; d += 1)
    {
        const Tier0InlineCallee* distinct = plan->distinct + d;
        loops = distinct->levels != 0 && tier0_inline_has_loop(input->callees[distinct->index]);
    }
    bool preserved = true;
    if (loops)
    {
        Scratch scratch = scratch_begin(arena);
        Tier0Function normalized;
        Tier0Job spliced;
        preserved = tier0_inline_home_plan(function, scratch.arena, &normalized, &spliced);
        for (uint32_t d = 0; preserved && d < plan->distinct_count; d += 1)
        {
            const Tier0InlineCallee* distinct = plan->distinct + d;
            const Tier0Function* callee = input->callees[distinct->index];
            if (distinct->levels != 0 && tier0_inline_has_loop(callee))
            {
                const uint64_t mark = scratch.arena->length;
                Tier0Function own;
                Tier0Job outlined;
                preserved = tier0_inline_home_plan(callee, scratch.arena, &own, &outlined);
                uint32_t locals[BUSTER_TIER0_INLINE_REGISTER_HOMES];
                const uint32_t count = preserved ? tier0_inline_loop_homes(&own, &outlined, locals) : 0;
                for (uint32_t level = 0; preserved && level < distinct->levels; level += 1)
                {
                    const uint32_t base = distinct->local_base + level * callee->local_count;
                    for (uint32_t k = 0; preserved && k < count; k += 1)
                    {
                        const uint32_t local = base + locals[k];
                        preserved = spliced.references[local] == 0 || spliced.homes[local].is_register;
                    }
                }
                arena_pop_to(scratch.arena, mark);
            }
        }
        scratch_end(scratch);
    }
    return preserved;
}

// What a splice made: G (`function`), the starts in G of the caller's entries and its end (count + 1 of them), and how
// many calls it inlined.
typedef struct Tier0Inlined Tier0Inlined;
struct Tier0Inlined
{
    Tier0Function function;
    const uint32_t* starts;
    uint32_t sites;
};

// The splice of `input` into *result in `arena` (G's columns, which the result's function points into, and the starts),
// and tier0_validate's check of G: false, and `arena` as it was, where there is no site, no room, or G breaks the
// stream's rules (the caller then keeps its calls).
static bool tier0_inline_attempt(const Tier0InlineInput* input, bool keep_loops, Arena* arena, Tier0Inlined* result, bool* retry)
{
    const uint64_t mark = arena->length;
    const Tier0Function* caller = input->caller;
    Tier0InlinePlan plan;
    bool valid = tier0_inline_plan(input, keep_loops, arena, &plan);
    Tier0Function g = valid ? *caller : (Tier0Function){ 0 };
    uint8_t* kinds = valid ? BUSTER_TIER0_INLINE_PUSH(arena, uint8_t, plan.count) : 0;
    uint8_t* classes = valid ? BUSTER_TIER0_INLINE_PUSH(arena, uint8_t, plan.count) : 0;
    uint64_t* values = valid ? BUSTER_TIER0_INLINE_PUSH(arena, uint64_t, plan.count) : 0;
    valid = valid && kinds != 0 && classes != 0 && values != 0 && tier0_inline_tables(input, &plan, arena, &g) && tier0_inline_labels(caller, &plan, arena, &g);
    if (valid)
    {
        Tier0InlineColumns columns = { .kinds = kinds, .classes = classes, .values = values };
        const uint32_t written = tier0_inline_entries(input, &plan, &columns);
        g.kinds = kinds;
        g.classes = classes;
        g.values = values;
        g.count = (uint32_t)plan.count;
        g.vectors = plan.vectors;
        g.masks = plan.masks;
        g.x87 = plan.x87;
        g.int128 = plan.int128;
        g.segments = plan.segments;
        g.volatiles = plan.volatiles;
        g.stack_protector = plan.stack_protector;
        g.stack_check_fail = plan.stack_check_fail;
        g.vector_bytes = plan.transport;
        // Entry-indexed IR annotations belong to the original streams. This splice reaches only Tier 0.
        g.predictable_branches = NULL;
        g.lifetime_entries = 0;
        g.lifetime_locals = 0;
        g.lifetime_count = 0;
        valid = written == g.count &&
                tier0_inline_forward(&g, &columns, caller->entry_records ? caller->local_count : 0, plan.starts, caller->count + 1, arena) &&
                tier0_validate(&g).error == TIER0_OK;
    }
    *retry = valid && input->shapes && !keep_loops && !tier0_inline_homes_preserved(input, &plan, &g, arena);
    valid = valid && !*retry;
    if (valid)
    {
        *result = (Tier0Inlined){ .function = g, .starts = plan.starts, .sites = plan.site_count };
    }
    else
    {
        arena_pop_to(arena, mark);
    }
    return valid;
}

// A failed home check retries once with loop-bearing callees outlined, retaining straight-line wrapper inlining.
// Restoring a call can itself consume registers another loop needed; holding all loop callees avoids an unbounded
// allocation/retry fixpoint. The default (shapes off) does no home analysis and preserves its existing stream.
static bool tier0_inline(const Tier0InlineInput* input, Arena* arena, Tier0Inlined* result)
{
    bool retry = false;
    bool valid = tier0_inline_attempt(input, false, arena, result, &retry);
    if (retry)
    {
        valid = tier0_inline_attempt(input, true, arena, result, &retry);
    }
    return valid;
}

// Tier 0's code `code` of a splice's G, with its entries' first records (Tier0Code.entry_records, G's count + 1 of
// them, where the compile kept them) and its locals' homes in terms of the caller's: entry i's first record is its
// start's in G (an inlined call's bindings and body are its CALL's), and the caller's locals are G's first ones.
static void tier0_inline_code(Tier0Code* code, const Tier0Function* caller, const Tier0Inlined* inlined, Arena* arena)
{
    if (code->error == TIER0_OK && code->entry_records != 0)
    {
        uint32_t* records = BUSTER_TIER0_INLINE_PUSH(arena, uint32_t, (uint64_t)caller->count + 1);
        if (records != 0)
        {
            for (uint32_t e = 0; e <= caller->count; e += 1)
            {
                records[e] = code->entry_records[inlined->starts[e]];
            }
            code->entry_records = records;
            code->entry_count = caller->count;
        }
        else
        {
            code->error = TIER0_ERROR_MEMORY;
        }
    }
    if (code->error == TIER0_OK && code->local_homes != 0)
    {
        code->local_count = caller->local_count;
    }
}

// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Bounded structural IR delta debugging, separate from production optimization. Candidate order is deterministic;
// every candidate passes both complete verifiers before a bounded built-in predicate sees it. Only an identical
// classified failure is accepted. Returned faults, limits, unsupported observations and unrelated failures differ.
// Four disjoint scratch views own best/candidate/work/cache storage; only the final function escapes into output.
// Include after ir_rebuild.c. Callbacks are a test-tool boundary, never a compiler execution-path dispatcher.
#include <time.h>

typedef enum IrReduceObservationStatus
{
    IR_REDUCE_OBSERVED,
    IR_REDUCE_UNSUPPORTED,
    IR_REDUCE_PREDICATE_LIMIT,
    IR_REDUCE_PREDICATE_FAULT,
    IR_REDUCE_PREDICATE_ERROR,
} IrReduceObservationStatus;

typedef struct IrReduceFailure IrReduceFailure;
struct IrReduceFailure
{
    // kind zero means no failure. The owner defines stable semantic classes, never an instruction's changing ID.
    uint32_t kind;
    uint32_t pass;
    uint32_t detail;
};

typedef struct IrReduceObservation IrReduceObservation;
struct IrReduceObservation
{
    IrReduceObservationStatus status;
    IrReduceFailure failure;
};

typedef struct IrReduceLimits IrReduceLimits;
struct IrReduceLimits
{
    uint64_t max_candidates;
    uint64_t max_milliseconds;
    uint64_t predicate_milliseconds;
    uint64_t memory_bytes;
    uint64_t execution_steps;
    uint32_t call_frames;
};

typedef IrReduceObservation (*IrReducePredicate)(const IrFunction*, const IrReduceLimits*, void*, Arena*);

static __attribute__((unused)) IrReduceLimits ir_reduce_limits(void)
{
    const IrReduceLimits result = {
        .max_candidates = 512,
        .max_milliseconds = 10000,
        .predicate_milliseconds = 1000,
        .memory_bytes = UINT64_C(64) << 20,
        .execution_steps = 100000,
        .call_frames = 32,
    };
    return result;
}

typedef enum IrReduceStatus
{
    IR_REDUCE_COMPLETE,
    IR_REDUCE_CANDIDATE_LIMIT,
    IR_REDUCE_TIME_LIMIT,
    IR_REDUCE_CACHE_LIMIT,
    IR_REDUCE_NOT_FAILING,
    IR_REDUCE_ORACLE_UNAVAILABLE,
    IR_REDUCE_INVALID,
    IR_REDUCE_RESOURCE,
} IrReduceStatus;

typedef struct IrReduceScore IrReduceScore;
struct IrReduceScore
{
    uint64_t blocks;
    uint64_t instructions;
    uint64_t metadata;
    uint64_t constant_bits;
    uint64_t constant_ones;
};

typedef struct IrReduceResult IrReduceResult;
struct IrReduceResult
{
    IrFunction function;
    IrReduceStatus status;
    IrReduceFailure failure;
    IrReduceScore original;
    IrReduceScore reduced;
    uint64_t candidates;
    uint64_t predicates;
    uint64_t cache_hits;
    uint64_t accepted;
    uint64_t invalid;
    uint64_t different;
    uint64_t faults;
    uint64_t limits;
};

typedef struct IrReduceCache IrReduceCache;
struct IrReduceCache
{
    IrReduceCache* next;
    uint8_t* bytes;
    uint64_t length;
    uint64_t hash;
    IrReduceObservation observation;
};

typedef struct IrReduceWork IrReduceWork;
struct IrReduceWork
{
    Arena best;
    Arena candidate;
    Arena scratch;
    Arena cache;
    IrReduceCache* first;
    IrReduceLimits limits;
    IrReducePredicate predicate;
    void* context;
    uint64_t started;
    IrReduceResult result;
};

static uint64_t ir_reduce_milliseconds(void)
{
    struct timespec now = { 0 };
    const bool valid = clock_gettime(CLOCK_MONOTONIC, &now) == 0 && now.tv_sec >= 0;
    const uint64_t result = valid ? (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000 : UINT64_MAX;
    return result;
}

static bool ir_reduce_same_failure(IrReduceFailure a, IrReduceFailure b)
{
    const bool result = a.kind == b.kind && a.pass == b.pass && a.detail == b.detail;
    return result;
}

static bool ir_reduce_resource(IrError error)
{
    const bool result = error == IR_ERROR_MEMORY || error == IR_ERROR_LIMIT;
    return result;
}

static IrReduceScore ir_reduce_score(const IrFunction* function)
{
    IrReduceScore result = {
        .blocks = function->block_count,
        .instructions = function->instruction_count,
        .metadata = (uint64_t)function->extra_count + function->constant_count + function->slot_count +
                    function->address_count + function->call_count + function->call_type_count,
    };
    for (uint32_t i = 0; i < function->constant_count; i += 1)
    {
        const IrConstant value = function->constants[i];
        result.constant_bits += value.high != 0 ? 128u - (uint32_t)__builtin_clzll(value.high) :
                                value.low != 0 ? 64u - (uint32_t)__builtin_clzll(value.low) : 0;
        result.constant_ones += (uint32_t)__builtin_popcountll(value.low) + (uint32_t)__builtin_popcountll(value.high);
    }
    return result;
}

static bool ir_reduce_smaller(IrReduceScore a, IrReduceScore b)
{
    const bool result = a.blocks != b.blocks ? a.blocks < b.blocks :
                        a.instructions != b.instructions ? a.instructions < b.instructions :
                        a.metadata != b.metadata ? a.metadata < b.metadata :
                        a.constant_bits != b.constant_bits ? a.constant_bits < b.constant_bits : a.constant_ones < b.constant_ones;
    return result;
}

static IrError ir_reduce_copy(const IrFunction* source, Arena* output, IrFunction* result)
{
    const IrSizes sizes = {
        .instruction_count = source->instruction_count,
        .block_count = source->block_count,
        .extra_count = source->extra_count,
        .constant_count = source->constant_count,
        .slot_count = source->slot_count,
        .address_count = source->address_count,
        .call_count = source->call_count,
        .call_type_count = source->call_type_count,
        .parameter_count = source->parameter_count,
    };
    IrFunction copy = { 0 };
    const IrError error = ir_create(output, &sizes, &copy);
    if (error == IR_OK)
    {
        copy.result_type = source->result_type;
        copy.symbol_count = source->symbol_count;
#define BUSTER_IR_REDUCE_COPY(field, count) if ((count) != 0) { memcpy(copy.field, source->field, (uint64_t)(count) * sizeof(*copy.field)); }
        BUSTER_IR_REDUCE_COPY(op, source->instruction_count);
        BUSTER_IR_REDUCE_COPY(type, source->instruction_count);
        BUSTER_IR_REDUCE_COPY(flags, source->instruction_count);
        BUSTER_IR_REDUCE_COPY(alignment_log2, source->instruction_count);
        BUSTER_IR_REDUCE_COPY(a, source->instruction_count);
        BUSTER_IR_REDUCE_COPY(b, source->instruction_count);
        BUSTER_IR_REDUCE_COPY(c, source->instruction_count);
        BUSTER_IR_REDUCE_COPY(block_offsets, (uint64_t)source->block_count + 1);
        BUSTER_IR_REDUCE_COPY(extra, source->extra_count);
        BUSTER_IR_REDUCE_COPY(constants, source->constant_count);
        BUSTER_IR_REDUCE_COPY(slots, source->slot_count);
        BUSTER_IR_REDUCE_COPY(addresses, source->address_count);
        BUSTER_IR_REDUCE_COPY(calls, source->call_count);
        BUSTER_IR_REDUCE_COPY(call_types, source->call_type_count);
        BUSTER_IR_REDUCE_COPY(parameter_types, source->parameter_count);
#undef BUSTER_IR_REDUCE_COPY
        *result = copy;
    }
    return error;
}

// Exact field serialization excludes C struct padding and arena addresses, but includes unused metadata and the
// symbol bound. A text-only cache would conflate cases the canonical printer intentionally normalizes away.
static uint8_t* ir_reduce_key(const IrFunction* function, Arena* arena, uint64_t* length, uint64_t* hash)
{
    const uint64_t bytes = 41 + (uint64_t)function->instruction_count * 16 + ((uint64_t)function->block_count + 1) * 4 +
        (uint64_t)function->extra_count * 4 + (uint64_t)function->constant_count * 16 + (uint64_t)function->slot_count * 14 +
        (uint64_t)function->address_count * 9 + (uint64_t)function->call_count * 18 + function->call_type_count + function->parameter_count;
    uint8_t* result = arena_push(arena, bytes, 1);
    if (result != NULL)
    {
        uint8_t* at = result;
#define BUSTER_IR_REDUCE_KEY(value, type) { const type encoded = (type)(value); memcpy(at, &encoded, sizeof(encoded)); at += sizeof(encoded); }
        BUSTER_IR_REDUCE_KEY(function->instruction_count, uint32_t);
        BUSTER_IR_REDUCE_KEY(function->block_count, uint32_t);
        BUSTER_IR_REDUCE_KEY(function->extra_count, uint32_t);
        BUSTER_IR_REDUCE_KEY(function->constant_count, uint32_t);
        BUSTER_IR_REDUCE_KEY(function->slot_count, uint32_t);
        BUSTER_IR_REDUCE_KEY(function->address_count, uint32_t);
        BUSTER_IR_REDUCE_KEY(function->call_count, uint32_t);
        BUSTER_IR_REDUCE_KEY(function->call_type_count, uint32_t);
        BUSTER_IR_REDUCE_KEY(function->parameter_count, uint32_t);
        BUSTER_IR_REDUCE_KEY(function->result_type, uint8_t);
        BUSTER_IR_REDUCE_KEY(function->symbol_count, uint32_t);
        for (uint32_t i = 0; i < function->instruction_count; i += 1)
        {
            BUSTER_IR_REDUCE_KEY(function->op[i], uint8_t);
            BUSTER_IR_REDUCE_KEY(function->type[i], uint8_t);
            BUSTER_IR_REDUCE_KEY(function->flags[i], uint8_t);
            BUSTER_IR_REDUCE_KEY(function->alignment_log2[i], uint8_t);
            BUSTER_IR_REDUCE_KEY(function->a[i], uint32_t);
            BUSTER_IR_REDUCE_KEY(function->b[i], uint32_t);
            BUSTER_IR_REDUCE_KEY(function->c[i], uint32_t);
        }
        for (uint32_t i = 0; i <= function->block_count; i += 1)
        {
            BUSTER_IR_REDUCE_KEY(function->block_offsets[i], uint32_t);
        }
        for (uint32_t i = 0; i < function->extra_count; i += 1)
        {
            BUSTER_IR_REDUCE_KEY(function->extra[i], uint32_t);
        }
        for (uint32_t i = 0; i < function->constant_count; i += 1)
        {
            BUSTER_IR_REDUCE_KEY(function->constants[i].low, uint64_t);
            BUSTER_IR_REDUCE_KEY(function->constants[i].high, uint64_t);
        }
        for (uint32_t i = 0; i < function->slot_count; i += 1)
        {
            BUSTER_IR_REDUCE_KEY(function->slots[i].size, uint64_t);
            BUSTER_IR_REDUCE_KEY(function->slots[i].alignment, uint32_t);
            BUSTER_IR_REDUCE_KEY(function->slots[i].type, uint8_t);
            BUSTER_IR_REDUCE_KEY(function->slots[i].flags, uint8_t);
        }
        for (uint32_t i = 0; i < function->address_count; i += 1)
        {
            BUSTER_IR_REDUCE_KEY(function->addresses[i].offset, uint64_t);
            BUSTER_IR_REDUCE_KEY(function->addresses[i].scale, uint8_t);
        }
        for (uint32_t i = 0; i < function->call_count; i += 1)
        {
            const IrCall* call = function->calls + i;
            BUSTER_IR_REDUCE_KEY(call->argument_first, uint32_t);
            BUSTER_IR_REDUCE_KEY(call->argument_count, uint32_t);
            BUSTER_IR_REDUCE_KEY(call->type_first, uint32_t);
            BUSTER_IR_REDUCE_KEY(call->parameter_count, uint32_t);
            BUSTER_IR_REDUCE_KEY(call->result_type, uint8_t);
            BUSTER_IR_REDUCE_KEY(call->variadic, uint8_t);
        }
        for (uint32_t i = 0; i < function->call_type_count; i += 1)
        {
            BUSTER_IR_REDUCE_KEY(function->call_types[i], uint8_t);
        }
        for (uint32_t i = 0; i < function->parameter_count; i += 1)
        {
            BUSTER_IR_REDUCE_KEY(function->parameter_types[i], uint8_t);
        }
#undef BUSTER_IR_REDUCE_KEY
        assert((uint64_t)(at - result) == bytes);
        uint64_t value = UINT64_C(14695981039346656037);
        for (uint64_t i = 0; i < bytes; i += 1)
        {
            value = (value ^ result[i]) * UINT64_C(1099511628211);
        }
        *length = bytes;
        *hash = value;
    }
    return result;
}

// Built-in predicates run directly with a private scratch suffix. They must be deterministic and honor the
// supplied execution/resource budgets; native-process predicates are outside this initial tool boundary.
static IrReduceObservation ir_reduce_predicate(const IrFunction* function, IrReduceWork* work)
{
    IrReduceObservation result = { .status = IR_REDUCE_PREDICATE_ERROR };
    const uint64_t started = ir_reduce_milliseconds();
    Arena staging = work->scratch;
    uint8_t* base = arena_push(&staging, 0, 64);
    if (base != NULL && started != UINT64_MAX && started - work->started < work->limits.max_milliseconds)
    {
        Arena scratch = { .base = base, .capacity = staging.capacity - staging.length };
        result = work->predicate(function, &work->limits, work->context, &scratch);
        const uint64_t finished = ir_reduce_milliseconds();
        if (finished == UINT64_MAX || finished - started >= work->limits.predicate_milliseconds ||
            finished - work->started >= work->limits.max_milliseconds)
        {
            result.status = IR_REDUCE_PREDICATE_LIMIT;
        }
        else if (result.status > IR_REDUCE_PREDICATE_ERROR)
        {
            result.status = IR_REDUCE_PREDICATE_ERROR;
        }
    }
    else if (base != NULL)
    {
        result.status = IR_REDUCE_PREDICATE_LIMIT;
    }
    return result;
}

static bool ir_reduce_observe(IrReduceWork* work, const IrFunction* function, IrReduceObservation* result)
{
    bool valid = true;
    const uint64_t mark = work->scratch.length;
    uint64_t length = 0;
    uint64_t hash = 0;
    uint8_t* key = ir_reduce_key(function, &work->scratch, &length, &hash);
    IrReduceCache* found = NULL;
    for (IrReduceCache* entry = work->first; key != NULL && found == NULL && entry != NULL; entry = entry->next)
    {
        if (entry->hash == hash && entry->length == length && memcmp(entry->bytes, key, length) == 0)
        {
            found = entry;
        }
    }
    if (key == NULL)
    {
        work->result.status = IR_REDUCE_RESOURCE;
        valid = false;
    }
    else if (found != NULL)
    {
        *result = found->observation;
        work->result.cache_hits += 1;
    }
    else
    {
        const uint64_t cache_mark = work->cache.length;
        IrReduceCache* entry = BUSTER_ARENA_PUSH_ARRAY(&work->cache, IrReduceCache, 1);
        uint8_t* saved = entry != NULL ? arena_push(&work->cache, length, 1) : NULL;
        if (saved == NULL)
        {
            arena_pop_to(&work->cache, cache_mark);
            work->result.status = IR_REDUCE_CACHE_LIMIT;
            valid = false;
        }
        else
        {
            memcpy(saved, key, length);
            *result = ir_reduce_predicate(function, work);
            *entry = (IrReduceCache){ .next = work->first, .bytes = saved, .length = length, .hash = hash, .observation = *result };
            work->first = entry;
            work->result.predicates += 1;
        }
    }
    arena_pop_to(&work->scratch, mark);
    return valid;
}

static bool ir_reduce_budget(IrReduceWork* work)
{
    bool available = work->result.status == IR_REDUCE_COMPLETE;
    if (available && work->result.candidates >= work->limits.max_candidates)
    {
        work->result.status = IR_REDUCE_CANDIDATE_LIMIT;
        available = false;
    }
    const uint64_t now = available ? ir_reduce_milliseconds() : 0;
    if (available && (now == UINT64_MAX || now - work->started >= work->limits.max_milliseconds))
    {
        work->result.status = IR_REDUCE_TIME_LIMIT;
        available = false;
    }
    return available;
}

static bool ir_reduce_try(IrReduceWork* work, IrFunction* candidate)
{
    bool accepted = false;
    if (ir_reduce_budget(work))
    {
        work->result.candidates += 1;
        IrCheck check = ir_verify_complete(candidate, &work->scratch, true);
        if (check.error == IR_OK)
        {
            check = ir_verify_complete(candidate, &work->scratch, false);
        }
        if (ir_reduce_resource(check.error))
        {
            work->result.status = IR_REDUCE_RESOURCE;
        }
        else if (check.error != IR_OK)
        {
            work->result.invalid += 1;
        }
        else
        {
            const IrReduceScore score = ir_reduce_score(candidate);
            if (ir_reduce_smaller(score, work->result.reduced))
            {
                IrReduceObservation observed;
                if (ir_reduce_observe(work, candidate, &observed))
                {
                    accepted = observed.status == IR_REDUCE_OBSERVED && ir_reduce_same_failure(observed.failure, work->result.failure);
                    work->result.different += observed.status == IR_REDUCE_OBSERVED && !accepted;
                    work->result.faults += observed.status == IR_REDUCE_PREDICATE_FAULT;
                    work->result.limits += observed.status == IR_REDUCE_PREDICATE_LIMIT;
                    if (accepted)
                    {
                        const Arena previous = work->best;
                        work->best = work->candidate;
                        work->candidate = previous;
                        work->result.function = *candidate;
                        work->result.reduced = score;
                        work->result.accepted += 1;
                    }
                }
            }
        }
    }
    return accepted;
}

// The conservative dead-row class cannot read memory, call, allocate/restore, trap or observe FP state. Removing
// a live row is never inferred from its result being unused; the exact rebuilt use graph must still verify.
static bool ir_reduce_pure(uint32_t op)
{
    bool result = false;
    switch (op)
    {
        case IR_CONST: case IR_UNDEF: case IR_PARAM: case IR_COPY: case IR_ADD: case IR_SUB: case IR_MUL:
        case IR_AND: case IR_OR: case IR_XOR: case IR_NEG: case IR_NOT: case IR_ICMP: case IR_SELECT:
        case IR_TRUNC: case IR_ZEXT: case IR_SEXT: case IR_INTTOPTR: case IR_PTRTOINT: case IR_BITCAST:
        case IR_STACK_ADDR: case IR_SYMBOL_ADDR: case IR_ADDRESS: case IR_PHI: result = true; break;
        default: break;
    }
    return result;
}

static bool ir_reduce_selection(IrReduceWork* work, uint32_t first, uint32_t width, bool blocks, bool unreachable)
{
    const IrFunction* source = &work->result.function;
    const uint64_t mark = work->scratch.length;
    arena_pop_to(&work->candidate, 0);
    uint32_t* selected_blocks = BUSTER_ARENA_PUSH_ARRAY(&work->scratch, uint32_t, source->block_count);
    uint32_t* selected_rows = BUSTER_ARENA_PUSH_ARRAY(&work->scratch, uint32_t, source->instruction_count);
    uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(&work->scratch, uint32_t, (uint64_t)source->block_count + 1);
    bool ready = selected_blocks != NULL && selected_rows != NULL && offsets != NULL;
    uint8_t* reachable = NULL;
    uint32_t* queue = NULL;
    IrCfg cfg = { 0 };
    if (ready && unreachable)
    {
        ready = ir_build_cfg(source, &work->scratch, &work->candidate, &cfg) == IR_OK;
        reachable = ready ? BUSTER_ARENA_PUSH_ARRAY(&work->scratch, uint8_t, source->block_count) : NULL;
        queue = reachable != NULL ? BUSTER_ARENA_PUSH_ARRAY(&work->scratch, uint32_t, source->block_count) : NULL;
        ready = queue != NULL;
        if (ready)
        {
            memset(reachable, 0, source->block_count);
            uint32_t head = 0;
            uint32_t tail = 1;
            queue[0] = 0;
            reachable[0] = 1;
            while (head < tail)
            {
                const uint32_t block = queue[head++];
                for (uint32_t edge = cfg.successor_offsets[block]; edge < cfg.successor_offsets[block + 1]; edge += 1)
                {
                    const uint32_t next = cfg.successors[edge];
                    if (!reachable[next])
                    {
                        reachable[next] = 1;
                        queue[tail++] = next;
                    }
                }
            }
        }
        arena_pop_to(&work->candidate, 0);
    }
    uint32_t block_count = 0;
    uint32_t row_count = 0;
    for (uint32_t b = 0; ready && b < source->block_count; b += 1)
    {
        const bool keep_block = b == 0 || (unreachable ? reachable[b] != 0 : !blocks || b < first || b - first >= width);
        if (keep_block)
        {
            selected_blocks[block_count] = b;
            offsets[block_count++] = row_count;
            for (uint32_t i = source->block_offsets[b]; i < source->block_offsets[b + 1]; i += 1)
            {
                const bool remove = !blocks && !unreachable && i >= first && i - first < width && ir_reduce_pure(source->op[i]);
                if (!remove)
                {
                    selected_rows[row_count++] = i;
                }
            }
        }
    }
    if (ready)
    {
        offsets[block_count] = row_count;
    }
    const bool changed = ready && (block_count != source->block_count || row_count != source->instruction_count);
    IrFunction candidate = { 0 };
    const IrRebuildPlan plan = { .blocks = selected_blocks, .block_count = block_count, .rows = selected_rows,
                                .offsets = offsets, .instruction_count = row_count };
    arena_pop_to(&work->candidate, 0);
    const IrCheck rebuilt = changed ? ir_rebuild(source, &plan, &work->candidate, &work->scratch, &candidate) : (IrCheck){ .error = IR_ERROR_STRUCTURE };
    arena_pop_to(&work->scratch, mark);
    bool accepted = false;
    if (changed && rebuilt.error == IR_OK)
    {
        accepted = ir_reduce_try(work, &candidate);
    }
    else if (changed && ir_reduce_resource(rebuilt.error))
    {
        work->result.status = IR_REDUCE_RESOURCE;
    }
    else if (changed)
    {
        work->result.invalid += 1;
        work->result.candidates += 1;
    }
    else
    {
        work->result.candidates += 1;
    }
    if (!ready)
    {
        work->result.status = IR_REDUCE_RESOURCE;
    }
    return accepted;
}

static bool ir_reduce_alias(IrReduceWork* work, uint32_t row, uint32_t target)
{
    const IrFunction* source = &work->result.function;
    const uint64_t mark = work->scratch.length;
    uint32_t* blocks = BUSTER_ARENA_PUSH_ARRAY(&work->scratch, uint32_t, source->block_count);
    uint32_t* rows = BUSTER_ARENA_PUSH_ARRAY(&work->scratch, uint32_t, source->instruction_count);
    uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(&work->scratch, uint32_t, (uint64_t)source->block_count + 1);
    uint32_t* aliases = BUSTER_ARENA_PUSH_ARRAY(&work->scratch, uint32_t, source->instruction_count);
    bool ready = blocks != NULL && rows != NULL && offsets != NULL && aliases != NULL;
    uint32_t written = 0;
    if (ready)
    {
        memset(aliases, 0xff, (uint64_t)source->instruction_count * sizeof(*aliases));
        aliases[row] = target;
        for (uint32_t b = 0; b < source->block_count; b += 1)
        {
            blocks[b] = b;
            offsets[b] = written;
            for (uint32_t i = source->block_offsets[b]; i < source->block_offsets[b + 1]; i += 1)
            {
                if (i != row)
                {
                    rows[written++] = i;
                }
            }
        }
        offsets[source->block_count] = written;
    }
    arena_pop_to(&work->candidate, 0);
    IrFunction candidate = { 0 };
    const IrRebuildPlan plan = { .blocks = blocks, .block_count = source->block_count, .rows = rows, .offsets = offsets,
                                .instruction_count = written, .aliases = aliases };
    const IrCheck check = ready ? ir_rebuild(source, &plan, &work->candidate, &work->scratch, &candidate) :
                                 (IrCheck){ .error = IR_ERROR_MEMORY };
    arena_pop_to(&work->scratch, mark);
    bool accepted = false;
    if (check.error == IR_OK)
    {
        accepted = ir_reduce_try(work, &candidate);
    }
    else
    {
        work->result.candidates += 1;
        if (ir_reduce_resource(check.error))
        {
            work->result.status = IR_REDUCE_RESOURCE;
        }
        else
        {
            work->result.invalid += 1;
        }
    }
    return accepted;
}

static __attribute__((unused)) IrReduceStatus ir_reduce(const IrFunction* source, IrReducePredicate predicate, void* context,
                                                       const IrReduceLimits* requested, Arena* output, Arena* scratch, IrReduceResult* result)
{
    IrReduceStatus status = IR_REDUCE_INVALID;
    const bool owners = predicate != NULL && requested != NULL &&
                        ir_rebuild_owners(source, NULL, output, scratch, result, sizeof(*result)) &&
                        ir_rebuild_borrow(requested, sizeof(*requested), output, scratch) &&
                        !ir_rebuild_overlap(requested, sizeof(*requested), result, sizeof(*result));
    const uint64_t output_mark = owners ? output->length : 0;
    const uint64_t scratch_mark = owners ? scratch->length : 0;
    IrReduceWork work = { .predicate = predicate, .context = context };
    if (owners && requested->max_candidates != 0 && requested->max_milliseconds != 0 && requested->predicate_milliseconds != 0 &&
        requested->memory_bytes >= (UINT64_C(1) << 16) && requested->execution_steps != 0 && requested->call_frames != 0)
    {
        work.limits = *requested;
        work.started = ir_reduce_milliseconds();
        const IrCheck vector = ir_verify_complete(source, scratch, true);
        const IrCheck scalar = vector.error == IR_OK ? ir_verify_complete(source, scratch, false) : vector;
        if (ir_reduce_resource(vector.error) || ir_reduce_resource(scalar.error))
        {
            status = IR_REDUCE_RESOURCE;
        }
        bool supported = vector.error == IR_OK && scalar.error == IR_OK && source->block_count != 0 && source->result_type <= IR_PTR;
        for (uint32_t i = 0; supported && i < source->instruction_count; i += 1)
        {
            supported = ir_rebuild_values(source->op[i]) != BUSTER_IR_NONE && source->type[i] <= IR_PTR;
        }
        for (uint32_t i = 0; supported && i < source->slot_count; i += 1)
        {
            supported = source->slots[i].type <= IR_PTR;
        }
        for (uint32_t i = 0; supported && i < source->parameter_count; i += 1)
        {
            supported = source->parameter_types[i] <= IR_PTR;
        }
        for (uint32_t i = 0; supported && i < source->call_type_count; i += 1)
        {
            supported = source->call_types[i] <= IR_PTR;
        }
        for (uint32_t i = 0; supported && i < source->call_count; i += 1)
        {
            supported = source->calls[i].result_type <= IR_PTR;
        }
        if (supported)
        {
            status = IR_REDUCE_RESOURCE;
            const uint64_t capacity = work.limits.memory_bytes / 4 & ~UINT64_C(63);
            uint8_t* region = arena_push(scratch, capacity * 4, 64);
            if (region != NULL)
            {
                work.best = (Arena){ .base = region, .capacity = capacity };
                work.candidate = (Arena){ .base = region + capacity, .capacity = capacity };
                work.scratch = (Arena){ .base = region + capacity * 2, .capacity = capacity };
                work.cache = (Arena){ .base = region + capacity * 3, .capacity = capacity };
                work.result.status = IR_REDUCE_COMPLETE;
                work.result.original = ir_reduce_score(source);
                work.result.reduced = work.result.original;
                if (ir_reduce_copy(source, &work.best, &work.result.function) == IR_OK)
                {
                    IrReduceObservation original;
                    const bool observed = ir_reduce_observe(&work, &work.result.function, &original);
                    if (observed && original.status == IR_REDUCE_OBSERVED && original.failure.kind != 0)
                    {
                        work.result.failure = original.failure;
                        IrFunction compact = { 0 };
                        const IrCheck rebuilt = ir_rebuild(&work.result.function, NULL, &work.candidate, &work.scratch, &compact);
                        if (rebuilt.error == IR_OK)
                        {
                            ir_reduce_try(&work, &compact);
                        }
                        else if (ir_reduce_resource(rebuilt.error))
                        {
                            work.result.status = IR_REDUCE_RESOURCE;
                        }
                        bool changed = true;
                        while (changed && ir_reduce_budget(&work))
                        {
                            changed = ir_reduce_selection(&work, 0, 0, false, true);
                            for (uint32_t phase = 0; !changed && phase < 2 && ir_reduce_budget(&work); phase += 1)
                            {
                                uint32_t width = phase == 0 ? work.result.function.block_count : work.result.function.instruction_count;
                                while (!changed && width != 0 && ir_reduce_budget(&work))
                                {
                                    const uint32_t count = phase == 0 ? work.result.function.block_count : work.result.function.instruction_count;
                                    for (uint32_t first = phase == 0 ? 1 : 0; !changed && first < count && ir_reduce_budget(&work);)
                                    {
                                        changed = ir_reduce_selection(&work, first, width, phase == 0, false);
                                        first += width <= UINT32_MAX - first ? width : count - first;
                                    }
                                    width /= 2;
                                }
                            }
                            for (uint32_t c = 0; !changed && c < work.result.function.constant_count && ir_reduce_budget(&work); c += 1)
                            {
                                const IrConstant old = work.result.function.constants[c];
                                const IrConstant choices[] = { { 0, 0 }, { 1, 0 }, { old.low, 0 }, { old.low >> 1, old.high >> 1 } };
                                for (uint32_t choice = 0; !changed && choice < BUSTER_ARRAY_LENGTH(choices) && ir_reduce_budget(&work); choice += 1)
                                {
                                    arena_pop_to(&work.candidate, 0);
                                    IrFunction candidate = { 0 };
                                    if (ir_reduce_copy(&work.result.function, &work.candidate, &candidate) == IR_OK)
                                    {
                                        candidate.constants[c] = choices[choice];
                                        changed = ir_reduce_try(&work, &candidate);
                                    }
                                    else
                                    {
                                        work.result.status = IR_REDUCE_RESOURCE;
                                    }
                                }
                            }
                            for (uint32_t i = 0; !changed && i < work.result.function.instruction_count && ir_reduce_budget(&work); i += 1)
                            {
                                const IrFunction* current = &work.result.function;
                                const uint32_t op = current->op[i];
                                uint32_t target = op == IR_COPY ? current->a[i] :
                                                  op == IR_SELECT && current->b[i] == current->c[i] ? current->b[i] : BUSTER_IR_NONE;
                                if (op == IR_PHI)
                                {
                                    target = current->extra[current->a[i] + 1];
                                    for (uint32_t p = 1; target != BUSTER_IR_NONE && p < current->b[i]; p += 1)
                                    {
                                        target = current->extra[current->a[i] + p * 2 + 1] == target ? target : BUSTER_IR_NONE;
                                    }
                                }
                                if (target != BUSTER_IR_NONE && target != i)
                                {
                                    changed = ir_reduce_alias(&work, i, target);
                                }
                            }
                        }
                        status = work.result.status;
                    }
                    else
                    {
                        status = observed && original.status == IR_REDUCE_OBSERVED ? IR_REDUCE_NOT_FAILING :
                                 observed ? IR_REDUCE_ORACLE_UNAVAILABLE : work.result.status;
                        // A limited cache is a publishable partial reduction only after the original
                        // classified failure was established. No unobserved input is a reproducer.
                        if (!observed && status == IR_REDUCE_CACHE_LIMIT)
                        {
                            status = IR_REDUCE_RESOURCE;
                        }
                    }
                    if (status <= IR_REDUCE_CACHE_LIMIT)
                    {
                        IrFunction published = { 0 };
                        // A wall-clock guard is exceptional and scheduling dependent. Publish the original
                        // valid reproducer on this boundary rather than a timing-dependent partial reduction.
                        const IrFunction* final = status == IR_REDUCE_TIME_LIMIT ? source : &work.result.function;
                        if (ir_reduce_copy(final, output, &published) == IR_OK)
                        {
                            work.result.function = published;
                            work.result.reduced = ir_reduce_score(final);
                            work.result.status = status;
                            *result = work.result;
                        }
                        else
                        {
                            status = IR_REDUCE_RESOURCE;
                        }
                    }
                }
            }
        }
    }
    if (owners)
    {
        if (status > IR_REDUCE_CACHE_LIMIT)
        {
            arena_pop_to(output, output_mark);
        }
        arena_pop_to(scratch, scratch_mark);
    }
    return status;
}

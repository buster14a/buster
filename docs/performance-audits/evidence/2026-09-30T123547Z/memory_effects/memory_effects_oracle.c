/*
 * Independent finite-state oracle for a proposed bounded canonical-IR contract.
 * Not Buster production code and not a complete C or target memory model.
 * The reference interpreter operates on bytes, permissions and lifetimes.
 * Separately stated proof predicates decide three named transformations.
 * No host pointers, compiler provenance, undefined values or poison are modeled.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define OBJECT_COUNT 2u
#define OBJECT_BYTES 16u
#define REGISTER_COUNT 8u
#define PROGRAM_ROWS 16u
#define TRACE_COUNT 32u
#define NO_REGISTER UINT32_MAX

typedef enum Opcode
{
    OP_STORE, OP_LOAD, OP_CONSTANT, OP_OBSERVE, OP_UNKNOWN_CALL,
    OP_END_LIFETIME, OP_BEGIN_LIFETIME, OP_PERMISSIONS, OP_FAULT, OP_RETURN,
} Opcode;

typedef struct Row Row;
struct Row
{
    Opcode opcode;
    uint32_t object;
    uint32_t offset;
    uint32_t width;
    uint32_t destination;
    uint32_t source;
    uint64_t immediate;
    bool special;
};

typedef struct Program Program;
struct Program
{
    Row rows[PROGRAM_ROWS];
    uint32_t count;
};

typedef struct Object Object;
struct Object
{
    uint8_t bytes[OBJECT_BYTES];
    uint32_t generation;
    bool alive;
    bool readable;
    bool writable;
};

typedef struct Result Result;
struct Result
{
    uint64_t trace[TRACE_COUNT];
    uint32_t trace_count;
    uint64_t returned;
    bool fault;
    bool complete;
};

typedef struct State State;
struct State
{
    Object objects[OBJECT_COUNT];
    uint64_t registers[REGISTER_COUNT];
    Result result;
};

typedef struct Access Access;
struct Access
{
    uint32_t object;
    uint32_t generation;
    uint32_t offset;
    uint32_t width;
    uint32_t type;
    bool fixed_local;
    bool private_object;
    bool live;
    bool safe_read;
    bool safe_write;
    bool escaped;
    bool volatile_access;
    bool atomic_access;
    bool integer_access;
    bool normalized_value;
};

typedef enum Effects
{
    EFFECT_NONE = 0,
    EFFECT_OVERLAP_WRITE = 1u << 0,
    EFFECT_OVERLAP_READ = 1u << 1,
    EFFECT_UNKNOWN = 1u << 2,
    EFFECT_LIFETIME = 1u << 3,
    EFFECT_SPECIAL = 1u << 4,
    EFFECT_POSSIBLE_FAULT = 1u << 5,
    EFFECT_ESCAPE = 1u << 6,
    EFFECT_DISJOINT_WRITE = 1u << 7,
} Effects;

typedef struct Counts Counts;
struct Counts
{
    uint64_t equivalence_checks;
    uint64_t predicate_checks;
    uint64_t counterexamples;
    uint64_t failures;
};

static Counts counts;

static uint64_t mask_for_width(uint32_t width)
{
    uint64_t result = width == 8u ? UINT64_MAX : (UINT64_C(1) << (width * 8u)) - UINT64_C(1);
    return result;
}

static uint64_t read_bytes(Object const* object, uint32_t offset, uint32_t width)
{
    uint64_t result = 0;
    for (uint32_t byte = 0; byte < width; byte += 1u)
    {
        result |= (uint64_t)object->bytes[offset + byte] << (byte * 8u);
    }
    return result;
}

static void write_bytes(Object* object, uint32_t offset, uint32_t width, uint64_t value)
{
    for (uint32_t byte = 0; byte < width; byte += 1u)
    {
        object->bytes[offset + byte] = (uint8_t)(value >> (byte * 8u));
    }
}

static void trace(State* state, uint64_t event)
{
    if (state->result.trace_count < TRACE_COUNT)
    {
        state->result.trace[state->result.trace_count++] = event;
    }
    else
    {
        state->result.fault = true;
    }
}

static bool access_valid(Object const* object, Row const* row, bool write)
{
    bool width_valid = row->width == 1u || row->width == 2u || row->width == 4u || row->width == 8u;
    bool result = width_valid && row->offset <= OBJECT_BYTES && row->width <= OBJECT_BYTES - row->offset &&
                  object->alive && (write ? object->writable : object->readable);
    return result;
}

static Result execute(Program const* program)
{
    State state;
    memset(&state, 0, sizeof(state));
    for (uint32_t object = 0; object < OBJECT_COUNT; object += 1u)
    {
        state.objects[object].alive = true;
        state.objects[object].readable = true;
        state.objects[object].writable = true;
        state.objects[object].generation = 1u;
    }
    for (uint32_t index = 0; index < program->count && !state.result.fault && !state.result.complete; index += 1u)
    {
        Row const* row = program->rows + index;
        Object* object = row->object < OBJECT_COUNT ? state.objects + row->object : 0;
        uint64_t value = row->source < REGISTER_COUNT ? state.registers[row->source] : row->immediate;
        bool access = row->opcode == OP_LOAD || row->opcode == OP_STORE || row->opcode == OP_OBSERVE;
        if (access && (!object || !access_valid(object, row, row->opcode == OP_STORE)))
        {
            trace(&state, UINT64_C(0xf000000000000000) | ((uint64_t)row->object << 16) |
                          ((uint64_t)row->offset << 8) | row->width);
            state.result.fault = true;
        }
        else
        {
            switch (row->opcode)
            {
            case OP_STORE:
                write_bytes(object, row->offset, row->width, value);
                if (row->special)
                {
                    trace(&state, UINT64_C(0x1000000000000000) | row->width);
                    trace(&state, value & mask_for_width(row->width));
                }
                break;
            case OP_LOAD:
                value = read_bytes(object, row->offset, row->width);
                state.registers[row->destination] = value;
                if (row->special)
                {
                    trace(&state, UINT64_C(0x2000000000000000) | row->width);
                    trace(&state, value);
                }
                break;
            case OP_CONSTANT: state.registers[row->destination] = value; break;
            case OP_OBSERVE:
                trace(&state, UINT64_C(0x3000000000000000) | row->width);
                trace(&state, read_bytes(object, row->offset, row->width));
                break;
            case OP_UNKNOWN_CALL:
                /* This possible callee both observes and modifies known bytes. */
                trace(&state, UINT64_C(0x4000000000000000));
                trace(&state, read_bytes(object, row->offset, row->width));
                write_bytes(object, row->offset, row->width, value);
                break;
            case OP_END_LIFETIME: object->alive = false; break;
            case OP_BEGIN_LIFETIME:
                memset(object->bytes, 0, sizeof(object->bytes));
                object->generation += 1u;
                object->alive = true;
                object->readable = true;
                object->writable = true;
                break;
            case OP_PERMISSIONS:
                object->readable = (value & UINT64_C(1)) != 0;
                object->writable = (value & UINT64_C(2)) != 0;
                break;
            case OP_FAULT:
                trace(&state, UINT64_C(0xf100000000000000));
                state.result.fault = true;
                break;
            case OP_RETURN:
                state.result.returned = value;
                state.result.complete = true;
                break;
            }
        }
    }
    return state.result;
}

static bool results_equal(Result const* left, Result const* right)
{
    bool result = left->fault == right->fault && left->complete == right->complete &&
                  left->returned == right->returned && left->trace_count == right->trace_count &&
                  memcmp(left->trace, right->trace, left->trace_count * sizeof(left->trace[0])) == 0;
    return result;
}

static Row memory_row(Opcode opcode, uint32_t object, uint32_t offset, uint32_t width,
                      uint32_t destination, uint64_t immediate)
{
    Row result = {.opcode = opcode, .object = object, .offset = offset, .width = width,
                  .destination = destination, .source = NO_REGISTER, .immediate = immediate};
    return result;
}

static Row return_register(uint32_t source)
{
    Row result = {.opcode = OP_RETURN, .source = source};
    return result;
}

static void append(Program* program, Row row)
{
    if (program->count < PROGRAM_ROWS)
    {
        program->rows[program->count++] = row;
    }
    else
    {
        counts.failures += 1u;
    }
}

static Program remove_row(Program const* source, uint32_t removed)
{
    Program result = {0};
    for (uint32_t index = 0; index < source->count; index += 1u)
    {
        if (index != removed) append(&result, source->rows[index]);
    }
    return result;
}

static Program replace_load(Program const* source, uint32_t load, uint32_t register_source, uint64_t value)
{
    Program result = *source;
    Row* row = result.rows + load;
    row->opcode = OP_CONSTANT;
    row->source = register_source;
    row->immediate = value;
    row->special = false;
    return result;
}

static void check_equivalent(char const* name, Program const* source, Program const* transformed)
{
    Result left = execute(source);
    Result right = execute(transformed);
    counts.equivalence_checks += 1u;
    if (!results_equal(&left, &right))
    {
        printf("FAIL equivalence: %s\n", name);
        counts.failures += 1u;
    }
}

static void check_counterexample(char const* name, Program const* source, Program const* transformed)
{
    Result left = execute(source);
    Result right = execute(transformed);
    counts.counterexamples += 1u;
    if (results_equal(&left, &right))
    {
        printf("FAIL counterexample did not distinguish: %s\n", name);
        counts.failures += 1u;
    }
    else
    {
        printf("COUNTEREXAMPLE %s: source(return=%" PRIu64 ",fault=%u,events=%u) candidate(return=%" PRIu64
               ",fault=%u,events=%u)\n", name, left.returned, (unsigned)left.fault, left.trace_count,
               right.returned, (unsigned)right.fault, right.trace_count);
        uint32_t common = left.trace_count < right.trace_count ? left.trace_count : right.trace_count;
        bool printed = false;
        for (uint32_t index = 0; index < common && !printed; index += 1u)
        {
            if (left.trace[index] != right.trace[index])
            {
                printf("  first trace difference: event[%u]=%" PRIu64 " versus %" PRIu64 "\n",
                       index, left.trace[index], right.trace[index]);
                printed = true;
            }
        }
    }
}

/* Contract predicates, not an optimizer implementation. All facts are inputs. */
static bool ordinary_private(Access const* access)
{
    bool result = access->fixed_local && access->private_object && access->live && !access->escaped &&
                  !access->volatile_access && !access->atomic_access && access->integer_access &&
                  (access->width == 4u || access->width == 8u);
    return result;
}

static bool same_access(Access const* first, Access const* second)
{
    bool result = first->object == second->object && first->generation == second->generation &&
                  first->offset == second->offset && first->width == second->width && first->type == second->type;
    return result;
}

static bool can_reuse_load(Access const* first, Access const* second, uint32_t effects)
{
    uint32_t blockers = EFFECT_OVERLAP_WRITE | EFFECT_UNKNOWN | EFFECT_LIFETIME | EFFECT_SPECIAL |
                        EFFECT_POSSIBLE_FAULT | EFFECT_ESCAPE;
    bool result = ordinary_private(first) && ordinary_private(second) && first->safe_read && second->safe_read &&
                  same_access(first, second) && !(effects & blockers);
    return result;
}

static bool can_forward_store(Access const* store, Access const* load, uint32_t effects)
{
    uint32_t blockers = EFFECT_OVERLAP_WRITE | EFFECT_UNKNOWN | EFFECT_LIFETIME | EFFECT_SPECIAL |
                        EFFECT_POSSIBLE_FAULT | EFFECT_ESCAPE;
    bool result = ordinary_private(store) && ordinary_private(load) && store->safe_write && load->safe_read &&
                  store->normalized_value && same_access(store, load) && !(effects & blockers);
    return result;
}

static bool can_delete_overwritten_store(Access const* first, Access const* second, uint32_t effects)
{
    uint32_t blockers = EFFECT_OVERLAP_READ | EFFECT_UNKNOWN | EFFECT_LIFETIME | EFFECT_SPECIAL |
                        EFFECT_POSSIBLE_FAULT | EFFECT_ESCAPE;
    bool result = ordinary_private(first) && ordinary_private(second) && first->safe_write && second->safe_write &&
                  same_access(first, second) && !(effects & blockers);
    return result;
}

static Access supported_access(uint32_t object, uint32_t offset, uint32_t width)
{
    Access result = {.object = object, .generation = 1u, .offset = offset, .width = width, .type = width,
                     .fixed_local = true, .private_object = true, .live = true, .safe_read = true,
                     .safe_write = true, .integer_access = true, .normalized_value = true};
    return result;
}

static void check_predicate(char const* name, bool actual, bool expected)
{
    counts.predicate_checks += 1u;
    if (actual != expected)
    {
        printf("FAIL predicate: %s expected=%u actual=%u\n", name, (unsigned)expected, (unsigned)actual);
        counts.failures += 1u;
    }
}

static void test_predicates(void)
{
    Access first = supported_access(0u, 0u, 4u);
    Access second = first;
    check_predicate("reuse exact i32", can_reuse_load(&first, &second, EFFECT_NONE), true);
    check_predicate("forward exact i32", can_forward_store(&first, &second, EFFECT_NONE), true);
    check_predicate("delete exact i32", can_delete_overwritten_store(&first, &second, EFFECT_NONE), true);
    first.width = 8u; first.type = 8u; second = first;
    check_predicate("reuse exact i64", can_reuse_load(&first, &second, EFFECT_NONE), true);
    check_predicate("forward exact i64", can_forward_store(&first, &second, EFFECT_NONE), true);
    check_predicate("delete exact i64", can_delete_overwritten_store(&first, &second, EFFECT_NONE), true);
    uint32_t barrier_bits[] = {EFFECT_UNKNOWN, EFFECT_LIFETIME, EFFECT_SPECIAL, EFFECT_POSSIBLE_FAULT, EFFECT_ESCAPE};
    for (uint32_t index = 0; index < sizeof(barrier_bits) / sizeof(barrier_bits[0]); index += 1u)
    {
        check_predicate("reuse barrier", can_reuse_load(&first, &second, barrier_bits[index]), false);
        check_predicate("forward barrier", can_forward_store(&first, &second, barrier_bits[index]), false);
        check_predicate("delete barrier", can_delete_overwritten_store(&first, &second, barrier_bits[index]), false);
    }
    check_predicate("reuse overlap write", can_reuse_load(&first, &second, EFFECT_OVERLAP_WRITE), false);
    check_predicate("forward overlap write", can_forward_store(&first, &second, EFFECT_OVERLAP_WRITE), false);
    check_predicate("delete overlap read", can_delete_overwritten_store(&first, &second, EFFECT_OVERLAP_READ), false);
    check_predicate("reuse disjoint write", can_reuse_load(&first, &second, EFFECT_DISJOINT_WRITE), true);
    check_predicate("forward disjoint write", can_forward_store(&first, &second, EFFECT_DISJOINT_WRITE), true);
    check_predicate("delete disjoint write", can_delete_overwritten_store(&first, &second, EFFECT_DISJOINT_WRITE), true);
    for (uint32_t kind = 0; kind < 12u; kind += 1u)
    {
        second = first;
        switch (kind)
        {
        case 0: second.object = 1u; break;
        case 1: second.generation = 2u; break;
        case 2: second.offset = 1u; break;
        case 3: second.width = 4u; break;
        case 4: second.type = 17u; break;
        case 5: second.fixed_local = false; break;
        case 6: second.private_object = false; break;
        case 7: second.live = false; break;
        case 8: second.escaped = true; break;
        case 9: second.volatile_access = true; break;
        case 10: second.atomic_access = true; break;
        case 11: second.width = 1u; break;
        }
        check_predicate("reuse changed fact", can_reuse_load(&first, &second, EFFECT_NONE), false);
        check_predicate("forward changed fact", can_forward_store(&first, &second, EFFECT_NONE), false);
        check_predicate("delete changed fact", can_delete_overwritten_store(&first, &second, EFFECT_NONE), false);
    }
    second = first; second.safe_read = false;
    check_predicate("reuse read fault", can_reuse_load(&first, &second, EFFECT_NONE), false);
    check_predicate("forward read fault", can_forward_store(&first, &second, EFFECT_NONE), false);
    second = first; first.safe_write = false;
    check_predicate("forward store fault", can_forward_store(&first, &second, EFFECT_NONE), false);
    check_predicate("delete store fault", can_delete_overwritten_store(&first, &second, EFFECT_NONE), false);
    first = second; first.normalized_value = false;
    check_predicate("forward unnormalized", can_forward_store(&first, &second, EFFECT_NONE), false);
    first = second; first.integer_access = false; second.integer_access = false;
    check_predicate("reuse equal-width noninteger", can_reuse_load(&first, &second, EFFECT_NONE), false);
    check_predicate("forward equal-width noninteger", can_forward_store(&first, &second, EFFECT_NONE), false);
    check_predicate("delete equal-width noninteger", can_delete_overwritten_store(&first, &second, EFFECT_NONE), false);
}

static void test_accepted_equivalences(void)
{
    uint64_t values[] = {0, 1, 2, 3, 255, 256, UINT32_MAX, UINT64_C(0x80000000),
                         UINT64_C(0x100000000), UINT64_C(0x8000000000000000), UINT64_MAX};
    for (uint32_t width = 4u; width <= 8u; width *= 2u)
    {
        for (uint32_t offset = 0; offset <= OBJECT_BYTES - width; offset += width)
        {
            for (uint32_t a = 0; a < sizeof(values) / sizeof(values[0]); a += 1u)
            {
                uint64_t value = values[a] & mask_for_width(width);
                Program reuse = {0};
                append(&reuse, memory_row(OP_STORE, 0u, offset, width, 0u, value));
                append(&reuse, memory_row(OP_LOAD, 0u, offset, width, 0u, 0));
                append(&reuse, memory_row(OP_STORE, 1u, offset, width, 0u, UINT64_MAX));
                append(&reuse, memory_row(OP_LOAD, 0u, offset, width, 1u, 0));
                append(&reuse, return_register(1u));
                Program transformed = replace_load(&reuse, 3u, 0u, 0);
                check_equivalent("redundant load after distinct-object store", &reuse, &transformed);
                Program forward = {0};
                append(&forward, memory_row(OP_STORE, 0u, offset, width, 0u, value));
                append(&forward, memory_row(OP_STORE, 1u, offset, width, 0u, UINT64_MAX));
                append(&forward, memory_row(OP_LOAD, 0u, offset, width, 1u, 0));
                append(&forward, return_register(1u));
                transformed = replace_load(&forward, 2u, NO_REGISTER, value);
                check_equivalent("store forwarding after distinct-object store", &forward, &transformed);
                for (uint32_t b = 0; b < sizeof(values) / sizeof(values[0]); b += 1u)
                {
                    Program overwritten = {0};
                    append(&overwritten, memory_row(OP_STORE, 0u, offset, width, 0u, value));
                    append(&overwritten, memory_row(OP_STORE, 1u, offset, width, 0u, UINT64_MAX));
                    append(&overwritten, memory_row(OP_STORE, 0u, offset, width, 0u, values[b]));
                    append(&overwritten, memory_row(OP_OBSERVE, 0u, offset, width, 0u, 0));
                    append(&overwritten, memory_row(OP_OBSERVE, 1u, offset, width, 0u, 0));
                    append(&overwritten, memory_row(OP_LOAD, 0u, offset, width, 1u, 0));
                    append(&overwritten, return_register(1u));
                    transformed = remove_row(&overwritten, 0u);
                    check_equivalent("overwritten store retains other object and final bytes", &overwritten, &transformed);
                }
            }
        }
    }
    /* Small exhaustive values, including full coverage of a byte domain. */
    for (uint32_t a = 0; a < 256u; a += 1u)
    {
        for (uint32_t b = 0; b < 256u; b += 1u)
        {
            Program source = {0};
            append(&source, memory_row(OP_STORE, 0u, 0u, 4u, 0u, a));
            append(&source, memory_row(OP_STORE, 0u, 4u, 4u, 0u, b));
            append(&source, memory_row(OP_LOAD, 0u, 0u, 4u, 1u, 0));
            append(&source, memory_row(OP_OBSERVE, 0u, 4u, 4u, 0u, 0));
            append(&source, return_register(1u));
            Program transformed = replace_load(&source, 2u, NO_REGISTER, a);
            check_equivalent("same-object disjoint subobject store", &source, &transformed);
        }
    }
}

static void test_counterexamples(void)
{
    Program source = {0};
    append(&source, memory_row(OP_STORE, 0u, 0u, 4u, 0u, UINT64_C(0x11223344)));
    append(&source, memory_row(OP_LOAD, 0u, 0u, 4u, 0u, 0));
    append(&source, memory_row(OP_STORE, 0u, 1u, 1u, 0u, UINT64_C(0x99)));
    append(&source, memory_row(OP_LOAD, 0u, 0u, 4u, 1u, 0));
    append(&source, return_register(1u));
    Program transformed = replace_load(&source, 3u, 0u, 0);
    check_counterexample("partial byte overwrite invalidates whole load", &source, &transformed);
    source.rows[2] = memory_row(OP_STORE, 0u, 0u, 4u, 0u, 9u);
    transformed = replace_load(&source, 3u, 0u, 0);
    check_counterexample("different union fields share bytes", &source, &transformed);
    source.rows[2] = memory_row(OP_UNKNOWN_CALL, 0u, 0u, 4u, 0u, 9u);
    transformed = replace_load(&source, 3u, 0u, 0);
    check_counterexample("unknown call modifies load source", &source, &transformed);
    source.rows[2] = memory_row(OP_END_LIFETIME, 0u, 0u, 0u, 0u, 0);
    source.rows[3] = memory_row(OP_BEGIN_LIFETIME, 0u, 0u, 0u, 0u, 0);
    source.rows[4] = memory_row(OP_LOAD, 0u, 0u, 4u, 1u, 0);
    append(&source, return_register(1u));
    transformed = replace_load(&source, 4u, 0u, 0);
    check_counterexample("same numeric storage different allocation generation", &source, &transformed);

    source = (Program){0};
    append(&source, memory_row(OP_STORE, 0u, 0u, 4u, 0u, 7u));
    append(&source, memory_row(OP_LOAD, 0u, 0u, 4u, 0u, 0));
    append(&source, memory_row(OP_PERMISSIONS, 0u, 0u, 0u, 0u, 2u));
    append(&source, memory_row(OP_LOAD, 0u, 0u, 4u, 1u, 0));
    append(&source, return_register(1u));
    transformed = replace_load(&source, 3u, 0u, 0);
    check_counterexample("successful earlier load does not prove later permissions", &source, &transformed);
    source = (Program){0};
    append(&source, memory_row(OP_PERMISSIONS, 0u, 0u, 0u, 0u, 2u));
    append(&source, memory_row(OP_STORE, 0u, 0u, 4u, 0u, 7u));
    append(&source, memory_row(OP_LOAD, 0u, 0u, 4u, 1u, 0));
    append(&source, return_register(1u));
    transformed = replace_load(&source, 2u, NO_REGISTER, 7u);
    check_counterexample("successful store does not prove readable later access", &source, &transformed);

    source = (Program){0};
    append(&source, memory_row(OP_STORE, 0u, 0u, 1u, 0u, 256u));
    append(&source, memory_row(OP_LOAD, 0u, 0u, 1u, 1u, 0));
    append(&source, return_register(1u));
    transformed = replace_load(&source, 1u, NO_REGISTER, 256u);
    check_counterexample("narrow load normalizes stored RHS", &source, &transformed);

    source = (Program){0};
    Row special_store = memory_row(OP_STORE, 0u, 0u, 4u, 0u, 7u);
    special_store.special = true;
    append(&source, special_store);
    append(&source, memory_row(OP_STORE, 0u, 0u, 4u, 0u, 9u));
    append(&source, memory_row(OP_LOAD, 0u, 0u, 4u, 1u, 0));
    append(&source, return_register(1u));
    transformed = remove_row(&source, 0u);
    check_counterexample("volatile or atomic store has an event even if overwritten", &source, &transformed);
    source.rows[0].special = false;
    source.rows[1] = memory_row(OP_LOAD, 0u, 0u, 4u, 0u, 0);
    source.rows[2].special = true;
    transformed = replace_load(&source, 2u, 0u, 0);
    check_counterexample("volatile or atomic repeated load cannot lose its event", &source, &transformed);

    source = (Program){0};
    append(&source, memory_row(OP_STORE, 0u, 0u, 4u, 0u, 7u));
    append(&source, memory_row(OP_OBSERVE, 0u, 0u, 4u, 0u, 0));
    append(&source, memory_row(OP_STORE, 0u, 0u, 4u, 0u, 9u));
    append(&source, memory_row(OP_LOAD, 0u, 0u, 4u, 1u, 0));
    append(&source, return_register(1u));
    transformed = remove_row(&source, 0u);
    check_counterexample("intermediate reader observes first store", &source, &transformed);
    source.rows[1] = memory_row(OP_UNKNOWN_CALL, 0u, 0u, 4u, 0u, 11u);
    transformed = remove_row(&source, 0u);
    check_counterexample("escaped storage observed by unknown call", &source, &transformed);
    source.rows[1] = memory_row(OP_STORE, 0u, 0u, 1u, 0u, 9u);
    source.rows[2] = memory_row(OP_LOAD, 0u, 0u, 4u, 1u, 0);
    source.rows[3] = return_register(1u);
    source.count = 4u;
    source.rows[0].immediate = UINT64_C(0x11223344);
    transformed = remove_row(&source, 0u);
    check_counterexample("partial overwrite does not kill whole earlier store", &source, &transformed);

    source = (Program){0};
    append(&source, memory_row(OP_PERMISSIONS, 0u, 0u, 0u, 0u, 1u));
    append(&source, memory_row(OP_STORE, 0u, 0u, 4u, 0u, 7u));
    append(&source, memory_row(OP_PERMISSIONS, 0u, 0u, 0u, 0u, 3u));
    append(&source, memory_row(OP_STORE, 0u, 0u, 4u, 0u, 9u));
    append(&source, memory_row(OP_LOAD, 0u, 0u, 4u, 1u, 0));
    append(&source, return_register(1u));
    transformed = remove_row(&source, 1u);
    check_counterexample("deleting overwritten store suppresses its own fault", &source, &transformed);

    source = (Program){0};
    append(&source, memory_row(OP_LOAD, 0u, 15u, 4u, 1u, 0));
    append(&source, memory_row(OP_CONSTANT, 0u, 0u, 0u, 2u, 13u));
    append(&source, return_register(2u));
    transformed = remove_row(&source, 0u);
    check_counterexample("unused load can still fault", &source, &transformed);
}

int main(void)
{
    test_predicates();
    test_accepted_equivalences();
    test_counterexamples();
    printf("RESULT equivalence_checks=%" PRIu64 " predicate_checks=%" PRIu64 " counterexamples=%" PRIu64
           " failures=%" PRIu64 "\n", counts.equivalence_checks, counts.predicate_checks,
           counts.counterexamples, counts.failures);
    int result = counts.failures ? 1 : 0;
    return result;
}

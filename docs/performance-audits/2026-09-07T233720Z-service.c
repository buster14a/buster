/* Standalone Linux old/new ABI service benchmark for #39. Compile this same
   harness against each revision's real IR/cache/arena implementation. Only
   inspection/reset adapters differ under BUSTER_ABI_BENCH_BEFORE. */
#define BUSTER_UNITY_BUILD 1
#define BUSTER_SINGLE_THREADED 1
#define BUSTER_INCLUDE_TESTS 0
#include <buster/lib/base.h>
#include <buster/lib/os.h>
#include <buster/lib/arena.h>
#include <buster/lib/string.h>
#include <buster/lib/file.h>
#include <buster/lib/integer.h>
#include <buster/lib/string.c>
#include <buster/lib/os.c>
#include <buster/lib/arena.c>
#include <buster/lib/file.c>
#include <buster/lib/integer.c>
#include <stdio.h>
BUSTER_GLOBAL_LOCAL void audit_initialize(void)
{
    static ProgramState state;
    program_state = &state;
    os_state.page_size = (u64)sysconf(_SC_PAGESIZE);
    os_state.allocation_granularity = os_state.page_size;
    os_state.logical_thread_count = 1;
    pthread_mutex_init(&os_state.entity_mutex, 0);
    os_state.entity_arena = arena_create((ArenaCreation){0});
    state.arena = arena_create((ArenaCreation){0});
    thread_context_select(thread_context_allocate());
}

#include <buster/lib/hash.c>
#include <buster/lib/target.c>
#if BUSTER_CPU_ARCH_X86_64
#include <buster/lib/compiler/assembly/x86_64_metadata.c>
#endif
#include <buster/lib/compiler/ir/ir.c>

#include <time.h>


BUSTER_GLOBAL_LOCAL IrProgram audit_corpus(Arena* arena)
{
    IrProgram abi_program = ir_program_initialize(arena, 0, 32, 0, 0);
    IrTypeId abi_f32 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_FLOAT,
        .bit_width = 32,
        .layout = {.size = 4, .alignment = 4, .abi_class = IR_ABI_CLASS_FLOAT, .resolved = true},
    });
    IrTypeId abi_f64 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_FLOAT,
        .bit_width = 64,
        .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_FLOAT, .resolved = true},
    });
    IrTypeId abi_f80 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_FLOAT,
        .bit_width = 80,
        .layout = {.size = 16, .alignment = 16, .abi_class = IR_ABI_CLASS_FLOAT, .resolved = true},
    });
    IrTypeId abi_integer = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_INTEGER,
        .bit_width = 32,
        .layout = {.size = 4, .alignment = 4, .abi_class = IR_ABI_CLASS_INTEGER, .resolved = true},
    });
    IrTypeId abi_enum = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_ENUM,
        .bit_width = 32,
        .layout = {.size = 4, .alignment = 4, .abi_class = IR_ABI_CLASS_INTEGER, .resolved = true},
    });
    IrField* abi_struct_f80_fields = arena_allocate(arena, IrField, 1);
    abi_struct_f80_fields[0] = (IrField){.type = abi_f80, .offset = 0};
    IrTypeId abi_struct_f80 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_STRUCT,
        .fields = abi_struct_f80_fields,
        .field_count = 1,
        .layout = {.size = 16, .alignment = 16, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    // `long double _Complex` and the identically laid out plain struct: the
    // two differ only in `is_complex`, which is what System V's COMPLEX_X87
    // class is keyed on, so the pair is the whole test.
    IrField* abi_complex_f80_fields = arena_allocate(arena, IrField, 2);
    abi_complex_f80_fields[0] = (IrField){.type = abi_f80, .offset = 0};
    abi_complex_f80_fields[1] = (IrField){.type = abi_f80, .offset = 16};
    IrTypeId abi_complex_f80 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_STRUCT,
        .fields = abi_complex_f80_fields,
        .element_type = abi_f80,
        .field_count = 2,
        .is_complex = true,
        .layout = {.size = 32, .alignment = 16, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_pair_f80_fields = arena_allocate(arena, IrField, 2);
    abi_pair_f80_fields[0] = (IrField){.type = abi_f80, .offset = 0};
    abi_pair_f80_fields[1] = (IrField){.type = abi_f80, .offset = 16};
    IrTypeId abi_pair_f80 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_STRUCT,
        .fields = abi_pair_f80_fields,
        .element_type = abi_f80,
        .field_count = 2,
        .layout = {.size = 32, .alignment = 16, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_union_same_f80_fields = arena_allocate(arena, IrField, 2);
    abi_union_same_f80_fields[0] = (IrField){.type = abi_f80, .offset = 0};
    abi_union_same_f80_fields[1] = (IrField){.type = abi_f80, .offset = 0};
    IrTypeId abi_union_same_f80 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_UNION,
        .fields = abi_union_same_f80_fields,
        .field_count = 2,
        .layout = {.size = 16, .alignment = 16, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_union_mixed_fields = arena_allocate(arena, IrField, 2);
    abi_union_mixed_fields[0] = (IrField){.type = abi_f80, .offset = 0};
    abi_union_mixed_fields[1] = (IrField){.type = abi_integer, .offset = 0};
    IrTypeId abi_union_mixed = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_UNION,
        .fields = abi_union_mixed_fields,
        .field_count = 2,
        .layout = {.size = 16, .alignment = 16, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_union_f80_f64_fields = arena_allocate(arena, IrField, 2);
    abi_union_f80_f64_fields[0] = (IrField){.type = abi_f80, .offset = 0};
    abi_union_f80_f64_fields[1] = (IrField){.type = abi_f64, .offset = 0};
    IrTypeId abi_union_f80_f64 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_UNION,
        .fields = abi_union_f80_f64_fields,
        .field_count = 2,
        .layout = {.size = 16, .alignment = 16, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_struct_unaligned_f80_fields = arena_allocate(arena, IrField, 1);
    abi_struct_unaligned_f80_fields[0] = (IrField){.type = abi_f80, .offset = 1};
    IrTypeId abi_struct_unaligned_f80 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_STRUCT,
        .fields = abi_struct_unaligned_f80_fields,
        .field_count = 1,
        // Deliberately model a packed/unaligned f80 within the 16-byte
        // classifier limit.  The field's own 16-byte alignment/extent must
        // force the aggregate to MEMORY rather than exposing x87 classes.
        .layout = {.size = 16, .alignment = 1, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_struct_enum_fields = arena_allocate(arena, IrField, 1);
    abi_struct_enum_fields[0] = (IrField){.type = abi_enum, .offset = 0};
    IrTypeId abi_struct_enum = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_STRUCT,
        .fields = abi_struct_enum_fields,
        .field_count = 1,
        .layout = {.size = 4, .alignment = 4, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_struct_large_fields = arena_allocate(arena, IrField, 2);
    abi_struct_large_fields[0] = (IrField){.type = abi_f80, .offset = 0};
    abi_struct_large_fields[1] = (IrField){.type = abi_integer, .offset = 16};
    IrTypeId abi_struct_large = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_STRUCT,
        .fields = abi_struct_large_fields,
        .field_count = 2,
        .layout = {.size = 32, .alignment = 16, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });

    IrField* abi_struct_f64x2_fields = arena_allocate(arena, IrField, 2);
    abi_struct_f64x2_fields[0] = (IrField){.type = abi_f64, .offset = 0};
    abi_struct_f64x2_fields[1] = (IrField){.type = abi_f64, .offset = 8};
    IrTypeId abi_struct_f64x2 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_STRUCT,
        .fields = abi_struct_f64x2_fields,
        .field_count = 2,
        .layout = {.size = 16, .alignment = 8, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrTypeId abi_union_f64[4];
    u32 abi_union_f64_count = (u32)(sizeof(abi_union_f64) / sizeof(abi_union_f64[0]));
    for (u32 index = 0; index < abi_union_f64_count; index += 1)
    {
        IrField* fields = arena_allocate(arena, IrField, index + 1);
        for (u32 field_index = 0; field_index <= index; field_index += 1)
        {
            fields[field_index] = (IrField){.type = abi_f64, .offset = 0};
        }
        abi_union_f64[index] = ir_program_add_type(&abi_program, (IrType){
            .kind = IR_TYPE_UNION,
            .fields = fields,
            .field_count = index + 1,
            .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
        });
    }
    IrField* abi_union_float_double_fields = arena_allocate(arena, IrField, 2);
    abi_union_float_double_fields[0] = (IrField){.type = abi_f32, .offset = 0};
    abi_union_float_double_fields[1] = (IrField){.type = abi_f64, .offset = 0};
    IrTypeId abi_union_float_double = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_UNION,
        .fields = abi_union_float_double_fields,
        .field_count = 2,
        .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_union_double_integer_fields = arena_allocate(arena, IrField, 2);
    abi_union_double_integer_fields[0] = (IrField){.type = abi_f64, .offset = 0};
    abi_union_double_integer_fields[1] = (IrField){.type = abi_integer, .offset = 0};
    IrTypeId abi_union_double_integer = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_UNION,
        .fields = abi_union_double_integer_fields,
        .field_count = 2,
        .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_union_struct_f64x2_fields = arena_allocate(arena, IrField, 2);
    abi_union_struct_f64x2_fields[0] = (IrField){.type = abi_struct_f64x2, .offset = 0};
    abi_union_struct_f64x2_fields[1] = (IrField){.type = abi_f64, .offset = 0};
    IrTypeId abi_union_struct_f64x2 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_UNION,
        .fields = abi_union_struct_f64x2_fields,
        .field_count = 2,
        .layout = {.size = 16, .alignment = 8, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });
    IrField* abi_struct_union_f64_fields = arena_allocate(arena, IrField, 2);
    abi_struct_union_f64_fields[0] = (IrField){.type = abi_union_f64[0], .offset = 0};
    abi_struct_union_f64_fields[1] = (IrField){.type = abi_f64, .offset = 8};
    IrTypeId abi_struct_union_f64 = ir_program_add_type(&abi_program, (IrType){
        .kind = IR_TYPE_STRUCT,
        .fields = abi_struct_union_f64_fields,
        .field_count = 2,
        .layout = {.size = 16, .alignment = 8, .abi_class = IR_ABI_CLASS_AGGREGATE, .resolved = true},
    });


    return abi_program;
}

BUSTER_GLOBAL_LOCAL u64 audit_now(void)
{
    struct timespec timestamp;
    if (clock_gettime(CLOCK_MONOTONIC, &timestamp)) abort();
    return (u64)timestamp.tv_sec * 1000000000 + (u64)timestamp.tv_nsec;
}

// A fixed trace, shared by both revisions. The sparse case asks for scalar
// f32/f80, complex f80 and a two-double record; the dense case asks all types.
BUSTER_GLOBAL_LOCAL __attribute__((noinline)) u64 audit_query_trace(IrProgram* program, bool sparse)
{
    u32 sparse_ids[] = {0, 2, 6, 14};
    u32 count = sparse ? 4 : program->types.count;
    u64 checksum = 0;
    for (u32 index = 0; index < count; index += 1)
    {
        IrTypeId type = {sparse ? sparse_ids[index] : index};
        for (u32 use = 0; use < IR_ABI_USE_VARIADIC_ARGUMENT; use += 1)
        {
            IrAbiValue value = ir_type_abi_value(program, type, IR_ABI_CONVENTION_SYSTEMV_X86_64, (IrAbiUse)use);
            checksum += value.part_count + value.parts[0].size;
        }
    }
    return checksum;
}

BUSTER_GLOBAL_LOCAL void audit_reset(IrProgram* program)
{
#if BUSTER_ABI_BENCH_BEFORE
    for (u32 type = 0; type < program->types.count; type += 1) program->types.types[type].abi = 0;
#else
    memset(program->abi_contexts, 0, sizeof(program->abi_contexts));
#endif
}

BUSTER_GLOBAL_LOCAL u64 audit_classifications(IrProgram* program)
{
    u64 result = 0;
#if BUSTER_ABI_BENCH_BEFORE
    // The unchanged old SysV resolver calls the classifier for argument and
    // result, then copies argument to the variadic slot: two per resolved id.
    for (u32 type = 0; type < program->types.count; type += 1)
    {
        IrTypeAbi* abi = program->types.types[type].abi;
        result += abi && abi->resolved[IR_ABI_CONVENTION_SYSTEMV_X86_64] ? 2 : 0;
    }
#else
    result = program->abi_contexts[IR_ABI_CONVENTION_SYSTEMV_X86_64].classified_values;
#endif
    return result;
}

int main(int argc, char** argv)
{
    audit_initialize();
    bool sparse = argc > 1 && !strcmp(argv[1], "sparse");
    enum { COLD_ITERATIONS = 2048, WARM_REPETITIONS = 32768 };
    IrProgram program = audit_corpus(program_state->arena);
    if (program.types.count != 23) abort();
    TemporalArena checkpoint = arena_begin_temporal(program.arena);
    u64 preparation_ns = 0;
    u64 first_queries_ns = 0;
    u64 checksum = 0;
    u64 prepared_classifications = 0;
    u64 cold_classifications = 0;
    // One cold preflight warms allocator/scratch pages and validates every
    // result byte against the production classifier outside measured spans.
    for (u32 iteration = 0; iteration <= COLD_ITERATIONS; iteration += 1)
    {
        scratch_end(checkpoint);
        audit_reset(&program);
        u64 start = audit_now();
        ir_prepare_program_abi(&program, IR_ABI_CONVENTION_SYSTEMV_X86_64);
        u64 middle = audit_now();
        checksum += audit_query_trace(&program, sparse);
        u64 end = audit_now();
        if (iteration)
        {
            preparation_ns += middle - start;
            first_queries_ns += end - middle;
        }
        if (!iteration)
        {
            u32 sparse_ids[] = {0, 2, 6, 14};
            u32 count = sparse ? 4 : program.types.count;
            for (u32 index = 0; index < count; index += 1)
            {
                IrTypeId type = {sparse ? sparse_ids[index] : index};
                for (u32 use = 0; use < IR_ABI_USE_VARIADIC_ARGUMENT; use += 1)
                {
                    IrAbiValue actual = ir_type_abi_value(&program, type, IR_ABI_CONVENTION_SYSTEMV_X86_64, (IrAbiUse)use);
                    IrAbiValue expected = ir_classify_abi_value(&program, type, IR_ABI_CONVENTION_SYSTEMV_X86_64, use == IR_ABI_USE_RESULT, false);
                    if (memcmp(&actual, &expected, sizeof(actual))) abort();
                }
            }
        }
    }
    cold_classifications = audit_classifications(&program);
    u64 warm_start = audit_now();
    for (u32 repeat = 0; repeat < WARM_REPETITIONS; repeat += 1)
    {
        checksum += audit_query_trace(&program, sparse);
    }
    u64 warm_ns = audit_now() - warm_start;
    u64 warm_misses = audit_classifications(&program) - cold_classifications;
    u64 cache_bytes = 0;
    u32 cache_allocations = 0;
#if BUSTER_ABI_BENCH_BEFORE
    prepared_classifications = (u64)program.types.count * 2;
    for (u32 type = 0; type < program.types.count; type += 1)
    {
        cache_allocations += program.types.types[type].abi != 0;
        cache_bytes += program.types.types[type].abi ? sizeof(IrTypeAbi) : 0;
    }
#else
    IrAbiContext* context = program.abi_contexts + IR_ABI_CONVENTION_SYSTEMV_X86_64;
    cache_bytes = context->allocated_bytes;
    for (u32 use = 0; use < IR_ABI_USE_COUNT; use += 1)
    {
        if (context->pages[use])
        {
            cache_allocations += 1;
            for (u32 page = 0; page < context->page_capacity; page += 1)
            {
                cache_allocations += context->pages[use][page] != 0;
            }
        }
    }
#endif
    u64 fingerprint = UINT64_C(14695981039346656037);
    u32 sparse_ids[] = {0, 2, 6, 14};
    u32 count = sparse ? 4 : program.types.count;
    for (u32 index = 0; index < count; index += 1)
    {
        IrTypeId type = {sparse ? sparse_ids[index] : index};
        for (u32 use = 0; use < IR_ABI_USE_VARIADIC_ARGUMENT; use += 1)
        {
            IrAbiValue value = ir_type_abi_value(&program, type, IR_ABI_CONVENTION_SYSTEMV_X86_64, (IrAbiUse)use);
            for (u32 byte = 0; byte < sizeof(value); byte += 1)
            {
                fingerprint = (fingerprint ^ ((u8*)&value)[byte]) * UINT64_C(1099511628211);
            }
        }
    }
    printf("{\"before\":%d,\"case\":\"%s\",\"types\":%u,\"cold_iterations\":%u,\"queries_per_trace\":%u,"
           "\"preparation_ns\":%llu,\"first_queries_ns\":%llu,\"warm_ns\":%llu,\"warm_queries\":%llu,"
           "\"prepared_classifications\":%llu,\"cold_classifications\":%llu,\"warm_misses\":%llu,"
           "\"cache_bytes\":%llu,\"cache_allocations\":%u,\"type_bytes\":%llu,\"checksum\":%llu,\"fingerprint\":\"%016llx\"}\n",
           BUSTER_ABI_BENCH_BEFORE, sparse ? "sparse" : "dense", program.types.count, COLD_ITERATIONS, count * 2,
           (unsigned long long)preparation_ns, (unsigned long long)first_queries_ns, (unsigned long long)warm_ns,
           (unsigned long long)WARM_REPETITIONS * count * 2, (unsigned long long)prepared_classifications,
           (unsigned long long)cold_classifications, (unsigned long long)warm_misses, (unsigned long long)cache_bytes,
           cache_allocations, (unsigned long long)sizeof(IrType) * program.types.count, (unsigned long long)checksum,
           (unsigned long long)fingerprint);
    return 0;
}

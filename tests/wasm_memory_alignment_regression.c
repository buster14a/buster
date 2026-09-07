#define BUSTER_UNITY_BUILD 1
#define BUSTER_SINGLE_THREADED 1
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
#include <buster/lib/compiler/ir/ir.c>
#include <buster/lib/compiler/wasm/wasm.c>

int main(void)
{
    audit_initialize();
    Wasm64Context context = {.arena = program_state->arena};
    Wasm64FunctionEmitter emitter = {.context = &context, .body = {.arena = program_state->arena}};
    IrType types[] = {
        {.kind=IR_TYPE_INTEGER, .bit_width=8, .layout={.size=1, .resolved=true}},
        {.kind=IR_TYPE_INTEGER, .bit_width=16, .layout={.size=2, .resolved=true}},
        {.kind=IR_TYPE_INTEGER, .bit_width=32, .layout={.size=4, .resolved=true}},
        {.kind=IR_TYPE_INTEGER, .bit_width=64, .layout={.size=8, .resolved=true}},
        {.kind=IR_TYPE_BOOLEAN, .bit_width=1, .layout={.size=1, .resolved=true}},
        {.kind=IR_TYPE_FLOAT, .bit_width=32, .layout={.size=4, .resolved=true}},
        {.kind=IR_TYPE_FLOAT, .bit_width=64, .layout={.size=8, .resolved=true}},
        {.kind=IR_TYPE_POINTER, .bit_width=64, .layout={.size=8, .resolved=true}},
    };
    for (u32 t=0; t<BUSTER_ARRAY_LENGTH(types); t+=1)
    {
        for (u32 alignment=1; alignment<=64; alignment*=2)
        {
            types[t].layout.alignment=alignment;
            Wasm64ValType valtype=WASM64_VALTYPE_INVALID;
            wasm64_valtype_for_type(&types[t],false,&valtype);
            for (u32 store=0; store<2; store+=1)
            {
                emitter.body.length=0;
                if (store) wasm64_fe_store(&emitter,&types[t]);
                else wasm64_fe_load(&emitter,&types[t]);
                printf("%u %u %u %u ",t,alignment,store,valtype);
                for (u64 i=0; i<emitter.body.length; i+=1) printf("%02x",emitter.body.data[i]);
                puts("");
            }
        }
    }
    return context.error.code!=WASM64_ERROR_NONE;
}

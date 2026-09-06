/* Executes actual emitter output; no kernel privileges or handwritten emitter. */
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
#include <buster/lib/compiler/ebpf/ebpf.c>

BUSTER_GLOBAL_LOCAL bool audit_ebpf_run(EbpfSection* section, u64 regs[11])
{
    bool ok = section->data.length % 8 == 0;
    u64 pc = 0, steps = 0;
    while (ok && pc < section->data.length / 8 && steps++ < 1024)
    {
        u8* p = section->data.data + pc * 8;
        u8 code = p[0], dst = p[1] & 15, src = p[1] >> 4;
        if (dst >= 11 || src >= 11 || (code == 0x18 && pc + 1 >= section->data.length / 8))
        {
            ok = false;
            break;
        }
        s16 off; s32 imm;
        memcpy(&off, p + 2, 2); memcpy(&imm, p + 4, 4);
        u64 rhs = code & 8 ? regs[src] : (u64)(s64)imm;
        pc++;
        if (code == 0x18)
        {
            s32 high; memcpy(&high, section->data.data + pc * 8 + 4, 4);
            regs[dst] = (u64)(u32)imm | ((u64)(u32)high << 32); pc++;
        }
        else if ((code & 7) == 7)
        {
            switch (code & 0xf0)
            {
            case 0xb0: regs[dst] = rhs; break;
            case 0xa0: regs[dst] ^= rhs; break;
            case 0x50: regs[dst] &= rhs; break;
            case 0x60: regs[dst] <<= rhs & 63; break;
            case 0xc0: regs[dst] = (u64)((s64)regs[dst] >> (rhs & 63)); break;
            case 0x80: regs[dst] = 0 - regs[dst]; break;
            default: ok = false; break;
            }
        }
        else if ((code & 7) == 5)
        {
            bool take = false;
            switch (code & 0xf0)
            {
            case 0x00: take = true; break;
            case 0x10: take = regs[dst] == rhs; break;
            case 0x20: take = regs[dst] > rhs; break;
            case 0x30: take = regs[dst] >= rhs; break;
            case 0x50: take = regs[dst] != rhs; break;
            case 0x60: take = (s64)regs[dst] > (s64)rhs; break;
            case 0x70: take = (s64)regs[dst] >= (s64)rhs; break;
            case 0xa0: take = regs[dst] < rhs; break;
            case 0xb0: take = regs[dst] <= rhs; break;
            case 0xc0: take = (s64)regs[dst] < (s64)rhs; break;
            case 0xd0: take = (s64)regs[dst] <= (s64)rhs; break;
            default: ok = false; break;
            }
            if (take) pc = (u64)((s64)pc + off);
        }
        else ok = false;
    }
    return ok && pc == section->data.length / 8 && steps < 1024;
}

BUSTER_GLOBAL_LOCAL bool audit_ebpf_expected(IrBinaryOperation op, u64 a, u64 b)
{
    bool r = false;
    switch (op)
    {
    case IR_BINARY_INTEGER_EQUAL: case IR_BINARY_POINTER_EQUAL: case IR_BINARY_BOOLEAN_EQUAL: r = a == b; break;
    case IR_BINARY_INTEGER_NOT_EQUAL: case IR_BINARY_POINTER_NOT_EQUAL: case IR_BINARY_BOOLEAN_NOT_EQUAL: r = a != b; break;
    case IR_BINARY_SIGNED_LESS: r = (s64)a < (s64)b; break;
    case IR_BINARY_SIGNED_LESS_EQUAL: r = (s64)a <= (s64)b; break;
    case IR_BINARY_SIGNED_GREATER: r = (s64)a > (s64)b; break;
    case IR_BINARY_SIGNED_GREATER_EQUAL: r = (s64)a >= (s64)b; break;
    case IR_BINARY_UNSIGNED_LESS: r = a < b; break;
    case IR_BINARY_UNSIGNED_LESS_EQUAL: r = a <= b; break;
    case IR_BINARY_UNSIGNED_GREATER: r = a > b; break;
    case IR_BINARY_UNSIGNED_GREATER_EQUAL: r = a >= b; break;
    default: abort();
    }
    return r;
}

int main(int argc, char** argv)
{
    audit_initialize();
    bool compare = argc < 2 || strcmp(argv[1], "compare") == 0;
    bool logical = argc < 2 || strcmp(argv[1], "logical") == 0;
    bool bitnot = argc < 2 || strcmp(argv[1], "bitnot") == 0;
    unsigned tests = 0, failures = 0;
    IrType types[2] = {
        {.id={0}, .kind=IR_TYPE_INTEGER, .bit_width=32, .is_signed=true, .layout={.size=4, .alignment=4, .resolved=true}},
        {.id={1}, .kind=IR_TYPE_BOOLEAN, .bit_width=1, .layout={.size=1, .alignment=1, .resolved=true}}
    };
    IrProgram program = {.types={.types=types,.count=2}};
    EbpfContext context = {.arena=program_state->arena, .program=&program};
    EbpfSection section = {.data={.arena=program_state->arena}};
    EbpfFunctionEmitter emitter = {.context=&context, .section=&section};
    IrBinaryOperation ops[] = {IR_BINARY_INTEGER_EQUAL, IR_BINARY_INTEGER_NOT_EQUAL, IR_BINARY_POINTER_EQUAL, IR_BINARY_POINTER_NOT_EQUAL,
        IR_BINARY_BOOLEAN_EQUAL, IR_BINARY_BOOLEAN_NOT_EQUAL, IR_BINARY_SIGNED_LESS, IR_BINARY_SIGNED_LESS_EQUAL, IR_BINARY_SIGNED_GREATER,
        IR_BINARY_SIGNED_GREATER_EQUAL, IR_BINARY_UNSIGNED_LESS, IR_BINARY_UNSIGNED_LESS_EQUAL, IR_BINARY_UNSIGNED_GREATER, IR_BINARY_UNSIGNED_GREATER_EQUAL};
    u64 values[] = {0, 1, 2, UINT64_MAX, (u64)INT64_MIN, (u64)INT64_MAX};
    for (u32 i=0; compare && i<BUSTER_ARRAY_LENGTH(ops); i++)
    {
        u32 count = ops[i] == IR_BINARY_BOOLEAN_EQUAL || ops[i] == IR_BINARY_BOOLEAN_NOT_EQUAL ? 2 : BUSTER_ARRAY_LENGTH(values);
        for (u32 a=0; a<count; a++) for (u32 b=0; b<count; b++)
        {
            section.data.length=0;
            ebpf_fe_emit_comparison(&emitter, ops[i], 0, 9, 8);
            u64 regs[11] = {0}; regs[0]=values[a]; regs[9]=values[b];
            bool ok=audit_ebpf_run(&section,regs) && regs[8]==audit_ebpf_expected(ops[i],values[a],values[b]);
            tests++; failures+=!ok;
            if (!ok && failures<=3) printf("comparison op=%u left=%llu right=%llu: got=%llu expected=%u\n",ops[i],(unsigned long long)values[a],(unsigned long long)values[b],(unsigned long long)regs[8],audit_ebpf_expected(ops[i],values[a],values[b]));
        }
    }
    IrValueId operand = {0};
    u64 immediate=0;
    IrInstruction instructions[2] = {
        {.opcode=IR_OPCODE_CONSTANT_INTEGER,.canonical_type={0},.result={0},.immediate_count=1,.immediates=&immediate},
        {.opcode=IR_OPCODE_UNARY,.canonical_type={0},.result={1},.operand_count=1,.operands=&operand}
    };
    IrValue ir_values[2] = {{.canonical_type={0},.definition={0}},{.canonical_type={0},.definition={1}}};
    IrFunction function={.instructions=instructions,.instruction_count=2,.values=ir_values,.value_count=2};
    s16 slots[2]={EBPF_SLOT_NONE,EBPF_SLOT_NONE};
    emitter.function=&function; emitter.value_slots=slots;
    if (logical)
    {
        instructions[0].canonical_type.value=1; instructions[1].canonical_type.value=1;
        ir_values[0].canonical_type.value=1; ir_values[1].canonical_type.value=1;
        instructions[1].unary_operation=IR_UNARY_BOOLEAN_NOT;
        for (immediate=0;immediate<2;immediate++)
        {
            section.data.length=0; ebpf_fe_emit_unary(&emitter,&instructions[1]);
            u64 regs[11]={0}; bool ok=audit_ebpf_run(&section,regs) && regs[8]==!immediate;
            tests++;failures+=!ok;
            if(!ok)printf("logical-not input=%llu: got=%llu expected=%u\n",(unsigned long long)immediate,(unsigned long long)regs[8],!immediate);
        }
    }
    if (bitnot)
    {
        instructions[0].canonical_type.value=0; instructions[1].canonical_type.value=0;
        ir_values[0].canonical_type.value=0; ir_values[1].canonical_type.value=0;
        instructions[1].unary_operation=IR_UNARY_INTEGER_BITWISE_NOT;
        u32 widths[]={8,16,32,64};
        for(u32 w=0;w<4;w++)for(u32 sign=0;sign<2;sign++)for(u32 a=0;a<4;a++)
        {
            types[0].bit_width=widths[w]; types[0].is_signed=sign;
            immediate=values[a]; section.data.length=0; ebpf_fe_emit_unary(&emitter,&instructions[1]);
            u64 mask=widths[w]==64?UINT64_MAX:((UINT64_C(1)<<widths[w])-1);
            u64 want=~immediate & mask;
            if(sign && widths[w]<64 && (want&(UINT64_C(1)<<(widths[w]-1))))want|=~mask;
            u64 regs[11]={0}; bool ok=audit_ebpf_run(&section,regs) && regs[0]==want;
            tests++;failures+=!ok;
            if(!ok && failures<=3)printf("bitnot width=%u signed=%u input=%llu got=%llu expected=%llu\n",widths[w],sign,(unsigned long long)immediate,(unsigned long long)regs[0],(unsigned long long)want);
        }
    }
    printf("%u/%u passed; %u failures; emitter_error=%u\n",tests-failures,tests,failures,context.error.code);
    return failures!=0 || context.error.code!=EBPF_ERROR_NONE;
}

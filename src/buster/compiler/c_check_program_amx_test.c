// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// AMX code-generation oracle without executing AMX: compile independent load/compute/store chains with Clang,
// decode their actual instructions, and compare a trace of configured shapes, packed input addresses, operation
// operands and output stores with the generated leaves. General instructions execute in this small concrete model;
// tile operations produce expression hashes. Native AMX arithmetic remains a hardware qualification.

typedef struct CheckTestAmxTrace
{
    uint64_t registers[16];
    uint8_t stack[16384];
    uint8_t config[64];
    uint64_t tiles[8];
    uint64_t vectors[32];
    uint64_t result;
    uint32_t releases;
    uint32_t clears;
    bool valid;
} CheckTestAmxTrace;

static uint64_t check_test_amx_hash(uint64_t a, uint64_t b)
{
    return (a ^ b) * UINT64_C(1099511628211);
}

static uint64_t check_test_amx_named(const char* name)
{
    uint64_t result = UINT64_C(14695981039346656037);
    for (uint32_t i = 0; name[i] != 0; i += 1)
    {
        result = check_test_amx_hash(result, (uint8_t)name[i]);
    }
    return result;
}

static uint64_t check_test_amx_address(CheckTestAmxTrace* trace, X86Instruction instruction)
{
    uint64_t result = trace->registers[instruction.rm] + (uint64_t)(int64_t)instruction.displacement;
    if ((instruction.mode & BUSTER_X86_MODE_INDEX) != 0)
    {
        result += trace->registers[instruction.index] << (instruction.mode & BUSTER_X86_MODE_SCALE);
    }
    return result;
}

static uint64_t check_test_amx_read(CheckTestAmxTrace* trace, X86Instruction instruction, uint32_t bytes)
{
    uint64_t result = trace->registers[instruction.rm];
    if (instruction.mode != BUSTER_X86_REGISTER_DIRECT)
    {
        const uint64_t address = check_test_amx_address(trace, instruction);
        result = 0;
        trace->valid &= address <= sizeof(trace->stack) && bytes <= sizeof(trace->stack) - address;
        for (uint32_t b = 0; trace->valid && b < bytes; b += 1)
        {
            result |= (uint64_t)trace->stack[address + b] << (b * 8);
        }
    }
    return result;
}

static void check_test_amx_write(CheckTestAmxTrace* trace, X86Instruction instruction, uint64_t value, uint32_t bytes)
{
    if (instruction.mode == BUSTER_X86_REGISTER_DIRECT)
    {
        const uint64_t mask = bytes == 8 ? UINT64_MAX : (UINT64_C(1) << (bytes * 8)) - 1;
        trace->registers[instruction.rm] = (bytes >= 4 ? 0 : trace->registers[instruction.rm] & ~mask) | (value & mask);
    }
    else
    {
        const uint64_t address = check_test_amx_address(trace, instruction);
        trace->valid &= address <= sizeof(trace->stack) && bytes <= sizeof(trace->stack) - address;
        for (uint32_t b = 0; trace->valid && b < bytes; b += 1)
        {
            trace->stack[address + b] = (uint8_t)(value >> (8 * b));
        }
    }
}

static uint64_t check_test_amx_shape(const CheckTestAmxTrace* trace, uint32_t tile)
{
    return (uint64_t)trace->config[48 + tile] << 32 | (uint32_t)trace->config[16 + 2 * tile] | (uint32_t)trace->config[17 + 2 * tile] << 8;
}

static void check_test_amx_step(CheckTestAmxTrace* trace, X86Instruction instruction)
{
    const X86FormDescription* form = x86_form_descriptions + instruction.form;
    const char* name = form->mnemonic;
    const uint32_t bytes = form->operand_bytes;
    if (strcmp(name, "mov") == 0)
    {
        if (form->syntax == X86_SYNTAX_REG_RM)
        {
            const uint64_t value = check_test_amx_read(trace, instruction, bytes);
            X86Instruction target = { .rm = instruction.reg };
            check_test_amx_write(trace, target, value, bytes);
        }
        else
        {
            const bool immediate = form->immediate_bytes != 0;
            check_test_amx_write(trace, instruction, immediate ? (uint64_t)(int64_t)instruction.immediate : trace->registers[instruction.reg], bytes);
        }
    }
    else if (strcmp(name, "push") == 0)
    {
        trace->registers[X86_REGISTER_RSP] -= 8;
        check_test_amx_write(trace, (X86Instruction){ .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE }, trace->registers[instruction.rm], 8);
    }
    else if (strcmp(name, "pop") == 0)
    {
        trace->registers[instruction.rm] = check_test_amx_read(trace, (X86Instruction){ .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE }, 8);
        trace->registers[X86_REGISTER_RSP] += 8;
    }
    else if (strcmp(name, "and") == 0 || strcmp(name, "sub") == 0 || strcmp(name, "shr") == 0)
    {
        uint64_t* value = trace->registers + instruction.rm;
        if (strcmp(name, "and") == 0)
        {
            *value &= (uint64_t)(int64_t)(int8_t)instruction.immediate;
        }
        else if (strcmp(name, "sub") == 0)
        {
            *value -= (uint64_t)(int64_t)instruction.immediate;
        }
        else
        {
            *value = (uint32_t)*value >> instruction.immediate;
        }
    }
    else if (strcmp(name, "xor") == 0)
    {
        trace->valid &= instruction.rm == instruction.reg;
        trace->registers[instruction.rm] = 0;
    }
    else if (strcmp(name, "xorps") == 0 || strcmp(name, "vxorps") == 0 || strcmp(name, "pxor") == 0 || strcmp(name, "vpxor") == 0)
    {
        trace->valid &= instruction.rm == (form->family == BUSTER_X86_FAMILY_LEGACY ? instruction.reg : instruction.vvvv);
        trace->vectors[instruction.reg] = 0;
    }
    else if (strcmp(name, "lea") == 0)
    {
        trace->registers[instruction.reg] = check_test_amx_address(trace, instruction);
    }
    else if (strcmp(name, "ldtilecfg") == 0)
    {
        const uint64_t address = check_test_amx_address(trace, instruction);
        trace->valid &= address <= sizeof(trace->stack) - 64;
        if (trace->valid)
        {
            memcpy(trace->config, trace->stack + address, 64);
            trace->valid &= trace->config[0] == 1;
            for (uint32_t b = 1; b < 64; b += 1)
            {
                const bool reserved = b < 16 || (b >= 32 && b < 48) || b >= 56;
                trace->valid &= !reserved || trace->config[b] == 0;
            }
            for (uint32_t tile = 0; tile < 8; tile += 1)
            {
                const uint32_t columns = (uint32_t)trace->config[16 + 2 * tile] | (uint32_t)trace->config[17 + 2 * tile] << 8;
                const uint32_t rows = trace->config[48 + tile];
                trace->valid &= columns <= 64 && rows <= 16 && ((columns == 0) == (rows == 0));
            }
        }
    }
    else if (strncmp(name, "tileloadd", 9) == 0)
    {
        trace->tiles[instruction.reg] = check_test_amx_hash(check_test_amx_hash(check_test_amx_named(name), check_test_amx_shape(trace, instruction.reg)),
                                                         check_test_amx_hash(trace->registers[instruction.rm], trace->registers[instruction.index]));
    }
    else if (strcmp(name, "tilezero") == 0)
    {
        trace->tiles[instruction.reg] = check_test_amx_hash(check_test_amx_named(name), check_test_amx_shape(trace, instruction.reg));
    }
    else if (strncmp(name, "tdp", 3) == 0 || strncmp(name, "tcmm", 4) == 0)
    {
        uint64_t value = check_test_amx_hash(check_test_amx_named(name), trace->tiles[instruction.reg]);
        value = check_test_amx_hash(value, trace->tiles[instruction.rm]);
        trace->tiles[instruction.reg] = check_test_amx_hash(value, trace->tiles[instruction.vvvv]);
    }
    else if (strncmp(name, "tcvtrow", 7) == 0 || strcmp(name, "tilemovrow") == 0)
    {
        trace->vectors[instruction.reg] = check_test_amx_hash(check_test_amx_hash(check_test_amx_named(name), trace->tiles[instruction.rm]),
                                                           trace->registers[instruction.vvvv]);
    }
    else if (strcmp(name, "tilestored") == 0)
    {
        trace->result = check_test_amx_hash(check_test_amx_hash(trace->tiles[instruction.reg], trace->registers[instruction.rm]),
                                           trace->registers[instruction.index]);
    }
    else if (strcmp(name, "movups") == 0 || strcmp(name, "vmovups") == 0 || strcmp(name, "vmovdqu64") == 0)
    {
        const uint64_t address = check_test_amx_address(trace, instruction);
        if (address < sizeof(trace->stack))
        {
            trace->valid &= trace->vectors[instruction.reg] == 0 && bytes <= sizeof(trace->stack) - address;
            if (trace->valid)
            {
                memset(trace->stack + address, 0, bytes);
            }
        }
        else
        {
            trace->result = check_test_amx_hash(check_test_amx_hash(trace->vectors[instruction.reg], address), 64);
        }
    }
    else if (strcmp(name, "rep stosq") == 0)
    {
        trace->valid &= trace->registers[X86_REGISTER_RAX] == 0 && trace->registers[X86_REGISTER_RCX] == 128 &&
                        trace->registers[X86_REGISTER_RDI] == 0x50000;
        trace->registers[X86_REGISTER_RDI] += 1024;
        trace->registers[X86_REGISTER_RCX] = 0;
        trace->clears += 1;
    }
    else if (strcmp(name, "tilerelease") == 0)
    {
        trace->releases += 1;
        memset(trace->config, 0, sizeof(trace->config));
    }
    else
    {
        const bool harmless = strcmp(name, "ret") == 0 || strcmp(name, "vzeroupper") == 0 || strcmp(name, "nop") == 0;
        if (!harmless)
        {
            fprintf(stderr, "AMX trace has no rule for %s\n", name);
        }
        trace->valid &= harmless;
    }
}

static uint64_t check_test_amx_trace(const X86Instruction* records, const X86Decoded* decoded, uint32_t count,
                                   const CBuiltinX86HelperInfo* info, bool oracle, uint32_t m, uint32_t n, uint32_t k)
{
    CheckTestAmxTrace trace = { .valid = true };
    // A missing config initializer must not be hidden by zero-initialized test memory.
    memset(trace.stack, 0xa5, sizeof(trace.stack));
    trace.registers[X86_REGISTER_RSP] = 8192;
    if (oracle)
    {
        trace.registers[X86_REGISTER_RDI] = 0x50000;
        trace.registers[X86_REGISTER_RSI] = 0x20000;
        trace.registers[X86_REGISTER_RDX] = 0x30000;
        trace.registers[X86_REGISTER_RCX] = 0x40000;
        trace.registers[X86_REGISTER_R8] = m > 2 ? 2 : 0;
    }
    else
    {
        trace.registers[X86_REGISTER_RDI] = m;
        trace.registers[X86_REGISTER_RSI] = n;
        trace.registers[X86_REGISTER_RDX] = info->amx == CBUILTIN_AMX_DOT ? k : info->amx == CBUILTIN_AMX_ZERO ? 0x50000 :
                                             info->amx == CBUILTIN_AMX_STORE ? 0x50000 : 0x20000;
        trace.registers[X86_REGISTER_RCX] = info->amx == CBUILTIN_AMX_DOT ? 0x20000 : info->amx == CBUILTIN_AMX_ROW ? m > 2 ? 2 : 0 : 92;
        trace.registers[X86_REGISTER_R8] = info->amx == CBUILTIN_AMX_DOT ? 0x30000 : info->amx == CBUILTIN_AMX_STORE ? 0x20000 : 0x50000;
        trace.registers[X86_REGISTER_R9] = 0x40000;
        check_test_amx_write(&trace, (X86Instruction){ .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 8 }, 0x50000, 8);
    }
    for (uint32_t r = 0; trace.valid && r < count; r += 1)
    {
        check_test_amx_step(&trace, decoded != NULL ? decoded[r].instruction : records[r]);
    }
    return trace.valid && trace.releases == 1 && (oracle || info->output_bytes != 1024 || trace.clears == 1) ? trace.result : 0;
}

static void check_test_program_amx_oracle(CheckTest* test, const char* clang)
{
    char directory[] = "/tmp/buster-amx-XXXXXX";
    const bool temporary = mkdtemp(directory) != NULL;
    char source[128], object_path[128], log[128];
    snprintf(source, sizeof(source), "%s/oracle.c", directory);
    snprintf(object_path, sizeof(object_path), "%s/oracle.o", directory);
    snprintf(log, sizeof(log), "%s/oracle.log", directory);
    FILE* file = temporary ? fopen(source, "w") : NULL;
    bool valid = file != NULL;
    static const uint32_t shapes[3][3] = { { 1, 4, 4 }, { 3, 20, 12 }, { 16, 64, 64 } };
    if (file != NULL)
    {
        fprintf(file, "#include <immintrin.h>\n");
        for (uint32_t kind = CBUILTIN_X86_HELPER_TILEZERO; kind < CBUILTIN_X86_HELPER_COUNT; kind += 1)
        {
            const CBuiltinX86HelperInfo* info = cbuiltin_x86_helper_infos + kind;
            for (uint32_t shape = 0; shape < BUSTER_ARRAY_LENGTH(shapes); shape += 1)
            {
                const uint32_t m = shapes[shape][0], n = shapes[shape][1], k = shapes[shape][2];
                fprintf(file, "__attribute__((target(\"amx-tile,amx-int8,amx-bf16,amx-fp16,amx-complex,amx-fp8,amx-avx512,amx-movrs\")))"
                              "void case_%u_%u(void*out,const void*d,const void*a,const void*b,unsigned row){", kind, shape);
                if (info->amx == CBUILTIN_AMX_DOT)
                {
                    fprintf(file, "_tile1024i x=__builtin_ia32_tileloadd64_internal(%u,%u,d,%u);"
                                  "_tile1024i y=__builtin_ia32_tileloadd64_internal(%u,%u,a,%u);"
                                  "_tile1024i z=__builtin_ia32_tileloadd64_internal(%u,%u,b,%u);"
                                  "_tile1024i r=%s(%u,%u,%u,x,y,z);", m,n,n,m,k,k,k/4,n,n,info->name,m,n,k);
                }
                else if (info->amx == CBUILTIN_AMX_LOAD)
                {
                    fprintf(file, "_tile1024i r=%s(%u,%u,d,92);", info->name,m,n);
                }
                else if (info->amx == CBUILTIN_AMX_ZERO)
                {
                    fprintf(file, "_tile1024i r=%s(%u,%u);", info->name,m,n);
                }
                else
                {
                    fprintf(file, "_tile1024i x=__builtin_ia32_tileloadd64_internal(%u,%u,d,%u);", m,n,n);
                }
                if (info->amx == CBUILTIN_AMX_ROW)
                {
                    fprintf(file, "__m512i r=(__m512i)%s(%u,%u,x,row);__builtin_memcpy(out,&r,64);", info->name,m,n);
                }
                else if (info->amx == CBUILTIN_AMX_STORE)
                {
                    fprintf(file, "%s(%u,%u,out,92,x);", info->name,m,n);
                }
                else
                {
                    fprintf(file, "__builtin_ia32_tilestored64_internal(%u,%u,out,%u,r);", m,n,n);
                }
                fprintf(file, "}\n");
            }
        }
        valid &= fclose(file) == 0;
    }
    char* command[] = { (char*)clang, "-O1", "-Wno-psabi", "-fno-stack-protector", "-c", source, "-o", object_path, NULL };
    valid = valid && object_test_run(command, log, log) == 0 && x86_disassembler_initialize();
    X86IntrinsicObject object = { 0 };
    valid = valid && x86_intrinsic_object_read(&object, object_path);
    check_test_expect(test, valid, "AMX Clang instruction oracle compiles and decodes without executing AMX");
    Arena output = arena_reserve(1 << 20);
    valid = valid && output.base != NULL;
    for (uint32_t kind = CBUILTIN_X86_HELPER_TILEZERO; valid && kind < CBUILTIN_X86_HELPER_COUNT; kind += 1)
    {
        const CBuiltinX86HelperInfo* info = cbuiltin_x86_helper_infos + kind;
        const CBuiltinX86Helper helper = cbuiltin_x86_helper((CBuiltinX86HelperKind)kind, &output);
        for (uint32_t shape = 0; shape < BUSTER_ARRAY_LENGTH(shapes); shape += 1)
        {
            char name[64], failure[128] = { 0 };
            snprintf(name, sizeof(name), "case_%u_%u", kind, shape);
            const Elf64_Sym* symbol = x86_intrinsic_function_named(&object, name);
            X86IntrinsicFunction function = { 0 };
            const bool decoded = symbol != NULL && x86_intrinsic_decode(&object, symbol, &function, failure, sizeof(failure));
            const uint32_t m = shapes[shape][0], n = shapes[shape][1], k = shapes[shape][2];
            const uint64_t oracle = decoded ? check_test_amx_trace(NULL, function.records, (uint32_t)function.count, info, true,m,n,k) : 0;
            const uint64_t ours = check_test_amx_trace(helper.code.records, NULL, helper.code.count, info, false,m,n,k);
            char description[192];
            snprintf(description, sizeof(description), "%s at %ux%ux%u: tilecfg, packed loads, operation and result store agree with Clang", info->name,m,n,k);
            check_test_expect(test, oracle != 0 && ours == oracle, description);
            if (info->amx == CBUILTIN_AMX_DOT && shape == 1)
            {
                X86Instruction changed[64];
                memcpy(changed, helper.code.records, helper.code.count * sizeof(*changed));
                for (uint32_t r = 0; r < helper.code.count; r += 1)
                {
                    if (changed[r].form == info->amx_form)
                    {
                        changed[r].rm = 2;
                        changed[r].vvvv = 1;
                    }
                }
                check_test_expect(test, check_test_amx_trace(changed,NULL,helper.code.count,info,false,m,n,k) != oracle,
                                  "AMX oracle rejects swapped multiply inputs");
                memcpy(changed, helper.code.records, helper.code.count * sizeof(*changed));
                for (uint32_t r = 0; r < helper.code.count; r += 1)
                {
                    if (changed[r].form == X86_FORM_MOV_RM16_R16 && changed[r].displacement == 18)
                    {
                        changed[r].reg = X86_REGISTER_RSI;
                    }
                }
                check_test_expect(test, check_test_amx_trace(changed,NULL,helper.code.count,info,false,m,n,k) != oracle,
                                  "AMX oracle rejects a wrong configured source width");
                memcpy(changed, helper.code.records, helper.code.count * sizeof(*changed));
                for (uint32_t r = 0; r < helper.code.count; r += 1)
                {
                    if (changed[r].form == X86_FORM_MOV_RM64_R64 && changed[r].mode == BUSTER_X86_MEMORY_BASE && changed[r].displacement == 32)
                    {
                        changed[r] = (X86Instruction){ .form = X86_FORM_NOP };
                    }
                }
                check_test_expect(test, check_test_amx_trace(changed,NULL,helper.code.count,info,false,m,n,k) != oracle,
                                  "AMX oracle rejects uninitialized reserved configuration bytes");
            }
            free(function.records);
        }
    }
    arena_release(&output);
    free(object.bytes);
    unlink(source);
    unlink(object_path);
    unlink(log);
    if (temporary)
    {
        rmdir(directory);
    }
}

static void check_test_program_amx(CheckTest* test, const char* clang)
{
    check_test_program_amx_oracle(test, clang);
    X86Target target = { 0 };
    const bool parsed = x86_target_parse("znver5", strlen("znver5"), &target);
    check_test_expect(test, parsed, "AMX value tests' vector target");
    if (parsed)
    {
        check_test_program_build_release();
        check_test_program_march = "-march=znver5";
        const X86Target native = x86_target_native();
        const uint64_t vector_features = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL);
        check_test_program_x86_source(test, clang, &target, (native.features & vector_features) == vector_features,
            "AMX-tile-values-memory-ABI",
            "typedef int Tile __attribute__((vector_size(1024),aligned(64)));"
            "static Tile transform(Tile a,Tile b,int seed){unsigned char*p=(unsigned char*)&a;"
            "const unsigned char*q=(const unsigned char*)&b;for(unsigned i=0;i<1024;i++)p[i]=(unsigned char)(p[i]+q[1023-i]+seed+i*7);return a;}"
            "int tested(int n){Tile a,b,c;fill(&a,sizeof(a),(unsigned)n);fill(&b,sizeof(b),(unsigned)n*13+7);"
            "Tile(*fp)(Tile,Tile,int)=transform;c=transform(a,b,n);c=fp(c,b,n+1);a[(unsigned)n&255]=n;"
            "Tile literal={n,2};unsigned h=2166136261u;FOLD(a);FOLD(b);FOLD(c);FOLD(literal);"
            "h^=(unsigned)transform(a,b,n)[(unsigned)n&255];return (int)h;}");
        target.extensions |= BUSTER_X86_EXTENSION(AMXTILE) | BUSTER_X86_EXTENSION(AMXINT8) | BUSTER_X86_EXTENSION(AMXBF16) |
                             BUSTER_X86_EXTENSION(AMXFP16) | BUSTER_X86_EXTENSION(AMXCOMPLEX) | BUSTER_X86_EXTENSION(AMXFP8) |
                             BUSTER_X86_EXTENSION(AMXAVX512) | BUSTER_X86_EXTENSION(AMXMOVRS) | BUSTER_X86_EXTENSION(AVX10_2);
        for (uint32_t kind = CBUILTIN_X86_HELPER_TILEZERO; kind < CBUILTIN_X86_HELPER_COUNT; kind += 1)
        {
            const CBuiltinX86HelperInfo* info = cbuiltin_x86_helper_infos + kind;
            char body[4096];
            const int prefix = snprintf(body, sizeof(body),
                "typedef int Tile __attribute__((vector_size(1024),aligned(64)));"
                "__attribute__((target(\"amx-tile,amx-int8,amx-bf16,amx-fp16,amx-complex,amx-fp8,amx-avx512,amx-movrs\")))"
                "int tested(int seed){Tile x,y,z,r;fill(&x,sizeof(x),(unsigned)seed);fill(&y,sizeof(y),(unsigned)seed+1);"
                "fill(&z,sizeof(z),(unsigned)seed+2);unsigned h=2166136261u;");
            size_t used = prefix > 0 ? (size_t)prefix : sizeof(body);
            if (used < sizeof(body))
            {
                int added = 0;
                if (info->amx == CBUILTIN_AMX_DOT)
                {
                    added = snprintf(body + used, sizeof(body) - used, "r=%s(3,20,12,x,y,z);FOLD(r);", info->name);
                }
                else if (info->amx == CBUILTIN_AMX_LOAD)
                {
                    added = snprintf(body + used, sizeof(body) - used, "r=%s(3,20,&x,92);FOLD(r);", info->name);
                }
                else if (info->amx == CBUILTIN_AMX_ZERO)
                {
                    added = snprintf(body + used, sizeof(body) - used, "r=%s(3,20);FOLD(r);", info->name);
                }
                else if (info->amx == CBUILTIN_AMX_STORE)
                {
                    added = snprintf(body + used, sizeof(body) - used, "%s(3,20,&r,92,x);FOLD(r);", info->name);
                }
                else
                {
                    const char* type = kind == CBUILTIN_X86_HELPER_TCVTROWD2PS ? "v16sf" :
                                       kind == CBUILTIN_X86_HELPER_TCVTROWPS2BF16H || kind == CBUILTIN_X86_HELPER_TCVTROWPS2BF16L ? "v32bf" :
                                       kind == CBUILTIN_X86_HELPER_TILEMOVROW ? "v16si" : "v32hf";
                    added = snprintf(body + used, sizeof(body) - used, "typedef _Float16 v32hf __attribute__((vector_size(64)));%s q=%s(3,20,x,2);FOLD(q);", type,info->name);
                }
                used += added > 0 ? (size_t)added : sizeof(body);
            }
            if (used < sizeof(body))
            {
                snprintf(body + used, sizeof(body) - used, "return (int)h;}");
                check_test_program_x86_source(test, clang, &target, false, info->name, body);
            }
            else
            {
                check_test_expect(test, false, "AMX builtin fixture source fits its bounded buffer");
            }
        }
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64";
        X86Target baseline = { 0 };
        const bool baseline_parsed = x86_target_parse("x86-64", strlen("x86-64"), &baseline);
        check_test_expect(test, baseline_parsed, "AMX refusal's baseline target");
        check_test_program_boundary_target(test,
            "typedef int Tile __attribute__((vector_size(1024),aligned(64)));"
            "Tile tested(void){return __builtin_ia32_tilezero_internal(3,20);}",
            "AMX-tile-operation-requires-target", true,false,0,0,"this AMX intrinsic without its required target features",&baseline);
        check_test_program_march = "-march=znver5";
        check_test_program_boundary_target(test,
            "typedef int Tile __attribute__((vector_size(1024),aligned(64)));typedef float V __attribute__((vector_size(64)));"
            "__attribute__((target(\"amx-avx512,no-avx10.2\"))) V tested(Tile x){return __builtin_ia32_tcvtrowd2ps_internal(3,20,x,2);}",
            "AMX-row-operation-requires-avx10.2", true,false,0,0,"this AMX intrinsic without its required target features",&target);
        check_test_program_march = "-march=x86-64";
        check_test_program_boundary_target(test,
            "typedef int Tile __attribute__((vector_size(1024),aligned(64)));Tile tested(Tile x){return x+x;}",
            "AMX-value-address-never-used-as-scalar-arithmetic", true,false,0,0,"arithmetic on 1024-byte tile vectors",&baseline);
        static const struct
        {
            const char* name;
            const char* source;
            const char* reason;
        } unsupported[] = {
            { "AMX-value-bitwise", "Tile tested(Tile x){return ~x;}", "arithmetic on 1024-byte tile vectors" },
            { "AMX-value-comparison", "Tile tested(Tile x,Tile y){return x==y;}", "arithmetic on 1024-byte tile vectors" },
            { "AMX-value-update", "Tile tested(Tile x){x+=1;return x;}", "arithmetic on 1024-byte tile vectors" },
            { "AMX-value-scalar-splat", "Tile tested(Tile x,int n){return x+n;}", "arithmetic on 1024-byte tile vectors" },
            { "AMX-value-volatile-element", "int tested(volatile Tile*x){return (*x)[3];}", "element access through volatile vectors" },
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(unsupported); i += 1)
        {
            char source[512];
            snprintf(source,sizeof(source),"typedef int Tile __attribute__((vector_size(1024),aligned(64)));%s",unsupported[i].source);
            check_test_program_boundary_target(test,source,unsupported[i].name,true,false,0,0,unsupported[i].reason,&baseline);
        }
        printf("  AMX: native tile arithmetic not executed; decoded Clang instruction traces and native memory-value ABI are separate checks\n");
    }
}

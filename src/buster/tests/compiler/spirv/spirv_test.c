// Direct compute regression boundary. spirv_compute_tests drives real C input;
// spirv_test_inspect independently walks little-endian words and interface
// contracts; spirv_test_emulate interprets only this bounded module subset.
// CPU emulation is semantic evidence, never physical Vulkan/GPU execution.
// Numeric opcodes come from SPIRV-Headers 3f17b2af6784bfa2c5aa5dbb8e0e74a607dd8b3b,
// include/spirv/unified1/spirv.core.grammar.json, independently of emitter tables.

#include <buster/tests/compiler/spirv/spirv_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/compiler/spirv/spirv.h>
#include <buster/lib/file.h>
#include <buster/lib/string.h>
#include <buster/lib/system_headers.h>

enum
{
    SPIRV_TEST_ID_LIMIT = 1024,
};

typedef struct SpirvTestModule SpirvTestModule;
struct SpirvTestModule
{
    u32 uint_type;
    u32 length;
    u32 index;
    u32 condition;
    u32 body;
    u32 merge;
    u32 constants;
    u32 multiply_offset;
    u32 add_count;
    u32 subtract_count;
    u32 multiply_count;
    u32 and_count;
    u32 or_count;
    u32 xor_count;
    u32 loads;
    u32 stores;
    bool shader;
    bool memory_model;
    bool entry;
    bool local_size;
    bool stride;
    bool block;
    bool member_offset;
    bool descriptor_set;
    bool binding;
    bool invocation;
    bool valid;
};

BUSTER_GLOBAL_LOCAL u32 spirv_test_word(ByteSlice bytes, u32 index)
{
    u8* word = bytes.pointer + (u64)index * 4;
    return (u32)word[0] | ((u32)word[1] << 8) | ((u32)word[2] << 16) | ((u32)word[3] << 24);
}

BUSTER_GLOBAL_LOCAL void spirv_test_write_word(ByteSlice bytes, u32 index, u32 value)
{
    for (u32 byte = 0; byte < 4; byte += 1) bytes.pointer[(u64)index * 4 + byte] = (u8)(value >> (byte * 8));
}

BUSTER_GLOBAL_LOCAL SpirvTestModule spirv_test_inspect(ByteSlice bytes)
{
    SpirvTestModule result = {0};
    result.valid = bytes.pointer && bytes.length >= 20 && bytes.length % 4 == 0 && bytes.length / 4 <= UINT32_MAX;
    u32 words = result.valid ? (u32)(bytes.length / 4) : 0;
    if (result.valid)
    {
        result.valid = spirv_test_word(bytes, 0) == UINT32_C(0x07230203) && spirv_test_word(bytes, 1) == UINT32_C(0x00010500) &&
                       spirv_test_word(bytes, 3) > 1 && spirv_test_word(bytes, 3) <= SPIRV_TEST_ID_LIMIT && spirv_test_word(bytes, 4) == 0;
    }
    u32 label = 0;
    for (u32 offset = 5; result.valid && offset < words;)
    {
        u32 first = spirv_test_word(bytes, offset);
        u32 count = first >> 16;
        u32 opcode = first & 65535u;
        result.valid = count && count <= words - offset;
        if (result.valid)
        {
            u32 a = count > 1 ? spirv_test_word(bytes, offset + 1) : 0;
            u32 b = count > 2 ? spirv_test_word(bytes, offset + 2) : 0;
            u32 c = count > 3 ? spirv_test_word(bytes, offset + 3) : 0;
            u32 d = count > 4 ? spirv_test_word(bytes, offset + 4) : 0;
            switch (opcode)
            {
            case 17: result.shader = count == 2 && a == 1; break;
            case 14: result.memory_model = count == 3 && a == 0 && b == 1; break;
            case 15: result.entry = count >= 7 && a == 5 && c == UINT32_C(0x6e72656b) && d == UINT32_C(0x00006c65); break;
            case 16:
                result.local_size = count == 6 && b == 17 && c == 1 && d == 1 && spirv_test_word(bytes, offset + 5) == 1;
                break;
            case 21: if (count == 4 && b == 32 && c == 0) result.uint_type = a; break;
            case 43:
                if (count == 4 && a == result.uint_type)
                {
                    if (c == 3) result.constants |= 1;
                    if (c == 17) result.constants |= 2;
                    if (c == UINT32_C(0xa5a5a5a5)) result.constants |= 4;
                }
                break;
            case 71:
                if (count == 3 && b == 2) result.block = true;
                if (count == 4 && b == 6 && c == 4) result.stride = true;
                if (count == 4 && b == 34 && c == 0) result.descriptor_set = true;
                if (count == 4 && b == 33 && c == 0) result.binding = true;
                if (count == 4 && b == 11 && c == 28) result.invocation = true;
                break;
            case 72: if (count == 5 && b == 0 && c == 35 && d == 0) result.member_offset = true; break;
            case 68: result.valid = count == 5 && a == result.uint_type && d == 0; result.length = b; break;
            case 176: result.valid = count == 5 && d == result.length; result.condition = b; result.index = c; break;
            case 247: result.valid = count == 3 && b == 0; result.merge = a; break;
            case 250: result.valid = count == 4 && a == result.condition && c == result.merge; result.body = b; break;
            case 248: result.valid = count == 2; label = a; break;
            case 61:
                if (a == result.uint_type)
                {
                    result.valid = count == 4 && result.body && label == result.body;
                    result.loads += 1;
                }
                break;
            case 62: result.valid = count == 3 && result.body && label == result.body; result.stores += 1; break;
            case 128: result.add_count += 1; break;
            case 130: result.subtract_count += 1; break;
            case 132: result.multiply_count += 1; result.multiply_offset = offset; break;
            case 197: result.or_count += 1; break;
            case 198: result.xor_count += 1; break;
            case 199: result.and_count += 1; break;
            default: break;
            }
            offset += count;
        }
    }
    return result;
}

typedef struct SpirvTestExecution SpirvTestExecution;
struct SpirvTestExecution
{
    u32 loads;
    u32 stores;
    bool success;
};

BUSTER_GLOBAL_LOCAL bool spirv_test_ids(ByteSlice bytes, u32 offset, u32 first, u32 end, u32 bound)
{
    bool valid = true;
    for (u32 index = first; valid && index < end; index += 1)
    {
        u32 id = spirv_test_word(bytes, offset + index);
        valid = id && id < bound;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL SpirvTestExecution spirv_test_emulate(ByteSlice bytes, u32* buffer, u32 length, u32 invocation)
{
    SpirvTestExecution result = {0};
    SpirvTestModule module = spirv_test_inspect(bytes);
    bool valid = module.valid;
    u32 words = valid ? (u32)(bytes.length / 4) : 0;
    u32 bound = valid ? spirv_test_word(bytes, 3) : 0;
    u32 values[SPIRV_TEST_ID_LIMIT] = {0};
    u32 labels[SPIRV_TEST_ID_LIMIT] = {0};
    u8 pointers[SPIRV_TEST_ID_LIMIT] = {0};
    for (u32 offset = 5; valid && offset < words; offset += spirv_test_word(bytes, offset) >> 16)
    {
        u32 first = spirv_test_word(bytes, offset);
        if ((first & 65535u) == 248)
        {
            valid = (first >> 16) == 2 && spirv_test_ids(bytes, offset, 1, 2, bound);
            if (valid) labels[spirv_test_word(bytes, offset + 1)] = offset;
        }
    }
    u32 offset = 5;
    bool returned = false;
    for (u32 steps = 0; valid && !returned && offset < words && steps < words * 2; steps += 1)
    {
        u32 first = spirv_test_word(bytes, offset);
        u32 count = first >> 16;
        u32 opcode = first & 65535u;
        u32 next = offset + count;
        u32 a = count > 1 ? spirv_test_word(bytes, offset + 1) : 0;
        u32 b = count > 2 ? spirv_test_word(bytes, offset + 2) : 0;
        u32 c = count > 3 ? spirv_test_word(bytes, offset + 3) : 0;
        u32 d = count > 4 ? spirv_test_word(bytes, offset + 4) : 0;
        switch (opcode)
        {
        // Metadata/type declarations carry no runtime effects in this oracle.
        case 14: case 15: case 16: case 17: case 19: case 20: case 21: case 23:
        case 29: case 30: case 32: case 33: case 54: case 56: case 71: case 72: case 247: case 248: break;
        case 43:
            valid = count == 4 && spirv_test_ids(bytes, offset, 1, 3, bound);
            if (valid) values[b] = c;
            break;
        case 59:
            valid = count == 4 && spirv_test_ids(bytes, offset, 1, 3, bound) && (c == 1 || c == 12);
            if (valid) pointers[b] = (u8)(c == 1 ? 1 : 2);
            break;
        case 61:
            valid = count == 4 && spirv_test_ids(bytes, offset, 1, 4, bound);
            if (valid && pointers[c] == 1) values[b] = invocation;
            else if (valid && pointers[c] == 3 && values[c] < length && buffer)
            {
                values[b] = buffer[values[c]];
                result.loads += 1;
            }
            else valid = false;
            break;
        case 81:
            valid = count == 5 && d == 0 && spirv_test_ids(bytes, offset, 1, 4, bound);
            if (valid) values[b] = values[c];
            break;
        case 68:
            valid = count == 5 && d == 0 && spirv_test_ids(bytes, offset, 1, 4, bound) && pointers[c] == 2;
            if (valid) values[b] = length;
            break;
        case 65:
            valid = count == 6 && spirv_test_ids(bytes, offset, 1, 6, bound) && pointers[c] == 2 && values[d] == 0;
            if (valid)
            {
                pointers[b] = 3;
                values[b] = values[spirv_test_word(bytes, offset + 5)];
            }
            break;
        case 62:
            valid = count == 3 && spirv_test_ids(bytes, offset, 1, 3, bound) && pointers[a] == 3 && values[a] < length && buffer;
            if (valid) { buffer[values[a]] = values[b]; result.stores += 1; }
            break;
        case 128: case 130: case 132: case 176: case 197: case 198: case 199:
            valid = count == 5 && spirv_test_ids(bytes, offset, 1, 5, bound);
            if (valid)
            {
                if (opcode == 128) values[b] = values[c] + values[d];
                else if (opcode == 130) values[b] = values[c] - values[d];
                else if (opcode == 132) values[b] = values[c] * values[d];
                else if (opcode == 176) values[b] = values[c] < values[d];
                else if (opcode == 197) values[b] = values[c] | values[d];
                else if (opcode == 198) values[b] = values[c] ^ values[d];
                else values[b] = values[c] & values[d];
            }
            break;
        case 250:
            valid = count == 4 && spirv_test_ids(bytes, offset, 1, 4, bound);
            if (valid) { next = labels[values[a] ? b : c]; valid = next >= 5; }
            break;
        case 249:
            valid = count == 2 && spirv_test_ids(bytes, offset, 1, 2, bound);
            if (valid) { next = labels[a]; valid = next >= 5; }
            break;
        case 253: valid = count == 1; returned = valid; break;
        default: valid = false; break;
        }
        offset = next;
    }
    result.success = valid && returned;
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerDriverResult spirv_test_compile(Arena* arena, String8 source, String8 output, String8 frontend)
{
    String8 command[] = {S8("-nostdinc"), S8("-g0"), S8("-target"), S8("spirv-vulkan1.2-compute"), S8("-c"),
                         S8("-fno-canonical-fast"), frontend, S8("-o"), output, source};
    CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
    return compiler_driver_execute_invocation(arena, invocation);
}

BUSTER_GLOBAL_LOCAL UnitTestResult spirv_test_transform(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 output = buster_test_temporary_path(arena, S8("buster-direct-transform"), S8(".spv"));
    String8 forms[] = {S8("-ffrontend-ssa"), S8("-fno-frontend-ssa")};
    for (u32 form = 0; form < BUSTER_ARRAY_LENGTH(forms); form += 1)
    {
        CompilerDriverResult compiled = spirv_test_compile(arena, S8("src/buster/tests/compiler/spirv/fixtures/direct_transform.c"), output, forms[form]);
        if (compiled.error != COMPILER_DRIVER_ERROR_NONE) arguments->show(arguments, S8("direct SPIR-V form={u32}: {S8}\n"), form, compiled.diagnostic);
        if (BUSTER_REQUIRE(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_spirv && compiled.spirv.success))
        {
            BUSTER_TEST(arguments, !compiled.has_gpu && !compiled.has_object && !compiled.has_llvm_bitcode && !compiled.has_wasm && !compiled.has_ebpf);
            ByteSlice bytes = compiled.spirv.bytes;
            ByteSlice written = file_read(arena, output, (FileReadOptions){0});
            BUSTER_TEST(arguments, written.pointer && written.length == bytes.length && memory_compare(written.pointer, bytes.pointer, bytes.length));
            CompilerDriverResult repeated = spirv_test_compile(arena, S8("src/buster/tests/compiler/spirv/fixtures/direct_transform.c"), output, forms[form]);
            BUSTER_TEST(arguments, repeated.error == COMPILER_DRIVER_ERROR_NONE && repeated.spirv.bytes.length == bytes.length &&
                                   memory_compare(repeated.spirv.bytes.pointer, bytes.pointer, bytes.length));
            SpirvTestModule module = spirv_test_inspect(bytes);
            BUSTER_TEST(arguments, module.valid && module.shader && module.memory_model && module.entry && module.local_size);
            BUSTER_TEST(arguments, module.stride && module.block && module.member_offset && module.descriptor_set && module.binding && module.invocation);
            BUSTER_TEST(arguments, module.length && module.condition && module.body && module.merge && module.index);
            BUSTER_TEST(arguments, module.constants == 7 && module.add_count == 1 && module.multiply_count == 1 && module.xor_count == 1);
            BUSTER_TEST(arguments, module.loads == 1 && module.stores == 1);
            u32 input[] = {0, 1, UINT32_MAX, UINT32_C(0x80000000), UINT32_C(0x7fffffff), UINT32_C(0xfffffff0)};
            u32 known[] = {UINT32_C(0xa5a5a5b4), UINT32_C(0xa5a5a5b1), UINT32_C(0xa5a5a5ab), UINT32_C(0x25a5a5b4), UINT32_C(0x25a5a5ab), UINT32_C(0x5a5a5a44)};
            u32 actual[BUSTER_ARRAY_LENGTH(input)];
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(input); index += 1)
            {
                // Independent widening and explicit modulo in the CPU reference.
                u32 reference = (u32)(((u64)input[index] * 3 + 17) & UINT32_MAX) ^ UINT32_C(0xa5a5a5a5);
                BUSTER_TEST(arguments, reference == known[index]);
                actual[index] = input[index];
            }
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(input) + 2; index += 1)
            {
                SpirvTestExecution execution = spirv_test_emulate(bytes, actual, (u32)BUSTER_ARRAY_LENGTH(actual), index);
                BUSTER_TEST(arguments, execution.success && execution.loads == (u32)(index < BUSTER_ARRAY_LENGTH(input)) &&
                                       execution.stores == (u32)(index < BUSTER_ARRAY_LENGTH(input)));
            }
            BUSTER_TEST(arguments, memory_compare(actual, known, sizeof(known)));
            SpirvTestExecution excess = spirv_test_emulate(bytes, actual, (u32)BUSTER_ARRAY_LENGTH(actual), UINT32_MAX);
            BUSTER_TEST(arguments, excess.success && !excess.loads && !excess.stores && memory_compare(actual, known, sizeof(known)));
            SpirvTestExecution empty = spirv_test_emulate(bytes, 0, 0, 0);
            BUSTER_TEST(arguments, empty.success && !empty.loads && !empty.stores);
            if (BUSTER_REQUIRE(arguments, module.valid && module.multiply_offset))
            {
                ByteSlice mutated = {.pointer = arena_allocate(arena, u8, bytes.length), .length = bytes.length};
                memcpy(mutated.pointer, bytes.pointer, bytes.length);
                spirv_test_write_word(mutated, module.multiply_offset, (5u << 16) | 128u);
                u32 wrong[] = {1};
                SpirvTestExecution control = spirv_test_emulate(mutated, wrong, 1, 0);
                BUSTER_TEST(arguments, control.success && control.stores == 1 && wrong[0] != known[1]);
                spirv_test_write_word(mutated, module.multiply_offset, (5u << 16) | 136u); // Unsupported OpFDiv.
                BUSTER_TEST(arguments, !spirv_test_emulate(mutated, wrong, 1, 0).success);
                spirv_test_write_word(mutated, 5, 17u); // Zero word count must fail before interpretation.
                BUSTER_TEST(arguments, !spirv_test_emulate(mutated, wrong, 1, 0).success);
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult spirv_test_bitwise(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 input = buster_test_temporary_path(arena, S8("buster-direct-bitwise"), S8(".c"));
    String8 output = buster_test_temporary_path(arena, S8("buster-direct-bitwise"), S8(".spv"));
    String8 source = S8("void kernel(unsigned *buffer, unsigned index) { buffer[index] = ((buffer[index] - 1u) & 0xffffu) | 0x80000000u; }\n");
    if (BUSTER_REQUIRE(arguments, file_write(input, BUSTER_SLICE_TO_BYTE_SLICE(source))))
    {
        CompilerDriverResult compiled = spirv_test_compile(arena, input, output, S8("-ffrontend-ssa"));
        if (BUSTER_REQUIRE(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_spirv))
        {
            SpirvTestModule module = spirv_test_inspect(compiled.spirv.bytes);
            BUSTER_TEST(arguments, module.valid && module.subtract_count == 1 && module.and_count == 1 && module.or_count == 1);
            u32 actual[] = {0, 1, UINT32_MAX, UINT32_C(0x80000000)};
            u32 expected[] = {UINT32_C(0x8000ffff), UINT32_C(0x80000000), UINT32_C(0x8000fffe), UINT32_C(0x8000ffff)};
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(actual); index += 1)
            {
                SpirvTestExecution execution = spirv_test_emulate(compiled.spirv.bytes, actual, (u32)BUSTER_ARRAY_LENGTH(actual), index);
                BUSTER_TEST(arguments, execution.success && execution.stores == 1);
            }
            BUSTER_TEST(arguments, memory_compare(actual, expected, sizeof(expected)));
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult spirv_test_rejections(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 input = buster_test_temporary_path(arena, S8("buster-direct-rejection"), S8(".c"));
    String8 output = buster_test_temporary_path(arena, S8("buster-direct-rejection"), S8(".spv"));
    String8 cases[] = {
        S8("void kernel(unsigned *b,unsigned i){ b[i+1u]=b[i]; }"),
        S8("void kernel(unsigned *b,unsigned i){ b[0u]=b[i]; }"),
        S8("void kernel(unsigned *b,unsigned i){ *(b+i)=1u; }"),
        S8("void kernel(unsigned *b,unsigned i){ b[i]=b[i]/3u; }"),
        S8("void kernel(unsigned *b,unsigned i){ b[i]=b[i]<<1u; }"),
        S8("void kernel(unsigned *b,unsigned i){ if(i) b[i]=1u; }"),
        S8("void kernel(unsigned *b,unsigned i){ while(i) b[i]=--i; }"),
        S8("unsigned external(unsigned); void kernel(unsigned *b,unsigned i){ b[i]=external(i); }"),
        S8("void barrier(void); void kernel(unsigned *b,unsigned i){ barrier(); b[i]=1u; }"),
        S8("void kernel(unsigned *b,unsigned i){ b[i]=(unsigned)((float)b[i]*1.5f); }"),
        S8("void kernel(int *b,unsigned i){ b[i]=1; }"),
        S8("void kernel(volatile unsigned *b,unsigned i){ b[i]=1u; }"),
        S8("void kernel(_Atomic unsigned *b,unsigned i){ b[i]=1u; }"),
        S8("unsigned global; void kernel(unsigned *b,unsigned i){ b[i]=global; }"),
        S8("void other(unsigned *b,unsigned i){ b[i]=1u; }"),
        S8("unsigned kernel(unsigned *b,unsigned i){ b[i]=1u; return i; }"),
        S8("void kernel(unsigned *b,unsigned i){ (void)b; (void)i; }"),
    };
    ByteSlice sentinel = BUSTER_SLICE_TO_BYTE_SLICE(S8("unchanged-existing-artifact"));
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        if (BUSTER_REQUIRE(arguments, file_write(input, BUSTER_SLICE_TO_BYTE_SLICE(cases[index])) && file_write(output, sentinel)))
        {
            CompilerDriverResult rejected = spirv_test_compile(arena, input, output, S8("-ffrontend-ssa"));
            if (rejected.error == COMPILER_DRIVER_ERROR_NONE) arguments->show(arguments, S8("unexpected direct SPIR-V acceptance case={u32}\n"), index);
            BUSTER_TEST(arguments, rejected.error != COMPILER_DRIVER_ERROR_NONE && rejected.diagnostic.length && !rejected.has_spirv && !rejected.spirv.bytes.length);
            BUSTER_TEST(arguments, !rejected.has_gpu && !rejected.has_object && !rejected.has_llvm_bitcode);
            ByteSlice after = file_read(arena, output, (FileReadOptions){0});
            BUSTER_TEST(arguments, after.pointer && after.length == sentinel.length && memory_compare(after.pointer, sentinel.pointer, sentinel.length));
        }
    }
    SpirvArtifact invalid = spirv_emit(arena, 0, 0);
    BUSTER_TEST(arguments, !invalid.success && invalid.diagnostic.length && !invalid.bytes.length);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult spirv_test_invocation_rejections(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 output = buster_test_temporary_path(arena, S8("buster-direct-options"), S8(".spv"));
    String8 options[] = {S8("-S"), S8("-emit-llvm"), S8("-g"), S8("-fPIC"), S8("-fregister-allocator=fast"),
                         S8("--gpu-clang=unavailable-external-compiler"), S8("--gpu-entry=other"), S8("-fverify-codegen")};
    ByteSlice sentinel = BUSTER_SLICE_TO_BYTE_SLICE(S8("unchanged-existing-artifact"));
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(options); index += 1)
    {
        if (BUSTER_REQUIRE(arguments, file_write(output, sentinel)))
        {
            String8 command[] = {S8("-nostdinc"), S8("-target"), S8("spirv-vulkan1.2-compute"), S8("-c"),
                                 S8("-o"), output, options[index], S8("src/buster/tests/compiler/spirv/fixtures/direct_transform.c")};
            CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
            CompilerDriverResult rejected = compiler_driver_execute_invocation(arena, invocation);
            BUSTER_TEST(arguments, rejected.error != COMPILER_DRIVER_ERROR_NONE && rejected.diagnostic.length && !rejected.has_spirv && !rejected.has_gpu);
            ByteSlice after = file_read(arena, output, (FileReadOptions){0});
            BUSTER_TEST(arguments, after.pointer && after.length == sentinel.length && memory_compare(after.pointer, sentinel.pointer, sentinel.length));
        }
    }
    String8 assembly = buster_test_temporary_path(arena, S8("buster-direct-assembly-suffix"), S8(".S"));
    String8 paths[] = {S8("src/buster/tests/compiler/spirv/fixtures/direct_transform.c"), S8("src/buster/tests/compiler/spirv/fixtures/direct_transform.c")};
    if (BUSTER_REQUIRE(arguments, file_write(assembly, BUSTER_SLICE_TO_BYTE_SLICE(S8("void kernel(unsigned *b,unsigned i){b[i]=1u;}\n")))))
    {
        for (u32 index = 0; index < 7; index += 1)
        {
            if (BUSTER_REQUIRE(arguments, file_write(output, sentinel)))
            {
                String8 command[] = {S8("-nostdinc"), S8("-target"), S8("spirv-vulkan1.2-compute"), S8("-c"),
                                     S8("-x"), S8("c"), S8("-o"), output, index == 6 ? assembly : paths[0]};
                CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
                if (index == 0) invocation.action = COMPILER_DRIVER_ACTION_LINK;
                else if (index == 1) invocation.emit_llvm_bitcode = true;
                else if (index == 2) { invocation.input_paths = paths; invocation.input_count = 2; }
                else if (index == 3) { invocation.input_paths = &assembly; }
                else if (index == 4) invocation.gpu_tools.clang_path = S8("unavailable-external-compiler");
                else if (index == 5) { invocation.input_languages = 0; invocation.input_language_count = 0; invocation.language = COMPILER_DRIVER_LANGUAGE_OPENCL; }
                CompilerDriverResult rejected = compiler_driver_execute_invocation(arena, invocation);
                BUSTER_TEST(arguments, rejected.error != COMPILER_DRIVER_ERROR_NONE && rejected.diagnostic.length && !rejected.has_spirv && !rejected.has_gpu && !rejected.has_object);
                ByteSlice after = file_read(arena, output, (FileReadOptions){0});
                BUSTER_TEST(arguments, after.pointer && after.length == sentinel.length && memory_compare(after.pointer, sentinel.pointer, sentinel.length));
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult spirv_test_invalid_ir(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 command[] = {S8("-nostdinc"), S8("-target"), S8("spirv-vulkan1.2-compute"), S8("-c"), S8("src/buster/tests/compiler/spirv/fixtures/direct_transform.c")};
    CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
    String8 source = S8("void kernel(unsigned *buffer,unsigned index){ buffer[index]=buffer[index]+1u; }\n");
    CPreprocessResult preprocessed = c_preprocess(arena, source, (CPreprocessOptions){.source_path = S8("spirv-invalid-ir.c"),
        .target = invocation.target, .data_layout = target_data_layout(invocation.target)});
    if (BUSTER_REQUIRE(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE && !preprocessed.error_count))
    {
        CParserResult syntax = c_parse_ast(arena, preprocessed);
        if (BUSTER_REQUIRE(arguments, !syntax.diagnostic_count))
        {
            CIRLowerResult lowered = c_analyze_with_options(arena, S8("spirv-invalid-ir.c"), preprocessed, syntax, invocation.target, (CIRLowerOptions){0});
            if (BUSTER_REQUIRE(arguments, lowered.program && !lowered.diagnostic_count && lowered.program->modules[0].function_count == 1))
            {
                IrModule* module = lowered.program->modules;
                SpirvArtifact original = spirv_emit(arena, lowered.program, module);
                if (BUSTER_REQUIRE(arguments, original.success))
                {
                    IrFunction* function = module->functions;
                    bool corrupted = false;
                    for (u32 index = 0; !corrupted && index < function->instruction_count; index += 1)
                    {
                        IrInstruction* row = function->instructions + index;
                        if (row->opcode == IR_OPCODE_BINARY && row->operand_count == 2)
                        {
                            row->operands[0] = IR_VALUE_ID_INVALID;
                            corrupted = true;
                        }
                    }
                    BUSTER_TEST(arguments, corrupted);
                    SpirvArtifact rejected = spirv_emit(arena, lowered.program, module);
                    BUSTER_TEST(arguments, !rejected.success && !rejected.bytes.length && rejected.diagnostic.length);
                    BUSTER_STRING_TEST(arguments, rejected.diagnostic, S8("canonical IR validation failed before SPIR-V emission"));
                }
            }
        }
    }
    return result;
}

UnitTestResult spirv_compute_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, spirv_test_transform);
    BUSTER_TEST_FIXTURE(arguments, spirv_test_bitwise);
    BUSTER_TEST_FIXTURE(arguments, spirv_test_rejections);
    BUSTER_TEST_FIXTURE(arguments, spirv_test_invocation_rejections);
    BUSTER_TEST_FIXTURE(arguments, spirv_test_invalid_ir);
    return result;
}

#endif

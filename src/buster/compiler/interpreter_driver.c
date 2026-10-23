// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Text IR command boundary. Function symbols are their one-based order in the input; native bindings are explicit.
// Source bytes are an exact, bounded regular-file mapping. Parsing and execution own separate arenas, released here.
#include <dlfcn.h>

typedef struct InterpreterDriverFunction InterpreterDriverFunction;
struct InterpreterDriverFunction
{
    IrFunction function;
    InterpreterDriverFunction* next;
};

static bool interpreter_driver_number(const char* text, uint64_t* value)
{
    char* end = 0;
    errno = 0;
    const uint64_t parsed = strtoull(text, &end, 0);
    const bool result = text[0] != 0 && end != text && *end == 0 && errno == 0;
    if (result)
    {
        *value = parsed;
    }
    return result;
}

static int run_ir_interpreter(int argc, char** argv)
{
    int result = 1;
    Arena storage = arena_reserve(UINT64_C(1) << 32);
    Arena scratch = arena_reserve(UINT64_C(1) << 32);
    InterpreterValue* arguments = BUSTER_ARENA_PUSH_ARRAY(&storage, InterpreterValue, (uint32_t)argc);
    InterpreterNativeBinding* natives = BUSTER_ARENA_PUSH_ARRAY(&storage, InterpreterNativeBinding, (uint32_t)argc);
    uint32_t argument_count = 0;
    uint32_t native_count = 0;
    SymbolId entry = 1;
    const char* path = 0;
    bool print = false;
    bool show_result = false;
    bool valid = arguments != 0 && natives != 0;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--print") == 0)
        {
            print = true;
        }
        else if (strcmp(argv[i], "--result") == 0)
        {
            show_result = true;
        }
        else if (strcmp(argv[i], "--entry") == 0 && i + 1 < argc)
        {
            uint64_t number = 0;
            valid = interpreter_driver_number(argv[++i], &number) && number > 0 && number < UINT32_MAX;
            entry = (SymbolId)number;
        }
        else if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc)
        {
            char* end = 0;
            errno = 0;
            const char* specification = argv[++i];
            const unsigned long symbol = strtoul(specification, &end, 0);
            valid = end != specification && *end == '=' && end[1] != 0 && errno == 0 && symbol > 0 && symbol < UINT32_MAX;
            void* address = valid ? dlsym(RTLD_DEFAULT, end + 1) : 0;
            valid = valid && address != 0;
            uint32_t flags = 0;
            if (valid)
            {
                const char* name = end + 1;
                flags = strcmp(name, "malloc") == 0 ? BUSTER_INTERPRETER_NATIVE_ALLOCATE : strcmp(name, "calloc") == 0 ? BUSTER_INTERPRETER_NATIVE_CALLOC : strcmp(name, "realloc") == 0 ? BUSTER_INTERPRETER_NATIVE_REALLOCATE : strcmp(name, "free") == 0 ? BUSTER_INTERPRETER_NATIVE_FREE : 0;
                if (strcmp(name, "memcpy") == 0 || strcmp(name, "memmove") == 0)
                {
                    flags = BUSTER_INTERPRETER_NATIVE_COPY;
                }
                else if (strcmp(name, "memset") == 0)
                {
                    flags = BUSTER_INTERPRETER_NATIVE_SET;
                }
                for (uint32_t n = 0; n < native_count; n += 1)
                {
                    valid = valid && natives[n].symbol != symbol;
                }
                natives[native_count++] = (InterpreterNativeBinding){
                    .symbol = (uint32_t)symbol,
                    .address = address,
                    .flags = flags,
                };
            }
        }
        else if (path == 0)
        {
            path = argv[i];
        }
        else
        {
            uint64_t number = 0;
            valid = interpreter_driver_number(argv[i], &number);
            arguments[argument_count++] = (InterpreterValue){
                .low = number,
                .high = 0,
            };
        }
    }
    valid = valid && path != 0;
    int descriptor = valid ? open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC) : -1;
    struct stat information = { 0 };
    valid = valid && descriptor >= 0 && fstat(descriptor, &information) == 0 && S_ISREG(information.st_mode) && information.st_size > 0;
    const uint64_t length = valid ? (uint64_t)information.st_size : 0;
    void* mapping = valid ? mmap(0, length, PROT_READ, MAP_PRIVATE, descriptor, 0) : MAP_FAILED;
    valid = valid && mapping != MAP_FAILED;
    if (descriptor >= 0)
    {
        close(descriptor);
    }
    InterpreterDriverFunction* first = 0;
    InterpreterDriverFunction* last = 0;
    uint32_t count = 0;
    uint64_t offset = 0;
    while (valid && offset < length)
    {
        InterpreterDriverFunction* node = BUSTER_ARENA_PUSH_ARRAY(&storage, InterpreterDriverFunction, 1);
        if (node == 0 || count == UINT32_MAX - 1)
        {
            valid = false;
        }
        else
        {
            const IrParseResult parsed = ir_parse((String8){ (char*)mapping + offset, length - offset }, &storage, &scratch, &node->function);
            if (parsed.error == IR_PARSE_END)
            {
                offset = length;
            }
            else if (parsed.error != IR_PARSE_OK || parsed.consumed == 0)
            {
                uint64_t line = 1;
                uint64_t column = 1;
                for (uint64_t b = 0; b < offset + parsed.offset && b < length; b += 1)
                {
                    column = ((char*)mapping)[b] == '\n' ? 1 : column + 1;
                    line += ((char*)mapping)[b] == '\n';
                }
                fprintf(stderr, "%s:%" PRIu64 ":%" PRIu64 ": IR parse error %u (verifier %u)\n", path, line, column, (unsigned)parsed.error, (unsigned)parsed.check.error);
                valid = false;
            }
            else
            {
                node->next = 0;
                if (last != 0)
                {
                    last->next = node;
                }
                else
                {
                    first = node;
                }
                last = node;
                count += 1;
                offset += parsed.consumed;
            }
        }
    }
    IrInterpreterFunction* functions = valid ? BUSTER_ARENA_PUSH_ARRAY(&storage, IrInterpreterFunction, count) : 0;
    valid = valid && count != 0 && functions != 0;
    uint32_t index = 0;
    for (InterpreterDriverFunction* node = first; valid && node != 0; node = node->next)
    {
        functions[index] = (IrInterpreterFunction){ .symbol = index + 1, .function = &node->function };
        index += 1;
        for (uint32_t n = 0; n < native_count; n += 1)
        {
            valid = valid && natives[n].symbol != index;
        }
        if (print)
        {
            valid = valid && ir_print(&node->function, &scratch, stdout) == IR_OK;
        }
    }
    if (valid && print)
    {
        result = 0;
    }
    else if (valid)
    {
        const IrInterpreterProgram program = {
            .functions = functions,
            .function_count = count,
            .bindings = 0,
            .binding_count = 0,
            .natives = natives,
            .native_count = native_count,
        };
        const InterpreterOptions options = interpreter_default_options();
        const InterpreterResult execution = ir_interpret(&program, entry, arguments, argument_count, &options, &scratch);
        valid = execution.status == INTERPRETER_OK;
        if (valid)
        {
            if (show_result)
            {
                printf("0x%016" PRIx64 "%016" PRIx64 "\n", execution.value.high, execution.value.low);
            }
            result = show_result ? 0 : (int)(execution.value.low & 255);
        }
        else
        {
            fprintf(stderr, "%s: interpreter error %u in function %u, instruction %u after %" PRIu64 " steps\n", path, (unsigned)execution.status, execution.function, execution.entry, execution.steps);
        }
    }
    if (!valid && first == 0)
    {
        fprintf(stderr, "usage: compiler interpret-ir [--print] [--result] [--entry ID] [--bind ID=HOST_SYMBOL] FILE [U64_BITS ...]\n");
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, length);
    }
    arena_release(&scratch);
    arena_release(&storage);
    return result;
}

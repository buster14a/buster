struct ArgumentSlice
{
    char** pointer;
    unsigned long length;
};

struct ArgumentState
{
    struct ArgumentSlice arguments;
};

static struct ArgumentState argument_state;
static char* parsed_paths[8];

struct ParsedArguments
{
    char** input_paths;
    unsigned int input_count;
};

struct AbiLarge
{
    unsigned long long first;
    unsigned long long second;
    unsigned long long third;
};

struct AbiPair
{
    unsigned long long left;
    unsigned long long right;
};

static unsigned long long indirect_argument_position_sum(unsigned long long prefix, struct AbiLarge large, struct AbiPair pair)
{
    return prefix + large.first + large.second + large.third + pair.left + pair.right;
}

struct Text
{
    char* pointer;
    unsigned long length;
};

struct TextSlice
{
    struct Text* pointer;
    unsigned long length;
};

struct ParameterLike
{
    struct Text name;
    struct
    {
        unsigned long offset;
        unsigned int line;
        unsigned int column;
    } location;
    unsigned int type;
    unsigned int entity;
};

struct FakeTarget
{
    int cpu_arch;
    int cpu_model;
    int os;
    _Bool cpu_features_explicit;
    unsigned char reserved[4];
    unsigned long cpu_features;
};

struct LargeInvocation
{
    struct Text* input_paths;
    struct Text* include_paths;
    struct Text* system_include_paths;
    struct Text* definitions;
    struct Text* undefinitions;
    struct Text* linker_arguments;
    struct Text output_path;
    struct Text sysroot;
    struct Text diagnostic;
    struct FakeTarget target;
    unsigned int input_count;
    unsigned int include_path_count;
    unsigned int system_include_path_count;
    unsigned int definition_count;
    unsigned int undefinition_count;
    unsigned int linker_argument_count;
    int language;
    int action;
    int dialect;
    int error;
    _Bool verbose;
    _Bool no_standard_includes;
    unsigned char reserved[2];
};

static struct Text large_paths[8];
static struct ParameterLike indexed_parameters[3];
static struct Text large_arguments[] = {
    {"-fsyntax-only", 13},
    {"input.c", 7},
};

static int local_text_array(void)
{
    struct Text arguments[] = {
        {"-fsyntax-only", 13},
        {"input.c", 7},
    };
    return arguments[0].pointer[0] == '-' && arguments[0].length == 13 && arguments[1].pointer[0] == 'i' && arguments[1].length == 7;
}

static int indexed_aggregate_copy(void)
{
    struct ParameterLike* parameters = indexed_parameters;
    unsigned int parameter_count = 2;
    unsigned int written_parameter_count = 1;
    parameters[parameter_count++] = (struct ParameterLike){
        .name =
            {
                .pointer = "right",
                .length = 5,
            },
        .location =
            {
                .offset = 34,
                .line = 1,
                .column = 35,
            },
        .type = 7,
        .entity = ~0u,
    };
    parameters[written_parameter_count++] = parameters[parameter_count - 1];
    parameter_count -= 1;
    return parameter_count == 2 && written_parameter_count == 2 && parameters[1].name.pointer[0] == 'r' && parameters[1].name.length == 5 &&
           parameters[1].location.offset == 34 && parameters[1].location.line == 1 && parameters[1].location.column == 35 && parameters[1].type == 7 &&
           parameters[1].entity == ~0u;
}

static int negative_pointer_indices(void)
{
    int values[] = {3, 5, 7};
    unsigned int count = 3;
    int* end = values + count;
    int* last = values + count - 1;
    return end[-1] == 7 && *(end - 2) == 5 && *last == 7;
}

static int large_tail_local(void)
{
    struct LargeInvocation invocation = {
        .input_count = 11,
        .action = 7,
        .dialect = 17,
    };
    invocation.input_count += 1;
    invocation.action = 3;
    return invocation.input_count == 12 && invocation.action == 3 && invocation.dialect == 17;
}

static unsigned long stack_argument_sum(unsigned long a01, unsigned long a02, unsigned long a03, unsigned long a04, unsigned long a05, unsigned long a06,
                                        unsigned long a07, unsigned long a08, unsigned long a09, unsigned long a10, unsigned long a11, unsigned long a12,
                                        unsigned long a13, unsigned long a14, unsigned long a15, unsigned long a16, unsigned long a17, unsigned long a18,
                                        unsigned long a19, unsigned long a20, unsigned long a21, unsigned long a22, unsigned long a23, unsigned long a24)
{
    return a01 + a02 + a03 + a04 + a05 + a06 + a07 + a08 + a09 + a10 + a11 + a12 + a13 + a14 + a15 + a16 + a17 + a18 + a19 + a20 + a21 + a22 + a23 + a24;
}

static int repeated_stack_argument_calls(void)
{
    unsigned long result = 0;
    for (unsigned int iteration = 0; iteration < 4096; iteration += 1)
    {
        result = stack_argument_sum(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24);
        if (result != 300)
        {
            return 0;
        }
    }
    return 1;
}

static _Bool text_equal(struct Text left, char const* right, unsigned long right_length)
{
    if (left.length != right_length)
    {
        return 0;
    }
    for (unsigned long index = 0; index < right_length; index += 1)
    {
        if (left.pointer[index] != right[index])
        {
            return 0;
        }
    }
    return 1;
}

static struct LargeInvocation parse_large_arguments(struct TextSlice arguments)
{
    struct LargeInvocation invocation = {
        .input_paths = large_paths,
        .action = 7,
        .dialect = 17,
    };
    _Bool options_ended = 0;
    for (unsigned long argument_index = 0; argument_index < arguments.length; argument_index += 1)
    {
        struct Text argument = arguments.pointer[argument_index];
        if (options_ended || !argument.length || argument.pointer[0] != '-')
        {
            invocation.input_paths[invocation.input_count++] = argument;
            continue;
        }
        if (text_equal(argument, "--", 2))
        {
            options_ended = 1;
            continue;
        }
        if (text_equal(argument, "-fsyntax-only", 13))
        {
            invocation.action = 3;
            continue;
        }
        invocation.error = 1;
        return invocation;
    }
    return invocation;
}

static struct ParsedArguments parse_arguments(struct ArgumentSlice arguments)
{
    struct ParsedArguments result = {
        .input_paths = parsed_paths,
    };
    for (unsigned long index = 0; index < arguments.length; index += 1)
    {
        char* argument = arguments.pointer[index];
        if (argument[0] != '-')
        {
            result.input_paths[result.input_count++] = argument;
        }
    }
    return result;
}

static void select_arguments(struct ArgumentSlice arguments, unsigned long index)
{
    argument_state.arguments = (struct ArgumentSlice){
        .pointer = arguments.pointer + index + 1,
        .length = arguments.length - index - 1,
    };
}

int main(int argc, char** argv)
{
    unsigned long expected_invocation_size = sizeof(unsigned long) == 4 ? 168 : 176;
    if (sizeof(struct LargeInvocation) != expected_invocation_size)
    {
        return 30;
    }
    if (!large_tail_local())
    {
        return 31;
    }
    if (!local_text_array())
    {
        return 32;
    }
    if (!indexed_aggregate_copy())
    {
        return 33;
    }
    if (!negative_pointer_indices())
    {
        return 34;
    }
    if (!repeated_stack_argument_calls())
    {
        return 35;
    }
    if (indirect_argument_position_sum(13, (struct AbiLarge){2, 3, 5}, (struct AbiPair){7, 11}) != 41)
    {
        return 36;
    }
    struct LargeInvocation large = parse_large_arguments((struct TextSlice){
        .pointer = large_arguments,
        .length = 2,
    });
    struct ArgumentSlice arguments = {
        .pointer = argv,
        .length = (unsigned long)argc,
    };
    select_arguments(arguments, 1);
    struct ArgumentSlice command_arguments = {
        .pointer = argv + 1,
        .length = (unsigned long)argc - 1,
    };
    struct ParsedArguments parsed = parse_arguments(command_arguments);
    if (argument_state.arguments.length != 2 || argument_state.arguments.pointer[0][0] != 'a' || argument_state.arguments.pointer[1][0] != 'b' ||
        parsed.input_count != 3 || parsed.input_paths[2][0] != 'b')
    {
        return 10;
    }
    if (large.action != 3)
    {
        return 23;
    }
    if (large.dialect != 17)
    {
        return 24;
    }
    if (large.error != 0)
    {
        return 25;
    }
    if (large.input_count != 1)
    {
        return 20;
    }
    if (large.input_paths[0].length != 7)
    {
        return 21;
    }
    if (large.input_paths[0].pointer[0] != 'i')
    {
        return 22;
    }
    return 0;
}

static int factor = 14;
static const int divisor = 2;
static int calls;

typedef void* va_list;
typedef char char8;
typedef unsigned char u8;

#define ACCUMULATE_BOOLEAN(result, expression)                                                                                                                 \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        _Bool success_ = (expression);                                                                                                                         \
        result += success_;                                                                                                                                    \
    } while (0)

struct WidePair
{
    unsigned long long left;
    unsigned long long right;
};

struct LargeValue
{
    unsigned long long first;
    unsigned long long second;
    unsigned long long third;
};

struct PackedFlags
{
    unsigned long long enabled : 1;
    unsigned long long mode : 3;
    unsigned long long : 0;
    unsigned long long priority : 4;
};

typedef int Callback(int value);

typedef struct QualifiedForward QualifiedForward;

static int qualified_forward_value(QualifiedForward const* value);

struct QualifiedForward
{
    int value;
};

struct CallbackHolder
{
    union
    {
        struct
        {
            Callback* callback;
            int argument;
        } thread;
    };
};

struct FourFields
{
    int first, second, third, fourth;
};

struct GlobalMemberHolder
{
    int value;
};

struct BigReturn
{
    unsigned long long values[16];
};

_Static_assert(sizeof(struct FourFields) == 16, "comma-separated fields");

static struct PackedFlags packed_flags = {.enabled = 1, .mode = 5, .priority = 9};

static struct GlobalMemberHolder global_member_holder;
static int* global_member_pointer = &global_member_holder.value;

static unsigned long long wide_identity(unsigned long long value)
{
    return value;
}

static unsigned long long pair_sum(struct WidePair value)
{
    return value.left + value.right;
}

static unsigned long long large_sum(struct LargeValue value)
{
    return value.first + value.second + value.third;
}

static unsigned long long flags_sum(struct PackedFlags value)
{
    value.mode += 1;
    return value.enabled + value.mode + value.priority;
}

static int increment(int value)
{
    return value + 1;
}

static Callback* global_callbacks[] = {
    &increment,
};

static _Bool pointer_is_set(int* pointer)
{
    return pointer != 0;
}

static _Bool pointer_compound_boolean_offset(void)
{
    int values[] = {3, 5, 7};
    int* pointer = &values[1];
    _Bool one = 1;
    pointer -= one;
    pointer += one;
    return pointer == &values[1];
}

static int indexed_postfix_value(void)
{
    int offsets[] = {10};
    int counts[] = {4, 6, 8};
    int slots[] = {0, 0};
    int* pointer = &counts[2];
    slots[0] = offsets[0] + counts[0]++;
    slots[1] = ++counts[1] + (*pointer)++;
    return slots[0] == 14 && counts[0] == 5 && slots[1] == 15 && counts[1] == 7 && counts[2] == 9;
}

static _Bool conditional_chain(_Bool first, _Bool second, int* pointer)
{
    return first ? pointer_is_set(pointer) : second ? pointer_is_set(pointer) : pointer_is_set(pointer);
}

static int conditional_integer_promotion(_Bool condition, int* pointer)
{
    return condition ? pointer_is_set(pointer) : pointer != 0;
}

static int invoke_callback(struct CallbackHolder* holder)
{
    return holder->thread.callback(holder->thread.argument);
}

static int matrix_element(int values[][2], int row, int column)
{
    return values[row][column];
}

static struct WidePair pair_make(unsigned long long left, unsigned long long right)
{
    return (struct WidePair){left, right};
}

static struct LargeValue large_make(unsigned long long first, unsigned long long second, unsigned long long third)
{
    return (struct LargeValue){
        first,
        second,
        third,
    };
}

static struct BigReturn big_make(void)
{
    struct BigReturn result = {0};
    result.values[14] = 123;
    return result;
}

static unsigned long long sum_nine(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e,
                                   unsigned long long f, unsigned long long g, unsigned long long h, unsigned long long i)
{
    return a + b + c + d + e + f + g + h + i;
}

static int variadic_sum(int count, ...)
{
    va_list arguments;
    va_list copy;
    int result = 0;
    int index = 0;
    __builtin_va_start(arguments, count);
    __builtin_va_copy(copy, arguments);
    while (index < count)
    {
        result += __builtin_va_arg(copy, int);
        index += 1;
    }
    __builtin_va_end(copy);
    __builtin_va_end(arguments);
    return result;
}

static int answer(void)
{
    static int local_calls;
    int value = (6 * factor) / divisor % 50;
    int extra;
    local_calls += 1;
    calls += local_calls;
    extra = ((3 << 2) >> 1) + (((5 | 2) ^ 3) & 7);
    value = value + extra + -(-1) + (~0 & 1);
    return value;
}

static int incomplete_array_compound_literal(void)
{
    char8* bytes = (char8*)(u8[]){
        0x1f,
        0x20,
        0x03,
        0xd5,
    };
    u8* unsigned_bytes = (u8*)bytes;
    u8* designated = (u8[]){
        [3] = 9,
    };
    return unsigned_bytes[0] + unsigned_bytes[1] + unsigned_bytes[2] + unsigned_bytes[3] + designated[0] + designated[3] + sizeof((u8[]){1, 2, 3});
}

static int local_pointer_target(void)
{
    return 42;
}

static int invoke_local_function_pointer(void)
{
    int (*entry)(void) = local_pointer_target;
    return entry();
}

static int repeated_macro_locals(void)
{
    int result = 0;
    ACCUMULATE_BOOLEAN(result, 1);
    ACCUMULATE_BOOLEAN(result, 2);
    return result;
}

static int sizeof_member_array_bound(void)
{
    struct GlobalMemberHolder local = {0};
    unsigned char bytes[3 * sizeof(local.value)];
    return sizeof(bytes);
}

struct OperationByteSlice
{
    unsigned char* pointer;
    unsigned long long length;
};

struct OperationFlexiblePacket
{
    unsigned short tag;
    unsigned short count;
    unsigned char bytes[];
};

static int sizeof_dereferenced_member(void)
{
    unsigned char bytes[3] = {1, 2, 3};
    struct OperationByteSlice slice = {
        bytes,
        sizeof(bytes),
    };
    int side_effect = 7;
    return sizeof(*slice.pointer) == 1 && slice.length * sizeof(*((slice).pointer)) == 3 && sizeof *slice.pointer == 1 && sizeof slice.pointer[0] == 1 &&
           sizeof &side_effect == sizeof(int*) && sizeof(1 + 2) == sizeof(int) && sizeof(1.0 + side_effect) == sizeof(double) &&
           sizeof(side_effect++) == sizeof(int) && sizeof side_effect++ == sizeof(int) && side_effect == 7;
}

static int flexible_array_layout(void)
{
    return sizeof(struct OperationFlexiblePacket) == 4 && _Alignof(struct OperationFlexiblePacket) == 2 &&
           __builtin_offsetof(struct OperationFlexiblePacket, bytes) == 4;
}

static int qualified_typedef_cast(void)
{
    u8 const* bytes = (u8 const*)"A";
    return bytes[0];
}

static int qualified_forward_value(QualifiedForward const* value)
{
    return value->value;
}

static unsigned long long signed_magnitude(long long value)
{
    unsigned long long result = (unsigned long long)value;
    if (value < 0)
    {
        result = 0 - result;
    }
    return result;
}

static int initialized_integer_arrays(void)
{
    unsigned char bytes[] = {
        'J', 'o', 's', 0xc3, 0xa9, '/', 0xf0, 0x9f, 0x98, 0x80, 0,
    };
    unsigned short words[] = {
        'J', 'o', 's', 0x00e9, '/', 0xd83d, 0xde00, 0,
    };
    return bytes[3] == 0xc3 && bytes[9] == 0x80 && words[3] == 0x00e9 && words[6] == 0xde00;
}

typedef struct OperationUtf8Result
{
    unsigned char buffer[4];
    unsigned int count;
} OperationUtf8Result;

typedef struct OperationUtf8DecodeResult
{
    unsigned int code_point;
    unsigned long long advance;
} OperationUtf8DecodeResult;

static int operation_is_continuation(unsigned char code_unit)
{
    return (code_unit & 0xc0u) == 0x80u;
}

static OperationUtf8DecodeResult operation_utf8_decode_four(unsigned char* string, unsigned long long length)
{
    OperationUtf8DecodeResult result = {
        string[0],
        1,
    };
    unsigned char first = string[0];
    if ((first & 0xf8u) == 0xf0u && 3 < length)
    {
        unsigned char second = string[1];
        unsigned char third = string[2];
        unsigned char fourth = string[3];
        if (operation_is_continuation(second) && operation_is_continuation(third) && operation_is_continuation(fourth))
        {
            unsigned int code_point = ((unsigned int)(first & 0x07u) << 18) | ((unsigned int)(second & 0x3fu) << 12) | ((unsigned int)(third & 0x3fu) << 6) |
                                      (unsigned int)(fourth & 0x3fu);
            if (code_point >= 0x10000u && code_point <= 0x10ffffu)
            {
                result.code_point = code_point;
                result.advance = 4;
            }
        }
    }
    return result;
}

static OperationUtf8DecodeResult operation_utf8_decode(unsigned char* string, unsigned long long length, unsigned long long index)
{
    OperationUtf8DecodeResult result = {
        string[index],
        1,
    };
    unsigned char first = string[index];
    if ((first & 0x80u) == 0)
    {
    }
    else if ((first & 0xe0u) == 0xc0u && index + 1 < length)
    {
        unsigned char second = string[index + 1];
        if (operation_is_continuation(second))
        {
            unsigned int code_point = ((unsigned int)(first & 0x1fu) << 6) | (unsigned int)(second & 0x3fu);
            if (code_point >= 0x80u)
            {
                result.code_point = code_point;
                result.advance = 2;
            }
        }
    }
    else if ((first & 0xf0u) == 0xe0u && index + 2 < length)
    {
        unsigned char second = string[index + 1];
        unsigned char third = string[index + 2];
        if (operation_is_continuation(second) && operation_is_continuation(third))
        {
            unsigned int code_point = ((unsigned int)(first & 0x0fu) << 12) | ((unsigned int)(second & 0x3fu) << 6) | (unsigned int)(third & 0x3fu);
            if (code_point >= 0x800u && !(code_point >= 0xd800u && code_point <= 0xdfffu))
            {
                result.code_point = code_point;
                result.advance = 3;
            }
        }
    }
    else if ((first & 0xf8u) == 0xf0u && index + 3 < length)
    {
        return operation_utf8_decode_four(string + index, length - index);
    }
    return result;
}

static unsigned long long operation_utf16_write(unsigned short* destination, unsigned int code_point)
{
    if (code_point <= 0xffffu)
    {
        destination[0] = (unsigned short)code_point;
        return 1;
    }
    code_point -= 0x10000u;
    destination[0] = (unsigned short)(0xd800u + (code_point >> 10));
    destination[1] = (unsigned short)(0xdc00u + (code_point & 0x3ffu));
    return 2;
}

static OperationUtf8Result operation_utf8_from_code_point(unsigned int code_point)
{
    OperationUtf8Result result = {0};
    if (code_point > 0x10ffffu || (code_point >= 0xd800u && code_point <= 0xdfffu))
    {
        code_point = 0xfffdu;
    }
    if (code_point <= 0x7fu)
    {
        result.buffer[0] = (unsigned char)code_point;
        result.count = 1;
    }
    else if (code_point <= 0x7ffu)
    {
        result.buffer[0] = (unsigned char)(0xc0u | (code_point >> 6));
        result.buffer[1] = (unsigned char)(0x80u | (code_point & 0x3fu));
        result.count = 2;
    }
    else if (code_point <= 0xffffu)
    {
        result.buffer[0] = (unsigned char)(0xe0u | (code_point >> 12));
        result.buffer[1] = (unsigned char)(0x80u | ((code_point >> 6) & 0x3fu));
        result.buffer[2] = (unsigned char)(0x80u | (code_point & 0x3fu));
        result.count = 3;
    }
    else
    {
        result.buffer[0] = (unsigned char)(0xf0u | (code_point >> 18));
        result.buffer[1] = (unsigned char)(0x80u | ((code_point >> 12) & 0x3fu));
        result.buffer[2] = (unsigned char)(0x80u | ((code_point >> 6) & 0x3fu));
        result.buffer[3] = (unsigned char)(0x80u | (code_point & 0x3fu));
        result.count = 4;
    }
    return result;
}

static int unicode_exception_paths(void)
{
    OperationUtf8Result replacement = operation_utf8_from_code_point(0xd83du);
    OperationUtf8Result maximum = operation_utf8_from_code_point(0x10ffffu);
    unsigned char maximum_utf8[] = {
        '[', 0xf4, 0x8f, 0xbf, 0xbf, ']',
    };
    OperationUtf8DecodeResult decoded = operation_utf8_decode(maximum_utf8, sizeof(maximum_utf8), 1);
    unsigned short maximum_utf16[2] = {0};
    unsigned long long written = operation_utf16_write(maximum_utf16, decoded.code_point);
    return replacement.count == 3 && replacement.buffer[0] == 0xef && replacement.buffer[1] == 0xbf && replacement.buffer[2] == 0xbd && maximum.count == 4 &&
           maximum.buffer[0] == 0xf4 && maximum.buffer[1] == 0x8f && maximum.buffer[2] == 0xbf && maximum.buffer[3] == 0xbf &&
           decoded.code_point == 0x10ffffu && decoded.advance == 4 && written == 2 && maximum_utf16[0] == 0xdbff && maximum_utf16[1] == 0xdfff;
}

static int unicode_index_conditions(void)
{
    unsigned char bytes[] = {
        '[', 0xf4, 0x8f, 0xbf, 0xbf, ']',
    };
    unsigned long long index = 1;
    unsigned long long fourth = index + 3;
    unsigned char first = bytes[index];
    return (first == 0xf4) + 2 * ((first & 0xf8u) == 0xf0u) + 4 * (index + 3 < sizeof(bytes)) + 8 * (bytes[index + 1] == 0x8f) +
           16 * (bytes[index + 2] == 0xbf) + 32 * (bytes[index + 3] == 0xbf) + 64 * (bytes[4] == 0xbf) + 128 * (bytes[fourth] == 0xbf);
}

static int narrow_unsigned_conversions(void)
{
    unsigned int maximum = 0xffffffffu;
    unsigned int negative_ten = (unsigned int)-10;
    unsigned long long widened_maximum = (unsigned long long)maximum;
    unsigned long long widened_negative = (unsigned long long)negative_ten;
    return widened_maximum == 4294967295ULL && widened_maximum - 1 == 4294967294ULL && widened_negative == 4294967286ULL;
}

static unsigned long long integer_width_mask(unsigned int width)
{
    return width >= 64 ? 0xffffffffffffffffULL : width ? (((unsigned long long)1 << width) - 1) : 0;
}

typedef struct VariadicSlice
{
    unsigned char* pointer;
    unsigned long long length;
} VariadicSlice;

typedef unsigned long long VariadicSliceCallback(int marker, VariadicSlice slice, ...);

typedef struct VariadicCallbackHolder
{
    VariadicSliceCallback* callback;
} VariadicCallbackHolder;

static unsigned long long variadic_slice_target(int marker, VariadicSlice slice, ...)
{
    va_list arguments;
    unsigned long long first;
    unsigned long long second;
    __builtin_va_start(arguments, slice);
    first = __builtin_va_arg(arguments, unsigned long long);
    second = __builtin_va_arg(arguments, unsigned long long);
    __builtin_va_end(arguments);
    return (unsigned long long)marker + slice.length + first + second;
}

static int indirect_variadic_struct_argument(void)
{
    unsigned char bytes[] = {1, 2, 3};
    VariadicSlice slice = {
        bytes,
        sizeof(bytes),
    };
    VariadicCallbackHolder holder = {
        variadic_slice_target,
    };
    return holder.callback(4, slice, 5ULL, 6ULL) == 18;
}

int main(void)
{
    int value = answer();
    *global_member_pointer = 37;
    int negative = -1;
    int update = 1;
    int postfix = (int)update++;
    int prefix = ++update;
    int written_parameter_count = 0;
    int matrix[2][2];
    matrix[0 + written_parameter_count++][0] = 1;
    matrix[0][1] = 2;
    matrix[1][0] = 3;
    matrix[1][1] = 4;
    struct CallbackHolder holder;
    struct LargeValue made = large_make(7, 11, 13);
    struct BigReturn big = big_make();
    QualifiedForward qualified = {19};
    holder.thread.callback = &increment;
    holder.thread.argument = 8;
    unsigned long long wide = wide_identity((unsigned long long)1 << 40);
    return (value == 54) + (value != 55) + (negative < 0) + (negative <= -1) + (0 > negative) + (-1 >= negative) + (calls == 1) + (!0) + (!!1) + sizeof(int) +
           _Alignof(int) + sizeof(char*) + (long)3 + (int)(long)4 + postfix + prefix + update + written_parameter_count + (1, 5) + __builtin_expect(2, 0) +
           __builtin_expect_with_probability(3, 0, 0.5) + (wide == ((unsigned long long)1 << 40)) + (wide / 1024 == ((unsigned long long)1 << 30)) +
           (wide >> 36 == 16) + ((wide | 3) == (((unsigned long long)1 << 40) | 3)) + (wide > 0xffffffff) + (pair_sum((struct WidePair){5, 7}) == 12) +
           (pair_sum(pair_make(11, 13)) == 24) + (large_sum((struct LargeValue){2, 3, 5}) == 10) + (large_sum(made) == 31) + (big.values[14] == 123) +
           (flags_sum(packed_flags) == 16) + (sum_nine(1, 2, 3, 4, 5, 6, 7, 8, 9) == 45) + (variadic_sum(3, 4, 5, 6) == 15) + (invoke_callback(&holder) == 9) +
           (global_callbacks[0](4) == 5) + pointer_compound_boolean_offset() + indexed_postfix_value() + (matrix_element(matrix, 1, 1) == 4) +
           conditional_chain(0, 1, &value) + conditional_integer_promotion(1, &value) + ((value ? matrix[0][1] : matrix[1][0]) == 2) +
           ((__typeof__(value))5 == 5) + (pair_sum(value ? (struct WidePair){1, 2} : (struct WidePair){3, 4}) == 3) +
           (incomplete_array_compound_literal() == 291) + (invoke_local_function_pointer() == 42) + (global_member_holder.value == 37) +
           (repeated_macro_locals() == 2) + (sizeof_member_array_bound() == 12) + sizeof_dereferenced_member() + flexible_array_layout() +
           (qualified_typedef_cast() == 65) + (qualified_forward_value(&qualified) == 19) +
           (signed_magnitude((-9223372036854775807LL - 1)) == 9223372036854775808ULL) + 2 * (signed_magnitude(-1) == 1) +
           4 * ((unsigned long long)(-9223372036854775807LL - 1) == 9223372036854775808ULL) +
           8 * (0ULL - (unsigned long long)(-9223372036854775807LL - 1) == 9223372036854775808ULL) + 16 * ((-10LL - 1) == -11LL) +
           32 * (9223372036854775807LL == 0x7fffffffffffffffLL) + 64 * (-9223372036854775807LL == (long long)0x8000000000000001ULL) +
           2 * initialized_integer_arrays() + unicode_exception_paths() + unicode_index_conditions() + narrow_unsigned_conversions() +
           (integer_width_mask(32) == 0xffffffffULL) + indirect_variadic_struct_argument() - 470;
}

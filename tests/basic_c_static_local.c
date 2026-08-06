#ifndef BUSTER_C_STATIC_LOCAL_PROBE
#define BUSTER_C_STATIC_LOCAL_PROBE 0
#endif

struct StaticPair
{
    int first;
    int second;
};

struct StaticPositionalBits
{
    unsigned : 3;
    unsigned x : 3;
    unsigned y : 5;
};

struct StaticZeroWidthBits
{
    unsigned : 0;
    unsigned x : 3;
};

struct StaticBraceA
{
    int x[3];
};

struct StaticBraceB
{
    struct StaticBraceA a;
    int z;
};

static struct StaticBraceB static_brace_elision = {
    .a.x[1] = 7,
    .z = 10,
};
static struct StaticBraceB static_brace_array[3] = {
    [2].a.x[1] = 7,
    [2].z = 10,
};
static struct StaticBraceB static_brace_inferred[] = {
    [2].a.x[1] = 7,
    [2].z = 10,
};

struct StaticEntry
{
    const char *name;
    int value;
};

enum StaticKind
{
    STATIC_KIND_FIRST = 1,
    STATIC_KIND_SECOND = 3,
};

typedef int (*StaticCallback)(int);

static int static_callback(int value)
{
    return value + 1;
}

typedef unsigned char StaticChar;
typedef unsigned long StaticSize;
typedef struct StaticString StaticString;
struct StaticString
{
    StaticChar *pointer;
    StaticSize length;
};

#define STATIC_STRING_INITIALIZER(text) {.pointer = (StaticChar *)(text), .length = sizeof(text) - 1}

#define ADD_STATIC_TABLE(sum, first_value, second_value) \
    { \
        static const int table[] = {first_value, second_value}; \
        sum += table[0]; \
    }

static int static_nested_table_sum(void)
{
    static StaticString const names[] = {STATIC_STRING_INITIALIZER("rax"), STATIC_STRING_INITIALIZER("rcx")};
    static const struct
    {
        StaticString const *names;
        unsigned short width;
    } groups[] = {{names, 64}};
    return groups[0].names[1].length + groups[0].width;
}

static int macro_static_table_sum(void)
{
    int result = 0;
    ADD_STATIC_TABLE(result, 8, 9);
    ADD_STATIC_TABLE(result, 10, 11);
    return result;
}

static int enum_static_table_sum(void)
{
    static const struct
    {
        enum StaticKind kind;
        StaticCallback callback;
    } table[2] = {
        [1] = {.kind = STATIC_KIND_SECOND, .callback = static_callback},
        [0] = {.kind = STATIC_KIND_FIRST, .callback = static_callback},
    };
    return table[0].kind + table[1].kind;
}

static const struct StaticEntry *static_entry_table(void)
{
    static const struct StaticEntry table[] = {
        [3] = {"returned", 12},
        [7] = {"table", 13},
    };
    return table;
}

static int static_table_sum(void)
{
    static const int scalar = 7, second_scalar = 8;
    static void *self_pointer = &self_pointer;
    static const char *string_pointer = "ok";
    static const unsigned char *cast_string_pointer = (const unsigned char *)"ok";
    static void *void_string_pointer = "ok";
    static const struct StaticPair table[2] = {{1, 2}, {3, 4}};
    static const struct StaticEntry entries[2] = {{"first", 5}, {"second", 6}};
    static const StaticString register_table[2] = {STATIC_STRING_INITIALIZER("rax"), STATIC_STRING_INITIALIZER("rbx")};
    return scalar + second_scalar - 1 + table[0].first + table[1].second + entries[0].value + entries[1].value +
           (entries[0].name[0] == 'f' ? 0 : 1) + (register_table[0].length == 3 ? 0 : 1) +
           (self_pointer == (void *)&self_pointer ? 0 : 1) + (string_pointer[1] == 'k' ? 0 : 1) +
           (cast_string_pointer[1] == 'k' ? 0 : 1) + (((const char *)void_string_pointer)[1] == 'k' ? 0 : 1);
}

#if BUSTER_C_STATIC_LOCAL_PROBE == 0 || BUSTER_C_STATIC_LOCAL_PROBE == 1
static int static_thread_local_sum(void)
{
    static _Thread_local int c_thread_value = 4;
    static __thread int gnu_thread_value = 6;
    c_thread_value += 1;
    gnu_thread_value += 1;
    return c_thread_value + gnu_thread_value;
}
#endif

#if BUSTER_C_STATIC_LOCAL_PROBE == 0 || BUSTER_C_STATIC_LOCAL_PROBE == 2
static int static_thread_local_zero_sum(void)
{
    static _Thread_local struct StaticPair pair_zero = {0};
    static _Thread_local int scalar_zero = 0;
    static _Thread_local int *pointer_zero = (int *)0;
    static _Thread_local char empty_string[1] = "";
    return pair_zero.first + pair_zero.second + scalar_zero + (pointer_zero != 0) + empty_string[0];
}
#endif

static int static_aligned_address_ok(void)
{
    static _Alignas(64) int aligned_value = 3;
    return ((unsigned long)&aligned_value & 63UL) == 0;
}

static int static_bit_field_sum(void)
{
    static struct StaticPositionalBits positional = {5, 9};
    static struct StaticZeroWidthBits zero_width = {5};
    return positional.x == 5 && positional.y == 9 && zero_width.x == 5;
}

static int static_brace_designator_sum(void)
{
    return static_brace_elision.a.x[1] == 7 && static_brace_elision.z == 10 && static_brace_array[2].a.x[1] == 7 &&
           static_brace_array[2].z == 10 && static_brace_inferred[2].a.x[1] == 7 && static_brace_inferred[2].z == 10;
}

#if BUSTER_C_STATIC_LOCAL_PROBE == 0
static int static_zero_aggregate_sum(void)
{
    static struct StaticPair mutable_zero = {0};
    static _Thread_local struct StaticPair tls_zero = {0};
    static const struct StaticPair const_zero = {0};
    static int mutable_scalar_zero = 0;
    static _Thread_local int tls_scalar_zero = 0;
    static const int const_scalar_zero = 0;
    static int *mutable_pointer_zero = (int *)0;
    static _Thread_local int *tls_pointer_zero = (int *)0;
    static int *const const_pointer_zero = (int *)0;
    static char empty_string[1] = "";
    static _Thread_local char tls_empty_string[1] = "";
    static const char const_empty_string[1] = "";
    static struct StaticPair nonzero = {0, 1};
    return mutable_zero.first + mutable_zero.second + tls_zero.first + tls_zero.second + const_zero.first + const_zero.second + nonzero.second +
           mutable_scalar_zero + tls_scalar_zero + const_scalar_zero + (mutable_pointer_zero != 0) + (tls_pointer_zero != 0) +
           (const_pointer_zero != 0) + empty_string[0] + tls_empty_string[0] + const_empty_string[0];
}
#endif

static int static_non_tls_static_local_sum(void)
{
    return static_table_sum() == 30 && static_nested_table_sum() == 67 && macro_static_table_sum() == 18 && enum_static_table_sum() == 4 &&
                   static_entry_table()[7].value == 13 && static_aligned_address_ok() && static_bit_field_sum() && static_brace_designator_sum()
               ? 0
               : 1;
}

int main(void)
{
#if BUSTER_C_STATIC_LOCAL_PROBE == 1
    return static_thread_local_sum() == 12 && static_thread_local_sum() == 14 ? 0 : 1;
#elif BUSTER_C_STATIC_LOCAL_PROBE == 2
    return static_thread_local_zero_sum() == 0 ? 0 : 1;
#elif BUSTER_C_STATIC_LOCAL_PROBE == 3
    return static_non_tls_static_local_sum();
#else
    return static_table_sum() == 30 && static_nested_table_sum() == 67 && macro_static_table_sum() == 18 && enum_static_table_sum() == 4 &&
                   static_entry_table()[7].value == 13 && static_thread_local_sum() == 12 && static_thread_local_sum() == 14 && static_aligned_address_ok() &&
                   static_bit_field_sum() && static_brace_designator_sum() &&
                   static_zero_aggregate_sum() == 1 && static_thread_local_zero_sum() == 0
               ? 0
               : 1;
#endif
}

struct StaticPair
{
    int first;
    int second;
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
    static const struct StaticPair table[2] = {{1, 2}, {3, 4}};
    static const struct StaticEntry entries[2] = {{"first", 5}, {"second", 6}};
    static const StaticString register_table[2] = {STATIC_STRING_INITIALIZER("rax"), STATIC_STRING_INITIALIZER("rbx")};
    return scalar + second_scalar - 1 + table[0].first + table[1].second + entries[0].value + entries[1].value +
           (entries[0].name[0] == 'f' ? 0 : 1) + (register_table[0].length == 3 ? 0 : 1);
}

static int static_thread_local_sum(void)
{
    static _Thread_local int c_thread_value = 4;
    static __thread int gnu_thread_value = 6;
    c_thread_value += 1;
    gnu_thread_value += 1;
    return c_thread_value + gnu_thread_value;
}

static int static_aligned_address_ok(void)
{
    static _Alignas(64) int aligned_value = 3;
    return ((unsigned long)&aligned_value & 63UL) == 0;
}

int main(void)
{
    return static_table_sum() == 30 && static_nested_table_sum() == 67 && macro_static_table_sum() == 18 && enum_static_table_sum() == 4 &&
                   static_entry_table()[7].value == 13 && static_thread_local_sum() == 12 && static_thread_local_sum() == 14 && static_aligned_address_ok()
               ? 0
               : 1;
}

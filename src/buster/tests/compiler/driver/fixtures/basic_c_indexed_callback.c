// First-party reduction of #3472's HEVC callback-table shape.
// Independent counters belong to separate objects: C does not prescribe
// the order of callee and argument evaluation, only their occurrence.
typedef int (*Unary)(int);
typedef int (*Weighted)(int, int);
static int callee_calls, index_calls[3], argument_calls, callback_calls;
#define CALLBACK(name, tag) static int name(int value) { callback_calls += 1; return tag + value; }
CALLBACK(f000, 0)
CALLBACK(f001, 1)
CALLBACK(f010, 10)
CALLBACK(f011, 11)
CALLBACK(f100, 100)
CALLBACK(f101, 101)
CALLBACK(f110, 110)
CALLBACK(f111, 111)
static int weighted(int value, int weight) { callback_calls += 1; return value * weight; }
struct Dsp { Unary uni[2][2][2]; Weighted uni_w[2][2][2]; };
struct Context { struct Dsp dsp; };
static struct Context context;
struct Slot { Unary callback; };
static struct Slot slots[2][2];
static Unary table[2][2][2];
static struct Context *get_context(void) { callee_calls += 1; return &context; }
static int index0(int value) { index_calls[0] += 1; return value; }
static int index1(int value) { index_calls[1] += 1; return value; }
static int index2(int value) { index_calls[2] += 1; return value; }
static int arg(int value) { argument_calls += 1; return value; }
#define RESET() callee_calls = 0; index_calls[0] = 0; index_calls[1] = 0; index_calls[2] = 0; argument_calls = 0; callback_calls = 0
int indexed_plain(int i, int my, int mx)
{
    RESET();
    int value = table[i][!!my][!!mx](arg(7));
    return value == i * 100 + !!my * 10 + !!mx + 7 && argument_calls == 1 && callback_calls == 1;
}
int indexed_member(int i, int my, int mx)
{
    RESET();
    struct Context *s = &context;
    int value = s->dsp.uni[i][!!my][!!mx](arg(7));
    return value == i * 100 + !!my * 10 + !!mx + 7 && argument_calls == 1 && callback_calls == 1;
}
int indexed_weighted(int i, int my, int mx)
{
    RESET();
    struct Context *s = &context;
    int value = s->dsp.uni_w[i][!!my][!!mx](arg(7), 3);
    return value == 21 && argument_calls == 1 && callback_calls == 1;
}
int indexed_effects(void)
{
    RESET();
    int value = get_context()->dsp.uni[index0(1)][!!index1(-4)][!!index2(2)](arg(9));
    return value == 120 && callee_calls == 1 && index_calls[0] == 1 && index_calls[1] == 1 &&
           index_calls[2] == 1 && argument_calls == 1 && callback_calls == 1;
}
int indexed_slot(void)
{
    RESET();
    int value = slots[index0(1)][!!index1(-2)].callback(arg(8));
    return value == 119 && index_calls[0] == 1 && index_calls[1] == 1 && argument_calls == 1 && callback_calls == 1;
}
int indexed_group(void)
{
    RESET();
    int value = (context.dsp.uni[index0(1)][!!index1(2)][!!index2(0)])(arg(5));
    return value == 115 && index_calls[0] == 1 && index_calls[1] == 1 && index_calls[2] == 1 &&
           argument_calls == 1 && callback_calls == 1;
}
int indexed_lazy(int enabled)
{
    RESET();
    int value = enabled && get_context()->dsp.uni[index0(1)][!!index1(-1)][!!index2(3)](arg(9));
    return value == enabled && callee_calls == enabled && index_calls[0] == enabled &&
           index_calls[1] == enabled && index_calls[2] == enabled && argument_calls == enabled && callback_calls == enabled;
}
int indexed_updates(void)
{
    RESET();
    int i = 0, j = 0, k = 0, value_arg = 7;
    int value = context.dsp.uni[i++][!!j++][!!k++](value_arg++);
    return value == 7 && i == 1 && j == 1 && k == 1 && value_arg == 8 && callback_calls == 1;
}
int main(void)
{
    Unary row[2][2][2] = {{{f000, f001}, {f010, f011}}, {{f100, f101}, {f110, f111}}};
    int failed = 0;
    for (int i = 0; i < 2; i += 1)
    {
        for (int j = 0; j < 2; j += 1)
        {
            for (int k = 0; k < 2; k += 1)
            {
                table[i][j][k] = row[i][j][k];
                context.dsp.uni[i][j][k] = row[i][j][k];
                context.dsp.uni_w[i][j][k] = weighted;
            }
            slots[i][j].callback = row[i][j][1];
        }
    }
    for (int i = 0; i < 2; i += 1)
    {
        for (int j = -2; j <= 2; j += 1)
        {
            for (int k = -2; k <= 2; k += 1)
            {
                failed |= !indexed_plain(i, j, k);
                failed |= !indexed_member(i, j, k);
                failed |= !indexed_weighted(i, j, k);
            }
        }
    }
    failed |= !indexed_effects();
    failed |= !indexed_slot();
    failed |= !indexed_group();
    failed |= !indexed_lazy(0);
    failed |= !indexed_lazy(1);
    failed |= !indexed_updates();
    return failed;
}

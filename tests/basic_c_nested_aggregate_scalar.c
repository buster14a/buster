// Nested compound literals carry designated scalar members through an
// aggregate field.  yyjson's error fixtures use this shape for `.err = {
// .code = CODE }`.
typedef struct pair
{
    int value;
    int padding;
} pair;

typedef struct holder
{
    pair nested;
    int marker;
} holder;

static int check_holder(holder value)
{
    return value.nested.value != 7 || value.nested.padding != 0 || value.marker != 11;
}

int main(void)
{
    return check_holder((holder){
        .nested = {.value = 7},
        .marker = 11,
    });
}

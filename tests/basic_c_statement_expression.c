// GNU statement expressions in the positions the construct exists for: a
// declaration's initializer, where the names a body declares have to stay
// visible to the statements that follow them inside the same body, and where
// a body that branches still has to hand its final expression back.

static int side_effect_count;

static int produce_scalar(int value)
{
    side_effect_count += 1;
    return value;
}

struct StatementPair
{
    int left;
    int right;
};

#define STATEMENT_MAX(a, b)                                                                                                                \
    ({                                                                                                                                     \
        __auto_type _a = (a);                                                                                                              \
        __auto_type _b = (b);                                                                                                              \
        _a > _b ? _a : _b;                                                                                                                 \
    })

int main(void)
{
    int input = 4;

    int initializer = ({
        int local = input + 1;
        local;
    });
    if (initializer != 5)
    {
        return 1;
    }

    // The same body in the arm of a conditional, which is where a macro that
    // expands to one lands as often as not.
    int conditional = input > 100 ? 1 : ({
        int local = input + 2;
        local;
    });
    if (conditional != 6)
    {
        return 2;
    }

    // A body's declaration shadows the enclosing block's name of the same
    // spelling, and leaves it alone.
    int shadowed = 7;
    int inner = ({
        int shadowed = input * 3;
        shadowed;
    });
    if (shadowed != 7 || inner != 12)
    {
        return 3;
    }

    // A body that declares several names, each visible to the next.
    int chained = ({
        int first = input + 1;
        int second = first * 2;
        int third = second + first;
        third;
    });
    if (chained != 15)
    {
        return 4;
    }

    // One body nested in another body's declaration.
    int nested = ({
        int outer_local = ({
            int inner_local = input;
            inner_local + 1;
        });
        outer_local * 2;
    });
    if (nested != 10)
    {
        return 5;
    }

    // Two bodies in one initializer, each with its own scope.
    int combined = ({
        int left = input;
        left;
    }) + ({
        int right = input;
        right * 2;
    });
    if (combined != 12)
    {
        return 6;
    }

    // Two declarators of one declaration, each initialized by a body.
    int first_declarator = ({
        int local = input;
        local;
    }),
        second_declarator = ({
            int local = input + 1;
            local;
        });
    if (first_declarator != 4 || second_declarator != 5)
    {
        return 7;
    }

    // A `for` initializer takes the same declaration path.
    int sum = 0;
    for (int index = ({
             int local = input;
             local;
         });
         index < 8; index += 1)
    {
        sum += index;
    }
    if (sum != 22)
    {
        return 8;
    }

    // Inside a braced initializer, for an array and for a structure.
    int array[2] = {({
                        int local = input;
                        local;
                    }),
                    9};
    if (array[0] != 4 || array[1] != 9)
    {
        return 9;
    }
    struct StatementPair pair = {({
                                     int local = input;
                                     local * 5;
                                 }),
                                 11};
    if (pair.left != 20 || pair.right != 11)
    {
        return 10;
    }

    // Non-integer results.
    long long wide = ({
        long long local = (long long)input * 1000000007ll;
        local;
    });
    if (wide != 4000000028ll)
    {
        return 11;
    }
    double floating = ({
        double local = input * 0.5;
        local + 0.25;
    });
    if (floating != 2.25)
    {
        return 12;
    }
    static int storage[3] = {13, 17, 19};
    int* pointer = ({
        int* local = storage + 1;
        local;
    });
    if (*pointer != 17)
    {
        return 13;
    }

    // A body may declare a type, and a storage class inside it belongs to the
    // body's own declaration rather than to the one being initialized.
    int through_typedef = ({
        typedef int StatementInt;
        StatementInt local = input + 6;
        local;
    });
    if (through_typedef != 10)
    {
        return 14;
    }
    int through_static = ({
        static int local = 21;
        local + input;
    });
    if (through_static != 25)
    {
        return 15;
    }

    // The idiomatic macro shape, whose body declares the names its result
    // expression reads.
    int maximum = STATEMENT_MAX(input, 9);
    if (maximum != 9)
    {
        return 16;
    }

    // A call in a body's initializer runs exactly once.
    int called = ({
        int local = produce_scalar(input);
        local + 1;
    });
    if (called != 5 || side_effect_count != 1)
    {
        return 17;
    }

    // A body that both declares names and branches: the declaration's scope
    // and the body's value come from two different fixes, and their shapes
    // only meet here.
    int with_block = ({
        int local = 0;
        {
            int inner = input;
            local = inner + 3;
        }
        local;
    });
    if (with_block != 7)
    {
        return 19;
    }
    int with_selection = ({
        int local = input;
        if (local > 2)
        {
            local = local * 10;
        }
        local + 1;
    });
    if (with_selection != 41)
    {
        return 20;
    }
    int with_loop = ({
        int total = 0;
        for (int index = 0; index < input; index += 1)
        {
            total += index;
        }
        total;
    });
    if (with_loop != 6)
    {
        return 21;
    }
    int with_while = ({
        int total = 0;
        int remaining = input;
        while (remaining > 0)
        {
            total += remaining;
            remaining -= 1;
        }
        total;
    });
    if (with_while != 10)
    {
        return 22;
    }

    // The statement and assignment positions, which never lost the scope, stay
    // covered beside the ones that did.
    int assigned;
    assigned = ({
        int local = input;
        local + 3;
    });
    if (assigned != 7)
    {
        return 18;
    }
    (void)({
        int local = input;
        local;
    });

    return side_effect_count != 1;
}

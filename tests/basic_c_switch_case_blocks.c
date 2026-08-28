// Case labels that live inside a block of the switch body, which is how
// SQLite's bytecode interpreter is written: `case OP_SorterNext: { ... goto
// next_tail; case OP_Prev: ... case OP_Next: ... next_tail: ... }`.  Every
// label there is a dispatch target of the enclosing switch even though the
// tokens belong to the block the first case opened, and the code between two
// of them is reachable only through the dispatch -- so unreachable-code
// skipping has to stop at them the way it stops at a named label.
union Payload
{
    int as_int;
    unsigned char as_bytes[4];
};

static int interpret(int opcode, int value)
{
    int result = 0;
    switch (opcode)
    {
        // Objects declared before the first label: no statement stream
        // reaches this declaration, but the cases below name it.
        union Payload payload;
        int scratch;

    case 0: {
        result = 100;
        goto tail;

    case 1:
        result = 200;
        goto tail;

    case 2:
        result = 300;

    tail:
        result += value;
        break;
    }
    case 3: {
        payload.as_int = 0;
        payload.as_bytes[1] = (unsigned char)value;
        result = payload.as_int;
        break;
    }
    case 4:
        scratch = value * 3;
        switch (value)
        {
        case 1:
            scratch += 1;
            break;
        case 2: {
            scratch += 2;
            break;
        }
        default:
            scratch += 100;
        }
        result = scratch;
        break;
    default:
        result = -1;
    }
    return result;
}

// The same shape inside a loop, which is what an interpreter dispatch is.
static int run(const int *program, int count)
{
    int total = 0;
    int index;
    for (index = 0; index < count; index += 1)
    {
        switch (program[index])
        {
        case 7: {
            total += 1;
            if (total > 1000) break;

        case 8:
            total += 10;
            break;
        }
        default:
            total += 100;
        }
    }
    return total;
}

int main(void)
{
    static const int program[] = {7, 8, 9, 7};
    if (interpret(0, 7) != 107) return 1;
    if (interpret(1, 7) != 207) return 2;
    if (interpret(2, 7) != 307) return 3;
    if (interpret(3, 2) != 512) return 4;
    if (interpret(4, 1) != 4) return 5;
    if (interpret(4, 2) != 8) return 6;
    if (interpret(4, 5) != 115) return 7;
    if (interpret(9, 1) != -1) return 8;
    if (run(program, 4) != 132) return 9;
    return 0;
}

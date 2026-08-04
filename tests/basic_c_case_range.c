static int evaluation_count;

static int read_value(int value)
{
    evaluation_count += 1;
    return value;
}

static int dispatch(int value)
{
    int result = 0;
    switch (value)
    {
    case -3 ... -1:
        result += 10;
        break;
    case 0 ... 2:
        result += 20;
        /* fall through */
    case 3:
        result += 30;
        break;
    default:
        result = -1;
        break;
    }
    return result;
}

static int unsigned_dispatch(unsigned int value)
{
    switch (value)
    {
    case 0xfffffffeu ... 0xffffffffu:
        return 11;
    default:
        return 12;
    }
}

static int nested_dispatch(int value)
{
    int result = 0;
    switch (value)
    {
    case 1 ... 2:
        switch (value - 1)
        {
        case 0 ... 0:
            result = 21;
            break;
        default:
            result = -21;
            break;
        }
        break;
    default:
        result = -1;
        break;
    }
    return result;
}

static int loop_dispatch(void)
{
    int result = 0;
    for (int value = -1; value < 4; value += 1)
    {
        switch (value)
        {
        case -1 ... 0:
            result += 1;
            break;
        case 1 ... 2:
            if (value == 1)
            {
                continue;
            }
            result += 2;
            break;
        default:
            result += 3;
            break;
        }
        result += 10;
    }
    return result;
}

int main(void)
{
    if (dispatch(-3) != 10 || dispatch(-1) != 10 || dispatch(0) != 50 || dispatch(2) != 50 || dispatch(3) != 30 || dispatch(4) != -1)
    {
        return 1;
    }
    if (unsigned_dispatch(0xfffffffeu) != 11 || unsigned_dispatch(0xffffffffu) != 11 || unsigned_dispatch(0u) != 12)
    {
        return 2;
    }
    if (nested_dispatch(1) != 21 || nested_dispatch(2) != -21 || nested_dispatch(3) != -1)
    {
        return 3;
    }
    if (loop_dispatch() != 47)
    {
        return 4;
    }
    evaluation_count = 0;
    if (dispatch(read_value(1)) != 50 || evaluation_count != 1)
    {
        return 5;
    }
    return 0;
}

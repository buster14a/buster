// Addressing a VLA row must keep its runtime dimensions without loading the
// array. Byte offsets start at the complete object and use scalar sizeof, so
// the expected addresses do not depend on another VLA subscript or row stride.

static int counted_width(int width, int *calls)
{
    *calls += 1;
    return width;
}

static int check_char_rows(int width, char rows[5][width], char *storage)
{
    int result = 0;
    int index = 1;
    char (*selected)[width] = &((rows[index++]));
    if (index != 2 || (char *)selected != storage + width * sizeof(char))
    {
        result = 1;
    }
    if ((char *)&rows[3] != storage + 3 * width * sizeof(char) ||
        (char *)(&((rows[1])) + 1) != storage + 2 * width * sizeof(char) ||
        (char *)(&rows[3] - 2) != storage + width * sizeof(char))
    {
        result = 2;
    }
    if ((char *)(selected + 2) != storage + 3 * width * sizeof(char) ||
        (char *)(2 + selected) != storage + 3 * width * sizeof(char) ||
        (char *)(&rows[4] - 1) != storage + 3 * width * sizeof(char))
    {
        result = 3;
    }
    index = 1;
    char *next = (char *)(&rows[index++] + 1);
    if (index != 2 || next != storage + 2 * width * sizeof(char))
    {
        result = 4;
    }
    for (int row = 0; row < 5; row += 1)
    {
        for (int column = 0; column < width; column += 1)
        {
            if (rows[row][column] != (char)(row * 13 + column))
            {
                result = 5;
            }
        }
    }
    int column = width - 1;
    (&rows[1] + 1)[0][column--] = 91;
    if (column != width - 2 || selected[1][width - 1] != 91)
    {
        result = 6;
    }
    return result;
}

static int check_chars(int width)
{
    int result = 0;
    int calls = 0;
    char rows[5][counted_width(width, &calls)];
    char *storage = (char *)&rows;
    for (int byte = 0; byte < 5 * width; byte += 1)
    {
        storage[byte] = (char)((byte / width) * 13 + byte % width);
    }
    int checked = check_char_rows(width, rows, storage);
    if (calls != 1 || checked != 0)
    {
        result = checked != 0 ? checked : 7;
    }
    // Inspect every byte, including both neighboring rows, independently of
    // the subscript and pointer-to-VLA paths which performed the store.
    for (int byte = 0; byte < 5 * width; byte += 1)
    {
        char expected = byte == 3 * width - 1 ? 91 : (char)((byte / width) * 13 + byte % width);
        if (storage[byte] != expected)
        {
            result = 8;
        }
    }
    return result;
}

static int check_int_rows(int width, int rows[5][width], unsigned char *storage)
{
    int result = 0;
    int captured_width = width;
    int (*selected)[captured_width] = &rows[1];
    captured_width += 2;
    if (sizeof(*selected) != (unsigned long long)width * sizeof(int) ||
        (unsigned char *)(selected + 2) != storage + 3 * width * sizeof(int) ||
        (unsigned char *)(&((rows[1])) + 1) != storage + 2 * width * sizeof(int) ||
        (unsigned char *)(&rows[3] - 2) != storage + width * sizeof(int))
    {
        result = 1;
    }
    int index = 2;
    int (*last)[width] = &((rows[index++]));
    if (index != 3 || (unsigned char *)last != storage + 2 * width * sizeof(int))
    {
        result = 2;
    }
    unsigned int expected = 0;
    for (unsigned int byte = 0; byte < sizeof(int); byte += 1)
    {
        expected = (expected << 8) | 0x5a;
    }
    for (int row = 0; row < 5; row += 1)
    {
        for (int column = 0; column < width; column += 1)
        {
            if ((unsigned int)rows[row][column] != expected)
            {
                result = 6;
            }
        }
    }
    // A zero store has an independently observable byte representation; the
    // surrounding 0x5a bytes catch wrong element widths and wrong row strides.
    int column = width - 1;
    (&rows[1] + 1)[0][column--] = 0;
    if (column != width - 2 || last[0][width - 1] != 0 || selected[1][width - 1] != 0)
    {
        result = 3;
    }
    return result;
}

static int check_ints(int width)
{
    int result = 0;
    int bound = width;
    int rows[5][bound++];
    unsigned char *storage = (unsigned char *)&rows;
    for (unsigned long long byte = 0; byte < sizeof rows; byte += 1)
    {
        storage[byte] = 0x5a;
    }
    int checked = check_int_rows(width, rows, storage);
    if (bound != width + 1 || checked != 0)
    {
        result = checked != 0 ? checked : 4;
    }
    for (unsigned long long byte = 0; byte < sizeof rows; byte += 1)
    {
        unsigned long long start = (unsigned long long)(3 * width - 1) * sizeof(int);
        int expected = byte >= start && byte < start + sizeof(int) ? 0 : 0x5a;
        if (storage[byte] != expected)
        {
            result = 5;
        }
    }
    return result;
}

static int check_cube(int height, int width)
{
    int result = 0;
    int cube[4][height][width];
    unsigned char *storage = (unsigned char *)&cube;
    for (unsigned long long byte = 0; byte < sizeof cube; byte += 1)
    {
        storage[byte] = 0x5a;
    }
    int plane_index = 1;
    int row_index = height - 1;
    int (*plane)[height][width] = &((cube[plane_index++]));
    int (*row)[width] = &((cube[1][row_index--]));
    unsigned long long plane_bytes = (unsigned long long)height * width * sizeof(int);
    unsigned long long row_bytes = (unsigned long long)width * sizeof(int);
    if (plane_index != 2 || row_index != height - 2 ||
        (unsigned char *)plane != storage + plane_bytes ||
        (unsigned char *)row != storage + plane_bytes + (height - 1) * row_bytes)
    {
        result = 1;
    }
    if ((unsigned char *)(&cube[1] + 1) != storage + 2 * plane_bytes ||
        (unsigned char *)(plane + 2) != storage + 3 * plane_bytes ||
        (unsigned char *)(&cube[3] - 2) != storage + plane_bytes ||
        (unsigned char *)(&cube[1][height - 1] - (height - 1)) != storage + plane_bytes)
    {
        result = 2;
    }
    (&cube[1] + 1)[0][height - 1][width - 1] = 0;
    if (plane[1][height - 1][width - 1] != 0 || cube[2][height - 1][width - 1] != 0)
    {
        result = 3;
    }
    for (unsigned long long byte = 0; byte < sizeof cube; byte += 1)
    {
        unsigned long long start = 3 * plane_bytes - sizeof(int);
        int expected = byte >= start && byte < start + sizeof(int) ? 0 : 0x5a;
        if (storage[byte] != expected)
        {
            result = 4;
        }
    }
    return result;
}

static int check_pointer_forms(int width)
{
    int result = 0;
    int rows[5][width];
    unsigned char *storage = (unsigned char *)&rows;
    int (*p)[width] = &rows[1];
    if ((unsigned char *)(&p + 1) - (unsigned char *)&p != sizeof p ||
        (unsigned char *)&rows[5] != storage + 5 * width * sizeof(int))
    {
        result = 1;
    }
    if (p - &rows[0] != 1 || &rows[0] - p != -1)
    {
        result = 2;
    }
    p++;
    p += 1;
    if (p - &rows[0] != 3)
    {
        result = 3;
    }
    p -= 2;
    (*p)[width - 1] = 73;
    if (rows[1][width - 1] != 73 || (unsigned char *)p != storage + width * sizeof(int))
    {
        result = 4;
    }
    for (int choose = 0; choose < 2; choose += 1)
    {
        if ((unsigned char *)((choose ? &rows[0] : &rows[1]) + 1) != storage + (choose ? 1 : 2) * width * sizeof(int) ||
            (unsigned char *)((choose ? &rows[1] : &rows[0]) - 0) != storage + choose * width * sizeof(int))
        {
            result = 5;
        }
    }
    if ((unsigned char *)(width += 0, &rows[1]) + 1 != storage + width * sizeof(int) + 1)
    {
        result = 6;
    }
    return result;
}

int main(void)
{
    int result = 0;
    for (int width = 1; width <= 7; width += 2)
    {
        int chars = check_chars(width);
        int ints = check_ints(width);
        int cube = check_cube(3, width);
        int pointers = check_pointer_forms(width);
        if (chars != 0)
        {
            result = chars;
        }
        else if (ints != 0)
        {
            result = 10 + ints;
        }
        else if (cube != 0)
        {
            result = 20 + cube;
        }
        else if (pointers != 0)
        {
            result = 30 + pointers;
        }
    }
    return result;
}

int buster_pic_global;
int buster_pic_values[2] = {11, 23};

int* buster_pic_address(void)
{
    return &buster_pic_global;
}

int buster_pic_read(void)
{
    return buster_pic_global;
}

// A runtime index retains a signed absolute displacement in non-PIC x86-64
// objects from both GCC and Clang, rather than relying on pointer truncation.
int buster_pic_index(int index)
{
    return buster_pic_values[index];
}

int main(void)
{
    buster_pic_global = 7;
    return buster_pic_address() != &buster_pic_global || buster_pic_read() != 7 ||
           buster_pic_index(0) != 11 || buster_pic_index(1) != 23;
}

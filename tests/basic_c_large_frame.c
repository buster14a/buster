static int large_frame(void)
{
    unsigned char bytes[40000];
    bytes[0] = 13;
    bytes[39999] = 17;
    return bytes[0] + bytes[39999];
}

int main(void)
{
    return large_frame() == 30 ? 0 : 1;
}

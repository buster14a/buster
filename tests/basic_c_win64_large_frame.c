#ifndef BUSTER_FRAME_BYTES
#define BUSTER_FRAME_BYTES 131088
#endif

struct Argument
{
    long long first;
    long long second;
};

static long long large_frame(long long first, double second, struct Argument third,
                            long long fourth, long long fifth)
{
    volatile unsigned char bytes[BUSTER_FRAME_BYTES];
    bytes[0] = 13;
    bytes[4095] = 17;
    bytes[4096] = 19;
    bytes[BUSTER_FRAME_BYTES - 1] = 23;
    return first + (long long)second + third.first + third.second + fourth + fifth +
           bytes[0] + bytes[4095] + bytes[4096] + bytes[BUSTER_FRAME_BYTES - 1];
}

int main(void)
{
    struct Argument argument = {5, 7};
    return large_frame(2, 3.0, argument, 11, 29) == 129 ? 0 : 1;
}

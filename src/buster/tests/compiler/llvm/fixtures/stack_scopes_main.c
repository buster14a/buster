// Compiled by Clang, independently of the Buster-produced bitcode.
int scoped_vlas(int n, int repetitions);
int scoped_exits(int n, int mode);

int observe_bytes(volatile unsigned char* bytes, int count, int first, int last)
{
    return bytes[0] == first && bytes[count - 1] == last ? 1 : -10000;
}

int main(void)
{
    int failures = 0;
    failures += scoped_vlas(3, 5) != 71 + 83 + 5 + 2;
    // 1024 x 16 KiB exceeds an ordinary thread stack if no loop restore runs.
    failures += scoped_vlas(16384, 1024) != 71 + 83 + 1024 + 342;
    failures += scoped_exits(23, 0) != 42 + 4;
    failures += scoped_exits(23, 1) != 42 + 3;
    failures += scoped_exits(23, 2) != 42 + 1;
    failures += scoped_exits(23, 3) != 42 + 2;
    failures += scoped_exits(23, 4) != 42;
    return failures;
}

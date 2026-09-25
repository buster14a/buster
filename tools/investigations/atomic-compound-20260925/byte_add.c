static _Atomic(unsigned char) value = 255;
int main(void) {
    int observed = (value += 1);
    return (observed != (0)) | ((value != (0)) << 1);
}

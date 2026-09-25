static _Atomic(float) value = 1.0f;
int main(void) {
    double observed = (value -= 0x1.000001p0);
    return (observed != (-0x1p-24)) | ((value != (-0x1p-24f)) << 1);
}

int printf(const char *, ...);
static _Atomic(unsigned char) byte = 255;
static _Atomic(float) floating = 1.0f;
static _Atomic(int) integer = -2;
int main(void) {
    int byte_result = (byte += 1);
    double floating_result = (floating -= 0x1.000001p0);
    int integer_result = (integer += 1.5);
    printf("byte=%u result=%d floating=%a result=%a integer=%d result=%d\n", (unsigned)byte, byte_result, (double)floating, floating_result, (int)integer, integer_result);
    return 0;
}

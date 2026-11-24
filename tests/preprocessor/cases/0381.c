#pragma GCC poison bad
int a[] = {
#embed <dir//bytes.bin> limit(1) prefix(bad,)
};
int b[] = {
#embed <dir/bytes.bin>> limit(1) > bad
};
int c[] = {
#embed <dir/bytes.bin> limit(1) prefix(bad,)
};
x

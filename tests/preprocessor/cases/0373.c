int a[] = {
#embed <dir//bytes.bin> limit(0) if_empty(42)
};
int b[] = {
#embed <dir//bytes.bin> limit(1)
};
int c[] = {
#embed <dir//bytes.bin> clang::offset(1)
};
int d[] = {
#embed <dir//bytes.bin> /* c */ limit(1) /* d */
};
int e[] = {
#embed <dir/\
/bytes.bin> limit(1)
};
x

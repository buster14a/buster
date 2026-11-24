int a[] = {
#embed <dir//bytes.bin> /* comment
*/
};
int b[] = {
#embed <dir//bytes.bin> /* c
*/ limit(1)
};
int c[] = {
#embed <dir//bytes.bin> /* c */ /* d
*/ limit(1) /* e
*/
};
after

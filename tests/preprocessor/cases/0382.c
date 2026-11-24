#pragma GCC poison bad
int a[] = {
#embed <dir/*x.bin> limit(1) /*/ > bad */
};
int b[] = {
#embed <dir//bytes.bin> limit(1) /* bad
*/
};
x

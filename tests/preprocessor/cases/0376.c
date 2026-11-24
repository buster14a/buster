int a[] = {
#embed <dir//bytes.bin> limit(1) /* comment
*/ suffix(,42)
};
int b[] = {
#embed <dir//bytes.bin> prefix(1 /* c
 */ ,) limit(1)
};
int c[] = {
#embed <dir//bytes.bin> limit(1) /* c
#define X 1
*/
};
X
#define STR(x) #x
int d[] = {
#embed <dir//bytes.bin> prefix(STR(a\
+b),) limit(1)
};
end

int a[] = {
#embed "hi.txt" limit(1)
/* comment *\
/

};
int b[] = {
#embed "hi.txt" limit(1)
/\
* comment */

};
int c[] = {
#embed "hi.txt" limit(1)
/\
* comment
*/ };
#line 100 /* x *\
/
#embed "hi.txt" limit(1)
int z;

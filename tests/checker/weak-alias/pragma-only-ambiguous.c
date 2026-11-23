#pragma weak item = object
#pragma weak item = object
int object = 1;
int function(void) { return 2; }
#pragma weak item = function
int use(void) { return item(); }

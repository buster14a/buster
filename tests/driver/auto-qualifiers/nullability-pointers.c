int object;
int* pointer;
int array[2];
int function(void);

__auto_type _Nullable address = &object;
__auto_type _Nonnull elements = array;
__auto_type _Null_unspecified call = function;
__auto_type _Nullable_result text = "example";
__auto_type * _Nullable suffix = &object;
__auto_type _Nullable *outer = &pointer;
__auto_type (* _Nullable declarator)(void) = function;

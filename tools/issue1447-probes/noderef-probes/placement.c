#define ND __attribute__((noderef))
ND int *before_base;
int ND *before_star;
int *ND after_star;
int *after_name ND;
ND int scalar;
ND int array[3];
int array_suffix[3] ND;
int *ND *outer_pointer;
ND int **inner_pointer;
ND int *both_first, *both_second;
ND int (*array_pointer)[3];
int ND (*function_pointer)(void);
int *ND (*pointer_function)(void);
int ND *returns_pointer(void);
int *returns_plain_pointer(void) ND;
typedef int ND *TypedPointer;
typedef int *TrailingPointer ND;
typedef int ND ScalarType;
typedef TypedPointer NestedAlias;
TypedPointer typed;
NestedAlias aliased;
TrailingPointer trailing;
ScalarType *scalar_alias;
void parameter_array(int ND p[3], int *ND q, int r[3] ND);
void use_placement(void)
{
    (void)*before_base;
    (void)*before_star;
    (void)*after_star;
    (void)*after_name;
    (void)array[0];
    (void)array_suffix[0];
    (void)*outer_pointer;
    (void)**outer_pointer;
    (void)*inner_pointer;
    (void)**inner_pointer;
    (void)*both_first;
    (void)*both_second;
    (void)(*array_pointer)[0];
    (void)*returns_pointer();
    (void)*returns_plain_pointer();
    (void)*typed;
    (void)*aliased;
    (void)*trailing;
    (void)*scalar_alias;
}

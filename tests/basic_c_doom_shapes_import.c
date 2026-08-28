// The other half of tests/basic_c_doom_shapes.c: these objects are defined
// here so that the fixture's static initializer references them as imports.
// A file-scope initializer is the only place their names appear over there,
// which is exactly the shape that used to relocate nothing.

int doom_shape_import_a = 11;
int doom_shape_import_b = 22;
int doom_shape_import_c = 33;

int doom_shape_helper(int value)
{
    return value + 1;
}

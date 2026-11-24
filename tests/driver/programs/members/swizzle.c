typedef float v4 __attribute__((ext_vector_type(4)));
float f(v4* p) { return (*p).y; }

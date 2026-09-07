typedef float FloatVector __attribute__((vector_size(16)));
struct VectorWrapper { FloatVector value; };
struct NestedVectorWrapper { struct VectorWrapper value[1]; };
union VectorFirst { FloatVector vector; double first; };
union VectorHalves { FloatVector vector; double halves[2]; };

struct VectorWrapper llvm_vector_wrapper(struct VectorWrapper value)
{
    return value;
}
struct NestedVectorWrapper llvm_nested_vector(struct NestedVectorWrapper value)
{
    return value;
}
union VectorFirst llvm_vector_first(union VectorFirst value)
{
    return value;
}
union VectorHalves llvm_vector_halves(union VectorHalves value)
{
    return value;
}

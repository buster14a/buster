typedef float FloatVector __attribute__((vector_size(16)));
struct VectorWrapper { FloatVector value; };
struct NestedVectorWrapper { struct VectorWrapper value[1]; };
union VectorFirst { FloatVector vector; double first; };
union VectorHalves { FloatVector vector; double halves[2]; };
extern struct VectorWrapper llvm_vector_wrapper(struct VectorWrapper);
extern struct NestedVectorWrapper llvm_nested_vector(struct NestedVectorWrapper);
extern union VectorFirst llvm_vector_first(union VectorFirst);
extern union VectorHalves llvm_vector_halves(union VectorHalves);
int main(void)
{
    struct VectorWrapper input = {{1, 2, 3, 4}};
    struct VectorWrapper (*volatile call)(struct VectorWrapper) = llvm_vector_wrapper;
    struct VectorWrapper direct = llvm_vector_wrapper(input);
    struct VectorWrapper indirect = call(input);
    struct NestedVectorWrapper nested_input = {{input}};
    struct NestedVectorWrapper nested = llvm_nested_vector(nested_input);
    union VectorFirst first_input = {.vector = {5, 6, 7, 8}};
    union VectorFirst first = llvm_vector_first(first_input);
    union VectorHalves halves_input = {.halves = {9, 10}};
    union VectorHalves halves = llvm_vector_halves(halves_input);
    return direct.value[0] != 1 || direct.value[3] != 4 || indirect.value[1] != 2 || indirect.value[2] != 3 ||
           nested.value[0].value[0] != 1 || nested.value[0].value[3] != 4 ||
           first.vector[0] != 5 || first.vector[3] != 8 || halves.halves[0] != 9 || halves.halves[1] != 10;
}

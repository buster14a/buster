enum E { A __attribute__((ext_vector_type(4)))=1 }; _Static_assert(_Generic(A,int:1,default:0),"scalar type");

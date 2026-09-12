// Pointer boundaries let an independently compiled scalar observer inspect
// every lane, including the first and last bytes of wide vector results.
typedef signed char ArithmeticBytes __attribute__((vector_size(8)));
typedef short ArithmeticShorts __attribute__((vector_size(16)));
typedef unsigned int ArithmeticWords __attribute__((vector_size(32)));
typedef unsigned long long ArithmeticLongs __attribute__((vector_size(64)));
typedef int ArithmeticWordMasks __attribute__((vector_size(32)));
typedef double ArithmeticDoubles __attribute__((vector_size(64)));

void arithmetic_bytes(void* output, void const* left_data, void const* right_data)
{
    ArithmeticBytes const* left = left_data;
    ArithmeticBytes const* right = right_data;
    *(ArithmeticBytes*)output = ((*left / *right) + (*left % *right)) ^ ((*left >> 1) + (*left < *right));
}

void arithmetic_shorts(void* output, void const* left_data, void const* right_data)
{
    ArithmeticShorts const* left = left_data;
    ArithmeticShorts const* right = right_data;
    *(ArithmeticShorts*)output = (-*left / *right) ^ (~*left + (*left >= *right));
}

void arithmetic_words(void* output, void const* left_data, void const* right_data)
{
    ArithmeticWords const* left = left_data;
    ArithmeticWords const* right = right_data;
    *(ArithmeticWords*)output = ((*left / *right) + (*left % *right)) ^ (*left << 2);
}

void arithmetic_longs(void* output, void const* left_data, void const* right_data)
{
    ArithmeticLongs const* left = left_data;
    ArithmeticLongs const* right = right_data;
    *(ArithmeticLongs*)output = ((*left * *right) ^ (*left / *right)) + (*left % *right);
}

void arithmetic_doubles(void* output, void const* left_data, void const* right_data)
{
    ArithmeticDoubles const* left = left_data;
    ArithmeticDoubles const* right = right_data;
    *(ArithmeticDoubles*)output = -((*left + *right) * *right - *left / *right);
}

void arithmetic_word_masks(void* output, void const* left_data, void const* right_data)
{
    ArithmeticWords const* left = left_data;
    ArithmeticWords const* right = right_data;
    *(ArithmeticWordMasks*)output = *left > *right;
}

#include "basic_c_packed_layout_shapes.h"

// Strict native-IR caller for the selected ABI boundary. The ordinary packed
// caller retains the complete, independent layout and qualifier corpus.
int main(void)
{
    struct packed_bit_padded_record record = {-8.5f};
    struct packed_bit_padded_record neighbors = packed_layout_bit_padded_neighbors(19, record, 23, 3.25f, 5.5f);
    struct packed_bit_padded_record guarded = packed_layout_bit_padded_register_result();
    struct packed_bit_padded_record made = packed_layout_make_bit_padded(6.25f);
    int result = packed_layout_bit_padded_lead(record) != -8.5f || neighbors.lead != 42.25f ||
                 guarded.lead != 45.25f || made.lead != 6.25f;
    return result;
}

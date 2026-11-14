_Static_assert(__builtin_eh_return_data_regno(0) == 0 && __builtin_eh_return_data_regno(1) == 1, "");
_Static_assert(__builtin_eh_return_data_regno(2) == -1 && __builtin_eh_return_data_regno(-1) == -1, "");
_Static_assert(__builtin_eh_return_data_regno(1L << 32) == 0 && __builtin_eh_return_data_regno('\1') == 1, "");
int r = __builtin_eh_return_data_regno(sizeof(char));

// The completing definition for the import half of
// basic_c_extern_incomplete_array.c: that unit declares
// `extern char external_pad[];` and never completes it, so the incomplete
// extern array must lower as an import resolved against this object.

char external_pad[4] = {60, 61, 62, 63};

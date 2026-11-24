#define RAW_QUERY __has_include
#if RAW_QUERY(<R"(x>) && RAW_QUERY(<R"(x>)
two
#endif
after

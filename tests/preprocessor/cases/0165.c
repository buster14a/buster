#define RAW_QUERY __has_include
#define RAW_BEFORE 11
#if RAW_QUERY(<R"(x>)
hit
#endif
#define RAW_AFTER 22
RAW_BEFORE RAW_AFTER

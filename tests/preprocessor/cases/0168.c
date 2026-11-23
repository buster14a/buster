#define RAW_QUERY __has_embed
#if RAW_QUERY(<R"(x>) == 1
resource
#endif
after

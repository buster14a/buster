// Invalid expression subscripts inside a type-name bound remain checked.
#ifndef BUSTER_TAG_ARRAY_TYPE_NAME_FIXTURE
#define BUSTER_TAG_ARRAY_TYPE_NAME_FIXTURE(...) __VA_ARGS__
#endif
BUSTER_TAG_ARRAY_TYPE_NAME_FIXTURE(
struct item { int value; };
int item(unsigned count) { return sizeof(struct item[item[count]]); }
)

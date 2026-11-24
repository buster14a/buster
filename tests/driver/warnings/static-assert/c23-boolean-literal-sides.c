enum { K = 2 };
static_assert(true == K);
static_assert(K == false + 3);
static_assert(false == (K + 1));
static_assert((_Bool)K == false);
static_assert(false != 0);

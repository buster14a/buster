union U { int x; float f; };
const union U u = {};
enum { E = *(const int*)&u };

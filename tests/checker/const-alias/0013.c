union U { int x; float f; };
const union U u = {};
enum { E = (int)*(const float*)&u.x };

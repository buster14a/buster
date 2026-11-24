union U{int x;float f;};const union U u={.f=1.0f};
enum { E=u.x };

struct S { int x:3; }; struct S s; enum { A=__alignof__(s.x) };

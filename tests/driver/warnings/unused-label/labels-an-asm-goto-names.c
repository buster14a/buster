void f(void) { asm goto("" :::: l2, l4); l2: ; l4: ; l5: ; }
void g(void) { { __label__ l; asm goto("" :::: l); l: ; } }
void h(void) { { __label__ m; m: ; } asm goto("" :::: m); m: ; }
void q(void) { { __label__ z; z: ; } { __label__ z; asm goto("" :::: z); z: ; } }

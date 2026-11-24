#define X 0xe2
signed char take(signed char x) { return x; }
signed char small(void) { return (0xff); }
signed char large(void) { return 0x100; }
signed char single=0xff, overflow=0x1ff;
signed char values[8]; short s;
void f(void) { values[0]=0xe2; values[1]=(0342); values[2]=0b11100010; values[3]=X; s=0xffff; take(0x80); }
void hex_loss(void) { values[4]=0x100; }
void dec_loss(void) { values[5]=226; }
void calc_loss(void) { values[6]=0xe2+0; }
void unary_loss(void) { values[7]=+0xe2; }
void short_loss(void) { s=65535; }
void call_loss(void) { take(0x180); }

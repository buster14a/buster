typedef _BitInt(5) S5;typedef _BitInt(13) S13;typedef _BitInt(40) S40;
#define U5 unsigned _BitInt(5)
#define U13 unsigned _BitInt(13)
#define U27 unsigned _BitInt(27)
#define U40 unsigned _BitInt(40)
static S5 table[3]={-3,7,-16};U5 u5(int n){return (U5)n;}S5 s5(int n){return (S5)(n%16);}struct F{S5 x:3;U13 y:9;S40 z:20;};int tested(int n){U5 a=u5(n*3);S5 p=s5(n);int sw=0;switch(a){case 3:sw=1;break;case 30:sw=2;break;case 9 ... 12:sw=4;break;default:sw=3;}switch(p){case -1:sw+=10;break;case -16 ... -10:sw+=20;break;default:sw+=30;}struct F fs={0};fs.x=(S5)(n&3);fs.y=(U13)(n*37);fs.z=(S40)(n-7)*1000;fs.y+=300;fs.x-=1;S13 dst=0;int o=__builtin_mul_overflow(n*1000,7,&dst);U5 small=0;int o2=__builtin_add_overflow(n,20,&small);S5 kk=(S5)-3;S5 z=(S5)(kk+(S5)1);return sw+(int)fs.x*100+(int)fs.y*1000+(int)(fs.z%1000)*7+o*13+o2*17+(int)dst*3+(int)small*19+(int)table[n&1]*23+(int)z*29;}
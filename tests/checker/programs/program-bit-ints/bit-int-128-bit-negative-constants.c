typedef _BitInt(5) S5;typedef _BitInt(13) S13;typedef _BitInt(40) S40;
#define U5 unsigned _BitInt(5)
#define U13 unsigned _BitInt(13)
#define U27 unsigned _BitInt(27)
#define U40 unsigned _BitInt(40)
static S5 table[3]={-3,7,-16};U5 u5(int n){return (U5)n;}S5 s5(int n){return (S5)(n%16);}int tested(int n){__int128 y=(_BitInt(100))-5;unsigned __int128 u=(unsigned _BitInt(100))-5;__int128 w=(_BitInt(127))-1;__int128 v=(_BitInt(65))-0x123456789abcdefLL;int c=((_BitInt(100))-5<n)+((_BitInt(72))-9>(_BitInt(72))n)*2;return (int)((y+n)%1000003)+(int)((u>>64)%1009)*3+(w==-1)*7+(int)(((v>>32)*n)%1009)*11+c*13;}
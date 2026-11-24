void f(short);short g(void){if(0)return 65536;if(0){short a=65536;(void)a;}return 0;}void h(void){if(0)f(65536);f(65537);}int i(void){return 0&&(f(65536),1);}int j(void){return 1?0:(f(65536),1);}

struct S{union{int x;};int y;};int main(void){struct S s;s.x=1;return s.x;}

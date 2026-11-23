int f(int n) { int x=0; for(int i=0;i<n;i++) { if(i) continue; x+=i; if(x) break; } return x; }

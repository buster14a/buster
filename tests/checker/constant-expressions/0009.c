enum { A=__builtin_choose_expr(1,-(2+3),0), B=_Generic(0,int:2*3+1,default:0), C=__builtin_choose_expr(0,0,_Generic(0,int:(2,7),default:0)) }; _Static_assert(A==-5 && B==7 && C==7,"selection roots");

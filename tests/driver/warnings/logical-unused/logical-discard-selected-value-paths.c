int work(int);void f(int a){a&&_Generic(a,int:work(a),default:0);a&&__builtin_choose_expr(1,work(a),0);}

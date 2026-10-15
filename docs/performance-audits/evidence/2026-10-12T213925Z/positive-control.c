int f(int x){ int y = ({ int t = x + 1; if (t) t += 2; t; }); return ({ int u = y; while (u > 3) u--; u; }) + ({ ({ y; }); x; }); }

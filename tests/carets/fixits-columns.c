// Fix-it columns follow displayed tabs, wide characters and combining marks (#2376).
int column_fixes(int x, int y)
{
	if (/* 界 é */ x = y) x += 1;
	x /* 界 é */ == y;
	return x;
}

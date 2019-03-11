#include <stdio.h>
#include <stdlib.h>

void bar(int m, int n)
{
	int tmp;
	tmp = m;
	m = n;
	n = tmp;
}

void foo(int x, int y)
{
	int a, b;
	bar(x, y);
	bar(y, x);
	bar(x,y);
}

int main()
{
	int a = 1;
	int b = 2;
	printf("Before:\na = %d\tb = %d\n", a, b);
	printf("swapping three times\n");
	foo(a , b);
	printf("After:\na = %d\tb = %d\n", a, b);
}

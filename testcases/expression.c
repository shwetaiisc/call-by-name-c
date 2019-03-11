#include <stdio.h>
#include <stdlib.h>

void simple_exp(int a, int b)
{
	int tmp;
	b = 10;
	tmp = a + b;
	printf("tmp = %d\n", tmp);
}

int main()
{
	int a = 1, b = 2;
	simple_exp(a + b * 10, b);
	return 0;
}

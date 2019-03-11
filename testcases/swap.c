#include <stdio.h>
#include <stdlib.h>

void swap(int a, int b)
{
	int tmp;
	tmp = a;
	a = b;
	b = tmp;
}

int main()
{
	int a = 1, b= 2;
	printf("Before:\na = %d\nb = %d\n", a, b);
	swap(a,b);
	printf("After:\na = %d\nb = %d\n", a, b);
	return 0;
}

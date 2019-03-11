#include <stdio.h>
#include <stdlib.h>

void increment(int *x)
{
	int tmp;
       	tmp = *x + 1;
	*x = *x + 1;
}

int main()
{
	int a = 1;
	int *ptr;
	ptr = &a;
	printf("Before:\na = %d\n", a);
	increment(ptr);
	printf("After:\na = %d\n", a);
	return 0;
}

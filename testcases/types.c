#include <stdio.h>
#include <stdlib.h>

void compute(int x, float y, float c)
{
	float tmp;
	tmp = x;
	x = y;
	float abc;
        abc = c;
	y = x + y + c;
}

int main()
{
	int a = 1;
        float b = 2.5;
	printf("Before:\na = %d\nb = %f\n", a, b);
	compute(a, b, b+10);
	printf("Middle:\na = %d\nb = %f\n", a, b);
	compute(a, b, b*2 + 5 - 9 + 20);
	printf("After:\na = %d\nb = %f\n", a, b);
	return 0;
}

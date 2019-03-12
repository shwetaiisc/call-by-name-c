#include <stdio.h>
#include <stdlib.h>

void compute(int x, int y)
{
	y =  y + 10;
	x = y * 10 + 5 * 6;
}

int main()
{
	int a[5] = {1,2,3,4,5};
	int i;

	printf("Before:\n");
	for (i = 0; i < 5; i++) {
		printf("a[%d] = %d\n", i, a[i]);
	}

	for (i = -10; i < -5; i++) {
		int j = i;
		compute(a[j], j);
	}

	printf("After:\n");
	for (i = 0; i < 5; i++) {
		printf("a[%d] = %d\n", i, a[i]);
	}

	return 0;
}

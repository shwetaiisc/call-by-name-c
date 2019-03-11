#include <stdio.h>
#include <stdlib.h>

void swap(int a, int b)
{
	int tmp;
	tmp = a;
	a = b;
	b = tmp;
}

void exchange(int x, int y)
{
	swap(x, y);
}

int main()
{
	int a[5] = {1,2,3,4,5};
	int b[5] = {100,99,98,97,96};
	int i;
	for (i = 0; i < 5; i++) {
		exchange(a[i], b[ 4 - i ]);
	}

	for (i = 0; i < 5; i++)
		printf("a[%d] = %d \tb[%d] = %d\n", i, a[i], i, b[i]);

	return 0;
}

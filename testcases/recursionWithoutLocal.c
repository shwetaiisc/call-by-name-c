#include <stdio.h>
#include <stdlib.h>

void recur(int x, int y)
{
	if (x < y) {
		x = x + 1;
		y = y - 1;
		recur(x, y);
	}
	return;
}

int main()
{
	int x = 0;
	int y = 1000;

	printf("Before: x = %d\ty = %d\n", x, y);
	recur(x, y);
	printf("After: x = %d\ty = %d\n", x, y);
}

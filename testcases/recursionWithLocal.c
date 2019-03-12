#include <stdio.h>
#include <stdlib.h>
int iteration = 0;

void recur(int x)
{
	if (x == 10) {
		x = x + 100;
		return;
	}

	int tmp;
	tmp = x + 1;
	x = x + 100;
	recur(tmp);
	printf("Local Variable: %d\n", tmp);
}	

int main()
{
	int a = 0;
	recur(a);
}

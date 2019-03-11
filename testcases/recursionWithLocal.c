#include <stdio.h>
#include <stdlib.h>

int fact(int n)
{
	if (n == 0)
		return 1;

	int tmp;
	int ans, var;

	var = n;
	tmp = n - 1;

	ans = fact(tmp);
	ans = ans * var;
	return ans;
}

int main()
{
	int x = 6;
	int result;

	result = fact(x);
	printf("num = %d\nfactorial = %d\n", x, result);
}

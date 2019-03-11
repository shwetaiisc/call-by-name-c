#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct student
{
	char name[10];
	int cgpa;
};

void swapcgpa(struct student x, struct student y)
{
	int tmp;

	tmp = x.cgpa;
	x.cgpa = y.cgpa;
	y.cgpa = tmp;
}

int main()
{
	struct student a, b;
	strcpy(a.name, "Alice");
	a.cgpa = 9;

	strcpy(b.name,"Bob");
	b.cgpa = 4;

	printf("Before:\nName = %s\tCGPA = %d\n", a.name, a.cgpa);
	printf("Name = %s\tCGPA = %d\n", b.name, b.cgpa);
	swapcgpa(a, b);
	printf("After:\nName = %s\tCGPA = %d\n", a.name, a.cgpa);
	printf("Name = %s\tCGPA = %d\n", b.name, b.cgpa);
	return 0;
}

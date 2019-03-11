for f in testcases/*
do
	./build/callByName $f
	gcc output.c
	./a.out
done
rm output.c
rm a.out

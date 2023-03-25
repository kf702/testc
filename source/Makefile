

all:
	gcc test.c -L./ -lconvert2b -lm -o test.out
	gcc test32.c -L./ -lconvert2b -lm -o test32.out

lib: 
	gcc -shared -fpic -o libconvert2b.so convert2b.c

clean: 
	rm -f *.out *.so

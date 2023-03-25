
LIB_SOURCE_DIR=source

all:
	gcc test_batch.c -I./${LIB_SOURCE_DIR} -L./${LIB_SOURCE_DIR} -lconvert2b -lm -o test_batch.out

lib: 
	make -C ${LIB_SOURCE_DIR} lib

clean: 
	rm -f *.out *.so

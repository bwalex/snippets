all: test_bc test_write

test_bc: buffer_cache.c test_bc.c
	gcc -O4 buffer_cache.c test_bc.c -o test_bc -lpthread

test_write: test_write.c
	gcc -O0 test_write.c -o test_write

clean:
	rm -f test_bc
	rm -f test_write

all: test_bc test_write

test_bc: buffer_cache.c lz4/lz4.c lz4/xxhash.c test_bc.c
	gcc -O4 $^ -o test_bc -lpthread

test_write: test_write.c
	gcc -O0 test_write.c -o test_write

clean:
	rm -f test_bc
	rm -f test_write

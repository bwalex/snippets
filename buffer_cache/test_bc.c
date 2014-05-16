#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "buffer_cache.h"


int
main(int argc, char *argv[]) {
	struct buffer_cache_ctx *bc;
	char buf[16];
	int i, j, r;

	memset(buf, 0, sizeof(buf));
	bc = buffer_cache_init("bc_test.trace", BC_COMP_LZ4, 64, 4);
	assert (bc != NULL);

	for (i = 0, j = 1024*1024*512; i < 1024*1024*1024; i++, j--) {
		memcpy(buf, &i, sizeof(i));
		memcpy(buf+sizeof(i), &j, sizeof(j));
		r = buffer_cache_write(bc, buf, sizeof(i)+sizeof(j)+4);
		assert (r == 0);
	}

	buffer_cache_destroy(bc);

	return 0;
}

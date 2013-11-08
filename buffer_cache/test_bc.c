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
	int i, r;

	memset(buf, 0, sizeof(buf));
	bc = buffer_cache_init("bc_test.trace", 32, 4);
	assert (bc != NULL);

	for (i = 0; i < 1024*1024*128; i++) {
		memcpy(buf, &i, sizeof(i));
		r = buffer_cache_write(bc, buf, sizeof(buf));
		assert (r == 0);

		if ((i % 1024) == 0)
			buffer_cache_drain(bc);
	}

	buffer_cache_destroy(bc);

	return 0;
}

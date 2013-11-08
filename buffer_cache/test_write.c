#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>


int
main(int argc, char *argv[]) {
	struct buffer_cache_ctx *bc;
	char buf[16];
	int i, fd;
	ssize_t ssz;

	memset(buf, 0, sizeof(buf));
	fd = open("wr_test.trace", O_WRONLY | O_CREAT | O_TRUNC, 00666);
	assert (fd >= 0);

	for (i = 0; i < 1024*1024*128; i++) {
		memcpy(buf, &i, sizeof(i));
		ssz = write(fd, buf, sizeof(buf));
		assert (ssz == sizeof(buf));
	}

	close(fd);

	return 0;
}

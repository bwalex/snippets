#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <fcntl.h>
#include "pmjit.h"


int main(void)
{
	jit_ctx_t ctx;
	jit_tmp_t a, b, c, d, e, f, g;
	struct jit_codebuf codebuf;
	int fd;

	ctx = jit_new_ctx();
	jit_init_codebuf(ctx, &codebuf);

	d = jit_new_tmp64(ctx);
	e = jit_new_tmp32(ctx);
	f = jit_new_tmp32(ctx);
	g = jit_new_tmp32(ctx);

	jit_emit_fn_prologue(ctx, "DDD", &a, &b, &c);
	jit_emit_add(ctx, d, a, b);
	jit_emit_xor(ctx, e, b, c);
	jit_emit_movi(ctx, f, 0xffffffff);
	jit_emit_and(ctx, g, f, b);
	jit_emit_xori(ctx, e, e, f);
	jit_emit_or(ctx, e, e, d);
	jit_emit_or(ctx, e, e, b);
	jit_emit_ret(ctx, e);

	printf("\n=== Pre-optimization IR:  ===\n");
	jit_print_ir(ctx);

	jit_optimize(ctx);
	printf("\n=== Post-optimization IR: ===\n");
	jit_print_ir(ctx);

	jit_process(ctx);

	fd = open("jit.raw", O_WRONLY | O_CREAT | O_TRUNC);
	assert (fd > 0);

	write(fd, codebuf.code_ptr, codebuf.code_sz);

	close(fd);

	return 0;
}

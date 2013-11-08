/*
 * Copyright (c) 2013 Alex Hornung <alex@alexhornung.com>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/stat.h>

#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>

#include "buffer_cache.h"

struct bc_buffer {
	struct bc_buffer *next;
	struct bc_buffer *prev; /* only used on drain list */

	size_t	bytes_used;
	size_t	bytes_left;

	unsigned char *bufp;

	unsigned char buf[0];
};

struct buffer_cache_ctx {
	char	*file;
	size_t	buffer_size;
	size_t	buffer_cnt;
	int	fd;
	size_t	empty_cnt;
	size_t	drain_cnt;
	struct bc_buffer *empty;
	struct bc_buffer *drain;
	struct bc_buffer *drain_tail;
	struct bc_buffer *current_wr;

	int		thr_created;
	int		exit_drain;
	pthread_t	io_thread;
	pthread_cond_t	drain_cv;
	pthread_cond_t	empty_cv;
	pthread_mutex_t	drain_mtx;
	pthread_mutex_t	empty_mtx;
};

static
void *
_drain_thr(void *priv)
{
	struct buffer_cache_ctx *ctx = (struct buffer_cache_ctx *)priv;
	struct bc_buffer *buf;
	ssize_t ssz_written, sz_left;

	for (;;) {
		/*
		 * Lock the drain mutex and check if there is anything to
		 * drain.
		 * If there isn't, then wait on the drain cv until the
		 * write signals us to say that there now is some data
		 * on the drain list.
		 */
		pthread_mutex_lock(&ctx->drain_mtx);

		if (ctx->drain_cnt == 0) {
			if (ctx->exit_drain) {
				pthread_mutex_unlock(&ctx->drain_mtx);
				return NULL;
			}

			pthread_cond_wait(&ctx->drain_cv, &ctx->drain_mtx);

			if (ctx->exit_drain) {
				pthread_mutex_unlock(&ctx->drain_mtx);
				return NULL;
			}
		}

		/*
		 * Grab a buffer from the head of the drain list.
		 */
		assert (ctx->drain_cnt > 0);

		--ctx->drain_cnt;

		buf = ctx->drain;
		ctx->drain = buf->next;

		if (ctx->drain_tail == buf) {
			ctx->drain_tail = NULL;
		} else {
			ctx->drain->prev = NULL;
			if (ctx->drain->next != NULL)
				ctx->drain->next->prev = ctx->drain;
		}

		/*
		 * Now that we are done operating on the drain list we
		 * unlock again.
		 */
		pthread_mutex_unlock(&ctx->drain_mtx);


		/*
		 * Do the actual I/O: drain the buffer we picked from
		 * the drain list, ideally in buffer-sized chunks.
		 */
		sz_left = buf->bytes_used;
		while (sz_left > 0) {
			ssz_written = write(ctx->fd, buf->buf, sz_left);
			assert (ssz_written >= 0);
			sz_left -= (size_t)ssz_written;
		}


		/*
		 * Lock the empty mutex, reinitialize the now-empty
		 * buffer and place it on the head of the empty
		 * list.
		 * Signal any listeners that are waiting for buffers
		 * to become empty (either _write or _destroy).
		 */
		pthread_mutex_lock(&ctx->empty_mtx);

		buf->bytes_left = ctx->buffer_size;
		buf->bytes_used = 0;
		buf->bufp = buf->buf;
		buf->prev = NULL;
		buf->next = ctx->empty;
		ctx->empty = buf;
		assert (ctx->empty != NULL);
		++ctx->empty_cnt;

		pthread_cond_broadcast(&ctx->empty_cv);
		pthread_mutex_unlock(&ctx->empty_mtx);
	}

	return NULL;
}


struct buffer_cache_ctx *
buffer_cache_init(const char *file, size_t buffer_size_mb, size_t buffer_cnt)
{
	struct bc_buffer *buf;
	struct buffer_cache_ctx *ctx = NULL;
	size_t buffer_size_b;
	size_t i;
	int r;

	buffer_size_b = buffer_size_mb*1024*1024;

	if (buffer_size_mb < 1 || buffer_cnt < 1) {
		fprintf(stderr, "Didn't specify at least one buffer of at least 1 MB\n");
		return NULL;
	}

	if ((ctx = malloc(sizeof(*ctx))) == NULL) {
		fprintf(stderr, "Failed to allocate ctx memory\n");
		return NULL;
	}

	memset(ctx, 0, sizeof(*ctx));
	ctx->fd = -1;

	pthread_cond_init(&ctx->empty_cv, NULL);
	pthread_cond_init(&ctx->drain_cv, NULL);
	pthread_mutex_init(&ctx->empty_mtx, NULL);
	pthread_mutex_init(&ctx->drain_mtx, NULL);

	ctx->file = strdup(file);
	if (ctx->file == NULL) {
		fprintf(stderr, "Failed to allocate strdup memory\n");
		buffer_cache_destroy(ctx);
		return NULL;
	}

	if ((ctx->fd = open(ctx->file, O_WRONLY | O_CREAT | O_TRUNC, 00666)) < 0) {
		fprintf(stderr, "Failed to open file %s\n", ctx->file);
		buffer_cache_destroy(ctx);
		return NULL;
	}

	ctx->buffer_size = buffer_size_b;
	ctx->buffer_cnt = buffer_cnt;

	/*
	 * Allocate all the buffers that have been requested and place them
	 * on the empty list.
	 */
	for (i = 0; i < buffer_cnt; i++) {
		if ((buf = malloc(sizeof(*buf) + buffer_size_b)) == NULL) {
			fprintf(stderr, "Failed to allocate %ju bytes for buffer %ju\n", sizeof(*buf) + buffer_size_b, i);
			buffer_cache_destroy(ctx);
			return NULL;
		}

		memset(buf, 0, sizeof(*buf) + buffer_size_b);
		buf->bufp = buf->buf;
		buf->bytes_left = buffer_size_b;
		buf->bytes_used = 0;
		buf->prev = NULL;
		buf->next = ctx->empty;
		if (ctx->empty != NULL)
			ctx->empty->prev = buf;
		ctx->empty = buf;

		++ctx->empty_cnt;
	}

	assert (ctx->empty_cnt == ctx->buffer_cnt);

	/*
	 * Remove the first buffer from the empty list and use it as the
	 * current write buffer.
	 */
	ctx->current_wr = ctx->empty;
	ctx->empty = ctx->empty->next;
	--ctx->empty_cnt;

	/*
	 * Initialize the drain thread.
	 */
	if ((r = pthread_create(&ctx->io_thread, NULL, _drain_thr, ctx)) != 0) {
		fprintf(stderr, "Failed to pthread_create()\n");
		buffer_cache_destroy(ctx);
		return NULL;
	}

	ctx->thr_created = 1;

	return ctx;
}

static
void
_drain_current(struct buffer_cache_ctx *ctx)
{
	struct bc_buffer *buf = ctx->current_wr;

	assert (buf != NULL);

	/*
	 * Lock the drain mutex and move the current write buffer
	 * onto the tail of the drain list and signal the drain
	 * thread that there's something for it to drain now.
	 */
	buf->prev = NULL;
	buf->next = NULL;

	pthread_mutex_lock(&ctx->drain_mtx);

	if (ctx->drain_tail == NULL) {
		assert (ctx->drain == NULL);
		ctx->drain = buf;
	} else {
		ctx->drain_tail->next = buf;
		buf->prev = ctx->drain_tail;
	}

	ctx->drain_tail = buf;
	++ctx->drain_cnt;
	ctx->current_wr = NULL;

	pthread_cond_signal(&ctx->drain_cv);
	pthread_mutex_unlock(&ctx->drain_mtx);
}

int
buffer_cache_write(struct buffer_cache_ctx *ctx, const void *data, size_t count)
{
	struct bc_buffer *buf = ctx->current_wr;

	/*
	 * If the current buffer doesn't have enough space to write the
	 * new data into it, move it to the drain list, lock the empty
	 * mutex and grab an empty buffer if one is available - otherwise
	 * wait until we are told that there is.
	 */
	if (buf->bytes_left < count) {
		_drain_current(ctx);

		pthread_mutex_lock(&ctx->empty_mtx);

		if (ctx->empty_cnt == 0)
			pthread_cond_wait(&ctx->empty_cv, &ctx->empty_mtx);

		assert (ctx->empty_cnt > 0);
		assert (ctx->empty != NULL);
		--ctx->empty_cnt;

		buf = ctx->current_wr = ctx->empty;
		ctx->empty = buf->next;

		pthread_mutex_unlock(&ctx->empty_mtx);
	}

	/*
	 * The critical path is a simple memcpy and some minor pointer/
	 * counter adjustments on the current buffer. No locking
	 * necessary as the current buffer is only ever touched by
	 * this thread.
	 */
	memcpy(buf->bufp, data, count);
	buf->bufp += count;
	buf->bytes_left -= count;
	buf->bytes_used += count;

	return 0;
}


void
buffer_cache_destroy(struct buffer_cache_ctx *ctx)
{
	struct bc_buffer *buf;
	struct bc_buffer *next;

	if (ctx->thr_created) {
		/*
		 * If the drain thread already exists, make sure the
		 * current buffer is moved onto the drain list before
		 * doing anything else.
		 */
		if (ctx->current_wr != NULL) {
			_drain_current(ctx);
		}

		/*
		 * Now that all buffers are either on the empty or the
		 * drain list, wait for all buffers to become empty.
		 *
		 * Once that happens, we can tell the drain thread to
		 * exit.
		 */
		pthread_mutex_lock(&ctx->empty_mtx);
		while (ctx->empty_cnt != ctx->buffer_cnt)
			pthread_cond_wait(&ctx->empty_cv, &ctx->empty_mtx);
		pthread_mutex_unlock(&ctx->empty_mtx);

		pthread_mutex_lock(&ctx->drain_mtx);
		ctx->exit_drain = 1;
		pthread_cond_signal(&ctx->drain_cv);
		pthread_mutex_unlock(&ctx->drain_mtx);

		pthread_join(ctx->io_thread, NULL);
	}

	pthread_cond_destroy(&ctx->drain_cv);
	pthread_cond_destroy(&ctx->empty_cv);
	pthread_mutex_destroy(&ctx->drain_mtx);
	pthread_mutex_destroy(&ctx->empty_mtx);

	if (ctx->fd >= 0)
		close(ctx->fd);

	if (ctx->file != NULL)
		free(ctx->file);

	if (ctx->current_wr != NULL)
		free(ctx->current_wr);


	for (buf = ctx->drain; buf != NULL; buf = next) {
		next = buf->next;
		free(buf);
	}

	for (buf = ctx->empty; buf != NULL; buf = next) {
		next = buf->next;
		free(buf);
	}
}

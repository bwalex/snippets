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

#ifndef _WITHOUT_LZ4
#include "lz4/lz4.h"
#include "lz4/xxhash.h"
#endif

#ifdef _WITH_ZLIB
#include "zlib.h"
#endif
#include "buffer_cache.h"

#define LZ4_EXTRA_SZ	64*1024
#define LZ4_BLOCK_SZ	4*1024*1024
#define ZLIB_BLOCK_SZ	LZ4_BLOCK_SZ

struct bc_buffer {
	struct bc_buffer *next;
	struct bc_buffer *prev; /* only used on drain list */

	size_t	bytes_used;
	size_t	bytes_left;

	unsigned char *bufp;

	unsigned char buf[0];
};

#ifndef _WITHOUT_LZ4
struct lz4_state {
	int		hdr_written;
	int		first;
	int		stream_checksum;
	void		*lz4_state;
	void		*xxh32_state;
	unsigned char	lz4_link_buf[LZ4_EXTRA_SZ];
	unsigned char   lz4_obuf[LZ4_COMPRESSBOUND(LZ4_BLOCK_SZ)+4];
};
#endif

#ifdef _WITH_ZLIB
struct zlib_state {
	int		hdr_written;
	unsigned int	isize;
	unsigned int	zlib_crc32;
	z_stream	zlib_strm;
	unsigned char   zlib_obuf[ZLIB_BLOCK_SZ];
};
#endif

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

	int		compress;
	union {
		int	_dummy;
#ifndef _WITHOUT_LZ4
		struct lz4_state	lz4_state;
#endif
#ifdef _WITH_ZLIB
		struct zlib_state	zlib_state;
#endif
	};

	int		thr_created;
	int		exit_drain;
	pthread_t	io_thread;
	pthread_cond_t	drain_cv;
	pthread_cond_t	empty_cv;
	pthread_mutex_t	drain_mtx;
	pthread_mutex_t	empty_mtx;
};


#ifndef _WITHOUT_LZ4
static
int
lz4_write_hdr(struct buffer_cache_ctx *ctx)
{
	struct lz4_state *lz4_ctx = &ctx->lz4_state;
	ssize_t ssz_written;
	unsigned char buf[19];
	unsigned int magic = 0x184D2204;
	int hdr_sz = 0;

	memset(buf, 0, sizeof(buf));
	memcpy(&buf[0], &magic, sizeof(magic));
	hdr_sz += sizeof(magic);
	buf[hdr_sz++] = (0x1 << 6) | (lz4_ctx->stream_checksum << 2); // FLG:{VER, stream checksum}
	buf[hdr_sz++] = (0x7 << 4); // BD:{4MB blocks}
	buf[hdr_sz++] = (XXH32(&buf[4], 2, 0) >> 8) & 0xFF; // HC

	assert (hdr_sz <= sizeof(buf));

	ssz_written = write(ctx->fd, buf, (size_t)hdr_sz);
	if (ssz_written == (ssize_t)hdr_sz) {
		lz4_ctx->hdr_written = 1;
		return 0;
	} else {
		return 1;
	}
}

static
int
lz4_write_tail(struct buffer_cache_ctx *ctx)
{
	struct lz4_state *lz4_ctx = &ctx->lz4_state;
	unsigned int eos = 0;
	unsigned int cksum;
	ssize_t ssz_written;

	/* Write End-of-Stream marker */
	ssz_written = write(ctx->fd, &eos, 4);
	if (ssz_written != 4)
		return 1;

	/* Write stream checksum, if enabled */
	if (lz4_ctx->stream_checksum) {
		cksum = XXH32_digest(lz4_ctx->xxh32_state);
		ssz_written = write(ctx->fd, &cksum, 4);
		if (ssz_written != 4)
			return 1;
	}

	return 0;
}

static
int
lz4_write_buf(struct buffer_cache_ctx *ctx, struct bc_buffer *buf)
{
	struct lz4_state *lz4_ctx = &ctx->lz4_state;
	ssize_t ssz_written;
	size_t sz_left;
	unsigned int in_sz, out_sz;
	unsigned char *bufp;
	unsigned int sz_val;

	if (!lz4_ctx->first) {
		/*
		 * XXX: revisit whenever the LZ4 streaming API understands separate
		 * input blocks...
		 */
	} else {
		lz4_ctx->first = 0;
	}

	sz_left = buf->bytes_used;
	buf->bufp = buf->buf + LZ4_EXTRA_SZ;
	while (sz_left > 0) {
		in_sz = (sz_left < LZ4_BLOCK_SZ) ? (int)sz_left : LZ4_BLOCK_SZ;

		if (lz4_ctx->stream_checksum)
			XXH32_update(lz4_ctx->xxh32_state, buf->bufp, in_sz);

		/*
		 * (Try to) compress into obuf+4, keeping the first 4 bytes for
		 * size information.
		 */
		out_sz = LZ4_compress_limitedOutput((char *)buf->bufp,
		    (char *)lz4_ctx->lz4_obuf+4, in_sz, in_sz-1);

		/* XXX: all things lz4 assume little endian */
		if (out_sz > 0) {
			/* Block compressed fine */

			/* Copy the compressed block size into the output buffer */
			memcpy(lz4_ctx->lz4_obuf, &out_sz, 4);
			out_sz += 4;

			/* Write the compressed block, prefixed with the block size */
			bufp = lz4_ctx->lz4_obuf;
			while (out_sz > 0) {
				ssz_written = write(ctx->fd, bufp, (size_t)out_sz);
				assert (ssz_written >= 0);
				bufp += ssz_written;
				out_sz -= (int)ssz_written;
			}
		} else {
			/* Couldn't compress */

			/* First, write the size, with the "uncompressed" flag */
			sz_val = in_sz | 0x80000000;
			ssz_written = write(ctx->fd, &sz_val, 4);
			assert (ssz_written == 4);

			/* Now, write the uncompressed input block */
			bufp = buf->bufp;
			while (in_sz > 0) {
				ssz_written = write(ctx->fd, bufp, (size_t)in_sz);
				assert (ssz_written >= 0);
				bufp += ssz_written;
				in_sz -= (int)ssz_written;
			}

			/* Restore in_sz */
			in_sz = (sz_left < LZ4_BLOCK_SZ) ? (int)sz_left : LZ4_BLOCK_SZ;
		}

		buf->bufp += in_sz;
		sz_left -= (size_t)in_sz;
	}

	return 0;
}
#endif


#ifdef _WITH_ZLIB
static
int
zlib_write_hdr(struct buffer_cache_ctx *ctx)
{
	struct zlib_state *zlib_ctx = &ctx->zlib_state;
	ssize_t ssz_written;
	unsigned char buf[10];
	int hdr_sz = 0;
	unsigned int mtime = 0;

	buf[hdr_sz++] = 0x1f; // ID1
	buf[hdr_sz++] = 0x8b; // ID2
	buf[hdr_sz++] = 0x08; // CM:{deflate}
	buf[hdr_sz++] = 0x00; // FLG
	memcpy(&buf[4], &mtime, 4);
	hdr_sz += 4;
	buf[hdr_sz++] = 0x00; // XFL:{used fastest algorithm}
	buf[hdr_sz++] = 0xff; // OS:{unknown}

	assert (hdr_sz <= sizeof(buf));

	ssz_written = write(ctx->fd, buf, (size_t)hdr_sz);
	if (ssz_written == (ssize_t)hdr_sz) {
		zlib_ctx->hdr_written = 1;
		return 0;
	} else {
		return 1;
	}
}

static
int
zlib_write_tail(struct buffer_cache_ctx *ctx)
{
	struct zlib_state *zlib_ctx = &ctx->zlib_state;
	ssize_t ssz_written;
	size_t out_sz;
	unsigned char *bufp;
	int r;

	/* Finish stream */
	do {
		zlib_ctx->zlib_strm.next_out = zlib_ctx->zlib_obuf;
		zlib_ctx->zlib_strm.avail_out = ZLIB_BLOCK_SZ;

		r = deflate(&zlib_ctx->zlib_strm, Z_FINISH);
		assert (r != Z_STREAM_ERROR);
		assert (r != Z_BUF_ERROR);

		out_sz = ZLIB_BLOCK_SZ - zlib_ctx->zlib_strm.avail_out;

		/* Write the compressed output zlib has provided so far */
		bufp = zlib_ctx->zlib_obuf;
		while (out_sz > 0) {
			ssz_written = write(ctx->fd, bufp, out_sz);
			assert (ssz_written >= 0);
			bufp += ssz_written;
			out_sz -= (size_t)ssz_written;
		}

	} while (r != Z_STREAM_END);

	/* Write checksum */
	ssz_written = write(ctx->fd, &zlib_ctx->zlib_crc32, 4);
	if (ssz_written != 4)
		return 1;

	/* Write isize (length % 2^32) */
	ssz_written = write(ctx->fd, &zlib_ctx->isize, 4);
	if (ssz_written != 4)
		return 1;

	return 0;
}

static
int
zlib_write_buf(struct buffer_cache_ctx *ctx, struct bc_buffer *buf)
{
	struct zlib_state *zlib_ctx = &ctx->zlib_state;
	ssize_t ssz_written;
	size_t sz_left;
	size_t out_sz;
	unsigned char *bufp;
	unsigned int sz_val;
	int r;

	sz_left = buf->bytes_used;
	buf->bufp = buf->buf + LZ4_EXTRA_SZ;

	zlib_ctx->zlib_strm.next_in = buf->bufp;
	zlib_ctx->zlib_strm.avail_in = sz_left;

	zlib_ctx->isize += (unsigned int)sz_left;
	zlib_ctx->zlib_crc32 = crc32(zlib_ctx->zlib_crc32, buf->bufp, sz_left);

	do {
		zlib_ctx->zlib_strm.next_out = zlib_ctx->zlib_obuf;
		zlib_ctx->zlib_strm.avail_out = ZLIB_BLOCK_SZ;

		r = deflate(&zlib_ctx->zlib_strm, Z_NO_FLUSH);
		assert (r != Z_STREAM_ERROR);
		assert (r != Z_BUF_ERROR);

		out_sz = ZLIB_BLOCK_SZ - zlib_ctx->zlib_strm.avail_out;

		/* Write the compressed output zlib has provided so far */
		bufp = zlib_ctx->zlib_obuf;
		while (out_sz > 0) {
			ssz_written = write(ctx->fd, bufp, out_sz);
			assert (ssz_written >= 0);
			bufp += ssz_written;
			out_sz -= (size_t)ssz_written;
		}
	} while (zlib_ctx->zlib_strm.avail_in);

	return 0;
}
#endif


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
		switch (ctx->compress) {
#ifndef _WITHOUT_LZ4
		case BC_COMP_LZ4:
			assert (lz4_write_buf(ctx, buf) == 0);
			break;
#endif

#ifdef _WITH_ZLIB
		case BC_COMP_ZLIB:
			assert (zlib_write_buf(ctx, buf) == 0);
			break;
#endif

		case BC_COMP_NONE:
		default:
			sz_left = buf->bytes_used;
			buf->bufp = buf->buf + LZ4_EXTRA_SZ;
			while (sz_left > 0) {
				ssz_written = write(ctx->fd, buf->bufp, sz_left);
				assert (ssz_written >= 0);
				buf->bufp += ssz_written;
				sz_left -= (size_t)ssz_written;
			}
			break;
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
		buf->bufp = buf->buf + LZ4_EXTRA_SZ;
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
buffer_cache_init(const char *file, int compress, size_t buffer_size_mb, size_t buffer_cnt)
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
	ctx->compress = compress;

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

	switch (ctx->compress) {
#ifndef _WITHOUT_LZ4
	case BC_COMP_LZ4:
		ctx->lz4_state.stream_checksum = 1;
		ctx->lz4_state.first = 1;
		ctx->lz4_state.xxh32_state = XXH32_init(0);
		if ((r = lz4_write_hdr(ctx)) != 0) {
			fprintf(stderr, "Failed to write LZ4 header");
			buffer_cache_destroy(ctx);
			return NULL;
		}
		break;
#endif

#ifdef _WITH_ZLIB
	case BC_COMP_ZLIB:
		ctx->zlib_state.zlib_strm.zalloc = NULL;
		ctx->zlib_state.zlib_strm.zfree = NULL;
		ctx->zlib_state.zlib_strm.opaque = NULL;
		ctx->zlib_state.isize = 0;
		ctx->zlib_state.zlib_crc32 = crc32(0L, Z_NULL, 0);
		if ((r = deflateInit2(&ctx->zlib_state.zlib_strm, 1 /* level */, Z_DEFLATED, (-MAX_WBITS), 8, Z_DEFAULT_STRATEGY)) != Z_OK) {
			fprintf(stderr, "Failed to initialize deflate");
			buffer_cache_destroy(ctx);
			return NULL;
		}
		if ((r = zlib_write_hdr(ctx)) != 0) {
			fprintf(stderr, "Failed to write gzip header");
			buffer_cache_destroy(ctx);
			return NULL;
		}
		break;
#endif

	case BC_COMP_NONE:
		break;

	default:
		fprintf(stderr, "Invalid compression option\n");
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
		if ((buf = malloc(sizeof(*buf) + buffer_size_b + LZ4_EXTRA_SZ)) == NULL) {
			fprintf(stderr, "Failed to allocate %ju bytes for buffer %ju\n", sizeof(*buf) + buffer_size_b, i);
			buffer_cache_destroy(ctx);
			return NULL;
		}

		memset(buf, 0, sizeof(*buf) + buffer_size_b + LZ4_EXTRA_SZ);
		buf->bufp = buf->buf + LZ4_EXTRA_SZ;
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
	if ((buf == NULL) || (buf->bytes_left < count)) {
		if (buf != NULL)
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

int
buffer_cache_drain(struct buffer_cache_ctx *ctx)
{
	if (ctx->current_wr != NULL)
		_drain_current(ctx);

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

	if (ctx->fd >= 0) {
		switch (ctx->compress) {
#ifndef _WITHOUT_LZ4
		case BC_COMP_LZ4:
			if (ctx->lz4_state.hdr_written)
				lz4_write_tail(ctx);
			break;
#endif

#ifdef _WITH_ZLIB
		case BC_COMP_ZLIB:
			if (ctx->zlib_state.hdr_written)
				zlib_write_tail(ctx);
			break;
#endif

		default:
			break;
		}

		close(ctx->fd);
	}

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

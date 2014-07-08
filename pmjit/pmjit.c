/*
 * Copyright (c) 2012 Alex Hornung <alex@alexhornung.com>.
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
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
*/
#include <sys/mman.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include "pmjit.h"
#include "pmjit-tgt.h"

/*
 * XXX: GET_TMP_STATE() should check if the tmp is allocated or not in
 *      the first place.
 */
#define GET_TMP_STATE(ctx, t)						\
    ((JIT_TMP_IS_LOCAL(t)) ? &ctx->local_tmps[JIT_TMP_INDEX(t)] :	\
     &ctx->bb_tmps[JIT_TMP_INDEX(t)])



int
jit_ctx_init(jit_ctx_t ctx)
{
	memset(ctx, 0, sizeof(struct jit_ctx));
	return 0;
}

static
void
tmp_add_use(jit_ctx_t ctx, jit_bb_t bb, jit_tmp_t tmp, int use_idx)
{
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, tmp);
	jit_tmp_use_t use;

	use = malloc(sizeof(struct jit_tmp_use));
	assert (use != NULL);

	memset(use, 0, sizeof(struct jit_tmp_use));
	use->use_idx = use_idx;
	use->generation = ts->scan.generation;

	if (ts->scan.use_head == NULL) {
		ts->scan.use_head = ts->scan.use_tail = use;
	} else {
		use->prev = ts->scan.use_tail;
		if (use->prev != NULL)
			use->prev->next = use;
		ts->scan.use_tail = use;
	}
}

static
int
tmp_is_dead(jit_ctx_t ctx, jit_bb_t bb, jit_tmp_t tmp)
{
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, tmp);

	return (ts->scan.use_head == NULL);
}

static
int
tmp_is_relatively_dead(jit_ctx_t ctx, jit_bb_t bb, jit_tmp_t tmp, int rel_generation)
{
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, tmp);

	if (tmp_is_dead(ctx, bb, tmp))
		return 1;

	return (ts->scan.use_head->generation > rel_generation);
}

static
void
tmp_pop_use(jit_ctx_t ctx, jit_tmp_t tmp)
{
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, tmp);
	jit_tmp_use_t use;

	assert (ts->scan.use_head != NULL);

	use = ts->scan.use_head;

	if (ts->scan.use_tail == use) {
		ts->scan.use_head = NULL;
		ts->scan.use_tail = NULL;
	} else {
		use->next->prev = NULL;
		ts->scan.use_head = use->next;
	}

	free(use);
}

static
inline
jit_tmp_t
_jit_new_tmp(jit_ctx_t ctx, int local, int constant, int w64)
{
	jit_tmp_state_t tmp_state;
	jit_tmp_t tmp = 0;

	if (local) {
		if (ctx->local_tmps_cnt == ctx->local_tmps_sz) {
			ctx->local_tmps_sz += 8;
			ctx->local_tmps = realloc(ctx->local_tmps,
			    ctx->local_tmps_sz * sizeof(struct jit_tmp_state));
			assert (ctx->local_tmps != NULL);
		}
		tmp = JIT_TMP_LOCAL(ctx->local_tmps_cnt++);
		tmp_state = &ctx->local_tmps[JIT_TMP_INDEX(tmp)];
	} else {
		if (ctx->bb_tmps_cnt == ctx->bb_tmps_sz) {
			ctx->bb_tmps_sz += 12;
			ctx->bb_tmps = realloc(ctx->bb_tmps,
			    ctx->bb_tmps_sz * sizeof(struct jit_tmp_state));
			assert (ctx->bb_tmps != NULL);
		}
		tmp = ctx->bb_tmps_cnt++;
		tmp_state = &ctx->bb_tmps[JIT_TMP_INDEX(tmp)];
	}

	memset(tmp_state, 0, sizeof(struct jit_tmp_state));

	tmp_state->scan.use_head = tmp_state->scan.use_tail = NULL;
	tmp_state->dirty = 0;
	tmp_state->local = local;
	tmp_state->constant = constant;
	tmp_state->pinned = 0;
	tmp_state->mem_allocated = 0;
	tmp_state->w64 = w64;
	tmp_state->loc = JITLOC_UNALLOCATED;
	tmp_state->id = JIT_TMP_INDEX(tmp);

	return tmp;
}


jit_tmp_t
jit_new_tmp64(jit_ctx_t ctx)
{
	return _jit_new_tmp(ctx, 0, 0, 1);
}


jit_tmp_t
jit_new_tmp32(jit_ctx_t ctx)
{
	return _jit_new_tmp(ctx, 0, 0, 0);
}


jit_tmp_t
jit_new_local_tmp64(jit_ctx_t ctx)
{
	return _jit_new_tmp(ctx, 1, 0, 1);
}


jit_tmp_t
jit_new_local_tmp32(jit_ctx_t ctx)
{
	return _jit_new_tmp(ctx, 1, 0, 0);
}


void
jit_pin_local_tmp(jit_ctx_t ctx, jit_tmp_t tmp)
{
	jit_tmp_state_t t1 = GET_TMP_STATE(ctx, tmp);
	t1->pinned = 1;
}

static
inline
jit_bb_t
_cur_block(jit_ctx_t ctx)
{
	jit_bb_t bb;

	if (ctx->cur_block == ctx->blocks_sz) {
		ctx->blocks_sz += 1;

		ctx->blocks = realloc(ctx->blocks,
		    ctx->blocks_sz * sizeof(struct jit_bb));
		assert (ctx->blocks != NULL);

		bb = &ctx->blocks[ctx->cur_block];

		memset(bb, 0, sizeof(struct jit_bb));

		bb->tmp_idx_1st = -1;
		bb->tmp_idx_last = -1;
		bb->opc_ptr = &bb->opcodes[0];
		bb->param_ptr = &bb->params[0];
		++ctx->block_cnt;
	}

	return &ctx->blocks[ctx->cur_block];
}

static
inline
void
_end_bb(jit_ctx_t ctx)
{
	++ctx->cur_block;
}


static
void
jit_emitv(jit_ctx_t ctx, jit_op_t op, int dw, const char *fmt, ...)
{
	va_list ap;
	jit_bb_t bb = _cur_block(ctx);
	jit_op_def_t def = &op_def[op];
	jit_tmp_t t;
	jit_tmp_state_t ts;
	jit_label_t l;
	jit_cc_t cc;
	int cc_dw = JITOP_DW_64;
	int count = 0;
	uint64_t u64;

	va_start(ap, fmt);
	for (; *fmt != '\0'; fmt++) {
		switch (*fmt) {
		case 'r':
			t = va_arg(ap, jit_tmp_t);
			ts = GET_TMP_STATE(ctx, t);
			/* XXX: store ts instead? */
			*bb->param_ptr++ = (uint64_t)t;

			if (count < def->out_args) {
				++ts->scan.generation;
			} else {
				tmp_add_use(ctx, bb, t, bb->opc_cnt);
			}
			break;
		case 'i':
		case 'I':
			u64 = va_arg(ap, uint64_t);
			*bb->param_ptr++ = (uint64_t)u64;
			break;
		case 'c':
			cc_dw = va_arg(ap, int);
			cc = va_arg(ap, jit_cc_t);
			*bb->param_ptr++ = (uint64_t)cc;
			break;
		case 'l':
			l = va_arg(ap, jit_label_t);
			*bb->param_ptr++ = (uint64_t)l;
			break;
		default:
			assert (0);
		}

		++count;
	}
	va_end(ap);

	*bb->opc_ptr++ = JITOP(op, dw, cc_dw);
	++bb->opc_cnt;
}

void
jit_emit_and(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, dst);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_AND, dw, "rrr", dst, r1, r2);
}

void
jit_emit_andi(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, uint64_t imm)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, dst);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_ANDI, ts->w64 ? JITOP_DW_64 : JITOP_DW_32,
	    "rri", dst, r1, imm);
}

void
jit_emit_or(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, dst);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_OR, dw, "rrr", dst, r1, r2);
}

void
jit_emit_ori(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, uint64_t imm)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, dst);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_ORI, ts->w64 ? JITOP_DW_64 : JITOP_DW_32,
	    "rri", dst, r1, imm);
}

void
jit_emit_xor(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, dst);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_XOR, dw, "rrr", dst, r1, r2);
}

void
jit_emit_xori(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, uint64_t imm)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, dst);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_XORI, ts->w64 ? JITOP_DW_64 : JITOP_DW_32,
	    "rri", dst, r1, imm);
}

void
jit_emit_add(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, dst);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_ADD, dw, "rrr", dst, r1, r2);
}

void
jit_emit_addi(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, uint64_t imm)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, dst);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_ADDI, ts->w64 ? JITOP_DW_64 : JITOP_DW_32,
	    "rri", dst, r1, imm);
}

void
jit_emit_shl(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, dst);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_SHL, dw, "rrr", dst, r1, r2);
}

void
jit_emit_shli(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, uint8_t imm)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, dst);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_SHLI, ts->w64 ? JITOP_DW_64 : JITOP_DW_32,
	    "rri", dst, r1, (uint64_t)imm);
}

void
jit_emit_shr(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, dst);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_SHR, dw, "rrr", dst, r1, r2);
}

void
jit_emit_shri(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, uint8_t imm)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, dst);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_SHRI, ts->w64 ? JITOP_DW_64 : JITOP_DW_32,
	    "rri", dst, r1, (uint64_t)imm);
}

void
jit_emit_mov(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, r1);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_MOV, dw, "rr", dst, r1);
}

void
jit_emit_movi(jit_ctx_t ctx, jit_tmp_t dst, uint64_t imm)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, dst);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_MOVI, ts->w64 ? JITOP_DW_64 : JITOP_DW_32,
	    "ri", dst, imm);
}

void
jit_emit_not(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, r1);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_NOT, dw, "rr", dst, r1);
}

void
jit_emit_bswap(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, r1);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_BSWAP, dw, "rr", dst, r1);
}

void
jit_emit_clz(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, r1);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_CLZ, dw, "rr", dst, r1);
}

void
jit_emit_bfe(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, uint8_t hi, uint8_t lo)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, r1);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_BSWAP, dw, "rrII", dst, r1, (uint64_t)hi, (uint64_t)lo);
}

void
jit_emit_branch(jit_ctx_t ctx, jit_label_t l)
{
	jit_bb_t bb = _cur_block(ctx);

	jit_emitv(ctx, JITOP_BRANCH, JITOP_DW_64, "l", l);
	_end_bb(ctx);
}

void
jit_emit_bcmp(jit_ctx_t ctx, jit_label_t l, jit_cc_t cc, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	jit_tmp_state_t ts2 = GET_TMP_STATE(ctx, r2);
	int dw = (ts1->w64 && ts2->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_BCMP, JITOP_DW_64, "lcrr", l, dw, cc, r1, r2);
	_end_bb(ctx);
}

void
jit_emit_bcmpi(jit_ctx_t ctx, jit_label_t l, jit_cc_t cc, jit_tmp_t r1, uint64_t imm)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	int dw = (ts1->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_BCMP, JITOP_DW_64, "lcri", l, dw, cc, r1, imm);
	_end_bb(ctx);
}

void
jit_emit_bncmp(jit_ctx_t ctx, jit_label_t l, jit_cc_t cc, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	jit_tmp_state_t ts2 = GET_TMP_STATE(ctx, r2);
	int dw = (ts1->w64 && ts2->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_BNCMP, JITOP_DW_64, "lcrr", l, dw, cc, r1, r2);
	_end_bb(ctx);
}

void
jit_emit_bncmpi(jit_ctx_t ctx, jit_label_t l, jit_cc_t cc, jit_tmp_t r1, uint64_t imm)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	int dw = (ts1->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_BNCMPI, JITOP_DW_64, "lcri", l, dw, cc, r1, imm);
	_end_bb(ctx);
}

void
jit_emit_btest(jit_ctx_t ctx, jit_label_t l, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	jit_tmp_state_t ts2 = GET_TMP_STATE(ctx, r2);
	int dw = (ts1->w64 && ts2->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_BTEST, dw, "lrr", l, r1, r2);
	_end_bb(ctx);
}

void
jit_emit_btesti(jit_ctx_t ctx, jit_label_t l, jit_tmp_t r1, uint64_t imm)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	int dw = (ts1->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_BTESTI, dw, "lri", l, r1, imm);
	_end_bb(ctx);
}

void
jit_emit_bntest(jit_ctx_t ctx, jit_label_t l, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	jit_tmp_state_t ts2 = GET_TMP_STATE(ctx, r2);
	int dw = (ts1->w64 && ts2->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_BNTEST, dw, "lrr", l, r1, r2);
	_end_bb(ctx);
}

void
jit_emit_bntesti(jit_ctx_t ctx, jit_label_t l, jit_tmp_t r1, uint64_t imm)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	int dw = (ts1->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_BNTESTI, dw, "lri", l, r1, imm);
	_end_bb(ctx);
}

void
jit_emit_cmov(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src, jit_cc_t cc, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_tmp_state_t ts_dst = GET_TMP_STATE(ctx, dst);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	jit_tmp_state_t ts2 = GET_TMP_STATE(ctx, r2);

	int dw = (ts_dst->w64) ? JITOP_DW_64 : JITOP_DW_32;
	int cc_dw = (ts1->w64 && ts2->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_CMOV, dw, "rrcrr", dst, src, cc_dw, cc, r1, r2);
}

void
jit_emit_cmovi(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src, jit_cc_t cc, jit_tmp_t r1, uint64_t imm)
{
	jit_tmp_state_t ts_dst = GET_TMP_STATE(ctx, dst);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);

	int dw = (ts_dst->w64) ? JITOP_DW_64 : JITOP_DW_32;
	int cc_dw = (ts1->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_CMOVI, dw, "rrcri", dst, src, cc_dw, cc, r1, imm);
}

void
jit_emit_csel(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src1, jit_tmp_t src2, jit_cc_t cc, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_tmp_state_t ts_dst = GET_TMP_STATE(ctx, dst);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	jit_tmp_state_t ts2 = GET_TMP_STATE(ctx, r2);

	int dw = (ts_dst->w64) ? JITOP_DW_64 : JITOP_DW_32;
	int cc_dw = (ts1->w64 && ts2->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_CSEL, dw, "rrrcrr", dst, src1, src2, cc_dw, cc, r1, r2);
}

void
jit_emit_cseli(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src1, jit_tmp_t src2, jit_cc_t cc, jit_tmp_t r1, uint64_t imm)
{
	jit_tmp_state_t ts_dst = GET_TMP_STATE(ctx, dst);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);

	int dw = (ts_dst->w64) ? JITOP_DW_64 : JITOP_DW_32;
	int cc_dw = (ts1->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_CSELI, dw, "rrrcri", dst, src1, src2, cc_dw, cc, r1, imm);
}

void
jit_emit_cset(jit_ctx_t ctx, jit_tmp_t dst, jit_cc_t cc, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_tmp_state_t ts_dst = GET_TMP_STATE(ctx, dst);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	jit_tmp_state_t ts2 = GET_TMP_STATE(ctx, r2);

	int dw = (ts_dst->w64) ? JITOP_DW_64 : JITOP_DW_32;
	int cc_dw = (ts1->w64 && ts2->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_CSET, dw, "rcrr", dst, cc_dw, cc, r1, r2);
}

void
jit_emit_cseti(jit_ctx_t ctx, jit_tmp_t dst, jit_cc_t cc, jit_tmp_t r1, uint64_t imm)
{
	jit_tmp_state_t ts_dst = GET_TMP_STATE(ctx, dst);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);

	int dw = (ts_dst->w64) ? JITOP_DW_64 : JITOP_DW_32;
	int cc_dw = (ts1->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_CSETI, dw, "rcri", dst, cc_dw, cc, r1, imm);
}

void
jit_emit_set_label(jit_ctx_t ctx, jit_label_t l)
{
	jit_bb_t bb = _cur_block(ctx);
	/*
	 * Don't end the current basic block if the set label is the
	 * first instruction of a basic block.
	 */
	if (bb->opc_cnt > 0)
		_end_bb(ctx);

	jit_emitv(ctx, JITOP_SET_LABEL, JITOP_DW_64, "l", l);
}



void
jit_emit_fn_prologue(jit_ctx_t ctx, const char *fmt, ...)
{
	va_list ap;
	int cnt = 0;
	int stack_idx = 0;
	int total_cnt = 0;
	jit_tmp_t *p_tmp;
	jit_tmp_t tmp;
	jit_bb_t bb = _cur_block(ctx);

	for (total_cnt = 0; fmt[total_cnt] != '\0'; total_cnt++)
		;

	*bb->param_ptr++ = (uint64_t)total_cnt;

	va_start(ap, fmt);
	for (; *fmt != '\0'; fmt++) {
		/* D => w64, d => w32 */
		assert ((*fmt == 'D') || (*fmt == 'd'));

		p_tmp = va_arg(ap, jit_tmp_t *);

		tmp = _jit_new_tmp(ctx, 1, 0, (*fmt == 'D'));

		++cnt;
		*p_tmp = tmp;
		*bb->param_ptr++ = (uint64_t)tmp;
	}
	va_end(ap);

	*bb->opc_ptr++ = JITOP(JITOP_FN_PROLOGUE, JITOP_DW_64, JITOP_DW_64);
	++bb->opc_cnt;
}

void
jit_emit_ret(jit_ctx_t ctx, jit_tmp_t r1)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, r1);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_RET, dw, "r", r1);
}

void
jit_emit_reti(jit_ctx_t ctx, uint64_t imm)
{
	jit_bb_t bb = _cur_block(ctx);

	jit_emitv(ctx, JITOP_RET, JITOP_DW_64, "i", imm);
}
#if 0
static
int
jit_jmp_cc_disp(jit_cmptype_t c)
{
	switch (c) {
	case CMP_GE:
		return 13;
	case CMP_LE:
		return 14;
	case CMP_NE:
		return 5;
	case CMP_EQ:
		return 4;
	case CMP_GT:
		return 15;
	case CMP_LT:
		return 12;
	default:
		return 0;
	}
}

static
void
jit_emit32_at(uint8_t * buf, uint32_t u32)
{
	*buf++ = (u32 >> 0) & 0xff;
	*buf++ = (u32 >> 8) & 0xff;
	*buf++ = (u32 >> 16) & 0xff;
	*buf++ = (u32 >> 24) & 0xff;
}

static
void
jit_emit64_at(uint8_t * buf, uint64_t u64)
{
	*buf++ = (u64 >> 0) & 0xff;
	*buf++ = (u64 >> 8) & 0xff;
	*buf++ = (u64 >> 16) & 0xff;
	*buf++ = (u64 >> 24) & 0xff;
	*buf++ = (u64 >> 32) & 0xff;
	*buf++ = (u64 >> 40) & 0xff;
	*buf++ = (u64 >> 48) & 0xff;
	*buf++ = (u64 >> 56) & 0xff;
}

void
jit_emit8(jit_codebuf_t code, uint8_t u8)
{
	assert(code->code_sz + 1 <= code->buf_sz);

	*code->code_ptr++ = u8;
	code->code_sz += 1;
}

void
jit_emit32(jit_codebuf_t code, uint32_t u32)
{
	assert(code->code_sz + 4 <= code->buf_sz);

	*code->code_ptr++ = (u32 >> 0) & 0xff;
	*code->code_ptr++ = (u32 >> 8) & 0xff;
	*code->code_ptr++ = (u32 >> 16) & 0xff;
	*code->code_ptr++ = (u32 >> 24) & 0xff;
	code->code_sz += 4;
}

void
jit_emit64(jit_codebuf_t code, uint32_t u64)
{
	assert(code->code_sz + 8 <= code->buf_sz);

	*code->code_ptr++ = (u64 >> 0) & 0xff;
	*code->code_ptr++ = (u64 >> 8) & 0xff;
	*code->code_ptr++ = (u64 >> 16) & 0xff;
	*code->code_ptr++ = (u64 >> 24) & 0xff;
	*code->code_ptr++ = (u64 >> 32) & 0xff;
	*code->code_ptr++ = (u64 >> 40) & 0xff;
	*code->code_ptr++ = (u64 >> 48) & 0xff;
	*code->code_ptr++ = (u64 >> 56) & 0xff;
	code->code_sz += 8;
}
#endif

#if 0
static
void
_jit_label_resize(jit_label_t label)
{
	label->max_relocs += 16;
	label->relocs = realloc(label->relocs,
	    label->max_relocs * sizeof(struct jit_reloc));

	assert(label->relocs != NULL);
}

int
jit_label_init(jit_codebuf_t code)
{
	int label_idx;

	if (code->label_count == code->max_labels) {
		code->max_labels += 16;
		code->labels = realloc(code->labels,
		    code->max_labels * sizeof(struct jit_label));

		assert(code->labels != NULL);
	}

	label_idx = code->label_count++;

	code->labels[label_idx].has_target = 0;
	code->labels[label_idx].reloc_count = 0;
	code->labels[label_idx].max_relocs = 0;
	code->labels[label_idx].relocs = NULL;

	_jit_label_resize(&code->labels[label_idx]);

	return label_idx;
}

static
void
jit_label_uninit(jit_codebuf_t code, int label_idx)
{
	jit_label_t label;

	assert(label_idx < code->label_count);
	label = &code->labels[label_idx];

	free(label->relocs);
}

uintptr_t
jit_label_get_target(jit_codebuf_t code, int label_idx)
{
	jit_label_t label;

	assert(label_idx < code->label_count);
	label = &code->labels[label_idx];

	if (label->has_target) {
		return label->target;
	} else {
		return UINTPTR_MAX;
	}
}

static
void
jit_label_add_reloc(jit_codebuf_t code, int label_idx)
{
	jit_label_t label;
	jit_reloc_t reloc;

	assert(label_idx < code->label_count);
	label = &code->labels[label_idx];

	assert(label->reloc_count < label->max_relocs);
	reloc = &label->relocs[label->reloc_count++];

	if (label->reloc_count == label->max_relocs)
		_jit_label_resize(label);

	reloc->code_ptr = code->code_ptr;
}
#endif

#if 0
void
jit_label_set_target(jit_codebuf_t code, int label_idx, uintptr_t abs_dst)
{
	intptr_t disp;
	jit_label_t label;
	jit_reloc_t reloc;
	int i = 0;

	assert(label_idx < code->label_count);
	label = &code->labels[label_idx];

	label->has_target = 1;
	label->target = abs_dst;

	for (i = 0; i < label->reloc_count; i++) {
		reloc = &label->relocs[i];
		disp = abs_dst - (intptr_t) reloc->code_ptr - 4;
		jit_emit32_at(reloc->code_ptr, (uint32_t) disp);
	}
}

void
jit_label_set_target_here(jit_codebuf_t code, int label_idx)
{
	jit_label_set_target(code, label_idx, (uintptr_t) code->code_ptr);
}

void *
jit_codebuf_init(jit_codebuf_t code, size_t sz)
{
	code->code_ptr = mmap(0, sz, PROT_WRITE | PROT_EXEC,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (code->code_ptr == MAP_FAILED)
		return NULL;

	code->start_ptr = code->code_ptr;
	code->buf_sz = sz;
	code->code_sz = 0;

	code->label_count = code->max_labels = 0;
	code->labels = NULL;

	return code;
}

void
jit_codebuf_seek(jit_codebuf_t code, off_t offset, int from_start)
{
	if (from_start) {
		code->code_ptr = code->start_ptr;
		code->code_ptr += offset;
		code->code_sz = offset;
	} else {
		code->code_ptr += offset;
		code->code_sz += offset;
	}
}

void
jit_codebuf_uninit(jit_codebuf_t code)
{
	int i;

	for (i = 0; i < code->label_count; i++)
		jit_label_uninit(code, i);

	free(code->labels);

	munmap(code->start_ptr, code->buf_sz);
}

static
void
jit_emit_opc1_reg_regrm(jit_codebuf_t code, uint8_t opc, uint8_t reg,
    uint8_t rm_reg)
{
	uint8_t modrm;

	reg &= 0x7;
	rm_reg &= 0x7;

	modrm = (MODRM_MOD_RM_REG << 6) | (reg << 3) | (rm_reg);

	jit_emit8(code, opc);
	jit_emit8(code, modrm);
}

void
jit_emit_mov_reg_reg(jit_codebuf_t code, uint8_t dst_reg, uint8_t src_reg)
{
	jit_emit_opc1_reg_regrm(code, OPC_MOV_REG_RM, dst_reg, src_reg);
}

void
jit_emit_mov_reg_imm32(jit_codebuf_t code, uint8_t dst_reg, uint32_t imm)
{
	dst_reg &= 0x7;
	jit_emit8(code, OPC_MOV_REG_IMM32 + dst_reg);
	jit_emit32(code, imm);
}

void
jit_emit_ret(jit_codebuf_t code)
{
	jit_emit8(code, OPC_RET);
}

void
jit_emit_xor_reg_reg(jit_codebuf_t code, uint8_t dst_reg, uint8_t op2_reg)
{
	jit_emit_opc1_reg_regrm(code, OPC_XOR_REG_RM, dst_reg, op2_reg);
}

void
jit_emit_shift_imm8(jit_codebuf_t code, int left, uint8_t rm_reg, int amt)
{
	jit_emit_opc1_reg_regrm(code, OPC_SHIFT_RM_IMM,
	    left ? MODRM_REG_SHL : MODRM_REG_SHR, rm_reg);
	jit_emit8(code, amt & 0xFF);
}

void
jit_emit_cmp_imm32(jit_codebuf_t code, uint8_t rm_reg, uint32_t imm)
{
	if (rm_reg == REG_EAX) {
		jit_emit8(code, OPC_CMP_EAX_IMM32);
	} else {
		jit_emit_opc1_reg_regrm(code, OPC_CMP_RM_IMM32,
		    MODRM_REG_CMP_IMM, rm_reg);
	}
	jit_emit32(code, imm);
}

void
jit_emit_and_imm32(jit_codebuf_t code, uint8_t rm_reg, uint32_t imm)
{
	if (rm_reg == REG_EAX) {
		jit_emit8(code, OPC_AND_EAX_IMM32);
	} else {
		jit_emit_opc1_reg_regrm(code, OPC_AND_RM_IMM32,
		    MODRM_REG_AND_IMM, rm_reg);
	}
	jit_emit32(code, imm);
}

static
void
jit_emit_jmp_disp(jit_codebuf_t code, uintptr_t abs_dst)
{
	intptr_t disp = abs_dst - (intptr_t) code->code_ptr - 4;

	jit_emit32(code, (uint32_t) disp);
}

static
void
jit_emit_reloc(jit_codebuf_t code, int label_idx)
{
	uintptr_t abs_dst;

	if ((abs_dst = jit_label_get_target(code, label_idx)) != UINTPTR_MAX) {
		jit_emit_jmp_disp(code, abs_dst);
	} else {
		jit_label_add_reloc(code, label_idx);
		jit_emit32(code, 0xDEADC0DE);
	}
}

void
jit_emit_jcc(jit_codebuf_t code, jit_cmptype_t cmp, int label_idx)
{
	jit_emit8(code, OPC_JMP1);
	jit_emit8(code, OPC_JMP2_CC + jit_jmp_cc_disp(cmp));
	jit_emit_reloc(code, label_idx);
}

void
jit_emit_jncc(jit_codebuf_t code, jit_cmptype_t cmp, int label_idx)
{
	jit_emit8(code, OPC_JMP1);
	jit_emit8(code, OPC_JMP2_CC + (1^jit_jmp_cc_disp(cmp)));
	jit_emit_reloc(code, label_idx);
}
#endif

static
void
jit_print_bb(jit_ctx_t ctx, jit_bb_t bb)
{
	jit_op_t op;
	int opc_idx;
	int param_idx = 0;
	uint64_t param;
	int iidx;
	int oidx;
	int dw;
	int cc_dw;
	uint64_t imm;
	jit_label_t label;
	jit_tmp_t tmp;
	jit_tmp_state_t ts;
	jit_cc_t cc;
	int arg_cnt;
	jit_op_def_t def;

	for (opc_idx = 0; opc_idx < bb->opc_cnt; opc_idx++) {
		op = JITOP_OP(bb->opcodes[opc_idx]);
		dw = JITOP_DW(bb->opcodes[opc_idx]);
		cc_dw = JITOP_CC_DW(bb->opcodes[opc_idx]);

		switch (op) {
		case JITOP_LDRI:
			break;
		case JITOP_LDRR:
			break;
		case JITOP_LDRBPO:
			break;
		case JITOP_LDRBPSO:
			break;

		case JITOP_STRI:
			break;
		case JITOP_STRR:
			break;
		case JITOP_STRBPO:
			break;
		case JITOP_STRBPSO:
			break;

		case JITOP_FN_PROLOGUE:
			printf("function(");
			arg_cnt = bb->params[param_idx];
			++param_idx;
			for (; arg_cnt > 0; arg_cnt--) {
				tmp = (jit_tmp_t)bb->params[param_idx];
				ts = GET_TMP_STATE(ctx, tmp);
				printf("%c%d", ts->local ? 'l' : 't', ts->id);
				if (arg_cnt > 1)
					printf(", ");
				++param_idx;
			}
			printf(") =\n");
			break;

		case JITOP_SET_LABEL:
			label = (jit_label_t)bb->params[param_idx];
			++param_idx;
			printf("label%d:\n", label->id);
			break;

		default:
			def = &op_def[op];
			printf("\t%s.%-10s", def->mnemonic, (dw == JITOP_DW_8 ) ? "8"  :
			                                    (dw == JITOP_DW_16) ? "16" :
							    (dw == JITOP_DW_32) ? "32" : "64");
			if (def->out_args > 0) {
				printf("\t");
				for (oidx = 0; oidx < def->out_args; oidx++) {
					tmp = bb->params[param_idx];
					ts = GET_TMP_STATE(ctx, tmp);
					printf("%c%d", ts->local ? 'l' : 't', ts->id);
					++param_idx;

					if (oidx != def->out_args-1) {
						printf(", ");
					}
				}

				for (iidx = 0; iidx < def->in_args; iidx++) {
					if (def->out_args != 0 || iidx > 0) {
						printf(", ");
					}

					switch (def->fmt[iidx]) {
					case 'r':
						tmp = (jit_tmp_t)bb->params[param_idx];
						ts = GET_TMP_STATE(ctx, tmp);
						printf("%c%d", ts->local ? 'l' : 't', ts->id);
						break;

					case 'i':
					case 'I':
						imm = (uint64_t)bb->params[param_idx];
						if (dw == 64)
							printf("$%#lx", imm);
						else
							printf("$%#x", (uint32_t)imm);
						break;

					case 'l':
						label = (jit_label_t)bb->params[param_idx];
						printf("label%d", label->id);
						break;

					case 'c':
						cc = (jit_cc_t)bb->params[param_idx];
						printf("%s", (cc == CMP_GE) ? "ge" :
						             (cc == CMP_LE) ? "le" :
							     (cc == CMP_NE) ? "ne" :
							     (cc == CMP_EQ) ? "eq" :
							     (cc == CMP_GT) ? "gt" :
							     (cc == CMP_LT) ? "lt" : "<unknown cc>");
						printf(".%s", (cc_dw == JITOP_DW_8 ) ? "8"  :
			                                      (cc_dw == JITOP_DW_16) ? "16" :
							      (cc_dw == JITOP_DW_32) ? "32" : "64");
						break;
					}
					++param_idx;
				}

			}
			printf("\n");
		}
	}
}

void
jit_print_ir(jit_ctx_t ctx)
{
	int i;

	for (i = 0; i < ctx->block_cnt; i++) {
		jit_print_bb(ctx, &ctx->blocks[i]);
	}
}

static
void
save_temp(jit_ctx_t ctx, jit_tmp_t tmp)
{
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, tmp);
	assert (ts->loc == JITLOC_REG);
	uint64_t tparams[7];

	/* If it's not dirty (and allocated), there's nothing to do */
	if (!ts->dirty && ts->mem_allocated) {
		return;
	}

	/* If it doesn't have any spill location allocated, allocate one */
	if (!ts->mem_allocated) {
		/* XXX: += ? or -=? */
		ts->mem_allocated = 1;
		ts->mem_offset = ctx->spill_stack_offset;
		ts->mem_base_reg = JIT_TGT_STACK_BASE_REG;
		ctx->spill_stack_offset += sizeof(uint64_t);
	}

	if (ts->dirty) {
		tparams[0] = ts->reg;
		tparams[1] = ts->mem_base_reg;
		tparams[2] = ts->mem_offset;

		jit_tgt_emit(ctx, JITOP(JITOP_STRBPO, JITOP_DW_64, JITOP_DW_64),
		    tparams);
	}

	ts->dirty = 0;
}

static
void
spill_temp(jit_ctx_t ctx, jit_tmp_t tmp)
{
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, tmp);

	/* Make sure tmp is actually in a register, and it's not pinned */
	assert (ts->loc == JITLOC_REG);
	assert (jit_regset_test(ctx->regs_used, ts->reg));
	assert (!ts->pinned);

	/* If the variable is not dirty w.r.t. memory, don't spill anything */
	if (!ts->dirty && ts->mem_allocated) {
		jit_regset_clear(ctx->regs_used, ts->reg);
		return;
	}

	save_temp(ctx, tmp);

	ts->loc = JITLOC_STACK;
	ts->dirty = 0;

	jit_regset_clear(ctx->regs_used, ts->reg);
}

static
void
fill_temp(jit_ctx_t ctx, jit_tmp_t tmp, int reg)
{
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, tmp);
	jit_tmp_t tmp_spill;
	jit_tmp_state_t ts_spill;
	uint64_t tparams[7];

	assert (ts->loc == JITLOC_STACK);
	assert (ts->mem_allocated);

	/* If the register is currently in use, spill it */
	if (jit_regset_test(ctx->regs_used, reg)) {
		tmp_spill = ctx->reg_to_tmp[reg];
		ts_spill = GET_TMP_STATE(ctx, tmp_spill);

		assert (ts_spill->loc == JITLOC_REG);
		spill_temp(ctx, tmp_spill);
	}

	/* Make sure the register is unused by now */
	assert (!jit_regset_test(ctx->regs_used, reg));

	tparams[0] = reg;
	tparams[1] = ts->mem_base_reg;
	tparams[2] = ts->mem_offset;

	jit_tgt_emit(ctx, JITOP(JITOP_LDRBPO, JITOP_DW_64, JITOP_DW_64),
	    tparams);

	ts->loc = JITLOC_REG;
	ts->reg = reg;
	ts->dirty = 0;

	jit_regset_set(ctx->regs_used, reg);
}

static
void
expire_regs(jit_ctx_t ctx, jit_bb_t bb)
{
	int reg;
	jit_tmp_t tmp;
	jit_tmp_state_t ts;

	for (reg = 0; reg < 64; reg++) {
		if (!jit_regset_test(ctx->regs_used, reg))
			continue;

		tmp = ctx->reg_to_tmp[reg];
		ts = GET_TMP_STATE(ctx, tmp);

		if (tmp_is_relatively_dead(ctx, bb, tmp, ts->out_scan.generation))
			jit_regset_clear(ctx->regs_used, reg);
	}
}

static
inline
int
_tmp_weight(jit_ctx_t ctx, jit_tmp_t tmp)
{
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, tmp);

	/*
	 * Otherwise, we stick to the following preferences:
	 *   - furthest out next use
	 *   - clean w.r.t. memory
	 */
	/* XXX: take into account next use */
	return !ts->dirty;
}

static
int
allocate_temp_reg(jit_ctx_t ctx, jit_bb_t bb, jit_tmp_t tmp_alloc, jit_regset_t choice_regset, int flags)
{

	int reg;
	jit_tmp_t tmp;
	jit_tmp_state_t ts_alloc = GET_TMP_STATE(ctx, tmp_alloc);
	jit_tmp_state_t ts;
	int max_weight = -1;
	int max_weight_reg = 0;
	int weight;
	int found_empty = 0;

	assert (!jit_regset_is_empty(choice_regset));

	for (reg = 0; reg < 64; reg++) {
		/* If the register is not a valid choice, don't even try */
		if (!jit_regset_test(choice_regset, reg))
			continue;

		/*
		 * If we find an unused register, we are done.
		 */
		if (!jit_regset_test(ctx->regs_used, reg)) {
			found_empty = 1;
			break;
		}

		tmp = ctx->reg_to_tmp[reg];

		weight = _tmp_weight(ctx, tmp);
		if (weight > max_weight) {
			max_weight = weight;
			max_weight_reg = reg;
		}
	}

	if (!found_empty) {
		reg = max_weight_reg;
		tmp = ctx->reg_to_tmp[reg];
		spill_temp(ctx, tmp);
	}

	jit_regset_set(ctx->regs_used, reg);
	ctx->reg_to_tmp[reg] = tmp_alloc;

	return reg;
}

static
void
translate_insn(jit_ctx_t ctx, jit_bb_t bb, int opc_idx, uint32_t opc, uint64_t *params)
{
	jit_op_t op = JITOP_OP(opc);
	int dw = JITOP_DW(opc);
	int cc_dw = JITOP_CC_DW(opc);
	jit_op_def_t def = &op_def[op];
	jit_tgt_op_def_t tgt_def = &tgt_op_def[op];
	int idx;
	int reg, old_reg;
	uint64_t tparams[16];
	uint64_t tparams2[16];
	jit_regset_t current_choice = ctx->overall_choice;
	jit_regset_t restricted_choice, limited_choice;

	jit_tmp_t tmp, tmp1;
	jit_tmp_state_t ts, ts1;

	if (op == JITOP_FN_PROLOGUE) {
		/*
		 * function prologue is completely different from anything else,
		 * so we handle it separately.
		 */
		/* XXX: set up temps themselves, regs_used, reg_to_tmp, etc */
		return;
	}

	assert (sizeof(tparams)/sizeof(uint64_t) >= def->in_args + def->out_args);

	/* XXX: expire temps in regs (dead ones, anyway) - generation needs fixing */
	expire_regs(ctx, bb);

	/* Allocate input argument registers */
	for (idx = def->in_args+def->out_args-1; idx >= def->out_args; idx--) {
		/* -1 to account for terminating \0 */
		if ((sizeof(tgt_def->i_restrict)-1 > idx-def->out_args) &&
		    (tgt_def->i_restrict[idx-def->out_args] != '-')) {
			limited_choice = jit_tgt_reg_restrict(ctx, op, tgt_def->i_restrict[idx-def->out_args]);
		} else {
			jit_regset_full(limited_choice);
		}
		restricted_choice = jit_regset_intersection(current_choice, limited_choice);

		switch (def->fmt[idx-def->out_args]) {
		case 'I':
		case 'l':
		case 'c':
			/* Always ok - immediate will be trimmed if necessary */
			break;

		case 'i':
			/*
			 * Verify that the immediate can be encoded as such on the
			 * target platform for this instruction.
			 * If not, we need to allocate a register for it and change
			 * the opcode to the register variant.
			 */
			if (!jit_tgt_check_imm(ctx, op, idx, params[idx])) {
				/* XXX: Allocate a register and emit a movi */
				/* XXX: data width of movi, XXX: data width of strbpo, ldrbpo */
				/* XXX: pick register, and spill if needed */
				/* XXX: update def, tgt_def after we had to change opcode? */
				op -= 1;
				def = &op_def[op];
				tgt_def = &tgt_op_def[op];
				/* -1 to account for terminating \0 */

				if ((sizeof(tgt_def->i_restrict)-1 > idx-def->out_args) &&
				    (tgt_def->i_restrict[idx-def->out_args] != '-')) {
					limited_choice = jit_tgt_reg_restrict(ctx, op,
					    tgt_def->i_restrict[idx-def->out_args]);
				} else {
					jit_regset_full(limited_choice);
				}

				tmp = _jit_new_tmp(ctx, 0, 1, 1);
				restricted_choice = jit_regset_intersection(current_choice, limited_choice);
				reg = allocate_temp_reg(ctx, bb, tmp, restricted_choice, 0);

				tparams2[0] = reg;
				tparams2[1] = params[idx];
				jit_tgt_emit(ctx, JITOP(JITOP_MOVI, JITOP_DW_64, JITOP_DW_64),
				    tparams2);
				tparams[idx] = reg;

				jit_regset_clear(current_choice, reg);

			} else {
				tparams[idx] = params[idx];
			}
			break;

		case 'r':
			tmp = params[idx];
			ts = GET_TMP_STATE(ctx, tmp);

			tmp_pop_use(ctx, tmp);

			if (ts->loc == JITLOC_STACK) {
				/*
				 * If the temp is in memory, pick a register and fill.
				 */
				reg = allocate_temp_reg(ctx, bb, tmp, restricted_choice, 0);

				fill_temp(ctx, tmp, reg);

				tparams[idx] = reg;
				jit_regset_clear(current_choice, reg);
			} else if (ts->loc == JITLOC_REG) {
				/*
				 * If the temp is in a register, but not a valid one for this
				 * op, pick a valid register and swap the two registers, if
				 * swapping is prefered. Otherwise just spill the valid
				 * register and emit a mov.
				 *
				 * If the temp is in a valid register, we're done.
				 */
				if (!jit_regset_test(restricted_choice, ts->reg)) {
					old_reg = ts->reg;

					reg = allocate_temp_reg(ctx, bb, tmp, restricted_choice, 0);

					tparams2[0] = reg;
					tparams2[1] = old_reg;
					jit_tgt_emit(ctx, JITOP(JITOP_MOV, ts->w64 ? JITOP_DW_64 : JITOP_DW_32,
								JITOP_DW_64), tparams2);

					/* XXX: eventually implement xchg logic to avoid spill */

					tparams[idx] = reg;
					jit_regset_clear(current_choice, reg);
				} else {
					tparams[idx] = ts->reg;
					jit_regset_clear(current_choice, ts->reg);
				}
			} else {
				/* XXX: or just allocate a register anyway */
				assert (0);
			}
			break;

		default:
			assert (0);
			break;
		}
	}

	/*
	 * Allocate output argument registers
	 */
	current_choice = ctx->overall_choice;

	/* XXX: expire temps in regs (dead ones, anyway) - generation needs fixing */
	expire_regs(ctx, bb);

	/* Clear the aliased registers from the available selection */
	for (idx = def->out_args-1; idx >= 0; idx--) {
		if (sizeof(tgt_def->alias) > idx && tgt_def->alias[idx] >= '0') {
			tmp1 = params[def->out_args + (tgt_def->alias[idx] - '0')];
			ts1 = GET_TMP_STATE(ctx, tmp1);
			jit_regset_clear(current_choice, ts1->reg);
		}
	}

	for (idx = def->out_args-1; idx >= 0; idx--) {
		tmp = params[idx];
		ts = GET_TMP_STATE(ctx, tmp);

		if (sizeof(tgt_def->alias) > idx && tgt_def->alias[idx] >= '0') {
			/*
			 * The destination register will have to be the same as one of the
			 * source registers.
			 * If the source temp is still needed after this, spill it to memory
			 * and mark it as in-memory.
			 */
			tmp1 = params[def->out_args + (tgt_def->alias[idx] - '0')];
			ts1 = GET_TMP_STATE(ctx, tmp1);
			reg = ts1->reg;

			tparams[idx] = reg;

			if ((ts1 != ts) &&
			    !tmp_is_relatively_dead(ctx, bb, tmp1, ts1->out_scan.generation)) {
				/*
				 * If the aliased outputs/inputs use different temps, the
				 * source temp needs to be spilled if it's going to be used
				 * again in its current generation.
				 */
				/* XXX: is save_temp what I mean? */
				save_temp(ctx, tmp1);
				ts1->loc = JITLOC_STACK;
			}
		} else {
			/*
			 * Pick a register, possibly spilling its current value to memory,
			 * and use that as output register.
			 */

			reg = allocate_temp_reg(ctx, bb, tmp, current_choice, 0);
			tparams[idx] = reg;
			jit_regset_clear(current_choice, ts->reg);
		}
	}

	/*
	 * Emit actual operation, using the registers we just allocated.
	 */
	opc = JITOP(op, dw, cc_dw);
	jit_tgt_emit(ctx, opc, tparams);
}


static
void
process_bb(jit_ctx_t ctx, jit_bb_t bb) {
	int opc_idx = 0;
	int opparam_idx = 0;
	int cnt;
	int i;
	uint32_t opc;
	jit_op_def_t def;
	jit_tmp_t tmp;
	jit_tmp_state_t ts;

	for (opc_idx = 0; opc_idx < bb->opc_cnt; opc_idx++) {
		opc = bb->opcodes[opc_idx];

		translate_insn(ctx, bb, opc_idx, opc, &bb->params[opparam_idx]);

		switch (JITOP_OP(opc)) {
		case JITOP_FN_PROLOGUE:
			cnt = 1 + bb->params[opparam_idx];
			opparam_idx += cnt;
			break;

		default:
			def = &op_def[JITOP_OP(opc)];
			cnt = def->in_args + def->out_args;

			for (i = 0; i < def->out_args; i++) {
				tmp = bb->params[opparam_idx+i];
				ts = GET_TMP_STATE(ctx, tmp);

				++ts->out_scan.generation;
			}

			opparam_idx += cnt;
			break;
		}
	}
}

void
jit_process(jit_ctx_t ctx)
{
	int i;

	for (i = 0; i < ctx->block_cnt; i++) {
		process_bb(ctx, &ctx->blocks[i]);
	}
}

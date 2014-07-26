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
#include "pmjit-internal.h"
#include "pmjit-tgt.h"


struct jit_op_def const op_def[] = {
	[JITOP_AND]	= { .mnemonic = "and",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 2, .fmt = "rr"    },
	[JITOP_ANDI]	= { .mnemonic = "andi",    .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 2, .fmt = "ri"    },
	[JITOP_OR]	= { .mnemonic = "or",      .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 2, .fmt = "rr"    },
	[JITOP_ORI]	= { .mnemonic = "ori",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 2, .fmt = "ri"    },
	[JITOP_XOR]	= { .mnemonic = "xor",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 2, .fmt = "rr"    },
	[JITOP_XORI]	= { .mnemonic = "xori",    .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 2, .fmt = "ri"    },
	[JITOP_ADD]	= { .mnemonic = "add",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 2, .fmt = "rr"    },
	[JITOP_ADDI]	= { .mnemonic = "addi",    .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 2, .fmt = "ri"    },
	[JITOP_SUB]	= { .mnemonic = "sub",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 2, .fmt = "rr"    },
	[JITOP_SUBI]	= { .mnemonic = "subi",    .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 2, .fmt = "ri"    },
	[JITOP_SHL]	= { .mnemonic = "shl",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 2, .fmt = "rr"    },
	[JITOP_SHLI]	= { .mnemonic = "shli",    .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 2, .fmt = "rI"    },
	[JITOP_SHR]	= { .mnemonic = "shr",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 2, .fmt = "rr"    },
	[JITOP_SHRI]	= { .mnemonic = "shri",    .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 2, .fmt = "rI"    },
	[JITOP_MOV]	= { .mnemonic = "mov",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 1, .fmt = "r"     },
	[JITOP_MOV_SEXT]	= { .mnemonic = "mov.sx",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 1, .fmt = "r"     },
	[JITOP_MOVI]	= { .mnemonic = "movi",    .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 1, .fmt = "i"     },
	[JITOP_NOT]	= { .mnemonic = "not",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 1, .fmt = "r"     },
	[JITOP_BSWAP]	= { .mnemonic = "bswap",   .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 1, .fmt = "r"     },
	[JITOP_CLZ]	= { .mnemonic = "clz",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 1, .fmt = "r"     },
	[JITOP_BFE]	= { .mnemonic = "bfe",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 3, .fmt = "rII"   },
	[JITOP_LDRI]	= { .mnemonic = "ldr",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 1, .fmt = "i"     },
	[JITOP_LDRR]	= { .mnemonic = "ldr",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 1, .fmt = "r"     },
	[JITOP_LDRBPO]	= { .mnemonic = "ldr",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 2, .fmt = "ri"    },
	[JITOP_LDRBPSI]	= { .mnemonic = "ldr",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 3, .fmt = "rri"   },
	[JITOP_LDRI_SEXT]	= { .mnemonic = "ldr.sx",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 1, .fmt = "i"     },
	[JITOP_LDRR_SEXT]	= { .mnemonic = "ldr.sx",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 1, .fmt = "r"     },
	[JITOP_LDRBPO_SEXT]	= { .mnemonic = "ldr.sx",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 2, .fmt = "ri"    },
	[JITOP_LDRBPSI_SEXT]	= { .mnemonic = "ldr.sx",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 3, .fmt = "rri"   },
	[JITOP_STRI]	= { .mnemonic = "str",     .side_effects = 1, .save_locals = SAVE_NORMAL, .out_args = 0, .in_args = 2, .fmt = "ri"    },
	[JITOP_STRR]	= { .mnemonic = "str",     .side_effects = 1, .save_locals = SAVE_NORMAL, .out_args = 0, .in_args = 2, .fmt = "rr"    },
	[JITOP_STRBPO]	= { .mnemonic = "str",     .side_effects = 1, .save_locals = SAVE_NORMAL, .out_args = 0, .in_args = 3, .fmt = "rri"   },
	[JITOP_STRBPSI]	= { .mnemonic = "str",     .side_effects = 1, .save_locals = SAVE_NORMAL, .out_args = 0, .in_args = 4, .fmt = "rrri"  },
	[JITOP_BRANCH]	= { .mnemonic = "b",       .side_effects = 1, .save_locals = SAVE_BEFORE, .out_args = 0, .in_args = 1, .fmt = "l"     },
	[JITOP_BCMP]	= { .mnemonic = "bcmp",    .side_effects = 1, .save_locals = SAVE_BEFORE, .out_args = 0, .in_args = 4, .fmt = "lcrr"  },
	[JITOP_BCMPI]	= { .mnemonic = "bcmpi",   .side_effects = 1, .save_locals = SAVE_BEFORE, .out_args = 0, .in_args = 4, .fmt = "lcri"  },
	[JITOP_BNCMP]	= { .mnemonic = "bncmp",   .side_effects = 1, .save_locals = SAVE_BEFORE, .out_args = 0, .in_args = 4, .fmt = "lcrr"  },
	[JITOP_BNCMPI]	= { .mnemonic = "bncmpi",  .side_effects = 1, .save_locals = SAVE_BEFORE, .out_args = 0, .in_args = 4, .fmt = "lcri"  },
	[JITOP_BTEST]	= { .mnemonic = "btest",   .side_effects = 1, .save_locals = SAVE_BEFORE, .out_args = 0, .in_args = 4, .fmt = "lCrr"   },
	[JITOP_BTESTI]	= { .mnemonic = "btesti",  .side_effects = 1, .save_locals = SAVE_BEFORE, .out_args = 0, .in_args = 4, .fmt = "lCri"   },
	[JITOP_BNTEST]	= { .mnemonic = "bntest",  .side_effects = 1, .save_locals = SAVE_BEFORE, .out_args = 0, .in_args = 4, .fmt = "lCrr"   },
	[JITOP_BNTESTI]	= { .mnemonic = "bntesti", .side_effects = 1, .save_locals = SAVE_BEFORE, .out_args = 0, .in_args = 4, .fmt = "lCri"   },
	[JITOP_RET]	= { .mnemonic = "ret",     .side_effects = 1, .save_locals = SAVE_NEVER,  .out_args = 0, .in_args = 1, .fmt = "r"     },
	[JITOP_RETI]	= { .mnemonic = "reti",    .side_effects = 1, .save_locals = SAVE_NEVER,  .out_args = 0, .in_args = 1, .fmt = "i"     },
	[JITOP_CMOV]	= { .mnemonic = "cmov",    .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 4, .fmt = "rcrr"  },
	[JITOP_CMOVI]	= { .mnemonic = "cmovi",   .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 4, .fmt = "rcri"  },
	[JITOP_CSEL]	= { .mnemonic = "csel",    .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 5, .fmt = "rrcrr" },
	[JITOP_CSELI]	= { .mnemonic = "cseli",   .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 5, .fmt = "rrcri" },
	[JITOP_CSET]	= { .mnemonic = "cset",    .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 3, .fmt = "crr"   },
	[JITOP_CSETI]	= { .mnemonic = "cseti",   .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 3, .fmt = "cri"   },
	[JITOP_TMOV]	= { .mnemonic = "tmov",    .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 4, .fmt = "rCrr"  },
	[JITOP_TMOVI]	= { .mnemonic = "tmovi",   .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 4, .fmt = "rCri"  },
	[JITOP_TSEL]	= { .mnemonic = "tsel",    .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 5, .fmt = "rrCrr" },
	[JITOP_TSELI]	= { .mnemonic = "tseli",   .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 5, .fmt = "rrCri" },
	[JITOP_TSET]	= { .mnemonic = "tset",    .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 3, .fmt = "Crr"   },
	[JITOP_TSETI]	= { .mnemonic = "tseti",   .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 1, .in_args = 3, .fmt = "Cri"   },
	[JITOP_NOP]	= { .mnemonic = "nop",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 0, .in_args = 0, .fmt = "" },
	[JITOP_NOP1]	= { .mnemonic = "nop",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 0, .in_args = 1, .fmt = "i" },
	[JITOP_NOPN]	= { .mnemonic = "nop",     .side_effects = 0, .save_locals = SAVE_NORMAL, .out_args = 0, .in_args = -1, .fmt = "" },
	[JITOP_SET_LABEL] = { .mnemonic = "set_label", .side_effects = 1, .save_locals = SAVE_NEVER, .out_args = 0, .in_args = 1, .fmt = "l" },
	[JITOP_FN_PROLOGUE] = { .mnemonic = "fn_prologue", .side_effects = 1, .save_locals = SAVE_NORMAL, .out_args = -1, .in_args = 0, .fmt = "" },
	[JITOP_CALL] = { .mnemonic = "call", .side_effects = 1, .save_locals = SAVE_NORMAL, .out_args = -1, .in_args = -1, .fmt = "" }
};

static
void
tmp_state_ctor(void *priv, void *elem)
{
	jit_tmp_state_t ts = (jit_tmp_state_t)elem;

	ts->scan.use_head = ts->scan.use_tail = NULL;
}

static
void
tmp_state_dtor(void *priv, void *elem)
{
	jit_tmp_state_t ts = (jit_tmp_state_t)elem;
	/* XXX: free scan stuff, and other dynamic structures */
}

static
void
label_ctor(void *priv, void *elem)
{
	static int id = 0;
	jit_label_t label = (jit_label_t)elem;

	label->id = id++;
	dyn_array_init(&label->relocs, sizeof(struct jit_reloc), 8, NULL, NULL, NULL);
}

static
void
label_dtor(void *priv, void *elem)
{
	jit_label_t label = (jit_label_t)elem;

	dyn_array_free_all(&label->relocs);
}

jit_label_t
jit_new_label(jit_ctx_t ctx)
{
	return dyn_array_new_elem(&ctx->labels);
}

static
void
jit_label_set_target(jit_label_t label, jit_bb_t bb)
{
	label->has_target = 1;
	label->target = bb;
}

static
void
jit_label_add_reloc(jit_label_t label, jit_bb_t bb)
{
	jit_reloc_t reloc;

	reloc = dyn_array_new_elem(&label->relocs);
	reloc->bb = bb;
}

static
void
dump_regs(jit_ctx_t ctx)
{
	int reg;
	jit_tmp_t tmp;
	jit_tmp_state_t ts;
	jit_tmp_use_t use;

	printf("\n===========================\n");
	for (reg = 0; reg < 64; reg++) {
		if (jit_regset_test(ctx->regs_used, reg)) {
			tmp = ctx->reg_to_tmp[reg];
			ts = GET_TMP_STATE(ctx, tmp);

			printf("  %.2d -> %c%d", reg, JIT_TMP_IS_LOCAL(tmp)?'l':'t', ts->id);
			for (use = ts->scan.use_head; use != NULL; use = use->next) {
				printf(" %d{%d}", use->use_idx, use->generation);
			}

			printf(" - gen=%d - loc=%s\n", ts->out_scan.generation, (ts->loc == JITLOC_STACK) ? "stack" :
			    (ts->loc == JITLOC_REG) ? "reg" : "<unknown>");
		}
	}
	printf("===========================\n\n");
}

int
jit_init_codebuf(jit_ctx_t ctx, jit_codebuf_t code)
{
	size_t sz = 8192; /* XXX */

	code->code_ptr = mmap(0, sz, PROT_WRITE | PROT_EXEC,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (code->code_ptr == MAP_FAILED)
		return -1;

	code->emit_ptr = code->map_ptr = code->code_ptr;
	code->map_sz = sz;
	code->code_sz = 0;

	if (ctx != NULL)
		ctx->codebuf = code;

	return 0;
}

static
int
jit_ctx_init(jit_ctx_t ctx)
{
	memset(ctx, 0, sizeof(struct jit_ctx));

	dyn_array_init(&ctx->labels, sizeof(struct jit_label), 4, NULL, label_ctor,
	    label_dtor);
	dyn_array_init(&ctx->local_tmps, sizeof(struct jit_tmp_state), 16, NULL,
	    tmp_state_ctor, tmp_state_dtor);
	dyn_array_init(&ctx->bb_tmps, sizeof(struct jit_tmp_state), 16, NULL,
	    tmp_state_ctor, tmp_state_dtor);

	jit_tgt_ctx_init(ctx);

	return 0;
}

jit_ctx_t
jit_new_ctx(void)
{
	jit_ctx_t ctx;

	if ((ctx = malloc(sizeof(*ctx))) == NULL)
		return NULL;

	jit_ctx_init(ctx);

	return ctx;
}

void
jit_free_ctx(jit_ctx_t ctx)
{
	/* XXX: implement me */
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
	use->bb_id = bb->id;

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
void
tmp_del_use(jit_ctx_t ctx, jit_bb_t bb, jit_tmp_t tmp, int use_idx)
{
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, tmp);
	jit_tmp_use_t use;

	for (use = ts->scan.use_head; use != NULL; use = use->next) {
		if (use->use_idx != use_idx || use->bb_id != bb->id)
			continue;

		if (use->prev == NULL)
			ts->scan.use_head = use->next;
		else
			use->prev->next = use->next;

		if (use->next == NULL)
			ts->scan.use_tail = use->prev;
		else
			use->next->prev = use->prev;

		break;
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

	return ((ts->scan.use_head->generation > rel_generation) ||
		(ts->scan.use_head->bb_id > bb->id));
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
	int idx;

	if (local) {
		tmp_state = dyn_array_new_elem2(&ctx->local_tmps, &idx);
		tmp = JIT_TMP_LOCAL(idx);
	} else {
		tmp_state = dyn_array_new_elem2(&ctx->bb_tmps, &idx);
		tmp = idx;
	}

	memset(tmp_state, 0, sizeof(struct jit_tmp_state));

	tmp_state->local = local;
	tmp_state->constant = constant;
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
void
_add_new_link(jit_bb_links_t links, jit_bb_t bb)
{
	if (links->cnt == links->sz) {
		links->sz += 4;

		links->bbs = realloc(links->bbs,
		    links->sz * sizeof(jit_bb_t));
		assert (links->bbs != NULL);
	}

	links->bbs[links->cnt++] = bb;
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
		bb->id = ctx->block_cnt;
		++ctx->block_cnt;

		bb->in_links.cnt = bb->in_links.sz = 0;
		bb->out_links.cnt = bb->out_links.sz = 0;
		bb->in_links.bbs = bb->out_links.bbs = NULL;
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
	jit_test_cc_t tcc;
	int op_dw = dw;
	int count = 0;
	uint64_t u64;
	jit_tmp_state_t out_ts[4];

	assert ((int)(sizeof(out_ts)/sizeof(jit_tmp_state_t)) >= def->out_args);

	va_start(ap, fmt);
	for (; *fmt != '\0'; fmt++) {
		switch (*fmt) {
		case 'o':
			op_dw = va_arg(ap, int);
			break;
		case 'r':
			t = va_arg(ap, jit_tmp_t);
			ts = GET_TMP_STATE(ctx, t);
			/* XXX: store ts instead? */
			*bb->param_ptr++ = (uint64_t)t;

			if (count >= def->out_args) {
				tmp_add_use(ctx, bb, t, bb->opc_cnt);
			} else {
				out_ts[count] = ts;
			}
			break;
		case 'i':
		case 'I':
			u64 = va_arg(ap, uint64_t);
			*bb->param_ptr++ = (uint64_t)u64;
			break;
		case 'c':
			op_dw = va_arg(ap, int);
			cc = va_arg(ap, jit_cc_t);
			*bb->param_ptr++ = (uint64_t)cc;
			break;
		case 'C':
			op_dw = va_arg(ap, int);
			tcc = va_arg(ap, jit_test_cc_t);
			*bb->param_ptr++ = (uint64_t)tcc;
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

	for (count = 0; count < def->out_args; count++) {
		++out_ts[count]->scan.generation;
	}
	*bb->opc_ptr++ = JITOP(op, dw, op_dw);
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
jit_emit_sub(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, dst);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_SUB, dw, "rrr", dst, r1, r2);
}

void
jit_emit_subi(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, uint64_t imm)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, dst);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_SUBI, ts->w64 ? JITOP_DW_64 : JITOP_DW_32,
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
jit_emit_mov(jit_ctx_t ctx, int width, jit_ext_type_t ext_type, jit_tmp_t dst, jit_tmp_t r1)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, r1);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;
	int op_dw = width;

	jit_emitv(ctx, (ext_type == SEXT) ? JITOP_MOV_SEXT : JITOP_MOV, dw, "orr", op_dw, dst, r1);
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
jit_emit_bfe(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, uint8_t lsb, uint8_t len)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts = GET_TMP_STATE(ctx, r1);
	int dw = ts->w64 ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_BSWAP, dw, "rrII", dst, r1, (uint64_t)lsb, (uint64_t)len);
}

void
jit_emit_ldr_base(jit_ctx_t ctx, int width, jit_ext_type_t ext_type, jit_tmp_t dst, jit_tmp_t base)
{
	jit_emitv(ctx, (ext_type == SEXT) ? JITOP_LDRR_SEXT : JITOP_LDRR,
	    width, "rr", dst, base);
}

void
jit_emit_ldr_imm(jit_ctx_t ctx, int width, jit_ext_type_t ext_type, jit_tmp_t dst, uint64_t imm)
{
	jit_emitv(ctx, (ext_type == SEXT) ? JITOP_LDRI_SEXT : JITOP_LDRI,
	    width, "ri", dst, imm);
}

void
jit_emit_ldr_base_disp(jit_ctx_t ctx, int width, jit_ext_type_t ext_type, jit_tmp_t dst, jit_tmp_t base, uint64_t imm)
{
	jit_emitv(ctx, (ext_type == SEXT) ? JITOP_LDRBPO_SEXT : JITOP_LDRBPO,
	    width, "rri", dst, base, imm);
}

void
jit_emit_ldr_base_si(jit_ctx_t ctx, int width, jit_ext_type_t ext_type, jit_tmp_t dst, jit_tmp_t base, jit_tmp_t index, uint8_t scale)
{
	jit_emitv(ctx, (ext_type == SEXT) ? JITOP_LDRBPSI_SEXT : JITOP_LDRBPSI,
	    width, "rrri", dst, base, index, scale);
}

void
jit_emit_str_base(jit_ctx_t ctx, int width, jit_tmp_t src, jit_tmp_t base)
{
	jit_emitv(ctx, JITOP_STRR, width, "rr", src, base);
}

void
jit_emit_str_imm(jit_ctx_t ctx, int width, jit_tmp_t src, uint64_t imm)
{
	jit_emitv(ctx, JITOP_STRI, width, "ri", src, imm);
}

void
jit_emit_str_base_disp(jit_ctx_t ctx, int width, jit_tmp_t src, jit_tmp_t base, uint64_t imm)
{
	jit_emitv(ctx, JITOP_STRBPO, width, "rri", src, base, imm);
}

void
jit_emit_str_base_si(jit_ctx_t ctx, int width, jit_tmp_t src, jit_tmp_t base, jit_tmp_t index, uint8_t scale)
{
	jit_emitv(ctx, JITOP_STRBPSI, width, "rrri", src, base, index, scale);
}

void
jit_emit_branch(jit_ctx_t ctx, jit_label_t l)
{
	jit_bb_t bb = _cur_block(ctx);

	jit_emitv(ctx, JITOP_BRANCH, JITOP_DW_64, "l", l);

	jit_label_add_reloc(l, bb);
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

	jit_label_add_reloc(l, bb);
	_end_bb(ctx);
}

void
jit_emit_bcmpi(jit_ctx_t ctx, jit_label_t l, jit_cc_t cc, jit_tmp_t r1, uint64_t imm)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	int dw = (ts1->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_BCMP, JITOP_DW_64, "lcri", l, dw, cc, r1, imm);

	jit_label_add_reloc(l, bb);
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

	jit_label_add_reloc(l, bb);
	_end_bb(ctx);
}

void
jit_emit_bncmpi(jit_ctx_t ctx, jit_label_t l, jit_cc_t cc, jit_tmp_t r1, uint64_t imm)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	int dw = (ts1->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_BNCMPI, JITOP_DW_64, "lcri", l, dw, cc, r1, imm);

	jit_label_add_reloc(l, bb);
	_end_bb(ctx);
}

void
jit_emit_btest(jit_ctx_t ctx, jit_label_t l, jit_test_cc_t cc, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	jit_tmp_state_t ts2 = GET_TMP_STATE(ctx, r2);
	int dw = (ts1->w64 && ts2->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_BTEST, dw, "lCrr", l, dw, cc, r1, r2);

	jit_label_add_reloc(l, bb);
	_end_bb(ctx);
}

void
jit_emit_btesti(jit_ctx_t ctx, jit_label_t l, jit_test_cc_t cc, jit_tmp_t r1, uint64_t imm)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	int dw = (ts1->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_BTESTI, dw, "lCri", l, dw, cc, r1, imm);

	jit_label_add_reloc(l, bb);
	_end_bb(ctx);
}

void
jit_emit_bntest(jit_ctx_t ctx, jit_label_t l, jit_test_cc_t cc, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	jit_tmp_state_t ts2 = GET_TMP_STATE(ctx, r2);
	int dw = (ts1->w64 && ts2->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_BNTEST, dw, "lCrr", l, dw, cc, r1, r2);

	jit_label_add_reloc(l, bb);
	_end_bb(ctx);
}

void
jit_emit_bntesti(jit_ctx_t ctx, jit_label_t l, jit_test_cc_t cc, jit_tmp_t r1, uint64_t imm)
{
	jit_bb_t bb = _cur_block(ctx);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	int dw = (ts1->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_BNTESTI, dw, "lCri", l, dw, cc, r1, imm);

	jit_label_add_reloc(l, bb);
	_end_bb(ctx);
}

void
jit_emit_cmov(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src, jit_cc_t cc, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_tmp_state_t ts_dst = GET_TMP_STATE(ctx, dst);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	jit_tmp_state_t ts2 = GET_TMP_STATE(ctx, r2);

	int dw = (ts_dst->w64) ? JITOP_DW_64 : JITOP_DW_32;
	int op_dw = (ts1->w64 && ts2->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_CMOV, dw, "rrcrr", dst, src, op_dw, cc, r1, r2);
}

void
jit_emit_cmovi(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src, jit_cc_t cc, jit_tmp_t r1, uint64_t imm)
{
	jit_tmp_state_t ts_dst = GET_TMP_STATE(ctx, dst);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);

	int dw = (ts_dst->w64) ? JITOP_DW_64 : JITOP_DW_32;
	int op_dw = (ts1->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_CMOVI, dw, "rrcri", dst, src, op_dw, cc, r1, imm);
}

void
jit_emit_csel(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src1, jit_tmp_t src2, jit_cc_t cc, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_tmp_state_t ts_dst = GET_TMP_STATE(ctx, dst);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	jit_tmp_state_t ts2 = GET_TMP_STATE(ctx, r2);

	int dw = (ts_dst->w64) ? JITOP_DW_64 : JITOP_DW_32;
	int op_dw = (ts1->w64 && ts2->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_CSEL, dw, "rrrcrr", dst, src1, src2, op_dw, cc, r1, r2);
}

void
jit_emit_cseli(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src1, jit_tmp_t src2, jit_cc_t cc, jit_tmp_t r1, uint64_t imm)
{
	jit_tmp_state_t ts_dst = GET_TMP_STATE(ctx, dst);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);

	int dw = (ts_dst->w64) ? JITOP_DW_64 : JITOP_DW_32;
	int op_dw = (ts1->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_CSELI, dw, "rrrcri", dst, src1, src2, op_dw, cc, r1, imm);
}

void
jit_emit_cset(jit_ctx_t ctx, jit_tmp_t dst, jit_cc_t cc, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_tmp_state_t ts_dst = GET_TMP_STATE(ctx, dst);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	jit_tmp_state_t ts2 = GET_TMP_STATE(ctx, r2);

	int dw = (ts_dst->w64) ? JITOP_DW_64 : JITOP_DW_32;
	int op_dw = (ts1->w64 && ts2->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_CSET, dw, "rcrr", dst, op_dw, cc, r1, r2);
}

void
jit_emit_cseti(jit_ctx_t ctx, jit_tmp_t dst, jit_cc_t cc, jit_tmp_t r1, uint64_t imm)
{
	jit_tmp_state_t ts_dst = GET_TMP_STATE(ctx, dst);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);

	int dw = (ts_dst->w64) ? JITOP_DW_64 : JITOP_DW_32;
	int op_dw = (ts1->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_CSETI, dw, "rcri", dst, op_dw, cc, r1, imm);
}















void
jit_emit_tmov(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src, jit_test_cc_t cc, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_tmp_state_t ts_dst = GET_TMP_STATE(ctx, dst);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	jit_tmp_state_t ts2 = GET_TMP_STATE(ctx, r2);

	int dw = (ts_dst->w64) ? JITOP_DW_64 : JITOP_DW_32;
	int op_dw = (ts1->w64 && ts2->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_TMOV, dw, "rrCrr", dst, src, op_dw, cc, r1, r2);
}

void
jit_emit_tmovi(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src, jit_test_cc_t cc, jit_tmp_t r1, uint64_t imm)
{
	jit_tmp_state_t ts_dst = GET_TMP_STATE(ctx, dst);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);

	int dw = (ts_dst->w64) ? JITOP_DW_64 : JITOP_DW_32;
	int op_dw = (ts1->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_TMOVI, dw, "rrCri", dst, src, op_dw, cc, r1, imm);
}

void
jit_emit_tsel(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src1, jit_tmp_t src2, jit_test_cc_t cc, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_tmp_state_t ts_dst = GET_TMP_STATE(ctx, dst);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	jit_tmp_state_t ts2 = GET_TMP_STATE(ctx, r2);

	int dw = (ts_dst->w64) ? JITOP_DW_64 : JITOP_DW_32;
	int op_dw = (ts1->w64 && ts2->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_TSEL, dw, "rrrCrr", dst, src1, src2, op_dw, cc, r1, r2);
}

void
jit_emit_tseli(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src1, jit_tmp_t src2, jit_test_cc_t cc, jit_tmp_t r1, uint64_t imm)
{
	jit_tmp_state_t ts_dst = GET_TMP_STATE(ctx, dst);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);

	int dw = (ts_dst->w64) ? JITOP_DW_64 : JITOP_DW_32;
	int op_dw = (ts1->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_TSELI, dw, "rrrCri", dst, src1, src2, op_dw, cc, r1, imm);
}

void
jit_emit_tset(jit_ctx_t ctx, jit_tmp_t dst, jit_test_cc_t cc, jit_tmp_t r1, jit_tmp_t r2)
{
	jit_tmp_state_t ts_dst = GET_TMP_STATE(ctx, dst);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);
	jit_tmp_state_t ts2 = GET_TMP_STATE(ctx, r2);

	int dw = (ts_dst->w64) ? JITOP_DW_64 : JITOP_DW_32;
	int op_dw = (ts1->w64 && ts2->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_TSET, dw, "rCrr", dst, op_dw, cc, r1, r2);
}

void
jit_emit_tseti(jit_ctx_t ctx, jit_tmp_t dst, jit_test_cc_t cc, jit_tmp_t r1, uint64_t imm)
{
	jit_tmp_state_t ts_dst = GET_TMP_STATE(ctx, dst);
	jit_tmp_state_t ts1 = GET_TMP_STATE(ctx, r1);

	int dw = (ts_dst->w64) ? JITOP_DW_64 : JITOP_DW_32;
	int op_dw = (ts1->w64) ? JITOP_DW_64 : JITOP_DW_32;

	jit_emitv(ctx, JITOP_TSETI, dw, "rCri", dst, op_dw, cc, r1, imm);
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
	bb = _cur_block(ctx);

	jit_emitv(ctx, JITOP_SET_LABEL, JITOP_DW_64, "l", l);

	jit_label_set_target(l, bb);
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

	*bb->param_ptr++ = (uint64_t)total_cnt;

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

	_end_bb(ctx);
}

void
jit_emit_reti(jit_ctx_t ctx, uint64_t imm)
{
	jit_bb_t bb = _cur_block(ctx);

	jit_emitv(ctx, JITOP_RETI, JITOP_DW_64, "i", imm);

	_end_bb(ctx);
}

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
	int op_dw;
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
		op_dw = JITOP_OP_DW(bb->opcodes[opc_idx]);

		switch (op) {
		case JITOP_LDRI:
			break;
		case JITOP_LDRR:
			break;
		case JITOP_LDRBPO:
			break;
		case JITOP_LDRBPSI:
			break;

		case JITOP_STRI:
			break;
		case JITOP_STRR:
			break;
		case JITOP_STRBPO:
			break;
		case JITOP_STRBPSI:
			break;

		case JITOP_NOP:
			break;

		case JITOP_NOP1:
			param_idx += 1;
			break;

		case JITOP_NOPN:
			arg_cnt = bb->params[param_idx];
			param_idx += 2 + arg_cnt;
			break;

		case JITOP_FN_PROLOGUE:
			printf("function(");
			arg_cnt = bb->params[param_idx];
			++param_idx;
			for (; arg_cnt > 0; arg_cnt--) {
				tmp = (jit_tmp_t)bb->params[param_idx];
				ts = GET_TMP_STATE(ctx, tmp);
				printf("%c%d", JIT_TMP_IS_LOCAL(tmp) ? 'l' : 't', ts->id);
				if (arg_cnt > 1)
					printf(", ");
				++param_idx;
			}
			++param_idx;
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
					printf("%c%d", JIT_TMP_IS_LOCAL(tmp) ? 'l' : 't', ts->id);
					++param_idx;

					if (oidx != def->out_args-1) {
						printf(", ");
					}
				}
			}
			if (def->in_args > 0) {
				if (def->out_args == 0)
					printf("\t");
				for (iidx = 0; iidx < def->in_args; iidx++) {
					if (def->out_args != 0 || iidx > 0) {
						printf(", ");
					}

					switch (def->fmt[iidx]) {
					case 'r':
						tmp = (jit_tmp_t)bb->params[param_idx];
						ts = GET_TMP_STATE(ctx, tmp);
						printf("%c%d", JIT_TMP_IS_LOCAL(tmp) ? 'l' : 't', ts->id);
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
						printf(".%s", (op_dw == JITOP_DW_8 ) ? "8"  :
			                                      (op_dw == JITOP_DW_16) ? "16" :
							      (op_dw == JITOP_DW_32) ? "32" : "64");
						break;
					case 'C':
						printf("<tst>");
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
		printf("<%d>\n", ctx->blocks[i].id);
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
		ts->mem_base_reg = jit_tgt_stack_base_reg;
		ctx->spill_stack_offset -= sizeof(uint64_t);
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
	assert (ts->mem_allocated || ts->local);

	/* If it doesn't have any spill location allocated, allocate one */
	if (!ts->mem_allocated) {
		/* XXX: += ? or -=? */
		ts->mem_allocated = 1;
		ts->mem_offset = ctx->spill_stack_offset;
		ts->mem_base_reg = jit_tgt_stack_base_reg;
		ctx->spill_stack_offset -= sizeof(uint64_t);
	}

#if 0
	/* XXX: can't really check here, as it'll have been allocated already! */
	/* If the register is currently in use, spill it */
	if (jit_regset_test(ctx->regs_used, reg)) {
		tmp_spill = ctx->reg_to_tmp[reg];
		ts_spill = GET_TMP_STATE(ctx, tmp_spill);

		printf("fill_temp: spill temp: %d (local? %d)\n", ts_spill->id, ts_spill->local);
		assert (ts_spill->loc == JITLOC_REG);
		spill_temp(ctx, tmp_spill);
	}

	/* Make sure the register is unused by now */
	assert (!jit_regset_test(ctx->regs_used, reg));
#endif

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

		if (tmp_is_relatively_dead(ctx, bb, tmp, ts->out_scan.generation)) {
			printf("expire_regs: %c%d\n", JIT_TMP_IS_LOCAL(tmp) ? 'l':'t', ts->id);
			/* If we are expiring a local, make sure it's spilled, if needed */
			if (ts->local) {
				save_temp(ctx, tmp);
				ts->loc = JITLOC_STACK;
			}

			jit_regset_clear(ctx->regs_used, reg);
		}
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
	int max_empty_weight = -1;
	int max_empty_weight_reg = 0;
	int weight;
	int found_empty = 0;

	printf("allocating reg for temp %c%d\n", ts_alloc->local?'l':'t', ts_alloc->id);
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
			weight = jit_tgt_reg_empty_weight(ctx, reg);
			if (weight > max_empty_weight) {
				max_empty_weight = weight;
				max_empty_weight_reg = reg;
			}
			continue;
		}

		tmp = ctx->reg_to_tmp[reg];

		weight = _tmp_weight(ctx, tmp);
		if (weight > max_weight) {
			max_weight = weight;
			max_weight_reg = reg;
		}
	}

#if 1
	if (!found_empty && (flags & CAN_FAIL)) {
		if (max_weight <= _tmp_weight(ctx, tmp_alloc))
			return -1;
	}
#endif

	printf("  -> found_empty? %d\n", found_empty);
	if (found_empty) {
		reg = max_empty_weight_reg;
	} else {
		reg = max_weight_reg;
		tmp = ctx->reg_to_tmp[reg];
		ts = GET_TMP_STATE(ctx, tmp);
		printf("  -> picking and spilling non-empty: reg=%d, tmp=%c%d\n", reg, ts->local?'l':'t', ts->id);
		spill_temp(ctx, tmp);
	}

	jit_regset_set(ctx->regs_used, reg);
	ctx->reg_to_tmp[reg] = tmp_alloc;

	jit_regset_set(ctx->regs_ever_used, reg);

	return reg;
}

static
void
translate_insn(jit_ctx_t ctx, jit_bb_t bb, int opc_idx, uint32_t opc, uint64_t *params)
{
	jit_op_t op = JITOP_OP(opc);
	int dw = JITOP_DW(opc);
	int op_dw = JITOP_OP_DW(opc);
	jit_op_def_t def = &op_def[op];
	jit_tgt_op_def_t tgt_def = &tgt_op_def[op];
	int i, idx;
	int reg, old_reg;
	int cnt;
	int extra_regs = -1;
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
		cnt = params[0];
		jit_tgt_emit_fn_prologue(ctx, cnt, &params[1]);
		return;
	} else if (op == JITOP_NOP || op == JITOP_NOPN || op == JITOP_NOP1) {
		return;
	}

	assert ((int)(sizeof(tparams)/sizeof(uint64_t)) >= def->in_args + def->out_args);

	/* XXX: expire temps in regs (dead ones, anyway) - generation needs fixing */
	expire_regs(ctx, bb);

	if (op == JITOP_CALL) {
		cnt = params[0];

		/*
		 * Get the call info for all the temps; the output
		 * temp is at [2], the other ones come after that.
		 * [1] is the function pointer, [0] is the argument
		 * count including the output register and the
		 * function pointer.
		 */
		jit_tgt_setup_call(ctx, cnt-1, &params[2]);

		/*
		 *  0) mark all temps that are part of the function call
		 *     arguments.
		 */
		for (idx = 3; idx <= cnt; idx++) {
			tmp = params[idx];
			ts = GET_TMP_STATE(ctx, tmp);
			ts->out_scan.mark = 1;
		}

		/*
		 *  1) find in-use caller saved regs that are not in use by
		 *     actual arguments and spill those temps (and mark the
		 *     register as unused).
		 */
		/*
		 *  2) For any argument left in caller-saved registers, save
		 *     the temp if necessary (in its normal save location) -
		 *     i.e. if it is not dead after this op (but don't mark
		 *     register unused).
		 */

		for (reg = 0; reg < 64; reg++) {
			if (!jit_regset_test(ctx->regs_caller_saved, reg))
				continue;
			if (!jit_regset_test(ctx->regs_used, reg))
				continue;

			tmp = ctx->reg_to_tmp[reg];
			ts = GET_TMP_STATE(ctx, tmp);
			if (ts->out_scan.mark) {
				if (!tmp_is_relatively_dead(ctx, bb, tmp, ts->out_scan.generation) ||
				    ts->local)
					save_temp(ctx, tmp);
			} else {
				spill_temp(ctx, tmp);
			}
		}

		/*
		 *  3) go through temps that are still in registers, and write
		 *     them to the stack argument location if they are supposed
		 *     to go onto the stack (and mark the register as unused).
		 */
		for (reg = 0; reg < 64; reg++) {
			if (!jit_regset_test(ctx->regs_used, reg))
				continue;

			tmp = ctx->reg_to_tmp[reg];
			ts = GET_TMP_STATE(ctx, tmp);

			if (ts->out_scan.mark && ts->call_info.loc == JITLOC_STACK) {
				tparams2[0] = reg;
				tparams2[1] = ts->call_info.mem_base_reg;
				tparams2[2] = ts->call_info.mem_offset;

				jit_tgt_emit(ctx, JITOP(JITOP_STRBPO, JITOP_DW_64, JITOP_DW_64),
				    tparams2);

				jit_regset_clear(ctx->regs_used, ts->reg);

				ts->out_scan.mark = 0;
			}
		}
		/*
		 *  4) go through temps in argument registers (which must all
		 *     be arguments by now) and move them into the right register
		 *     if they aren't yet.
		 *     If the right register is in use, try to move the temp in
		 *     that register to its right register. If that's not possible
		 *     (because that destination is also in use), exchange
		 *     registers (either via xchg if the target supports it, or via
		 *     the stack).
		 */
		for (reg = 0; reg < 64; reg++) {
			if (!jit_regset_test(ctx->regs_used, reg))
				continue;

again:
			tmp = ctx->reg_to_tmp[reg];
			ts = GET_TMP_STATE(ctx, tmp);

			/* If not a function argument, forget about it */
			if (!ts->out_scan.mark)
				continue;

			/*
			 * Any argument left in registers must be a register argument
			 * at this point.
			 */
			assert (ts->call_info.loc == JITLOC_REG);

			if (jit_regset_test(ctx->regs_used, ts->call_info.reg)) {
				tmp1 = ctx->reg_to_tmp[ts->call_info.reg];
				ts1 = GET_TMP_STATE(ctx, tmp1);

				/*
				 * If the target register is in use, it must be
				 * in use by an argument - otherwise it would've
				 * been spilled by now.
				 */
				assert (ts1->out_scan.mark);
			}

			if (reg == ts->call_info.reg)
				continue;

			ts->reg = ts->call_info.reg;
			ctx->reg_to_tmp[ts->call_info.reg] = tmp;
			ts->out_scan.mark = 0;

			if (!jit_regset_test(ctx->regs_used, ts->call_info.reg)) {
				/*
				 * If the target register is currently empty,
				 * just mov into it.
				 */
				tparams2[0] = ts->call_info.reg;
				tparams2[1] = reg;
				jit_tgt_emit(ctx, JITOP(JITOP_MOV,
				    ts->w64 ? JITOP_DW_64 : JITOP_DW_32, JITOP_DW_64), tparams2);

				jit_regset_clear(ctx->regs_used, reg);
				jit_regset_set(ctx->regs_used, ts->call_info.reg);
			} else if (jit_tgt_have_xchg && jit_tgt_prefer_xchg) {
				/*
				 * Issuing a series of xchgs can be quite
				 * efficient on an out-of-order machine, where
				 * they just affect register renaming.
				 *
				 * If the target platform prefers xchgs, just
				 * swap things around until everything's in place.
				 */
				tparams2[0] = ts->call_info.reg;
				tparams2[1] = reg;
				jit_tgt_emit(ctx, JITOP(JITOP_XCHG,
				    (ts->w64 || ts1->w64) ? JITOP_DW_64 : JITOP_DW_32, JITOP_DW_64),
				    tparams2);

				ts1->reg = reg;
				ctx->reg_to_tmp[reg] = tmp1;

				goto again;
			} else {
				/*
				 * If the target architecture doesn't want us to
				 * use xchgs, just spill the target register and
				 * mov into it.
				 */
				spill_temp(ctx, tmp1);

				tparams2[0] = ts->call_info.reg;
				tparams2[1] = reg;
				jit_tgt_emit(ctx, JITOP(JITOP_MOV,
				    ts->w64 ? JITOP_DW_64 : JITOP_DW_32, JITOP_DW_64), tparams2);

				jit_regset_clear(ctx->regs_used, reg);
			}
		}

		/*
		 *  5) Go through function argument temps that are currently
		 *     spilled to memory, and emit either a ldr or a ldr +
		 *     str, depending on whether they go into an argument
		 *     register or the stack.
		 */
		for (idx = 3; idx <= cnt; idx++) {
			tmp = params[idx];
			ts = GET_TMP_STATE(ctx, tmp);

			/*
			 * If the temp is already in it's place, the out scan mark
			 * would've been cleared by now - so skip these.
			 */
			if (!ts->out_scan.mark)
				continue;

			ts->out_scan.mark = 0;

			if (ts->call_info.loc == JITLOC_REG) {
				if (ts->loc == JITLOC_CONST) {
					tparams2[0] = ts->call_info.reg;
					tparams2[1] = ts->value;

					jit_tgt_emit(ctx, JITOP(JITOP_MOVI, JITOP_DW_64, JITOP_DW_64),
					    tparams2);
				} else if (ts->loc == JITLOC_STACK) {
					tparams2[0] = ts->call_info.reg;
					tparams2[1] = ts->mem_base_reg;
					tparams2[2] = ts->mem_offset;

					jit_tgt_emit(ctx, JITOP(JITOP_LDRBPO, JITOP_DW_64, JITOP_DW_64),
					    tparams2);
				}
			} else {
				/*
				 * XXX: no reason we use regs_caller_saved as base - might as well
				 *      use all available regs (- argument regs) and rely on it
				 *      picking free registers first.
				 */
				limited_choice = jit_regset_intersection(ctx->regs_caller_saved,
				    jit_regset_invert(ctx->regs_call_arguments));
				reg = allocate_temp_reg(ctx, bb, tmp, limited_choice, 0);
				if ((ts->loc == JITLOC_CONST) && 0 /*tgt_has_str_imm*/) {
					/* XXX */
					continue;
				} else if (ts->loc == JITLOC_CONST) {
					tparams2[0] = reg;
					tparams2[1] = ts->value;

					jit_tgt_emit(ctx, JITOP(JITOP_MOVI, JITOP_DW_64, JITOP_DW_64),
					    tparams2);
				} else if (ts->loc == JITLOC_STACK) {
					tparams2[0] = reg;
					tparams2[1] = ts->mem_base_reg;
					tparams2[2] = ts->mem_offset;

					jit_tgt_emit(ctx, JITOP(JITOP_LDRBPO, JITOP_DW_64, JITOP_DW_64),
					    tparams2);
				}

				tparams2[0] = reg;
				tparams2[1] = ts->call_info.mem_base_reg;
				tparams2[2] = ts->call_info.mem_offset;

				jit_tgt_emit(ctx, JITOP(JITOP_STRBPO, JITOP_DW_64, JITOP_DW_64),
				    tparams2);

				/* Mark the register as unused again */
				jit_regset_clear(ctx->regs_used, reg);
			}
		}

		/* Set up output */
		tmp = params[2];
		ts = GET_TMP_STATE(ctx, tmp);

		ts->loc = JITLOC_REG;
		ts->reg = ts->call_info.reg;
		ts->dirty = 1;
		ctx->reg_to_tmp[ts->reg] = tmp;
		jit_regset_set(ctx->regs_used, ts->reg);
		jit_regset_set(ctx->regs_ever_used, ts->reg);

		jit_tgt_emit_call(ctx, cnt, &params[1]);

		return;
	}

	/* Allocate input argument registers */
	for (idx = def->in_args+def->out_args-1; idx >= def->out_args; idx--) {

		if (((int)strlen(tgt_def->i_restrict) > idx-def->out_args) &&
		    (tgt_def->i_restrict[idx-def->out_args] != '-')) {
			limited_choice = jit_tgt_reg_restrict(ctx, op, tgt_def->i_restrict[idx-def->out_args]);
		} else {
			jit_regset_full(limited_choice);
		}
		restricted_choice = jit_regset_intersection(current_choice, limited_choice);
		printf("current_choice:    %"PRIx64"\n", current_choice);
		printf("limited_choice:    %"PRIx64"\n", limited_choice);
		printf("restricted_choice: %"PRIx64"\n", restricted_choice);
		printf("input index: %d\n", idx-def->out_args);

		switch (def->fmt[idx-def->out_args]) {
		case 'I':
		case 'l':
		case 'c':
		case 'C':
			tparams[idx] = params[idx];
			/* Always ok - immediate will be trimmed if necessary */
			break;

		case 'i':
			/*
			 * Verify that the immediate can be encoded as such on the
			 * target platform for this instruction.
			 * If not, we need to allocate a register for it and change
			 * the opcode to the register variant.
			 */
			if (!jit_tgt_check_imm(ctx, op, dw, op_dw, idx, params[idx])) {
				/* XXX: Allocate a register and emit a movi */
				/* XXX: data width of movi, XXX: data width of strbpo, ldrbpo */
				/* XXX: pick register, and spill if needed */
				/* XXX: update def, tgt_def after we had to change opcode? */
				op -= 1;
				def = &op_def[op];
				tgt_def = &tgt_op_def[op];
				/* -1 to account for terminating \0 */

				if (((int)strlen(tgt_def->i_restrict) > idx-def->out_args) &&
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

			printf(" -- dealing with temp: %c%d (loc: %s)\n", JIT_TMP_IS_LOCAL(tmp) ? 'l' : 't', ts->id,
			       (ts->loc == JITLOC_STACK) ? "stack" : (ts->loc == JITLOC_REG) ? "reg" : "<unknown>");
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
					printf("restricted_choice: %"PRIx64", ts->reg: %d\n", restricted_choice, ts->reg);
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
					jit_regset_set(ctx->regs_ever_used, ts->reg);
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

	if (tgt_def->check_needed) {
		extra_regs = jit_tgt_feature_check(ctx, op);
		idx = def->in_args + def->out_args;
		for (i = 0; i < extra_regs; i++, idx++) {
			jit_regset_full(limited_choice);
			restricted_choice = jit_regset_intersection(current_choice, limited_choice);
			tmp = _jit_new_tmp(ctx, 0, 0, 1);
			reg = allocate_temp_reg(ctx, bb, tmp, restricted_choice, 0);
			tparams[idx] = reg;
			jit_regset_clear(current_choice, reg);
		}
	}

	if (extra_regs == -1) {
		/*
		 * Allocate output argument registers
		 */
		current_choice = ctx->overall_choice;

		expire_regs(ctx, bb);
	}

	/* Clear the aliased registers from the available selection */
	for (idx = def->out_args-1; idx >= 0; idx--) {
		if ((int)strlen(tgt_def->alias) > idx && tgt_def->alias[idx] >= '0') {
			tmp1 = params[def->out_args + (tgt_def->alias[idx] - '0')];
			ts1 = GET_TMP_STATE(ctx, tmp1);
			jit_regset_clear(current_choice, ts1->reg);
		}
	}

	for (idx = def->out_args-1; idx >= 0; idx--) {
		tmp = params[idx];
		ts = GET_TMP_STATE(ctx, tmp);

		/*
		 * If extra registers are needed, this implies a non-optimal
		 * software emulation of an instruction, so we ignore aliases
		 * and allocate as many registers as required.
		 */
		if ((int)sizeof(tgt_def->alias) > idx && tgt_def->alias[idx] >= '0' &&
		    (extra_regs == -1)) {
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

			if ((ts1 != ts) && (ts1->loc == JITLOC_REG) &&
			    (!tmp_is_relatively_dead(ctx, bb, tmp1, ts1->out_scan.generation) ||
			     ts1->local)) {
				int reg_move = -1;
				/*
				 * Second-chance register allocation for the aliased case.
				 */
				if (!tmp_is_relatively_dead(ctx, bb, tmp1, ts1->out_scan.generation)) {
					/* allocate something from current_choice */
					reg_move = allocate_temp_reg(ctx, bb, tmp1, current_choice, CAN_FAIL);

				}

				if (reg_move >= 0) {
					tparams2[0] = reg_move;
					tparams2[1] = ts1->reg;
					jit_tgt_emit(ctx, JITOP(JITOP_MOV, ts1->w64 ? JITOP_DW_64 : JITOP_DW_32,
								JITOP_DW_64), tparams2);

					ts1->reg = reg_move;
				} else {
					/*
					 * If the aliased outputs/inputs use different temps, the
					 * source temp needs to be spilled if it's going to be used
					 * again in its current generation.
					 */
					save_temp(ctx, tmp1);
					ts1->loc = JITLOC_STACK;
				}
			}

			ctx->reg_to_tmp[reg] = tmp;
			jit_regset_set(ctx->regs_used, reg);
		} else {
			/*
			 * Pick a register, possibly spilling its current value to memory,
			 * and use that as output register.
			 */

			/* XXX: implement simple hinting for the non-aliased case at least */
			reg = allocate_temp_reg(ctx, bb, tmp, current_choice, 0);
			tparams[idx] = reg;
			jit_regset_clear(current_choice, ts->reg);
		}

		ts->loc = JITLOC_REG;
		ts->reg = reg;
		ts->dirty = 1;

		jit_regset_set(ctx->regs_ever_used, reg);
	}

	/*
	 * Emit actual operation, using the registers we just allocated.
	 */
	opc = JITOP(op, dw, op_dw);
	jit_tgt_emit(ctx, opc, tparams);
}


static
void
bb_remove_dead(jit_ctx_t ctx, jit_bb_t bb) {
	const char *fmt;
	int opc_idx = 0;
	int opparam_idx = (bb->param_ptr - bb->params);
	int cnt;
	int i;
	int dead;
	uint32_t opc;
	jit_op_def_t def;
	jit_tmp_t tmp;
	jit_tmp_state_t ts;

	for (opc_idx = bb->opc_cnt-1; opc_idx >= 0; opc_idx--) {
		opc = bb->opcodes[opc_idx];
		def = &op_def[JITOP_OP(opc)];
		fmt = def->fmt;

		cnt = def->in_args + def->out_args;
		if (def->in_args < 0 || def->out_args < 0) {
			cnt = bb->params[opparam_idx-1]+2;
		}

		opparam_idx -= cnt;

		switch (JITOP_OP(opc)) {
		case JITOP_FN_PROLOGUE:
		case JITOP_NOPN:
			dead = 0;
			break;

		case JITOP_NOP1:
			dead = 0;
			break;

		case JITOP_NOP:
			dead = 0;
			break;

		default:
			dead = !def->side_effects;
			for (i = 0; i < def->out_args; i++) {
				tmp = bb->params[opparam_idx+i];
				ts = GET_TMP_STATE(ctx, tmp);

				/*
				 * If the temp is not a local temp, and it's relatively dead,
				 * then this result is dead.
				 */
				if (ts->local || ts->out_scan.mark)
					dead = 0;

				ts->out_scan.mark = 0;
			}

			for (i = 0; i < def->in_args; i++) {
				if (fmt[i] == 'r') {
					tmp = bb->params[opparam_idx+def->out_args+i];
					ts = GET_TMP_STATE(ctx, tmp);
					if (dead) {
						tmp_del_use(ctx, bb, tmp, opc_idx);
					} else {
						ts->out_scan.mark = 1;
					}
				}
			}

			break;
		}

		if (dead) {
			/* XXX: remove use from used temps if dead! */
			if (cnt == 0) {
				bb->opcodes[opc_idx] = JITOP(JITOP_NOP, JITOP_DW_64, JITOP_DW_64);
			} else if (cnt == 1) {
				bb->opcodes[opc_idx] = JITOP(JITOP_NOP1, JITOP_DW_64, JITOP_DW_64);
			} else {
				bb->opcodes[opc_idx] = JITOP(JITOP_NOPN, JITOP_DW_64, JITOP_DW_64);
				bb->params[opparam_idx] = cnt-2;
				bb->params[opparam_idx+cnt-1] = cnt-2;
			}
		}
	}
}

static
void
save_spill_locals(jit_ctx_t ctx, int only_save)
{
	int reg;
	jit_tmp_t tmp;
	jit_tmp_state_t ts;

	for (reg = 0; reg < 64; reg++) {
		if (!jit_regset_test(ctx->regs_used, reg))
			continue;

		tmp = ctx->reg_to_tmp[reg];
		ts = GET_TMP_STATE(ctx, tmp);

		if (!ts->local)
			continue;

		if (only_save)
			save_temp(ctx, tmp);
		else
			spill_temp(ctx, tmp);
	}
}

static
void
process_bb(jit_ctx_t ctx, jit_bb_t bb) {
	int opc_idx = 0;
	int opparam_idx = 0;
	int cnt;
	int i;
	int last;
	uint32_t opc;
	jit_op_def_t def;
	jit_tmp_t tmp;
	jit_tmp_state_t ts;
	jit_op_t op;

	for (opc_idx = 0; opc_idx < bb->opc_cnt; opc_idx++) {
		opc = bb->opcodes[opc_idx];
		op = JITOP_OP(opc);
		def = &op_def[op];

		last = (opc_idx == bb->opc_cnt-1);

		/*
		 * If it is the last opcode, we need to save all
		 * local temps if it is a branch.
		 */
		if (last && (def->save_locals == SAVE_BEFORE)) {
			save_spill_locals(ctx, 1);
		}

		printf("translate_insn: %.3d\n", opc_idx);

		/*
		 * XXX:
		 *  1) save all local temps before last instruction (if branch)
		 *     or after last instruction (if not a branch, ret, etc)
		 *     (but make sure any local temp being used as an input can
		 *      be used safely - i.e. it stays in a register as well, and
		 *      is filled if needed)
		 * 2) clear all currently allocated registers.
		 */
		translate_insn(ctx, bb, opc_idx, opc, &bb->params[opparam_idx]);

		if (last && (def->save_locals == SAVE_NORMAL)) {
			save_spill_locals(ctx, 0);
		}

		switch (JITOP_OP(opc)) {
		case JITOP_FN_PROLOGUE:
		case JITOP_NOPN:
		case JITOP_CALL:
			cnt = 2 + bb->params[opparam_idx];
			opparam_idx += cnt;
			break;

		case JITOP_NOP1:
			opparam_idx += 1;
			break;

		default:
			cnt = def->in_args + def->out_args;

			for (i = 0; i < def->out_args; i++) {
				tmp = bb->params[opparam_idx+i];
				ts = GET_TMP_STATE(ctx, tmp);

				++ts->out_scan.generation;
			}

			opparam_idx += cnt;
			break;
		}

		printf("DUMP: %.3d\n", opc_idx);
		dump_regs(ctx);
	}
}

static
void
zero_scan(jit_ctx_t ctx)
{
	jit_tmp_state_t ts;
	int i;

	dyn_array_foreach(&ctx->local_tmps, ts) {
		memset(&ts->out_scan, 0, sizeof(ts->out_scan));
	}

	dyn_array_foreach(&ctx->bb_tmps, ts) {
		memset(&ts->out_scan, 0, sizeof(ts->out_scan));
	}
}

static
void
downgrade_locals(jit_ctx_t ctx)
{
	jit_tmp_state_t ts;
	int i;

	dyn_array_foreach(&ctx->local_tmps, ts) {
		ts->local = 0;
	}
}

void
jit_optimize(jit_ctx_t ctx)
{
	int i;

	if (ctx->block_cnt == 1) {
		downgrade_locals(ctx);
	}

	for (i = 0; i < ctx->block_cnt; i++) {
		zero_scan(ctx);
		bb_remove_dead(ctx, &ctx->blocks[i]);
	}

	/* XXX: rest of optimizations... */
}

void
jit_process(jit_ctx_t ctx)
{
	int i;

	zero_scan(ctx);

	for (i = 0; i < ctx->block_cnt; i++) {
		process_bb(ctx, &ctx->blocks[i]);
		jit_regset_empty(ctx->regs_used);
	}

	jit_tgt_ctx_finish_emit(ctx);
}

void
jit_resolve_links(jit_ctx_t ctx)
{
	int i, j;
	int last_block = ctx->block_cnt-1;
	jit_bb_t bb = NULL;
	jit_bb_t bb_prev = NULL;
	jit_label_t label;
	jit_reloc_t reloc;

	for (i = 0; i < ctx->block_cnt; i++) {
		bb = &ctx->blocks[i];

		if (bb->opc_cnt == 0)
			continue;

		if (bb_prev) {
			if (bb_prev->opc_cnt >= 1 && bb_prev->opc_ptr[-1] != JITOP_BRANCH && bb_prev->opc_ptr[-1] != JITOP_RET && bb_prev->opc_ptr[-1] != JITOP_RETI) {
				_add_new_link(&bb->in_links, bb_prev);
				_add_new_link(&bb_prev->out_links, bb);
			}
		}

		bb_prev = bb;
	}

	dyn_array_foreach(&ctx->labels, label) {
		if (!dyn_array_is_empty(&label->relocs) && !label->has_target) {
			printf("WARNING: label with relocations but without target found!\n");
			continue;
		}

		bb = label->target;

		dyn_array_foreach(&label->relocs, reloc) {
			_add_new_link(&bb->in_links, reloc->bb);
			_add_new_link(&reloc->bb->out_links, bb);
		}
	}
}

int
jit_output_cfg(jit_ctx_t ctx, const char *file)
{
	int i, j;
	jit_bb_t bb, linked_bb;
	FILE *fp;

	fp = fopen(file, "w");
	if (fp == NULL)
		return -1;

	fprintf(fp, "digraph CFG {\n");
	for (i = 0; i < ctx->block_cnt; i++) {
		bb = &ctx->blocks[i];

		if (bb->out_links.cnt > 0) {
			for (j = 0; j < bb->out_links.cnt; j++) {
				linked_bb = bb->out_links.bbs[j];
				fprintf(fp, "\t%d -> %d;\n", bb->id, linked_bb->id);
			}
		}
	}
	fprintf(fp, "}\n");

	fclose(fp);
	return 0;
}

void
jit_init(void)
{
	jit_tgt_init();
}

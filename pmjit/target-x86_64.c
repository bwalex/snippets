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

typedef enum {
	REG_RAX = 0,
	REG_RCX,
	REG_RDX,
	REG_RBX,
	REG_RSP,
	REG_RBP,
	REG_RSI,
	REG_RDI,
	REG_R8,
	REG_R9,
	REG_R10,
	REG_R11,
	REG_R12,
	REG_R13,
	REG_R14,
	REG_R15
} x86_reg;

const char *reg_to_name[] = {
	[REG_RAX]	= "rax",
	[REG_RCX]	= "rcx",
	[REG_RDX]	= "rdx",
	[REG_RBX]	= "rbx",
	[REG_RSP]	= "rsp",
	[REG_RBP]	= "rbp",
	[REG_RSI]	= "rsi",
	[REG_RDI]	= "rdi",
	[REG_R8]	= "r8 ",
	[REG_R9]	= "r9 ",
	[REG_R10]	= "r10",
	[REG_R11]	= "r11",
	[REG_R12]	= "r12",
	[REG_R13]	= "r13",
	[REG_R14]	= "r14",
	[REG_R15]	= "r15"
};

#ifdef JIT_FPO
const int jit_tgt_stack_base_reg = REG_RSP;
#else
const int jit_tgt_stack_base_reg = REG_RBP;
#endif

struct jit_tgt_op_def const tgt_op_def[] = {
	[JITOP_AND]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	/* AND r/m32/64, r32/64 (and vice versa) */

	[JITOP_ANDI]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	/* AND r/m32/64, imm32 XXX */

	[JITOP_OR]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	/* OR r/m32/64, r32/64 (and vice versa) */

	[JITOP_ORI]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	/* OR r/m32/64, imm32 XXX */

	[JITOP_XOR]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	/* XOR r/m32/64, r32/64 (and vice versa) */

	[JITOP_XORI]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	/* XOR r/m32/64, imm32 XXX */

	[JITOP_ADD]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	/* LEA r32/64, m XXX */
	/* ADD r/m32/64, r32/64 (and vice versa) */

	[JITOP_ADDI]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	/* LEA r32/64, m XXX */
	/* ADD r/m32/64, imm32 XXX */

	[JITOP_NOT]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	/* NOT r/m32/64 */

	[JITOP_SHL]	= { .alias = "0", .o_restrict = "", .i_restrict = "-c" },
	/* SHL r/m32/64, cl */

	[JITOP_SHLI]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	/* SHL r/m32/64, imm8 */

	[JITOP_SHR]	= { .alias = "0", .o_restrict = "", .i_restrict = "-c" },
	/* SHR r/m32/64, cl */

	[JITOP_SHRI]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	/* SHR r/m32/64, imm8 */

	[JITOP_MOV]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	/* MOV r/m32/64, r32/64 (and vice versa) */

	[JITOP_MOVI]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	/* MOV r32/64, imm32/64 */

	[JITOP_LDRR]	= { .alias = "", .o_restrict = "", .i_restrict = "" },

	[JITOP_LDRBPO]	= { .alias = "", .o_restrict = "", .i_restrict = "" },

	[JITOP_LDRBPSO]	= { .alias = "", .o_restrict = "", .i_restrict = "" },

	[JITOP_STRI]	= { .alias = "", .o_restrict = "", .i_restrict = "" },

	[JITOP_STRR]	= { .alias = "", .o_restrict = "", .i_restrict = "" },

	[JITOP_STRBPO]	= { .alias = "", .o_restrict = "", .i_restrict = "" },

	[JITOP_STRBPSO]	= { .alias = "", .o_restrict = "", .i_restrict = "" },

	[JITOP_BRANCH]	= { .alias = "", .o_restrict = "", .i_restrict = "" },

	[JITOP_BCMP]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	/* CMP r/m32/64, r32/64 (and vice versa) */

	[JITOP_BCMPI]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	/* CMP r/m32/64, imm32 XXX */

	[JITOP_BNCMP]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	/* CMP r/m32/64, r32/64 (and vice versa) */

	[JITOP_BNCMPI]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	/* CMP r/m32/64, imm32 XXX */

	[JITOP_BTEST]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	/* CMP r/m32/64, r32/64 (and vice versa) */

	[JITOP_BTESTI]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	/* CMP r/m32/64, imm32/64 */

	[JITOP_BNTEST]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	/* CMP r/m32/64, r32/64 (and vice versa) */

	[JITOP_BNTESTI]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	/* CMP r/m32/64, imm32/64 */

	[JITOP_SET_LABEL] = { .alias = "", .o_restrict = "", .i_restrict = "" },

	[JITOP_RET]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	/* MOV + RETN */

	[JITOP_RETI]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	/* MOV + RETN */

	[JITOP_BSWAP]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	/* BSWAP r32/64 */

	[JITOP_CLZ]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	/* BSF/BSR r32/64, r/m32/64 */
};

static
int
check_pc_rel32s(jit_ctx_t ctx, uint64_t imm)
{
	/* XXX */
	return 1;
}

int
jit_tgt_check_imm(jit_ctx_t ctx, jit_op_t op, int argidx, uint64_t imm)
{
	/* XXX */
	if (op == JITOP_MOVI)
		return 1;
	else if (op == JITOP_LDRI || op == JITOP_STRI)
		return check_pc_rel32s(ctx, imm);
	else if (/* XXX */0)
		return 1;
	else
		return 0;
}

jit_regset_t
jit_tgt_reg_restrict(jit_ctx_t ctx, jit_op_t op, char constraint)
{
	jit_regset_t rs;

	jit_regset_empty(rs);

	switch (constraint) {
	case 'c':
		jit_regset_set(rs, REG_RCX);
		break;

	default:
		printf("constraint: %c\n", constraint);
		assert (0);
		break;
	}

	return rs;
}
/* XXX: how does OP r/m64, imm32 work? is imm32 zero-extended? */

void
jit_tgt_emit(jit_ctx_t ctx, uint32_t opc, uint64_t *params)
{
	jit_op_t op = JITOP_OP(opc);
	int dw = JITOP_DW(opc);
	int cc_dw = JITOP_CC_DW(opc);

	printf("cg:\t");
	switch (op) {
	case JITOP_MOV:
		printf("%s.%d\t%s, %s\n", op_def[op].mnemonic, dw, reg_to_name[params[0]], reg_to_name[params[1]]);
		break;

	case JITOP_MOVI:
		printf("%s.%d\t%s, $%"PRIx64"\n", op_def[op].mnemonic, dw, reg_to_name[params[0]], params[1]);
		break;

	case JITOP_OR:
	case JITOP_AND:
	case JITOP_XOR:
	case JITOP_ADD:
		printf("%s.%d\t%s, %s\n", op_def[op].mnemonic, dw, reg_to_name[params[1]], reg_to_name[params[2]]);
		//printf("%s.%d\t%s, %s, %s\n", op_def[op].mnemonic, dw, reg_to_name[params[0]], reg_to_name[params[1]], reg_to_name[params[2]]);
		assert(params[0] == params[1]);
		break;

	case JITOP_ORI:
	case JITOP_ANDI:
	case JITOP_XORI:
	case JITOP_ADDI:
		printf("%s.%d\t%s, $%"PRIx64"\n", op_def[op].mnemonic, dw, reg_to_name[params[1]], params[2]);
		//printf("%s.%d\t%s, %s, $%"PRIx64"\n", op_def[op].mnemonic, dw, reg_to_name[params[0]], reg_to_name[params[1]], params[2]);
		assert(params[0] == params[1]);
		break;

	case JITOP_LDRBPO:
		printf("%s.%d\t%s, [%s + $%"PRId64"]\n", op_def[op].mnemonic, dw, reg_to_name[params[0]], reg_to_name[params[1]], params[2]);
		break;

	case JITOP_STRBPO:
		printf("%s.%d\t%s, [%s + $%"PRId64"]\n", op_def[op].mnemonic, dw, reg_to_name[params[0]], reg_to_name[params[1]], params[2]);
		break;

	default:
		printf("%s.%d\n", op_def[op].mnemonic, dw);
		break;
	}
}


void
jit_tgt_emit_fn_prologue(jit_ctx_t ctx, int cnt, uint64_t *params)
{
	jit_tmp_t tmp;
	jit_tmp_state_t ts;
	int i;

	for (i = 0; i < cnt; i++) {
		tmp = (jit_tmp_t)params[i];
		ts = GET_TMP_STATE(ctx, tmp);

		/* XXX: do properly... */
#if 0
		ts->loc = JITLOC_STACK;
		ts->mem_allocated = 1;
		ts->mem_base_reg = jit_tgt_stack_base_reg;
		ts->mem_offset = -i*sizeof(uint64_t);
#endif
#if 1
		ts->loc = JITLOC_REG;
		ts->mem_allocated = 0;
		ts->reg = i;
		ts->dirty = 1;
		jit_regset_set(ctx->regs_used, i);
		ctx->reg_to_tmp[i] = tmp;
#endif
	}
}


void
jit_tgt_ctx_init(jit_ctx_t ctx)
{
	jit_regset_empty(ctx->overall_choice);
	jit_regset_set(ctx->overall_choice, REG_RAX);
	jit_regset_set(ctx->overall_choice, REG_RCX);
#if 1
	jit_regset_set(ctx->overall_choice, REG_RDX);
	jit_regset_set(ctx->overall_choice, REG_RBX);
#ifdef JIT_FPO
	jit_regset_set(ctx->overall_choice, REG_RBP);
#endif
	jit_regset_set(ctx->overall_choice, REG_RSI);
	jit_regset_set(ctx->overall_choice, REG_RDI);
	jit_regset_set(ctx->overall_choice, REG_R8);
	jit_regset_set(ctx->overall_choice, REG_R9);
	jit_regset_set(ctx->overall_choice, REG_R10);
	jit_regset_set(ctx->overall_choice, REG_R11);
	jit_regset_set(ctx->overall_choice, REG_R12);
	jit_regset_set(ctx->overall_choice, REG_R13);
	jit_regset_set(ctx->overall_choice, REG_R14);
	jit_regset_set(ctx->overall_choice, REG_R15);
#endif
}


void
jit_tgt_init(void)
{
	/* XXX: do cpuid or whatever to detect supported features */
	return;
}

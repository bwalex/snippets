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

struct jit_tgt_op_def tgt_op_def[] = {
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
		assert (0);
		break;
	}

	return rs;
}
/* XXX: how does OP r/m64, imm32 work? is imm32 zero-extended? */

void
jit_tgt_emit(jit_ctx_t ctx, uint32_t opc, uint64_t *params)
{
}

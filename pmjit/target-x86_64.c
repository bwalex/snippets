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

#define MAX_PROLOGUE_SZ		30
#define MAX_EPILOGUE_SZ		30
#define NO_REG ((uint8_t)-1)

#define OPC_MOV_REG_RM		0x8B
#define OPC_MOV_RM_REG		0x89
#define OPC_MOV_REG_IMM32	0xB8
#define OPC_MOV_RM_IMM32	0xC7

#define OPC_XOR_REG_RM		0x33
#define OPC_XOR_RM_REG		0x31
#define OPC_XOR_RM_IMM32	0x81
#define OPC_XOR_EAX_IMM32	0x35

#define OPC_AND_REG_RM		0x23
#define OPC_AND_RM_REG		0x21
#define OPC_AND_RM_IMM32	0x81
#define OPC_AND_EAX_IMM32	0x25

#define OPC_OR_REG_RM		0x0B
#define OPC_OR_RM_REG		0x09
#define OPC_OR_RM_IMM32		0x81
#define OPC_OR_EAX_IMM32	0x0D

#define OPC_ADD_REG_RM		0x03
#define OPC_ADD_RM_REG		0x01
#define OPC_ADD_RM_IMM32	0x81
#define OPC_ADD_EAX_IMM32	0x05
#define OPC_ADD_RM_IMM8		0x83

#define OPC_SUB_REG_RM		0x2B
#define OPC_SUB_RM_REG		0x29
#define OPC_SUB_RM_IMM32	0x81
#define OPC_SUB_EAX_IMM32	0x2D
#define OPC_SUB_RM_IMM8		0x83



#define OPC_CMP_REG_RM		0x3B
#define OPC_CMP_RM_REG		0x39
#define OPC_CMP_RM_IMM32	0x81
#define OPC_CMP_EAX_IMM32	0x3D

#define OPC_NOT_RM		0xF7

#define OPC_SHIFT_RM_IMM	0xC1

#define OPC_JMP1		0x0F
#define OPC_JMP2_CC		0x80

#define OPC_RET			0xC3

#define OPC_PUSH_REG		0x50
#define OPC_POP_REG		0x58
#define OPC_ENTER		0xC8
#define OPC_LEAVE		0xC9

#define OPC_SHIFT_RM_CL		0xD3
#define OPC_TEST_RM_IMM32	0xF7
#define MODRM_REG_TEST_IMM	0x00 /* XXX: or 0x01 - what do they mean? */
#define OPC_TEST_RM_IMM64	0xF7


/* ModR/M mod field values */
#define MODRM_MOD_DISP0		0x0
#define MODRM_MOD_DISP1B	0x1
#define MODRM_MOD_DISP4B	0x2
#define MODRM_MOD_RM_REG	0x3

/* ModR/M reg field special values */
#define MODRM_REG_MOV_IMM	0x0
#define MODRM_REG_AND_IMM	0x4
#define MODRM_REG_CMP_IMM	0x0
#define MODRM_REG_OR_IMM	0x1
#define MODRM_REG_XOR_IMM	0x6
#define MODRM_REG_SHL		0x4
#define MODRM_REG_SHR		0x5
#define MODRM_REG_ADD_IMM	0x0
#define MODRM_REG_SUB_IMM	0x5


/*
 * XXX: sort out sign extension, zero extension messes;
 *      in particular things like and that sign-extend by
 *      default...
 */


typedef struct x86_ctx {
	void	**epilogues;
	int	epilogues_sz;
	int	epilogues_cnt;
} *x86_ctx_t;

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

static const char *reg_to_name[] = {
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

static const int reg_empty_weight[] = {
	[REG_RAX]	= 1,
	[REG_RCX]	= 1,
	[REG_RDX]	= 1,
	[REG_RBX]	= 0,
	[REG_RSP]	= 1,
	[REG_RBP]	= 0,
	[REG_RSI]	= 1,
	[REG_RDI]	= 1,
	[REG_R8]	= 1,
	[REG_R9]	= 1,
	[REG_R10]	= 1,
	[REG_R11]	= 1,
	[REG_R12]	= 0,
	[REG_R13]	= 0,
	[REG_R14]	= 0,
	[REG_R15]	= 0
};

static const int callee_saved_regs[] = {
	REG_RBX,
#ifdef JIT_FPO
	/* Without FPO, this gets saved anyway */
	REG_RBP,
#endif
	REG_R12,
	REG_R13,
	REG_R14,
	REG_R15
};

#define CALLEE_SAVED_REG_CNT	(int)((sizeof(callee_saved_regs)/sizeof(int)))


#ifdef JIT_FPO
const int jit_tgt_stack_base_reg = REG_RSP;
#else
const int jit_tgt_stack_base_reg = REG_RBP;
#endif


static
int
jit_jmp_cc_disp(jit_cc_t c)
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
jit_emit32_at(uint8_t *buf, uint32_t u32)
{
	*buf++ = (u32 >> 0) & 0xff;
	*buf++ = (u32 >> 8) & 0xff;
	*buf++ = (u32 >> 16) & 0xff;
	*buf++ = (u32 >> 24) & 0xff;
}

static
void
jit_emit64_at(uint8_t *buf, uint64_t u64)
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
	assert(code->code_sz + 1 <= code->map_sz);

	*code->emit_ptr++ = u8;
	code->code_sz += 1;
}

void
jit_emit32(jit_codebuf_t code, uint32_t u32)
{
	assert(code->code_sz + 4 <= code->map_sz);

	*code->emit_ptr++ = (u32 >> 0) & 0xff;
	*code->emit_ptr++ = (u32 >> 8) & 0xff;
	*code->emit_ptr++ = (u32 >> 16) & 0xff;
	*code->emit_ptr++ = (u32 >> 24) & 0xff;
	code->code_sz += 4;
}

void
jit_emit64(jit_codebuf_t code, uint64_t u64)
{
	assert(code->code_sz + 8 <= code->map_sz);

	*code->emit_ptr++ = (u64 >> 0) & 0xff;
	*code->emit_ptr++ = (u64 >> 8) & 0xff;
	*code->emit_ptr++ = (u64 >> 16) & 0xff;
	*code->emit_ptr++ = (u64 >> 24) & 0xff;
	*code->emit_ptr++ = (u64 >> 32) & 0xff;
	*code->emit_ptr++ = (u64 >> 40) & 0xff;
	*code->emit_ptr++ = (u64 >> 48) & 0xff;
	*code->emit_ptr++ = (u64 >> 56) & 0xff;
	code->code_sz += 8;
}

static
void
jit_emit_rex(jit_codebuf_t code, int w64, int r_reg_ext, int x_sib_ext, int b_rm_ext)
{
	uint8_t rex;

	rex = (0x4 << 4) |
	    ((w64       ? 1 : 0) << 3) |
	    ((r_reg_ext ? 1 : 0) << 2) |
	    ((x_sib_ext ? 1 : 0) << 1) |
	    ((b_rm_ext  ? 1 : 0) << 0);

	jit_emit8(code, rex);
}

static
void
jit_emit_sib(jit_codebuf_t code, int scale_factor, uint8_t index, uint8_t base)
{
	int sib;
	int scale;

	scale = (scale_factor == 1) ? 0 :
	    (scale_factor == 2) ? 1 :
	    (scale_factor == 4) ? 2 :
	    (scale_factor == 8) ? 3 : -1;

	assert (scale != -1);

	sib = ((scale & 0x3) << 6) |
	    ((index & 0x7) << 3) |
	    ((base & 0x7) << 0);

	jit_emit8(code, sib);
}


static
void
jit_emit_opc1_reg_memrm(jit_codebuf_t code, int w64, uint8_t opc, uint8_t reg, uint8_t reg_base, uint8_t reg_index, int scale, int32_t disp, uint64_t abs)
{
	int no_base = (reg_base == NO_REG);
	int no_index = (reg_index == NO_REG);
	int rip_relative = (!no_base && !no_index) || (abs != 0);
	int no_disp = (disp == 0);
	int reg_ext = reg & 0x8;
	int base_ext = reg_base & 0x8;
	int index_ext = (!no_index) ? reg_index & 0x8 : 0;
	int use_sib = 0;
	int8_t disp8 = (int8_t)disp;
	int use_disp8 = ((int32_t)disp8 == disp) && !rip_relative;
	int use_disp = (disp != 0) || rip_relative;
	uint8_t mod;
	uint8_t rm;
	uint8_t modrm;

	/* XXX: don't particularly like it, but abs is used for rip-relative */
	if (rip_relative)
		assert ((disp == 0) && (scale == 0) && no_base && no_index);

	use_sib =
	    /* Use sib if an index was specified */
	    (!no_index) ||
	    /* or if RSP or R12 are being used as base */
	    (!no_base && ((reg_base == REG_RSP) || (reg_base == REG_R12)));

	/*
	 * For [base + (index*scale)] addressing with RBP or R13 as base, the
	 * displacement version has to be used no matter what - i.e. mod = 1 or 2.
	 */
	if (use_sib && ((reg_base == REG_RBP) || (reg_base == REG_R13)))
		use_disp = 1;

	if (rip_relative)
		mod = 0x0;
	else if (use_sib && !use_disp)
		mod = 0x0;
	else if (!use_sib && (disp == 0)) {
		if (reg_base == REG_RBP ||
		    reg_base == REG_R13) {
			mod = 0x01;
			use_disp8 = 1;
			use_disp = 1;
			disp8 = 0;
		} else {
			mod = 0x0;
		}
	}
	else if (use_disp8)
		mod = 0x1;
	else
		mod = 0x2;

	reg &= 0x7;
	if (!no_base)
		reg_base &= 0x7;
	if (!no_index)
		reg_index &= 0x7;

	if (rip_relative) {
		base_ext = 0;
		rm = 0x5;
	} else if (use_sib) {
		rm = 0x4;
	} else {
		rm = reg_base;
	}

	modrm = ((mod & 0x3) << 6) | ((reg & 0x7) << 3) | (rm & 0x7);

	if (w64 || reg_ext || base_ext || index_ext)
		jit_emit_rex(code, w64, reg_ext, index_ext, base_ext);

	jit_emit8(code, opc);
	jit_emit8(code, modrm);

	if (use_sib)
		jit_emit_sib(code, scale, reg_index, reg_base);

	if (use_disp) {
		if (rip_relative) {
			disp = (int32_t)((code->code_ptr +4) - abs);
			jit_emit32(code, disp);
		} else if (use_disp8) {
			jit_emit8(code, disp8);
		} else {
			jit_emit32(code, disp);
		}
	}
}

static
void
jit_emit_opc1_reg_regrm(jit_codebuf_t code, int w64, uint8_t opc, uint8_t reg,
    uint8_t rm_reg)
{
	uint8_t modrm;
	int reg_ext = reg & 0x8;
	int rm_ext = rm_reg & 0x8;

	reg &= 0x7;
	rm_reg &= 0x7;

	if (reg_ext || rm_ext || w64)
		jit_emit_rex(code, w64, reg_ext, 0, rm_ext);

	modrm = (MODRM_MOD_RM_REG << 6) | (reg << 3) | (rm_reg);

	jit_emit8(code, opc);
	jit_emit8(code, modrm);
}

void
jit_emit_mov_reg_reg(jit_codebuf_t code, int w64, uint8_t dst_reg, uint8_t src_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, OPC_MOV_REG_RM, dst_reg, src_reg);
}

void
jit_emit_or_reg_reg(jit_codebuf_t code, int w64, uint8_t dst_reg, uint8_t src_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, OPC_OR_REG_RM, dst_reg, src_reg);
}

void
jit_emit_xor_reg_reg(jit_codebuf_t code, int w64, uint8_t dst_reg, uint8_t src_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, OPC_XOR_REG_RM, dst_reg, src_reg);
}

void
jit_emit_and_reg_reg(jit_codebuf_t code, int w64, uint8_t dst_reg, uint8_t src_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, OPC_AND_REG_RM, dst_reg, src_reg);
}

void
jit_emit_add_reg_reg(jit_codebuf_t code, int w64, uint8_t dst_reg, uint8_t src_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, OPC_ADD_REG_RM, dst_reg, src_reg);
}

void
jit_emit_sub_reg_reg(jit_codebuf_t code, int w64, uint8_t dst_reg, uint8_t src_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, OPC_SUB_REG_RM, dst_reg, src_reg);
}

void
jit_emit_mov_reg_imm32(jit_codebuf_t code, uint8_t dst_reg, uint32_t imm)
{
	int reg_ext = dst_reg & 0x8;

	dst_reg &= 0x7;

	if (reg_ext)
		jit_emit_rex(code, 0, reg_ext, 0, 0);

	jit_emit8(code, OPC_MOV_REG_IMM32 + dst_reg);
	jit_emit32(code, imm);
}

void
jit_emit_mov_reg_imm64(jit_codebuf_t code, uint8_t dst_reg, uint64_t imm)
{
	int reg_ext = dst_reg & 0x8;

	dst_reg &= 0x7;

	jit_emit_rex(code, 1, reg_ext, 0, 0);
	jit_emit8(code, OPC_MOV_REG_IMM32 + dst_reg);
	jit_emit64(code, imm);

	/* XXX: consider using optimum encoding, either zero-extend, or MOVSX, or in general just minimum width immediate */
}

void
jit_emit_push_reg(jit_codebuf_t code, uint8_t reg)
{
	int reg_ext = reg & 0x8;

	reg &= 0x7;

	if (reg_ext)
		jit_emit_rex(code, 1, reg_ext, 0, 0);

	jit_emit8(code, OPC_PUSH_REG + reg);
}

void
jit_emit_pop_reg(jit_codebuf_t code, uint8_t reg)
{
	int reg_ext = reg & 0x8;

	reg &= 0x7;

	if (reg_ext)
		jit_emit_rex(code, 1, reg_ext, 0, 0);

	jit_emit8(code, OPC_POP_REG + reg);
}

void
jit_emit_shift_reg_imm8(jit_codebuf_t code, int w64, int left, uint8_t rm_reg, int amt)
{
	jit_emit_opc1_reg_regrm(code, w64, OPC_SHIFT_RM_IMM,
	    left ? MODRM_REG_SHL : MODRM_REG_SHR, rm_reg);

	jit_emit8(code, amt & 0xFF);
}

void
jit_emit_shift_reg_cl(jit_codebuf_t code, int w64, int left, uint8_t rm_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, OPC_SHIFT_RM_CL,
	    left ? MODRM_REG_SHL : MODRM_REG_SHR, rm_reg);
}

void
jit_emit_cmp_reg_imm32(jit_codebuf_t code, uint8_t rm_reg, uint32_t imm)
{
	if (rm_reg == REG_RAX) {
		jit_emit8(code, OPC_CMP_EAX_IMM32);
	} else {
		jit_emit_opc1_reg_regrm(code, 0, OPC_CMP_RM_IMM32,
		    MODRM_REG_CMP_IMM, rm_reg);
	}
	jit_emit32(code, imm);
}

void
jit_emit_test_reg_imm32(jit_codebuf_t code, uint8_t rm_reg, uint32_t imm)
{
	jit_emit_opc1_reg_regrm(code, 0, OPC_TEST_RM_IMM32,
	    MODRM_REG_TEST_IMM, rm_reg);

	jit_emit32(code, imm);
}

void
jit_emit_test_reg_imm64(jit_codebuf_t code, uint8_t rm_reg, uint64_t imm)
{
	jit_emit_opc1_reg_regrm(code, 1, OPC_TEST_RM_IMM64,
	    MODRM_REG_TEST_IMM, rm_reg);

	jit_emit64(code, imm);
}

void
jit_emit_and_reg_imm32(jit_codebuf_t code, int w64, uint8_t rm_reg, uint32_t imm)
{
	if (rm_reg == REG_RAX) {
		jit_emit8(code, OPC_AND_EAX_IMM32);
	} else {
		jit_emit_opc1_reg_regrm(code, 0, OPC_AND_RM_IMM32,
		    MODRM_REG_AND_IMM, rm_reg);
	}
	jit_emit32(code, imm);
}

void
jit_emit_or_reg_imm32(jit_codebuf_t code, int w64, uint8_t rm_reg, uint32_t imm)
{
	if (rm_reg == REG_RAX) {
		jit_emit8(code, OPC_OR_EAX_IMM32);
	} else {
		jit_emit_opc1_reg_regrm(code, w64, OPC_OR_RM_IMM32,
		    MODRM_REG_OR_IMM, rm_reg);
	}
	jit_emit32(code, imm);
}

void
jit_emit_xor_reg_imm32(jit_codebuf_t code, int w64, uint8_t rm_reg, uint32_t imm)
{
	if (rm_reg == REG_RAX) {
		jit_emit8(code, OPC_XOR_EAX_IMM32);
	} else {
		jit_emit_opc1_reg_regrm(code, w64, OPC_XOR_RM_IMM32,
		    MODRM_REG_XOR_IMM, rm_reg);
	}
	jit_emit32(code, imm);
}

void
jit_emit_add_reg_imm32(jit_codebuf_t code, int w64, uint8_t rm_reg, int32_t imm)
{
	int8_t imm8 = (int8_t)imm;
	int use_imm8 = ((int32_t)imm8 == imm);

	if (rm_reg == REG_RAX) {
		jit_emit8(code, OPC_ADD_EAX_IMM32);
	} else {
		jit_emit_opc1_reg_regrm(code, w64,
		    use_imm8 ? OPC_ADD_RM_IMM8 : OPC_ADD_RM_IMM32,
		    MODRM_REG_ADD_IMM, rm_reg);
	}

	if (use_imm8)
		jit_emit8(code, imm8);
	else
		jit_emit32(code, imm);
}

void
jit_emit_sub_reg_imm32(jit_codebuf_t code, int w64, uint8_t rm_reg, int32_t imm)
{
	int8_t imm8 = (int8_t)imm;
	int use_imm8 = ((int32_t)imm8 == imm);

	if (rm_reg == REG_RAX) {
		jit_emit8(code, OPC_SUB_EAX_IMM32);
	} else {
		jit_emit_opc1_reg_regrm(code, w64,
		    use_imm8 ? OPC_SUB_RM_IMM8 : OPC_SUB_RM_IMM32,
		    MODRM_REG_SUB_IMM, rm_reg);
	}

	if (use_imm8)
		jit_emit8(code, imm8);
	else
		jit_emit32(code, imm);
}


/* XXX */
void
jit_emit_not_(jit_codebuf_t code, int w64, uint8_t rm_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, OPC_NOT_RM,
	    2, rm_reg);
}

/* XXX */
void
jit_emit_ret_(jit_ctx_t ctx, jit_codebuf_t code)
{
	x86_ctx_t x86_ctx = (x86_ctx_t)ctx->tgt_ctx;

#if 0
	int i, saved_reg;

	for (i = 0; i < CALLEE_SAVED_REG_CNT; i++) {
		saved_reg = callee_saved_regs[i];
		if (!jit_regset_test(ctx->regs_ever_used, saved_reg))
			continue;

		/* mov -(i*8)(%rbp), %saved_reg */
		jit_emit_opc1_reg_memrm(ctx->codebuf, 1, OPC_MOV_REG_RM, saved_reg,
					REG_RBP, NO_REG, 0, -i*8, 0);
	}

	jit_emit8(code, OPC_LEAVE);
	jit_emit8(code, OPC_RET);
#endif

	if (x86_ctx->epilogues_cnt == x86_ctx->epilogues_sz) {
		x86_ctx->epilogues_sz += 4;
		x86_ctx->epilogues = realloc(x86_ctx->epilogues,
		    x86_ctx->epilogues_sz * sizeof(void *));
	}

	x86_ctx->epilogues[x86_ctx->epilogues_cnt++] = code->emit_ptr;

	code->code_sz += MAX_EPILOGUE_SZ;
	code->emit_ptr += MAX_EPILOGUE_SZ;
}



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

	[JITOP_SUB]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	/* LEA r32/64, m XXX */
	/* SUB r/m32/64, r32/64 (and vice versa) */

	[JITOP_SUBI]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	/* LEA r32/64, m XXX */
	/* SUB r/m32/64, imm32 XXX */

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

	[JITOP_RET]	= { .alias = "", .o_restrict = "", .i_restrict = "a" },
	/* MOV + RETN */

	[JITOP_RETI]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	/* MOV + RETN */

	[JITOP_BSWAP]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	/* BSWAP r32/64 */

	[JITOP_CLZ]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	/* BSF/BSR r32/64, r/m32/64 */
};

int
jit_tgt_reg_empty_weight(jit_ctx_t ctx, int reg)
{
	return reg_empty_weight[reg];
}

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
	else if (op == JITOP_RETI)
		return 0;
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
	case 'a':
		jit_regset_set(rs, REG_RAX);
		break;

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
		jit_emit_mov_reg_reg(ctx->codebuf, (dw == JITOP_DW_64), params[0], params[1]);
		break;

	case JITOP_MOVI:
		printf("%s.%d\t%s, $%"PRIx64"\n", op_def[op].mnemonic, dw, reg_to_name[params[0]], params[1]);
		if (params[1] == 0)
			jit_emit_xor_reg_reg(ctx->codebuf, (dw == JITOP_DW_64), params[0], params[0]);
		else if ((dw == JITOP_DW_64) && ((uint32_t)params[1] != (uint64_t)params[1]))
			jit_emit_mov_reg_imm64(ctx->codebuf, params[0], params[1]);
		else
			jit_emit_mov_reg_imm32(ctx->codebuf, params[0], params[1]);
		break;

	case JITOP_OR:
		jit_emit_or_reg_reg(ctx->codebuf, (dw == JITOP_DW_64), params[1], params[2]);
		printf("%s.%d\t%s, %s\n", op_def[op].mnemonic, dw, reg_to_name[params[1]], reg_to_name[params[2]]);
		break;
	case JITOP_AND:
		jit_emit_and_reg_reg(ctx->codebuf, (dw == JITOP_DW_64), params[1], params[2]);
		printf("%s.%d\t%s, %s\n", op_def[op].mnemonic, dw, reg_to_name[params[1]], reg_to_name[params[2]]);
		break;
	case JITOP_XOR:
		jit_emit_xor_reg_reg(ctx->codebuf, (dw == JITOP_DW_64), params[1], params[2]);
		printf("%s.%d\t%s, %s\n", op_def[op].mnemonic, dw, reg_to_name[params[1]], reg_to_name[params[2]]);
		break;
	case JITOP_ADD:
		jit_emit_add_reg_reg(ctx->codebuf, (dw == JITOP_DW_64), params[1], params[2]);
		printf("%s.%d\t%s, %s\n", op_def[op].mnemonic, dw, reg_to_name[params[1]], reg_to_name[params[2]]);
		break;
	case JITOP_SUB:
		jit_emit_sub_reg_reg(ctx->codebuf, (dw == JITOP_DW_64), params[1], params[2]);
		printf("%s.%d\t%s, %s\n", op_def[op].mnemonic, dw, reg_to_name[params[1]], reg_to_name[params[2]]);
		//printf("%s.%d\t%s, %s, %s\n", op_def[op].mnemonic, dw, reg_to_name[params[0]], reg_to_name[params[1]], reg_to_name[params[2]]);
		assert(params[0] == params[1]);
		break;

	case JITOP_ORI:
		jit_emit_or_reg_imm32(ctx->codebuf, (dw == JITOP_DW_64), params[1], params[2]);
		printf("%s.%d\t%s, $%"PRIx64"\n", op_def[op].mnemonic, dw, reg_to_name[params[1]], params[2]);
		break;
	case JITOP_ANDI:
		jit_emit_and_reg_imm32(ctx->codebuf, (dw == JITOP_DW_64), params[1], params[2]);
		printf("%s.%d\t%s, $%"PRIx64"\n", op_def[op].mnemonic, dw, reg_to_name[params[1]], params[2]);
		break;
	case JITOP_XORI:
		jit_emit_xor_reg_imm32(ctx->codebuf, (dw == JITOP_DW_64), params[1], params[2]);
		printf("%s.%d\t%s, $%"PRIx64"\n", op_def[op].mnemonic, dw, reg_to_name[params[1]], params[2]);
		break;
	case JITOP_ADDI:
		jit_emit_add_reg_imm32(ctx->codebuf, (dw == JITOP_DW_64), params[1], params[2]);
		printf("%s.%d\t%s, $%"PRIx64"\n", op_def[op].mnemonic, dw, reg_to_name[params[1]], params[2]);
		break;
	case JITOP_SUBI:
		jit_emit_sub_reg_imm32(ctx->codebuf, (dw == JITOP_DW_64), params[1], params[2]);
		printf("%s.%d\t%s, $%"PRIx64"\n", op_def[op].mnemonic, dw, reg_to_name[params[1]], params[2]);
		//printf("%s.%d\t%s, %s, $%"PRIx64"\n", op_def[op].mnemonic, dw, reg_to_name[params[0]], reg_to_name[params[1]], params[2]);
		assert(params[0] == params[1]);
		break;

	case JITOP_LDRBPO:
		printf("%s.%d\t%s, [%s + $%"PRId64"]\n", op_def[op].mnemonic, dw, reg_to_name[params[0]], reg_to_name[params[1]], params[2]);
		jit_emit_opc1_reg_memrm(ctx->codebuf, (dw == JITOP_DW_64), OPC_MOV_REG_RM, params[0],
					params[1], NO_REG, 0, params[2], 0);
		break;

	case JITOP_STRBPO:
		printf("%s.%d\t%s, [%s + $%"PRId64"]\n", op_def[op].mnemonic, dw, reg_to_name[params[0]], reg_to_name[params[1]], params[2]);
		jit_emit_opc1_reg_memrm(ctx->codebuf, (dw == JITOP_DW_64), OPC_MOV_RM_REG, params[0],
					params[1], NO_REG, 0, params[2], 0);
		break;

	case JITOP_RET:
		printf("ret.%d\n", dw);
		jit_emit_ret_(ctx, ctx->codebuf);
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

#ifdef JIT_FPO
#else
	ctx->spill_stack_offset = -(8*CALLEE_SAVED_REG_CNT);
#endif

	ctx->codebuf->emit_ptr = ctx->codebuf->code_ptr =
	    ctx->codebuf->code_ptr + MAX_PROLOGUE_SZ;
}

static
void
emit_prologue(jit_ctx_t ctx)
{
	struct jit_codebuf prologue_buf;
	size_t prologue_sz;
	off_t offset;
	int32_t frame_size;
	int i, reg;
	int r;

#ifdef JIT_FPO
#else
	r = jit_init_codebuf(NULL, &prologue_buf);
	assert (r == 0);

	/* push %rbp */
	jit_emit_push_reg(&prologue_buf, REG_RBP);
	/* mov  %rsp, %rbp */
	jit_emit_mov_reg_reg(&prologue_buf, 1, REG_RBP, REG_RSP);

	/* sub  $frame_size, %rsp */
	frame_size = _ALIGN_SZ(8*(CALLEE_SAVED_REG_CNT + ctx->local_tmps_cnt + ctx->bb_tmps_cnt), 16);
	jit_emit_sub_reg_imm32(&prologue_buf, 1, REG_RSP, frame_size);

	/* ^^^ equivalent to enter (but enter seems to take more cycles) */
	for (i = 0; i < CALLEE_SAVED_REG_CNT; i++) {
		reg = callee_saved_regs[i];
		if (!jit_regset_test(ctx->regs_ever_used, reg))
			continue;

		/* mov %reg, -(i*8)(%rbp) */
		jit_emit_opc1_reg_memrm(&prologue_buf, 1, OPC_MOV_RM_REG, reg,
					REG_RBP, NO_REG, 0, -i*8, 0);
	}
#endif

	assert(prologue_buf.code_sz <= MAX_PROLOGUE_SZ);

	offset = MAX_PROLOGUE_SZ - prologue_buf.code_sz;
	ctx->codebuf->code_ptr =
	    ctx->codebuf->code_ptr - prologue_buf.code_sz;

	ctx->codebuf->code_sz += prologue_buf.code_sz;

	memcpy(ctx->codebuf->code_ptr, prologue_buf.code_ptr, prologue_buf.code_sz);
}

static
void
emit_epilogue(jit_ctx_t ctx)
{
	int i, saved_reg;

	for (i = 0; i < CALLEE_SAVED_REG_CNT; i++) {
		saved_reg = callee_saved_regs[i];
		if (!jit_regset_test(ctx->regs_ever_used, saved_reg))
			continue;

		/* mov -(i*8)(%rbp), %saved_reg */
		jit_emit_opc1_reg_memrm(ctx->codebuf, 1, OPC_MOV_REG_RM, saved_reg,
					REG_RBP, NO_REG, 0, -i*8, 0);
	}

	jit_emit8(ctx->codebuf, OPC_LEAVE);
	jit_emit8(ctx->codebuf, OPC_RET);
}

void
jit_tgt_ctx_finish_emit(jit_ctx_t ctx)
{
	int i;
	size_t orig_size;

	x86_ctx_t x86_ctx = (x86_ctx_t)ctx->tgt_ctx;

	emit_prologue(ctx);

	for (i = 0; i < x86_ctx->epilogues_cnt; i++) {
		orig_size = ctx->codebuf->code_sz;
		ctx->codebuf->emit_ptr = x86_ctx->epilogues[i];
		emit_epilogue(ctx);
		assert (ctx->codebuf->code_sz - orig_size <= MAX_EPILOGUE_SZ);
		ctx->codebuf->code_sz = orig_size;
	}
}

void
jit_tgt_ctx_init(jit_ctx_t ctx)
{
	x86_ctx_t x86_ctx;

	x86_ctx = malloc(sizeof(*x86_ctx));
	assert (x86_ctx != NULL);
	memset(x86_ctx, 0, sizeof(*x86_ctx));
	ctx->tgt_ctx = x86_ctx;

	jit_regset_empty(ctx->overall_choice);
	jit_regset_set(ctx->overall_choice, REG_RAX);
	jit_regset_set(ctx->overall_choice, REG_RCX);
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

	/* XXX: ! */
	jit_regset_full(ctx->regs_ever_used);
}


void
jit_tgt_init(void)
{
	/* XXX: do cpuid or whatever to detect supported features */
	return;
}


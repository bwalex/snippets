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
#define OPC_XOR_RM_IMM8		0x80

#define OPC_AND_REG_RM		0x23
#define OPC_AND_RM_REG		0x21
#define OPC_AND_RM_IMM32	0x81
#define OPC_AND_EAX_IMM32	0x25
#define OPC_AND_RM_IMM8		0x80

#define OPC_OR_REG_RM		0x0B
#define OPC_OR_RM_REG		0x09
#define OPC_OR_RM_IMM32		0x81
#define OPC_OR_EAX_IMM32	0x0D
#define OPC_OR_RM_IMM8		0x80

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

#define OPC_ZEROF_PREFIX	0x0F
#define OPC_BSWAP_REG		0xC8
#define OPC_BSF_REG		0xBC
#define OPC_BSR_REG		0xBD

#define OPC_CMP_REG_RM		0x3B
#define OPC_CMP_RM_REG		0x39
#define OPC_CMP_RM_IMM32	0x81
#define OPC_CMP_EAX_IMM32	0x3D

#define OPC_MOVZX8		0xB6
#define OPC_MOVZX16		0xB7

#define OPC_MOVSX8		0xBE
#define OPC_MOVSX16		0xBF
#define OPC_MOVSX32		0x63

#define OPC_NOT_RM		0xF7

#define OPC_SHIFT_RM_IMM	0xC1

#define OPC_JMP			0xE9
#define OPC_JMP_CC		0x80

#define OPC_RET			0xC3

#define OPC_SET_CC		0x90
#define OPC_CMOV_CC		0x40

#define OPC_PUSH_REG		0x50
#define OPC_POP_REG		0x58
#define OPC_ENTER		0xC8
#define OPC_LEAVE		0xC9

#define OPC_SHIFT_RM_CL		0xD3
#define OPC_TEST_RM_REG		0x85
#define OPC_TEST_RM_IMM32	0xF7
#define OPC_TEST_RM_IMM64	0xF7
#define MODRM_REG_TEST_IMM	0x00 /* XXX: or 0x01 - what do they mean? */


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
#define MODRM_REG_NOT		0x2
#define MODRM_REG_SETCC		0x0


/*
 * XXX: sort out sign extension, zero extension messes;
 *      in particular things like and that sign-extend by
 *      default...
 */

struct x86_reloc
{
	void		*loc;
	jit_label_t	label;
};

typedef struct x86_ctx {
	struct dyn_array epilogues;
	struct dyn_array relocs;
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


static const int fn_arg_regs[] = {
	REG_RDI,
	REG_RSI,
	REG_RDX,
	REG_RCX,
	REG_R8,
	REG_R9
};

static const int cc_to_code[] = {
	[CMP_EQ] = 4,
	[CMP_NE] = 5,

	[CMP_AE] = 3,
	[CMP_B]  = 2,
	[CMP_A]  = 7,
	[CMP_BE] = 6,

	[CMP_LT] = 12,
	[CMP_GE] = 13,
	[CMP_LE] = 14,
	[CMP_GT] = 15
};

static const int test_cc_to_code[] = {
	[TST_Z]       = 4,
	[TST_NZ]      = 5,
	[TST_MSB_SET] = 8,
	[TST_MSB_CLR] = 9
};

#define FN_ARG_REG_CNT		(int)((sizeof(fn_arg_regs)/sizeof(int)))


#ifdef JIT_FPO
const int jit_tgt_stack_base_reg = REG_RSP;
#else
const int jit_tgt_stack_base_reg = REG_RBP;
#endif

static int have_popcnt;
static int have_lzcnt;
static int have_bmi1;
static int have_bmi2;

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
jit_emit_opc1_reg_memrm(jit_codebuf_t code, int w64, int zerof_prefix, uint8_t opc, uint8_t reg, uint8_t reg_base, uint8_t reg_index, int scale, int32_t disp, uint64_t abs)
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
		mod = MODRM_MOD_DISP0;
	else if (use_sib && !use_disp)
		mod = MODRM_MOD_DISP0;
	else if (!use_sib && (disp == 0)) {
		if (reg_base == REG_RBP ||
		    reg_base == REG_R13) {
			mod = MODRM_MOD_DISP1B;
			use_disp8 = 1;
			use_disp = 1;
			disp8 = 0;
		} else {
			mod = MODRM_MOD_DISP0;
		}
	}
	else if (use_disp8)
		mod = MODRM_MOD_DISP1B;
	else
		mod = MODRM_MOD_DISP4B;

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

	if (zerof_prefix)
		jit_emit8(code, OPC_ZEROF_PREFIX);

	jit_emit8(code, opc);
	jit_emit8(code, modrm);

	if (use_sib)
		jit_emit_sib(code, scale, reg_index, reg_base);

	if (use_disp) {
		if (rip_relative) {
			disp = (int32_t)((uint64_t)(code->emit_ptr + 4) - abs);
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
jit_emit_opc1_reg_regrm(jit_codebuf_t code, int w64, int zerof_prefix, int force_rex, uint8_t opc, uint8_t reg,
    uint8_t rm_reg)
{
	uint8_t modrm;
	int reg_ext = reg & 0x8;
	int rm_ext = rm_reg & 0x8;

	reg &= 0x7;
	rm_reg &= 0x7;

	if (reg_ext || rm_ext || w64 || force_rex)
		jit_emit_rex(code, w64, reg_ext, 0, rm_ext);

	modrm = (MODRM_MOD_RM_REG << 6) | (reg << 3) | (rm_reg);

	if (zerof_prefix)
		jit_emit8(code, OPC_ZEROF_PREFIX);

	jit_emit8(code, opc);
	jit_emit8(code, modrm);
}

static
void
jit_emit_mov_reg_reg(jit_codebuf_t code, int w64, uint8_t dst_reg, uint8_t src_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, 0, 0, OPC_MOV_REG_RM, dst_reg, src_reg);
}

static
void
jit_emit_movzx_reg_reg(jit_codebuf_t code, int dw, uint8_t dst_reg, uint8_t src_reg)
{
	jit_emit_opc1_reg_regrm(code, 0, 1, 0,
	    (dw == JITOP_DW_8)  ? OPC_MOVZX8 : OPC_MOVZX16, dst_reg, src_reg);
}

static
void
jit_emit_movsx_reg_reg(jit_codebuf_t code, int dw, uint8_t dst_reg, uint8_t src_reg)
{
	jit_emit_opc1_reg_regrm(code, 1, (dw != JITOP_DW_32), 0,
	    (dw == JITOP_DW_8)  ? OPC_MOVSX8 :
	    (dw == JITOP_DW_16) ? OPC_MOVSX16 : OPC_MOVSX32, dst_reg, src_reg);
}

static
void
jit_emit_or_reg_reg(jit_codebuf_t code, int w64, uint8_t dst_reg, uint8_t src_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, 0, 0, OPC_OR_REG_RM, dst_reg, src_reg);
}

static
void
jit_emit_xor_reg_reg(jit_codebuf_t code, int w64, uint8_t dst_reg, uint8_t src_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, 0, 0, OPC_XOR_REG_RM, dst_reg, src_reg);
}

static
void
jit_emit_and_reg_reg(jit_codebuf_t code, int w64, uint8_t dst_reg, uint8_t src_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, 0, 0, OPC_AND_REG_RM, dst_reg, src_reg);
}

static
void
jit_emit_add_reg_reg(jit_codebuf_t code, int w64, uint8_t dst_reg, uint8_t src_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, 0, 0, OPC_ADD_REG_RM, dst_reg, src_reg);
}

static
void
jit_emit_sub_reg_reg(jit_codebuf_t code, int w64, uint8_t dst_reg, uint8_t src_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, 0, 0, OPC_SUB_REG_RM, dst_reg, src_reg);
}

static
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

static
void
jit_emit_mov_reg_simm32(jit_codebuf_t code, int w64, uint8_t dst_reg, uint32_t imm)
{
	jit_emit_opc1_reg_regrm(code, w64, 0, 0, OPC_MOV_RM_IMM32, MODRM_REG_MOV_IMM, dst_reg);
	jit_emit32(code, imm);
}

static
void
jit_emit_mov_reg_imm64(jit_codebuf_t code, uint8_t dst_reg, uint64_t imm)
{
	int reg_ext = dst_reg & 0x8;

	dst_reg &= 0x7;

	jit_emit_rex(code, 1, reg_ext, 0, 0);
	jit_emit8(code, OPC_MOV_REG_IMM32 + dst_reg);
	jit_emit64(code, imm);
}

static
void
jit_emit_push_reg(jit_codebuf_t code, uint8_t reg)
{
	int reg_ext = reg & 0x8;

	reg &= 0x7;

	if (reg_ext)
		jit_emit_rex(code, 1, reg_ext, 0, 0);

	jit_emit8(code, OPC_PUSH_REG + reg);
}

static
void
jit_emit_pop_reg(jit_codebuf_t code, uint8_t reg)
{
	int reg_ext = reg & 0x8;

	reg &= 0x7;

	if (reg_ext)
		jit_emit_rex(code, 1, reg_ext, 0, 0);

	jit_emit8(code, OPC_POP_REG + reg);
}

static
void
jit_emit_bswap_reg(jit_codebuf_t code, int w64, uint8_t reg)
{
	int reg_ext = reg & 0x8;

	reg &= 0x7;

	if (reg_ext || w64)
		jit_emit_rex(code, w64, reg_ext, 0, 0);

	jit_emit8(code, OPC_ZEROF_PREFIX);
	jit_emit8(code, OPC_BSWAP_REG + reg);
}

static
void
jit_emit_clz_reg_reg(jit_codebuf_t code, int w64, uint8_t reg, uint8_t rm_reg)
{
	int reg_ext = reg & 0x8;
	int rm_ext = rm_reg & 0x8;

	reg &= 0x7;
	rm_reg &= 0x7;
	/* XXX: deal with ZF */
	/* XXX: convert from index-of-MSB1 to leading-zeroes */
	/* XXX: this is just plain wrong */
}

static
void
jit_emit_shift_reg_imm8(jit_codebuf_t code, int w64, int left, uint8_t rm_reg, int amt)
{
	jit_emit_opc1_reg_regrm(code, w64, 0, 0, OPC_SHIFT_RM_IMM,
	    left ? MODRM_REG_SHL : MODRM_REG_SHR, rm_reg);

	jit_emit8(code, amt & 0xFF);
}

static
void
jit_emit_shift_reg_cl(jit_codebuf_t code, int w64, int left, uint8_t rm_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, 0, 0, OPC_SHIFT_RM_CL,
	    left ? MODRM_REG_SHL : MODRM_REG_SHR, rm_reg);
}

static
void
jit_emit_cmp_reg_imm32(jit_codebuf_t code, int w64, uint8_t rm_reg, uint32_t imm)
{
	if (rm_reg == REG_RAX) {
		if (w64)
			jit_emit_rex(code, w64, 0, 0, 0);
		jit_emit8(code, OPC_CMP_EAX_IMM32);
	} else {
		jit_emit_opc1_reg_regrm(code, w64, 0, 0, OPC_CMP_RM_IMM32,
		    MODRM_REG_CMP_IMM, rm_reg);
	}
	jit_emit32(code, imm);
}

static
void
jit_emit_cmp_reg_reg(jit_codebuf_t code, int w64, uint8_t reg, uint8_t rm_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, 0, 0, OPC_CMP_REG_RM, reg, rm_reg);
}

static
void
jit_emit_test_reg_imm32(jit_codebuf_t code, int w64, uint8_t rm_reg, uint32_t imm)
{
	jit_emit_opc1_reg_regrm(code, w64, 0, 0, OPC_TEST_RM_IMM32,
	    MODRM_REG_TEST_IMM, rm_reg);

	jit_emit32(code, imm);
}

static
void
jit_emit_test_reg_reg(jit_codebuf_t code, int w64, uint8_t reg, uint8_t rm_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, 0, 0, OPC_TEST_RM_REG, reg, rm_reg);
}

static
void
jit_emit_and_reg_imm32(jit_codebuf_t code, int w64, uint8_t rm_reg, uint32_t imm)
{
	int8_t imm8 = (int8_t)imm;
	int use_imm8 = check_signed8(imm);

	if (rm_reg == REG_RAX) {
		jit_emit8(code, OPC_AND_EAX_IMM32);
	} else {
		jit_emit_opc1_reg_regrm(code, 0, 0,
		    (rm_reg == REG_RBP || rm_reg == REG_RSP || rm_reg == REG_RSI || rm_reg == REG_RDI),
		    use_imm8 ? OPC_AND_RM_IMM8 : OPC_AND_RM_IMM32,
		    MODRM_REG_AND_IMM, rm_reg);
	}

	if (use_imm8)
		jit_emit8(code, imm8);
	else
		jit_emit32(code, imm);
}

static
void
jit_emit_or_reg_imm32(jit_codebuf_t code, int w64, uint8_t rm_reg, uint32_t imm)
{
	int8_t imm8 = (int8_t)imm;
	int use_imm8 = check_signed8(imm);

	if (rm_reg == REG_RAX) {
		jit_emit8(code, OPC_OR_EAX_IMM32);
	} else {
		jit_emit_opc1_reg_regrm(code, w64, 0,
		    (rm_reg == REG_RBP || rm_reg == REG_RSP || rm_reg == REG_RSI || rm_reg == REG_RDI),
		    use_imm8 ? OPC_OR_RM_IMM8 : OPC_OR_RM_IMM32,
		    MODRM_REG_OR_IMM, rm_reg);
	}

	if (use_imm8)
		jit_emit8(code, imm8);
	else
		jit_emit32(code, imm);
}

static
void
jit_emit_xor_reg_imm32(jit_codebuf_t code, int w64, uint8_t rm_reg, uint32_t imm)
{
	int8_t imm8 = (int8_t)imm;
	int use_imm8 = check_signed8(imm);

	if (rm_reg == REG_RAX) {
		jit_emit8(code, OPC_XOR_EAX_IMM32);
	} else {
		jit_emit_opc1_reg_regrm(code, w64, 0,
		    (rm_reg == REG_RBP || rm_reg == REG_RSP || rm_reg == REG_RSI || rm_reg == REG_RDI),
		    use_imm8 ? OPC_XOR_RM_IMM8 : OPC_XOR_RM_IMM32,
		    MODRM_REG_XOR_IMM, rm_reg);
	}

	if (use_imm8)
		jit_emit8(code, imm8);
	else
		jit_emit32(code, imm);
}

static
void
jit_emit_add_reg_imm32(jit_codebuf_t code, int w64, uint8_t rm_reg, int32_t imm)
{
	int8_t imm8 = (int8_t)imm;
	int use_imm8 = ((int32_t)imm8 == imm);

	if (rm_reg == REG_RAX) {
		jit_emit8(code, OPC_ADD_EAX_IMM32);
	} else {
		jit_emit_opc1_reg_regrm(code, w64, 0, 0,
		    use_imm8 ? OPC_ADD_RM_IMM8 : OPC_ADD_RM_IMM32,
		    MODRM_REG_ADD_IMM, rm_reg);
	}

	if (use_imm8)
		jit_emit8(code, imm8);
	else
		jit_emit32(code, imm);
}

static
void
jit_emit_sub_reg_imm32(jit_codebuf_t code, int w64, uint8_t rm_reg, int32_t imm)
{
	int8_t imm8 = (int8_t)imm;
	int use_imm8 = ((int32_t)imm8 == imm);

	if (rm_reg == REG_RAX) {
		jit_emit8(code, OPC_SUB_EAX_IMM32);
	} else {
		jit_emit_opc1_reg_regrm(code, w64, 0, 0,
		    use_imm8 ? OPC_SUB_RM_IMM8 : OPC_SUB_RM_IMM32,
		    MODRM_REG_SUB_IMM, rm_reg);
	}

	if (use_imm8)
		jit_emit8(code, imm8);
	else
		jit_emit32(code, imm);
}


/* XXX */
static
void
jit_emit_not_(jit_codebuf_t code, int w64, uint8_t rm_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, 0, 0, OPC_NOT_RM,
	    MODRM_REG_NOT, rm_reg);
}

/* XXX */
static
void
jit_emit_ret_(jit_ctx_t ctx, jit_codebuf_t code)
{
	x86_ctx_t x86_ctx = (x86_ctx_t)ctx->tgt_ctx;
	void **elem;

	/*
	 * The actual epilogue will be emitted later, whenever we know
	 * which callee-saved registers actually need to be restored.
	 */
	elem = dyn_array_new_elem(&x86_ctx->epilogues);
	*elem = code->emit_ptr;

	code->code_sz += MAX_EPILOGUE_SZ;
	code->emit_ptr += MAX_EPILOGUE_SZ;
}

static
void
jit_emit_reloc(jit_ctx_t ctx, jit_label_t label)
{
	struct x86_ctx *x86_ctx = (struct x86_ctx *)ctx->tgt_ctx;
	struct x86_reloc *reloc;

	reloc = dyn_array_new_elem(&x86_ctx->relocs);
	reloc->loc = ctx->codebuf->emit_ptr;
	reloc->label = label;
	jit_emit32(ctx->codebuf, 0xDEADC0DE);
}

static
void
jit_emit_jmp(jit_ctx_t ctx, jit_label_t label)
{
	jit_emit8(ctx->codebuf, OPC_JMP);
	jit_emit_reloc(ctx, label);
}

static
void
jit_emit_jcc(jit_ctx_t ctx, int cc, jit_label_t label)
{
	jit_emit8(ctx->codebuf, OPC_ZEROF_PREFIX);
	jit_emit8(ctx->codebuf, OPC_JMP_CC + cc);
	jit_emit_reloc(ctx, label);
}

static
void
jit_emit_jncc(jit_ctx_t ctx, int cc, jit_label_t label)
{
	jit_emit8(ctx->codebuf, OPC_ZEROF_PREFIX);
	jit_emit8(ctx->codebuf, OPC_JMP_CC + (1^cc));
	jit_emit_reloc(ctx, label);
}

static
void
jit_emit_cmovcc_reg_reg(jit_codebuf_t code, int w64, int cc, uint8_t dst_reg, uint8_t src_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, 1, 0, OPC_CMOV_CC + cc, dst_reg, src_reg);
}

static
void
jit_emit_setcc_reg(jit_codebuf_t code, int w64, int cc, uint8_t dst_reg)
{
	jit_emit_opc1_reg_regrm(code, w64, 1,
	    (dst_reg == REG_RBP || dst_reg == REG_RSP || dst_reg == REG_RSI || dst_reg == REG_RDI),
	OPC_SET_CC + cc, MODRM_REG_SETCC, dst_reg);
}

int
jit_tgt_reg_empty_weight(jit_ctx_t ctx, int reg)
{
	return reg_empty_weight[reg];
}

static
int
check_pc_rel32s(jit_ctx_t ctx, int dw, uint64_t imm)
{
	uint64_t approx_pc;
	uint64_t diff;
	uint32_t diff_lim;

	assert (ctx->codebuf != NULL);

	approx_pc = (uint64_t)ctx->codebuf->emit_ptr;

	if (imm > approx_pc)
		diff = imm - approx_pc;
	else
		diff = approx_pc - imm;

	/* 4K safety buffer */
	diff += 4096;

	/* Check it would fit within a signed integer */
	diff_lim = (uint32_t)diff;
	diff_lim &= 0x7FFFFFFF;

	return ((uint64_t)diff_lim == diff);
}

int
jit_tgt_check_imm(jit_ctx_t ctx, jit_op_t op, int dw, int op_dw, int argidx, uint64_t imm)
{
	/* XXX */
	switch (op) {
	case JITOP_MOVI:
		return 1;

	case JITOP_LDRI:
	case JITOP_LDRI_SEXT:
	case JITOP_STRI:
		return check_pc_rel32s(ctx, JITOP_DW_64, imm);

	case JITOP_LDRBPO:
	case JITOP_LDRBPO_SEXT:
		return check_signed32(imm);

	case JITOP_ANDI:
	case JITOP_ORI:
	case JITOP_XORI:
	case JITOP_ADDI:
	case JITOP_SUBI:
		return (dw != JITOP_DW_64) ? 1 : check_signed32(imm);

	case JITOP_BCMPI:
	case JITOP_BNCMPI:
	case JITOP_BTESTI:
	case JITOP_BNTESTI:
	case JITOP_CMOVI:
	case JITOP_CSELI:
	case JITOP_CSETI:
	case JITOP_TMOVI:
	case JITOP_TSELI:
	case JITOP_TSETI:
		return (op_dw != JITOP_DW_64) ? 1 : check_signed32(imm);

	case JITOP_RETI:
		return 0;

	default:
		return 0;
	}
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

void
jit_tgt_emit(jit_ctx_t ctx, uint32_t opc, uint64_t *params)
{
	jit_op_t op = JITOP_OP(opc);
	int dw = JITOP_DW(opc);
	int op_dw = JITOP_OP_DW(opc);
	int w64 = (dw == JITOP_DW_64);
	int op_w64 = (op_dw == JITOP_DW_64);
	jit_label_t label;

	switch (op) {
	case JITOP_MOV:
		if (dw == JITOP_DW_8 || dw == JITOP_DW_16)
			jit_emit_movzx_reg_reg(ctx->codebuf, dw, params[0], params[1]);
		else
			jit_emit_mov_reg_reg(ctx->codebuf, w64, params[0], params[1]);
		break;

	case JITOP_MOV_SEXT:
		if (!w64)
			jit_emit_movsx_reg_reg(ctx->codebuf, dw, params[0], params[1]);
		else
			jit_emit_mov_reg_reg(ctx->codebuf, w64, params[0], params[1]);
		break;

	case JITOP_MOVI:
		if (params[1] == 0)
			jit_emit_xor_reg_reg(ctx->codebuf, w64, params[0], params[0]);
		else if (w64 && check_signed32(params[1]))
			jit_emit_mov_reg_simm32(ctx->codebuf, w64, params[0], params[1]);
		else if (w64 && !check_unsigned32(params[1]))
			jit_emit_mov_reg_imm64(ctx->codebuf, params[0], params[1]);
		else
			jit_emit_mov_reg_imm32(ctx->codebuf, params[0], params[1]);
		break;

	case JITOP_OR:
		jit_emit_or_reg_reg(ctx->codebuf, w64, params[1], params[2]);
		break;
	case JITOP_AND:
		jit_emit_and_reg_reg(ctx->codebuf, w64, params[1], params[2]);
		break;
	case JITOP_XOR:
		jit_emit_xor_reg_reg(ctx->codebuf, w64, params[1], params[2]);
		break;
	case JITOP_ADD:
		jit_emit_add_reg_reg(ctx->codebuf, w64, params[1], params[2]);
		break;
	case JITOP_SUB:
		jit_emit_sub_reg_reg(ctx->codebuf, w64, params[1], params[2]);
		assert(params[0] == params[1]);
		break;

	case JITOP_ORI:
		jit_emit_or_reg_imm32(ctx->codebuf, w64, params[1], params[2]);
		break;
	case JITOP_ANDI:
		jit_emit_and_reg_imm32(ctx->codebuf, w64, params[1], params[2]);
		break;
	case JITOP_XORI:
		jit_emit_xor_reg_imm32(ctx->codebuf, w64, params[1], params[2]);
		break;
	case JITOP_ADDI:
		jit_emit_add_reg_imm32(ctx->codebuf, w64, params[1], params[2]);
		break;
	case JITOP_SUBI:
		jit_emit_sub_reg_imm32(ctx->codebuf, w64, params[1], params[2]);
		assert(params[0] == params[1]);
		break;

	case JITOP_LDRBPO:
		if (dw == JITOP_DW_64 || dw == JITOP_DW_32)
			jit_emit_opc1_reg_memrm(ctx->codebuf, w64, 0, OPC_MOV_REG_RM, params[0],
			    params[1], NO_REG, 0, params[2], 0);
		else if (dw == JITOP_DW_16)
			jit_emit_opc1_reg_memrm(ctx->codebuf, 0, 1, OPC_MOVZX16, params[0],
			    params[1], NO_REG, 0, params[2], 0);
		else
			jit_emit_opc1_reg_memrm(ctx->codebuf, 0, 1, OPC_MOVZX8, params[0],
			    params[1], NO_REG, 0, params[2], 0);
		break;

	case JITOP_LDRBPO_SEXT:
		if (dw == JITOP_DW_64)
			jit_emit_opc1_reg_memrm(ctx->codebuf, w64, 0, OPC_MOV_REG_RM, params[0],
			    params[1], NO_REG, 0, params[2], 0);
		else if (dw == JITOP_DW_32)
			jit_emit_opc1_reg_memrm(ctx->codebuf, 1, 0, OPC_MOVSX32, params[0],
			    params[1], NO_REG, 0, params[2], 0);
		else if (dw == JITOP_DW_16)
			jit_emit_opc1_reg_memrm(ctx->codebuf, 1, 1, OPC_MOVSX16, params[0],
			    params[1], NO_REG, 0, params[2], 0);
		else
			jit_emit_opc1_reg_memrm(ctx->codebuf, 1, 1, OPC_MOVSX8, params[0],
			    params[1], NO_REG, 0, params[2], 0);
		break;

	case JITOP_STRBPO:
		jit_emit_opc1_reg_memrm(ctx->codebuf, w64, 0, OPC_MOV_RM_REG, params[0],
					params[1], NO_REG, 0, params[2], 0);
		break;

	case JITOP_RET:
		jit_emit_ret_(ctx, ctx->codebuf);
		break;

	case JITOP_BRANCH:
		label = (jit_label_t)params[0];
		jit_emit_jmp(ctx, label);
		break;

	case JITOP_BCMP:
		label = (jit_label_t)params[0];
		jit_emit_cmp_reg_reg(ctx->codebuf, op_w64, params[2], params[3]);
		jit_emit_jcc(ctx, cc_to_code[params[1]], label);
		break;

	case JITOP_BCMPI:
		label = (jit_label_t)params[0];
		jit_emit_cmp_reg_imm32(ctx->codebuf, op_w64, params[2], params[3]);
		jit_emit_jcc(ctx, cc_to_code[params[1]], label);
		break;

	case JITOP_BNCMP:
		label = (jit_label_t)params[0];
		jit_emit_cmp_reg_reg(ctx->codebuf, op_w64, params[2], params[3]);
		jit_emit_jncc(ctx, cc_to_code[params[1]], label);
		break;

	case JITOP_BNCMPI:
		label = (jit_label_t)params[0];
		jit_emit_cmp_reg_imm32(ctx->codebuf, op_w64, params[2], params[3]);
		jit_emit_jncc(ctx, cc_to_code[params[1]], label);
		break;

	case JITOP_BTEST:
		label = (jit_label_t)params[0];
		jit_emit_test_reg_reg(ctx->codebuf, op_w64, params[2], params[3]);
		jit_emit_jcc(ctx, test_cc_to_code[params[1]], label);
		break;

	case JITOP_BTESTI:
		label = (jit_label_t)params[0];
		jit_emit_test_reg_imm32(ctx->codebuf, op_w64, params[2], params[3]);
		jit_emit_jcc(ctx, test_cc_to_code[params[1]], label);
		break;

	case JITOP_BNTEST:
		label = (jit_label_t)params[0];
		jit_emit_test_reg_reg(ctx->codebuf, op_w64, params[2], params[3]);
		jit_emit_jncc(ctx, test_cc_to_code[params[1]], label);
		break;

	case JITOP_BNTESTI:
		label = (jit_label_t)params[0];
		jit_emit_test_reg_imm32(ctx->codebuf, op_w64, params[2], params[3]);
		jit_emit_jncc(ctx, test_cc_to_code[params[1]], label);
		break;

	case JITOP_SET_LABEL:
		label = (jit_label_t)params[0];
		label->tgt_info = ctx->codebuf->emit_ptr;
		break;

	case JITOP_CMOV:
		jit_emit_cmp_reg_reg(ctx->codebuf, op_w64, params[3], params[4]);
		jit_emit_cmovcc_reg_reg(ctx->codebuf, w64, cc_to_code[params[2]], params[0], params[1]);
		break;

	case JITOP_CMOVI:
		jit_emit_cmp_reg_imm32(ctx->codebuf, op_w64, params[3], params[4]);
		jit_emit_cmovcc_reg_reg(ctx->codebuf, w64, cc_to_code[params[2]], params[0], params[1]);
		break;

	case JITOP_CSEL:
		jit_emit_cmp_reg_reg(ctx->codebuf, op_w64, params[4], params[5]);
		jit_emit_cmovcc_reg_reg(ctx->codebuf, w64, cc_to_code[params[2]], params[0], params[1]);
		jit_emit_cmovcc_reg_reg(ctx->codebuf, w64, 1^cc_to_code[params[2]], params[0], params[2]);
		break;

	case JITOP_CSELI:
		jit_emit_cmp_reg_imm32(ctx->codebuf, op_w64, params[4], params[5]);
		jit_emit_cmovcc_reg_reg(ctx->codebuf, w64, cc_to_code[params[2]], params[0], params[1]);
		jit_emit_cmovcc_reg_reg(ctx->codebuf, w64, 1^cc_to_code[params[2]], params[0], params[2]);
		break;

	case JITOP_CSET:
		jit_emit_cmp_reg_reg(ctx->codebuf, op_w64, params[2], params[3]);
		jit_emit_xor_reg_reg(ctx->codebuf, w64, params[0], params[0]);
		jit_emit_setcc_reg(ctx->codebuf, w64, cc_to_code[params[1]], params[0]);
		break;

	case JITOP_CSETI:
		jit_emit_cmp_reg_imm32(ctx->codebuf, op_w64, params[2], params[3]);
		jit_emit_xor_reg_reg(ctx->codebuf, w64, params[0], params[0]);
		jit_emit_setcc_reg(ctx->codebuf, w64, cc_to_code[params[1]], params[0]);
		break;

	case JITOP_TMOV:
		jit_emit_test_reg_reg(ctx->codebuf, op_w64, params[3], params[4]);
		jit_emit_cmovcc_reg_reg(ctx->codebuf, w64, test_cc_to_code[params[2]], params[0], params[1]);
		break;

	case JITOP_TMOVI:
		jit_emit_test_reg_imm32(ctx->codebuf, op_w64, params[3], params[4]);
		jit_emit_cmovcc_reg_reg(ctx->codebuf, w64, test_cc_to_code[params[2]], params[0], params[1]);
		break;

	case JITOP_TSEL:
		jit_emit_test_reg_reg(ctx->codebuf, op_w64, params[4], params[5]);
		jit_emit_cmovcc_reg_reg(ctx->codebuf, w64, test_cc_to_code[params[2]], params[0], params[1]);
		jit_emit_cmovcc_reg_reg(ctx->codebuf, w64, 1^test_cc_to_code[params[2]], params[0], params[2]);
		break;

	case JITOP_TSELI:
		jit_emit_test_reg_imm32(ctx->codebuf, op_w64, params[4], params[5]);
		jit_emit_cmovcc_reg_reg(ctx->codebuf, w64, test_cc_to_code[params[2]], params[0], params[1]);
		jit_emit_cmovcc_reg_reg(ctx->codebuf, w64, 1^test_cc_to_code[params[2]], params[0], params[2]);
		break;

	case JITOP_TSET:
		jit_emit_test_reg_reg(ctx->codebuf, op_w64, params[2], params[3]);
		jit_emit_xor_reg_reg(ctx->codebuf, w64, params[0], params[0]);
		jit_emit_setcc_reg(ctx->codebuf, w64, test_cc_to_code[params[1]], params[0]);
		break;

	case JITOP_TSETI:
		jit_emit_test_reg_imm32(ctx->codebuf, op_w64, params[2], params[3]);
		jit_emit_xor_reg_reg(ctx->codebuf, w64, params[0], params[0]);
		jit_emit_setcc_reg(ctx->codebuf, w64, test_cc_to_code[params[1]], params[0]);
		break;

	default:
		printf("unhandled: %s.%d\n", op_def[op].mnemonic, dw);
		break;
	}
}


void
jit_tgt_emit_fn_prologue(jit_ctx_t ctx, int cnt, uint64_t *params)
{
	jit_tmp_t tmp;
	jit_tmp_state_t ts;
	int i;
	int mem_offset = 0x10;

	for (i = 0; i < cnt; i++) {
		tmp = (jit_tmp_t)params[i];
		ts = GET_TMP_STATE(ctx, tmp);

		if (i < FN_ARG_REG_CNT) {
			ts->loc = JITLOC_REG;
			ts->mem_allocated = 0;
			ts->reg = fn_arg_regs[i];
			ts->dirty = 1;
			jit_regset_set(ctx->regs_used, ts->reg);
			ctx->reg_to_tmp[ts->reg] = tmp;
		} else {
			ts->loc = JITLOC_STACK;
			ts->mem_allocated = 1;
			ts->mem_base_reg = jit_tgt_stack_base_reg;
			ts->mem_offset = mem_offset;

			mem_offset += sizeof(uint64_t);
		}
	}

#ifdef JIT_FPO
#else
	ctx->spill_stack_offset = -((int)sizeof(uint64_t)*CALLEE_SAVED_REG_CNT);
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
	frame_size = _ALIGN_SZ(8*(CALLEE_SAVED_REG_CNT +
				  dyn_array_size(&ctx->local_tmps) +
				  dyn_array_size(&ctx->bb_tmps)), 16);
	jit_emit_sub_reg_imm32(&prologue_buf, 1, REG_RSP, frame_size);

	/* ^^^ equivalent to enter (but enter seems to take more cycles) */
	for (i = 0; i < CALLEE_SAVED_REG_CNT; i++) {
		reg = callee_saved_regs[i];
		if (!jit_regset_test(ctx->regs_ever_used, reg))
			continue;

		/* mov %reg, -(i*8)(%rbp) */
		jit_emit_opc1_reg_memrm(&prologue_buf, 1, 0, OPC_MOV_RM_REG, reg,
					REG_RBP, NO_REG, 0, -i*8, 0);
	}
#endif

	assert(prologue_buf.code_sz <= MAX_PROLOGUE_SZ);

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
		jit_emit_opc1_reg_memrm(ctx->codebuf, 1, 0, OPC_MOV_REG_RM, saved_reg,
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
	void **epi;
	struct x86_reloc *reloc;
	jit_label_t label;
	uintptr_t diff;
	int32_t disp;

	x86_ctx_t x86_ctx = (x86_ctx_t)ctx->tgt_ctx;

	emit_prologue(ctx);

	dyn_array_foreach(&x86_ctx->epilogues, epi) {
		orig_size = ctx->codebuf->code_sz;
		ctx->codebuf->emit_ptr = *epi;
		emit_epilogue(ctx);
		assert (ctx->codebuf->code_sz - orig_size <= MAX_EPILOGUE_SZ);
		ctx->codebuf->code_sz = orig_size;
	}

	dyn_array_foreach(&x86_ctx->relocs, reloc) {
		label = reloc->label;
		if ((uintptr_t)label->tgt_info > (uintptr_t)reloc->loc) {
			diff = (uintptr_t)label->tgt_info - (uintptr_t)reloc->loc - 4;
			disp = (int32_t)diff;
		} else {
			diff = (uintptr_t)reloc->loc - (uintptr_t)label->tgt_info + 4;
			disp = -(int32_t)diff;
		}

		assert (diff <= INT32_MAX);

		jit_emit32_at(reloc->loc, (uint32_t)disp);
	}
}

void
jit_tgt_ctx_init(jit_ctx_t ctx)
{
	x86_ctx_t x86_ctx;

	x86_ctx = malloc(sizeof(*x86_ctx));
	assert (x86_ctx != NULL);
	memset(x86_ctx, 0, sizeof(*x86_ctx));

	dyn_array_init(&x86_ctx->epilogues, sizeof(void *), 4, NULL, NULL, NULL);
	dyn_array_init(&x86_ctx->relocs, sizeof(struct x86_reloc), 8, NULL, NULL,
	    NULL);

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
#if 0
	jit_regset_full(ctx->regs_ever_used);
#endif
}

struct cpuid_regs
{
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
};

/* Basic Feature Information */
#define CPUID_ENUMERATE_BFI	0x01
#define CPUID_BFI_ECX_AVX	(1 << 28)
#define CPUID_BFI_ECX_POPCNT	(1 << 23)

/* Structured Extended Feature Flags */
#define CPUID_ENUMERATE_SEFF	0x07
#define CPUID_SEFF_EBX_BMI1	(1 << 3)
#define CPUID_SEFF_EBX_HLE	(1 << 4)
#define CPUID_SEFF_EBX_AVX2	(1 << 5)
#define CPUID_SEFF_EBX_BMI2	(1 << 8)

/* Extended Function */
#define CPUID_ENUMERATE_EF	0x80000001U
#define CPUID_EF_ECX_LZCNT	(1 << 5)

static
void
cpuid(struct cpuid_regs *regs, uint32_t eax)
{
	__asm__ __volatile__("cpuid"
	    : "=a" (regs->eax),
	      "=b" (regs->ebx),
	      "=c" (regs->ecx),
	      "=d" (regs->edx)
	    : "a"  (eax),
	      "c"  (0)
	    : "memory");

	return;
}

void
jit_tgt_init(void)
{
	struct cpuid_regs r;

	cpuid(&r, CPUID_ENUMERATE_BFI);
	have_popcnt = (r.ecx & CPUID_BFI_ECX_POPCNT);

	cpuid(&r, CPUID_ENUMERATE_SEFF);
	have_bmi1 = (r.ebx & CPUID_SEFF_EBX_BMI1);
	have_bmi2 = (r.ebx & CPUID_SEFF_EBX_BMI2);

	cpuid(&r, CPUID_ENUMERATE_EF);
	have_lzcnt = (r.ecx & CPUID_EF_ECX_LZCNT);

#if 0
	printf("have_popcnt=%s\n", have_popcnt ? "YES" : "NO");
	printf("have_lzcnt=%s\n", have_lzcnt ? "YES" : "NO");
	printf("have_bmi1=%s\n", have_bmi1 ? "YES" : "NO");
	printf("have_bmi2=%s\n", have_bmi2 ? "YES" : "NO");
#endif
	return;
}

struct jit_tgt_op_def const tgt_op_def[] = {
	[JITOP_AND]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	[JITOP_ANDI]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	[JITOP_OR]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	[JITOP_ORI]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	[JITOP_XOR]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	[JITOP_XORI]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	[JITOP_ADD]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	/* LEA r32/64, m XXX */
	[JITOP_ADDI]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	/* LEA r32/64, m XXX */
	[JITOP_SUB]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	[JITOP_SUBI]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	[JITOP_SHL]	= { .alias = "0", .o_restrict = "", .i_restrict = "-c" },
	[JITOP_SHLI]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	[JITOP_SHR]	= { .alias = "0", .o_restrict = "", .i_restrict = "-c" },
	[JITOP_SHRI]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	[JITOP_MOV]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_MOV_SEXT]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_MOVI]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_NOT]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	[JITOP_BSWAP]	= { .alias = "0", .o_restrict = "", .i_restrict = "" },
	[JITOP_CLZ]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	/* LZCNT (VEX stuff?) */
	[JITOP_BFE]     = { .alias = "", .o_restrict = "", .i_restrict = "" },
	/* BEXTR (needs VEX stuff...) */
	[JITOP_LDRI]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_LDRR]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_LDRBPO]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_LDRBPSO]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	/* MOVSX for dw={8, 16, 32} */
	[JITOP_LDRI_SEXT]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_LDRR_SEXT]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_LDRBPO_SEXT]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_LDRBPSO_SEXT]	= { .alias = "", .o_restrict = "", .i_restrict = "" },

	[JITOP_STRI]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_STRR]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_STRBPO]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_STRBPSO]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_BRANCH]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_BCMP]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_BCMPI]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_BNCMP]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_BNCMPI]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_BTEST]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_BTESTI]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_BNTEST]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_BNTESTI]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_RET]	= { .alias = "", .o_restrict = "", .i_restrict = "a" },
	[JITOP_RETI]	= { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_CMOV]    = { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_CMOVI]   = { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_CSEL]    = { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_CSELI]   = { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_CSET]    = { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_CSETI]   = { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_TMOV]    = { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_TMOVI]   = { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_TSEL]    = { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_TSELI]   = { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_TSET]    = { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_TSETI]   = { .alias = "", .o_restrict = "", .i_restrict = "" },
	[JITOP_SET_LABEL] = { .alias = "", .o_restrict = "", .i_restrict = "" },
};

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
#define DWARF_DEBUG_ENABLE 1

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
#ifdef DWARF_DEBUG_ENABLE
#include <libdwarf.h>
#include <dwarf.h>
#include "pmjit-gdb.h"
#endif
#include "pmjit.h"
#include "pmjit-internal.h"
#include "pmjit-tgt.h"

#define MAX_PROLOGUE_SZ		30
#define MAX_EPILOGUE_SZ		30
#define NO_REG ((uint8_t)-1)

#define OPC_MOV_REG_RM		0x8B
#define OPC_MOV_RM_REG		0x89
#define OPC_MOV_RM_REG8		0x88
#define OPC_MOV_REG_IMM32	0xB8
#define OPC_MOV_RM_IMM32	0xC7

#define OPC_XOR_REG_RM		0x33
#define OPC_XOR_RM_REG		0x31
#define OPC_XOR_RM_IMM32	0x81
#define OPC_XOR_EAX_IMM8	0x34
#define OPC_XOR_EAX_IMM32	0x35
#define OPC_XOR_RM_IMM8		0x80

#define OPC_AND_REG_RM		0x23
#define OPC_AND_RM_REG		0x21
#define OPC_AND_RM_IMM32	0x81
#define OPC_AND_EAX_IMM8	0x24
#define OPC_AND_EAX_IMM32	0x25
#define OPC_AND_RM_IMM8		0x80

#define OPC_OR_REG_RM		0x0B
#define OPC_OR_RM_REG		0x09
#define OPC_OR_RM_IMM32		0x81
#define OPC_OR_EAX_IMM8		0x0C
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
#define OPC_JMP_REL8		0xEB
#define OPC_JMP_CC		0x80
#define OPC_JMP_CC_REL8		0x70

#define OPC_CALL_REL32		0xE8
#define OPC_CALL_RM		0xFF

#define OPC_INC			0xFF
#define OPC_DEC			0xFF

#define OPC_RET			0xC3

#define OPC_SET_CC		0x90
#define OPC_CMOV_CC		0x40

#define OPC_PUSH_REG		0x50
#define OPC_PUSH_RM		0xFF
#define OPC_POP_REG		0x58
#define OPC_POP_RM		0x8F
#define OPC_ENTER		0xC8
#define OPC_LEAVE		0xC9

#define OPC_LZCNT		0xBD

#define OPC_XCHG_REG_RM		0x87
#define OPC_XCHG_EAX_REG	0x90

#define OPC_SHIFT_RM_CL		0xD3
#define OPC_TEST_RM_REG		0x85
#define OPC_TEST_RM_IMM32	0xF7
#define OPC_TEST_RM_IMM64	0xF7
#define MODRM_REG_TEST_IMM	0x00 /* XXX: or 0x01 - what do they mean? */

#define PREFIX_OP_SIZE		0x66

/* ModR/M mod field values */
#define MODRM_MOD_DISP0		0x0
#define MODRM_MOD_DISP1B	0x1
#define MODRM_MOD_DISP4B	0x2
#define MODRM_MOD_RM_REG	0x3

/* ModR/M reg field special values */
#define MODRM_REG_MOV_IMM	0x0
#define MODRM_REG_AND_IMM	0x4
#define MODRM_REG_CMP_IMM	0x7
#define MODRM_REG_OR_IMM	0x1
#define MODRM_REG_XOR_IMM	0x6
#define MODRM_REG_SHL		0x4
#define MODRM_REG_SHR		0x5
#define MODRM_REG_ADD_IMM	0x0
#define MODRM_REG_SUB_IMM	0x5
#define MODRM_REG_NOT		0x2
#define MODRM_REG_SETCC		0x0
#define MODRM_REG_INC		0x0
#define MODRM_REG_DEC		0x1
#define MODRM_REG_CALL_NEAR	0x2
#define MODRM_REG_PUSH		0x6
#define MODRM_REG_POP		0x0


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

#ifdef DWARF_DEBUG_ENABLE
	Dwarf_P_Debug	dw;
#endif
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

#ifdef DWARF_DEBUG_ENABLE
/* From System V ABI for AMD64 */
#define DW_REG_RAX	0
#define DW_REG_RDX	1
#define DW_REG_RCX	2
#define DW_REG_RBX	3
#define DW_REG_RSI	4
#define DW_REG_RDI	5
#define DW_REG_FP	6
#define DW_REG_SP	7
#define DW_REG_R8	8
#define DW_REG_R9	9
#define DW_REG_R10	10
#define DW_REG_R11	11
#define DW_REG_R12	12
#define DW_REG_R13	13
#define DW_REG_R14	14
#define DW_REG_R15	15
#define DW_REG_RA	16


static const int reg_to_dwarf_reg[] = {
	[REG_RAX]	= DW_REG_RAX,
	[REG_RDX]	= DW_REG_RDX,
	[REG_RCX]	= DW_REG_RCX,
	[REG_RBX]	= DW_REG_RBX,
	[REG_RSP]	= DW_REG_SP,
	[REG_RBP]	= DW_REG_FP,
	[REG_RSI]	= DW_REG_RSI,
	[REG_RDI]	= DW_REG_RDI,
	[REG_R8]	= DW_REG_R9,
	[REG_R9]	= DW_REG_R8,
	[REG_R10]	= DW_REG_R10,
	[REG_R11]	= DW_REG_R11,
	[REG_R12]	= DW_REG_R12,
	[REG_R13]	= DW_REG_R13,
	[REG_R14]	= DW_REG_R14,
	[REG_R15]	= DW_REG_R15
};

#ifdef JIT_FPO
#else
static uint8_t dw_init_insns[] = {
	DW_CFA_def_cfa_sf, DW_REG_SP, 1,
	DW_CFA_offset | DW_REG_RA, 1
};
#endif
#endif

static const int reg_empty_weight[] = {
	/*
	 * Weights:
	 *   0: callee-saved
	 *   1: special-purpose for some instructions
	 *   2: all others
	 */
	[REG_RAX]	= 2,
	[REG_RCX]	= 1,
	[REG_RDX]	= 2,
	[REG_RBX]	= 0,
	[REG_RBP]	= 0,
	[REG_RSI]	= 2,
	[REG_RDI]	= 2,
	[REG_R8]	= 2,
	[REG_R9]	= 2,
	[REG_R10]	= 2,
	[REG_R11]	= 2,
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

const int jit_tgt_have_xchg = 1;
const int jit_tgt_prefer_xchg = 1;

static int have_popcnt;
static int have_lzcnt;
static int have_bmi1;
static int have_bmi2;

static
int32_t
calculate_disp32(uintptr_t tgt, uintptr_t reloc)
{
	uintptr_t diff;
	int32_t disp;

	if ((uintptr_t)tgt > (uintptr_t)reloc) {
		diff = (uintptr_t)tgt - (uintptr_t)reloc - 4;
		disp = (int32_t)diff;
	} else {
		diff = (uintptr_t)reloc - (uintptr_t)tgt + 4;
		disp = -(int32_t)diff;
	}

	assert (diff <= INT32_MAX);

	return disp;
}

static
int8_t
calculate_disp8(uintptr_t tgt, uintptr_t reloc)
{
	uintptr_t diff;
	int8_t disp;

	if ((uintptr_t)tgt > (uintptr_t)reloc) {
		diff = (uintptr_t)tgt - (uintptr_t)reloc - 1;
		disp = (int8_t)diff;
	} else {
		diff = (uintptr_t)reloc - (uintptr_t)tgt + 1;
		disp = -(int8_t)diff;
	}

	assert (diff <= INT8_MAX);

	return disp;
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
jit_emit_opc1_reg_memrm_abs32(jit_codebuf_t code, int w64, int zerof_prefix, int force_rex, uint8_t opc, uint8_t reg, uint32_t abs32)
{
	int reg_ext = reg & 0x8;
	uint8_t mod;
	uint8_t rm;
	uint8_t modrm;

	mod = MODRM_MOD_DISP0;

	reg &= 0x7;
	rm = REG_RSP;

	modrm = ((mod & 0x3) << 6) | ((reg & 0x7) << 3) | (rm & 0x7);

	if (w64 || reg_ext || force_rex)
		jit_emit_rex(code, w64, reg_ext, 0, 0);

	if (zerof_prefix)
		jit_emit8(code, OPC_ZEROF_PREFIX);

	jit_emit8(code, opc);
	jit_emit8(code, modrm);

	jit_emit_sib(code, 1, REG_RSP, REG_RBP);

	jit_emit32(code, abs32);
}

static
void
jit_emit_opc1_reg_memrm(jit_codebuf_t code, int w64, int zerof_prefix, int force_rex, uint8_t opc, uint8_t reg, uint8_t reg_base, uint8_t reg_index, int scale, int32_t disp, uint64_t abs)
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

	/*
	 * If it's an immediate memory address, use [disp32] addressing if it
	 * fits into disp32.
	 */
	if (rip_relative && check_unsigned32(abs)) {
		jit_emit_opc1_reg_memrm_abs32(code, w64, zerof_prefix, force_rex,
		    opc, reg, (uint32_t)abs);

		return;
	}

	/* XXX: don't particularly like it, but abs is used for rip-relative */
	if (rip_relative)
		assert ((disp == 0) && (scale == 0) && no_base && no_index);

	use_sib =
	    /* Use sib if an index was specified */
	    (!no_index) ||
	    /* or if RSP or R12 are being used as base */
	    (!no_base && ((reg_base == REG_RSP) || (reg_base == REG_R12)));

	/* When we use SIB without an index, force scale to 1 */
	if (use_sib && no_index) {
		scale = 1;
		reg_index = REG_RSP;
	}

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

	if (w64 || reg_ext || base_ext || index_ext || force_rex)
		jit_emit_rex(code, w64, reg_ext, index_ext, base_ext);

	if (zerof_prefix)
		jit_emit8(code, OPC_ZEROF_PREFIX);

	jit_emit8(code, opc);
	jit_emit8(code, modrm);

	if (use_sib)
		jit_emit_sib(code, scale, reg_index, reg_base);

	if (use_disp) {
		if (rip_relative) {
			disp = calculate_disp32(abs, (uintptr_t)code->emit_ptr);
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
jit_emit_opc1_reg_regrm2(jit_codebuf_t code, int w64, int f3_prefix, int zerof_prefix, int force_rex, uint8_t opc, uint8_t reg,
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

	if (f3_prefix)
		jit_emit8(code, 0xF3);

	if (zerof_prefix)
		jit_emit8(code, OPC_ZEROF_PREFIX);

	jit_emit8(code, opc);
	jit_emit8(code, modrm);
}

static
void
jit_emit_opc1_reg_regrm(jit_codebuf_t code, int w64, int zerof_prefix, int force_rex, uint8_t opc, uint8_t reg,
    uint8_t rm_reg)
{
	jit_emit_opc1_reg_regrm2(code, w64, 0, zerof_prefix, force_rex, opc, reg, rm_reg);
}

static
void
jit_emit_common_load(jit_codebuf_t code, int dw, uint8_t reg, uint8_t reg_base, uint8_t reg_index, int scale, int32_t disp, uint64_t abs)
{
	int w64 = (JITOP_DW_64);

	if (dw == JITOP_DW_64 || dw == JITOP_DW_32)
		jit_emit_opc1_reg_memrm(code, w64, 0, 0, OPC_MOV_REG_RM, reg,
		    reg_base, reg_index, scale, disp, abs);
	else if (dw == JITOP_DW_16)
		jit_emit_opc1_reg_memrm(code, 0, 1, 0, OPC_MOVZX16, reg,
		    reg_base, reg_index, scale, disp, abs);
	else
		jit_emit_opc1_reg_memrm(code, 0, 1, 0, OPC_MOVZX8, reg,
		    reg_base, reg_index, scale, disp, abs);
}

static
void
jit_emit_common_load_sext(jit_codebuf_t code, int dw, uint8_t reg, uint8_t reg_base, uint8_t reg_index, int scale, int32_t disp, uint64_t abs)
{
	int w64 = (JITOP_DW_64);

	if (dw == JITOP_DW_64)
		jit_emit_opc1_reg_memrm(code, w64, 0, 0, OPC_MOV_REG_RM, reg,
		    reg_base, reg_index, scale, disp, abs);
	else if (dw == JITOP_DW_32)
		jit_emit_opc1_reg_memrm(code, 1, 0, 0, OPC_MOVSX32, reg,
		    reg_base, reg_index, scale, disp, abs);
	else if (dw == JITOP_DW_16)
		jit_emit_opc1_reg_memrm(code, 1, 1, 0, OPC_MOVSX16, reg,
		    reg_base, reg_index, scale, disp, abs);
	else
		jit_emit_opc1_reg_memrm(code, 1, 1, 0, OPC_MOVSX8, reg,
		    reg_base, reg_index, scale, disp, abs);
}

static
void
jit_emit_common_store(jit_codebuf_t code, int dw, uint8_t reg, uint8_t reg_base, uint8_t reg_index, int scale, int32_t disp, uint64_t abs)
{
	int w64 = (JITOP_DW_64);

	if (dw == JITOP_DW_64 || JITOP_DW_32) {
		jit_emit_opc1_reg_memrm(code, w64, 0, 0, OPC_MOV_RM_REG, reg,
		    reg_base, reg_index, scale, disp, abs);
	} else if (dw == JITOP_DW_16) {
		jit_emit8(code, PREFIX_OP_SIZE);
		jit_emit_opc1_reg_memrm(code, 0, 0, 0, OPC_MOV_RM_REG, reg,
		    reg_base, reg_index, scale, disp, abs);
	} else {
		jit_emit_opc1_reg_memrm(code, 0, 0, 1, OPC_MOV_RM_REG8, reg,
		    reg_base, reg_index, scale, disp, abs);
	}
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
jit_emit_xchg_reg_reg(jit_codebuf_t code, int w64, uint8_t reg1, uint8_t reg2)
{
	int reg1_ext = reg1 & 0x8;
	int reg2_ext = reg2 & 0x8;

	if (reg1 == REG_RAX) {
		if (reg2_ext || w64)
			jit_emit_rex(code, w64, reg2_ext, 0, 0);

		jit_emit8(code, OPC_XCHG_EAX_REG + (reg2 & 0x7));
	} else if (reg2 == REG_RAX) {
		if (reg1_ext || w64)
			jit_emit_rex(code, w64, reg1_ext, 0, 0);

		jit_emit8(code, OPC_XCHG_EAX_REG + (reg1 & 0x7));
	} else {
		jit_emit_opc1_reg_regrm(code, w64, 0, 0,
		    OPC_XCHG_REG_RM,
		    reg1, reg2);
	}
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
jit_emit_inc_reg(jit_codebuf_t code, int w64, uint8_t regrm)
{
	jit_emit_opc1_reg_regrm(code, w64, 0, 0, OPC_INC, MODRM_REG_INC, regrm);
}

static
void
jit_emit_dec_reg(jit_codebuf_t code, int w64, uint8_t regrm)
{
	jit_emit_opc1_reg_regrm(code, w64, 0, 0, OPC_DEC, MODRM_REG_DEC, regrm);
}

static
void
jit_emit_mov_reg_imm32(jit_codebuf_t code, uint8_t dst_reg, uint32_t imm)
{
	int reg_ext = dst_reg & 0x8;

	dst_reg &= 0x7;

	if (reg_ext)
		jit_emit_rex(code, 0, 0, 0, reg_ext);

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

	jit_emit_rex(code, 1, 0, 0, reg_ext);
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
		jit_emit_rex(code, 1, 0, 0, reg_ext);

	jit_emit8(code, OPC_PUSH_REG + reg);
}

static
void
jit_emit_pop_reg(jit_codebuf_t code, uint8_t reg)
{
	int reg_ext = reg & 0x8;

	reg &= 0x7;

	if (reg_ext)
		jit_emit_rex(code, 1, 0, 0, reg_ext);

	jit_emit8(code, OPC_POP_REG + reg);
}

static
void
jit_emit_bswap_reg(jit_codebuf_t code, int w64, uint8_t reg)
{
	int reg_ext = reg & 0x8;

	reg &= 0x7;

	if (reg_ext || w64)
		jit_emit_rex(code, w64, 0, 0, reg_ext);

	jit_emit8(code, OPC_ZEROF_PREFIX);
	jit_emit8(code, OPC_BSWAP_REG + reg);
}

static
void
jit_emit_lzcnt_reg_reg(jit_codebuf_t code, int w64, uint8_t reg, uint8_t rm_reg)
{
	jit_emit_opc1_reg_regrm2(code, w64, 1, 1, 0, OPC_LZCNT, reg, rm_reg);
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
		jit_emit8(code, use_imm8 ? OPC_AND_EAX_IMM8 : OPC_AND_EAX_IMM32);
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
		jit_emit8(code, use_imm8 ? OPC_OR_EAX_IMM8 : OPC_OR_EAX_IMM32);
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
		jit_emit8(code, use_imm8 ? OPC_XOR_EAX_IMM8 : OPC_XOR_EAX_IMM32);
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
track_reloc(jit_ctx_t ctx, jit_label_t label, uintptr_t loc)
{
	struct x86_ctx *x86_ctx = (struct x86_ctx *)ctx->tgt_ctx;
	struct x86_reloc *reloc;

	reloc = dyn_array_new_elem(&x86_ctx->relocs);
	reloc->loc = (void *)loc;
	reloc->label = label;
}

static
uintptr_t
jit_emit_jmp_internal(jit_codebuf_t code)
{
	uintptr_t reloc;

	jit_emit8(code, OPC_JMP);
	reloc = (uintptr_t)code->emit_ptr;
	jit_emit32(code, 0xDEADC0DE);

	return reloc;
}

static
uintptr_t
jit_emit_jcc_internal(jit_codebuf_t code, int cc)
{
	uintptr_t reloc;

	jit_emit8(code, OPC_ZEROF_PREFIX);
	jit_emit8(code, OPC_JMP_CC + cc);
	reloc = (uintptr_t)code->emit_ptr;
	jit_emit32(code, 0xDEADC0DE);

	return reloc;
}

static
uintptr_t
jit_emit_jncc_internal(jit_codebuf_t code, int cc)
{
	uintptr_t reloc;

	jit_emit8(code, OPC_ZEROF_PREFIX);
	jit_emit8(code, OPC_JMP_CC + (1 ^cc));
	reloc = (uintptr_t)code->emit_ptr;
	jit_emit32(code, 0xDEADC0DE);

	return reloc;
}

static
uintptr_t
jit_emit_jmp_rel8_internal(jit_codebuf_t code)
{
	uintptr_t reloc;

	jit_emit8(code, OPC_JMP_REL8);
	reloc = (uintptr_t)code->emit_ptr;
	jit_emit8(code, 0x00);

	return reloc;
}

static
uintptr_t
jit_emit_jcc_rel8_internal(jit_codebuf_t code, int cc)
{
	uintptr_t reloc;

	jit_emit8(code, OPC_JMP_CC_REL8 + cc);
	reloc = (uintptr_t)code->emit_ptr;
	jit_emit8(code, 0x00);

	return reloc;
}

static
void
jit_emit_jmp(jit_ctx_t ctx, jit_label_t label)
{
	track_reloc(ctx, label, jit_emit_jmp_internal(ctx->codebuf));
}

static
void
jit_emit_jcc(jit_ctx_t ctx, int cc, jit_label_t label)
{
	track_reloc(ctx, label, jit_emit_jcc_internal(ctx->codebuf, cc));
}

static
void
jit_emit_jncc(jit_ctx_t ctx, int cc, jit_label_t label)
{

	track_reloc(ctx, label, jit_emit_jncc_internal(ctx->codebuf, cc));
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

static
void
jit_emit_bsr_reg_reg(jit_codebuf_t code, int w64, uint8_t reg, uint8_t rmreg)
{
	jit_emit_opc1_reg_regrm(code, w64, 1, 0, OPC_BSR_REG, reg, rmreg);
}

static
void
jit_emit_sw_lzcnt_reg_reg(jit_codebuf_t code, int w64, uint8_t dst_reg, uint8_t src_reg)
{
	uintptr_t label;
	uintptr_t label_reloc;

	jit_emit_bsr_reg_reg(code, w64, dst_reg, src_reg);
	label_reloc = jit_emit_jcc_rel8_internal(code, cc_to_code[CMP_NE] /* JNZ */);
	jit_emit_mov_reg_imm32(code, dst_reg, w64 ? 127 : 63);

	label = (uintptr_t)code->emit_ptr;
	jit_emit_xor_reg_imm32(code, w64, dst_reg, w64 ? 63 : 31);

	jit_emit8_at((void *)label_reloc,
	    calculate_disp8(label, label_reloc));
}

static
void
jit_emit_sw_bfe_reg(jit_codebuf_t code, int w64, uint8_t dst_reg, uint8_t src_reg, int lsb, int len)
{
	uint32_t mask32;
	uint64_t mask64;

	mask32 = 0xFFFFFFFFUL >> (32-len);
	mask64 = 0xFFFFFFFFFFFFFFFFULL >> (64-len);
	mask64 <<= lsb;

	if (w64) {
		if (check_signed32(mask64))
			jit_emit_mov_reg_simm32(code, w64, dst_reg, mask64);
		else if (check_unsigned32(mask64))
			jit_emit_mov_reg_imm32(code, dst_reg, mask64);
		else
			jit_emit_mov_reg_imm64(code, dst_reg, mask64);
		jit_emit_and_reg_reg(code, w64, dst_reg, src_reg);
		if (lsb > 0)
			jit_emit_shift_reg_imm8(code, w64, 0, dst_reg, lsb);
	} else {
		jit_emit_mov_reg_reg(code, w64, dst_reg, src_reg);
		if (lsb > 0)
			jit_emit_shift_reg_imm8(code, w64, 0, dst_reg, lsb);
		jit_emit_and_reg_imm32(code, w64, dst_reg, mask32);
	}
}

int
jit_tgt_reg_empty_weight(jit_ctx_t ctx, int reg)
{
	return reg_empty_weight[reg];
}

static
int
check_pc_rel32s(jit_ctx_t ctx, uint64_t imm)
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
		return check_pc_rel32s(ctx, imm) || check_unsigned32(imm);

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

/*
 * Returns how many extra registers are needed, depending
 * on a CPU-feature check.
 */
int
jit_tgt_feature_check(jit_ctx_t ctx, jit_op_t op)
{
	switch (op) {
	case JITOP_CLZ:
		return (have_lzcnt) ? -1 : 0;
	case JITOP_BFE:
		return 0;
	default:
		return -1;
	}
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

	uint64_t mem_abs;
	int32_t mem_disp;
	int mem_scale;
	uint8_t mem_reg_index;
	uint8_t mem_reg_base;

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

	case JITOP_XCHG:
		jit_emit_xchg_reg_reg(ctx->codebuf, w64, params[0], params[1]);
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

	case JITOP_SHL:
	case JITOP_SHR:
		assert (params[0] == params[1]);
		assert (params[2] == REG_RCX);
		jit_emit_shift_reg_cl(ctx->codebuf, w64, (op == JITOP_SHL), params[1]);
		break;

	case JITOP_SHLI:
	case JITOP_SHRI:
		assert (params[0] == params[1]);
		jit_emit_shift_reg_imm8(ctx->codebuf, w64, (op == JITOP_SHLI), params[1],
		    (int)params[2]);
		break;

	case JITOP_LDRBPSI:
		jit_emit_common_load(ctx->codebuf, dw, params[0], params[1], params[2],
		    params[3], 0, 0);
		break;
	case JITOP_LDRBPO:
		jit_emit_common_load(ctx->codebuf, dw, params[0], params[1], NO_REG, 0,
		    params[2], 0);
		break;
	case JITOP_LDRR:
		jit_emit_common_load(ctx->codebuf, dw, params[0], params[1], NO_REG, 0, 0,
		    0);
		break;
	case JITOP_LDRI:
		jit_emit_common_load(ctx->codebuf, dw, params[0], NO_REG, NO_REG, 0, 0,
		    params[1]);
		break;

	case JITOP_LDRBPSI_SEXT:
		jit_emit_common_load_sext(ctx->codebuf, dw, params[0], params[1], params[2],
		    params[3], 0, 0);
		break;
	case JITOP_LDRBPO_SEXT:
		jit_emit_common_load_sext(ctx->codebuf, dw, params[0], params[1], NO_REG, 0,
		    params[2], 0);
		break;
	case JITOP_LDRR_SEXT:
		jit_emit_common_load_sext(ctx->codebuf, dw, params[0], params[1], NO_REG, 0,
		    0, 0);
		break;
	case JITOP_LDRI_SEXT:
		jit_emit_common_load_sext(ctx->codebuf, dw, params[0], NO_REG, NO_REG, 0, 0,
		    params[1]);
		break;

	case JITOP_STRBPSI:
		jit_emit_common_store(ctx->codebuf, dw, params[0], params[1], params[2],
		    params[3], 0, 0);
		break;
	case JITOP_STRBPO:
		jit_emit_common_store(ctx->codebuf, dw, params[0], params[1], NO_REG, 0,
		    params[2], 0);
		break;
	case JITOP_STRR:
		jit_emit_common_store(ctx->codebuf, dw, params[0], params[1], NO_REG, 0, 0,
		    0);
		break;
	case JITOP_STRI:
		jit_emit_common_store(ctx->codebuf, dw, params[0], NO_REG, NO_REG, 0, 0,
		    params[1]);
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

	case JITOP_NOT:
		assert (params[0] == params[1]);
		jit_emit_not_(ctx->codebuf, w64, params[0]);
		break;

	case JITOP_BSWAP:
		assert (params[0] == params[1]);
		jit_emit_bswap_reg(ctx->codebuf, w64, params[0]);
		break;

	case JITOP_BFE:
		jit_emit_sw_bfe_reg(ctx->codebuf, w64, params[0], params[1], params[2], params[3]);
		break;

	case JITOP_CLZ:
		if (have_lzcnt) {
			jit_emit_lzcnt_reg_reg(ctx->codebuf, w64, params[0], params[1]);
		} else {
			jit_emit_sw_lzcnt_reg_reg(ctx->codebuf, w64, params[0], params[1]);
		}
		break;


	default:
		printf("unhandled: %s.%d\n", op_def[op].mnemonic, dw);
		break;
	}
}

void
jit_tgt_setup_call(jit_ctx_t ctx, int cnt, uint64_t *params, int no_emit)
{
	jit_tmp_t tmp;
	jit_tmp_state_t ts;
	int i;

	/*
	 * params[0]: output temp
	 * params[1..cnt-1]: input temps
	 */
	int in_temps = (cnt - 1);
	int32_t mem_offset = 0;
	int32_t stack_disp = (in_temps > FN_ARG_REG_CNT) ?
	    (in_temps - FN_ARG_REG_CNT)*sizeof(uint64_t) : 0;

	stack_disp = _ALIGN_SZ(stack_disp, 32);

	/* Set up output temp */
	tmp = (jit_tmp_t)params[0];
	ts = GET_TMP_STATE(ctx, tmp);
	ts->call_info.loc = JITLOC_REG;
	ts->call_info.reg = REG_RAX;

	params++;

	for (i = 0; i < cnt-1; i++) {
		tmp = (jit_tmp_t)params[i];
		ts = GET_TMP_STATE(ctx, tmp);

		if (i < FN_ARG_REG_CNT) {
			ts->call_info.loc = JITLOC_REG;
			ts->call_info.reg = fn_arg_regs[i];
		} else {
			ts->call_info.loc = JITLOC_STACK;
			ts->call_info.mem_base_reg = REG_RSP;
			ts->call_info.mem_offset = mem_offset;

			mem_offset += sizeof(uint64_t);
		}
	}

	if (!no_emit && stack_disp > 0)
		jit_emit_sub_reg_imm32(ctx->codebuf, 1, REG_RSP, stack_disp);
}

void
jit_tgt_emit_call(jit_ctx_t ctx, int cnt, uint64_t *params)
{
	/*
	 * params[0]: function pointer
	 * params[1]: output temp
	 * params[2..cnt-1]: input temps
	 */
	int in_temps = (cnt - 2);
	int32_t stack_disp = (in_temps > FN_ARG_REG_CNT) ?
	    (in_temps - FN_ARG_REG_CNT)*sizeof(uint64_t) : 0;
	uintptr_t fn_ptr = params[0];
	int32_t disp32;
	uint8_t modrm;

	stack_disp = _ALIGN_SZ(stack_disp, 32);

	if (check_pc_rel32s(ctx, fn_ptr)) {
		/* Can use RIP-relative call */
		jit_emit8(ctx->codebuf, OPC_CALL_REL32);
		disp32 = calculate_disp32(fn_ptr, (uintptr_t)ctx->codebuf->emit_ptr);
		jit_emit32(ctx->codebuf, disp32);
	} else {
		/* Move into a scratch register first */
		if (check_unsigned32(fn_ptr))
			jit_emit_mov_reg_imm32(ctx->codebuf, REG_RAX, fn_ptr);
		else
			jit_emit_mov_reg_imm64(ctx->codebuf, REG_RAX, fn_ptr);

		jit_emit_opc1_reg_regrm(ctx->codebuf, 0, 0, 0, OPC_CALL_RM,
		    MODRM_REG_CALL_NEAR, REG_RAX);
	}

	/* Restore stack pointer */
	if (stack_disp > 0)
		jit_emit_add_reg_imm32(ctx->codebuf, 1, REG_RSP, stack_disp);
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
	ctx->spill_stack_offset = -((int)sizeof(uint64_t)*(CALLEE_SAVED_REG_CNT+1));
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
	uint8_t *prev_emit_ptr;
	x86_ctx_t x86_ctx = (x86_ctx_t)ctx->tgt_ctx;

#ifdef DWARF_DEBUG_ENABLE
	Dwarf_Error dw_err = 0;
	Dwarf_Unsigned cie;
	Dwarf_P_Fde fde;
	Dwarf_P_Die die0, die1, die2;

	cie = dwarf_add_frame_cie(x86_ctx->dw, __DECONST(char *, ""),
	    /* code alignment factor */1,
	    /* data alignment factor */-8,
	    DW_REG_RA,
	    dw_init_insns,
	    sizeof(dw_init_insns),
	    &dw_err);
	assert (!dw_err);

	fde = dwarf_new_fde(x86_ctx->dw, &dw_err);
	assert (!dw_err);
#endif

#ifdef JIT_FPO
#else
	r = jit_init_codebuf(NULL, &prologue_buf);
	assert (r == 0);

	/* push %rbp */
	prev_emit_ptr = prologue_buf.emit_ptr;
	jit_emit_push_reg(&prologue_buf, REG_RBP);
#ifdef DWARF_DEBUG_ENABLE
	dwarf_add_fde_inst(fde,
	    DW_CFA_advance_loc, (prologue_buf.emit_ptr - prev_emit_ptr), 0,
	    &dw_err);
	assert (!dw_err);

	dwarf_add_fde_inst(fde,
	    DW_CFA_def_cfa_offset_sf, -2, 0,
	    &dw_err);
	assert (!dw_err);

	dwarf_add_fde_inst(fde,
	    DW_CFA_offset, DW_REG_FP, 2,
	    &dw_err);
	assert (!dw_err);
#endif

	/* mov  %rsp, %rbp */
	prev_emit_ptr = prologue_buf.emit_ptr;
	jit_emit_mov_reg_reg(&prologue_buf, 1, REG_RBP, REG_RSP);
#ifdef DWARF_DEBUG_ENABLE
	dwarf_add_fde_inst(fde,
	    DW_CFA_advance_loc, (prologue_buf.emit_ptr - prev_emit_ptr), 0,
	    &dw_err);
	assert (!dw_err);

	dwarf_add_fde_inst(fde,
	    DW_CFA_def_cfa_register, DW_REG_FP, 0,
	    &dw_err);
	assert (!dw_err);
#endif

	/* sub  $frame_size, %rsp */
	frame_size = _ALIGN_SZ(8*(CALLEE_SAVED_REG_CNT +
				  dyn_array_size(&ctx->local_tmps) +
				  dyn_array_size(&ctx->bb_tmps)), 16);

	prev_emit_ptr = prologue_buf.emit_ptr;
	jit_emit_sub_reg_imm32(&prologue_buf, 1, REG_RSP, frame_size);
#ifdef DWARF_DEBUG_ENABLE
	dwarf_add_fde_inst(fde,
	    DW_CFA_advance_loc, (prologue_buf.emit_ptr - prev_emit_ptr), 0,
	    &dw_err);
	assert (!dw_err);
#endif

	for (i = 0; i < CALLEE_SAVED_REG_CNT; i++) {
		reg = callee_saved_regs[i];
		if (!jit_regset_test(ctx->regs_ever_used, reg))
			continue;

		/* mov %reg, -((i+1)*8)(%rbp) */
		prev_emit_ptr = prologue_buf.emit_ptr;
		jit_emit_opc1_reg_memrm(&prologue_buf, 1, 0, 0, OPC_MOV_RM_REG, reg,
					REG_RBP, NO_REG, 0, -(i+1)*8, 0);
#ifdef DWARF_DEBUG_ENABLE
		dwarf_add_fde_inst(fde,
		    DW_CFA_advance_loc, (prologue_buf.emit_ptr - prev_emit_ptr), 0,
		    &dw_err);
		assert (!dw_err);

		dwarf_add_fde_inst(fde,
		    DW_CFA_offset, reg_to_dwarf_reg[reg], (3+i),
		    &dw_err);
		assert (!dw_err);
#endif

	}
#endif

	assert(prologue_buf.code_sz <= MAX_PROLOGUE_SZ);

	ctx->codebuf->code_ptr =
	    ctx->codebuf->code_ptr - prologue_buf.code_sz;

	ctx->codebuf->code_sz += prologue_buf.code_sz;

	memcpy(ctx->codebuf->code_ptr, prologue_buf.code_ptr, prologue_buf.code_sz);

#ifdef DWARF_DEBUG_ENABLE
	dwarf_add_frame_fde(x86_ctx->dw,
	    fde,
	    (Dwarf_P_Die)0,
	    cie,
	    /* virt_addr_of_described_code */0,
	    /* length_of_code */0,
	    /* symbol_index */0,
	    &dw_err);
	assert (!dw_err);

	die0 = dwarf_new_die(x86_ctx->dw, DW_TAG_compile_unit, NULL, NULL, NULL,
	    NULL, &dw_err);
	assert (die0 != NULL);

	dwarf_add_AT_targ_address(x86_ctx->dw, die0, DW_AT_low_pc,
	    (Dwarf_Unsigned)ctx->codebuf->code_ptr, 0, &dw_err);
	dwarf_add_AT_targ_address(x86_ctx->dw, die0, DW_AT_high_pc,
	    (Dwarf_Unsigned)(ctx->codebuf->code_ptr + ctx->codebuf->code_sz), 0, &dw_err);


	die1 = dwarf_new_die(x86_ctx->dw, DW_TAG_subprogram, die0, NULL, NULL,
	    NULL, &dw_err);
	assert (die1 != NULL);

	dwarf_add_AT_name(die1, __DECONST(char *, "jitted_func"), &dw_err);
	dwarf_add_AT_targ_address(x86_ctx->dw, die1, DW_AT_low_pc,
	    (Dwarf_Unsigned)ctx->codebuf->code_ptr, 0, &dw_err);
	dwarf_add_AT_targ_address(x86_ctx->dw, die1, DW_AT_high_pc,
	    (Dwarf_Unsigned)(ctx->codebuf->code_ptr + ctx->codebuf->code_sz), 0, &dw_err);


	if (dwarf_add_die_to_debug(x86_ctx->dw, die0, &dw_err) != DW_DLV_OK) {
		assert (0);
	}
#endif
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
		jit_emit_opc1_reg_memrm(ctx->codebuf, 1, 0, 0, OPC_MOV_REG_RM, saved_reg,
					REG_RBP, NO_REG, 0, -(i+1)*8, 0);
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
		disp = calculate_disp32((uintptr_t)label->tgt_info, (uintptr_t)reloc->loc);
		jit_emit32_at(reloc->loc, (uint32_t)disp);
	}

#ifdef DWARF_DEBUG_ENABLE
	jit_gdb_info(ctx->codebuf->code_ptr, ctx->codebuf->code_sz, x86_ctx->dw);
#endif
}

#ifdef DWARF_DEBUG_ENABLE
static
int
create_section_cb(char *name, int size, Dwarf_Unsigned type, Dwarf_Unsigned flags,
    Dwarf_Unsigned link, Dwarf_Unsigned info, Dwarf_Unsigned *sect_name_index,
    void *user_data, int *error)
{
	if (strcmp(name, ".debug_frame") == 0)
		return ELF_SECTION_debug_frame;
	else if (strcmp(name, ".debug_info") == 0)
		return ELF_SECTION_debug_info;
	else if (strcmp(name, ".debug_abbrev") == 0)
		return ELF_SECTION_debug_abbrev;
#if 0
	else if (strcmp(name, ".debug_line") == 0)
		return ELF_SECTION_debug_line;
#endif
	else
		return 0;
}
#endif

void
jit_tgt_ctx_init(jit_ctx_t ctx)
{
	x86_ctx_t x86_ctx;
#ifdef DWARF_DEBUG_ENABLE
	Dwarf_Error dw_err = 0;
#endif

	x86_ctx = malloc(sizeof(*x86_ctx));
	assert (x86_ctx != NULL);
	memset(x86_ctx, 0, sizeof(*x86_ctx));

#ifdef DWARF_DEBUG_ENABLE
	x86_ctx->dw = dwarf_producer_init_c(
	    DW_DLC_WRITE | DW_DLC_SIZE_64,
	    create_section_cb,
	    (Dwarf_Handler)0,
	    (Dwarf_Ptr)0,
	    (void *)0,
	    &dw_err);

	assert (!dw_err);
#endif

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
	jit_regset_set(ctx->regs_caller_saved, REG_RAX);
	jit_regset_set(ctx->regs_caller_saved, REG_RCX);
	jit_regset_set(ctx->regs_caller_saved, REG_RDX);
	jit_regset_set(ctx->regs_caller_saved, REG_RSI);
	jit_regset_set(ctx->regs_caller_saved, REG_RDI);
	jit_regset_set(ctx->regs_caller_saved, REG_R8);
	jit_regset_set(ctx->regs_caller_saved, REG_R9);
	jit_regset_set(ctx->regs_caller_saved, REG_R10);
	jit_regset_set(ctx->regs_caller_saved, REG_R11);

	jit_regset_set(ctx->regs_call_arguments, REG_RDI);
	jit_regset_set(ctx->regs_call_arguments, REG_RSI);
	jit_regset_set(ctx->regs_call_arguments, REG_RDX);
	jit_regset_set(ctx->regs_call_arguments, REG_RCX);
	jit_regset_set(ctx->regs_call_arguments, REG_R8);
	jit_regset_set(ctx->regs_call_arguments, REG_R9);
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
	[JITOP_AND]	= { .alias = "0", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_ANDI]	= { .alias = "0", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_OR]	= { .alias = "0", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_ORI]	= { .alias = "0", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_XOR]	= { .alias = "0", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_XORI]	= { .alias = "0", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_ADD]	= { .alias = "0", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	/* LEA r32/64, m XXX */
	[JITOP_ADDI]	= { .alias = "0", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	/* LEA r32/64, m XXX */
	[JITOP_SUB]	= { .alias = "0", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_SUBI]	= { .alias = "0", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_SHL]	= { .alias = "0", .o_restrict = "", .i_restrict = "-c", .check_needed = 0 },
	[JITOP_SHLI]	= { .alias = "0", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_SHR]	= { .alias = "0", .o_restrict = "", .i_restrict = "-c", .check_needed = 0 },
	[JITOP_SHRI]	= { .alias = "0", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_MOV]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_MOV_SEXT]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_MOVI]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_NOT]	= { .alias = "0", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_BSWAP]	= { .alias = "0", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_CLZ]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 1 },
	/* LZCNT (VEX stuff?) */
	[JITOP_BFE]     = { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	/* BEXTR (needs VEX stuff...) */
	[JITOP_LDRI]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_LDRR]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_LDRBPO]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_LDRBPSI]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	/* MOVSX for dw={8, 16, 32} */
	[JITOP_LDRI_SEXT]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_LDRR_SEXT]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_LDRBPO_SEXT]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_LDRBPSI_SEXT]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },

	[JITOP_STRI]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_STRR]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_STRBPO]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_STRBPSI]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_BRANCH]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_BCMP]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_BCMPI]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_BNCMP]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_BNCMPI]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_BTEST]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_BTESTI]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_BNTEST]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_BNTESTI]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_RET]	= { .alias = "", .o_restrict = "", .i_restrict = "a", .check_needed = 0 },
	[JITOP_RETI]	= { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_CMOV]    = { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_CMOVI]   = { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_CSEL]    = { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_CSELI]   = { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_CSET]    = { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_CSETI]   = { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_TMOV]    = { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_TMOVI]   = { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_TSEL]    = { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_TSELI]   = { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_TSET]    = { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_TSETI]   = { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_SET_LABEL] = { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
	[JITOP_XCHG]    = { .alias = "", .o_restrict = "", .i_restrict = "", .check_needed = 0 },
};

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
#ifndef _PMJIT_H_
#define _PMJIT_H_

#include <ctype.h>
#include <sys/types.h>
#include <stdint.h>

typedef enum {
	CMP_GE,
	CMP_LE,
	CMP_NE,
	CMP_EQ,
	CMP_GT,
	CMP_LT
} jit_cc_t;

// http://wiki.osdev.org/X86-64_Instruction_Encoding
// http://courses.engr.illinois.edu/ece390/archive/spr2002/books/labmanual/index.html

#define JIT_SHR			0x00
#define JIT_SHL			0x01

#define OPC_MOV_REG_RM		0x8B
#define OPC_MOV_RM_REG		0x89
#define OPC_MOV_REG_IMM32	0xB8
#define OPC_MOV_RM_IMM32	0xC7

#define OPC_XOR_REG_RM		0x33
#define OPC_XOR_RM_REG		0x31

#define OPC_AND_REG_RM		0x23
#define OPC_AND_RM_REG		0x21
#define OPC_AND_RM_IMM32	0x81
#define OPC_AND_EAX_IMM32	0x25

#define OPC_CMP_REG_RM		0x3B
#define OPC_CMP_RM_REG		0x39
#define OPC_CMP_RM_IMM32	0x81
#define OPC_CMP_EAX_IMM32	0x3D

#define OPC_SHIFT_RM_IMM	0xC1

#define OPC_JMP1		0x0F
#define OPC_JMP2_CC		0x80

#define OPC_RET			0xC3

/* ModR/M mod field values */
#define MODRM_MOD_DISP0		0x0
#define MODRM_MOD_DISP1B	0x1
#define MODRM_MOD_DISP4B	0x2
#define MODRM_MOD_RM_REG	0x3

/* ModR/M reg field special values */
#define MODRM_REG_MOV_IMM	0x0
#define MODRM_REG_AND_IMM	0x4
#define MODRM_REG_CMP_IMM	0x0
#define MODRM_REG_SHL		0x4
#define MODRM_REG_SHR		0x5

#if 0
#define REG_EAX			0x0
#define REG_ECX			0x1
#define REG_EDX			0x2
#define REG_EBX			0x3
#define REG_ESP			0x4
#define REG_EBP			0x5
#define REG_ESI			0x6
#define REG_EDI			0x7

/* Long mode registers that need REX.W */
#define REG_R8			0x8
#define REG_R9			0x9
#define REG_R10			0xA
#define REG_R11			0xB
#define REG_R12			0xC
#define REG_R13			0xD
#define REG_R14			0xE
#define REG_R15			0xF
#endif

typedef struct jit_reloc
{
	void		*code_ptr;
} *jit_reloc_t;

typedef struct jit_label
{
	int		id;

	int		has_target;
	uintptr_t	target;

	int		reloc_count;
	int		max_relocs;
	jit_reloc_t relocs;
} *jit_label_t;

typedef struct jit_codebuf
{
	void		*start_ptr;
	size_t		buf_sz;

	uint8_t		*code_ptr;
	size_t		code_sz;

	int		label_count;
	int		max_labels;
	jit_label_t	labels;
} *jit_codebuf_t;


typedef enum {
	JITOP_AND = 0,
	JITOP_ANDI,
	JITOP_OR,
	JITOP_ORI,
	JITOP_XOR,
	JITOP_XORI,
	JITOP_ADD,
	JITOP_ADDI,
	JITOP_NOT,
	JITOP_SHL,
	JITOP_SHLI,
	JITOP_SHR,
	JITOP_SHRI,
	JITOP_MOV,
	JITOP_MOVI,
	JITOP_LDRI,
	JITOP_LDRR,
	JITOP_LDRBPO,
	JITOP_LDRBPSO,
	JITOP_STRI,
	JITOP_STRR,
	JITOP_STRBPO,
	JITOP_STRBPSO,
	JITOP_BRANCH,
	JITOP_BCMP,
	JITOP_BCMPI,
	JITOP_BNCMP,
	JITOP_BNCMPI,
	JITOP_BTEST,
	JITOP_BTESTI,
	JITOP_BNTEST,
	JITOP_BNTESTI,
	JITOP_SET_LABEL,
	JITOP_RET,
	JITOP_RETI,
	JITOP_BSWAP,
	JITOP_CLZ,
	JITOP_BFE,
	JITOP_CMOV,
	JITOP_CMOVI,
	JITOP_CSEL,
	JITOP_CSELI,
	JITOP_CSET,
	JITOP_CSETI,
	JITOP_FN_PROLOGUE,
	JITOP_NOP,
	JITOP_NOPN
} jit_op_t;

typedef struct jit_op_def {
	const char	*mnemonic;
	int		out_args;
	int		in_args;
	int		side_effects;
	const char	*fmt;
} const *jit_op_def_t;


extern struct jit_op_def const op_def[];

#define JITOP_DW_8	0x00
#define	JITOP_DW_16	0x01
#define JITOP_DW_32	0x02
#define JITOP_DW_64	0x03

#define JITOP(op, dw, cc_dw)	\
    (((op) & 0xffff) | (((dw) & 0x3) << 16) | (((cc_dw) & 0x3) << 24))

#define JITOP_OP(opc)	\
    ((opc) & 0xffff)

#define JITOP_DW(opc)	\
    (((opc) >> 16) & 0x3)

#define JITOP_CC_DW(opc)	\
    (((opc) >> 24) & 0x3)

typedef enum jit_tmp_loc {
	JITLOC_UNALLOCATED,
	JITLOC_STACK,
	JITLOC_REG
} jit_tmp_loc_t;

typedef struct jit_tmp_use {
	int			use_idx;
	int			generation;

	struct jit_tmp_use	*prev;
	struct jit_tmp_use	*next;
} *jit_tmp_use_t;

typedef struct jit_tmp_scan {
	/*
	 * Keep track of the generation of the temp; bumped every time it's used
	 * as a destination of an operation.
	 */
	int		generation;
	jit_tmp_use_t	use_head;
	jit_tmp_use_t	use_tail;

	/* Use metrics */
	int		use_count;
	int		first_use;
} *jit_tmp_scan_t;

typedef struct jit_tmp_out_scan {
	int		generation;
	int		last_used;
} *jit_tmp_out_scan_t;

typedef struct jit_tmp_state {
	unsigned int	dirty  : 1;
	unsigned int	local  : 1;
	unsigned int	w64    : 1;
	unsigned int	pinned : 1;
	unsigned int	mem_allocated : 1;
	unsigned int	constant : 1;
	jit_tmp_loc_t	loc;
	int		reg;
	int		mem_base_reg;
	int		mem_offset;
	int		id;
	struct jit_tmp_scan	scan;
	struct jit_tmp_out_scan	out_scan;
} *jit_tmp_state_t;

struct jit_ctx;

typedef uint32_t jit_tmp_t;

typedef uint64_t jit_regset_t;

#define popcnt(x)	__builtin_popcountl((x))

#define jit_regset_empty(r)		((r) = 0UL)
#define jit_regset_full(r)		((r) = 0xFFFFFFFFFFFFFFFFUL)
#define jit_regset_assign(r, i)		((r) = i)

#define jit_regset_union(r1, r2)	((r1) | (r2))
#define jit_regset_intersection(r1, r2)	((r1) & (r2))

#define jit_regset_set(r, reg)		((r) |=  (1UL << reg))
#define jit_regset_clear(r, reg)	((r) &= ~(1UL << reg))
#define jit_regset_test(r, reg)		((r) &  (1UL << reg))
#define jit_regset_is_empty(r)		((r) == 0)
#define jit_regset_is_full(r)		((r) == 0xFFFFFFFFFFFFFFFFUL)
#define jit_regset_popcnt(r)		(popcnt(r))


#define JIT_TMP_LOCAL(idx)	((idx) | (1 << 31))
#define JIT_TMP_IS_LOCAL(tmp)	(((tmp) & (1 << 31)) != 0)
#define JIT_TMP_INDEX(tmp)	((tmp) & 0x7FFFFFFF)

/*
 * XXX: GET_TMP_STATE() should check if the tmp is allocated or not in
 *      the first place.
 */
#define GET_TMP_STATE(ctx, t)						\
    ((JIT_TMP_IS_LOCAL(t)) ? &ctx->local_tmps[JIT_TMP_INDEX(t)] :	\
     &ctx->bb_tmps[JIT_TMP_INDEX(t)])


/* Basic block, ending at branch or label */
typedef struct jit_bb
{
	struct jit_ctx	*ctx;
	uint32_t	opcodes[1024];
	uint64_t	params[3192];
	uint32_t	*opc_ptr;
	uint64_t	*param_ptr;

	int		opc_cnt;

	/* XXX: fill these out... */
	int		tmp_idx_1st;
	int		tmp_idx_last;
} *jit_bb_t;

#define REG_SET_USED(ctx, regno, tmp)			\
    do {						\
	    (ctx)->regs_used  |= (1UL << regno);	\
	    (ctx)->reg_to_tmp[regno] = tmp;		\
    } while (0)

#define REG_CLR_USED(ctx, regno)			\
    do {						\
	    (ctx)->regs_used &= ~(1UL << regno);	\
    } while (0)


/*
 * XXX: which instructions have requirements on which registers can be used?
 * XXX: other constraints for example on immediate values (e.g. only power-of-2)
 * XXX: how to satisfy those constraints?
 */

/*
 * XXX: at end of bb:
 *       - do REG_CLR_USED of all bb temps
 *       - spill all (ctx-) local temps (unless pinned)
 */
/*
 * XXX: proper liveness analysis on local temps, to avoid the spilling &
 *      filling drama if unused
 */

typedef struct jit_ctx
{
	int		local_tmps_cnt;
	int		local_tmps_sz;
	jit_tmp_state_t	local_tmps;

	int		bb_tmps_cnt;
	int		bb_tmps_sz;
	jit_tmp_state_t	bb_tmps;


	int		cur_block;
	int		block_cnt;
	int		blocks_sz;
	jit_bb_t	blocks;

	uint8_t		*code_buf;
	size_t		code_buf_sz;

	uint8_t		*code_ptr;
	size_t		code_sz;

	/* Register allocation tracking */
	jit_regset_t	regs_used;
	jit_tmp_t	reg_to_tmp[64];
	jit_regset_t	overall_choice;

	int32_t		spill_stack_offset;
} *jit_ctx_t;

/* Data processing: logical */
void jit_emit_and(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_andi(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, uint64_t imm);
void jit_emit_or(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_ori(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, uint64_t imm);
void jit_emit_xor(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_xori(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, uint64_t imm);
void jit_emit_not(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1);
void jit_emit_shl(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_shli(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, uint8_t imm);
void jit_emit_shr(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_shri(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, uint8_t imm);

/* Data processing: moves */
void jit_emit_mov(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1);
void jit_emit_movi(jit_ctx_t ctx, jit_tmp_t dst, uint64_t imm);

/* Data processing: misc bitops, etc */
void jit_emit_bswap(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1);
void jit_emit_clz(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1);
void jit_emit_bfe(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, uint8_t hi, uint8_t lo);

/* Data processing: arithmetic */
void jit_emit_add(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_addi(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, uint64_t imm);

/* Control flow: local branches */
void jit_emit_branch(jit_ctx_t ctx, jit_label_t label);
void jit_emit_bcmp(jit_ctx_t ctx, jit_label_t label, jit_cc_t cc, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_bcmpi(jit_ctx_t ctx, jit_label_t label, jit_cc_t cc, jit_tmp_t r1, uint64_t imm);
void jit_emit_bncmp(jit_ctx_t ctx, jit_label_t label, jit_cc_t cc, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_bncmpi(jit_ctx_t ctx, jit_label_t label, jit_cc_t cc, jit_tmp_t r1, uint64_t imm);
void jit_emit_btest(jit_ctx_t ctx, jit_label_t label, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_btesti(jit_ctx_t ctx, jit_label_t label, jit_tmp_t r1, uint64_t imm);
void jit_emit_bntest(jit_ctx_t ctx, jit_label_t label, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_bntesti(jit_ctx_t ctx, jit_label_t label, jit_tmp_t r1, uint64_t imm);
void jit_emit_set_label(jit_ctx_t ctx, jit_label_t label);


void
jit_emit_cmov(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src, jit_cc_t cc, jit_tmp_t r1, jit_tmp_t r2);
void
jit_emit_cmovi(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src, jit_cc_t cc, jit_tmp_t r1, uint64_t imm);
void jit_emit_csel(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src1, jit_tmp_t src2, jit_cc_t cc, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_cseli(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src1, jit_tmp_t src2, jit_cc_t cc, jit_tmp_t r1, uint64_t imm);
void jit_emit_cset(jit_ctx_t ctx, jit_tmp_t dst, jit_cc_t cc, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_cseti(jit_ctx_t ctx, jit_tmp_t dst, jit_cc_t cc, jit_tmp_t r1, uint64_t imm);

/* XXX: load byte, load halfword, load word, load doubleword */
/* XXX: and same for store... */
/* Memory: loads */
/* r1 <- [r2] */
void jit_emit_ldr_reg(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r2);
/* r1 <- [imm] */
void jit_emit_ldr_imm(jit_ctx_t ctx, jit_tmp_t dst, uint64_t imm);
/* r1 <- [r2 + imm] */
void jit_emit_ldr_base_imm(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r2, uint64_t imm);
/* r1 <- [r2 + (r3 << scale)] */
void jit_emit_ldr_base_sr(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r2, jit_tmp_t r3, uint8_t scale);

/* Memory: stores */
/* [r2] <- r1 */
void jit_emit_str_reg(jit_ctx_t ctx, jit_tmp_t r1, jit_tmp_t r2);
/* [imm] <- r1 */
void jit_emit_str_imm(jit_ctx_t ctx, jit_tmp_t r1, uint64_t imm);
/* [r2 + imm] <- r1 */
void jit_emit_str_base_imm(jit_ctx_t ctx, jit_tmp_t r1, jit_tmp_t r2, uint64_t imm);
/* [r2 + (r3 << scale)] <- r1 */
void jit_emit_str_base_sr(jit_ctx_t ctx, jit_tmp_t r1, jit_tmp_t r2, jit_tmp_t r3, uint8_t scale);

int jit_ctx_init(jit_ctx_t ctx);
jit_tmp_t jit_new_tmp64(jit_ctx_t ctx);
jit_tmp_t jit_new_tmp32(jit_ctx_t ctx);
jit_tmp_t jit_new_local_tmp64(jit_ctx_t ctx);
jit_tmp_t jit_new_local_tmp32(jit_ctx_t ctx);

void jit_print_ir(jit_ctx_t ctx);
void jit_optimize(jit_ctx_t ctx);
void jit_process(jit_ctx_t ctx);

/* XXX: force the specified tmp to always be in a register (nessarily always the same reg) */
void jit_pin_local_tmp(jit_ctx_t ctx, jit_tmp_t tmp);

void jit_emit_fn_prologue(jit_ctx_t ctx, const char *fmt, ...);
void jit_emit_ret(jit_ctx_t ctx, jit_tmp_t r1);
void jit_emit_reti(jit_ctx_t ctx, uint64_t imm);

/* XXX: branch comparison width */
/* XXX: branch label first */

/* XXX: fmt: iittti for immediate, immediate, temp, temp, temp, immediate */
/* XXX: return value tmp? also make optional... */
void jit_emit_call(jit_ctx_t, void *fn, const char *fmt, ...);

#if 0
/* XXX: code gen stuff */
void jit_emit8(jit_codebuf_t code, uint8_t u8);
void jit_emit32(jit_codebuf_t code, uint32_t u32);
void jit_emit64(jit_codebuf_t code, uint32_t u32);

int jit_label_init(jit_codebuf_t code);
uintptr_t jit_label_get_target(jit_codebuf_t code, int label_idx);
void jit_label_set_target(jit_codebuf_t code, int label_idx, uintptr_t abs_dst);
void jit_label_set_target_here(jit_codebuf_t code, int label_idx);

void *jit_codebuf_init(jit_codebuf_t code, size_t sz);
void jit_codebuf_seek(jit_codebuf_t code, off_t offset, int from_start);
void jit_codebuf_uninit(jit_codebuf_t code);

void jit_emit_mov_reg_reg(jit_codebuf_t code, uint8_t dst_reg, uint8_t src_reg);
void jit_emit_mov_reg_imm32(jit_codebuf_t code, uint8_t dst_reg, uint32_t imm);
void jit_emit_ret(jit_codebuf_t code);
void jit_emit_xor_reg_reg(jit_codebuf_t code, uint8_t dst_reg, uint8_t op2_reg);
void jit_emit_shift_imm8(jit_codebuf_t code, int left, uint8_t rm_reg, int amt);
void jit_emit_cmp_imm32(jit_codebuf_t code, uint8_t rm_reg, uint32_t imm);
void jit_emit_and_imm32(jit_codebuf_t code, uint8_t rm_reg, uint32_t imm);
void jit_emit_jcc(jit_codebuf_t code, jit_cmptype_t cmp, int label_idx);
void jit_emit_jncc(jit_codebuf_t code, jit_cmptype_t cmp, int label_idx);

#endif
#endif


/* XXX!!! */

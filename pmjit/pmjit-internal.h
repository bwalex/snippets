#ifndef _PMJIT_INTERNAL_H_
#define _PMJIT_INTERNAL_H_


#define SAVE_NORMAL	0
#define SAVE_BEFORE	1
#define SAVE_NEVER	2

typedef struct jit_op_def {
	const char	*mnemonic;
	int		out_args;
	int		in_args;
	int		side_effects;
	int		save_locals;
	const char	*fmt;
} const *jit_op_def_t;


extern struct jit_op_def const op_def[];


typedef enum jit_tmp_loc {
	JITLOC_UNALLOCATED,
	JITLOC_STACK,
	JITLOC_REG
} jit_tmp_loc_t;


typedef struct jit_tmp_use {
	int			use_idx;
	int			generation;
	int			bb_id;

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
	int		mark;
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


typedef uint64_t jit_regset_t;

struct jit_bb;

typedef struct jit_bb_links
{
	int		cnt;
	int		sz;
	struct jit_bb	**bbs;
} *jit_bb_links_t;

/* Basic block, ending at branch or label */
typedef struct jit_bb
{
	int		id;

	struct jit_ctx	*ctx;
	uint32_t	opcodes[1024];
	uint64_t	params[3192];
	uint32_t	*opc_ptr;
	uint64_t	*param_ptr;

	int		opc_cnt;

	struct jit_bb_links in_links;
	struct jit_bb_links out_links;

	/* XXX: fill these out... */
	int		tmp_idx_1st;
	int		tmp_idx_last;
} *jit_bb_t;


typedef struct jit_reloc
{
	jit_bb_t	bb;
} *jit_reloc_t;

struct jit_label {
	int		id;

	int		has_target;
	jit_bb_t	target;

	int		reloc_count;
	int		max_relocs;
	jit_reloc_t	relocs;
};

struct jit_ctx
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

	struct jit_label *labels;
	int		label_cnt;
	int		labels_sz;


	uint8_t		*code_buf;
	size_t		code_buf_sz;

	uint8_t		*code_ptr;
	size_t		code_sz;

	/* Register allocation tracking */
	jit_regset_t	regs_used;
	jit_tmp_t	reg_to_tmp[64];
	jit_regset_t	overall_choice;

	jit_regset_t	regs_ever_used;

	int32_t		spill_stack_offset;

	void		*tgt_ctx;

	struct jit_codebuf *codebuf;
};

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



#define REG_SET_USED(ctx, regno, tmp)			\
    do {						\
	    (ctx)->regs_used  |= (1UL << regno);	\
	    (ctx)->reg_to_tmp[regno] = tmp;		\
    } while (0)

#define REG_CLR_USED(ctx, regno)			\
    do {						\
	    (ctx)->regs_used &= ~(1UL << regno);	\
    } while (0)


#define CAN_FAIL	0x01


#define _ALIGN_SZ(sz, align_sz)				\
	(((sz) % (align_sz) != 0) ?			\
	    ((align_sz) + (sz) - ((sz) % (align_sz))) : \
	     (sz))



#if 0

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

#if 0

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


#endif

#ifndef _PMJIT_INTERNAL_H_
#define _PMJIT_INTERNAL_H_
#include "dyn_array.h"


typedef struct jit_op_def {
	const char	*mnemonic;
	int		out_args;
	int		in_args;
	int		side_effects;
	int		save_locals;
	const char	*fmt;
} const *jit_op_def_t;

typedef enum jit_tmp_loc {
	JITLOC_UNALLOCATED,
	JITLOC_CONST,
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

typedef struct jit_tmp_call_info {
	jit_tmp_loc_t	loc;
	int		reg;
	int		mem_base_reg;
	int		mem_offset;
} *jit_tmp_call_info_t;

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

	uint64_t	value;

	struct jit_tmp_scan		scan;
	struct jit_tmp_out_scan		out_scan;
	struct jit_tmp_call_info	call_info;
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
	void		*tgt_info;

	struct dyn_array	relocs;
};

struct jit_ctx
{
	struct dyn_array	local_tmps;
	struct dyn_array	bb_tmps;
	struct dyn_array	labels;

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

	jit_regset_t	regs_ever_used;

	jit_regset_t	regs_caller_saved;
	jit_regset_t	regs_call_arguments;

	int32_t		spill_stack_offset;

	void		*tgt_ctx;

	struct jit_codebuf *codebuf;
};


typedef enum {
	JITOP_AND = 0,
	JITOP_ANDI,
	JITOP_OR,
	JITOP_ORI,
	JITOP_XOR,
	JITOP_XORI,
	JITOP_ADD,
	JITOP_ADDI,
	JITOP_SUB,
	JITOP_SUBI,
	JITOP_NOT,
	JITOP_SHL,
	JITOP_SHLI,
	JITOP_SHR,
	JITOP_SHRI,
	JITOP_MOV,
	JITOP_MOVI,
	JITOP_MOV_SEXT,
	JITOP_LDRI,
	JITOP_LDRR,
	JITOP_LDRBPO,
	JITOP_LDRBPSI,
	JITOP_LDRI_SEXT,
	JITOP_LDRR_SEXT,
	JITOP_LDRBPO_SEXT,
	JITOP_LDRBPSI_SEXT,
	JITOP_STRI,
	JITOP_STRR,
	JITOP_STRBPO,
	JITOP_STRBPSI,
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
	JITOP_TMOV,
	JITOP_TMOVI,
	JITOP_TSEL,
	JITOP_TSELI,
	JITOP_TSET,
	JITOP_TSETI,
	JITOP_FN_PROLOGUE,
	JITOP_CALL,
	JITOP_XCHG,
	JITOP_NOP,
	JITOP_NOP1,
	JITOP_NOPN
} jit_op_t;


#define SAVE_NORMAL	0
#define SAVE_BEFORE	1
#define SAVE_NEVER	2

#define CAN_FAIL	0x01

#define JITOP_DW_8	0x00
#define	JITOP_DW_16	0x01
#define JITOP_DW_32	0x02
#define JITOP_DW_64	0x03

#define JITOP(op, dw, op_dw)	\
    (((op) & 0xffff) | (((dw) & 0x3) << 16) | (((op_dw) & 0x3) << 24))

#define JITOP_OP(opc)	\
    ((opc) & 0xffff)

#define JITOP_DW(opc)	\
    (((opc) >> 16) & 0x3)

#define JITOP_OP_DW(opc) \
    (((opc) >> 24) & 0x3)



#define popcnt(x)	__builtin_popcountl((x))

#define jit_regset_empty(r)		((r) = 0UL)
#define jit_regset_full(r)		((r) = 0xFFFFFFFFFFFFFFFFUL)
#define jit_regset_assign(r, i)		((r) = i)

#define jit_regset_union(r1, r2)	((r1) | (r2))
#define jit_regset_intersection(r1, r2)	((r1) & (r2))
#define jit_regset_invert(r1)		(~r1)

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
#define GET_TMP_STATE(ctx, t)					\
    ((JIT_TMP_IS_LOCAL(t)) ?					\
     dyn_array_get(&ctx->local_tmps, JIT_TMP_INDEX(t)) :	\
     dyn_array_get(&ctx->bb_tmps, JIT_TMP_INDEX(t)))



#define REG_SET_USED(ctx, regno, tmp)			\
    do {						\
	    (ctx)->regs_used  |= (1UL << regno);	\
	    (ctx)->reg_to_tmp[regno] = tmp;		\
    } while (0)

#define REG_CLR_USED(ctx, regno)			\
    do {						\
	    (ctx)->regs_used &= ~(1UL << regno);	\
    } while (0)



#define _ALIGN_SZ(sz, align_sz)				\
	(((sz) % (align_sz) != 0) ?			\
	    ((align_sz) + (sz) - ((sz) % (align_sz))) : \
	     (sz))


extern struct jit_op_def const op_def[];

#endif

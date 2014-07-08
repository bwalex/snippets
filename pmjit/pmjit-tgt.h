#ifndef _PMJIT_TGT_H_
#define _PMJIT_TGT_H_

typedef struct jit_tgt_op_def {
	const char	*alias;
	const char	*o_restrict;
	const char	*i_restrict;
} *jit_tgt_op_def_t;

void jit_tgt_emit(jit_ctx_t ctx, uint32_t opc, uint64_t *params);
jit_regset_t jit_tgt_reg_restrict(jit_ctx_t ctx, jit_op_t op, char constraint);
int jit_tgt_check_imm(jit_ctx_t ctx, jit_op_t op, int idx, uint64_t imm);

extern struct jit_tgt_op_def tgt_op_def[];

#endif

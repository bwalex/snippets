#ifndef _PMJIT_TGT_H_
#define _PMJIT_TGT_H_
#include "pmjit.h"
#include "pmjit-internal.h"

typedef struct jit_tgt_op_def {
	const char	*alias;
	const char	*o_restrict;
	const char	*i_restrict;
} const *jit_tgt_op_def_t;


struct jit_tgt_reloc {
	void		*loc;
};

struct jit_tgt_label {
	int		has_target;
	void		*target;

	struct dyn_array relocs;
};

void jit_tgt_init(void);
void jit_tgt_ctx_init(jit_ctx_t ctx);

void jit_tgt_emit_fn_prologue(jit_ctx_t ctx, int cnt, uint64_t *params);

void jit_tgt_emit(jit_ctx_t ctx, uint32_t opc, uint64_t *params);
jit_regset_t jit_tgt_reg_restrict(jit_ctx_t ctx, jit_op_t op, char constraint);
int jit_tgt_check_imm(jit_ctx_t ctx, jit_op_t op, int dw, int op_dw, int idx, uint64_t imm);
int jit_tgt_reg_empty_weight(jit_ctx_t ctx, int reg);

void jit_tgt_ctx_finish_emit(jit_ctx_t ctx);

extern struct jit_tgt_op_def const tgt_op_def[];
extern const int jit_tgt_stack_base_reg;

static
inline
void
jit_emit32_at(uint8_t *buf, uint32_t u32)
{
	*buf++ = (u32 >> 0) & 0xff;
	*buf++ = (u32 >> 8) & 0xff;
	*buf++ = (u32 >> 16) & 0xff;
	*buf++ = (u32 >> 24) & 0xff;
}

static
inline
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

static
inline
void
jit_emit8(jit_codebuf_t code, uint8_t u8)
{
	assert(code->code_sz + 1 <= code->map_sz);

	*code->emit_ptr++ = u8;
	code->code_sz += 1;
}

static
inline
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

static
inline
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
inline
int
check_signed32(uint64_t imm)
{
	int64_t simm = (int64_t)imm;
	int32_t simm32 = (int32_t)simm;

	return ((int64_t)simm32 == simm);
}

static
inline
int
check_unsigned32(uint64_t imm)
{
	uint32_t imm32 = (uint32_t)imm;

	return ((uint64_t)imm32 == imm);
}

static
inline
int
check_signed8(uint64_t imm)
{
	int64_t simm = (int64_t)imm;
	int8_t simm8 = (int8_t)simm;

	return ((int64_t)simm8 == simm);
}

static
inline
int
check_unsigned8(uint64_t imm)
{
	uint8_t imm8 = (uint8_t)imm;

	return ((uint64_t)imm8 == imm);
}

#endif

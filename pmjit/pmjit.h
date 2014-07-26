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
	CMP_EQ,
	CMP_NE,

	/* unsigned */
	CMP_AE,
	CMP_B,
	CMP_A,
	CMP_BE,

	/* signed */
	CMP_LT,
	CMP_GE,
	CMP_LE,
	CMP_GT
} jit_cc_t;

typedef enum {
	TST_Z,
	TST_NZ,
	TST_MSB_SET,
	TST_MSB_CLR
} jit_test_cc_t;

typedef enum {
	ZEXT,
	SEXT
} jit_ext_type_t;

typedef struct jit_codebuf {
	uint8_t	*code_ptr;
	size_t	code_sz;

	void	*map_ptr;
	size_t	map_sz;

	uint8_t	*emit_ptr;
} *jit_codebuf_t;

struct jit_ctx;
struct jit_label;

typedef struct jit_label *jit_label_t;
typedef struct jit_ctx *jit_ctx_t;

typedef uint32_t jit_tmp_t;

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
void jit_emit_mov(jit_ctx_t ctx, int width, jit_ext_type_t ext_type, jit_tmp_t dst, jit_tmp_t r1);
void jit_emit_movi(jit_ctx_t ctx, jit_tmp_t dst, uint64_t imm);

/* Data processing: misc bitops, etc */
void jit_emit_bswap(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1);
void jit_emit_clz(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1);
void jit_emit_bfe(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, uint8_t lsb, uint8_t len);

/* Data processing: arithmetic */
void jit_emit_add(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_addi(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, uint64_t imm);
void jit_emit_sub(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_subi(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t r1, uint64_t imm);

/* Control flow: local branches */
void jit_emit_branch(jit_ctx_t ctx, jit_label_t label);
void jit_emit_bcmp(jit_ctx_t ctx, jit_label_t label, jit_cc_t cc, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_bcmpi(jit_ctx_t ctx, jit_label_t label, jit_cc_t cc, jit_tmp_t r1, uint64_t imm);
void jit_emit_bncmp(jit_ctx_t ctx, jit_label_t label, jit_cc_t cc, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_bncmpi(jit_ctx_t ctx, jit_label_t label, jit_cc_t cc, jit_tmp_t r1, uint64_t imm);
void jit_emit_btest(jit_ctx_t ctx, jit_label_t label, jit_test_cc_t cc, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_btesti(jit_ctx_t ctx, jit_label_t label, jit_test_cc_t cc, jit_tmp_t r1, uint64_t imm);
void jit_emit_bntest(jit_ctx_t ctx, jit_label_t label, jit_test_cc_t cc, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_bntesti(jit_ctx_t ctx, jit_label_t label, jit_test_cc_t cc, jit_tmp_t r1, uint64_t imm);
void jit_emit_set_label(jit_ctx_t ctx, jit_label_t label);

/* Conditional instructions other than branches */
void jit_emit_cmov(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src, jit_cc_t cc, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_cmovi(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src, jit_cc_t cc, jit_tmp_t r1, uint64_t imm);
void jit_emit_csel(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src1, jit_tmp_t src2, jit_cc_t cc, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_cseli(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src1, jit_tmp_t src2, jit_cc_t cc, jit_tmp_t r1, uint64_t imm);
void jit_emit_cset(jit_ctx_t ctx, jit_tmp_t dst, jit_cc_t cc, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_cseti(jit_ctx_t ctx, jit_tmp_t dst, jit_cc_t cc, jit_tmp_t r1, uint64_t imm);

void jit_emit_tmov(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src, jit_test_cc_t cc, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_tmovi(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src, jit_test_cc_t cc, jit_tmp_t r1, uint64_t imm);
void jit_emit_tsel(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src1, jit_tmp_t src2, jit_test_cc_t cc, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_tseli(jit_ctx_t ctx, jit_tmp_t dst, jit_tmp_t src1, jit_tmp_t src2, jit_test_cc_t cc, jit_tmp_t r1, uint64_t imm);
void jit_emit_tset(jit_ctx_t ctx, jit_tmp_t dst, jit_test_cc_t cc, jit_tmp_t r1, jit_tmp_t r2);
void jit_emit_tseti(jit_ctx_t ctx, jit_tmp_t dst, jit_test_cc_t cc, jit_tmp_t r1, uint64_t imm);

/* Memory: loads */
/* r1 <- [r2] */
void jit_emit_ldr_base(jit_ctx_t ctx, int width, jit_ext_type_t ext_type, jit_tmp_t dst, jit_tmp_t r2);
/* r1 <- [imm] */
void jit_emit_ldr_imm(jit_ctx_t ctx, int width, jit_ext_type_t ext_type, jit_tmp_t dst, uint64_t imm);
/* r1 <- [r2 + imm] */
void jit_emit_ldr_base_disp(jit_ctx_t ctx, int width, jit_ext_type_t ext_type, jit_tmp_t dst, jit_tmp_t r2, uint64_t imm);
/* r1 <- [r2 + (r3 << scale)] */
void jit_emit_ldr_base_si(jit_ctx_t ctx, int width, jit_ext_type_t ext_type, jit_tmp_t dst, jit_tmp_t r2, jit_tmp_t r3, uint8_t scale);

/* Memory: stores */
/* [r2] <- src */
void jit_emit_str_base(jit_ctx_t ctx, int width, jit_tmp_t src, jit_tmp_t r2);
/* [imm] <- src */
void jit_emit_str_imm(jit_ctx_t ctx, int width, jit_tmp_t src, uint64_t imm);
/* [r2 + imm] <- src */
void jit_emit_str_base_disp(jit_ctx_t ctx, int width, jit_tmp_t src, jit_tmp_t r2, uint64_t imm);
/* [r2 + (r3 << scale)] <- src */
void jit_emit_str_base_si(jit_ctx_t ctx, int width, jit_tmp_t src, jit_tmp_t r2, jit_tmp_t r3, uint8_t scale);

jit_ctx_t jit_new_ctx(void);
void jit_free_ctx(jit_ctx_t ctx);
jit_tmp_t jit_new_tmp64(jit_ctx_t ctx);
jit_tmp_t jit_new_tmp32(jit_ctx_t ctx);
jit_tmp_t jit_new_local_tmp64(jit_ctx_t ctx);
jit_tmp_t jit_new_local_tmp32(jit_ctx_t ctx);
jit_label_t jit_new_label(jit_ctx_t ctx);
int jit_init_codebuf(jit_ctx_t ctx, jit_codebuf_t codebuf);

void jit_print_ir(jit_ctx_t ctx);
void jit_optimize(jit_ctx_t ctx);
void jit_process(jit_ctx_t ctx);
void jit_resolve_links(jit_ctx_t ctx);
int jit_output_cfg(jit_ctx_t ctx, const char *file);

/* XXX: force the specified tmp to always be in a register (nessarily always the same reg) */
void jit_pin_local_tmp(jit_ctx_t ctx, jit_tmp_t tmp);

void jit_emit_fn_prologue(jit_ctx_t ctx, const char *fmt, ...);
void jit_emit_ret(jit_ctx_t ctx, jit_tmp_t r1);
void jit_emit_reti(jit_ctx_t ctx, uint64_t imm);

void jit_emit_call(jit_ctx_t ctx, void *fn_ptr, jit_tmp_t ret_tmp, const char *fmt, ...);

void jit_init(void);

#endif

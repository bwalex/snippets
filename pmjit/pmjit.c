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

static
int
jit_jmp_cc_disp(jit_cmptype_t c)
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
jit_emit32_at(uint8_t * buf, uint32_t u32)
{
	*buf++ = (u32 >> 0) & 0xff;
	*buf++ = (u32 >> 8) & 0xff;
	*buf++ = (u32 >> 16) & 0xff;
	*buf++ = (u32 >> 24) & 0xff;
}

void
jit_emit8(jit_codebuf_t code, uint8_t u8)
{
	assert(code->code_sz + 1 <= code->buf_sz);

	*code->code_ptr++ = u8;
	code->code_sz += 1;
}

void
jit_emit32(jit_codebuf_t code, uint32_t u32)
{
	assert(code->code_sz + 4 <= code->buf_sz);

	*code->code_ptr++ = (u32 >> 0) & 0xff;
	*code->code_ptr++ = (u32 >> 8) & 0xff;
	*code->code_ptr++ = (u32 >> 16) & 0xff;
	*code->code_ptr++ = (u32 >> 24) & 0xff;
	code->code_sz += 4;
}

static
void
_jit_label_resize(jit_label_t label)
{
	label->max_relocs += 16;
	label->relocs = realloc(label->relocs,
	    label->max_relocs * sizeof(struct jit_reloc));

	assert(label->relocs != NULL);
}

int
jit_label_init(jit_codebuf_t code)
{
	int label_idx;

	if (code->label_count == code->max_labels) {
		code->max_labels += 16;
		code->labels = realloc(code->labels,
		    code->max_labels * sizeof(struct jit_label));

		assert(code->labels != NULL);
	}

	label_idx = code->label_count++;

	code->labels[label_idx].has_target = 0;
	code->labels[label_idx].reloc_count = 0;
	code->labels[label_idx].max_relocs = 0;
	code->labels[label_idx].relocs = NULL;

	_jit_label_resize(&code->labels[label_idx]);

	return label_idx;
}

static
void
jit_label_uninit(jit_codebuf_t code, int label_idx)
{
	jit_label_t label;

	assert(label_idx < code->label_count);
	label = &code->labels[label_idx];

	free(label->relocs);
}

uintptr_t
jit_label_get_target(jit_codebuf_t code, int label_idx)
{
	jit_label_t label;

	assert(label_idx < code->label_count);
	label = &code->labels[label_idx];

	if (label->has_target) {
		return label->target;
	} else {
		return UINTPTR_MAX;
	}
}

static
void
jit_label_add_reloc(jit_codebuf_t code, int label_idx)
{
	jit_label_t label;
	jit_reloc_t reloc;

	assert(label_idx < code->label_count);
	label = &code->labels[label_idx];

	assert(label->reloc_count < label->max_relocs);
	reloc = &label->relocs[label->reloc_count++];

	if (label->reloc_count == label->max_relocs)
		_jit_label_resize(label);

	reloc->code_ptr = code->code_ptr;
}

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

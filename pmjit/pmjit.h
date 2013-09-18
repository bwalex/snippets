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
} jit_cmptype_t;

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

typedef struct jit_reloc
{
	void		*code_ptr;
} *jit_reloc_t;

typedef struct jit_label
{
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


void jit_emit8(jit_codebuf_t code, uint8_t u8);
void jit_emit32(jit_codebuf_t code, uint32_t u32);

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

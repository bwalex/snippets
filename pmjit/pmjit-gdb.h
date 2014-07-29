#ifndef _PMJIT_GDB_H_
#define _PMJIT_GDB_H_

typedef enum {
	JIT_NOACTION = 0,
	JIT_REGISTER_FN,
	JIT_UNREGISTER_FN
} jit_actions_t;

struct jit_code_entry {
	struct jit_code_entry *next_entry;
	struct jit_code_entry *prev_entry;
	const char *symfile_addr;
	uint64_t symfile_size;
};

struct jit_descriptor {
	uint32_t version;
	/*
	 * This type should be jit_actions_t, but we use uint32_t
	 * to be explicit about the bitwidth.
	 */
	uint32_t action_flag;
	struct jit_code_entry *relevant_entry;
	struct jit_code_entry *first_entry;
};

enum {
	ELF_SECTION_null,
	ELF_SECTION_debug_frame,
	ELF_SECTION_debug_info,
	ELF_SECTION_debug_abbrev,
#if 0
	ELF_SECTION_debug_line,
#endif
	ELF_SECTION_text,
	ELF_SECTION_shstrtab,
	ELF_SECTION_strtab,
	ELF_SECTION_symtab,
	ELF_SECTION_last
};

enum {
	ELF_SYM_null,
	ELF_SYM_func,
	ELF_SYM_last
};


void jit_gdb_info(uint8_t *mcaddr, size_t mcsize, Dwarf_P_Debug dw);

#endif

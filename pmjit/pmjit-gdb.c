#include <sys/types.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <stdio.h>
#include <libdwarf.h>
#include "elf.h"

#include "pmjit-gdb.h"

/* GDB puts a breakpoint in this function.  */
void __attribute__((noinline)) __jit_debug_register_code(void);
void __attribute__((noinline)) __jit_debug_register_code(void) { };

/* Make sure to specify the version statically, because the
	 debugger may check the version before we can set it.  */
struct jit_descriptor __jit_debug_descriptor = { 1, 0, 0, 0 };


static
int
jit_register_gdb_entry(const char *symfile_addr, size_t symfile_size)
{
	struct jit_code_entry *code_entry;

	if ((code_entry = malloc(sizeof(*code_entry))) == NULL)
		return -1;

	memset(code_entry, 0, sizeof(*code_entry));

	code_entry->symfile_addr = symfile_addr;
	code_entry->symfile_size = (uint64_t)symfile_size;

	code_entry->next_entry = __jit_debug_descriptor.first_entry;
	if (code_entry->next_entry != NULL)
		code_entry->next_entry->prev_entry = code_entry;
	code_entry->prev_entry = NULL;

	__jit_debug_descriptor.action_flag = JIT_REGISTER_FN;
	__jit_debug_descriptor.first_entry = code_entry;
	__jit_debug_descriptor.relevant_entry = code_entry;

	__jit_debug_register_code();

	return 0;
}

#define MAX_ELF_DATA	2048

struct elf_obj {
	Elf64_Ehdr	elf_hdr;
	Elf64_Shdr	elf_shdr[ELF_SECTION_last];
	Elf64_Sym	elf_sym[ELF_SYM_last];

	uint8_t		data[MAX_ELF_DATA];
};


static const Elf64_Ehdr ehdr_template = {
	.e_ident	= { [EI_MAG0]	= ELFMAG0,
			    [EI_MAG1]	= ELFMAG1,
			    [EI_MAG2]	= ELFMAG2,
			    [EI_MAG3]	= ELFMAG3,
			    [EI_CLASS]	= ELFCLASS64,
			    [EI_DATA]	= ELFDATA2LSB,
			    [EI_VERSION] = EV_CURRENT,
			    [EI_OSABI]	= ELFOSABI_NONE,
			    [EI_ABIVERSION] = 0	},
	.e_type		= ET_REL,
	.e_machine	= EM_X86_64,
	.e_version	= EV_CURRENT,
	.e_entry	= 0,
	.e_phoff	= 0,
	.e_shoff	= offsetof(struct elf_obj, elf_shdr),
	.e_flags	= 0,
	.e_ehsize	= sizeof(Elf64_Ehdr),
	.e_phentsize	= 0,
	.e_phnum	= 0,
	.e_shentsize	= sizeof(Elf64_Shdr),
	.e_shnum	= ELF_SECTION_last,
	.e_shstrndx	= ELF_SECTION_shstrtab
};

#define EMIT_STR(p, str) \
    do {				\
	memcpy((p), str, sizeof(str));	\
	p += sizeof(str);		\
    } while (0);


static
size_t
emit_section_shstrtab(struct elf_obj *obj, uint8_t *p)
{
	uint8_t *start_p = p;
	Elf64_Shdr *shdr;

	*p++ = '\0';

	shdr = &obj->elf_shdr[ELF_SECTION_text];
	shdr->sh_name = p - obj->data;
	shdr->sh_type = SHT_NOBITS;
	shdr->sh_addralign = 16;
	shdr->sh_flags = SHF_ALLOC | SHF_EXECINSTR;
	EMIT_STR(p, ".text");

	shdr = &obj->elf_shdr[ELF_SECTION_debug_frame];
	shdr->sh_name = p - obj->data;
	shdr->sh_type = SHT_PROGBITS;
	shdr->sh_addralign = 8;
	shdr->sh_flags = SHF_ALLOC;
	EMIT_STR(p, ".debug_frame");

	shdr = &obj->elf_shdr[ELF_SECTION_debug_info];
	shdr->sh_name = p - obj->data;
	shdr->sh_type = SHT_PROGBITS;
	shdr->sh_addralign = 1;
	EMIT_STR(p, ".debug_info");

	shdr = &obj->elf_shdr[ELF_SECTION_debug_abbrev];
	shdr->sh_name = p - obj->data;
	shdr->sh_type = SHT_PROGBITS;
	shdr->sh_addralign = 1;
	EMIT_STR(p, ".debug_abbrev");

	shdr = &obj->elf_shdr[ELF_SECTION_strtab];
	shdr->sh_name = p - obj->data;
	shdr->sh_type = SHT_STRTAB;
	shdr->sh_addralign = 1;
	EMIT_STR(p, ".strtab");

	shdr = &obj->elf_shdr[ELF_SECTION_symtab];
	shdr->sh_name = p - obj->data;
	shdr->sh_type = SHT_SYMTAB;
	shdr->sh_addralign = 8;
	shdr->sh_offset = offsetof(struct elf_obj, elf_sym);
	shdr->sh_size = sizeof(obj->elf_sym);
	shdr->sh_link = ELF_SECTION_strtab;
	shdr->sh_entsize = sizeof(Elf64_Sym);
	shdr->sh_info = ELF_SYM_func;
	EMIT_STR(p, ".symtab");

	shdr = &obj->elf_shdr[ELF_SECTION_shstrtab];
	shdr->sh_name = p - obj->data;
	shdr->sh_type = SHT_STRTAB;
	shdr->sh_addralign = 1;
	EMIT_STR(p, ".shstrtab");

	shdr->sh_offset = (start_p - (uint8_t *)obj);
	shdr->sh_size = (p - start_p);

	return (p - start_p);
}

static
size_t
emit_section_text(struct elf_obj *obj, uint8_t *p, uint8_t *code_ptr, size_t code_sz)
{
	Elf64_Shdr *shdr = &obj->elf_shdr[ELF_SECTION_text];

	shdr->sh_addr = (Elf64_Addr)code_ptr;
	shdr->sh_size = (Elf64_Xword)code_sz;

	return 0;
}

static
size_t
emit_section_debug(struct elf_obj *obj, uint8_t *p, Dwarf_P_Debug dw)
{
	uint8_t *start_p = p;
	Elf64_Shdr *shdr;
	Dwarf_Unsigned dwarf_len;
	Dwarf_Signed dwarf_elf_idx;
	Dwarf_Error dwarf_error = 0;
	uint8_t *dwarf_bytes;
	Dwarf_Signed dwarf_s;
	Dwarf_Signed i;

	dwarf_s = dwarf_transform_to_disk_form(dw, &dwarf_error);
	assert (!dwarf_error);

	for (i = 0; i < dwarf_s; i++) {
		start_p = p;
		dwarf_len = 0;

		dwarf_bytes = dwarf_get_section_bytes(dw, 0,
		    &dwarf_elf_idx, &dwarf_len, &dwarf_error);

		switch (dwarf_elf_idx) {
		case ELF_SECTION_debug_info:
		case ELF_SECTION_debug_abbrev:
		case ELF_SECTION_debug_frame:
			shdr = &obj->elf_shdr[dwarf_elf_idx];
			shdr->sh_offset = (Elf64_Off)(start_p - (uint8_t *)obj);
			shdr->sh_size = (Elf64_Xword)(dwarf_len);
			memcpy(p, dwarf_bytes, dwarf_len);
			p += dwarf_len;
			break;
		}
	}

	return (p - start_p);
}

static
size_t
emit_section_strtab_syms(struct elf_obj *obj, uint8_t *p, size_t code_size)
{
	uint8_t *start_p = p;
	Elf64_Shdr *shdr;
	Elf64_Sym *sym;

	*p++ = '\0';

	shdr = &obj->elf_shdr[ELF_SECTION_strtab];

	sym = &obj->elf_sym[ELF_SYM_func];
	sym->st_name = p - start_p;
	sym->st_shndx = ELF_SECTION_text;
	sym->st_value = 0;
	sym->st_size = code_size;
	sym->st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
	EMIT_STR(p, "jitted_func");

	shdr->sh_offset = (start_p - (uint8_t *)obj);
	shdr->sh_size = (p - start_p);

	return (p - start_p);
}

void
jit_gdb_info(uint8_t *code_ptr, size_t code_size, Dwarf_P_Debug dw)
{
	uint8_t *p;
	struct elf_obj *obj;

	obj = malloc(sizeof(*obj));
	assert (obj != NULL);

	memcpy(&obj->elf_hdr, &ehdr_template, sizeof(ehdr_template));
	memset(obj->elf_shdr, 0, sizeof(obj->elf_shdr));
	memset(obj->elf_sym, 0, sizeof(obj->elf_sym));

	p = obj->data;

	p += emit_section_shstrtab(obj, p);
	p += emit_section_text(obj, p, code_ptr, code_size);
	p += emit_section_strtab_syms(obj, p, code_size);
	p += emit_section_debug(obj, p, dw);

	int fd = open("jit.elf", O_CREAT | O_TRUNC | O_WRONLY);
	write(fd, obj, sizeof(*obj));
	close(fd);

	jit_register_gdb_entry((const char *)obj, sizeof(*obj));
}

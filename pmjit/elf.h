/*-
 * Copyright (c) 2001 David E. O'Brien.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#ifndef _ELF_H_
#define _ELF_H_

/* Indexes into the e_ident array.  Keep synced with
   http://www.sco.com/developers/gabi/latest/ch4.eheader.html */
#define	EI_MAG0		0	/* Magic number, byte 0. */
#define	EI_MAG1		1	/* Magic number, byte 1. */
#define	EI_MAG2		2	/* Magic number, byte 2. */
#define	EI_MAG3		3	/* Magic number, byte 3. */
#define	EI_CLASS	4	/* Class of machine. */
#define	EI_DATA		5	/* Data format. */
#define	EI_VERSION	6	/* ELF format version. */
#define	EI_OSABI	7	/* Operating system / ABI identification */
#define	EI_ABIVERSION	8	/* ABI version */
#define	OLD_EI_BRAND	8	/* Start of architecture identification. */
#define	EI_PAD		9	/* Start of padding (per SVR4 ABI). */
#define	EI_NIDENT	16	/* Size of e_ident array. */

/* Values for the magic number bytes. */
#define	ELFMAG0		0x7f
#define	ELFMAG1		'E'
#define	ELFMAG2		'L'
#define	ELFMAG3		'F'
#define	ELFMAG		"\177ELF"	/* magic string */
#define	SELFMAG		4		/* magic string size */

/* Values for e_ident[EI_VERSION] and e_version. */
#define	EV_NONE		0
#define	EV_CURRENT	1

/* Values for e_ident[EI_CLASS]. */
#define	ELFCLASSNONE	0	/* Unknown class. */
#define	ELFCLASS32	1	/* 32-bit architecture. */
#define	ELFCLASS64	2	/* 64-bit architecture. */

/* Values for e_ident[EI_DATA]. */
#define	ELFDATANONE	0	/* Unknown data format. */
#define	ELFDATA2LSB	1	/* 2's complement little-endian. */
#define	ELFDATA2MSB	2	/* 2's complement big-endian. */

/* Values for e_ident[EI_OSABI]. */
#define	ELFOSABI_NONE		0	/* UNIX System V ABI */
#define	ELFOSABI_HPUX		1	/* HP-UX operating system */
#define	ELFOSABI_NETBSD		2	/* NetBSD */
#define	ELFOSABI_LINUX		3	/* GNU/Linux */
#define	ELFOSABI_HURD		4	/* GNU/Hurd */
#define	ELFOSABI_86OPEN		5	/* 86Open common IA32 ABI */
#define	ELFOSABI_SOLARIS	6	/* Solaris */
#define	ELFOSABI_AIX		7	/* AIX */
#define	ELFOSABI_IRIX		8	/* IRIX */
#define	ELFOSABI_FREEBSD	9	/* FreeBSD */
#define	ELFOSABI_TRU64		10	/* TRU64 UNIX */
#define	ELFOSABI_MODESTO	11	/* Novell Modesto */
#define	ELFOSABI_OPENBSD	12	/* OpenBSD */
#define	ELFOSABI_OPENVMS	13	/* Open VMS */
#define	ELFOSABI_NSK		14	/* HP Non-Stop Kernel */
#define	ELFOSABI_AROS		15	/* Amiga Research OS */
#define	ELFOSABI_ARM		97	/* ARM */
#define	ELFOSABI_STANDALONE	255	/* Standalone (embedded) application */

#define	ELFOSABI_SYSV		ELFOSABI_NONE	/* symbol used in old spec */
#define	ELFOSABI_MONTEREY	ELFOSABI_AIX	/* Monterey */

/* Values for e_type. */
#define	ET_NONE		0	/* Unknown type. */
#define	ET_REL		1	/* Relocatable. */
#define	ET_EXEC		2	/* Executable. */
#define	ET_DYN		3	/* Shared object. */
#define	ET_CORE		4	/* Core file. */
#define	ET_LOOS		0xfe00	/* First operating system specific. */
#define	ET_HIOS		0xfeff	/* Last operating system-specific. */
#define	ET_LOPROC	0xff00	/* First processor-specific. */
#define	ET_HIPROC	0xffff	/* Last processor-specific. */

/* Values for e_machine. */
#define	EM_NONE		0	/* Unknown machine. */
#define	EM_M32		1	/* AT&T WE32100. */
#define	EM_SPARC	2	/* Sun SPARC. */
#define	EM_386		3	/* Intel i386. */
#define	EM_68K		4	/* Motorola 68000. */
#define	EM_88K		5	/* Motorola 88000. */
#define	EM_860		7	/* Intel i860. */
#define	EM_MIPS		8	/* MIPS R3000 Big-Endian only. */
#define	EM_S370		9	/* IBM System/370. */
#define	EM_MIPS_RS3_LE	10	/* MIPS R3000 Little-Endian. */
#define	EM_PARISC	15	/* HP PA-RISC. */
#define	EM_VPP500	17	/* Fujitsu VPP500. */
#define	EM_SPARC32PLUS	18	/* SPARC v8plus. */
#define	EM_960		19	/* Intel 80960. */
#define	EM_PPC		20	/* PowerPC 32-bit. */
#define	EM_PPC64	21	/* PowerPC 64-bit. */
#define	EM_S390		22	/* IBM System/390. */
#define	EM_V800		36	/* NEC V800. */
#define	EM_FR20		37	/* Fujitsu FR20. */
#define	EM_RH32		38	/* TRW RH-32. */
#define	EM_RCE		39	/* Motorola RCE. */
#define	EM_ARM		40	/* ARM. */
#define	EM_SH		42	/* Hitachi SH. */
#define	EM_SPARCV9	43	/* SPARC v9 64-bit. */
#define	EM_TRICORE	44	/* Siemens TriCore embedded processor. */
#define	EM_ARC		45	/* Argonaut RISC Core. */
#define	EM_H8_300	46	/* Hitachi H8/300. */
#define	EM_H8_300H	47	/* Hitachi H8/300H. */
#define	EM_H8S		48	/* Hitachi H8S. */
#define	EM_H8_500	49	/* Hitachi H8/500. */
#define	EM_IA_64	50	/* Intel IA-64 Processor. */
#define	EM_MIPS_X	51	/* Stanford MIPS-X. */
#define	EM_COLDFIRE	52	/* Motorola ColdFire. */
#define	EM_68HC12	53	/* Motorola M68HC12. */
#define	EM_MMA		54	/* Fujitsu MMA. */
#define	EM_PCP		55	/* Siemens PCP. */
#define	EM_NCPU		56	/* Sony nCPU. */
#define	EM_NDR1		57	/* Denso NDR1 microprocessor. */
#define	EM_STARCORE	58	/* Motorola Star*Core processor. */
#define	EM_ME16		59	/* Toyota ME16 processor. */
#define	EM_ST100	60	/* STMicroelectronics ST100 processor. */
#define	EM_TINYJ	61	/* Advanced Logic Corp. TinyJ processor. */
#define	EM_X86_64	62	/* Advanced Micro Devices x86-64 */
#define	EM_AMD64	EM_X86_64	/* Advanced Micro Devices x86-64 (compat) */
#define	EM_PDSP		63	/* Sony DSP Processor. */
#define	EM_FX66		66	/* Siemens FX66 microcontroller. */
#define	EM_ST9PLUS	67	/* STMicroelectronics ST9+ 8/16
				   microcontroller. */
#define	EM_ST7		68	/* STmicroelectronics ST7 8-bit
				   microcontroller. */
#define	EM_68HC16	69	/* Motorola MC68HC16 microcontroller. */
#define	EM_68HC11	70	/* Motorola MC68HC11 microcontroller. */
#define	EM_68HC08	71	/* Motorola MC68HC08 microcontroller. */
#define	EM_68HC05	72	/* Motorola MC68HC05 microcontroller. */
#define	EM_SVX		73	/* Silicon Graphics SVx. */
#define	EM_ST19		74	/* STMicroelectronics ST19 8-bit mc. */
#define	EM_VAX		75	/* Digital VAX. */
#define	EM_CRIS		76	/* Axis Communications 32-bit embedded
				   processor. */
#define	EM_JAVELIN	77	/* Infineon Technologies 32-bit embedded
				   processor. */
#define	EM_FIREPATH	78	/* Element 14 64-bit DSP Processor. */
#define	EM_ZSP		79	/* LSI Logic 16-bit DSP Processor. */
#define	EM_MMIX		80	/* Donald Knuth's educational 64-bit proc. */
#define	EM_HUANY	81	/* Harvard University machine-independent
				   object files. */
#define	EM_PRISM	82	/* SiTera Prism. */
#define	EM_AVR		83	/* Atmel AVR 8-bit microcontroller. */
#define	EM_FR30		84	/* Fujitsu FR30. */
#define	EM_D10V		85	/* Mitsubishi D10V. */
#define	EM_D30V		86	/* Mitsubishi D30V. */
#define	EM_V850		87	/* NEC v850. */
#define	EM_M32R		88	/* Mitsubishi M32R. */
#define	EM_MN10300	89	/* Matsushita MN10300. */
#define	EM_MN10200	90	/* Matsushita MN10200. */
#define	EM_PJ		91	/* picoJava. */
#define	EM_OPENRISC	92	/* OpenRISC 32-bit embedded processor. */
#define	EM_ARC_A5	93	/* ARC Cores Tangent-A5. */
#define	EM_XTENSA	94	/* Tensilica Xtensa Architecture. */
#define	EM_VIDEOCORE	95	/* Alphamosaic VideoCore processor. */
#define	EM_TMM_GPP	96	/* Thompson Multimedia General Purpose
				   Processor. */
#define	EM_NS32K	97	/* National Semiconductor 32000 series. */
#define	EM_TPC		98	/* Tenor Network TPC processor. */
#define	EM_SNP1K	99	/* Trebia SNP 1000 processor. */
#define	EM_ST200	100	/* STMicroelectronics ST200 microcontroller. */
#define	EM_IP2K		101	/* Ubicom IP2xxx microcontroller family. */
#define	EM_MAX		102	/* MAX Processor. */
#define	EM_CR		103	/* National Semiconductor CompactRISC
				   microprocessor. */
#define	EM_F2MC16	104	/* Fujitsu F2MC16. */
#define	EM_MSP430	105	/* Texas Instruments embedded microcontroller
				   msp430. */
#define	EM_BLACKFIN	106	/* Analog Devices Blackfin (DSP) processor. */
#define	EM_SE_C33	107	/* S1C33 Family of Seiko Epson processors. */
#define	EM_SEP		108	/* Sharp embedded microprocessor. */
#define	EM_ARCA		109	/* Arca RISC Microprocessor. */
#define	EM_UNICORE	110	/* Microprocessor series from PKU-Unity Ltd.
				   and MPRC of Peking University */
#define EM_AARCH64      183	/* ARM AARCH64 */
#define EM_TILEPRO      188	/* Tilera TILEPro */
#define EM_MICROBLAZE   189	/* Xilinx MicroBlaze */
#define EM_TILEGX       191	/* Tilera TILE-Gx */


/* Special section indexes. */
#define	SHN_UNDEF	     0		/* Undefined, missing, irrelevant. */
#define	SHN_LORESERVE	0xff00		/* First of reserved range. */
#define	SHN_LOPROC	0xff00		/* First processor-specific. */
#define	SHN_HIPROC	0xff1f		/* Last processor-specific. */
#define	SHN_LOOS	0xff20		/* First operating system-specific. */
#define	SHN_HIOS	0xff3f		/* Last operating system-specific. */
#define	SHN_ABS		0xfff1		/* Absolute values. */
#define	SHN_COMMON	0xfff2		/* Common data. */
#define	SHN_XINDEX	0xffff		/* Escape -- index stored elsewhere. */
#define	SHN_HIRESERVE	0xffff		/* Last of reserved range. */

/* sh_type */
#define	SHT_NULL		0	/* inactive */
#define	SHT_PROGBITS		1	/* program defined information */
#define	SHT_SYMTAB		2	/* symbol table section */
#define	SHT_STRTAB		3	/* string table section */
#define	SHT_RELA		4	/* relocation section with addends */
#define	SHT_HASH		5	/* symbol hash table section */
#define	SHT_DYNAMIC		6	/* dynamic section */
#define	SHT_NOTE		7	/* note section */
#define	SHT_NOBITS		8	/* no space section */
#define	SHT_REL			9	/* relocation section - no addends */
#define	SHT_SHLIB		10	/* reserved - purpose unknown */
#define	SHT_DYNSYM		11	/* dynamic symbol table section */
#define	SHT_INIT_ARRAY		14	/* Initialization function pointers. */
#define	SHT_FINI_ARRAY		15	/* Termination function pointers. */
#define	SHT_PREINIT_ARRAY	16	/* Pre-initialization function ptrs. */
#define	SHT_GROUP		17	/* Section group. */
#define	SHT_SYMTAB_SHNDX	18	/* Section indexes (see SHN_XINDEX). */
#define	SHT_LOOS		0x60000000	/* First of OS specific semantics */
#define	SHT_LOSUNW		0x6ffffff4
#define	SHT_SUNW_dof		0x6ffffff4
#define	SHT_SUNW_cap		0x6ffffff5
#define	SHT_SUNW_SIGNATURE	0x6ffffff6
#define	SHT_GNU_HASH		0x6ffffff6
#define	SHT_GNU_LIBLIST		0x6ffffff7
#define	SHT_SUNW_ANNOTATE	0x6ffffff7
#define	SHT_SUNW_DEBUGSTR	0x6ffffff8
#define	SHT_SUNW_DEBUG		0x6ffffff9
#define	SHT_SUNW_move		0x6ffffffa
#define	SHT_SUNW_COMDAT		0x6ffffffb
#define	SHT_SUNW_syminfo	0x6ffffffc
#define	SHT_SUNW_verdef		0x6ffffffd
#define	SHT_GNU_verdef		0x6ffffffd	/* Symbol versions provided */
#define	SHT_SUNW_verneed	0x6ffffffe
#define	SHT_GNU_verneed		0x6ffffffe	/* Symbol versions required */
#define	SHT_SUNW_versym		0x6fffffff
#define	SHT_GNU_versym		0x6fffffff	/* Symbol version table */
#define	SHT_HISUNW		0x6fffffff
#define	SHT_HIOS		0x6fffffff	/* Last of OS specific semantics */
#define	SHT_LOPROC		0x70000000	/* reserved range for processor */
#define	SHT_AMD64_UNWIND	0x70000001	/* unwind information */
#define	SHT_ARM_EXIDX		0x70000001	/* Exception index table. */
#define	SHT_ARM_PREEMPTMAP	0x70000002	/* BPABI DLL dynamic linking 
						   pre-emption map. */
#define	SHT_ARM_ATTRIBUTES	0x70000003	/* Object file compatibility 
						   attributes. */
#define	SHT_ARM_DEBUGOVERLAY	0x70000004	/* See DBGOVL for details. */
#define	SHT_ARM_OVERLAYSECTION	0x70000005	/* See DBGOVL for details. */
#define	SHT_MIPS_REGINFO	0x70000006
#define	SHT_MIPS_OPTIONS	0x7000000d
#define	SHT_MIPS_DWARF		0x7000001e	/* MIPS gcc uses MIPS_DWARF */
#define	SHT_HIPROC		0x7fffffff	/* specific section header types */
#define	SHT_LOUSER		0x80000000	/* reserved range for application */
#define	SHT_HIUSER		0xffffffff	/* specific indexes */

/* Flags for sh_flags. */
#define	SHF_WRITE		0x1	/* Section contains writable data. */
#define	SHF_ALLOC		0x2	/* Section occupies memory. */
#define	SHF_EXECINSTR		0x4	/* Section contains instructions. */
#define	SHF_MERGE		0x10	/* Section may be merged. */
#define	SHF_STRINGS		0x20	/* Section contains strings. */
#define	SHF_INFO_LINK		0x40	/* sh_info holds section index. */
#define	SHF_LINK_ORDER		0x80	/* Special ordering requirements. */
#define	SHF_OS_NONCONFORMING	0x100	/* OS-specific processing required. */
#define	SHF_GROUP		0x200	/* Member of section group. */
#define	SHF_TLS			0x400	/* Section contains TLS data. */
#define	SHF_MASKOS	0x0ff00000	/* OS-specific semantics. */
#define	SHF_MASKPROC	0xf0000000	/* Processor-specific semantics. */

/* Values for p_type. */
#define	PT_NULL		0	/* Unused entry. */
#define	PT_LOAD		1	/* Loadable segment. */
#define	PT_DYNAMIC	2	/* Dynamic linking information segment. */
#define	PT_INTERP	3	/* Pathname of interpreter. */
#define	PT_NOTE		4	/* Auxiliary information. */
#define	PT_SHLIB	5	/* Reserved (not used). */
#define	PT_PHDR		6	/* Location of program header itself. */
#define	PT_TLS		7	/* Thread local storage segment */
#define	PT_LOOS		0x60000000	/* First OS-specific. */
#define	PT_SUNW_UNWIND	0x6464e550	/* amd64 UNWIND program header */
#define	PT_GNU_EH_FRAME	0x6474e550
#define	PT_GNU_STACK	0x6474e551
#define	PT_GNU_RELRO	0x6474e552
#define	PT_LOSUNW	0x6ffffffa
#define	PT_SUNWBSS	0x6ffffffa	/* Sun Specific segment */
#define	PT_SUNWSTACK	0x6ffffffb	/* describes the stack segment */
#define	PT_SUNWDTRACE	0x6ffffffc	/* private */
#define	PT_SUNWCAP	0x6ffffffd	/* hard/soft capabilities segment */
#define	PT_HISUNW	0x6fffffff
#define	PT_HIOS		0x6fffffff	/* Last OS-specific. */
#define	PT_LOPROC	0x70000000	/* First processor-specific type. */
#define	PT_HIPROC	0x7fffffff	/* Last processor-specific type. */



/* Values for d_tag. */
#define	DT_NULL		0	/* Terminating entry. */
#define	DT_NEEDED	1	/* String table offset of a needed shared
				   library. */
#define	DT_PLTRELSZ	2	/* Total size in bytes of PLT relocations. */
#define	DT_PLTGOT	3	/* Processor-dependent address. */
#define	DT_HASH		4	/* Address of symbol hash table. */
#define	DT_STRTAB	5	/* Address of string table. */
#define	DT_SYMTAB	6	/* Address of symbol table. */
#define	DT_RELA		7	/* Address of ElfNN_Rela relocations. */
#define	DT_RELASZ	8	/* Total size of ElfNN_Rela relocations. */
#define	DT_RELAENT	9	/* Size of each ElfNN_Rela relocation entry. */
#define	DT_STRSZ	10	/* Size of string table. */
#define	DT_SYMENT	11	/* Size of each symbol table entry. */
#define	DT_INIT		12	/* Address of initialization function. */
#define	DT_FINI		13	/* Address of finalization function. */
#define	DT_SONAME	14	/* String table offset of shared object
				   name. */
#define	DT_RPATH	15	/* String table offset of library path. [sup] */
#define	DT_SYMBOLIC	16	/* Indicates "symbolic" linking. [sup] */
#define	DT_REL		17	/* Address of ElfNN_Rel relocations. */
#define	DT_RELSZ	18	/* Total size of ElfNN_Rel relocations. */
#define	DT_RELENT	19	/* Size of each ElfNN_Rel relocation. */
#define	DT_PLTREL	20	/* Type of relocation used for PLT. */
#define	DT_DEBUG	21	/* Reserved (not used). */
#define	DT_TEXTREL	22	/* Indicates there may be relocations in
				   non-writable segments. [sup] */
#define	DT_JMPREL	23	/* Address of PLT relocations. */
#define	DT_BIND_NOW	24	/* [sup] */
#define	DT_INIT_ARRAY	25	/* Address of the array of pointers to
				   initialization functions */
#define	DT_FINI_ARRAY	26	/* Address of the array of pointers to
				   termination functions */
#define	DT_INIT_ARRAYSZ	27	/* Size in bytes of the array of
				   initialization functions. */
#define	DT_FINI_ARRAYSZ	28	/* Size in bytes of the array of
				   termination functions. */
#define	DT_RUNPATH	29	/* String table offset of a null-terminated
				   library search path string. */
#define	DT_FLAGS	30	/* Object specific flag values. */
#define	DT_ENCODING	32	/* Values greater than or equal to DT_ENCODING
				   and less than DT_LOOS follow the rules for
				   the interpretation of the d_un union
				   as follows: even == 'd_ptr', odd == 'd_val'
				   or none */
#define	DT_PREINIT_ARRAY 32	/* Address of the array of pointers to
				   pre-initialization functions. */
#define	DT_PREINIT_ARRAYSZ 33	/* Size in bytes of the array of
				   pre-initialization functions. */
#define	DT_MAXPOSTAGS	34	/* number of positive tags */
#define	DT_LOOS		0x6000000d	/* First OS-specific */
#define	DT_SUNW_AUXILIARY	0x6000000d	/* symbol auxiliary name */
#define	DT_SUNW_RTLDINF		0x6000000e	/* ld.so.1 info (private) */
#define	DT_SUNW_FILTER		0x6000000f	/* symbol filter name */
#define	DT_SUNW_CAP		0x60000010	/* hardware/software */
#define	DT_HIOS		0x6ffff000	/* Last OS-specific */



/* Symbol Binding - ELFNN_ST_BIND - st_info */
#define	STB_LOCAL	0	/* Local symbol */
#define	STB_GLOBAL	1	/* Global symbol */
#define	STB_WEAK	2	/* like global - lower precedence */
#define	STB_LOOS	10	/* Reserved range for operating system */
#define	STB_HIOS	12	/*   specific semantics. */
#define	STB_LOPROC	13	/* reserved range for processor */
#define	STB_HIPROC	15	/*   specific semantics. */

/* Symbol type - ELFNN_ST_TYPE - st_info */
#define	STT_NOTYPE	0	/* Unspecified type. */
#define	STT_OBJECT	1	/* Data object. */
#define	STT_FUNC	2	/* Function. */
#define	STT_SECTION	3	/* Section. */
#define	STT_FILE	4	/* Source file. */
#define	STT_COMMON	5	/* Uninitialized common block. */
#define	STT_TLS		6	/* TLS object. */
#define	STT_NUM		7
#define	STT_LOOS	10	/* Reserved range for operating system */
#define	STT_GNU_IFUNC	10
#define	STT_HIOS	12	/*   specific semantics. */
#define	STT_LOPROC	13	/* reserved range for processor */
#define	STT_HIPROC	15	/*   specific semantics. */

/* Symbol visibility - ELFNN_ST_VISIBILITY - st_other */
#define	STV_DEFAULT	0x0	/* Default visibility (see binding). */
#define	STV_INTERNAL	0x1	/* Special meaning in relocatable objects. */
#define	STV_HIDDEN	0x2	/* Not visible. */
#define	STV_PROTECTED	0x3	/* Visible but not preemptible. */
#define	STV_EXPORTED	0x4
#define	STV_SINGLETON	0x5
#define	STV_ELIMINATE	0x6



/*
 * ELF definitions common to all 64-bit architectures.
 */

typedef uint64_t	Elf64_Addr;
typedef uint16_t	Elf64_Half;
typedef uint64_t	Elf64_Off;
typedef int32_t		Elf64_Sword;
typedef int64_t		Elf64_Sxword;
typedef uint32_t	Elf64_Word;
typedef uint64_t	Elf64_Lword;
typedef uint64_t	Elf64_Xword;

typedef Elf64_Word	Elf64_Hashelt;

/* Non-standard class-dependent datatype used for abstraction. */
typedef Elf64_Xword	Elf64_Size;
typedef Elf64_Sxword	Elf64_Ssize;

/*
 * ELF header.
 */

typedef struct {
	unsigned char	e_ident[EI_NIDENT];	/* File identification. */
	Elf64_Half	e_type;		/* File type. */
	Elf64_Half	e_machine;	/* Machine architecture. */
	Elf64_Word	e_version;	/* ELF format version. */
	Elf64_Addr	e_entry;	/* Entry point. */
	Elf64_Off	e_phoff;	/* Program header file offset. */
	Elf64_Off	e_shoff;	/* Section header file offset. */
	Elf64_Word	e_flags;	/* Architecture-specific flags. */
	Elf64_Half	e_ehsize;	/* Size of ELF header in bytes. */
	Elf64_Half	e_phentsize;	/* Size of program header entry. */
	Elf64_Half	e_phnum;	/* Number of program header entries. */
	Elf64_Half	e_shentsize;	/* Size of section header entry. */
	Elf64_Half	e_shnum;	/* Number of section header entries. */
	Elf64_Half	e_shstrndx;	/* Section name strings section. */
} Elf64_Ehdr;

/*
 * Section header.
 */

typedef struct {
	Elf64_Word	sh_name;	/* Section name (index into the
					   section header string table). */
	Elf64_Word	sh_type;	/* Section type. */
	Elf64_Xword	sh_flags;	/* Section flags. */
	Elf64_Addr	sh_addr;	/* Address in memory image. */
	Elf64_Off	sh_offset;	/* Offset in file. */
	Elf64_Xword	sh_size;	/* Size in bytes. */
	Elf64_Word	sh_link;	/* Index of a related section. */
	Elf64_Word	sh_info;	/* Depends on section type. */
	Elf64_Xword	sh_addralign;	/* Alignment in bytes. */
	Elf64_Xword	sh_entsize;	/* Size of each entry in section. */
} Elf64_Shdr;

/*
 * Program header.
 */

typedef struct {
	Elf64_Word	p_type;		/* Entry type. */
	Elf64_Word	p_flags;	/* Access permission flags. */
	Elf64_Off	p_offset;	/* File offset of contents. */
	Elf64_Addr	p_vaddr;	/* Virtual address in memory image. */
	Elf64_Addr	p_paddr;	/* Physical address (not used). */
	Elf64_Xword	p_filesz;	/* Size of contents in file. */
	Elf64_Xword	p_memsz;	/* Size of contents in memory. */
	Elf64_Xword	p_align;	/* Alignment in memory and file. */
} Elf64_Phdr;

/*
 * Symbol table entries.
 */

typedef struct {
	Elf64_Word	st_name;	/* String table index of name. */
	unsigned char	st_info;	/* Type and binding information. */
	unsigned char	st_other;	/* Reserved (not used). */
	Elf64_Half	st_shndx;	/* Section index of symbol. */
	Elf64_Addr	st_value;	/* Symbol value. */
	Elf64_Xword	st_size;	/* Size of associated object. */
} Elf64_Sym;

/* Macros for accessing the fields of st_info. */
#define	ELF64_ST_BIND(info)		((info) >> 4)
#define	ELF64_ST_TYPE(info)		((info) & 0xf)

/* Macro for constructing st_info from field values. */
#define	ELF64_ST_INFO(bind, type)	(((bind) << 4) + ((type) & 0xf))

/* Macro for accessing the fields of st_other. */
#define	ELF64_ST_VISIBILITY(oth)	((oth) & 0x3)


#endif

// SPDX-License-Identifier: BSD-3-Clause

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <elf.h>
#include <string.h>
#include <sys/random.h>

#define STACK_PAGE_C 1024

void *map_elf(const char *filename)
{
	// This part helps you store the content of the ELF file inside the buffer.
	struct stat st;
	void *file;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror("open");
		exit(1);
	}

	fstat(fd, &st);

	file = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (file == MAP_FAILED) {
		perror("mmap");
		close(fd);
		exit(1);
	}

	return file;
}

void load_and_run(const char *filename, int argc, char **argv, char **envp)
{
	void *elf_contents = (char *)map_elf(filename);
	Elf64_Ehdr *elf_header = (Elf64_Ehdr *)elf_contents;
	if (memcmp(elf_header->e_ident, ELFMAG, SELFMAG)) {
		fprintf(stderr, "Not a valid ELF file\n");
		exit(3);
	}

	if ((char)elf_header->e_ident[EI_CLASS] != ELFCLASS64) {
		fprintf(stderr, "Not a 64-bit ELF\n");
		exit(4);
	}

	long page_size = sysconf(_SC_PAGESIZE);
	long at_phdr = 0;

	Elf64_Phdr *program_header_table = elf_contents + elf_header->e_phoff;
	Elf64_Phdr *program_header = program_header_table;
	for (int i = 0; i < elf_header->e_phnum; i++) {

		if (program_header->p_type != PT_LOAD) {
			program_header = (Elf64_Phdr *)((void *)program_header + elf_header->e_phentsize);
			continue;
		}

		if (elf_header->e_phoff >= program_header->p_offset &&
		    elf_header->e_phoff < program_header->p_offset + program_header->p_filesz) {
			at_phdr = program_header->p_vaddr + (elf_header->e_phoff - program_header->p_offset);
		}

		int permisions = 0;
		if (program_header->p_flags & PF_R) {
			permisions = permisions | PROT_READ;
		}
		if (program_header->p_flags & PF_W) {
			permisions = permisions | PROT_WRITE;
		}
		if (program_header->p_flags & PF_X) {
			permisions = permisions | PROT_EXEC;
		}


		void *p_ali_vaddr = program_header->p_vaddr - (program_header->p_vaddr % page_size);

		mmap(p_ali_vaddr, program_header->p_memsz + (program_header->p_vaddr % page_size),
			 PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED_NOREPLACE, -1, 0);

		memcpy(program_header->p_vaddr, elf_contents + program_header->p_offset, program_header->p_filesz);

		mprotect(p_ali_vaddr, program_header->p_memsz + (program_header->p_vaddr % page_size), permisions);

		program_header = (Elf64_Phdr *)((void *)program_header + elf_header->e_phentsize);
	}

	void *stack_base = mmap(NULL, STACK_PAGE_C * page_size, PROT_READ | PROT_WRITE,
							MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	
	long arg_size = 0;
	for (int i = 0; i < argc; i++) {
		arg_size += strlen(argv[i]);
	}

	long envc = 0;
	long env_size = 0;

	for (int i = 0; envp[i] != 0; i++) {
		envc++;
		env_size += strlen(envp[i]);
	}

	void *stack_top = stack_base + STACK_PAGE_C * page_size;
	void *stack_data = (stack_top - arg_size - env_size - 128)
					  - (((long)stack_top - arg_size - env_size - 16) % 16);
	long c = (envc + argc) % 2 == 0 ? 4 : 3;
	void *stack_pointers = stack_data - (envc + argc + c) * 8 - 14 * 16;
	void *sp = stack_pointers;

	*(long *)stack_pointers = argc;
	stack_pointers += 8;

	for (int i = 0; i < argc; i++) {

		*(char **)stack_pointers = stack_data;
		stack_data =  stpcpy(stack_data, argv[i]) + 1;
		
		stack_pointers += 8;
	}

	*(long *)stack_pointers = 0;
	stack_pointers += 8;

	for (int i = 0; i < envc; i++) {

		*(char **)stack_pointers = stack_data;
		stack_data =  stpcpy(stack_data, envp[i]) + 1;
		
		stack_pointers += 8;
	}

	*(long *)stack_pointers = 0;
	stack_pointers += 8;
	

	uint64_t auxv[28] = {
		 AT_PHDR, at_phdr,
		 AT_PHENT, elf_header->e_phentsize,
		 AT_PHNUM, elf_header->e_phnum,
		 AT_PAGESZ, page_size,
		 AT_BASE, 0,
		 AT_ENTRY, elf_header->e_entry,
		 AT_UID, getuid(),
		 AT_EUID, geteuid(),
		 AT_GID, getgid(),
		 AT_EGID, getegid(),
		 AT_SECURE, 0,
		 AT_RANDOM, 0,
		 AT_PLATFORM, 0,
		 AT_NULL, 0
	};

	getrandom(stack_data, 16, 0);
	auxv[23]= (uint64_t)stack_data;

	stack_data += 16;

	strcpy(stack_data, "x86_64");
	auxv[25] = (uint64_t)stack_data;

	memcpy(stack_pointers, auxv, sizeof(auxv));

	/**
	 * TODO: Support Static PIE Executables
	 * Map PT_LOAD segments at a random load base.
	 * Adjust virtual addresses of segments and entry point by load_base.
	 * Stack setup (argc, argv, envp, auxv) same as above.
	 */

	// TODO: Set the entry point and the stack pointer
	void (*entry)() = elf_header->e_entry;

	// Transfer control
	__asm__ __volatile__(
			"mov %0, %%rsp\n"
			"xor %%rbp, %%rbp\n"
			"jmp *%1\n"
			:
			: "r"(sp), "r"(entry)
			: "memory"
			);
}

int main(int argc, char **argv, char **envp)
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <static-elf-binary>\n", argv[0]);
		exit(1);
	}

	load_and_run(argv[1], argc - 1, &argv[1], envp);
	return 0;
}

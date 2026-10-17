#include <sys/types.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/mman.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <stdint.h>

#include <fstream>
#include <string_view>
#include <vector>

#include "elf.h"

static int ptraceAttach(pid_t pid)
{
	if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) < 0) {
		perror("ptrace_attach: PTRACE_ATTACH failed");
		return -1;
	}

	int status;
	if (waitpid(pid, &status, 0) == -1) {
		perror("ptrace_attach: waitpid failed");
		return -1;
	}

	if (!WIFSTOPPED(status)) {
		fprintf(stderr, "process %d did not stop as expected\n", pid);
		return -1;
	}

	return 0;
}

static void ptraceDetach(pid_t pid)
{
	ptrace(PTRACE_DETACH, pid, NULL, NULL);
}

#if 0
static char *ptrace_read_string(pid_t pid, void *addr)
{
	char buf[4096];
	size_t len = 0;

	while (len < sizeof(buf) - 1) {
		long val = ptrace(PTRACE_PEEKTEXT, pid,
				  (void *)(addr + len), NULL);
		if (val == -1 && errno != EIO) {
			perror("ptrace_read_string: PEEKTEXT failed");
			return NULL;
		}

		unsigned char *bytes = (unsigned char *)&val;
		for (int i = 0; i < sizeof(long); i++) {
			if (bytes[i] == '\0') {
				buf[len] = '\0';
				char *str = g_malloc(len + 1);
				memcpy(str, buf, len + 1);
				return str;
			}
			buf[len++] = bytes[i];
		}
	}

	buf[len] = '\0';
	return g_strndup(buf, len);
}
#endif

static void ptrace_write_string(pid_t pid, long *addr, long val)
{
	if (ptrace(PTRACE_POKETEXT, pid, (void *)addr, (void *)&val) < 0) {
		perror("ptrace_write_string: POKETEXT failed");
		return;
	}
}

static bool hasLibMutter(std::ifstream &file)
{
	std::string line;
	while (std::getline(file, line)) {
		// 7fef9a800000-7fef9a83e000 r-xp 00000000 103:04 23623016                  /usr/lib/libmutter-18.so.0.0.0
		auto dash = line.find('-');
		if (dash == std::string::npos)
			continue;
		auto space = line.find(' ');
		if (space == std::string::npos)
			continue;

		std::string_view lineSV{line};
		std::string_view perms = lineSV.substr(space + 1, 4); // rw-p
		if (perms.length() != 4)
			continue;

		if ('x' != perms[2] && 'r' != perms[0])
			continue;

		size_t npos = line.find("libmutter");
		if (std::string::npos != npos)
			return true;
	}

	return false;
}

static bool detectLibc(std::ifstream &file)
{
	std::string line;
	while (std::getline(file, line)) {
		// 7fef9a800000-7fef9a83e000 r-xp 00000000 103:04 23623016                  /usr/lib/libmutter-18.so.0.0.0
		auto dash = line.find('-');
		if (dash == std::string::npos)
			continue;
		auto space = line.find(' ');
		if (space == std::string::npos)
			continue;

		std::string_view lineSV{line};
		std::string_view perms = lineSV.substr(space + 1, 4); // rw-p
		if (perms.length() != 4)
			continue;

		if ('x' != perms[2] && 'r' != perms[0])
			continue;

		size_t npos = line.find("libc.so.6");
		if (std::string::npos != npos)
			return true;
	}

	return false;
}


struct LibcFunctions find_libc(int pid)
{
    struct LibcFunctions ret = {0};

    char libcPath[256];
    uint64_t elfloc = 0;

    struct Section xloc;
    struct Sections sections;

    // Discover the location of libc.so.6 in the target process's memory space
    {
        struct ProcfsMapIter* iter = procfs_map_iter_new(pid, "libc.so.6");
        if (!iter)
        {
            return ret;
        }

        sections_init(&sections);

        struct ProcfsMapEntry* entry;
        while ((entry = procfs_map_iter_next(iter)))
        {
            if (!entry->pathname)
            {
                log("Found libc: %llx %llx-%llx %.4s (no pathname?)\n", entry->offset, entry->start, entry->end, entry->perms);
                continue;
            }

            log("Found libc: %llx %llx-%llx %.4s\n", entry->offset, entry->start, entry->end, entry->perms);
            if (0 == entry->offset)
            {
                elfloc = entry->start;
                strncpy(libcPath, entry->pathname, sizeof(libcPath) - 1);
                libcPath[255] = '\0';
            }

            if (entry->perms[2] == 'x')
            {
                xloc.start  = entry->start;
                xloc.end    = entry->end;
                xloc.offset = entry->offset;
            }
            if (entry->perms[0] == 'r'
             && entry->perms[1] != 'w'
             && entry->perms[2] != 'x')
            {
                sections_add(&sections, entry->start, entry->end, entry->offset);
            }
        }

        procfs_map_iter_free(iter);
    }

    if (!elfloc || !xloc.start || !xloc.end)
    {
        log("Failed to find libc.so.6 in process %d\n", pid);
        sections_free(&sections);
        return ret;
    }

    FILE* ram = NULL;
    Elf64_Shdr* shdrs = NULL;
    Elf64_Sym* sym_table = NULL;
    char* str_table = NULL;

    // Analyze and prove that we have libc decently mapped by reading the ELF header and section headers
    {
        char path[64] = {0};
        snprintf(path, sizeof(path), "/proc/%d/mem", pid);
        ram = fopen(path, "rb");
        if (!ram)
        {
            log("Failed to open /proc/%d/mem\n", pid);
            goto fini;
        }
    }

    // Start reading ELF
    Elf64_Ehdr ehdr;
    {
        fseeko64(ram, elfloc, SEEK_SET);
        if (1 != fread(&ehdr, sizeof(ehdr), 1, ram))
        {
            log("Failed to read ELF header from libc.so.6 in process %d\n", pid);
            goto fini;
        }

        if (ehdr.e_ident[0] != 0x7f || ehdr.e_ident[1] != 'E' || ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F')
        {
            log("Failed to read ELF header from libc.so.6 in process %d\n", pid);
            goto fini;
        }
    }

    // shdrs are not actually mapped in memory so we have to open the file and read them from disk
    shdrs = malloc(sizeof(Elf64_Shdr) * ehdr.e_shnum);
    {
        FILE* libc = fopen(libcPath, "rb");
        if (!libc)
        {
            log("Failed to open libc.so.6 at '%s'\n", libcPath);
            goto fini;
        }

        fseeko64(libc, ehdr.e_shoff, SEEK_SET);
        size_t amt = fread(shdrs, sizeof(Elf64_Shdr), ehdr.e_shnum, libc);
        fclose(libc);

        if (amt != ehdr.e_shnum)
        {
            log("Failed to read section headers from libc.so.6\n");
            goto fini;
        }
    }

    // as raw pointers inside 'shdrs'
    Elf64_Shdr* dynsym = NULL;
    Elf64_Shdr* strtab = NULL;
    for (int i = 0; i < ehdr.e_shnum; i++)
    {
        Elf64_Shdr* shdr = &shdrs[i]; 
        if (!(shdr->sh_flags & SHF_ALLOC))
        {
            continue;
        }

        // TODO: uncba to check the names...
        if (shdr->sh_type == SHT_DYNSYM)
            dynsym = shdr;
        else if (shdr->sh_type == SHT_STRTAB)
            strtab = shdr;
    }

    if (!dynsym || !strtab)
    {
        log("Failed to find dynsym or strtab in libc.so.6\n");
        goto fini;
    }

    // Load in from RAM the dynsym and strtab sections so we can iterate over them and find the symbols we want
    struct Section* dynsymSec = sections_find(&sections, dynsym->sh_offset, dynsym->sh_size);
    struct Section* strtabSec = sections_find(&sections, strtab->sh_offset, strtab->sh_size);
    if (!dynsymSec || !strtabSec)
    {
        log("Failed to find dynsym or strtab in mapped sections\n");
        goto fini;
    }

    sym_table = malloc(dynsym->sh_size);
    fseeko64(ram, dynsymSec->start + (dynsym->sh_offset - dynsymSec->offset), SEEK_SET);
    if (1 != fread(sym_table, dynsym->sh_size, 1, ram))
    {
        log("Failed to read dynsym from process %d\n", pid);
        goto fini;
    }

    str_table = malloc(strtab->sh_size);
    fseeko64(ram, strtabSec->start + (strtab->sh_offset - strtabSec->offset), SEEK_SET);
    if (1 != fread((void*)str_table, strtab->sh_size, 1, ram))
    {
        log("Failed to read strtab from process %d\n", pid);
        goto fini;
    }

    int total_symbols = dynsym->sh_size / sizeof(Elf64_Sym);
    for (int i = 0; i < total_symbols; i++) {
        if (sym_table[i].st_name == 0)
            continue;

        const char *sym_name = safe_str(str_table, strtab->sh_size, sym_table[i].st_name, 16);
        if (!sym_name)
            continue;

        if (strcmp(sym_name, "__errno_location") == 0)
        {
            ret.errno_location = xloc.start + sym_table[i].st_value - xloc.offset;
            if (ret.errno_location > xloc.end)
            {
                log("Found __errno_location outside of libc.so.6 mapping\n");
                ret.errno_location = 0;
            }
        }
        else if (strcmp(sym_name, "syscall") == 0)
        {
            ret.syscall = xloc.start + sym_table[i].st_value - xloc.offset;
            if (ret.syscall > xloc.end)
            {
                log("Found syscall outside of libc.so.6 mapping\n");
                ret.syscall = 0;
            }
        }
    }

    // verify that libc errno is actually usable. must be
    /*
    0:  f3 0f 1e fa                   <prolog stuff>
    4:  48 8b 05 05 b2 1e 00          mov    rax,QWORD PTR [rip+OFFSET]
    b:  64 48 03 04 25 00 00 00 00    add    rax,QWORD PTR fs:0x0
    14: c3                            <epilog stuff>
    */
    {
        uint8_t code[64];
        fseeko64(ram, ret.errno_location, SEEK_SET);
        if (1 != fread(code, sizeof(code), 1, ram))
        {
            log("Failed to read __errno_location code from process %d\n", pid);
            goto fini;
        }

        static const uint8_t offLoad[] = { 0x48, 0x8b, 0x05 };
        static const uint8_t fsLoad [] = { 0x64, 0x48, 0x03, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00 };

        uint8_t* offAt = memmem(code, sizeof(code), offLoad, sizeof(offLoad));
        uint8_t* fsAt  = memmem(code, sizeof(code), fsLoad,  sizeof(fsLoad));
        if (!offAt && !fsAt)
        {
            log("Failed to find __errno_location code pattern in process %d\n", pid);
            log_buff("errno_location_code", code, sizeof(code));
            goto fini;
        }

        uint64_t errno_loc = *(uint32_t*)(offAt + 3)
                           + (offAt - code)
                           + 7
                           + ret.errno_location;

        log("Found __errno_location fs offset: 0x%llx\n", errno_loc);
        int64_t offset;
        fseeko64(ram, errno_loc, SEEK_SET);
        if (1 != fread(&offset, sizeof(offset), 1, ram))
        {
            log("Failed to read __errno_location fs offset from process %d\n", pid);
            goto fini;
        }

        log("Found __errno_location fs offset value: 0x%llx\n", offset);
        if (ERRNO_OFFSET_MIN < offset && offset < ERRNO_OFFSET_MAX)
        {
            ret.errno_fs_offset = (int)offset;
        }
        else
        {
            log("Found __errno_location fs offset value is out of range: 0x%llx\n", offset);
            goto fini;
        }
    }

fini:
    if (str_table) free(str_table);
    if (sym_table) free(sym_table);
    if (shdrs) free(shdrs);
    if (ram) fclose(ram);
    sections_free(&sections);
    return ret;
}

static std::vector<pid_t> searchProcess()
{
	std::vector<pid_t> result;

	DIR *dir = opendir("/proc");
	while (auto entry = readdir(dir)) {
		const char *name = entry->d_name;
		if (name[0] == '.')
			continue;

		pid_t pid = atoi(name);
		if (!pid)
			continue;

		char mapPath[100];
		sprintf(mapPath, "/proc/%d/maps", pid);

		std::ifstream file(mapPath);
		if (hasLibMutter(file))
		{
			result.push_back(pid);
		}
	}
	closedir(dir);

	return result;
}

int main()
{
	auto pids = searchProcess();

	for (auto pid : pids)
	{
		printf("%d\n", pid);
		if (ptraceAttach(pid) < 0)
			continue;

		ptraceDetach(pid);
	}

	return 0;
}

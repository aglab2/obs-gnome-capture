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
#include <sys/syscall.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <stdint.h>
#include <inttypes.h>

#include <fstream>
#include <string_view>
#include <vector>

#include "elf.h"

// MARK: logging
#if 1
#define log(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define log() do{}while(0)
#endif

// MARK: ptrace

static int ptraceAttach(pid_t pid)
{
	if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) < 0)
    {
		perror("ptraceAttach: PTRACE_ATTACH failed");
		return -1;
	}

	int status;
	if (waitpid(pid, &status, 0) == -1)
    {
		log("ptraceAttach: waitpid failed?\n");
		return -1;
	}

	if (!WIFSTOPPED(status))
    {
		log("ptraceAttach: process %d did not stop as expected\n", pid);
		return -1;
	}

    return 0;
}

static void ptraceDetach(pid_t pid)
{
	ptrace(PTRACE_DETACH, pid, NULL, NULL);
}

static int processStop(pid_t pid)
{
    return kill(pid, SIGSTOP);
}

static void processCont(pid_t pid)
{
	kill(pid, SIGCONT);
}

struct Ptraced
{
public:
	Ptraced(pid_t pid)
	{
		if (0 != processStop(pid)) return;
		pid_ = pid;

        char line[128];
        sprintf(line, "/proc/%d/task", pid_);
        if (DIR* dir = opendir(line))
        {
            while (auto entry = readdir(dir))
            {
                pid_t tid = atoi(entry->d_name);
                if (!tid)
                    continue;

                threads_.push_back(tid);
            }

            closedir(dir);
        }

        for (auto tid : threads_)
        {
            log("Attaching to %d\n", tid);
            ptraceAttach(tid);
        }
	}
	~Ptraced()
	{
		if (!pid_) return;

        for (auto tid : threads_)
        {
    		ptraceDetach(tid);
        }

		processCont(pid_);
	}

	explicit operator bool() { return 0 != pid_; } 

private:
	pid_t pid_;
	std::vector<pid_t> threads_;
};

// MARK: alloc memory via ptrace

static void* ptraceAllocateMemory(pid_t pid, uint64_t syscallAddr, size_t size)
{
	struct user_regs_struct orig, work;
	if (ptrace(PTRACE_GETREGS, pid, NULL, &orig) < 0)
		return NULL;

	memcpy(&work, &orig, sizeof(work));
	work.rax  = __NR_mmap;
	work.rdi  = (uint64_t)NULL;
	work.rsi  = size;
	work.rdx  = PROT_READ | PROT_EXEC;
	work.r10  = MAP_PRIVATE | MAP_ANONYMOUS;
	work.r8   = -1;
	work.r9   = 0;

	work.rip = syscallAddr;

	if (ptrace(PTRACE_SETREGS, pid, NULL, &work) < 0)
		return NULL;

	if (ptrace(PTRACE_SINGLESTEP, pid, NULL, NULL) < 0)
		return NULL;

	int status;
	if (waitpid(pid, &status, 0) == -1)
		return NULL;

	if (ptrace(PTRACE_GETREGS, pid, NULL, &work) < 0)
		return NULL;

	uint64_t result = work.rax;

	work.rip = orig.rip;
	work.rax = orig.rax;
	work.rdi = orig.rdi;
	work.rsi = orig.rsi;
	work.rdx = orig.rdx;
	work.r10 = orig.r10;
	work.r9  = orig.r9;
	work.r8  = orig.r8;

	if (ptrace(PTRACE_SETREGS, pid, NULL, &work) < 0)
		return NULL;

	// Check for error: mmap returns >= -4096 on failure
	if (result >= (uint64_t)(long)(-4095)) {
		log("  mmap returned error: %p %ld\n", (void*)result, (long) result);
		return NULL;
	}

	log("  Allocated: %p (size = %zu bytes)\n", (void*)result, size);
	return (void*)result;
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

// Write a block of bytes to the target process via ptrace POKETEXT
static bool ptrace_write_bytes(pid_t pid, void *dest, const uint8_t *src, size_t len)
{
	size_t offset = 0;
	while (offset < len) {
		long val = 0;
		size_t copy = sizeof(val);
		if (offset + copy > len)
			copy = len - offset;

		memset(&val, 0, sizeof(val));
		memcpy(&val, src + offset, copy);

		if (ptrace(PTRACE_POKETEXT, pid, (void *)((char *)dest + offset), (void *)&val) < 0) {
			perror("ptrace_write_bytes: POKETEXT failed");
			return false;
		}
		offset += copy;
	}
	return true;
}

// MARK: procfs parser

struct ProcfsMapEntry
{
    uint64_t start;
    uint64_t end;
    uint64_t offset;
    char perms[4];
    std::string pathname;
};

struct ProcfsMapIter
{
public:
    ProcfsMapIter(pid_t pid, std::string filter);
    struct ProcfsMapEntry* next();

    explicit operator bool() const { return file_.is_open(); }

private:
    std::ifstream file_;
    std::string filter_;
    ProcfsMapEntry entry_;
    std::string line_;
};

ProcfsMapIter::ProcfsMapIter(pid_t pid, std::string filter)
: filter_(std::move(filter))
{
    char line[100];
    sprintf(line, "/proc/%d/maps", pid);

    file_.open(line, std::ios::in | std::ios::binary);
    if (!file_.is_open())
        return;
}

static inline const char* sstrchr(const std::string& line, char sym)
{
    auto loc = line.find(sym);
    if (loc == std::string::npos)
    {
        return NULL;
    }
    else
    {
        return line.data() + loc;
    }
}

struct ProcfsMapEntry* ProcfsMapIter::next()
{
    while (std::getline(file_, line_))
    {
        if (std::string::npos == line_.find(filter_))
            continue;

        const char* dash = sstrchr(line_, '-');
        if (!dash)
            continue;
        const char* space = sstrchr(line_, ' ');
        if (!space)
            continue;

        const char* perms = space + 1;
        const char* offsetStart = perms + 5;
        const char* offsetEnd = strchr(offsetStart, ' ');
        if (!offsetEnd)
            continue;

        char tmp[32] = {0};

        strncpy(tmp, line_.data(), dash - line_.data());
        tmp[dash - line_.data()] = '\0';
        entry_.start = strtoull(tmp, NULL, 16);

        strncpy(tmp, dash + 1, space - dash - 1);
        tmp[space - dash - 1] = '\0';
        entry_.end = strtoull(tmp, NULL, 16);

        strncpy(tmp, offsetStart, offsetEnd - offsetStart);
        tmp[offsetEnd - offsetStart] = '\0';
        entry_.offset = strtoull(tmp, NULL, 16);

        memcpy(entry_.perms, perms, 4);

        entry_.pathname = strchr(offsetEnd, '/');

        return &entry_;
    }

    return NULL;
}

// MARK: unix file

struct UnixFile
{
    UnixFile() : UnixFile(-1) {}
    UnixFile(int fd);
    ~UnixFile();

    UnixFile& operator=(const UnixFile&) = delete;
    UnixFile(const UnixFile&) = delete;

    UnixFile& operator=(UnixFile&&);
    UnixFile(UnixFile&&);

    bool pread(size_t sz, off_t off, void* buf);
    explicit operator bool() { return fd_ != -1; }

private:
    int fd_;
};

UnixFile::UnixFile(int fd) : fd_(fd)
{}

UnixFile::~UnixFile()
{
    if (fd_ != -1) close(fd_);
}

UnixFile& UnixFile::operator=(UnixFile&& o)
{
    if (fd_ != -1) close(fd_);
    fd_ = o.fd_;
    o.fd_ = -1;
    return *this;
}

UnixFile::UnixFile(UnixFile&& o) : fd_(o.fd_)
{
    o.fd_ = -1;
}

bool UnixFile::pread(size_t sz, off_t off, void* buf)
{
    ssize_t ret = ::pread(fd_, buf, sz, off);
    return ret == (ssize_t)sz;
}

// MARK: recon

struct Section
{
    uint64_t start;
    uint64_t end;
    uint64_t offset;
    std::vector<uint8_t> data;
};

static struct Section* sectionsFind(std::vector<Section>& sections, uint64_t offset, uint64_t size)
{
    uint64_t end = offset + size;
    for (auto& s : sections)
    {
        uint64_t section_start = s.offset;
        uint64_t section_end   = s.offset + (s.end - s.start);
        if (section_start <= offset && end <= section_end)
        {
            return &s;
        }
    }

    return NULL;
}

static const char* safeStr(const std::vector<char>& buf, size_t offset, size_t limit)
{
    if (offset >= buf.size())
        return NULL;

    if (limit > buf.size())
    {
        for (size_t i = offset; i < buf.size(); i++)
        {
            if (buf[i] == '\0')
                return buf.data() + offset;
        }

        return NULL;
    }

    return buf.data() + offset;
}

static uint64_t findFunction(int pid, UnixFile& ram, const char* library, const char* symname)
{
    std::string libPath;
    uint64_t elfloc = 0;

    Section xloc{};
    std::vector<Section> sections;
    uint64_t ret = 0;

    {
        ProcfsMapIter iter{ pid, library };
        if (!iter)
        {
            return ret;
        }

        while (struct ProcfsMapEntry* entry = iter.next())
        {
            if (!entry->pathname.size())
            {
                log("Found: %" PRIx64 " %" PRIx64 "-%" PRIx64 " %.4s (no pathname?)\n", entry->offset, entry->start, entry->end, entry->perms);
                continue;
            }

            log("Found: %" PRIx64 " %" PRIx64 "-%" PRIx64 " %.4s\n", entry->offset, entry->start, entry->end, entry->perms);
            if (0 == entry->offset)
            {
                elfloc = entry->start;
                libPath = entry->pathname;
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
                sections.push_back({ entry->start, entry->end, entry->offset });
            }
        }
    }

    if (!elfloc || !xloc.start || !xloc.end)
    {
        log("Failed to find %s in process %d\n", library, pid);
        return ret;
    }

    std::vector<Elf64_Shdr> shdrs;

    Elf64_Sym* sym_table = NULL;
    struct AutoFree{
        AutoFree(Elf64_Sym*& p) : ptr(p) {}
        ~AutoFree(){ if (ptr) free(ptr); }

        Elf64_Sym*& ptr;
    } _af{ sym_table };

    std::vector<char> str_table;

    // Start reading ELF
    Elf64_Ehdr ehdr;
    {
        if (!ram.pread(sizeof(ehdr), elfloc, &ehdr))
        {
            log("Failed to read ELF header from %s in process %d\n", library, pid);
            return ret;
        }

        if (ehdr.e_ident[0] != 0x7f || ehdr.e_ident[1] != 'E' || ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F')
        {
            log("Failed to read ELF header from %s in process %d\n", library, pid);
            return ret;
        }
    }

    // shdrs are not actually mapped in memory so we have to open the file and read them from disk
    shdrs.resize(ehdr.e_shnum);
    {
        UnixFile lib = open(libPath.c_str(), O_RDONLY);
        if (!lib)
        {
            log("Failed to open %s at '%s'\n", library, libPath.c_str());
            return ret;
        }

        if (!lib.pread(ehdr.e_shnum * sizeof(Elf64_Shdr), ehdr.e_shoff, shdrs.data()))
        {
            log("Failed to read section headers from %s\n", library);
            return ret;
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
        log("Failed to find dynsym or strtab in %s\n", library);
        return ret;
    }

    // Load in from RAM the dynsym and strtab sections so we can iterate over them and find the symbols we want
    struct Section* dynsymSec = sectionsFind(sections, dynsym->sh_offset, dynsym->sh_size);
    struct Section* strtabSec = sectionsFind(sections, strtab->sh_offset, strtab->sh_size);
    if (!dynsymSec || !strtabSec)
    {
        log("Failed to find dynsym or strtab in mapped sections\n");
        return ret;
    }

    sym_table = (Elf64_Sym*) malloc(dynsym->sh_size);
    if (!ram.pread(dynsym->sh_size, dynsymSec->start + (dynsym->sh_offset - dynsymSec->offset), sym_table))
    {
        log("Failed to read dynsym from process %d\n", pid);
        return ret;
    }

    str_table.resize(strtab->sh_size);
    if (!ram.pread(strtab->sh_size, strtabSec->start + (strtab->sh_offset - strtabSec->offset), str_table.data()))
    {
        log("Failed to read strtab from process %d\n", pid);
        return ret;
    }

    size_t total_symbols = dynsym->sh_size / sizeof(Elf64_Sym);
    for (size_t i = 0; i < total_symbols; i++) {
        if (sym_table[i].st_name == 0)
            continue;

        const char *sym_name = safeStr(str_table, sym_table[i].st_name, 16);
        if (!sym_name)
            continue;

        if (strcmp(sym_name, symname) == 0)
        {
            ret = xloc.start + sym_table[i].st_value - xloc.offset;
            if (ret > xloc.end)
            {
                log("Found %s outside of %s mapping\n", symname, library);
                ret = 0;
            }
        }
    }

    if (!ret) {
		log("Fail\n");
        return 0;
	}

    return ret;
}

static uint64_t findSyscallInLibc(UnixFile& ram, int pid)
{
	uint64_t ret = findFunction(pid, ram, "libc.so.6", "syscall");
	if (!ret)
		return 0;

    {
        uint8_t code[64];
        ram.pread(sizeof(code), ret, code);

        // search for 'syscall'
        const uint8_t sc[] = { 0x0f, 0x05 };

        uint8_t* at = (uint8_t*)memmem(code, sizeof(code), sc, sizeof(sc));
        if (!at)
            return 0;
        
        ret += at - code;
    }

    return ret;
}

static uint64_t findListWindowsInMutter(UnixFile& ram, int pid)
{
	return findFunction(pid, ram, "libmutter-18.so", "meta_display_list_windows");
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

        ProcfsMapIter iter{pid, "libmutter"};
        if (iter.next())
            result.push_back(pid);
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
		UnixFile ram;
		{
			char path[64] = {0};
			snprintf(path, sizeof(path), "/proc/%d/mem", pid);
			ram = open(path, O_RDONLY);
			if (!ram)
			{
				log("Failed to open /proc/%d/mem\n", pid);
				continue;
			}
		}

		Ptraced ptraced(pid);
		if (!ptraced)
			continue;

        // Find syscall instruction in libc
        uint64_t syscallAddr = findSyscallInLibc(ram, pid);
        if (!syscallAddr) {
            log("  Failed to find syscall in libc\n");
            continue;
        }
        printf("  Syscall instruction at: %p\n", (void*)syscallAddr);

		uint64_t listWindowAddr = findListWindowsInMutter(ram, pid);
		if (!listWindowAddr) {
			log("   Failed to find list windows in mutter\n");
			continue;
		}
		printf("  Mutter list windows at: %p\n", (void*) listWindowAddr);

        // void* result = ptraceAllocateMemory(pid, syscallAddr, 4096);
        // printf("   %p created\n", result);
	}

	return 0;
}

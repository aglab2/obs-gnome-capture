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

#include <array>
#include <fstream>
#include <string_view>
#include <vector>

#include "elf.h"

// MARK: logging
#if 1
#define log(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define log() \
	do {  \
	} while (0)
#endif

// MARK: ptrace

static int ptraceAttach(pid_t pid)
{
	if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) < 0) {
		perror("ptraceAttach: PTRACE_ATTACH failed");
		return -1;
	}

	int status;
	if (waitpid(pid, &status, 0) == -1) {
		log("ptraceAttach: waitpid failed?\n");
		return -1;
	}

	if (!WIFSTOPPED(status)) {
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

struct Ptraced {
public:
	Ptraced(pid_t pid)
	{
		// if (0 != processStop(pid)) return;
		if (0 != ptraceAttach(pid)) {
			// processCont(pid);
			return;
		}
		pid_ = pid;
	}
	~Ptraced()
	{
		if (!pid_)
			return;

		ptraceDetach(pid_);
		// processCont(pid_);
	}

	explicit operator bool() { return 0 != pid_; }

	void moveThreadsOutOfRedzone(uint64_t start, uint64_t end);

private:
	pid_t pid_ = 0;
};

void Ptraced::moveThreadsOutOfRedzone(uint64_t start, uint64_t end)
{
	while (1) {
		struct user_regs_struct orig;
		if (ptrace(PTRACE_GETREGS, pid_, NULL, &orig) < 0)
			break;

		if (start <= orig.rip && orig.rip <= end) {
			log("Advancing thread %d to get out of red zone\n", pid_);
			if (ptrace(PTRACE_SINGLESTEP, pid_, NULL, NULL) < 0)
				break;

			int status;
			if (waitpid(pid_, &status, 0) == -1)
				break;

			// continue checking -> to while(1)
		} else {
			break;
		}
	}
}

// MARK: alloc memory via ptrace

enum MemoryType {
	MT_RW,
	MT_RX,
};

static uint64_t ptraceAllocateMemory(pid_t pid, uint64_t syscallAddr, size_t size, MemoryType mt)
{
	struct user_regs_struct orig, work;
	if (ptrace(PTRACE_GETREGS, pid, NULL, &orig) < 0)
		return 0;

	memcpy(&work, &orig, sizeof(work));
	work.rax = __NR_mmap;
	work.rdi = (uint64_t)NULL;
	work.rsi = size;
	work.rdx = mt == MT_RW ? (PROT_READ | PROT_WRITE) : (PROT_READ | PROT_EXEC);
	work.r10 = MAP_PRIVATE | MAP_ANONYMOUS;
	work.r8 = -1;
	work.r9 = 0;

	work.rip = syscallAddr;

	if (ptrace(PTRACE_SETREGS, pid, NULL, &work) < 0)
		return 0;

	if (ptrace(PTRACE_SINGLESTEP, pid, NULL, NULL) < 0)
		return 0;

	int status;
	if (waitpid(pid, &status, 0) == -1)
		return 0;

	if (ptrace(PTRACE_GETREGS, pid, NULL, &work) < 0)
		return 0;

	uint64_t result = work.rax;
	log("%lu\n", result);

	work.rip = orig.rip;
	work.rax = orig.rax;
	work.rdi = orig.rdi;
	work.rsi = orig.rsi;
	work.rdx = orig.rdx;
	work.r10 = orig.r10;
	work.r9 = orig.r9;
	work.r8 = orig.r8;

	if (ptrace(PTRACE_SETREGS, pid, NULL, &work) < 0)
		return 0;

	// Check for error: mmap returns >= -4096 on failure
	if (result >= (uint64_t)(long)(-4095)) {
		log("  mmap returned error: %p %ld\n", (void *)result, (long)result);
		return 0;
	}

	log("  Allocated: %p (size = %zu bytes)\n", (void *)result, size);
	return result;
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

static bool ptraceWriteBytes(pid_t pid, uint64_t dest, const void *src, size_t len, bool rx)
{
	if (len % sizeof(long) != 0) {
		log("Size %zd must be aligned!\n", len);
		return false;
	}

	size_t offset = 0;
	while (offset < len) {
		size_t copy = sizeof(long);
		if (ptrace(rx ? PTRACE_POKETEXT : PTRACE_POKEDATA, pid, (char *)dest + offset,
			   *(long *)((const uint8_t *)src + offset)) < 0) {
			log("ptrace_write_bytes: POKETEXT failed\n");
			return false;
		}
		offset += copy;
	}
	return true;
}

// MARK: procfs parser

struct ProcfsMapEntry {
	uint64_t start;
	uint64_t end;
	uint64_t offset;
	char perms[4];
	std::string pathname;
};

struct ProcfsMapIter {
public:
	ProcfsMapIter(pid_t pid, std::string filter);
	struct ProcfsMapEntry *next();

	explicit operator bool() const { return file_.is_open(); }

private:
	std::ifstream file_;
	std::string prefix_;
	bool hasDigitsCheck_ = false;
	std::string suffix_;
	ProcfsMapEntry entry_;
	std::string line_;
};

ProcfsMapIter::ProcfsMapIter(pid_t pid, std::string filter)
{
	size_t digitsLoc = filter.find("DIGITS");
	if (std::string::npos != digitsLoc) {
		prefix_ = filter.substr(0, digitsLoc);
		hasDigitsCheck_ = true;
		suffix_ = filter.substr(digitsLoc + sizeof("DIGITS") - 1);
	} else {
		prefix_ = filter;
	}

	char line[100];
	sprintf(line, "/proc/%d/maps", pid);

	file_.open(line, std::ios::in | std::ios::binary);
	if (!file_.is_open())
		return;
}

static inline const char *sstrchr(const std::string &line, char sym)
{
	auto loc = line.find(sym);
	if (loc == std::string::npos) {
		return NULL;
	} else {
		return line.data() + loc;
	}
}

struct ProcfsMapEntry *ProcfsMapIter::next()
{
	while (std::getline(file_, line_)) {
		size_t loc = line_.find(prefix_);
		if (std::string::npos == loc)
			continue;

		if (hasDigitsCheck_) {
			loc += prefix_.length();
			while ('0' <= line_[loc] && line_[loc] <= '9')
				loc++;

			if (0 != memcmp(line_.data() + loc, suffix_.data(), suffix_.length())) {
				continue;
			}
		}

		const char *dash = sstrchr(line_, '-');
		if (!dash)
			continue;
		const char *space = sstrchr(line_, ' ');
		if (!space)
			continue;

		const char *perms = space + 1;
		const char *offsetStart = perms + 5;
		const char *offsetEnd = strchr(offsetStart, ' ');
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

struct UnixFile {
	UnixFile() : UnixFile(-1) {}
	UnixFile(int fd);
	~UnixFile();

	UnixFile &operator=(const UnixFile &) = delete;
	UnixFile(const UnixFile &) = delete;

	UnixFile &operator=(UnixFile &&);
	UnixFile(UnixFile &&);

	bool pread(size_t sz, off_t off, void *buf);
	explicit operator bool() { return fd_ != -1; }

private:
	int fd_;
};

UnixFile::UnixFile(int fd) : fd_(fd) {}

UnixFile::~UnixFile()
{
	if (fd_ != -1)
		close(fd_);
}

UnixFile &UnixFile::operator=(UnixFile &&o)
{
	if (fd_ != -1)
		close(fd_);
	fd_ = o.fd_;
	o.fd_ = -1;
	return *this;
}

UnixFile::UnixFile(UnixFile &&o) : fd_(o.fd_)
{
	o.fd_ = -1;
}

bool UnixFile::pread(size_t sz, off_t off, void *buf)
{
	ssize_t ret = ::pread(fd_, buf, sz, off);
	return ret == (ssize_t)sz;
}

// MARK: recon

struct Section {
	uint64_t start;
	uint64_t end;
	uint64_t offset;
	std::vector<uint8_t> data;
};

static struct Section *sectionsFind(std::vector<Section> &sections, uint64_t offset, uint64_t size)
{
	uint64_t end = offset + size;
	for (auto &s : sections) {
		uint64_t section_start = s.offset;
		uint64_t section_end = s.offset + (s.end - s.start);
		if (section_start <= offset && end <= section_end) {
			return &s;
		}
	}

	return NULL;
}

static const char *safeStr(const std::vector<char> &buf, size_t offset, size_t limit)
{
	if (offset >= buf.size())
		return NULL;

	if (limit > buf.size()) {
		for (size_t i = offset; i < buf.size(); i++) {
			if (buf[i] == '\0')
				return buf.data() + offset;
		}

		return NULL;
	}

	return buf.data() + offset;
}

static std::vector<uint64_t> findFunction(int pid, UnixFile &ram, const char *library,
					  std::vector<const char *> symnames)
{
	std::vector<uint64_t> result;
	std::string libPath;
	uint64_t elfloc = 0;

	Section xloc{};
	std::vector<Section> sections;

	{
		ProcfsMapIter iter{pid, library};
		if (!iter) {
			return result;
		}

		while (struct ProcfsMapEntry *entry = iter.next()) {
			if (!entry->pathname.size()) {
				log("Found: %" PRIx64 " %" PRIx64 "-%" PRIx64 " %.4s (no pathname?)\n", entry->offset,
				    entry->start, entry->end, entry->perms);
				continue;
			}

			log("Found: %" PRIx64 " %" PRIx64 "-%" PRIx64 " %.4s\n", entry->offset, entry->start,
			    entry->end, entry->perms);
			if (0 == entry->offset) {
				elfloc = entry->start;
				libPath = entry->pathname;
			}

			if (entry->perms[2] == 'x') {
				xloc.start = entry->start;
				xloc.end = entry->end;
				xloc.offset = entry->offset;
			}
			if (entry->perms[0] == 'r' && entry->perms[1] != 'w' && entry->perms[2] != 'x') {
				sections.push_back({entry->start, entry->end, entry->offset});
			}
		}
	}

	if (!elfloc || !xloc.start || !xloc.end) {
		log("Failed to find %s in process %d\n", library, pid);
		return result;
	}

	std::vector<Elf64_Shdr> shdrs;

	Elf64_Sym *sym_table = NULL;
	struct AutoFree {
		AutoFree(Elf64_Sym *&p) : ptr(p) {}
		~AutoFree()
		{
			if (ptr)
				free(ptr);
		}

		Elf64_Sym *&ptr;
	} _af{sym_table};

	std::vector<char> str_table;

	// Start reading ELF
	Elf64_Ehdr ehdr;
	{
		if (!ram.pread(sizeof(ehdr), elfloc, &ehdr)) {
			log("Failed to read ELF header from %s in process %d\n", library, pid);
			return result;
		}

		if (ehdr.e_ident[0] != 0x7f || ehdr.e_ident[1] != 'E' || ehdr.e_ident[2] != 'L' ||
		    ehdr.e_ident[3] != 'F') {
			log("Failed to read ELF header from %s in process %d\n", library, pid);
			return result;
		}
	}

	// shdrs are not actually mapped in memory so we have to open the file and read them from disk
	shdrs.resize(ehdr.e_shnum);
	{
		UnixFile lib = open(libPath.c_str(), O_RDONLY);
		if (!lib) {
			log("Failed to open %s at '%s'\n", library, libPath.c_str());
			return result;
		}

		if (!lib.pread(ehdr.e_shnum * sizeof(Elf64_Shdr), ehdr.e_shoff, shdrs.data())) {
			log("Failed to read section headers from %s\n", library);
			return result;
		}
	}

	// as raw pointers inside 'shdrs'
	Elf64_Shdr *dynsym = NULL;
	Elf64_Shdr *strtab = NULL;
	for (int i = 0; i < ehdr.e_shnum; i++) {
		Elf64_Shdr *shdr = &shdrs[i];
		if (!(shdr->sh_flags & SHF_ALLOC)) {
			continue;
		}

		// TODO: uncba to check the names...
		if (shdr->sh_type == SHT_DYNSYM)
			dynsym = shdr;
		else if (shdr->sh_type == SHT_STRTAB)
			strtab = shdr;
	}

	if (!dynsym || !strtab) {
		log("Failed to find dynsym or strtab in %s\n", library);
		return result;
	}

	// Load in from RAM the dynsym and strtab sections so we can iterate over them and find the symbols we want
	struct Section *dynsymSec = sectionsFind(sections, dynsym->sh_offset, dynsym->sh_size);
	struct Section *strtabSec = sectionsFind(sections, strtab->sh_offset, strtab->sh_size);
	if (!dynsymSec || !strtabSec) {
		log("Failed to find dynsym or strtab in mapped sections\n");
		return result;
	}

	sym_table = (Elf64_Sym *)malloc(dynsym->sh_size);
	if (!ram.pread(dynsym->sh_size, dynsymSec->start + (dynsym->sh_offset - dynsymSec->offset), sym_table)) {
		log("Failed to read dynsym from process %d\n", pid);
		return result;
	}

	str_table.resize(strtab->sh_size);
	if (!ram.pread(strtab->sh_size, strtabSec->start + (strtab->sh_offset - strtabSec->offset), str_table.data())) {
		log("Failed to read strtab from process %d\n", pid);
		return result;
	}

	result.resize(symnames.size());
	size_t total_symbols = dynsym->sh_size / sizeof(Elf64_Sym);
	for (size_t i = 0; i < total_symbols; i++) {
		if (sym_table[i].st_name == 0)
			continue;

		const char *sym_name = safeStr(str_table, sym_table[i].st_name, 16);
		if (!sym_name)
			continue;

		for (size_t j = 0; j < symnames.size(); j++) {
			const char *symname = symnames[j];
			if (strcmp(sym_name, symname) == 0) {
				uint64_t ret = xloc.start + sym_table[i].st_value - xloc.offset;
				if (ret > xloc.end) {
					log("Found %s outside of %s mapping\n", symname, library);
				} else {
					result[j] = ret;
				}
			}
		}
	}

	return result;
}

enum Instruction {
	I_UNKNOWN,
	I_MOV,
	I_LEA,
	I_SUB,
	I_CALL,
};

using CodePiece = std::array<uint8_t, 64>;
using DecompilePiece = std::array<uint8_t, 256>;

struct DecompileResult {
	int shift;
	int count;
	uint64_t callingTo;
	DecompilePiece code;
};

static CodePiece toCodePiece(const DecompilePiece &dp)
{
	CodePiece cp;
	memcpy(cp.data(), dp.data(), cp.size());
	return cp;
}

// MARK: Decompiling
static void decompile(uint64_t fn, int amount, struct DecompileResult &result)
{
	const uint8_t *code = result.code.data();
	const uint8_t *p = code;
	const uint8_t *end = p + result.code.size();

	if (code[0] == 0xf3 && code[1] == 0x0f && code[2] == 0x1e && code[3] == 0xfa) {
		result.shift = 4;
		p += 4;
	} else {
		result.shift = 0;
	}

#define consume(n)           \
	do {                 \
		p += n;      \
		amount -= n; \
		fn += n;     \
	} while (0)

	while (p < end) {
		if (amount < 0) {
			if (!result.count) {
				result.count = (int)(p - code) - result.shift;
			}
		} else {
			log("left %d\n", amount);
		}

		uint8_t maybeSegEx = *p;
		if (maybeSegEx == 0x64) {
			log("seg: %02x\n", maybeSegEx);
			consume(1);
		}

		uint8_t maybeRex = *p;
		if (maybeRex >= 0x40 && maybeRex <= 0x4F) {
			log("Rex: %02x\n", maybeRex);
			consume(1);
		}

		bool want_modrm = false;
		bool want_imm8 = false;
		bool want_imm32 = false;
		bool want_group = false;

		enum Instruction instruction = I_UNKNOWN;

		uint8_t opcode = *p;
		consume(1);
		if (0x0f == opcode) {
			log("0f\n");
			opcode = *p;
			consume(1);
			if (0xba == opcode) {
				log("btr\n");
				want_modrm = true;
				want_imm8 = true;
			} else if (0x84 == opcode) {
				log("je\n");
				want_imm32 = true;
			} else {
				log("unknown opcode: 0f %02x???\n", opcode);
				return;
			}
		} else if (0x50 <= opcode && opcode <= 0x57) {
			log("push r%d\n", opcode - 0x50);
		} else if (0x58 <= opcode && opcode <= 0x5F) {
			log("pop r%d\n", opcode - 0x58);
		} else if (0x72 == opcode) {
			log("jb\n");
			want_imm8 = true;
		} else if (0x87 == opcode) {
			log("xchg\n");
			want_modrm = true;
		} else if (0x89 == opcode) {
			log("mov\n");
			instruction = I_MOV;
			want_modrm = true;
		} else if (0x8b == opcode) {
			log("mov\n");
			instruction = I_MOV;
			want_modrm = true;
		} else if (0x8D == opcode) {
			log("lea\n");
			instruction = I_LEA;
			want_modrm = true;
		} else if (0x8e == opcode) {
			log("movseg\n");
			want_modrm = true;
		} else if (0x9C == opcode) {
			log("pushf\n");
		} else if (0xcf == opcode) {
			log("iret\n");
			break;
		} else if (0xff == opcode) {
			log("group\n");
			want_modrm = true;
			want_group = true;
		} else if (0x83 == opcode) {
			log("sub\n");
			instruction = I_SUB;
			want_modrm = true;
			want_imm8 = true;
		} else if (0x85 == opcode) {
			log("test\n");
			want_modrm = true;
		} else if (0xe8 == opcode) {
			log("call\n");
			instruction = I_CALL;
		} else if (0x31 == opcode) {
			log("xor\n");
			want_modrm = true;
		} else {
			log("unknown opcode: %02x???\n", opcode);
			return;
		}

		if (want_modrm) {
			uint8_t modrm = *p;
			log("\tmodrm: %02x\n", modrm);
			consume(1);

			uint8_t mod = (modrm & 0xC0) >> 6;
			uint8_t reg = (modrm & 0x38) >> 3;
			uint8_t rm = (modrm & 0x07);

			log("\tmod: %02x, reg: %02x, rm: %02x\n", mod, reg, rm);

			if (want_group) {
				if (mod != 0 || reg != 2 || rm != 5) {
					log("unkn group\n");
				} else {
					instruction = I_CALL;
				}
			} else {
				if (mod != 3 && rm == 4) {
					log("\tsib: %02x\n", *p);
					consume(5);
				}

				switch (mod) {
				case 1:
					log("\tdisp8: %02x\n", *p);
					consume(1);
					break;
				case 2:
					log("\tdisp32: %08x\n", *(unsigned *)p);
					consume(4);
					break;
				case 3:
					log("\treg: %02x\n", rm);
					break;
				default:
					log("unkn\n");
					break;
				}
			}
		}

		if (want_imm8) {
			log("\timm8: %02x\n", *p);
			consume(1);
		}

		if (want_imm32) {
			log("\timm32: %x\n", *(int *)p);
			consume(4);
		}

		if (I_CALL == instruction) {
			int off = *(int *)p;
			log("\t%08x\n", off);
			consume(4);
			result.callingTo = off + fn;
			break;
		}
	}
}

static struct DecompileResult decompile(UnixFile &ram, uint64_t fn, int amount)
{
	struct DecompileResult result{};

	if (amount > 64)
		return result;

	uint8_t *code = result.code.data();
	if (!ram.pread(result.code.size(), fn, code))
		return result;

	decompile(fn, amount, result);

	return result;
}

// MARK: Analysis

struct LibcRecon {
	uint64_t syscall;
	uint64_t dlopen;
	uint64_t dlsym;
};

static struct LibcRecon reconLibc(UnixFile &ram, int pid)
{
	struct LibcRecon result{};

	auto ret = findFunction(pid, ram, "libc.so.6", {"syscall", "dlopen", "dlsym"});
	if (ret.size() != 3) {
		log("Incorrect return size\n");
		return {};
	}

	result.syscall = ret[0];
	result.dlopen = ret[1];
	result.dlsym = ret[2];

	if (result.syscall) {
		uint8_t code[64];
		if (!ram.pread(sizeof(code), result.syscall, code))
			return {};

		// search for 'syscall'
		const uint8_t sc[] = {0x0f, 0x05};

		uint8_t *at = (uint8_t *)memmem(code, sizeof(code), sc, sizeof(sc));
		if (!at)
			return {};

		result.syscall += at - code;
	}

	return result;
}

static uint64_t findListWindowsInMutter(UnixFile &ram, int pid)
{
	auto ret = findFunction(pid, ram, "libmutter-DIGITS.so", {"meta_display_list_windows"});
	if (ret.size() != 1)
		return 0;

	return ret[0];
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

static uint64_t findIsraCopy(UnixFile &ram, uint64_t fn, uint64_t expectedCall, int amount, DecompileResult &res)
{
	std::vector<uint8_t> scanBuf;
	scanBuf.resize(4096);
	if (!ram.pread(scanBuf.size(), fn, scanBuf.data()))
		return 0;

	uint8_t *buf = (uint8_t *)memmem(scanBuf.data() + 1, scanBuf.size() - 1, scanBuf.data(), 8);
	if (!buf)
		return 0;

	size_t off = buf - scanBuf.data();
	res = {};
	memcpy(res.code.data(), buf, std::min(res.code.size(), 4096 - off));

	decompile(fn + off, amount, res);

	if (res.callingTo != expectedCall)
		return 0;

	return fn + off;
}

// MARK: Detours
static void writeJmpViaRAX(uint8_t *code, uint64_t off)
{
	code[0] = 0x48;
	code[1] = 0xb8;
	*(uint64_t *)&code[2] = off;
	code[10] = 0x3e;
	code[11] = 0xFF;
	code[12] = 0xE0;
}

static void writeJmpViaRIP(uint8_t *code, uint64_t codeRip, uint64_t targetRip)
{
	code[0] = 0x3e;
	code[1] = 0xff;
	code[2] = 0x25;
	*(uint32_t *)&code[3] = (uint32_t)(targetRip - (codeRip + 7));
}

int main()
{
	auto pids = searchProcess();

	for (auto pid : pids) {
		printf("%d\n", pid);
		UnixFile ram;
		{
			char path[64] = {0};
			snprintf(path, sizeof(path), "/proc/%d/mem", pid);
			ram = open(path, O_RDONLY);
			if (!ram) {
				log("Failed to open /proc/%d/mem\n", pid);
				continue;
			}
		}

		// Prepare and recon info about process
		Ptraced ptraced(pid);
		if (!ptraced)
			continue;

		struct LibcRecon libcRecon = reconLibc(ram, pid);
		if (!libcRecon.syscall) {
			log("  Failed to find syscall in libc\n");
			continue;
		}

		if (!libcRecon.dlopen) {
			log("  Failed to find dlopen in libc\n");
			continue;
		}

		if (!libcRecon.dlsym) {
			log("  Failed to find dlsym in libc\n");
			continue;
		}

		uint64_t syscallAddr = libcRecon.syscall;

		printf("  Syscall instruction at: %p\n", (void *)syscallAddr);

		uint64_t listWindowAddr = findListWindowsInMutter(ram, pid);
		if (!listWindowAddr) {
			log("   Failed to find list windows in mutter\n");
			continue;
		}
		printf("  Mutter list windows at: %p\n", (void *)listWindowAddr);

		struct DecompileResult decResult = decompile(ram, listWindowAddr, 13 /*need at least mov + jmp*/);
		if (decResult.count < 0)
			continue;

		printf("   decompile count=%d shift=%d call=%p\n", decResult.count, decResult.shift,
		       (void *)decResult.callingTo);

		uint64_t rwPage = ptraceAllocateMemory(pid, syscallAddr, 0x1000, MT_RW);
		printf("   %p created\n", (void *)rwPage);
		if (!rwPage)
			continue;

		uint64_t rxPage = ptraceAllocateMemory(pid, syscallAddr, 0x1000, MT_RX);
		printf("   %p created\n", (void *)rxPage);
		if (!rxPage)
			continue;

		// make rx page with hook
		CodePiece patchCodeWithCall = toCodePiece(decResult.code);
		writeJmpViaRIP(patchCodeWithCall.data() + decResult.shift + decResult.count,
			       rxPage + decResult.shift + decResult.count, rxPage + 0xf00);
		if (!ptraceWriteBytes(pid, rxPage + 0x000, patchCodeWithCall.data(), patchCodeWithCall.size(),
				      1 /*rx*/)) {
			log("   Failed to write to rx page\n");
		}

		// 0x100
		static const uint8_t content[128] = {
			0x55, // push rbp
			0x48,
			0x89,
			0xe5, // mov rbp, rsp
			0x48,
			0x8b,
			0x0d,
			0x0d,
			0x0e,
			0x00,
			0x00, // mov rcx, [rip+disp32] <- rwPage at 0xf18
			0x48,
			0x8b,
			0x01, // mov rax, [rcx]
			0x48,
			0x85,
			0xc0, // test rax, rax
			0x0f,
			0x85,
			0x32,
			0x00,
			0x00,
			0x00, // jne <- lea rdx at 0x49

			0x57, // push rdi
			0x56, // push rsi
			0x48,
			0x8d,
			0x3d,
			0xe0,
			0x0c,
			0x00,
			0x00, // lea rdi, [rip+disp32] <- filename at 0xe00
			0x48,
			0xc7,
			0xc6,
			1,
			0,
			0,
			0, // mov rsi,1
			0xff,
			0x15,
			0xdb,
			0x0d,
			0x00,
			0x00, // call [rip+disp32]    <- dlopen at 0xf08
			0x48,
			0x89,
			0xc7, // mov rdi,rax
			0x48,
			0x8d,
			0x35,
			0xc9,
			0x0b,
			0x00,
			0x00, // lea rsi, [rip+disp32] <- symname at 0xd00
			0xff,
			0x15,
			0xd3,
			0x0d,
			0x00,
			0x00, // call [rip+disp32]    <- dlsym at 0xf10
			0x48,
			0x8b,
			0x0d,
			0xd4,
			0x0d,
			0x00,
			0x00, // mov rcx, [rip+disp32] <- rwPage at 0xf18
			0x48,
			0x89,
			0x01, // mov [rcx], rax
			0x5e, // pop rsi
			0x5f, // pop rdi

			// rax jne here
			0x48,
			0x8d,
			0x15,
			0xb0,
			0xfe,
			0xff,
			0xff, // lea rdx, [rip+disp32] <- orig fn at 0
			0xff,
			0xd0, // call rax
			0x5d, // pop rbp
			0xc3, // ret
		};

		if (!ptraceWriteBytes(pid, rxPage + 0x100, content, sizeof(content), 1 /*rx*/)) {
			log("   Failed to write to rx page\n");
		}

		// oftentimes gcc will decide to emit an isra copy of meta_display_list_windows with META_LIST_DEFAULT inlined
		// try to guess that it exists by looking at first few bytes of the function and doing an educated guess...
		DecompileResult decResultIsra;
		uint64_t listWindowAddrIsra = findIsraCopy(ram, listWindowAddr, decResult.callingTo, 13, decResultIsra);

		// 0x200
		CodePiece patchCodeWithCallIsra = toCodePiece(decResultIsra.code);
		writeJmpViaRIP(patchCodeWithCallIsra.data() + decResultIsra.shift + decResultIsra.count,
			       rxPage + 0x200 + decResultIsra.shift + decResultIsra.count, rxPage + 0xf20);
		if (!ptraceWriteBytes(pid, rxPage + 0x200, patchCodeWithCallIsra.data(), patchCodeWithCallIsra.size(),
				      1 /*rx*/)) {
			log("   Failed to write to rx page\n");
		}

		// 0x300
		static const uint8_t contentIsra[128] = {
			0x55, // push rbp
			0x48,
			0x89,
			0xe5, // mov rbp, rsp
			0x48,
			0x8b,
			0x0d,
			0x0d,
			0x0c,
			0x00,
			0x00, // mov rcx, [rip+disp32] <- rwPage at 0xf18
			0x48,
			0x8b,
			0x01, // mov rax, [rcx]
			0x48,
			0x85,
			0xc0, // test rax, rax
			0x0f,
			0x85,
			0x32,
			0x00,
			0x00,
			0x00, // jne <- lea rdx at 0x49

			0x57, // push rdi
			0x56, // push rsi
			0x48,
			0x8d,
			0x3d,
			0xe0,
			0x0a,
			0x00,
			0x00, // lea rdi, [rip+disp32] <- filename at 0xe00
			0x48,
			0xc7,
			0xc6,
			1,
			0,
			0,
			0, // mov rsi,1
			0xff,
			0x15,
			0xdb,
			0x0b,
			0x00,
			0x00, // call [rip+disp32]    <- dlopen at 0xf08
			0x48,
			0x89,
			0xc7, // mov rdi,rax
			0x48,
			0x8d,
			0x35,
			0xc9,
			0x09,
			0x00,
			0x00, // lea rsi, [rip+disp32] <- symname at 0xd00
			0xff,
			0x15,
			0xd3,
			0x0b,
			0x00,
			0x00, // call [rip+disp32]    <- dlsym at 0xf10
			0x48,
			0x8b,
			0x0d,
			0xd4,
			0x0b,
			0x00,
			0x00, // mov rcx, [rip+disp32] <- rwPage at 0xf18
			0x48,
			0x89,
			0x01, // mov [rcx], rax
			0x5e, // pop rsi
			0x5f, // pop rdi

			// rax jne here
			0x48,
			0x8d,
			0x15,
			0xb0,
			0xfe,
			0xff,
			0xff, // lea rdx, [rip+disp32] <- orig fn at 0x200
			0xff,
			0xd0, // call rax
			0x5d, // pop rbp
			0xc3, // ret
		};

		if (!ptraceWriteBytes(pid, rxPage + 0x300, contentIsra, sizeof(contentIsra), 1 /*rx*/)) {
			log("   Failed to write to rx page\n");
		}

		uint64_t supplPointers[] = {
			listWindowAddr + decResult.shift + decResult.count,
			libcRecon.dlopen,
			libcRecon.dlsym,
			rwPage,
			listWindowAddrIsra + decResult.shift + decResult.count,
		};
		if (!ptraceWriteBytes(pid, rxPage + 0xf00, supplPointers, sizeof(supplPointers), 1 /*rx*/)) {
			log("   Failed to write to rx page supplementary pointers\n");
		}

		static const char dlpath[64] = "/usr/local/bin/libmutter_list_windows.so";
		if (!ptraceWriteBytes(pid, rxPage + 0xe00, dlpath, sizeof(dlpath), 1 /*rx*/)) {
			log("   Failed to write to rx page dlpath\n");
		}

		static const char dlsymName[40] = "meta_display_list_window_detour";
		if (!ptraceWriteBytes(pid, rxPage + 0xd00, dlsymName, sizeof(dlsymName), 1 /*rx*/)) {
			log("   Failed to write to rx page dlsymName\n");
		}

		// write detour to rx page
		{
			ptraced.moveThreadsOutOfRedzone(listWindowAddr + decResult.shift,
							listWindowAddr + decResult.shift + decResult.count);
			CodePiece mutterCodeWithHook = toCodePiece(decResult.code);
			writeJmpViaRAX(mutterCodeWithHook.data() + decResult.shift, rxPage + 0x100);
			if (!ptraceWriteBytes(pid, listWindowAddr, mutterCodeWithHook.data(), mutterCodeWithHook.size(),
					      1 /*rx*/)) {
				log("   Failed to write detour\n");
			}
		}

		if (listWindowAddrIsra) {
			ptraced.moveThreadsOutOfRedzone(listWindowAddrIsra + decResultIsra.shift,
							listWindowAddrIsra + decResultIsra.shift + decResultIsra.count);
			CodePiece mutterCodeWithHook = toCodePiece(decResultIsra.code);
			writeJmpViaRAX(mutterCodeWithHook.data() + decResultIsra.shift, rxPage + 0x300);
			if (!ptraceWriteBytes(pid, listWindowAddrIsra, mutterCodeWithHook.data(),
					      mutterCodeWithHook.size(), 1 /*rx*/)) {
				log("   Failed to write detour for isra\n");
			}
		}
	}

	return 0;
}

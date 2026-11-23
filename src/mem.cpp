// hl2mp-nextbot: see mem.h
#include "mem.h"
#include <string.h>
#include <stdlib.h>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace mem
{
bool Module::Load(const char *dllName)
{
	HMODULE h = GetModuleHandleA(dllName);
	if (!h)
		return false;
	base = (uint8_t *)h;
	auto *dos = (IMAGE_DOS_HEADER *)base;
	auto *nt = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
	auto *sec = IMAGE_FIRST_SECTION(nt);
	for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
	{
		char name[9] = {0};
		memcpy(name, sec->Name, 8);
		Section s{base + sec->VirtualAddress, sec->Misc.VirtualSize};
		if (!strcmp(name, ".text"))
			text = s;
		else if (!strcmp(name, ".rdata"))
			rdata = s;
		else if (!strcmp(name, ".data"))
			data = s;
	}
	return text.start && rdata.start && data.start;
}

static bool ParsePattern(const char *p, std::vector<int> &out)
{
	while (*p)
	{
		while (*p == ' ')
			++p;
		if (!*p)
			break;
		if (p[0] == '?')
		{
			out.push_back(-1);
			p += (p[1] == '?') ? 2 : 1;
			continue;
		}
		char hex[3] = {p[0], p[1], 0};
		char *end = nullptr;
		long v = strtol(hex, &end, 16);
		if (end != hex + 2)
			return false;
		out.push_back((int)v);
		p += 2;
	}
	return !out.empty();
}

uint8_t *FindPattern(const Section &sec, const char *pattern, uint8_t *from)
{
	std::vector<int> pat;
	if (!ParsePattern(pattern, pat) || !sec.start)
		return nullptr;
	uint8_t *p = from ? from : sec.start;
	uint8_t *end = sec.start + sec.size - pat.size();
	for (; p <= end; ++p)
	{
		size_t i = 0;
		for (; i < pat.size(); ++i)
			if (pat[i] >= 0 && p[i] != (uint8_t)pat[i])
				break;
		if (i == pat.size())
			return p;
	}
	return nullptr;
}

static uint8_t *FindBytes(const Section &sec, const void *needle, size_t n, uint8_t *from = nullptr)
{
	uint8_t *p = from ? from : sec.start;
	uint8_t *end = sec.start + sec.size - n;
	for (; p <= end; ++p)
		if (!memcmp(p, needle, n))
			return p;
	return nullptr;
}

void **FindVTable(const Module &m, const char *cls)
{
	char name[256];
	_snprintf(name, sizeof(name), ".?AV%s@@", cls);
	size_t n = strlen(name) + 1;
	// The type descriptor (pVFTable, spare, name[]) lives in .data.
	uint8_t *tdName = FindBytes(m.data, name, n);
	if (!tdName)
		return nullptr;
	uint32_t td = (uint32_t)(tdName - 8);
	// Complete object locator in .rdata: signature 0, offset, cdOffset, pTypeDescriptor, pClassDescriptor.
	for (uint8_t *p = FindBytes(m.rdata, &td, 4); p; p = FindBytes(m.rdata, &td, 4, p + 1))
	{
		uint32_t *col = (uint32_t *)(p - 12);
		if (!m.rdata.Contains(col, 20) || col[0] != 0 || col[1] != 0)
			continue;
		uint32_t colAddr = (uint32_t)col;
		uint8_t *ref = FindBytes(m.rdata, &colAddr, 4);
		if (ref)
			return (void **)(ref + 4);
	}
	return nullptr;
}

struct SavedPatch
{
	void *addr;
	std::vector<uint8_t> orig;
};
static std::vector<SavedPatch> s_patches;
static std::vector<void *> s_allocs;

bool Patch(void *addr, const void *bytes, size_t n)
{
	DWORD old;
	if (!VirtualProtect(addr, n, PAGE_EXECUTE_READWRITE, &old))
		return false;
	SavedPatch sp{addr, std::vector<uint8_t>((uint8_t *)addr, (uint8_t *)addr + n)};
	memcpy(addr, bytes, n);
	DWORD tmp;
	VirtualProtect(addr, n, old, &tmp);
	FlushInstructionCache(GetCurrentProcess(), addr, n);
	s_patches.push_back(sp);
	return true;
}

void RestoreAll()
{
	for (auto it = s_patches.rbegin(); it != s_patches.rend(); ++it)
	{
		DWORD old, tmp;
		if (VirtualProtect(it->addr, it->orig.size(), PAGE_EXECUTE_READWRITE, &old))
		{
			memcpy(it->addr, it->orig.data(), it->orig.size());
			VirtualProtect(it->addr, it->orig.size(), old, &tmp);
			FlushInstructionCache(GetCurrentProcess(), it->addr, it->orig.size());
		}
	}
	s_patches.clear();
	for (void *p : s_allocs)
		VirtualFree(p, 0, MEM_RELEASE);
	s_allocs.clear();
}

uint8_t *AllocExec(size_t n)
{
	void *p = VirtualAlloc(nullptr, n, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (p)
		s_allocs.push_back(p);
	return (uint8_t *)p;
}
} // namespace mem

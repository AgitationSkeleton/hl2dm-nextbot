// hl2mp-nextbot: in-process helpers for HL2MP's server.dll (MSVC x86): sections, RTTI vtables,
// byte patterns, reversible patches. Everything is verified before it is used; a mismatch disables
// the dependent feature instead of guessing.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace mem
{
struct Section
{
	uint8_t *start = nullptr;
	size_t size = 0;
	bool Contains(const void *p, size_t n = 1) const
	{
		return (const uint8_t *)p >= start && (const uint8_t *)p + n <= start + size;
	}
};

struct Module
{
	uint8_t *base = nullptr;
	Section text, rdata, data;
	bool Load(const char *dllName);
};

// "8B B7 54 05 00 00 ?? 0F" style pattern; returns the first match at or after `from`, or nullptr.
uint8_t *FindPattern(const Section &sec, const char *pattern, uint8_t *from = nullptr);

// Primary vtable (complete-object-locator offset 0) of an MSVC class, found through its RTTI
// type descriptor ".?AV<cls>@@". nullptr if absent.
void **FindVTable(const Module &m, const char *cls);

// Target of an E8/E9 rel32 at `insn`.
inline uint8_t *RelTarget(uint8_t *insn) { return insn + 5 + *(int32_t *)(insn + 1); }

// Reversible write into code/rodata. Patches are restored in reverse order by RestoreAll().
bool Patch(void *addr, const void *bytes, size_t n);
void RestoreAll();

// Small executable allocation for trampolines (freed by RestoreAll()).
uint8_t *AllocExec(size_t n);
} // namespace mem

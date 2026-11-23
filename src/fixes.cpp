// hl2mp-nextbot: small fixes to Valve's shipped HL2MP bot code, located at load time and verified.
//
// CHL2MPBotGetAmmo::IsPossible (hl2mp_bot_get_ammo.cpp) collects candidates with
//     gEntList.FindEntityByClassname( ammo, "*_health*" )
// (copied from GetHealth), so its ammo filter only ever sees health items and bots never go for
// ammo. We point those two pushes (IsPossible and the copy inlined into OnStart) at "item_*"; the
// stock CAmmoFilter then keeps only CItems that carry ammo for the bot's active weapon.
#include <string.h>
#include "plugin.h"
#include "mem.h"
#include <convar.h>

static const char s_itemPattern[] = "item_*";

namespace fixes
{
static void FixAmmoSearch(mem::Module &m)
{
	static const char needle[] = "*_health*";
	uint8_t *str = nullptr;
	for (uint8_t *p = m.rdata.start; p + sizeof(needle) <= m.rdata.start + m.rdata.size; ++p)
		if (!memcmp(p, needle, sizeof(needle)) && p[-1] == 0)
		{
			str = p;
			break;
		}
	ConVar *range = HNB_FindVar("hl2mp_bot_ammo_search_range");
	if (!str || !range)
	{
		HNB_Log("ammo-search fix: anchors not found, skipped");
		return;
	}
	uint8_t push[5] = {0x68};
	*(uint32_t *)(push + 1) = (uint32_t)str;
	uint8_t *refs[8];
	int n = 0;
	char pat[32];
	V_snprintf(pat, sizeof(pat), "68 %02X %02X %02X %02X", push[1], push[2], push[3], push[4]);
	for (uint8_t *p = mem::FindPattern(m.text, pat); p && n < 8; p = mem::FindPattern(m.text, pat, p + 1))
		refs[n++] = p;
	// GetAmmo::IsPossible reads hl2mp_bot_ammo_search_range (mov eax, [convar.m_pParent]) just before the push.
	uint8_t readVar[5] = {0xA1};
	*(uint32_t *)(readVar + 1) = (uint32_t)range + 0x1C;
	bool anchored = false;
	if (n == 4)
		for (uint8_t *p = refs[0] - 0x100; p < refs[0]; ++p)
			if (!memcmp(p, readVar, 5))
				anchored = true;
	if (!anchored || refs[1] - refs[0] > 0x300 || refs[2] < refs[1])
	{
		HNB_Log("ammo-search fix: code layout not recognised (%d refs), skipped", n);
		return;
	}
	uint32_t repl = (uint32_t)s_itemPattern;
	mem::Patch(refs[0] + 1, &repl, 4);
	mem::Patch(refs[1] + 1, &repl, 4);
	HNB_Log("ammo-search fix applied (GetAmmo now scans item_*)");
}

// CHL2MPBotMainAction::SelectTargetPoint (hl2mp_bot_behavior.cpp) aims the gravity gun with
//     float mass = pHeldEntity->VPhysicsGetObject()->GetMass();
// with no NULL check; a held object without a physics object crashes the server (seen in testing).
// Guard it: NULL -> take the "no held prop" path.
static void FixPhyscannonMass(mem::Module &m)
{
	//   mov [ebp-8],eax ; test eax,eax ; je NOPROP ; lea eax,[esi+0x1478] ; mov ecx,edi ; mov esi,[eax] ; mov [ebp-0x10],eax
	static const char head[] = "89 45 F8 85 C0 0F 84 ?? ?? ?? ?? 8D 86 78 14 00 00 8B CF 8B 30 89 45 F0";
	//   mov ecx,[ecx+0x20c] ; movss xmm0,[eax+0x2c] ; movss [ebp-0xc],xmm0 ; mov eax,[ecx] ; mov eax,[eax+0x74] ; call eax
	static const char site[] = "8B 89 0C 02 00 00 F3 0F 10 40 2C F3 0F 11 45 F4 8B 01 8B 40 74 FF D0";
	uint8_t *h = mem::FindPattern(m.text, head);
	uint8_t *s = h ? mem::FindPattern(mem::Section{h, 0x80}, site) : nullptr;
	if (!h || !s || mem::FindPattern(m.text, head, h + 1))
	{
		HNB_Log("physcannon fix: code layout not recognised, skipped");
		return;
	}
	uint8_t *noProp = h + 11 + *(int32_t *)(h + 7);
	uint8_t *stub = mem::AllocExec(32);
	uint8_t *p = stub;
	memcpy(p, s, 6); p += 6;                                     // mov ecx,[ecx+0x20c]
	*p++ = 0x85; *p++ = 0xC9;                                    // test ecx,ecx
	*p++ = 0x74; *p++ = 0x05;                                    // jz null
	*p = 0xE9; *(int32_t *)(p + 1) = (int32_t)((s + 6) - (p + 5)); p += 5;     // jmp back
	*p++ = 0x8B; *p++ = 0x75; *p++ = 0xF0;                       // null: mov esi,[ebp-0x10]
	*p++ = 0x81; *p++ = 0xEE; *(uint32_t *)p = 0x1478; p += 4;   // sub esi,0x1478   (esi = me again)
	*p = 0xE9; *(int32_t *)(p + 1) = (int32_t)(noProp - (p + 5)); p += 5;      // jmp NOPROP
	uint8_t jmp[6] = {0xE9, 0, 0, 0, 0, 0x90};
	*(int32_t *)(jmp + 1) = (int32_t)(stub - (s + 5));
	mem::Patch(s, jmp, 6);
	HNB_Log("physcannon held-prop NULL fix applied");
}

void Apply()
{
	mem::Module m;
	if (!m.Load("server.dll"))
	{
		HNB_Log("server.dll sections not found; fixes skipped");
		return;
	}
	FixAmmoSearch(m);
	FixPhyscannonMass(m);
}
} // namespace fixes

// hl2mp-nextbot: better nav meshes from HL2MP's own generator (nav_generate in server.dll).
//
// hl2mpnb_nav_generate (needs sv_cheats 1; meant for an offline/test server) runs the stock
// generator with four corrections, all undone when the generator reloads the map:
//  1. Seeds: stock CNavMesh::AddWalkableSeeds seeds only the FIRST info_player_start, so DM maps
//     without one cannot be generated and areas not walkable from it are missed. We hook the
//     virtual and add every info_player_* spawn plus every weapon_*/item_* spot.
//  2. Seed skipping: stock GetNextWalkableSeedNode returns NULL for a seed that sits on an
//     already-sampled node, and the caller takes NULL as "no seeds left": every later seed is
//     dropped. A trampoline makes it move on to the next seed instead.
//  3. Trace mask: stock GetGenerationTraceMask is MASK_NPCSOLID_BRUSHONLY (monster clip). Bots
//     are players, so we generate with MASK_PLAYERSOLID_BRUSHONLY (player clip).
//  4. Toggle brushes: the generator treats every func_brush with Solidity "Toggle" as walkable,
//     even when it is enabled (dm_lockdown's cell-block wall). Enabled toggle brushes are made
//     "Always solid" for the run.
#include <math.h>
#include <string.h>
#include <vector>
#include "plugin.h"
#include "mem.h"
#include <eiface.h>
#include <convar.h>
#include <engine/IEngineTrace.h>
#include <gametrace.h>
#include <toolframework/itoolentity.h>

ConVar hnb_nav_mask("hl2mpnb_nav_mask", "0x201400B", FCVAR_NONE,
	"Trace mask the nav generator uses while hl2mpnb_nav_generate runs (default MASK_PLAYERSOLID: player clip + fixed props).");

namespace navgen
{
enum { SLOT_ADD_WALKABLE_SEEDS = 30, SLOT_GET_GENERATION_TRACE_MASK = 31, SEEDS_OFFSET = 0x540 };

struct SeedSpot
{
	Vector pos, normal;
};

static bool s_installed = false, s_installFailed = false;
static bool s_busy = false;
static float s_busySince = 0.0f;
static std::vector<SeedSpot> s_seeds;

typedef void (__fastcall *AddSeedsFn)(void *, void *);
typedef unsigned int (__fastcall *MaskFn)(const void *, void *);
typedef int (__fastcall *InsertBeforeFn)(void *, void *, int, const void *);
static AddSeedsFn s_origAddSeeds = nullptr;
static InsertBeforeFn s_insertBefore = nullptr;

bool Busy()
{
	if (s_busy && gpGlobals && gpGlobals->curtime - s_busySince > 1800.0f)
		s_busy = false;     // generation never finished (e.g. failed): give the server back
	return s_busy;
}

void OnLevelInit()
{
	s_busy = false;
	s_seeds.clear();
}

static void __fastcall Hook_AddWalkableSeeds(void *nav, void *)
{
	s_origAddSeeds(nav, nullptr);
	if (!s_busy)
		return;
	uint8_t *vec = (uint8_t *)nav + SEEDS_OFFSET;
	int before = *(int *)(vec + 0xC);
	for (const SeedSpot &s : s_seeds)
		s_insertBefore(vec, nullptr, *(int *)(vec + 0xC), &s);
	HNB_Log("nav seeds: %d stock + %d added", before, (int)s_seeds.size());
}

static unsigned int __fastcall Hook_GetGenerationTraceMask(const void *, void *)
{
	return (unsigned int)strtoul(hnb_nav_mask.GetString(), nullptr, 0);
}

static bool Install()
{
	if (s_installed || s_installFailed)
		return s_installed;
	s_installFailed = true;
	mem::Module m;
	if (!m.Load("server.dll"))
		return false;
	void **vt = mem::FindVTable(m, "CNavMesh");
	if (!vt)
	{
		HNB_Log("nav: CNavMesh vtable not found");
		return false;
	}
	uint8_t *mask = (uint8_t *)vt[SLOT_GET_GENERATION_TRACE_MASK];
	uint8_t *seeds = (uint8_t *)vt[SLOT_ADD_WALKABLE_SEEDS];
	static const uint8_t maskCode[] = {0xB8, 0x0B, 0x40, 0x02, 0x00, 0xC3};   // mov eax,MASK_NPCSOLID_BRUSHONLY; ret
	static const uint8_t seedsCode[] = {0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08};  // aligned-stack prologue
	if (!m.text.Contains(mask, 6) || memcmp(mask, maskCode, 6) || !m.text.Contains(seeds, 6) || memcmp(seeds, seedsCode, 6))
	{
		HNB_Log("nav: CNavMesh vtable slots do not match this server.dll build");
		return false;
	}
	// AddWalkableSeed is inlined at the end of AddWalkableSeeds: lea ecx,[edi+0x540] ... call InsertBefore
	mem::Section fn{seeds, 0x400};
	uint8_t *lea = mem::FindPattern(fn, "8D 8F 40 05 00 00");
	// call InsertBefore; pop edi; pop esi; mov esp,ebp; pop ebp; mov esp,ebx; pop ebx; ret
	uint8_t *call = lea ? mem::FindPattern(mem::Section{lea, 0x80}, "E8 ?? ?? ?? ?? 5F 5E 8B E5 5D 8B E3 5B C3") : nullptr;
	if (call && !m.text.Contains(mem::RelTarget(call)))
		call = nullptr;
	if (!call)
	{
		HNB_Log("nav: walkable-seed vector not found");
		return false;
	}
	s_insertBefore = (InsertBeforeFn)mem::RelTarget(call);
	s_origAddSeeds = (AddSeedsFn)seeds;

	// Seed skipping (inlined GetNextWalkableSeedNode in SampleStep).
	uint8_t *head = mem::FindPattern(m.text, "8B B7 54 05 00 00 3B B7 4C 05 00 00 0F 8D");
	uint8_t *site = head ? mem::FindPattern(mem::Section{head, 0x90}, "85 C0 75 ?? 68 94 00 00 00 E8") : nullptr;
	uint8_t *found = site ? site + 4 + (int8_t)site[3] : nullptr;
	bool seedFix = found && !memcmp(found, "\xF3\x0F\x10\x1D", 4) && !memcmp(found + 8, "\xF3\x0F\x10\x15", 4) &&
		!memcmp(found + 16, "\xC7\x87\xF0\x04\x00\x00\x00\x00\x00\x00", 10);
	if (seedFix)
	{
		uint8_t *stub = mem::AllocExec(64);
		uint8_t *p = stub;
		*p++ = 0x85; *p++ = 0xC0;                          // test eax,eax   (node already at this seed?)
		*p++ = 0x75; *p++ = 10;                            // jne skip
		memcpy(p, site + 4, 5); p += 5;                    // push 0x94      (sizeof CNavNode)
		*p = 0xE9; *(int32_t *)(p + 1) = (int32_t)((site + 9) - (p + 5)); p += 5;   // jmp back to call operator new
		memcpy(p, found, 16); p += 16;                     // skip: reload xmm3/xmm2 constants
		*p = 0xE9; *(int32_t *)(p + 1) = (int32_t)(head - (p + 5)); p += 5;         // jmp to "next seed"
		uint8_t jmp[9] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90};
		*(int32_t *)(jmp + 1) = (int32_t)(stub - (site + 5));
		mem::Patch(site, jmp, 9);
	}
	else
		HNB_Log("nav: seed-skip code not recognised; later seeds may be dropped");

	void *repl[2] = {(void *)&Hook_AddWalkableSeeds, (void *)&Hook_GetGenerationTraceMask};
	mem::Patch(&vt[SLOT_ADD_WALKABLE_SEEDS], repl, sizeof(repl));
	s_installed = true;
	s_installFailed = false;
	HNB_Log("nav generation hooks installed%s", seedFix ? " (with seed-skip fix)" : "");
	return true;
}

class CWorldOnlyFilter : public ITraceFilter
{
public:
	bool ShouldHitEntity(IHandleEntity *, int) override { return false; }
	TraceType_t GetTraceType() const override { return TRACE_WORLD_ONLY; }
};

static bool GetOrigin(CBaseEntity *e, Vector &out)
{
	char buf[128];
	if (!servertools->GetKeyValue(e, "origin", buf, sizeof(buf)))
		return false;
	return sscanf(buf, "%f %f %f", &out.x, &out.y, &out.z) == 3;
}

static float Snap(float v)
{
	return 25.0f * floorf(v / 25.0f + 0.5f);   // GenerationStepSize
}

static void AddSeedAt(const Vector &where)
{
	Vector start(Snap(where.x), Snap(where.y), where.z + 24.0f);
	Ray_t ray;
	ray.Init(start, start - Vector(0, 0, 320.0f));
	trace_t tr;
	CWorldOnlyFilter filter;
	enginetrace->TraceRay(ray, (unsigned int)strtoul(hnb_nav_mask.GetString(), nullptr, 0), &filter, &tr);
	if (tr.startsolid || tr.fraction >= 1.0f || tr.plane.normal.z < 0.7f)
		return;
	for (const SeedSpot &s : s_seeds)
		if ((s.pos - tr.endpos).LengthSqr() < 1.0f)
			return;
	s_seeds.push_back({tr.endpos, tr.plane.normal});
}

static void CollectSeeds()
{
	s_seeds.clear();
	static const char *classes[] = {"info_player_deathmatch", "info_player_rebel", "info_player_combine",
		"info_player_start", "weapon_*", "item_*"};
	for (const char *cls : classes)
		for (CBaseEntity *e = servertools->FindEntityByClassname(nullptr, cls); e;
			 e = servertools->FindEntityByClassname(e, cls))
		{
			Vector o;
			if (GetOrigin(e, o))
				AddSeedAt(o);
		}
}

static int SolidifyToggleBrushes()
{
	int n = 0;
	for (CBaseEntity *e = servertools->FindEntityByClassname(nullptr, "func_brush"); e;
		 e = servertools->FindEntityByClassname(e, "func_brush"))
	{
		char sol[16], dis[16];
		if (!servertools->GetKeyValue(e, "Solidity", sol, sizeof(sol)) || atoi(sol) != 0)
			continue;
		if (servertools->GetKeyValue(e, "StartDisabled", dis, sizeof(dis)) && atoi(dis) != 0)
			continue;
		servertools->SetKeyValue(e, "Solidity", "1");
		char now[16] = "";
		servertools->GetKeyValue(e, "Solidity", now, sizeof(now));
		if (atoi(now) == 1)
			++n;
		else
			HNB_Log("nav: func_brush Solidity write did not stick (%s)", now);
	}
	return n;
}
// Props only block traces whose mask has CONTENTS_MONSTER (StandardFilterRules), which our mask has.
// Movable physics objects, weapons and pickups must not become nav holes: remove them for the run
// (the generator reloads the map when it is done). Motion-disabled props stay as obstacles.
static int RemoveMovables()
{
	std::vector<CBaseEntity *> doomed;
	static const char *classes[] = {"prop_physics*", "func_physbox*", "weapon_*", "item_*", "prop_ragdoll"};
	for (const char *cls : classes)
		for (CBaseEntity *e = servertools->FindEntityByClassname(nullptr, cls); e;
			 e = servertools->FindEntityByClassname(e, cls))
		{
			char buf[64] = "";
			servertools->GetKeyValue(e, "classname", buf, sizeof(buf));
			if (!strcmp(buf, "item_healthcharger") || !strcmp(buf, "item_suitcharger"))
				continue;      // wall-mounted, fixed
			if (!strncmp(buf, "prop_physics", 12) || !strncmp(buf, "func_physbox", 12))
			{
				char sf[32] = "0";
				servertools->GetKeyValue(e, "spawnflags", sf, sizeof(sf));
				if (atoi(sf) & 8)      // SF_PHYSPROP_MOTIONDISABLED / SF_PHYSBOX_MOTIONDISABLED
					continue;
			}
			doomed.push_back(e);
		}
	for (CBaseEntity *e : doomed)
		servertools->RemoveEntity(e);      // deferred (UTIL_Remove): safe for weapons bots still hold
	return (int)doomed.size();
}
} // namespace navgen

CON_COMMAND(hl2mpnb_nav_generate, "Generate this map's nav mesh with the hl2mp-nextbot corrections (needs sv_cheats 1; reloads the map when done).")
{
	ConVar *cheats = HNB_FindVar("sv_cheats");
	if (!cheats || !cheats->GetBool())
	{
		META_CONPRINTF("[%s] hl2mpnb_nav_generate needs sv_cheats 1 (use it on a test server)\n", HNB_TAG);
		return;
	}
	if (!navgen::Install())
	{
		META_CONPRINTF("[%s] nav hooks unavailable on this build; running plain nav_generate\n", HNB_TAG);
		engine->ServerCommand("nav_generate\n");
		return;
	}
	navgen::s_busy = true;
	navgen::s_busySince = gpGlobals->curtime;
	engine->ServerCommand("hl2mp_bot_kick all\n");
	navgen::CollectSeeds();
	int brushes = navgen::SolidifyToggleBrushes();
	int removed = navgen::RemoveMovables();
	HNB_Log("nav_generate: %d seeds, %d toggle brushes made solid, %d movable objects removed, mask %s",
		(int)navgen::s_seeds.size(), brushes, removed,
		hnb_nav_mask.GetString());
	engine->ServerCommand("nav_generate\n");
}

class CHitAllFilter : public ITraceFilter
{
public:
	bool ShouldHitEntity(IHandleEntity *, int) override { return true; }
	TraceType_t GetTraceType() const override { return TRACE_EVERYTHING; }
};

CON_COMMAND(hl2mpnb_trace, "hl2mpnb_trace x1 y1 z1 x2 y2 z2 [mask]: player-hull trace, prints what it hits (debug).")
{
	if (args.ArgC() < 7)
		return;
	Vector a(atof(args[1]), atof(args[2]), atof(args[3])), b(atof(args[4]), atof(args[5]), atof(args[6]));
	unsigned int mask = args.ArgC() > 7 ? strtoul(args[7], nullptr, 0) : 0x201400B;   // MASK_PLAYERSOLID
	Ray_t ray;
	ray.Init(a, b, Vector(-16, -16, 0), Vector(16, 16, 72));
	trace_t tr;
	CHitAllFilter filter;
	enginetrace->TraceRay(ray, mask, &filter, &tr);
	char cls[64] = "?";
	if (tr.m_pEnt)
		servertools->GetKeyValue(tr.m_pEnt, "classname", cls, sizeof(cls));
	META_CONPRINTF("[%s] frac %.3f startsolid %d end %.1f %.1f %.1f contents 0x%x ent %s\n", HNB_TAG, tr.fraction,
		(int)tr.startsolid, tr.endpos.x, tr.endpos.y, tr.endpos.z, tr.contents, tr.m_pEnt ? cls : "none");
}

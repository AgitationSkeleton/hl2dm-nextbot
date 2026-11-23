// hl2mp-nextbot: free bots that are embedded in world geometry.
//
// Some maps have spawn points a few units inside the floor (crossfire's info_player_deathmatch at
// 32 320 -1840 sits 4 units under a raised floor). A player spawned there is stuck in solid and can
// never move; a human can sometimes jump out, a bot never does (crossfire: 137 stuck events in a
// 5-minute soak from one bot). Twice a second, a live bot whose player hull starts in solid world
// is lifted to the first free height above it.
#include <stdio.h>
#include "plugin.h"
#include <eiface.h>
#include <convar.h>
#include <engine/IEngineTrace.h>
#include <gametrace.h>
#include <toolframework/itoolentity.h>

ConVar hnb_unstick("hl2mpnb_unstick", "1", FCVAR_NONE, "Lift bots that are stuck inside world geometry (bad spawn points).");

namespace unstick
{
class CWorldOnly : public ITraceFilter
{
public:
	bool ShouldHitEntity(IHandleEntity *, int) override { return false; }
	TraceType_t GetTraceType() const override { return TRACE_WORLD_ONLY; }
};

static float s_next = 0.0f;

static bool InSolid(const Vector &pos)
{
	Ray_t ray;
	ray.Init(pos, pos, Vector(-16, -16, 0), Vector(16, 16, 36));   // crouch hull: a ducking bot under a low ceiling is not "in solid"
	trace_t tr;
	CWorldOnly filter;
	enginetrace->TraceRay(ray, 0x1400B /* MASK_PLAYERSOLID_BRUSHONLY */, &filter, &tr);
	return tr.startsolid;
}

void Frame()
{
	if (!gpGlobals || !hnb_unstick.GetBool() || gpGlobals->curtime < s_next)
		return;
	s_next = gpGlobals->curtime + 0.5f;
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		edict_t *e = engine->PEntityOfEntIndex(i);
		if (!e || e->IsFree())
			continue;
		IPlayerInfo *pi = playerinfomanager->GetPlayerInfo(e);
		if (!pi || !pi->IsConnected() || !pi->IsFakeClient() || pi->IsHLTV() || pi->IsDead() || pi->IsObserver())
			continue;
		Vector o = pi->GetAbsOrigin();
		if (!InSolid(o))
			continue;
		for (float dz = 2.0f; dz <= 32.0f; dz += 2.0f)
		{
			Vector p = o + Vector(0, 0, dz);
			if (InSolid(p))
				continue;
			CBaseEntity *ent = servertools->GetBaseEntityByEntIndex(i);
			if (ent)
			{
				servertools->SetKeyValue(ent, "origin", p);
				HNB_Log("unstuck %s from solid at %.0f %.0f %.0f (+%.0f)", pi->GetName(), o.x, o.y, o.z, dz);
			}
			break;
		}
	}
}
} // namespace unstick

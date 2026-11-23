// hl2mp-nextbot: Metamod:Source plugin entry, interfaces, engine hooks.
#include <stdio.h>
#include <stdarg.h>
#include "plugin.h"
#include "mem.h"
#include <eiface.h>
#include <convar.h>
#include <engine/IEngineTrace.h>
#include <filesystem.h>
#include <toolframework/itoolentity.h>
#include <tier1/fmtstr.h>

SH_DECL_HOOK6(IServerGameDLL, LevelInit, SH_NOATTRIB, 0, bool, char const *, char const *, char const *, char const *, bool, bool);
SH_DECL_HOOK0_void(IServerGameDLL, LevelShutdown, SH_NOATTRIB, 0);
SH_DECL_HOOK1_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool);
SH_DECL_HOOK1_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0, edict_t *);
SH_DECL_HOOK2(IVEngineServer, CreateFakeClientEx, SH_NOATTRIB, 0, edict_t *, const char *, bool);
SH_DECL_HOOK1(IVEngineServer, CreateFakeClient, SH_NOATTRIB, 0, edict_t *, const char *);
SH_DECL_HOOK3_void(IVEngineServer, SetFakeClientConVarValue, SH_NOATTRIB, 0, edict_t *, const char *, const char *);

HL2MPNextBotPlugin g_Plugin;
PLUGIN_EXPOSE(HL2MPNextBotPlugin, g_Plugin);

IVEngineServer     *engine = nullptr;
IServerGameDLL     *gamedll = nullptr;
IServerGameClients *gameclients = nullptr;
IPlayerInfoManager *playerinfomanager = nullptr;
IServerTools       *servertools = nullptr;
IEngineTrace       *enginetrace = nullptr;
IFileSystem        *filesystem = nullptr;
CGlobalVars        *gpGlobals = nullptr;
static ICvar       *icvar = nullptr;

class BaseAccessor : public IConCommandBaseAccessor
{
public:
	bool RegisterConCommandBase(ConCommandBase *cmd) override { return META_REGCVAR(cmd); }
} s_BaseAccessor;

void HNB_Log(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	META_CONPRINTF("[%s] %s\n", HNB_TAG, buf);
	if (engine)
		engine->LogPrint(CFmtStrN<1100>("[%s] %s\n", HNB_TAG, buf));
}

ConVar *HNB_FindVar(const char *name)
{
	return icvar ? icvar->FindVar(name) : nullptr;
}

bool HL2MPNextBotPlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
	GET_V_IFACE_CURRENT(GetEngineFactory, icvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, enginetrace, IEngineTrace, INTERFACEVERSION_ENGINETRACE_SERVER);
	GET_V_IFACE_ANY(GetServerFactory, gamedll, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_ANY(GetServerFactory, gameclients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
	GET_V_IFACE_ANY(GetServerFactory, playerinfomanager, IPlayerInfoManager, INTERFACEVERSION_PLAYERINFOMANAGER);
	GET_V_IFACE_ANY(GetServerFactory, servertools, IServerTools, VSERVERTOOLS_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, filesystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
	gpGlobals = ismm->GetCGlobals();

	if (!icvar->FindVar("hl2mp_bot_quota") || !icvar->FindCommand("hl2mp_bot_add"))
	{
		ismm->Format(error, maxlen, "this server.dll has no hl2mp_bot (needs the 2025+ HL2DM build)");
		return false;
	}

	SH_ADD_HOOK_MEMFUNC(IServerGameDLL, LevelInit, gamedll, this, &HL2MPNextBotPlugin::Hook_LevelInit, true);
	SH_ADD_HOOK_MEMFUNC(IServerGameDLL, LevelShutdown, gamedll, this, &HL2MPNextBotPlugin::Hook_LevelShutdown, false);
	SH_ADD_HOOK_MEMFUNC(IServerGameDLL, GameFrame, gamedll, this, &HL2MPNextBotPlugin::Hook_GameFrame, true);
	SH_ADD_HOOK_MEMFUNC(IServerGameClients, ClientDisconnect, gameclients, this, &HL2MPNextBotPlugin::Hook_ClientDisconnect, false);
	SH_ADD_HOOK_MEMFUNC(IVEngineServer, CreateFakeClientEx, engine, this, &HL2MPNextBotPlugin::Hook_CreateFakeClientEx, false);
	SH_ADD_HOOK_MEMFUNC(IVEngineServer, CreateFakeClientEx, engine, this, &HL2MPNextBotPlugin::Hook_CreateFakeClientExPost, true);
	SH_ADD_HOOK_MEMFUNC(IVEngineServer, CreateFakeClient, engine, this, &HL2MPNextBotPlugin::Hook_CreateFakeClient, false);
	SH_ADD_HOOK_MEMFUNC(IVEngineServer, CreateFakeClient, engine, this, &HL2MPNextBotPlugin::Hook_CreateFakeClientPost, true);
	SH_ADD_HOOK_MEMFUNC(IVEngineServer, SetFakeClientConVarValue, engine, this, &HL2MPNextBotPlugin::Hook_SetFakeClientConVarValue, false);

	g_pCVar = icvar;
	ConVar_Register(0, &s_BaseAccessor);

	fixes::Apply();
	pop::OnLoad(late);
	HNB_Log("%s %s loaded (built %s)", HNB_NAME, HNB_VERSION, __DATE__);
	return true;
}

bool HL2MPNextBotPlugin::Unload(char *error, size_t maxlen)
{
	SH_REMOVE_HOOK_MEMFUNC(IServerGameDLL, LevelInit, gamedll, this, &HL2MPNextBotPlugin::Hook_LevelInit, true);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameDLL, LevelShutdown, gamedll, this, &HL2MPNextBotPlugin::Hook_LevelShutdown, false);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameDLL, GameFrame, gamedll, this, &HL2MPNextBotPlugin::Hook_GameFrame, true);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, ClientDisconnect, gameclients, this, &HL2MPNextBotPlugin::Hook_ClientDisconnect, false);
	SH_REMOVE_HOOK_MEMFUNC(IVEngineServer, CreateFakeClientEx, engine, this, &HL2MPNextBotPlugin::Hook_CreateFakeClientEx, false);
	SH_REMOVE_HOOK_MEMFUNC(IVEngineServer, CreateFakeClientEx, engine, this, &HL2MPNextBotPlugin::Hook_CreateFakeClientExPost, true);
	SH_REMOVE_HOOK_MEMFUNC(IVEngineServer, CreateFakeClient, engine, this, &HL2MPNextBotPlugin::Hook_CreateFakeClient, false);
	SH_REMOVE_HOOK_MEMFUNC(IVEngineServer, CreateFakeClient, engine, this, &HL2MPNextBotPlugin::Hook_CreateFakeClientPost, true);
	SH_REMOVE_HOOK_MEMFUNC(IVEngineServer, SetFakeClientConVarValue, engine, this, &HL2MPNextBotPlugin::Hook_SetFakeClientConVarValue, false);
	mem::RestoreAll();
	ConVar_Unregister();
	return true;
}

bool HL2MPNextBotPlugin::Hook_LevelInit(const char *map, const char *, const char *, const char *, bool, bool)
{
	navgen::OnLevelInit();
	pop::OnLevelInit(map);
	RETURN_META_VALUE(MRES_IGNORED, true);
}

void HL2MPNextBotPlugin::Hook_LevelShutdown()
{
	RETURN_META(MRES_IGNORED);
}

void HL2MPNextBotPlugin::Hook_GameFrame(bool simulating)
{
	pop::Frame();
	if (!navgen::Busy())
		unstick::Frame();
	RETURN_META(MRES_IGNORED);
}

void HL2MPNextBotPlugin::Hook_ClientDisconnect(edict_t *ent)
{
	if (ent)
		pop::OnClientDisconnect(engine->IndexOfEdict(ent));
	RETURN_META(MRES_IGNORED);
}

// --- bot creation: give our bots their names from the start ---------------------------------
// hl2mp_bot_add creates the client with a placeholder ("Bot Name"); swapping the netname here
// means the bot joins under its real name, with no "changed name" broadcast.
edict_t *HL2MPNextBotPlugin::Hook_CreateFakeClientEx(const char *netname, bool reportFakeClient)
{
	if (const char *want = pop::PendingName())
		RETURN_META_VALUE_NEWPARAMS(MRES_IGNORED, nullptr, &IVEngineServer::CreateFakeClientEx, (want, reportFakeClient));
	RETURN_META_VALUE(MRES_IGNORED, nullptr);
}

edict_t *HL2MPNextBotPlugin::Hook_CreateFakeClientExPost(const char *netname, bool reportFakeClient)
{
	pop::RecordCreated(META_RESULT_ORIG_RET(edict_t *), netname);
	RETURN_META_VALUE(MRES_IGNORED, nullptr);
}

edict_t *HL2MPNextBotPlugin::Hook_CreateFakeClient(const char *netname)
{
	if (const char *want = pop::PendingName())
		RETURN_META_VALUE_NEWPARAMS(MRES_IGNORED, nullptr, &IVEngineServer::CreateFakeClient, (want));
	RETURN_META_VALUE(MRES_IGNORED, nullptr);
}

edict_t *HL2MPNextBotPlugin::Hook_CreateFakeClientPost(const char *netname)
{
	pop::RecordCreated(META_RESULT_ORIG_RET(edict_t *), netname);
	RETURN_META_VALUE(MRES_IGNORED, nullptr);
}

// Stock hl2mp_bot_add / quota code has two bugs here (src/game/server/hl2mp/bot):
//  * "name" is passed an uninitialised stack buffer when a name argument is given;
//  * "cl_playermodel" indexes g_ppszRandom*Models with RandomInt(0, ARRAYSIZE), one past the end.
// Never read `value` for those two: substitute a known-good string instead.
void HL2MPNextBotPlugin::Hook_SetFakeClientConVarValue(edict_t *ent, const char *cvar, const char *value)
{
	if (ent && cvar)
	{
		if (!V_stricmp(cvar, "cl_playermodel"))
			RETURN_META_NEWPARAMS(MRES_IGNORED, &IVEngineServer::SetFakeClientConVarValue, (ent, cvar, pop::RandomModel()));
		if (!V_stricmp(cvar, "name"))
		{
			const char *rec = pop::RecordedName(engine->IndexOfEdict(ent));
			if (rec && *rec)
				RETURN_META_NEWPARAMS(MRES_IGNORED, &IVEngineServer::SetFakeClientConVarValue, (ent, cvar, rec));
		}
	}
	RETURN_META(MRES_IGNORED);
}

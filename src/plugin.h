// hl2mp-nextbot: Metamod:Source plugin that runs Valve's own HL2MP NextBot (CHL2MPBot, shipped in
// HL2MP's server.dll since the 2025 SDK update) as the server's bots: a fill-style population and
// names, fixes for the stock bot add/quota/ammo bugs, and a better nav mesh generator front end.
#pragma once
#include <ISmmPlugin.h>
#include <iplayerinfo.h>

static_assert(METAMOD_PLAPI_VERSION == 16, "HL2DM runs Metamod 1.11/1.12 (plugin API 16)");

#define HNB_NAME    "HL2MP NextBot"
#define HNB_VERSION "1.0.0"
#define HNB_TAG     "HL2MPNB"

class IVEngineServer;
class IServerGameDLL;
class IServerGameClients;
class IServerTools;
class IEngineTrace;
class ICvar;
class IFileSystem;
class CGlobalVars;
class ConVar;
struct edict_t;

extern IVEngineServer     *engine;
extern IServerGameDLL     *gamedll;
extern IServerGameClients *gameclients;
extern IPlayerInfoManager *playerinfomanager;
extern IServerTools       *servertools;
extern IEngineTrace       *enginetrace;
extern IFileSystem        *filesystem;
extern CGlobalVars        *gpGlobals;

class HL2MPNextBotPlugin : public ISmmPlugin
{
public:
	bool Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late) override;
	bool Unload(char *error, size_t maxlen) override;
	bool Pause(char *error, size_t maxlen) override { return true; }
	bool Unpause(char *error, size_t maxlen) override { return true; }
	void AllPluginsLoaded() override {}
	const char *GetAuthor() override      { return "hl2mp-nextbot contributors"; }
	const char *GetName() override        { return HNB_NAME; }
	const char *GetDescription() override { return "Valve's HL2MP NextBot as server bots: population, names, fixes, nav generation"; }
	const char *GetURL() override         { return ""; }
	const char *GetLicense() override     { return "See README (own code; TF2 name list from the Source SDK)"; }
	const char *GetVersion() override     { return HNB_VERSION; }
	const char *GetDate() override        { return __DATE__; }
	const char *GetLogTag() override      { return HNB_TAG; }

	bool Hook_LevelInit(const char *map, const char *ents, const char *oldLevel, const char *landmark, bool loadGame, bool background);
	void Hook_LevelShutdown();
	void Hook_GameFrame(bool simulating);
	void Hook_ClientDisconnect(edict_t *ent);
	edict_t *Hook_CreateFakeClientEx(const char *netname, bool reportFakeClient);
	edict_t *Hook_CreateFakeClientExPost(const char *netname, bool reportFakeClient);
	edict_t *Hook_CreateFakeClient(const char *netname);
	edict_t *Hook_CreateFakeClientPost(const char *netname);
	void Hook_SetFakeClientConVarValue(edict_t *ent, const char *cvar, const char *value);
};

extern HL2MPNextBotPlugin g_Plugin;
PLUGIN_GLOBALVARS();

void HNB_Log(const char *fmt, ...);
ConVar *HNB_FindVar(const char *name);

// population.cpp
namespace pop
{
void OnLoad(bool late);
void OnLevelInit(const char *map);
void Frame();
void OnClientDisconnect(int index);
const char *PendingName();          // name for the fake client being created right now, or nullptr
void RecordCreated(edict_t *ent, const char *name);
const char *RecordedName(int index); // name a bot slot was created with ("" if none)
const char *RandomModel();
} // namespace pop

// fixes.cpp
namespace fixes
{
void Apply();
} // namespace fixes

// unstick.cpp
namespace unstick
{
void Frame();
} // namespace unstick

// navgen.cpp
namespace navgen
{
bool Busy();                        // a hl2mpnb_nav_generate run is in progress on this map
void OnLevelInit();
} // namespace navgen

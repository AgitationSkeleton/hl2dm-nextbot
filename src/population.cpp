// hl2mp-nextbot: bot population ("fill to N players, humans replace bots") and names.
//
// Valve's own quota (hl2mp_bot_quota) is kept at 0: in deathmatch everybody is on TEAM_UNASSIGNED,
// which its counting ignores (bots are never counted -> it adds until the server is full, and
// humans never replace bots). We add bots with "hl2mp_bot_add noquota" and kick them by userid.
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
#include <map>
#include "plugin.h"
#include "mem.h"
#include <eiface.h>
#include <convar.h>
#include <filesystem.h>
#include <vstdlib/random.h>
#include <tier1/fmtstr.h>

#define MAX_PLAYERS 65

static const char *s_defaultNames[] = {
#include "default_names.inc"
};

static const char *s_models[] = {
	"models/humans/group03/male_01.mdl",   "models/humans/group03/male_02.mdl",
	"models/humans/group03/female_01.mdl", "models/humans/group03/male_03.mdl",
	"models/humans/group03/female_02.mdl", "models/humans/group03/male_04.mdl",
	"models/humans/group03/female_03.mdl", "models/humans/group03/male_05.mdl",
	"models/humans/group03/female_04.mdl", "models/humans/group03/male_06.mdl",
	"models/humans/group03/female_06.mdl", "models/humans/group03/male_07.mdl",
	"models/humans/group03/female_07.mdl", "models/humans/group03/male_08.mdl",
	"models/humans/group03/male_09.mdl",   "models/combine_soldier.mdl",
	"models/combine_soldier_prisonguard.mdl", "models/combine_super_soldier.mdl",
	"models/police.mdl",
};

static void OnNamesFileChanged(IConVar *var, const char *oldValue, float flOldValue);

ConVar hnb_enable("hl2mpnb_enable", "1", FCVAR_NONE, "Manage the bot population (Valve's hl2mp_bot) with hl2mp-nextbot.");
ConVar hnb_quota("hl2mpnb_quota", "4", FCVAR_NONE, "Bot quota (see hl2mpnb_quota_mode).");
ConVar hnb_quota_mode("hl2mpnb_quota_mode", "fill", FCVAR_NONE,
	"'fill': keep hl2mpnb_quota players in the game, humans replace bots. 'normal': always hl2mpnb_quota bots.");
ConVar hnb_join_after_player("hl2mpnb_join_after_player", "0", FCVAR_NONE,
	"If nonzero, no bots until at least one human is in the game.");
ConVar hnb_count_spectators("hl2mpnb_count_spectators", "0", FCVAR_NONE,
	"If nonzero, humans in spectator count as players for 'fill'.");
ConVar hnb_difficulty("hl2mpnb_difficulty", "-1", FCVAR_NONE,
	"Difficulty for new bots: -1 = use hl2mp_bot_difficulty, 0 easy, 1 normal, 2 hard, 3 expert, 4 = random 0-3.");
ConVar hnb_names_file("hl2mpnb_names_file", "addons/hl2mpnextbot/bot_names.txt", FCVAR_NONE,
	"Optional bot names file (one name per line, // comments). Built-in TF2 bot names are used when it is missing or empty.",
	OnNamesFileChanged);

namespace pop
{
static std::vector<std::string> s_names;
static std::string s_namesSource = "built-in";
static const char *s_pending = nullptr;
static char s_nameSlot[64] = "Bot Name";
static std::string s_pendingBuf;
static std::string s_created[MAX_PLAYERS + 1];   // name a slot was created with (fake clients)
static std::map<int, std::string> s_managed;      // userid -> name for bots this plugin added
static float s_nextThink = 0.0f;
static float s_addBlockedUntil = 0.0f;
static bool s_warnedQuota = false;

static void LoadNames()
{
	s_names.clear();
	s_namesSource = "built-in";
	const char *path = hnb_names_file.GetString();
	if (path && *path && filesystem)
	{
		FileHandle_t f = filesystem->Open(path, "rb", "GAME");
		if (f)
		{
			int size = filesystem->Size(f);
			std::string buf(size > 0 ? size : 0, '\0');
			if (size > 0)
				filesystem->Read(&buf[0], size, f);
			filesystem->Close(f);
			size_t pos = 0;
			while (pos < buf.size())
			{
				size_t nl = buf.find('\n', pos);
				std::string line = buf.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
				pos = (nl == std::string::npos) ? buf.size() : nl + 1;
				while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
					line.pop_back();
				size_t s = line.find_first_not_of(" \t\xEF\xBB\xBF");
				line = (s == std::string::npos) ? "" : line.substr(s);
				if (line.empty() || line.compare(0, 2, "//") == 0)
					continue;
				if (line.size() > 31)
					line.resize(31);
				s_names.push_back(line);
			}
			if (!s_names.empty())
				s_namesSource = path;
		}
	}
	if (s_names.empty())
		for (const char *n : s_defaultNames)
			s_names.push_back(n);
}

const char *PendingName() { return s_pending; }

void RecordCreated(edict_t *ent, const char *netname)
{
	if (!ent)
		return;
	int idx = engine->IndexOfEdict(ent);
	if (idx >= 1 && idx <= MAX_PLAYERS)
		s_created[idx] = s_pending ? s_pending : (netname ? netname : "");
}

const char *RecordedName(int index)
{
	return (index >= 1 && index <= MAX_PLAYERS) ? s_created[index].c_str() : "";
}

// s_models: 15 citizens, then 4 combine. In teamplay HL2MP moves a player to the team its model
// belongs to, so the model must match the team the bot is about to join. hl2mp_bot_add picks
// that team as (rebels < combine) ? rebels : combine, counted before the new bot joins.
const char *RandomModel()
{
	enum { CITIZENS = 15, TEAM_COMBINE = 2, TEAM_REBELS = 3 };
	ConVar *tp = HNB_FindVar("mp_teamplay");
	if (!tp || !tp->GetBool())
		return s_models[RandomInt(0, (int)ARRAYSIZE(s_models) - 1)];
	int rebels = 0, combine = 0;
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		edict_t *e = engine->PEntityOfEntIndex(i);
		IPlayerInfo *pi = (e && !e->IsFree()) ? playerinfomanager->GetPlayerInfo(e) : nullptr;
		if (!pi || !pi->IsConnected())
			continue;
		if (pi->GetTeamIndex() == TEAM_REBELS)
			++rebels;
		else if (pi->GetTeamIndex() == TEAM_COMBINE)
			++combine;
	}
	if (rebels < combine)
		return s_models[RandomInt(0, CITIZENS - 1)];
	return s_models[RandomInt(CITIZENS, (int)ARRAYSIZE(s_models) - 1)];
}

void OnClientDisconnect(int index)
{
	if (index >= 1 && index <= MAX_PLAYERS)
		s_created[index].clear();
}

struct Slot
{
	int index, userid, team;
	bool bot, managed, dead;
	std::string name;
};

static void Collect(std::vector<Slot> &out)
{
	out.clear();
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		edict_t *e = engine->PEntityOfEntIndex(i);
		if (!e || e->IsFree())
			continue;
		IPlayerInfo *pi = playerinfomanager->GetPlayerInfo(e);
		if (!pi || !pi->IsConnected() || pi->IsHLTV() || pi->IsReplay())
			continue;
		Slot s;
		s.index = i;
		s.userid = pi->GetUserID();
		s.team = pi->GetTeamIndex();
		s.bot = pi->IsFakeClient();
		s.dead = pi->IsDead();
		s.name = pi->GetName() ? pi->GetName() : "";
		s.managed = s.bot && s_managed.count(s.userid);
		out.push_back(s);
	}
}

static bool NameInList(const std::string &n)
{
	for (auto &x : s_names)
		if (x == n)
			return true;
	return false;
}

static const char *PickName(const std::vector<Slot> &slots)
{
	std::vector<int> free;
	for (int i = 0; i < (int)s_names.size(); ++i)
	{
		bool used = false;
		for (auto &s : slots)
			if (!V_stricmp(s.name.c_str(), s_names[i].c_str()))
				used = true;
		if (!used)
			free.push_back(i);
	}
	if (free.empty())
		return s_names[RandomInt(0, (int)s_names.size() - 1)].c_str();
	return s_names[free[RandomInt(0, (int)free.size() - 1)]].c_str();
}

static void AddBot(const std::vector<Slot> &slots)
{
	s_pendingBuf = PickName(slots);
	static const char *skills[] = {"easy", "normal", "hard", "expert"};
	int d = hnb_difficulty.GetInt();
	if (d >= 4)
		d = RandomInt(0, 3);
	char cmd[128];
	if (d >= 0 && d <= 3)
		V_snprintf(cmd, sizeof(cmd), "hl2mp_bot_add noquota %s\n", skills[d]);
	else
		V_snprintf(cmd, sizeof(cmd), "hl2mp_bot_add noquota\n");

	std::vector<int> before;
	for (auto &s : slots)
		before.push_back(s.userid);

	s_pending = s_pendingBuf.c_str();
	V_strncpy(s_nameSlot, s_pending, sizeof(s_nameSlot));
	engine->ServerCommand(cmd);
	engine->ServerExecute();
	V_strncpy(s_nameSlot, "Bot Name", sizeof(s_nameSlot));
	s_pending = nullptr;

	std::vector<Slot> now;
	Collect(now);
	for (auto &s : now)
	{
		if (!s.bot || std::find(before.begin(), before.end(), s.userid) != before.end())
			continue;
		s_managed[s.userid] = s_pendingBuf;
		return;
	}
	HNB_Log("hl2mp_bot_add did not create a bot (server full or no nav?); retrying in 10 s");
	s_addBlockedUntil = gpGlobals->curtime + 10.0f;
}

static void KickBot(const std::vector<Slot> &slots)
{
	const Slot *victim = nullptr;
	for (auto &s : slots)
		if (s.managed && (!victim || (s.dead && !victim->dead)))
			victim = &s;
	if (!victim)
		return;
	s_managed.erase(victim->userid);
	engine->ServerCommand(CFmtStr("kickid %d\n", victim->userid));
}

// Valve's GetRandomBotName() has a one-entry list { "Bot Name", NULL } (hl2mp_bot.cpp). The bot
// entity is built with that name and the netname is only fixed afterwards, which broadcasts a
// "changed name" message. Point the list entry at our buffer so the bot is created with its name.
static void RedirectNameList()
{
	mem::Module m;
	if (!m.Load("server.dll"))
		return;
	static const char needle[] = "\0Bot Name";   // NUL-preceded string, then its terminator
	uint8_t *str = nullptr;
	for (uint8_t *p = m.rdata.start; p + sizeof(needle) < m.rdata.start + m.rdata.size; ++p)
		if (!memcmp(p, needle, sizeof(needle)))
		{
			str = p + 1;
			break;
		}
	uint32_t *slot = nullptr;
	int refs = 0;
	for (uint32_t *q = (uint32_t *)m.data.start; str && (uint8_t *)(q + 2) <= m.data.start + m.data.size; ++q)
		if (*q == (uint32_t)str)
		{
			++refs;
			if (q[1] == 0)
				slot = q;
		}
	if (!slot || refs != 1)
	{
		HNB_Log("bot name list not found; bots are renamed after joining");
		return;
	}
	V_strncpy(s_nameSlot, "Bot Name", sizeof(s_nameSlot));
	uint32_t ours = (uint32_t)s_nameSlot;
	mem::Patch(slot, &ours, 4);
}

void OnLoad(bool late)
{
	RedirectNameList();
	LoadNames();
	HNB_Log("%d bot names (%s)", (int)s_names.size(), s_namesSource.c_str());
	if (late)
	{
		// adopt bots we added before a plugin reload (same names list)
		std::vector<Slot> slots;
		Collect(slots);
		for (auto &s : slots)
			if (s.bot && NameInList(s.name))
				s_managed[s.userid] = s.name;
	}
}

void OnLevelInit(const char *map)
{
	LoadNames();
	s_nextThink = 0.0f;
	s_addBlockedUntil = 0.0f;
}

void Frame()
{
	if (!gpGlobals || !hnb_enable.GetBool() || navgen::Busy())
		return;
	if (gpGlobals->curtime < s_nextThink && s_nextThink - gpGlobals->curtime < 5.0f)
		return;
	s_nextThink = gpGlobals->curtime + 0.5f;

	if (ConVar *q = HNB_FindVar("hl2mp_bot_quota"))
	{
		if (q->GetInt() != 0)
		{
			if (!s_warnedQuota)
				HNB_Log("hl2mp_bot_quota forced to 0 (hl2mpnb_quota manages bots)");
			s_warnedQuota = true;
			q->SetValue(0);
		}
	}

	std::vector<Slot> slots;
	Collect(slots);
	// forget managed userids that are gone
	for (auto it = s_managed.begin(); it != s_managed.end();)
	{
		bool present = false;
		for (auto &s : slots)
			if (s.userid == it->first && s.bot)
				present = true;
		it = present ? std::next(it) : s_managed.erase(it);
	}

	int humans = 0, others = 0, managed = 0;
	for (auto &s : slots)
	{
		if (s.managed)
			++managed;
		else if (s.team == 1 && !hnb_count_spectators.GetBool())
			continue;           // spectators do not take a bot's place
		else
		{
			++others;
			if (!s.bot)
				++humans;
		}
	}

	int desired = hnb_quota.GetInt();
	if (!V_stricmp(hnb_quota_mode.GetString(), "fill"))
		desired -= others;
	if (hnb_join_after_player.GetBool() && humans == 0)
		desired = 0;
	int connected = (int)slots.size() - managed;
	desired = MIN(desired, gpGlobals->maxClients - connected - 1);   // always leave a slot for a human
	desired = MAX(desired, 0);

	if (managed < desired && gpGlobals->curtime >= s_addBlockedUntil)
		AddBot(slots);
	else if (managed > desired)
		KickBot(slots);
}
} // namespace pop

static void OnNamesFileChanged(IConVar *, const char *, float)
{
	pop::LoadNames();
}

CON_COMMAND(hl2mpnb_status, "List the bots hl2mp-nextbot manages.")
{
	META_CONPRINTF("[%s] %s, quota %d (%s), names: %d from %s\n", HNB_TAG,
		hnb_enable.GetBool() ? "enabled" : "disabled", hnb_quota.GetInt(), hnb_quota_mode.GetString(),
		(int)pop::s_names.size(), pop::s_namesSource.c_str());
	for (auto &m : pop::s_managed)
		META_CONPRINTF("  userid %d  %s\n", m.first, m.second.c_str());
}

CON_COMMAND(hl2mpnb_reload_names, "Re-read hl2mpnb_names_file.")
{
	pop::LoadNames();
	META_CONPRINTF("[%s] %d names from %s\n", HNB_TAG, (int)pop::s_names.size(), pop::s_namesSource.c_str());
}

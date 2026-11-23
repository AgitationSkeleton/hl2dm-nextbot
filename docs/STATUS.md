# hl2dm-nextbot status

## Design decision

The plan was a NextBot port in the style of prefortress2-nextbot (vendored Valve NextBot + a proxy
game layer). Investigation showed HL2DM no longer needs one: the current Steam HL2DM `server.dll`
(2025 SDK multiplayer code, build 10889068) already contains Valve's NextBot, the nav mesh with
`nav_generate`, and a complete HL2MP bot (`CHL2MPBot`, `src/game/server/hl2mp/bot/` in
ValveSoftware/source-sdk-2013) derived from TFBot. Vendoring a second NextBot next to it would
duplicate what the game ships and fight it for the same players. So this addon drives and fixes
the native bot instead.

## Engine facts (HL2DM build 10889068, x86 server.dll 2026-05-25)

- The dedicated server is 32-bit: `srcds.exe` in the install root (PE machine 0x14c) loads
  `bin/engine.dll` and `hl2mp/bin/server.dll`. `bin/x64` / `hl2mp/bin/x64` exist but there is no
  64-bit srcds in the install. Metamod 1.11 (plugin API 16) and SourceMod load as x86.
- Interface headers: alliedmodders hl2sdk branch `hl2dm` matches Valve's 2025 `eiface.h`
  (VEngineServer023, ServerGameDLL012). VSERVERTOOLS003 is exported.
- Source for the shipped bot code: ValveSoftware/source-sdk-2013 master; verified against the
  binary by RTTI (`.?AVCHL2MPBot*@@`) and disassembly.
- `CNavMesh` vtable: slot 30 `AddWalkableSeeds`, slot 31 `GetGenerationTraceMask`
  (`mov eax,0x2400B; ret`). `m_walkableSeeds` at +0x540 (size +0x54C), `m_seedIdx` +0x554,
  `m_spawnName` +0x53C. HL2MP uses the plain `CNavMesh` (no game subclass).
- `StandardFilterRules`: non-brush entities (props) are only hit by traces whose mask contains
  `CONTENTS_MONSTER`; the stock generation mask (brush-only) never sees props.
- `IsEntityWalkable` (nav.h) treats every `func_brush` with Solidity "Toggle" as walkable during
  generation, even when enabled.
- Stock quota (`CHL2MPBotManager::MaintainBotQuota`) only counts players on teams 2/3; deathmatch
  players are team 0, so in deathmatch it never counts bots and humans never replace them.
- Stock bugs fixed in memory by the addon: model table index `RandomInt(0, ARRAYSIZE)`;
  uninitialised `name` buffer in `hl2mp_bot_add <name>`; `VPhysicsGetObject()->GetMass()` without a
  NULL check in gravity-gun aiming (crashed the test server); `GetAmmo` scanning `"*_health*"`.
- `GetRandomBotName()` is a one-entry list `{ "Bot Name", NULL }` in `.data`; the addon points the
  entry at its own buffer so bots are created with their final name.
- Bots keep the server out of hibernation, like any connected player.
- Msg output of rcon-issued commands comes back in the rcon reply.

## Native bot behavior (what Valve's code already does)

Deathmatch and teamplay enemies; SeekAndDestroy paths to weapons the bot does not own, then roams
spawn points; weapon choice per threat range (`EquipBestWeaponForThreat`); aim with lead for RPG,
crossbow, gravity-gun throws and grenade arcs; gravity-gun prop pickup/throw (prop freaks/haters);
health kits and health chargers (with +use) when hurt; retreat to cover; stuck detection and
`hl2mp_bot_debug_stuck_log`.

## Test results (LAN test server, 2026-11-22/23)

Test server: a dedicated HL2DM install on the LAN, `sv_lan 1`, Metamod only (no SourceMod),
4 bots (`hl2mpnb_quota 4`, `hl2mp_bot_difficulty 1`). The addon then ran on a public server
(Metamod + SourceMod) with no crashes so far.

- Nav: 44/44 maps generated with `hl2mpnb_nav_generate` (`tools/navbatch.py`; dm_blood_gulch_v3
  and dm_finaldrive with `--viewdist 2500`). `tools/navcheck.py` over 43 maps (blood_gulch added
  later, 16/16 spawns, 23/23 items): spawns covered 924/929 (99.5%), pickups covered 2281/2408
  (94.7%), pickups reachable from spawns over walk/ladder links 86.8% (teleport/jump-pad-only areas
  count as unreachable). dm_lockdown stock vs corrected: pickups covered 65 -> 74 of 77, the
  through-the-wall cell-block links are gone.
- Soak 1 (deathmatch, 8 maps x 5 min = 40 min, under cdb): no crash, 231 kills, 0 team kills.
  Kills by weapon: smg1 81, shotgun 43, ar2 31, pistol 15, 357 15, crowbar 14, physics (gravity gun)
  11, stunstick 8, crossbow 7, rpg 6, so bots pick up and use map weapons.
- Soak 2 (4 maps deathmatch + 2 teamplay, 30 min): no crash. NextBot behavior starts
  (`nb_debug BEHAVIOR`, deathmatch maps): GetAmmo 92 (the stock search scans "*_health*" and cannot
  select ammo; not measured before the fix), GetHealth 61, GetProp 397, Attack 160. `unstick` lifted a bot out of crossfire's
  embedded spawn.
- Soak 3 (teamplay after the team-model fix, 10 min): teams 2v2, kills on both maps, 0 team kills.
- Crash found and fixed: gravity-gun aiming NULL physics object (Valve bug, see fixes.cpp).

## Known gaps

- Armor: no behavior seeks batteries or suit chargers (Valve's bot has none; bots pick batteries
  up when they walk over them).
- Stuck events remain where small rooms are full of movable physics props (dm_overwatch control
  room ~5760 5888 0: chairs and file cabinets) and on dm_lockdown/dm_steamlab; NextBot's own
  stuck handling recovers them but they waste time there.
- Teamplay: kill rates are low on big team maps with 4 bots (2v2); HL2MP's auto-balance swaps
  bots between teams occasionally (teams stay even).
- Fixes are located by byte patterns in this server.dll build; after a game update each one either
  re-applies or logs "skipped" (bots still work, with the stock bug).
- `hl2mpnb_nav_generate` needs `sv_cheats 1`: test server only. New maps need a mesh made there.
- The soaks ran without SourceMod. The addon does not touch SourceMod and runs next to it on a public
  server, but SourceMod plugins that manage bots themselves were not tested with it.

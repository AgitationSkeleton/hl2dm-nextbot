# hl2dm-nextbot

Server bots for **Half-Life 2: Deathmatch** built on Valve's **NextBot**, as a Metamod:Source addon.

Since the 2025 Source SDK update, HL2DM's own `server.dll` contains Valve's HL2MP NextBot
(`CHL2MPBot`, `hl2mp_bot_*`). It is a port of TFBot's design: path following on the nav mesh,
weapon choice by range, aim with projectile lead, gravity-gun prop throwing, retreat to cover, and
seeking health and weapons. Out of the box it is not usable on a deathmatch server, though: its
quota cannot count deathmatch players, bots get placeholder names, a few stock bugs crash the
server or break ammo seeking, and the stock nav generator makes poor meshes for DM maps.

This addon makes that native bot the server's bot. Nothing in the game is replaced; fixes are
small, verified in-memory corrections to the shipped code, and each one switches itself off (with a
log line) if the code it expects is not found after a game update.

| Area | What the addon does |
|---|---|
| Population | Fill-style quota: `hl2mpnb_quota 4` + `fill` keeps 4 players in the game; humans replace bots; deathmatch and teamplay. Valve's `hl2mp_bot_quota` is held at 0 (its counting ignores `TEAM_UNASSIGNED`, so in deathmatch it never counts its bots and keeps adding them). |
| Names | Built-in list: the stock TF2 bot names. An optional names file (`addons/hl2mpnextbot/bot_names.txt`, or `hl2mpnb_names_file`) overrides them. Bots are created with their name, with no "changed name" message. |
| Stock bug fixes | `hl2mp_bot_add`: `cl_playermodel` read one past the model table, and `name` passed an uninitialised buffer when a name was given. Gravity-gun aim (`SelectTargetPoint`): `VPhysicsGetObject()->GetMass()` with no NULL check crashed the server. `CHL2MPBotGetAmmo` searched `"*_health*"` (copied from GetHealth), so bots never went for ammo. |
| Nav meshes | `hl2mpnb_nav_generate` runs the game's own generator with corrections: seeds at every spawn point and pickup (stock seeds only the first `info_player_start`, so many DM maps either fail or lose regions), a fix for the stock "seed already sampled ends all seeding" bug, the player-clip trace mask, fixed props and enabled toggle brushes treated as solid, and movable props/pickups ignored. Meshes are standard `maps/<map>.nav`. |

## Download

Each [release](../../releases) has `hl2dm-nextbot-<version>-windows.zip` (drop-in for the `hl2mp/`
game folder) and `hl2dm-nextbot-navmeshes-stock.zip` (meshes for the eight stock HL2DM maps:
dm_lockdown, dm_overwatch, dm_powerhouse, dm_resistance, dm_runoff, dm_steamlab, dm_underpass,
halls3). Bots stand still on a map without a mesh; see [docs/INSTALL.md](docs/INSTALL.md) for
making one for a custom map.

Needs Metamod:Source 1.11 or 1.12 on a Windows HL2DM dedicated server (the current Steam build).

## Build

x86 only (HL2DM's dedicated server runs the 32-bit `srcds.exe` and `hl2mp/bin/server.dll`). It
needs VS2022 Build Tools (x86), alliedmodders `hl2sdk` (branch `hl2dm`) and `metamod-source` headers
(plugin API 16).

    python -I tools/build.py          # output build/hl2mpnextbot.dll

Paths: `HL2SDK`, `MMSOURCE` (and optionally `VCVARS`) or `tools/local.json`.

## Install

    hl2mp/addons/hl2mpnextbot/bin/hl2mpnextbot.dll
    hl2mp/addons/metamod/hl2mpnextbot.vdf
    hl2mp/maps/<map>.nav                         (nav meshes)
    hl2mp/addons/hl2mpnextbot/bot_names.txt      (optional, your own names)

`server.cfg`:

    hl2mpnb_quota 4
    hl2mpnb_quota_mode fill
    hl2mpnb_join_after_player 0
    hl2mp_bot_difficulty 1

Details, checks and removal: [docs/INSTALL.md](docs/INSTALL.md). What was tested and the known
gaps: [docs/STATUS.md](docs/STATUS.md).

## Console

| Name | Default | Meaning |
|---|---|---|
| `hl2mpnb_enable` | 1 | manage the bot population |
| `hl2mpnb_quota` | 4 | bot quota |
| `hl2mpnb_quota_mode` | fill | `fill`: N players including humans; `normal`: always N bots |
| `hl2mpnb_join_after_player` | 0 | no bots until a human is in the game |
| `hl2mpnb_count_spectators` | 0 | spectating humans count as players for `fill` |
| `hl2mpnb_difficulty` | -1 | -1 = `hl2mp_bot_difficulty`; 0-3; 4 = random per bot |
| `hl2mpnb_names_file` | `addons/hl2mpnextbot/bot_names.txt` | optional names file |
| `hl2mpnb_status`, `hl2mpnb_reload_names` | | status / re-read names |
| `hl2mpnb_nav_generate` | | corrected nav generation (sv_cheats 1, test server only; reloads the map) |
| `hl2mpnb_nav_mask` | 0x201400B | trace mask used by `hl2mpnb_nav_generate` |
| `hl2mpnb_trace x1 y1 z1 x2 y2 z2 [mask]` | | debug: player-hull trace, prints what it hits |

Valve's own bot cvars (`hl2mp_bot_difficulty`, `hl2mp_bot_*`, `nb_debug`, ...) work as usual.

## Tools

| Tool | Use |
|---|---|
| `tools/build.py` | build |
| `tools/testsrv.py` | LAN test server: start / kill / deploy / rcon / log |
| `tools/cdbrun.py` | run the test server under cdb; symbolized first-fault stack in `build/cdb.log` |
| `tools/navbatch.py` | generate nav meshes for every map on the test server |
| `tools/navcheck.py` | offline nav coverage: spawns and pickups covered/reachable |
| `tools/soak.py` | multi-map soak with kills, stuck events and behavior counts |
| `tools/rtti.py` | MSVC RTTI vtable dump and disassembly of `server.dll` (mingw objdump) |

## License

The code in `src/` and `tools/` is this project's own. `src/default_names.inc` is the TF2 bot
name list from Valve's Source SDK 2013 (`tf_bot.cpp`), under the Source 1 SDK License.

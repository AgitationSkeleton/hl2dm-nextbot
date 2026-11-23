# Installing hl2dm-nextbot

HL2DM's dedicated server is the 32-bit `srcds.exe` in the root of the install. It loads
`hl2mp/bin/server.dll` (x86) and Metamod's x86 `addons/metamod/bin/server.dll`. hl2dm-nextbot is
an x86 Metamod:Source plugin; install Metamod:Source 1.11 or 1.12 first.

## Files

| Path (under `hl2mp/`) | |
|---|---|
| `addons/hl2mpnextbot/bin/hl2mpnextbot.dll` | the addon (`.pdb` optional, for crash stacks) |
| `addons/metamod/hl2mpnextbot.vdf` | tells Metamod to load it |
| `addons/hl2mpnextbot/bot_names.example.txt` | copy to `bot_names.txt` for your own bot names (optional) |
| `maps/<map>.nav` | one nav mesh per map |

No game file is replaced. If another bot plugin is installed, disable it first (two bot
managers would fight over the player slots).

## server.cfg

    // hl2dm-nextbot: fill to 4 players, humans replace bots
    hl2mpnb_quota 4
    hl2mpnb_quota_mode fill
    hl2mpnb_join_after_player 0
    hl2mp_bot_difficulty 1

`hl2mp_bot_difficulty`: 0 easy, 1 normal, 2 hard, 3 expert (`hl2mpnb_difficulty 4` = random per
bot). Leave Valve's `hl2mp_bot_quota` at 0; the addon manages the population.

## Check

After a restart, over rcon or the server console:

- `meta list` shows "HL2MP NextBot".
- `hl2mpnb_status` shows the quota, mode, names source and the bots.
- The console log has `ammo-search fix applied` and `physcannon held-prop NULL fix applied`. After a
  game update a fix may log "skipped" instead: bots still work, with that stock bug.

## Nav meshes

Bots need `maps/<map>.nav`; without one they stand still. The release has meshes for the stock
maps. For any other map, generate one on a separate test server (never on a public server: it needs
`sv_cheats 1` and reloads the map):

    sv_cheats 1
    map <map>
    hl2mpnb_nav_generate

The map reloads when it is done; copy `maps/<map>.nav` to the server. `tools/navbatch.py` does this
for every map of a test server, and `tools/navcheck.py` reports how many spawns and pickups each
mesh covers. Large open maps can take very long in the visibility pass; use a shorter view distance
(`nav_max_view_distance 2500` before generating, or `navbatch.py --viewdist 2500`).

## Removal

Delete or rename `addons/metamod/hl2mpnextbot.vdf` and restart. The `.nav` files can stay.

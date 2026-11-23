#!/usr/bin/env python3
"""Multi-map bot soak on the LAN test copy, with per-map evidence from console.log.

  python -I tools/soak.py <secs-per-map> map [map ...] [--teamplay] [--quota N]

The test copy must be running (ideally under tools/cdbrun.py so a crash leaves a stack in
build/cdb.log). Per map it reports: kills by weapon, team kills (teamplay), suicides, stuck
events, and NextBot behavior starts (GetHealth / GetAmmo / GetProp / RetreatToCover) from
nb_debug BEHAVIOR (needs sv_cheats 1). Results: build/soak.json.
"""
import collections, json, os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import testsrv  # noqa: E402

LOG = os.path.join(testsrv.GAME, "console.log")
KILL = re.compile(r'"([^"<]*)<(\d+)><[^>]*><([^>]*)>" killed "([^"<]*)<(\d+)><[^>]*><([^>]*)>" with "([^"]*)"')
SUICIDE = re.compile(r'committed suicide with "([^"]*)"')
STUCK = re.compile(r'" stuck \(position "([-\d.]+) ([-\d.]+) ([-\d.]+)"')
BEHAV = re.compile(r" STARTING (\w+)")


def rcon(cmd):
    for _ in range(30):
        try:
            return testsrv.rcon(cmd, timeout=10)
        except OSError:
            time.sleep(3)
    return None


def size():
    return os.path.getsize(LOG) if os.path.exists(LOG) else 0


def analyse(text):
    r = {"kills": collections.Counter(), "teamkills": 0, "suicides": collections.Counter(), "stuck": 0,
         "behaviors": collections.Counter(), "bot_kills": collections.Counter(), "stuck_spots": collections.Counter()}
    for line in text.splitlines():
        m = KILL.search(line)
        if m:
            r["kills"][m.group(7)] += 1
            r["bot_kills"][m.group(1)] += 1
            if m.group(3) and m.group(3) == m.group(6) and m.group(3) not in ("Unassigned", ""):
                r["teamkills"] += 1
            continue
        m = SUICIDE.search(line)
        if m:
            r["suicides"][m.group(1)] += 1
        m = STUCK.search(line)
        if m:
            r["stuck"] += 1
            r["stuck_spots"]["%d %d %d" % tuple(64 * round(float(v) / 64) for v in m.groups())] += 1
        for b in BEHAV.findall(line):
            r["behaviors"][b] += 1
    r["stuck_spots"] = collections.Counter(dict(r["stuck_spots"].most_common(8)))
    return {k: (dict(v) if isinstance(v, collections.Counter) else v) for k, v in r.items()}


def main():
    a = sys.argv[1:]
    teamplay = "--teamplay" in a
    a = [x for x in a if x != "--teamplay"]
    quota = 4
    if "--quota" in a:
        i = a.index("--quota"); quota = int(a[i + 1]); del a[i:i + 2]
    secs, maps = int(a[0]), a[1:]
    testsrv.guard()
    rcon("mp_teamplay %d" % (1 if teamplay else 0))
    rcon("hl2mpnb_quota %d" % quota)
    rcon("developer 1")          # NextBot debug output is only printed with developer on
    rcon("nb_debug BEHAVIOR")
    out = os.path.join(testsrv.ROOT, "build", "soak.json")
    results = json.load(open(out)) if os.path.exists(out) else {}
    for m in maps:
        rcon("changelevel " + m)
        time.sleep(15)
        pos = size()
        t0 = time.time()
        died = False
        while time.time() - t0 < secs:
            time.sleep(10)
            if not testsrv.test_pids():
                died = True
                break
        text = open(LOG, "rb").read()[pos:].decode("utf-8", "replace")
        r = analyse(text)
        r["secs"] = round(time.time() - t0)
        r["crashed"] = died
        r["teamplay"] = teamplay
        key = m + (" (teamplay)" if teamplay else "")
        results[key] = r
        json.dump(results, open(out, "w"), indent=1)
        print("%-30s %4ds kills %3d tk %d stuck %3d beh %s%s" % (key, r["secs"], sum(r["kills"].values()),
              r["teamkills"], r["stuck"], r["behaviors"], "  CRASHED" if died else ""), flush=True)
        if died:
            break
    rcon("nb_debug")
    rcon("developer 0")


if __name__ == "__main__":
    main()

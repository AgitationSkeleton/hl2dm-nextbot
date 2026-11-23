#!/usr/bin/env python3
"""Generate nav meshes for maps on the LAN test copy with hl2mpnb_nav_generate.

  python -I tools/navbatch.py [--all | map ...] [--timeout SECS] [--viewdist UNITS]

--viewdist sets nav_max_view_distance (default 6000) for the run: the visibility pass is roughly
quadratic, and on huge open maps (dm_blood_gulch_v3) it takes hours at 6000.

Needs the test copy running with +sv_cheats 1 and the plugin loaded. For each map: changelevel,
wait for the map, run hl2mpnb_nav_generate, wait for <map>.nav to be (re)written. Bots are
switched off (hl2mpnb_quota 0) for the run and restored afterwards. Results: build/navbatch.json.
"""
import json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import testsrv  # noqa: E402

MAPS = os.path.join(testsrv.GAME, "maps")


def rcon(cmd, tries=40):
    for _ in range(tries):
        try:
            return testsrv.rcon(cmd, timeout=10)
        except OSError:
            time.sleep(3)
    raise SystemExit("rcon unreachable (server down?)")


def current_map():
    out = rcon("status")
    for line in out.splitlines():
        if line.startswith("map"):
            return line.split(":", 1)[1].split()[0]
    return ""


def gen(m, timeout):
    nav = os.path.join(MAPS, m + ".nav")
    before = os.path.getmtime(nav) if os.path.exists(nav) else 0
    rcon("changelevel " + m)
    t0 = time.time()
    while time.time() - t0 < 120:
        time.sleep(3)
        try:
            if current_map() == m:
                break
        except SystemExit:
            raise
    else:
        return "map did not load"
    time.sleep(3)
    out = rcon("hl2mpnb_nav_generate")
    t0 = time.time()
    while time.time() - t0 < timeout:
        time.sleep(4)
        if os.path.exists(nav) and os.path.getmtime(nav) > before:
            time.sleep(2)
            return "ok"
        if not testsrv.test_pids():
            return "server died"
    return "timeout"


def main():
    args = sys.argv[1:]
    timeout = 900
    if "--timeout" in args:
        i = args.index("--timeout"); timeout = int(args[i + 1]); del args[i:i + 2]
    viewdist = None
    if "--viewdist" in args:
        i = args.index("--viewdist"); viewdist = args[i + 1]; del args[i:i + 2]
    if not args or args == ["--all"]:
        maps = sorted(f[:-4] for f in os.listdir(MAPS) if f.endswith(".bsp"))
    else:
        maps = args
    testsrv.guard()
    rcon("hl2mpnb_quota 0")
    rcon("nav_max_view_distance %s" % (viewdist or "6000"))
    res = {}
    out = os.path.join(testsrv.ROOT, "build", "navbatch.json")
    if os.path.exists(out):
        res = json.load(open(out))
    for m in maps:
        t = time.time()
        r = gen(m, timeout)
        res[m] = {"result": r, "secs": round(time.time() - t), "viewdist": int(viewdist or 6000)}
        json.dump(res, open(out, "w"), indent=1)
        print("%-26s %-16s %4ds" % (m, r, time.time() - t), flush=True)
        if r == "server died":
            print("server died on", m, "- restart it and rerun the remaining maps")
            break
    try:
        rcon("hl2mpnb_quota 4", tries=2)
    except SystemExit:
        pass


if __name__ == "__main__":
    main()
